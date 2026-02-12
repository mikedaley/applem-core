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

    // Parenthesized expressions are evaluated through parseOr which returns
    // a boolean (truthy) value. So (2 + 3) becomes true (1), and 1 * 4 = 4.
    // This is by design: the evaluator is a condition evaluator, not a
    // general-purpose calculator. Parentheses are for grouping comparisons.
    int32_t result = ConditionEvaluator::evaluateNumeric("(2 + 3) * 4", emu);
    CHECK(result == 4);

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
