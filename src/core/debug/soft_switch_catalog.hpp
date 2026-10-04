/*
 * soft_switch_catalog.hpp - The soft switches a machine has, for a debugger
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "machine/machine_profile.hpp"

#include <cstdint>
#include <vector>

namespace a2e {

/**
 * One switch or register, as a debugger shows it and breaks on it.
 *
 * There is one list, here, so every host shows the same switches with the
 * same names and a breakpoint saved by one is found by another. Which entries
 * a machine gets is decided by its profile: a II+ has no 80COL to watch, and
 * only a IIgs has NEWVIDEO.
 *
 * `source` is what MachineDebug's switch breakpoints read. A one-bit switch
 * is bit `bit` of the packed word (MachineDebug::SWITCH_FLAGS, laid out by
 * packSoftSwitchState). A register is a byte, read by peeking its I/O address,
 * which a debugger may do without disturbing the machine.
 */
struct SoftSwitchInfo {
  const char *key;         // Stable, for saving: "page2", "newvideo"
  const char *name;        // "PAGE2"
  const char *group;       // "Display Mode"
  const char *address;     // "$C054/55", as the reference manuals write it
  const char *description;
  uint32_t source;         // SWITCH_FLAGS, or the register's I/O address
  uint8_t bit;             // In the packed word; 0 for a register
  bool readOnly;           // A status the program reads and cannot set

  bool isRegister() const { return source != 0; }
  // The bits a breakpoint on the whole switch looks at.
  uint64_t mask() const { return isRegister() ? 0xFF : (1ULL << bit); }
};

/** The switches and registers this machine has, grouped and in order. */
std::vector<SoftSwitchInfo> softSwitchCatalog(const MachineProfile &machine);

/** The entry with this key on this machine, or null if it has none. */
const SoftSwitchInfo *findSoftSwitch(const std::vector<SoftSwitchInfo> &catalog,
                                     const char *key);

} // namespace a2e
