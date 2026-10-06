/*
 * test_profiler.cpp - The profiler's call tree, timeline and per-line costs
 *
 * Fed by hand, one step at a time, as a machine feeds it: where the
 * instruction was, its opcode, where the processor went, the stack either
 * side, whether it was an interrupt's entry, and the time it took. The
 * machines' own wiring is pinned in test_emulator_debug.cpp and
 * test_iigs_debug.cpp.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "debug/profiler.hpp"

using namespace a2e;

namespace {

constexpr uint8_t JSR = 0x20, RTS = 0x60, NOP = 0xEA, PHA = 0x48, PLA = 0x68, JMP = 0x4C, RTI = 0x40;

// A program's steps, with the stack pointer kept as the processor would.
struct Feed {
  Profiler p;
  uint16_t sp = 0x01FF;

  explicit Feed(Profiler::Isa isa = Profiler::Isa::Mos6502) : p(isa) { p.setEnabled(true); }

  void step(uint32_t pc, uint8_t opcode, uint32_t next, int spDelta, double time) {
    const uint16_t before = sp;
    sp = static_cast<uint16_t>(sp + spDelta);
    p.record(pc, opcode, next, before, sp, false, time);
  }
  void interrupt(uint32_t pc, uint32_t handler, double time) {
    const uint16_t before = sp;
    sp = static_cast<uint16_t>(sp - 3);
    p.record(pc, 0, handler, before, sp, true, time);
  }
  void call(uint32_t pc, uint32_t target) { step(pc, JSR, target, -2, 6); }
  void ret(uint32_t pc, uint32_t to) { step(pc, RTS, to, +2, 6); }
  void nop(uint32_t pc, double time = 2) { step(pc, NOP, pc + 1, 0, time); }

  // The node for a path of functions from the top level, or -1.
  int find(std::initializer_list<uint32_t> path) const {
    int at = 0;
    for (uint32_t f : path) {
      int found = -1;
      for (size_t i = 0; i < p.nodes().size(); i++) {
        if (p.nodes()[i].parent == at && p.nodes()[i].function == f) found = static_cast<int>(i);
      }
      if (found < 0) return -1;
      at = found;
    }
    return at;
  }
  const Profiler::Node &node(std::initializer_list<uint32_t> path) const {
    const int n = find(path);
    REQUIRE(n >= 0);
    return p.nodes()[static_cast<size_t>(n)];
  }
};

} // namespace

TEST_CASE("A call and its return charge the routine, and the call to the caller", "[profiler]") {
  Feed f;
  f.call(0x2000, 0x3000); // the JSR's own time is the caller's
  f.nop(0x3000, 2);
  f.nop(0x3001, 3);
  f.ret(0x3002, 0x2003);  // the RTS's is the routine's
  f.nop(0x2003);

  CHECK(f.node({}).self == 6 + 2);
  const auto &sub = f.node({0x3000});
  CHECK(sub.self == 2 + 3 + 6);
  CHECK(sub.calls == 1);
  CHECK(sub.entry == Profiler::Entry::Call);
  CHECK(f.p.depth() == 0);
  CHECK(f.p.totalTime() == 6 + 2 + 3 + 6 + 2);
  CHECK(f.p.instructions() == 5);
}

TEST_CASE("The same routine from two callers is two paths", "[profiler]") {
  Feed f;
  f.call(0x2000, 0x3000);
  f.call(0x3000, 0x4000);
  f.ret(0x4000, 0x3003);
  f.ret(0x3003, 0x2003);
  f.call(0x2003, 0x4000);
  f.ret(0x4000, 0x2006);
  f.call(0x2006, 0x4000);
  f.ret(0x4000, 0x2009);

  CHECK(f.node({0x3000, 0x4000}).calls == 1);
  CHECK(f.node({0x4000}).calls == 2);
}

TEST_CASE("An RTS used as a jump inside a routine does not end it", "[profiler]") {
  Feed f;
  f.call(0x2000, 0x3000);
  f.step(0x3000, PHA, 0x3001, -1, 3);
  f.step(0x3001, PHA, 0x3002, -1, 3);
  f.step(0x3002, RTS, 0x5000, +2, 6); // to $5000, the address pushed plus one
  f.nop(0x5000);
  CHECK(f.p.depth() == 1);
  CHECK(f.p.nodes()[f.p.currentNode()].function == 0x3000);
  f.ret(0x5001, 0x2003);
  CHECK(f.p.depth() == 0);
}

TEST_CASE("A routine that discards its return address ends where the stack says", "[profiler]") {
  Feed f;
  f.call(0x2000, 0x3000);
  f.call(0x3000, 0x4000);
  f.step(0x4000, PLA, 0x4001, +1, 4);
  CHECK(f.p.depth() == 2); // one byte of the return address is still there
  f.step(0x4001, PLA, 0x4002, +1, 4);
  CHECK(f.p.depth() == 1); // the call to $4000 is over
  f.step(0x4002, JMP, 0x3010, 0, 3);
  f.nop(0x3010);
  CHECK(f.p.nodes()[f.p.currentNode()].function == 0x3000);
}

TEST_CASE("A stack reset ends every call", "[profiler]") {
  Feed f;
  f.call(0x2000, 0x3000);
  f.call(0x3000, 0x4000);
  f.step(0x4000, 0x9A /* TXS */, 0x4001, 0x01FF - f.sp, 2);
  CHECK(f.p.depth() == 0);
  CHECK(f.p.currentNode() == 0);
}

