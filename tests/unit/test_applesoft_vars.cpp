/*
 * test_applesoft_vars.cpp - Tests for Applesoft variable representation
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "basic/applesoft_vars.hpp"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace a2e;

namespace {

/** Build a float byte array literally, for asserting against known encodings. */
struct Float5 {
  uint8_t b[APPLESOFT_FLOAT_SIZE];
  Float5(uint8_t e, uint8_t m1, uint8_t m2, uint8_t m3, uint8_t m4)
      : b{e, m1, m2, m3, m4} {}
};

std::string nameOf(uint8_t b1, uint8_t b2, BasicVarType *type = nullptr) {
  char buf[APPLESOFT_NAME_MAX];
  BasicVarType t;
  ApplesoftVars::parseName(b1, b2, buf, &t);
  if (type) *type = t;
  return std::string(buf);
}

} // namespace

// ============================================================================
// Float decoding
// ============================================================================

TEST_CASE("decodeFloat known encodings", "[applesoft][float]") {
  // Exponent is excess-129 with an implied leading 1, so $81 means 2^0.
  CHECK(ApplesoftVars::decodeFloat(Float5(0x81, 0x00, 0x00, 0x00, 0x00).b) == Approx(1.0));
  CHECK(ApplesoftVars::decodeFloat(Float5(0x81, 0x80, 0x00, 0x00, 0x00).b) == Approx(-1.0));
  CHECK(ApplesoftVars::decodeFloat(Float5(0x80, 0x00, 0x00, 0x00, 0x00).b) == Approx(0.5));
  CHECK(ApplesoftVars::decodeFloat(Float5(0x82, 0x00, 0x00, 0x00, 0x00).b) == Approx(2.0));

  // 10.0 = 1.25 * 2^3 -> exponent 3+129 = $84, fraction 0.25 -> 0.25*128 = $20
  CHECK(ApplesoftVars::decodeFloat(Float5(0x84, 0x20, 0x00, 0x00, 0x00).b) == Approx(10.0));
}

TEST_CASE("decodeFloat treats a zero exponent as zero", "[applesoft][float]") {
  // The mantissa is deliberately non-zero: a zero exponent wins regardless.
  CHECK(ApplesoftVars::decodeFloat(Float5(0x00, 0xFF, 0xFF, 0xFF, 0xFF).b) == 0.0);
}

TEST_CASE("decodeFloat carries the sign from mantissa bit 7", "[applesoft][float]") {
  const double positive = ApplesoftVars::decodeFloat(Float5(0x84, 0x20, 0x00, 0x00, 0x00).b);
  const double negative = ApplesoftVars::decodeFloat(Float5(0x84, 0xA0, 0x00, 0x00, 0x00).b);

  CHECK(positive == Approx(10.0));
  CHECK(negative == Approx(-10.0));
}

// ============================================================================
// Float encoding
// ============================================================================

TEST_CASE("encodeFloat produces the canonical encodings", "[applesoft][float]") {
  uint8_t out[APPLESOFT_FLOAT_SIZE];

  ApplesoftVars::encodeFloat(1.0, out);
  CHECK(memcmp(out, Float5(0x81, 0x00, 0x00, 0x00, 0x00).b, APPLESOFT_FLOAT_SIZE) == 0);

  ApplesoftVars::encodeFloat(-1.0, out);
  CHECK(memcmp(out, Float5(0x81, 0x80, 0x00, 0x00, 0x00).b, APPLESOFT_FLOAT_SIZE) == 0);

  ApplesoftVars::encodeFloat(10.0, out);
  CHECK(memcmp(out, Float5(0x84, 0x20, 0x00, 0x00, 0x00).b, APPLESOFT_FLOAT_SIZE) == 0);
}

TEST_CASE("encodeFloat writes Applesoft zero for zero", "[applesoft][float]") {
  uint8_t out[APPLESOFT_FLOAT_SIZE];
  ApplesoftVars::encodeFloat(0.0, out);

  for (int i = 0; i < APPLESOFT_FLOAT_SIZE; i++) CHECK(out[i] == 0);
}

