/*
 * assembler.cpp - Merlin-compatible 65C02 assembler
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 *
 * The assembler models Merlin rather than a generic 65C02 assembler, and three
 * of its habits are load-bearing:
 *
 * Expressions evaluate strictly left to right with no operator precedence, so
 * 1+2*3 is 9. That is what Merlin does, and real Merlin sources are written
 * assuming it; adding precedence would silently change the bytes they produce.
 *
 * A line is four whitespace-separated fields — label, opcode, operand, comment
 * — and the comment needs no semicolon. An operand therefore ends at the first
 * space outside a string, which is also why Merlin operands never contain
 * spaces.
 *
 * There is no "pass 1 sizes, pass 2 emits" split. The whole source is run to
 * completion repeatedly until the symbol table and the object size stop moving,
 * then once more with diagnostics on. Macros, conditionals and loops make the
 * line stream depend on symbol values, so a sizing pass that did not emit could
 * not have followed the same path as the pass that did.
 */

#include "assembler.hpp"
#include "../disassembler/disassembler.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace a2e {

// ============================================================================
// Tables
// ============================================================================

// Directives that produce or reserve object bytes, control assembly, or are
// accepted and ignored. Anything not here is a mnemonic or a macro call.
static const char* DIRECTIVES[] = {
  // Object layout
  "ORG", "OBJ", "EQU", "=", "VAR", "DS", "DUM", "DEND",
  // Data
  "DFB", "DB", "DW", "DA", "DDB", "ADR", "ADRL", "HEX", "CHK",
  // Strings
  "ASC", "DCI", "INV", "FLS", "REV", "STR", "STRL",
  // Control flow
  "DO", "IF", "ELSE", "FIN", "LUP", "--^", "ELUP", "END", "ERR",
  // Macros
  "MAC", "EOM", "<<<", ">>>", "PMC",
  // Source files
  "PUT", "USE", "CHN",
  // Object file
  "DSK", "SAV", "TYP",
  // Listing and assembler options
  "LST", "EXP", "PAG", "TTL", "SKP", "PAU", "DAT", "CYC", "TR", "KBD",
  "USR", "SW", "XC", "MX",
  // Recognised, not supported
  "REL", "ENT", "EXT", "LNK",
  nullptr
};

// Directives whose operand opens with a delimiter character and may therefore
// contain spaces.
static bool isStringDirective(const std::string& s) {
  return s == "ASC" || s == "DCI" || s == "INV" || s == "FLS" ||
         s == "REV" || s == "STR" || s == "STRL";
}

static bool isDirective(const std::string& s) {
  for (int i = 0; DIRECTIVES[i]; i++) {
    if (s == DIRECTIVES[i]) return true;
  }
  return false;
}

// Base cycle counts per opcode. Page-crossing and branch-taken penalties are
// not included: a listing reports the instruction's own cost, not what the
// operands happen to do at run time.
static const uint8_t CYCLE_TABLE[256] = {
  /* 00 */ 7, 6, 2, 1, 5, 3, 5, 5, 3, 2, 2, 1, 6, 4, 6, 5,
  /* 10 */ 2, 5, 5, 1, 5, 4, 6, 5, 2, 4, 2, 1, 6, 4, 6, 5,
  /* 20 */ 6, 6, 2, 1, 3, 3, 5, 5, 4, 2, 2, 1, 4, 4, 6, 5,
  /* 30 */ 2, 5, 5, 1, 4, 4, 6, 5, 2, 4, 2, 1, 4, 4, 6, 5,
  /* 40 */ 6, 6, 2, 1, 3, 3, 5, 5, 3, 2, 2, 1, 3, 4, 6, 5,
  /* 50 */ 2, 5, 5, 1, 4, 4, 6, 5, 2, 4, 3, 1, 8, 4, 6, 5,
  /* 60 */ 6, 6, 2, 1, 3, 3, 5, 5, 4, 2, 2, 1, 6, 4, 6, 5,
  /* 70 */ 2, 5, 5, 1, 4, 4, 6, 5, 2, 4, 4, 1, 6, 4, 6, 5,
  /* 80 */ 3, 6, 2, 1, 3, 3, 3, 5, 2, 2, 2, 1, 4, 4, 4, 5,
  /* 90 */ 2, 6, 5, 1, 4, 4, 4, 5, 2, 5, 2, 1, 4, 5, 5, 5,
  /* A0 */ 2, 6, 2, 1, 3, 3, 3, 5, 2, 2, 2, 1, 4, 4, 4, 5,
  /* B0 */ 2, 5, 5, 1, 4, 4, 4, 5, 2, 4, 2, 1, 4, 4, 4, 5,
  /* C0 */ 2, 6, 2, 1, 3, 3, 5, 5, 2, 2, 2, 3, 4, 4, 6, 5,
  /* D0 */ 2, 5, 5, 1, 4, 4, 6, 5, 2, 4, 3, 3, 4, 4, 7, 5,
  /* E0 */ 2, 6, 2, 1, 3, 3, 5, 5, 2, 2, 2, 1, 4, 4, 6, 5,
  /* F0 */ 2, 5, 5, 1, 4, 4, 6, 5, 2, 4, 4, 1, 4, 4, 7, 5,
};

int Assembler::cyclesForOpcode(uint8_t opcode) {
  return CYCLE_TABLE[opcode];
}

// Sweet-16 interpreter mnemonics, enabled by the SW directive.
namespace {
enum class S16Kind : uint8_t {
  REG,      // Rn         one byte
  REG_IND,  // @Rn        one byte
  SET,      // Rn,expr    three bytes
  BRANCH,   // target     two bytes, relative
  IMPLIED   // -          one byte
};

struct S16Op {
  const char* name;
  uint8_t base;
  S16Kind kind;
};

const S16Op SWEET16_OPS[] = {
  {"SET", 0x10, S16Kind::SET},
  {"LD",  0x20, S16Kind::REG},      // LD @Rn is patched to 0x40 below
  {"ST",  0x30, S16Kind::REG},      // ST @Rn is patched to 0x50 below
  {"LDD", 0x60, S16Kind::REG_IND},
  {"STD", 0x70, S16Kind::REG_IND},
  {"POP", 0x80, S16Kind::REG_IND},
  {"STP", 0x90, S16Kind::REG_IND},
  {"ADD", 0xA0, S16Kind::REG},
  {"SUB", 0xB0, S16Kind::REG},
  {"POPD",0xC0, S16Kind::REG_IND},
  {"CPR", 0xD0, S16Kind::REG},
  {"INR", 0xE0, S16Kind::REG},
  {"DCR", 0xF0, S16Kind::REG},
  {"RTN", 0x00, S16Kind::IMPLIED},
  {"BR",  0x01, S16Kind::BRANCH},
  {"BNC", 0x02, S16Kind::BRANCH},
  {"BC",  0x03, S16Kind::BRANCH},
  {"BP",  0x04, S16Kind::BRANCH},
  {"BM",  0x05, S16Kind::BRANCH},
  {"BZ",  0x06, S16Kind::BRANCH},
  {"BNZ", 0x07, S16Kind::BRANCH},
  {"BM1", 0x08, S16Kind::BRANCH},
  {"BNM1",0x09, S16Kind::BRANCH},
  {"BK",  0x0A, S16Kind::IMPLIED},
  {"RS",  0x0B, S16Kind::IMPLIED},
  {"BS",  0x0C, S16Kind::BRANCH},
};

const S16Op* findSweet16(const std::string& name) {
  for (const auto& op : SWEET16_OPS) {
    if (name == op.name) return &op;
  }
  return nullptr;
}
} // namespace

// ============================================================================
// Small string helpers
// ============================================================================

static std::string toUpper(const std::string& s) {
  std::string r = s;
  for (auto& c : r) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  return r;
}

static std::string trim(const std::string& s) {
  size_t start = 0, end = s.size();
  while (start < end && (s[start] == ' ' || s[start] == '\t')) start++;
  while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t')) end--;
  return s.substr(start, end - start);
}

static void skipSpaces(const char*& p) {
  while (*p && (*p == ' ' || *p == '\t')) p++;
}

// A label may hold letters, digits and the punctuation Merlin allows in a
// symbol. ']' and ':' lead a variable or a local label respectively.
static bool isIdentChar(char c) {
  return isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' ||
         c == ':' || c == ']' || c == '?';
}

