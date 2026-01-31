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
 * Since we don't have the MC68705 MCU ROM, the firmware entry points
 * are intercepted directly: when the CPU fetches an opcode at a known
 * entry point in the slot ROM, we perform the operation (updating
 * screen holes, CPU registers) and return RTS ($60) so the caller
 * gets immediate results without the MC6805 communication protocol.
 *
 * Mouse state is communicated through "screen holes" - unused bytes
 * in the text screen memory:
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
 * The 6821 PIA provides 4 registers at I/O offsets 0-3 and the
 * real ROM is served for card identification (signature bytes at
 * $Cn05, $Cn07, $Cn0B, $Cn0C and entry point table at $Cn12-$Cn20).
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

    // No expansion ROM - the mouse card uses banked slot ROM instead
    bool hasExpansionROM() const override { return false; }
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

    // Firmware interception callbacks
    // These allow the card to read/write Apple II memory and CPU registers
    // directly when intercepting firmware entry points.
    using MemReadCB = std::function<uint8_t(uint16_t)>;
    using MemWriteCB = std::function<void(uint16_t, uint8_t)>;
    using IsOpcodeFetchCB = std::function<bool()>;
    using GetRegCB = std::function<uint8_t()>;
    using SetRegCB = std::function<void(uint8_t)>;

    void setFirmwareCallbacks(
        IsOpcodeFetchCB isOpcodeFetch,
        MemReadCB memRead, MemWriteCB memWrite,
        GetRegCB getA, GetRegCB getP,
        SetRegCB setA, SetRegCB setX, SetRegCB setY, SetRegCB setP);

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
    uint8_t getMode() const { return mode_; }

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

    // Mouse mode (set by SetMouse firmware call)
    uint8_t mode_ = 0;

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

    // 6805 protocol state machine (kept for debug display)
    uint8_t lastCommand_ = 0;
    uint8_t responseState_ = 0;

    // Firmware interception callbacks
    IsOpcodeFetchCB isOpcodeFetch_;
    MemReadCB memRead_;
    MemWriteCB memWrite_;
    GetRegCB getRegA_;
    GetRegCB getRegP_;
    SetRegCB setRegA_;
    SetRegCB setRegX_;
    SetRegCB setRegY_;
    SetRegCB setRegP_;

    void updateIRQState();
    void processCommand(uint8_t cmd);

    // Firmware entry point handlers
    bool handleFirmwareCall(uint8_t offset);
    void fwInitMouse();
    void fwSetMouse();
    void fwServeMouse();
    void fwReadMouse();
    void fwClearMouse();
    void fwPosMouse();
    void fwClampMouse();
    void fwHomeMouse();

    // Helpers
    void writeScreenHoles();
    void clearCarry();
};

} // namespace a2e
