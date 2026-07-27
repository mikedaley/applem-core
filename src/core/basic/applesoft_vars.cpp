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


// ============================================================================
// Table walking
// ============================================================================

namespace {

/** Read a little-endian zero-page pointer pair. */
uint16_t readPointer(const VarMemReadFn &read, uint16_t zpAddr) {
  return static_cast<uint16_t>(read(zpAddr) | (read(zpAddr + 1) << 8));
}

// Applesoft zero page: VARTAB/ARYTAB/STREND delimit the variable and array
// regions. TXTTAB is $0801, and the tables always sit above it.
constexpr uint16_t ZP_VARTAB = 0x69;
constexpr uint16_t ZP_ARYTAB = 0x6B;
constexpr uint16_t ZP_STREND = 0x6D;

constexpr uint16_t MIN_TABLE_ADDR = 0x0800;
constexpr uint16_t MAX_TABLE_ADDR = 0xC000; // I/O space starts here

/** Simple variables are a fixed seven bytes: 2 name + 5 value. */
constexpr uint16_t SIMPLE_VAR_SIZE = 7;

bool plausibleRange(uint16_t lo, uint16_t hi) {
  return lo != 0 && hi != 0 && lo < hi && lo >= MIN_TABLE_ADDR && hi <= MAX_TABLE_ADDR;
}

} // namespace

std::string ApplesoftVarReader::readString(const VarMemReadFn &read, uint16_t ptr,
                                           uint8_t length) {
  if (length == 0 || ptr == 0) return {};

  std::string out;
  out.reserve(length);
  for (uint8_t i = 0; i < length; i++) {
    // Applesoft stores text with the high bit set.
    out.push_back(static_cast<char>(read(static_cast<uint16_t>(ptr + i)) & 0x7F));
  }
  return out;
}

std::vector<BasicVariableInfo> ApplesoftVarReader::readVariables(const VarMemReadFn &read) {
  std::vector<BasicVariableInfo> variables;

  const uint16_t vartab = readPointer(read, ZP_VARTAB);
  const uint16_t arytab = readPointer(read, ZP_ARYTAB);
  if (!plausibleRange(vartab, arytab)) return variables;

  for (uint16_t addr = vartab; addr + SIMPLE_VAR_SIZE <= arytab; addr += SIMPLE_VAR_SIZE) {
    const uint8_t b1 = read(addr);
    const uint8_t b2 = read(static_cast<uint16_t>(addr + 1));

    // A zero first name byte marks the end of the used portion of the table.
    if (b1 == 0) break;

    BasicVariableInfo info;
    char name[APPLESOFT_NAME_MAX];
    ApplesoftVars::parseName(b1, b2, name, &info.type);
    info.name = name;
    info.address = addr;

    const uint16_t value = static_cast<uint16_t>(addr + 2);
    switch (info.type) {
    case BasicVarType::Integer:
      info.intValue = ApplesoftVars::decodeInteger(read(value),
                                                   read(static_cast<uint16_t>(value + 1)));
      break;
    case BasicVarType::String: {
      const uint8_t len = read(value);
      const uint16_t ptr = static_cast<uint16_t>(read(static_cast<uint16_t>(value + 1)) |
                                                 (read(static_cast<uint16_t>(value + 2)) << 8));
      info.stringValue = readString(read, ptr, len);
      break;
    }
    case BasicVarType::Real: {
      uint8_t bytes[APPLESOFT_FLOAT_SIZE];
      for (int i = 0; i < APPLESOFT_FLOAT_SIZE; i++) {
        bytes[i] = read(static_cast<uint16_t>(value + i));
      }
      info.realValue = ApplesoftVars::decodeFloat(bytes);
      break;
    }
    }

    variables.push_back(std::move(info));
  }

  return variables;
}

std::vector<BasicArrayInfo> ApplesoftVarReader::readArrays(const VarMemReadFn &read) {
  std::vector<BasicArrayInfo> arrays;

  const uint16_t arytab = readPointer(read, ZP_ARYTAB);
  const uint16_t strend = readPointer(read, ZP_STREND);
  if (!plausibleRange(arytab, strend)) return arrays;

  uint16_t addr = arytab;
  while (addr + 5 <= strend) {
    const uint8_t b1 = read(addr);
    const uint8_t b2 = read(static_cast<uint16_t>(addr + 1));
    if (b1 == 0) break;

    BasicArrayInfo info;
    char name[APPLESOFT_NAME_MAX];
    ApplesoftVars::parseName(b1, b2, name, &info.type);
    info.name = name;
    info.address = addr;

    // Header: 2 name + 2 total size (little-endian) + 1 dimension count.
    info.totalSize = static_cast<uint16_t>(read(static_cast<uint16_t>(addr + 2)) |
                                           (read(static_cast<uint16_t>(addr + 3)) << 8));
    info.numDims = read(static_cast<uint16_t>(addr + 4));

    // A zero or nonsensical total size would not advance the walk, so stop
    // rather than spin on a corrupt header.
    if (info.totalSize < 5 || addr + info.totalSize > strend) break;

    // Dimension sizes are big-endian, unlike the total size.
    uint32_t elementCount = info.numDims > 0 ? 1u : 0u;
    for (uint8_t d = 0; d < info.numDims; d++) {
      const uint16_t off = static_cast<uint16_t>(addr + 5 + d * 2);
      const uint16_t dim = static_cast<uint16_t>((read(off) << 8) |
                                                 read(static_cast<uint16_t>(off + 1)));
      info.dimensions.push_back(dim);
      elementCount *= dim;
    }

    if (elementCount > MAX_ARRAY_ELEMENTS) elementCount = MAX_ARRAY_ELEMENTS;
    info.elementCount = elementCount;

    const uint16_t dataStart = static_cast<uint16_t>(addr + 5 + info.numDims * 2);
    const int stride = ApplesoftVars::elementSize(info.type);

    for (uint32_t i = 0; i < elementCount; i++) {
      const uint16_t elem = static_cast<uint16_t>(dataStart + i * stride);

      switch (info.type) {
      case BasicVarType::Integer:
        info.intValues.push_back(
            ApplesoftVars::decodeInteger(read(elem), read(static_cast<uint16_t>(elem + 1))));
        break;
      case BasicVarType::String: {
        const uint8_t len = read(elem);
        const uint16_t ptr = static_cast<uint16_t>(read(static_cast<uint16_t>(elem + 1)) |
                                                   (read(static_cast<uint16_t>(elem + 2)) << 8));
        info.stringValues.push_back(readString(read, ptr, len));
        break;
      }
      case BasicVarType::Real: {
        uint8_t bytes[APPLESOFT_FLOAT_SIZE];
        for (int b = 0; b < APPLESOFT_FLOAT_SIZE; b++) {
          bytes[b] = read(static_cast<uint16_t>(elem + b));
        }
        info.realValues.push_back(ApplesoftVars::decodeFloat(bytes));
        break;
      }
      }
    }

    addr = static_cast<uint16_t>(addr + info.totalSize);
    arrays.push_back(std::move(info));
  }

  return arrays;
}

} // namespace a2e
