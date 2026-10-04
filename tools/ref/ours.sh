#!/bin/bash
# Records one scenario in the SDL preview for sheet.py (see docs/ANIM_DIFF.md).
#   ours.sh NAME DECK "SCRIPT" [FROM-TO]
# DECK: STS_DECK list (5 copies of one card keep the hand uniform, so any slot is that card).
# SCRIPT: STS_SCRIPT items after the fight is up (frame >= 400). The default record range 460-700
# puts frame 500 at --at 0.667 s. Output: ../ref-captures/ours/NAME.mkv
cd "$(dirname "$0")/../.."
# captures live next to the main checkout, also when run from a worktree
main=$(cd "$(git rev-parse --path-format=absolute --git-common-dir)/.." && pwd)
out=${STS_REF_CAP:-$main/../ref-captures}/ours; mkdir -p "$out"
deck=(); [ -n "$2" ] && deck=(STS_DECK="$2")  # an expanded VAR=x word is not an assignment: pass it via env
env STS_HIDDEN=1 STS_FIXED_STEP=1 STS_SEED=42 STS_ENCOUNTER="${ENC:-ShrinkerBeetleWeak}" "${deck[@]}" \
  STS_SCRIPT="40:A,100:A,160:A,220:A,$3" STS_RECORD="${4:-460-700}:$out/$1.mkv" \
  timeout 120 ./build/sts2-preview >/dev/null 2>&1 || echo "preview failed ($?)" >&2
echo "$out/$1.mkv"
