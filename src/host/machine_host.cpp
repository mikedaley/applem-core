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
#include "iigs/iigs_clock.hpp"
#include "iigs/iigs_memory.hpp"

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

void MachineHost::setGamePortDevice(GamePortDevice device) {
  if (iigs_) iigs_->setGamePortDevice(device);
  else if (emulator_) emulator_->setGamePortDevice(device);
}

GamePortDevice MachineHost::gamePortDevice() const {
  if (iigs_) return iigs_->gamePortDevice();
  if (emulator_) return emulator_->gamePortDevice();
  return GamePortDevice::AppleJoystick;
}

void MachineHost::setJoyportStick(int stick, int switches) {
  if (iigs_) iigs_->setJoyportStick(stick, switches);
  else if (emulator_) emulator_->setJoyportStick(stick, switches);
}

bool MachineHost::hasMouse() {
  if (iigs_) return true; // the ADB's, always there
  return emulator_ && emulator_->isMouseInstalled();
}

void MachineHost::mouseMove(int dx, int dy) {
  if (iigs_) iigs_->mouseMove(dx, dy);
  else if (emulator_) emulator_->mouseMove(dx, dy);
}

void MachineHost::mouseButton(bool pressed) {
  if (iigs_) iigs_->mouseButton(pressed);
  else if (emulator_) emulator_->mouseButton(pressed);
}

void MachineHost::setSpeedMultiplier(int multiplier) {
  if (emulator_) emulator_->setSpeedMultiplier(multiplier);
}

int MachineHost::speedMultiplier() const {
  return emulator_ ? emulator_->getSpeedMultiplier() : 1;
}

bool MachineHost::insertDisk(int drive, const uint8_t *data, size_t size,
                             const char *filename) {
  if (iigs_) return iigs_->insertDisk(drive, data, size, filename ? filename : "");
  if (emulator_) return emulator_->insertDisk(drive, data, size, filename);
  return false;
}

// The controller makes the blank, and every machine's drives are a
// DiskController, so a IIgs gets one too; asking only the //e family's
// Emulator left the button doing nothing there.
bool MachineHost::insertBlankDisk(int drive) {
  DiskController *disk = diskController();
  return disk && disk->insertBlankDisk(drive);
}

void MachineHost::ejectDisk(int drive) {
  if (iigs_) iigs_->ejectDisk(drive);
  else if (emulator_) emulator_->ejectDisk(drive);
}

bool MachineHost::isDiskInserted(int drive) {
  DiskController *disk = diskController();
  return disk && disk->hasDisk(drive);
}

bool MachineHost::isDiskModified(int drive) {
  DiskController *disk = diskController();
  if (!disk || !disk->hasDisk(drive)) return false;
  const DiskImage *image = disk->getDiskImage(drive);
  return image && image->isModified();
}

const char *MachineHost::diskFilename(int drive) const {
  if (iigs_) return iigs_->getDiskFilename(drive);
  if (emulator_) return emulator_->getDiskFilename(drive);
  return nullptr;
}

const uint8_t *MachineHost::exportDiskAs(int drive, DiskSaveFormat format, size_t *size) {
  if (iigs_) return iigs_->exportDiskDataAs(drive, format, size);
  if (emulator_) return emulator_->exportDiskDataAs(drive, format, size);
  if (size) *size = 0;
  return nullptr;
}

bool MachineHost::canExportDiskAs(int drive, DiskSaveFormat format) {
  if (iigs_) return iigs_->canExportDiskAs(drive, format);
  return emulator_ && emulator_->canExportDiskAs(drive, format);
}

DiskSaveFormat MachineHost::diskNativeFormat(int drive) {
  if (iigs_) return iigs_->getDiskNativeFormat(drive);
  if (emulator_) return emulator_->getDiskNativeFormat(drive);
  return DiskSaveFormat::DOSOrder;
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

bool MachineHost::isBlockImageInserted(int device) {
  SmartPortCard *card = smartPort();
  return card && card->isImageInserted(device);
}

bool MachineHost::isBlockImageModified(int device) {
  SmartPortCard *card = smartPort();
  return card && card->isImageModified(device);
}

std::string MachineHost::blockImageFilename(int device) {
  SmartPortCard *card = smartPort();
  return card ? card->getImageFilename(device) : std::string();
}

const uint8_t *MachineHost::exportBlockImage(int device, size_t *size) {
  SmartPortCard *card = smartPort();
  if (!card) {
    if (size) *size = 0;
    return nullptr;
  }
  return card->exportImageData(device, size);
}

bool MachineHost::isSmartPortROMPending() {
  SmartPortCard *card = smartPort();
  return card && card->isROMPending();
}

std::string MachineHost::slotCard(int slot) const {
  if (emulator_) return emulator_->getSlotCardName(static_cast<uint8_t>(slot));
  if (iigs_) return iigs_->getSlotCardName(static_cast<uint8_t>(slot));
  return "invalid";
}

bool MachineHost::setSlotCard(int slot, const std::string &cardId) {
  if (emulator_) return emulator_->setSlotCard(static_cast<uint8_t>(slot), cardId.c_str());
  if (iigs_) return iigs_->setSlotCard(static_cast<uint8_t>(slot), cardId.empty() ? "empty" : cardId);
  return false;
}

bool MachineHost::isSlotInternal(int slot) const {
  return iigs_ ? iigs_->isSlotInternal(static_cast<uint8_t>(slot)) : true;
}

void MachineHost::setSlotInternal(int slot, bool internal) {
  if (iigs_) iigs_->setSlotInternal(static_cast<uint8_t>(slot), internal);
}

void MachineHost::setNoSlotClock(bool enabled) {
  if (emulator_) emulator_->enableNoSlotClock(enabled);
}

bool MachineHost::noSlotClock() const {
  return emulator_ && emulator_->isNoSlotClockEnabled();
}

std::vector<uint8_t> MachineHost::batteryRam() {
  if (!iigs_) return {};
  const uint8_t *bytes = iigs_->memory().clock().batteryRamBytes();
  return std::vector<uint8_t>(bytes, bytes + iigs::IIgsClock::batteryRamSize());
}

void MachineHost::setBatteryRam(const std::vector<uint8_t> &bytes) {
  if (iigs_ && !bytes.empty()) iigs_->memory().clock().loadBatteryRam(bytes.data(), bytes.size());
}

bool MachineHost::takeBatteryRamChanged() {
  return iigs_ && iigs_->memory().clock().takeBatteryRamChanged();
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
