/*
 * mmu.cpp - Memory management unit implementation
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "mmu.hpp"
#include "../cards/expansion_card.hpp"
#include "../noslot_clock.hpp"
#include <cstring>

namespace a2e {

MMU::MMU() : noSlotClock_(std::make_unique<NoSlotClock>()) { reset(); }

MMU::~MMU() = default;

void MMU::reset() {
  // Clear RAM
  mainRAM_.fill(0);
  auxRAM_.fill(0);
  lcBank1_.fill(0);
  lcBank2_.fill(0);
  lcHighRAM_.fill(0);
  auxLcBank1_.fill(0);
  auxLcBank2_.fill(0);
  auxLcHighRAM_.fill(0);

  // Reset soft switches to default state
  switches_ = SoftSwitches{};

  // Keyboard
  keyboardLatch_ = 0;

  // Reset expansion slot state
  activeExpansionSlot_ = 0;

  // Reset all installed cards
  for (auto& card : slots_) {
    if (card) {
      card->reset();
    }
  }

  // Reset No-Slot Clock state (preserve enabled flag)
  if (noSlotClock_) noSlotClock_->reset();

  // Clear tracking (but preserve enabled state)
  clearTracking();
}

void MMU::warmReset() {
  // Reset soft switches to default state (preserves all RAM)
  // On real Apple IIe hardware, the reset signal resets the IOU/MMU
  // soft switches but does not clear memory
  switches_ = SoftSwitches{};

  // Keyboard
  keyboardLatch_ = 0;

  // Reset expansion slot state
  activeExpansionSlot_ = 0;

  // Reset all installed cards
  for (auto& card : slots_) {
    if (card) {
      card->reset();
    }
  }
}

// ===== Expansion Slot Management =====

std::unique_ptr<ExpansionCard> MMU::insertCard(uint8_t slot, std::unique_ptr<ExpansionCard> card) {
  if (slot < 1 || slot > 7) {
    return card; // Invalid slot, return card unchanged
  }

  std::unique_ptr<ExpansionCard> previous = std::move(slots_[slot - 1]);
  slots_[slot - 1] = std::move(card);

  // If the removed card owned the expansion ROM, clear it
  if (activeExpansionSlot_ == slot) {
    activeExpansionSlot_ = 0;
  }

  return previous;
}

std::unique_ptr<ExpansionCard> MMU::removeCard(uint8_t slot) {
  if (slot < 1 || slot > 7) {
    return nullptr;
  }

  std::unique_ptr<ExpansionCard> card = std::move(slots_[slot - 1]);

  // If this card owned the expansion ROM, clear it
  if (activeExpansionSlot_ == slot) {
    activeExpansionSlot_ = 0;
  }

  return card;
}

ExpansionCard* MMU::getCard(uint8_t slot) const {
  if (slot < 1 || slot > 7) {
    return nullptr;
  }
  return slots_[slot - 1].get();
}

bool MMU::isSlotEmpty(uint8_t slot) const {
  if (slot < 1 || slot > 7) {
    return true;
  }
  return !slots_[slot - 1];
}

void MMU::enableNoSlotClock(bool enable) {
  if (noSlotClock_) noSlotClock_->setEnabled(enable);
}

bool MMU::isNoSlotClockEnabled() const {
  return noSlotClock_ && noSlotClock_->isEnabled();
}

void MMU::clearTracking() {
  readCounts_.fill(0);
  writeCounts_.fill(0);
}

void MMU::decayTracking(uint8_t amount) {
  for (size_t i = 0; i < 65536; ++i) {
    if (readCounts_[i] > amount) {
      readCounts_[i] -= amount;
    } else {
      readCounts_[i] = 0;
    }
    if (writeCounts_[i] > amount) {
      writeCounts_[i] -= amount;
    } else {
      writeCounts_[i] = 0;
    }
  }
}

void MMU::loadROM(const uint8_t *systemRom, size_t systemSize,
                  const uint8_t *charRom, size_t charSize) {
  if (systemRom && systemSize > 0) {
    std::memcpy(systemROM_.data(), systemRom,
                std::min(systemSize, systemROM_.size()));
  }
  if (charRom && charSize > 0) {
    std::memcpy(charROM_.data(), charRom, std::min(charSize, charROM_.size()));
  }
}

uint8_t MMU::readCharROM(uint16_t address) const {
  return charROM_[address & (CHAR_ROM_SIZE - 1)];
}

uint8_t MMU::readRAM(uint16_t address, bool aux) const {
  if (aux) {
    return auxRAM_[address];
  }
  return mainRAM_[address];
}

void MMU::writeRAM(uint16_t address, uint8_t value, bool aux) {
  if (aux) {
    auxRAM_[address] = value;
  } else {
    mainRAM_[address] = value;
  }
}

uint8_t MMU::peek(uint16_t address) const {
  // Non-side-effecting read for debugger/memory viewer
  // Same logic as read() but without any state changes or callbacks

  // Zero page and stack
  if (address < 0x0200) {
    if (switches_.altzp) {
      return auxRAM_[address];
    }
    return mainRAM_[address];
  }

  // Main RAM: $0200-$BFFF
  if (address < 0xC000) {
    // Text page 1: $0400-$07FF
    if (address >= 0x0400 && address < 0x0800) {
      if (switches_.store80) {
        return switches_.page2 ? auxRAM_[address] : mainRAM_[address];
      }
      return switches_.ramrd ? auxRAM_[address] : mainRAM_[address];
    }

    // Text page 2: $0800-$0BFF
    if (address >= 0x0800 && address < 0x0C00) {
      return switches_.ramrd ? auxRAM_[address] : mainRAM_[address];
    }

    // HiRes page 1: $2000-$3FFF
    if (address >= 0x2000 && address < 0x4000) {
      if (switches_.store80 && switches_.hires) {
        return switches_.page2 ? auxRAM_[address] : mainRAM_[address];
      }
      return switches_.ramrd ? auxRAM_[address] : mainRAM_[address];
    }

    // HiRes page 2: $4000-$5FFF
    if (address >= 0x4000 && address < 0x6000) {
      return switches_.ramrd ? auxRAM_[address] : mainRAM_[address];
    }

    // All other main RAM
    return switches_.ramrd ? auxRAM_[address] : mainRAM_[address];
  }

  // I/O and soft switches: $C000-$C0FF
  if (address < 0xC100) {
    return peekSoftSwitch(address);
  }

  // Slot ROM space: $C100-$CFFF
  if (address < 0xD000) {
    if (switches_.intcxrom) {
      return systemROM_[address - 0xC000];
    }

    // Slot 3 ($C300-$C3FF)
    if (address >= 0xC300 && address < 0xC400) {
      if (!switches_.slotc3rom) {
        return systemROM_[address - 0xC000];
      }
      return 0xFF; // No card, return high byte
    }

    // Slot ROM: $C100-$C7FF - check slot system
    if (address < 0xC800) {
      uint8_t slot = (address >> 8) & 0x07;
      uint8_t offset = address & 0xFF;
      if (slot >= 1 && slot <= 7 && slots_[slot - 1]) {
        return slots_[slot - 1]->readROM(offset);
      }
      return 0xFF;
    }

    // $C800-$CFFF: Expansion ROM space
    if (address >= 0xC800) {
      if (switches_.intc8rom) {
        return systemROM_[address - 0xC000];
      }
      return 0xFF;
    }

    // Other slot ROM space - no cards
    return 0xFF;
  }

  // Language card area: $D000-$FFFF
  if (switches_.lcram) {
    bool useAux = switches_.altzp;
    if (address < 0xE000) {
      uint16_t offset = address - 0xD000;
      if (switches_.lcram2) {
        return useAux ? auxLcBank2_[offset] : lcBank2_[offset];
      } else {
        return useAux ? auxLcBank1_[offset] : lcBank1_[offset];
      }
    } else {
      uint16_t offset = address - 0xE000;
      return useAux ? auxLcHighRAM_[offset] : lcHighRAM_[offset];
    }
  }
  return systemROM_[address - 0xC000];
}

uint8_t MMU::peekAux(uint16_t address) const {
  // Direct read of auxiliary memory for text selection in 80-column mode
  // This always reads from aux RAM regardless of soft switch state
  return auxRAM_[address];
}

uint8_t MMU::peekSoftSwitch(uint16_t address) const {
  // Non-side-effecting soft switch read for debugger
  uint8_t reg = address & 0xFF;

  switch (reg) {
  // Keyboard - return current latch without updating
  // Per AppleWin/hardware behavior, reading any address in $C000-$C00F returns keyboard latch
  case 0x00:
  case 0x01:
  case 0x02:
  case 0x03:
  case 0x04:
  case 0x05:
  case 0x06:
  case 0x07:
  case 0x08:
  case 0x09:
  case 0x0A:
  case 0x0B:
  case 0x0C:
  case 0x0D:
  case 0x0E:
  case 0x0F:
    return keyboardLatch_;
  case 0x10: {
    // Peek returns AKD in bit 7, key code in bits 0-6 (without clearing strobe)
    bool anyKeyDown = anyKeyDownCallback_ ? anyKeyDownCallback_() : false;
    return (anyKeyDown ? 0x80 : 0x00) | (keyboardLatch_ & 0x7F);
  }

  // Memory switch status reads (these are safe - just report state)
  case 0x11:
    return switches_.lcram2 ? 0x80 : 0x00;
  case 0x12:
    return switches_.lcram ? 0x80 : 0x00;
  case 0x13:
    return switches_.ramrd ? 0x80 : 0x00;
  case 0x14:
    return switches_.ramwrt ? 0x80 : 0x00;
  case 0x15:
    return switches_.intcxrom ? 0x80 : 0x00;
  case 0x16:
    return switches_.altzp ? 0x80 : 0x00;
  case 0x17:
    return switches_.slotc3rom ? 0x80 : 0x00;
  case 0x18:
    return switches_.store80 ? 0x80 : 0x00;
  case 0x19:
    return 0x00; // VBL status - return arbitrary value for peek
  case 0x1A:
    return switches_.text ? 0x80 : 0x00;
  case 0x1B:
    return switches_.mixed ? 0x80 : 0x00;
  case 0x1C:
    return switches_.page2 ? 0x80 : 0x00;
  case 0x1D:
    return switches_.hires ? 0x80 : 0x00;
  case 0x1E:
    return switches_.altCharSet ? 0x80 : 0x00;
  case 0x1F:
    return switches_.col80 ? 0x80 : 0x00;

  // Buttons - peek returns current state
  case 0x61:
    return buttonCallback_ ? (buttonCallback_(0) & 0x80) : 0x00;
  case 0x62:
    return buttonCallback_ ? (buttonCallback_(1) & 0x80) : 0x00;
  case 0x63:
    return buttonCallback_ ? (buttonCallback_(2) & 0x80) : 0x00;

  // Paddle inputs
  case 0x64:
  case 0x65:
  case 0x66:
  case 0x67:
    return 0x00;

  // Slot I/O space: $C090-$C0FF (slots 1-7)
  case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
  case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E: case 0x9F:
  case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5: case 0xA6: case 0xA7:
  case 0xA8: case 0xA9: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF:
  case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
  case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
  case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: case 0xC6: case 0xC7:
  case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: case 0xCF:
  case 0xD0: case 0xD1: case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: case 0xD7:
  case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
  case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: case 0xE6: case 0xE7:
  case 0xE8: case 0xE9: case 0xEA: case 0xEB: case 0xEC: case 0xED: case 0xEE: case 0xEF:
  case 0xF0: case 0xF1: case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: case 0xF7:
  case 0xF8: case 0xF9: case 0xFA: case 0xFB: case 0xFC: case 0xFD: case 0xFE: case 0xFF: {
    // Calculate slot number
    uint8_t slot = ((reg - 0x80) >> 4);
    uint8_t offset = reg & 0x0F;

    if (slot >= 1 && slot <= 7 && slots_[slot - 1]) {
      return slots_[slot - 1]->peekIO(offset);
    }

    return 0x00;
  }

  // Language card switches - report switch state
  case 0x80:
  case 0x81:
  case 0x82:
  case 0x83:
  case 0x84:
  case 0x85:
  case 0x86:
  case 0x87:
  case 0x88:
  case 0x89:
  case 0x8A:
  case 0x8B:
  case 0x8C:
  case 0x8D:
  case 0x8E:
  case 0x8F:
    return switches_.lcram ? 0x80 : 0x00;

  default:
    return 0x00;
  }
}

uint8_t MMU::read(uint16_t address) {
  // Track read access
  if (trackingEnabled_ && readCounts_[address] < 255) {
    ++readCounts_[address];
  }

  // Watchpoint check on read
  if (watchpointsActive_ && watchpointReadCallback_) {
    // Determine value without side effects for the callback
    uint8_t val = peek(address);
    watchpointReadCallback_(address, val);
  }

  // Zero page and stack
  if (address < 0x0200) {
    if (switches_.altzp) {
      return auxRAM_[address];
    }
    return mainRAM_[address];
  }

  // Main RAM: $0200-$BFFF
  if (address < 0xC000) {
    // Text page 1: $0400-$07FF
    if (address >= 0x0400 && address < 0x0800) {
      if (switches_.store80) {
        // 80STORE on: PAGE2 controls main/aux, RAMRD is ignored
        return switches_.page2 ? auxRAM_[address] : mainRAM_[address];
      }
      // 80STORE off: RAMRD controls main/aux
      return switches_.ramrd ? auxRAM_[address] : mainRAM_[address];
    }

    // Text page 2: $0800-$0BFF
    if (address >= 0x0800 && address < 0x0C00) {
      if (switches_.ramrd) {
        return auxRAM_[address];
      }
      return mainRAM_[address];
    }

    // HiRes page 1: $2000-$3FFF
    if (address >= 0x2000 && address < 0x4000) {
      if (switches_.store80 && switches_.hires) {
        // 80STORE+HIRES on: PAGE2 controls main/aux, RAMRD is ignored
        return switches_.page2 ? auxRAM_[address] : mainRAM_[address];
      }
      // 80STORE off or HIRES off: RAMRD controls main/aux
      return switches_.ramrd ? auxRAM_[address] : mainRAM_[address];
    }

    // HiRes page 2: $4000-$5FFF
    if (address >= 0x4000 && address < 0x6000) {
      if (switches_.ramrd) {
        return auxRAM_[address];
      }
      return mainRAM_[address];
    }

    // All other main RAM
    if (switches_.ramrd) {
      return auxRAM_[address];
    }
    return mainRAM_[address];
  }

  // I/O and soft switches: $C000-$C0FF
  if (address < 0xC100) {
    return readSoftSwitch(address);
  }

  // Slot ROM space: $C100-$CFFF
  if (address < 0xD000) {
    // When INTCXROM is ON, all of $C100-$CFFF uses internal ROM
    if (switches_.intcxrom) {
      return systemROM_[address - 0xC000];
    }

    // INTCXROM is OFF - use slot ROMs

    // Slot 3 ($C300-$C3FF) has special handling via SLOTC3ROM
    if (address >= 0xC300 && address < 0xC400) {
      if (!switches_.slotc3rom) {
        // SLOTC3ROM off: use internal ROM for slot 3
        // Also activates internal ROM for $C800-$CFFF
        switches_.intc8rom = true;
        uint8_t romValue = systemROM_[address - 0xC000];
        // No-Slot Clock intercepts reads in this region
        if (noSlotClock_) {
          romValue = noSlotClock_->interceptRead(address, romValue);
        }
        return romValue;
      }
      // SLOTC3ROM on: use slot 3 ROM (no card, return floating bus)
      return getFloatingBusValue();
    }

    // Slot ROM space: $C100-$C7FF
    // Each slot gets 256 bytes: slot N at $CN00-$CNFF
    if (address < 0xC800) {
      uint8_t slot = (address >> 8) & 0x07;
      uint8_t offset = address & 0xFF;

      // Access to slot ROM activates that card's expansion ROM
      if (slot >= 1 && slot <= 7 && slots_[slot - 1]) {
        activeExpansionSlot_ = slot;
        return slots_[slot - 1]->readROM(offset);
      }

      return getFloatingBusValue();
    }

    // $C800-$CFFF: Expansion ROM space

    // Return internal ROM if slot 3 internal ROM was accessed
    if (switches_.intc8rom) {
      // Access to $CFFF clears the internal ROM select AFTER the read
      if (address == 0xCFFF) {
        uint8_t value = systemROM_[address - 0xC000];
        switches_.intc8rom = false;
        activeExpansionSlot_ = 0;
        return value;
      }
      return systemROM_[address - 0xC000];
    }

    // Check if a card owns the expansion ROM space
    if (activeExpansionSlot_ >= 1 && activeExpansionSlot_ <= 7) {
      auto& card = slots_[activeExpansionSlot_ - 1];
      if (card && card->hasExpansionROM()) {
        uint8_t value = card->readExpansionROM(address - 0xC800);
        // Access to $CFFF clears the expansion ROM select AFTER the read
        if (address == 0xCFFF) {
          activeExpansionSlot_ = 0;
        }
        return value;
      }
    }

    // Access to $CFFF with no active expansion ROM still clears the select
    if (address == 0xCFFF) {
      switches_.intc8rom = false;
      activeExpansionSlot_ = 0;
    }

    // No expansion ROM active, return floating bus
    return getFloatingBusValue();
  }

  // Language card area: $D000-$FFFF
  return readLanguageCard(address);
}

void MMU::write(uint16_t address, uint8_t value) {
  // Track write access
  if (trackingEnabled_ && writeCounts_[address] < 255) {
    ++writeCounts_[address];
  }

  // Watchpoint check on write
  if (watchpointsActive_ && watchpointWriteCallback_) {
    watchpointWriteCallback_(address, value);
  }

  // Zero page and stack
  if (address < 0x0200) {
    if (switches_.altzp) {
      auxRAM_[address] = value;
    } else {
      mainRAM_[address] = value;
    }
    return;
  }

  // Main RAM: $0200-$BFFF
  if (address < 0xC000) {
    // Text page 1: $0400-$07FF
    if (address >= 0x0400 && address < 0x0800) {
      if (switches_.store80) {
        // 80STORE on: PAGE2 controls main/aux, RAMWRT is ignored
        if (switches_.page2) {
          auxRAM_[address] = value;
        } else {
          mainRAM_[address] = value;
        }
        return;
      }
      // 80STORE off: RAMWRT controls main/aux
      if (switches_.ramwrt) {
        auxRAM_[address] = value;
      } else {
        mainRAM_[address] = value;
      }
      return;
    }

    // Text page 2: $0800-$0BFF
    if (address >= 0x0800 && address < 0x0C00) {
      if (switches_.ramwrt) {
        auxRAM_[address] = value;
      } else {
        mainRAM_[address] = value;
      }
      return;
    }

    // HiRes page 1: $2000-$3FFF
    if (address >= 0x2000 && address < 0x4000) {
      if (switches_.store80 && switches_.hires) {
        // 80STORE+HIRES on: PAGE2 controls main/aux, RAMWRT is ignored
        if (switches_.page2) {
          auxRAM_[address] = value;
        } else {
          mainRAM_[address] = value;
        }
        return;
      }
      // 80STORE off or HIRES off: RAMWRT controls main/aux
      if (switches_.ramwrt) {
        auxRAM_[address] = value;
      } else {
        mainRAM_[address] = value;
      }
      return;
    }

    // HiRes page 2: $4000-$5FFF
    if (address >= 0x4000 && address < 0x6000) {
      if (switches_.ramwrt) {
        auxRAM_[address] = value;
      } else {
        mainRAM_[address] = value;
      }
      return;
    }

    // All other main RAM
    if (switches_.ramwrt) {
      auxRAM_[address] = value;
    } else {
      mainRAM_[address] = value;
    }
    return;
  }

  // I/O and soft switches: $C000-$C0FF
  if (address < 0xC100) {
    writeSoftSwitch(address, value);
    return;
  }

  // Slot ROM space: $C100-$CFFF
  // Most cards don't handle writes, but some (like Mockingboard) use ROM space for I/O
  if (address < 0xD000) {
    // Access to $CFFF clears the expansion ROM select
    if (address == 0xCFFF) {
      switches_.intc8rom = false;
      activeExpansionSlot_ = 0;
    }

    // No-Slot Clock intercepts writes in $C300-$C3FF
    if (address >= 0xC300 && address < 0xC400 && noSlotClock_) {
      noSlotClock_->interceptWrite(address);
    }

    // Route writes to slot ROM space through cards
    if (address < 0xC800) {
      uint8_t slot = (address >> 8) & 0x07;
      uint8_t offset = address & 0xFF;

      if (slot >= 1 && slot <= 7 && slots_[slot - 1]) {
        slots_[slot - 1]->writeROM(offset, value);
      }
    }
    return;
  }

  // Language card area: $D000-$FFFF
  writeLanguageCard(address, value);
}

uint8_t MMU::getFloatingBusValue() {
  // The floating bus returns whatever byte the video hardware is currently
  // reading. This is determined by the current scanline and horizontal position
  // within the frame.
  //
  // Apple IIe timing:
  // - 65 cycles per scanline (25 hblank + 40 visible)
  // - 262 scanlines per frame (192 visible + 70 vertical blank)
  // - Cycles 0-24: horizontal blanking, cycles 25-64: visible display

  if (!cycleCallback_) {
    return 0x00;
  }

  uint64_t cycles = cycleCallback_();
  uint32_t frameCycle = cycles % CYCLES_PER_FRAME;
  uint32_t scanline = frameCycle / CYCLES_PER_SCANLINE;
  uint32_t hPos = frameCycle % CYCLES_PER_SCANLINE;

  // During horizontal blank (cycles 0-24), video reads from
  // unpredictable locations. Return 0 for simplicity during hblank.
  if (hPos < 25) {
    return 0x00;
  }

  // Convert to visible column (0-39)
  uint32_t col = hPos - 25;

  // During vertical blank (scanlines 192-261), return data from the
  // last few lines' worth of addresses (video wraps during vblank)
  if (scanline >= 192) {
    scanline = scanline % 192;
  }

  // Calculate memory address based on current video mode
  uint16_t address;

  if (switches_.text || !switches_.hires) {
    // Text mode or LoRes mode - reads from text page
    // Each scanline covers 8 text rows due to character height
    int textRow = scanline / 8;
    if (textRow >= 24)
      textRow = 23;

    // Text/LoRes base address
    uint16_t base = switches_.page2 ? 0x0800 : 0x0400;

    // Apple II text memory is interleaved in groups of 8 rows
    // Rows 0-7:   $000, $080, $100, $180, $200, $280, $300, $380
    // Rows 8-15:  $028, $0A8, $128, $1A8, $228, $2A8, $328, $3A8
    // Rows 16-23: $050, $0D0, $150, $1D0, $250, $2D0, $350, $3D0
    static const uint16_t rowOffsets[24] = {
        0x000, 0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380,
        0x028, 0x0A8, 0x128, 0x1A8, 0x228, 0x2A8, 0x328, 0x3A8,
        0x050, 0x0D0, 0x150, 0x1D0, 0x250, 0x2D0, 0x350, 0x3D0};

    address = base + rowOffsets[textRow] + col;
  } else {
    // HiRes mode - reads from hi-res page
    uint16_t base = switches_.page2 ? 0x4000 : 0x2000;

    // Hi-res memory is also interleaved
    // Each group of 64 lines shares a similar pattern
    int group = scanline / 64;       // 0, 1, or 2
    int lineInGroup = scanline % 64; // 0-63
    int subGroup = lineInGroup / 8;  // 0-7
    int lineInSubGroup = lineInGroup % 8;

    // Calculate offset within page
    // Lines 0,8,16,24,32,40,48,56 of each group start at group*$28
    // Plus $80 for each subgroup, plus $400 for each line within subgroup
    uint16_t offset = (group * 0x28) + (subGroup * 0x80) + (lineInSubGroup * 0x400);
    address = base + offset + col;
  }

  // Read from the appropriate memory bank
  if (switches_.store80 && switches_.page2) {
    // When 80STORE is on and PAGE2, read from aux memory for display pages
    if ((address >= 0x0400 && address < 0x0800) ||
        (switches_.hires && address >= 0x2000 && address < 0x4000)) {
      return auxRAM_[address];
    }
  }

  if (switches_.ramrd) {
    return auxRAM_[address];
  }
  return mainRAM_[address];
}

uint8_t MMU::readSoftSwitch(uint16_t address) {
  uint8_t reg = address & 0xFF;

  switch (reg) {
  // Keyboard
  case 0x00: // KEYBOARD
    if (keyboardCallback_) {
      keyboardLatch_ = keyboardCallback_();
    }
    return keyboardLatch_;

  case 0x10: { // KBDSTRB - clear keyboard strobe, return any-key-down status
    if (keyStrobeCallback_) {
      keyStrobeCallback_();
    }
    keyboardLatch_ &= 0x7F;
    // Bit 7 = any key down status, bits 0-6 = key code
    bool anyKeyDown = anyKeyDownCallback_ ? anyKeyDownCallback_() : false;
    return (anyKeyDown ? 0x80 : 0x00) | (keyboardLatch_ & 0x7F);
  }

  // Memory switches - reading returns switch state in bit 7, floating bus in bits 0-6
  case 0x11:
    return (switches_.lcram2 ? 0x80 : 0x00) | (getFloatingBusValue() & 0x7F); // RDLCBNK2
  case 0x12:
    return (switches_.lcram ? 0x80 : 0x00) | (getFloatingBusValue() & 0x7F); // RDLCRAM
  case 0x13:
    return (switches_.ramrd ? 0x80 : 0x00) | (getFloatingBusValue() & 0x7F); // RDRAMRD
  case 0x14:
    return (switches_.ramwrt ? 0x80 : 0x00) | (getFloatingBusValue() & 0x7F); // RDRAMWRT
  case 0x15:
    return (switches_.intcxrom ? 0x80 : 0x00) | (getFloatingBusValue() & 0x7F); // RDCXROM
  case 0x16:
    return (switches_.altzp ? 0x80 : 0x00) | (getFloatingBusValue() & 0x7F); // RDALTZP
  case 0x17:
    return (switches_.slotc3rom ? 0x80 : 0x00) | (getFloatingBusValue() & 0x7F); // RDC3ROM
  case 0x18:
    return (switches_.store80 ? 0x80 : 0x00) | (getFloatingBusValue() & 0x7F); // RD80STORE
  case 0x19: { // RDVBLBAR - vertical blank status
    // Bit 7 = 0 during vertical blank (scanlines 192-261), 1 during active display
    uint64_t cycles = cycleCallback_ ? cycleCallback_() : 0;
    uint32_t scanline = (cycles % CYCLES_PER_FRAME) / CYCLES_PER_SCANLINE;
    bool inVBL = (scanline >= 192);
    return (inVBL ? 0x00 : 0x80) | (getFloatingBusValue() & 0x7F);
  }
  case 0x1A:
    return (switches_.text ? 0x80 : 0x00) | (getFloatingBusValue() & 0x7F); // RDTEXT
  case 0x1B:
    return (switches_.mixed ? 0x80 : 0x00) | (getFloatingBusValue() & 0x7F); // RDMIXED
  case 0x1C:
    return (switches_.page2 ? 0x80 : 0x00) | (getFloatingBusValue() & 0x7F); // RDPAGE2
  case 0x1D:
    return (switches_.hires ? 0x80 : 0x00) | (getFloatingBusValue() & 0x7F); // RDHIRES
  case 0x1E:
    return (switches_.altCharSet ? 0x80 : 0x00) | (getFloatingBusValue() & 0x7F); // RDALTCHAR
  case 0x1F:
    return (switches_.col80 ? 0x80 : 0x00) | (getFloatingBusValue() & 0x7F); // RD80COL

  // Cassette output toggle ($C020)
  case 0x20: // CASSETTE OUT - toggle cassette output
    switches_.cassetteOut = !switches_.cassetteOut;
    return getFloatingBusValue();

  // Unused/reserved ($C021-$C02F)
  case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
  case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F:
    return getFloatingBusValue();

  // Speaker - returns floating bus
  case 0x30: // SPKR
    if (speakerCallback_) {
      speakerCallback_();
    }
    return getFloatingBusValue();

  // Unused/reserved ($C031-$C03F)
  case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37:
  case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x3E: case 0x3F:
    return getFloatingBusValue();

  // Utility strobe ($C040)
  case 0x40: // STROBE - utility strobe
    return getFloatingBusValue();

  // Reserved ($C041-$C04F)
  case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
  case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
    return getFloatingBusValue();

  // Annunciators - return floating bus
  case 0x58:
    switches_.an0 = false;
    return getFloatingBusValue();
  case 0x59:
    switches_.an0 = true;
    return getFloatingBusValue();
  case 0x5A:
    switches_.an1 = false;
    return getFloatingBusValue();
  case 0x5B:
    switches_.an1 = true;
    return getFloatingBusValue();
  case 0x5C:
    switches_.an2 = false;
    return getFloatingBusValue();
  case 0x5D:
    switches_.an2 = true;
    return getFloatingBusValue();
  case 0x5E:
    switches_.an3 = false;
    if (videoSwitchCallback_) videoSwitchCallback_();
    return getFloatingBusValue(); // AN3 OFF = DHIRES enabled
  case 0x5F:
    switches_.an3 = true;
    if (videoSwitchCallback_) videoSwitchCallback_();
    return getFloatingBusValue(); // AN3 ON = DHIRES disabled

  // Display switches - reading also sets the switch, returns floating bus
  case 0x50:
    switches_.text = false;
    if (videoSwitchCallback_) videoSwitchCallback_();
    return getFloatingBusValue(); // TXTCLR (graphics)
  case 0x51:
    switches_.text = true;
    if (videoSwitchCallback_) videoSwitchCallback_();
    return getFloatingBusValue(); // TXTSET (text)
  case 0x52:
    switches_.mixed = false;
    if (videoSwitchCallback_) videoSwitchCallback_();
    return getFloatingBusValue(); // MIXCLR
  case 0x53:
    switches_.mixed = true;
    if (videoSwitchCallback_) videoSwitchCallback_();
    return getFloatingBusValue(); // MIXSET
  case 0x54:
    switches_.page2 = false;
    if (videoSwitchCallback_) videoSwitchCallback_();
    return getFloatingBusValue(); // LOWSCR (page 1)
  case 0x55:
    switches_.page2 = true;
    if (videoSwitchCallback_) videoSwitchCallback_();
    return getFloatingBusValue(); // HISCR (page 2)
  case 0x56:
    switches_.hires = false;
    if (videoSwitchCallback_) videoSwitchCallback_();
    return getFloatingBusValue(); // LORES
  case 0x57:
    switches_.hires = true;
    if (videoSwitchCallback_) videoSwitchCallback_();
    return getFloatingBusValue(); // HIRES

  // 80-column / memory switches (IIe specific)
  // $C000-$C00F are WRITE-ONLY switches - writes modify state, reads return keyboard data
  // Per AppleWin/hardware behavior, reading any address in $C000-$C00F returns keyboard latch
  case 0x01: // 80STORE on (write-only)
  case 0x02: // RAMRD off (write-only)
  case 0x03: // RAMRD on (write-only)
  case 0x04: // RAMWRT off (write-only)
  case 0x05: // RAMWRT on (write-only)
  case 0x06: // INTCXROM off (write-only)
  case 0x07: // INTCXROM on (write-only)
  case 0x08: // ALTZP off (write-only)
  case 0x09: // ALTZP on (write-only)
  case 0x0A: // SLOTC3ROM off (write-only)
  case 0x0B: // SLOTC3ROM on (write-only)
  case 0x0C: // 80COL off (write-only)
  case 0x0D: // 80COL on (write-only)
  case 0x0E: // ALTCHAR off (write-only)
  case 0x0F: // ALTCHAR on (write-only)
    return keyboardLatch_;

  // Cassette input ($C060)
  case 0x60: // CASSETTE IN - cassette input (active high)
    // Always return low (no cassette) - bit 7 indicates cassette signal
    return getFloatingBusValue() & 0x7F;

  // Buttons (Open Apple, Closed Apple, Button 2) - bit 7 = state, bits 0-6 = floating bus
  case 0x61: // PB0 / Open Apple
    if (buttonCallback_) {
      return (buttonCallback_(0) & 0x80) | (getFloatingBusValue() & 0x7F);
    }
    return getFloatingBusValue() & 0x7F;
  case 0x62: // PB1 / Closed Apple
    if (buttonCallback_) {
      return (buttonCallback_(1) & 0x80) | (getFloatingBusValue() & 0x7F);
    }
    return getFloatingBusValue() & 0x7F;
  case 0x63: // PB2 / Shift key modifier
    if (buttonCallback_) {
      return (buttonCallback_(2) & 0x80) | (getFloatingBusValue() & 0x7F);
    }
    return getFloatingBusValue() & 0x7F;

  // Paddle inputs - bit 7 = timer status, bits 0-6 = floating bus
  case 0x64: // PDL0 (joystick X)
  case 0x65: // PDL1 (joystick Y)
  case 0x66: // PDL2
  case 0x67: { // PDL3
    int paddle = reg - 0x64;
    uint64_t currentCycle = cycleCallback_ ? cycleCallback_() : 0;
    uint64_t elapsedCycles = currentCycle - paddleTriggerCycle_;
    uint64_t timerDuration = paddleValues_[paddle] * PADDLE_CYCLES_PER_UNIT;
    // Bit 7 = 1 while timer is running, 0 when expired
    bool timerRunning = (elapsedCycles < timerDuration);
    return (timerRunning ? 0x80 : 0x00) | (getFloatingBusValue() & 0x7F);
  }

  // Paddle trigger reset - starts all paddle timers
  case 0x70: // PTRIG - reset paddle timers
    paddleTriggerCycle_ = cycleCallback_ ? cycleCallback_() : 0;
    return getFloatingBusValue();

  // Reserved/unused ($C068-$C06F) - State register on IIc
  case 0x68: // STATEREG (IIc only) - on IIe returns floating bus
  case 0x69: case 0x6A: case 0x6B: case 0x6C: case 0x6D: case 0x6E: case 0x6F:
    return getFloatingBusValue();

  // Bank switch registers ($C071-$C07E) - mostly IIc/IIgs specific
  case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
  case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E:
    return getFloatingBusValue();

  // IOUDIS ($C07F) - IOU disable (IIc specific, ignored on IIe)
  case 0x7F:
    // On IIe, reading $C07F returns DHIRES status in bit 7 (same as AN3 inverted)
    return (!switches_.an3 ? 0x80 : 0x00) | (getFloatingBusValue() & 0x7F);

  // Slot I/O space: $C090-$C0FF (slots 1-7)
  // Each slot gets 16 bytes: slot N at $C080 + (N * 16)
  case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
  case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E: case 0x9F:
  case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5: case 0xA6: case 0xA7:
  case 0xA8: case 0xA9: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF:
  case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
  case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
  case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: case 0xC6: case 0xC7:
  case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: case 0xCF:
  case 0xD0: case 0xD1: case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: case 0xD7:
  case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
  case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: case 0xE6: case 0xE7:
  case 0xE8: case 0xE9: case 0xEA: case 0xEB: case 0xEC: case 0xED: case 0xEE: case 0xEF:
  case 0xF0: case 0xF1: case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: case 0xF7:
  case 0xF8: case 0xF9: case 0xFA: case 0xFB: case 0xFC: case 0xFD: case 0xFE: case 0xFF: {
    // Calculate slot number: $C090 = slot 1, $C0A0 = slot 2, etc.
    uint8_t slot = ((reg - 0x80) >> 4);
    uint8_t offset = reg & 0x0F;

    if (slot >= 1 && slot <= 7 && slots_[slot - 1]) {
      return slots_[slot - 1]->readIO(offset);
    }

    return getFloatingBusValue();
  }

  // Language card
  case 0x80:
  case 0x81:
  case 0x82:
  case 0x83:
  case 0x84:
  case 0x85:
  case 0x86:
  case 0x87:
  case 0x88:
  case 0x89:
  case 0x8A:
  case 0x8B:
  case 0x8C:
  case 0x8D:
  case 0x8E:
  case 0x8F:
    return handleLanguageCardSwitch(reg);

  default:
    // Unimplemented soft switches return floating bus value
    return getFloatingBusValue();
  }

  return 0x00;
}

uint8_t MMU::handleLanguageCardSwitch(uint8_t reg) {
  // Language card switches at $C080-$C08F
  // Bit 3: bank select (0 = bank 2, 1 = bank 1)
  // Bits 0-1 determine read source and write enable:
  //   0: Read RAM, write disabled
  //   1: Read ROM, write enabled (after 2 reads)
  //   2: Read ROM, write disabled
  //   3: Read RAM, write enabled (after 2 reads)

  bool bank2 = !(reg & 0x08);
  uint8_t op = reg & 0x03;

  // RAM vs ROM read selection
  // Pattern: 0=RAM, 1=ROM, 2=ROM, 3=RAM
  bool readRAM = (op == 0 || op == 3);

  switch (op) {
  case 0: // $C080, $C088: Read RAM, write disabled
    switches_.lcwrite = false;
    switches_.lcprewrite = false;
    break;

  case 1: // $C081, $C089: Read ROM, write enable on second read
    if (switches_.lcprewrite) {
      switches_.lcwrite = true;
    }
    switches_.lcprewrite = true;
    break;

  case 2: // $C082, $C08A: Read ROM, write disabled
    switches_.lcwrite = false;
    switches_.lcprewrite = false;
    break;

  case 3: // $C083, $C08B: Read RAM, write enable on second read
    if (switches_.lcprewrite) {
      switches_.lcwrite = true;
    }
    switches_.lcprewrite = true;
    break;
  }

  switches_.lcram = readRAM;
  switches_.lcram2 = bank2;

  return getFloatingBusValue();
}

void MMU::writeSoftSwitch(uint16_t address, uint8_t value) {
  uint8_t reg = address & 0xFF;

  switch (reg) {
  // Keyboard strobe
  case 0x10:
    if (keyStrobeCallback_) {
      keyStrobeCallback_();
    }
    keyboardLatch_ &= 0x7F;
    break;

  // Speaker
  case 0x30:
    if (speakerCallback_) {
      speakerCallback_();
    }
    break;

  // 80STORE
  case 0x00:
    switches_.store80 = false;
    if (videoSwitchCallback_) videoSwitchCallback_();
    break;
  case 0x01:
    switches_.store80 = true;
    if (videoSwitchCallback_) videoSwitchCallback_();
    break;

  // Memory switches
  case 0x02:
    switches_.ramrd = false;
    break;
  case 0x03:
    switches_.ramrd = true;
    break;
  case 0x04:
    switches_.ramwrt = false;
    break;
  case 0x05:
    switches_.ramwrt = true;
    break;
  case 0x06:
    switches_.intcxrom = false;
    break;
  case 0x07:
    switches_.intcxrom = true;
    break;
  case 0x08:
    switches_.altzp = false;
    break;
  case 0x09:
    switches_.altzp = true;
    break;
  case 0x0A:
    switches_.slotc3rom = false;
    break;
  case 0x0B:
    switches_.slotc3rom = true;
    break;
  case 0x0C:
    switches_.col80 = false;
    if (videoSwitchCallback_) videoSwitchCallback_();
    break;
  case 0x0D:
    switches_.col80 = true;
    if (videoSwitchCallback_) videoSwitchCallback_();
    break;
  case 0x0E:
    switches_.altCharSet = false;
    if (videoSwitchCallback_) videoSwitchCallback_();
    break;
  case 0x0F:
    switches_.altCharSet = true;
    if (videoSwitchCallback_) videoSwitchCallback_();
    break;

  // Display switches
  case 0x50:
    switches_.text = false;
    if (videoSwitchCallback_) videoSwitchCallback_();
    break;
  case 0x51:
    switches_.text = true;
    if (videoSwitchCallback_) videoSwitchCallback_();
    break;
  case 0x52:
    switches_.mixed = false;
    if (videoSwitchCallback_) videoSwitchCallback_();
    break;
  case 0x53:
    switches_.mixed = true;
    if (videoSwitchCallback_) videoSwitchCallback_();
    break;
  case 0x54:
    switches_.page2 = false;
    if (videoSwitchCallback_) videoSwitchCallback_();
    break;
  case 0x55:
    switches_.page2 = true;
    if (videoSwitchCallback_) videoSwitchCallback_();
    break;
  case 0x56:
    switches_.hires = false;
    if (videoSwitchCallback_) videoSwitchCallback_();
    break;
  case 0x57:
    switches_.hires = true;
    if (videoSwitchCallback_) videoSwitchCallback_();
    break;

  // Paddle trigger (write also triggers)
  case 0x70:
    paddleTriggerCycle_ = cycleCallback_ ? cycleCallback_() : 0;
    break;

  // Annunciators
  case 0x58:
    switches_.an0 = false;
    break;
  case 0x59:
    switches_.an0 = true;
    break;
  case 0x5A:
    switches_.an1 = false;
    break;
  case 0x5B:
    switches_.an1 = true;
    break;
  case 0x5C:
    switches_.an2 = false;
    break;
  case 0x5D:
    switches_.an2 = true;
    break;
  case 0x5E:
    switches_.an3 = false;
    if (videoSwitchCallback_) videoSwitchCallback_();
    break;
  case 0x5F:
    switches_.an3 = true;
    if (videoSwitchCallback_) videoSwitchCallback_();
    break;

  // Slot I/O space: $C090-$C0FF (slots 1-7)
  case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
  case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E: case 0x9F:
  case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5: case 0xA6: case 0xA7:
  case 0xA8: case 0xA9: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF:
  case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
  case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
  case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: case 0xC6: case 0xC7:
  case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: case 0xCF:
  case 0xD0: case 0xD1: case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: case 0xD7:
  case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
  case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: case 0xE6: case 0xE7:
  case 0xE8: case 0xE9: case 0xEA: case 0xEB: case 0xEC: case 0xED: case 0xEE: case 0xEF:
  case 0xF0: case 0xF1: case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: case 0xF7:
  case 0xF8: case 0xF9: case 0xFA: case 0xFB: case 0xFC: case 0xFD: case 0xFE: case 0xFF: {
    // Calculate slot number: $C090 = slot 1, $C0A0 = slot 2, etc.
    uint8_t slot = ((reg - 0x80) >> 4);
    uint8_t offset = reg & 0x0F;

    if (slot >= 1 && slot <= 7 && slots_[slot - 1]) {
      slots_[slot - 1]->writeIO(offset, value);
    }
    break;
  }

  // Language card - writes do NOT enable write, and reset prewrite state
  case 0x80:
  case 0x81:
  case 0x82:
  case 0x83:
  case 0x84:
  case 0x85:
  case 0x86:
  case 0x87:
  case 0x88:
  case 0x89:
  case 0x8A:
  case 0x8B:
  case 0x8C:
  case 0x8D:
  case 0x8E:
  case 0x8F:
    handleLanguageCardSwitchWrite(reg);
    break;

  default:
    break;
  }
}

void MMU::handleLanguageCardSwitchWrite(uint8_t reg) {
  // Language card soft switches on WRITE access
  // On Apple IIe, writes to LC soft switches:
  // - Do NOT count toward the "double read" requirement for write-enable
  // - ALL writes reset the prewrite state (clearing any pending write-enable)
  // - Bank and read source selection still applies
  //
  // This means LDA $C083 + STA $C083 + LDA $C083 does NOT enable writes
  // (the STA resets the counter), but INC $C083 DOES enable writes
  // (because INC does two reads before the write).

  bool bank2 = !(reg & 0x08);
  uint8_t op = reg & 0x03;

  // RAM vs ROM read selection (same as reads)
  bool readRAM = (op == 0 || op == 3);

  switch (op) {
  case 0: // $C080, $C088: Read RAM, write disabled
    switches_.lcwrite = false;
    switches_.lcprewrite = false;
    break;

  case 1: // $C081, $C089: Read ROM
    // Write resets prewrite but doesn't disable existing lcwrite
    switches_.lcprewrite = false;
    break;

  case 2: // $C082, $C08A: Read ROM, write disabled
    switches_.lcwrite = false;
    switches_.lcprewrite = false;
    break;

  case 3: // $C083, $C08B: Read RAM
    // Write resets prewrite but doesn't disable existing lcwrite
    switches_.lcprewrite = false;
    break;
  }

  switches_.lcram = readRAM;
  switches_.lcram2 = bank2;
}

uint8_t MMU::readLanguageCard(uint16_t address) {
  if (switches_.lcram) {
    // Read from RAM
    bool useAux = switches_.altzp;

    if (address < 0xE000) {
      // $D000-$DFFF
      uint16_t offset = address - 0xD000;
      if (switches_.lcram2) {
        return useAux ? auxLcBank2_[offset] : lcBank2_[offset];
      } else {
        return useAux ? auxLcBank1_[offset] : lcBank1_[offset];
      }
    } else {
      // $E000-$FFFF
      uint16_t offset = address - 0xE000;
      return useAux ? auxLcHighRAM_[offset] : lcHighRAM_[offset];
    }
  } else {
    // Read from ROM
    return systemROM_[address - 0xC000];
  }
}

void MMU::writeLanguageCard(uint16_t address, uint8_t value) {
  if (!switches_.lcwrite) {
    return; // Write not enabled
  }

  bool useAux = switches_.altzp;

  if (address < 0xE000) {
    // $D000-$DFFF
    uint16_t offset = address - 0xD000;
    if (switches_.lcram2) {
      if (useAux) {
        auxLcBank2_[offset] = value;
      } else {
        lcBank2_[offset] = value;
      }
    } else {
      if (useAux) {
        auxLcBank1_[offset] = value;
      } else {
        lcBank1_[offset] = value;
      }
    }
  } else {
    // $E000-$FFFF
    uint16_t offset = address - 0xE000;
    if (useAux) {
      auxLcHighRAM_[offset] = value;
    } else {
      lcHighRAM_[offset] = value;
    }
  }
}

} // namespace a2e
