/*
 * iwm.cpp - The Integrated Woz Machine, a //c's and a IIgs's disk controller
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "iwm.hpp"

#include <algorithm>
#include <vector>

namespace a2e {

namespace {
// The mode register is five bits wide; the top three read back as zero.
constexpr uint8_t MODE_MASK = 0x1F;
constexpr uint8_t MODE_LATCH = 0x01;    // L: a read byte is held until taken
constexpr uint8_t MODE_ASYNC = 0x02;    // H: the chip times writes itself
constexpr uint8_t MODE_NO_DELAY = 0x04; // M: ENABLE off means off

// Status register
constexpr uint8_t STATUS_SENSE = 0x80; // The line ca2/ca1/ca0 selected
constexpr uint8_t STATUS_MOTOR = 0x20; // ENABLE, the drive motor

// Handshake register
constexpr uint8_t HANDSHAKE_READY = 0x80;    // Write data register is empty
constexpr uint8_t HANDSHAKE_NO_UNDERRUN = 0x40;

// $C031
constexpr uint8_t DISK_REGISTER_35 = 0x40;
constexpr uint8_t DISK_REGISTER_SEL = 0x80;

// A 3.5" bit cell is 2us: two cycles of the 1.023MHz clock.
constexpr uint64_t CYCLES_PER_35_CELL = 2;
// More than a turn of the slowest track, for a gap with nobody listening.
constexpr uint64_t MAX_35_CATCHUP_CELLS = 160000;
} // namespace

IWM::Register IWM::selectedRegister() const {
    if (!q7_) return q6_ ? Register::Status : Register::Data;
    return q6_ ? Register::Write : Register::Handshake;
}

uint8_t IWM::readStatus() const {
    // SENSE is whichever drive line the ca lines have selected. On a 5.25"
    // drive the firmware asks for exactly one of them — write protect — and
    // the sequencer already senses it the card's way, so that is what is
    // reported. A 3.5" drive answers its selector.
    uint8_t status = mode_ & MODE_MASK;
    if (en35_) {
        auto* self = const_cast<IWM*>(this);
        if (self->sony_[selectedDrive_].sense(getCycles())) status |= STATUS_SENSE;
    } else {
        const DiskImage* disk = getDiskImage(selectedDrive_);
        if (disk && disk->isWriteProtected()) status |= STATUS_SENSE;
    }
    if (isMotorOn()) status |= STATUS_MOTOR;
    return status;
}

uint8_t IWM::readHandshake() const {
    // A 3.5" write is the chip's own: the buffer is ready for a byte once the
    // one before has gone into the shifter, and an underrun is the shifter
    // running dry with nothing waiting, which is how firmware knows its last
    // byte has reached the disk.
    if (en35_) {
        return static_cast<uint8_t>((bufferFull_ ? 0 : HANDSHAKE_READY) |
                                    (underrun_ ? 0 : HANDSHAKE_NO_UNDERRUN));
    }
    // The 5.25" sequencer consumes a written byte within the bit cell it was
    // loaded in, so the write data register is always empty when asked and
    // there is never an underrun. Firmware polls bit 7 before loading the
    // next byte; answering "ready" is what lets that loop run rather than spin.
    return HANDSHAKE_READY | HANDSHAKE_NO_UNDERRUN;
}

// ===== The 3.5" port =====

void IWM::setDiskRegister(uint8_t value) {
    const uint64_t now = getCycles();
    catchUp35(now);
    const bool en35 = (value & DISK_REGISTER_35) != 0;
    if (en35 != en35_) {
        // The other port takes over from here, from where it is now.
        if (!en35) lastLSSCycle_ = now;
        last35Cycle_ = now;
    }
    en35_ = en35;
    sel_ = (value & DISK_REGISTER_SEL) != 0;
    sony_[0].setSel(sel_);
    sony_[1].setSel(sel_);
    routeEnable(now);
}

void IWM::routeEnable(uint64_t now) {
    // /ENBL, held up through the motor-off delay as on the 5.25" port, reaches
    // the 3.5" drive SELECT names while that port is selected, and no other.
    const bool line = isMotorOn();
    for (int d = 0; d < 2; d++) {
        sony_[d].setEnabled(en35_ && line && d == selectedDrive_, now);
    }
}

void IWM::catchUp35(uint64_t now) {
    SonyDrive& drive = sony_[selectedDrive_];
    if (!en35_ || now <= last35Cycle_ || !isMotorOn() || !drive.isSpinning(now)) {
        last35Cycle_ = now;
        return;
    }
    uint64_t cells = (now - last35Cycle_) / CYCLES_PER_35_CELL;
    last35Cycle_ += cells * CYCLES_PER_35_CELL;
    if (cells > MAX_35_CATCHUP_CELLS) cells = MAX_35_CATCHUP_CELLS;

    const bool writing = q7_ && isDriveEnabled() && (mode_ & MODE_ASYNC);
    for (uint64_t i = 0; i < cells; i++) {
        if (!q7_) {
            // Read: shift until the top bit is set, which every disk byte
            // has, and the byte is then the processor's to read.
            const uint8_t bit = drive.readBit();
            if (q6_) continue; // sensing status: the bits go past
            shift35_ = static_cast<uint8_t>((shift35_ << 1) | bit);
            if (shift35_ & 0x80) {
                data35_ = shift35_;
                shift35_ = 0;
            }
            continue;
        }
        if (!writing || underrun_) {
            drive.readBit(); // the disk turns; nothing is written
            continue;
        }
        if (writeBits_ == 0) {
            if (!bufferFull_) {
                // Nothing waiting: the write stops here, and the handshake
                // register says so.
                underrun_ = true;
                drive.readBit();
                continue;
            }
            writeShift_ = writeBuffer_;
            writeBits_ = 8;
            bufferFull_ = false;
        }
        drive.writeBit(static_cast<uint8_t>((writeShift_ >> 7) & 1));
        writeShift_ = static_cast<uint8_t>(writeShift_ << 1);
        writeBits_--;
    }
}

// ===== The state lines =====

uint8_t IWM::access(uint8_t offset, bool isWrite) {
    const uint64_t now = getCycles();
    catchUp35(now);
    const bool wasWriting = q7_;

    uint8_t fromSwitch = 0;
    if (en35_ && offset < 8) {
        // CA0-CA2 and LSTRB: the 3.5" drive's selector and strobe, not a
        // stepper. The selected drive hears them.
        const int line = offset >> 1;
        const bool on = (offset & 1) != 0;
        if (on) phaseStates_ |= static_cast<uint8_t>(1 << line);
        else phaseStates_ &= static_cast<uint8_t>(~(1 << line));
        sony_[selectedDrive_].setLine(line, on, now);
    } else {
        fromSwitch = handleSoftSwitch(offset, isWrite);
    }

    // With M set ENABLE off is off at once, which is what a 3.5" drive wants.
    if (offset == MOTOR_OFF && (mode_ & MODE_NO_DELAY)) stopMotor();

    if (q7_ != wasWriting) {
        // Into write mode the buffer starts empty and nothing has run out;
        // out of it, reading starts a byte afresh.
        bufferFull_ = false;
        writeBits_ = 0;
        underrun_ = false;
        shift35_ = 0;
    }
    routeEnable(now);
    return fromSwitch;
}

uint8_t IWM::readIO(uint8_t offset) {
    // Every one of the sixteen addresses is a state line, set by odd addresses
    // and cleared by even ones; which register the read returns is then the
    // Q7/Q6 pair's business, not the address's. The card behaves the same way
    // — its reads all return the data latch — so the switch handling, the
    // sequencer catch-up and the stepper are the shared ones.
    const uint8_t fromSwitch = access(offset & 0x0F, false);

    switch (selectedRegister()) {
    case Register::Data:
        if (en35_) {
            // In latch mode a byte is held until it is read, and then gone,
            // so a loop polling for bit 7 waits for the next one.
            const uint8_t value = data35_;
            if (mode_ & MODE_LATCH) data35_ = 0;
            return value;
        }
        return fromSwitch;
    case Register::Status:
        return readStatus();
    case Register::Handshake:
        return readHandshake();
    case Register::Write:
        break;
    }
    return en35_ ? data35_ : dataRegister_;
}

void IWM::writeIO(uint8_t offset, uint8_t value) {
    access(offset & 0x0F, true);

    if (selectedRegister() != Register::Write) return;

    // With both latches high the chip takes a byte, and which byte depends on
    // whether a drive is enabled: the mode register is only writable while it
    // is not, which is how the firmware sets the chip up before it starts a
    // drive and cannot disturb it mid-write afterwards.
    //
    // ENABLE is the wire, not the motor. A drive keeps turning for about a
    // second after the CPU switches it off, and `isMotorOn()` says so — but
    // the chip's registers answer to the line, which went low immediately. The
    // firmware writes the mode register the instruction after it touches
    // $C0E8, reads it back, and will not go on until the two agree; asking
    // about the mechanism instead of the wire sends that byte to the write
    // data register for a second, and the read-back never matches.
    if (isDriveEnabled()) {
        if (en35_ && (mode_ & MODE_ASYNC)) {
            writeBuffer_ = value;
            bufferFull_ = true;
        } else {
            busData_ = value;
        }
    } else {
        mode_ = value & MODE_MASK;
    }
}

uint8_t IWM::peekIO(uint8_t offset) const {
    // A peek is the debugger looking, so it answers from the state the chip is
    // already in rather than touching a state line on the way past.
    (void)offset;
    switch (selectedRegister()) {
    case Register::Status:
        return readStatus();
    case Register::Handshake:
        return readHandshake();
    case Register::Data:
    case Register::Write:
        break;
    }
    return en35_ ? data35_ : dataRegister_;
}

void IWM::reset() {
    DiskController::reset();
    mode_ = 0;
    en35_ = false;
    sel_ = false;
    last35Cycle_ = 0;
    shift35_ = data35_ = 0;
    writeBuffer_ = writeShift_ = writeBits_ = 0;
    bufferFull_ = underrun_ = false;
    sony_[0].reset();
    sony_[1].reset();
}

// ===== Save state =====

namespace {
constexpr uint8_t STATE_35_MARKER = 0x35;
}

size_t IWM::getStateSize() const {
    return DiskController::getStateSize() + 128;
}

size_t IWM::serialize(uint8_t* buffer, size_t maxSize) const {
    size_t offset = DiskController::serialize(buffer, maxSize);
    if (offset == 0 || offset >= maxSize) return offset;
    buffer[offset++] = mode_;

    // The 3.5" port, after a marker so a state from before it reads as none.
    std::vector<uint8_t> port;
    port.push_back(STATE_35_MARKER);
    port.push_back(static_cast<uint8_t>((en35_ ? 1 : 0) | (sel_ ? 2 : 0) |
                                        (bufferFull_ ? 4 : 0) | (underrun_ ? 8 : 0)));
    port.push_back(shift35_);
    port.push_back(data35_);
    port.push_back(writeBuffer_);
    port.push_back(writeShift_);
    port.push_back(writeBits_);
    for (int i = 0; i < 8; i++) port.push_back(static_cast<uint8_t>(last35Cycle_ >> (8 * i)));
    sony_[0].serialize(port);
    sony_[1].serialize(port);
    if (offset + port.size() > maxSize) return offset;
    std::copy(port.begin(), port.end(), buffer + offset);
    return offset + port.size();
}

size_t IWM::deserialize(const uint8_t* buffer, size_t size) {
    size_t offset = DiskController::deserialize(buffer, size);
    if (offset == 0) return 0;
    // A state written before the chip had a mode register leaves it at zero,
    // which is what the firmware finds on a machine that has just come up.
    mode_ = (offset < size) ? (buffer[offset++] & MODE_MASK) : 0;

    en35_ = false;
    if (offset < size && buffer[offset] == STATE_35_MARKER && size - offset >= 15) {
        const uint8_t* p = buffer + offset + 1;
        const uint8_t* end = buffer + size;
        const uint8_t flags = *p++;
        en35_ = (flags & 1) != 0;
        sel_ = (flags & 2) != 0;
        bufferFull_ = (flags & 4) != 0;
        underrun_ = (flags & 8) != 0;
        shift35_ = *p++;
        data35_ = *p++;
        writeBuffer_ = *p++;
        writeShift_ = *p++;
        writeBits_ = *p++;
        last35Cycle_ = 0;
        for (int i = 0; i < 8; i++) last35Cycle_ |= static_cast<uint64_t>(*p++) << (8 * i);
        if (!sony_[0].deserialize(p, end) || !sony_[1].deserialize(p, end)) return 0;
        offset = static_cast<size_t>(p - buffer);
    }
    return offset;
}

} // namespace a2e