static bool isIdentStart(char c) {
  return isalpha(static_cast<unsigned char>(c)) || c == '_' || c == ':' ||
         c == ']' || c == '?';
}

// Merlin picks the high bit from the delimiter: an apostrophe and anything
// above it in ASCII gives plain ASCII, anything below gives high ASCII. That
// is the rule behind the familiar pair — ' is $27 and " is $22.
static bool delimiterSetsHighBit(char delim) {
  return static_cast<unsigned char>(delim) < 0x27;
}

// Split a Merlin macro parameter list. Parameters are separated by ';', which
// is why a macro call's operand is never scanned for a comment.
static std::vector<std::string> splitMacroParams(const std::string& s) {
  std::vector<std::string> parts;
  std::string current;
  for (char c : s) {
    if (c == ';') {
      parts.push_back(current);
      current.clear();
    } else {
      current += c;
    }
  }
  parts.push_back(current);
  return parts;
}

// ============================================================================
// Constructor / opcode tables
// ============================================================================

Assembler::Assembler()
    : reverseTableBuilt(false), result_(nullptr), finalPass_(false),
      unresolved_(false), ended_(false), pc(0x0800), objAddress_(0),
      inDummy_(false), dummyResumePC_(0), checksum_(0), listingOn_(true),
      cycleCounts_(false), sweet16_(false), totalCycles_(0), macroDepth_(0),
      expansionCounter_(0), lineStartPC_(0) {
  memset(reverseOpcodes, 0xFF, sizeof(reverseOpcodes));
}

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

int Assembler::findMnemonicIndex(const std::string& mnemonic) {
  int count = getMnemonicCount();
  for (int i = 1; i < count; i++) {
    if (mnemonic == getMnemonicByIndex(static_cast<uint8_t>(i))) {
      return i;
    }
  }
  return -1;
}

bool Assembler::isBranchMnemonic(int mnemonicIndex) {
  return mnemonicIndex >= 0 && mnemonicIndex < 99 &&
         reverseOpcodes[mnemonicIndex][static_cast<int>(AddrMode::REL)] != 0xFF;
}

bool Assembler::isZPRMnemonic(int mnemonicIndex) {
  return mnemonicIndex >= 0 && mnemonicIndex < 99 &&
         reverseOpcodes[mnemonicIndex][static_cast<int>(AddrMode::ZPR)] != 0xFF;
}

// ============================================================================
// Parsing
// ============================================================================

std::vector<Assembler::Line> Assembler::parseSource(const std::string& source,
                                                    bool isMain,
                                                    int reportLine) {
  std::vector<Line> lines;
  const char* p = source.c_str();
  int lineNum = 1;

  while (*p) {
    const char* lineStart = p;
    while (*p && *p != '\n' && *p != '\r') p++;
    std::string lineStr(lineStart, static_cast<size_t>(p - lineStart));
    if (*p == '\r') p++;
    if (*p == '\n') p++;

    lines.push_back(parseLine(lineStr, lineNum, isMain, reportLine));
    lineNum++;
  }

  return lines;
}

Assembler::Line Assembler::parseLine(const std::string& text, int lineNumber,
                                     bool isMain, int reportLine) {
  Line result;
  result.text = text;
  result.lineNumber = lineNumber;
  result.reportLine = isMain ? lineNumber : reportLine;
  result.isMain = isMain;

  const char* line = text.c_str();
  const char* p = line;

  skipSpaces(p);
  if (!*p) return result;

  // A '*' or ';' in the first column is a comment for the whole line. '*' is
  // only a comment there — elsewhere it is the program counter.
  if (*p == ';') return result;
  if (line[0] == '*') return result;

  p = line;

  // Field 1: label, only when the line starts in column 1.
  if (*p && *p != ' ' && *p != '\t') {
    const char* start = p;
    while (*p && !isspace(static_cast<unsigned char>(*p))) p++;
    result.label = std::string(start, static_cast<size_t>(p - start));
    // A trailing colon is not Merlin, but it is what most people type. A lone
    // ':' prefix marks a local label and must survive.
    if (result.label.size() > 1 && result.label.back() == ':') {
      result.label.pop_back();
    }
  }

  // Field 2: opcode / directive / macro name.
  skipSpaces(p);
  if (!*p || *p == ';') return result;
  const char* opcStart = p;
  while (*p && !isspace(static_cast<unsigned char>(*p))) p++;
  result.mnemonic = toUpper(std::string(opcStart, static_cast<size_t>(p - opcStart)));

  // Field 3: operand. It ends at the first space outside a string, because
  // whatever follows that space is Merlin's comment field.
  skipSpaces(p);
  if (!*p || *p == ';') return result;

  const char* opStart = p;
  if (isStringDirective(result.mnemonic)) {
    // The operand opens with a delimiter of the author's choosing, so the
    // string runs to the next copy of that character whatever it is.
    char delim = *p;
    p++;
    while (*p && *p != delim) p++;
    if (*p == delim) p++;
  }
  while (*p && !isspace(static_cast<unsigned char>(*p))) {
    if (*p == '\'' || *p == '"') {
      char q = *p;
      p++;
      while (*p && *p != q) p++;
      if (!*p) break;
    }
    p++;
  }
  result.operand = std::string(opStart, static_cast<size_t>(p - opStart));

  return result;
}

// ============================================================================
// Expressions
//
// Merlin has no operator precedence and no grouping: terms are combined left
// to right in the order written.
// ============================================================================

std::string Assembler::qualifyLabel(const std::string& name) {
  if (!name.empty() && name[0] == ':') {
    return globalLabel_ + name;
  }
  return name;
}

bool Assembler::lookupSymbol(const std::string& name, int32_t& value) {
  auto it = symbols.find(name);
  if (it != symbols.end()) {
    value = it->second;
    return true;
  }
  // A symbol defined later in the source resolves to the value the previous
  // pass gave it; the pass loop keeps going until those stop moving.
  auto prev = lastPassSymbols_.find(name);
  if (prev != lastPassSymbols_.end()) {
    value = prev->second;
    return true;
  }
  return false;
}

int32_t Assembler::evaluate(const std::string& expr, bool& error,
                            std::string& errorMsg) {
  const char* p = expr.c_str();
  skipSpaces(p);

  if (!*p) {
    error = true;
    errorMsg = "Missing expression";
    return 0;
  }

  // A byte selector applies to the whole expression, not just the first term:
  // #>LABEL+1 is the high byte of LABEL+1.
  char selector = 0;
  if (*p == '<' || *p == '>' || *p == '^') {
    selector = *p;
    p++;
    skipSpaces(p);
  }

  int32_t value = evalTerm(p, error, errorMsg);
  if (error) return 0;

  while (*p) {
    skipSpaces(p);
    char op = *p;
    if (op != '+' && op != '-' && op != '*' && op != '/' && op != '!' &&
        op != '.' && op != '&') {
      break;
    }
    p++;
    int32_t rhs = evalTerm(p, error, errorMsg);
    if (error) return 0;
    switch (op) {
      case '+': value += rhs; break;
      case '-': value -= rhs; break;
      case '*': value *= rhs; break;
      case '/':
        if (rhs == 0) {
          error = true;
          errorMsg = "Division by zero";
          return 0;
        }
        value /= rhs;
        break;
      case '!': value ^= rhs; break;   // Merlin: exclusive or
      case '.': value |= rhs; break;   // Merlin: or
      case '&': value &= rhs; break;   // Merlin: and
      default: break;
    }
  }

  switch (selector) {
    case '<': return value & 0xFF;
    case '>': return (value >> 8) & 0xFF;
    case '^': return (value >> 16) & 0xFF;
    default: return value;
  }
}