TEST_CASE("encodeFloat clamps out-of-range magnitudes to zero", "[applesoft][float]") {
  uint8_t out[APPLESOFT_FLOAT_SIZE];

  // The excess-129 exponent spans roughly 2^-129 .. 2^126; beyond that the
  // interpreter itself stores zero rather than a wrapped exponent.
  ApplesoftVars::encodeFloat(1e300, out);
  for (int i = 0; i < APPLESOFT_FLOAT_SIZE; i++) CHECK(out[i] == 0);

  ApplesoftVars::encodeFloat(1e-300, out);
  for (int i = 0; i < APPLESOFT_FLOAT_SIZE; i++) CHECK(out[i] == 0);
}

TEST_CASE("encodeFloat round-trips through decodeFloat", "[applesoft][float]") {
  // 31 stored mantissa bits give about nine significant decimal digits.
  const double values[] = {
    1.0, -1.0, 0.5, -0.5, 2.0, 10.0, -10.0, 3.14159265, -2.718281828,
    100.0, 0.001, 65535.0, -32768.0, 1.0 / 3.0, 1e10, 1e-10,
  };

  uint8_t out[APPLESOFT_FLOAT_SIZE];
  for (double v : values) {
    ApplesoftVars::encodeFloat(v, out);
    CHECK(ApplesoftVars::decodeFloat(out) == Approx(v).epsilon(1e-9));
  }
}

TEST_CASE("encodeFloat handles powers of two exactly", "[applesoft][float]") {
  // log2() is not exact at powers of two, so this is where the normalisation
  // nudge in encodeFloat earns its place.
  uint8_t out[APPLESOFT_FLOAT_SIZE];
  for (int e = -20; e <= 20; e++) {
    const double v = std::pow(2.0, e);
    ApplesoftVars::encodeFloat(v, out);
    CHECK(ApplesoftVars::decodeFloat(out) == Approx(v));
  }
}

// ============================================================================
// Names and types
// ============================================================================

TEST_CASE("parseName decodes type from the name high bits", "[applesoft][name]") {
  BasicVarType type;

  // Neither high bit: real.
  CHECK(nameOf('A', 'B', &type) == "AB");
  CHECK(type == BasicVarType::Real);

  // Second high bit only: string.
  CHECK(nameOf('A', 'B' | 0x80, &type) == "AB$");
  CHECK(type == BasicVarType::String);

  // Both high bits: integer.
  CHECK(nameOf('A' | 0x80, 'B' | 0x80, &type) == "AB%");
  CHECK(type == BasicVarType::Integer);
}

TEST_CASE("parseName handles one-character names", "[applesoft][name]") {
  BasicVarType type;

  CHECK(nameOf('X', 0, &type) == "X");
  CHECK(type == BasicVarType::Real);

  // A one-character string: the suffix must still follow the single letter.
  CHECK(nameOf('X', 0x80, &type) == "X$");
  CHECK(type == BasicVarType::String);

  CHECK(nameOf('X' | 0x80, 0x80, &type) == "X%");
  CHECK(type == BasicVarType::Integer);
}

TEST_CASE("elementSize matches the in-array layout", "[applesoft][array]") {
  CHECK(ApplesoftVars::elementSize(BasicVarType::Integer) == 2);
  CHECK(ApplesoftVars::elementSize(BasicVarType::String) == 3);
  CHECK(ApplesoftVars::elementSize(BasicVarType::Real) == APPLESOFT_FLOAT_SIZE);
}

// ============================================================================
// Integers
// ============================================================================

