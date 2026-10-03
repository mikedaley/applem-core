/*
 * machine_host.hpp - The running machine, whichever kind it is
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "../core/emulator.hpp"
#include "../core/debug/condition_evaluator.hpp"
#include "../core/debug/machine_debug.hpp"
#include "../core/disassembler/disassembler.hpp"
#include "../core/machine/machine_profile.hpp"
#include "iigs/iigs_machine.hpp"
#include "../core/disk-image/disk_converter.hpp"
#include "../core/input/joyport.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace a2e {

class Audio;
class DiskController;
class MockingboardCard;
class SmartPortCard;
class Video;

namespace host {

// ---------------------------------------------------------------------------
// What a debugger asks of a processor, at the widest shape
//
// A 6502's answers are a 65816's with the high halves zero and no banks, so
// one shape describes both and a front end reads the machine profile to know
// how much of it to show. These live here rather than in either front end
// because both need exactly the same answers.
// ---------------------------------------------------------------------------

// The processor as it stands, and where the beam is.
struct CpuState {
  uint32_t pc = 0; // 24 bits, the bank included
  uint16_t a = 0, x = 0, y = 0, sp = 0;
  uint16_t d = 0;        // the direct page; zero on a 6502
  uint8_t p = 0;
  uint8_t pbr = 0, dbr = 0; // zero on a 6502
  uint8_t widths = 0;       // MachineDebug::WIDTH_ flags
  bool irqPending = false, nmiPending = false, nmiEdge = false;
  uint64_t cycles = 0;      // the machine's own clock, as totalCycles()
  int frameCycle = 0;       // cycles into the current frame
  BeamPosition beam{};

  bool emulation() const { return widths & MachineDebug::WIDTH_EMULATION; }
  bool accumulator8() const { return widths & MachineDebug::WIDTH_A8; }
  bool index8() const { return widths & MachineDebug::WIDTH_INDEX8; }
};

enum class CpuRegister { A, X, Y, SP, PC, P, PBR, DBR, D };

// How an instruction finds its operand, named for both processors. A 6502's
// zero page is a 65816's direct page at zero, so it is one mode here.
enum class OperandMode : uint8_t {
  Implied, Accumulator, Immediate,
  Direct, DirectX, DirectY,
  DirectIndirect, DirectIndexedIndirect, DirectIndirectIndexed,
  DirectIndirectLong, DirectIndirectLongIndexed,
  Absolute, AbsoluteX, AbsoluteY, AbsoluteLong, AbsoluteLongX,
  AbsoluteIndirect, AbsoluteIndexedIndirect, AbsoluteIndirectLong,
  StackRelative, StackRelativeIndirectIndexed,
  Relative, RelativeLong, DirectRelative, BlockMove,
};

// One instruction, decoded as the machine's own disassembler decodes it.
struct Instruction {
  uint32_t address = 0;
  uint32_t target = 0; // where a branch or jump goes; else the address
  uint8_t length = 1;
  uint8_t bytes[4] = {0, 0, 0, 0};
  std::string mnemonic;
  std::string operand; // as a monitor writes it: "#$12", "($06),Y"
  OperandMode mode = OperandMode::Implied;
  InstrCategory category = InstrCategory::UNKNOWN;
  FlowType flow = FlowType::SEQUENTIAL;
};

// What an instruction is about to touch, worked out from the registers as
// they stand: the address its operand resolves to, and the byte there.
struct EffectiveAddress {
  uint32_t address = 0;
  uint8_t value = 0;
};

// One instruction from the trace ring.
struct TraceLine {
  uint32_t cycle = 0;
  Instruction instruction;
  uint16_t a = 0, x = 0, y = 0, sp = 0;
  uint8_t p = 0;
  uint8_t widths = 0;
};

// What an instruction costs in cycles. Exact where the processor's state
// decides it (the widths, the direct page, the decimal flag, as they stand),
// a range where only running it would tell: an index that may or may not
// cross a page, a branch that may or may not be taken. A block move costs
// this much for every byte it moves.
struct CycleCost {
  uint8_t min = 0;
  uint8_t max = 0;
  bool perByte = false;
};

// Owns the one machine a host runs, and routes what every host asks of it.
//
// The Apple II family is coordinated by `Emulator` and a IIgs by
// `IIgsMachine`, and exactly one of the two is alive at a time. Which one it
// is, and how a question about a drive, the speaker, the debugger or the
// picture reaches it, used to be decided inside the WebAssembly bindings, so a
// second front end would have had to decide it again. It is decided here, in
// C++ with no host in it, and both the browser bindings and the native app sit
// on top.
//
// A run of the machine's memory, as a memory viewer browses it. The
// processor's view is what a program sees now, switches and all; the others
// are a //e's banks themselves, whatever the switches say. A //e's main and
// auxiliary RAM are each laid out as a IIgs lays out a bank: $0000-$BFFF as
// it is, the language card's bank 1 at $C000, and bank 2 and the rest of the
// card at $D000-$FFFF, so all 64K of each half is in one place.
// A IIgs has a space per bank, each its processor's view of that bank.
struct MemorySpace {
  enum class Kind { Processor, MainRAM, AuxRAM, ROM };
  Kind kind = Kind::Processor;
  std::string name;
  uint32_t base = 0; // the first address, bank included
  uint32_t size = 0x10000;
  bool writable = true;
  // Its addresses are the processor's: the PC, the stack and breakpoints
  // mean something in it.
  bool processor = true;
  // Reads and writes are counted while activity is on (setMemoryActivity).
  bool activity = false;
};

// Nothing here is thread safe. A host that runs the machine on its own thread
// serialises access around it.
class MachineHost {
public:
  MachineHost() = default;
  ~MachineHost();

  MachineHost(const MachineHost &) = delete;
  MachineHost &operator=(const MachineHost &) = delete;

  // Build the selected machine if nothing is built yet. A machine that is
  // already built is left exactly as it is.
  void build();

  // Switch machines. There is no way to convert a running machine into a
  // different one, so this destroys the machine and builds the new one:
  // inserted media and memory do not survive. Selecting the machine that is
  // already running changes nothing.
  bool setMachine(MachineId id);

  // NTSC or PAL. The standard is the host's choice, made for each machine,
  // so it is held here and given to every machine built; switching it times
  // the running machine afresh without rebuilding it (Emulator::
  // setVideoStandard). A machine not made in that standard runs NTSC, and
  // false is returned.
  VideoStandard videoStandard() const;
  bool setVideoStandard(VideoStandard standard);

  // How much fast RAM a IIgs has. Changing it rebuilds a running IIgs; any
  // other machine remembers it for when a IIgs is built.
  size_t iigsFastRam() const;
  bool setIIgsFastRam(size_t bytes);

  // Called whenever an `Emulator` is built, so a host can install the
  // callbacks that reach it before any card is fitted.
  void setEmulatorBuiltCallback(std::function<void(Emulator &)> callback) {
    emulatorBuilt_ = std::move(callback);
  }

  MachineId machineId() const { return machineId_; }
  const MachineProfile &profile() const { return machineProfile(machineId_, videoStandard()); }
  bool isBuilt() const { return emulator_ || iigs_; }

  // Exactly one of these is non-null once the machine is built.
  Emulator *emulator() { return emulator_.get(); }
  iigs::IIgsMachine *iigs() { return iigs_.get(); }

  // The parts every machine has, from whichever machine it is.
  DiskController *diskController();
  MachineDebug *debug();
  MachineView view();
  Audio *speaker();
  Video *video();
  SmartPortCard *smartPort();
  // A Mockingboard, from either kind of machine, or nullptr when none is
  // fitted (a //e parks the card it does not have rather than freeing it).
  MockingboardCard *mockingboard();

  // Whether the running machine's system ROM is in this build.
  bool hasSystemROM() const;

  // Running and showing.
  void reset();
  void warmReset();
  void runCycles(int cycles);
  int generateStereoAudioSamples(float *buffer, int sampleCount);
  // How many whole frames' worth of samples have been generated since the
  // last ask, 800 at 48kHz each. A host publishes a picture when it is one.
  int consumeFrameSamples();
  void setPaused(bool paused);
  bool isPaused() const;
  const uint8_t *framebuffer();
  size_t framebufferSize() const;
  // True once per finished frame: answering clears it.
  bool takeFrameReady();
  void forceRenderFrame();
  // The machine's own clock: a //e's CPU cycles, a IIgs's Mega II cycles.
  uint64_t totalCycles() const;

  // Input.
  int handleRawKeyDown(int browserKeycode, bool shift, bool ctrl, bool alt,
                       bool meta, bool capsLock, int keyLocation);
  void handleRawKeyUp(int browserKeycode, bool shift, bool ctrl, bool alt,
                      bool meta, int keyLocation);
  void releaseModifiers();
  size_t pasteText(const char *utf8);
  // The text screen as text, a line to a row with trailing spaces trimmed:
  // forty or eighty columns as the machine shows them, empty in graphics.
  std::string screenText();
  bool pastePending() const;
  void setButton(int button, bool pressed);
  void setPaddleValue(int paddle, int value);

  void setGamePortDevice(GamePortDevice device);
  GamePortDevice gamePortDevice() const;
  void setJoyportStick(int stick, int switches);
  // The mouse, wherever the machine keeps it: a card on a //e, the IOU on a
  // //c, the ADB on a IIgs. hasMouse() is whether there is one to drive.
  bool hasMouse();
  void mouseMove(int dx, int dy);
  void mouseButton(bool pressed);

  // The CPU clock as a multiple of the machine's own. A IIgs keeps its own
  // speed register, so this answers 1 there and ignores a change.
  void setSpeedMultiplier(int multiplier);
  int speedMultiplier() const;

  // Floppies. A sector image, a nibble image or a WOZ; the format comes
  // from the name.
  bool insertDisk(int drive, const uint8_t *data, size_t size,
                  const char *filename);
  // An unformatted WOZ, ready for the machine to format.
  bool insertBlankDisk(int drive);
  void ejectDisk(int drive);
  bool isDiskInserted(int drive);
  bool isDiskModified(int drive);
  const char *diskFilename(int drive) const;
  // The disk as a file in the given format, or nullptr if it cannot be
  // written that way (a copy-protected nibble track as sectors, say).
  const uint8_t *exportDiskAs(int drive, DiskSaveFormat format, size_t *size);
  bool canExportDiskAs(int drive, DiskSaveFormat format);
  DiskSaveFormat diskNativeFormat(int drive);

  // Block devices on the SmartPort. On a IIgs an image inserted while the
  // machine runs takes over slot 5 only at the next reset.
  bool insertBlockImage(int device, const uint8_t *data, size_t size,
                        const char *filename);
  void ejectBlockImage(int device);
  bool isBlockImageInserted(int device);
  bool isBlockImageModified(int device);
  std::string blockImageFilename(int device);
  const uint8_t *exportBlockImage(int device, size_t *size);
  bool isSmartPortROMPending();

  // Expansion slots, by the card ids the profile and the browser use.
  std::string slotCard(int slot) const;
  bool setSlotCard(int slot, const std::string &cardId);
  // A IIgs's slots each have a built-in device as well as a socket, and
  // $C02D says which answers. Every other machine's slots are sockets only,
  // so they answer true and ignore a change.
  bool isSlotInternal(int slot) const;
  void setSlotInternal(int slot, bool internal);
  void setNoSlotClock(bool enabled);
  bool noSlotClock() const;

  // A IIgs's 256 bytes of battery-backed settings, which the host keeps as
  // a real battery would. Empty on any other machine.
  std::vector<uint8_t> batteryRam();
  void setBatteryRam(const std::vector<uint8_t> &bytes);
  // Whether anything has written to it since this was last asked.
  bool takeBatteryRamChanged();

  // Save states.
  const uint8_t *exportState(size_t *size);
  bool importState(const uint8_t *data, size_t size);

  // ===== The debugger (machine_host_debug.cpp) =====

  CpuState cpuState();
  // A //e's registers take the low byte; PBR, DBR and D are ignored by a
  // machine that has none. The PC takes its bank with it on a IIgs.
  void setRegister(CpuRegister reg, uint32_t value);

  // One instruction, then stop. Over and out run until the temporary
  // breakpoint they set, which they return (bank included), or step once
  // and return 0 when there is nothing to step over or out of.
  void stepInstruction();
  uint32_t stepOver();
  uint32_t stepOut();

  // A byte, as a debugger reads one: no soft switch is touched and no card
  // is told. A //e ignores the bank.
  uint8_t peek(uint32_t address);

  // The machine's memory, as a memory viewer browses it (MemorySpace). A read
  // or a write outside the space's addresses wraps into it. A write answers
  // whether it landed: I/O and ROM refuse.
  std::vector<MemorySpace> memorySpaces();
  uint8_t peekSpace(const MemorySpace &space, uint32_t address);
  void readSpace(const MemorySpace &space, uint32_t address, uint8_t *out, size_t count);
  bool pokeSpace(const MemorySpace &space, uint32_t address, uint8_t value);

  // Every read and write the processor makes, counted per address up to 255
  // and decayed by the caller, for a memory view to light. A //e's only:
  // a IIgs's memory does not go through anything that counts.
  bool hasMemoryActivity() const { return emulator_ != nullptr; }
  void setMemoryActivity(bool on);
  void memoryActivity(uint32_t address, size_t count, uint8_t *reads, uint8_t *writes);
  void decayMemoryActivity(uint8_t amount);

  // One instruction at an address, at the processor's current widths.
  Instruction disassemble(uint32_t address);
  // `count` instructions, with up to `before` of them leading up to
  // `centre`, which is always listed as an instruction however the bytes
  // above it decode (disasm_align.hpp). A bank is a wall: the listing stays
  // in the bank the centre is in.
  std::vector<Instruction> disassembleRange(uint32_t centre, int before, int count);

  // Where the instruction at the PC will read or write, and whether a branch
  // there will be taken. Empty for one that touches no memory, or whose
  // address cannot be known before it runs.
  std::optional<EffectiveAddress> effectiveAddress(const Instruction &instruction,
                                                   const CpuState &state);
  std::optional<bool> branchTaken(const Instruction &instruction, const CpuState &state);

  // An instruction's cost, by the core's own base tables and the extra
  // cycles the core charges. With `atPC`, it is the instruction about to run
  // and the registers and memory decide the page crossings and the branch, so
  // the answer is a single number; anywhere else they are left as a range.
  CycleCost cycleCost(const Instruction &instruction, const CpuState &state, bool atPC);

  // The trace ring, oldest first: entries [start, start + count).
  std::vector<TraceLine> traceLines(size_t start, size_t count);

  // Where the time goes. A //e counts the cycles spent at every address; a
  // IIgs records only whether an address has run (the counts would be 64MB),
  // so it has coverage and no heat. Counting costs the run loop a little, so
  // it is on only while a debugger asks for it.
  bool hasCycleProfile() const { return emulator_ != nullptr; }
  bool hasCoverage() const { return emulator_ != nullptr || iigs_ != nullptr; }
  void setProfiling(bool on);
  void clearProfile();
  uint32_t profileCycles(uint32_t address);
  bool wasExecuted(uint32_t address);
  // The hottest address's count, and the total, for scaling a heat map.
  void profileTotals(uint32_t &max, uint64_t &total);

  // A breakpoint condition, against the machine as it stands. The error is
  // the last expression's, or empty.
  bool evaluateCondition(const std::string &expression);
  // The same language for a value, as a watch shows one: PEEK($24), A, X+1.
  int32_t evaluateExpression(const std::string &expression);
  std::string conditionError() const;

private:
  void destroy();

  std::unique_ptr<Emulator> emulator_;
  std::unique_ptr<iigs::IIgsMachine> iigs_;
  MachineId machineId_ = MachineId::AppleIIe;
  VideoStandard videoStandard_ = VideoStandard::NTSC;
  // A ROM 01 shipped with 256K on the board and a memory expansion card took
  // it further, which is what most of them had.
  size_t iigsFastRam_ = iigs::FAST_RAM_SIZE_ROM01;
  std::function<void(Emulator &)> emulatorBuilt_;
};

} // namespace host
} // namespace a2e
