/*
 * machine_debug.cpp - The parts of debugging that are not about one processor
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "machine_debug.hpp"

#include <utility>

namespace a2e {

// ============================================================================
// Execution breakpoints
// ============================================================================

void MachineDebug::addBreakpoint(uint32_t address) {
  breakpoints_.insert(address & 0xFFFFFF);
}

void MachineDebug::removeBreakpoint(uint32_t address) {
  address &= 0xFFFFFF;
  breakpoints_.erase(address);
  disabledBreakpoints_.erase(address);
  if (breakpointHit_ && breakpointAddress_ == address) breakpointHit_ = false;
}

void MachineDebug::enableBreakpoint(uint32_t address, bool enabled) {
  address &= 0xFFFFFF;
  if (enabled) {
    disabledBreakpoints_.erase(address);
  } else {
    disabledBreakpoints_.insert(address);
    if (breakpointHit_ && breakpointAddress_ == address) breakpointHit_ = false;
  }
}

void MachineDebug::clearBreakpoints() {
  breakpoints_.clear();
  disabledBreakpoints_.clear();
  pcRanges_.clear();
  spRanges_.clear();
  breakpointHit_ = false;
  stackHit_ = false;
}

void MachineDebug::setTempBreakpoint(uint32_t address) {
  tempAddress_ = address & 0xFFFFFF;
  tempActive_ = true;
  tempHit_ = false;
}

void MachineDebug::clearTempBreakpoint() {
  tempActive_ = false;
  tempAddress_ = 0;
  tempHit_ = false;
}

// ============================================================================
// Ranges that fire on entry
// ============================================================================

namespace {
template <typename Ranges>
auto findRange(Ranges &ranges, uint32_t low) {
  for (auto it = ranges.begin(); it != ranges.end(); ++it) {
    if (it->low == low) return it;
  }
  return ranges.end();
}
} // namespace

void MachineDebug::addBreakpointRange(uint32_t start, uint32_t end) {
  start &= 0xFFFFFF;
  end &= 0xFFFFFF;
  if (end < start) std::swap(start, end);
  removeBreakpointRange(start);
  pcRanges_.push_back({start, end, true, false, false});
}

void MachineDebug::removeBreakpointRange(uint32_t start) {
  auto it = findRange(pcRanges_, start & 0xFFFFFF);
  if (it != pcRanges_.end()) pcRanges_.erase(it);
}

void MachineDebug::enableBreakpointRange(uint32_t start, bool enabled) {
  auto it = findRange(pcRanges_, start & 0xFFFFFF);
  if (it != pcRanges_.end()) it->enabled = enabled;
}

void MachineDebug::addStackBreakpoint(uint32_t low, uint32_t high) {
  low &= 0xFFFF;
  high &= 0xFFFF;
  if (high < low) std::swap(low, high);
  removeStackBreakpoint(low);
  spRanges_.push_back({low, high, true, false, false});
}

void MachineDebug::removeStackBreakpoint(uint32_t low) {
  auto it = findRange(spRanges_, low & 0xFFFF);
  if (it != spRanges_.end()) spRanges_.erase(it);
  if (stackHit_ && stackHitLow_ == (low & 0xFFFF)) stackHit_ = false;
}

void MachineDebug::enableStackBreakpoint(uint32_t low, bool enabled) {
  auto it = findRange(spRanges_, low & 0xFFFF);
  if (it != spRanges_.end()) it->enabled = enabled;
}

MachineDebug::EntryRange *
MachineDebug::entered(std::vector<EntryRange> &ranges, uint32_t value) {
  EntryRange *hit = nullptr;
  for (EntryRange &range : ranges) {
    const bool inside = value >= range.low && value <= range.high;
    const bool entering = inside && !range.inside && range.primed;
    range.inside = inside;
    range.primed = true;
    if (entering && range.enabled && !hit) hit = &range;
  }
  return hit;
}

bool MachineDebug::shouldBreakBefore(uint32_t pc, uint32_t sp) {
  pc &= 0xFFFFFF;
  sp &= 0xFFFF;

  // The ranges are measured on every instruction, whatever else stops the
  // machine here, so that entering one is always relative to the instruction
  // before rather than to whenever it was last asked.
  EntryRange *pcRange = pcRanges_.empty() ? nullptr : entered(pcRanges_, pc);
  EntryRange *spRange = spRanges_.empty() ? nullptr : entered(spRanges_, sp);

  if (tempActive_ && pc == tempAddress_) {
    // Disarm first, then record the hit: clearTempBreakpoint() resets the hit
    // flag, so setting it before the call wiped it again and the host could
    // never see that a step over had landed.
    clearTempBreakpoint();
    tempHit_ = true;
    breakpointHit_ = true;
    breakpointAddress_ = pc;
    return true;
  }

  if (pcRange) {
    breakpointHit_ = true;
    breakpointAddress_ = pc;
    return true;
  }
  if (spRange) {
    stackHit_ = true;
    stackHitLow_ = spRange->low;
    return true;
  }

  if (breakpoints_.empty()) return false;

  if (skipBreakpointOnce_) {
    // Resuming from the breakpoint we are sitting on: let this one instruction
    // through, or continuing would stop again without running anything.
    skipBreakpointOnce_ = false;
    return false;
  }
  if (breakpoints_.count(pc) && !disabledBreakpoints_.count(pc)) {
    breakpointHit_ = true;
    breakpointAddress_ = pc;
    return true;
  }
  return false;
}

// ============================================================================
// Watchpoints
// ============================================================================

void MachineDebug::addWatchpoint(uint32_t start, uint32_t end,
                                 WatchpointType type) {
  watchpoints_.push_back({start & 0xFFFFFF, end & 0xFFFFFF, type, true});
}

void MachineDebug::removeWatchpoint(uint32_t start) {
  start &= 0xFFFFFF;
  for (auto it = watchpoints_.begin(); it != watchpoints_.end(); ++it) {
    if (it->start == start) {
      watchpoints_.erase(it);
      return;
    }
  }
}

void MachineDebug::clearWatchpoints() {
  watchpoints_.clear();
  watchpointHit_ = false;
}

bool MachineDebug::onRead(uint32_t address, uint8_t value) {
  if (watchpointHit_) return false;
  address &= 0xFFFFFF;
  for (const Watchpoint &wp : watchpoints_) {
    if (!wp.enabled || !(wp.type & WP_READ)) continue;
    if (address < wp.start || address > wp.end) continue;
    watchpointHit_ = true;
    watchpointAddress_ = address;
    watchpointValue_ = value;
    watchpointIsWrite_ = false;
    return true;
  }
  return false;
}

bool MachineDebug::onWrite(uint32_t address, uint8_t value) {
  if (watchpointHit_) return false;
  address &= 0xFFFFFF;
  for (const Watchpoint &wp : watchpoints_) {
    if (!wp.enabled || !(wp.type & WP_WRITE)) continue;
    if (address < wp.start || address > wp.end) continue;
    watchpointHit_ = true;
    watchpointAddress_ = address;
    watchpointValue_ = value;
    watchpointIsWrite_ = true;
    return true;
  }
  return false;
}

// ============================================================================
// Beam breakpoints
// ============================================================================

int32_t MachineDebug::addBeamBreakpoint(int16_t scanline, int16_t hPos) {
  if (beamBreakpoints_.size() >= MAX_BEAM_BREAKPOINTS) return -1;
  const int32_t id = nextBeamId_++;
  beamBreakpoints_.push_back({id, scanline, hPos, true, UINT64_MAX, -1});
  return id;
}

void MachineDebug::removeBeamBreakpoint(int32_t id) {
  for (auto it = beamBreakpoints_.begin(); it != beamBreakpoints_.end(); ++it) {
    if (it->id == id) {
      beamBreakpoints_.erase(it);
      if (beamHitId_ == id) {
        beamHit_ = false;
        beamHitId_ = -1;
      }
      return;
    }
  }
}

void MachineDebug::enableBeamBreakpoint(int32_t id, bool enabled) {
  for (BeamBreakpoint &bp : beamBreakpoints_) {
    if (bp.id == id) {
      bp.enabled = enabled;
      return;
    }
  }
}

void MachineDebug::clearBeamBreakpoints() {
  beamBreakpoints_.clear();
  beamHit_ = false;
  beamHitId_ = -1;
  beamHitScanline_ = -1;
  beamHitHPos_ = -1;
}

bool MachineDebug::shouldBreakAtBeam(uint64_t frameBaseCycle, int scanline,
                                     int hPos) {
  if (beamBreakpoints_.empty()) return false;
  const int16_t sl = static_cast<int16_t>(scanline);
  const int16_t hp = static_cast<int16_t>(hPos);

  for (BeamBreakpoint &bp : beamBreakpoints_) {
    if (!bp.enabled) continue;
    // A breakpoint that names neither a scanline nor a position matches every
    // cycle, which is not a breakpoint.
    if (bp.scanline < 0 && bp.hPos < 0) continue;
    if (bp.scanline >= 0 && sl != bp.scanline) continue;
    // At or past the named position: the beam may not be sampled on the exact
    // cycle, because an instruction takes several.
    if (bp.hPos >= 0 && hp < bp.hPos) continue;

    // One that names a scanline fires once a frame; one that names only a
    // horizontal position fires once a line.
    const bool alreadyFired =
        bp.scanline < 0
            ? (bp.lastFireFrame == frameBaseCycle && bp.lastFireScanline == sl)
            : (bp.lastFireFrame == frameBaseCycle);
    if (alreadyFired) continue;

    beamHit_ = true;
    beamHitId_ = bp.id;
    beamHitScanline_ = sl;
    beamHitHPos_ = hp;
    bp.lastFireFrame = frameBaseCycle;
    bp.lastFireScanline = sl;
    return true;
  }
  return false;
}

// ============================================================================
// Soft switch breakpoints
// ============================================================================

int32_t MachineDebug::addSwitchBreakpoint(uint32_t source, uint64_t mask,
                                          SwitchCondition condition,
                                          uint64_t value) {
  const int32_t id = nextSwitchId_++;
  switchBreakpoints_.push_back(
      {id, source, mask, condition, value & mask, true, false, 0});
  return id;
}

void MachineDebug::removeSwitchBreakpoint(int32_t id) {
  for (auto it = switchBreakpoints_.begin(); it != switchBreakpoints_.end();
       ++it) {
    if (it->id == id) {
      switchBreakpoints_.erase(it);
      if (switchHit_ && switchHitId_ == id) switchHit_ = false;
      return;
    }
  }
}

void MachineDebug::enableSwitchBreakpoint(int32_t id, bool enabled) {
  for (SwitchBreakpoint &bp : switchBreakpoints_) {
    if (bp.id == id) {
      bp.enabled = enabled;
      return;
    }
  }
}

void MachineDebug::clearSwitchBreakpoints() {
  switchBreakpoints_.clear();
  switchHit_ = false;
  switchHitId_ = -1;
}

// ============================================================================
// The trace ring
// ============================================================================

MachineDebug::TraceEntry *MachineDebug::beginTraceEntry() {
  if (!traceEnabled_) return nullptr;
  if (traceBuffer_.empty()) {
    // Ten thousand instructions, which is about ten milliseconds of a //e and
    // enough to see how the machine arrived somewhere. Allocated on first use
    // rather than at construction, because most sessions never trace.
    traceBuffer_.resize(10000);
  }
  TraceEntry *entry = &traceBuffer_[traceHead_];
  *entry = TraceEntry{};
  traceHead_ = (traceHead_ + 1) % traceBuffer_.size();
  if (traceCount_ < traceBuffer_.size()) traceCount_++;
  return entry;
}

// ============================================================================
// Hit state
// ============================================================================

void MachineDebug::clearHits() {
  breakpointHit_ = false;
  stackHit_ = false;
  tempHit_ = false;
  watchpointHit_ = false;
  beamHit_ = false;
  beamHitId_ = -1;
  switchHit_ = false;
  switchHitId_ = -1;
}

void MachineDebug::reset() {
  clearHits();
  clearTempBreakpoint();
  skipBreakpointOnce_ = false;
  beamHitScanline_ = -1;
  beamHitHPos_ = -1;
  // Every beam breakpoint is available again: they fire once per frame, and
  // the frame they last fired in is gone.
  for (BeamBreakpoint &bp : beamBreakpoints_) {
    bp.lastFireFrame = UINT64_MAX;
    bp.lastFireScanline = -1;
  }
  // A reset puts the switches back as the machine does, which is not the
  // program changing them, so each one starts again from what it finds.
  for (SwitchBreakpoint &bp : switchBreakpoints_) bp.primed = false;
}

// ============================================================================
// The beam
// ============================================================================

BeamPosition beamPosition(uint64_t cycle, const MachineTiming &timing) {
  BeamPosition out{};
  const int frameCycle = static_cast<int>(cycle % timing.cyclesPerFrame());
  out.scanline = frameCycle / timing.cyclesPerScanline;
  out.hPos = frameCycle % timing.cyclesPerScanline;
  // A scanline starts in blanking and the visible columns follow it, so a
  // beam still inside the blanking interval is on no column at all.
  out.column = out.hPos >= timing.hblankCycles ? out.hPos - timing.hblankCycles : -1;
  out.inVerticalBlank = out.scanline >= timing.visibleScanlines;
  out.inHorizontalBlank = out.hPos < timing.hblankCycles;
  return out;
}

} // namespace a2e