TEST_CASE("An interrupt is a call to its handler, entry included", "[profiler]") {
  Feed f;
  f.nop(0x2000);
  f.interrupt(0x2001, 0xFA40, 7);
  f.nop(0xFA40, 2);
  f.step(0xFA41, RTI, 0x2001, +3, 6);
  f.nop(0x2001);

  const auto &irq = f.node({0xFA40});
  CHECK(irq.entry == Profiler::Entry::Interrupt);
  CHECK(irq.self == 7 + 2 + 6);
  CHECK(irq.calls == 1);
  CHECK(f.p.depth() == 0);
  // The entry's time is put at the handler, and is not an execution of it.
  CHECK(f.p.timeAt(0xFA40) == 7 + 2);
  CHECK(f.p.executionsAt(0xFA40) == 1);
  CHECK(f.p.executionsAt(0x2001) == 1);
}

TEST_CASE("Which opcodes call depends on the processor", "[profiler]") {
  Feed c02(Profiler::Isa::Mos6502);
  c02.step(0x2000, 0xFC, 0x2003, 0, 4); // a NOP on a 65C02
  CHECK(c02.p.depth() == 0);

  Feed w(Profiler::Isa::W65816);
  w.step(0x002000, 0x22, 0x014000, -3, 8); // JSL
  CHECK(w.node({0x014000}).calls == 1);
  w.step(0x014000, 0x6B /* RTL */, 0x002004, +3, 6);
  CHECK(w.p.depth() == 0);
  w.step(0x002004, 0xFC, 0x005000, -2, 8); // JSR (a,X)
  CHECK(w.node({0x005000}).calls == 1);
}

TEST_CASE("Each frame keeps what each path took in it", "[profiler]") {
  Feed f;
  f.nop(0x2000, 10);
  f.call(0x2001, 0x3000);
  f.nop(0x3000, 4);
  f.p.endFrame();
  f.nop(0x3001, 5);
  f.ret(0x3002, 0x2004);
  f.p.endFrame();

  std::vector<Profiler::Frame> frames;
  f.p.framesFrom(0, frames);
  REQUIRE(frames.size() == 2);
  CHECK(frames[0].index == 0);
  CHECK(frames[0].time == 10 + 6 + 4);
  CHECK(frames[1].time == 5 + 6);

  auto sumOf = [](const Profiler::Frame &fr) {
    double sum = 0;
    for (const auto &s : fr.samples) sum += s.self;
    return sum;
  };
  CHECK(sumOf(frames[0]) == frames[0].time);
  CHECK(sumOf(frames[1]) == frames[1].time);

  const int sub = f.find({0x3000});
  bool calledInFirst = false;
  for (const auto &s : frames[0].samples) calledInFirst |= s.node == static_cast<uint32_t>(sub) && s.calls == 1;
  CHECK(calledInFirst);

  frames.clear();
  f.p.framesFrom(1, frames);
  REQUIRE(frames.size() == 1);
  CHECK(frames[0].index == 1);
}

TEST_CASE("The hottest lines come first, and banks are kept apart", "[profiler]") {
  Feed f(Profiler::Isa::W65816);
  f.nop(0x012000, 9);
  f.nop(0x002000, 2);
  f.nop(0x012000, 9);
  f.nop(0x002001, 5);
  const auto hot = f.p.hottest(2);
  REQUIRE(hot.size() == 2);
  CHECK(hot[0].address == 0x012000);
  CHECK(hot[0].time == 18);
  CHECK(hot[0].executions == 2);
  CHECK(hot[1].address == 0x002001);
  CHECK(f.p.timeAt(0x002000) == 2);
}

TEST_CASE("Clearing forgets everything and the tree starts again", "[profiler]") {
  Feed f;
  f.call(0x2000, 0x3000);
  f.p.endFrame();
  f.p.clear();
  CHECK(f.p.nodes().size() == 1);
  CHECK(f.p.depth() == 0);
  CHECK(f.p.totalTime() == 0);
  CHECK(f.p.framesCompleted() == 0);
  CHECK(f.p.timeAt(0x2000) == 0);
  CHECK(f.p.enabled());
}
