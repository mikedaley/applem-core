#include "via6522.hpp"
#include "ay8910.hpp"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

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
    psgAddressLatched_ = false;
    prevIrqActive_ = false;
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
    if (cycles <= 0) return;

    uint32_t cyclesToProcess = static_cast<uint32_t>(cycles);

    // Update Timer 1
    if (t1Running_) {
        if (cyclesToProcess > t1Counter_) {
            // Timer 1 underflowed
            uint32_t overflow = cyclesToProcess - t1Counter_ - 1;

            if (!t1Fired_) {
                ifr_ |= IRQ_T1;
                checkIRQ();
                t1Fired_ = true;
            }

            // Check ACR for timer mode
            if (acr_ & 0x40) {
                // Free-running mode - reload from latch and continue
                if (t1Latch_ > 0) {
                    // Handle potential multiple wraparounds
                    overflow = overflow % (static_cast<uint32_t>(t1Latch_) + 1);
                    t1Counter_ = t1Latch_ - static_cast<uint16_t>(overflow);
                } else {
                    t1Counter_ = 0;
                }
                t1Fired_ = false;  // Can fire again next time
            } else {
                // One-shot mode - counter wraps but doesn't reload or re-fire
                // Timer continues running (wraps around) but no more interrupts
                t1Counter_ = static_cast<uint16_t>(0xFFFF - (overflow % 0x10000));
            }
        } else {
            t1Counter_ -= static_cast<uint16_t>(cyclesToProcess);
        }
    }

    // Update Timer 2
    if (t2Running_) {
        // Timer 2 only operates in timed mode (pulse counting not implemented)
        if (!(acr_ & 0x20)) {  // Timed mode
            if (cyclesToProcess > t2Counter_) {
                // Timer 2 underflowed
                uint32_t overflow = cyclesToProcess - t2Counter_ - 1;

                if (!t2Fired_) {
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
}

void VIA6522::updatePSG() {
    if (!psg_) return;

    // Port B controls the PSG via BC1, BDIR, and RESET lines
    // BC1 = bit 0, BDIR = bit 1, ~RESET = bit 2 (active low)
    // Note: Only look at bits that are configured as outputs
    uint8_t control = orb_ & ddrb_ & 0x07;

    // Check for PSG reset - bit 2 going low resets the PSG
    // This is used by software to silence the PSG when done playing
    bool resetActive = (control & 0x04) == 0;  // Bit 2 = 0 means reset active
    bool wasResetActive = (prevPsgControl_ & 0x04) == 0;

    if (resetActive && !wasResetActive) {
        // Reset just became active - reset the PSG
        psg_->reset();
#ifdef __EMSCRIPTEN__
        if (viaDebugLogging_) {
            EM_ASM({
                console.log("VIA" + $0 + ": PSG RESET asserted");
            }, viaId_);
        }
#endif
    }

    // Only perform PSG operations on state TRANSITIONS
    if (control == prevPsgControl_) {
        return;  // No change in control state
    }

    uint8_t prevControl = prevPsgControl_;
    prevPsgControl_ = control;

#ifdef __EMSCRIPTEN__
    if (viaDebugLogging_) {
        EM_ASM({
            console.log("VIA" + $4 + ": ctrl " + $0 + "->" + $1 + " ORA=0x" + $2.toString(16) + " DDRA=0x" + $3.toString(16));
        }, prevControl, control, ora_, ddra_, viaId_);
    }
#endif

    // PSG control modes:
    // 0x00/0x04: Inactive (BDIR=0, BC1=0)
    // 0x01/0x05: Read from PSG (BDIR=0, BC1=1)
    // 0x02/0x06: Write to PSG register (BDIR=1, BC1=0)
    // 0x03/0x07: Latch address (BDIR=1, BC1=1)

    bool isInactive = (control == 0x00 || control == 0x04);

    // Transitioning to inactive - no operation needed
    if (isInactive) {
        return;
    }

    // Latch address - trigger on ANY transition to latch state
    // Software may do rapid latch→write→latch→write sequences without
    // going to inactive between operations
    if (control == 0x03 || control == 0x07) {
        uint8_t addr = ora_ & ddra_;
        if (addr <= 0x0F) {
            psg_->setRegisterAddress(addr);
            psgAddressLatched_ = true;  // Mark that a valid address was latched
        } else {
            psgAddressLatched_ = false;  // Invalid address - reject subsequent writes
#ifdef __EMSCRIPTEN__
            if (viaDebugLogging_) {
                EM_ASM({
                    console.log("VIA: Rejected invalid address 0x" + $0.toString(16).toUpperCase());
                }, addr);
            }
#endif
        }
        return;
    }

    // Write data - trigger on ANY transition to write state
    // Only write if a valid address was previously latched (AppleWin behavior)
    if (control == 0x02 || control == 0x06) {
        if (psgAddressLatched_) {
            psg_->writeRegister(ora_ & ddra_);
        } else {
#ifdef __EMSCRIPTEN__
            if (viaDebugLogging_) {
                EM_ASM({
                    console.log("VIA: Write rejected - no address latched, data=0x" + $0.toString(16).toUpperCase());
                }, ora_ & ddra_);
            }
#endif
        }
        return;
    }

    // Read data - trigger on ANY transition to read state
    if (control == 0x01 || control == 0x05) {
        if (psgAddressLatched_) {
            ira_ = psg_->readRegister();
        }
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
    buffer[offset++] = t1Counter_ & 0xFF;
    buffer[offset++] = (t1Counter_ >> 8) & 0xFF;
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

    return offset;  // Should be ~23 bytes
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

    // Restore IRQ state
    prevIrqActive_ = (ifr_ & ier_ & 0x7F) != 0;
}

} // namespace a2e
