#include "mouse_card.hpp"
#include "roms.cpp" // For embedded ROM data
#include "../types.hpp"
#include <algorithm>
#include <cstring>

namespace a2e {

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

// Firmware entry point offsets within ROM page 0
// These are the target addresses stored in the entry point table at $Cn12-$Cn20
static constexpr uint8_t ENTRY_BOOT    = 0x00;  // Card scan / boot entry
static constexpr uint8_t ENTRY_INIT    = 0x08;  // InitMouse
static constexpr uint8_t ENTRY_COMMON  = 0x20;  // Common init (target of $00 and $08)
static constexpr uint8_t ENTRY_POS     = 0x48;  // PosMouse
static constexpr uint8_t ENTRY_CLAMP   = 0x53;  // ClampMouse
static constexpr uint8_t ENTRY_SERV    = 0x9B;  // ServeMouse
static constexpr uint8_t ENTRY_SET     = 0xB3;  // SetMouse
static constexpr uint8_t ENTRY_READ    = 0xC0;  // ReadMouse
static constexpr uint8_t ENTRY_CLEAR   = 0xDD;  // ClearMouse
static constexpr uint8_t ENTRY_HOME    = 0xE6;  // HomeMouse

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
    mode_ = 0;

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

void MouseCard::setFirmwareCallbacks(
    IsOpcodeFetchCB isOpcodeFetch,
    MemReadCB memRead, MemWriteCB memWrite,
    GetRegCB getA, GetRegCB getP,
    SetRegCB setA, SetRegCB setX, SetRegCB setY, SetRegCB setP)
{
    isOpcodeFetch_ = std::move(isOpcodeFetch);
    memRead_ = std::move(memRead);
    memWrite_ = std::move(memWrite);
    getRegA_ = std::move(getA);
    getRegP_ = std::move(getP);
    setRegA_ = std::move(setA);
    setRegX_ = std::move(setX);
    setRegY_ = std::move(setY);
    setRegP_ = std::move(setP);
}

// ============================================================================
// PIA Register Access
// ============================================================================

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
                orb_ = value;
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

// ============================================================================
// Firmware Entry Point Interception
// ============================================================================

uint8_t MouseCard::readROM(uint8_t offset) {
    // Intercept ALL opcode fetches from the slot ROM.
    //
    // We detect opcode fetch by checking cpu_->isInstructionComplete(),
    // which returns true when cycleCount_ == 0 (before the instruction
    // begins executing). During operand reads, cycleCount_ > 0.
    //
    // For known entry points (page 0): perform the mouse operation and
    // return RTS ($60) so the caller gets results immediately.
    //
    // For any other opcode fetch: return RTS to prevent the firmware's
    // MC68705 communication code from executing (since we don't emulate
    // the MC68705 MCU, the firmware would hang polling for responses).
    //
    // Data reads (card ID bytes, entry point table) are unaffected since
    // they occur during instruction execution when cycleCount_ > 0.
    if (isOpcodeFetch_ && isOpcodeFetch_()) {
        // Try to handle as a known firmware entry point (page 0 only)
        if ((orb_ & 0x0E) == 0) {
            handleFirmwareCall(offset);
        }
        // Return RTS for ALL opcode fetches - prevents firmware from
        // executing MC68705 protocol code that would hang
        return 0x60;
    }

    // Data/operand reads: return real ROM bytes with page banking
    // PIA Port B bits 1-3 select which 256-byte page of the 2KB ROM
    uint16_t romBank = static_cast<uint16_t>(orb_ & 0x0E) << 7;
    uint16_t romOffset = romBank | offset;
    if (rom_ && romOffset < romSize_) {
        return rom_[romOffset];
    }
    return 0xFF;
}

bool MouseCard::handleFirmwareCall(uint8_t offset) {
    // Only handle if firmware callbacks are wired
    if (!memRead_ || !memWrite_) return false;

    switch (offset) {
        // Card scan / boot entry ($Cn00) and common init target ($Cn20)
        // The system JSRs to $Cn00 during card scanning; the firmware at $00
        // branches to $20 for PIA/MC6805 init. We intercept both to prevent
        // the firmware from trying to communicate with the MC68705.
        case ENTRY_BOOT:
        case ENTRY_COMMON:
            lastCommand_ = 0x50;
            fwInitMouse();
            return true;

        case ENTRY_INIT:  lastCommand_ = 0x50; fwInitMouse();  return true;
        case ENTRY_SET:   lastCommand_ = 0x00; fwSetMouse();   return true;
        case ENTRY_SERV:  lastCommand_ = 0x20; fwServeMouse(); return true;
        case ENTRY_READ:  lastCommand_ = 0x10; fwReadMouse();  return true;
        case ENTRY_CLEAR: lastCommand_ = 0x30; fwClearMouse(); return true;
        case ENTRY_POS:   lastCommand_ = 0x40; fwPosMouse();   return true;
        case ENTRY_CLAMP: lastCommand_ = 0x60; fwClampMouse(); return true;
        case ENTRY_HOME:  lastCommand_ = 0x70; fwHomeMouse();  return true;
        default: return false;
    }
}

void MouseCard::fwInitMouse() {
    // Reset clamp bounds to default
    clampMinX_ = 0;
    clampMaxX_ = 1023;
    clampMinY_ = 0;
    clampMaxY_ = 1023;

    // Clear position
    mouseX_ = 0;
    mouseY_ = 0;
    moved_ = false;
    buttonChanged_ = false;

    // Disable mouse
    mode_ = 0;

    // Clear screen holes
    writeScreenHoles();
    memWrite_(0x0678 + slotNum_, 0);   // Button status
    memWrite_(0x06F8 + slotNum_, 0);   // Reserved
    memWrite_(0x0778 + slotNum_, 0);   // Status
    memWrite_(0x07F8 + slotNum_, 0);   // Mode

    clearCarry();
}

void MouseCard::fwSetMouse() {
    // Mode in A register
    if (getRegA_) {
        mode_ = getRegA_() & 0x0F;
    }

    // Write mode to screen hole
    memWrite_(0x07F8 + slotNum_, mode_);

    // If mouse is being disabled, deassert all interrupts
    if (!(mode_ & MODE_MOUSE_ON)) {
        irqActive_ = false;
        vblInterruptPending_ = false;
        moveInterruptPending_ = false;
        buttonInterruptPending_ = false;
        cra_ &= 0x3F;
    }

    clearCarry();
}

void MouseCard::fwServeMouse() {
    // Build interrupt status byte
    uint8_t status = 0;
    if (moveInterruptPending_)   status |= STAT_INT_MOVEMENT;
    if (buttonInterruptPending_) status |= STAT_INT_BUTTON;
    if (vblInterruptPending_)    status |= STAT_INT_VBL;

    // Write to screen hole
    memWrite_(0x0778 + slotNum_, status);

    // Clear all pending interrupts and deassert IRQ
    vblInterruptPending_ = false;
    moveInterruptPending_ = false;
    buttonInterruptPending_ = false;
    irqActive_ = false;
    cra_ &= 0x3F;

    // Return: X = $Cn, Y = interrupt status
    if (setRegX_) setRegX_(0xC0 | slotNum_);
    if (setRegY_) setRegY_(status);

    clearCarry();
}

void MouseCard::fwReadMouse() {
    // Build status byte
    uint8_t status = 0;
    if (mouseButton_)  status |= STAT_CURR_BUTTON0;
    if (lastButton_)   status |= STAT_PREV_BUTTON0;
    if (moved_)        status |= STAT_MOVEMENT_SINCE_READMOUSE;
    if (moveInterruptPending_)   status |= STAT_INT_MOVEMENT;
    if (buttonInterruptPending_) status |= STAT_INT_BUTTON;
    if (vblInterruptPending_)    status |= STAT_INT_VBL;

    // Write position and status to screen holes
    writeScreenHoles();
    memWrite_(0x0678 + slotNum_, mouseButton_ ? 0x80 : 0x00);
    memWrite_(0x0778 + slotNum_, status);

    // Clear movement tracking after read
    moved_ = false;
    lastButton_ = mouseButton_;

    // Return: X = $Cn, Y = status
    if (setRegX_) setRegX_(0xC0 | slotNum_);
    if (setRegY_) setRegY_(status);

    clearCarry();
}

void MouseCard::fwClearMouse() {
    mouseX_ = 0;
    mouseY_ = 0;
    moved_ = false;

    writeScreenHoles();
    clearCarry();
}

void MouseCard::fwPosMouse() {
    // Read desired position from screen holes
    mouseX_ = static_cast<int16_t>(
        memRead_(0x0478 + slotNum_) |
        (memRead_(0x04F8 + slotNum_) << 8));
    mouseY_ = static_cast<int16_t>(
        memRead_(0x0578 + slotNum_) |
        (memRead_(0x05F8 + slotNum_) << 8));

    // Apply clamping
    if (mouseX_ < clampMinX_) mouseX_ = clampMinX_;
    if (mouseX_ > clampMaxX_) mouseX_ = clampMaxX_;
    if (mouseY_ < clampMinY_) mouseY_ = clampMinY_;
    if (mouseY_ > clampMaxY_) mouseY_ = clampMaxY_;

    clearCarry();
}

void MouseCard::fwClampMouse() {
    // A register = axis (0 = X, 1 = Y)
    uint8_t axis = 0;
    if (getRegA_) {
        axis = getRegA_();
    }

    // Clamping bounds are NOT slot-indexed
    int16_t minVal = static_cast<int16_t>(
        memRead_(0x0478) | (memRead_(0x04F8) << 8));
    int16_t maxVal = static_cast<int16_t>(
        memRead_(0x0578) | (memRead_(0x05F8) << 8));

    if (axis == 0) {
        // X axis
        clampMinX_ = minVal;
        clampMaxX_ = maxVal;
        if (mouseX_ < clampMinX_) mouseX_ = clampMinX_;
        if (mouseX_ > clampMaxX_) mouseX_ = clampMaxX_;
    } else {
        // Y axis
        clampMinY_ = minVal;
        clampMaxY_ = maxVal;
        if (mouseY_ < clampMinY_) mouseY_ = clampMinY_;
        if (mouseY_ > clampMaxY_) mouseY_ = clampMaxY_;
    }

    clearCarry();
}

void MouseCard::fwHomeMouse() {
    mouseX_ = clampMinX_;
    mouseY_ = clampMinY_;

    writeScreenHoles();
    clearCarry();
}

void MouseCard::writeScreenHoles() {
    if (!memWrite_) return;
    memWrite_(0x0478 + slotNum_, static_cast<uint8_t>(mouseX_ & 0xFF));
    memWrite_(0x04F8 + slotNum_, static_cast<uint8_t>((mouseX_ >> 8) & 0xFF));
    memWrite_(0x0578 + slotNum_, static_cast<uint8_t>(mouseY_ & 0xFF));
    memWrite_(0x05F8 + slotNum_, static_cast<uint8_t>((mouseY_ >> 8) & 0xFF));
}

void MouseCard::clearCarry() {
    if (getRegP_ && setRegP_) {
        setRegP_(getRegP_() & ~0x01);
    }
}

// ============================================================================
// Legacy PIA command processing (kept for reference but no longer primary path)
// ============================================================================

void MouseCard::processCommand(uint8_t cmd) {
    // This was the original MC6805 protocol emulation path.
    // With firmware entry point interception, this is no longer called
    // during normal operation, but kept for completeness.
    lastCommand_ = cmd;
    responseState_ = 0;
}

// ============================================================================
// VBL Interrupt Generation
// ============================================================================

void MouseCard::update(int cycles) {
    if (!cycleCallback_) return;

    uint64_t totalCycles = cycleCallback_();

    // Detect VBL transition (scanline 192, start of vertical blank)
    uint64_t cycleInFrame = totalCycles % CYCLES_PER_FRAME;
    int scanline = static_cast<int>(cycleInFrame / CYCLES_PER_SCANLINE);
    bool inVBL = (scanline >= 192);

    // Detect transition into VBL
    if (inVBL && !wasInVBL_) {
        if (mode_ & MODE_INT_VBL) {
            vblInterruptPending_ = true;
        }

        // Also check for movement/button interrupts at VBL
        if ((mode_ & MODE_INT_MOVEMENT) && moved_) {
            moveInterruptPending_ = true;
        }
        if ((mode_ & MODE_INT_BUTTON) && buttonChanged_) {
            buttonInterruptPending_ = true;
            buttonChanged_ = false;
        }

        // Fire IRQ if any interrupt is pending and mouse is enabled
        if ((mode_ & MODE_MOUSE_ON) &&
            (vblInterruptPending_ || moveInterruptPending_ || buttonInterruptPending_)) {
            irqActive_ = true;
            cra_ |= 0x80;
            if (irqCallback_) {
                irqCallback_();
            }
        }
    }

    wasInVBL_ = inVBL;
}

// ============================================================================
// Mouse Input
// ============================================================================

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

uint8_t MouseCard::readExpansionROM(uint16_t offset) {
    // The mouse card does not use expansion ROM - all firmware is
    // accessed through banked slot ROM. This should not be called.
    (void)offset;
    return 0xFF;
}

// ============================================================================
// State Serialization
// ============================================================================

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
    buffer[off++] = mode_;  // Was responseState_, now mode (more useful)

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
    mode_ = buffer[off++];  // Was responseState_, now mode

    // Slot number
    slotNum_ = buffer[off++];

    // VBL tracking
    wasInVBL_ = buffer[off++] != 0;

    // Skip reserved
    off = STATE_SIZE;

    return off;
}

} // namespace a2e
