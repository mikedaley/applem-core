#include "disassembler.hpp"
#include <iomanip>
#include <sstream>

namespace a2e {

// Addressing mode enumeration
enum AddrMode {
  IMP,  // Implied
  ACC,  // Accumulator
  IMM,  // Immediate #$nn
  ZP,   // Zero Page $nn
  ZPX,  // Zero Page,X $nn,X
  ZPY,  // Zero Page,Y $nn,Y
  ABS,  // Absolute $nnnn
  ABX,  // Absolute,X $nnnn,X
  ABY,  // Absolute,Y $nnnn,Y
  IND,  // Indirect ($nnnn)
  IZX,  // Indexed Indirect ($nn,X)
  IZY,  // Indirect Indexed ($nn),Y
  REL,  // Relative (branches)
  ZPI,  // Zero Page Indirect ($nn) - 65C02
  AIX,  // Absolute Indexed Indirect ($nnnn,X) - 65C02
  ZPR   // Zero Page Relative (BBR/BBS) - 65C02
};

// Instruction category for syntax highlighting
enum InstrCategory {
  CAT_BRANCH,  // Jumps and branches
  CAT_LOAD,    // Load/store
  CAT_MATH,    // Math and logic
  CAT_STACK,   // Stack and register operations
  CAT_FLAG,    // Flag operations
  CAT_UNKNOWN  // Unknown/illegal opcodes
};

// Opcode info: mnemonic, addressing mode, and category
struct OpcodeInfo {
  const char *mnemonic;
  AddrMode mode;
  InstrCategory category;
};

// Full 65C02 opcode table
static const OpcodeInfo opcodes[256] = {
    // 0x00-0x0F
    {"BRK", IMP, CAT_STACK}, {"ORA", IZX, CAT_MATH},   {"???", IMP, CAT_UNKNOWN}, {"???", IMP, CAT_UNKNOWN},
    {"TSB", ZP, CAT_MATH},   {"ORA", ZP, CAT_MATH},    {"ASL", ZP, CAT_MATH},     {"RMB0", ZP, CAT_MATH},
    {"PHP", IMP, CAT_STACK}, {"ORA", IMM, CAT_MATH},   {"ASL", ACC, CAT_MATH},    {"???", IMP, CAT_UNKNOWN},
    {"TSB", ABS, CAT_MATH},  {"ORA", ABS, CAT_MATH},   {"ASL", ABS, CAT_MATH},    {"BBR0", ZPR, CAT_BRANCH},
    // 0x10-0x1F
    {"BPL", REL, CAT_BRANCH}, {"ORA", IZY, CAT_MATH},   {"ORA", ZPI, CAT_MATH},    {"???", IMP, CAT_UNKNOWN},
    {"TRB", ZP, CAT_MATH},    {"ORA", ZPX, CAT_MATH},   {"ASL", ZPX, CAT_MATH},    {"RMB1", ZP, CAT_MATH},
    {"CLC", IMP, CAT_FLAG},   {"ORA", ABY, CAT_MATH},   {"INC", ACC, CAT_MATH},    {"???", IMP, CAT_UNKNOWN},
    {"TRB", ABS, CAT_MATH},   {"ORA", ABX, CAT_MATH},   {"ASL", ABX, CAT_MATH},    {"BBR1", ZPR, CAT_BRANCH},
    // 0x20-0x2F
    {"JSR", ABS, CAT_BRANCH}, {"AND", IZX, CAT_MATH},   {"???", IMP, CAT_UNKNOWN}, {"???", IMP, CAT_UNKNOWN},
    {"BIT", ZP, CAT_MATH},    {"AND", ZP, CAT_MATH},    {"ROL", ZP, CAT_MATH},     {"RMB2", ZP, CAT_MATH},
    {"PLP", IMP, CAT_STACK},  {"AND", IMM, CAT_MATH},   {"ROL", ACC, CAT_MATH},    {"???", IMP, CAT_UNKNOWN},
    {"BIT", ABS, CAT_MATH},   {"AND", ABS, CAT_MATH},   {"ROL", ABS, CAT_MATH},    {"BBR2", ZPR, CAT_BRANCH},
    // 0x30-0x3F
    {"BMI", REL, CAT_BRANCH}, {"AND", IZY, CAT_MATH},   {"AND", ZPI, CAT_MATH},    {"???", IMP, CAT_UNKNOWN},
    {"BIT", ZPX, CAT_MATH},   {"AND", ZPX, CAT_MATH},   {"ROL", ZPX, CAT_MATH},    {"RMB3", ZP, CAT_MATH},
    {"SEC", IMP, CAT_FLAG},   {"AND", ABY, CAT_MATH},   {"DEC", ACC, CAT_MATH},    {"???", IMP, CAT_UNKNOWN},
    {"BIT", ABX, CAT_MATH},   {"AND", ABX, CAT_MATH},   {"ROL", ABX, CAT_MATH},    {"BBR3", ZPR, CAT_BRANCH},
    // 0x40-0x4F
    {"RTI", IMP, CAT_BRANCH}, {"EOR", IZX, CAT_MATH},   {"???", IMP, CAT_UNKNOWN}, {"???", IMP, CAT_UNKNOWN},
    {"???", IMP, CAT_UNKNOWN},{"EOR", ZP, CAT_MATH},    {"LSR", ZP, CAT_MATH},     {"RMB4", ZP, CAT_MATH},
    {"PHA", IMP, CAT_STACK},  {"EOR", IMM, CAT_MATH},   {"LSR", ACC, CAT_MATH},    {"???", IMP, CAT_UNKNOWN},
    {"JMP", ABS, CAT_BRANCH}, {"EOR", ABS, CAT_MATH},   {"LSR", ABS, CAT_MATH},    {"BBR4", ZPR, CAT_BRANCH},
    // 0x50-0x5F
    {"BVC", REL, CAT_BRANCH}, {"EOR", IZY, CAT_MATH},   {"EOR", ZPI, CAT_MATH},    {"???", IMP, CAT_UNKNOWN},
    {"???", IMP, CAT_UNKNOWN},{"EOR", ZPX, CAT_MATH},   {"LSR", ZPX, CAT_MATH},    {"RMB5", ZP, CAT_MATH},
    {"CLI", IMP, CAT_FLAG},   {"EOR", ABY, CAT_MATH},   {"PHY", IMP, CAT_STACK},   {"???", IMP, CAT_UNKNOWN},
    {"???", IMP, CAT_UNKNOWN},{"EOR", ABX, CAT_MATH},   {"LSR", ABX, CAT_MATH},    {"BBR5", ZPR, CAT_BRANCH},
    // 0x60-0x6F
    {"RTS", IMP, CAT_BRANCH}, {"ADC", IZX, CAT_MATH},   {"???", IMP, CAT_UNKNOWN}, {"???", IMP, CAT_UNKNOWN},
    {"STZ", ZP, CAT_LOAD},    {"ADC", ZP, CAT_MATH},    {"ROR", ZP, CAT_MATH},     {"RMB6", ZP, CAT_MATH},
    {"PLA", IMP, CAT_STACK},  {"ADC", IMM, CAT_MATH},   {"ROR", ACC, CAT_MATH},    {"???", IMP, CAT_UNKNOWN},
    {"JMP", IND, CAT_BRANCH}, {"ADC", ABS, CAT_MATH},   {"ROR", ABS, CAT_MATH},    {"BBR6", ZPR, CAT_BRANCH},
    // 0x70-0x7F
    {"BVS", REL, CAT_BRANCH}, {"ADC", IZY, CAT_MATH},   {"ADC", ZPI, CAT_MATH},    {"???", IMP, CAT_UNKNOWN},
    {"STZ", ZPX, CAT_LOAD},   {"ADC", ZPX, CAT_MATH},   {"ROR", ZPX, CAT_MATH},    {"RMB7", ZP, CAT_MATH},
    {"SEI", IMP, CAT_FLAG},   {"ADC", ABY, CAT_MATH},   {"PLY", IMP, CAT_STACK},   {"???", IMP, CAT_UNKNOWN},
    {"JMP", AIX, CAT_BRANCH}, {"ADC", ABX, CAT_MATH},   {"ROR", ABX, CAT_MATH},    {"BBR7", ZPR, CAT_BRANCH},
    // 0x80-0x8F
    {"BRA", REL, CAT_BRANCH}, {"STA", IZX, CAT_LOAD},   {"???", IMP, CAT_UNKNOWN}, {"???", IMP, CAT_UNKNOWN},
    {"STY", ZP, CAT_LOAD},    {"STA", ZP, CAT_LOAD},    {"STX", ZP, CAT_LOAD},     {"SMB0", ZP, CAT_MATH},
    {"DEY", IMP, CAT_STACK},  {"BIT", IMM, CAT_MATH},   {"TXA", IMP, CAT_STACK},   {"???", IMP, CAT_UNKNOWN},
    {"STY", ABS, CAT_LOAD},   {"STA", ABS, CAT_LOAD},   {"STX", ABS, CAT_LOAD},    {"BBS0", ZPR, CAT_BRANCH},
    // 0x90-0x9F
    {"BCC", REL, CAT_BRANCH}, {"STA", IZY, CAT_LOAD},   {"STA", ZPI, CAT_LOAD},    {"???", IMP, CAT_UNKNOWN},
    {"STY", ZPX, CAT_LOAD},   {"STA", ZPX, CAT_LOAD},   {"STX", ZPY, CAT_LOAD},    {"SMB1", ZP, CAT_MATH},
    {"TYA", IMP, CAT_STACK},  {"STA", ABY, CAT_LOAD},   {"TXS", IMP, CAT_STACK},   {"???", IMP, CAT_UNKNOWN},
    {"STZ", ABS, CAT_LOAD},   {"STA", ABX, CAT_LOAD},   {"STZ", ABX, CAT_LOAD},    {"BBS1", ZPR, CAT_BRANCH},
    // 0xA0-0xAF
    {"LDY", IMM, CAT_LOAD},   {"LDA", IZX, CAT_LOAD},   {"LDX", IMM, CAT_LOAD},    {"???", IMP, CAT_UNKNOWN},
    {"LDY", ZP, CAT_LOAD},    {"LDA", ZP, CAT_LOAD},    {"LDX", ZP, CAT_LOAD},     {"SMB2", ZP, CAT_MATH},
    {"TAY", IMP, CAT_STACK},  {"LDA", IMM, CAT_LOAD},   {"TAX", IMP, CAT_STACK},   {"???", IMP, CAT_UNKNOWN},
    {"LDY", ABS, CAT_LOAD},   {"LDA", ABS, CAT_LOAD},   {"LDX", ABS, CAT_LOAD},    {"BBS2", ZPR, CAT_BRANCH},
    // 0xB0-0xBF
    {"BCS", REL, CAT_BRANCH}, {"LDA", IZY, CAT_LOAD},   {"LDA", ZPI, CAT_LOAD},    {"???", IMP, CAT_UNKNOWN},
    {"LDY", ZPX, CAT_LOAD},   {"LDA", ZPX, CAT_LOAD},   {"LDX", ZPY, CAT_LOAD},    {"SMB3", ZP, CAT_MATH},
    {"CLV", IMP, CAT_FLAG},   {"LDA", ABY, CAT_LOAD},   {"TSX", IMP, CAT_STACK},   {"???", IMP, CAT_UNKNOWN},
    {"LDY", ABX, CAT_LOAD},   {"LDA", ABX, CAT_LOAD},   {"LDX", ABY, CAT_LOAD},    {"BBS3", ZPR, CAT_BRANCH},
    // 0xC0-0xCF
    {"CPY", IMM, CAT_MATH},   {"CMP", IZX, CAT_MATH},   {"???", IMP, CAT_UNKNOWN}, {"???", IMP, CAT_UNKNOWN},
    {"CPY", ZP, CAT_MATH},    {"CMP", ZP, CAT_MATH},    {"DEC", ZP, CAT_MATH},     {"SMB4", ZP, CAT_MATH},
    {"INY", IMP, CAT_STACK},  {"CMP", IMM, CAT_MATH},   {"DEX", IMP, CAT_STACK},   {"WAI", IMP, CAT_STACK},
    {"CPY", ABS, CAT_MATH},   {"CMP", ABS, CAT_MATH},   {"DEC", ABS, CAT_MATH},    {"BBS4", ZPR, CAT_BRANCH},
    // 0xD0-0xDF
    {"BNE", REL, CAT_BRANCH}, {"CMP", IZY, CAT_MATH},   {"CMP", ZPI, CAT_MATH},    {"???", IMP, CAT_UNKNOWN},
    {"???", IMP, CAT_UNKNOWN},{"CMP", ZPX, CAT_MATH},   {"DEC", ZPX, CAT_MATH},    {"SMB5", ZP, CAT_MATH},
    {"CLD", IMP, CAT_FLAG},   {"CMP", ABY, CAT_MATH},   {"PHX", IMP, CAT_STACK},   {"STP", IMP, CAT_STACK},
    {"???", IMP, CAT_UNKNOWN},{"CMP", ABX, CAT_MATH},   {"DEC", ABX, CAT_MATH},    {"BBS5", ZPR, CAT_BRANCH},
    // 0xE0-0xEF
    {"CPX", IMM, CAT_MATH},   {"SBC", IZX, CAT_MATH},   {"???", IMP, CAT_UNKNOWN}, {"???", IMP, CAT_UNKNOWN},
    {"CPX", ZP, CAT_MATH},    {"SBC", ZP, CAT_MATH},    {"INC", ZP, CAT_MATH},     {"SMB6", ZP, CAT_MATH},
    {"INX", IMP, CAT_STACK},  {"SBC", IMM, CAT_MATH},   {"NOP", IMP, CAT_FLAG},    {"???", IMP, CAT_UNKNOWN},
    {"CPX", ABS, CAT_MATH},   {"SBC", ABS, CAT_MATH},   {"INC", ABS, CAT_MATH},    {"BBS6", ZPR, CAT_BRANCH},
    // 0xF0-0xFF
    {"BEQ", REL, CAT_BRANCH}, {"SBC", IZY, CAT_MATH},   {"SBC", ZPI, CAT_MATH},    {"???", IMP, CAT_UNKNOWN},
    {"???", IMP, CAT_UNKNOWN},{"SBC", ZPX, CAT_MATH},   {"INC", ZPX, CAT_MATH},    {"SMB7", ZP, CAT_MATH},
    {"SED", IMP, CAT_FLAG},   {"SBC", ABY, CAT_MATH},   {"PLX", IMP, CAT_STACK},   {"???", IMP, CAT_UNKNOWN},
    {"???", IMP, CAT_UNKNOWN},{"SBC", ABX, CAT_MATH},   {"INC", ABX, CAT_MATH},    {"BBS7", ZPR, CAT_BRANCH}
};

// Get CSS class for instruction category
static const char *getCategoryClass(InstrCategory cat) {
  switch (cat) {
    case CAT_BRANCH:  return "dis-branch";
    case CAT_LOAD:    return "dis-load";
    case CAT_MATH:    return "dis-math";
    case CAT_STACK:   return "dis-stack";
    case CAT_FLAG:    return "dis-flag";
    case CAT_UNKNOWN: return "dis-unknown";
    default:          return "dis-mnemonic";
  }
}

int getInstructionLength(uint8_t opcode) {
  switch (opcodes[opcode].mode) {
    case IMP:
    case ACC:
      return 1;
    case IMM:
    case ZP:
    case ZPX:
    case ZPY:
    case IZX:
    case IZY:
    case ZPI:
    case REL:
      return 2;
    case ABS:
    case ABX:
    case ABY:
    case IND:
    case AIX:
    case ZPR:
      return 3;
  }
  return 1;
}

std::string disassembleInstruction(const uint8_t *data, size_t size,
                                   uint16_t baseAddress, bool html) {
  if (size == 0 || data == nullptr) {
    return "";
  }

  std::ostringstream ss;
  ss << std::hex << std::uppercase << std::setfill('0');

  uint8_t opcode = data[0];
  const OpcodeInfo &info = opcodes[opcode];
  int instrLen = getInstructionLength(opcode);

  // Read operand bytes (if available)
  uint8_t lo = (size > 1 && instrLen >= 2) ? data[1] : 0;
  uint8_t hi = (size > 2 && instrLen >= 3) ? data[2] : 0;

  // Format address
  if (html) {
    ss << "<span class=\"dis-addr\">" << std::setw(4) << baseAddress << ":</span> ";
  } else {
    ss << std::setw(4) << baseAddress << ": ";
  }

  // Format instruction bytes
  if (html) {
    ss << "<span class=\"dis-bytes\">";
  }
  ss << std::setw(2) << static_cast<int>(opcode);
  if (instrLen >= 2 && size > 1) {
    ss << " " << std::setw(2) << static_cast<int>(lo);
  } else {
    ss << "   ";
  }
  if (instrLen >= 3 && size > 2) {
    ss << " " << std::setw(2) << static_cast<int>(hi);
  } else {
    ss << "   ";
  }
  if (html) {
    ss << "</span>";
  }

  // Mnemonic with category coloring
  if (html) {
    ss << "  <span class=\"" << getCategoryClass(info.category) << "\">"
       << info.mnemonic << "</span>";
  } else {
    ss << "  " << info.mnemonic;
  }

  // Format operand based on addressing mode
  switch (info.mode) {
    case IMP:
      break;
    case ACC:
      if (html) {
        ss << " <span class=\"dis-register\">A</span>";
      } else {
        ss << " A";
      }
      break;
    case IMM:
      if (html) {
        ss << " <span class=\"dis-punct\">#$</span><span class=\"dis-immediate\">"
           << std::setw(2) << static_cast<int>(lo) << "</span>";
      } else {
        ss << " #$" << std::setw(2) << static_cast<int>(lo);
      }
      break;
    case ZP:
      if (html) {
        ss << " <span class=\"dis-punct\">$</span><span class=\"dis-address\">"
           << std::setw(2) << static_cast<int>(lo) << "</span>";
      } else {
        ss << " $" << std::setw(2) << static_cast<int>(lo);
      }
      break;
    case ZPX:
      if (html) {
        ss << " <span class=\"dis-punct\">$</span><span class=\"dis-address\">"
           << std::setw(2) << static_cast<int>(lo)
           << "</span><span class=\"dis-punct\">,</span><span class=\"dis-register\">X</span>";
      } else {
        ss << " $" << std::setw(2) << static_cast<int>(lo) << ",X";
      }
      break;
    case ZPY:
      if (html) {
        ss << " <span class=\"dis-punct\">$</span><span class=\"dis-address\">"
           << std::setw(2) << static_cast<int>(lo)
           << "</span><span class=\"dis-punct\">,</span><span class=\"dis-register\">Y</span>";
      } else {
        ss << " $" << std::setw(2) << static_cast<int>(lo) << ",Y";
      }
      break;
    case ABS:
      if (html) {
        ss << " <span class=\"dis-punct\">$</span><span class=\"dis-address\">"
           << std::setw(4) << static_cast<int>((hi << 8) | lo) << "</span>";
      } else {
        ss << " $" << std::setw(4) << static_cast<int>((hi << 8) | lo);
      }
      break;
    case ABX:
      if (html) {
        ss << " <span class=\"dis-punct\">$</span><span class=\"dis-address\">"
           << std::setw(4) << static_cast<int>((hi << 8) | lo)
           << "</span><span class=\"dis-punct\">,</span><span class=\"dis-register\">X</span>";
      } else {
        ss << " $" << std::setw(4) << static_cast<int>((hi << 8) | lo) << ",X";
      }
      break;
    case ABY:
      if (html) {
        ss << " <span class=\"dis-punct\">$</span><span class=\"dis-address\">"
           << std::setw(4) << static_cast<int>((hi << 8) | lo)
           << "</span><span class=\"dis-punct\">,</span><span class=\"dis-register\">Y</span>";
      } else {
        ss << " $" << std::setw(4) << static_cast<int>((hi << 8) | lo) << ",Y";
      }
      break;
    case IND:
      if (html) {
        ss << " <span class=\"dis-punct\">($</span><span class=\"dis-address\">"
           << std::setw(4) << static_cast<int>((hi << 8) | lo)
           << "</span><span class=\"dis-punct\">)</span>";
      } else {
        ss << " ($" << std::setw(4) << static_cast<int>((hi << 8) | lo) << ")";
      }
      break;
    case IZX:
      if (html) {
        ss << " <span class=\"dis-punct\">($</span><span class=\"dis-address\">"
           << std::setw(2) << static_cast<int>(lo)
           << "</span><span class=\"dis-punct\">,</span><span class=\"dis-register\">X</span>"
           << "<span class=\"dis-punct\">)</span>";
      } else {
        ss << " ($" << std::setw(2) << static_cast<int>(lo) << ",X)";
      }
      break;
    case IZY:
      if (html) {
        ss << " <span class=\"dis-punct\">($</span><span class=\"dis-address\">"
           << std::setw(2) << static_cast<int>(lo)
           << "</span><span class=\"dis-punct\">),</span><span class=\"dis-register\">Y</span>";
      } else {
        ss << " ($" << std::setw(2) << static_cast<int>(lo) << "),Y";
      }
      break;
    case ZPI:
      if (html) {
        ss << " <span class=\"dis-punct\">($</span><span class=\"dis-address\">"
           << std::setw(2) << static_cast<int>(lo)
           << "</span><span class=\"dis-punct\">)</span>";
      } else {
        ss << " ($" << std::setw(2) << static_cast<int>(lo) << ")";
      }
      break;
    case AIX:
      if (html) {
        ss << " <span class=\"dis-punct\">($</span><span class=\"dis-address\">"
           << std::setw(4) << static_cast<int>((hi << 8) | lo)
           << "</span><span class=\"dis-punct\">,</span><span class=\"dis-register\">X</span>"
           << "<span class=\"dis-punct\">)</span>";
      } else {
        ss << " ($" << std::setw(4) << static_cast<int>((hi << 8) | lo) << ",X)";
      }
      break;
    case REL: {
      // Calculate branch target
      int8_t offset = static_cast<int8_t>(lo);
      uint16_t target = baseAddress + 2 + offset;
      if (html) {
        ss << " <span class=\"dis-punct\">$</span><span class=\"dis-target\">"
           << std::setw(4) << static_cast<int>(target) << "</span>";
      } else {
        ss << " $" << std::setw(4) << static_cast<int>(target);
      }
      break;
    }
    case ZPR: {
      // BBR/BBS: zero page address and relative branch
      int8_t offset = static_cast<int8_t>(hi);
      uint16_t target = baseAddress + 3 + offset;
      if (html) {
        ss << " <span class=\"dis-punct\">$</span><span class=\"dis-address\">"
           << std::setw(2) << static_cast<int>(lo)
           << "</span><span class=\"dis-punct\">,$</span><span class=\"dis-target\">"
           << std::setw(4) << static_cast<int>(target) << "</span>";
      } else {
        ss << " $" << std::setw(2) << static_cast<int>(lo)
           << ",$" << std::setw(4) << static_cast<int>(target);
      }
      break;
    }
  }

  return ss.str();
}

std::string disassembleBlock(const uint8_t *data, size_t size,
                             uint16_t baseAddress, bool html) {
  if (size == 0 || data == nullptr) {
    return "";
  }

  std::ostringstream result;
  size_t offset = 0;

  while (offset < size) {
    uint8_t opcode = data[offset];
    int instrLen = getInstructionLength(opcode);

    // Disassemble this instruction
    result << disassembleInstruction(data + offset, size - offset,
                                     static_cast<uint16_t>(baseAddress + offset),
                                     html);
    result << "\n";

    offset += instrLen;
  }

  return result.str();
}

} // namespace a2e
