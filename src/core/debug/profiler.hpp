/*
 * profiler.hpp - Where a program spends its time, by routine and by line
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>

namespace a2e {

/**
 * Profiler - the time a program spends, attributed to the routines it calls
 *
 * The machine hands over every instruction it runs, and the time it took, and
 * the profiler keeps three things:
 *
 * - **A call tree.** A shadow stack follows the program's own: a JSR (and on a
 *   65816 a JSL, a JSR (a,X), a BRK or a COP) pushes a frame, and so does an
 *   interrupt the processor takes. **A frame is popped by the stack pointer,
 *   not by the instruction that returns**: once SP climbs back to where it
 *   was before the call, the call is over, however it ended. That is what
 *   copes with the things Apple II programs really do: an RTS used as a
 *   computed jump (pushed and pulled inside one routine, so no frame moves),
 *   a routine that pulls its return address to read the bytes after the JSR
 *   and pushes it back, an error handler that discards a few frames and jumps,
 *   and a program that resets the stack with TXS. Each path through the tree
 *   is a node, and a node holds the time spent in it (self) and how often it
 *   was entered; totals are summed up the tree when they are asked for, so
 *   the hot path costs one addition.
 *
 * - **A timeline.** At the end of every video frame the time each node took
 *   in that frame is kept, which is what lets a range of frames be looked at
 *   on its own, and what a frame budget is drawn from.
 *
 * - **Every address.** The time spent at each instruction and how many times
 *   it ran, in 64K pages allocated as a bank is first used, so a IIgs pays
 *   for the banks its program runs in and not for sixteen megabytes.
 *
 * **Time is the machine's 1.023MHz clock**, which a //e's processor runs on
 * and a IIgs's slow side does: the IIgs hands over the slow clock's time an
 * instruction took, fractions included, so a routine that waits on the Mega
 * II costs what it really costs and a fast one is not charged as slow. That is
 * why the counts are doubles.
 *
 * Recording starts wherever the program happens to be, with nothing on the
 * shadow stack, so the routine it was in until then is charged to the top
 * level until it returns. A program that moves its stack to somewhere higher
 * (GS/OS does, around a tool call) ends the frames below the move early;
 * their time goes to the caller. Neither is worth guessing about.
 */
class Profiler {
public:
  // The node every path starts at: code not inside any call seen.
  static constexpr uint32_t TOP_LEVEL = 0xFFFFFFFF;
  static constexpr size_t MAX_NODES = 1u << 16;
  static constexpr size_t MAX_DEPTH = 256;
  // Three minutes of frames at 60Hz; the oldest go first.
  static constexpr size_t MAX_FRAMES = 60 * 60 * 3;

  enum class Isa : uint8_t { Mos6502, W65816 };
  enum class Entry : uint8_t { TopLevel, Call, Interrupt };

  struct Node {
    uint32_t function = TOP_LEVEL; // the address the call went to
    int32_t parent = -1;
    Entry entry = Entry::TopLevel;
    double self = 0; // time spent here, on this path, not in anything it called
    uint64_t calls = 0;
  };

  // One node's share of one frame.
  struct Sample {
    uint32_t node = 0;
    float self = 0;
    uint32_t calls = 0;
  };
  struct Frame {
    uint64_t index = 0; // frames since recording began
    double time = 0;
    std::vector<Sample> samples;
  };

  struct AddressCost {
    uint32_t address = 0;
    double time = 0;
    uint32_t executions = 0;
  };

  explicit Profiler(Isa isa = Isa::Mos6502) : isa_(isa) { clear(); }

  // Which instructions are calls: a 65816 has more of them, and on a 65C02
  // the same opcodes are NOPs.
  void setIsa(Isa isa) { isa_ = isa; }

  bool enabled() const { return enabled_; }
  // Stopping keeps what was recorded; clear() forgets it.
  void setEnabled(bool on) { enabled_ = on; }

