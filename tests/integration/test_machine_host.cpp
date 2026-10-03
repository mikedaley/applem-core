/*
 * test_machine_host.cpp - The running machine, whichever kind it is
 *
 * MachineHost is what every front end runs a machine through: the browser's
 * bindings and the native app. So the proof is the one a front end would
 * look for: each machine is built through it, paced the way a host paces it
 * (by asking for audio), and reaches its own prompt on its own screen.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "../../src/host/machine_host.hpp"
#include "cpu65816.hpp"
#include "disassembler/disassembler65816.hpp"

#include <string>
#include <vector>

using namespace a2e;
using a2e::host::MachineHost;

namespace {

// Run the machine the way a host does: audio samples asked for one frame's
// worth at a time, 800 at 48kHz, and the time they represent is the time the
// machine runs. Returns how many finished frames it reported.
int runSeconds(MachineHost &host, double seconds) {
  std::vector<float> buffer(800 * 2);
  int frames = 0;
  const int refills = static_cast<int>(seconds * 60.0);
  for (int i = 0; i < refills; i++) {
    host.generateStereoAudioSamples(buffer.data(), 800);
    if (host.takeFrameReady()) frames++;
  }
  return frames;
}

std::string screen(MachineHost &host) {
  if (host.iigs()) return host.iigs()->screenText();
  return host.emulator()->readScreenText(0, 0, 23, 39);
}

bool anyLitPixel(MachineHost &host) {
  const uint8_t *fb = host.framebuffer();
  for (size_t i = 0; i < host.framebufferSize(); i += 4) {
    if (fb[i] | fb[i + 1] | fb[i + 2]) return true;
  }
  return false;
}

} // namespace

TEST_CASE("A host starts as a //e and builds nothing until asked",
          "[host]") {
  MachineHost host;
  REQUIRE(host.machineId() == MachineId::AppleIIe);
  REQUIRE_FALSE(host.isBuilt());
  REQUIRE(host.diskController() == nullptr);
  REQUIRE(host.debug() == nullptr);

  host.build();
  REQUIRE(host.emulator() != nullptr);
  REQUIRE(host.iigs() == nullptr);
  REQUIRE(host.framebufferSize() == host.profile().display.framebufferSize());
}

TEST_CASE("Every machine boots to its prompt through the host",
          "[host][boot]") {
  struct Case {
    MachineId id;
    const char *expected;
  };
  const Case cases[] = {
      {MachineId::AppleIIe, "Apple //e"},
      {MachineId::AppleIIPlus, "APPLE ]["},
      {MachineId::AppleIIc, "Apple //c"},
      // Past its power-on diagnostics; it then spends a while looking for
      // a startup device, which test_iigs_boot runs on to.
      {MachineId::AppleIIgs, "Apple IIgs"},
  };

  for (const Case &c : cases) {
    const MachineProfile &profile = machineProfile(c.id);
    DYNAMIC_SECTION(profile.name) {
      if (!Emulator::isMachineRunnable(c.id)) {
        WARN(profile.name << " ROM not built in; skipping");
        continue;
      }
      MachineHost host;
      REQUIRE(host.setMachine(c.id));
      REQUIRE(host.machineId() == c.id);
      REQUIRE((host.iigs() != nullptr) ==
              (profile.family == MachineFamily::AppleIIgs));
      REQUIRE(host.hasSystemROM());

      const int frames = runSeconds(host, 4.0);
      // About sixty a second; the point is that frames arrive at all and at
      // the rate the host paces, not the exact count.
      CHECK(frames > 200);
      REQUIRE(host.framebufferSize() == profile.display.framebufferSize());
      REQUIRE(anyLitPixel(host));

      const std::string text = screen(host);
      INFO("screen:\n" << text);
      REQUIRE(text.find(c.expected) != std::string::npos);
    }
  }
}

TEST_CASE("Selecting the running machine changes nothing", "[host]") {
  MachineHost host;
  host.build();
  Emulator *before = host.emulator();
  REQUIRE(host.setMachine(MachineId::AppleIIe));
  REQUIRE(host.emulator() == before);
}

TEST_CASE("Switching machines rebuilds, and the build callback hears of it",
          "[host]") {
  MachineHost host;
  int built = 0;
  host.setEmulatorBuiltCallback([&](Emulator &) { built++; });
  host.build();
  REQUIRE(built == 1);

  if (Emulator::isMachineRunnable(MachineId::AppleIIc)) {
    REQUIRE(host.setMachine(MachineId::AppleIIc));
    REQUIRE(built == 2);
    REQUIRE(host.emulator()->getMachine().id == MachineId::AppleIIc);
  }
  if (Emulator::isMachineRunnable(MachineId::AppleIIgs)) {
    REQUIRE(host.setMachine(MachineId::AppleIIgs));
    REQUIRE(host.emulator() == nullptr);
    REQUIRE(host.iigs() != nullptr);
    // A IIgs is not an Emulator, so the callback is not for it.
    REQUIRE(built == (Emulator::isMachineRunnable(MachineId::AppleIIc) ? 2 : 1));
  }
}

TEST_CASE("The IIgs memory size is remembered, and rebuilds only a IIgs",
          "[host][iigs]") {
  MachineHost host;
  host.build();
  Emulator *emulator = host.emulator();

  REQUIRE(host.setIIgsFastRam(1024 * 1024));
  REQUIRE(host.iigsFastRam() == 1024 * 1024);
  REQUIRE(host.emulator() == emulator); // a //e is untouched

  if (!Emulator::isMachineRunnable(MachineId::AppleIIgs)) return;
  REQUIRE(host.setMachine(MachineId::AppleIIgs));
  REQUIRE(host.iigs()->memory().fastRamSize() == 1024 * 1024);

  iigs::IIgsMachine *before = host.iigs();
  REQUIRE(host.setIIgsFastRam(4 * 1024 * 1024));
  REQUIRE(host.iigs() != nullptr);
  REQUIRE(host.iigs()->memory().fastRamSize() == 4 * 1024 * 1024);
  (void)before;
}

// Frames the machine finished in a second of audio, as a host counts them.
int framesInASecond(MachineHost &host) {
  std::vector<float> buffer(800 * 2);
  host.consumeFrameSamples(); // whatever went before
  int frames = 0;
  for (int i = 0; i < 60; i++) {
    host.generateStereoAudioSamples(buffer.data(), 800);
    frames += host.consumeFrameSamples();
  }
  return frames;
}

TEST_CASE("Switching to PAL retimes the running machine and keeps it",
          "[host][pal]") {
  MachineHost host;
  host.build();
  runSeconds(host, 0.5);
  Emulator *emulator = host.emulator();
  emulator->getMMU().write(0x0300, 0xA5);
  REQUIRE(framesInASecond(host) == Approx(60).margin(1));

  REQUIRE(host.setVideoStandard(VideoStandard::PAL));
  REQUIRE(host.emulator() == emulator); // the same machine, not a new one
  REQUIRE(emulator->videoStandard() == VideoStandard::PAL);
  REQUIRE(host.profile().timing.scanlinesPerFrame == 312);
  REQUIRE(emulator->getMMU().read(0x0300) == 0xA5);
  REQUIRE(framesInASecond(host) == Approx(50).margin(1));

  // A reset is not a change of standard.
  emulator->reset();
  REQUIRE(emulator->videoStandard() == VideoStandard::PAL);

  REQUIRE(host.setVideoStandard(VideoStandard::NTSC));
  REQUIRE(framesInASecond(host) == Approx(60).margin(1));
}

TEST_CASE("A IIgs stays NTSC, and the choice is kept for the next machine",
          "[host][pal]") {
  MachineHost host;
  REQUIRE(host.setVideoStandard(VideoStandard::PAL));
  host.build();
  REQUIRE(host.emulator()->videoStandard() == VideoStandard::PAL);

  if (!Emulator::isMachineRunnable(MachineId::AppleIIgs)) return;
  REQUIRE(host.setMachine(MachineId::AppleIIgs));
  REQUIRE(host.videoStandard() == VideoStandard::NTSC);
  REQUIRE(host.profile().timing.standard == VideoStandard::NTSC);
  REQUIRE_FALSE(host.setVideoStandard(VideoStandard::PAL));

  REQUIRE(host.setMachine(MachineId::AppleIIe));
  REQUIRE(host.videoStandard() == VideoStandard::PAL);
  REQUIRE(host.emulator()->getMachine().timing.scanlinesPerFrame == 312);
}

TEST_CASE("The parts every machine has are found on either kind",
          "[host]") {
  for (MachineId id : {MachineId::AppleIIe, MachineId::AppleIIgs}) {
    if (!Emulator::isMachineRunnable(id)) continue;
    MachineHost host;
    REQUIRE(host.setMachine(id));
    REQUIRE(host.diskController() != nullptr);
    REQUIRE(host.debug() != nullptr);
    REQUIRE(host.speaker() != nullptr);
    REQUIRE(host.video() != nullptr);

    runSeconds(host, 0.1);
    const MachineView view = host.view();
    REQUIRE(view.peek);
    // The view reads the same memory the machine runs from.
    const uint32_t pc = view.pc;
    if (host.iigs()) {
      REQUIRE(view.peek(pc) == host.iigs()->memory().peek(pc & 0xFFFFFF));
    } else {
      REQUIRE(view.peek(pc) == host.emulator()->peekMemory(pc & 0xFFFF));
    }
  }
}

TEST_CASE("A save state goes out and back through the host", "[host][state]") {
  MachineHost host;
  host.build();
  runSeconds(host, 0.5);

  size_t size = 0;
  const uint8_t *state = host.exportState(&size);
  REQUIRE(state != nullptr);
  REQUIRE(size > 0);
  const std::vector<uint8_t> copy(state, state + size);
  const uint64_t cycles = host.totalCycles();

  runSeconds(host, 0.5);
  REQUIRE(host.totalCycles() > cycles);
  REQUIRE(host.importState(copy.data(), copy.size()));
  REQUIRE(host.totalCycles() == cycles);
}

TEST_CASE("Floppies go in, come out as files, and come out again", "[host][disk]") {
  MachineHost host;
  host.build();
  REQUIRE_FALSE(host.isDiskInserted(0));
  REQUIRE(host.insertBlankDisk(0));
  REQUIRE(host.isDiskInserted(0));
  REQUIRE_FALSE(host.isDiskModified(0));

  // A blank disk is an unformatted WOZ: there are no sectors yet to write
  // out as a DSK, but it is a WOZ already.
  REQUIRE(host.diskNativeFormat(0) == DiskSaveFormat::WOZ);
  REQUIRE_FALSE(host.canExportDiskAs(0, DiskSaveFormat::DOSOrder));
  REQUIRE(host.canExportDiskAs(0, DiskSaveFormat::WOZ));
  size_t size = 0;
  const uint8_t *woz = host.exportDiskAs(0, DiskSaveFormat::WOZ, &size);
  REQUIRE(woz != nullptr);
  REQUIRE(size > 0);
  const std::vector<uint8_t> image(woz, woz + size);

  // The same bytes back in, in the other drive, by name.
  REQUIRE(host.insertDisk(1, image.data(), image.size(), "copy.woz"));
  REQUIRE(host.isDiskInserted(1));
  REQUIRE(std::string(host.diskFilename(1)) == "copy.woz");

  host.ejectDisk(0);
  REQUIRE_FALSE(host.isDiskInserted(0));
  REQUIRE(host.isDiskInserted(1));
}

TEST_CASE("A IIgs takes a blank disk too", "[host][disk][iigs]") {
  if (!Emulator::isMachineRunnable(MachineId::AppleIIgs)) return;
  MachineHost host;
  REQUIRE(host.setMachine(MachineId::AppleIIgs));
  REQUIRE(host.insertBlankDisk(0));
  REQUIRE(host.isDiskInserted(0));
}

TEST_CASE("Slots are refitted by card id", "[host][slots]") {
  MachineHost host;
  host.build();
  REQUIRE(host.slotCard(6) == "disk2");
  REQUIRE(host.setSlotCard(4, "mouse"));
  REQUIRE(host.slotCard(4) == "mouse");
  REQUIRE(host.hasMouse());
  REQUIRE(host.setSlotCard(4, "empty"));
  REQUIRE_FALSE(host.hasMouse());

  host.setNoSlotClock(true);
  REQUIRE(host.noSlotClock());
}

TEST_CASE("A IIgs's battery RAM goes out and back as it was", "[host][iigs]") {
  if (!Emulator::isMachineRunnable(MachineId::AppleIIgs)) return;
  MachineHost host;
  REQUIRE(host.batteryRam().empty()); // nothing built yet
  REQUIRE(host.setMachine(MachineId::AppleIIgs));
  runSeconds(host, 3.0);
  // The firmware wrote its settings while starting.
  REQUIRE(host.takeBatteryRamChanged());
  const std::vector<uint8_t> written = host.batteryRam();
  REQUIRE(written.size() == 256);

  MachineHost next;
  REQUIRE(next.setMachine(MachineId::AppleIIgs));
  next.setBatteryRam(written);
  REQUIRE(next.batteryRam() == written);
}

TEST_CASE("Only a //e takes a speed multiplier", "[host]") {
  MachineHost host;
  host.build();
  host.setSpeedMultiplier(4);
  REQUIRE(host.speedMultiplier() == 4);
  if (!Emulator::isMachineRunnable(MachineId::AppleIIgs)) return;
  REQUIRE(host.setMachine(MachineId::AppleIIgs));
  host.setSpeedMultiplier(4);
  REQUIRE(host.speedMultiplier() == 1);
}

TEST_CASE("A //e's SmartPort and clock come from the slot layout, not the constructor",
          "[host][smartport]") {
  // The Emulator fits only the drives, the Mockingboard and a //c's ports
  // itself; the profile's other defaults are fitted when the host applies
  // the slot layout, as the browser's Expansion Slots window does at
  // startup. A host that forgets has no SmartPort.
  MachineHost host;
  host.build();
  REQUIRE(host.slotCard(6) == "disk2");
  REQUIRE(host.slotCard(4) == "mockingboard");
  REQUIRE(host.smartPort() == nullptr);
  REQUIRE(host.setSlotCard(7, "smartport"));
  REQUIRE(host.smartPort() != nullptr);
}

TEST_CASE("A Mockingboard is found on either kind of machine, and only when fitted",
          "[host][mockingboard]") {
  // The front ends' Mockingboard windows are offered on this answer, so a
  // card that has been taken out must not still be found: a //e parks the
  // card it does not have rather than freeing it.
  MachineHost host;
  host.build();
  REQUIRE(host.mockingboard() != nullptr);
  REQUIRE(host.setSlotCard(4, "empty"));
  REQUIRE(host.mockingboard() == nullptr);
  REQUIRE(host.setSlotCard(4, "mockingboard"));
  REQUIRE(host.mockingboard() != nullptr);

  if (!Emulator::isMachineRunnable(MachineId::AppleIIgs)) return;
  REQUIRE(host.setMachine(MachineId::AppleIIgs));
  REQUIRE(host.mockingboard() == nullptr);
  REQUIRE(host.setSlotCard(4, "mockingboard"));
  REQUIRE(host.mockingboard() != nullptr);
}

namespace {

// A small program at $2000 on a //e that has run to its prompt, with the
// machine paused on its first instruction.
void loadProgram(MachineHost &host, const std::vector<uint8_t> &code) {
  host.build();
  runSeconds(host, 1.5);
  host.setPaused(true);
  for (size_t i = 0; i < code.size(); i++) {
    host.emulator()->writeMemory(static_cast<uint16_t>(0x2000 + i), code[i]);
  }
  host.setRegister(a2e::host::CpuRegister::PC, 0x2000);
}

} // namespace

TEST_CASE("The debugger steps a //e and reads its registers through the host",
          "[host][debugger]") {
  MachineHost host;
  // LDA #$42 / LDX #$03 / STA $0300,X / BNE +2 / NOP / NOP
  loadProgram(host, {0xA9, 0x42, 0xA2, 0x03, 0x9D, 0x00, 0x03, 0xD0, 0x02, 0xEA, 0xEA});
  REQUIRE(host.cpuState().pc == 0x2000);

  host.stepInstruction();
  host.stepInstruction();
  a2e::host::CpuState s = host.cpuState();
  REQUIRE(s.a == 0x42);
  REQUIRE(s.x == 0x03);
  REQUIRE(s.pc == 0x2004);
  REQUIRE(s.emulation());
  REQUIRE(s.accumulator8());

  // The store is about to write $0303, and the host can say so first.
  const a2e::host::Instruction store = host.disassemble(s.pc);
  REQUIRE(store.mnemonic == "STA");
  REQUIRE(store.operand == "$0300,X");
  REQUIRE(store.mode == a2e::host::OperandMode::AbsoluteX);
  const auto ea = host.effectiveAddress(store, s);
  REQUIRE(ea);
  REQUIRE(ea->address == 0x0303);
  host.stepInstruction();
  REQUIRE(host.peek(0x0303) == 0x42);

  // A not-equal with Z clear goes, and the listing knows where.
  s = host.cpuState();
  const a2e::host::Instruction branch = host.disassemble(s.pc);
  REQUIRE(branch.mnemonic == "BNE");
  REQUIRE(branch.target == 0x200B);
  REQUIRE(host.branchTaken(branch, s) == std::optional<bool>(true));
  REQUIRE_FALSE(host.branchTaken(store, s).has_value());

  // Registers are set through the host as a debugger edits them.
  host.setRegister(a2e::host::CpuRegister::A, 0x1234);
  REQUIRE(host.cpuState().a == 0x34); // a //e keeps the low byte
}

TEST_CASE("The host resolves a pointer through the zero page",
          "[host][debugger]") {
  MachineHost host;
  // LDY #$05 / LDA ($06),Y, with $06/$07 pointing at $4000.
  loadProgram(host, {0xA0, 0x05, 0xB1, 0x06});
  host.emulator()->writeMemory(0x06, 0x00);
  host.emulator()->writeMemory(0x07, 0x40);
  host.emulator()->writeMemory(0x4005, 0x99);
  host.stepInstruction();
  const a2e::host::CpuState s = host.cpuState();
  const auto ea = host.effectiveAddress(host.disassemble(s.pc), s);
  REQUIRE(ea);
  REQUIRE(ea->address == 0x4005);
  REQUIRE(ea->value == 0x99);
}

TEST_CASE("A listing always shows its centre as an instruction",
          "[host][debugger]") {
  MachineHost host;
  loadProgram(host, {0xA9, 0x42, 0xA2, 0x03, 0x9D, 0x00, 0x03, 0xD0, 0x02, 0xEA, 0xEA});
  const auto lines = host.disassembleRange(0x2004, 2, 6);
  REQUIRE(lines.size() == 6);
  bool found = false;
  for (const auto &line : lines) found = found || line.address == 0x2004;
  REQUIRE(found);
  REQUIRE(lines[0].address == 0x2000);
}

TEST_CASE("The trace ring comes back through the host, oldest first",
          "[host][debugger]") {
  MachineHost host;
  loadProgram(host, {0xA9, 0x42, 0xA2, 0x03, 0xEA});
  host.debug()->clearTrace();
  host.debug()->setTraceEnabled(true);
  host.stepInstruction();
  host.stepInstruction();
  const auto lines = host.traceLines(0, 10);
  REQUIRE(lines.size() == 2);
  REQUIRE(lines[0].instruction.address == 0x2000);
  REQUIRE(lines[0].instruction.mnemonic == "LDA");
  REQUIRE(lines[1].instruction.mnemonic == "LDX");
}

TEST_CASE("A IIgs's instructions read as its own monitor writes them",
          "[host][debugger][iigs]") {
  // The host splits the mnemonic from the operand for a front end to colour;
  // put back together they must be what formatDisasm816 writes.
  if (!Emulator::isMachineRunnable(MachineId::AppleIIgs)) return;
  MachineHost host;
  REQUIRE(host.setMachine(MachineId::AppleIIgs));
  const uint32_t at = 0x002000;
  const a2e::CPU65816 &cpu = host.iigs()->cpu();
  for (int op = 0; op < 256; op++) {
    host.iigs()->memory().write(at, static_cast<uint8_t>(op));
    host.iigs()->memory().write(at + 1, 0x34);
    host.iigs()->memory().write(at + 2, 0x12);
    host.iigs()->memory().write(at + 3, 0x01);
    const uint8_t bytes[4] = {static_cast<uint8_t>(op), 0x34, 0x12, 0x01};
    const std::string whole = a2e::formatDisasm816(
        a2e::disassemble816(bytes, 4, at, cpu.accumulator8(), cpu.index8()));
    const a2e::host::Instruction in = host.disassemble(at);
    const std::string text = in.operand.empty() ? in.mnemonic : in.mnemonic + " " + in.operand;
    INFO("opcode " << op);
    REQUIRE(whole.substr(whole.size() - text.size()) == text);
  }
}

TEST_CASE("An instruction's cost at the PC is what a //e then charges for it",
          "[host][debugger][cycles]") {
  // Every opcode, with indexes that do and do not carry into the next page,
  // and flags that take and refuse every branch: the debugger's column is
  // the core's own arithmetic, so the two must never disagree.
  MachineHost host;
  loadProgram(host, {0xEA});
  const uint8_t indexes[] = {0x00, 0xF0};
  const uint8_t flags[] = {0x00, 0xC3, 0x08};
  for (int op = 0; op < 256; op++) {
    if (op == 0xCB || op == 0xDB) continue; // WAI and STP would stop the processor
    for (uint8_t index : indexes) {
      for (uint8_t p : flags) {
        host.emulator()->writeMemory(0x2000, static_cast<uint8_t>(op));
        host.emulator()->writeMemory(0x2001, 0x34);
        host.emulator()->writeMemory(0x2002, 0x12);
        // A zero page pointer for ($34),Y, near the end of a page.
        host.emulator()->writeMemory(0x34, 0x80);
        host.emulator()->writeMemory(0x35, 0x40);
        host.setRegister(a2e::host::CpuRegister::PC, 0x2000);
        host.setRegister(a2e::host::CpuRegister::X, index);
        host.setRegister(a2e::host::CpuRegister::Y, index);
        host.setRegister(a2e::host::CpuRegister::SP, 0xF0);
        host.setRegister(a2e::host::CpuRegister::P, p | 0x20);
        const a2e::host::CpuState s = host.cpuState();
        const a2e::host::Instruction in = host.disassemble(s.pc);
        const a2e::host::CycleCost cost = host.cycleCost(in, s, true);
        const a2e::host::CycleCost range = host.cycleCost(in, s, false);
        const uint64_t before = host.emulator()->getTotalCycles();
        host.stepInstruction();
        const uint64_t used = host.emulator()->getTotalCycles() - before;
        INFO("opcode $" << std::hex << op << " " << in.mnemonic << " " << in.operand << " index $"
                        << int(index) << " P $" << int(p));
        REQUIRE(cost.min == cost.max);
        REQUIRE(cost.min == used);
        REQUIRE(range.min <= used);
        REQUIRE(range.max >= used);
      }
    }
  }
}

TEST_CASE("An instruction's cost at the PC is what a IIgs then charges for it",
          "[host][debugger][cycles][iigs]") {
  if (!Emulator::isMachineRunnable(MachineId::AppleIIgs)) return;
  MachineHost host;
  REQUIRE(host.setMachine(MachineId::AppleIIgs));
  host.build();
  host.setPaused(true);
  a2e::CPU65816 &cpu = host.iigs()->cpu();
  auto &memory = host.iigs()->memory();

  struct Mode {
    bool native;
    uint8_t widths; // the M and X bits of P
    uint16_t d;
  };
  const Mode modes[] = {{false, 0x30, 0x0000}, {true, 0x30, 0x0000}, {true, 0x00, 0x0000},
                        {true, 0x20, 0x0012}, {true, 0x10, 0x0000}};
  const uint16_t indexes[] = {0x0000, 0x00F0};
  const uint8_t flags[] = {0x00, 0xC3};
  for (const Mode &mode : modes) {
    for (int op = 0; op < 256; op++) {
      // STP and WAI stop the clock; XCE, REP, SEP, PLP and RTI change the
      // very widths the cost was worked out at, which is still right, but
      // they would leave the next pass in a mode it did not ask for.
      if (op == 0xDB || op == 0xCB) continue;
      for (uint16_t index : indexes) {
        for (uint8_t p : flags) {
          // Put the processor in the mode, then the registers.
          cpu.setEmulation(!mode.native);
          cpu.setP(static_cast<uint8_t>((p & ~0x30) | (mode.native ? mode.widths : 0x30)));
          host.setRegister(a2e::host::CpuRegister::D, mode.d);
          host.setRegister(a2e::host::CpuRegister::DBR, 0);
          host.setRegister(a2e::host::CpuRegister::X, index);
          host.setRegister(a2e::host::CpuRegister::Y, index);
          host.setRegister(a2e::host::CpuRegister::SP, 0x01F0);
          host.setRegister(a2e::host::CpuRegister::A, 0x0000); // MVN/MVP move one byte
          const uint32_t at = 0x002000;
          memory.write(at, static_cast<uint8_t>(op));
          memory.write(at + 1, 0x34);
          memory.write(at + 2, 0x12);
          memory.write(at + 3, 0x00);
          memory.write(mode.d + 0x34, 0x80);
          memory.write(mode.d + 0x35, 0x40);
          memory.write(mode.d + 0x36, 0x00);
          host.setRegister(a2e::host::CpuRegister::PC, at);
          const a2e::host::CpuState s = host.cpuState();
          const a2e::host::Instruction in = host.disassemble(s.pc);
          const a2e::host::CycleCost cost = host.cycleCost(in, s, true);
          const a2e::host::CycleCost range = host.cycleCost(in, s, false);
          const uint64_t before = cpu.getTotalCycles();
          host.stepInstruction();
          const uint64_t used = cpu.getTotalCycles() - before;
          INFO("opcode $" << std::hex << op << " " << in.mnemonic << " " << in.operand << " native "
                          << mode.native << " widths $" << int(mode.widths) << " D $" << mode.d
                          << " index $" << index << " P $" << int(p));
          REQUIRE(cost.min == cost.max);
          REQUIRE(cost.min == used);
          REQUIRE(range.min <= used);
          REQUIRE(range.max >= used);
        }
      }
    }
  }
}

TEST_CASE("Heat counts a //e's cycles where they were spent, and only while asked",
          "[host][debugger][profile]") {
  MachineHost host;
  // LDA #$42 / NOP, with a line nobody runs after it.
  loadProgram(host, {0xA9, 0x42, 0xEA, 0xEA});
  REQUIRE(host.hasCycleProfile());
  host.stepInstruction();
  REQUIRE(host.profileCycles(0x2000) == 0); // not counting yet

  host.setRegister(a2e::host::CpuRegister::PC, 0x2000);
  host.setProfiling(true);
  host.clearProfile();
  host.stepInstruction();
  host.stepInstruction();
  REQUIRE(host.profileCycles(0x2000) == 2);
  REQUIRE(host.profileCycles(0x2002) == 2);
  REQUIRE(host.wasExecuted(0x2000));
  REQUIRE_FALSE(host.wasExecuted(0x2003));
  uint32_t max = 0;
  uint64_t total = 0;
  host.profileTotals(max, total);
  REQUIRE(max == 2);
  REQUIRE(total == 4);

  host.clearProfile();
  REQUIRE_FALSE(host.wasExecuted(0x2000));
}

TEST_CASE("A IIgs records what has run, by its full address",
          "[host][debugger][profile][iigs]") {
  if (!Emulator::isMachineRunnable(MachineId::AppleIIgs)) return;
  MachineHost host;
  REQUIRE(host.setMachine(MachineId::AppleIIgs));
  host.build();
  host.setPaused(true);
  REQUIRE_FALSE(host.hasCycleProfile());
  REQUIRE(host.hasCoverage());
  auto &memory = host.iigs()->memory();
  memory.write(0x012000, 0xEA);
  memory.write(0x012001, 0xEA);
  host.setRegister(a2e::host::CpuRegister::PC, 0x012000);
  host.setProfiling(true);
  host.stepInstruction();
  REQUIRE(host.wasExecuted(0x012000));
  REQUIRE_FALSE(host.wasExecuted(0x002000)); // the same offset in another bank
  REQUIRE_FALSE(host.wasExecuted(0x012001));
  REQUIRE(host.profileCycles(0x012000) == 0); // no heat on this machine
  host.setProfiling(false);
  REQUIRE_FALSE(host.wasExecuted(0x012000));
}

namespace {

const a2e::host::MemorySpace *spaceOf(const std::vector<a2e::host::MemorySpace> &spaces,
                                      a2e::host::MemorySpace::Kind kind) {
  for (const auto &space : spaces) {
    if (space.kind == kind) return &space;
  }
  return nullptr;
}

} // namespace

TEST_CASE("A memory view reaches every bank of a //e, whatever the switches say",
          "[host][debugger][memory]") {
  using Kind = a2e::host::MemorySpace::Kind;
  MachineHost host;
  host.build();
  host.setPaused(true);
  const auto spaces = host.memorySpaces();
  const auto *cpu = spaceOf(spaces, Kind::Processor);
  const auto *main = spaceOf(spaces, Kind::MainRAM);
  const auto *aux = spaceOf(spaces, Kind::AuxRAM);
  const auto *rom = spaceOf(spaces, Kind::ROM);
  REQUIRE(cpu);
  REQUIRE(main);
  REQUIRE(aux);
  REQUIRE(rom);
  REQUIRE(cpu->activity);
  REQUIRE_FALSE(rom->writable);

  MMU &mmu = host.emulator()->getMMU();

  SECTION("main and auxiliary RAM are each their own bank") {
    REQUIRE(host.pokeSpace(*main, 0x1234, 0x11));
    REQUIRE(host.pokeSpace(*aux, 0x1234, 0x22));
    REQUIRE(mmu.readRAM(0x1234, false) == 0x11);
    REQUIRE(mmu.readRAM(0x1234, true) == 0x22);
    REQUIRE(host.peekSpace(*main, 0x1234) == 0x11);
    REQUIRE(host.peekSpace(*aux, 0x1234) == 0x22);
  }

  SECTION("the processor's view writes where it reads, switches and all") {
    host.emulator()->writeMemory(0xC003, 0); // RAMRD on: reads come from aux
    REQUIRE(host.pokeSpace(*cpu, 0x1234, 0x5A));
    REQUIRE(mmu.readRAM(0x1234, true) == 0x5A);
    REQUIRE(host.peekSpace(*cpu, 0x1234) == 0x5A);
    REQUIRE_FALSE(host.pokeSpace(*cpu, 0xC000, 0x00)); // I/O is not edited
  }

  SECTION("the language card's two banks are laid out as a IIgs lays them out") {
    // Bank 1 at $C000, bank 2 at $D000, the rest of the card above.
    REQUIRE(host.pokeSpace(*main, 0xC100, 0x01));
    REQUIRE(host.pokeSpace(*main, 0xD100, 0x02));
    REQUIRE(host.pokeSpace(*aux, 0xE100, 0x03));
    REQUIRE(mmu.peekLanguageCardBank(0xD100, false, false) == 0x01);
    REQUIRE(mmu.peekLanguageCardBank(0xD100, false, true) == 0x02);
    REQUIRE(mmu.peekLanguageCardBank(0xE100, true, false) == 0x03);
    REQUIRE(host.peekSpace(*main, 0xC100) == 0x01);
    REQUIRE(host.peekSpace(*main, 0xD100) == 0x02);
    REQUIRE(host.peekSpace(*aux, 0xE100) == 0x03);
  }

  SECTION("the ROM reads and refuses a write") {
    REQUIRE(host.peekSpace(*rom, 0xFFFC) == mmu.getSystemROM()[0x3FFC]);
    REQUIRE_FALSE(host.pokeSpace(*rom, 0xFFFC, 0x00));
  }

  SECTION("the processor's reads and writes are counted while asked") {
    loadProgram(host, {0xAD, 0x00, 0x30, 0x8D, 0x01, 0x30}); // LDA $3000 / STA $3001
    host.setMemoryActivity(true);
    host.stepInstruction();
    host.stepInstruction();
    uint8_t reads[2], writes[2];
    host.memoryActivity(0x3000, 2, reads, writes);
    REQUIRE(reads[0] >= 1);
    REQUIRE(writes[0] == 0);
    REQUIRE(writes[1] >= 1);
    host.decayMemoryActivity(255);
    host.memoryActivity(0x3000, 2, reads, writes);
    REQUIRE(reads[0] == 0);
    REQUIRE(writes[1] == 0);
    host.setMemoryActivity(false);
  }
}

TEST_CASE("A II Plus has no auxiliary bank to show", "[host][debugger][memory]") {
  if (!Emulator::isMachineRunnable(MachineId::AppleIIPlus)) return;
  MachineHost host;
  REQUIRE(host.setMachine(MachineId::AppleIIPlus));
  host.build();
  REQUIRE(spaceOf(host.memorySpaces(), a2e::host::MemorySpace::Kind::AuxRAM) == nullptr);
}

TEST_CASE("A IIgs's memory view is a space per bank, and an edit shadows",
          "[host][debugger][memory][iigs]") {
  if (!Emulator::isMachineRunnable(MachineId::AppleIIgs)) return;
  MachineHost host;
  REQUIRE(host.setMachine(MachineId::AppleIIgs));
  host.build();
  host.setPaused(true);
  const auto spaces = host.memorySpaces();
  const size_t fastBanks = host.iigs()->memory().fastRamSize() / 0x10000;
  REQUIRE(spaces.size() >= fastBanks + 3);
  REQUIRE(spaces.front().base == 0x000000);
  const a2e::host::MemorySpace *bank1 = nullptr, *e1 = nullptr, *rom = nullptr;
  for (const auto &space : spaces) {
    if (space.base == 0x010000) bank1 = &space;
    if (space.base == 0xE10000) e1 = &space;
    if (space.base == 0xFF0000) rom = &space;
  }
  REQUIRE(bank1);
  REQUIRE(e1);
  REQUIRE(rom);
  REQUIRE_FALSE(rom->writable);
  REQUIRE_FALSE(host.hasMemoryActivity());

  const uint64_t clock = host.iigs()->memory().slowCycles();
  REQUIRE(host.pokeSpace(*bank1, 0x013000, 0x77));
  REQUIRE(host.peekSpace(*bank1, 0x013000) == 0x77);
  REQUIRE(host.iigs()->memory().fastRamByte(0x013000) == 0x77);
  // Shadowed into $E1 as the bus would, and charged nothing for it.
  if (host.iigs()->memory().peek(0xE13000) == 0x77) {
    REQUIRE(host.peekSpace(*e1, 0xE13000) == 0x77);
  }
  REQUIRE(host.iigs()->memory().slowCycles() == clock);
  REQUIRE_FALSE(host.pokeSpace(*rom, 0xFFFFFC, 0x00));
  REQUIRE_FALSE(host.pokeSpace(spaces.front(), 0x00C000, 0x00)); // I/O
}
