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
#include <functional>
#include <string>
#include <vector>

namespace a2e {

/** Reads one byte of emulated memory. Matches basic_tokenizer's convention. */
using VarMemReadFn = std::function<uint8_t(uint16_t)>;

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

/** One simple (non-array) variable, with its value already decoded. */
struct BasicVariableInfo {
  std::string name;
  BasicVarType type = BasicVarType::Real;
  uint16_t address = 0;
  double realValue = 0.0;  // Real
  int32_t intValue = 0;    // Integer
  std::string stringValue; // String
};

/** One array variable, with every element decoded. */
struct BasicArrayInfo {
  std::string name;
  BasicVarType type = BasicVarType::Real;
  uint16_t address = 0;
  uint16_t totalSize = 0;
  uint8_t numDims = 0;
  std::vector<uint16_t> dimensions;
  uint32_t elementCount = 0;
  // Only the vector matching `type` is populated.
  std::vector<double> realValues;
  std::vector<int32_t> intValues;
  std::vector<std::string> stringValues;
};

/**
 * Walks Applesoft's variable and array tables in emulated memory.
 *
 * Applesoft lays these out as two contiguous regions: simple variables from
 * VARTAB ($69) to ARYTAB ($6B), each a fixed seven bytes, then arrays from
 * ARYTAB to STREND ($6D), each self-describing via a total-size field.
 *
 * Reading them here rather than in the debugger UI keeps the format knowledge
 * next to the interpreter it belongs to, and collapses what used to be one
 * memory read per byte of array data into a single call.
 */
class ApplesoftVarReader {
public:
  /**
   * Arrays can address far more elements than a debugger can usefully show, and
   * a corrupt header can claim an absurd count, so enumeration stops here.
   */
  static constexpr uint32_t MAX_ARRAY_ELEMENTS = 10000;

  /** Read all simple variables between VARTAB and ARYTAB. */
  static std::vector<BasicVariableInfo> readVariables(const VarMemReadFn &read);

  /** Read all arrays between ARYTAB and STREND, including element values. */
  static std::vector<BasicArrayInfo> readArrays(const VarMemReadFn &read);

  /** Read a string body, masking off the high bit Applesoft leaves set. */
  static std::string readString(const VarMemReadFn &read, uint16_t ptr, uint8_t length);
};

} // namespace a2e
