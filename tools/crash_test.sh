#!/usr/bin/env bash
# kill -9 torture loop: the M2 demo ("WAL replay recovers state after kill -9").
#
#   tools/crash_test.sh [--build build/debug] [--iterations 50] [--sync_every 1]
#                       [--db /tmp/strata_crash] [--max_ms 800]
#
# Each iteration: start strata_crash_writer, let it run a random time,
# SIGKILL it, run strata_crash_verify with the last acked index, resume.
#
# NOTE: kill -9 is a PROCESS crash; the kernel keeps the page cache, so
# even unsynced writes survive. To test POWER loss, use the MemFileSystem
# tests (ctest -L m2) or the dm-flakey lab (curriculum/labs/02).
set -euo pipefail

BUILD=build/debug
ITERATIONS=50
SYNC_EVERY=1
DB=/tmp/strata_crash
MAX_MS=800

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build) BUILD="$2"; shift 2 ;;
    --iterations) ITERATIONS="$2"; shift 2 ;;
    --sync_every) SYNC_EVERY="$2"; shift 2 ;;
    --db) DB="$2"; shift 2 ;;
    --max_ms) MAX_MS="$2"; shift 2 ;;
    *) echo "unknown arg $1"; exit 2 ;;
  esac
done

WRITER="$BUILD/tools/strata_crash_writer"
VERIFY="$BUILD/tools/strata_crash_verify"
[[ -x "$WRITER" && -x "$VERIFY" ]] || { echo "build the tools first: cmake --build $BUILD"; exit 2; }

rm -rf "$DB"
ACKS=$(mktemp)
trap 'rm -f "$ACKS"' EXIT
next=0
acked=-1

for ((i = 1; i <= ITERATIONS; i++)); do
  : > "$ACKS"
  "$WRITER" --db="$DB" --start="$next" --sync_every="$SYNC_EVERY" > "$ACKS" 2>/dev/null &
  pid=$!
  sleep_ms=$(( (RANDOM % MAX_MS) + 20 ))
  sleep "$(printf '0.%03d' "$sleep_ms")"
  kill -9 "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true

  last=$(tail -n 1 "$ACKS" | awk '{print $2}')
  [[ -n "$last" ]] && acked=$last

  if ! out=$("$VERIFY" --db="$DB" --acked="$acked"); then
    echo "iteration $i: FAILED (acked through $acked)"
    echo "DB left at $DB for inspection; try: $BUILD/tools/strata_wal_dump $DB/*.log"
    exit 1
  fi
  new_next=${out#next=}
  printf 'iteration %3d: killed after %4d ms, %7d keys recovered (acked through %d)\n' \
    "$i" "$sleep_ms" "$new_next" "$acked"
  next=$new_next
done
echo "PASS: $ITERATIONS kill -9 cycles, $next keys, no acknowledged write lost, no holes"
