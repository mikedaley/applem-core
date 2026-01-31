#pragma once

#include "expansion_card.hpp"
#include <cstdint>

namespace a2e {

/**
 * MouseCard - Apple Mouse Interface Card emulation
 *
 * Emulates the Apple Mouse Interface Card (342-0270-C ROM) which uses
 * a Motorola MC6821 PIA (Peripheral Interface Adapter) paired with an
 * MC6805 microcontroller to interface a mouse to the Apple II.
 *
 * In this emulation, the MC6805 protocol is emulated directly rather
 * than executing 6805 instructions. The firmware communicates mouse
 * state through "screen holes" - unused bytes in the text screen memory:
 *
 *   $0478+n: Mouse X position low byte
 *   $04F8+n: Mouse X position high byte
 *   $0578+n: Mouse Y position low byte
 *   $05F8+n: Mouse Y position high byte
 *   $0678+n: Button 0/1 state + interrupt flags
 *   $06F8+n: Reserved
 *   $0778+n: Status byte
 *   $07F8+n: Mode byte
 *
 * The 6821 PIA provides 4 registers at I/O offsets 0-3:
 *   Offset 0: Port A data / DDRA (selected by CRA bit 2)
 *   Offset 1: Control Register A (CRA)
 *   Offset 2: Port B data / DDRB (selected by CRB bit 2)
 *   Offset 3: Control Register B (CRB)
 *
 * The firmware uses PIA Port B to send commands to the 6805 and
 * Port A to receive responses. The 6805 protocol commands include
 * reading mouse position, button state, and setting operating modes.
 *
 * VBL (Vertical Blank) interrupt generation:
 * When the mouse mode has bit 3 set (VBL interrupt enable), an IRQ
 * is generated at the start of each vertical blanking period.
 *
 * Available in slots 2, 4, and 7.
 */
class MouseCard : public ExpansionCard {
public:
    MouseCard();
    ~MouseCard() override = default;

    // Delete copy
    MouseCard(const MouseCard&) = delete;
    MouseCard& operator=(const MouseCard&) = delete;

    // Allow move
    MouseCard(MouseCard&&) = default;
    MouseCard& operator=(MouseCard&&) = default;

    // ===== ExpansionCard Interface =====

    // I/O space ($C0n0-$C0nF)
    uint8_t readIO(uint8_t offset) override;
    void writeIO(uint8_t offset, uint8_t value) override;
    uint8_t peekIO(uint8_t offset) const override;

    // ROM space ($Cn00-$CnFF)
    uint8_t readROM(uint8_t offset) override;
    bool hasROM() const override { return true; }

    // Expansion ROM ($C800-$CFFF)
    bool hasExpansionROM() const override { return true; }
    uint8_t readExpansionROM(uint16_t offset) override;

    void reset() override;
    void update(int cycles) override;

    const char* getName() const override { return "Mouse"; }
    uint8_t getPreferredSlot() const override { return 4; }

    // IRQ support
    void setIRQCallback(IRQCallback callback) override { irqCallback_ = callback; }
    void setCycleCallback(CycleCallback callback) override { cycleCallback_ = callback; }
    bool isIRQActive() const override { return irqActive_; }

    // State serialization
    static constexpr size_t STATE_SIZE = 36;
    size_t getStateSize() const override { return STATE_SIZE; }
    size_t serialize(uint8_t* buffer, size_t maxSize) const override;
    size_t deserialize(const uint8_t* buffer, size_t size) override;

    // Mouse input from browser
    void setSlotNumber(uint8_t slot) { slotNum_ = slot; }
    void addDelta(int dx, int dy);
    void setMouseButton(bool pressed);

    // Debug accessors
    uint8_t getSlotNumber() const { return slotNum_; }
    int16_t getMouseX() const { return mouseX_; }
    int16_t getMouseY() const { return mouseY_; }
    bool getMouseButton() const { return mouseButton_; }
    bool getMoved() const { return moved_; }
    bool getButtonChanged() const { return buttonChanged_; }
    int16_t getClampMinX() const { return clampMinX_; }
    int16_t getClampMaxX() const { return clampMaxX_; }
    int16_t getClampMinY() const { return clampMinY_; }
    int16_t getClampMaxY() const { return clampMaxY_; }
    uint8_t getDDRA() const { return ddra_; }
    uint8_t getDDRB() const { return ddrb_; }
    uint8_t getORA() const { return ora_; }
    uint8_t getORB() const { return orb_; }
    uint8_t getIRA() const { return ira_; }
    uint8_t getIRB() const { return irb_; }
    uint8_t getCRA() const { return cra_; }
    uint8_t getCRB() const { return crb_; }
    bool getVBLInterruptPending() const { return vblInterruptPending_; }
    bool getMoveInterruptPending() const { return moveInterruptPending_; }
    bool getButtonInterruptPending() const { return buttonInterruptPending_; }
    uint8_t getLastCommand() const { return lastCommand_; }
    uint8_t getResponseState() const { return responseState_; }
    bool getWasInVBL() const { return wasInVBL_; }
    uint8_t getMode() const { return ora_ & 0x0F; }

private:
    // ROM data
    const uint8_t* rom_;
    size_t romSize_;

    // Slot number (needed for screen hole addressing)
    uint8_t slotNum_ = 4;

    // 6821 PIA registers
    uint8_t ddra_ = 0;     // Data Direction Register A (1=output, 0=input)
    uint8_t ddrb_ = 0;     // Data Direction Register B
    uint8_t ora_ = 0;      // Output Register A
    uint8_t orb_ = 0;      // Output Register B
    uint8_t ira_ = 0;      // Input Register A (peripheral side)
    uint8_t irb_ = 0;      // Input Register B (peripheral side)
    uint8_t cra_ = 0;      // Control Register A
    uint8_t crb_ = 0;      // Control Register B

    // Mouse state
    int16_t mouseX_ = 0;
    int16_t mouseY_ = 0;
    bool mouseButton_ = false;
    bool lastButton_ = false;
    bool moved_ = false;
    bool buttonChanged_ = false;

    // Mouse clamping
    int16_t clampMinX_ = 0;
    int16_t clampMaxX_ = 1023;
    int16_t clampMinY_ = 0;
    int16_t clampMaxY_ = 1023;

    // Interrupt state
    bool irqActive_ = false;
    bool vblInterruptPending_ = false;
    bool moveInterruptPending_ = false;
    bool buttonInterruptPending_ = false;

    // VBL tracking
    uint64_t lastVBLCycle_ = 0;
    bool wasInVBL_ = false;

    // IRQ/cycle callbacks
    IRQCallback irqCallback_;
    CycleCallback cycleCallback_;

    // 6805 protocol state machine
    // The firmware writes commands via Port B, reads responses via Port A
    uint8_t lastCommand_ = 0;
    uint8_t responseState_ = 0;

    void updateIRQState();
    void processCommand(uint8_t cmd);
};

} // namespace a2e
