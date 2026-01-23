#pragma once

#include <cstdint>
#include <functional>

namespace a2e {

// Forward declaration
class AY8910;

// 6522 VIA (Versatile Interface Adapter) emulation
// Two of these are used in the Mockingboard, each controlling one AY-3-8910
class VIA6522 {
public:
    using IRQCallback = std::function<void()>;

    VIA6522();

    // Register access
    uint8_t read(uint8_t reg);
    void write(uint8_t reg, uint8_t value);

    // Timer update (call with CPU cycles elapsed)
    void update(int cycles);

    // Connect to PSG chip
    void connectPSG(AY8910* psg) { psg_ = psg; }

    // IRQ callback
    void setIRQCallback(IRQCallback cb) { irqCallback_ = std::move(cb); }

    // Reset
    void reset();

    // Check if IRQ is active
    bool isIRQActive() const { return (ifr_ & ier_ & 0x7F) != 0; }

private:
    // Register addresses
    static constexpr int REG_ORB = 0x00;   // Output Register B / Input Register B
    static constexpr int REG_ORA = 0x01;   // Output Register A / Input Register A
    static constexpr int REG_DDRB = 0x02;  // Data Direction Register B
    static constexpr int REG_DDRA = 0x03;  // Data Direction Register A
    static constexpr int REG_T1CL = 0x04;  // Timer 1 Counter Low
    static constexpr int REG_T1CH = 0x05;  // Timer 1 Counter High
    static constexpr int REG_T1LL = 0x06;  // Timer 1 Latch Low
    static constexpr int REG_T1LH = 0x07;  // Timer 1 Latch High
    static constexpr int REG_T2CL = 0x08;  // Timer 2 Counter Low
    static constexpr int REG_T2CH = 0x09;  // Timer 2 Counter High
    static constexpr int REG_SR = 0x0A;    // Shift Register
    static constexpr int REG_ACR = 0x0B;   // Auxiliary Control Register
    static constexpr int REG_PCR = 0x0C;   // Peripheral Control Register
    static constexpr int REG_IFR = 0x0D;   // Interrupt Flag Register
    static constexpr int REG_IER = 0x0E;   // Interrupt Enable Register
    static constexpr int REG_ORA_NH = 0x0F; // Output Register A (no handshake)

    // IRQ flag bits
    static constexpr uint8_t IRQ_CA2 = 0x01;
    static constexpr uint8_t IRQ_CA1 = 0x02;
    static constexpr uint8_t IRQ_SR = 0x04;
    static constexpr uint8_t IRQ_CB2 = 0x08;
    static constexpr uint8_t IRQ_CB1 = 0x10;
    static constexpr uint8_t IRQ_T2 = 0x20;
    static constexpr uint8_t IRQ_T1 = 0x40;
    static constexpr uint8_t IRQ_ANY = 0x80;

    // Port registers
    uint8_t ora_ = 0;    // Output Register A
    uint8_t orb_ = 0;    // Output Register B
    uint8_t ddra_ = 0;   // Data Direction Register A (1 = output)
    uint8_t ddrb_ = 0;   // Data Direction Register B (1 = output)
    uint8_t ira_ = 0;    // Input Register A (external input)
    uint8_t irb_ = 0;    // Input Register B (external input)

    // Timer 1
    uint16_t t1Counter_ = 0xFFFF;
    uint16_t t1Latch_ = 0xFFFF;
    bool t1Running_ = false;
    bool t1Fired_ = false;

    // Timer 2
    uint16_t t2Counter_ = 0xFFFF;
    uint8_t t2LatchLow_ = 0xFF;
    bool t2Running_ = false;
    bool t2Fired_ = false;

    // Shift register
    uint8_t sr_ = 0;

    // Control registers
    uint8_t acr_ = 0;   // Auxiliary Control Register
    uint8_t pcr_ = 0;   // Peripheral Control Register
    uint8_t ifr_ = 0;   // Interrupt Flag Register
    uint8_t ier_ = 0;   // Interrupt Enable Register

    // Connected PSG
    AY8910* psg_ = nullptr;

    // IRQ callback
    IRQCallback irqCallback_;

    // Helper methods
    void updatePSG();
    void checkIRQ();

    // Previous PSG control state for edge detection
    uint8_t prevPsgControl_ = 0;
};

} // namespace a2e
