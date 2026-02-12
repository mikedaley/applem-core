/*
 * test_basic_tokenizer.cpp - Unit tests for Applesoft BASIC tokenizer
 *
 * Tests the BASIC tokenizer including:
 * - Single-line tokenization
 * - Multi-line programs
 * - Memory layout (next-addr, line-num, tokens, terminator)
 * - Keyword token values (PRINT, GOTO, etc.)
 * - Empty input handling
 * - Round-trip tokenize/detokenize recovery
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "basic_tokenizer.hpp"
#include "basic_detokenizer.hpp"

#include <array>
#include <cstring>
#include <string>
#include <functional>

using namespace a2e;

// ---------------------------------------------------------------------------
// Test fixture: 64KB memory with zero-page pointers set up
// ---------------------------------------------------------------------------

struct BasicMemory {
    std::array<uint8_t, 65536> mem{};
    MemReadFn readMem;
    MemWriteFn writeMem;

    BasicMemory() {
        mem.fill(0);
        // Set up TXTTAB pointer at $67/$68 = $0801
        mem[0x67] = 0x01;
        mem[0x68] = 0x08;
        // Set up HIMEM/MEMSIZE at $73/$74 = $9600
        mem[0x73] = 0x00;
        mem[0x74] = 0x96;

        readMem = [this](uint16_t a) -> uint8_t { return mem[a]; };
        writeMem = [this](uint16_t a, uint8_t v) { mem[a] = v; };
    }

    // Read a 16-bit little-endian value from memory
    uint16_t read16(uint16_t addr) const {
        return mem[addr] | (mem[addr + 1] << 8);
    }
};

// ---------------------------------------------------------------------------
// Simple PRINT tokenization
// ---------------------------------------------------------------------------

TEST_CASE("loadBasicProgram tokenizes simple PRINT line", "[basic][tokenizer]") {
    BasicMemory m;
    int count = loadBasicProgram("10 PRINT \"HELLO\"", m.readMem, m.writeMem);
    REQUIRE(count == 1);
}

TEST_CASE("Tokenized memory has correct structure at $0801", "[basic][tokenizer]") {
    BasicMemory m;
    int count = loadBasicProgram("10 PRINT \"HELLO\"", m.readMem, m.writeMem);
    REQUIRE(count == 1);

    // At $0801: [next-addr:2][line-num:2][tokens...][0x00]
    uint16_t nextAddr = m.read16(0x0801);
    uint16_t lineNum = m.read16(0x0803);

    CHECK(lineNum == 10);
    CHECK(nextAddr > 0x0805);  // Must point past this line

    // The first token byte should be the PRINT token (0xBA)
    CHECK(m.mem[0x0805] == 0xBA);

    // The line should end with 0x00
    uint16_t termPos = nextAddr - 1;
    CHECK(m.mem[termPos] == 0x00);
}

TEST_CASE("Program ends with double zero bytes", "[basic][tokenizer]") {
    BasicMemory m;
    int count = loadBasicProgram("10 PRINT \"HI\"", m.readMem, m.writeMem);
    REQUIRE(count == 1);

    uint16_t nextAddr = m.read16(0x0801);
    // After the last line, there should be 0x00, 0x00
    CHECK(m.mem[nextAddr] == 0x00);
    CHECK(m.mem[nextAddr + 1] == 0x00);
}

// ---------------------------------------------------------------------------
// Multiple lines
// ---------------------------------------------------------------------------

TEST_CASE("loadBasicProgram returns correct count for multiple lines", "[basic][tokenizer]") {
    BasicMemory m;
    int count = loadBasicProgram(
        "10 PRINT \"A\"\n"
        "20 PRINT \"B\"\n"
        "30 PRINT \"C\"",
        m.readMem, m.writeMem
    );
    REQUIRE(count == 3);
}

TEST_CASE("Multiple lines are chained in memory", "[basic][tokenizer]") {
    BasicMemory m;
    int count = loadBasicProgram(
        "10 PRINT \"A\"\n"
        "20 GOTO 10",
        m.readMem, m.writeMem
    );
    REQUIRE(count == 2);

    // Line 1 starts at $0801
    uint16_t line1Next = m.read16(0x0801);
    uint16_t line1Num = m.read16(0x0803);
    CHECK(line1Num == 10);

    // Line 2 starts at line1Next
    uint16_t line2Next = m.read16(line1Next);
    uint16_t line2Num = m.read16(line1Next + 2);
    CHECK(line2Num == 20);

    // End of program
    CHECK(m.mem[line2Next] == 0x00);
    CHECK(m.mem[line2Next + 1] == 0x00);
}

// ---------------------------------------------------------------------------
// Empty / null input
// ---------------------------------------------------------------------------

TEST_CASE("loadBasicProgram with empty string returns 0", "[basic][tokenizer]") {
    BasicMemory m;
    int count = loadBasicProgram("", m.readMem, m.writeMem);
    CHECK(count == 0);
}

TEST_CASE("loadBasicProgram with null pointer returns -1", "[basic][tokenizer]") {
    BasicMemory m;
    int count = loadBasicProgram(nullptr, m.readMem, m.writeMem);
    CHECK(count == -1);
}

TEST_CASE("loadBasicProgram with whitespace-only lines returns 0", "[basic][tokenizer]") {
    BasicMemory m;
    int count = loadBasicProgram("   \n   \n", m.readMem, m.writeMem);
    CHECK(count == 0);
}

// ---------------------------------------------------------------------------
// Specific token values
// ---------------------------------------------------------------------------

TEST_CASE("GOTO keyword is tokenized as 0xAB", "[basic][tokenizer]") {
    BasicMemory m;
    int count = loadBasicProgram("10 GOTO 100", m.readMem, m.writeMem);
    REQUIRE(count == 1);

    // Scan token bytes at $0805+ for the GOTO token (0xAB)
    uint16_t nextAddr = m.read16(0x0801);
    bool foundGoto = false;
    for (uint16_t addr = 0x0805; addr < nextAddr; addr++) {
        if (m.mem[addr] == 0xAB) {
            foundGoto = true;
            break;
        }
    }
    CHECK(foundGoto);
}

TEST_CASE("PRINT keyword is tokenized as 0xBA", "[basic][tokenizer]") {
    BasicMemory m;
    int count = loadBasicProgram("10 PRINT", m.readMem, m.writeMem);
    REQUIRE(count == 1);

    CHECK(m.mem[0x0805] == 0xBA);
}

TEST_CASE("HOME keyword is tokenized as 0x97", "[basic][tokenizer]") {
    BasicMemory m;
    int count = loadBasicProgram("10 HOME", m.readMem, m.writeMem);
    REQUIRE(count == 1);

    CHECK(m.mem[0x0805] == 0x97);
}

TEST_CASE("FOR keyword is tokenized as 0x81", "[basic][tokenizer]") {
    BasicMemory m;
    int count = loadBasicProgram("10 FOR I=1 TO 10", m.readMem, m.writeMem);
    REQUIRE(count == 1);

    CHECK(m.mem[0x0805] == 0x81);  // FOR token
}

TEST_CASE("REM keyword is tokenized as 0xB2", "[basic][tokenizer]") {
    BasicMemory m;
    int count = loadBasicProgram("10 REM THIS IS A COMMENT", m.readMem, m.writeMem);
    REQUIRE(count == 1);

    CHECK(m.mem[0x0805] == 0xB2);  // REM token
}

// ---------------------------------------------------------------------------
// String content in quotes preserved
// ---------------------------------------------------------------------------

TEST_CASE("Quoted string content is preserved verbatim", "[basic][tokenizer]") {
    BasicMemory m;
    int count = loadBasicProgram("10 PRINT \"AB\"", m.readMem, m.writeMem);
    REQUIRE(count == 1);

    // After PRINT token (0xBA) there should be: " A B "
    // Find the quote in the token stream
    uint16_t nextAddr = m.read16(0x0801);
    bool foundQuotedAB = false;
    for (uint16_t addr = 0x0805; addr + 3 < nextAddr; addr++) {
        if (m.mem[addr] == '"' && m.mem[addr + 1] == 'A' &&
            m.mem[addr + 2] == 'B' && m.mem[addr + 3] == '"') {
            foundQuotedAB = true;
            break;
        }
    }
    CHECK(foundQuotedAB);
}

// ---------------------------------------------------------------------------
// Zero-page pointers are set correctly
// ---------------------------------------------------------------------------

TEST_CASE("TXTTAB pointer is set to $0801", "[basic][tokenizer][zeropage]") {
    BasicMemory m;
    loadBasicProgram("10 PRINT", m.readMem, m.writeMem);

    CHECK(m.read16(0x67) == 0x0801);
}

TEST_CASE("VARTAB and ARYTAB point past end of program", "[basic][tokenizer][zeropage]") {
    BasicMemory m;
    int count = loadBasicProgram("10 PRINT \"HI\"", m.readMem, m.writeMem);
    REQUIRE(count == 1);

    uint16_t vartab = m.read16(0x69);  // VARTAB
    uint16_t arytab = m.read16(0x6B);  // ARYTAB
    CHECK(vartab > 0x0801);
    CHECK(arytab == vartab);
}

// ---------------------------------------------------------------------------
// Round-trip: tokenize then detokenize recovers keywords
// ---------------------------------------------------------------------------

TEST_CASE("Round-trip tokenize/detokenize recovers PRINT keyword", "[basic][roundtrip]") {
    BasicMemory m;
    int count = loadBasicProgram("10 PRINT \"HELLO\"", m.readMem, m.writeMem);
    REQUIRE(count == 1);

    // Build a data buffer from memory starting at $0801
    // Find program end (double zero)
    uint16_t addr = 0x0801;
    while (addr < 0x9600) {
        uint16_t nextAddr = m.read16(addr);
        if (nextAddr == 0x0000) break;
        addr = nextAddr;
    }
    uint16_t progEnd = addr + 2;  // Include the final 0x00, 0x00

    int dataSize = progEnd - 0x0801;
    REQUIRE(dataSize > 0);
    REQUIRE(dataSize < 65536);

    const char* listing = BasicDetokenizer::detokenizeApplesoft(
        &m.mem[0x0801], dataSize, false
    );
    REQUIRE(listing != nullptr);

    std::string result(listing);
    // The listing should contain "PRINT" and "HELLO"
    CHECK(result.find("PRINT") != std::string::npos);
    CHECK(result.find("HELLO") != std::string::npos);
}

TEST_CASE("Round-trip tokenize/detokenize recovers GOTO keyword", "[basic][roundtrip]") {
    BasicMemory m;
    int count = loadBasicProgram("10 GOTO 100", m.readMem, m.writeMem);
    REQUIRE(count == 1);

    uint16_t addr = 0x0801;
    while (addr < 0x9600) {
        uint16_t nextAddr = m.read16(addr);
        if (nextAddr == 0x0000) break;
        addr = nextAddr;
    }
    uint16_t progEnd = addr + 2;
    int dataSize = progEnd - 0x0801;

    const char* listing = BasicDetokenizer::detokenizeApplesoft(
        &m.mem[0x0801], dataSize, false
    );
    REQUIRE(listing != nullptr);

    std::string result(listing);
    CHECK(result.find("GOTO") != std::string::npos);
    CHECK(result.find("100") != std::string::npos);
}

TEST_CASE("Round-trip multi-line program", "[basic][roundtrip]") {
    BasicMemory m;
    int count = loadBasicProgram(
        "10 HOME\n"
        "20 PRINT \"HELLO\"\n"
        "30 GOTO 20",
        m.readMem, m.writeMem
    );
    REQUIRE(count == 3);

    uint16_t addr = 0x0801;
    while (addr < 0x9600) {
        uint16_t nextAddr = m.read16(addr);
        if (nextAddr == 0x0000) break;
        addr = nextAddr;
    }
    uint16_t progEnd = addr + 2;
    int dataSize = progEnd - 0x0801;

    const char* listing = BasicDetokenizer::detokenizeApplesoft(
        &m.mem[0x0801], dataSize, false
    );
    REQUIRE(listing != nullptr);

    std::string result(listing);
    CHECK(result.find("HOME") != std::string::npos);
    CHECK(result.find("PRINT") != std::string::npos);
    CHECK(result.find("GOTO") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Question mark shorthand for PRINT
// ---------------------------------------------------------------------------

TEST_CASE("Question mark is tokenized as PRINT", "[basic][tokenizer]") {
    BasicMemory m;
    int count = loadBasicProgram("10 ?\"HI\"", m.readMem, m.writeMem);
    REQUIRE(count == 1);

    // Should find PRINT token (0xBA)
    CHECK(m.mem[0x0805] == 0xBA);
}

// ---------------------------------------------------------------------------
// Lines are sorted by line number
// ---------------------------------------------------------------------------

TEST_CASE("Lines provided out of order are sorted by line number", "[basic][tokenizer]") {
    BasicMemory m;
    int count = loadBasicProgram(
        "30 END\n"
        "10 PRINT\n"
        "20 GOTO 10",
        m.readMem, m.writeMem
    );
    REQUIRE(count == 3);

    // Line 1 at $0801 should be line 10
    uint16_t line1Num = m.read16(0x0803);
    CHECK(line1Num == 10);

    // Line 2 should be line 20
    uint16_t line1Next = m.read16(0x0801);
    uint16_t line2Num = m.read16(line1Next + 2);
    CHECK(line2Num == 20);

    // Line 3 should be line 30
    uint16_t line2Next = m.read16(line1Next);
    uint16_t line3Num = m.read16(line2Next + 2);
    CHECK(line3Num == 30);
}
