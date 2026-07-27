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

[ -d "$CORE" ] || { echo "check-core-purity: missing $CORE" >&2; exit 2; }

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
              -- "$pattern" "$CORE" 2>/dev/null)"; then
    echo "check-core-purity: $name found in src/core/ — $explanation" >&2
    echo "$hits" | sed "s|^$ROOT/|  |" >&2
    status=1
  fi
done

if [ "$status" -eq 0 ]; then
  files=$(find "$CORE" \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) | wc -l | tr -d ' ')
  echo "check-core-purity: OK ($files core files, no host dependencies)"
fi

exit "$status"
