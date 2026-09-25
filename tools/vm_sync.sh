#!/usr/bin/env bash
# Run INSIDE the Lima VM: copies the repo from the virtiofs-mounted Mac
# workspace onto the VM's own disk (~/strata by default), where builds are
# fast and storage experiments mean something.
#
#   tools/vm_sync.sh              # -> ~/strata
#   tools/vm_sync.sh ~/elsewhere
#
# Lima mounts the host workspace at the same absolute path as on the Mac,
# so the source is found from the mount table, not hard-coded.
set -euo pipefail
[[ "$(uname)" == Linux ]] || { echo "run this inside the VM: limactl shell strata"; exit 2; }
DEST=${1:-$HOME/strata}
SRC=""
while read -r target; do
  if [[ -f "$target/strata/CMakeLists.txt" ]]; then SRC="$target/strata"; break; fi
  if [[ -f "$target/CMakeLists.txt" && -d "$target/src/db" ]]; then SRC="$target"; break; fi
done < <(findmnt -t virtiofs -no TARGET)
[[ -n "$SRC" ]] || { echo "no virtiofs mount containing strata found (see lima/strata.yaml mounts)"; exit 2; }
mkdir -p "$DEST"
rsync -a --delete --exclude /build/ --exclude /.cache/ "$SRC/" "$DEST/"
echo "synced $SRC -> $DEST"
