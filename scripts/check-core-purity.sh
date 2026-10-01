#!/usr/bin/env bash
#
# check-core-purity.sh - Assert src/core/ has no host-platform dependencies
#
# src/core/ is the shared emulation core: pure C++ that knows nothing about the
# program hosting it. Platform glue belongs in src/bindings/. When that rule
# erodes, the core stops being portable and stops being testable outside a
# browser — the Mockingboard's EM_ASM console tracing was exactly that, and it
# meant the native test binaries silently lost the logging.
#
# Debug output now goes through a2e::debugLog(), which the host wires to
# wherever it wants (see src/core/debug/debug_log.hpp).
#
# Patterns match code, not prose, so documentation may still name the thing it
# is warning about.
#
# Written by
#  Mike Daley <michael_daley@icloud.com>

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CORE="$ROOT/src/core"
# src/host/ is held to the same rule: it is the layer every front end shares,
# the browser's bindings and the native app alike, so it may not lean on
# either of them.
HOST="$ROOT/src/host"

for dir in "$CORE" "$HOST"; do
  [ -d "$dir" ] || { echo "check-core-purity: missing $dir" >&2; exit 2; }
done

# name : extended-regex : explanation
CHECKS=(
  "Emscripten macro|__EMSCRIPTEN__|conditional compilation on the browser host"
  "Inline JavaScript|EM_ASM[[:space:]]*\(|inline JS; use a2e::debugLog() or a host callback"
  "Emscripten header|#[[:space:]]*include[[:space:]]*<emscripten|Emscripten SDK header"
  "Emscripten bind|emscripten::|Embind types leak the host into the core"
)

status=0

for check in "${CHECKS[@]}"; do
  IFS='|' read -r name pattern explanation <<< "$check"

  # --include limits the sweep to sources; -E for extended regex.
  if hits="$(grep -rnE --include='*.cpp' --include='*.hpp' --include='*.h' \
              -- "$pattern" "$CORE" "$HOST" 2>/dev/null)"; then
    echo "check-core-purity: $name found in src/core/ or src/host/ — $explanation" >&2
    echo "$hits" | sed "s|^$ROOT/|  |" >&2
    status=1
  fi
done

if [ "$status" -eq 0 ]; then
  files=$(find "$CORE" "$HOST" \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) | wc -l | tr -d ' ')
  echo "check-core-purity: OK ($files core and host files, no platform dependencies)"
fi

exit "$status"