  void clear() {
    nodes_.assign(1, Node{});
    children_.clear();
    depth_ = 0;
    top_ = 0;
    for (auto &bank : banks_) bank.reset();
    time_ = 0;
    instructions_ = 0;
    frames_.clear();
    framesDone_ = 0;
    frameTime_ = 0;
    frameSelf_.assign(1, 0.0);
    frameCalls_.assign(1, 0);
    touched_.clear();
    touchedFlag_.assign(1, 0);
    touch(0);
    truncated_ = false;
  }

  /**
   * One instruction, after it ran: where it was, its opcode, where the
   * processor went next, the stack pointer either side (a 6502's with $0100
   * added, so the two processors compare alike), whether what ran was an
   * interrupt's entry rather than an instruction, and the time it took.
   */
  void record(uint32_t pc, uint8_t opcode, uint32_t pcAfter, uint16_t spBefore, uint16_t spAfter,
              bool interrupted, double time) {
    if (interrupted) {
      // The entry's cycles are the handler's, and the instruction that was
      // about to run did not, so the time is put at the handler's first line
      // without counting an execution there.
      push(pcAfter & 0xFFFFFF, spBefore, Entry::Interrupt);
      charge(time);
      bank(pcAfter)->time[pcAfter & 0xFFFF] += time;
      return;
    }

    charge(time);
    Bank *b = bank(pc);
    b->time[pc & 0xFFFF] += time;
    b->executions[pc & 0xFFFF]++;
    instructions_++;

    // A return, or anything else that climbed back past a call's stack.
    while (depth_ > 0 && spAfter >= stack_[depth_ - 1].spBefore) pop();

    switch (callKind(opcode)) {
    case Entry::Call: push(pcAfter & 0xFFFFFF, spBefore, Entry::Call); break;
    case Entry::Interrupt: push(pcAfter & 0xFFFFFF, spBefore, Entry::Interrupt); break;
    case Entry::TopLevel: break;
    }
  }

  // The machine finished a video frame.
  void endFrame() {
    Frame frame;
    frame.index = framesDone_++;
    frame.time = frameTime_;
    frame.samples.reserve(touched_.size());
    for (uint32_t n : touched_) {
      if (frameSelf_[n] > 0 || frameCalls_[n] > 0) {
        frame.samples.push_back(Sample{n, static_cast<float>(frameSelf_[n]), frameCalls_[n]});
      }
      frameSelf_[n] = 0;
      frameCalls_[n] = 0;
      touchedFlag_[n] = 0;
    }
    touched_.clear();
    touch(top_);
    frameTime_ = 0;
    frames_.push_back(std::move(frame));
    if (frames_.size() > MAX_FRAMES) frames_.pop_front();
  }

  // ===== What was recorded =====

  const std::vector<Node> &nodes() const { return nodes_; }
  // The node the processor is in now, and how deep the shadow stack is.
  uint32_t currentNode() const { return top_; }
  size_t depth() const { return depth_; }
  // Whether the tree ran out of room; paths found after that are charged to
  // the deepest node that fitted.
  bool truncated() const { return truncated_; }
  double totalTime() const { return time_; }
  uint64_t instructions() const { return instructions_; }
  uint64_t framesCompleted() const { return framesDone_; }

  // The frames kept whose index is `from` or later, appended to `out`.
  void framesFrom(uint64_t from, std::vector<Frame> &out) const {
    if (frames_.empty()) return;
    const uint64_t first = frames_.front().index;
    const size_t start = from <= first ? 0 : static_cast<size_t>(std::min<uint64_t>(from - first, frames_.size()));
    for (size_t i = start; i < frames_.size(); i++) out.push_back(frames_[i]);
  }
  uint64_t firstFrameKept() const { return frames_.empty() ? framesDone_ : frames_.front().index; }

  double timeAt(uint32_t address) const {
    const Bank *b = banks_[(address >> 16) & 0xFF].get();
    return b ? b->time[address & 0xFFFF] : 0;
  }
  uint32_t executionsAt(uint32_t address) const {
    const Bank *b = banks_[(address >> 16) & 0xFF].get();
    return b ? b->executions[address & 0xFFFF] : 0;
  }

