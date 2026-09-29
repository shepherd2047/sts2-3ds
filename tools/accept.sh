#!/bin/bash
# Acceptance checks (docs/PLAN.md "Acceptance"): SDL build, unit checks, the SIM_SAVELOAD
# matrix, optional 3DS build. Prints one line per check and "ACCEPT: PASS" or "ACCEPT: FAIL".
# Run from the UCRT64 shell at the repo root:  bash tools/accept.sh [--quick] [--3ds]
#   --quick  fewer seeds and save floors (for a lane's own check between commits)
#   --3ds    also build the .3dsx (Windows: devkitPro MSYS shell; macOS: in ~/dev/sts2-3ds-build)
cd "$(dirname "$0")/.."
export OS=${OS:-Windows_NT}
[ -d /ucrt64/bin ] && export PATH=/ucrt64/bin:$PATH
seeds=12; floors="3 9 17 30"; do3ds=0
for a in "$@"; do
  case $a in
    --quick) seeds=6; floors="9 17" ;;
    --3ds) do3ds=1 ;;
  esac
done
fail=0
say() { echo "$1"; case $1 in FAIL*|DIFF*) fail=1 ;; esac; }

if make -f Makefile.sdl -j8 >build/accept_build.log 2>&1; then say "ok   SDL build"; else say "FAIL SDL build (build/accept_build.log)"; fi
out=$(make -f Makefile.sdl check 2>&1)
if echo "$out" | grep -qE "[1-9][0-9]* failed|Error"; then say "FAIL make check"; echo "$out" | tail -5
else say "ok   make check ($(echo "$out" | grep -oE '[0-9]+ checks, 0 failed' | tr '\n' ' '))"; fi

sets=("X=1" "SIM_ALLCARDS=1" "SIM_ALLRELICS=1 SIM_ALLCARDS=1" "SIM_ALLPOTIONS=1" "SIM_ENCHANT=1")
for c in ${ACCEPT_CHARS:-}; do sets+=("SIM_CHAR=$c SIM_ALLCARDS=1"); done
for env in "${sets[@]}"; do
  base=$(env $env ./build/sim $seeds 2>&1 | grep -E "^seed|^wins|STUCK|MISMATCH")
  if echo "$base" | grep -q "STUCK"; then say "FAIL $env: STUCK"; continue; fi
  for k in $floors; do
    got=$(env $env SIM_SAVELOAD=$k ./build/sim $seeds 2>&1 | grep -E "^seed|^wins|STUCK|MISMATCH|FAILED")
    if [ "$base" == "$got" ]; then say "ok   $env K=$k"; else say "DIFF $env K=$k"; fi
  done
done

mtime() { stat -c %Y "$1" 2>/dev/null || stat -f %m "$1" 2>/dev/null || echo 0; }
if [ $do3ds = 1 ]; then
  if [ -x /c/msys64/usr/bin/bash.exe ]; then
    out3ds=sts2-3ds.3dsx; before=$(mtime $out3ds)
    MSYSTEM=MSYS /c/msys64/usr/bin/bash.exe -lc "cd '$(pwd)' && source /etc/profile.d/devkit-env.sh && make -j8 PYTHON=" >build/accept_3ds.log 2>&1
  elif [ "$(uname)" = Darwin ] && [ -d /opt/devkitpro ]; then
    # macOS: devkitPro's make breaks on the repo's non-ASCII path, so build in the ASCII copy
    # (CLAUDE.md: ~/dev/sts2-3ds-build). romfs_3ds/ is kept there between builds.
    dst=~/dev/sts2-3ds-build; mkdir -p $dst
    rsync -a --delete --exclude build/ --exclude .git/ --exclude .claude/ --exclude /audio/ --exclude romfs/audio/ --exclude romfs_3ds/ --exclude '*.3dsx' ./ $dst/
    out3ds=$dst/sts2-3ds.3dsx; before=$(mtime $out3ds)
    (cd $dst && DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM make -j8) >build/accept_3ds.log 2>&1
  else out3ds=; echo "skip 3DS build (no devkitPro here)"; fi
  if [ -n "$out3ds" ]; then
    after=$(mtime $out3ds)
    if [ "$after" -gt "$before" ] && ! grep -qE " error|Error " build/accept_3ds.log; then say "ok   3DS build"
    else say "FAIL 3DS build (build/accept_3ds.log)"; fi
  fi
fi
[ $fail = 0 ] && echo "ACCEPT: PASS" || echo "ACCEPT: FAIL"
exit $fail
