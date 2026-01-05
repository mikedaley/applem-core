#include "cpu6502.hpp"
#include <iomanip>
#include <sstream>

namespace a2e {

CPU6502::CPU6502(ReadCallback read, WriteCallback write, CPUVariant variant)
    : read_(std::move(read)), write_(std::move(write)), variant_(variant) {}

void CPU6502::reset() {
  a_ = 0;
  x_ = 0;
  y_ = 0;
  sp_ = 0xFD;
  p_ = 0x24; // I flag set, U always set

  // Read reset vector
  uint16_t lo = read_(0xFFFC);
  uint16_t hi = read_(0xFFFD);
  pc_ = lo | (hi << 8);

  cycleCount_ = 0;
  totalCycles_ += 7; // Reset takes 7 cycles

  irqPending_ = false;
  nmiPending_ = false;
  nmiEdge_ = false;
}

void CPU6502::executeInstruction() {
  // Handle pending interrupts
  if (nmiPending_) {
    nmiPending_ = false;
    pushWord(pc_);
    push((p_ & ~FLAG_B) | FLAG_U);
    setFlag(FLAG_I, true);
    pc_ = read_(0xFFFA) | (read_(0xFFFB) << 8);
    totalCycles_ += 7;
    return;
  }

  if (irqPending_ && !getFlag(FLAG_I)) {
    irqPending_ = false;
    pushWord(pc_);
    push((p_ & ~FLAG_B) | FLAG_U);
    setFlag(FLAG_I, true);
    pc_ = read_(0xFFFE) | (read_(0xFFFF) << 8);
    totalCycles_ += 7;
    return;
  }

  // Fetch and execute opcode
  pageCrossed_ = false;
  uint8_t opcode = fetch();
  cycleCount_ = CYCLE_TABLE[opcode];

  executeOpcode(opcode);

  if (pageCrossed_) {
    // Some instructions take an extra cycle on page crossing
    // This is handled per-instruction in executeOpcode
  }

  totalCycles_ += cycleCount_;
  cycleCount_ = 0;
}

void CPU6502::step() {
  // For cycle-accurate emulation, we execute one instruction at a time
  // and track the cycles. A more sophisticated implementation would
  // break down each instruction into individual cycle steps.
  if (cycleCount_ == 0) {
    executeInstruction();
  } else {
    cycleCount_--;
  }
}

void CPU6502::irq() { irqPending_ = true; }

void CPU6502::nmi() {
  // NMI is edge-triggered
  if (!nmiEdge_) {
    nmiPending_ = true;
    nmiEdge_ = true;
  }
}

uint8_t CPU6502::read(uint16_t address) { return read_(address); }

void CPU6502::write(uint16_t address, uint8_t value) { write_(address, value); }

uint8_t CPU6502::fetch() { return read(pc_++); }

uint16_t CPU6502::fetchWord() {
  uint8_t lo = fetch();
  uint8_t hi = fetch();
  return lo | (hi << 8);
}

void CPU6502::push(uint8_t value) {
  write(0x0100 | sp_, value);
  sp_--;
}

uint8_t CPU6502::pop() {
  sp_++;
  return read(0x0100 | sp_);
}

void CPU6502::pushWord(uint16_t value) {
  push(value >> 8);
  push(value & 0xFF);
}

uint16_t CPU6502::popWord() {
  uint8_t lo = pop();
  uint8_t hi = pop();
  return lo | (hi << 8);
}

void CPU6502::updateNZ(uint8_t value) {
  setFlag(FLAG_Z, value == 0);
  setFlag(FLAG_N, (value & 0x80) != 0);
}

void CPU6502::compare(uint8_t reg, uint8_t value) {
  uint16_t result = reg - value;
  setFlag(FLAG_C, reg >= value);
  setFlag(FLAG_Z, reg == value);
  setFlag(FLAG_N, (result & 0x80) != 0);
}

// Addressing modes
uint16_t CPU6502::addrImmediate() { return pc_++; }

uint16_t CPU6502::addrZeroPage() { return fetch(); }

uint16_t CPU6502::addrZeroPageX() { return (fetch() + x_) & 0xFF; }

uint16_t CPU6502::addrZeroPageY() { return (fetch() + y_) & 0xFF; }

uint16_t CPU6502::addrAbsolute() { return fetchWord(); }

uint16_t CPU6502::addrAbsoluteX(bool checkPage) {
  uint16_t base = fetchWord();
  uint16_t addr = base + x_;
  if (checkPage && ((base & 0xFF00) != (addr & 0xFF00))) {
    pageCrossed_ = true;
    cycleCount_++;
  }
  return addr;
}

uint16_t CPU6502::addrAbsoluteY(bool checkPage) {
  uint16_t base = fetchWord();
  uint16_t addr = base + y_;
  if (checkPage && ((base & 0xFF00) != (addr & 0xFF00))) {
    pageCrossed_ = true;
    cycleCount_++;
  }
  return addr;
}

uint16_t CPU6502::addrIndirect() {
  uint16_t ptr = fetchWord();
  // 6502 bug: if low byte is 0xFF, high byte comes from same page
  if (variant_ == CPUVariant::NMOS_6502 && (ptr & 0xFF) == 0xFF) {
    return read(ptr) | (read(ptr & 0xFF00) << 8);
  }
  return read(ptr) | (read(ptr + 1) << 8);
}

uint16_t CPU6502::addrIndexedIndirect() {
  uint8_t zp = (fetch() + x_) & 0xFF;
  return read(zp) | (read((zp + 1) & 0xFF) << 8);
}

uint16_t CPU6502::addrIndirectIndexed(bool checkPage) {
  uint8_t zp = fetch();
  uint16_t base = read(zp) | (read((zp + 1) & 0xFF) << 8);
  uint16_t addr = base + y_;
  if (checkPage && ((base & 0xFF00) != (addr & 0xFF00))) {
    pageCrossed_ = true;
    cycleCount_++;
  }
  return addr;
}

uint16_t CPU6502::addrIndirectZP() {
  uint8_t zp = fetch();
  return read(zp) | (read((zp + 1) & 0xFF) << 8);
}

// ALU operations
void CPU6502::opADC(uint8_t value) {
  if (getFlag(FLAG_D)) {
    // Decimal mode
    uint16_t lo = (a_ & 0x0F) + (value & 0x0F) + (getFlag(FLAG_C) ? 1 : 0);
    uint16_t hi = (a_ & 0xF0) + (value & 0xF0);

    if (lo > 9) {
      lo += 6;
      hi += 0x10;
    }

    setFlag(FLAG_V, ~(a_ ^ value) & (a_ ^ hi) & 0x80);

    if (hi > 0x90) {
      hi += 0x60;
    }

    setFlag(FLAG_C, hi > 0xFF);
    a_ = (lo & 0x0F) | (hi & 0xF0);
    updateNZ(a_);
  } else {
    uint16_t result = a_ + value + (getFlag(FLAG_C) ? 1 : 0);
    setFlag(FLAG_C, result > 0xFF);
    setFlag(FLAG_V, ~(a_ ^ value) & (a_ ^ result) & 0x80);
    a_ = result & 0xFF;
    updateNZ(a_);
  }
}

void CPU6502::opSBC(uint8_t value) {
  if (getFlag(FLAG_D)) {
    // Decimal mode
    uint16_t lo = (a_ & 0x0F) - (value & 0x0F) - (getFlag(FLAG_C) ? 0 : 1);
    uint16_t hi = (a_ & 0xF0) - (value & 0xF0);

    if (lo & 0x10) {
      lo -= 6;
      hi -= 0x10;
    }

    if (hi & 0x0100) {
      hi -= 0x60;
    }

    uint16_t result = a_ - value - (getFlag(FLAG_C) ? 0 : 1);
    setFlag(FLAG_C, result < 0x100);
    setFlag(FLAG_V, (a_ ^ value) & (a_ ^ result) & 0x80);
    a_ = (lo & 0x0F) | (hi & 0xF0);
    updateNZ(a_);
  } else {
    uint16_t result = a_ - value - (getFlag(FLAG_C) ? 0 : 1);
    setFlag(FLAG_C, result < 0x100);
    setFlag(FLAG_V, (a_ ^ value) & (a_ ^ result) & 0x80);
    a_ = result & 0xFF;
    updateNZ(a_);
  }
}

void CPU6502::opAND(uint8_t value) {
  a_ &= value;
  updateNZ(a_);
}

void CPU6502::opORA(uint8_t value) {
  a_ |= value;
  updateNZ(a_);
}

void CPU6502::opEOR(uint8_t value) {
  a_ ^= value;
  updateNZ(a_);
}

void CPU6502::opCMP(uint8_t value) { compare(a_, value); }

void CPU6502::opCPX(uint8_t value) { compare(x_, value); }

void CPU6502::opCPY(uint8_t value) { compare(y_, value); }

void CPU6502::opBIT(uint8_t value) {
  setFlag(FLAG_Z, (a_ & value) == 0);
  setFlag(FLAG_N, (value & 0x80) != 0);
  setFlag(FLAG_V, (value & 0x40) != 0);
}

void CPU6502::opASL_A() {
  setFlag(FLAG_C, (a_ & 0x80) != 0);
  a_ <<= 1;
  updateNZ(a_);
}

uint8_t CPU6502::opASL(uint8_t value) {
  setFlag(FLAG_C, (value & 0x80) != 0);
  value <<= 1;
  updateNZ(value);
  return value;
}

void CPU6502::opLSR_A() {
  setFlag(FLAG_C, (a_ & 0x01) != 0);
  a_ >>= 1;
  updateNZ(a_);
}

uint8_t CPU6502::opLSR(uint8_t value) {
  setFlag(FLAG_C, (value & 0x01) != 0);
  value >>= 1;
  updateNZ(value);
  return value;
}

void CPU6502::opROL_A() {
  uint8_t carry = getFlag(FLAG_C) ? 1 : 0;
  setFlag(FLAG_C, (a_ & 0x80) != 0);
  a_ = (a_ << 1) | carry;
  updateNZ(a_);
}

uint8_t CPU6502::opROL(uint8_t value) {
  uint8_t carry = getFlag(FLAG_C) ? 1 : 0;
  setFlag(FLAG_C, (value & 0x80) != 0);
  value = (value << 1) | carry;
  updateNZ(value);
  return value;
}

void CPU6502::opROR_A() {
  uint8_t carry = getFlag(FLAG_C) ? 0x80 : 0;
  setFlag(FLAG_C, (a_ & 0x01) != 0);
  a_ = (a_ >> 1) | carry;
  updateNZ(a_);
}

uint8_t CPU6502::opROR(uint8_t value) {
  uint8_t carry = getFlag(FLAG_C) ? 0x80 : 0;
  setFlag(FLAG_C, (value & 0x01) != 0);
  value = (value >> 1) | carry;
  updateNZ(value);
  return value;
}

uint8_t CPU6502::opINC(uint8_t value) {
  value++;
  updateNZ(value);
  return value;
}

uint8_t CPU6502::opDEC(uint8_t value) {
  value--;
  updateNZ(value);
  return value;
}

void CPU6502::branch(bool condition) {
  int8_t offset = static_cast<int8_t>(fetch());
  if (condition) {
    uint16_t oldPC = pc_;
    pc_ += offset;
    cycleCount_++;
    if ((oldPC & 0xFF00) != (pc_ & 0xFF00)) {
      cycleCount_++; // Page crossing penalty
    }
  }
}

void CPU6502::executeOpcode(uint8_t opcode) {
  uint16_t addr;
  uint8_t value;

  switch (opcode) {
  // LDA
  case 0xA9:
    opAND(read(addrImmediate()));
    a_ = read(pc_ - 1);
    updateNZ(a_);
    break;
  case 0xA5:
    a_ = read(addrZeroPage());
    updateNZ(a_);
    break;
  case 0xB5:
    a_ = read(addrZeroPageX());
    updateNZ(a_);
    break;
  case 0xAD:
    a_ = read(addrAbsolute());
    updateNZ(a_);
    break;
  case 0xBD:
    a_ = read(addrAbsoluteX());
    updateNZ(a_);
    break;
  case 0xB9:
    a_ = read(addrAbsoluteY());
    updateNZ(a_);
    break;
  case 0xA1:
    a_ = read(addrIndexedIndirect());
    updateNZ(a_);
    break;
  case 0xB1:
    a_ = read(addrIndirectIndexed());
    updateNZ(a_);
    break;

  // LDX
  case 0xA2:
    x_ = read(addrImmediate());
    updateNZ(x_);
    break;
  case 0xA6:
    x_ = read(addrZeroPage());
    updateNZ(x_);
    break;
  case 0xB6:
    x_ = read(addrZeroPageY());
    updateNZ(x_);
    break;
  case 0xAE:
    x_ = read(addrAbsolute());
    updateNZ(x_);
    break;
  case 0xBE:
    x_ = read(addrAbsoluteY());
    updateNZ(x_);
    break;

  // LDY
  case 0xA0:
    y_ = read(addrImmediate());
    updateNZ(y_);
    break;
  case 0xA4:
    y_ = read(addrZeroPage());
    updateNZ(y_);
    break;
  case 0xB4:
    y_ = read(addrZeroPageX());
    updateNZ(y_);
    break;
  case 0xAC:
    y_ = read(addrAbsolute());
    updateNZ(y_);
    break;
  case 0xBC:
    y_ = read(addrAbsoluteX());
    updateNZ(y_);
    break;

  // STA
  case 0x85:
    write(addrZeroPage(), a_);
    break;
  case 0x95:
    write(addrZeroPageX(), a_);
    break;
  case 0x8D:
    write(addrAbsolute(), a_);
    break;
  case 0x9D:
    write(addrAbsoluteX(false), a_);
    break;
  case 0x99:
    write(addrAbsoluteY(false), a_);
    break;
  case 0x81:
    write(addrIndexedIndirect(), a_);
    break;
  case 0x91:
    write(addrIndirectIndexed(false), a_);
    break;

  // STX
  case 0x86:
    write(addrZeroPage(), x_);
    break;
  case 0x96:
    write(addrZeroPageY(), x_);
    break;
  case 0x8E:
    write(addrAbsolute(), x_);
    break;

  // STY
  case 0x84:
    write(addrZeroPage(), y_);
    break;
  case 0x94:
    write(addrZeroPageX(), y_);
    break;
  case 0x8C:
    write(addrAbsolute(), y_);
    break;

  // STZ (65C02)
  case 0x64:
    if (variant_ == CPUVariant::CMOS_65C02)
      write(addrZeroPage(), 0);
    break;
  case 0x74:
    if (variant_ == CPUVariant::CMOS_65C02)
      write(addrZeroPageX(), 0);
    break;
  case 0x9C:
    if (variant_ == CPUVariant::CMOS_65C02)
      write(addrAbsolute(), 0);
    break;
  case 0x9E:
    if (variant_ == CPUVariant::CMOS_65C02)
      write(addrAbsoluteX(false), 0);
    break;

  // Transfer
  case 0xAA:
    x_ = a_;
    updateNZ(x_);
    break; // TAX
  case 0xA8:
    y_ = a_;
    updateNZ(y_);
    break; // TAY
  case 0xBA:
    x_ = sp_;
    updateNZ(x_);
    break; // TSX
  case 0x8A:
    a_ = x_;
    updateNZ(a_);
    break; // TXA
  case 0x9A:
    sp_ = x_;
    break; // TXS
  case 0x98:
    a_ = y_;
    updateNZ(a_);
    break; // TYA

  // Stack
  case 0x48:
    push(a_);
    break; // PHA
  case 0x68:
    a_ = pop();
    updateNZ(a_);
    break; // PLA
  case 0x08:
    push(p_ | FLAG_B | FLAG_U);
    break; // PHP
  case 0x28:
    p_ = (pop() & ~FLAG_B) | FLAG_U;
    break; // PLP
  case 0xDA:
    if (variant_ == CPUVariant::CMOS_65C02)
      push(x_);
    break; // PHX (65C02)
  case 0xFA:
    if (variant_ == CPUVariant::CMOS_65C02) {
      x_ = pop();
      updateNZ(x_);
    }
    break; // PLX (65C02)
  case 0x5A:
    if (variant_ == CPUVariant::CMOS_65C02)
      push(y_);
    break; // PHY (65C02)
  case 0x7A:
    if (variant_ == CPUVariant::CMOS_65C02) {
      y_ = pop();
      updateNZ(y_);
    }
    break; // PLY (65C02)

  // ADC
  case 0x69:
    opADC(read(addrImmediate()));
    break;
  case 0x65:
    opADC(read(addrZeroPage()));
    break;
  case 0x75:
    opADC(read(addrZeroPageX()));
    break;
  case 0x6D:
    opADC(read(addrAbsolute()));
    break;
  case 0x7D:
    opADC(read(addrAbsoluteX()));
    break;
  case 0x79:
    opADC(read(addrAbsoluteY()));
    break;
  case 0x61:
    opADC(read(addrIndexedIndirect()));
    break;
  case 0x71:
    opADC(read(addrIndirectIndexed()));
    break;
  case 0x72:
    if (variant_ == CPUVariant::CMOS_65C02)
      opADC(read(addrIndirectZP()));
    break; // (65C02)

  // SBC
  case 0xE9:
    opSBC(read(addrImmediate()));
    break;
  case 0xE5:
    opSBC(read(addrZeroPage()));
    break;
  case 0xF5:
    opSBC(read(addrZeroPageX()));
    break;
  case 0xED:
    opSBC(read(addrAbsolute()));
    break;
  case 0xFD:
    opSBC(read(addrAbsoluteX()));
    break;
  case 0xF9:
    opSBC(read(addrAbsoluteY()));
    break;
  case 0xE1:
    opSBC(read(addrIndexedIndirect()));
    break;
  case 0xF1:
    opSBC(read(addrIndirectIndexed()));
    break;
  case 0xF2:
    if (variant_ == CPUVariant::CMOS_65C02)
      opSBC(read(addrIndirectZP()));
    break; // (65C02)

  // AND
  case 0x29:
    opAND(read(addrImmediate()));
    break;
  case 0x25:
    opAND(read(addrZeroPage()));
    break;
  case 0x35:
    opAND(read(addrZeroPageX()));
    break;
  case 0x2D:
    opAND(read(addrAbsolute()));
    break;
  case 0x3D:
    opAND(read(addrAbsoluteX()));
    break;
  case 0x39:
    opAND(read(addrAbsoluteY()));
    break;
  case 0x21:
    opAND(read(addrIndexedIndirect()));
    break;
  case 0x31:
    opAND(read(addrIndirectIndexed()));
    break;
  case 0x32:
    if (variant_ == CPUVariant::CMOS_65C02)
      opAND(read(addrIndirectZP()));
    break; // (65C02)

  // ORA
  case 0x09:
    opORA(read(addrImmediate()));
    break;
  case 0x05:
    opORA(read(addrZeroPage()));
    break;
  case 0x15:
    opORA(read(addrZeroPageX()));
    break;
  case 0x0D:
    opORA(read(addrAbsolute()));
    break;
  case 0x1D:
    opORA(read(addrAbsoluteX()));
    break;
  case 0x19:
    opORA(read(addrAbsoluteY()));
    break;
  case 0x01:
    opORA(read(addrIndexedIndirect()));
    break;
  case 0x11:
    opORA(read(addrIndirectIndexed()));
    break;
  case 0x12:
    if (variant_ == CPUVariant::CMOS_65C02)
      opORA(read(addrIndirectZP()));
    break; // (65C02)

  // EOR
  case 0x49:
    opEOR(read(addrImmediate()));
    break;
  case 0x45:
    opEOR(read(addrZeroPage()));
    break;
  case 0x55:
    opEOR(read(addrZeroPageX()));
    break;
  case 0x4D:
    opEOR(read(addrAbsolute()));
    break;
  case 0x5D:
    opEOR(read(addrAbsoluteX()));
    break;
  case 0x59:
    opEOR(read(addrAbsoluteY()));
    break;
  case 0x41:
    opEOR(read(addrIndexedIndirect()));
    break;
  case 0x51:
    opEOR(read(addrIndirectIndexed()));
    break;
  case 0x52:
    if (variant_ == CPUVariant::CMOS_65C02)
      opEOR(read(addrIndirectZP()));
    break; // (65C02)

  // CMP
  case 0xC9:
    opCMP(read(addrImmediate()));
    break;
  case 0xC5:
    opCMP(read(addrZeroPage()));
    break;
  case 0xD5:
    opCMP(read(addrZeroPageX()));
    break;
  case 0xCD:
    opCMP(read(addrAbsolute()));
    break;
  case 0xDD:
    opCMP(read(addrAbsoluteX()));
    break;
  case 0xD9:
    opCMP(read(addrAbsoluteY()));
    break;
  case 0xC1:
    opCMP(read(addrIndexedIndirect()));
    break;
  case 0xD1:
    opCMP(read(addrIndirectIndexed()));
    break;
  case 0xD2:
    if (variant_ == CPUVariant::CMOS_65C02)
      opCMP(read(addrIndirectZP()));
    break; // (65C02)

  // CPX
  case 0xE0:
    opCPX(read(addrImmediate()));
    break;
  case 0xE4:
    opCPX(read(addrZeroPage()));
    break;
  case 0xEC:
    opCPX(read(addrAbsolute()));
    break;

  // CPY
  case 0xC0:
    opCPY(read(addrImmediate()));
    break;
  case 0xC4:
    opCPY(read(addrZeroPage()));
    break;
  case 0xCC:
    opCPY(read(addrAbsolute()));
    break;

  // BIT
  case 0x24:
    opBIT(read(addrZeroPage()));
    break;
  case 0x2C:
    opBIT(read(addrAbsolute()));
    break;
  case 0x89:
    if (variant_ == CPUVariant::CMOS_65C02) { // BIT immediate (65C02)
      value = read(addrImmediate());
      setFlag(FLAG_Z, (a_ & value) == 0);
    }
    break;
  case 0x34:
    if (variant_ == CPUVariant::CMOS_65C02)
      opBIT(read(addrZeroPageX()));
    break;
  case 0x3C:
    if (variant_ == CPUVariant::CMOS_65C02)
      opBIT(read(addrAbsoluteX()));
    break;

  // ASL
  case 0x0A:
    opASL_A();
    break;
  case 0x06:
    addr = addrZeroPage();
    write(addr, opASL(read(addr)));
    break;
  case 0x16:
    addr = addrZeroPageX();
    write(addr, opASL(read(addr)));
    break;
  case 0x0E:
    addr = addrAbsolute();
    write(addr, opASL(read(addr)));
    break;
  case 0x1E:
    addr = addrAbsoluteX(false);
    write(addr, opASL(read(addr)));
    break;

  // LSR
  case 0x4A:
    opLSR_A();
    break;
  case 0x46:
    addr = addrZeroPage();
    write(addr, opLSR(read(addr)));
    break;
  case 0x56:
    addr = addrZeroPageX();
    write(addr, opLSR(read(addr)));
    break;
  case 0x4E:
    addr = addrAbsolute();
    write(addr, opLSR(read(addr)));
    break;
  case 0x5E:
    addr = addrAbsoluteX(false);
    write(addr, opLSR(read(addr)));
    break;

  // ROL
  case 0x2A:
    opROL_A();
    break;
  case 0x26:
    addr = addrZeroPage();
    write(addr, opROL(read(addr)));
    break;
  case 0x36:
    addr = addrZeroPageX();
    write(addr, opROL(read(addr)));
    break;
  case 0x2E:
    addr = addrAbsolute();
    write(addr, opROL(read(addr)));
    break;
  case 0x3E:
    addr = addrAbsoluteX(false);
    write(addr, opROL(read(addr)));
    break;

  // ROR
  case 0x6A:
    opROR_A();
    break;
  case 0x66:
    addr = addrZeroPage();
    write(addr, opROR(read(addr)));
    break;
  case 0x76:
    addr = addrZeroPageX();
    write(addr, opROR(read(addr)));
    break;
  case 0x6E:
    addr = addrAbsolute();
    write(addr, opROR(read(addr)));
    break;
  case 0x7E:
    addr = addrAbsoluteX(false);
    write(addr, opROR(read(addr)));
    break;

  // INC
  case 0xE6:
    addr = addrZeroPage();
    write(addr, opINC(read(addr)));
    break;
  case 0xF6:
    addr = addrZeroPageX();
    write(addr, opINC(read(addr)));
    break;
  case 0xEE:
    addr = addrAbsolute();
    write(addr, opINC(read(addr)));
    break;
  case 0xFE:
    addr = addrAbsoluteX(false);
    write(addr, opINC(read(addr)));
    break;
  case 0x1A:
    if (variant_ == CPUVariant::CMOS_65C02) {
      a_ = opINC(a_);
    }
    break; // INC A (65C02)

  // DEC
  case 0xC6:
    addr = addrZeroPage();
    write(addr, opDEC(read(addr)));
    break;
  case 0xD6:
    addr = addrZeroPageX();
    write(addr, opDEC(read(addr)));
    break;
  case 0xCE:
    addr = addrAbsolute();
    write(addr, opDEC(read(addr)));
    break;
  case 0xDE:
    addr = addrAbsoluteX(false);
    write(addr, opDEC(read(addr)));
    break;
  case 0x3A:
    if (variant_ == CPUVariant::CMOS_65C02) {
      a_ = opDEC(a_);
    }
    break; // DEC A (65C02)

  // INX/INY/DEX/DEY
  case 0xE8:
    x_++;
    updateNZ(x_);
    break; // INX
  case 0xC8:
    y_++;
    updateNZ(y_);
    break; // INY
  case 0xCA:
    x_--;
    updateNZ(x_);
    break; // DEX
  case 0x88:
    y_--;
    updateNZ(y_);
    break; // DEY

  // Branches
  case 0x10:
    branch(!getFlag(FLAG_N));
    break; // BPL
  case 0x30:
    branch(getFlag(FLAG_N));
    break; // BMI
  case 0x50:
    branch(!getFlag(FLAG_V));
    break; // BVC
  case 0x70:
    branch(getFlag(FLAG_V));
    break; // BVS
  case 0x90:
    branch(!getFlag(FLAG_C));
    break; // BCC
  case 0xB0:
    branch(getFlag(FLAG_C));
    break; // BCS
  case 0xD0:
    branch(!getFlag(FLAG_Z));
    break; // BNE
  case 0xF0:
    branch(getFlag(FLAG_Z));
    break; // BEQ
  case 0x80:
    if (variant_ == CPUVariant::CMOS_65C02)
      branch(true);
    break; // BRA (65C02)

  // JMP
  case 0x4C:
    pc_ = addrAbsolute();
    break;
  case 0x6C:
    pc_ = addrIndirect();
    break;
  case 0x7C:
    if (variant_ == CPUVariant::CMOS_65C02) { // JMP (abs,X) - 65C02
      uint16_t base = fetchWord();
      pc_ = read(base + x_) | (read(base + x_ + 1) << 8);
    }
    break;

  // JSR/RTS/RTI
  case 0x20:
    addr = fetchWord();
    pushWord(pc_ - 1);
    pc_ = addr;
    break;
  case 0x60:
    pc_ = popWord() + 1;
    break;
  case 0x40:
    p_ = (pop() & ~FLAG_B) | FLAG_U;
    pc_ = popWord();
    break;

  // Flag operations
  case 0x18:
    setFlag(FLAG_C, false);
    break; // CLC
  case 0x38:
    setFlag(FLAG_C, true);
    break; // SEC
  case 0x58:
    setFlag(FLAG_I, false);
    break; // CLI
  case 0x78:
    setFlag(FLAG_I, true);
    break; // SEI
  case 0xB8:
    setFlag(FLAG_V, false);
    break; // CLV
  case 0xD8:
    setFlag(FLAG_D, false);
    break; // CLD
  case 0xF8:
    setFlag(FLAG_D, true);
    break; // SED

  // BRK
  case 0x00:
    pc_++;
    pushWord(pc_);
    push(p_ | FLAG_B | FLAG_U);
    setFlag(FLAG_I, true);
    if (variant_ == CPUVariant::CMOS_65C02) {
      setFlag(FLAG_D, false); // 65C02 clears D on BRK
    }
    pc_ = read(0xFFFE) | (read(0xFFFF) << 8);
    break;

  // NOP
  case 0xEA:
    break;

  // 65C02 NOPs with operands
  case 0x02:
  case 0x22:
  case 0x42:
  case 0x62:
  case 0x82:
  case 0xC2:
  case 0xE2:
    if (variant_ == CPUVariant::CMOS_65C02)
      pc_++;
    break;
  case 0x44:
    if (variant_ == CPUVariant::CMOS_65C02)
      pc_++;
    break;
  case 0x54:
  case 0xD4:
  case 0xF4:
    if (variant_ == CPUVariant::CMOS_65C02)
      pc_++;
    break;
  case 0x5C:
    if (variant_ == CPUVariant::CMOS_65C02)
      pc_ += 2;
    break;
  case 0xDC:
  case 0xFC:
    if (variant_ == CPUVariant::CMOS_65C02)
      pc_ += 2;
    break;

  // TRB/TSB (65C02)
  case 0x14:
    if (variant_ == CPUVariant::CMOS_65C02) {
      addr = addrZeroPage();
      value = read(addr);
      setFlag(FLAG_Z, (a_ & value) == 0);
      write(addr, value & ~a_);
    }
    break;
  case 0x1C:
    if (variant_ == CPUVariant::CMOS_65C02) {
      addr = addrAbsolute();
      value = read(addr);
      setFlag(FLAG_Z, (a_ & value) == 0);
      write(addr, value & ~a_);
    }
    break;
  case 0x04:
    if (variant_ == CPUVariant::CMOS_65C02) {
      addr = addrZeroPage();
      value = read(addr);
      setFlag(FLAG_Z, (a_ & value) == 0);
      write(addr, value | a_);
    }
    break;
  case 0x0C:
    if (variant_ == CPUVariant::CMOS_65C02) {
      addr = addrAbsolute();
      value = read(addr);
      setFlag(FLAG_Z, (a_ & value) == 0);
      write(addr, value | a_);
    }
    break;

  // BBR/BBS (65C02 - Rockwell/WDC)
  case 0x0F:
  case 0x1F:
  case 0x2F:
  case 0x3F:
  case 0x4F:
  case 0x5F:
  case 0x6F:
  case 0x7F:
    if (variant_ == CPUVariant::CMOS_65C02) {
      addr = addrZeroPage();
      value = read(addr);
      int8_t offset = static_cast<int8_t>(fetch());
      int bit = (opcode >> 4) & 7;
      if (!(value & (1 << bit))) {
        pc_ += offset;
      }
    }
    break;
  case 0x8F:
  case 0x9F:
  case 0xAF:
  case 0xBF:
  case 0xCF:
  case 0xDF:
  case 0xEF:
  case 0xFF:
    if (variant_ == CPUVariant::CMOS_65C02) {
      addr = addrZeroPage();
      value = read(addr);
      int8_t offset = static_cast<int8_t>(fetch());
      int bit = (opcode >> 4) & 7;
      if (value & (1 << bit)) {
        pc_ += offset;
      }
    }
    break;

  // RMB/SMB (65C02 - Rockwell/WDC)
  case 0x07:
  case 0x17:
  case 0x27:
  case 0x37:
  case 0x47:
  case 0x57:
  case 0x67:
  case 0x77:
    if (variant_ == CPUVariant::CMOS_65C02) {
      addr = addrZeroPage();
      value = read(addr);
      int bit = (opcode >> 4) & 7;
      write(addr, value & ~(1 << bit));
    }
    break;
  case 0x87:
  case 0x97:
  case 0xA7:
  case 0xB7:
  case 0xC7:
  case 0xD7:
  case 0xE7:
  case 0xF7:
    if (variant_ == CPUVariant::CMOS_65C02) {
      addr = addrZeroPage();
      value = read(addr);
      int bit = (opcode >> 4) & 7;
      write(addr, value | (1 << bit));
    }
    break;

  // WAI/STP (65C02 - WDC)
  case 0xCB: // WAI - Wait for interrupt
    if (variant_ == CPUVariant::CMOS_65C02) {
      // Wait until interrupt - simplified: just continue
    }
    break;
  case 0xDB: // STP - Stop processor
    if (variant_ == CPUVariant::CMOS_65C02) {
      // Stop until reset - simplified: do nothing
    }
    break;

  default:
    // Unknown opcode - treat as NOP
    break;
  }
}

std::string CPU6502::disassembleAt(uint16_t address) const {
  std::ostringstream ss;
  ss << std::hex << std::uppercase << std::setfill('0');
  ss << std::setw(4) << address << ": ";

  uint8_t opcode = read_(address);
  ss << std::setw(2) << static_cast<int>(opcode) << " ";

  // This is a simplified disassembler - a full implementation would
  // decode all opcodes with proper mnemonics and operands
  static const char *mnemonics[256] = {
      "BRK", "ORA",  "???", "???",  "TSB", "ORA",  "ASL", "RMB0", "PHP", "ORA",
      "ASL", "???",  "TSB", "ORA",  "ASL", "BBR0", "BPL", "ORA",  "ORA", "???",
      "TRB", "ORA",  "ASL", "RMB1", "CLC", "ORA",  "INC", "???",  "TRB", "ORA",
      "ASL", "BBR1", "JSR", "AND",  "???", "???",  "BIT", "AND",  "ROL", "RMB2",
      "PLP", "AND",  "ROL", "???",  "BIT", "AND",  "ROL", "BBR2", "BMI", "AND",
      "AND", "???",  "BIT", "AND",  "ROL", "RMB3", "SEC", "AND",  "DEC", "???",
      "BIT", "AND",  "ROL", "BBR3", "RTI", "EOR",  "???", "???",  "???", "EOR",
      "LSR", "RMB4", "PHA", "EOR",  "LSR", "???",  "JMP", "EOR",  "LSR", "BBR4",
      "BVC", "EOR",  "EOR", "???",  "???", "EOR",  "LSR", "RMB5", "CLI", "EOR",
      "PHY", "???",  "???", "EOR",  "LSR", "BBR5", "RTS", "ADC",  "???", "???",
      "STZ", "ADC",  "ROR", "RMB6", "PLA", "ADC",  "ROR", "???",  "JMP", "ADC",
      "ROR", "BBR6", "BVS", "ADC",  "ADC", "???",  "STZ", "ADC",  "ROR", "RMB7",
      "SEI", "ADC",  "PLY", "???",  "JMP", "ADC",  "ROR", "BBR7", "BRA", "STA",
      "???", "???",  "STY", "STA",  "STX", "SMB0", "DEY", "BIT",  "TXA", "???",
      "STY", "STA",  "STX", "BBS0", "BCC", "STA",  "STA", "???",  "STY", "STA",
      "STX", "SMB1", "TYA", "STA",  "TXS", "???",  "STZ", "STA",  "STZ", "BBS1",
      "LDY", "LDA",  "LDX", "???",  "LDY", "LDA",  "LDX", "SMB2", "TAY", "LDA",
      "TAX", "???",  "LDY", "LDA",  "LDX", "BBS2", "BCS", "LDA",  "LDA", "???",
      "LDY", "LDA",  "LDX", "SMB3", "CLV", "LDA",  "TSX", "???",  "LDY", "LDA",
      "LDX", "BBS3", "CPY", "CMP",  "???", "???",  "CPY", "CMP",  "DEC", "SMB4",
      "INY", "CMP",  "DEX", "WAI",  "CPY", "CMP",  "DEC", "BBS4", "BNE", "CMP",
      "CMP", "???",  "???", "CMP",  "DEC", "SMB5", "CLD", "CMP",  "PHX", "STP",
      "???", "CMP",  "DEC", "BBS5", "CPX", "SBC",  "???", "???",  "CPX", "SBC",
      "INC", "SMB6", "INX", "SBC",  "NOP", "???",  "CPX", "SBC",  "INC", "BBS6",
      "BEQ", "SBC",  "SBC", "???",  "???", "SBC",  "INC", "SMB7", "SED", "SBC",
      "PLX", "???",  "???", "SBC",  "INC", "BBS7"};

  ss << mnemonics[opcode];
  return ss.str();
}

} // namespace a2e
