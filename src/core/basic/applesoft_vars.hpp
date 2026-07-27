/*
 * applesoft_vars.hpp - Applesoft variable representation primitives
 *
 * Applesoft stores variables in a fixed layout: a two-byte name whose high bits
 * carry the type, then a value that is a 5-byte MBF float, a 2-byte big-endian
 * integer, or a length-plus-pointer string descriptor.
 *
 * That knowledge had accumulated in three places — the condition evaluator (for
 * breakpoint expressions) and the debugger's variable inspector in JavaScript,
 * which also owned the only float *encoder*. This is the single home for it.
 *
 * These functions are pure: they take bytes and return values, with no
 * dependency on Emulator or on any memory-reading interface, which keeps them
 * directly testable and usable from every caller.
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <cstdint>

namespace a2e {

/** Applesoft variable types, as encoded in the high bits of the name bytes. */
enum class BasicVarType { Real, Integer, String };

/** Bytes in an Applesoft MBF float: 1 exponent + 4 mantissa. */
inline constexpr int APPLESOFT_FLOAT_SIZE = 5;

/** Longest printable name: two characters plus a `%` or `$` suffix, plus NUL. */
inline constexpr int APPLESOFT_NAME_MAX = 4;

class ApplesoftVars {
public:
  /**
   * Decode a 5-byte Applesoft float.
   *
   * Excess-129 exponent with an implied leading 1 in the mantissa, and the sign
   * in bit 7 of the first mantissa byte. A zero exponent means the value is
   * zero regardless of the mantissa.
   */
  static double decodeFloat(const uint8_t bytes[APPLESOFT_FLOAT_SIZE]);

  /**
   * Encode a double into Applesoft's 5-byte float.
   *
   * Values that cannot be represented — zero, and anything that under- or
   * overflows the excess-129 exponent — are written as Applesoft zero (all
   * bytes zero), which is what the interpreter itself stores.
   */
  static void encodeFloat(double value, uint8_t out[APPLESOFT_FLOAT_SIZE]);

  /**
   * Decode the two name bytes of a variable or array entry.
   *
   * The type lives in the high bits: both set is an integer, only the second
   * set is a string, neither is a real. `outName` receives the printable name
   * including its `%` or `$` suffix and must have room for APPLESOFT_NAME_MAX
   * bytes. A zero second character means a one-character name.
   */
  static void parseName(uint8_t byte1, uint8_t byte2, char *outName,
                        BasicVarType *outType);

  /** Bytes each element of this type occupies inside an array's data area. */
  static int elementSize(BasicVarType type);

  /** Decode a 2-byte big-endian Applesoft integer as signed. */
  static int32_t decodeInteger(uint8_t high, uint8_t low);
};

} // namespace a2e
