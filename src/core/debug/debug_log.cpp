/*
 * debug_log.cpp - Host-installed debug log sink for the emulation core
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "debug_log.hpp"

#include <cstdarg>
#include <cstdio>

namespace a2e {

namespace {
DebugLogFn g_sink = nullptr;

// Debug lines are short register traces. Anything longer is truncated rather
// than heap-allocated: this can be called from inside the CPU loop.
constexpr int MAX_LOG_LINE = 512;
} // namespace

void setDebugLogSink(DebugLogFn sink) { g_sink = sink; }

bool hasDebugLogSink() { return g_sink != nullptr; }

void debugLog(const char *format, ...) {
  if (!g_sink || !format) return;

  char buffer[MAX_LOG_LINE];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  g_sink(buffer);
}

} // namespace a2e
