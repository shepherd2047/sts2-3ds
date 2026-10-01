#!/bin/bash
# H2 memory soak: the sanitizer sim over every character and the SIM_ALL* / SIM_MODIFIERS /
# SIM_SAVELOAD mixes, configs in parallel. One summary line per config:
#   <config>: runs R, crashes C, sanitizer S, stuck T, mismatch M, peakRSS <MB>
# Usage: bash tools/soak.sh [N=1000] [jobs=4]   (logs in build/soak/; exit 1 if anything failed)
# Sanitizer: build/sim-asan (ASan+UBSan) when its runtime starts; otherwise (macOS 27: ASan hangs
# at start-up) build/sim-ubsan with libmalloc scribble/guard-edge checks. On macOS it then runs
# `leaks --atExit` over SOAK_LEAKS (default 20) runs per character, since LeakSanitizer is not
# available there. SOAK_SAN=asan|ubsan forces the mode.
cd "$(dirname "$0")/.."
N=${1:-1000}; JOBS=${2:-4}
out=build/soak; rm -rf $out; mkdir -p $out
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}
export ASAN_OPTIONS=${ASAN_OPTIONS:-halt_on_error=1}
san=${SOAK_SAN:-auto}
if [ $san = auto ]; then
  san=ubsan
  if make -f Makefile.sdl build/sim-asan >$out/build_asan.log 2>&1; then
    ./build/sim-asan 0 >/dev/null 2>&1 & p=$!
    for _ in $(seq 30); do kill -0 $p 2>/dev/null || break; sleep 1; done
    if kill -0 $p 2>/dev/null; then kill -9 $p; else wait $p && san=asan; fi
  fi
fi
make -f Makefile.sdl build/sim-$san build/sim >$out/build.log 2>&1 || { echo "build failed ($out/build.log)"; exit 1; }
BIN=./build/sim-$san
[ $san = ubsan ] && export MallocScribble=1 MallocPreScribble=1 MallocGuardEdges=1 MallocErrorAbort=1
echo "soak: $N runs per config, $BIN"
cfgs=()
for c in Ironclad Silent Defect Regent Necrobinder; do cfgs+=("SIM_CHAR=$c"); done
for c in Ironclad Silent Defect Regent Necrobinder; do cfgs+=("SIM_CHAR=$c SIM_ALLCARDS=1"); done
cfgs+=("SIM_ALLRELICS=1 SIM_ALLCARDS=1" "SIM_ALLPOTIONS=1" "SIM_ENCHANT=1" "SIM_ASC=10 SIM_ALLCARDS=1"
       "SIM_MODIFIERS=Draft,Midas" "SIM_MODIFIERS=SealedDeck,Hoarder" "SIM_MODIFIERS=CharacterCards:Silent,Specialized"
       "SIM_CHAR=Defect SIM_SAVELOAD=17" "SIM_CHAR=Regent SIM_ALLCARDS=1 SIM_SAVELOAD=9")
run_one() {  # $1 index, $2 config. One process for all N seeds (RSS growth = leak signal);
  # after a crash / sanitizer abort / STUCK exit it resumes at the next seed (SIM_FIRST).
  local log=$out/$1.log s=1 last
  while [ $s -le $N ]; do
    /usr/bin/time -l env $2 SIM_FIRST=$s $BIN $N >>$log 2>&1 && break
    last=$(grep -oE '^seed +[0-9]+' $log | tail -1 | grep -oE '[0-9]+'); last=${last:-$((s - 1))}
    if tail -40 $log | grep -qE "^seed +$last: STUCK"; then s=$((last + 1))
    else echo "EXIT seed $((last + 1))" >>$log; s=$((last + 2)); fi
  done
}
for i in "${!cfgs[@]}"; do
  while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do sleep 1; done
  run_one "$i" "${cfgs[$i]}" &
done
wait
fail=0
for i in "${!cfgs[@]}"; do
  log=$out/$i.log
  runs=$(grep -cE '^seed ' $log); stuck=$(grep -c 'STUCK' $log)
  sanr=$(grep -cE '^==[0-9]+==ERROR|runtime error:|malloc: \*\*\*' $log); crash=$(grep -c '^EXIT seed' $log)
  mism=$(grep -cE 'MISMATCH|LOAD FAILED' $log)
  rss=$(grep 'maximum resident set size' $log | awk '$1>m{m=$1}END{printf "%.0f", m/1048576}')
  printf '%-50s runs %d, crashes %d, sanitizer %d, stuck %d, mismatch %d, peakRSS %sMB\n' \
    "${cfgs[$i]}:" "$runs" "$crash" "$sanr" "$stuck" "$mism" "$rss"
  [ $((crash + sanr + stuck + mism)) -gt 0 ] && fail=1
done
if [ "$(uname)" = Darwin ]; then
  # leaks needs a debuggable process: an ad-hoc signed copy with get-task-allow.
  printf '<?xml version="1.0" encoding="UTF-8"?>\n<plist version="1.0"><dict><key>com.apple.security.get-task-allow</key><true/></dict></plist>\n' >$out/gta.plist
  cp build/sim $out/sim-dbg && codesign -f -s - --entitlements $out/gta.plist $out/sim-dbg 2>/dev/null
  L=${SOAK_LEAKS:-20}; G=${SOAK_GMALLOC:-50}
  for c in Ironclad Silent Defect Regent Necrobinder; do
    [ "$L" != 0 ] && { r=$(SIM_CHAR=$c SIM_ALLCARDS=1 leaks --atExit -- $out/sim-dbg $L 2>&1 | grep -E '^Process [0-9]+: [0-9]+ (leaks|nodes)' | sed 's/^Process [0-9]*: //' | tr '\n' ' ')
      echo "leaks SIM_CHAR=$c SIM_ALLCARDS=1 ($L runs): ${r:-?}"; echo "$r" | grep -qE '(^| )0 leaks for' || fail=1; }
    # Guard Malloc (every allocation on its own page: overruns / use-after-free fault at once).
    [ "$G" != 0 ] && { SIM_CHAR=$c SIM_ALLCARDS=1 DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib ./build/sim $G >$out/gm_$c.log 2>&1; rc=$?
      echo "gmalloc SIM_CHAR=$c SIM_ALLCARDS=1 ($G runs): exit $rc, $(grep -cE '^seed' $out/gm_$c.log) runs"; [ $rc = 0 ] || fail=1; }
  done
fi
exit $fail
