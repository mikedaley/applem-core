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
