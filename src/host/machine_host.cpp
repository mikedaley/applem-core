/*
 * machine_host.cpp - The running machine, whichever kind it is
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "machine_host.hpp"

#include "../core/audio/audio.hpp"
#include "../core/cards/disk_controller.hpp"
#include "../core/cards/smartport/smartport_card.hpp"
#include "../core/video/video.hpp"
#include "cpu/65816/cpu65816.hpp"

namespace a2e::host {

MachineHost::~MachineHost() { destroy(); }

void MachineHost::destroy() {
  emulator_.reset();
  iigs_.reset();
}

void MachineHost::build() {
  // A IIgs is a different machine built from different parts, so it is a
  // different object. Which one exists is decided here and nowhere else.
  if (profile().family == MachineFamily::AppleIIgs) {
    if (iigs_) return;
    emulator_.reset();
    size_t romSize = 0;
    size_t characterSize = 0;
    const uint8_t *rom = Emulator::systemROMFor(machineId_, romSize);
    const uint8_t *characters =
        Emulator::characterROMFor(machineId_, characterSize);
    iigs_ = std::make_unique<iigs::IIgsMachine>(iigsFastRam_);
    iigs_->init(rom, romSize, characters, characterSize);
    return;
  }

  iigs_.reset();
  if (emulator_) return;
  emulator_ = std::make_unique<Emulator>(machineId_);
  emulator_->init();
  if (emulatorBuilt_) emulatorBuilt_(*emulator_);
}

bool MachineHost::setMachine(MachineId id) {
  if (id == machineId_ && isBuilt()) return true;
  machineId_ = id;
  destroy();
  build();
  return isBuilt();
}

size_t MachineHost::iigsFastRam() const {
  return iigs_ ? iigs_->memory().fastRamSize() : iigsFastRam_;
}

bool MachineHost::setIIgsFastRam(size_t bytes) {
  if (bytes == 0) return false;
  const size_t requested = iigs::clampFastRamSize(bytes);
  if (requested == iigsFastRam_ && iigs_) return true;

  iigsFastRam_ = requested;
  if (!iigs_) return true; // Remembered for when a IIgs is built

  iigs_.reset();
  build();
  return iigs_ != nullptr;
}

// The 5.25" controller of whichever machine is running: the card in a //e's
// slot 6, the chip on a //c's board, or the one a IIgs has where a slot would
// be. The class that answers is the same class whichever machine it is.
DiskController *MachineHost::diskController() {
  if (emulator_) return emulator_->getDiskPtr();
  if (iigs_) return &iigs_->disk();
  return nullptr;
}

// A breakpoint is a breakpoint, and a machine with banks and one without both
// answer at 24 bits.
MachineDebug *MachineHost::debug() {
  if (emulator_) return &emulator_->debug();
  if (iigs_) return &iigs_->debug();
  return nullptr;
}

// What a breakpoint condition can ask about whichever machine is running.
// Without this a condition could only be asked of a //e, so a conditional
// breakpoint on a IIgs silently never fired.
MachineView MachineHost::view() {
  if (emulator_) return ConditionEvaluator::viewOf(*emulator_);
  if (iigs_) {
    iigs::IIgsMachine *machine = iigs_.get();
    MachineView view;
    view.peek = [machine](uint32_t address) {
      return machine->memory().peek(address & 0xFFFFFF);
    };
    view.pc = machine->cpu().getPCFull();
    view.a = machine->cpu().getA();
    view.x = machine->cpu().getX();
    view.y = machine->cpu().getY();
    view.sp = machine->cpu().getSP();
    view.p = machine->cpu().getP();
    return view;
  }
  return {};
}

// A IIgs has a speaker too ($C030 is a Mega II address), so a volume control
// means the same thing to it as to every other machine.
Audio *MachineHost::speaker() {
  if (emulator_) return &emulator_->getAudio();
  if (iigs_) return &iigs_->audio();
  return nullptr;
}

Video *MachineHost::video() {
  if (emulator_) return &emulator_->getVideo();
  if (iigs_) return &iigs_->video();
  return nullptr;
}

// On a //e the SmartPort is a card the user fits; on a IIgs it is part of the
// machine, in slot 5. Either way the class that holds the block devices is the
// same class.
SmartPortCard *MachineHost::smartPort() {
  if (emulator_) return emulator_->getSmartPortCard();
  if (iigs_) return &iigs_->smartPort();
  return nullptr;
}

bool MachineHost::hasSystemROM() const {
  if (emulator_) return emulator_->hasSystemROM();
  return iigs_ != nullptr && Emulator::isMachineRunnable(machineId_);
}

void MachineHost::reset() {
  if (iigs_) iigs_->reset();
  else if (emulator_) emulator_->reset();
}

void MachineHost::warmReset() {
  if (iigs_) iigs_->warmReset();
  else if (emulator_) emulator_->warmReset();
}

void MachineHost::runCycles(int cycles) {
  if (iigs_) iigs_->runCycles(cycles);
  else if (emulator_) emulator_->runCycles(cycles);
}

// This is what paces the emulation: the host asks for samples and the time
// they represent is the time the machine gets to run.
int MachineHost::generateStereoAudioSamples(float *buffer, int sampleCount) {
  if (iigs_) return iigs_->generateStereoAudioSamples(buffer, sampleCount);
  if (emulator_) return emulator_->generateStereoAudioSamples(buffer, sampleCount);
  return 0;
}

int MachineHost::consumeFrameSamples() {
  if (iigs_) return iigs_->consumeFrameSamples();
  if (emulator_) return emulator_->consumeFrameSamples();
  return 0;
}

void MachineHost::setPaused(bool paused) {
  if (iigs_) iigs_->setPaused(paused);
  else if (emulator_) emulator_->setPaused(paused);
}

bool MachineHost::isPaused() const {
  if (iigs_) return iigs_->isPaused();
  if (emulator_) return emulator_->isPaused();
  return false;
}

const uint8_t *MachineHost::framebuffer() {
  if (iigs_) return iigs_->framebuffer();
  if (emulator_) return emulator_->getFramebuffer();
  return nullptr;
}

size_t MachineHost::framebufferSize() const {
  if (iigs_) return iigs_->framebufferSize();
  if (emulator_) return emulator_->getFramebufferSize();
  return profile().display.framebufferSize();
}

bool MachineHost::takeFrameReady() {
  if (iigs_) {
    const bool ready = iigs_->isFrameReady();
    if (ready) iigs_->clearFrameReady();
    return ready;
  }
  if (!emulator_) return false;
  const bool ready = emulator_->isFrameReady();
  if (ready) emulator_->clearFrameReady();
  return ready;
}

void MachineHost::forceRenderFrame() {
  if (Video *v = video()) v->forceRenderFrame();
}

uint64_t MachineHost::totalCycles() const {
  if (iigs_) return iigs_->slowCycles();
  if (emulator_) return emulator_->getTotalCycles();
  return 0;
}

int MachineHost::handleRawKeyDown(int browserKeycode, bool shift, bool ctrl,
                                  bool alt, bool meta, bool capsLock,
                                  int keyLocation) {
  if (iigs_) {
    return iigs_->handleRawKeyDown(browserKeycode, shift, ctrl, alt, meta,
                                   capsLock, keyLocation);
  }
  if (!emulator_) return -1;
  return emulator_->handleRawKeyDown(browserKeycode, shift, ctrl, alt, meta,
                                     capsLock, keyLocation);
}

void MachineHost::handleRawKeyUp(int browserKeycode, bool shift, bool ctrl,
                                 bool alt, bool meta, int keyLocation) {
  if (iigs_) iigs_->handleRawKeyUp(browserKeycode, shift, ctrl, alt, meta, keyLocation);
  else if (emulator_) emulator_->handleRawKeyUp(browserKeycode, shift, ctrl, alt, meta, keyLocation);
}

void MachineHost::releaseModifiers() {
  if (emulator_) emulator_->releaseModifiers();
}

size_t MachineHost::pasteText(const char *utf8) {
  return emulator_ ? emulator_->pasteText(utf8) : 0;
}

bool MachineHost::pastePending() const {
  return emulator_ && emulator_->pastePending() > 0;
}

void MachineHost::setButton(int button, bool pressed) {
  if (iigs_) iigs_->setButton(button, pressed);
  else if (emulator_) emulator_->setButton(button, pressed);
}

void MachineHost::setPaddleValue(int paddle, int value) {
  if (iigs_) iigs_->setPaddleValue(paddle, value);
  else if (emulator_) emulator_->setPaddleValue(paddle, value);
}

bool MachineHost::insertDisk(int drive, const uint8_t *data, size_t size,
                             const char *filename) {
  if (iigs_) return iigs_->insertDisk(drive, data, size, filename ? filename : "");
  if (emulator_) return emulator_->insertDisk(drive, data, size, filename);
  return false;
}

void MachineHost::ejectDisk(int drive) {
  if (iigs_) iigs_->ejectDisk(drive);
  else if (emulator_) emulator_->ejectDisk(drive);
}

// A IIgs decides when its SmartPort's ROM may appear, so it goes through the
// machine rather than straight to the card.
bool MachineHost::insertBlockImage(int device, const uint8_t *data, size_t size,
                                   const char *filename) {
  if (iigs_) return iigs_->insertBlockImage(device, data, size, filename ? filename : "");
  if (emulator_) return emulator_->insertSmartPortImage(device, data, size, filename);
  return false;
}

void MachineHost::ejectBlockImage(int device) {
  if (iigs_) iigs_->ejectBlockImage(device);
  else if (emulator_) emulator_->ejectSmartPortImage(device);
}

const uint8_t *MachineHost::exportState(size_t *size) {
  if (emulator_) return emulator_->exportState(size);
  if (iigs_) return iigs_->exportState(size);
  *size = 0;
  return nullptr;
}

bool MachineHost::importState(const uint8_t *data, size_t size) {
  if (emulator_) return emulator_->importState(data, size);
  if (iigs_) return iigs_->importState(data, size);
  return false;
}

} // namespace a2e::host