int32_t Assembler::evalTerm(const char*& p, bool& error,
                            std::string& errorMsg) {
  skipSpaces(p);

  if (*p == '-') {
    p++;
    return -evalTerm(p, error, errorMsg);
  }

  // Current program counter — the address of the line being assembled.
  if (*p == '*') {
    p++;
    return static_cast<int32_t>(pc);
  }

  if (*p == '$') {
    p++;
    if (!isxdigit(static_cast<unsigned char>(*p))) {
      error = true;
      errorMsg = "Expected hex digit after $";
      return 0;
    }
    int32_t val = 0;
    while (isxdigit(static_cast<unsigned char>(*p))) {
      val *= 16;
      if (*p >= '0' && *p <= '9') val += *p - '0';
      else val += (toupper(static_cast<unsigned char>(*p)) - 'A') + 10;
      p++;
    }
    return val;
  }

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

  // Character constants. The delimiter chooses the high bit exactly as it does
  // for a string, so 'A is $41 and "A" is $C1.
  if (*p == '\'' || *p == '"') {
    char delim = *p;
    p++;
    if (!*p) {
      error = true;
      errorMsg = "Unterminated character constant";
      return 0;
    }
    int32_t val = static_cast<uint8_t>(*p);
    if (delimiterSetsHighBit(delim)) val |= 0x80;
    p++;
    if (*p == delim) p++;  // the closing delimiter is optional in Merlin
    return val;
  }

  if (isdigit(static_cast<unsigned char>(*p))) {
    int32_t val = 0;
    while (isdigit(static_cast<unsigned char>(*p))) {
      val = val * 10 + (*p - '0');
      p++;
    }
    return val;
  }

  if (isIdentStart(*p)) {
    const char* start = p;
    while (*p && isIdentChar(*p)) p++;
    std::string name = toUpper(std::string(start, static_cast<size_t>(p - start)));
    std::string key = qualifyLabel(name);

    int32_t value = 0;
    if (lookupSymbol(key, value)) return value;

    // On the passes before the last one an unknown name is just a symbol the
    // source has not reached yet. Nothing is reported, but the caller is told
    // the value is a guess so it does not size the instruction as zero page.
    unresolved_ = true;
    if (finalPass_) {
      error = true;
      errorMsg = "Undefined symbol: " + name;
    }
    return 0;
  }

  error = true;
  errorMsg = std::string("Unexpected character '") + *p + "' in expression";
  return 0;
}

// ============================================================================
// Emission
// ============================================================================

void Assembler::startSegment() {
  needNewSegment_ = true;
}

void Assembler::emitByte(uint8_t value) {
  uint16_t addr = pc;
  pc = static_cast<uint16_t>(pc + 1);

  // A dummy section reserves addresses without producing object code.
  if (inDummy_) return;

  lineBytes_.push_back(value);
  checksum_ = static_cast<uint8_t>(checksum_ ^ value);

  if (needNewSegment_ || result_->segments.empty()) {
    AsmSegment seg;
    seg.address = addr;
    seg.offset = static_cast<uint32_t>(result_->output.size());
    seg.length = 0;
    result_->segments.push_back(seg);
    needNewSegment_ = false;
  }
  result_->output.push_back(value);
  result_->segments.back().length++;
}

void Assembler::defineSymbol(const std::string& name, int32_t value,
                             const Line& line) {
  std::string plain = toUpper(name);
  std::string key = qualifyLabel(plain);

  // Variables are meant to be reassigned; everything else is defined once.
  bool isVariable = !plain.empty() && plain[0] == ']';
  if (!isVariable && finalPass_ && symbols.count(key)) {
    addError(line, "Duplicate symbol: " + name);
    return;
  }

  symbols[key] = value;
  // A global label opens a new scope for the local labels that follow it. The
  // test is on the name as written: the key has already been qualified, so a
  // local's key no longer looks local.
  if (!plain.empty() && plain[0] != ':' && plain[0] != ']') {
    globalLabel_ = plain;
  }
}

// ============================================================================
// Diagnostics and listing
// ============================================================================

void Assembler::addError(const Line& line, const std::string& msg) {
  if (!finalPass_) return;
  AsmError err;
  err.lineNumber = line.reportLine;
  err.warning = false;
  std::string full = msg;
  if (!line.isMain) {
    char prefix[32];
    snprintf(prefix, sizeof(prefix), "(line %d) ", line.lineNumber);
    full = std::string(prefix) + msg;
  }
  strncpy(err.message, full.c_str(), ASM_MAX_ERROR_MSG - 1);
  err.message[ASM_MAX_ERROR_MSG - 1] = '\0';
  result_->errors.push_back(err);
}

void Assembler::addWarning(const Line& line, const std::string& msg) {
  if (!finalPass_) return;
  AsmError err;
  err.lineNumber = line.reportLine;
  err.warning = true;
  strncpy(err.message, msg.c_str(), ASM_MAX_ERROR_MSG - 1);
  err.message[ASM_MAX_ERROR_MSG - 1] = '\0';
  result_->errors.push_back(err);
}

void Assembler::listLine(const Line& line, uint16_t address,
                         const uint8_t* bytes, int count, int cycles) {
  if (!finalPass_ || !listingOn_) return;

  char buf[128];
  std::string byteField;
  int shown = count < 4 ? count : 4;
  for (int i = 0; i < shown; i++) {
    char hex[4];
    snprintf(hex, sizeof(hex), "%02X ", bytes[i]);
    byteField += hex;
  }
  while (byteField.size() < 12) byteField += ' ';

  if (count > 0) {
    snprintf(buf, sizeof(buf), "%5d  %04X: %s", line.lineNumber, address,
             byteField.c_str());
  } else {
    snprintf(buf, sizeof(buf), "%5d        %s", line.lineNumber,
             std::string(12, ' ').c_str());
  }
  result_->listing += buf;

  if (cycleCounts_) {
    char cyc[8];
    if (cycles > 0) {
      snprintf(cyc, sizeof(cyc), "%2d ", cycles);
    } else {
      snprintf(cyc, sizeof(cyc), "   ");
    }
    result_->listing += cyc;
  }

  result_->listing += line.text;
  result_->listing += '\n';
}

void Assembler::recordLine(const Line& line, uint16_t address,
                           const uint8_t* bytes, int count, int cycles) {
  if (!finalPass_ || !line.isMain) return;
  size_t index = static_cast<size_t>(line.lineNumber);
  if (index >= lineRecorded_.size()) lineRecorded_.resize(index + 1, false);
  if (lineRecorded_[index]) return;  // a LUP body reports its first pass only
  lineRecorded_[index] = true;

  AsmLineInfo info;
  info.lineNumber = line.lineNumber;
  info.address = address;
  info.cycles = static_cast<uint16_t>(cycles);
  info.byteCount = static_cast<uint8_t>(count > 255 ? 255 : count);
  for (int i = 0; i < 4; i++) {
    info.bytes[i] = i < count ? bytes[i] : 0;
  }
  result_->lines.push_back(info);
}

// ============================================================================
// Pass driver
// ============================================================================

AsmResult Assembler::assemble(const char* source) {
  buildReverseOpcodeTable();

  std::vector<Line> lines = parseSource(source ? source : "", true, 0);

  lastPassSymbols_.clear();
  symbols.clear();

  // Run the source to completion until the symbol table and the object size
  // stop changing. Zero-page selection shrinks instructions once a forward
  // reference is known, which moves every later label, so this normally
  // settles on the second or third pass.
  const int MAX_PASSES = 16;
  size_t previousSize = static_cast<size_t>(-1);
  for (int pass = 0; pass < MAX_PASSES; pass++) {
    AsmResult scratch;
    std::map<std::string, int32_t> before = symbols;
    runPass(lines, scratch, false);
    if (pass > 0 && symbols == before && scratch.output.size() == previousSize) {
      break;
    }
    previousSize = scratch.output.size();
  }

  AsmResult result;
  runPass(lines, result, true);

  // Export the symbol table. std::map already holds it in name order.
  result.symbols.clear();
  result.symbols.reserve(symbols.size());
  for (const auto& entry : symbols) {
    AsmSymbol sym;
    std::strncpy(sym.name, entry.first.c_str(), sizeof(sym.name) - 1);
    sym.name[sizeof(sym.name) - 1] = '\0';
    sym.value = entry.second;
    result.symbols.push_back(sym);
  }

  return result;
}

