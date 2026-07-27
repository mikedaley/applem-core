/*
 * applesoft_vars.cpp - Applesoft variable representation primitives
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "applesoft_vars.hpp"

#include <cmath>

namespace a2e {

double ApplesoftVars::decodeFloat(const uint8_t bytes[APPLESOFT_FLOAT_SIZE]) {
  const uint8_t exp = bytes[0];

  // A zero exponent is Applesoft's representation of zero; the mantissa bytes
  // are not meaningful in that case.
  if (exp == 0) return 0.0;

  const double sign = (bytes[1] & 0x80) ? -1.0 : 1.0;

  // Normalized 1.xxxx form: the leading 1 is implied, so start there and add
  // the stored fraction bits. Bit 7 of the first mantissa byte is the sign, so
  // only its low 7 bits contribute.
  double mantissa = 1.0;
  mantissa += (bytes[1] & 0x7F) / 128.0;
  mantissa += bytes[2] / 32768.0;
  mantissa += bytes[3] / 8388608.0;
  mantissa += bytes[4] / 2147483648.0;

  // Excess-129: the implied 1 sits at 2^-1 rather than 2^0.
  return sign * mantissa * std::pow(2.0, static_cast<int>(exp) - 129);
}

void ApplesoftVars::encodeFloat(double value, uint8_t out[APPLESOFT_FLOAT_SIZE]) {
  for (int i = 0; i < APPLESOFT_FLOAT_SIZE; i++) out[i] = 0;

  // Zero, and anything not finite, is stored as Applesoft zero.
  if (value == 0.0 || !std::isfinite(value)) return;

  const int sign = value < 0.0 ? 1 : 0;
  double abs = std::fabs(value);

  // Normalise so that 1.0 <= mantissa < 2.0.
  int exp = static_cast<int>(std::floor(std::log2(abs)));
  double mantissa = abs / std::pow(2.0, exp);

  // log2 is not exact at powers of two, so nudge the result back into range.
  if (mantissa < 1.0) { mantissa *= 2.0; exp--; }
  if (mantissa >= 2.0) { mantissa /= 2.0; exp++; }

  const int expByte = exp + 129;
  if (expByte <= 0 || expByte > 255) return; // under/overflow -> zero

  out[0] = static_cast<uint8_t>(expByte);

  // Drop the implied leading 1 and spread 31 fraction bits over bytes 1..4.
  // Byte 1 holds 7 fraction bits with the sign in bit 7.
  mantissa -= 1.0;

  mantissa *= 128.0;
  out[1] = static_cast<uint8_t>((static_cast<int>(std::floor(mantissa)) & 0x7F) | (sign << 7));
  mantissa -= std::floor(mantissa);

  mantissa *= 256.0;
  out[2] = static_cast<uint8_t>(static_cast<int>(std::floor(mantissa)) & 0xFF);
  mantissa -= std::floor(mantissa);

  mantissa *= 256.0;
  out[3] = static_cast<uint8_t>(static_cast<int>(std::floor(mantissa)) & 0xFF);
  mantissa -= std::floor(mantissa);

  mantissa *= 256.0;
  out[4] = static_cast<uint8_t>(static_cast<int>(std::lround(mantissa)) & 0xFF);
}

void ApplesoftVars::parseName(uint8_t byte1, uint8_t byte2, char *outName,
                              BasicVarType *outType) {
  const char char1 = static_cast<char>(byte1 & 0x7F);
  const char char2 = static_cast<char>(byte2 & 0x7F);

  const bool isInteger = (byte1 & 0x80) != 0 && (byte2 & 0x80) != 0;
  const bool isString = !isInteger && (byte2 & 0x80) != 0;

  BasicVarType type = BasicVarType::Real;
  char suffix = '\0';
  if (isInteger) {
    type = BasicVarType::Integer;
    suffix = '%';
  } else if (isString) {
    type = BasicVarType::String;
    suffix = '$';
  }

  int n = 0;
  outName[n++] = char1;
  if (char2 != '\0') outName[n++] = char2; // one-character names pad with NUL
  if (suffix != '\0') outName[n++] = suffix;
  outName[n] = '\0';

  if (outType) *outType = type;
}

int ApplesoftVars::elementSize(BasicVarType type) {
  switch (type) {
  case BasicVarType::Integer: return 2; // high, low
  case BasicVarType::String:  return 3; // length, pointer low, pointer high
  case BasicVarType::Real:    return APPLESOFT_FLOAT_SIZE;
  }
  return APPLESOFT_FLOAT_SIZE;
}

int32_t ApplesoftVars::decodeInteger(uint8_t high, uint8_t low) {
  int32_t value = (static_cast<int32_t>(high) << 8) | low;
  if (value >= 0x8000) value -= 0x10000; // two's complement
  return value;
}

} // namespace a2e
