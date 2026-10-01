#!/bin/zsh
# Build scheduler for all sts2-3ds worktrees (the Mac has 8 GB RAM; unbounded parallel builds hang it).
# Usage: sts2-build-lock.sh <timeout-seconds> <command...>
#
# - A pool of 6 compile tokens (= at most 6 compilers at once). A `make` takes as many tokens as it
#   can get, up to 4, and starts as soon as it has 1 (make -jN with N = tokens held): it never waits
#   while a token is free.
# - Builds compiling more than 8 files may only use tokens t0..t3, so t4/t5 are always left for
#   small incremental builds (their wait stays near zero).
# - Any other command (sim, tests, soak) runs without tokens at low priority (nice 15).
# - Compiles go through ccache (config: ~/Library/Preferences/ccache/ccache.conf).
# - Every wait is appended to /Users/m/dev/.sts2-build-wait.log.
DIR=/Users/m/dev/.sts2-build-tokens
LOG=/Users/m/dev/.sts2-build-wait.log
GATE=$DIR/gate
mkdir -p "$DIR"
T="$1"; shift

if [ "$1" != "make" ]; then
  exec timeout "$T" nice -n 15 "$@"
fi

args=("${@:2}")
clean=()
for a in "${args[@]}"; do [[ "$a" == -j* ]] || clean+=("$a"); done
n=$(make -n "${clean[@]}" 2>/dev/null | grep -c -- ' -c ')
if [ "$n" -le 8 ]; then cls=light; pool=(t4 t5 t0 t1 t2 t3); else cls=heavy; pool=(t0 t1 t2 t3); fi
want=4

reap() {  # free tokens whose holder died
  for t in "$DIR"/t*(N); do
    h=$(cat "$t/pid" 2>/dev/null)
    if [ -z "$h" ] || ! kill -0 "$h" 2>/dev/null; then rm -rf "$t"; fi
  done
}

start=$(date +%s); held=()
while [ ${#held} -eq 0 ]; do
  if mkdir "$GATE" 2>/dev/null; then
    reap
    for t in "${pool[@]}"; do
      [ ${#held} -ge $want ] && break
      if mkdir "$DIR/$t" 2>/dev/null; then echo $$ > "$DIR/$t/pid"; held+=("$t"); fi
    done
    rmdir "$GATE"
  else
    g=$(stat -f %m "$GATE" 2>/dev/null); [ -n "$g" ] && [ $(( $(date +%s) - g )) -gt 30 ] && rmdir "$GATE" 2>/dev/null
  fi
  [ ${#held} -eq 0 ] && sleep 2
done
trap 'for t in "${held[@]}"; do rm -rf "$DIR/$t"; done' EXIT INT TERM HUP
waited=$(( $(date +%s) - start ))
echo "$(date +%H:%M:%S) wait=${waited}s class=$cls files=$n tokens=${#held} dir=${PWD##*/}" >> "$LOG"
echo "[$cls: ${#held} token(s) after ${waited}s wait, $n files to compile]"
# Makefile.sdl used to force `MAKEFLAGS += -j<ncpu>` (8 compilers per build, overriding -j): build from a
# copy without that line so the token count really is the job count.
mk=(); skip=0
for a in "${clean[@]}"; do
  if [ $skip = 1 ]; then sed '/^MAKEFLAGS += -j/d' "$a" > .sched.mk; mk+=(.sched.mk); skip=0; continue; fi
  [ "$a" = "-f" ] && skip=1; mk+=("$a")
done
# ccache only for the host build (Makefile.sdl); the 3DS Makefile uses devkitARM's own compiler.
cx=(); [[ " ${clean[*]} " == *" Makefile.sdl "* ]] && cx=(CXX="ccache c++")
timeout "$T" make -j${#held} "${cx[@]}" "${mk[@]}"
