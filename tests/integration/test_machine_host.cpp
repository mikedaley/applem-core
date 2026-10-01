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
