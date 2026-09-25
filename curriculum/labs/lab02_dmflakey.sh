#!/usr/bin/env bash
# Lab 02 driver. LINUX ONLY (Lima VM), needs sudo. Simulates power loss with
# device-mapper "flakey": after the program exits, all writes are silently
# dropped, then the FS is unmounted and remounted from what actually hit the
# "disk". Anything not made durable before the drop is gone.
#
#   curriculum/labs/lab02_dmflakey.sh build/debug file-only ext4
#   curriculum/labs/lab02_dmflakey.sh build/debug file-only ext2
#   curriculum/labs/lab02_dmflakey.sh build/debug with-dir  ext2
#
# Predict each result first. Then explain: why does file-only (usually)
# survive on ext4 but not on ext2? What is ext4's journal doing on your
# behalf, and why must a storage engine NOT rely on it? (Search for
# "ext4 fsync entanglement" after you've formed your own answer.)
set -euo pipefail

BUILD=${1:?build dir, e.g. build/debug}
MODE=${2:?file-only or with-dir}
FSTYPE=${3:-ext4}
BIN="$BUILD/curriculum/labs/lab02_durable_create"
[[ -x "$BIN" ]] || { echo "build first: cmake --build $BUILD"; exit 2; }
[[ "$(uname)" == Linux ]] || { echo "Linux only: run inside the Lima VM"; exit 2; }

IMG=/tmp/lab02.img
MNT=/mnt/lab02
DEV=lab02flakey
LOOP=""

cleanup() {
  sudo umount "$MNT" 2>/dev/null || true
  sudo dmsetup remove "$DEV" 2>/dev/null || true
  [[ -n "$LOOP" ]] && sudo losetup -d "$LOOP" 2>/dev/null || true
}
trap cleanup EXIT

# --nolockfs matters: a plain `dmsetup suspend` freezes the file system,
# which FLUSHES it, and your "power loss" would make everything durable.
reload() {
  sudo dmsetup suspend --nolockfs "$DEV"
  sudo dmsetup load "$DEV" --table "$1"
  sudo dmsetup resume "$DEV"
}

truncate -s 256M "$IMG"
LOOP=$(sudo losetup -f --show "$IMG")
SECTORS=$(sudo blockdev --getsz "$LOOP")
sudo "mkfs.$FSTYPE" -q -F "$LOOP" 2>/dev/null || sudo "mkfs.$FSTYPE" -f -q "$LOOP"
ALLOW="0 $SECTORS flakey $LOOP 0 180 0"
DROP="0 $SECTORS flakey $LOOP 0 0 180 1 drop_writes"

sudo dmsetup create "$DEV" --table "$ALLOW"
sudo mkdir -p "$MNT"
sudo mount -t "$FSTYPE" "/dev/mapper/$DEV" "$MNT"
sudo chmod 777 "$MNT"
sync  # the empty FS itself is durable

"$BIN" "$MNT" "$MODE"

reload "$DROP"     # POWER LOSS: from now on every write vanishes
sudo umount "$MNT" # its final flushes are dropped too
reload "$ALLOW"    # "reboot"
sudo mount -t "$FSTYPE" "/dev/mapper/$DEV" "$MNT"
echo "[$FSTYPE, $MODE]"
"$BIN" "$MNT" check || true
