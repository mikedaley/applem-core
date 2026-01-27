#include "disk2_card.hpp"
#include <algorithm>
#include <cstring>

namespace a2e {

Disk2Card::Disk2Card()
    : controller_(std::make_unique<Disk2Controller>())
{
    rom_.fill(0xFF);
}

Disk2Card::Disk2Card(const uint8_t* rom, size_t romSize)
    : controller_(std::make_unique<Disk2Controller>())
{
    rom_.fill(0xFF);
    if (rom && romSize > 0) {
        loadROM(rom, romSize);
    }
}

void Disk2Card::loadROM(const uint8_t* rom, size_t size) {
    if (rom && size > 0) {
        std::memcpy(rom_.data(), rom, std::min(size, rom_.size()));
    }
}

uint8_t Disk2Card::readIO(uint8_t offset) {
    return controller_->read(offset & 0x0F);
}

void Disk2Card::writeIO(uint8_t offset, uint8_t value) {
    controller_->write(offset & 0x0F, value);
}

uint8_t Disk2Card::peekIO(uint8_t offset) const {
    return controller_->peek(offset & 0x0F);
}

uint8_t Disk2Card::readROM(uint8_t offset) {
    return rom_[offset];
}

void Disk2Card::reset() {
    controller_->reset();
}

void Disk2Card::update(int cycles) {
    controller_->update(cycles);
}

void Disk2Card::setCycleCallback(CycleCallback callback) {
    controller_->setCycleCallback(std::move(callback));
}

size_t Disk2Card::getStateSize() const {
    // Controller state: motor, drive select, Q6, Q7, phases, data latch
    // Plus disk image modifications
    return 32; // Base controller state (actual size varies with disk modifications)
}

size_t Disk2Card::serialize(uint8_t* buffer, size_t maxSize) const {
    if (!buffer || maxSize < 16) return 0;

    size_t offset = 0;

    // Controller state
    buffer[offset++] = controller_->isMotorOn() ? 1 : 0;
    buffer[offset++] = static_cast<uint8_t>(controller_->getSelectedDrive());
    buffer[offset++] = controller_->getQ6() ? 1 : 0;
    buffer[offset++] = controller_->getQ7() ? 1 : 0;
    buffer[offset++] = controller_->getPhaseStates();
    buffer[offset++] = controller_->getDataLatch();

    // Track positions for both drives
    int track0 = 0, track1 = 0;
    if (controller_->hasDisk(0)) {
        const DiskImage* img = controller_->getDiskImage(0);
        if (img) track0 = img->getQuarterTrack();
    }
    if (controller_->hasDisk(1)) {
        const DiskImage* img = controller_->getDiskImage(1);
        if (img) track1 = img->getQuarterTrack();
    }
    buffer[offset++] = static_cast<uint8_t>(track0);
    buffer[offset++] = static_cast<uint8_t>(track1);

    return offset;
}

size_t Disk2Card::deserialize(const uint8_t* buffer, size_t size) {
    if (!buffer || size < 8) return 0;

    size_t offset = 0;

    controller_->setMotorOn(buffer[offset++] != 0);
    controller_->setSelectedDrive(buffer[offset++]);
    controller_->setQ6(buffer[offset++] != 0);
    controller_->setQ7(buffer[offset++] != 0);
    controller_->setPhaseStates(buffer[offset++]);
    controller_->setDataLatch(buffer[offset++]);

    // Track positions
    int track0 = buffer[offset++];
    int track1 = buffer[offset++];

    // Restore track positions
    if (controller_->hasDisk(0)) {
        DiskImage* img = controller_->getMutableDiskImage(0);
        if (img) img->setQuarterTrack(track0);
    }
    if (controller_->hasDisk(1)) {
        DiskImage* img = controller_->getMutableDiskImage(1);
        if (img) img->setQuarterTrack(track1);
    }

    return offset;
}

} // namespace a2e
