#include "mouse_card.hpp"
#include "roms.cpp" // For embedded ROM data
#include "../types.hpp"
#include <algorithm>
#include <cstring>

namespace a2e {

// Mouse commands (upper nibble of command byte)
static constexpr uint8_t CMD_SET    = 0x00;  // Set mouse mode
static constexpr uint8_t CMD_READ   = 0x10;  // Read mouse position/buttons
static constexpr uint8_t CMD_SERV   = 0x20;  // Service mouse interrupt
static constexpr uint8_t CMD_CLEAR  = 0x30;  // Clear position to 0
static constexpr uint8_t CMD_POS    = 0x40;  // Set position
static constexpr uint8_t CMD_INIT   = 0x50;  // Initialize
static constexpr uint8_t CMD_CLAMP  = 0x60;  // Set clamp bounds
static constexpr uint8_t CMD_HOME   = 0x70;  // Home mouse (upper-left)

// Mode byte bits
static constexpr uint8_t MODE_MOUSE_ON      = (1 << 0);
static constexpr uint8_t MODE_INT_MOVEMENT   = (1 << 1);
static constexpr uint8_t MODE_INT_BUTTON     = (1 << 2);
static constexpr uint8_t MODE_INT_VBL        = (1 << 3);

// Status byte bits
static constexpr uint8_t STAT_PREV_BUTTON1             = (1 << 0);
static constexpr uint8_t STAT_INT_MOVEMENT              = (1 << 1);
static constexpr uint8_t STAT_INT_BUTTON                = (1 << 2);
static constexpr uint8_t STAT_INT_VBL                   = (1 << 3);
static constexpr uint8_t STAT_CURR_BUTTON1              = (1 << 4);
static constexpr uint8_t STAT_MOVEMENT_SINCE_READMOUSE  = (1 << 5);
static constexpr uint8_t STAT_PREV_BUTTON0              = (1 << 6);
static constexpr uint8_t STAT_CURR_BUTTON0              = (1 << 7);

// PIA register select (lower 2 bits of offset)
static constexpr uint8_t PIA_PORT_A = 0;  // Port A data / DDRA
static constexpr uint8_t PIA_CRA    = 1;  // Control Register A
static constexpr uint8_t PIA_PORT_B = 2;  // Port B data / DDRB
static constexpr uint8_t PIA_CRB    = 3;  // Control Register B

MouseCard::MouseCard()
    : rom_(roms::ROM_MOUSE)
    , romSize_(roms::ROM_MOUSE_SIZE)
{
    reset();
}

void MouseCard::reset() {
    // Reset PIA
    ddra_ = 0;
    ddrb_ = 0;
    ora_ = 0;
    orb_ = 0;
    ira_ = 0;
    irb_ = 0;
    cra_ = 0;
    crb_ = 0;

    // Reset mouse state
    mouseX_ = 0;
    mouseY_ = 0;
    mouseButton_ = false;
    lastButton_ = false;
    moved_ = false;
    buttonChanged_ = false;

    // Default clamp bounds
    clampMinX_ = 0;
    clampMaxX_ = 1023;
    clampMinY_ = 0;
    clampMaxY_ = 1023;

    // Reset interrupt state
    irqActive_ = false;
    vblInterruptPending_ = false;
    moveInterruptPending_ = false;
    buttonInterruptPending_ = false;

    // Reset VBL tracking
    lastVBLCycle_ = 0;
    wasInVBL_ = false;

    // Reset protocol state
    lastCommand_ = 0;
    responseState_ = 0;
}

uint8_t MouseCard::readIO(uint8_t offset) {
    uint8_t reg = offset & 0x03;

    switch (reg) {
        case PIA_PORT_A: {
            if (cra_ & 0x04) {
                // Data register selected: return (output & DDR) | (input & ~DDR)
                uint8_t value = (ora_ & ddra_) | (ira_ & ~ddra_);
                // Reading data port clears IRQ flags (bits 7,6) in CRA
                cra_ &= 0x3F;
                updateIRQState();
                return value;
            } else {
                // DDR selected
                return ddra_;
            }
        }

        case PIA_CRA:
            return cra_;

        case PIA_PORT_B: {
            if (crb_ & 0x04) {
                // Data register selected
                uint8_t value = (orb_ & ddrb_) | (irb_ & ~ddrb_);
                // Reading data port clears IRQ flags (bits 7,6) in CRB
                crb_ &= 0x3F;
                updateIRQState();
                return value;
            } else {
                return ddrb_;
            }
        }

        case PIA_CRB:
            return crb_;
    }

    return 0xFF;
}

void MouseCard::writeIO(uint8_t offset, uint8_t value) {
    uint8_t reg = offset & 0x03;

    switch (reg) {
        case PIA_PORT_A: {
            if (cra_ & 0x04) {
                // Write to output register A
                ora_ = value;
            } else {
                // Write to DDR A
                ddra_ = value;
            }
            break;
        }

        case PIA_CRA:
            // Bits 6,7 are read-only IRQ flags
            cra_ = (cra_ & 0xC0) | (value & 0x3F);
            updateIRQState();
            break;

        case PIA_PORT_B: {
            if (crb_ & 0x04) {
                // Write to output register B
                uint8_t prevOrb = orb_;
                orb_ = value;

                // The firmware uses Port B writes to communicate commands
                // to the (emulated) 6805 microcontroller.
                // Bit 5: write strobe - rising edge clocks command data
                // Bit 4: read strobe - rising edge clocks response data
                // Bits 0-2: ROM page select

                // Detect write strobe rising edge (bit 5)
                if ((value & 0x20) && !(prevOrb & 0x20)) {
                    // Command byte is on Port A output
                    uint8_t cmdByte = ora_;
                    processCommand(cmdByte);
                }

                // Detect read strobe rising edge (bit 4)
                if ((value & 0x10) && !(prevOrb & 0x10)) {
                    // Advance to next response byte
                    responseState_++;
                }
            } else {
                // Write to DDR B
                ddrb_ = value;
            }
            break;
        }

        case PIA_CRB:
            crb_ = (crb_ & 0xC0) | (value & 0x3F);
            updateIRQState();
            break;
    }
}

uint8_t MouseCard::peekIO(uint8_t offset) const {
    uint8_t reg = offset & 0x03;
    switch (reg) {
        case PIA_PORT_A:
            if (cra_ & 0x04) {
                return (ora_ & ddra_) | (ira_ & ~ddra_);
            }
            return ddra_;
        case PIA_CRA: return cra_;
        case PIA_PORT_B:
            if (crb_ & 0x04) {
                return (orb_ & ddrb_) | (irb_ & ~ddrb_);
            }
            return ddrb_;
        case PIA_CRB: return crb_;
    }
    return 0xFF;
}

void MouseCard::processCommand(uint8_t cmd) {
    uint8_t cmdType = cmd & 0xF0;
    lastCommand_ = cmd;
    responseState_ = 0;

    switch (cmdType) {
        case CMD_READ: {
            // Build response: X low, X high, Y low, Y high, status, reserved
            // Response is placed on Port A input pins for firmware to read
            // The firmware will strobe bit 4 of Port B to clock through bytes

            // Build status byte
            uint8_t status = 0;
            if (mouseButton_)  status |= STAT_CURR_BUTTON0;
            if (lastButton_)   status |= STAT_PREV_BUTTON0;
            if (moved_)        status |= STAT_MOVEMENT_SINCE_READMOUSE;

            // Set interrupt reason bits
            if (moveInterruptPending_)   status |= STAT_INT_MOVEMENT;
            if (buttonInterruptPending_) status |= STAT_INT_BUTTON;
            if (vblInterruptPending_)    status |= STAT_INT_VBL;

            // First response byte is X low
            ira_ = static_cast<uint8_t>(mouseX_ & 0xFF);

            // Clear movement tracking
            moved_ = false;
            lastButton_ = mouseButton_;
            break;
        }

        case CMD_SERV: {
            // Service interrupt - return interrupt status and deassert IRQ
            uint8_t intStatus = 0;
            if (moveInterruptPending_)   intStatus |= STAT_INT_MOVEMENT;
            if (buttonInterruptPending_) intStatus |= STAT_INT_BUTTON;
            if (vblInterruptPending_)    intStatus |= STAT_INT_VBL;

            ira_ = intStatus;

            // Clear pending interrupts
            vblInterruptPending_ = false;
            moveInterruptPending_ = false;
            buttonInterruptPending_ = false;
            irqActive_ = false;
            break;
        }

        case CMD_SET: {
            // Set mode - lower nibble contains mode bits
            // Mode is applied when data is written
            ira_ = cmd & 0x0F;
            break;
        }

        case CMD_CLEAR:
            // Clear mouse position
            mouseX_ = 0;
            mouseY_ = 0;
            moved_ = false;
            ira_ = 0;
            break;

        case CMD_POS:
            // Position mouse - data follows
            ira_ = 0;
            break;

        case CMD_INIT:
            // Initialize - reset clamps to default
            clampMinX_ = 0;
            clampMaxX_ = 1023;
            clampMinY_ = 0;
            clampMaxY_ = 1023;
            mouseX_ = 0;
            mouseY_ = 0;
            moved_ = false;
            ira_ = 0;
            break;

        case CMD_CLAMP:
            // Set clamp bounds - data follows
            ira_ = 0;
            break;

        case CMD_HOME:
            // Home to upper-left of clamp window
            mouseX_ = clampMinX_;
            mouseY_ = clampMinY_;
            moved_ = false;
            ira_ = 0;
            break;

        default:
            ira_ = 0;
            break;
    }
}

void MouseCard::update(int cycles) {
    if (!cycleCallback_) return;

    uint64_t totalCycles = cycleCallback_();

    // Detect VBL transition (scanline 192, start of vertical blank)
    uint64_t cycleInFrame = totalCycles % CYCLES_PER_FRAME;
    int scanline = static_cast<int>(cycleInFrame / CYCLES_PER_SCANLINE);
    bool inVBL = (scanline >= 192);

    // Detect transition into VBL
    if (inVBL && !wasInVBL_) {
        // Get mode from Port A output (the firmware stores mode there after CMD_SET)
        uint8_t mode = ora_ & 0x0F;

        if (mode & MODE_INT_VBL) {
            vblInterruptPending_ = true;
        }

        // Also check for movement/button interrupts at VBL
        if ((mode & MODE_INT_MOVEMENT) && moved_) {
            moveInterruptPending_ = true;
        }
        if ((mode & MODE_INT_BUTTON) && buttonChanged_) {
            buttonInterruptPending_ = true;
            buttonChanged_ = false;
        }

        // Fire IRQ if any interrupt is pending and mouse is enabled
        if ((mode & MODE_MOUSE_ON) &&
            (vblInterruptPending_ || moveInterruptPending_ || buttonInterruptPending_)) {
            irqActive_ = true;
            // Set PIA IRQ flags (CA1/CB1 IRQ)
            cra_ |= 0x80;
            if (irqCallback_) {
                irqCallback_();
            }
        }
    }

    wasInVBL_ = inVBL;
}

void MouseCard::addDelta(int dx, int dy) {
    if (dx == 0 && dy == 0) return;

    mouseX_ += static_cast<int16_t>(dx);
    mouseY_ += static_cast<int16_t>(dy);

    // Apply clamping
    if (mouseX_ < clampMinX_) mouseX_ = clampMinX_;
    if (mouseX_ > clampMaxX_) mouseX_ = clampMaxX_;
    if (mouseY_ < clampMinY_) mouseY_ = clampMinY_;
    if (mouseY_ > clampMaxY_) mouseY_ = clampMaxY_;

    moved_ = true;
}

void MouseCard::setMouseButton(bool pressed) {
    if (pressed != mouseButton_) {
        lastButton_ = mouseButton_;
        mouseButton_ = pressed;
        buttonChanged_ = true;
    }
}

void MouseCard::updateIRQState() {
    // IRQ is active if any PIA IRQ flag is set and the corresponding
    // control register enables it
    bool piaIRQ = (cra_ & 0x80) || (crb_ & 0x80);
    if (!piaIRQ) {
        irqActive_ = false;
    }
}

uint8_t MouseCard::readROM(uint8_t offset) {
    // Slot ROM ($Cn00-$CnFF) always serves the first 256 bytes (page 0).
    // This contains the card identification bytes and entry point table.
    if (rom_ && offset < romSize_) {
        return rom_[offset];
    }
    return 0xFF;
}

uint8_t MouseCard::readExpansionROM(uint16_t offset) {
    // Expansion ROM ($C800-$CFFF) maps the full 2KB ROM directly.
    // The window is exactly 2KB, matching the ROM size.
    if (rom_ && offset < romSize_) {
        return rom_[offset];
    }
    return 0xFF;
}

size_t MouseCard::serialize(uint8_t* buffer, size_t maxSize) const {
    if (maxSize < STATE_SIZE) return 0;

    size_t off = 0;

    // PIA state (8 bytes)
    buffer[off++] = ddra_;
    buffer[off++] = ddrb_;
    buffer[off++] = ora_;
    buffer[off++] = orb_;
    buffer[off++] = ira_;
    buffer[off++] = irb_;
    buffer[off++] = cra_;
    buffer[off++] = crb_;

    // Mouse position (8 bytes)
    buffer[off++] = static_cast<uint8_t>(mouseX_ & 0xFF);
    buffer[off++] = static_cast<uint8_t>((mouseX_ >> 8) & 0xFF);
    buffer[off++] = static_cast<uint8_t>(mouseY_ & 0xFF);
    buffer[off++] = static_cast<uint8_t>((mouseY_ >> 8) & 0xFF);

    // Clamp bounds (8 bytes)
    buffer[off++] = static_cast<uint8_t>(clampMinX_ & 0xFF);
    buffer[off++] = static_cast<uint8_t>((clampMinX_ >> 8) & 0xFF);
    buffer[off++] = static_cast<uint8_t>(clampMaxX_ & 0xFF);
    buffer[off++] = static_cast<uint8_t>((clampMaxX_ >> 8) & 0xFF);
    buffer[off++] = static_cast<uint8_t>(clampMinY_ & 0xFF);
    buffer[off++] = static_cast<uint8_t>((clampMinY_ >> 8) & 0xFF);
    buffer[off++] = static_cast<uint8_t>(clampMaxY_ & 0xFF);
    buffer[off++] = static_cast<uint8_t>((clampMaxY_ >> 8) & 0xFF);

    // Flags (4 bytes)
    buffer[off++] = (mouseButton_ ? 1 : 0) | (lastButton_ ? 2 : 0)
                  | (moved_ ? 4 : 0) | (buttonChanged_ ? 8 : 0);
    buffer[off++] = (irqActive_ ? 1 : 0) | (vblInterruptPending_ ? 2 : 0)
                  | (moveInterruptPending_ ? 4 : 0) | (buttonInterruptPending_ ? 8 : 0);
    buffer[off++] = lastCommand_;
    buffer[off++] = responseState_;

    // Slot number (1 byte)
    buffer[off++] = slotNum_;

    // VBL tracking (1 byte)
    buffer[off++] = wasInVBL_ ? 1 : 0;

    // Reserved (6 bytes to reach STATE_SIZE=36)
    while (off < STATE_SIZE) {
        buffer[off++] = 0;
    }

    return off;
}

size_t MouseCard::deserialize(const uint8_t* buffer, size_t size) {
    if (size < STATE_SIZE) return 0;

    size_t off = 0;

    // PIA state
    ddra_ = buffer[off++];
    ddrb_ = buffer[off++];
    ora_ = buffer[off++];
    orb_ = buffer[off++];
    ira_ = buffer[off++];
    irb_ = buffer[off++];
    cra_ = buffer[off++];
    crb_ = buffer[off++];

    // Mouse position
    mouseX_ = static_cast<int16_t>(buffer[off] | (buffer[off + 1] << 8));
    off += 2;
    mouseY_ = static_cast<int16_t>(buffer[off] | (buffer[off + 1] << 8));
    off += 2;

    // Clamp bounds
    clampMinX_ = static_cast<int16_t>(buffer[off] | (buffer[off + 1] << 8));
    off += 2;
    clampMaxX_ = static_cast<int16_t>(buffer[off] | (buffer[off + 1] << 8));
    off += 2;
    clampMinY_ = static_cast<int16_t>(buffer[off] | (buffer[off + 1] << 8));
    off += 2;
    clampMaxY_ = static_cast<int16_t>(buffer[off] | (buffer[off + 1] << 8));
    off += 2;

    // Flags
    uint8_t flags1 = buffer[off++];
    mouseButton_ = (flags1 & 1) != 0;
    lastButton_ = (flags1 & 2) != 0;
    moved_ = (flags1 & 4) != 0;
    buttonChanged_ = (flags1 & 8) != 0;

    uint8_t flags2 = buffer[off++];
    irqActive_ = (flags2 & 1) != 0;
    vblInterruptPending_ = (flags2 & 2) != 0;
    moveInterruptPending_ = (flags2 & 4) != 0;
    buttonInterruptPending_ = (flags2 & 8) != 0;

    lastCommand_ = buffer[off++];
    responseState_ = buffer[off++];

    // Slot number
    slotNum_ = buffer[off++];

    // VBL tracking
    wasInVBL_ = buffer[off++] != 0;

    // Skip reserved
    off = STATE_SIZE;

    return off;
}

} // namespace a2e
