/*
 * disk2_card.hpp - Disk II controller card
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "expansion_card.hpp"
#include "../disk-image/disk_image.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace a2e {

/**
 * Disk2Card - Disk II Controller Card
 *
 * Complete implementation of the Disk II controller card for Apple IIe.
 * The Disk II typically occupies slot 6, providing:
 * - I/O space: $C0E0-$C0EF (16 soft switches for disk control)
 * - ROM space: $C600-$C6FF (256 byte bootstrap ROM, P5A 341-0027)
 *
 * The card does not use expansion ROM ($C800-$CFFF).
 *
 * Soft switch addresses (offset from slot base):
 * $00 - Phase 0 off     $01 - Phase 0 on
 * $02 - Phase 1 off     $03 - Phase 1 on
 * $04 - Phase 2 off     $05 - Phase 2 on
 * $06 - Phase 3 off     $07 - Phase 3 on
 * $08 - Motor off       $09 - Motor on
 * $0A - Drive 1 select  $0B - Drive 2 select
 * $0C - Q6L (read)      $0D - Q6H (WP sense/write load)
 * $0E - Q7L (read mode) $0F - Q7H (write mode)
 */
class Disk2Card : public ExpansionCard {
public:
    Disk2Card();
    explicit Disk2Card(const uint8_t* rom, size_t romSize);
    ~Disk2Card() override = default;

    // Delete copy (disk images are non-copyable)
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

    // ===== Disk Operations =====

    /**
     * Insert a disk image into a drive
     * @param drive Drive number (0 or 1)
     * @param data Pointer to disk image data
     * @param size Size of the data
     * @param filename Original filename (for format detection)
     * @return true on success
     */
    bool insertDisk(int drive, const uint8_t* data, size_t size, const std::string& filename);

    /**
     * Insert a blank, unformatted disk into a drive
     * @param drive Drive number (0 or 1)
     * @return true on success
     */
    bool insertBlankDisk(int drive);

    /**
     * Eject disk from a drive
     * @param drive Drive number (0 or 1)
     */
    void ejectDisk(int drive);

    /**
     * Check if a drive has a disk inserted
     * @param drive Drive number (0 or 1)
     * @return true if disk is inserted
     */
    bool hasDisk(int drive) const;

    /**
     * Get the disk data for saving (DSK format)
     * @param drive Drive number (0 or 1)
     * @param size Output: size of the data
     * @return Pointer to disk data, or nullptr if no disk
     */
    const uint8_t* getDiskData(int drive, size_t* size) const;

    /**
     * Export disk data in its native format for saving
     * This works for both DSK and WOZ formats.
     * @param drive Drive number (0 or 1)
     * @param size Output: size of the exported data
     * @return Pointer to exported data, or nullptr if no disk
     */
    const uint8_t* exportDiskData(int drive, size_t* size);

    /**
     * Get the disk image for a drive (for UI display)
     * @param drive Drive number (0 or 1)
     * @return Pointer to disk image, or nullptr if no disk
     */
    const DiskImage* getDiskImage(int drive) const;

    /**
     * Get mutable disk image for a drive (for state restoration)
     * @param drive Drive number (0 or 1)
     * @return Pointer to disk image, or nullptr if no disk
     */
    DiskImage* getMutableDiskImage(int drive);

    /**
     * Check if motor is currently on
     * @return true if motor is running
     */
    bool isMotorOn() const;

    /**
     * Stop the motor immediately (for warm reset)
     * Does not reset other controller state like track position
     */
    void stopMotor();

    /**
     * Get currently selected drive (0 or 1)
     * @return Selected drive number
     */
    int getSelectedDrive() const { return selectedDrive_; }

    /**
     * Get the current track position from the selected drive's disk image
     * @return Track number (0-34), or -1 if no disk
     */
    int getCurrentTrack() const;

    /**
     * Get the current quarter-track position from the selected drive's disk image
     * @return Quarter-track number (0-139), or -1 if no disk
     */
    int getQuarterTrack() const;

