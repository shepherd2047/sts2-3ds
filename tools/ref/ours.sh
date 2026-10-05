#!/bin/bash
# Records one scenario in the SDL preview for sheet.py (see docs/ANIM_DIFF.md).
#   ours.sh NAME DECK "SCRIPT" [FROM-TO]
# DECK: STS_DECK list (5 copies of one card keep the hand uniform, so any slot is that card).
# SCRIPT: STS_SCRIPT items after the fight is up (frame >= 400). The default record range 460-700
# puts frame 500 at --at 0.667 s. Output: ../ref-captures/ours/NAME.mkv
# Environment (all optional):
#   ENC        STS_ENCOUNTER (default ShrinkerBeetleWeak; ENC= (empty) sets none, e.g. for menu scenes)
#   NAV        STS_SCRIPT prefix (default "40:A,100:A,160:A,220:A," = title -> first room; NAV= for none)
#   OURS_OUT   exact output path instead of ../ref-captures/ours/NAME.mkv
#   OURS_BIN   preview binary (default: this tree's build/sts2-preview, else build/dev/sts2-preview,
#              else the main checkout's)
#   OURS_TIMEOUT seconds (default 120)
#   any other STS_* variable is passed through (STS_CHAR, STS_ROOM, STS_POTIONS, ...)
# romfs/ is not in a worktree: the preview runs in whichever of this tree / the main checkout has it.
here=$(cd "$(dirname "$0")/../.." && pwd)
# captures live next to the main checkout, also when run from a worktree
main=$(cd "$(git -C "$here" rev-parse --path-format=absolute --git-common-dir)/.." && pwd)
out=${STS_REF_CAP:-$main/../ref-captures}/ours; mkdir -p "$out"
dest=${OURS_OUT:-$out/$1.mkv}; mkdir -p "$(dirname "$dest")"
bin=$OURS_BIN
for c in "$here/build/sts2-preview" "$here/build/dev/sts2-preview" "$main/build/sts2-preview"; do
  [ -z "$bin" ] && [ -x "$c" ] && bin=$c
done
[ -n "$bin" ] || { echo "no sts2-preview built" >&2; exit 1; }
root=$here; [ -d "$root/romfs" ] || root=$main
deck=(); [ -n "$2" ] && deck=(STS_DECK="$2")  # an expanded VAR=x word is not an assignment: pass it via env
enc=(); e=${ENC-ShrinkerBeetleWeak}; [ -n "$e" ] && enc=(STS_ENCOUNTER="$e")
nav=${NAV-40:A,100:A,160:A,220:A,}
cd "$root" && env STS_HIDDEN=1 STS_FIXED_STEP=1 STS_SEED=${STS_SEED:-42} "${enc[@]}" "${deck[@]}" \
  STS_SCRIPT="$nav$3" STS_RECORD="${4:-460-700}:$dest" \
  timeout "${OURS_TIMEOUT:-120}" "$bin" >/dev/null 2>&1 || echo "preview failed ($?)" >&2
[ -s "$dest" ] || { echo "no video written: $dest" >&2; exit 1; }
echo "$dest"
