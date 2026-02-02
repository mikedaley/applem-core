/*
 * keyboard.cpp - Browser keycode to Apple II ASCII translation
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "keyboard.hpp"

namespace a2e {

Keyboard::Keyboard() {}

int Keyboard::handleKeyDown(int browserKeycode, bool shift, bool ctrl,
                            bool alt, bool meta, bool capsLock) {
  // Track modifier keys (Apple buttons)
  if (browserKeycode == 18) { // Alt
    openApplePressed_ = true;
    return -1; // Don't generate a key
  }
  if (browserKeycode == 91 || browserKeycode == 93) { // Meta (left/right)
    closedApplePressed_ = true;
    return -1;
  }

  // Skip pure modifier keys
  if (browserKeycode == 16 || browserKeycode == 17) { // Shift, Ctrl
    return -1;
  }

  // Translate browser keycode to base Apple II code
  int appleKey = translateKeycode(browserKeycode);
  if (appleKey < 0) {
    return -1; // Not a mapped key
  }

  // Handle letters (a-z)
  if (appleKey >= 0x61 && appleKey <= 0x7A) {
    // Apply caps lock and shift
    if (capsLock && !shift) {
      // Caps lock on, no shift -> uppercase
      appleKey -= 32;
    } else if (!capsLock && shift) {
      // Caps lock off, shift pressed -> uppercase
      appleKey -= 32;
    }
    // Otherwise stays lowercase
  } else if (shift) {
    // Apply shift to non-letter keys
    appleKey = applyShift(browserKeycode, appleKey);
  }

  // Apply control modifier (produces control characters)
  if (ctrl) {
    appleKey = applyControl(appleKey);
  }

  // Send to emulator via callback
  if (keyCallback_) {
    keyCallback_(appleKey);
  }

  return appleKey;
}

void Keyboard::handleKeyUp(int browserKeycode, bool shift, bool ctrl,
                           bool alt, bool meta) {
  (void)shift;
  (void)ctrl;

  // Track modifier keys
  if (browserKeycode == 18) { // Alt
    openApplePressed_ = false;
    return;
  }
  if (browserKeycode == 91 || browserKeycode == 93) { // Meta
    closedApplePressed_ = false;
    return;
  }
}

int Keyboard::translateKeycode(int browserKeycode) const {
  // Letters A-Z (browser codes 65-90) -> lowercase a-z (0x61-0x7A)
  if (browserKeycode >= 65 && browserKeycode <= 90) {
    return browserKeycode + 32; // Convert to lowercase
  }

  // Numbers 0-9 (browser codes 48-57) -> ASCII 0x30-0x39
  if (browserKeycode >= 48 && browserKeycode <= 57) {
    return browserKeycode;
  }

  // Special keys
  switch (browserKeycode) {
  case 13:
    return 0x0D; // Enter -> CR
  case 8:
    return 0x08; // Backspace -> Left arrow (delete)
  case 27:
    return 0x1B; // Escape
  case 32:
    return 0x20; // Space
  case 9:
    return 0x09; // Tab

  // Arrow keys
  case 37:
    return 0x08; // Left arrow
  case 38:
    return 0x0B; // Up arrow
  case 39:
    return 0x15; // Right arrow
  case 40:
    return 0x0A; // Down arrow

  // Punctuation (US keyboard layout)
  case 188:
    return 0x2C; // Comma
  case 190:
    return 0x2E; // Period
  case 191:
    return 0x2F; // Slash
  case 186:
    return 0x3B; // Semicolon
  case 222:
    return 0x27; // Quote
  case 219:
    return 0x5B; // Left bracket
  case 221:
    return 0x5D; // Right bracket
  case 220:
    return 0x5C; // Backslash
  case 189:
    return 0x2D; // Minus
  case 187:
    return 0x3D; // Equals
  case 192:
    return 0x60; // Backtick

  default:
    return -1; // Not mapped
  }
}

int Keyboard::applyShift(int browserKeycode, int baseKey) const {
  // Number row shifted symbols
  switch (browserKeycode) {
  case 48:
    return 0x29; // 0 -> )
  case 49:
    return 0x21; // 1 -> !
  case 50:
    return 0x40; // 2 -> @
  case 51:
    return 0x23; // 3 -> #
  case 52:
    return 0x24; // 4 -> $
  case 53:
    return 0x25; // 5 -> %
  case 54:
    return 0x5E; // 6 -> ^
  case 55:
    return 0x26; // 7 -> &
  case 56:
    return 0x2A; // 8 -> *
  case 57:
    return 0x28; // 9 -> (

  // Punctuation shifted
  case 188:
    return 0x3C; // , -> <
  case 190:
    return 0x3E; // . -> >
  case 191:
    return 0x3F; // / -> ?
  case 186:
    return 0x3A; // ; -> :
  case 222:
    return 0x22; // ' -> "
  case 219:
    return 0x7B; // [ -> {
  case 221:
    return 0x7D; // ] -> }
  case 220:
    return 0x7C; // \ -> |
  case 189:
    return 0x5F; // - -> _
  case 187:
    return 0x2B; // = -> +
  case 192:
    return 0x7E; // ` -> ~

  default:
    return baseKey;
  }
}

int Keyboard::applyControl(int key) const {
  // Convert a-z to Ctrl+A-Z (0x01-0x1A)
  if (key >= 0x61 && key <= 0x7A) {
    return key - 0x60;
  }
  // Convert A-Z to Ctrl+A-Z
  if (key >= 0x41 && key <= 0x5A) {
    return key - 0x40;
  }
  return key;
}

int charToAppleKey(int charCode) {
  // Newline/CR -> CR
  if (charCode == 0x0A || charCode == 0x0D) {
    return 0x0D;
  }
  // Tab
  if (charCode == 0x09) {
    return 0x09;
  }
  // Printable ASCII (space through tilde)
  if (charCode >= 0x20 && charCode <= 0x7E) {
    return charCode;
  }
  // Not mappable
  return -1;
}

} // namespace a2e
