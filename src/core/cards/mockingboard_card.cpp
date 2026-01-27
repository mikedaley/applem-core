#include "mockingboard_card.hpp"

namespace a2e {

MockingboardCard::MockingboardCard()
    : mockingboard_(std::make_unique<Mockingboard>())
{
}

uint8_t MockingboardCard::readIO(uint8_t offset) {
    // Mockingboard doesn't use I/O space ($C0C0-$C0CF)
    // It uses ROM space for VIA registers
    return 0xFF;
}

void MockingboardCard::writeIO(uint8_t offset, uint8_t value) {
    // Mockingboard doesn't use I/O space
}

uint8_t MockingboardCard::peekIO(uint8_t offset) const {
    return 0xFF;
}

uint8_t MockingboardCard::readROM(uint8_t offset) {
    // Mockingboard uses ROM space for VIA registers
    // $C400-$C4FF maps to Mockingboard
    // The Mockingboard::read expects a full address ($C400-$C4FF)
    uint16_t address = 0xC400 + offset;
    return mockingboard_->read(address);
}

void MockingboardCard::writeROM(uint8_t offset, uint8_t value) {
    // Mockingboard uses ROM space for VIA registers
    uint16_t address = 0xC400 + offset;
    mockingboard_->write(address, value);
}

void MockingboardCard::reset() {
    mockingboard_->reset();
}

void MockingboardCard::update(int cycles) {
    mockingboard_->update(cycles);
}

void MockingboardCard::setIRQCallback(IRQCallback callback) {
    mockingboard_->setIRQCallback(std::move(callback));
}

void MockingboardCard::setCycleCallback(CycleCallback callback) {
    mockingboard_->setCycleCallback(std::move(callback));
}

bool MockingboardCard::isIRQActive() const {
    return mockingboard_->isIRQActive();
}

bool MockingboardCard::isEnabled() const {
    return mockingboard_->isEnabled();
}

void MockingboardCard::setEnabled(bool enabled) {
    mockingboard_->setEnabled(enabled);
}

size_t MockingboardCard::getStateSize() const {
    return Mockingboard::STATE_SIZE;
}

size_t MockingboardCard::serialize(uint8_t* buffer, size_t maxSize) const {
    return mockingboard_->exportState(buffer, maxSize);
}

size_t MockingboardCard::deserialize(const uint8_t* buffer, size_t size) {
    return mockingboard_->importState(buffer, size);
}

} // namespace a2e
