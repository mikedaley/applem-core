/*
 * condition_evaluator.hpp - Expression parser for debugger breakpoint conditions and watches
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <cstdint>
#include <cstring>

namespace a2e {

class Emulator;

/**
 * Evaluates breakpoint condition expressions.
 *
 * Syntax:
 *   Registers: A, X, Y, SP, PC, P
 *   Flags: C, Z, I, D, B, V, N (return 0 or 1)
 *   Memory: PEEK($addr), DEEK($addr) (16-bit little-endian)
 *   Hex literals: #$FF, $FFFF
 *   Decimal literals: 42
 *   Comparisons: ==, !=, >=, <=, >, <
 *   Arithmetic: +, -, *
 *   Logic: &&, ||
 *   Grouping: ( )
 */
class ConditionEvaluator {
public:
  /**
   * Evaluate a condition expression as a boolean.
   * Returns true if the condition is satisfied.
   */
  static bool evaluate(const char* expr, const Emulator& emu);

  /**
   * Evaluate an expression and return the raw numeric value.
   * Used for watch expressions.
   */
  static int32_t evaluateNumeric(const char* expr, const Emulator& emu);

  /**
   * Get the last error message (empty string if no error).
   */
  static const char* getLastError();

private:
  // Token types
  enum TokenType : uint8_t {
    TOK_NUM,     // Numeric literal
    TOK_ID,      // Identifier (register, PEEK, etc.)
    TOK_OP2,     // Two-character operator (==, !=, >=, <=, &&, ||)
    TOK_OP1,     // Single-character operator (<, >, +, -, *, (, ))
    TOK_END      // End of tokens
  };

  struct Token {
    TokenType type;
    int32_t numVal;     // For TOK_NUM
    char strVal[8];     // For TOK_ID or TOK_OP (null-terminated)
  };

  static constexpr int MAX_TOKENS = 128;

  struct ParseState {
    Token tokens[MAX_TOKENS];
    int count;
    int pos;
    const Emulator* emu;
  };

  static int tokenize(const char* expr, Token* tokens, int maxTokens);
  static bool parseOr(ParseState& s);
  static bool parseAnd(ParseState& s);
  static bool parseComparison(ParseState& s);
  static int32_t parseExpr(ParseState& s);
  static int32_t parseAtom(ParseState& s);

  static char errorBuf_[128];
};

} // namespace a2e
