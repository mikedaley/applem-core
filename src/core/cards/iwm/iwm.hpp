/*
 * iwm.hpp - The Integrated Woz Machine, a //c's and a IIgs's disk controller
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "../disk_controller.hpp"
#include "sony_drive.hpp"
#include <cstdint>

namespace a2e {

/**
 * IWM - Integrated Woz Machine (344-0041)
 *
 * A //c has no slot 6 to put a Disk II card in; it has this chip soldered to
 * the board, its internal drive on one of the two drive connectors and the
 * external 5.25" port on the other. The chip is the Disk II controller in one
 * package, and it decodes slot 6's sixteen addresses — $C0E0-$C0EF — with the
 * same meanings, which is why the //c's firmware is recognisably the same disk
 * code and why the drives and the sequencer are DiskController's, shared with
 * the card.
 *
 * What it adds is a register file. On a Disk II every read returns the data
 * latch; on an IWM a read returns whichever of four registers the Q7/Q6 pair
 * selects:
 *
 *   Q7 Q6
 *    0  0   data register — the sequencer's shift register, as the card's
 *    0  1   status: SENSE in bit 7, the motor in bit 5, the mode in bits 4-0
 *    1  0   handshake: write-data ready in bit 7, underrun in bit 6
 *    1  1   write: a write loads the data register, or the mode register when
 *           the motor is off
 *
 * The mode register is where firmware asks for the timings it wants: L, the
 * latch (bit 0); H, asynchronous writes (bit 1); M, no motor-off delay (bit
 * 2); C, the 2us bit cell (bit 3). A 5.25" drive is read with all of them
 * clear, through the P6 sequencer, at the one rate a 5.25" drive uses.
 *
 * **A IIgs points the chip at a 3.5" port as well**, through bit 6 of $C031
 * (setDiskRegister). Then the four phase lines and SEL (bit 7 of $C031) go to
 * a Sony drive as its command selector rather than to a stepper, SENSE is
 * whichever status bit that selects, and the data path is the chip's own
 * rather than the P6 ROM's: bits arrive every 2us, a read in latch mode holds
 * a whole byte until the processor has taken it, and a write in asynchronous
 * mode is a byte buffer the chip empties onto the disk itself, with the
 * handshake register saying when it wants the next byte and when it ran out.
 * The firmware sets mode $0F for that, and the 5.25" sequencer stands still.
 * A //c never writes $C031, so on a //c none of this is ever reached.
 *
 * There is no ROM. A card's boot ROM lives in its slot's 256 bytes; a //c's
 * disk firmware is part of the 16KB system ROM, which is also why $C600 boots
 * a //c at all with nothing in a socket.
 */
class IWM : public DiskController {
public:
    IWM() = default;
    ~IWM() override = default;

    IWM(const IWM&) = delete;
    IWM& operator=(const IWM&) = delete;

    // ===== ExpansionCard Interface =====

    uint8_t readIO(uint8_t offset) override;
    void writeIO(uint8_t offset, uint8_t value) override;
    uint8_t peekIO(uint8_t offset) const override;

    // Nothing answers in slot 6's ROM space: the firmware is in the system ROM.
    uint8_t readROM(uint8_t /*offset*/) override { return 0xFF; }
    bool hasROM() const override { return false; }

    void reset() override;

    size_t getStateSize() const override;
    size_t serialize(uint8_t* buffer, size_t maxSize) const override;
    size_t deserialize(const uint8_t* buffer, size_t size) override;

    const char* getName() const override { return "IWM"; }

    // ===== IWM Specific =====

    /** The mode register as the firmware last wrote it (bits 4-0). */
    uint8_t getModeRegister() const { return mode_; }

    /** The status register: SENSE, the motor, and the mode. */
    uint8_t readStatus() const;

    /** The handshake register: write-data ready, and the underrun flag. */
    uint8_t readHandshake() const;

    // ===== The 3.5" port (a IIgs) =====

    /** $C031: bit 6 picks the 3.5" drives, bit 7 is their SEL line. */
    void setDiskRegister(uint8_t value);
    bool is35Selected() const { return en35_; }

    /** The 3.5" drives, 0 and 1. */
    SonyDrive& sonyDrive(int drive) { return sony_[drive & 1]; }
    const SonyDrive& sonyDrive(int drive) const { return sony_[drive & 1]; }

    /** A 5.25" drive turning: the IWM enabled and pointed at that port. */
    bool isFiveInchMotorOn() const override { return !en35_ && isMotorOn(); }

protected:
    bool fiveInchSelected() const override { return !en35_; }

private:
    // Which register a read sees, from the Q7/Q6 pair.
    enum class Register { Data, Status, Handshake, Write };
    Register selectedRegister() const;

    // A state line, wherever it goes, and what follows from it.
    uint8_t access(uint8_t offset, bool isWrite);
    // Run the 3.5" data path up to now.
    void catchUp35(uint64_t now);
    // /ENBL goes to one 3.5" drive: the one selected, while the port is.
    void routeEnable(uint64_t now);

    uint8_t mode_ = 0; // Mode register, bits 4-0

    // The 3.5" port
    SonyDrive sony_[2];
    bool en35_ = false;
    bool sel_ = false;
    uint64_t last35Cycle_ = 0;  // the data path has run to here
    uint8_t shift35_ = 0;       // bits arriving
    uint8_t data35_ = 0;        // the byte the processor reads
    uint8_t writeBuffer_ = 0;   // the byte the processor wrote
    bool bufferFull_ = false;
    uint8_t writeShift_ = 0;    // the byte going onto the disk
    uint8_t writeBits_ = 0;
    bool underrun_ = false;
};

} // namespace a2e
