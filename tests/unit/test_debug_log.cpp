/*
 * test_debug_log.cpp - Tests for the host-installed debug log sink
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "debug/debug_log.hpp"

#include <string>
#include <vector>

namespace {

std::vector<std::string> g_captured;

void captureSink(const char *message) { g_captured.emplace_back(message); }

// Restores the global sink so one test's installation cannot leak into the
// next — the sink is process-wide by design.
struct SinkGuard {
  SinkGuard() { g_captured.clear(); }
  ~SinkGuard() { a2e::setDebugLogSink(nullptr); g_captured.clear(); }
};

} // namespace

TEST_CASE("no sink installed", "[debug_log]") {
  SinkGuard guard;

  REQUIRE_FALSE(a2e::hasDebugLogSink());

  // Must be safe, and must not crash on format specifiers it never expands.
  a2e::debugLog("dropped %d", 42);
  REQUIRE(g_captured.empty());
}

TEST_CASE("installed sink receives formatted messages", "[debug_log]") {
  SinkGuard guard;
  a2e::setDebugLogSink(captureSink);

  REQUIRE(a2e::hasDebugLogSink());

  a2e::debugLog("VIA%d: ctrl %d->%d ORA=0x%X", 1, 0, 7, 0xAB);

  REQUIRE(g_captured.size() == 1);
  REQUIRE(g_captured[0] == "VIA1: ctrl 0->7 ORA=0xAB");
}

TEST_CASE("messages arrive in order", "[debug_log]") {
  SinkGuard guard;
  a2e::setDebugLogSink(captureSink);

  a2e::debugLog("first");
  a2e::debugLog("second");
  a2e::debugLog("third");

  REQUIRE(g_captured == std::vector<std::string>{"first", "second", "third"});
}

TEST_CASE("sink can be replaced and cleared", "[debug_log]") {
  SinkGuard guard;

  a2e::setDebugLogSink(captureSink);
  a2e::debugLog("kept");

  a2e::setDebugLogSink(nullptr);
  a2e::debugLog("dropped");

  REQUIRE(a2e::hasDebugLogSink() == false);
  REQUIRE(g_captured == std::vector<std::string>{"kept"});
}

TEST_CASE("a null format string is ignored", "[debug_log]") {
  SinkGuard guard;
  a2e::setDebugLogSink(captureSink);

  a2e::debugLog(nullptr);

  REQUIRE(g_captured.empty());
}

TEST_CASE("over-long messages are truncated, not overflowed", "[debug_log]") {
  SinkGuard guard;
  a2e::setDebugLogSink(captureSink);

  // The sink formats into a fixed 512-byte buffer; this is deliberately longer.
  const std::string huge(2000, 'x');
  a2e::debugLog("%s", huge.c_str());

  REQUIRE(g_captured.size() == 1);
  REQUIRE(g_captured[0].size() < huge.size());
  REQUIRE(g_captured[0].find_first_not_of('x') == std::string::npos);
}