void Assembler::runPass(const std::vector<Line>& lines, AsmResult& out,
                        bool finalPass) {
  out.output.clear();
  out.errors.clear();
  out.segments.clear();
  out.lines.clear();
  out.listing.clear();
  out.origin = 0x0800;
  out.endAddress = 0x0800;
  out.success = false;
  out.hasObjectFile = false;
  out.objectFilename[0] = '\0';
  out.objectDrive = 1;
  out.objectType = 0x06;  // ProDOS BIN

  result_ = &out;
  finalPass_ = finalPass;
  ended_ = false;
  unresolved_ = false;

  lastPassSymbols_ = symbols;
  symbols.clear();
  variableText_.clear();
  macros_.clear();
  includeStack_.clear();
  conds_.clear();
  lineRecorded_.assign(lines.size() + 2, false);

  pc = 0x0800;
  objAddress_ = 0;
  inDummy_ = false;
  dummyResumePC_ = 0;
  checksum_ = 0;
  listingOn_ = true;
  cycleCounts_ = false;
  sweet16_ = false;
  totalCycles_ = 0;
  globalLabel_.clear();
  macroDepth_ = 0;
  expansionCounter_ = 0;
  needNewSegment_ = true;
  originSet_ = false;
  xcCount_ = 0;

  execLines(lines);

  if (!conds_.empty() && finalPass) {
    AsmError err;
    err.lineNumber = 0;
    err.warning = false;
    strncpy(err.message, "DO/IF without matching FIN", ASM_MAX_ERROR_MSG - 1);
    err.message[ASM_MAX_ERROR_MSG - 1] = '\0';
    out.errors.push_back(err);
  }

  if (!out.segments.empty()) {
    out.origin = out.segments.front().address;
  }
  out.endAddress = pc;

  bool fatal = false;
  for (const auto& err : out.errors) {
    if (!err.warning) fatal = true;
  }
  out.success = !fatal;

  result_ = nullptr;
}

// ============================================================================
// Execution
// ============================================================================

void Assembler::execLines(const std::vector<Line>& lines) {
  for (size_t i = 0; i < lines.size() && !ended_; i++) {
    execLine(lines[i], i, lines);
  }
}

std::vector<Assembler::Line> Assembler::collectBlock(
    const std::vector<Line>& lines, size_t& index, const char* opener,
    const char* const* closers, bool& unterminated) {
  std::vector<Line> body;
  int depth = 1;
  size_t i = index + 1;
  for (; i < lines.size(); i++) {
    const std::string& m = lines[i].mnemonic;
    if (m == opener) {
      depth++;
    } else {
      bool isCloser = false;
      for (int c = 0; closers[c]; c++) {
        if (m == closers[c]) { isCloser = true; break; }
      }
      if (isCloser) {
        depth--;
        if (depth == 0) break;
      }
    }
    body.push_back(lines[i]);
  }
  unterminated = (i >= lines.size());
  index = unterminated ? lines.size() - 1 : i;
  return body;
}

void Assembler::execLine(const Line& line, size_t& index,
                         const std::vector<Line>& lines) {
  const std::string& mnem = line.mnemonic;

  lineBytes_.clear();
  lineStartPC_ = pc;

  // ------------------------------------------------------------------
  // Conditionals. These are read even inside a region that is not being
  // assembled, so that nesting stays balanced.
  // ------------------------------------------------------------------
  if (mnem == "DO" || mnem == "IF") {
    bool parentActive = active();
    bool taken = false;
    if (parentActive) {
      if (mnem == "IF") {
        // Merlin's IF compares a character with the first character of its
        // second operand: IF "A",]1. It is written for macro parameters, and a
        // parameter has already been substituted textually by the time the
        // line is read — so the comparison is against text, not a value. That
        // is what makes IF "",]1 the idiom for "was this parameter omitted?".
        size_t comma = line.operand.find(',');
        if (comma != std::string::npos) {
          std::string lhs = trim(line.operand.substr(0, comma));
          std::string rhs = trim(line.operand.substr(comma + 1));
          if (!lhs.empty() && (lhs[0] == '"' || lhs[0] == '\'')) {
            char quote = lhs[0];
            bool closed = lhs.size() >= 2 && lhs.back() == quote;
            lhs = closed ? lhs.substr(1, lhs.size() - 2) : lhs.substr(1);
          }
          char want = lhs.empty() ? 0 : lhs[0];

          std::string haveText = rhs;
          if (!rhs.empty() && rhs[0] == ']') {
            auto it = variableText_.find(qualifyLabel(toUpper(rhs)));
            haveText = it != variableText_.end() ? it->second : std::string();
          }
          if (!haveText.empty() &&
              (haveText[0] == '"' || haveText[0] == '\'')) {
            haveText = haveText.substr(1);
          }
          char have = haveText.empty() ? 0 : haveText[0];
          taken = (want == have);
        } else {
          bool error = false;
          std::string msg;
          taken = evaluate(line.operand, error, msg) != 0;
          if (error) addError(line, "IF: " + msg);
        }
      } else {
        bool error = false;
        std::string msg;
        taken = evaluate(line.operand, error, msg) != 0;
        if (error) addError(line, "DO: " + msg);
      }
    }
    Cond frame;
    frame.parentActive = parentActive;
    frame.active = parentActive && taken;
    frame.everActive = frame.active;
    conds_.push_back(frame);
    listLine(line, pc, nullptr, 0, 0);
    return;
  }

  if (mnem == "ELSE") {
    if (conds_.empty()) {
      addError(line, "ELSE without DO");
    } else {
      Cond& frame = conds_.back();
      frame.active = frame.parentActive && !frame.everActive;
      frame.everActive = frame.everActive || frame.active;
    }
    listLine(line, pc, nullptr, 0, 0);
    return;
  }

  if (mnem == "FIN") {
    if (conds_.empty()) {
      addError(line, "FIN without DO");
    } else {
      conds_.pop_back();
    }
    listLine(line, pc, nullptr, 0, 0);
    return;
  }

  // ------------------------------------------------------------------
  // Block bodies must be consumed whether or not they are being assembled,
  // or their contents would leak into the surrounding source.
  // ------------------------------------------------------------------
  if (mnem == "MAC") {
    static const char* closers[] = {"EOM", "<<<", nullptr};
    bool unterminated = false;
    std::vector<Line> body = collectBlock(lines, index, "MAC", closers,
                                          unterminated);
    if (unterminated) {
      addError(line, "MAC without EOM");
      return;
    }
    if (active()) {
      if (line.label.empty()) {
        addError(line, "MAC needs a name in the label field");
      } else {
        Macro macro;
        macro.name = toUpper(line.label);
        macro.body = std::move(body);
        macros_[macro.name] = std::move(macro);
      }
    }
    listLine(line, pc, nullptr, 0, 0);
    return;
  }

  if (mnem == "LUP") {
    static const char* closers[] = {"--^", "ELUP", nullptr};
    bool unterminated = false;
    std::vector<Line> body = collectBlock(lines, index, "LUP", closers,
                                          unterminated);
    if (unterminated) {
      addError(line, "LUP without --^");
      return;
    }
    if (active()) {
      bool error = false;
      std::string msg;
      int32_t count = evaluate(line.operand, error, msg);
      if (error) {
        addError(line, "LUP: " + msg);
        return;
      }
      if (count < 0 || count > 0x8000) {
        addError(line, "LUP count out of range (0..32768)");
        return;
      }
      listLine(line, pc, nullptr, 0, 0);
      for (int32_t i = 0; i < count && !ended_; i++) {
        execLines(body);
      }
      return;
    }
    listLine(line, pc, nullptr, 0, 0);
    return;
  }

  if (!active()) {
    listLine(line, pc, nullptr, 0, 0);
    return;
  }

  // ------------------------------------------------------------------
  // Label
  // ------------------------------------------------------------------
  bool labelIsAddress = true;
  if (mnem == "EQU" || mnem == "=" || mnem == "VAR" || mnem == "KBD" ||
      mnem == "ORG" || mnem == "DUM") {
    labelIsAddress = false;
  }
  if (!line.label.empty() && labelIsAddress) {
    defineSymbol(line.label, static_cast<int32_t>(pc), line);
  }

  if (mnem.empty()) {
    listLine(line, pc, nullptr, 0, 0);
    if (!line.label.empty()) recordLine(line, pc, nullptr, 0, 0);
    return;
  }

  // ------------------------------------------------------------------
  // Directive, macro call or instruction
  // ------------------------------------------------------------------
  if (isDirective(mnem)) {
    handleDirective(line, mnem, index, lines);
    return;
  }

  auto macro = macros_.find(mnem);
  if (macro != macros_.end()) {
    listLine(line, pc, nullptr, 0, 0);
    expandAndRecord(macro->second, line.operand, line);
    return;
  }

  assembleInstruction(line, mnem);
}

