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
#include "../core/machine/machine_profile.hpp"
#include "iigs/iigs_machine.hpp"
#include "../core/disk-image/disk_converter.hpp"
#include "../core/input/joyport.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace a2e {

class Audio;
class DiskController;
class MockingboardCard;
class SmartPortCard;
class Video;

namespace host {

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
  const MachineProfile &profile() const { return machineProfile(machineId_); }
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

private:
  void destroy();

  std::unique_ptr<Emulator> emulator_;
  std::unique_ptr<iigs::IIgsMachine> iigs_;
  MachineId machineId_ = MachineId::AppleIIe;
  // A ROM 01 shipped with 256K on the board and a memory expansion card took
  // it further, which is what most of them had.
  size_t iigsFastRam_ = iigs::FAST_RAM_SIZE_ROM01;
  std::function<void(Emulator &)> emulatorBuilt_;
};

} // namespace host
} // namespace a2e
