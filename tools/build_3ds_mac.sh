#!/bin/bash
# Mac: build the 3DS version and put it on the Desktop, in one step.
#   bash tools/build_3ds_mac.sh            # source + 3DS build
#   bash tools/build_3ds_mac.sh --assets   # also rebuild romfs first (after a pull that touched
#                                          # tools/build_assets.py or UI strings)
# devkitPro's make can't handle this repo's path (spaces / non-ASCII), so the build runs in an ASCII
# copy at ~/dev/sts2-3ds-build. Running `make` there without syncing first rebuilds the OLD source
# ("is up to date") — this script always syncs.
set -e
cd "$(dirname "$0")/.."
REPO=$PWD
OUT=~/dev/sts2-3ds-build
LOCK=/Users/m/dev/sts2-build-lock.sh
[ -x "$LOCK" ] || LOCK=""
run() { if [ -n "$LOCK" ]; then "$LOCK" "$@"; else shift; "$@"; fi; }

if [ "$1" = "--assets" ]; then
  echo "== build_assets"
  run 2400 python3 tools/build_assets.py > /dev/null
fi
echo "== sync $(git log --oneline -1) -> $OUT"
mkdir -p "$OUT"
rsync -a --exclude build/ --exclude .git/ --exclude .claude/ "$REPO/" "$OUT/"
echo "== make (3DS)"
cd "$OUT"
run 2400 make 2>&1 | grep -E "error|converted|\.3dsx" || true
cp sts2-3ds.3dsx ~/Desktop/sts2-3ds.3dsx
a=$(shasum -a 256 sts2-3ds.3dsx | cut -c1-16); b=$(shasum -a 256 ~/Desktop/sts2-3ds.3dsx | cut -c1-16)
[ "$a" = "$b" ] && echo "== Desktop/sts2-3ds.3dsx updated ($a)" || { echo "hash mismatch"; exit 1; }
