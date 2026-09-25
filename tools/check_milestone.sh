#!/usr/bin/env bash
# The objective gate for a milestone. The tutor runs this before any review;
# a milestone is not "done" until it prints PASS.
#
#   tools/check_milestone.sh M3            debug build + tests M1..M3
#   tools/check_milestone.sh M3 --asan     also under ASan+UBSan
#   tools/check_milestone.sh M7 --tsan     also under TSan (required for M3, M7)
#   tools/check_milestone.sh M4 --tidy     also clang-tidy on changed sources
#
# Gates:
#   1. no STRATA_TODO left for this milestone
#   2. builds with -Werror (strict preset: -Wconversion etc.)
#   3. ctest -L m1 .. -L mN all pass (earlier milestones are regression tests)
#   4. optional sanitizer / clang-tidy runs
set -uo pipefail
cd "$(dirname "$0")/.."

if [[ $# -lt 1 ]]; then
  echo "usage: tools/check_milestone.sh M<n> [--asan] [--tsan] [--tidy]"; exit 2
fi
M="$1"; shift
N="${M#M}"
[[ "$N" =~ ^[1-8]$ ]] || { echo "milestone must be M1..M8"; exit 2; }
command -v rg >/dev/null || { echo "needs ripgrep (brew install ripgrep / apt install ripgrep)"; exit 2; }
ASAN=0; TSAN=0; TIDY=0
for a in "$@"; do
  case "$a" in
    --asan) ASAN=1 ;; --tsan) TSAN=1 ;; --tidy) TIDY=1 ;;
    *) echo "unknown option $a"; exit 2 ;;
  esac
done

FAILED=()
step() { printf '\n\033[1m== %s\033[0m\n' "$*"; }
ok()   { printf '\033[32mok\033[0m  %s\n' "$*"; }
bad()  { printf '\033[31mFAIL\033[0m %s\n' "$*"; FAILED+=("$*"); }

labels() { local l=""; for ((i = 1; i <= N; i++)); do l+="m$i|"; done; echo "^(${l%|})\$"; }

step "1. remaining STRATA_TODO($M) sites"
left=$({ rg -c "STRATA_TODO(_NOEXCEPT)?\(\"$M\"" src include 2>/dev/null || true; } | awk -F: '{s+=$2} END {print s+0}')
if [[ "$left" == 0 ]]; then ok "none"; else bad "$left STRATA_TODO($M) site(s) left: tools/todo_report.sh $M"; fi

run_preset() {
  local preset="$1" log
  log=$(mktemp)
  step "build + test: preset '$preset' (labels m1..m$N)"
  if ! cmake --preset "$preset" >"$log" 2>&1; then
    tail -n 20 "$log"; bad "$preset: configure"; return
  fi
  if ! cmake --build --preset "$preset" >"$log" 2>&1; then
    grep -E "error|warning" "$log" | head -n 40; bad "$preset: build (warnings are errors here)"; return
  fi
  ok "$preset: build"
  if ctest --preset "$preset" -L "$(labels)" --output-on-failure -j "$(getconf _NPROCESSORS_ONLN)" >"$log" 2>&1; then
    ok "$preset: all m1..m$N tests pass ($(grep -Eo '[0-9]+ tests? passed|out of [0-9]+' "$log" | tail -n 1))"
  else
    grep -E "^\s*[0-9]+ - |tests passed" "$log" | tail -n 40; bad "$preset: tests (full log: $log)"
    return
  fi
  rm -f "$log"
}

run_preset strict
[[ $ASAN == 1 ]] && run_preset asan
[[ $TSAN == 1 ]] && run_preset tsan

if [[ $TIDY == 1 ]]; then
  # Baseline: the previous milestone's commit ("milestone <N-1>: ..."), so
  # every .cc touched during this milestone is checked, committed or not.
  # Headers are checked through the .cc files that include them.
  base=$(git log --grep="^milestone $((N - 1)):" --format=%H -1 2>/dev/null || true)
  step "clang-tidy (src/*.cc changed since ${base:0:8})"
  TIDY_BIN=$(command -v clang-tidy || command -v clang-tidy-18 || ls /opt/homebrew/opt/llvm/bin/clang-tidy 2>/dev/null || true)
  if [[ -z "$TIDY_BIN" ]]; then
    bad "clang-tidy not found (brew install llvm, or run in the Lima VM)"
  elif [[ -z "$base" ]]; then
    bad "no 'milestone $((N - 1)): ...' commit found to diff against"
  else
    files=$(git diff --name-only --diff-filter=d "$base" -- 'src/*.cc' 2>/dev/null || true)
    extra=()
    # Homebrew's clang-tidy doesn't know where Apple's SDK headers live.
    [[ "$(uname)" == Darwin ]] && extra+=("--extra-arg=-isysroot$(xcrun --show-sdk-path)")
    if [[ -z "$files" ]]; then ok "no changed sources"; else
      if "$TIDY_BIN" -p build/strict --quiet ${extra[@]+"${extra[@]}"} $files; then ok "clang-tidy clean"; else bad "clang-tidy findings"; fi
    fi
  fi
fi

step "summary for $M"
if [[ ${#FAILED[@]} -eq 0 ]]; then
  printf '\033[32mPASS\033[0m  %s gates green. Now: run the demo from docs/roadmap.md, commit, and ask for a review.\n' "$M"
  exit 0
fi
printf '\033[31mNOT DONE\033[0m\n'; for f in "${FAILED[@]}"; do echo "  - $f"; done
exit 1
