#include "via6522.hpp"
#include "ay8910.hpp"

namespace a2e {

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
            return (ora_ & ddra_) | (ira_ & ~ddra_);

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
            ddrb_ = value;
            break;

        case REG_DDRA:
            ddra_ = value;
            break;

        case REG_T1CL:
        case REG_T1LL:
            t1Latch_ = (t1Latch_ & 0xFF00) | value;
            break;

        case REG_T1CH:
            t1Latch_ = (t1Latch_ & 0x00FF) | (value << 8);
            // Writing T1CH also loads counter and clears interrupt
            t1Counter_ = t1Latch_;
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
            t2Running_ = true;
            t2Fired_ = false;
            ifr_ &= ~IRQ_T2;
            checkIRQ();
            break;

        case REG_SR:
            sr_ = value;
            break;

        case REG_ACR:
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
    // Update Timer 1
    if (t1Running_) {
        if (cycles >= t1Counter_) {
            // Timer 1 underflowed
            if (!t1Fired_) {
                ifr_ |= IRQ_T1;
                checkIRQ();
                t1Fired_ = true;
            }

            // Check ACR for timer mode
            if (acr_ & 0x40) {
                // Free-running mode - reload from latch
                int remaining = cycles - t1Counter_ - 1;
                t1Counter_ = t1Latch_;
                // Handle multiple wraparounds
                while (remaining > t1Latch_ && t1Latch_ > 0) {
                    remaining -= (t1Latch_ + 1);
                }
                t1Counter_ = t1Latch_ - remaining;
                t1Fired_ = false;  // Can fire again
            } else {
                // One-shot mode - counter keeps running but no more interrupts
                t1Counter_ = 0xFFFF - (cycles - t1Counter_ - 1);
                // In one-shot mode, timer continues to count down but doesn't
                // generate more interrupts until T1CH is written again
            }
        } else {
            t1Counter_ -= cycles;
        }
    }

    // Update Timer 2
    if (t2Running_) {
        // Timer 2 only operates in timed mode (pulse counting not implemented)
        if (!(acr_ & 0x20)) {  // Timed mode
            if (cycles >= t2Counter_) {
                // Timer 2 underflowed
                if (!t2Fired_) {
                    ifr_ |= IRQ_T2;
                    checkIRQ();
                    t2Fired_ = true;
                }
                // Timer 2 is one-shot only, continues counting but no reload
                t2Counter_ = 0xFFFF - (cycles - t2Counter_ - 1);
            } else {
                t2Counter_ -= cycles;
            }
        }
    }
}

void VIA6522::updatePSG() {
    if (!psg_) return;

    // Port B controls the PSG via BC1 and BDIR lines
    // BC1 = bit 0, BDIR = bit 1 of ORB
    // Note: Only look at bits that are configured as outputs
    uint8_t control = orb_ & ddrb_ & 0x07;

    // Only perform PSG operations on TRANSITIONS to active states
    // This prevents spurious writes when ORA is changed while control is held
    if (control == prevPsgControl_) {
        return;  // No change in control state
    }

    uint8_t prevControl = prevPsgControl_;
    prevPsgControl_ = control;

    // PSG control modes (accent on transitions):
    // BC1=0, BDIR=0 (0x00/0x04): Inactive
    // BC1=1, BDIR=0 (0x01/0x05): Read from PSG
    // BC1=0, BDIR=1 (0x02/0x06): Write to PSG register
    // BC1=1, BDIR=1 (0x03/0x07): Latch address

    // Latch address: transition TO 0x03 or 0x07
    if ((control == 0x03 || control == 0x07) &&
        (prevControl != 0x03 && prevControl != 0x07)) {
        psg_->setRegisterAddress(ora_ & ddra_);
    }
    // Write data: transition TO 0x02 or 0x06
    else if ((control == 0x02 || control == 0x06) &&
             (prevControl != 0x02 && prevControl != 0x06)) {
        psg_->writeRegister(ora_ & ddra_);
    }
    // Read: transition TO 0x01 or 0x05
    else if ((control == 0x01 || control == 0x05) &&
             (prevControl != 0x01 && prevControl != 0x05)) {
        ira_ = psg_->readRegister();
    }
}

void VIA6522::checkIRQ() {
    if ((ifr_ & ier_ & 0x7F) != 0) {
        // IRQ is active
        if (irqCallback_) {
            irqCallback_();
        }
    }
}

} // namespace a2e
