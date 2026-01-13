#include "mmu.hpp"
#include "../disk/disk2.hpp"
#include <cstring>

namespace a2e {

MMU::MMU() { reset(); }

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
}

void MMU::loadROM(const uint8_t *systemRom, size_t systemSize,
                  const uint8_t *charRom, size_t charSize,
                  const uint8_t *diskRom, size_t diskSize) {
  if (systemRom && systemSize > 0) {
    std::memcpy(systemROM_.data(), systemRom,
                std::min(systemSize, systemROM_.size()));
  }
  if (charRom && charSize > 0) {
    std::memcpy(charROM_.data(), charRom, std::min(charSize, charROM_.size()));
  }
  if (diskRom && diskSize > 0) {
    std::memcpy(diskROM_.data(), diskRom, std::min(diskSize, diskROM_.size()));
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

uint8_t MMU::read(uint16_t address) {
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
      if (switches_.store80 && switches_.page2) {
        return auxRAM_[address];
      }
      if (switches_.ramrd) {
        return auxRAM_[address];
      }
      return mainRAM_[address];
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
      if (switches_.store80 && switches_.page2 && switches_.hires) {
        return auxRAM_[address];
      }
      if (switches_.ramrd) {
        return auxRAM_[address];
      }
      return mainRAM_[address];
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
    // Slot 3 special handling
    if (address >= 0xC300 && address < 0xC400) {
      if (!switches_.slotc3rom) {
        // Use internal ROM
        return systemROM_[address - 0xC000];
      }
      // Use slot 3 ROM (if any)
      return systemROM_[address - 0xC000];
    }

    // Slot 6 (Disk II): $C600-$C6FF
    if (address >= 0xC600 && address < 0xC700) {
      return diskROM_[address - 0xC600];
    }

    // Other slots - use internal ROM or slot ROM based on INTCXROM
    if (switches_.intcxrom) {
      return systemROM_[address - 0xC000];
    }

    // Default to internal ROM
    return systemROM_[address - 0xC000];
  }

  // Language card area: $D000-$FFFF
  return readLanguageCard(address);
}

void MMU::write(uint16_t address, uint8_t value) {
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
      if (switches_.store80 && switches_.page2) {
        auxRAM_[address] = value;
        return;
      }
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
      if (switches_.store80 && switches_.page2 && switches_.hires) {
        auxRAM_[address] = value;
        return;
      }
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

  // Slot ROM space: $C100-$CFFF - writes are ignored
  if (address < 0xD000) {
    return;
  }

  // Language card area: $D000-$FFFF
  writeLanguageCard(address, value);
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

  case 0x10: // KBDSTRB - clear keyboard strobe
    if (keyStrobeCallback_) {
      keyStrobeCallback_();
    }
    keyboardLatch_ &= 0x7F;
    return keyboardLatch_;

  // Memory switches - reading returns switch state in bit 7
  case 0x11:
    return switches_.lcram2 ? 0x80 : 0x00; // RDLCBNK2
  case 0x12:
    return switches_.lcram ? 0x80 : 0x00; // RDLCRAM
  case 0x13:
    return switches_.ramrd ? 0x80 : 0x00; // RDRAMRD
  case 0x14:
    return switches_.ramwrt ? 0x80 : 0x00; // RDRAMWRT
  case 0x15:
    return switches_.intcxrom ? 0x80 : 0x00; // RDCXROM
  case 0x16:
    return switches_.altzp ? 0x80 : 0x00; // RDALTZP
  case 0x17:
    return switches_.slotc3rom ? 0x80 : 0x00; // RDC3ROM
  case 0x18:
    return switches_.store80 ? 0x80 : 0x00; // RD80STORE
  case 0x19:
    return 0x00; // RDVBLBAR (vertical blank) - TODO
  case 0x1A:
    return switches_.text ? 0x80 : 0x00; // RDTEXT
  case 0x1B:
    return switches_.mixed ? 0x80 : 0x00; // RDMIXED
  case 0x1C:
    return switches_.page2 ? 0x80 : 0x00; // RDPAGE2
  case 0x1D:
    return switches_.hires ? 0x80 : 0x00; // RDHIRES
  case 0x1E:
    return switches_.altCharSet ? 0x80 : 0x00; // RDALTCHAR
  case 0x1F:
    return switches_.col80 ? 0x80 : 0x00; // RD80COL

  // Speaker
  case 0x30: // SPKR
    if (speakerCallback_) {
      speakerCallback_();
    }
    return 0x00;

  // Annunciators
  case 0x58:
    switches_.an0 = false;
    return 0x00;
  case 0x59:
    switches_.an0 = true;
    return 0x00;
  case 0x5A:
    switches_.an1 = false;
    return 0x00;
  case 0x5B:
    switches_.an1 = true;
    return 0x00;
  case 0x5C:
    switches_.an2 = false;
    return 0x00;
  case 0x5D:
    switches_.an2 = true;
    return 0x00;
  case 0x5E:
    switches_.an3 = false;
    return 0x00; // Also DHIRES off
  case 0x5F:
    switches_.an3 = true;
    return 0x00; // Also DHIRES on

  // Display switches - reading also sets the switch
  case 0x50:
    switches_.text = false;
    return 0x00; // TXTCLR (graphics)
  case 0x51:
    switches_.text = true;
    return 0x00; // TXTSET (text)
  case 0x52:
    switches_.mixed = false;
    return 0x00; // MIXCLR
  case 0x53:
    switches_.mixed = true;
    return 0x00; // MIXSET
  case 0x54:
    switches_.page2 = false;
    return 0x00; // LOWSCR (page 1)
  case 0x55:
    switches_.page2 = true;
    return 0x00; // HISCR (page 2)
  case 0x56:
    switches_.hires = false;
    return 0x00; // LORES
  case 0x57:
    switches_.hires = true;
    return 0x00; // HIRES

  // 80-column / memory switches (IIe specific)
  case 0x02:
    switches_.ramrd = false;
    return 0x00; // RDMAINRAM
  case 0x03:
    switches_.ramrd = true;
    return 0x00; // RDCARDRAM
  case 0x04:
    switches_.ramwrt = false;
    return 0x00; // WRMAINRAM
  case 0x05:
    switches_.ramwrt = true;
    return 0x00; // WRCARDRAM
  case 0x06:
    switches_.intcxrom = false;
    return 0x00; // SETINTCXROM
  case 0x07:
    switches_.intcxrom = true;
    return 0x00; // SETSLOTCXROM
  case 0x08:
    switches_.altzp = false;
    return 0x00; // SETSTDZP
  case 0x09:
    switches_.altzp = true;
    return 0x00; // SETALTZP
  case 0x0A:
    switches_.slotc3rom = false;
    return 0x00; // SETINTC3ROM
  case 0x0B:
    switches_.slotc3rom = true;
    return 0x00; // SETSLOTC3ROM
  case 0x0C:
    switches_.col80 = false;
    return 0x00; // CLR80COL
  case 0x0D:
    switches_.col80 = true;
    return 0x00; // SET80COL
  case 0x0E:
    switches_.altCharSet = false;
    return 0x00; // CLRALTCHAR
  case 0x0F:
    switches_.altCharSet = true;
    return 0x00; // SETALTCHAR

  // Disk II controller (slot 6): $C0E0-$C0EF
  case 0xE0:
  case 0xE1:
  case 0xE2:
  case 0xE3:
  case 0xE4:
  case 0xE5:
  case 0xE6:
  case 0xE7:
  case 0xE8:
  case 0xE9:
  case 0xEA:
  case 0xEB:
  case 0xEC:
  case 0xED:
  case 0xEE:
  case 0xEF:
    if (diskController_) {
      return diskController_->read(reg - 0xE0);
    }
    return 0x00;

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
    return 0x00;
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

  return 0x00;
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
    break;
  case 0x01:
    switches_.store80 = true;
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
    break;
  case 0x0D:
    switches_.col80 = true;
    break;
  case 0x0E:
    switches_.altCharSet = false;
    break;
  case 0x0F:
    switches_.altCharSet = true;
    break;

  // Display switches
  case 0x50:
    switches_.text = false;
    break;
  case 0x51:
    switches_.text = true;
    break;
  case 0x52:
    switches_.mixed = false;
    break;
  case 0x53:
    switches_.mixed = true;
    break;
  case 0x54:
    switches_.page2 = false;
    break;
  case 0x55:
    switches_.page2 = true;
    break;
  case 0x56:
    switches_.hires = false;
    break;
  case 0x57:
    switches_.hires = true;
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
    break;
  case 0x5F:
    switches_.an3 = true;
    break;

  // Disk II controller (slot 6)
  case 0xE0:
  case 0xE1:
  case 0xE2:
  case 0xE3:
  case 0xE4:
  case 0xE5:
  case 0xE6:
  case 0xE7:
  case 0xE8:
  case 0xE9:
  case 0xEA:
  case 0xEB:
  case 0xEC:
  case 0xED:
  case 0xEE:
  case 0xEF:
    if (diskController_) {
      diskController_->write(reg - 0xE0, value);
    }
    break;

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
  // Writes have the same effect as reads EXCEPT:
  // - Writes do NOT count toward the "double access" requirement for write-enable
  // - Writes reset the pre-write state (clearing any pending write-enable)
  //
  // Reference: "Any in-between write will reset the counter and require two more READS"

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
    // Write does NOT enable lcwrite, and resets prewrite
    switches_.lcprewrite = false;
    break;

  case 2: // $C082, $C08A: Read ROM, write disabled
    switches_.lcwrite = false;
    switches_.lcprewrite = false;
    break;

  case 3: // $C083, $C08B: Read RAM
    // Write does NOT enable lcwrite, and resets prewrite
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
