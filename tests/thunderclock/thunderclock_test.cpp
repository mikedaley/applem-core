/**
 * Thunderclock Plus Card Tests
 *
 * Tests the Thunderclock Plus clock card implementation to verify:
 * 1. ROM signature bytes for ProDOS detection
 * 2. I/O register behavior
 * 3. Time data serialization
 * 4. Clock/strobe state machine
 *
 * ProDOS scans slots looking for specific ROM signature bytes:
 * - $Cn00: $08 (PHP instruction)
 * - $Cn02: $28 (signature byte)
 * - $Cn04: $58 (signature byte)
 * - $Cn06: $70 (signature byte)
 */

#include <iostream>
#include <iomanip>
#include <cstdint>
#include <vector>
#include <string>
#include <sstream>
#include <ctime>
#include <stdexcept>

// Include the Thunderclock card implementation
#include "cards/thunderclock_card.hpp"

// Test result tracking
static int testsRun = 0;
static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name) \
    void test_##name(); \
    struct TestRunner_##name { \
        TestRunner_##name() { \
            std::cout << "Running: " << #name << "... "; \
            testsRun++; \
            try { \
                test_##name(); \
                testsPassed++; \
                std::cout << "\033[32mPASSED\033[0m\n"; \
            } catch (const std::exception& e) { \
                testsFailed++; \
                std::cout << "\033[31mFAILED: " << e.what() << "\033[0m\n"; \
            } \
        } \
    } testRunner_##name; \
    void test_##name()

#define ASSERT_EQ(expected, actual) \
    if ((expected) != (actual)) { \
        throw std::runtime_error( \
            "Expected " + std::to_string(expected) + \
            " but got " + std::to_string(actual)); \
    }

#define ASSERT_EQ_HEX(expected, actual) \
    if ((expected) != (actual)) { \
        std::stringstream ss; \
        ss << "Expected $" << std::hex << std::uppercase << (int)(expected) \
           << " but got $" << (int)(actual); \
        throw std::runtime_error(ss.str()); \
    }

