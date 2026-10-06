/*
 * test_basic_tokenizer.cpp - Unit tests for Applesoft BASIC tokenizer
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 *  Shawn Bullock <shawn@agenticexpert.ai>
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
#include <vector>

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

TEST_CASE("A line number too long for an int is skipped, not thrown on", "[basic][tokenizer]") {
    // std::stoi threw on these, and nothing caught it: the app quit.
    BasicMemory m;
    int count = 0;
    REQUIRE_NOTHROW(count = loadBasicProgram("12345678901 PRINT 1\n99999999999999999999 PRINT 2\n20 PRINT 3", m.readMem, m.writeMem));
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
// Case handling: keywords fold case for matching, everything else verbatim
// ---------------------------------------------------------------------------

TEST_CASE("Lowercase keyword is tokenized", "[basic][tokenizer]") {
    BasicMemory m;
    int count = loadBasicProgram("10 print \"x\"", m.readMem, m.writeMem);
    REQUIRE(count == 1);

    CHECK(m.mem[0x0805] == 0xBA);  // PRINT token, despite lowercase input
}

TEST_CASE("Mixed-case REM comment preserves original case", "[basic][tokenizer]") {
    BasicMemory m;
    int count = loadBasicProgram("10 REM Hello", m.readMem, m.writeMem);
    REQUIRE(count == 1);

    CHECK(m.mem[0x0805] == 0xB2);  // REM token
    CHECK(m.mem[0x0807] == 'H');   // comment body emitted verbatim
    CHECK(m.mem[0x0808] == 'e');
    CHECK(m.mem[0x0809] == 'l');
}

TEST_CASE("Lowercase string literal preserves original case", "[basic][tokenizer]") {
    BasicMemory m;
    int count = loadBasicProgram("10 PRINT \"aB\"", m.readMem, m.writeMem);
    REQUIRE(count == 1);

    uint16_t nextAddr = m.read16(0x0801);
    bool foundLowerAB = false;
    for (uint16_t addr = 0x0805; addr + 3 < nextAddr; addr++) {
        if (m.mem[addr] == '"' && m.mem[addr + 1] == 'a' &&
            m.mem[addr + 2] == 'B' && m.mem[addr + 3] == '"') {
            foundLowerAB = true;
            break;
        }
    }
    CHECK(foundLowerAB);
}

TEST_CASE("Mixed-case DATA values preserve original case", "[basic][tokenizer]") {
    BasicMemory m;
    int count = loadBasicProgram("10 DATA aB,cD", m.readMem, m.writeMem);
    REQUIRE(count == 1);

    CHECK(m.mem[0x0805] == 0x83);  // DATA token

    // DATA body is emitted verbatim (case + spacing preserved)
    uint16_t nextAddr = m.read16(0x0801);
    bool foundLowerAB = false;
    for (uint16_t addr = 0x0806; addr + 1 < nextAddr; addr++) {
        if (m.mem[addr] == 'a' && m.mem[addr + 1] == 'B') {
            foundLowerAB = true;
            break;
        }
    }
    CHECK(foundLowerAB);
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

// ---------------------------------------------------------------------------
// HIMEM, and writing nothing on failure
// ---------------------------------------------------------------------------

namespace {

// A program of `lines` lines, each a REM long enough to make it large.
std::string bigProgram(int lines) {
    std::string src;
    for (int i = 1; i <= lines; i++) {
        src += std::to_string(i) + " REM " + std::string(200, 'X') + "\n";
    }
    return src;
}

} // namespace

TEST_CASE("A program that would run past HIMEM writes nothing", "[basic][tokenizer][himem]") {
    // Under DOS 3.3 and ProDOS HIMEM is $9600, and DOS lives above it. A
    // program written line by line until $C000 overwrote DOS and left
    // memory half written under the old pointers.
    BasicMemory m;
    for (int a = 0x0801; a < 0xC000; a++) m.mem[a] = 0xA5;
    m.mem[0x69] = 0x34; m.mem[0x6A] = 0x12; // VARTAB from some earlier program
    const auto before = m.mem;

    // About 41K of program: under $C000 but well over $9600.
    const std::string src = bigProgram(200);
    const BasicWriteResult r = writeBasicProgram(src.c_str(), m.readMem, m.writeMem);
    CHECK(r.status == BasicWriteStatus::TooLarge);
    CHECK(r.limit == 0x9600);
    CHECK(r.size > 0x9600 - 0x0801);
    CHECK(m.mem == before);
    CHECK(loadBasicProgram(src.c_str(), m.readMem, m.writeMem) == -1);
    CHECK(m.mem == before);
}

TEST_CASE("A program that ends exactly at HIMEM is written", "[basic][tokenizer][himem]") {
    BasicMemory m;
    const BasicProgramImage image = tokenizeBasicProgram("10 PRINT \"HI\"");
    // Put HIMEM exactly at the end of the program.
    const uint16_t end = static_cast<uint16_t>(0x0801 + image.bytes.size());
    m.mem[0x73] = end & 0xFF;
    m.mem[0x74] = end >> 8;
    const BasicWriteResult r = writeBasicProgram("10 PRINT \"HI\"", m.readMem, m.writeMem);
    CHECK(r.status == BasicWriteStatus::Written);
    CHECK(m.read16(0x69) == end);
    CHECK(m.read16(0x6F) == end); // FRETOP: string space starts empty at HIMEM

    // One byte less and it does not fit.
    BasicMemory n;
    n.mem[0x73] = (end - 1) & 0xFF;
    n.mem[0x74] = (end - 1) >> 8;
    CHECK(writeBasicProgram("10 PRINT \"HI\"", n.readMem, n.writeMem).status == BasicWriteStatus::TooLarge);
}

TEST_CASE("The tokenised image is what is written", "[basic][tokenizer]") {
    BasicMemory m;
    const char* src = "10 HOME\n20 PRINT \"A\";B: GOTO 10";
    const BasicProgramImage image = tokenizeBasicProgram(src);
    REQUIRE(loadBasicProgram(src, m.readMem, m.writeMem) == 2);
    REQUIRE(image.lines == 2);
    for (size_t i = 0; i < image.bytes.size(); i++) {
        CHECK(m.mem[0x0801 + i] == image.bytes[i]);
    }
    CHECK(m.read16(0x69) == 0x0801 + image.bytes.size());
}

// ---------------------------------------------------------------------------
// DATA, duplicates, line endings
// ---------------------------------------------------------------------------

TEST_CASE("A colon in quotes in DATA is part of the item", "[basic][tokenizer][data]") {
    // Applesoft's PARSE tests for a quote before the DATA flag, so the
    // colon in "A:B" does not end the statement and PRINT is still a token.
    const BasicProgramImage image = tokenizeBasicProgram("10 DATA \"A:B\",C: PRINT");
    const std::vector<uint8_t> tokens(image.bytes.begin() + 4, image.bytes.end() - 3);
    const std::vector<uint8_t> expected = {0x83, ' ', '"', 'A', ':', 'B', '"', ',', 'C', ':', 0xBA};
    CHECK(tokens == expected);
}

TEST_CASE("A quoted colon in DATA survives a round trip", "[basic][roundtrip][data]") {
    BasicMemory m;
    REQUIRE(loadBasicProgram("10 DATA \"A:B\",C: PRINT 1", m.readMem, m.writeMem) == 1);
    const uint16_t end = m.read16(0x69);
    const std::string listing = BasicDetokenizer::detokenizeApplesoft(&m.mem[0x0801], end - 0x0801, false);
    CHECK(listing.find("\"A:B\"") != std::string::npos);

    BasicMemory again;
    REQUIRE(loadBasicProgram(listing.c_str(), again.readMem, again.writeMem) == 1);
    for (uint16_t a = 0x0801; a < end; a++) CHECK(again.mem[a] == m.mem[a]);
}

TEST_CASE("A line number given twice keeps the later line", "[basic][tokenizer][duplicate]") {
    BasicMemory m;
    const BasicWriteResult r = writeBasicProgram("10 PRINT 1\n20 PRINT 2\n30 END\n20 PRINT 3", m.readMem, m.writeMem);
    REQUIRE(r.status == BasicWriteStatus::Written);
    CHECK(r.lines == 3);
    CHECK(r.replacedLines == std::vector<int>{20});
    const uint16_t second = m.read16(0x0801);
    CHECK(m.read16(second + 2) == 20);
    CHECK(m.mem[second + 4] == 0xBA);
    CHECK(m.mem[second + 5] == '3');
    const uint16_t third = m.read16(second);
    CHECK(m.read16(third + 2) == 30);
}

TEST_CASE("CR, LF and CRLF all end a line", "[basic][tokenizer]") {
    BasicMemory m;
    CHECK(loadBasicProgram("10 PRINT 1\r20 PRINT 2\r30 END\r", m.readMem, m.writeMem) == 3);
    CHECK(loadBasicProgram("10 PRINT 1\r\n20 PRINT 2\r\n30 END", m.readMem, m.writeMem) == 3);
    // A CRLF line has no stray CR at its end.
    const uint16_t next = m.read16(0x0801);
    CHECK(m.mem[next - 1] == 0x00);
    CHECK(m.mem[next - 2] == '1');
}

// ---------------------------------------------------------------------------
// Control characters
// ---------------------------------------------------------------------------

TEST_CASE("A control character's token is written as the byte", "[basic][tokenizer][control]") {
    const BasicProgramImage image = tokenizeBasicProgram("10 PRINT \"{ctrl-d}CATALOG\": REM {^G}{chr:27}{del}");
    const std::vector<uint8_t> tokens(image.bytes.begin() + 4, image.bytes.end() - 3);
    const std::vector<uint8_t> expected = {0xBA, '"', 0x04, 'C', 'A', 'T', 'A', 'L', 'O', 'G', '"', ':',
                                           0xB2, ' ', 0x07, 0x1B, 0x7F};
    CHECK(tokens == expected);
}

TEST_CASE("PRINT \"^DCATALOG\" survives a read and a write", "[basic][roundtrip][control]") {
    // The detokenizer dropped bytes below $20 in strings, REM and DATA, so a
    // program that ran DOS commands lost them between Read and Write.
    BasicMemory m;
    REQUIRE(loadBasicProgram("10 PRINT \"{ctrl-d}CATALOG\"\n20 DATA {ctrl-g}X\n30 REM {ctrl-[}", m.readMem, m.writeMem) == 3);
    const uint16_t end = m.read16(0x69);
    const std::string listing = BasicDetokenizer::detokenizeApplesoft(&m.mem[0x0801], end - 0x0801, false);
    CHECK(listing.find("\"{ctrl-d}CATALOG\"") != std::string::npos);
    CHECK(listing.find("{ctrl-g}X") != std::string::npos);
    CHECK(listing.find("{ctrl-[}") != std::string::npos);

    BasicMemory again;
    REQUIRE(loadBasicProgram(listing.c_str(), again.readMem, again.writeMem) == 3);
    REQUIRE(again.read16(0x69) == end);
    for (uint16_t a = 0x0801; a < end; a++) CHECK(again.mem[a] == m.mem[a]);
}

TEST_CASE("A brace that reads as a token is escaped and comes back", "[basic][roundtrip][control]") {
    // A string that really says {ctrl-d}, eight printable characters.
    const std::vector<uint8_t> line = {0xBA, '"', '{', 'c', 't', 'r', 'l', '-', 'd', '}', '{', 0x04, '"'};
    std::vector<uint8_t> program = {0, 0, 10, 0};
    program.insert(program.end(), line.begin(), line.end());
    program.push_back(0);
    const uint16_t next = static_cast<uint16_t>(0x0801 + program.size());
    program[0] = next & 0xFF;
    program[1] = next >> 8;
    program.push_back(0);
    program.push_back(0);

    const std::string listing = BasicDetokenizer::detokenizeApplesoft(program.data(), static_cast<int>(program.size()), false);
    CHECK(listing.find("\"{chr:123}ctrl-d}{{ctrl-d}\"") != std::string::npos);

    const BasicProgramImage image = tokenizeBasicProgram(listing.c_str());
    CHECK(image.bytes == program);
}
