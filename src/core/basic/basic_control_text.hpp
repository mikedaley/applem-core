/*
 * basic_control_text.hpp - Control characters in a BASIC listing, as text
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace a2e::basic_text {

// An Applesoft program can hold bytes a listing cannot show: PRINT "^DCATALOG"
// carries a Control-D in its string, and REM and DATA keep whatever was typed.
// A listing that dropped them wrote a different program back, so a control
// character is written into the text as a token in braces and read back from
// it: {ctrl-d} for $04, {ctrl-[} for Escape, {del} for $7F. These are the
// tokens the browser already reads in pasted and typed text (input-handler.js,
// specialKeyToAppleCode), so a listing can be pasted into either front end;
// {^d}, {ctrl+d} and {chr:4} (decimal, $hex or 0xhex) read the same way.
//
// Only the bytes a listing cannot otherwise show are tokens. $00 never is,
// because it ends a line in memory, and a token naming a printable character
// is left as text, with one exception: a { that would itself read as a token
// is written {chr:123}, so a program that really does say {ctrl-d} in a string
// comes back saying it.

// Whether a byte has to be written as a token.
inline bool needsToken(uint8_t value) {
  return (value >= 0x01 && value < 0x20) || value == 0x7F;
}

// The token at text[pos], if there is one there: its value and how many
// characters it spans.
inline bool decodeToken(std::string_view text, size_t pos, uint8_t &value, size_t &length) {
  if (pos >= text.size() || text[pos] != '{') return false;
  const size_t close = text.find('}', pos + 1);
  if (close == std::string_view::npos || close - pos > 12) return false;
  std::string body;
  for (size_t i = pos + 1; i < close; i++) {
    body += static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
  }
  int v = -1;
  if (body == "del" || body == "delete") {
    v = 0x7F;
  } else if ((body.size() == 6 && (body.compare(0, 5, "ctrl-") == 0 || body.compare(0, 5, "ctrl+") == 0)) ||
             (body.size() == 2 && body[0] == '^')) {
    const unsigned char c = static_cast<unsigned char>(body.back());
    if (c >= 'a' && c <= 'z') v = c - 0x60;
    else if (c >= 0x40 && c <= 0x5F) v = c & 0x1F;
  } else if (body.compare(0, 4, "chr:") == 0 && body.size() > 4) {
    std::string digits = body.substr(4);
    int base = 10;
    if (digits[0] == '$') digits = digits.substr(1), base = 16;
    else if (digits.compare(0, 2, "0x") == 0) digits = digits.substr(2), base = 16;
    if (!digits.empty() && digits.size() <= 3) {
      v = 0;
      for (char c : digits) {
        const int d = std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
                      : base == 16 && c >= 'a' && c <= 'f' ? c - 'a' + 10
                                                          : -1;
        if (d < 0) {
          v = -1;
          break;
        }
        v = v * base + d;
      }
      if (v > 0xFF) v = -1;
    }
  }
  if (v < 0) return false;
  v &= 0x7F; // as the browser reads them: Applesoft's text is seven bits
  if (!needsToken(static_cast<uint8_t>(v)) && v != '{') return false;
  value = static_cast<uint8_t>(v);
  length = close - pos + 1;
  return true;
}

// The text for a control character.
inline std::string tokenFor(uint8_t value) {
  if (value == 0x7F) return "{del}";
  std::string out = "{ctrl-";
  out += static_cast<char>(value >= 0x01 && value <= 0x1A ? 'a' + value - 1 : 0x40 + (value & 0x1F));
  return out + "}";
}

// Bytes as a listing shows them: control characters as tokens, and a { that
// would read as one escaped. Every other byte is itself.
inline std::string encode(std::string_view raw) {
  std::string out;
  out.reserve(raw.size());
  for (size_t i = 0; i < raw.size(); i++) {
    const uint8_t c = static_cast<uint8_t>(raw[i]);
    uint8_t v = 0;
    size_t n = 0;
    if (needsToken(c)) out += tokenFor(c);
    else if (c == '{' && decodeToken(raw, i, v, n)) out += "{chr:123}";
    else out += static_cast<char>(c);
  }
  return out;
}

// A listing's text back into bytes: encode()'s inverse.
inline std::string decode(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    uint8_t v = 0;
    size_t n = 0;
    if (decodeToken(text, i, v, n)) {
      out += static_cast<char>(v);
      i += n;
    } else {
      out += text[i++];
    }
  }
  return out;
}

} // namespace a2e::basic_text
