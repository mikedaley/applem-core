/*
 * assembler.cpp - 65C02 multi-pass assembler
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "assembler.hpp"
#include "../disassembler/disassembler.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace a2e {

// Directive names (uppercase)
static const char* DIRECTIVES[] = {
  "ORG", "EQU", "DS", "DFB", "DB", "DW", "DA", "DDB",
  "HEX", "ASC", "DCI", nullptr
};

// Merlin-specific directives we recognise but don't support
static const char* UNSUPPORTED_DIRECTIVES[] = {
  "PUT", "USE", "MAC", "EOM", "<<<", "DO", "ELSE", "FIN",
  "LUP", "ELUP", "--^", "OBJ", "LST", "REL", "TYP", "SAV",
  "DSK", "CHN", "ENT", "EXT", "DUM", "DEND", "ERR", "CYC",
  "DAT", "EXP", "PAU", "SW", "USR", "XC", "MX", "TR",
  "KBD", "PMC", "PAG", "TTL", "SKP", "CHK", "IF", "END",
  "ADR", "ADRL", "LNK", "STR", "STRL", "REV", nullptr
};

static bool isDirective(const std::string& s) {
  for (int i = 0; DIRECTIVES[i]; i++) {
    if (s == DIRECTIVES[i]) return true;
  }
  return false;
}

static bool isUnsupportedDirective(const std::string& s) {
  for (int i = 0; UNSUPPORTED_DIRECTIVES[i]; i++) {
    if (s == UNSUPPORTED_DIRECTIVES[i]) return true;
  }
  return false;
}

static std::string toUpper(const std::string& s) {
  std::string r = s;
  for (auto& c : r) c = toupper(c);
  return r;
}

static void skipSpaces(const char*& p) {
  while (*p && (*p == ' ' || *p == '\t')) p++;
}

static bool isIdentChar(char c) {
  return isalnum(c) || c == '_' || c == '.' || c == ':' || c == ']';
}

// ============================================================================
// Constructor
// ============================================================================

Assembler::Assembler() : reverseTableBuilt(false), pc(0x0800) {
  memset(reverseOpcodes, 0xFF, sizeof(reverseOpcodes));
}

// ============================================================================
// Build reverse opcode table from disassembler's forward table
// ============================================================================

void Assembler::buildReverseOpcodeTable() {
  if (reverseTableBuilt) return;

  const OpcodeInfo* table = getOpcodeTable();
  for (int i = 0; i < 256; i++) {
    uint8_t mnem = table[i].mnemonicIndex;
    uint8_t mode = table[i].mode;
    if (mnem == 0) continue; // skip unknown opcodes
    if (mnem < 99 && mode < 16) {
      reverseOpcodes[mnem][mode] = static_cast<uint8_t>(i);
    }
  }
  reverseTableBuilt = true;
}

// ============================================================================
// Find mnemonic index by name
// ============================================================================

int Assembler::findMnemonicIndex(const std::string& mnemonic) {
  int count = getMnemonicCount();
  for (int i = 1; i < count; i++) {
    if (mnemonic == getMnemonicByIndex(i)) {
      return i;
    }
  }
  return -1;
}

// ============================================================================
// Branch detection helpers
// ============================================================================

bool Assembler::isBranchMnemonic(int mnemonicIndex) {
  // Check if the opcode table has this mnemonic in REL mode
  const OpcodeInfo* table = getOpcodeTable();
  for (int i = 0; i < 256; i++) {
    if (table[i].mnemonicIndex == mnemonicIndex &&
        table[i].mode == static_cast<uint8_t>(AddrMode::REL)) {
      return true;
    }
  }
  return false;
}

bool Assembler::isZPRMnemonic(int mnemonicIndex) {
  const OpcodeInfo* table = getOpcodeTable();
  for (int i = 0; i < 256; i++) {
    if (table[i].mnemonicIndex == mnemonicIndex &&
        table[i].mode == static_cast<uint8_t>(AddrMode::ZPR)) {
      return true;
    }
  }
  return false;
}

// ============================================================================
// Parse source into lines
// ============================================================================

std::vector<Assembler::ParsedLine> Assembler::parseSource(const char* source) {
  std::vector<ParsedLine> lines;
  const char* p = source;
  int lineNum = 1;

  while (*p) {
    // Find end of line
    const char* lineStart = p;
    while (*p && *p != '\n' && *p != '\r') p++;

    std::string lineStr(lineStart, p - lineStart);

    // Skip line endings
    if (*p == '\r') p++;
    if (*p == '\n') p++;

    ParsedLine parsed = parseLine(lineStr.c_str(), lineNum);
    if (!parsed.mnemonic.empty() || !parsed.label.empty()) {
      lines.push_back(parsed);
    }

    lineNum++;
  }

  return lines;
}

// ============================================================================
// Parse a single line
// ============================================================================

Assembler::ParsedLine Assembler::parseLine(const char* line, int lineNumber) {
  ParsedLine result;
  result.lineNumber = lineNumber;

  const char* p = line;

  // Skip empty lines
  skipSpaces(p);
  if (!*p) return result;

  // Full-line comments
  if (*p == ';' || *p == '*') return result;

  // Reset p to start of line for column detection
  p = line;

  // If line starts with non-whitespace, first token is a label
  if (*p && *p != ' ' && *p != '\t' && *p != ';' && *p != '*') {
    const char* start = p;
    while (*p && !isspace(*p)) p++;
    result.label = std::string(start, p - start);
    // Strip optional trailing colon (non-Merlin convention but common)
    if (!result.label.empty() && result.label.back() == ':') {
      result.label.pop_back();
    }
  }

  // Skip whitespace to opcode
  skipSpaces(p);
  if (!*p || *p == ';') return result;

  // Extract opcode/mnemonic
  const char* opcStart = p;
  while (*p && !isspace(*p)) p++;
  result.mnemonic = toUpper(std::string(opcStart, p - opcStart));

  // Skip whitespace to operand
  skipSpaces(p);
  if (!*p || *p == ';') return result;

  // Extract operand (up to comment or end of line)
  // Respect string delimiters
  const char* opStart = p;
  bool inSingle = false, inDouble = false;
  while (*p) {
    if (*p == '\'' && !inDouble) inSingle = !inSingle;
    else if (*p == '"' && !inSingle) inDouble = !inDouble;
    else if (*p == ';' && !inSingle && !inDouble) break;
    p++;
  }

  // Trim trailing whitespace from operand
  const char* opEnd = p;
  while (opEnd > opStart && isspace(*(opEnd - 1))) opEnd--;
  result.operand = std::string(opStart, opEnd - opStart);

  return result;
}

// ============================================================================
// Expression evaluator (recursive descent)
// ============================================================================

int32_t Assembler::evaluateExpression(const std::string& expr, bool& error,
                                      std::string& errorMsg, int lineNumber) {
  const char* p = expr.c_str();
  skipSpaces(p);
  int32_t val = evalAddSub(p, error, errorMsg, lineNumber);
  return val;
}

int32_t Assembler::evalAddSub(const char*& p, bool& error,
                              std::string& errorMsg, int lineNumber) {
  int32_t left = evalMulDiv(p, error, errorMsg, lineNumber);
  if (error) return 0;

  while (*p == '+' || *p == '-') {
    char op = *p++;
    skipSpaces(p);
    int32_t right = evalMulDiv(p, error, errorMsg, lineNumber);
    if (error) return 0;
    if (op == '+') left += right;
    else left -= right;
  }
  return left;
}

int32_t Assembler::evalMulDiv(const char*& p, bool& error,
                              std::string& errorMsg, int lineNumber) {
  int32_t left = evalUnary(p, error, errorMsg, lineNumber);
  if (error) return 0;

  while (*p == '*' || *p == '/') {
    // Peek: if * is followed by nothing meaningful (end/space/operator),
    // it might be a standalone PC reference - stop here
    char op = *p;
    if (op == '*') {
      // Check if this is multiplication or PC reference
      // Multiplication only if preceded by a value and followed by a value
      const char* next = p + 1;
      skipSpaces(next);
      if (!*next || *next == '+' || *next == '-' || *next == ')' ||
          *next == ',' || *next == ';') {
        break; // Not multiplication
      }
    }
    p++;
    skipSpaces(p);
    int32_t right = evalUnary(p, error, errorMsg, lineNumber);
    if (error) return 0;
    if (op == '/') {
      if (right == 0) {
        error = true;
        errorMsg = "Division by zero";
        return 0;
      }
      left /= right;
    } else {
      left *= right;
    }
  }
  return left;
}

int32_t Assembler::evalUnary(const char*& p, bool& error,
                             std::string& errorMsg, int lineNumber) {
  skipSpaces(p);

  // Low byte selector: <expr
  if (*p == '<') {
    p++;
    skipSpaces(p);
    int32_t val = evalUnary(p, error, errorMsg, lineNumber);
    return val & 0xFF;
  }

  // High byte selector: >expr
  if (*p == '>') {
    p++;
    skipSpaces(p);
    int32_t val = evalUnary(p, error, errorMsg, lineNumber);
    return (val >> 8) & 0xFF;
  }

  // Unary minus
  if (*p == '-') {
    p++;
    skipSpaces(p);
    int32_t val = evalUnary(p, error, errorMsg, lineNumber);
    return -val;
  }

  return evalPrimary(p, error, errorMsg, lineNumber);
}

int32_t Assembler::evalPrimary(const char*& p, bool& error,
                               std::string& errorMsg, int lineNumber) {
  skipSpaces(p);

  // Parenthesized expression
  if (*p == '(') {
    p++;
    skipSpaces(p);
    int32_t val = evalAddSub(p, error, errorMsg, lineNumber);
    if (error) return 0;
    skipSpaces(p);
    if (*p == ')') p++;
    return val;
  }

  // Current PC reference: *
  if (*p == '*') {
    p++;
    return static_cast<int32_t>(pc);
  }

  // Hex number: $xxxx
  if (*p == '$') {
    p++;
    if (!isxdigit(*p)) {
      error = true;
      errorMsg = "Expected hex digit after $";
      return 0;
    }
    int32_t val = 0;
    while (isxdigit(*p)) {
      val = val * 16;
      if (*p >= '0' && *p <= '9') val += *p - '0';
      else if (*p >= 'A' && *p <= 'F') val += *p - 'A' + 10;
      else if (*p >= 'a' && *p <= 'f') val += *p - 'a' + 10;
      p++;
    }
    return val;
  }

  // Binary number: %01010101
  if (*p == '%') {
    p++;
    if (*p != '0' && *p != '1') {
      error = true;
      errorMsg = "Expected binary digit after %";
      return 0;
    }
    int32_t val = 0;
    while (*p == '0' || *p == '1') {
      val = (val << 1) | (*p - '0');
      p++;
    }
    return val;
  }

  // Character literal: 'A'
  if (*p == '\'') {
    p++;
    if (!*p) {
      error = true;
      errorMsg = "Unterminated character literal";
      return 0;
    }
    int32_t val = static_cast<uint8_t>(*p);
    p++;
    if (*p == '\'') p++; // skip closing quote
    return val;
  }

  // Decimal number
  if (isdigit(*p)) {
    int32_t val = 0;
    while (isdigit(*p)) {
      val = val * 10 + (*p - '0');
      p++;
    }
    return val;
  }

  // Symbol / label reference
  if (isalpha(*p) || *p == '_' || *p == ':' || *p == ']') {
    const char* start = p;
    while (*p && isIdentChar(*p)) p++;
    std::string name(start, p - start);
    std::string upper = toUpper(name);

    auto it = symbols.find(upper);
    if (it != symbols.end()) {
      return it->second;
    }

    // Undefined symbol
    error = true;
    errorMsg = "Undefined symbol: " + name;
    return 0;
  }

  error = true;
  errorMsg = "Unexpected character in expression";
  return 0;
}

// ============================================================================
// Addressing mode detection
// ============================================================================

uint8_t Assembler::detectAddressingMode(const std::string& mnemonic,
                                        const std::string& operand,
                                        int32_t value, bool valueKnown) {
  int mnemIdx = findMnemonicIndex(mnemonic);
  if (mnemIdx < 0) return 0xFF;

  // No operand
  if (operand.empty()) {
    // Check if IMP exists for this mnemonic
    if (reverseOpcodes[mnemIdx][static_cast<int>(AddrMode::IMP)] != 0xFF) {
      return static_cast<uint8_t>(AddrMode::IMP);
    }
    // Some shift/rotate instructions use ACC with no explicit operand
    if (reverseOpcodes[mnemIdx][static_cast<int>(AddrMode::ACC)] != 0xFF) {
      return static_cast<uint8_t>(AddrMode::ACC);
    }
    return 0xFF;
  }

  std::string op = operand;

  // Accumulator: "A" for shift/rotate instructions
  if (op == "A" || op == "a") {
    if (reverseOpcodes[mnemIdx][static_cast<int>(AddrMode::ACC)] != 0xFF) {
      return static_cast<uint8_t>(AddrMode::ACC);
    }
  }

  // Immediate: #expr
  if (op[0] == '#') {
    return static_cast<uint8_t>(AddrMode::IMM);
  }

  // Indirect modes: start with (
  if (op[0] == '(') {
    // (expr,X) - Indexed Indirect or Absolute Indexed Indirect
    if (op.size() >= 4) {
      std::string upper = toUpper(op);
      // Check for (expr,X)
      if (upper.back() == ')') {
        size_t commaPos = upper.rfind(',');
        if (commaPos != std::string::npos) {
          std::string afterComma = upper.substr(commaPos + 1);
          // Remove trailing )
          afterComma.pop_back();
          // Trim spaces
          while (!afterComma.empty() && afterComma[0] == ' ') afterComma.erase(0, 1);
          while (!afterComma.empty() && afterComma.back() == ' ') afterComma.pop_back();
          if (afterComma == "X") {
            // IZX (zero page) or AIX (absolute) based on value
            if (valueKnown && value >= 0 && value <= 255 &&
                reverseOpcodes[mnemIdx][static_cast<int>(AddrMode::IZX)] != 0xFF) {
              return static_cast<uint8_t>(AddrMode::IZX);
            }
            if (reverseOpcodes[mnemIdx][static_cast<int>(AddrMode::AIX)] != 0xFF) {
              return static_cast<uint8_t>(AddrMode::AIX);
            }
            return static_cast<uint8_t>(AddrMode::IZX);
          }
        }
      }
      // (expr),Y - Indirect Indexed
      size_t closeParen = upper.find(')');
      if (closeParen != std::string::npos && closeParen < upper.size() - 1) {
        std::string afterParen = upper.substr(closeParen + 1);
        // Trim spaces
        while (!afterParen.empty() && afterParen[0] == ' ') afterParen.erase(0, 1);
        if (afterParen.size() >= 2 && afterParen[0] == ',' &&
            (afterParen[1] == 'Y' || afterParen[1] == 'y' ||
             (afterParen.size() >= 3 && afterParen[2] == 'Y'))) {
          return static_cast<uint8_t>(AddrMode::IZY);
        }
      }
    }
    // (expr) - Indirect or Zero Page Indirect
    if (op.back() == ')') {
      if (valueKnown && value >= 0 && value <= 255 &&
          reverseOpcodes[mnemIdx][static_cast<int>(AddrMode::ZPI)] != 0xFF) {
        return static_cast<uint8_t>(AddrMode::ZPI);
      }
      return static_cast<uint8_t>(AddrMode::IND);
    }
  }

  // ZPR mode: zp,target (for BBR/BBS)
  if (isZPRMnemonic(mnemIdx)) {
    return static_cast<uint8_t>(AddrMode::ZPR);
  }

  // Branch instructions: REL mode
  if (isBranchMnemonic(mnemIdx)) {
    return static_cast<uint8_t>(AddrMode::REL);
  }

  // Check for ,X or ,Y suffix
  {
    std::string upper = toUpper(op);
    size_t commaPos = upper.rfind(',');
    if (commaPos != std::string::npos) {
      std::string suffix = upper.substr(commaPos + 1);
      while (!suffix.empty() && suffix[0] == ' ') suffix.erase(0, 1);

      if (suffix == "X") {
        if (valueKnown && value >= 0 && value <= 255 &&
            reverseOpcodes[mnemIdx][static_cast<int>(AddrMode::ZPX)] != 0xFF) {
          return static_cast<uint8_t>(AddrMode::ZPX);
        }
        return static_cast<uint8_t>(AddrMode::ABX);
      }
      if (suffix == "Y") {
        if (valueKnown && value >= 0 && value <= 255 &&
            reverseOpcodes[mnemIdx][static_cast<int>(AddrMode::ZPY)] != 0xFF) {
          return static_cast<uint8_t>(AddrMode::ZPY);
        }
        return static_cast<uint8_t>(AddrMode::ABY);
      }
    }
  }

  // Plain operand: ZP or ABS based on value
  if (valueKnown && value >= 0 && value <= 255 &&
      reverseOpcodes[mnemIdx][static_cast<int>(AddrMode::ZP)] != 0xFF) {
    return static_cast<uint8_t>(AddrMode::ZP);
  }
  return static_cast<uint8_t>(AddrMode::ABS);
}

// ============================================================================
// Instruction sizing (for pass 1)
// ============================================================================

int Assembler::getInstructionSize(const std::string& mnemonic,
                                  const std::string& operand,
                                  bool labelsComplete) {
  if (operand.empty()) {
    // IMP or ACC = 1 byte
    return 1;
  }

  // Immediate
  if (operand[0] == '#') return 2;

  int mnemIdx = findMnemonicIndex(mnemonic);
  if (mnemIdx < 0) return 0;

  // Branch
  if (isBranchMnemonic(mnemIdx)) return 2;

  // ZPR (BBR/BBS)
  if (isZPRMnemonic(mnemIdx)) return 3;

  // Accumulator
  if (toUpper(operand) == "A" &&
      reverseOpcodes[mnemIdx][static_cast<int>(AddrMode::ACC)] != 0xFF) {
    return 1;
  }

  // Try to evaluate to determine ZP vs ABS
  if (labelsComplete) {
    bool error = false;
    std::string errorMsg;

    // Extract base expression (before ,X or ,Y)
    std::string exprStr = operand;
    bool hasIndex = false;
    {
      std::string upper = toUpper(operand);
      size_t commaPos = upper.rfind(',');
      if (commaPos != std::string::npos) {
        std::string suffix = upper.substr(commaPos + 1);
        while (!suffix.empty() && suffix[0] == ' ') suffix.erase(0, 1);
        if (suffix == "X" || suffix == "Y") {
          exprStr = operand.substr(0, commaPos);
          hasIndex = true;
        }
      }
    }

    // Strip parentheses for indirect modes
    std::string evalStr = exprStr;
    if (!evalStr.empty() && evalStr[0] == '(') {
      evalStr = evalStr.substr(1);
      if (!evalStr.empty() && evalStr.back() == ')') evalStr.pop_back();
    }

    int32_t val = evaluateExpression(evalStr, error, errorMsg, 0);
    if (!error) {
      uint8_t mode = detectAddressingMode(mnemonic, operand, val, true);
      if (mode != 0xFF) {
        switch (static_cast<AddrMode>(mode)) {
          case AddrMode::IMP:
          case AddrMode::ACC:
            return 1;
          case AddrMode::IMM:
          case AddrMode::ZP:
          case AddrMode::ZPX:
          case AddrMode::ZPY:
          case AddrMode::IZX:
          case AddrMode::IZY:
          case AddrMode::ZPI:
          case AddrMode::REL:
            return 2;
          case AddrMode::ABS:
          case AddrMode::ABX:
          case AddrMode::ABY:
          case AddrMode::IND:
          case AddrMode::AIX:
          case AddrMode::ZPR:
            return 3;
        }
      }
    }
  }

  // Default: assume ABS (3 bytes) for forward references
  return 3;
}

// ============================================================================
// Directive sizing
// ============================================================================

int Assembler::getDirectiveSize(const std::string& directive,
                                const std::string& operand,
                                bool& error, std::string& errorMsg,
                                int lineNumber) {
  if (directive == "ORG" || directive == "EQU") return 0;

  if (directive == "DS") {
    int32_t val = evaluateExpression(operand, error, errorMsg, lineNumber);
    if (error) return 0;
    return static_cast<int>(val);
  }

  if (directive == "DFB" || directive == "DB") {
    // Count comma-separated values
    int count = 1;
    bool inStr = false;
    for (char c : operand) {
      if (c == '"' || c == '\'') inStr = !inStr;
      if (c == ',' && !inStr) count++;
    }
    return count;
  }

  if (directive == "DW" || directive == "DA") {
    int count = 1;
    for (char c : operand) {
      if (c == ',') count++;
    }
    return count * 2;
  }

  if (directive == "DDB") {
    int count = 1;
    for (char c : operand) {
      if (c == ',') count++;
    }
    return count * 2;
  }

  if (directive == "HEX") {
    // Count hex digit pairs (ignore spaces)
    int digits = 0;
    for (char c : operand) {
      if (isxdigit(c)) digits++;
    }
    return digits / 2;
  }

  if (directive == "ASC") {
    // Count characters between delimiters
    if (operand.size() >= 2) {
      char delim = operand[0];
      size_t end = operand.find(delim, 1);
      if (end != std::string::npos) {
        return static_cast<int>(end - 1);
      }
    }
    return static_cast<int>(operand.size());
  }

  if (directive == "DCI") {
    if (operand.size() >= 2) {
      char delim = operand[0];
      size_t end = operand.find(delim, 1);
      if (end != std::string::npos) {
        return static_cast<int>(end - 1);
      }
    }
    return static_cast<int>(operand.size());
  }

  return 0;
}

// ============================================================================
// Directive emission
// ============================================================================

void Assembler::emitDirective(const std::string& directive,
                              const std::string& operand,
                              std::vector<uint8_t>& output,
                              bool& error, std::string& errorMsg,
                              int lineNumber) {
  if (directive == "ORG" || directive == "EQU") return;

  if (directive == "DS") {
    int32_t val = evaluateExpression(operand, error, errorMsg, lineNumber);
    if (error) return;
    for (int32_t i = 0; i < val; i++) {
      output.push_back(0);
    }
    return;
  }

  if (directive == "DFB" || directive == "DB") {
    // Parse comma-separated byte values
    const char* p = operand.c_str();
    while (*p) {
      skipSpaces(p);
      if (!*p) break;

      // Find end of this value (next comma or end)
      const char* start = p;
      int depth = 0;
      while (*p && (*p != ',' || depth > 0)) {
        if (*p == '(') depth++;
        if (*p == ')') depth--;
        p++;
      }
      std::string val(start, p - start);
      // Trim
      while (!val.empty() && val.back() == ' ') val.pop_back();

      int32_t v = evaluateExpression(val, error, errorMsg, lineNumber);
      if (error) return;
      output.push_back(static_cast<uint8_t>(v & 0xFF));

      if (*p == ',') p++;
    }
    return;
  }

  if (directive == "DW" || directive == "DA") {
    const char* p = operand.c_str();
    while (*p) {
      skipSpaces(p);
      if (!*p) break;

      const char* start = p;
      int depth = 0;
      while (*p && (*p != ',' || depth > 0)) {
        if (*p == '(') depth++;
        if (*p == ')') depth--;
        p++;
      }
      std::string val(start, p - start);
      while (!val.empty() && val.back() == ' ') val.pop_back();

      int32_t v = evaluateExpression(val, error, errorMsg, lineNumber);
      if (error) return;
      output.push_back(static_cast<uint8_t>(v & 0xFF));
      output.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));

      if (*p == ',') p++;
    }
    return;
  }

  if (directive == "DDB") {
    const char* p = operand.c_str();
    while (*p) {
      skipSpaces(p);
      if (!*p) break;

      const char* start = p;
      int depth = 0;
      while (*p && (*p != ',' || depth > 0)) {
        if (*p == '(') depth++;
        if (*p == ')') depth--;
        p++;
      }
      std::string val(start, p - start);
      while (!val.empty() && val.back() == ' ') val.pop_back();

      int32_t v = evaluateExpression(val, error, errorMsg, lineNumber);
      if (error) return;
      // Big-endian
      output.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
      output.push_back(static_cast<uint8_t>(v & 0xFF));

      if (*p == ',') p++;
    }
    return;
  }

  if (directive == "HEX") {
    const char* p = operand.c_str();
    while (*p) {
      if (isspace(*p) || *p == ',') { p++; continue; }
      if (!isxdigit(*p)) {
        error = true;
        errorMsg = "Invalid hex digit";
        return;
      }
      char hi = *p++;
      if (!isxdigit(*p)) {
        error = true;
        errorMsg = "Odd number of hex digits";
        return;
      }
      char lo = *p++;
      auto hexVal = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return 0;
      };
      output.push_back((hexVal(hi) << 4) | hexVal(lo));
    }
    return;
  }

  if (directive == "ASC") {
    if (operand.size() < 2) return;
    char delim = operand[0];
    bool highBit = (delim == '"'); // Merlin convention: " sets high bit
    for (size_t i = 1; i < operand.size(); i++) {
      if (operand[i] == delim) break;
      uint8_t ch = static_cast<uint8_t>(operand[i]);
      if (highBit) ch |= 0x80;
      output.push_back(ch);
    }
    return;
  }

  if (directive == "DCI") {
    if (operand.size() < 2) return;
    char delim = operand[0];
    // Collect characters
    std::vector<uint8_t> chars;
    for (size_t i = 1; i < operand.size(); i++) {
      if (operand[i] == delim) break;
      chars.push_back(static_cast<uint8_t>(operand[i]));
    }
    // Emit all but last with normal, last with high bit set
    for (size_t i = 0; i < chars.size(); i++) {
      uint8_t ch = chars[i];
      if (i == chars.size() - 1) ch |= 0x80;
      output.push_back(ch);
    }
    return;
  }
}

// ============================================================================
// Main assemble function
// ============================================================================

AsmResult Assembler::assemble(const char* source) {
  AsmResult result;
  result.origin = 0x0800;
  result.endAddress = 0x0800;
  result.success = false;

  buildReverseOpcodeTable();
  symbols.clear();

  // Parse source
  auto lines = parseSource(source);
  if (lines.empty()) {
    result.success = true;
    return result;
  }

  auto addError = [&](int lineNum, const std::string& msg) {
    AsmError err;
    err.lineNumber = lineNum;
    strncpy(err.message, msg.c_str(), ASM_MAX_ERROR_MSG - 1);
    err.message[ASM_MAX_ERROR_MSG - 1] = '\0';
    result.errors.push_back(err);
  };

  // ========================================================================
  // Pass 1: Collect labels and compute sizes
  // ========================================================================

  pc = 0x0800;
  result.origin = pc;

  for (auto& line : lines) {
    std::string mnem = line.mnemonic;

    // Handle ORG directive
    if (mnem == "ORG") {
      bool error = false;
      std::string errorMsg;
      int32_t val = evaluateExpression(line.operand, error, errorMsg,
                                       line.lineNumber);
      if (error) {
        addError(line.lineNumber, "ORG: " + errorMsg);
        continue;
      }
      pc = static_cast<uint16_t>(val);
      if (result.output.empty()) {
        result.origin = pc;
      }
      continue;
    }

    // Record label address
    if (!line.label.empty()) {
      std::string labelUpper = toUpper(line.label);

      // Handle EQU: label = value
      if (mnem == "EQU") {
        bool error = false;
        std::string errorMsg;
        int32_t val = evaluateExpression(line.operand, error, errorMsg,
                                         line.lineNumber);
        if (!error) {
          symbols[labelUpper] = val;
        }
        // If error, will be caught in pass 2
        continue;
      }

      symbols[labelUpper] = static_cast<int32_t>(pc);
    }

    if (mnem.empty()) continue;

    // Handle unsupported directives
    if (isUnsupportedDirective(mnem)) {
      addError(line.lineNumber, "Unsupported directive: " + mnem);
      continue;
    }

    // Directive sizing
    if (isDirective(mnem)) {
      bool error = false;
      std::string errorMsg;
      int size = getDirectiveSize(mnem, line.operand, error, errorMsg,
                                  line.lineNumber);
      if (error) {
        // Ignore sizing errors in pass 1 (may have forward references)
        size = 0;
      }
      pc += size;
      continue;
    }

    // Instruction sizing
    int mnemIdx = findMnemonicIndex(mnem);
    if (mnemIdx < 0) {
      addError(line.lineNumber, "Unknown mnemonic: " + mnem);
      continue;
    }

    int size = getInstructionSize(mnem, line.operand, false);
    if (size == 0) {
      addError(line.lineNumber, "Invalid instruction: " + mnem);
      continue;
    }
    pc += size;
  }

  // Track if we had pass 1 errors (still run pass 2 to find more errors)
  bool hadPass1Errors = !result.errors.empty();

  // ========================================================================
  // Pass 2: Encode instructions (run even with errors to find all issues)
  // ========================================================================

  pc = result.origin;

  for (auto& line : lines) {
    std::string mnem = line.mnemonic;

    // Handle ORG
    if (mnem == "ORG") {
      bool error = false;
      std::string errorMsg;
      int32_t val = evaluateExpression(line.operand, error, errorMsg,
                                       line.lineNumber);
      if (error) {
        addError(line.lineNumber, "ORG: " + errorMsg);
        continue;
      }
      pc = static_cast<uint16_t>(val);
      continue;
    }

    // EQU already handled in pass 1
    if (mnem == "EQU") {
      // Re-evaluate to catch errors
      bool error = false;
      std::string errorMsg;
      int32_t val = evaluateExpression(line.operand, error, errorMsg,
                                       line.lineNumber);
      if (error) {
        addError(line.lineNumber, "EQU: " + errorMsg);
      } else {
        symbols[toUpper(line.label)] = val;
      }
      continue;
    }

    if (mnem.empty()) continue;

    // Skip unsupported directives (already errored in pass 1)
    if (isUnsupportedDirective(mnem)) continue;

    // Handle directives
    if (isDirective(mnem)) {
      bool error = false;
      std::string errorMsg;
      emitDirective(mnem, line.operand, result.output, error, errorMsg,
                    line.lineNumber);
      if (error) {
        addError(line.lineNumber, mnem + ": " + errorMsg);
      }
      // Advance PC by actual emitted bytes
      int size = getDirectiveSize(mnem, line.operand, error, errorMsg,
                                  line.lineNumber);
      pc += size;
      continue;
    }

    // Instruction encoding
    int mnemIdx = findMnemonicIndex(mnem);
    if (mnemIdx < 0) continue; // Already errored in pass 1

    // Evaluate operand expression
    std::string exprStr = line.operand;
    int32_t value = 0;
    bool valueKnown = false;

    if (!exprStr.empty()) {
      // Handle ZPR mode (BBR/BBS): zp,target
      if (isZPRMnemonic(mnemIdx)) {
        // Split on comma to get zp and target
        size_t commaPos = exprStr.find(',');
        if (commaPos == std::string::npos) {
          addError(line.lineNumber, "ZPR instructions need zp,target operand");
          continue;
        }
        std::string zpStr = exprStr.substr(0, commaPos);
        std::string targetStr = exprStr.substr(commaPos + 1);
        // Trim
        while (!zpStr.empty() && zpStr.back() == ' ') zpStr.pop_back();
        while (!targetStr.empty() && targetStr[0] == ' ') targetStr.erase(0, 1);

        bool zpError = false, targetError = false;
        std::string zpErrMsg, targetErrMsg;
        int32_t zpVal = evaluateExpression(zpStr, zpError, zpErrMsg,
                                           line.lineNumber);
        int32_t targetVal = evaluateExpression(targetStr, targetError,
                                              targetErrMsg, line.lineNumber);
        if (zpError) {
          addError(line.lineNumber, zpErrMsg);
          continue;
        }
        if (targetError) {
          addError(line.lineNumber, targetErrMsg);
          continue;
        }

        uint8_t opcode = reverseOpcodes[mnemIdx][static_cast<int>(AddrMode::ZPR)];
        if (opcode == 0xFF) {
          addError(line.lineNumber, "Invalid mode for " + mnem);
          continue;
        }

        // Calculate relative offset from PC+3 (instruction is 3 bytes)
        int32_t offset = targetVal - (pc + 3);
        if (offset < -128 || offset > 127) {
          addError(line.lineNumber, "Branch target out of range");
          continue;
        }

        result.output.push_back(opcode);
        result.output.push_back(static_cast<uint8_t>(zpVal & 0xFF));
        result.output.push_back(static_cast<uint8_t>(offset & 0xFF));
        pc += 3;
        continue;
      }

      // Strip index suffix for expression evaluation
      std::string evalStr = exprStr;
      {
        std::string upper = toUpper(exprStr);
        size_t commaPos = upper.rfind(',');
        if (commaPos != std::string::npos) {
          std::string suffix = upper.substr(commaPos + 1);
          while (!suffix.empty() && suffix[0] == ' ') suffix.erase(0, 1);
          if (suffix == "X" || suffix == "Y") {
            evalStr = exprStr.substr(0, commaPos);
          }
        }
      }

      // Strip # prefix for immediate
      if (!evalStr.empty() && evalStr[0] == '#') {
        evalStr = evalStr.substr(1);
      }

      // Strip parentheses for indirect
      if (!evalStr.empty() && evalStr[0] == '(') {
        evalStr = evalStr.substr(1);
        if (!evalStr.empty() && evalStr.back() == ')') evalStr.pop_back();
      }

      // Trim
      while (!evalStr.empty() && evalStr[0] == ' ') evalStr.erase(0, 1);
      while (!evalStr.empty() && evalStr.back() == ' ') evalStr.pop_back();

      bool error = false;
      std::string errorMsg;
      value = evaluateExpression(evalStr, error, errorMsg, line.lineNumber);
      if (error) {
        addError(line.lineNumber, errorMsg);
        continue;
      }
      valueKnown = true;
    }

    // Detect addressing mode
    uint8_t mode = detectAddressingMode(mnem, line.operand, value, valueKnown);
    if (mode == 0xFF) {
      addError(line.lineNumber, "Cannot determine addressing mode for " + mnem);
      continue;
    }

    // Look up opcode
    uint8_t opcode = reverseOpcodes[mnemIdx][mode];
    if (opcode == 0xFF) {
      addError(line.lineNumber, mnem + " does not support this addressing mode");
      continue;
    }

    // Emit instruction bytes
    AddrMode addrMode = static_cast<AddrMode>(mode);
    switch (addrMode) {
      case AddrMode::IMP:
      case AddrMode::ACC:
        result.output.push_back(opcode);
        pc += 1;
        break;

      case AddrMode::IMM:
      case AddrMode::ZP:
      case AddrMode::ZPX:
      case AddrMode::ZPY:
      case AddrMode::IZX:
      case AddrMode::IZY:
      case AddrMode::ZPI:
        result.output.push_back(opcode);
        result.output.push_back(static_cast<uint8_t>(value & 0xFF));
        pc += 2;
        break;

      case AddrMode::REL: {
        int32_t offset = value - (pc + 2);
        if (offset < -128 || offset > 127) {
          addError(line.lineNumber, "Branch target out of range");
          continue;
        }
        result.output.push_back(opcode);
        result.output.push_back(static_cast<uint8_t>(offset & 0xFF));
        pc += 2;
        break;
      }

      case AddrMode::ABS:
      case AddrMode::ABX:
      case AddrMode::ABY:
      case AddrMode::IND:
      case AddrMode::AIX:
        result.output.push_back(opcode);
        result.output.push_back(static_cast<uint8_t>(value & 0xFF));
        result.output.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        pc += 3;
        break;

      default:
        addError(line.lineNumber, "Internal error: unhandled addressing mode");
        continue;
    }
  }

  result.endAddress = pc;
  result.success = result.errors.empty();

  // Copy symbol table into result for inspection
  result.symbols.clear();
  result.symbols.reserve(symbols.size());
  for (const auto& [name, value] : symbols) {
    AsmSymbol sym;
    std::strncpy(sym.name, name.c_str(), sizeof(sym.name) - 1);
    sym.name[sizeof(sym.name) - 1] = '\0';
    sym.value = value;
    result.symbols.push_back(sym);
  }
  // Sort alphabetically
  std::sort(result.symbols.begin(), result.symbols.end(),
            [](const AsmSymbol& a, const AsmSymbol& b) {
              return std::strcmp(a.name, b.name) < 0;
            });

  return result;
}

} // namespace a2e
