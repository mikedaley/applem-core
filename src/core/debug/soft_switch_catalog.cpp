/*
 * soft_switch_catalog.cpp - The soft switches a machine has, for a debugger
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "soft_switch_catalog.hpp"

#include <cstring>

namespace a2e {

namespace {

constexpr uint32_t FLAGS = 0;

SoftSwitchInfo flag(const char *key, const char *name, const char *group,
                    const char *address, const char *description, uint8_t bit,
                    bool readOnly = false) {
  return {key, name, group, address, description, FLAGS, bit, readOnly};
}

SoftSwitchInfo reg(const char *key, const char *name, uint16_t address,
                   const char *addressText, const char *description,
                   bool readOnly = false) {
  return {key, name, "Machine Registers", addressText, description, address, 0,
          readOnly};
}

} // namespace

std::vector<SoftSwitchInfo> softSwitchCatalog(const MachineProfile &machine) {
  const MachineCapabilities &caps = machine.caps;
  std::vector<SoftSwitchInfo> list;

  // A IIgs's own registers come first: they decide what the //e switches
  // below even mean — whether a write is shadowed, which side of the machine
  // a bank is on, and how fast the processor is going. They are bytes, so a
  // breakpoint on one can ask for a value under a mask.
  if (machine.family == MachineFamily::AppleIIgs) {
    list.push_back(reg("newvideo", "NEWVIDEO", 0xC029, "$C029",
                       "Super Hi-Res (bit 7), linear video (bit 6), DHR mono (bit 5)"));
    list.push_back(reg("tcolor", "TCOLOR", 0xC022, "$C022",
                       "Text foreground and background colour"));
    list.push_back(reg("border", "BORDER", 0xC034, "$C034",
                       "Border colour, low nibble (the clock has the high)"));
    list.push_back(reg("shadow", "SHADOW", 0xC035, "$C035",
                       "Shadowing off per region; a set bit is off"));
    list.push_back(reg("cyareg", "CYAREG", 0xC036, "$C036",
                       "Fast speed (bit 7), slot motor detect (bits 0-3)"));
    list.push_back(reg("statereg", "STATEREG", 0xC068, "$C068",
                       "Eight of the //e's memory switches in one byte"));
    list.push_back(reg("slotreg", "SLOTREG", 0xC02D, "$C02D",
                       "Slots answering with a card rather than the firmware"));
    list.push_back(reg("vgcint", "VGCINT", 0xC023, "$C023",
                       "VGC interrupt enables and flags"));
    list.push_back(reg("inten", "INTEN", 0xC041, "$C041",
                       "Mega II interrupt enables"));
    list.push_back(reg("intflag", "INTFLAG", 0xC046, "$C046",
                       "Mega II interrupt flags", true));
    list.push_back(reg("vertcnt", "VERTCNT", 0xC02E, "$C02E",
                       "Vertical counter, as the VGC reports it", true));
    list.push_back(reg("horizcnt", "HORIZCNT", 0xC02F, "$C02F",
                       "Horizontal counter", true));
  }

  const char *display = "Display Mode";
  list.push_back(flag("text", "TEXT", display, "$C050/51", "Text mode", 0));
  list.push_back(flag("mixed", "MIXED", display, "$C052/53", "Mixed text and graphics", 1));
  list.push_back(flag("page2", "PAGE2", display, "$C054/55", "Display page 2", 2));
  list.push_back(flag("hires", "HIRES", display, "$C056/57", "Hi-res graphics", 3));
  if (caps.has80Column) {
    list.push_back(flag("col80", "80COL", display, "$C00C/0D", "80 column mode", 4));
  }
  if (caps.hasAltCharSet) {
    list.push_back(flag("altchar", "ALTCHAR", display, "$C00E/0F", "Alternate set (MouseText)", 5));
  }
  if (caps.hasDoubleHires) {
    list.push_back(flag("dhires", "DHIRES", display, "computed",
                        "Double hi-res: HIRES, 80COL, AN3 off", 28, true));
  }

  // The auxiliary bank's switches, and the slot ROM ones beside them, are the
  // //e's MMU; a II+ has neither, and ignores the whole $C000-$C00F group.
  const char *memory = "Memory Banking";
  if (caps.hasAuxRam) {
    list.push_back(flag("store80", "80STORE", memory, "$C000/01", "PAGE2 selects aux memory", 6));
    list.push_back(flag("ramrd", "RAMRD", memory, "$C002/03", "Read from aux RAM", 7));
    list.push_back(flag("ramwrt", "RAMWRT", memory, "$C004/05", "Write to aux RAM", 8));
  }
  if (caps.hasInternalSlotRom) {
    list.push_back(flag("intcxrom", "INTCXROM", memory, "$C006/07", "Internal $Cxxx ROM", 9));
  }
  if (caps.hasAuxRam) {
    list.push_back(flag("altzp", "ALTZP", memory, "$C008/09", "Aux zero page and stack", 10));
  }
  if (caps.hasInternalSlotRom) {
    list.push_back(flag("slotc3rom", "SLOTC3ROM", memory, "$C00A/0B", "Slot 3 ROM enabled", 11));
    list.push_back(flag("intc8rom", "INTC8ROM", memory, "internal", "Internal $C800 ROM", 12));
  }

  if (caps.hasLanguageCard) {
    const char *lc = "Language Card";
    list.push_back(flag("lcram", "LCRAM", lc, "$C080-8F", "Card RAM read enabled", 13));
    list.push_back(flag("lcbank2", "LCBANK2", lc, "$C080-8F", "Bank 2 selected", 14));
    list.push_back(flag("lcwrite", "LCWRITE", lc, "$C080-8F", "Card RAM write enabled", 15));
    list.push_back(flag("lcprewrite", "LCPREWRT", lc, "$C080-8F", "Pre-write state", 16));
  }

  const char *ann = "Annunciators";
  list.push_back(flag("an0", "AN0", ann, "$C058/59", "Annunciator 0", 17));
  list.push_back(flag("an1", "AN1", ann, "$C05A/5B", "Annunciator 1", 18));
  list.push_back(flag("an2", "AN2", ann, "$C05C/5D", "Annunciator 2", 19));
  list.push_back(flag("an3", "AN3", ann, "$C05E/5F",
                      caps.hasDoubleHires ? "Annunciator 3 / DHIRES" : "Annunciator 3", 20));

  // $C019 is one of the //e's status reads, $C011-$C01F, which arrived with
  // the auxiliary bank; a II+ answers there with the keyboard.
  const char *io = "I/O Status";
  if (caps.hasAuxRam) {
    list.push_back(flag("vblbar", "VBLBAR", io, "$C019", "Vertical blank", 21, true));
  }
  if (caps.hasCassette) {
    list.push_back(flag("cassout", "CASSOUT", io, "$C020", "Cassette output", 22));
    list.push_back(flag("cassin", "CASSIN", io, "$C060", "Cassette input", 23, true));
  }

  const char *buttons = "Buttons";
  const bool apple = caps.hasOpenAppleKeys;
  list.push_back(flag("btn0", "BTN0", buttons, "$C061",
                      apple ? "Open Apple / button 0" : "Pushbutton 0", 24, true));
  list.push_back(flag("btn1", "BTN1", buttons, "$C062",
                      apple ? "Closed Apple / button 1" : "Pushbutton 1", 25, true));
  list.push_back(flag("btn2", "BTN2", buttons, "$C063", "Button 2 / Shift", 26, true));

  list.push_back(flag("keyavail", "KEYAVAIL", "Keyboard", "$C000",
                      "Key available (bit 7)", 27, true));

  if (caps.hasIOUDisable) {
    list.push_back(flag("ioudis", "IOUDIS", "Other", "$C07E/7F", "IOU access disabled", 29));
  }

  return list;
}

const SoftSwitchInfo *findSoftSwitch(const std::vector<SoftSwitchInfo> &catalog,
                                     const char *key) {
  for (const SoftSwitchInfo &entry : catalog) {
    if (std::strcmp(entry.key, key) == 0) return &entry;
  }
  return nullptr;
}

} // namespace a2e
