/*
 * via6522.cpp - VIA 6522 timer chip emulation implementation
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "via6522.hpp"
#include "ay8910.hpp"
#include "../../debug/debug_log.hpp"

namespace a2e {

// Debug logging flag
static bool viaDebugLogging_ = false;

void VIA6522::setDebugLogging(bool enabled) {
    viaDebugLogging_ = enabled;
}

void VIA6522::setViaId(int id) {
    viaId_ = id;
}

VIA6522::VIA6522() {
    reset();
}

void VIA6522::reset() {
    ora_ = 0;
    orb_ = 0;
    ddra_ = 0;
    ddrb_ = 0;
    ira_ = 0xFF;  // Floating high
    irb_ = 0xFF;

    t1Counter_ = 0xFFFF;
    t1Latch_ = 0xFFFF;
    t1Running_ = false;
    t1Fired_ = false;

    t2Counter_ = 0xFFFF;
    t2LatchLow_ = 0xFF;
    t2Running_ = false;
    t2Fired_ = false;

    sr_ = 0;
    acr_ = 0;
    pcr_ = 0;
    ifr_ = 0;
    ier_ = 0;

    prevPsgControl_ = 0;
    lastPsgBus_ = 0;
    psgAddressLatched_ = false;
    psgState_ = PSG_INACTIVE;
    busDriven_ = false;
    prevIrqActive_ = false;
    t1IrqDelay_ = 0;
    t2IrqDelay_ = 0;
}

uint8_t VIA6522::read(uint8_t reg) {
    reg &= 0x0F;

    switch (reg) {
        case REG_ORB:
            // Reading ORB clears CB1/CB2 interrupt flags
            ifr_ &= ~(IRQ_CB1 | IRQ_CB2);
            checkIRQ();
            // Return output bits where direction is output, input where direction is input
            return (orb_ & ddrb_) | (irb_ & ~ddrb_);

        case REG_ORA:
        case REG_ORA_NH:
            // Reading ORA clears CA1/CA2 interrupt flags (unless ORA_NH)
            if (reg == REG_ORA) {
                ifr_ &= ~(IRQ_CA1 | IRQ_CA2);
                checkIRQ();
            }
            {
                // Input pins: driven by the PSG while it is in READ mode, else float high
                if (busDriven_ && psg_) ira_ = psg_->readRegister();
                uint8_t inputBits = busDriven_ ? ira_ : 0xFF;
                return (ora_ & ddra_) | (inputBits & ~ddra_);
            }

        case REG_DDRB:
            return ddrb_;

        case REG_DDRA:
            return ddra_;

        case REG_T1CL:
            // Reading T1CL clears T1 interrupt flag
            ifr_ &= ~IRQ_T1;
            checkIRQ();
            return t1Counter_ & 0xFF;

        case REG_T1CH:
            return (t1Counter_ >> 8) & 0xFF;

        case REG_T1LL:
            return t1Latch_ & 0xFF;

        case REG_T1LH:
            return (t1Latch_ >> 8) & 0xFF;

        case REG_T2CL:
            // Reading T2CL clears T2 interrupt flag
            ifr_ &= ~IRQ_T2;
            checkIRQ();
            return t2Counter_ & 0xFF;

        case REG_T2CH:
            return (t2Counter_ >> 8) & 0xFF;

        case REG_SR:
            return sr_;

        case REG_ACR:
            return acr_;

        case REG_PCR:
            return pcr_;

        case REG_IFR:
            // Bit 7 is set if any enabled interrupt flag is set
            if (ifr_ & ier_ & 0x7F) {
                return ifr_ | IRQ_ANY;
            }
            return ifr_;

        case REG_IER:
            // Reading IER returns bit 7 = 1
            return ier_ | 0x80;

        default:
            return 0xFF;
    }
}

void VIA6522::write(uint8_t reg, uint8_t value) {
    reg &= 0x0F;

    switch (reg) {
        case REG_ORB:
            orb_ = value;
            // Writing ORB clears CB1/CB2 interrupt flags
            ifr_ &= ~(IRQ_CB1 | IRQ_CB2);
            checkIRQ();
            updatePSG();
            break;

        case REG_ORA:
        case REG_ORA_NH:
            ora_ = value;
            // Writing ORA clears CA1/CA2 interrupt flags (unless ORA_NH)
            if (reg == REG_ORA) {
                ifr_ &= ~(IRQ_CA1 | IRQ_CA2);
                checkIRQ();
            }
            updatePSG();
            break;

        case REG_DDRB:
            // The direction decides which lines the VIA drives, so it moves
            // the PSG's control lines as surely as ORB does.
            ddrb_ = value;
            updatePSG();
            break;

        case REG_DDRA:
            ddra_ = value;
            updatePSG();
            break;

        case REG_T1CL:
        case REG_T1LL:
            t1Latch_ = (t1Latch_ & 0xFF00) | value;
            break;

        case REG_T1CH:
            t1Latch_ = (t1Latch_ & 0x00FF) | (value << 8);
            // Writing T1CH also loads counter and clears interrupt
            // Real 6522 period = latch + 2 (Rockwell datasheet Fig.16)
            // Counter starts at latch + 1 (the +2nd cycle is the write itself)
            t1Counter_ = static_cast<int32_t>(t1Latch_) + 1;
            t1Running_ = true;
            t1Fired_ = false;
            ifr_ &= ~IRQ_T1;
            checkIRQ();
            break;

        case REG_T1LH:
            t1Latch_ = (t1Latch_ & 0x00FF) | (value << 8);
            // Just updates latch, doesn't affect counter
            ifr_ &= ~IRQ_T1;  // Clears interrupt flag
            checkIRQ();
            break;

        case REG_T2CL:
            t2LatchLow_ = value;
            break;

        case REG_T2CH:
            t2Counter_ = t2LatchLow_ | (value << 8);
            t2Counter_ += 1;  // +1 extra cycle for proper period (total = latch + 2)
            t2Running_ = true;
            t2Fired_ = false;
            ifr_ &= ~IRQ_T2;
            checkIRQ();
            break;

        case REG_SR:
            sr_ = value;
            break;

        case REG_ACR:
            // Only a write to T1CH re-arms a one-shot timer; changing the
            // mode does not.
            acr_ = value;
            break;

        case REG_PCR:
            pcr_ = value;
            break;

        case REG_IFR:
            // Writing 1 to a bit clears that interrupt flag
            ifr_ &= ~(value & 0x7F);
            checkIRQ();
            break;

        case REG_IER:
            // Bit 7 controls set/clear mode
            if (value & 0x80) {
                // Set bits
                ier_ |= (value & 0x7F);
            } else {
                // Clear bits
                ier_ &= ~(value & 0x7F);
            }
            checkIRQ();
            break;
    }
}

void VIA6522::update(int cycles) {
    if (cycles <= 0) return;

    uint32_t cyclesToProcess = static_cast<uint32_t>(cycles);

    // Update Timer 1. The counter always decrements, which is how software
    // detects the card; it interrupts only once T1CH has been written.
    // Free-running, a period is latch + 2 cycles: down to zero, one cycle at
    // $FFFF (-1 here), then the reload. One-shot, it carries on down from
    // $FFFF and interrupts only the first time.
    const int64_t k = cyclesToProcess;
    if (t1Counter_ >= 0 && k <= t1Counter_) {
        t1Counter_ -= static_cast<int32_t>(k);
    } else {
        int64_t remaining = k;
        if (t1Counter_ >= 0) {
            remaining -= static_cast<int64_t>(t1Counter_) + 1;
            timer1Underflow();
        }
        // The counter is now at $FFFF with `remaining` cycles still to run.
        if (remaining == 0) {
            t1Counter_ = -1;
        } else if (acr_ & 0x40) {
            const int64_t period = static_cast<int64_t>(t1Latch_) + 2;
            if (remaining >= period) {
                timer1Underflow();
                remaining %= period;
            }
            t1Counter_ = (remaining == 0)
                ? -1
                : static_cast<int32_t>(t1Latch_ - (remaining - 1));
        } else {
            t1Counter_ = static_cast<int32_t>(0xFFFF - (remaining % 0x10000));
        }
    }

    // Update Timer 2
    // Timer 2 also always decrements (in timed mode)
    if (!(acr_ & 0x20)) {  // Timed mode (not pulse counting)
        if (cyclesToProcess > t2Counter_) {
            // Timer 2 underflowed
            uint32_t overflow = cyclesToProcess - t2Counter_ - 1;

            if (t2Running_ && !t2Fired_) {
                ifr_ |= IRQ_T2;
                checkIRQ();
                t2Fired_ = true;
            }
            // Timer 2 is one-shot only - wraps but doesn't reload or re-fire
            t2Counter_ = static_cast<uint16_t>(0xFFFF - (overflow % 0x10000));
        } else {
            t2Counter_ -= static_cast<uint16_t>(cyclesToProcess);
        }
    }
}

void VIA6522::timer1Underflow() {
    if (!t1Running_) return;
    if (acr_ & 0x40) {
        ifr_ |= IRQ_T1;  // Free-running: every underflow
    } else if (!t1Fired_) {
        ifr_ |= IRQ_T1;  // One-shot: the first underflow after T1CH
        t1Fired_ = true;
    } else {
        return;
    }
    checkIRQ();
}

void VIA6522::updatePSG() {
    if (!psg_) return;

    // Port B drives the PSG's BC1 (bit 0), BDIR (bit 1) and RESET (bit 2,
    // active low), and port A its data bus. A line the VIA is not driving
    // reads as low here. The AY decodes all of it as levels, not edges: while
    // RESET is low it is held reset and ignores everything; while LATCH or
    // WRITE is asserted it follows the bus, so changing the bus (or the
    // direction register) during one repeats the operation with the new value.
    const uint8_t control = orb_ & ddrb_ & 0x07;
    const uint8_t bus = ora_ & ddra_;
    const bool modeChanged = control != prevPsgControl_;
    const bool busChanged = bus != lastPsgBus_;
    const bool wasReset = (prevPsgControl_ & 0x04) == 0;
    prevPsgControl_ = control;
    lastPsgBus_ = bus;

    if ((control & 0x04) == 0) {
        if (!wasReset) {
            psg_->reset();
            if (viaDebugLogging_) {
                debugLog("VIA%d: PSG RESET asserted", viaId_);
            }
        }
        psgState_ = PSG_INACTIVE;
        busDriven_ = false;
        return;
    }

    if (!modeChanged && !busChanged) return;

    if (viaDebugLogging_) {
        debugLog("VIA%d: ctrl %d ORA=0x%X DDRA=0x%X", viaId_, control, ora_, ddra_);
    }

    switch (control & 0x03) {
        case 0x00:
            psgState_ = PSG_INACTIVE;
            busDriven_ = false;
            break;

        case 0x03:
            psgState_ = PSG_LATCH;
            busDriven_ = false;
            // The 8910's upper address nibble is mask-programmed to 0, so an
            // address with any of bits 4-7 set deselects the chip.
            if (bus <= 0x0F) {
                psg_->setRegisterAddress(bus);
                psgAddressLatched_ = true;
            } else {
                psgAddressLatched_ = false;
                if (viaDebugLogging_) {
                    debugLog("VIA: Deselected by address 0x%X", bus);
                }
            }
            break;

        case 0x02:
            psgState_ = PSG_WRITE;
            busDriven_ = false;
            if (psgAddressLatched_) {
                psg_->writeRegister(bus);
            } else if (viaDebugLogging_) {
                debugLog("VIA: Write ignored - chip not selected, data=0x%X", bus);
            }
            break;

        case 0x01:
            psgState_ = PSG_READ;
            busDriven_ = psgAddressLatched_;
            if (busDriven_) ira_ = psg_->readRegister();
            break;
    }
}

void VIA6522::checkIRQ() {
    bool irqActive = (ifr_ & ier_ & 0x7F) != 0;

    // Only trigger callback on transition from inactive to active
    // This prevents multiple IRQ assertions during a single interrupt handler
    if (irqActive && !prevIrqActive_) {
        if (irqCallback_) {
            irqCallback_();
        }
    }

    prevIrqActive_ = irqActive;
}

size_t VIA6522::exportState(uint8_t* buffer) const {
    size_t offset = 0;

    // Port registers
    buffer[offset++] = ora_;
    buffer[offset++] = orb_;
    buffer[offset++] = ddra_;
    buffer[offset++] = ddrb_;
    buffer[offset++] = ira_;
    buffer[offset++] = irb_;

    // Timer 1
    buffer[offset++] = static_cast<uint8_t>(t1Counter_ & 0xFF);
    buffer[offset++] = static_cast<uint8_t>((t1Counter_ >> 8) & 0xFF);
    buffer[offset++] = t1Latch_ & 0xFF;
    buffer[offset++] = (t1Latch_ >> 8) & 0xFF;
    buffer[offset++] = (t1Running_ ? 1 : 0) | (t1Fired_ ? 2 : 0);

    // Timer 2
    buffer[offset++] = t2Counter_ & 0xFF;
    buffer[offset++] = (t2Counter_ >> 8) & 0xFF;
    buffer[offset++] = t2LatchLow_;
    buffer[offset++] = (t2Running_ ? 1 : 0) | (t2Fired_ ? 2 : 0);

    // Control registers
    buffer[offset++] = sr_;
    buffer[offset++] = acr_;
    buffer[offset++] = pcr_;
    buffer[offset++] = ifr_;
    buffer[offset++] = ier_;

    // PSG control state
    buffer[offset++] = prevPsgControl_;
    buffer[offset++] = psgAddressLatched_ ? 1 : 0;
    buffer[offset++] = static_cast<uint8_t>(psgState_);
    buffer[offset++] = busDriven_ ? 1 : 0;
    buffer[offset++] = lastPsgBus_;
    buffer[offset++] = (t1Counter_ < 0) ? 1 : 0;

    // Pad to STATE_SIZE for consistent serialization
    while (offset < STATE_SIZE) {
        buffer[offset++] = 0;
    }

    return offset;  // Exactly STATE_SIZE bytes
}

void VIA6522::importState(const uint8_t* buffer) {
    size_t offset = 0;

    // Port registers
    ora_ = buffer[offset++];
    orb_ = buffer[offset++];
    ddra_ = buffer[offset++];
    ddrb_ = buffer[offset++];
    ira_ = buffer[offset++];
    irb_ = buffer[offset++];

    // Timer 1
    t1Counter_ = buffer[offset] | (buffer[offset + 1] << 8);
    offset += 2;
    t1Latch_ = buffer[offset] | (buffer[offset + 1] << 8);
    offset += 2;
    uint8_t t1Flags = buffer[offset++];
    t1Running_ = (t1Flags & 1) != 0;
    t1Fired_ = (t1Flags & 2) != 0;

    // Timer 2
    t2Counter_ = buffer[offset] | (buffer[offset + 1] << 8);
    offset += 2;
    t2LatchLow_ = buffer[offset++];
    uint8_t t2Flags = buffer[offset++];
    t2Running_ = (t2Flags & 1) != 0;
    t2Fired_ = (t2Flags & 2) != 0;

    // Control registers
    sr_ = buffer[offset++];
    acr_ = buffer[offset++];
    pcr_ = buffer[offset++];
    ifr_ = buffer[offset++];
    ier_ = buffer[offset++];

    // PSG control state
    prevPsgControl_ = buffer[offset++];
    psgAddressLatched_ = buffer[offset++] != 0;
    psgState_ = static_cast<PsgState>(buffer[offset++]);
    busDriven_ = buffer[offset++] != 0;
    lastPsgBus_ = buffer[offset++];
    if (buffer[offset++] & 1) t1Counter_ = -1;  // Older states pad this with 0

    // Restore IRQ state
    prevIrqActive_ = (ifr_ & ier_ & 0x7F) != 0;
    t1IrqDelay_ = 0;
    t2IrqDelay_ = 0;
}

} // namespace a2e
