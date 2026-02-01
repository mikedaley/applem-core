#include "condition_evaluator.hpp"
#include "../emulator.hpp"
#include <cctype>
#include <cstdio>

namespace a2e {

char ConditionEvaluator::errorBuf_[128] = "";

int ConditionEvaluator::tokenize(const char* expr, Token* tokens, int maxTokens) {
  int count = 0;
  int i = 0;
  int len = static_cast<int>(strlen(expr));

  while (i < len && count < maxTokens - 1) {
    // Skip whitespace
    if (expr[i] == ' ' || expr[i] == '\t') {
      i++;
      continue;
    }

    // Two-char operators
    if (i + 1 < len) {
      char c0 = expr[i], c1 = expr[i + 1];
      if ((c0 == '=' && c1 == '=') || (c0 == '!' && c1 == '=') ||
          (c0 == '>' && c1 == '=') || (c0 == '<' && c1 == '=') ||
          (c0 == '&' && c1 == '&') || (c0 == '|' && c1 == '|')) {
        tokens[count].type = TOK_OP2;
        tokens[count].strVal[0] = c0;
        tokens[count].strVal[1] = c1;
        tokens[count].strVal[2] = '\0';
        count++;
        i += 2;
        continue;
      }
    }

    // Single-char operators
    char ch = expr[i];
    if (ch == '<' || ch == '>' || ch == '(' || ch == ')' ||
        ch == '+' || ch == '-' || ch == '*') {
      tokens[count].type = TOK_OP1;
      tokens[count].strVal[0] = ch;
      tokens[count].strVal[1] = '\0';
      count++;
      i++;
      continue;
    }

    // Hex literal: #$XX or $XXXX
    if (ch == '#' && i + 1 < len && expr[i + 1] == '$') {
      i += 2;
      int32_t val = 0;
      while (i < len && isxdigit(static_cast<unsigned char>(expr[i]))) {
        int digit;
        char c = expr[i];
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else digit = c - 'A' + 10;
        val = (val << 4) | digit;
        i++;
      }
      tokens[count].type = TOK_NUM;
      tokens[count].numVal = val;
      count++;
      continue;
    }

    if (ch == '$') {
      i++;
      int32_t val = 0;
      while (i < len && isxdigit(static_cast<unsigned char>(expr[i]))) {
        int digit;
        char c = expr[i];
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else digit = c - 'A' + 10;
        val = (val << 4) | digit;
        i++;
      }
      tokens[count].type = TOK_NUM;
      tokens[count].numVal = val;
      count++;
      continue;
    }

    // Decimal literal
    if (isdigit(static_cast<unsigned char>(ch))) {
      int32_t val = 0;
      while (i < len && isdigit(static_cast<unsigned char>(expr[i]))) {
        val = val * 10 + (expr[i] - '0');
        i++;
      }
      tokens[count].type = TOK_NUM;
      tokens[count].numVal = val;
      count++;
      continue;
    }

    // Identifiers
    if (isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
      int start = i;
      while (i < len && (isalnum(static_cast<unsigned char>(expr[i])) || expr[i] == '_')) {
        i++;
      }
      int idLen = i - start;
      if (idLen > 7) idLen = 7; // Truncate to fit strVal
      tokens[count].type = TOK_ID;
      for (int j = 0; j < idLen; j++) {
        tokens[count].strVal[j] = toupper(static_cast<unsigned char>(expr[start + j]));
      }
      tokens[count].strVal[idLen] = '\0';
      count++;
      continue;
    }

    // Unknown char, skip
    i++;
  }

  // End sentinel
  tokens[count].type = TOK_END;
  return count;
}

bool ConditionEvaluator::evaluate(const char* expr, const Emulator& emu) {
  errorBuf_[0] = '\0';
  ParseState s;
  s.count = tokenize(expr, s.tokens, MAX_TOKENS);
  s.pos = 0;
  s.emu = &emu;

  bool result = parseOr(s);
  return result;
}

int32_t ConditionEvaluator::evaluateNumeric(const char* expr, const Emulator& emu) {
  errorBuf_[0] = '\0';
  ParseState s;
  s.count = tokenize(expr, s.tokens, MAX_TOKENS);
  s.pos = 0;
  s.emu = &emu;

  return parseExpr(s);
}

const char* ConditionEvaluator::getLastError() {
  return errorBuf_;
}

bool ConditionEvaluator::parseOr(ParseState& s) {
  bool left = parseAnd(s);
  while (s.pos < s.count && s.tokens[s.pos].type == TOK_OP2 &&
         s.tokens[s.pos].strVal[0] == '|') {
    s.pos++;
    bool right = parseAnd(s);
    left = left || right;
  }
  return left;
}

bool ConditionEvaluator::parseAnd(ParseState& s) {
  bool left = parseComparison(s);
  while (s.pos < s.count && s.tokens[s.pos].type == TOK_OP2 &&
         s.tokens[s.pos].strVal[0] == '&') {
    s.pos++;
    bool right = parseComparison(s);
    left = left && right;
  }
  return left;
}

bool ConditionEvaluator::parseComparison(ParseState& s) {
  int32_t left = parseExpr(s);

  if (s.pos < s.count) {
    const Token& tok = s.tokens[s.pos];
    if (tok.type == TOK_OP2 || tok.type == TOK_OP1) {
      const char* op = tok.strVal;
      if (strcmp(op, "==") == 0) { s.pos++; return left == parseExpr(s); }
      if (strcmp(op, "!=") == 0) { s.pos++; return left != parseExpr(s); }
      if (strcmp(op, ">=") == 0) { s.pos++; return left >= parseExpr(s); }
      if (strcmp(op, "<=") == 0) { s.pos++; return left <= parseExpr(s); }
      if (strcmp(op, ">") == 0)  { s.pos++; return left > parseExpr(s); }
      if (strcmp(op, "<") == 0)  { s.pos++; return left < parseExpr(s); }
    }
  }

  return left != 0; // Truthy if no comparison
}

int32_t ConditionEvaluator::parseExpr(ParseState& s) {
  int32_t val = parseAtom(s);

  while (s.pos < s.count) {
    const Token& tok = s.tokens[s.pos];
    if (tok.type == TOK_OP1) {
      char op = tok.strVal[0];
      if (op == '+') { s.pos++; val += parseAtom(s); }
      else if (op == '-') { s.pos++; val -= parseAtom(s); }
      else if (op == '*') { s.pos++; val *= parseAtom(s); }
      else break;
    } else break;
  }

  return val;
}

int32_t ConditionEvaluator::parseAtom(ParseState& s) {
  if (s.pos >= s.count) return 0;

  const Token& t = s.tokens[s.pos];

  // Number literal
  if (t.type == TOK_NUM) {
    s.pos++;
    return t.numVal;
  }

  // Parenthesized expression
  if (t.type == TOK_OP1 && t.strVal[0] == '(') {
    s.pos++;
    int32_t val = static_cast<int32_t>(parseOr(s));
    if (s.pos < s.count && s.tokens[s.pos].type == TOK_OP1 &&
        s.tokens[s.pos].strVal[0] == ')') {
      s.pos++;
    }
    return val;
  }

  // Identifier
  if (t.type == TOK_ID) {
    s.pos++;
    const char* id = t.strVal;
    const Emulator& emu = *s.emu;

    // PEEK(addr) - read byte
    if (strcmp(id, "PEEK") == 0 && s.pos < s.count &&
        s.tokens[s.pos].type == TOK_OP1 && s.tokens[s.pos].strVal[0] == '(') {
      s.pos++;
      int32_t addr = static_cast<int32_t>(parseOr(s));
      if (s.pos < s.count && s.tokens[s.pos].type == TOK_OP1 &&
          s.tokens[s.pos].strVal[0] == ')') {
        s.pos++;
      }
      return emu.peekMemory(static_cast<uint16_t>(addr & 0xFFFF));
    }

    // DEEK(addr) - read 16-bit word (little-endian)
    if (strcmp(id, "DEEK") == 0 && s.pos < s.count &&
        s.tokens[s.pos].type == TOK_OP1 && s.tokens[s.pos].strVal[0] == '(') {
      s.pos++;
      int32_t addr = static_cast<int32_t>(parseOr(s));
      if (s.pos < s.count && s.tokens[s.pos].type == TOK_OP1 &&
          s.tokens[s.pos].strVal[0] == ')') {
        s.pos++;
      }
      uint8_t lo = emu.peekMemory(static_cast<uint16_t>(addr & 0xFFFF));
      uint8_t hi = emu.peekMemory(static_cast<uint16_t>((addr + 1) & 0xFFFF));
      return (hi << 8) | lo;
    }

    // Registers
    if (strcmp(id, "A") == 0)  return emu.getA();
    if (strcmp(id, "X") == 0)  return emu.getX();
    if (strcmp(id, "Y") == 0)  return emu.getY();
    if (strcmp(id, "SP") == 0) return emu.getSP();
    if (strcmp(id, "PC") == 0) return emu.getPC();
    if (strcmp(id, "P") == 0)  return emu.getP();

    // Individual flags
    if (strcmp(id, "C") == 0) return (emu.getP() & 0x01) ? 1 : 0;
    if (strcmp(id, "Z") == 0) return (emu.getP() & 0x02) ? 1 : 0;
    if (strcmp(id, "I") == 0) return (emu.getP() & 0x04) ? 1 : 0;
    if (strcmp(id, "D") == 0) return (emu.getP() & 0x08) ? 1 : 0;
    if (strcmp(id, "B") == 0) return (emu.getP() & 0x10) ? 1 : 0;
    if (strcmp(id, "V") == 0) return (emu.getP() & 0x40) ? 1 : 0;
    if (strcmp(id, "N") == 0) return (emu.getP() & 0x80) ? 1 : 0;

    snprintf(errorBuf_, sizeof(errorBuf_), "Unknown identifier: %s", id);
    return 0;
  }

  // Fallback
  s.pos++;
  return 0;
}

} // namespace a2e