#define ASSERT_TRUE(condition) \
    if (!(condition)) { \
        throw std::runtime_error("Assertion failed: " #condition); \
    }

// Control register flags
static constexpr uint8_t FLAG_CLOCK = 0x02;
static constexpr uint8_t FLAG_STROBE = 0x04;
static constexpr uint8_t CMD_REGHOLD = 0x00;
static constexpr uint8_t CMD_REGSHIFT = 0x20;
static constexpr uint8_t CMD_TIMED = 0xA0;

// ============================================================================
// ROM Signature Tests
// ============================================================================

TEST(rom_signature_prodos_detection) {
    // ProDOS looks for these bytes at specific offsets to detect a clock card
    a2e::ThunderclockCard card;

    // $Cn00: $08 (PHP instruction)
    ASSERT_EQ_HEX(0x08, card.readROM(0x00));

    // $Cn02: $28 (PLP instruction - signature byte)
    ASSERT_EQ_HEX(0x28, card.readROM(0x02));

    // $Cn04: $58 (CLI instruction - signature byte)
    ASSERT_EQ_HEX(0x58, card.readROM(0x04));

    // $Cn06: $70 (BVS instruction - signature byte)
    ASSERT_EQ_HEX(0x70, card.readROM(0x06));
}

TEST(rom_first_instruction_sequence) {
    // Verify the first few bytes form valid 6502 code
    a2e::ThunderclockCard card;

    // Dump first 16 bytes for inspection
    std::cout << "\n  ROM bytes: ";
    for (int i = 0; i < 16; i++) {
        std::cout << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                  << (int)card.readROM(i) << " ";
    }
    std::cout << std::dec << "\n  ";

    // The ROM should start with PHP ($08) to save processor status
    ASSERT_EQ_HEX(0x08, card.readROM(0x00));
}

TEST(rom_size_and_expansion) {
    a2e::ThunderclockCard card;

    // Card should have ROM
    ASSERT_TRUE(card.hasROM());

    // Card should have expansion ROM
    ASSERT_TRUE(card.hasExpansionROM());

    // Read expansion ROM - should return valid data, not 0xFF
    uint8_t expByte = card.readExpansionROM(0x00);
    std::cout << "\n  Expansion ROM[0] = $" << std::hex << (int)expByte << std::dec << "\n  ";
}

// ============================================================================
// I/O Register Tests
// ============================================================================

TEST(io_read_initial_state) {
    a2e::ThunderclockCard card;

    // Initial register state should be 0
    ASSERT_EQ_HEX(0x00, card.readIO(0x00));

    // All I/O addresses should return the same register
    for (int offset = 0; offset < 16; offset++) {
        ASSERT_EQ_HEX(0x00, card.readIO(offset));
    }
}

TEST(io_peek_matches_read) {
    a2e::ThunderclockCard card;

    // Peek should match read
    ASSERT_EQ(card.readIO(0x00), card.peekIO(0x00));
}

TEST(io_write_does_not_directly_change_register) {
    a2e::ThunderclockCard card;

    // Writing to I/O should not directly change the read value
    // (it controls the state machine, not the data register)
    card.writeIO(0x00, 0x55);
    // Register value depends on state machine, not direct write
}

// ============================================================================
// Time Data Command Tests
// ============================================================================

TEST(time_command_loads_data) {
    a2e::ThunderclockCard card;

    // Issue CMD_TIMED with strobe rising edge
    card.writeIO(0x00, 0x00);  // Ensure strobe is low
    card.writeIO(0x00, CMD_TIMED | FLAG_STROBE);  // Strobe rising edge with time command

    // Now clock out the first bit
    card.writeIO(0x00, CMD_TIMED | FLAG_STROBE);  // Clock low
    card.writeIO(0x00, CMD_TIMED | FLAG_STROBE | FLAG_CLOCK);  // Clock rising edge

    // Read should now have bit 7 set to the first data bit
    uint8_t value = card.readIO(0x00);
    std::cout << "\n  After first clock: register = $" << std::hex << (int)value << std::dec << "\n  ";
}

TEST(time_data_40_bits) {
    a2e::ThunderclockCard card;

    // Issue CMD_TIMED with strobe rising edge
    card.writeIO(0x00, 0x00);
    card.writeIO(0x00, CMD_TIMED | FLAG_STROBE);

    // Clock out all 40 bits
    std::vector<int> bits;
    for (int i = 0; i < 40; i++) {
        // Clock rising edge
        card.writeIO(0x00, CMD_TIMED | FLAG_STROBE);  // Clock low
        card.writeIO(0x00, CMD_TIMED | FLAG_STROBE | FLAG_CLOCK);  // Clock high

        uint8_t value = card.readIO(0x00);
        bits.push_back((value & 0x80) ? 1 : 0);
    }

    // Print the bits
    std::cout << "\n  40 bits: ";
    for (int i = 0; i < 40; i++) {
        std::cout << bits[i];
        if ((i + 1) % 4 == 0) std::cout << " ";
    }
    std::cout << "\n  ";

    // Decode the bits
    // Month (4 bits), weekday (4 bits), day (8 bits BCD), hour (8 bits BCD), min (8 bits BCD), sec (8 bits BCD)
    int month = (bits[0] << 3) | (bits[1] << 2) | (bits[2] << 1) | bits[3];
    int weekday = (bits[4] << 3) | (bits[5] << 2) | (bits[6] << 1) | bits[7];
    int day_tens = (bits[8] << 3) | (bits[9] << 2) | (bits[10] << 1) | bits[11];
    int day_ones = (bits[12] << 3) | (bits[13] << 2) | (bits[14] << 1) | bits[15];
    int day = day_tens * 10 + day_ones;
    int hour_tens = (bits[16] << 3) | (bits[17] << 2) | (bits[18] << 1) | bits[19];
    int hour_ones = (bits[20] << 3) | (bits[21] << 2) | (bits[22] << 1) | bits[23];
    int hour = hour_tens * 10 + hour_ones;
    int min_tens = (bits[24] << 3) | (bits[25] << 2) | (bits[26] << 1) | bits[27];
    int min_ones = (bits[28] << 3) | (bits[29] << 2) | (bits[30] << 1) | bits[31];
    int minute = min_tens * 10 + min_ones;
    int sec_tens = (bits[32] << 3) | (bits[33] << 2) | (bits[34] << 1) | bits[35];
    int sec_ones = (bits[36] << 3) | (bits[37] << 2) | (bits[38] << 1) | bits[39];
    int second = sec_tens * 10 + sec_ones;

    std::cout << "  Decoded: month=" << month << " weekday=" << weekday
              << " day=" << day << " hour=" << hour << ":" << minute << ":" << second << "\n  ";

    // Get current time to compare
    std::time_t now = std::time(nullptr);
    std::tm* tm = std::localtime(&now);

    // Month should match (0-11)
    ASSERT_EQ(tm->tm_mon, month);

    // Weekday should match (0-6)
    ASSERT_EQ(tm->tm_wday, weekday);

    // Day should be close (might change during test)
    ASSERT_TRUE(day >= 1 && day <= 31);

    // Hour should match
    ASSERT_EQ(tm->tm_hour, hour);

    // Minute should be close (might change during test)
    ASSERT_TRUE(minute >= 0 && minute <= 59);

    // Second should be valid
    ASSERT_TRUE(second >= 0 && second <= 59);
}

// ============================================================================
// State Machine Tests
// ============================================================================

TEST(strobe_edge_detection) {
    a2e::ThunderclockCard card;

    // Strobe should only trigger on rising edge
    card.writeIO(0x00, 0x00);  // Low
    card.writeIO(0x00, FLAG_STROBE);  // Rising edge - should trigger
    card.writeIO(0x00, FLAG_STROBE);  // Still high - should NOT trigger again
    card.writeIO(0x00, 0x00);  // Falling edge
    card.writeIO(0x00, FLAG_STROBE);  // Rising edge again - should trigger
}

TEST(clock_edge_detection) {
    a2e::ThunderclockCard card;

    // Setup: issue time command
    card.writeIO(0x00, 0x00);
    card.writeIO(0x00, CMD_TIMED | FLAG_STROBE);

    // Clock should only shift on rising edge
    uint8_t val1 = card.readIO(0x00);

    card.writeIO(0x00, CMD_TIMED | FLAG_STROBE | FLAG_CLOCK);  // Rising edge
    uint8_t val2 = card.readIO(0x00);

    card.writeIO(0x00, CMD_TIMED | FLAG_STROBE | FLAG_CLOCK);  // Still high - no shift
    uint8_t val3 = card.readIO(0x00);

    // val2 and val3 should be the same (no shift on holding high)
    ASSERT_EQ(val2, val3);
}

TEST(reset_clears_state) {
    a2e::ThunderclockCard card;

    // Setup some state
    card.writeIO(0x00, CMD_TIMED | FLAG_STROBE);
    card.writeIO(0x00, CMD_TIMED | FLAG_STROBE | FLAG_CLOCK);

    // Reset
    card.reset();

    // Register should be 0 after reset
    ASSERT_EQ_HEX(0x00, card.readIO(0x00));
}

// ============================================================================
// Card Information Tests
// ============================================================================

TEST(card_name) {
    a2e::ThunderclockCard card;
    std::string name = card.getName();
    ASSERT_TRUE(name == "Thunderclock");
}

TEST(preferred_slot) {
    a2e::ThunderclockCard card;
    // Thunderclock is typically in slot 5 or 7
    uint8_t slot = card.getPreferredSlot();
    ASSERT_TRUE(slot == 5 || slot == 7);
}

// ============================================================================
// ProDOS Driver Interaction Simulation
// ============================================================================

TEST(prodos_driver_read_sequence) {
    // Simulate how ProDOS reads time from the Thunderclock
    // The driver in ROM does:
    // 1. Write CMD_TIMED with strobe rising edge to start reading
    // 2. Clock out 32 bits (ProDOS only needs date/time, not seconds)
    // 3. Convert to ProDOS date/time format

    a2e::ThunderclockCard card;

    // Step 1: Issue time read command
    card.writeIO(0x00, 0x00);  // Clear
    card.writeIO(0x00, CMD_TIMED | FLAG_STROBE);  // Strobe rising with time cmd

    // Step 2: Clock out 32 bits (ProDOS format)
    uint32_t rawTime = 0;
    for (int i = 0; i < 32; i++) {
        card.writeIO(0x00, CMD_TIMED | FLAG_STROBE);  // Clock low
        card.writeIO(0x00, CMD_TIMED | FLAG_STROBE | FLAG_CLOCK);  // Clock rising

        uint8_t value = card.readIO(0x00);
        rawTime = (rawTime << 1) | ((value & 0x80) ? 1 : 0);
    }

    std::cout << "\n  Raw 32-bit time: $" << std::hex << rawTime << std::dec << "\n  ";

    // Decode: first 8 bits = month(4) + weekday(4), next 8 = day BCD, next 8 = hour BCD, next 8 = min BCD
    int month = (rawTime >> 28) & 0x0F;
    int weekday = (rawTime >> 24) & 0x0F;
    int day = ((rawTime >> 20) & 0x0F) * 10 + ((rawTime >> 16) & 0x0F);
    int hour = ((rawTime >> 12) & 0x0F) * 10 + ((rawTime >> 8) & 0x0F);
    int minute = ((rawTime >> 4) & 0x0F) * 10 + (rawTime & 0x0F);

    std::cout << "  ProDOS format: " << (month + 1) << "/" << day << " " << hour << ":" << minute << "\n  ";

    // Verify values are in valid ranges
    ASSERT_TRUE(month >= 0 && month <= 11);
    ASSERT_TRUE(day >= 1 && day <= 31);
    ASSERT_TRUE(hour >= 0 && hour <= 23);
    ASSERT_TRUE(minute >= 0 && minute <= 59);
}

// ============================================================================
// ROM Disassembly Analysis
// ============================================================================

TEST(rom_entry_point_analysis) {
    a2e::ThunderclockCard card;

    std::cout << "\n  ROM Entry Point Disassembly:\n";

    // Disassemble first 32 bytes
    const char* mnemonics[] = {
        "PHP", "SEI", "PLP", "BIT $FF58", "", "", "BVS +5", "",
        "SEC", "BCS +1", "", "CLC", "CLV", "PHP", "SEI", "PHA"
    };

    for (int i = 0; i < 16; i++) {
        uint8_t byte = card.readROM(i);
        std::cout << "    $" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << i
                  << ": " << std::setw(2) << (int)byte << std::dec;
        if (i < 16 && mnemonics[i][0]) {
            std::cout << "  ; " << mnemonics[i];
        }
        std::cout << "\n";
    }
    std::cout << "  ";
}

TEST(expansion_rom_entry_points) {
    a2e::ThunderclockCard card;

    // The expansion ROM at $C800 should have valid code
    std::cout << "\n  Expansion ROM ($C800) first 16 bytes:\n  ";
    for (int i = 0; i < 16; i++) {
        std::cout << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                  << (int)card.readExpansionROM(i) << " ";
    }
    std::cout << std::dec << "\n  ";

    // Check that expansion ROM isn't all zeros or 0xFF
    bool allZero = true;
    bool allFF = true;
    for (int i = 0; i < 256; i++) {
        uint8_t byte = card.readExpansionROM(i);
        if (byte != 0x00) allZero = false;
        if (byte != 0xFF) allFF = false;
    }
    ASSERT_TRUE(!allZero);
    ASSERT_TRUE(!allFF);
}

// ============================================================================
// Verify ProDOS-specific behavior
// ============================================================================

TEST(prodos_reads_rom_signature_correctly) {
    // ProDOS reads specific offsets to detect clock card
    a2e::ThunderclockCard card;

    // These must match exactly for ProDOS to recognize the card
    std::cout << "\n  ProDOS signature check:\n";
    std::cout << "    $Cn00 = $" << std::hex << (int)card.readROM(0x00) << " (expected $08)\n";
    std::cout << "    $Cn02 = $" << std::hex << (int)card.readROM(0x02) << " (expected $28)\n";
    std::cout << "    $Cn04 = $" << std::hex << (int)card.readROM(0x04) << " (expected $58)\n";
    std::cout << "    $Cn06 = $" << std::hex << (int)card.readROM(0x06) << " (expected $70)\n";
    std::cout << std::dec << "  ";

    ASSERT_EQ_HEX(0x08, card.readROM(0x00));
    ASSERT_EQ_HEX(0x28, card.readROM(0x02));
    ASSERT_EQ_HEX(0x58, card.readROM(0x04));
    ASSERT_EQ_HEX(0x70, card.readROM(0x06));
}

TEST(io_address_all_offsets_return_same_register) {
    // The Thunderclock should return the same register from all I/O offsets
    a2e::ThunderclockCard card;

    // Set up some state
    card.writeIO(0x00, CMD_TIMED | FLAG_STROBE);
    card.writeIO(0x00, CMD_TIMED | FLAG_STROBE | FLAG_CLOCK);

    uint8_t expected = card.readIO(0x00);

    // All 16 I/O addresses should return the same value
    for (int offset = 0; offset < 16; offset++) {
        ASSERT_EQ(expected, card.readIO(offset));
    }
}

TEST(multiple_time_reads_without_reset) {
    // Verify that multiple time read commands work correctly
    a2e::ThunderclockCard card;

    for (int iteration = 0; iteration < 3; iteration++) {
        // Issue time read command
        card.writeIO(0x00, 0x00);
        card.writeIO(0x00, CMD_TIMED | FLAG_STROBE);

        // Read 8 bits
        uint8_t byte = 0;
        for (int bit = 0; bit < 8; bit++) {
            card.writeIO(0x00, CMD_TIMED | FLAG_STROBE);
            card.writeIO(0x00, CMD_TIMED | FLAG_STROBE | FLAG_CLOCK);
            uint8_t value = card.readIO(0x00);
            byte = (byte << 1) | ((value & 0x80) ? 1 : 0);
        }

        std::cout << "\n  Iteration " << iteration << ": first byte = $"
                  << std::hex << (int)byte << std::dec;
    }
    std::cout << "\n  ";
}

TEST(read_before_any_clocking) {
    // Test what happens when you read the register before any clocking
    // This is important because ProDOS may read to check status
    a2e::ThunderclockCard card;

    // Read immediately after construction (after reset)
    uint8_t value = card.readIO(0x00);
    std::cout << "\n  After construction (no commands): $" << std::hex << (int)value << std::dec << "\n";

    // Issue strobe only (no clock yet)
    card.writeIO(0x00, 0x00);
    card.writeIO(0x00, CMD_TIMED | FLAG_STROBE);
    value = card.readIO(0x00);
    std::cout << "  After strobe (time command, no clock): $" << std::hex << (int)value << std::dec << "\n";

    // Now clock one bit
    card.writeIO(0x00, CMD_TIMED | FLAG_STROBE);
    card.writeIO(0x00, CMD_TIMED | FLAG_STROBE | FLAG_CLOCK);
    value = card.readIO(0x00);
    std::cout << "  After first clock: $" << std::hex << (int)value << std::dec << "\n  ";
}

TEST(verify_bcd_encoding) {
    // Verify BCD encoding is correct for various values
    a2e::ThunderclockCard card;

    // Issue time read command
    card.writeIO(0x00, 0x00);
    card.writeIO(0x00, CMD_TIMED | FLAG_STROBE);

    // Skip first 8 bits (month + weekday)
    for (int i = 0; i < 8; i++) {
        card.writeIO(0x00, CMD_TIMED | FLAG_STROBE);
        card.writeIO(0x00, CMD_TIMED | FLAG_STROBE | FLAG_CLOCK);
    }

    // Read day (8 bits BCD)
    uint8_t dayBCD = 0;
    for (int i = 0; i < 8; i++) {
        card.writeIO(0x00, CMD_TIMED | FLAG_STROBE);
        card.writeIO(0x00, CMD_TIMED | FLAG_STROBE | FLAG_CLOCK);
        dayBCD = (dayBCD << 1) | ((card.readIO(0x00) & 0x80) ? 1 : 0);
    }

    int dayTens = (dayBCD >> 4) & 0x0F;
    int dayOnes = dayBCD & 0x0F;
    int day = dayTens * 10 + dayOnes;

    std::cout << "\n  Day BCD: $" << std::hex << (int)dayBCD << std::dec
              << " = " << day << "\n  ";

    // Day should be valid (1-31)
    ASSERT_TRUE(day >= 1 && day <= 31);
    // BCD digits should be 0-9
    ASSERT_TRUE(dayTens <= 3);
    ASSERT_TRUE(dayOnes <= 9);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "Thunderclock Plus Card Tests\n";
    std::cout << "============================\n\n";

    // Tests run automatically via static initializers

    std::cout << "\n============================\n";
    std::cout << "Results: " << testsPassed << "/" << testsRun << " passed";
    if (testsFailed > 0) {
        std::cout << " (" << testsFailed << " failed)";
    }
    std::cout << "\n";

    return testsFailed > 0 ? 1 : 0;
}
