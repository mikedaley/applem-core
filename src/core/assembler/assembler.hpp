/*
 * assembler.hpp - Merlin-compatible 65C02 assembler
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include <map>
#include <unordered_map>

namespace a2e {

// Maximum error message length
static constexpr int ASM_MAX_ERROR_MSG = 120;

// Assembly diagnostic. `warning` entries do not fail the assembly: they mark
// something Merlin would have done that this assembler cannot (interactive
// input, relocatable output) but that need not stop the object code coming out.
struct AsmError {
  int lineNumber;
  bool warning;
  char message[ASM_MAX_ERROR_MSG];
};

// Symbol entry for exposing the symbol table
struct AsmSymbol {
  char name[64];
  int32_t value;
};

// A contiguous run of object bytes and the address it assembles to. A source
// with one ORG produces one segment; every further ORG starts another, so a
// host can place each run where it belongs instead of assuming the object is
// one block starting at `origin`.
struct AsmSegment {
  uint16_t address;
  uint32_t offset;  // index into AsmResult::output
  uint32_t length;
};

// What one line of the main source produced. Only lines of the top-level
// source get an entry, and only the first time a line is assembled — a line
// inside a LUP or a macro body has no single address to report.
struct AsmLineInfo {
  int32_t lineNumber;
  uint16_t address;
  uint16_t cycles;    // 0 when the line is not an instruction
  uint8_t byteCount;  // total bytes the line emitted (saturates at 255)
  uint8_t bytes[4];   // first four of them, for a gutter display
};

// Assembly result
struct AsmResult {
  std::vector<uint8_t> output;
  std::vector<AsmError> errors;
  std::vector<AsmSymbol> symbols;
  std::vector<AsmSegment> segments;
  std::vector<AsmLineInfo> lines;
  std::string listing;
  uint16_t origin;
  uint16_t endAddress;
  bool success;

  // DSK/SAV directive: the object file the assembled code should be written
  // to, the drive to write it to (1 or 2), and the ProDOS file type TYP asked
  // for. The assembler only records what was asked for — it has no disk of its
  // own to write to, so the caller that owns the drives performs the write.
  bool hasObjectFile;
  char objectFilename[32];
  int objectDrive;
  int objectType;  // ProDOS file type; $06 (BIN) unless TYP says otherwise
};

// Resolves a PUT/USE filename to source text. Returns false when the file
// cannot be found, in which case the assembler reports the failure against the
// directive's line. Merlin read these off the disk in the drive; the host wires
// this to whatever it has.
using AsmIncludeProvider =
    std::function<bool(const std::string& name, std::string& outText)>;

class Assembler {
public:
  Assembler();

  /**
   * Assemble source text into machine code.
   * @param source  Null-terminated source string
   * @return Assembly result with output bytes and errors
   */
  AsmResult assemble(const char* source);

  /**
   * Install the resolver PUT and USE use to fetch included source. Without one
   * those directives report that no source is available.
   */
  void setIncludeProvider(AsmIncludeProvider provider) {
    includeProvider_ = std::move(provider);
  }

  /** Cycle count for an opcode byte, as the listing and gutter report it. */
  static int cyclesForOpcode(uint8_t opcode);

