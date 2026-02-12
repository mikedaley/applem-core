/*
 * test_keyboard.cpp - Unit tests for Apple IIe keyboard input handling
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "keyboard.hpp"

using namespace a2e;

// ============================================================================
// Basic key translation (handleKeyDown)
// ============================================================================

TEST_CASE("handleKeyDown returns translated Apple II keycode", "[keyboard][basic]") {
    Keyboard kb;
    // 'A' key (browser keycode 65) with no modifiers
    // translateKeycode maps 65 -> 0x61 ('a' lowercase)
    int result = kb.handleKeyDown(65, false, false, false, false, false);
    // Without caps lock or shift, should return lowercase 'a' = 0x61
    CHECK(result == 0x61);
}

TEST_CASE("Letter keys A-Z map to lowercase ASCII without modifiers", "[keyboard][letters]") {
    Keyboard kb;
    // Browser keycodes 65-90 for A-Z
    for (int browserKey = 65; browserKey <= 90; browserKey++) {
        int result = kb.handleKeyDown(browserKey, false, false, false, false, false);
        int expected = browserKey + 32; // lowercase ASCII
        INFO("Browser keycode " << browserKey << " expected 0x" << std::hex << expected);
        CHECK(result == expected);
    }
}

// ============================================================================
// Shift modifier
// ============================================================================

TEST_CASE("Shift+letter gives uppercase ASCII", "[keyboard][shift]") {
    Keyboard kb;
    // Browser keycode 65 ('A') with shift
    int result = kb.handleKeyDown(65, true, false, false, false, false);
    CHECK(result == 0x41); // 'A' uppercase
}

TEST_CASE("Shift modifier uppercase for all letters", "[keyboard][shift]") {
    Keyboard kb;
    for (int browserKey = 65; browserKey <= 90; browserKey++) {
        int result = kb.handleKeyDown(browserKey, true, false, false, false, false);
        int expected = browserKey; // uppercase ASCII = same as browser keycode
        INFO("Browser keycode " << browserKey);
        CHECK(result == expected);
    }
}

TEST_CASE("Caps lock gives uppercase without shift", "[keyboard][capslock]") {
    Keyboard kb;
    int result = kb.handleKeyDown(65, false, false, false, false, true);
    CHECK(result == 0x41); // 'A' uppercase
}

// ============================================================================
// Control modifier
// ============================================================================

TEST_CASE("Ctrl+A gives control character 0x01", "[keyboard][ctrl]") {
    Keyboard kb;
    // Browser keycode 65 ('A'), ctrl pressed
    int result = kb.handleKeyDown(65, false, true, false, false, false);
    CHECK(result == 0x01);
}

TEST_CASE("Ctrl+letters produce control characters 0x01-0x1A", "[keyboard][ctrl]") {
    Keyboard kb;
    for (int browserKey = 65; browserKey <= 90; browserKey++) {
        int result = kb.handleKeyDown(browserKey, false, true, false, false, false);
        int expected = browserKey - 64; // Ctrl+A=1, Ctrl+B=2, ...
        INFO("Ctrl+" << (char)browserKey << " expected 0x" << std::hex << expected);
        CHECK(result == expected);
    }
}

// ============================================================================
// Open Apple / Closed Apple (button state)
// ============================================================================

TEST_CASE("Alt key sets Open Apple pressed state", "[keyboard][apple]") {
    Keyboard kb;
    CHECK(kb.isOpenApplePressed() == false);

    // Alt key down (browser keycode 18)
    kb.handleKeyDown(18, false, false, true, false, false);
    CHECK(kb.isOpenApplePressed() == true);
}

TEST_CASE("Meta key sets Closed Apple pressed state", "[keyboard][apple]") {
    Keyboard kb;
    CHECK(kb.isClosedApplePressed() == false);

    // Left Meta key down (browser keycode 91)
    kb.handleKeyDown(91, false, false, false, true, false);
    CHECK(kb.isClosedApplePressed() == true);
}

TEST_CASE("Right Meta key also sets Closed Apple", "[keyboard][apple]") {
    Keyboard kb;
    // Right Meta key down (browser keycode 93)
    kb.handleKeyDown(93, false, false, false, true, false);
    CHECK(kb.isClosedApplePressed() == true);
}

// ============================================================================
// handleKeyUp clears button state
// ============================================================================

TEST_CASE("handleKeyUp clears Open Apple", "[keyboard][keyup]") {
    Keyboard kb;

    kb.handleKeyDown(18, false, false, true, false, false);
    CHECK(kb.isOpenApplePressed() == true);

    kb.handleKeyUp(18, false, false, true, false);
    CHECK(kb.isOpenApplePressed() == false);
}

TEST_CASE("handleKeyUp clears Closed Apple", "[keyboard][keyup]") {
    Keyboard kb;

    kb.handleKeyDown(91, false, false, false, true, false);
    CHECK(kb.isClosedApplePressed() == true);

    kb.handleKeyUp(91, false, false, false, true);
    CHECK(kb.isClosedApplePressed() == false);
}

// ============================================================================
// reset
// ============================================================================

TEST_CASE("reset clears modifier states", "[keyboard][reset]") {
    Keyboard kb;

    // Set both apple buttons
    kb.handleKeyDown(18, false, false, true, false, false);
    kb.handleKeyDown(91, false, false, false, true, false);
    CHECK(kb.isOpenApplePressed() == true);
    CHECK(kb.isClosedApplePressed() == true);

    kb.reset();

    CHECK(kb.isOpenApplePressed() == false);
    CHECK(kb.isClosedApplePressed() == false);
}

// ============================================================================
// Special keys
// ============================================================================

TEST_CASE("Enter key maps to CR (0x0D)", "[keyboard][special]") {
    Keyboard kb;
    int result = kb.handleKeyDown(13, false, false, false, false, false);
    CHECK(result == 0x0D);
}

TEST_CASE("Escape key maps to 0x1B", "[keyboard][special]") {
    Keyboard kb;
    int result = kb.handleKeyDown(27, false, false, false, false, false);
    CHECK(result == 0x1B);
}

TEST_CASE("Space key maps to 0x20", "[keyboard][special]") {
    Keyboard kb;
    int result = kb.handleKeyDown(32, false, false, false, false, false);
    CHECK(result == 0x20);
}

TEST_CASE("Left arrow maps to 0x08", "[keyboard][special]") {
    Keyboard kb;
    int result = kb.handleKeyDown(37, false, false, false, false, false);
    CHECK(result == 0x08);
}

TEST_CASE("Right arrow maps to 0x15", "[keyboard][special]") {
    Keyboard kb;
    int result = kb.handleKeyDown(39, false, false, false, false, false);
    CHECK(result == 0x15);
}

TEST_CASE("Up arrow maps to 0x0B", "[keyboard][special]") {
    Keyboard kb;
    int result = kb.handleKeyDown(38, false, false, false, false, false);
    CHECK(result == 0x0B);
}

TEST_CASE("Down arrow maps to 0x0A", "[keyboard][special]") {
    Keyboard kb;
    int result = kb.handleKeyDown(40, false, false, false, false, false);
    CHECK(result == 0x0A);
}

// ============================================================================
// Unmapped keys return -1
// ============================================================================

TEST_CASE("Unmapped key returns -1", "[keyboard][unmapped]") {
    Keyboard kb;
    // F-keys and other non-mapped keys
    int result = kb.handleKeyDown(112, false, false, false, false, false); // F1
    CHECK(result == -1);
}

TEST_CASE("Pure modifier keys return -1", "[keyboard][unmapped]") {
    Keyboard kb;
    // Shift key (16) returns -1
    CHECK(kb.handleKeyDown(16, true, false, false, false, false) == -1);
    // Ctrl key (17) returns -1
    CHECK(kb.handleKeyDown(17, false, true, false, false, false) == -1);
    // Alt key (18) returns -1 (but sets Open Apple state)
    CHECK(kb.handleKeyDown(18, false, false, true, false, false) == -1);
}

// ============================================================================
// charToAppleKey
// ============================================================================

TEST_CASE("charToAppleKey converts printable ASCII", "[keyboard][charToAppleKey]") {
    CHECK(charToAppleKey(0x20) == 0x20); // space
    CHECK(charToAppleKey(0x41) == 0x41); // 'A'
    CHECK(charToAppleKey(0x61) == 0x61); // 'a'
    CHECK(charToAppleKey(0x7E) == 0x7E); // '~'
    CHECK(charToAppleKey(0x30) == 0x30); // '0'
}

TEST_CASE("charToAppleKey converts newline to CR", "[keyboard][charToAppleKey]") {
    CHECK(charToAppleKey(0x0A) == 0x0D); // LF -> CR
    CHECK(charToAppleKey(0x0D) == 0x0D); // CR -> CR
}

TEST_CASE("charToAppleKey converts tab", "[keyboard][charToAppleKey]") {
    CHECK(charToAppleKey(0x09) == 0x09);
}

TEST_CASE("charToAppleKey returns -1 for unmappable characters", "[keyboard][charToAppleKey]") {
    CHECK(charToAppleKey(0x00) == -1);
    CHECK(charToAppleKey(0x01) == -1);
    CHECK(charToAppleKey(0x7F) == -1);
    CHECK(charToAppleKey(0x100) == -1); // Beyond ASCII
}

// ============================================================================
// Shift+number row
// ============================================================================

TEST_CASE("Shift+number produces correct symbols", "[keyboard][shift_symbols]") {
    Keyboard kb;
    CHECK(kb.handleKeyDown(49, true, false, false, false, false) == 0x21); // 1 -> !
    CHECK(kb.handleKeyDown(50, true, false, false, false, false) == 0x40); // 2 -> @
    CHECK(kb.handleKeyDown(51, true, false, false, false, false) == 0x23); // 3 -> #
    CHECK(kb.handleKeyDown(48, true, false, false, false, false) == 0x29); // 0 -> )
    CHECK(kb.handleKeyDown(57, true, false, false, false, false) == 0x28); // 9 -> (
}
