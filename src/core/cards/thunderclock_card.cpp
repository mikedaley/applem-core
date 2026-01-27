#include "thunderclock_card.hpp"
#include "roms.cpp" // For embedded ROM data
#include <ctime>
#include <cstring>

namespace a2e {

// I/O offsets (masked with 0x8F)
static constexpr uint8_t LOC_CONTROL = 0x00;  // $C0n0 - Control register
static constexpr uint8_t LOC_AUX = 0x08;      // $C0n8 - Aux register (unused)

// Control register flags
static constexpr uint8_t FLAG_CLOCK = 0x02;   // Clock signal
static constexpr uint8_t FLAG_STROBE = 0x04;  // Strobe signal

// Commands (bits 5-7 of control register)
static constexpr uint8_t CMD_REGHOLD = 0x00;   // Hold register
static constexpr uint8_t CMD_REGSHIFT = 0x20;  // Shift register
static constexpr uint8_t CMD_TIMED = 0xA0;     // Read time

ThunderclockCard::ThunderclockCard()
    : rom_(roms::ROM_THUNDERCLOCK)
    , romSize_(roms::ROM_THUNDERCLOCK_SIZE)
{
    latches_.fill(0);
    reset();
}

void ThunderclockCard::updateLatches() {
    // Get current time
    std::time_t now = std::time(nullptr);
    std::tm* tm = std::localtime(&now);

    if (!tm) {
        // Fallback if time unavailable
        latches_.fill(0);
        return;
    }

    // The Thunderclock returns time as a 40-bit serial stream
    // Format: month (4 bits), weekday (4 bits), day (8 bits BCD),
    //         hour (8 bits BCD), minute (8 bits BCD), second (8 bits BCD)
    // Total: 40 bits

    int month = tm->tm_mon;           // 0-11
    int dayOfWeek = tm->tm_wday;      // 0-6 (Sunday=0)
    int dayOfMonth = tm->tm_mday;     // 1-31
    int hour = tm->tm_hour;           // 0-23
    int minute = tm->tm_min;          // 0-59
    int second = tm->tm_sec;          // 0-59

    // Clear bits array
    bitIndex_ = 0;

    // Helper lambda to shift a value bit by bit (MSB first)
    auto shiftBits = [this](int value, int numBits) {
        for (int i = numBits - 1; i >= 0; i--) {
            if (bitIndex_ < 64) {
                bits_[bitIndex_++] = (value >> i) & 1;
            }
        }
    };

    // Helper to shift BCD value (tens digit then ones digit, 4 bits each)
    auto shiftBCD = [&shiftBits](int value) {
        shiftBits(value / 10, 4);  // Tens digit
        shiftBits(value % 10, 4);  // Ones digit
    };

    // Build the bit stream
    shiftBits(month, 4);       // Month (0-11), 4 bits
    shiftBits(dayOfWeek, 4);   // Day of week (0-6), 4 bits
    shiftBCD(dayOfMonth);      // Day (01-31), 8 bits BCD
    shiftBCD(hour);            // Hour (00-23), 8 bits BCD
    shiftBCD(minute);          // Minute (00-59), 8 bits BCD
    shiftBCD(second);          // Second (00-59), 8 bits BCD

    // Total: 4 + 4 + 8 + 8 + 8 + 8 = 40 bits
    currentBitIndex_ = 0;
}

uint8_t ThunderclockCard::readIO(uint8_t offset) {
    // The Thunderclock uses offset 0 ($C0n0) for the control/data register
    // All 16 I/O addresses appear to return the same register value
    (void)offset;

    // Return current register value
    // Bit 7 contains the current data bit
    return register_;
}

void ThunderclockCard::writeIO(uint8_t offset, uint8_t value) {
    // The Thunderclock uses offset 0 ($C0n0) for the control register
    // All 16 I/O addresses appear to access the same register
    (void)offset;

    // Extract control signals
    bool strobe = (value & FLAG_STROBE) != 0;
    bool clock = (value & FLAG_CLOCK) != 0;

    // Check for strobe rising edge
    if (strobe && !strobe_) {
        // Capture command from bits 5-7
        command_ = value & 0xE0;

        if (command_ == CMD_TIMED) {
            // Read time command - refresh the bit stream
            updateLatches();
        }
    }

    // Check for clock rising edge
    if (clock && !clock_) {
        // Shift out next bit
        if (command_ == CMD_REGSHIFT || command_ == CMD_TIMED) {
            if (currentBitIndex_ < bitIndex_) {
                // Set bit 7 of register based on current bit
                if (bits_[currentBitIndex_]) {
                    register_ |= 0x80;
                } else {
                    register_ &= ~0x80;
                }
                currentBitIndex_++;
            } else {
                register_ &= ~0x80;
            }
        }
    }

    // Save current state
    strobe_ = strobe;
    clock_ = clock;
}

uint8_t ThunderclockCard::peekIO(uint8_t offset) const {
    (void)offset;
    return register_;
}

uint8_t ThunderclockCard::readROM(uint8_t offset) {
    // Slot ROM is the first 256 bytes
    if (rom_ && romSize_ >= 256) {
        return rom_[offset];
    }
    return 0xFF;
}

uint8_t ThunderclockCard::readExpansionROM(uint16_t offset) {
    // Expansion ROM starts at byte 256 in the ROM file
    // $C800-$CFFF maps to ROM bytes 256 onward
    // The offset parameter is 0-2047 for the $C800-$CFFF range
    if (rom_ && romSize_ >= 256) {
        uint16_t romOffset = 256 + offset;
        if (romOffset < romSize_) {
            return rom_[romOffset];
        }
    }
    return 0xFF;
}

void ThunderclockCard::reset() {
    // Reset all state
    strobe_ = false;
    clock_ = false;
    command_ = 0;
    register_ = 0;
    bitIndex_ = 0;
    currentBitIndex_ = 0;

    // Initialize time data
    updateLatches();
}

} // namespace a2e
