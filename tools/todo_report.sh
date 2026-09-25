#!/usr/bin/env bash
# Where are the remaining TODOs?
#
#   tools/todo_report.sh          counts per milestone
#   tools/todo_report.sh M3       every M3 TODO site (file:line: what)
#
# Counts STRATA_TODO(...) call sites (the code you must write) and
# "TODO(Mx...)" comments (design questions, C++ exercises) separately.
# A milestone is done when its STRATA_TODO count is 0.
set -euo pipefail
cd "$(dirname "$0")/.."

if ! command -v rg >/dev/null; then
  echo "needs ripgrep (brew install ripgrep / apt install ripgrep)"; exit 2
fi

if [[ $# -eq 1 ]]; then
  m="$1"
  echo "== STRATA_TODO(\"$m\", ...) call sites"
  rg -n --no-heading -U -o -r '$1' "STRATA_TODO(?:_NOEXCEPT)?\(\"$m\",\s*\"([^\"]*)\"" src include || true
  echo
  echo "== TODO($m...) comments (design / C++ exercises)"
  rg -n --no-heading "TODO\($m[,)-]" src include tests bench || true
  exit 0
fi

printf '%-6s %12s %12s\n' milestone code-sites comments
for m in M1 M2 M3 M4 M5 M6 M7 M8; do
  code=$({ rg -c "STRATA_TODO(_NOEXCEPT)?\(\"$m\"" src include 2>/dev/null || true; } | awk -F: '{s+=$2} END {print s+0}')
  comments=$({ rg -c "TODO\($m[,)-]" src include tests bench 2>/dev/null || true; } | awk -F: '{s+=$2} END {print s+0}')
  printf '%-6s %12d %12d\n' "$m" "$code" "$comments"
done
