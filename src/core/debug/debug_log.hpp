/*
 * debug_log.hpp - Host-installed debug log sink for the emulation core
 *
 * The core must not know what kind of program is hosting it. Debug tracing
 * previously reached the browser console through EM_ASM, which welded the
 * emulation to Emscripten; anything else linking the core got silence, and
 * src/core/ stopped being platform-neutral.
 *
 * Instead the host installs a sink at startup and the core formats messages
 * into it. With no sink installed — unit tests, or a host that does not want
 * the output — formatting is skipped entirely, so a disabled log costs one
 * null check.
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

namespace a2e {

/** Receives one fully formatted, NUL-terminated log line (no trailing newline). */
using DebugLogFn = void (*)(const char *message);

/** Install (or clear, with nullptr) the process-wide debug log sink. */
void setDebugLogSink(DebugLogFn sink);

/** True when a sink is installed; lets callers skip expensive argument prep. */
bool hasDebugLogSink();

/** Format printf-style and forward to the sink. No-op when none is installed. */
void debugLog(const char *format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

} // namespace a2e