TEST_CASE("decodeInteger is big-endian and signed", "[applesoft][integer]") {
  CHECK(ApplesoftVars::decodeInteger(0x00, 0x00) == 0);
  CHECK(ApplesoftVars::decodeInteger(0x00, 0x01) == 1);
  CHECK(ApplesoftVars::decodeInteger(0x01, 0x00) == 256);
  CHECK(ApplesoftVars::decodeInteger(0x7F, 0xFF) == 32767);

  // Values at or above $8000 are negative.
  CHECK(ApplesoftVars::decodeInteger(0xFF, 0xFF) == -1);
  CHECK(ApplesoftVars::decodeInteger(0x80, 0x00) == -32768);
}

// ============================================================================
// Table walking
// ============================================================================

namespace {

/** A 64K memory image that tests can lay Applesoft tables into by hand. */
struct FakeMemory {
  std::vector<uint8_t> ram = std::vector<uint8_t>(0x10000, 0);

  VarMemReadFn reader() {
    return [this](uint16_t addr) { return ram[addr]; };
  }

  void writePointer(uint16_t zp, uint16_t value) {
    ram[zp] = value & 0xFF;
    ram[zp + 1] = (value >> 8) & 0xFF;
  }

  /** Lay down a simple variable entry: 2 name bytes + 5 value bytes. */
  void putVar(uint16_t addr, uint8_t b1, uint8_t b2, std::initializer_list<uint8_t> value) {
    ram[addr] = b1;
    ram[addr + 1] = b2;
    int i = 0;
    for (uint8_t v : value) ram[addr + 2 + i++] = v;
  }

  /** Applesoft stores string bodies with the high bit set. */
  void putString(uint16_t addr, const std::string &text) {
    for (size_t i = 0; i < text.size(); i++) {
      ram[addr + i] = static_cast<uint8_t>(text[i]) | 0x80;
    }
  }
};

constexpr uint16_t VARTAB = 0x0900;

} // namespace

TEST_CASE("readVariables decodes each variable type", "[applesoft][reader]") {
  FakeMemory mem;

  // Three 7-byte entries: a real, an integer and a string.
  mem.putVar(VARTAB + 0, 'A', 'B', {0x84, 0x20, 0x00, 0x00, 0x00}); // AB = 10.0
  mem.putVar(VARTAB + 7, 'C' | 0x80, 'D' | 0x80, {0xFF, 0xFF, 0, 0, 0}); // CD% = -1
  mem.putVar(VARTAB + 14, 'E', 'F' | 0x80, {5, 0x00, 0x0A, 0, 0}); // EF$ -> $0A00, len 5
  mem.putString(0x0A00, "HELLO");

  mem.writePointer(0x69, VARTAB);
  mem.writePointer(0x6B, VARTAB + 21); // ARYTAB
  mem.writePointer(0x6D, VARTAB + 21); // STREND

  const auto vars = ApplesoftVarReader::readVariables(mem.reader());

  REQUIRE(vars.size() == 3);

  CHECK(vars[0].name == "AB");
  CHECK(vars[0].type == BasicVarType::Real);
  CHECK(vars[0].realValue == Approx(10.0));
  CHECK(vars[0].address == VARTAB);

  CHECK(vars[1].name == "CD%");
  CHECK(vars[1].type == BasicVarType::Integer);
  CHECK(vars[1].intValue == -1);

  CHECK(vars[2].name == "EF$");
  CHECK(vars[2].type == BasicVarType::String);
  CHECK(vars[2].stringValue == "HELLO");
}

TEST_CASE("readVariables stops at an empty slot", "[applesoft][reader]") {
  FakeMemory mem;
  mem.putVar(VARTAB, 'A', 'B', {0x81, 0, 0, 0, 0});
  // Next entry left as zeros: a zero name byte ends the table.

  mem.writePointer(0x69, VARTAB);
  mem.writePointer(0x6B, VARTAB + 70);
  mem.writePointer(0x6D, VARTAB + 70);

  CHECK(ApplesoftVarReader::readVariables(mem.reader()).size() == 1);
}

