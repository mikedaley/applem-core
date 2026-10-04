/*
 * machine_host_debug.cpp - What a debugger asks of whichever machine is running
 *
 * The browser's bindings used to answer each of these with a branch per
 * machine of their own, which a second front end would have had to write
 * again. They are answered here once, and both front ends ask.
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "machine_host.hpp"

#include <algorithm>
#include <cstdio>
#include <initializer_list>

#include "../core/disassembler/disasm_align.hpp"
#include "../core/disassembler/disassembler65816.hpp"
#include "cpu/6502/cpu6502.hpp"
#include "cpu/65816/cpu65816.hpp"
#include "iigs/iigs_memory.hpp"
#include "iigs/iigs_video.hpp"
#include "video/ntsc.hpp"

namespace a2e::host {

namespace {

OperandMode modeOf(AddrMode mode) {
  switch (mode) {
  case AddrMode::IMP: return OperandMode::Implied;
  case AddrMode::ACC: return OperandMode::Accumulator;
  case AddrMode::IMM: return OperandMode::Immediate;
  case AddrMode::ZP: return OperandMode::Direct;
  case AddrMode::ZPX: return OperandMode::DirectX;
  case AddrMode::ZPY: return OperandMode::DirectY;
  case AddrMode::ABS: return OperandMode::Absolute;
  case AddrMode::ABX: return OperandMode::AbsoluteX;
  case AddrMode::ABY: return OperandMode::AbsoluteY;
  case AddrMode::IND: return OperandMode::AbsoluteIndirect;
  case AddrMode::IZX: return OperandMode::DirectIndexedIndirect;
  case AddrMode::IZY: return OperandMode::DirectIndirectIndexed;
  case AddrMode::REL: return OperandMode::Relative;
  case AddrMode::ZPI: return OperandMode::DirectIndirect;
  case AddrMode::AIX: return OperandMode::AbsoluteIndexedIndirect;
  case AddrMode::ZPR: return OperandMode::DirectRelative;
  }
  return OperandMode::Implied;
}

OperandMode modeOf(AddrMode816 mode) {
  switch (mode) {
  case AddrMode816::Implied: return OperandMode::Implied;
  case AddrMode816::Accumulator: return OperandMode::Accumulator;
  case AddrMode816::ImmediateByte:
  case AddrMode816::ImmediateA:
  case AddrMode816::ImmediateIndex: return OperandMode::Immediate;
  case AddrMode816::Direct: return OperandMode::Direct;
  case AddrMode816::DirectX: return OperandMode::DirectX;
  case AddrMode816::DirectY: return OperandMode::DirectY;
  case AddrMode816::DirectIndirect: return OperandMode::DirectIndirect;
  case AddrMode816::DirectIndexedIndirect: return OperandMode::DirectIndexedIndirect;
  case AddrMode816::DirectIndirectIndexed: return OperandMode::DirectIndirectIndexed;
  case AddrMode816::DirectIndirectLong: return OperandMode::DirectIndirectLong;
  case AddrMode816::DirectIndirectLongIndexed: return OperandMode::DirectIndirectLongIndexed;
  case AddrMode816::Absolute: return OperandMode::Absolute;
  case AddrMode816::AbsoluteX: return OperandMode::AbsoluteX;
  case AddrMode816::AbsoluteY: return OperandMode::AbsoluteY;
  case AddrMode816::AbsoluteLong: return OperandMode::AbsoluteLong;
  case AddrMode816::AbsoluteLongX: return OperandMode::AbsoluteLongX;
  case AddrMode816::AbsoluteIndirect: return OperandMode::AbsoluteIndirect;
  case AddrMode816::AbsoluteIndexedIndirect: return OperandMode::AbsoluteIndexedIndirect;
  case AddrMode816::AbsoluteIndirectLong: return OperandMode::AbsoluteIndirectLong;
  case AddrMode816::StackRelative: return OperandMode::StackRelative;
  case AddrMode816::StackRelativeIndirectIndexed: return OperandMode::StackRelativeIndirectIndexed;
  case AddrMode816::Relative: return OperandMode::Relative;
  case AddrMode816::RelativeLong: return OperandMode::RelativeLong;
  case AddrMode816::BlockMove: return OperandMode::BlockMove;
  }
  return OperandMode::Implied;
}

Instruction fromBytes6502(const uint8_t *bytes, uint16_t address) {
  const DisasmInstruction in = disassembleInstruction(bytes, 3, address);
  Instruction out;
  out.address = address;
  out.target = in.target;
  out.length = in.length > 0 ? in.length : 1;
  for (int i = 0; i < out.length && i < 3; i++) out.bytes[i] = bytes[i];
  out.mnemonic = in.mnemonic;
  out.operand = formatOperand(in);
  out.mode = modeOf(static_cast<AddrMode>(in.mode));
  out.category = static_cast<InstrCategory>(in.category);
  out.flow = getFlowType(in.opcode);
  return out;
}

Instruction fromBytes65816(const uint8_t *bytes, uint32_t address, bool accumulator8, bool index8) {
  const Disasm816Instruction in = disassemble816(bytes, 4, address, accumulator8, index8);
  Instruction out;
  out.address = address;
  out.target = in.target;
  out.length = in.length > 0 ? in.length : 1;
  for (int i = 0; i < out.length && i < 4; i++) out.bytes[i] = bytes[i];
  out.mnemonic = in.mnemonic;
  out.operand = formatOperand816(in);
  out.mode = modeOf(in.mode);
  out.category = in.category;
  out.flow = flowType816(in.opcode);
  return out;
}

} // namespace

CpuState MachineHost::cpuState() {
  CpuState s;
  if (iigs_) {
    const CPU65816 &cpu = iigs_->cpu();
    s.pc = cpu.getPCFull();
    s.a = cpu.getA();
    s.x = cpu.getX();
    s.y = cpu.getY();
    s.sp = cpu.getSP();
    s.d = cpu.getD();
    s.p = cpu.getP();
    s.pbr = cpu.getPBR();
    s.dbr = cpu.getDBR();
    s.widths = static_cast<uint8_t>((cpu.getEmulation() ? MachineDebug::WIDTH_EMULATION : 0) |
                                    (cpu.accumulator8() ? MachineDebug::WIDTH_A8 : 0) |
                                    (cpu.index8() ? MachineDebug::WIDTH_INDEX8 : 0));
    s.irqPending = cpu.isIRQPending();
    s.nmiPending = cpu.isNMIPending();
    s.cycles = iigs_->slowCycles();
    s.frameCycle = static_cast<int>(s.cycles % profile().timing.cyclesPerFrame());
    s.beam = iigs_->beam();
    return s;
  }
  if (!emulator_) return s;
  s.pc = emulator_->getPC();
  s.a = emulator_->getA();
  s.x = emulator_->getX();
  s.y = emulator_->getY();
  s.sp = emulator_->getSP();
  s.p = emulator_->getP();
  // A 6502 is permanently what a 65816 calls emulation mode with both
  // widths at eight bits.
  s.widths = MachineDebug::WIDTH_EMULATION | MachineDebug::WIDTH_A8 | MachineDebug::WIDTH_INDEX8;
  s.irqPending = emulator_->isIRQPending();
  s.nmiPending = emulator_->isNMIPending();
  s.nmiEdge = emulator_->isNMIEdge();
  s.cycles = emulator_->getTotalCycles();
  s.frameCycle = emulator_->getFrameCycle();
  s.beam = beamPosition(s.cycles, emulator_->getMachine().timing);
  return s;
}

void MachineHost::setRegister(CpuRegister reg, uint32_t value) {
  if (iigs_) {
    CPU65816 &cpu = iigs_->cpu();
    switch (reg) {
    case CpuRegister::A: cpu.setA(static_cast<uint16_t>(value)); break;
    case CpuRegister::X: cpu.setX(static_cast<uint16_t>(value)); break;
    case CpuRegister::Y: cpu.setY(static_cast<uint16_t>(value)); break;
    case CpuRegister::SP: cpu.setSP(static_cast<uint16_t>(value)); break;
    case CpuRegister::P: cpu.setP(static_cast<uint8_t>(value)); break;
    case CpuRegister::PBR: cpu.setPBR(static_cast<uint8_t>(value)); break;
    case CpuRegister::DBR: cpu.setDBR(static_cast<uint8_t>(value)); break;
    case CpuRegister::D: cpu.setD(static_cast<uint16_t>(value)); break;
    case CpuRegister::PC:
      // The bank travels with the address: editing the PC is the only way
      // to send the processor into another bank by hand.
      cpu.setPBR(static_cast<uint8_t>((value >> 16) & 0xFF));
      cpu.setPC(static_cast<uint16_t>(value & 0xFFFF));
      break;
    }
    return;
  }
  if (!emulator_) return;
  switch (reg) {
  case CpuRegister::A: emulator_->setA(static_cast<uint8_t>(value)); break;
  case CpuRegister::X: emulator_->setX(static_cast<uint8_t>(value)); break;
  case CpuRegister::Y: emulator_->setY(static_cast<uint8_t>(value)); break;
  case CpuRegister::SP: emulator_->setSP(static_cast<uint8_t>(value)); break;
  case CpuRegister::P: emulator_->setP(static_cast<uint8_t>(value)); break;
  case CpuRegister::PC: emulator_->setPC(static_cast<uint16_t>(value & 0xFFFF)); break;
  case CpuRegister::PBR:
  case CpuRegister::DBR:
  case CpuRegister::D: break; // a 6502 has none
  }
}

void MachineHost::stepInstruction() {
  if (iigs_) iigs_->stepInstruction();
  else if (emulator_) emulator_->stepInstruction();
}

uint32_t MachineHost::stepOver() {
  if (iigs_) return iigs_->stepOver();
  return emulator_ ? emulator_->stepOver() : 0;
}

uint32_t MachineHost::stepOut() {
  if (iigs_) return iigs_->stepOut();
  return emulator_ ? emulator_->stepOut() : 0;
}

uint8_t MachineHost::peek(uint32_t address) {
  if (iigs_) return iigs_->memory().peek(address & 0xFFFFFF);
  return emulator_ ? emulator_->peekMemory(static_cast<uint16_t>(address & 0xFFFF)) : 0;
}

std::vector<SoftSwitchInfo> MachineHost::softSwitches() const {
  return softSwitchCatalog(profile());
}

uint64_t MachineHost::softSwitchValue(uint32_t source) {
  if (iigs_) return iigs_->readSwitchSource(source);
  return emulator_ ? emulator_->readSwitchSource(source) : 0;
}

std::string MachineHost::switchHitText() {
  MachineDebug *d = debug();
  if (!d || !d->isSwitchBreakpointHit()) return {};

  // A flag is named by its bit; a register by its address, whatever mask
  // the breakpoint put over it.
  const std::vector<SoftSwitchInfo> catalog = softSwitches();
  const SoftSwitchInfo *entry = nullptr;
  for (const SoftSwitchInfo &s : catalog) {
    if (s.source != d->switchHitSource()) continue;
    if (s.isRegister() || s.mask() == d->switchHitMask()) {
      entry = &s;
      break;
    }
  }

  char where[16];
  const uint32_t pc = d->switchHitPC();
  if (profile().cpu == CPUVariant::CMOS_65C816) {
    std::snprintf(where, sizeof where, "%02X/%04X", pc >> 16, pc & 0xFFFF);
  } else {
    std::snprintf(where, sizeof where, "$%04X", pc & 0xFFFF);
  }

  char text[96];
  const char *name = entry ? entry->name : "Switch";
  if (entry && !entry->isRegister()) {
    std::snprintf(text, sizeof text, "%s %s, by %s", name,
                  d->switchHitAfter() ? "on" : "off", where);
  } else {
    std::snprintf(text, sizeof text, "%s $%02X to $%02X, by %s", name,
                  static_cast<unsigned>(d->switchHitBefore()),
                  static_cast<unsigned>(d->switchHitAfter()), where);
  }
  return text;
}

std::vector<MemorySpace> MachineHost::memorySpaces() {
  std::vector<MemorySpace> spaces;
  auto add = [&](MemorySpace::Kind kind, const std::string &name, uint32_t base, uint32_t size,
                 bool writable, bool processor, bool activity) {
    MemorySpace space;
    space.kind = kind;
    space.name = name;
    space.base = base;
    space.size = size;
    space.writable = writable;
    space.processor = processor;
    space.activity = activity;
    spaces.push_back(space);
  };
  using K = MemorySpace::Kind;
  if (iigs_) {
    char name[48];
    const size_t fastBanks = iigs_->memory().fastRamSize() / 0x10000;
    for (size_t i = 0; i < fastBanks; i++) {
      std::snprintf(name, sizeof name, "$%02X  Fast RAM", static_cast<unsigned>(i));
      add(K::Processor, name, static_cast<uint32_t>(i) << 16, 0x10000, true, true, false);
    }
    add(K::Processor, "$E0  Mega II main", uint32_t{iigs::SLOW_BANK_MAIN} << 16, 0x10000, true, true, false);
    add(K::Processor, "$E1  Mega II auxiliary", uint32_t{iigs::SLOW_BANK_AUX} << 16, 0x10000, true, true, false);
    size_t romSize = 0;
    Emulator::systemROMFor(machineId_, romSize);
    const size_t romBanks = romSize / 0x10000;
    for (size_t i = 0; i < romBanks; i++) {
      const unsigned bank = 0x100 - static_cast<unsigned>(romBanks) + static_cast<unsigned>(i);
      std::snprintf(name, sizeof name, "$%02X  ROM", bank);
      add(K::Processor, name, bank << 16, 0x10000, false, true, false);
    }
    return spaces;
  }
  if (!emulator_) return spaces;
  add(K::Processor, "Processor", 0, 0x10000, true, true, true);
  add(K::MainRAM, "Main RAM", 0, 0x10000, true, false, false);
  if (profile().caps.hasAuxRam) add(K::AuxRAM, "Auxiliary RAM", 0, 0x10000, true, false, false);
  add(K::ROM, "ROM", MMU::ROM_WINDOW_BASE, 0x4000, false, false, false);
  return spaces;
}

namespace {

// Where a //e bank's address lands in the language card, laid out as a IIgs
// bank: bank 2 at $D000 with the rest of the card, and bank 1 at $C000.
bool cardBank1(uint16_t &address) {
  if (address < 0xD000) {
    address = static_cast<uint16_t>(address + 0x1000);
    return true;
  }
  return false;
}

uint32_t inSpace(const MemorySpace &space, uint32_t address) {
  return space.base + ((address - space.base) % space.size);
}

} // namespace

uint8_t MachineHost::peekSpace(const MemorySpace &space, uint32_t address) {
  address = inSpace(space, address);
  if (iigs_ || space.kind == MemorySpace::Kind::Processor) return peek(address);
  if (!emulator_) return 0;
  MMU &mmu = emulator_->getMMU();
  uint16_t at = static_cast<uint16_t>(address);
  switch (space.kind) {
  case MemorySpace::Kind::MainRAM:
  case MemorySpace::Kind::AuxRAM: {
    const bool aux = space.kind == MemorySpace::Kind::AuxRAM;
    if (at < 0xC000) return mmu.readRAM(at, aux);
    const bool bank2 = !cardBank1(at);
    return mmu.peekLanguageCardBank(at, aux, bank2);
  }
  case MemorySpace::Kind::ROM:
    return mmu.getSystemROM()[at - MMU::ROM_WINDOW_BASE];
  case MemorySpace::Kind::Processor:
    break;
  }
  return 0;
}

void MachineHost::readSpace(const MemorySpace &space, uint32_t address, uint8_t *out, size_t count) {
  for (size_t i = 0; i < count; i++) out[i] = peekSpace(space, address + static_cast<uint32_t>(i));
}

bool MachineHost::pokeSpace(const MemorySpace &space, uint32_t address, uint8_t value) {
  if (!space.writable) return false;
  address = inSpace(space, address);
  if (iigs_) return iigs_->memory().poke(address, value);
  if (!emulator_) return false;
  MMU &mmu = emulator_->getMMU();
  uint16_t at = static_cast<uint16_t>(address);
  switch (space.kind) {
  case MemorySpace::Kind::Processor:
    return mmu.poke(at, value);
  case MemorySpace::Kind::MainRAM:
  case MemorySpace::Kind::AuxRAM: {
    const bool aux = space.kind == MemorySpace::Kind::AuxRAM;
    if (at < 0xC000) {
      mmu.writeRAM(at, value, aux);
      return true;
    }
    const bool bank2 = !cardBank1(at);
    mmu.pokeLanguageCardBank(at, value, aux, bank2);
    return true;
  }
  case MemorySpace::Kind::ROM:
    return false;
  }
  return false;
}

bool MachineHost::hasDisplayPage(DisplayPage page) const {
  if (!isBuilt()) return false;
  switch (page) {
  case DisplayPage::SuperHiRes: return iigs_ != nullptr;
  case DisplayPage::Text80: return profile().caps.has80Column;
  case DisplayPage::DoubleLoRes:
  case DisplayPage::DoubleHiRes: return profile().caps.hasDoubleHires;
  default: return true;
  }
}

bool MachineHost::renderDisplayPage(DisplayPage page, bool page2, VideoColorMode colours,
                                    std::vector<uint8_t> &rgba, int &width, int &height) {
  if (!hasDisplayPage(page)) return false;
  if (page == DisplayPage::SuperHiRes) {
    width = iigs::SHR_PIXELS_PER_LINE;
    height = iigs::SHR_LINES;
    rgba.resize(static_cast<size_t>(width) * height * 4);
    iigs_->screen().renderSuperHiResPicture(rgba.data());
    return true;
  }
  Video *v = video();
  if (!v) return false;
  static constexpr VideoPage PAGES[] = {VideoPage::Text40,      VideoPage::Text80, VideoPage::LoRes,
                                        VideoPage::DoubleLoRes, VideoPage::HiRes,  VideoPage::DoubleHiRes};
  width = ntsc::VISIBLE_DOTS;
  height = profile().timing.visibleScanlines;
  rgba.resize(static_cast<size_t>(width) * height * 4);
  v->renderPage(PAGES[static_cast<int>(page)], page2, colours, rgba.data());
  return true;
}

void MachineHost::setMemoryActivity(bool on) {
  if (!emulator_) return;
  MMU &mmu = emulator_->getMMU();
  if (on && !mmu.isTrackingEnabled()) mmu.clearTracking();
  mmu.enableTracking(on);
}

void MachineHost::memoryActivity(uint32_t address, size_t count, uint8_t *reads, uint8_t *writes) {
  if (!emulator_) {
    std::fill(reads, reads + count, 0);
    std::fill(writes, writes + count, 0);
    return;
  }
  const MMU &mmu = emulator_->getMMU();
  for (size_t i = 0; i < count; i++) {
    const uint16_t at = static_cast<uint16_t>(address + i);
    reads[i] = mmu.getReadCounts()[at];
    writes[i] = mmu.getWriteCounts()[at];
  }
}

void MachineHost::decayMemoryActivity(uint8_t amount) {
  if (emulator_) emulator_->getMMU().decayTracking(amount);
}

Instruction MachineHost::disassemble(uint32_t address) {
  if (iigs_) {
    // An instruction's bytes stay in its bank: the program counter wraps
    // within it rather than carrying into the next.
    uint8_t bytes[4];
    for (int i = 0; i < 4; i++) {
      bytes[i] = peek((address & 0xFF0000) | static_cast<uint16_t>((address & 0xFFFF) + i));
    }
    const CPU65816 &cpu = iigs_->cpu();
    return fromBytes65816(bytes, address & 0xFFFFFF, cpu.accumulator8(), cpu.index8());
  }
  const uint16_t at = static_cast<uint16_t>(address & 0xFFFF);
  uint8_t bytes[3];
  for (int i = 0; i < 3; i++) bytes[i] = peek(static_cast<uint16_t>(at + i));
  return fromBytes6502(bytes, at);
}

std::vector<Instruction> MachineHost::disassembleRange(uint32_t centre, int before, int count) {
  std::vector<Instruction> lines;
  if ((!emulator_ && !iigs_) || count <= 0) return lines;
  centre &= iigs_ ? 0xFFFFFF : 0xFFFF;
  const uint32_t bank = centre & 0xFF0000;
  // Where to begin is an alignment search: the centre must appear as an
  // instruction, whatever the bytes above it decode to.
  int at = alignedDisassemblyStart(static_cast<uint16_t>(centre & 0xFFFF), std::max(before, 0),
                                   iigs_ ? 4 : 3, [&](uint16_t offset) {
                                     return disassemble(bank | offset).length;
                                   });
  lines.reserve(static_cast<size_t>(count));
  for (int i = 0; i < count && at <= 0xFFFF; i++) {
    lines.push_back(disassemble(bank | static_cast<uint16_t>(at)));
    at += lines.back().length;
  }
  return lines;
}

namespace {

bool isOneOf(const std::string &m, std::initializer_list<const char *> names) {
  for (const char *n : names) {
    if (m == n) return true;
  }
  return false;
}

// The reads that pay for an index carrying into the next page. Stores and
// read-modify-writes never do: their base count already includes the cycle.
bool paysPageCross(const std::string &m) {
  return isOneOf(m, {"LDA", "LDX", "LDY", "ADC", "SBC", "AND", "ORA", "EOR", "CMP", "BIT"});
}

bool isIndexedRead(OperandMode mode) {
  return mode == OperandMode::AbsoluteX || mode == OperandMode::AbsoluteY ||
         mode == OperandMode::DirectIndirectIndexed;
}

bool isDirect(OperandMode mode) {
  switch (mode) {
  case OperandMode::Direct:
  case OperandMode::DirectX:
  case OperandMode::DirectY:
  case OperandMode::DirectIndirect:
  case OperandMode::DirectIndexedIndirect:
  case OperandMode::DirectIndirectIndexed:
  case OperandMode::DirectIndirectLong:
  case OperandMode::DirectIndirectLongIndexed: return true;
  default: return false;
  }
}

bool touchesMemory(OperandMode mode) {
  switch (mode) {
  case OperandMode::Implied:
  case OperandMode::Accumulator:
  case OperandMode::Immediate:
  case OperandMode::Relative:
  case OperandMode::RelativeLong:
  case OperandMode::BlockMove: return false;
  default: return true;
  }
}

} // namespace

void MachineHost::setProfiling(bool on) {
  if (emulator_) emulator_->setProfileEnabled(on);
  if (iigs_) iigs_->setCoverageEnabled(on);
}

void MachineHost::clearProfile() {
  if (emulator_) emulator_->clearProfile();
  if (iigs_) iigs_->clearCoverage();
}

uint32_t MachineHost::profileCycles(uint32_t address) {
  return emulator_ ? emulator_->getProfileCycles()[address & 0xFFFF] : 0;
}

bool MachineHost::wasExecuted(uint32_t address) {
  if (iigs_) return iigs_->wasExecuted(address);
  return profileCycles(address) != 0;
}

void MachineHost::profileTotals(uint32_t &max, uint64_t &total) {
  max = 0;
  total = 0;
  if (!emulator_) return;
  const uint32_t *counts = emulator_->getProfileCycles();
  for (size_t i = 0; i < 0x10000; i++) {
    max = std::max(max, counts[i]);
    total += counts[i];
  }
}

// The rules are the core's, read off where it charges them (cpu6502.cpp and
// cpu65816*.cpp), so the column cannot disagree with the machine it describes.
CycleCost MachineHost::cycleCost(const Instruction &in, const CpuState &s, bool atPC) {
  CycleCost cost;
  if (!emulator_ && !iigs_) return cost;
  const uint8_t opcode = in.bytes[0];
  const std::string &m = in.mnemonic;
  int base = iigs_ ? CPU65816::baseCycles(opcode) : CPU6502::baseCycles(opcode);
  int extraMin = 0;
  int extraMax = 0;
  auto fixed = [&](int n) {
    extraMin += n;
    extraMax += n;
  };

  // The page an index lands in, against the page it started from.
  auto crossesPage = [&](uint16_t from, uint16_t index) {
    return (from & 0xFF00) != ((from + (index & 0xFF)) & 0xFF00);
  };
  auto indexedBase = [&]() -> uint16_t {
    if (in.mode == OperandMode::DirectIndirectIndexed) {
      const uint32_t pointer = iigs_ ? ((s.d + in.bytes[1]) & 0xFFFF) : in.bytes[1];
      const uint32_t next = iigs_ ? ((pointer + 1) & 0xFFFF) : ((pointer + 1) & 0xFF);
      return static_cast<uint16_t>(peek(pointer) | (peek(next) << 8));
    }
    return static_cast<uint16_t>(in.bytes[1] | (in.bytes[2] << 8));
  };
  auto pageCross = [&](bool alwaysWhenWide) {
    const uint16_t index = in.mode == OperandMode::AbsoluteX ? s.x : s.y;
    if (alwaysWhenWide) {
      fixed(1);
    } else if (atPC) {
      if (crossesPage(indexedBase(), index)) fixed(1);
    } else {
      extraMax += 1;
    }
  };
  // A branch: a cycle for being taken, and one more for landing in another
  // page where the processor charges it.
  auto branch = [&](bool alwaysTaken, bool chargesCross) {
    const uint16_t next = static_cast<uint16_t>(in.address + in.length);
    const bool cross = chargesCross && (in.target & 0xFF00) != (next & 0xFF00);
    const int taken = 1 + (cross ? 1 : 0);
    std::optional<bool> known;
    if (alwaysTaken) known = true;
    else if (atPC) known = branchTaken(in, s);
    if (known) fixed(*known ? taken : 0);
    else extraMax += taken;
  };

  if (!iigs_) {
    const bool cmos = profile().cpu == CPUVariant::CMOS_65C02;
    if (isIndexedRead(in.mode) && paysPageCross(m)) pageCross(false);
    if (cmos && (s.p & 0x08) && isOneOf(m, {"ADC", "SBC"})) fixed(1);
    if (in.mode == OperandMode::Relative) branch(m == "BRA", true);
  } else {
    const bool m16 = !s.accumulator8();
    const bool x16 = !s.index8();
    const bool native = !s.emulation();
    const bool accumulatorOp = isOneOf(m, {"LDA", "STA", "ADC", "SBC", "AND", "ORA", "EOR", "CMP", "BIT", "STZ"});
    const bool readModifyWrite = isOneOf(m, {"ASL", "LSR", "ROL", "ROR", "INC", "DEC", "TSB", "TRB"});
    const bool indexOp = isOneOf(m, {"LDX", "LDY", "STX", "STY", "CPX", "CPY"});
    // A second byte read or written is a second cycle.
    if (in.mode == OperandMode::Immediate) {
      if ((accumulatorOp && m16) || (indexOp && x16)) fixed(1);
    } else if (touchesMemory(in.mode)) {
      if (accumulatorOp && m16) fixed(1);
      if (readModifyWrite && m16) fixed(2);
      if (indexOp && x16) fixed(1);
    }
    if (isDirect(in.mode) && (s.d & 0x00FF)) fixed(1);
    // An index sixteen bits wide carries every time, and the part stops
    // guessing and always takes the cycle.
    if (isIndexedRead(in.mode) && paysPageCross(m)) pageCross(x16);
    if ((m == "PHA" || m == "PLA") && m16) fixed(1);
    if (isOneOf(m, {"PHX", "PLX", "PHY", "PLY"}) && x16) fixed(1);
    if (native && isOneOf(m, {"RTI", "BRK", "COP"})) fixed(1);
    if (in.mode == OperandMode::Relative) branch(m == "BRA", !native);
    if (in.mode == OperandMode::BlockMove) cost.perByte = true;
  }
  cost.min = static_cast<uint8_t>(base + extraMin);
  cost.max = static_cast<uint8_t>(base + extraMax);
  return cost;
}

std::optional<bool> MachineHost::branchTaken(const Instruction &in, const CpuState &s) {
  if (in.flow != FlowType::CONDITIONAL && in.mnemonic != "BRA" && in.mnemonic != "BRL") return std::nullopt;
  const uint8_t p = s.p;
  const std::string &m = in.mnemonic;
  if (m == "BRA" || m == "BRL") return true;
  if (m == "BPL") return !(p & 0x80);
  if (m == "BMI") return (p & 0x80) != 0;
  if (m == "BVC") return !(p & 0x40);
  if (m == "BVS") return (p & 0x40) != 0;
  if (m == "BCC") return !(p & 0x01);
  if (m == "BCS") return (p & 0x01) != 0;
  if (m == "BNE") return !(p & 0x02);
  if (m == "BEQ") return (p & 0x02) != 0;
  // BBRn and BBSn test bit n of a zero page byte.
  if (m.size() == 4 && (m.rfind("BBR", 0) == 0 || m.rfind("BBS", 0) == 0)) {
    const int bit = m[3] - '0';
    if (bit < 0 || bit > 7) return std::nullopt;
    const bool set = (peek(in.bytes[1]) >> bit) & 1;
    return m[2] == 'S' ? set : !set;
  }
  return std::nullopt;
}

std::optional<EffectiveAddress> MachineHost::effectiveAddress(const Instruction &in, const CpuState &s) {
  const bool wide = iigs_ != nullptr;
  const uint32_t op8 = in.bytes[1];
  const uint32_t op16 = in.bytes[1] | (in.bytes[2] << 8);
  const uint32_t op24 = op16 | (static_cast<uint32_t>(in.bytes[3]) << 16);
  const uint32_t dataBank = static_cast<uint32_t>(s.dbr) << 16;
  const uint32_t programBank = s.pc & 0xFF0000;

  // The direct page: a 6502's zero page, or wherever a 65816's D has put it.
  // In emulation mode with D on a page boundary, indexing wraps in the page.
  auto direct = [&](uint32_t offset, uint32_t index) -> uint32_t {
    if (!wide || (s.emulation() && (s.d & 0xFF) == 0)) {
      return (s.d & 0xFF00) | ((offset + index) & 0xFF);
    }
    return (s.d + offset + index) & 0xFFFF;
  };
  auto pointer16 = [&](uint32_t at, bool inPage) -> uint32_t {
    const uint32_t next = inPage ? ((at & 0xFFFF00) | ((at + 1) & 0xFF)) : ((at & 0xFF0000) | ((at + 1) & 0xFFFF));
    return peek(at) | (peek(next) << 8);
  };
  auto pointer24 = [&](uint32_t at) -> uint32_t {
    return peek(at) | (peek((at + 1) & 0xFFFFFF) << 8) | (peek((at + 2) & 0xFFFFFF) << 16);
  };
  const bool directWraps = !wide || (s.emulation() && (s.d & 0xFF) == 0);

  std::optional<uint32_t> address;
  switch (in.mode) {
  case OperandMode::Direct: address = direct(op8, 0); break;
  case OperandMode::DirectX: address = direct(op8, s.x); break;
  case OperandMode::DirectY: address = direct(op8, s.y); break;
  case OperandMode::DirectRelative: address = direct(op8, 0); break;
  case OperandMode::DirectIndirect: address = dataBank | pointer16(direct(op8, 0), directWraps); break;
  case OperandMode::DirectIndexedIndirect: address = dataBank | pointer16(direct(op8, s.x), directWraps); break;
  case OperandMode::DirectIndirectIndexed:
    address = ((dataBank | pointer16(direct(op8, 0), directWraps)) + s.y) & (wide ? 0xFFFFFF : 0xFFFF);
    break;
  case OperandMode::DirectIndirectLong: address = pointer24(direct(op8, 0)); break;
  case OperandMode::DirectIndirectLongIndexed: address = (pointer24(direct(op8, 0)) + s.y) & 0xFFFFFF; break;
  case OperandMode::Absolute:
    // A jump's absolute operand is where it goes, which the listing shows
    // already; only data is worth resolving.
    if (in.flow != FlowType::SEQUENTIAL) return std::nullopt;
    address = dataBank | op16;
    break;
  case OperandMode::AbsoluteX: address = ((dataBank | op16) + s.x) & (wide ? 0xFFFFFF : 0xFFFF); break;
  case OperandMode::AbsoluteY: address = ((dataBank | op16) + s.y) & (wide ? 0xFFFFFF : 0xFFFF); break;
  case OperandMode::AbsoluteLong:
    if (in.flow != FlowType::SEQUENTIAL) return std::nullopt;
    address = op24;
    break;
  case OperandMode::AbsoluteLongX: address = (op24 + s.x) & 0xFFFFFF; break;
  // The indirect jumps: where they will go, read through their pointer.
  case OperandMode::AbsoluteIndirect: address = programBank | pointer16(op16, false); break;
  case OperandMode::AbsoluteIndexedIndirect:
    address = programBank | pointer16(programBank | ((op16 + s.x) & 0xFFFF), false);
    break;
  case OperandMode::AbsoluteIndirectLong: address = pointer24(op16); break;
  case OperandMode::StackRelative: address = (s.sp + op8) & 0xFFFF; break;
  case OperandMode::StackRelativeIndirectIndexed:
    address = ((dataBank | pointer16((s.sp + op8) & 0xFFFF, false)) + s.y) & 0xFFFFFF;
    break;
  default: return std::nullopt;
  }
  if (!wide) *address &= 0xFFFF;
  return EffectiveAddress{*address, peek(*address)};
}

std::vector<TraceLine> MachineHost::traceLines(size_t start, size_t count) {
  std::vector<TraceLine> lines;
  MachineDebug *dbg = debug();
  if (!dbg) return lines;
  const size_t total = dbg->traceCount();
  const size_t capacity = dbg->traceCapacity();
  if (total == 0 || capacity == 0) return lines;
  const MachineDebug::TraceEntry *entries = dbg->traceBuffer();
  const size_t head = dbg->traceHead();
  for (size_t i = 0; i < count && start + i < total; i++) {
    // The ring has not wrapped until it is full; after that the oldest entry
    // is wherever the write position now points.
    const size_t index = start + i;
    const size_t ring = total < capacity ? index : (head + index) % capacity;
    const MachineDebug::TraceEntry &e = entries[ring];
    TraceLine line;
    line.cycle = e.cycle;
    const uint8_t bytes[4] = {e.opcode, e.operand1, e.operand2, e.operand3};
    line.instruction = iigs_ ? fromBytes65816(bytes, e.pc, (e.widths & MachineDebug::WIDTH_A8) != 0,
                                              (e.widths & MachineDebug::WIDTH_INDEX8) != 0)
                             : fromBytes6502(bytes, static_cast<uint16_t>(e.pc & 0xFFFF));
    line.a = e.a;
    line.x = e.x;
    line.y = e.y;
    line.sp = e.sp;
    line.p = e.p;
    line.widths = e.widths;
    lines.push_back(std::move(line));
  }
  return lines;
}

bool MachineHost::evaluateCondition(const std::string &expression) {
  const MachineView machine = view();
  if (!machine.peek) return false;
  return ConditionEvaluator::evaluate(expression.c_str(), machine);
}

int32_t MachineHost::evaluateExpression(const std::string &expression) {
  const MachineView machine = view();
  if (!machine.peek) return 0;
  return ConditionEvaluator::evaluateNumeric(expression.c_str(), machine);
}

std::string MachineHost::conditionError() const {
  const char *error = ConditionEvaluator::getLastError();
  return error ? error : "";
}

} // namespace a2e::host
