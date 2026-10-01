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

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace a2e {

class Audio;
class DiskController;
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

  // Media.
  bool insertDisk(int drive, const uint8_t *data, size_t size,
                  const char *filename);
  void ejectDisk(int drive);
  bool insertBlockImage(int device, const uint8_t *data, size_t size,
                        const char *filename);
  void ejectBlockImage(int device);

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
