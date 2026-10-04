#!/bin/bash
# Mac: build the 3DS version in the repo and put it on the Desktop, in one step.
#   bash tools/build_3ds_mac.sh            # 3DS build
#   bash tools/build_3ds_mac.sh --assets   # also rebuild romfs first (after a pull that touched
#                                          # tools/build_assets.py or UI strings)
# devkitPro's make breaks on paths with spaces; the Mac repo lives at
# /Users/m/projects/sts2-3ds-work/sts2-3ds (no spaces), so it builds in place.
set -e
cd "$(dirname "$0")/.."
case "$PWD" in *" "*) echo "repo path has a space ($PWD): devkitPro make can't build here"; exit 1;; esac
LOCK=/Users/m/dev/sts2-build-lock.sh
[ -x "$LOCK" ] || LOCK=""
run() { if [ -n "$LOCK" ]; then "$LOCK" "$@"; else shift; "$@"; fi; }

if [ "$1" = "--assets" ]; then
  echo "== build_assets"
  run 2400 python3 tools/build_assets.py > /dev/null
fi
echo "== make (3DS) at $(git log --oneline -1)"
run 2400 make 2>&1 | grep -E "error|converted|\.3dsx" || true
cp sts2-3ds.3dsx ~/Desktop/sts2-3ds.3dsx
a=$(shasum -a 256 sts2-3ds.3dsx | cut -c1-16); b=$(shasum -a 256 ~/Desktop/sts2-3ds.3dsx | cut -c1-16)
[ "$a" = "$b" ] && echo "== Desktop/sts2-3ds.3dsx updated ($a)" || { echo "hash mismatch"; exit 1; }
