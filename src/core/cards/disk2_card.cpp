#include "disk2_card.hpp"
#include "../disk-image/dsk_disk_image.hpp"
#include "../disk-image/woz_disk_image.hpp"
#include <algorithm>
#include <cstring>

namespace a2e {

Disk2Card::Disk2Card() {
    rom_.fill(0xFF);
    reset();
}

Disk2Card::Disk2Card(const uint8_t* rom, size_t romSize) {
    rom_.fill(0xFF);
    if (rom && romSize > 0) {
        loadROM(rom, romSize);
    }
    reset();
}

void Disk2Card::loadROM(const uint8_t* rom, size_t size) {
    if (rom && size > 0) {
        std::memcpy(rom_.data(), rom, std::min(size, rom_.size()));
    }
}

// ===== ExpansionCard Interface =====

uint8_t Disk2Card::readIO(uint8_t offset) {
    return handleSoftSwitch(offset & 0x0F, false);
}

void Disk2Card::writeIO(uint8_t offset, uint8_t value) {
    uint8_t off = offset & 0x0F;

    // In write mode, any write loads data into the shift register
    if (q7_) {
        writeLatch_ = value;
    }

    handleSoftSwitch(off, true);
}

uint8_t Disk2Card::peekIO(uint8_t offset) const {
    uint8_t off = offset & 0x0F;

    switch (off) {
    case PHASE0_OFF:
    case PHASE0_ON:
    case PHASE1_OFF:
    case PHASE1_ON:
    case PHASE2_OFF:
    case PHASE2_ON:
    case PHASE3_OFF:
    case PHASE3_ON:
        return (phaseStates_ >> (off / 2)) & 1 ? 0x80 : 0x00;

    case MOTOR_OFF:
    case MOTOR_ON:
        return isMotorOn() ? 0x80 : 0x00;

    case DRIVE1_SELECT:
    case DRIVE2_SELECT:
        return selectedDrive_ == 1 ? 0x80 : 0x00;

    case Q6L:
        return dataLatch_;

    case Q6H:
        if (hasDisk(selectedDrive_)) {
            const DiskImage* disk = diskImages_[selectedDrive_].get();
            return disk->isWriteProtected() ? 0x80 : 0x00;
        }
        return 0x00;

    case Q7L:
        return q7_ ? 0x00 : 0x80;

    case Q7H:
        return q7_ ? 0x80 : 0x00;

    default:
        return 0x00;
    }
}

uint8_t Disk2Card::readROM(uint8_t offset) {
    return rom_[offset];
}

void Disk2Card::reset() {
    motorOn_ = false;
    motorOffCycle_ = 0;
    selectedDrive_ = 0;
    q6_ = false;
    q7_ = false;
    phaseStates_ = 0;

    totalCycles_ = 0;
    lastReadCycle_[0] = 0;
    lastReadCycle_[1] = 0;
    dataLatch_ = 0;
    latchValid_ = false;
    writeLatch_ = 0;

    // Reset disk image track positions (but preserve loaded disks)
    for (int i = 0; i < 2; i++) {
        if (diskImages_[i]) {
            diskImages_[i]->setQuarterTrack(0);
        }
    }
}

void Disk2Card::update(int cycles) {
    totalCycles_ += cycles;
}

void Disk2Card::setCycleCallback(CycleCallback callback) {
    cycleCallback_ = std::move(callback);
}

size_t Disk2Card::getStateSize() const {
    return 32;
}

size_t Disk2Card::serialize(uint8_t* buffer, size_t maxSize) const {
    if (!buffer || maxSize < 16) return 0;

    size_t offset = 0;

    buffer[offset++] = isMotorOn() ? 1 : 0;
    buffer[offset++] = static_cast<uint8_t>(selectedDrive_);
    buffer[offset++] = q6_ ? 1 : 0;
    buffer[offset++] = q7_ ? 1 : 0;
    buffer[offset++] = phaseStates_;
    buffer[offset++] = dataLatch_;

    // Track positions for both drives
    int track0 = 0, track1 = 0;
    if (hasDisk(0)) {
        const DiskImage* img = getDiskImage(0);
        if (img) track0 = img->getQuarterTrack();
    }
    if (hasDisk(1)) {
        const DiskImage* img = getDiskImage(1);
        if (img) track1 = img->getQuarterTrack();
    }
    buffer[offset++] = static_cast<uint8_t>(track0);
    buffer[offset++] = static_cast<uint8_t>(track1);

    return offset;
}

size_t Disk2Card::deserialize(const uint8_t* buffer, size_t size) {
    if (!buffer || size < 8) return 0;

    size_t offset = 0;

    setMotorOn(buffer[offset++] != 0);
    setSelectedDrive(buffer[offset++]);
    setQ6(buffer[offset++] != 0);
    setQ7(buffer[offset++] != 0);
    setPhaseStates(buffer[offset++]);
    setDataLatch(buffer[offset++]);

    int track0 = buffer[offset++];
    int track1 = buffer[offset++];

    if (hasDisk(0)) {
        DiskImage* img = getMutableDiskImage(0);
        if (img) img->setQuarterTrack(track0);
    }
    if (hasDisk(1)) {
        DiskImage* img = getMutableDiskImage(1);
        if (img) img->setQuarterTrack(track1);
    }

    return offset;
}

// ===== Disk Operations =====

bool Disk2Card::insertDisk(int drive, const uint8_t* data, size_t size,
                           const std::string& filename) {
    if (drive < 0 || drive > 1) {
        return false;
    }

    // Determine file type from extension
    std::string lowerFilename = filename;
    std::transform(lowerFilename.begin(), lowerFilename.end(),
                   lowerFilename.begin(), ::tolower);

    std::unique_ptr<DiskImage> image;

    if (lowerFilename.find(".woz") != std::string::npos) {
        image = std::make_unique<WozDiskImage>();
    } else if (lowerFilename.find(".dsk") != std::string::npos ||
               lowerFilename.find(".do") != std::string::npos ||
               lowerFilename.find(".po") != std::string::npos) {
        image = std::make_unique<DskDiskImage>();
    } else {
        // Try to detect format from content
        if (size >= 12 && data[0] == 'W' && data[1] == 'O' && data[2] == 'Z') {
            image = std::make_unique<WozDiskImage>();
        } else if (size == 143360) {
            image = std::make_unique<DskDiskImage>();
        } else {
            return false;
        }
    }

    if (!image->load(data, size, filename)) {
        return false;
    }

    diskImages_[drive] = std::move(image);

    // Reset timing state for this drive
    lastReadCycle_[drive] = 0;
    if (drive == selectedDrive_) {
        latchValid_ = false;
        dataLatch_ = 0;
    }

    return true;
}

bool Disk2Card::insertBlankDisk(int drive) {
    if (drive < 0 || drive > 1) {
        return false;
    }

    auto image = std::make_unique<WozDiskImage>();
    image->createBlank();

    diskImages_[drive] = std::move(image);
    return true;
}

void Disk2Card::ejectDisk(int drive) {
    if (drive < 0 || drive > 1) {
        return;
    }

    if (selectedDrive_ == drive) {
        motorOn_ = false;
        motorOffCycle_ = 0;
    }

    diskImages_[drive].reset();
}

bool Disk2Card::hasDisk(int drive) const {
    if (drive < 0 || drive > 1) {
        return false;
    }
    return diskImages_[drive] != nullptr && diskImages_[drive]->isLoaded();
}

const uint8_t* Disk2Card::getDiskData(int drive, size_t* size) const {
    if (drive < 0 || drive > 1 || !hasDisk(drive)) {
        *size = 0;
        return nullptr;
    }
    return diskImages_[drive]->getSectorData(size);
}

const uint8_t* Disk2Card::exportDiskData(int drive, size_t* size) {
    if (drive < 0 || drive > 1 || !hasDisk(drive)) {
        *size = 0;
        return nullptr;
    }
    return diskImages_[drive]->exportData(size);
}

const DiskImage* Disk2Card::getDiskImage(int drive) const {
    if (drive < 0 || drive > 1) {
        return nullptr;
    }
    return diskImages_[drive].get();
}

DiskImage* Disk2Card::getMutableDiskImage(int drive) {
    if (drive < 0 || drive > 1) {
        return nullptr;
    }
    return diskImages_[drive].get();
}

bool Disk2Card::isMotorOn() const {
    if (motorOn_ && motorOffCycle_ != 0) {
        if (totalCycles_ >= motorOffCycle_ + MOTOR_OFF_DELAY_CYCLES) {
            motorOn_ = false;
            motorOffCycle_ = 0;
        }
    }
    return motorOn_;
}

void Disk2Card::stopMotor() {
    motorOn_ = false;
    motorOffCycle_ = 0;
}

int Disk2Card::getCurrentTrack() const {
    if (hasDisk(selectedDrive_)) {
        return diskImages_[selectedDrive_]->getTrack();
    }
    return -1;
}

int Disk2Card::getQuarterTrack() const {
    if (hasDisk(selectedDrive_)) {
        return diskImages_[selectedDrive_]->getQuarterTrack();
    }
    return -1;
}

// ===== Private Methods =====

uint8_t Disk2Card::handleSoftSwitch(uint8_t offset, bool isWrite) {
    (void)isWrite;

    auto setPhase = [this](int phase, bool on) {
        if (hasDisk(selectedDrive_)) {
            diskImages_[selectedDrive_]->setPhase(phase, on);
        }
    };

    switch (offset) {
    case PHASE0_OFF:
        phaseStates_ &= ~0x01;
        setPhase(0, false);
        break;
    case PHASE0_ON:
        phaseStates_ |= 0x01;
        setPhase(0, true);
        break;
    case PHASE1_OFF:
        phaseStates_ &= ~0x02;
        setPhase(1, false);
        break;
    case PHASE1_ON:
        phaseStates_ |= 0x02;
        setPhase(1, true);
        break;
    case PHASE2_OFF:
        phaseStates_ &= ~0x04;
        setPhase(2, false);
        break;
    case PHASE2_ON:
        phaseStates_ |= 0x04;
        setPhase(2, true);
        break;
    case PHASE3_OFF:
        phaseStates_ &= ~0x08;
        setPhase(3, false);
        break;
    case PHASE3_ON:
        phaseStates_ |= 0x08;
        setPhase(3, true);
        break;

    case MOTOR_OFF:
        if (isMotorOn() && !q7_) {
            uint8_t result = readDiskData();
            if (motorOn_ && motorOffCycle_ == 0) {
                motorOffCycle_ = totalCycles_;
            }
            return result;
        }
        if (motorOn_ && motorOffCycle_ == 0) {
            motorOffCycle_ = totalCycles_;
        }
        break;
    case MOTOR_ON:
        motorOffCycle_ = 0;
        if (!motorOn_) {
            lastReadCycle_[selectedDrive_] = 0;
            latchValid_ = false;
        }
        motorOn_ = true;
        break;

    case DRIVE1_SELECT:
        selectedDrive_ = 0;
        break;
    case DRIVE2_SELECT:
        selectedDrive_ = 1;
        break;

    case Q6L:
        q6_ = false;
        if (!q7_) {
            return readDiskData();
        } else {
            writeDiskData();
        }
        break;

    case Q6H:
        q6_ = true;
        if (!q7_) {
            if (isMotorOn() && hasDisk(selectedDrive_)) {
                dataLatch_ = 0;
                latchValid_ = false;
                uint64_t currentCycle = getCycles();
                uint64_t& lastCycle = lastReadCycle_[selectedDrive_];
                if (lastCycle != 0) {
                    uint64_t elapsed = currentCycle - lastCycle;
                    uint64_t nibbles = elapsed / CYCLES_PER_NIBBLE;
                    if (nibbles > 0) {
                        DiskImage* disk = diskImages_[selectedDrive_].get();
                        for (uint64_t i = 0; i < nibbles && i < 50; i++) {
                            disk->readNibble();
                        }
                    }
                }
                lastCycle = currentCycle;
                return diskImages_[selectedDrive_]->isWriteProtected() ? 0x80 : 0x00;
            }
            return 0x80;
        }
        break;

    case Q7L:
        if (q7_) {
            lastReadCycle_[selectedDrive_] = getCycles();
            latchValid_ = false;
        }
        q7_ = false;
        break;

    case Q7H:
        q7_ = true;
        break;
    }

    return 0x00;
}

uint8_t Disk2Card::readDiskData() {
    if (!isMotorOn() || !hasDisk(selectedDrive_)) {
        return 0;
    }

    DiskImage* disk = diskImages_[selectedDrive_].get();

    if (!disk->hasData()) {
        return 0;
    }

    uint64_t currentCycle = getCycles();
    uint64_t& lastCycle = lastReadCycle_[selectedDrive_];

    bool newNibbleReady =
        (lastCycle == 0) || (currentCycle >= lastCycle + CYCLES_PER_NIBBLE);

    if (newNibbleReady) {
        if (lastCycle != 0 && currentCycle > lastCycle) {
            uint64_t elapsed = currentCycle - lastCycle;
            uint64_t extraNibbles = (elapsed / CYCLES_PER_NIBBLE);
            if (extraNibbles > 1) {
                if (extraNibbles > 10) {
                    extraNibbles = 1;
                }
                for (uint64_t i = 1; i < extraNibbles; i++) {
                    disk->readNibble();
                }
            }
        }

        dataLatch_ = disk->readNibble();
        lastCycle = currentCycle;
        latchValid_ = true;
    }

    if (latchValid_) {
        latchValid_ = false;
        return dataLatch_;
    } else {
        return dataLatch_ & 0x7F;
    }
}

void Disk2Card::writeDiskData() {
    if (!isMotorOn() || !hasDisk(selectedDrive_) || !q7_) {
        return;
    }

    DiskImage* disk = diskImages_[selectedDrive_].get();

    if (disk->isWriteProtected()) {
        return;
    }

    if (!disk->hasData()) {
        return;
    }

    disk->writeNibble(writeLatch_);
    lastReadCycle_[selectedDrive_] = getCycles();
}

} // namespace a2e