private:
  // ------------------------------------------------------------------
  // Source model
  // ------------------------------------------------------------------

  // One parsed source line. `reportLine` is the line of the *main* source an
  // error should be blamed on: for a line that came out of an include or a
  // macro body that is the PUT/call site, because that is the only line the
  // editor can put a marker against.
  struct Line {
    std::string label;
    std::string mnemonic;
    std::string operand;
    std::string text;  // raw source text, for the listing
    int lineNumber;    // 1-based within its own file
    int reportLine;    // 1-based within the main source, 0 if not attributable
    bool isMain;       // came from the top-level source
  };

  struct Macro {
    std::string name;
    std::vector<Line> body;
  };

  // ------------------------------------------------------------------
  // Per-pass state
  // ------------------------------------------------------------------

  // One frame of DO/IF ... ELSE ... FIN nesting.
  struct Cond {
    bool active;       // are lines in this branch being assembled?
    bool everActive;   // has any branch of this conditional been taken?
    bool parentActive; // was the enclosing region active?
  };

  // Reverse opcode table: reverseOpcodes[mnemonicIndex][addrMode] = opcode byte
  // 0xFF = invalid combination
  uint8_t reverseOpcodes[99][16];
  bool reverseTableBuilt;

  AsmIncludeProvider includeProvider_;

  // Symbol table: name -> value. Ordered so the exported table and the
  // pass-to-pass comparison are both stable without a separate sort.
  std::map<std::string, int32_t> symbols;

  // What the previous pass made of the same source. A forward reference reads
  // its value from here, which is what lets a single emitting pass resolve
  // labels it has not reached yet.
  std::map<std::string, int32_t> lastPassSymbols_;

  // The source text a ']' variable was last assigned, which is what Merlin's
  // IF compares a character against.
  std::unordered_map<std::string, std::string> variableText_;

  // Macros defined so far this pass, keyed by uppercase name.
  std::unordered_map<std::string, Macro> macros_;

  // Files already pulled in by PUT/USE this pass, so a circular include stops.
  std::vector<std::string> includeStack_;

  AsmResult* result_;      // the result being built this pass
  bool finalPass_;         // record errors, listing and line info?
  bool unresolved_;        // did the last expression reference an unknown symbol?
  bool ended_;             // has END been reached?

  uint16_t pc;             // current assembly address
  uint16_t objAddress_;    // OBJ target; recorded, never acted on
  bool inDummy_;           // inside DUM..DEND, so nothing is emitted
  uint16_t dummyResumePC_; // pc to restore at DEND
  uint8_t checksum_;       // running EOR of every byte emitted, for CHK
  bool listingOn_;
  bool cycleCounts_;       // CYC: annotate the listing with cycle counts
  bool sweet16_;           // SW: interpret Sweet-16 mnemonics
  bool needNewSegment_;    // the next byte emitted starts a fresh segment
  bool originSet_;         // has the first ORG been seen?
  int xcCount_;            // XC directives seen; the second one asks for 65816
  int totalCycles_;        // CYC AVE running total
  std::string globalLabel_; // scope that a :local label belongs to
  std::vector<Cond> conds_;
  int macroDepth_;
  int expansionCounter_;   // makes each macro expansion's locals unique
  std::vector<int32_t> lastSymbolValues_;

  // Lines of the main source that have already been recorded in
  // AsmResult::lines, so a LUP body reports its first iteration only.
  std::vector<bool> lineRecorded_;

  // ------------------------------------------------------------------
  // Setup
  // ------------------------------------------------------------------

  void buildReverseOpcodeTable();
  int findMnemonicIndex(const std::string& mnemonic);
  bool isBranchMnemonic(int mnemonicIndex);
  bool isZPRMnemonic(int mnemonicIndex);

  // ------------------------------------------------------------------
  // Parsing
  // ------------------------------------------------------------------

  std::vector<Line> parseSource(const std::string& source, bool isMain,
                                int reportLine);
  Line parseLine(const std::string& line, int lineNumber, bool isMain,
                 int reportLine);

  // ------------------------------------------------------------------
  // Expressions
  // ------------------------------------------------------------------

  // Merlin evaluates strictly left to right with no operator precedence.
  int32_t evaluate(const std::string& expr, bool& error, std::string& errorMsg);
  int32_t evalTerm(const char*& p, bool& error, std::string& errorMsg);
  bool lookupSymbol(const std::string& name, int32_t& value);
  std::string qualifyLabel(const std::string& name);

  // ------------------------------------------------------------------
  // Emission
  // ------------------------------------------------------------------

  void emitByte(uint8_t value);
  void startSegment();
  void defineSymbol(const std::string& name, int32_t value, const Line& line);

  // ------------------------------------------------------------------
  // Execution
  // ------------------------------------------------------------------

  void runPass(const std::vector<Line>& lines, AsmResult& out, bool finalPass);
  void execLines(const std::vector<Line>& lines);
  void execLine(const Line& line, size_t& index,
                const std::vector<Line>& lines);
  bool active() const {
    return conds_.empty() ? true : conds_.back().active;
  }

  // Collect the body of a block directive, consuming its terminator.
  std::vector<Line> collectBlock(const std::vector<Line>& lines, size_t& index,
                                 const char* opener, const char* const* closers,
                                 bool& unterminated);

  void expandMacro(const Macro& macro, const std::string& operand,
                   const Line& callSite);
  void expandAndRecord(const Macro& macro, const std::string& operand,
                       const Line& callSite);

  void assembleInstruction(const Line& line, const std::string& mnemonic);
  void assembleSweet16(const Line& line, const std::string& mnemonic);
  bool handleDirective(const Line& line, const std::string& directive,
                       size_t& index, const std::vector<Line>& lines);

  // Directive helpers
  void emitDataList(const std::string& operand, int byteCount, bool bigEndian,
                    const Line& line);
  void emitString(const std::string& operand, const std::string& directive,
                  const Line& line);
  void emitHex(const std::string& operand, const Line& line);
  void emitStorage(const std::string& operand, const Line& line);

  bool parseObjectFileOperand(const std::string& operand, std::string& filename,
                              int& drive, std::string& errorMsg);

  // ------------------------------------------------------------------
  // Diagnostics and listing
  // ------------------------------------------------------------------

  void addError(const Line& line, const std::string& msg);
  void addWarning(const Line& line, const std::string& msg);
  void listLine(const Line& line, uint16_t address, const uint8_t* bytes,
                int count, int cycles);
  void recordLine(const Line& line, uint16_t address, const uint8_t* bytes,
                  int count, int cycles);

  // Bytes emitted while assembling the line currently in flight, so the
  // listing and the per-line record see what it produced.
  std::vector<uint8_t> lineBytes_;
  uint16_t lineStartPC_;
};

} // namespace a2e
