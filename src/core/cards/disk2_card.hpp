#pragma once

#include "expansion_card.hpp"
#include "../disk/disk2.hpp"
#include <memory>
#include <array>

namespace a2e {

/**
 * Disk2Card - Disk II Controller Card adapter
 *
 * Wraps the existing Disk2Controller to implement the ExpansionCard interface.
 * The Disk II typically occupies slot 6, providing:
 * - I/O space: $C0E0-$C0EF (16 soft switches for disk control)
 * - ROM space: $C600-$C6FF (256 byte bootstrap ROM, P5A 341-0027)
 *
 * The card does not use expansion ROM ($C800-$CFFF).
 */
class Disk2Card : public ExpansionCard {
public:
    Disk2Card();
    explicit Disk2Card(const uint8_t* rom, size_t romSize);
    ~Disk2Card() override = default;

    // Delete copy (controller is non-copyable)
    Disk2Card(const Disk2Card&) = delete;
    Disk2Card& operator=(const Disk2Card&) = delete;

    // Allow move
    Disk2Card(Disk2Card&&) = default;
    Disk2Card& operator=(Disk2Card&&) = default;

    // ===== ExpansionCard Interface =====

    uint8_t readIO(uint8_t offset) override;
    void writeIO(uint8_t offset, uint8_t value) override;
    uint8_t peekIO(uint8_t offset) const override;

    uint8_t readROM(uint8_t offset) override;
    bool hasROM() const override { return true; }

    bool hasExpansionROM() const override { return false; }

    void reset() override;
    void update(int cycles) override;

    void setCycleCallback(CycleCallback callback) override;

    size_t getStateSize() const override;
    size_t serialize(uint8_t* buffer, size_t maxSize) const override;
    size_t deserialize(const uint8_t* buffer, size_t size) override;

    const char* getName() const override { return "Disk II"; }
    uint8_t getPreferredSlot() const override { return 6; }

    // ===== Disk II Specific Methods =====

    /**
     * Load the P5A ROM (341-0027)
     * @param rom ROM data
     * @param size ROM size (should be 256 bytes)
     */
    void loadROM(const uint8_t* rom, size_t size);

    /**
     * Get direct access to the underlying controller
     * Needed for disk operations (insert, eject, etc.)
     * @return Reference to the Disk2Controller
     */
    Disk2Controller& getController() { return *controller_; }
    const Disk2Controller& getController() const { return *controller_; }

    // ===== Disk Operations (delegated to controller) =====

    bool insertDisk(int drive, const uint8_t* data, size_t size, const std::string& filename) {
        return controller_->insertDisk(drive, data, size, filename);
    }

    bool insertBlankDisk(int drive) {
        return controller_->insertBlankDisk(drive);
    }

    void ejectDisk(int drive) {
        controller_->ejectDisk(drive);
    }

    bool hasDisk(int drive) const {
        return controller_->hasDisk(drive);
    }

    const uint8_t* getDiskData(int drive, size_t* size) const {
        return controller_->getDiskData(drive, size);
    }

    const uint8_t* exportDiskData(int drive, size_t* size) {
        return controller_->exportDiskData(drive, size);
    }

    const DiskImage* getDiskImage(int drive) const {
        return controller_->getDiskImage(drive);
    }

    DiskImage* getMutableDiskImage(int drive) {
        return controller_->getMutableDiskImage(drive);
    }

    bool isMotorOn() const {
        return controller_->isMotorOn();
    }

    void stopMotor() {
        controller_->stopMotor();
    }

    int getSelectedDrive() const {
        return controller_->getSelectedDrive();
    }

    int getCurrentTrack() const {
        return controller_->getCurrentTrack();
    }

    int getQuarterTrack() const {
        return controller_->getQuarterTrack();
    }

    uint8_t getPhaseStates() const {
        return controller_->getPhaseStates();
    }

    bool getQ6() const {
        return controller_->getQ6();
    }

    bool getQ7() const {
        return controller_->getQ7();
    }

    uint8_t getDataLatch() const {
        return controller_->getDataLatch();
    }

    // State restoration
    void setSelectedDrive(int drive) { controller_->setSelectedDrive(drive); }
    void setQ6(bool q6) { controller_->setQ6(q6); }
    void setQ7(bool q7) { controller_->setQ7(q7); }
    void setPhaseStates(uint8_t states) { controller_->setPhaseStates(states); }
    void setDataLatch(uint8_t latch) { controller_->setDataLatch(latch); }
    void setMotorOn(bool on) { controller_->setMotorOn(on); }

private:
    std::unique_ptr<Disk2Controller> controller_;
    std::array<uint8_t, 256> rom_;  // P5A ROM
};

} // namespace a2e
