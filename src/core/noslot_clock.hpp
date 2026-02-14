/*
 * noslot_clock.hpp - DS1215 No-Slot Clock emulation
 *
 * The No-Slot Clock piggybacks on ROM reads at $C300-$C3FF.
 * Software writes a 64-bit recognition pattern via address bit 0,
 * then reads 64 bits of BCD time data the same way.
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <cstdint>
#include <vector>

namespace a2e {

class NoSlotClock {
public:
  NoSlotClock();

  // Called by MMU on $C300-$C3FF reads when SLOTC3ROM is off (internal ROM).
  // Returns romValue transparently unless the clock is activated.
  uint8_t interceptRead(uint16_t address, uint8_t romValue);

  // Called by MMU on $C300-$C3FF writes to reset pattern matching.
  void interceptWrite(uint16_t address);

  void reset();

  bool isEnabled() const { return enabled_; }
  void setEnabled(bool enable) { enabled_ = enable; if (!enable) reset(); }

  // State serialization
  void serialize(std::vector<uint8_t>& buf) const;
  bool deserialize(const uint8_t* data, size_t size, size_t& offset);

private:
  enum class State { IDLE, PATTERN_MATCHING, CLOCK_READ };

  void loadCurrentTime();

  bool enabled_ = false;
  State state_ = State::IDLE;
  int bitIndex_ = 0;           // Current bit position (0-63)
  uint64_t clockData_ = 0;     // 64-bit BCD time data for reading

  // DS1215 recognition pattern: 0x5CA33AC55CA33AC5
  static constexpr uint64_t RECOGNITION_PATTERN = 0x5CA33AC55CA33AC5ULL;
};

} // namespace a2e
