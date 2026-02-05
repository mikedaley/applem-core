/*
 * assembler.hpp - 65C02 multi-pass assembler
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>

namespace a2e {

// Maximum error message length
static constexpr int ASM_MAX_ERROR_MSG = 80;

// Assembly error
struct AsmError {
  int lineNumber;
  char message[ASM_MAX_ERROR_MSG];
};

// Symbol entry for exposing the symbol table
struct AsmSymbol {
  char name[64];
  int32_t value;
};

// Assembly result
struct AsmResult {
  std::vector<uint8_t> output;
  std::vector<AsmError> errors;
  std::vector<AsmSymbol> symbols;
  uint16_t origin;
  uint16_t endAddress;
  bool success;
};

class Assembler {
public:
  Assembler();

  /**
   * Assemble source text into machine code.
   * @param source  Null-terminated source string
   * @return Assembly result with output bytes and errors
   */
  AsmResult assemble(const char* source);

private:
  // Parsed source line
  struct ParsedLine {
    std::string label;
    std::string mnemonic;
    std::string operand;
    int lineNumber;
  };

  // Reverse opcode table: reverseOpcodes[mnemonicIndex][addrMode] = opcode byte
  // 0xFF = invalid combination
  uint8_t reverseOpcodes[99][16];
  bool reverseTableBuilt;

  // Symbol table: label -> address
  std::unordered_map<std::string, int32_t> symbols;

  void buildReverseOpcodeTable();
  std::vector<ParsedLine> parseSource(const char* source);
  ParsedLine parseLine(const char* line, int lineNumber);

  // Expression evaluation
  int32_t evaluateExpression(const std::string& expr, bool& error,
                             std::string& errorMsg, int lineNumber);
  int32_t evalAddSub(const char*& p, bool& error, std::string& errorMsg,
                     int lineNumber);
  int32_t evalMulDiv(const char*& p, bool& error, std::string& errorMsg,
                     int lineNumber);
  int32_t evalUnary(const char*& p, bool& error, std::string& errorMsg,
                    int lineNumber);
  int32_t evalPrimary(const char*& p, bool& error, std::string& errorMsg,
                      int lineNumber);

  // Addressing mode detection
  uint8_t detectAddressingMode(const std::string& mnemonic,
                               const std::string& operand,
                               int32_t value, bool valueKnown);

  // Instruction sizing
  int getInstructionSize(const std::string& mnemonic,
                         const std::string& operand,
                         bool labelsComplete);

  // Directive processing
  int getDirectiveSize(const std::string& directive, const std::string& operand,
                       bool& error, std::string& errorMsg, int lineNumber);
  void emitDirective(const std::string& directive, const std::string& operand,
                     std::vector<uint8_t>& output,
                     bool& error, std::string& errorMsg, int lineNumber);

  // Mnemonic lookup
  int findMnemonicIndex(const std::string& mnemonic);

  // Helper to check if a mnemonic is a branch instruction
  bool isBranchMnemonic(int mnemonicIndex);
  bool isZPRMnemonic(int mnemonicIndex);

  // Current PC during assembly
  uint16_t pc;
};

} // namespace a2e
