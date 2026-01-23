#pragma once

#include "ay8910.hpp"
#include "via6522.hpp"
#include <cstdint>
#include <functional>
#include <memory>

namespace a2e {

// Mockingboard sound card emulation
// The Mockingboard has two 6522 VIA chips, each controlling one AY-3-8910 PSG
// Address space: $C400-$C4FF (slot 4)
//   $C400-$C40F: VIA 1 (left PSG)
//   $C480-$C48F: VIA 2 (right PSG)
class Mockingboard {
public:
    using IRQCallback = std::function<void()>;
    using CycleCallback = std::function<uint64_t()>;

    Mockingboard();

    // Memory-mapped I/O
    uint8_t read(uint16_t address);
    void write(uint16_t address, uint8_t value);

    // Update timers (call with CPU cycles elapsed)
    void update(int cycles);

    // Generate audio samples (call from audio callback)
    void generateSamples(float* buffer, int count, int sampleRate);

    // Callbacks
    void setIRQCallback(IRQCallback cb);
    void setCycleCallback(CycleCallback cb) { cycleCallback_ = std::move(cb); }

    // Enable/disable
    bool isEnabled() const { return enabled_; }
    void setEnabled(bool enabled) { enabled_ = enabled; }

    // Reset
    void reset();

    // State access for debugging
    const VIA6522& getVIA1() const { return via1_; }
    const VIA6522& getVIA2() const { return via2_; }
    const AY8910& getPSG1() const { return psg1_; }
    const AY8910& getPSG2() const { return psg2_; }

private:
    // Two VIA chips
    VIA6522 via1_;  // $C400-$C40F
    VIA6522 via2_;  // $C480-$C48F

    // Two PSG chips
    AY8910 psg1_;   // Connected to VIA1
    AY8910 psg2_;   // Connected to VIA2

    // Enabled state
    bool enabled_ = true;

    // Callbacks
    CycleCallback cycleCallback_;
};

} // namespace a2e
