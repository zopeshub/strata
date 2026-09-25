#!/usr/bin/env bash
# Lab 09 (curriculum/09-ssd-internals.md): characterize the device with fio,
# the tool storage engineers actually use. Linux (Lima VM) or macOS
# (brew install fio; engine psync/posixaio only, no io_uring/libaio).
#
#   curriculum/labs/lab09_fio.sh ~/lab     # a directory on the disk under test
#
# Produces one line per job: IOPS, bandwidth, p50/p99/p99.9 latency. Keep the
# output in curriculum/NOTES.md next to your lab07 numbers.
set -euo pipefail
DIR=${1:?directory on the disk to test}
SIZE=${SIZE:-1G}
RUNTIME=${RUNTIME:-15}
command -v fio >/dev/null || { echo "install fio (apt install fio / brew install fio)"; exit 2; }
command -v jq >/dev/null || { echo "install jq (apt install jq / brew install jq)"; exit 2; }

if [[ "$(uname)" == Linux ]]; then
  ASYNC=io_uring; DIRECT=1
else
  ASYNC=posixaio; DIRECT=0  # macOS: no O_DIRECT in fio; results include the page cache
  echo "note: macOS fio cannot bypass the page cache; randread numbers will be optimistic"
fi

run() {
  local name=$1; shift
  fio --name="$name" --directory="$DIR" --size="$SIZE" --runtime="$RUNTIME" --time_based \
      --direct="$DIRECT" --group_reporting --output-format=json "$@" > "/tmp/fio_$name.json"
  jq -r --arg n "$name" '
    .jobs[0] as $j
    | ($j.read.iops + $j.write.iops) as $iops
    | (($j.read.bw + $j.write.bw) / 1024) as $mbs
    | (if $j.read.iops > 0 then $j.read.clat_ns.percentile else $j.write.clat_ns.percentile end) as $p
    | "\($n | .[0:26] | . + (" " * (26 - length)))  IOPS \($iops | floor)\t  \($mbs * 10 | floor / 10) MiB/s\t  p50 \($p["50.000000"] / 1000 | floor) us\t  p99 \($p["99.000000"] / 1000 | floor) us\t  p99.9 \($p["99.900000"] / 1000 | floor) us"' \
    "/tmp/fio_$name.json"
}

echo "== reads"
run randread-4k-qd1   --rw=randread --bs=4k --ioengine=psync
run randread-4k-qd32  --rw=randread --bs=4k --ioengine="$ASYNC" --iodepth=32
run seqread-1m        --rw=read --bs=1m --ioengine="$ASYNC" --iodepth=8
echo "== writes"
run randwrite-4k-qd32 --rw=randwrite --bs=4k --ioengine="$ASYNC" --iodepth=32
run seqwrite-1m       --rw=write --bs=1m --ioengine="$ASYNC" --iodepth=8
echo "== the WAL pattern: small sequential writes, each made durable"
run wal-4k-fsync      --rw=write --bs=4k --ioengine=psync --fsync=1
run wal-4k-fdatasync  --rw=write --bs=4k --ioengine=psync --fdatasync=1
run wal-16k-fdatasync --rw=write --bs=16k --ioengine=psync --fdatasync=1
echo
echo "Now: why is wal-4k-fsync ~100x fewer IOPS than randwrite-4k-qd32? What would"
echo "group commit (lab08, M7) do to that number? What does the qd1 vs qd32 randread gap"
echo "say about the device's internal parallelism (09-ssd-internals.md)?"