  // The `count` addresses that took the most time, most first.
  std::vector<AddressCost> hottest(size_t count) const {
    std::vector<AddressCost> all;
    for (size_t bank = 0; bank < banks_.size(); bank++) {
      const Bank *b = banks_[bank].get();
      if (!b) continue;
      for (uint32_t i = 0; i < 0x10000; i++) {
        if (b->time[i] > 0) {
          all.push_back(AddressCost{static_cast<uint32_t>(bank << 16) | i, b->time[i], b->executions[i]});
        }
      }
    }
    const auto hotter = [](const AddressCost &a, const AddressCost &b) { return a.time > b.time; };
    if (all.size() > count) {
      std::partial_sort(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(count), all.end(), hotter);
      all.resize(count);
    } else {
      std::sort(all.begin(), all.end(), hotter);
    }
    return all;
  }

private:
  struct Bank {
    std::array<double, 0x10000> time{};
    std::array<uint32_t, 0x10000> executions{};
  };
  struct StackFrame {
    uint16_t spBefore = 0;
    uint32_t caller = 0; // the node to go back to
  };

  Entry callKind(uint8_t opcode) const {
    if (opcode == 0x20) return Entry::Call; // JSR abs
    if (opcode == 0x00) return Entry::Interrupt; // BRK
    if (isa_ == Isa::W65816) {
      if (opcode == 0x22 || opcode == 0xFC) return Entry::Call; // JSL, JSR (a,X)
      if (opcode == 0x02) return Entry::Interrupt; // COP
    }
    return Entry::TopLevel;
  }

  Bank *bank(uint32_t address) {
    std::unique_ptr<Bank> &b = banks_[(address >> 16) & 0xFF];
    if (!b) b = std::make_unique<Bank>();
    return b.get();
  }

  void charge(double time) {
    nodes_[top_].self += time;
    frameSelf_[top_] += time;
    frameTime_ += time;
    time_ += time;
  }

  void touch(uint32_t node) {
    if (touchedFlag_[node]) return;
    touchedFlag_[node] = 1;
    touched_.push_back(node);
  }

  uint32_t childOf(uint32_t parent, uint32_t function, Entry entry) {
    const uint64_t key = (static_cast<uint64_t>(parent) << 32) | function;
    if (auto it = children_.find(key); it != children_.end()) return it->second;
    if (nodes_.size() >= MAX_NODES) {
      truncated_ = true;
      return parent;
    }
    const auto index = static_cast<uint32_t>(nodes_.size());
    Node node;
    node.function = function;
    node.parent = static_cast<int32_t>(parent);
    node.entry = entry;
    nodes_.push_back(node);
    frameSelf_.push_back(0);
    frameCalls_.push_back(0);
    touchedFlag_.push_back(0);
    children_.emplace(key, index);
    return index;
  }

  void push(uint32_t function, uint16_t spBefore, Entry entry) {
    // Deeper than this is a runaway recursion or a stack the program is
    // using for something else; the SP rule still pops whatever was pushed.
    if (depth_ == MAX_DEPTH) return;
    const uint32_t child = childOf(top_, function, entry);
    nodes_[child].calls++;
    frameCalls_[child]++;
    stack_[depth_++] = StackFrame{spBefore, top_};
    top_ = child;
    touch(child);
  }

  void pop() {
    top_ = stack_[--depth_].caller;
    touch(top_);
  }

  Isa isa_;
  bool enabled_ = false;

  std::vector<Node> nodes_;
  std::unordered_map<uint64_t, uint32_t> children_;
  std::array<StackFrame, MAX_DEPTH> stack_{};
  size_t depth_ = 0;
  uint32_t top_ = 0;
  bool truncated_ = false;

  std::array<std::unique_ptr<Bank>, 256> banks_;
  double time_ = 0;
  uint64_t instructions_ = 0;

  std::deque<Frame> frames_;
  uint64_t framesDone_ = 0;
  double frameTime_ = 0;
  std::vector<double> frameSelf_;
  std::vector<uint32_t> frameCalls_;
  std::vector<uint32_t> touched_;
  std::vector<uint8_t> touchedFlag_;
};

} // namespace a2e
