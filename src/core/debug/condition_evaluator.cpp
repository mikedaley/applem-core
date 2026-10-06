/*
 * condition_evaluator.cpp - Expression parser for debugger breakpoint conditions and watches
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "condition_evaluator.hpp"
#include "../basic/applesoft_vars.hpp"
#include "../emulator.hpp"
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace a2e {

// Read a 5-byte Applesoft float out of memory and truncate toward zero.
// The decoding itself lives in ApplesoftVars so the debugger's variable
// inspector and this evaluator cannot drift apart.
static int32_t decodeApplesoftFloat(const MachineView& emu, uint16_t addr) {
  uint8_t bytes[APPLESOFT_FLOAT_SIZE];
  for (int i = 0; i < APPLESOFT_FLOAT_SIZE; i++) {
    bytes[i] = emu.peek(static_cast<uint16_t>(addr + i));
  }
  return static_cast<int32_t>(ApplesoftVars::decodeFloat(bytes));
}

// Read a BASIC simple variable value by its encoded name bytes
// Walks VARTAB to ARYTAB looking for matching name
static int32_t readBasicVariable(const MachineView& emu, uint8_t nameB1, uint8_t nameB2) {
  uint16_t vartab = emu.peek(0x69) | (emu.peek(0x6A) << 8);
  uint16_t arytab = emu.peek(0x6B) | (emu.peek(0x6C) << 8);

  if (vartab == 0 || arytab == 0 || vartab >= arytab) return 0;

  uint16_t addr = vartab;
  while (addr < arytab) {
    uint8_t b1 = emu.peek(addr);
    uint8_t b2 = emu.peek(addr + 1);

    if (b1 == nameB1 && b2 == nameB2) {
      // Found it - determine type from high bits
      bool isInteger = (b1 & 0x80) && (b2 & 0x80);
      if (isInteger) {
        uint8_t high = emu.peek(addr + 2);
        uint8_t low = emu.peek(addr + 3);
        int16_t val = static_cast<int16_t>((high << 8) | low);
        return static_cast<int32_t>(val);
      }
      // Real number (5-byte float)
      return decodeApplesoftFloat(emu, addr + 2);
    }
    addr += 7; // Each simple var is 7 bytes
  }
  return 0; // Variable not found
}

// Read a BASIC array element by encoded name bytes and flat index
static int32_t readBasicArrayElement(const MachineView& emu, uint8_t nameB1, uint8_t nameB2, int32_t flatIndex) {
  uint16_t arytab = emu.peek(0x6B) | (emu.peek(0x6C) << 8);
  uint16_t strend = emu.peek(0x6D) | (emu.peek(0x6E) << 8);

  if (arytab == 0 || strend == 0 || arytab >= strend) return 0;

  uint16_t addr = arytab;
  while (addr < strend) {
    uint8_t b1 = emu.peek(addr);
    uint8_t b2 = emu.peek(addr + 1);
    uint16_t totalSize = emu.peek(addr + 2) | (emu.peek(addr + 3) << 8);

    if (b1 == nameB1 && b2 == nameB2) {
      uint8_t numDims = emu.peek(addr + 4);
      uint16_t dataStart = addr + 5 + numDims * 2;

      bool isInteger = (b1 & 0x80) && (b2 & 0x80);
      int elementSize = isInteger ? 2 : 5;

      uint16_t elemAddr = dataStart + flatIndex * elementSize;
      if (isInteger) {
        uint8_t high = emu.peek(elemAddr);
        uint8_t low = emu.peek(elemAddr + 1);
        int16_t val = static_cast<int16_t>((high << 8) | low);
        return static_cast<int32_t>(val);
      }
      return decodeApplesoftFloat(emu, elemAddr);
    }
    addr += totalSize;
  }
  return 0;
}

// Read a BASIC 2D array element by encoded name bytes and two indices
// Applesoft stores dimensions in reverse order: DIM A(M,N) stores [N+1, M+1]
// Element A(i,j) flat index = i * dim2size + j  (dim2size is the FIRST stored dimension)
static int32_t readBasicArrayElement2D(const MachineView& emu, uint8_t nameB1, uint8_t nameB2, int32_t idx1, int32_t idx2) {
  uint16_t arytab = emu.peek(0x6B) | (emu.peek(0x6C) << 8);
  uint16_t strend = emu.peek(0x6D) | (emu.peek(0x6E) << 8);

  if (arytab == 0 || strend == 0 || arytab >= strend) return 0;

  uint16_t addr = arytab;
  while (addr < strend) {
    uint8_t b1 = emu.peek(addr);
    uint8_t b2 = emu.peek(addr + 1);
    uint16_t totalSize = emu.peek(addr + 2) | (emu.peek(addr + 3) << 8);

    if (b1 == nameB1 && b2 == nameB2) {
      uint8_t numDims = emu.peek(addr + 4);
      if (numDims < 2) return 0;

      // Applesoft stores arrays in column-major order with reversed dimensions.
      // For DIM A(M,N): dims[0]=N+1, dims[1]=M+1
      // Flat index for A(i,j) = j * dims[1] + i
      uint16_t dim1Size = (emu.peek(addr + 7) << 8) | emu.peek(addr + 8);

      int32_t flatIndex = idx2 * dim1Size + idx1;
      uint16_t dataStart = addr + 5 + numDims * 2;

      bool isInteger = (b1 & 0x80) && (b2 & 0x80);
      int elementSize = isInteger ? 2 : 5;

      uint16_t elemAddr = dataStart + flatIndex * elementSize;
      if (isInteger) {
        uint8_t high = emu.peek(elemAddr);
        uint8_t low = emu.peek(elemAddr + 1);
        int16_t val = static_cast<int16_t>((high << 8) | low);
        return static_cast<int32_t>(val);
      }
      return decodeApplesoftFloat(emu, elemAddr);
    }
    addr += totalSize;
  }
  return 0;
}

char ConditionEvaluator::errorBuf_[128] = "";

// The first error stands: what follows one is usually only its consequence.
void ConditionEvaluator::fail(const char* format, ...) {
  if (errorBuf_[0]) return;
  va_list args;
  va_start(args, format);
  vsnprintf(errorBuf_, sizeof(errorBuf_), format, args);
  va_end(args);
}

namespace {
int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
} // namespace

int ConditionEvaluator::tokenize(const char* expr, Token* tokens, int maxTokens, bool hexNumbers) {
  int count = 0;
  int i = 0;
  int len = static_cast<int>(strlen(expr));

  auto readHex = [&](int32_t& val) {
    const int from = i;
    val = 0;
    while (i < len && hexDigit(expr[i]) >= 0) {
      val = static_cast<int32_t>((static_cast<uint32_t>(val) << 4) | static_cast<uint32_t>(hexDigit(expr[i])));
      i++;
    }
    return i > from;
  };
  auto readDecimal = [&](int32_t& val) {
    const int from = i;
    val = 0;
    while (i < len && isdigit(static_cast<unsigned char>(expr[i]))) {
      val = val * 10 + (expr[i] - '0');
      i++;
    }
    return i > from;
  };
  auto number = [&](int32_t val) {
    tokens[count].type = TOK_NUM;
    tokens[count].numVal = val;
    count++;
  };

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

    // Single-char operators (including comma for function args)
    char ch = expr[i];
    if (ch == '<' || ch == '>' || ch == '(' || ch == ')' ||
        ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == ',') {
      tokens[count].type = TOK_OP1;
      tokens[count].strVal[0] = ch;
      tokens[count].strVal[1] = '\0';
      count++;
      i++;
      continue;
    }

    // The halves of an operator, alone, are a mistake worth naming: A = $41
    // read as A $41 compared nothing at all.
    if (ch == '=') {
      fail("Use == to compare: A == $41");
      i++;
      continue;
    }
    if (ch == '&' || ch == '|' || ch == '!') {
      fail(ch == '!' ? "Use != for not equal" : "Use %c%c to join conditions", ch, ch);
      i++;
      continue;
    }

    // Hex: $FF, #$FF, 0xFF. Decimal with # where numbers are hex: #42.
    if (ch == '#' && i + 1 < len && expr[i + 1] == '$') {
      i += 2;
      int32_t val;
      if (!readHex(val)) fail("A number follows #$");
      number(val);
      continue;
    }
    if (ch == '#') {
      i++;
      int32_t val;
      if (!readDecimal(val)) fail("A number follows #");
      number(val);
      continue;
    }
    if (ch == '$') {
      i++;
      int32_t val;
      if (!readHex(val)) fail("A number follows $");
      number(val);
      continue;
    }
    if (ch == '0' && i + 1 < len && (expr[i + 1] == 'x' || expr[i + 1] == 'X')) {
      i += 2;
      int32_t val;
      if (!readHex(val)) fail("A number follows 0x");
      number(val);
      continue;
    }

    // A number with no prefix: decimal, or hex as the monitor reads it.
    if (isdigit(static_cast<unsigned char>(ch))) {
      const int start = i;
      int32_t val;
      if (hexNumbers) readHex(val);
      else readDecimal(val);
      number(val);
      // "12AB" in decimal, or "12G" in hex, is not a number and not a name.
      if (i < len && (isalnum(static_cast<unsigned char>(expr[i])) || expr[i] == '_')) {
        while (i < len && (isalnum(static_cast<unsigned char>(expr[i])) || expr[i] == '_')) i++;
        fail(hexNumbers ? "Not a number: %.*s" : "Not a number: %.*s (hex wants a $)", i - start, expr + start);
      }
      continue;
    }

    // Identifiers
    if (isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
      int start = i;
      while (i < len && (isalnum(static_cast<unsigned char>(expr[i])) || expr[i] == '_')) {
        i++;
      }
      int idLen = i - start;
      if (idLen > 7) {
        fail("Unknown name: %.*s", i - start, expr + start);
        idLen = 7; // Truncate to fit strVal
      }
      tokens[count].type = TOK_ID;
      for (int j = 0; j < idLen; j++) {
        tokens[count].strVal[j] = toupper(static_cast<unsigned char>(expr[start + j]));
      }
      tokens[count].strVal[idLen] = '\0';
      count++;
      continue;
    }

    fail("Unexpected '%c'", ch);
    i++;
  }
  if (count >= maxTokens - 1 && i < len) fail("Too long");

  // End sentinel
  tokens[count].type = TOK_END;
  return count;
}

MachineView ConditionEvaluator::viewOf(const Emulator& emu) {
  MachineView view;
  // A //e's addresses are sixteen bits, so the bank a condition may have
  // written is ignored rather than reaching memory that does not exist.
  view.peek = [&emu](uint32_t address) {
    return emu.peekMemory(static_cast<uint16_t>(address & 0xFFFF));
  };
  view.pc = emu.getPC();
  view.a = emu.getA();
  view.x = emu.getX();
  view.y = emu.getY();
  view.sp = emu.getSP();
  view.p = emu.getP();
  return view;
}

// The whole of an expression, and nothing left over: "1 2" is not 1.
int32_t ConditionEvaluator::parseWhole(ParseState& s) {
  const int32_t value = parseOr(s);
  if (s.pos < s.count) {
    const Token& t = s.tokens[s.pos];
    if (t.type == TOK_NUM) fail("Unexpected %d", t.numVal);
    else if (t.type == TOK_OP1 && t.strVal[0] == ')') fail("A ) with no (");
    else fail("Unexpected %s", t.strVal);
  }
  return value;
}

bool ConditionEvaluator::evaluate(const char* expr, const MachineView& view) {
  errorBuf_[0] = '\0';
  ParseState s;
  s.count = tokenize(expr, s.tokens, MAX_TOKENS, false);
  s.pos = 0;
  s.view = &view;
  if (s.count == 0 && !errorBuf_[0]) fail("Nothing to evaluate");
  const int32_t result = parseWhole(s);
  return !errorBuf_[0] && result != 0;
}

int32_t ConditionEvaluator::evaluateNumeric(const char* expr,
                                            const MachineView& view,
                                            bool hexNumbers) {
  errorBuf_[0] = '\0';
  ParseState s;
  s.count = tokenize(expr, s.tokens, MAX_TOKENS, hexNumbers);
  s.pos = 0;
  s.view = &view;
  s.hexNumbers = hexNumbers;
  if (s.count == 0 && !errorBuf_[0]) fail("Nothing to evaluate");
  return parseWhole(s);
}

const char* ConditionEvaluator::check(const char* expr, bool hexNumbers) {
  errorBuf_[0] = '\0';
  MachineView none;
  none.peek = [](uint32_t) -> uint8_t { return 0; };
  ParseState s;
  s.count = tokenize(expr, s.tokens, MAX_TOKENS, hexNumbers);
  s.pos = 0;
  s.view = &none;
  s.hexNumbers = hexNumbers;
  s.checking = true;
  if (s.count == 0 && !errorBuf_[0]) fail("Nothing to evaluate");
  parseWhole(s);
  return errorBuf_;
}

bool ConditionEvaluator::evaluate(const char* expr, const Emulator& emu) {
  const MachineView view = viewOf(emu);
  return evaluate(expr, view);
}

int32_t ConditionEvaluator::evaluateNumeric(const char* expr,
                                            const Emulator& emu) {
  const MachineView view = viewOf(emu);
  return evaluateNumeric(expr, view);
}

const char* ConditionEvaluator::getLastError() {
  return errorBuf_;
}

int32_t ConditionEvaluator::parseOr(ParseState& s) {
  int32_t left = parseAnd(s);
  while (s.pos < s.count && s.tokens[s.pos].type == TOK_OP2 &&
         s.tokens[s.pos].strVal[0] == '|') {
    s.pos++;
    const int32_t right = parseAnd(s);
    left = (left != 0 || right != 0) ? 1 : 0;
  }
  return left;
}

int32_t ConditionEvaluator::parseAnd(ParseState& s) {
  int32_t left = parseComparison(s);
  while (s.pos < s.count && s.tokens[s.pos].type == TOK_OP2 &&
         s.tokens[s.pos].strVal[0] == '&') {
    s.pos++;
    const int32_t right = parseComparison(s);
    left = (left != 0 && right != 0) ? 1 : 0;
  }
  return left;
}

int32_t ConditionEvaluator::parseComparison(ParseState& s) {
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

  return left; // the value itself; a condition takes it as true when not zero
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
      else if (op == '/') {
        s.pos++;
        const int32_t by = parseAtom(s);
        if (by == 0) {
          // Checked without a machine, a register may simply read zero.
          if (!s.checking) fail("Division by zero");
          val = 0;
        } else {
          val /= by;
        }
      }
      else break;
    } else break;
  }

  return val;
}

int32_t ConditionEvaluator::parseAtom(ParseState& s) {
  if (s.pos >= s.count) {
    fail("Something is missing at the end");
    return 0;
  }

  const Token& t = s.tokens[s.pos];
  auto isOp = [&s](char op) {
    return s.pos < s.count && s.tokens[s.pos].type == TOK_OP1 && s.tokens[s.pos].strVal[0] == op;
  };
  // A function's arguments, in brackets and separated by commas.
  auto arguments = [&](const char* name, int32_t* out, int n) {
    s.pos++; // the (
    for (int i = 0; i < n; i++) {
      out[i] = parseExpr(s);
      if (i + 1 < n) {
        if (!isOp(',')) {
          fail("%s takes %d values", name, n);
          return;
        }
        s.pos++;
      }
    }
    if (!isOp(')')) {
      fail("A ( with no ): %s(...)", name);
      return;
    }
    s.pos++;
  };

  // Number literal
  if (t.type == TOK_NUM) {
    s.pos++;
    return t.numVal;
  }

  // A leading minus
  if (t.type == TOK_OP1 && t.strVal[0] == '-') {
    s.pos++;
    return -parseAtom(s);
  }

  // Parenthesized expression: its value, a sum or a truth
  if (t.type == TOK_OP1 && t.strVal[0] == '(') {
    s.pos++;
    int32_t val = parseOr(s);
    if (isOp(')')) s.pos++;
    else fail("A ( with no )");
    return val;
  }

  // Identifier
  if (t.type == TOK_ID) {
    s.pos++;
    const char* id = t.strVal;
    const MachineView& emu = *s.view;
    const bool call = isOp('(');

    // BV(b1,b2) - read BASIC simple variable value by encoded name bytes
    if (strcmp(id, "BV") == 0 && call) {
      int32_t a[2] = {0, 0};
      arguments("BV", a, 2);
      return readBasicVariable(emu, static_cast<uint8_t>(a[0]), static_cast<uint8_t>(a[1]));
    }

    // BA(b1,b2,idx) - read BASIC array element by encoded name bytes and flat index
    if (strcmp(id, "BA") == 0 && call) {
      int32_t a[3] = {0, 0, 0};
      arguments("BA", a, 3);
      return readBasicArrayElement(emu, static_cast<uint8_t>(a[0]), static_cast<uint8_t>(a[1]), a[2]);
    }

    // BA2(b1,b2,i1,i2) - read BASIC 2D array element
    if (strcmp(id, "BA2") == 0 && call) {
      int32_t a[4] = {0, 0, 0, 0};
      arguments("BA2", a, 4);
      return readBasicArrayElement2D(emu, static_cast<uint8_t>(a[0]), static_cast<uint8_t>(a[1]), a[2], a[3]);
    }

    // PEEK(addr) - read byte. The address is as wide as a machine's: a
    // IIgs's view reads any bank, and a //e's ignores the bank itself.
    if (strcmp(id, "PEEK") == 0 && call) {
      int32_t addr = 0;
      arguments("PEEK", &addr, 1);
      return emu.peek(static_cast<uint32_t>(addr) & 0xFFFFFF);
    }

    // DEEK(addr) - read 16-bit word (little-endian)
    if (strcmp(id, "DEEK") == 0 && call) {
      int32_t addr = 0;
      arguments("DEEK", &addr, 1);
      uint8_t lo = emu.peek(static_cast<uint32_t>(addr) & 0xFFFFFF);
      uint8_t hi = emu.peek((static_cast<uint32_t>(addr) + 1) & 0xFFFFFF);
      return (hi << 8) | lo;
    }

    // Registers
    if (strcmp(id, "A") == 0)  return emu.a;
    if (strcmp(id, "X") == 0)  return emu.x;
    if (strcmp(id, "Y") == 0)  return emu.y;
    if (strcmp(id, "SP") == 0) return emu.sp;
    if (strcmp(id, "PC") == 0) return emu.pc;
    if (strcmp(id, "P") == 0)  return emu.p;

    // Individual flags
    if (strcmp(id, "C") == 0) return (emu.p & 0x01) ? 1 : 0;
    if (strcmp(id, "Z") == 0) return (emu.p & 0x02) ? 1 : 0;
    if (strcmp(id, "I") == 0) return (emu.p & 0x04) ? 1 : 0;
    if (strcmp(id, "D") == 0) return (emu.p & 0x08) ? 1 : 0;
    if (strcmp(id, "B") == 0) return (emu.p & 0x10) ? 1 : 0;
    if (strcmp(id, "V") == 0) return (emu.p & 0x40) ? 1 : 0;
    if (strcmp(id, "N") == 0) return (emu.p & 0x80) ? 1 : 0;

    // Where numbers are hex, a word of hex digits that names nothing is one:
    // FF, BEEF. A register of the same spelling has already won.
    if (s.hexNumbers) {
      bool hex = id[0] != '\0';
      uint32_t value = 0;
      for (const char* c = id; *c; c++) {
        if (hexDigit(*c) < 0) hex = false;
        else value = (value << 4) | static_cast<uint32_t>(hexDigit(*c));
      }
      if (hex) return static_cast<int32_t>(value);
    }

    if (!strcmp(id, "PEEK") || !strcmp(id, "DEEK") || !strcmp(id, "BV") || !strcmp(id, "BA") ||
        !strcmp(id, "BA2")) {
      fail("%s takes its value in brackets: %s($24)", id, id);
      return 0;
    }
    fail("Unknown name: %s", id);
    return 0;
  }

  // An operator where a value should be
  fail("Unexpected %s", t.strVal);
  s.pos++;
  return 0;
}

} // namespace a2e
