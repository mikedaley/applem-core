#pragma once

#include <cstdint>
#include <vector>

namespace a2e {

/**
 * Standalone 6502/65C02 disassembler
 *
 * Returns raw structured data for frontend rendering.
 */

// Addressing modes
enum class AddrMode : uint8_t {
  IMP = 0,   // Implied
  ACC = 1,   // Accumulator
  IMM = 2,   // Immediate #$nn
  ZP = 3,    // Zero Page $nn
  ZPX = 4,   // Zero Page,X $nn,X
  ZPY = 5,   // Zero Page,Y $nn,Y
  ABS = 6,   // Absolute $nnnn
  ABX = 7,   // Absolute,X $nnnn,X
  ABY = 8,   // Absolute,Y $nnnn,Y
  IND = 9,   // Indirect ($nnnn)
  IZX = 10,  // Indexed Indirect ($nn,X)
  IZY = 11,  // Indirect Indexed ($nn),Y
  REL = 12,  // Relative (branches)
  ZPI = 13,  // Zero Page Indirect ($nn) - 65C02
  AIX = 14,  // Absolute Indexed Indirect ($nnnn,X) - 65C02
  ZPR = 15   // Zero Page Relative (BBR/BBS) - 65C02
};

// Instruction categories for syntax highlighting
enum class InstrCategory : uint8_t {
  BRANCH = 0,   // Jumps and branches
  LOAD = 1,     // Load/store
  MATH = 2,     // Math and logic
  STACK = 3,    // Stack and register operations
  FLAG = 4,     // Flag operations
  UNKNOWN = 5   // Unknown/illegal opcodes
};

// Disassembled instruction data - 16 bytes per instruction
// Layout: [address:2][target:2][length:1][opcode:1][op1:1][op2:1][mode:1][cat:1][mnem:4][pad:2]
struct DisasmInstruction {
  uint16_t address;       // offset 0-1: Memory address
  uint16_t target;        // offset 2-3: Branch/jump target address
  uint8_t length;         // offset 4: Instruction length (1-3)
  uint8_t opcode;         // offset 5: Opcode byte
  uint8_t operand1;       // offset 6: First operand byte
  uint8_t operand2;       // offset 7: Second operand byte
  uint8_t mode;           // offset 8: AddrMode enum value
  uint8_t category;       // offset 9: InstrCategory enum value
  char mnemonic[4];       // offset 10-13: Mnemonic (null-padded)
  uint8_t padding[2];     // offset 14-15: Alignment padding
};

// Result structure for disassembly
struct DisasmResult {
  std::vector<DisasmInstruction> instructions;
};

/**
 * Get the length of an instruction given its opcode
 */
int getInstructionLength(uint8_t opcode);

/**
 * Get mnemonic string for an opcode
 */
const char* getMnemonic(uint8_t opcode);

/**
 * Get mnemonic string by index (for use with DisasmInstruction.mnemonicIndex)
 */
const char* getMnemonicByIndex(uint8_t index);

/**
 * Get addressing mode for an opcode
 */
AddrMode getAddressingMode(uint8_t opcode);

/**
 * Get instruction category for an opcode
 */
InstrCategory getInstructionCategory(uint8_t opcode);

/**
 * Disassemble a single instruction from raw data
 */
DisasmInstruction disassembleInstruction(const uint8_t *data, size_t size,
                                          uint16_t address);

/**
 * Disassemble a block of raw binary data
 */
DisasmResult disassembleBlock(const uint8_t *data, size_t size,
                              uint16_t baseAddress);

} // namespace a2e
