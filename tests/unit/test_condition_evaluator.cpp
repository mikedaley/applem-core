/*
 * test_condition_evaluator.cpp - Unit tests for ConditionEvaluator
 *
 * This requires an Emulator instance since evaluate() and evaluateNumeric()
 * take a const Emulator& parameter.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "condition_evaluator.hpp"
#include "emulator.hpp"

#include <string>

using namespace a2e;

// Helper to create and initialize an emulator for testing.
// The emulator init() loads ROM and resets the CPU.
static Emulator& getEmulator() {
    static Emulator emu;
    static bool initialized = false;
    if (!initialized) {
        emu.init();
        initialized = true;
    }
    return emu;
}

// ============================================================================
// Simple numeric expression
// ============================================================================

TEST_CASE("ConditionEvaluator numeric literal 42 evaluates to 42", "[condeval][numeric]") {
    auto& emu = getEmulator();
    int32_t result = ConditionEvaluator::evaluateNumeric("42", emu);
    CHECK(result == 42);
}

TEST_CASE("ConditionEvaluator numeric literal 0 evaluates to 0", "[condeval][numeric]") {
    auto& emu = getEmulator();
    int32_t result = ConditionEvaluator::evaluateNumeric("0", emu);
    CHECK(result == 0);
}

TEST_CASE("ConditionEvaluator numeric literal 255 evaluates to 255", "[condeval][numeric]") {
    auto& emu = getEmulator();
    int32_t result = ConditionEvaluator::evaluateNumeric("255", emu);
    CHECK(result == 255);
}

// ============================================================================
// Hex literal
// ============================================================================

TEST_CASE("ConditionEvaluator hex literal $FF evaluates to 255", "[condeval][hex]") {
    auto& emu = getEmulator();
    int32_t result = ConditionEvaluator::evaluateNumeric("$FF", emu);
    CHECK(result == 255);
}

TEST_CASE("ConditionEvaluator hex literal $00 evaluates to 0", "[condeval][hex]") {
    auto& emu = getEmulator();
    int32_t result = ConditionEvaluator::evaluateNumeric("$00", emu);
    CHECK(result == 0);
}

TEST_CASE("ConditionEvaluator hex literal $FFFF evaluates to 65535", "[condeval][hex]") {
    auto& emu = getEmulator();
    int32_t result = ConditionEvaluator::evaluateNumeric("$FFFF", emu);
    CHECK(result == 65535);
}

// ============================================================================
// Register comparison
// ============================================================================

TEST_CASE("ConditionEvaluator A register comparison", "[condeval][register]") {
    auto& emu = getEmulator();

    // After init(), A register has some value (typically 0 after cold reset).
    // Set A to a known value via the emulator's CPU setter.
    emu.setA(0);

    bool result = ConditionEvaluator::evaluate("A == 0", emu);
    CHECK(result == true);

    // Negative test
    result = ConditionEvaluator::evaluate("A == 1", emu);
    CHECK(result == false);
}

TEST_CASE("ConditionEvaluator X register comparison", "[condeval][register]") {
    auto& emu = getEmulator();
    emu.setX(0x42);

    bool result = ConditionEvaluator::evaluate("X == $42", emu);
    CHECK(result == true);
}

TEST_CASE("ConditionEvaluator Y register comparison", "[condeval][register]") {
    auto& emu = getEmulator();
    emu.setY(0x10);

    bool result = ConditionEvaluator::evaluate("Y == $10", emu);
    CHECK(result == true);
}

// ============================================================================
// Invalid expression returns error
// ============================================================================

TEST_CASE("ConditionEvaluator unknown identifier sets error", "[condeval][error]") {
    auto& emu = getEmulator();

    // An expression with an unknown identifier triggers an error
    ConditionEvaluator::evaluate("FOOBAR == 1", emu);
    const char* err = ConditionEvaluator::getLastError();
    CHECK(strlen(err) > 0);
}

TEST_CASE("ConditionEvaluator valid expression clears error", "[condeval][error]") {
    auto& emu = getEmulator();

    // First trigger an error with an unknown identifier
    ConditionEvaluator::evaluate("FOOBAR == 1", emu);
    CHECK(strlen(ConditionEvaluator::getLastError()) > 0);

    // Now evaluate a valid expression
    ConditionEvaluator::evaluate("42 == 42", emu);
    // The error should be cleared (empty string)
    CHECK(strlen(ConditionEvaluator::getLastError()) == 0);
}

// ============================================================================
// Arithmetic expression
// ============================================================================

TEST_CASE("ConditionEvaluator arithmetic $10 + $20 = $30", "[condeval][arithmetic]") {
    auto& emu = getEmulator();
    int32_t result = ConditionEvaluator::evaluateNumeric("$10 + $20", emu);
    CHECK(result == 0x30);
}

TEST_CASE("ConditionEvaluator arithmetic subtraction", "[condeval][arithmetic]") {
    auto& emu = getEmulator();
    int32_t result = ConditionEvaluator::evaluateNumeric("100 - 30", emu);
    CHECK(result == 70);
}

TEST_CASE("ConditionEvaluator arithmetic multiplication", "[condeval][arithmetic]") {
    auto& emu = getEmulator();
    int32_t result = ConditionEvaluator::evaluateNumeric("6 * 7", emu);
    CHECK(result == 42);
}

// ============================================================================
// Boolean operations
// ============================================================================

TEST_CASE("ConditionEvaluator equality true", "[condeval][comparison]") {
    auto& emu = getEmulator();
    bool result = ConditionEvaluator::evaluate("42 == 42", emu);
    CHECK(result == true);
}

TEST_CASE("ConditionEvaluator inequality", "[condeval][comparison]") {
    auto& emu = getEmulator();
    bool result = ConditionEvaluator::evaluate("1 != 2", emu);
    CHECK(result == true);
}

TEST_CASE("ConditionEvaluator less than", "[condeval][comparison]") {
    auto& emu = getEmulator();
    bool result = ConditionEvaluator::evaluate("1 < 2", emu);
    CHECK(result == true);
}

TEST_CASE("ConditionEvaluator greater than", "[condeval][comparison]") {
    auto& emu = getEmulator();
    bool result = ConditionEvaluator::evaluate("10 > 5", emu);
    CHECK(result == true);
}

// ============================================================================
// Parenthesized expressions
// ============================================================================

TEST_CASE("ConditionEvaluator parenthesized expression", "[condeval][paren]") {
    auto& emu = getEmulator();

    // Brackets have the value inside them: a sum is a number and a
    // comparison is a truth. They used to make every sum a truth, so
    // (2 + 3) * 4 was 4, which the console's ? printed without complaint.
    int32_t result = ConditionEvaluator::evaluateNumeric("(2 + 3) * 4", emu);
    CHECK(result == 20);
    CHECK(ConditionEvaluator::evaluateNumeric("(1 < 2) + (3 == 3)", emu) == 2);

    // Verify parenthesized comparisons work as expected
    bool cmpResult = ConditionEvaluator::evaluate("(A == A) && (1 < 2)", emu);
    CHECK(cmpResult == true);
}

// ============================================================================
// Register numeric read
// ============================================================================

TEST_CASE("ConditionEvaluator reads register A as numeric", "[condeval][register_num]") {
    auto& emu = getEmulator();
    emu.setA(0xAB);
    int32_t result = ConditionEvaluator::evaluateNumeric("A", emu);
    CHECK(result == 0xAB);
}

TEST_CASE("ConditionEvaluator reads SP register", "[condeval][register_num]") {
    auto& emu = getEmulator();
    // SP should be a valid 8-bit value
    int32_t result = ConditionEvaluator::evaluateNumeric("SP", emu);
    CHECK(result >= 0);
    CHECK(result <= 0xFF);
}

// ============================================================================
// Errors are reported, not read past
// ============================================================================

TEST_CASE("ConditionEvaluator refuses what it cannot read", "[condeval][error]") {
    auto& emu = getEmulator();
    emu.setA(0x41);

    // A single = used to be dropped, leaving "A $41", which compared nothing
    // and read as A alone: true for any A but zero.
    CHECK_FALSE(ConditionEvaluator::evaluate("A = $41", emu));
    CHECK(std::string(ConditionEvaluator::getLastError()).find("==") != std::string::npos);

    // An unknown name is an error, and the condition is false.
    CHECK_FALSE(ConditionEvaluator::evaluate("FOO == 0", emu));
    CHECK(strlen(ConditionEvaluator::getLastError()) > 0);

    // Something left over, a missing operand or bracket.
    for (const char* bad : {"1 2", "A ==", "(A == 1", "A == 1)", "PEEK $24", "PEEK($24", "A & 1", "A @ 1",
                            "12AB", "BV(1)"}) {
        INFO(bad);
        ConditionEvaluator::evaluate(bad, emu);
        CHECK(strlen(ConditionEvaluator::getLastError()) > 0);
    }

    // A good condition after a bad one clears the error.
    CHECK(ConditionEvaluator::evaluate("A == $41", emu));
    CHECK(strlen(ConditionEvaluator::getLastError()) == 0);
}

TEST_CASE("ConditionEvaluator divides, and refuses to divide by zero", "[condeval][arithmetic]") {
    auto& emu = getEmulator();
    CHECK(ConditionEvaluator::evaluateNumeric("$100 / 2", emu) == 0x80);
    CHECK(strlen(ConditionEvaluator::getLastError()) == 0);
    // 1/0 used to read as 1, the / being skipped.
    ConditionEvaluator::evaluateNumeric("1/0", emu);
    CHECK(std::string(ConditionEvaluator::getLastError()) == "Division by zero");
    CHECK(ConditionEvaluator::evaluateNumeric("-5 + 8", emu) == 3);
}

TEST_CASE("ConditionEvaluator reads numbers as hex when asked, as the console does", "[condeval][hex]") {
    const MachineView view = ConditionEvaluator::viewOf(getEmulator());
    CHECK(ConditionEvaluator::evaluateNumeric("10", view, true) == 0x10);
    CHECK(ConditionEvaluator::evaluateNumeric("10+1", view, true) == 0x11);
    CHECK(ConditionEvaluator::evaluateNumeric("FF", view, true) == 0xFF);
    CHECK(ConditionEvaluator::evaluateNumeric("#10", view, true) == 10);
    CHECK(ConditionEvaluator::evaluateNumeric("0x20", view, false) == 0x20);
    // A register of the same spelling wins: A is the accumulator.
    getEmulator().setA(0x07);
    CHECK(ConditionEvaluator::evaluateNumeric("A", ConditionEvaluator::viewOf(getEmulator()), true) == 0x07);
    // Decimal by default, as conditions always were.
    CHECK(ConditionEvaluator::evaluateNumeric("10+1", view) == 11);
}

TEST_CASE("ConditionEvaluator checks a condition without a machine", "[condeval][error]") {
    CHECK(std::string(ConditionEvaluator::check("A == $41")).empty());
    CHECK_FALSE(std::string(ConditionEvaluator::check("A = $41")).empty());
    CHECK_FALSE(std::string(ConditionEvaluator::check("NOSUCH")).empty());
    // A divisor that is only zero because there is no machine is not an error.
    CHECK(std::string(ConditionEvaluator::check("PEEK($24) / A > 1")).empty());
}

TEST_CASE("PEEK and DEEK reach any bank a machine's view has", "[condeval][peek]") {
    MachineView view;
    view.peek = [](uint32_t address) -> uint8_t {
        if (address == 0xE12000) return 0x34;
        if (address == 0xE12001) return 0x12;
        return 0;
    };
    CHECK(ConditionEvaluator::evaluateNumeric("PEEK($E12000)", view) == 0x34);
    CHECK(ConditionEvaluator::evaluateNumeric("DEEK($E12000)", view) == 0x1234);
}