// ============================================================================
// Macros
// ============================================================================

// Expand a macro and credit whatever it emitted to the line that called it.
// The body's own lines came from a macro definition, not from the source being
// edited, so the call site is the only line an editor can hang the bytes on.
void Assembler::expandAndRecord(const Macro& macro, const std::string& operand,
                                const Line& callSite) {
  uint16_t address = pc;
  size_t before = result_->output.size();
  expandMacro(macro, operand, callSite);
  size_t after = result_->output.size();
  int count = static_cast<int>(after - before);
  recordLine(callSite, address, count ? &result_->output[before] : nullptr,
             count, 0);
}

void Assembler::expandMacro(const Macro& macro, const std::string& operand,
                            const Line& callSite) {
  if (macroDepth_ >= 32) {
    addError(callSite, "Macro nesting too deep (recursive macro?)");
    return;
  }

  std::vector<std::string> params = splitMacroParams(operand);

  // Merlin scopes a macro's local labels to one expansion, so the same macro
  // can be called twice without its :loops colliding.
  std::string savedGlobal = globalLabel_;
  char scope[24];
  snprintf(scope, sizeof(scope), "\x01M%d", ++expansionCounter_);
  globalLabel_ = scope;
  macroDepth_++;

  std::vector<Line> body;
  body.reserve(macro.body.size());
  for (const Line& src : macro.body) {
    // Parameters substitute textually, then the line is parsed again: a
    // parameter can carry a whole operand, so it can change the line's shape.
    std::string text = src.text;
    std::string expanded;
    for (size_t i = 0; i < text.size(); i++) {
      if (text[i] == ']' && i + 1 < text.size() && text[i + 1] >= '1' &&
          text[i + 1] <= '8' &&
          (i + 2 >= text.size() || !isIdentChar(text[i + 2]))) {
        size_t which = static_cast<size_t>(text[i + 1] - '1');
        if (which < params.size()) expanded += trim(params[which]);
        i++;
        continue;
      }
      expanded += text[i];
    }
    body.push_back(parseLine(expanded, src.lineNumber, false,
                             callSite.reportLine));
  }

  execLines(body);

  macroDepth_--;
  globalLabel_ = savedGlobal;
}

// ============================================================================
// Directives
// ============================================================================

bool Assembler::handleDirective(const Line& line, const std::string& directive,
                                size_t& index, const std::vector<Line>& lines) {
  (void)index;
  (void)lines;
  bool error = false;
  std::string msg;
  uint16_t address = pc;

  // ---- Object layout ----

  if (directive == "ORG") {
    int32_t value = evaluate(line.operand, error, msg);
    if (error) { addError(line, "ORG: " + msg); return true; }
    pc = static_cast<uint16_t>(value);
    startSegment();
    if (!originSet_) {
      result_->origin = pc;
      originSet_ = true;
    }
    if (!line.label.empty()) defineSymbol(line.label, static_cast<int32_t>(pc), line);
    listLine(line, pc, nullptr, 0, 0);
    recordLine(line, pc, nullptr, 0, 0);
    return true;
  }

  if (directive == "OBJ") {
    // Merlin uses OBJ to say where the object code is buffered while it is
    // being built. Nothing here buffers anything, so the value is recorded and
    // otherwise has no effect.
    int32_t value = evaluate(line.operand, error, msg);
    if (!error) objAddress_ = static_cast<uint16_t>(value);
    listLine(line, pc, nullptr, 0, 0);
    return true;
  }

  if (directive == "EQU" || directive == "=") {
    if (line.label.empty()) {
      addError(line, directive + " needs a label");
      return true;
    }
    int32_t value = evaluate(line.operand, error, msg);
    if (error) {
      addError(line, directive + ": " + msg);
      return true;
    }
    defineSymbol(line.label, value, line);
    if (!line.label.empty() && line.label[0] == ']') {
      variableText_[toUpper(line.label)] = trim(line.operand);
    }
    listLine(line, pc, nullptr, 0, 0);
    return true;
  }

  if (directive == "VAR") {
    // Merlin 16: VAR loads ]1, ]2 ... from a ';'-separated list.
    std::vector<std::string> values = splitMacroParams(line.operand);
    for (size_t i = 0; i < values.size() && i < 8; i++) {
      std::string text = trim(values[i]);
      if (text.empty()) continue;
      int32_t value = evaluate(text, error, msg);
      if (error) { addError(line, "VAR: " + msg); return true; }
      char name[4] = {']', static_cast<char>('1' + i), '\0', '\0'};
      symbols[name] = value;
      variableText_[name] = text;
    }
    listLine(line, pc, nullptr, 0, 0);
    return true;
  }

  if (directive == "DS") {
    emitStorage(line.operand, line);
    listLine(line, address, lineBytes_.data(), static_cast<int>(lineBytes_.size()), 0);
    recordLine(line, address, lineBytes_.data(), static_cast<int>(lineBytes_.size()), 0);
    return true;
  }

  if (directive == "DUM") {
    int32_t value = evaluate(line.operand, error, msg);
    if (error) { addError(line, "DUM: " + msg); return true; }
    if (!inDummy_) dummyResumePC_ = pc;
    inDummy_ = true;
    pc = static_cast<uint16_t>(value);
    if (!line.label.empty()) defineSymbol(line.label, static_cast<int32_t>(pc), line);
    listLine(line, pc, nullptr, 0, 0);
    return true;
  }

  if (directive == "DEND") {
    if (!inDummy_) {
      addError(line, "DEND without DUM");
      return true;
    }
    inDummy_ = false;
    pc = dummyResumePC_;
    startSegment();
    listLine(line, pc, nullptr, 0, 0);
    return true;
  }

  // ---- Data ----

  if (directive == "DFB" || directive == "DB") {
    emitDataList(line.operand, 1, false, line);
  } else if (directive == "DW" || directive == "DA") {
    emitDataList(line.operand, 2, false, line);
  } else if (directive == "DDB") {
    emitDataList(line.operand, 2, true, line);
  } else if (directive == "ADR") {
    emitDataList(line.operand, 3, false, line);
  } else if (directive == "ADRL") {
    emitDataList(line.operand, 4, false, line);
  } else if (directive == "HEX") {
    emitHex(line.operand, line);
  } else if (isStringDirective(directive)) {
    emitString(line.operand, directive, line);
  } else if (directive == "CHK") {
    // The checksum byte is the exclusive or of everything emitted so far, so
    // it has to be read before it is written.
    uint8_t sum = checksum_;
    emitByte(sum);
  } else {
    // ---- Control flow, files and options ----

    if (directive == "END") {
      ended_ = true;
      listLine(line, pc, nullptr, 0, 0);
      return true;
    }

    if (directive == "ERR") {
      int32_t value = evaluate(line.operand, error, msg);
      if (error) {
        addError(line, "ERR: " + msg);
      } else if (value != 0) {
        addError(line, "ERR: assertion failed");
      }
      listLine(line, pc, nullptr, 0, 0);
      return true;
    }

    if (directive == "PMC" || directive == ">>>") {
      std::string operand = line.operand;
      size_t sep = operand.find(';');
      std::string name = toUpper(trim(sep == std::string::npos
                                          ? operand
                                          : operand.substr(0, sep)));
      std::string params =
          sep == std::string::npos ? "" : operand.substr(sep + 1);
      auto macro = macros_.find(name);
      if (macro == macros_.end()) {
        addError(line, "Undefined macro: " + name);
        return true;
      }
      listLine(line, pc, nullptr, 0, 0);
      expandAndRecord(macro->second, params, line);
      return true;
    }

    if (directive == "EOM" || directive == "<<<") {
      addError(line, directive + " without MAC");
      return true;
    }

    if (directive == "--^" || directive == "ELUP") {
      addError(line, directive + " without LUP");
      return true;
    }

    if (directive == "PUT" || directive == "USE" || directive == "CHN") {
      std::string name = trim(line.operand);
      listLine(line, pc, nullptr, 0, 0);
      if (name.empty()) {
        addError(line, directive + " needs a filename");
        return true;
      }
      if (!includeProvider_) {
        addError(line, directive + ": no source files are available here");
        return true;
      }
      if (includeStack_.size() >= 8) {
        addError(line, directive + ": includes nested too deeply");
        return true;
      }
      for (const auto& open : includeStack_) {
        if (toUpper(open) == toUpper(name)) {
          addError(line, directive + ": " + name + " includes itself");
          return true;
        }
      }
      std::string text;
      if (!includeProvider_(name, text)) {
        addError(line, directive + ": cannot read " + name);
        return true;
      }
      includeStack_.push_back(name);
      std::vector<Line> included = parseSource(text, false, line.reportLine);
      execLines(included);
      includeStack_.pop_back();
      // CHN hands assembly over to the named file rather than returning.
      if (directive == "CHN") ended_ = true;
      return true;
    }

    if (directive == "DSK" || directive == "SAV") {
      if (result_->hasObjectFile) {
        addError(line, directive + ": only one object file per assembly is supported");
        return true;
      }
      std::string filename;
      std::string errorMsg;
      int drive = 1;
      if (!parseObjectFileOperand(line.operand, filename, drive, errorMsg)) {
        addError(line, directive + ": " + errorMsg);
        return true;
      }
      strncpy(result_->objectFilename, filename.c_str(),
              sizeof(result_->objectFilename) - 1);
      result_->objectFilename[sizeof(result_->objectFilename) - 1] = '\0';
      result_->objectDrive = drive;
      result_->hasObjectFile = true;
      listLine(line, pc, nullptr, 0, 0);
      return true;
    }

    if (directive == "TYP") {
      int32_t value = evaluate(line.operand, error, msg);
      if (error) {
        addError(line, "TYP: " + msg);
      } else {
        result_->objectType = value & 0xFF;
      }
      listLine(line, pc, nullptr, 0, 0);
      return true;
    }

    if (directive == "LST") {
      std::string arg = toUpper(trim(line.operand));
      listingOn_ = !(arg == "OFF" || arg == "0");
      listLine(line, pc, nullptr, 0, 0);
      return true;
    }

    if (directive == "CYC") {
      std::string arg = toUpper(trim(line.operand));
      cycleCounts_ = !(arg == "OFF");
      listLine(line, pc, nullptr, 0, 0);
      return true;
    }

    if (directive == "SW") {
      sweet16_ = toUpper(trim(line.operand)) != "OFF";
      listLine(line, pc, nullptr, 0, 0);
      return true;
    }

    if (directive == "XC") {
      // The first XC enables 65C02 opcodes, which are always on here. A second
      // one asks for the 65816, which the //e's processor cannot run.
      if (toUpper(trim(line.operand)) == "OFF") {
        xcCount_ = 0;
      } else if (++xcCount_ >= 2) {
        addWarning(line, "XC: 65816 opcodes are not available on a //e");
      }
      listLine(line, pc, nullptr, 0, 0);
      return true;
    }

    if (directive == "KBD") {
      // Merlin prompted the operator during assembly. Nothing here can, so the
      // variable takes the operand's value if it has one, and zero otherwise.
      int32_t value = 0;
      if (!trim(line.operand).empty()) {
        bool err2 = false;
        std::string m2;
        int32_t parsed = evaluate(line.operand, err2, m2);
        if (!err2) value = parsed;
      }
      if (!line.label.empty()) defineSymbol(line.label, value, line);
      addWarning(line, "KBD cannot prompt during assembly; used " +
                           std::to_string(value));
      listLine(line, pc, nullptr, 0, 0);
      return true;
    }

    if (directive == "REL" || directive == "ENT" || directive == "EXT" ||
        directive == "LNK") {
      addError(line, directive + ": relocatable output needs a linker, which "
                                 "this assembler does not have");
      return true;
    }

    // EXP, PAG, TTL, SKP, PAU, DAT, TR, USR, MX shape a printed listing or an
    // option this assembler has no equivalent for. They are accepted so that a
    // real Merlin source assembles unchanged.
    listLine(line, pc, nullptr, 0, 0);
    return true;
  }

  listLine(line, address, lineBytes_.data(), static_cast<int>(lineBytes_.size()), 0);
  recordLine(line, address, lineBytes_.data(), static_cast<int>(lineBytes_.size()), 0);
  return true;
}

