#include "thunderclock_card.hpp"
#include "roms.cpp" // For embedded ROM data
#include <ctime>
#include <cstring>
#include <cstdio>

// Enable debug output for Thunderclock I/O
#define THUNDERCLOCK_DEBUG 0

namespace a2e {

// I/O offsets (masked with 0x8F)
static constexpr uint8_t LOC_CONTROL = 0x00;  // $C0n0 - Control register
static constexpr uint8_t LOC_AUX = 0x08;      // $C0n8 - Aux register (unused)

// Control register flags
static constexpr uint8_t FLAG_CLOCK = 0x02;   // Clock signal
static constexpr uint8_t FLAG_STROBE = 0x04;  // Strobe signal

// Commands (bits 3-5 of control register, directly from uPD1990C C0-C2 pins)
// uPD1990C command encoding:
//   000 = Register Hold
//   001 = Register Shift
//   010 = Time Set
//   011 = Time Read
//   100 = TP 64Hz
//   101 = TP 256Hz
//   110 = TP 2048Hz
//   111 = Test Mode
static constexpr uint8_t CMD_REGHOLD = 0x00;   // 000 - Hold register
static constexpr uint8_t CMD_REGSHIFT = 0x01;  // 001 - Shift register
static constexpr uint8_t CMD_TIMESET = 0x02;   // 010 - Time set
static constexpr uint8_t CMD_TIMEREAD = 0x03;  // 011 - Time read

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

    int month = tm->tm_mon + 1;       // 1-12 (convert from 0-11)
    int dayOfWeek = tm->tm_wday;      // 0-6 (Sunday=0)
    int dayOfMonth = tm->tm_mday;     // 1-31
    int hour = tm->tm_hour;           // 0-23
    int minute = tm->tm_min;          // 0-59
    int second = tm->tm_sec;          // 0-59

    // Clear bits array
    bitIndex_ = 0;

    // Thunderclock Plus format (from AppleWin):
    // 10 BCD nibbles, LSB-first within each nibble
    // Order: sec_ones, sec_tens, min_ones, min_tens, hr_ones, hr_tens, day_ones, day_tens, dow, month
    auto shiftNibbleLSB = [this](int value) {
        for (int i = 0; i < 4; i++) {
            if (bitIndex_ < 64) {
                bits_[bitIndex_++] = (value >> i) & 1;
            }
        }
    };

    shiftNibbleLSB(second % 10);            // Nibble 0: Second ones
    shiftNibbleLSB(second / 10);            // Nibble 1: Second tens
    shiftNibbleLSB(minute % 10);            // Nibble 2: Minute ones
    shiftNibbleLSB(minute / 10);            // Nibble 3: Minute tens
    shiftNibbleLSB(hour % 10);              // Nibble 4: Hour ones
    shiftNibbleLSB(hour / 10);              // Nibble 5: Hour tens
    shiftNibbleLSB(dayOfMonth % 10);        // Nibble 6: Day ones
    shiftNibbleLSB(dayOfMonth / 10);        // Nibble 7: Day tens
    shiftNibbleLSB(dayOfWeek);              // Nibble 8: Day of week (0-6)
    shiftNibbleLSB(month);                  // Nibble 9: Month (1-12)

    // Total: 10 * 4 = 40 bits
    currentBitIndex_ = 0;

#if THUNDERCLOCK_DEBUG
    printf("TC === TIME DATA ===\n");
    printf("TC System time: %04d-%02d-%02d %02d:%02d:%02d (dow=%d)\n",
           tm->tm_year + 1900, month, dayOfMonth, hour, minute, second, dayOfWeek);
    printf("TC Want ProDOS: %02d-%s-%02d\n",
           dayOfMonth,
           (const char*[]){"???","JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"}[month],
           year);
    printf("TC Sending: mon=%d dowField=%d day=%d (adjusted from %d)\n",
           month, dowForYear, adjustedDay, dayOfMonth);
    printf("TC Nibbles: ");
    for (int n = 0; n < 10; n++) {
        int val = 0;
        for (int b = 0; b < 4; b++) {
            val |= (bits_[n*4 + b] << (3-b));
        }
        printf("%X ", val);
    }
    printf("(mon,dow,day,hr,min,sec)\n");
#endif
}

uint8_t ThunderclockCard::readIO(uint8_t offset) {
    // The Thunderclock uses offset 0 ($C0n0) for the control/data register
    // All 16 I/O addresses appear to return the same register value
    (void)offset;

#if THUNDERCLOCK_DEBUG
    static int readCount = 0;
    if (readCount < 50) {
        printf("TC readIO: returning $%02X (bit7=%d)\n", register_, (register_ >> 7) & 1);
        readCount++;
    }
#endif

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

#if THUNDERCLOCK_DEBUG
    static int debugCount = 0;
    if (debugCount < 100) {
        printf("TC writeIO: val=$%02X strobe=%d->%d clock=%d->%d cmd=$%02X\n",
               value, strobe_ ? 1 : 0, strobe ? 1 : 0, clock_ ? 1 : 0, clock ? 1 : 0, command_);
    }
#endif

    // Check for strobe rising edge
    if (strobe && !strobe_) {
        // Capture command from bits 3-5 (C0, C1, C2 of uPD1990C)
        command_ = (value >> 3) & 0x07;

        if (command_ == CMD_TIMEREAD) {
            // Time read command - refresh the bit stream
            updateLatches();
            // First bit should be immediately available on DATA OUT after load
            if (bitIndex_ > 0) {
                register_ = bits_[0] ? 0x80 : 0x00;
            }
        }
    }

    // Check for clock rising edge
    if (clock && !clock_) {
        // Shift out next bit - increment FIRST, then read
        // This is because bit 0 is already presented after strobe/load
        if (command_ == CMD_REGSHIFT || command_ == CMD_TIMEREAD) {
            currentBitIndex_++;  // Advance to next bit position
            if (currentBitIndex_ < bitIndex_) {
                // Set bit 7 of register based on current bit
                if (bits_[currentBitIndex_]) {
                    register_ |= 0x80;
                } else {
                    register_ &= ~0x80;
                }
#if THUNDERCLOCK_DEBUG
                if (readBitCount_ < 64) {
                    readBitLog_[readBitCount_++] = bits_[currentBitIndex_];
                }
                // When we've read all 40 bits, print summary
                if (currentBitIndex_ == 39) {
                    printf("TC === BITS READ BY PRODOS ===\n");
                    printf("TC Nibbles (MSB-first): ");
                    for (int n = 0; n < 10; n++) {
                        int val = 0;
                        for (int b = 0; b < 4; b++) {
                            val |= (readBitLog_[n*4 + b] << (3-b));
                        }
                        printf("%X ", val);
                    }
                    printf("= mon,dow,day,hr,min,sec\n");
                }
#endif
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
    // Expansion ROM maps the ENTIRE 2KB ROM at $C800-$CFFF
    // On real Thunderclock Plus hardware, the same 2KB ROM chip is used for both:
    // - Slot ROM ($Cn00-$CnFF): reads bytes 0-255
    // - Expansion ROM ($C800-$CFFF): reads bytes 0-2047 (full ROM)
    if (rom_ && offset < romSize_) {
        return rom_[offset];
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
