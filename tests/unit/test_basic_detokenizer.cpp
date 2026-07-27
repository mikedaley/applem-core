/*
 * test_basic_detokenizer.cpp - Unit tests for Applesoft and Integer BASIC detokenizer
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "basic_detokenizer.hpp"
#include "basic_program_builder.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace a2e;
using test::ApplesoftProgramBuilder;
using test::IntegerBasicProgramBuilder;

// ============================================================================
// Applesoft: simple PRINT line
// ============================================================================

TEST_CASE("detokenizeApplesoft simple PRINT line", "[basic][applesoft][print]") {
    ApplesoftProgramBuilder builder;

    // 10 PRINT "HELLO"
    // Token stream: PRINT(0xBA) '"' 'H' 'E' 'L' 'L' 'O' '"'
    builder.addLine(10, std::vector<uint8_t>{0xBA, 0x22, 'H', 'E', 'L', 'L', 'O', 0x22});

    auto data = builder.build();
    const char* result = BasicDetokenizer::detokenizeApplesoft(data.data(),
                                                               static_cast<int>(data.size()),
                                                               false);

    std::string output(result);
    // Should contain line number 10
    CHECK(output.find("10") != std::string::npos);
    // Should contain PRINT keyword
    CHECK(output.find("PRINT") != std::string::npos);
    // Should contain the string
    CHECK(output.find("HELLO") != std::string::npos);
}

// ============================================================================
// Applesoft: GOTO line
// ============================================================================

TEST_CASE("detokenizeApplesoft GOTO line", "[basic][applesoft][goto]") {
    ApplesoftProgramBuilder builder;

    // 20 GOTO 100
    // Token stream: GOTO(0xAB) '1' '0' '0'
    builder.addLine(20, std::vector<uint8_t>{0xAB, '1', '0', '0'});

    auto data = builder.build();
    const char* result = BasicDetokenizer::detokenizeApplesoft(data.data(),
                                                               static_cast<int>(data.size()),
                                                               false);

    std::string output(result);
    CHECK(output.find("20") != std::string::npos);
    CHECK(output.find("GOTO") != std::string::npos);
    CHECK(output.find("100") != std::string::npos);
}

// ============================================================================
// Applesoft: multiple lines
// ============================================================================

TEST_CASE("detokenizeApplesoft multiple lines", "[basic][applesoft][multi]") {
    ApplesoftProgramBuilder builder;

    // 10 PRINT "HI"
    builder.addLine(10, std::vector<uint8_t>{0xBA, 0x22, 'H', 'I', 0x22});

    // 20 GOTO 10
    builder.addLine(20, std::vector<uint8_t>{0xAB, '1', '0'});

    auto data = builder.build();
    const char* result = BasicDetokenizer::detokenizeApplesoft(data.data(),
                                                               static_cast<int>(data.size()),
                                                               false);

    std::string output(result);
    // Both line numbers should appear
    CHECK(output.find("10") != std::string::npos);
    CHECK(output.find("20") != std::string::npos);
    CHECK(output.find("PRINT") != std::string::npos);
    CHECK(output.find("GOTO") != std::string::npos);

    // Should have a newline separating the lines
    CHECK(output.find('\n') != std::string::npos);
}

// ============================================================================
// Applesoft: with length header (hasLengthHeader=true)
// ============================================================================

TEST_CASE("detokenizeApplesoft with length header", "[basic][applesoft][header]") {
    ApplesoftProgramBuilder builder;

    // 10 END
    builder.addLine(10, std::vector<uint8_t>{0x80}); // END token

    auto data = builder.buildWithHeader();
    const char* result = BasicDetokenizer::detokenizeApplesoft(data.data(),
                                                               static_cast<int>(data.size()),
                                                               true);

    std::string output(result);
    CHECK(output.find("10") != std::string::npos);
    CHECK(output.find("END") != std::string::npos);
}

// ============================================================================
// Applesoft: verify output contains line numbers
// ============================================================================

TEST_CASE("detokenizeApplesoft output contains padded line numbers", "[basic][applesoft][linenum]") {
    ApplesoftProgramBuilder builder;
    builder.addLine(100, std::vector<uint8_t>{0x80}); // END

    auto data = builder.build();
    const char* result = BasicDetokenizer::detokenizeApplesoft(data.data(),
                                                               static_cast<int>(data.size()),
                                                               false);

    std::string output(result);
    // Line number 100 should appear in the output, padded to 5 chars
    CHECK(output.find("100") != std::string::npos);
}

// ============================================================================
// Applesoft: HOME keyword (0x97)
// ============================================================================

TEST_CASE("detokenizeApplesoft HOME keyword", "[basic][applesoft][home]") {
    ApplesoftProgramBuilder builder;

    // 5 HOME
    builder.addLine(5, std::vector<uint8_t>{0x97});

    auto data = builder.build();
    const char* result = BasicDetokenizer::detokenizeApplesoft(data.data(),
                                                               static_cast<int>(data.size()),
                                                               false);

    std::string output(result);
    CHECK(output.find("HOME") != std::string::npos);
}

// ============================================================================
// Applesoft: FOR/NEXT
// ============================================================================

TEST_CASE("detokenizeApplesoft FOR/NEXT", "[basic][applesoft][for]") {
    ApplesoftProgramBuilder builder;

    // 10 FOR I = 1 TO 10
    // FOR(0x81) 'I' '=' '1' TO(0xC1) '1' '0'
    builder.addLine(10, std::vector<uint8_t>{0x81, 'I', 0xD0, '1', 0xC1, '1', '0'});

    // 20 NEXT I
    // NEXT(0x82) 'I'
    builder.addLine(20, std::vector<uint8_t>{0x82, 'I'});

    auto data = builder.build();
    const char* result = BasicDetokenizer::detokenizeApplesoft(data.data(),
                                                               static_cast<int>(data.size()),
                                                               false);

    std::string output(result);
    CHECK(output.find("FOR") != std::string::npos);
    CHECK(output.find("TO") != std::string::npos);
    CHECK(output.find("NEXT") != std::string::npos);
}

// ============================================================================
// Integer BASIC: simple program
// ============================================================================

TEST_CASE("detokenizeIntegerBasic simple PRINT program", "[basic][integer]") {
    IntegerBasicProgramBuilder builder;

    // Integer BASIC: 10 PRINT "HI"
    // PRINT token = 0x61 in integer BASIC token table
    // String: 0x28 = start quote, characters, 0x29 = end quote
    builder.addLine(10, std::vector<uint8_t>{0x61, 0x28, 'H', 'I', 0x29});

    auto data = builder.build();
    const char* result = BasicDetokenizer::detokenizeIntegerBasic(data.data(),
                                                                    static_cast<int>(data.size()),
                                                                    false);

    std::string output(result);
    CHECK(output.find("10") != std::string::npos);
    CHECK(output.find("PRINT") != std::string::npos);
    CHECK(output.find("HI") != std::string::npos);
}

TEST_CASE("detokenizeIntegerBasic with numeric constant", "[basic][integer][numeric]") {
    IntegerBasicProgramBuilder builder;

    // Integer BASIC: 10 PRINT 42
    // PRINT token = 0x61
    // Numeric: 0xB0 prefix + 2-byte LE value (42 = 0x002A)
    builder.addLine(10, std::vector<uint8_t>{0x61, 0xB0, 0x2A, 0x00});

    auto data = builder.build();
    const char* result = BasicDetokenizer::detokenizeIntegerBasic(data.data(),
                                                                    static_cast<int>(data.size()),
                                                                    false);

    std::string output(result);
    CHECK(output.find("10") != std::string::npos);
    CHECK(output.find("42") != std::string::npos);
}

TEST_CASE("detokenizeIntegerBasic with length header", "[basic][integer][header]") {
    IntegerBasicProgramBuilder builder;

    // 10 END
    builder.addLine(10, std::vector<uint8_t>{0x51}); // END token

    auto data = builder.buildWithHeader();
    const char* result = BasicDetokenizer::detokenizeIntegerBasic(data.data(),
                                                                    static_cast<int>(data.size()),
                                                                    true);

    std::string output(result);
    CHECK(output.find("10") != std::string::npos);
    CHECK(output.find("END") != std::string::npos);
}

// ============================================================================
// Applesoft: REM preserves text
// ============================================================================

TEST_CASE("detokenizeApplesoft REM preserves text", "[basic][applesoft][rem]") {
    ApplesoftProgramBuilder builder;

    // 10 REM THIS IS A COMMENT
    builder.addLine(10, std::vector<uint8_t>{0xB2, 'T', 'H', 'I', 'S', ' ', 'I', 'S',
                          ' ', 'A', ' ', 'C', 'O', 'M', 'M', 'E', 'N', 'T'});

    auto data = builder.build();
    const char* result = BasicDetokenizer::detokenizeApplesoft(data.data(),
                                                               static_cast<int>(data.size()),
                                                               false);

    std::string output(result);
    CHECK(output.find("REM") != std::string::npos);
    CHECK(output.find("THIS IS A COMMENT") != std::string::npos);
}

// ============================================================================
// Empty program
// ============================================================================

TEST_CASE("detokenizeApplesoft empty program returns empty string", "[basic][applesoft][empty]") {
    // Just the program terminator (0x00, 0x00)
    uint8_t data[] = {0x00, 0x00};
    const char* result = BasicDetokenizer::detokenizeApplesoft(data, sizeof(data), false);
    CHECK(strlen(result) == 0);
}

// ============================================================================
// Applesoft: programs longer than the old 4096-line cap
// ============================================================================

TEST_CASE("detokenizeApplesoft emits lines past 4096", "[basic][applesoft][limits]") {
    // The decoder used to fill a 4096-entry LineInfo array that nothing read,
    // and the bounds check guarding it also guarded the output append — so
    // every line past 4096 was silently dropped. Output is now limited only by
    // MAX_OUTPUT.
    ApplesoftProgramBuilder builder;

    constexpr int LINE_COUNT = 5000;
    for (int i = 0; i < LINE_COUNT; i++) {
        // `i` END — one token, so 5000 lines stay well inside MAX_OUTPUT.
        builder.addLine(i + 1, std::vector<uint8_t>{0x80});
    }

    auto data = builder.build();
    const char* result = BasicDetokenizer::detokenizeApplesoft(data.data(),
                                                               static_cast<int>(data.size()),
                                                               false);
    std::string output(result);

    // One newline per line after the first.
    const auto newlines = std::count(output.begin(), output.end(), '\n');
    CHECK(newlines == LINE_COUNT - 1);

    // The last line must actually be present, not truncated away.
    CHECK(output.find(" 5000 END") != std::string::npos);
}

// ============================================================================
// Applesoft: FOR/NEXT indentation
// ============================================================================

TEST_CASE("detokenizeApplesoft does not indent after a self-contained FOR/NEXT",
          "[basic][applesoft][indent]") {
    // 10 FOR AD = 1 TO 5 : NEXT
    // 20 PRINT
    // The loop opens and closes on line 10, so line 20 must sit at column zero.
    // Previously the NEXT decrement was clamped at zero before the FOR
    // increment was applied, leaving every following line indented forever.
    ApplesoftProgramBuilder builder;
    builder.addLine(10, std::vector<uint8_t>{
        0x81, 'A', 'D', 0xD0, '1', 0xC1, '5', 0x3A, 0x82}); // FOR AD=1 TO 5 : NEXT
    builder.addLine(20, std::vector<uint8_t>{0xBA});         // PRINT

    auto data = builder.build();
    std::string out(BasicDetokenizer::detokenizeApplesoft(
        data.data(), static_cast<int>(data.size()), false));

    // Take the text after the 5-wide line number and its single separator space.
    const auto secondLine = out.substr(out.find('\n') + 1);
    const auto body = secondLine.substr(6);

    CHECK(body.rfind("PRINT", 0) == 0);
}

TEST_CASE("detokenizeApplesoft still indents inside an open FOR",
          "[basic][applesoft][indent]") {
    // 10 FOR I = 1 TO 5
    // 20 PRINT
    // 30 NEXT
    // Line 20 is inside the loop and must be indented; line 30 closes it.
    ApplesoftProgramBuilder builder;
    builder.addLine(10, std::vector<uint8_t>{0x81, 'I', 0xD0, '1', 0xC1, '5'});
    builder.addLine(20, std::vector<uint8_t>{0xBA});
    builder.addLine(30, std::vector<uint8_t>{0x82});

    auto data = builder.build();
    std::string out(BasicDetokenizer::detokenizeApplesoft(
        data.data(), static_cast<int>(data.size()), false));

    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos <= out.size()) {
        const auto nl = out.find('\n', pos);
        lines.push_back(out.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }

    REQUIRE(lines.size() == 3);
    CHECK(lines[1].substr(6).rfind("   PRINT", 0) == 0); // indented one level
    CHECK(lines[2].substr(6).rfind("NEXT", 0) == 0);     // back to column zero
}

TEST_CASE("detokenizeApplesoft handles nested loops closing on one line",
          "[basic][applesoft][indent]") {
    // 10 FOR I = 1 TO 2 : FOR J = 1 TO 2
    // 20 PRINT
    // 30 NEXT J : NEXT I
    // 40 END
    // Line 20 sits two levels in; line 40 must return to column zero.
    ApplesoftProgramBuilder builder;
    builder.addLine(10, std::vector<uint8_t>{
        0x81, 'I', 0xD0, '1', 0xC1, '2', 0x3A, 0x81, 'J', 0xD0, '1', 0xC1, '2'});
    builder.addLine(20, std::vector<uint8_t>{0xBA});
    builder.addLine(30, std::vector<uint8_t>{0x82, 'J', 0x3A, 0x82, 'I'});
    builder.addLine(40, std::vector<uint8_t>{0x80});

    auto data = builder.build();
    std::string out(BasicDetokenizer::detokenizeApplesoft(
        data.data(), static_cast<int>(data.size()), false));

    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos <= out.size()) {
        const auto nl = out.find('\n', pos);
        lines.push_back(out.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }

    REQUIRE(lines.size() == 4);
    CHECK(lines[1].substr(6).rfind("      PRINT", 0) == 0); // two levels
    CHECK(lines[3].substr(6).rfind("END", 0) == 0);         // fully unwound
}