// ---- Data helpers ---------------------------------------------------------

void Assembler::emitDataList(const std::string& operand, int byteCount,
                             bool bigEndian, const Line& line) {
  if (trim(operand).empty()) {
    addError(line, "Expected a value");
    return;
  }
  const char* p = operand.c_str();
  while (*p) {
    skipSpaces(p);
    if (!*p) break;

    const char* start = p;
    while (*p && *p != ',') {
      if (*p == '\'' || *p == '"') {
        char q = *p;
        p++;
        while (*p && *p != q) p++;
        if (!*p) break;
      }
      p++;
    }
    std::string item = trim(std::string(start, static_cast<size_t>(p - start)));
    if (*p == ',') p++;
    if (item.empty()) continue;

    bool error = false;
    std::string msg;
    int32_t value = evaluate(item, error, msg);
    if (error) {
      addError(line, msg);
      return;
    }
    if (bigEndian) {
      for (int i = byteCount - 1; i >= 0; i--) {
        emitByte(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
      }
    } else {
      for (int i = 0; i < byteCount; i++) {
        emitByte(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
      }
    }
  }
}

void Assembler::emitHex(const std::string& operand, const Line& line) {
  const char* p = operand.c_str();
  auto hexVal = [](char c) -> uint8_t {
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    return static_cast<uint8_t>(toupper(static_cast<unsigned char>(c)) - 'A' + 10);
  };
  while (*p) {
    if (isspace(static_cast<unsigned char>(*p)) || *p == ',') { p++; continue; }
    if (!isxdigit(static_cast<unsigned char>(*p))) {
      addError(line, "Invalid hex digit");
      return;
    }
    char hi = *p++;
    if (!isxdigit(static_cast<unsigned char>(*p))) {
      addError(line, "Odd number of hex digits");
      return;
    }
    char lo = *p++;
    emitByte(static_cast<uint8_t>((hexVal(hi) << 4) | hexVal(lo)));
  }
}

void Assembler::emitStorage(const std::string& operand, const Line& line) {
  std::string arg = trim(operand);
  uint8_t fill = 0;

  size_t comma = arg.rfind(',');
  if (comma != std::string::npos) {
    bool error = false;
    std::string msg;
    int32_t value = evaluate(trim(arg.substr(comma + 1)), error, msg);
    if (error) {
      addError(line, "DS: " + msg);
      return;
    }
    fill = static_cast<uint8_t>(value & 0xFF);
    arg = trim(arg.substr(0, comma));
  }

  int32_t count = 0;
  if (arg == "\\") {
    // Merlin's page-align form: reserve up to the next $100 boundary.
    count = (pc & 0xFF) == 0 ? 0 : 0x100 - (pc & 0xFF);
  } else {
    bool error = false;
    std::string msg;
    count = evaluate(arg, error, msg);
    if (error) {
      addError(line, "DS: " + msg);
      return;
    }
  }

  if (count < 0 || count > 0x10000) {
    addError(line, "DS: count out of range");
    return;
  }
  for (int32_t i = 0; i < count; i++) emitByte(fill);
}

void Assembler::emitString(const std::string& operand,
                           const std::string& directive, const Line& line) {
  std::string arg = trim(operand);
  if (arg.empty()) {
    addError(line, directive + " needs a string");
    return;
  }

  size_t i = 0;
  bool emittedAnything = false;

  while (i < arg.size()) {
    if (arg[i] == ',') { i++; continue; }

    char first = arg[i];
    bool isDelimited = !isalnum(static_cast<unsigned char>(first));

    if (isDelimited) {
      char delim = first;
      size_t end = arg.find(delim, i + 1);
      if (end == std::string::npos) {
        addError(line, directive + ": unterminated string");
        return;
      }
      std::string text = arg.substr(i + 1, end - i - 1);
      i = end + 1;
      emittedAnything = true;

      bool high = delimiterSetsHighBit(delim);
      std::vector<uint8_t> chars;
      chars.reserve(text.size());
      for (char c : text) {
        uint8_t ch = static_cast<uint8_t>(c);
        if (directive == "INV" || directive == "FLS") {
          ch = static_cast<uint8_t>(toupper(static_cast<unsigned char>(ch)) & 0x3F);
          if (directive == "FLS") ch |= 0x40;
        } else if (high) {
          ch |= 0x80;
        }
        chars.push_back(ch);
      }

      if (directive == "REV") {
        std::reverse(chars.begin(), chars.end());
      } else if (directive == "DCI") {
        // Dextral character inverted: the last character carries the opposite
        // high bit, which is how a reader finds the end of the string.
        if (!chars.empty()) chars.back() ^= 0x80;
      } else if (directive == "STR") {
        if (chars.size() > 255) {
          addError(line, "STR: string longer than 255 characters");
          return;
        }
        emitByte(static_cast<uint8_t>(chars.size()));
      } else if (directive == "STRL") {
        emitByte(static_cast<uint8_t>(chars.size() & 0xFF));
        emitByte(static_cast<uint8_t>((chars.size() >> 8) & 0xFF));
      }

      for (uint8_t ch : chars) emitByte(ch);
      continue;
    }

    // Merlin lets hex bytes follow the string, with or without a comma, so a
    // terminator can be written as ASC "PROMPT"8D00.
    size_t start = i;
    while (i < arg.size() && isxdigit(static_cast<unsigned char>(arg[i]))) i++;
    std::string digits = arg.substr(start, i - start);
    if (digits.empty() || (digits.size() % 2) != 0) {
      addError(line, directive + ": expected pairs of hex digits after the string");
      return;
    }
    auto hexVal = [](char c) -> uint8_t {
      if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
      return static_cast<uint8_t>(toupper(static_cast<unsigned char>(c)) - 'A' + 10);
    };
    for (size_t d = 0; d + 1 < digits.size(); d += 2) {
      emitByte(static_cast<uint8_t>((hexVal(digits[d]) << 4) | hexVal(digits[d + 1])));
    }
    emittedAnything = true;
  }

  if (!emittedAnything) {
    addError(line, directive + " needs a string");
  }
}

// ============================================================================
// DSK / SAV operand
// ============================================================================

bool Assembler::parseObjectFileOperand(const std::string& operand,
                                       std::string& filename, int& drive,
                                       std::string& errorMsg) {
  filename.clear();
  drive = 1;

  // Split on commas: the first field is the filename, the rest are Merlin's
  // slot/drive qualifiers.
  std::vector<std::string> fields;
  std::string current;
  for (char c : operand) {
    if (c == ',') {
      fields.push_back(current);
      current.clear();
    } else {
      current += c;
    }
  }
  fields.push_back(current);

  filename = trim(fields[0]);
  // Merlin sources are unquoted, but a quoted name is the obvious thing to try
  if (filename.size() >= 2 && (filename.front() == '"' || filename.front() == '\'') &&
      filename.back() == filename.front()) {
    filename = filename.substr(1, filename.size() - 2);
  }

  if (filename.empty()) {
    errorMsg = "filename required";
    return false;
  }
  // 30 is the DOS 3.3 limit; ProDOS is stricter still, and the filesystem
  // itself rejects what it cannot store when the write happens.
  if (filename.size() > 30) {
    errorMsg = "filename too long (max 30 characters)";
    return false;
  }

  for (size_t i = 1; i < fields.size(); i++) {
    std::string qualifier = toUpper(trim(fields[i]));
    if (qualifier.empty()) continue;

    if (qualifier[0] == 'D' && qualifier.size() == 2 &&
        (qualifier[1] == '1' || qualifier[1] == '2')) {
      drive = qualifier[1] - '0';
      continue;
    }
    // A slot qualifier is accepted only for the slot the drives are actually
    // in; silently writing to a different drive than the source asked for
    // would be worse than saying no.
    if (qualifier[0] == 'S' && qualifier.size() == 2 && qualifier[1] == '6') {
      continue;
    }
    errorMsg = "unsupported qualifier '" + qualifier + "' (D1, D2 or S6 only)";
    return false;
  }

  return true;
}

// ============================================================================
// Instructions
// ============================================================================

namespace {
// How the operand was written, before any decision about how wide it is.
enum class OperandForm {
  NONE,      // no operand
  ACCUM,     // A
  IMMEDIATE, // #expr
  DIRECT,    // expr
  INDEX_X,   // expr,X
  INDEX_Y,   // expr,Y
  IND_X,     // (expr,X)
  IND_Y,     // (expr),Y
  INDIRECT,  // (expr)
  INVALID
};

struct ParsedOperand {
  OperandForm form = OperandForm::NONE;
  std::string expr;
  bool forceZeroPage = false;
  bool forceAbsolute = false;
};

ParsedOperand parseOperand(const std::string& raw) {
  ParsedOperand out;
  std::string s = trim(raw);
  if (s.empty()) return out;

  if (s[0] == '#') {
    out.form = OperandForm::IMMEDIATE;
    out.expr = trim(s.substr(1));
    return out;
  }

  // Outside an immediate, Merlin 16's '<' and '|'/'>' prefixes force the width
  // rather than select a byte.
  if (s[0] == '<') {
    out.forceZeroPage = true;
    s = trim(s.substr(1));
  } else if (s[0] == '|' || s[0] == '>') {
    out.forceAbsolute = true;
    s = trim(s.substr(1));
  }
  if (s.empty()) {
    out.form = OperandForm::INVALID;
    return out;
  }

  if (s[0] == '(') {
    size_t close = s.rfind(')');
    if (close == std::string::npos) {
      out.form = OperandForm::INVALID;
      return out;
    }
    std::string inner = trim(s.substr(1, close - 1));
    std::string after = trim(s.substr(close + 1));

    std::string upperInner = inner;
    for (auto& c : upperInner) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    if (upperInner.size() > 2 &&
        upperInner.compare(upperInner.size() - 2, 2, ",X") == 0) {
      out.form = OperandForm::IND_X;
      out.expr = trim(inner.substr(0, inner.size() - 2));
      return out;
    }

    std::string upperAfter = after;
    for (auto& c : upperAfter) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    if (upperAfter == ",Y") {
      out.form = OperandForm::IND_Y;
      out.expr = inner;
      return out;
    }
    if (after.empty()) {
      out.form = OperandForm::INDIRECT;
      out.expr = inner;
      return out;
    }
    out.form = OperandForm::INVALID;
    return out;
  }

  std::string upper = s;
  for (auto& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  if (upper == "A") {
    out.form = OperandForm::ACCUM;
    return out;
  }
  if (upper.size() > 2 && upper.compare(upper.size() - 2, 2, ",X") == 0) {
    out.form = OperandForm::INDEX_X;
    out.expr = trim(s.substr(0, s.size() - 2));
    return out;
  }
  if (upper.size() > 2 && upper.compare(upper.size() - 2, 2, ",Y") == 0) {
    out.form = OperandForm::INDEX_Y;
    out.expr = trim(s.substr(0, s.size() - 2));
    return out;
  }

  out.form = OperandForm::DIRECT;
  out.expr = s;
  return out;
}
} // namespace

void Assembler::assembleInstruction(const Line& line,
                                    const std::string& mnemonic) {
  if (sweet16_ && findSweet16(mnemonic)) {
    assembleSweet16(line, mnemonic);
    return;
  }

  int mnemIdx = findMnemonicIndex(mnemonic);
  if (mnemIdx < 0) {
    addError(line, "Unknown mnemonic or directive: " + mnemonic);
    return;
  }

  uint16_t address = pc;
  auto supports = [&](AddrMode mode) {
    return reverseOpcodes[mnemIdx][static_cast<int>(mode)] != 0xFF;
  };

  // ---- BBR/BBS take a zero page address and a branch target ----
  if (isZPRMnemonic(mnemIdx)) {
    size_t comma = line.operand.find(',');
    if (comma == std::string::npos) {
      addError(line, mnemonic + " needs a zp,target operand");
      return;
    }
    bool error = false;
    std::string msg;
    int32_t zpValue = evaluate(trim(line.operand.substr(0, comma)), error, msg);
    if (error) { addError(line, msg); return; }
    int32_t target = evaluate(trim(line.operand.substr(comma + 1)), error, msg);
    if (error) { addError(line, msg); return; }

    int32_t offset = target - (address + 3);
    if (finalPass_ && (offset < -128 || offset > 127)) {
      addError(line, "Branch target out of range");
      return;
    }
    uint8_t opcode = reverseOpcodes[mnemIdx][static_cast<int>(AddrMode::ZPR)];
    emitByte(opcode);
    emitByte(static_cast<uint8_t>(zpValue & 0xFF));
    emitByte(static_cast<uint8_t>(offset & 0xFF));
    listLine(line, address, lineBytes_.data(), static_cast<int>(lineBytes_.size()),
             CYCLE_TABLE[opcode]);
    recordLine(line, address, lineBytes_.data(), static_cast<int>(lineBytes_.size()),
               CYCLE_TABLE[opcode]);
    return;
  }

  ParsedOperand op = parseOperand(line.operand);
  if (op.form == OperandForm::INVALID) {
    addError(line, "Malformed operand: " + line.operand);
    return;
  }

  // "A" is a symbol before it is the accumulator: a source that defines A
  // means the symbol, and Merlin's own accumulator form is a bare mnemonic.
  if (op.form == OperandForm::ACCUM) {
    int32_t ignored = 0;
    if (!supports(AddrMode::ACC) || lookupSymbol("A", ignored)) {
      op.form = OperandForm::DIRECT;
      op.expr = "A";
    }
  }

  int32_t value = 0;
  bool valueKnown = false;
  bool wasUnresolved = false;
  if (op.form != OperandForm::NONE && op.form != OperandForm::ACCUM) {
    bool error = false;
    std::string msg;
    unresolved_ = false;
    value = evaluate(op.expr, error, msg);
    if (error) { addError(line, msg); return; }
    wasUnresolved = unresolved_;
    valueKnown = true;
  }

  // A value that is still a guess must not shrink the instruction to zero
  // page: doing so would move every label after it and never settle.
  bool fitsZeroPage = valueKnown && !wasUnresolved && value >= 0 && value <= 255;
  if (op.forceAbsolute) fitsZeroPage = false;
  if (op.forceZeroPage) fitsZeroPage = true;

  AddrMode mode;
  switch (op.form) {
    case OperandForm::NONE:
      if (supports(AddrMode::IMP)) mode = AddrMode::IMP;
      else if (supports(AddrMode::ACC)) mode = AddrMode::ACC;
      else { addError(line, mnemonic + " needs an operand"); return; }
      break;
    case OperandForm::ACCUM:
      mode = AddrMode::ACC;
      break;
    case OperandForm::IMMEDIATE:
      mode = AddrMode::IMM;
      break;
    case OperandForm::IND_X:
      mode = (fitsZeroPage && supports(AddrMode::IZX))
                 ? AddrMode::IZX
                 : (supports(AddrMode::AIX) ? AddrMode::AIX : AddrMode::IZX);
      break;
    case OperandForm::IND_Y:
      mode = AddrMode::IZY;
      break;
    case OperandForm::INDIRECT:
      mode = (fitsZeroPage && supports(AddrMode::ZPI)) ? AddrMode::ZPI
                                                       : AddrMode::IND;
      break;
    case OperandForm::INDEX_X:
      mode = (fitsZeroPage && supports(AddrMode::ZPX)) ? AddrMode::ZPX
                                                       : AddrMode::ABX;
      break;
    case OperandForm::INDEX_Y:
      mode = (fitsZeroPage && supports(AddrMode::ZPY)) ? AddrMode::ZPY
                                                       : AddrMode::ABY;
      break;
    case OperandForm::DIRECT:
    default:
      if (isBranchMnemonic(mnemIdx)) mode = AddrMode::REL;
      else if (fitsZeroPage && supports(AddrMode::ZP)) mode = AddrMode::ZP;
      else mode = AddrMode::ABS;
      break;
  }

  uint8_t opcode = reverseOpcodes[mnemIdx][static_cast<int>(mode)];
  if (opcode == 0xFF) {
    addError(line, mnemonic + " does not support this addressing mode");
    return;
  }

  switch (mode) {
    case AddrMode::IMP:
    case AddrMode::ACC:
      emitByte(opcode);
      break;

    case AddrMode::REL: {
      int32_t offset = value - (address + 2);
      if (finalPass_ && (offset < -128 || offset > 127)) {
        addError(line, "Branch target out of range");
        return;
      }
      emitByte(opcode);
      emitByte(static_cast<uint8_t>(offset & 0xFF));
      break;
    }

    case AddrMode::IMM:
    case AddrMode::ZP:
    case AddrMode::ZPX:
    case AddrMode::ZPY:
    case AddrMode::IZX:
    case AddrMode::IZY:
    case AddrMode::ZPI:
      emitByte(opcode);
      emitByte(static_cast<uint8_t>(value & 0xFF));
      break;

    default:
      emitByte(opcode);
      emitByte(static_cast<uint8_t>(value & 0xFF));
      emitByte(static_cast<uint8_t>((value >> 8) & 0xFF));
      break;
  }

  totalCycles_ += CYCLE_TABLE[opcode];
  listLine(line, address, lineBytes_.data(), static_cast<int>(lineBytes_.size()),
           CYCLE_TABLE[opcode]);
  recordLine(line, address, lineBytes_.data(), static_cast<int>(lineBytes_.size()),
             CYCLE_TABLE[opcode]);
}

// ============================================================================
// Sweet-16
// ============================================================================

void Assembler::assembleSweet16(const Line& line,
                                const std::string& mnemonic) {
  const S16Op* op = findSweet16(mnemonic);
  uint16_t address = pc;
  std::string operand = trim(line.operand);

  auto parseRegister = [&](const std::string& text, bool& indirect,
                           bool& ok) -> int32_t {
    std::string s = trim(text);
    indirect = false;
    ok = false;
    if (!s.empty() && s[0] == '@') {
      indirect = true;
      s = trim(s.substr(1));
    }
    if (s.size() < 2 || toupper(static_cast<unsigned char>(s[0])) != 'R') {
      return 0;
    }
    bool error = false;
    std::string msg;
    int32_t reg = evaluate(s.substr(1), error, msg);
    if (error || reg < 0 || reg > 15) return 0;
    ok = true;
    return reg;
  };

  switch (op->kind) {
    case S16Kind::IMPLIED:
      emitByte(op->base);
      break;

    case S16Kind::BRANCH: {
      bool error = false;
      std::string msg;
      int32_t target = evaluate(operand, error, msg);
      if (error) { addError(line, msg); return; }
      int32_t offset = target - (address + 2);
      if (finalPass_ && (offset < -128 || offset > 127)) {
        addError(line, "Sweet-16 branch target out of range");
        return;
      }
      emitByte(op->base);
      emitByte(static_cast<uint8_t>(offset & 0xFF));
      break;
    }

    case S16Kind::SET: {
      size_t comma = operand.find(',');
      if (comma == std::string::npos) {
        addError(line, "SET needs Rn,value");
        return;
      }
      bool indirect = false, ok = false;
      int32_t reg = parseRegister(operand.substr(0, comma), indirect, ok);
      if (!ok || indirect) {
        addError(line, "SET needs a register R0-R15");
        return;
      }
      bool error = false;
      std::string msg;
      int32_t value = evaluate(trim(operand.substr(comma + 1)), error, msg);
      if (error) { addError(line, msg); return; }
      emitByte(static_cast<uint8_t>(op->base | reg));
      emitByte(static_cast<uint8_t>(value & 0xFF));
      emitByte(static_cast<uint8_t>((value >> 8) & 0xFF));
      break;
    }

    case S16Kind::REG:
    case S16Kind::REG_IND: {
      bool indirect = false, ok = false;
      int32_t reg = parseRegister(operand, indirect, ok);
      if (!ok) {
        addError(line, mnemonic + " needs a register R0-R15");
        return;
      }
      uint8_t base = op->base;
      if (op->kind == S16Kind::REG && indirect) {
        // LD and ST have a second, indirect form one nibble higher.
        if (mnemonic == "LD") base = 0x40;
        else if (mnemonic == "ST") base = 0x50;
        else {
          addError(line, mnemonic + " has no indirect form");
          return;
        }
      }
      if (op->kind == S16Kind::REG_IND && !indirect) {
        // Merlin accepts the register with or without the '@' for these.
      }
      emitByte(static_cast<uint8_t>(base | reg));
      break;
    }
  }

  listLine(line, address, lineBytes_.data(), static_cast<int>(lineBytes_.size()), 0);
  recordLine(line, address, lineBytes_.data(), static_cast<int>(lineBytes_.size()), 0);
}

} // namespace a2e
