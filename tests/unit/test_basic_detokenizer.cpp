/*
 * test_basic_detokenizer.cpp - Unit tests for Applesoft and Integer BASIC detokenizer
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "basic_detokenizer.hpp"
#include "basic_program_builder.hpp"

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
