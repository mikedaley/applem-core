/*
 * basic_detokenizer.hpp - Applesoft and Integer BASIC program detokenizer
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace a2e {

class BasicDetokenizer {
public:
  /**
   * Detokenize Applesoft BASIC program to plain text.
   * Output includes line numbers and proper keyword spacing.
   * Returns pointer to internal static buffer (valid until next call).
   */
  static const char* detokenizeApplesoft(const uint8_t* data, int size, bool hasLengthHeader);

  /**
   * Detokenize Integer BASIC program to plain text.
   * Returns pointer to internal static buffer (valid until next call).
   */
  static const char* detokenizeIntegerBasic(const uint8_t* data, int size, bool hasLengthHeader);

private:
  static constexpr int MAX_OUTPUT = 256 * 1024; // 256KB max output
  static char outputBuffer_[MAX_OUTPUT];
  static int outputLen_;

  static void appendChar(char c);
  static void appendStr(const char* s);
  static void appendInt(int n);
  static void appendPaddedLineNum(int n);
};

} // namespace a2e