    /**
     * Get the current phase magnet states
     * @return Bit field where bit 0-3 represent phases 0-3 (1 = on, 0 = off)
     */
    uint8_t getPhaseStates() const { return phaseStates_; }

    /**
     * Get Q6 latch state
     * @return true if Q6 is high, false if low
     */
    bool getQ6() const { return q6_; }

    /**
     * Get Q7 latch state
     * @return true if Q7 is high (write mode), false if low (read mode)
     */
    bool getQ7() const { return q7_; }

    /**
     * Get the data latch value (last nibble read/written)
     * @return Current data latch value
     */
    uint8_t getDataLatch() const { return dataLatch_; }

    // ===== State Restoration Methods =====

    void setSelectedDrive(int drive) { selectedDrive_ = (drive == 0) ? 0 : 1; }
    void setQ6(bool q6) { q6_ = q6; }
    void setQ7(bool q7) { q7_ = q7; }
    void setPhaseStates(uint8_t states) { phaseStates_ = states; }
    void setDataLatch(uint8_t latch) { dataLatch_ = latch; }
    void setMotorOn(bool on) { motorOn_ = on; }

private:
    // Soft switch offsets
    static constexpr uint8_t PHASE0_OFF = 0x00;
    static constexpr uint8_t PHASE0_ON = 0x01;
    static constexpr uint8_t PHASE1_OFF = 0x02;
    static constexpr uint8_t PHASE1_ON = 0x03;
    static constexpr uint8_t PHASE2_OFF = 0x04;
    static constexpr uint8_t PHASE2_ON = 0x05;
    static constexpr uint8_t PHASE3_OFF = 0x06;
    static constexpr uint8_t PHASE3_ON = 0x07;
    static constexpr uint8_t MOTOR_OFF = 0x08;
    static constexpr uint8_t MOTOR_ON = 0x09;
    static constexpr uint8_t DRIVE1_SELECT = 0x0A;
    static constexpr uint8_t DRIVE2_SELECT = 0x0B;
    static constexpr uint8_t Q6L = 0x0C;
    static constexpr uint8_t Q6H = 0x0D;
    static constexpr uint8_t Q7L = 0x0E;
    static constexpr uint8_t Q7H = 0x0F;

    // Motor timeout: ~1 second at 1.023 MHz
    static constexpr uint64_t MOTOR_OFF_DELAY_CYCLES = 1023000;

    // Nibble timing: ~31 cycles per nibble
    static constexpr uint64_t CYCLES_PER_NIBBLE = 31;

    // P5A ROM (256 bytes)
    std::array<uint8_t, 256> rom_;

    // Controller state
    mutable bool motorOn_ = false;
    mutable uint64_t motorOffCycle_ = 0;
    int selectedDrive_ = 0;
    bool q6_ = false;
    bool q7_ = false;
    uint8_t phaseStates_ = 0;

    // Timing state
    uint64_t totalCycles_ = 0;
    uint64_t lastReadCycle_[2] = {0, 0};

    // Data latches
    uint8_t dataLatch_ = 0;
    bool latchValid_ = false;
    uint8_t writeLatch_ = 0;

    // Disk images for each drive
    std::unique_ptr<DiskImage> diskImages_[2];

    // Cycle callback
    CycleCallback cycleCallback_;

    /**
     * Get current cycle count, using callback if available
     */
    uint64_t getCycles() const {
        if (cycleCallback_) {
            return cycleCallback_();
        }
        return totalCycles_;
    }

    /**
     * Handle soft switch access
     * @param offset Offset (0x00-0x0F)
     * @param isWrite true if write access, false if read
     * @return byte value for reads
     */
    uint8_t handleSoftSwitch(uint8_t offset, bool isWrite);

    /**
     * Read a byte from the current disk
     * @return The data latch value
     */
    uint8_t readDiskData();

    /**
     * Write a byte to the current disk
     */
    void writeDiskData();
};

} // namespace a2e