TEST_CASE("readVariables rejects implausible pointers", "[applesoft][reader]") {
  FakeMemory mem;

  auto empty = [&] { return ApplesoftVarReader::readVariables(mem.reader()).empty(); };

  mem.writePointer(0x69, 0);      mem.writePointer(0x6B, 0x1000); CHECK(empty());
  mem.writePointer(0x69, 0x1000); mem.writePointer(0x6B, 0x1000); CHECK(empty()); // equal
  mem.writePointer(0x69, 0x2000); mem.writePointer(0x6B, 0x1000); CHECK(empty()); // inverted
  mem.writePointer(0x69, 0x0100); mem.writePointer(0x6B, 0x1000); CHECK(empty()); // below TXTTAB
  mem.writePointer(0x69, 0x1000); mem.writePointer(0x6B, 0xD000); CHECK(empty()); // into I/O
}

TEST_CASE("readArrays decodes dimensions and elements", "[applesoft][reader][array]") {
  FakeMemory mem;

  // A(3): header 2 name + 2 total size + 1 numDims + 2 per dimension, then
  // three 5-byte reals holding 1.0, 2.0 and 10.0.
  const uint16_t base = 0x0900;
  const uint16_t total = 5 + 2 + 3 * 5; // 22
  mem.ram[base] = 'A';
  mem.ram[base + 1] = 0;
  mem.ram[base + 2] = total & 0xFF;      // total size is little-endian
  mem.ram[base + 3] = (total >> 8) & 0xFF;
  mem.ram[base + 4] = 1;                 // one dimension
  mem.ram[base + 5] = 0;                 // dimension size is BIG-endian
  mem.ram[base + 6] = 3;

  const uint16_t data = base + 7;
  const uint8_t one[] = {0x81, 0x00, 0x00, 0x00, 0x00};
  const uint8_t two[] = {0x82, 0x00, 0x00, 0x00, 0x00};
  const uint8_t ten[] = {0x84, 0x20, 0x00, 0x00, 0x00};
  for (int i = 0; i < 5; i++) {
    mem.ram[data + i] = one[i];
    mem.ram[data + 5 + i] = two[i];
    mem.ram[data + 10 + i] = ten[i];
  }

  mem.writePointer(0x6B, base);          // ARYTAB
  mem.writePointer(0x6D, base + total);  // STREND

  const auto arrays = ApplesoftVarReader::readArrays(mem.reader());

  REQUIRE(arrays.size() == 1);
  CHECK(arrays[0].name == "A");
  CHECK(arrays[0].type == BasicVarType::Real);
  CHECK(arrays[0].numDims == 1);
  CHECK(arrays[0].dimensions == std::vector<uint16_t>{3});
  CHECK(arrays[0].elementCount == 3);
  REQUIRE(arrays[0].realValues.size() == 3);
  CHECK(arrays[0].realValues[0] == Approx(1.0));
  CHECK(arrays[0].realValues[1] == Approx(2.0));
  CHECK(arrays[0].realValues[2] == Approx(10.0));
}

TEST_CASE("readArrays does not spin on a corrupt total size", "[applesoft][reader][array]") {
  FakeMemory mem;
  const uint16_t base = 0x0900;

  mem.ram[base] = 'A';
  mem.ram[base + 1] = 0;
  mem.ram[base + 2] = 0; // total size 0 would never advance the walk
  mem.ram[base + 3] = 0;
  mem.ram[base + 4] = 1;

  mem.writePointer(0x6B, base);
  mem.writePointer(0x6D, base + 100);

  // Must terminate rather than loop forever.
  CHECK(ApplesoftVarReader::readArrays(mem.reader()).empty());
}

TEST_CASE("readString masks the Applesoft high bit", "[applesoft][reader][string]") {
  FakeMemory mem;
  mem.putString(0x0A00, "HI!");

  CHECK(ApplesoftVarReader::readString(mem.reader(), 0x0A00, 3) == "HI!");
  CHECK(ApplesoftVarReader::readString(mem.reader(), 0x0A00, 0).empty());
  CHECK(ApplesoftVarReader::readString(mem.reader(), 0, 5).empty());
}
