#!/bin/bash
# Drives the original Slay the Spire 2 (Steam, macOS) and records it for side-by-side comparison
# with our port. Captures go to ../ref-captures (outside the repo: they are derived from the game).
#
#   game.sh shot NAME              screenshot of the game window -> $CAP/NAME.png (1000 px wide copy too)
#   game.sh click X Y              click at image coords of a 1000-px-wide shot
#   game.sh drag X1 Y1 X2 Y2 [ms]  press-drag-release (same coords), ms = drag duration
#   game.sh hover X Y
#   game.sh key CODE               macOS key code (36 return, 53 esc, 49 space)
#   game.sh type TEXT              (pasted: see con)
#   game.sh con "CMD"              dev console: open, paste CMD, enter, close
#   game.sh newrun CHAR [ENC]      abandon the current run, start a new one as CHAR (Ironclad, Silent,
#                                  Regent, Necrobinder, Defect) and leave Neow with `fight ENC`
#                                  (default SHRINKER_BEETLE_WEAK; ENC=- stays at Neow). Remembers CHAR.
#   game.sh char                   prints the character newrun last started ("" if unknown)
#   game.sh play SLOT|last TARGET  drag a hand card to TARGET (enemy | enemy2 | self | X,Y); slots count
#                                  from the left in a hand of HAND=N cards (default 6: a fresh fight's
#                                  5 + one `con card`), "last" is the rightmost
#   game.sh endturn                click End Turn
#   game.sh rec-start NAME         start recording window (60 fps, wall-clock frame times) + log events
#   game.sh rec-stop               stop it; events in $CAP/NAME/events.txt
#   game.sh back                   switch back to Claude (always do this after a burst)
# The game pauses on another Space, so every command brings it to the front first.
# Profile: use the test profile (Save Profile 2, everything unlocked with `con "unlock all"`), never
# the owner's Profile 1: newrun abandons runs, and abandoned runs land in the run history.
REF=$(cd "$(dirname "$0")" && pwd)
MAIN=$(cd "$(git -C "$REF" rev-parse --path-format=absolute --git-common-dir)/.." && pwd)  # main checkout, also from a worktree
CAP=${STS_REF_CAP:-$(cd "$MAIN/.." && pwd)/ref-captures}
LOG="$HOME/Library/Application Support/SlayTheSpire2/logs/godot.log"
BIN="$REF/bin"; [ -x "$BIN/winb" ] || BIN="$MAIN/tools/ref/bin"  # bin/ is not in git: a worktree uses main's
mkdir -p "$CAP"
front() {
  osascript -e 'tell application "System Events" to set frontmost of (first process whose name contains "Slay the Spire") to true' >/dev/null
  sleep ${FRONT_WAIT:-0.4}
  read WID PID X Y W H SC < <("$BIN/winb") || { echo "game window not found"; exit 1; }
  # bounds include the title bar; content starts 28 pt lower in windowed mode
  TB=${TITLEBAR:-28}; CY=$(echo "$Y+$TB" | bc); CH=$(echo "$H-$TB" | bc)
}
mark() { # INPUT lines in the recording's events.txt, so sheet.py can anchor on the real input time
  [ -f "$CAP/.rec" ] && kill -0 "$(cat "$(cat "$CAP/.rec")/ffmpeg.pid" 2>/dev/null)" 2>/dev/null &&
    perl -MTime::HiRes=time -e 'printf "%.3f INPUT %s\n", time, "@ARGV"' "$@" >> "$(cat "$CAP/.rec")/events.txt"
}
pt() { # image coords (1000 wide, content area) -> screen points
  local f; f=$(echo "$W/1000" | bc -l)
  echo "$(printf %.0f "$(echo "$X+$1*$f" | bc -l)"),$(printf %.0f "$(echo "$CY+$2*$f" | bc -l)")"
}
glide() { # move the cursor to screen point "x,y" in steps: the game only notices hover targets
  # (creatures for potions, some buttons) when the pointer moves onto them, not when it jumps
  local to=$1 from fx fy tx ty i
  from=$(cliclick p:. 2>/dev/null | tail -1); fx=${from%,*}; fy=${from#*,}; tx=${to%,*}; ty=${to#*,}
  [[ "$fx" =~ ^-?[0-9]+$ && "$fy" =~ ^-?[0-9]+$ ]] || { cliclick m:"$to"; return; }
  for i in 1 2 3 4 5 6 7 8; do cliclick m:"$((fx + (tx - fx) * i / 8)),$((fy + (ty - fy) * i / 8))"; sleep 0.02; done
}
snap() { # screenshot to a temp file, prints its path
  local f; f=$(mktemp -t gsnap).png; screencapture -x -R"$X,$CY,$W,$CH" "$f"; echo "$f"
}
self() { bash "$REF/game.sh" "$@"; }
# Layout (1000-px frame, measured 2026-10-05 on v0.111.0, windowed 1275x808 pt):
# main menu labels x 403; Abandon Run confirm "Yes" 587,412; Singleplayer -> Standard card 292,320;
# character select icons y 558, x below; Embark 955,472; "Ascensions Unlocked" popup "Got it" 498,497;
# pause menu (Esc in a run) "Save and Quit" 500,416.
charx() { case $1 in Ironclad) echo 348;; Silent) echo 410;; Regent) echo 472;; Necrobinder) echo 534;; Defect) echo 596;; *) return 1;; esac; }
cmd=$1; shift
case $cmd in
  shot)
    front; screencapture -x -R"$X,$CY,$W,$CH" "$CAP/$1.png"
    sips -Z 1000 "$CAP/$1.png" --out "$CAP/$1_s.png" >/dev/null; echo "$CAP/$1_s.png" ;;
  click) front; p=$(pt "$1" "$2"); glide "$p"; sleep 0.15; mark click "$1" "$2"; cliclick dd:"$p"; sleep 0.06; cliclick du:"$p" ;;
  hover) front; p=$(pt "$1" "$2"); mark move "$1" "$2"; glide "$p" ;;
  drag)
    front; a=$(pt "$1" "$2"); b=$(pt "$3" "$4"); ms=${5:-400}; n=12
    cliclick m:"$a"; sleep 0.25; mark press "$1" "$2"; cliclick dd:"$a"
    for i in $(seq 1 $n); do
      cliclick m:"$(pt "$(echo "$1+($3-$1)*$i/$n" | bc -l)" "$(echo "$2+($4-$2)*$i/$n" | bc -l)")"
      sleep "$(echo "$ms/$n/1000" | bc -l)"
    done
    sleep ${HOLD:-0.3}; mark release "$3" "$4"; cliclick du:"$b" ;;
  # Keys go through System Events. KEY is a macOS key code (36 return, 53 esc, 49 space, 51 delete).
  key) front; mark key "$1"; osascript -e "tell application \"System Events\" to key code $1" ;;
  type) front; clip=$(pbpaste 2>/dev/null); printf '%s' "$1" | pbcopy  # paste: see con (input method)
    osascript -e 'tell application "System Events" to keystroke "v" using command down'; sleep 0.2
    printf '%s' "$clip" | pbcopy ;;
  con) # the console survives an app switch with its line unfocused (keys then reach the game:
    # space ends the turn), so: open it only if closed, click the input line, clear, run, close.
    front; f=$(snap); open=$(python3 "$REF/screen.py" console "$f"); rm -f "$f"
    [ "$open" = 1 ] || { osascript -e 'tell application "System Events" to key code 50'; sleep 0.4; }
    # The text goes in by paste: with a Chinese (pinyin) input source active, typed letters compose in
    # the IME, space picks a candidate and Return only commits the letters. The clipboard is restored.
    p=$(pt 300 273); cliclick c:"$p"; sleep 0.1
    clip=$(pbpaste 2>/dev/null); printf '%s' "$1" | pbcopy
    osascript -e 'tell application "System Events"' \
      -e 'repeat 80 times' -e 'key code 51' -e 'end repeat' -e 'keystroke "v" using command down' -e 'end tell'
    sleep 0.2; mark con "$1"
    osascript -e 'tell application "System Events"' -e 'key code 36' -e 'delay 0.4' -e 'key code 50' -e 'end tell'  # 50 = `
    printf '%s' "$clip" | pbcopy ;;
  newrun)
    char=$1; enc=${2:-SHRINKER_BEETLE_WEAK}; x=$(charx "$char") || { echo "unknown character $char"; exit 1; }
    front
    for try in 1 2 3 4 5 6; do  # get to the main menu: Esc out of screens; in a run Esc -> Save and Quit
      f=$(snap); rows=($(python3 "$REF/screen.py" menu "$f"))
      [ ${#rows[@]} -gt 0 ] && { rm -f "$f"; break; }
      if [ "$(python3 "$REF/screen.py" console "$f")" = 1 ]; then osascript -e 'tell application "System Events" to key code 50'; sleep 0.5
      elif [ "$(python3 "$REF/screen.py" pause "$f")" = 1 ]; then self click 500 416; sleep 5
      else self key 53; sleep 1.5; fi
      rm -f "$f"
    done
    [ ${#rows[@]} -gt 0 ] || { echo "newrun: main menu not reached"; exit 1; }
    if [ ${#rows[@]} -ge 7 ]; then  # Continue, Abandon Run, Multiplayer, Timeline, Settings, Compendium, Quit
      self click 403 "${rows[1]}"; sleep 1.5; self click 587 412; sleep 4
      f=$(snap); rows=($(python3 "$REF/screen.py" menu "$f")); rm -f "$f"
    fi
    [ ${#rows[@]} -ge 1 ] || { echo "newrun: no menu after abandoning"; exit 1; }
    self click 403 "${rows[0]}"; sleep 2      # Singleplayer
    self click 292 320; sleep 3               # Standard
    self click 498 497; sleep 1               # "Ascensions Unlocked" popup (first time only; harmless otherwise)
    self click "$x" 558; sleep 1.2
    self click 955 472; sleep 9               # Embark -> Neow
    echo "$char" > "$CAP/.char"
    [ "$enc" = - ] || { self con "fight $enc"; sleep 6; }
    echo "newrun: $char${enc:+, fight $enc}" ;;
  char) cat "$CAP/.char" 2>/dev/null || true ;;
  play)
    # hand fan of N cards: centre x 500, spacing shrinks as the hand grows (measured: 5-6 cards ~95-100 px, centre x 488)
    n=${HAND:-6}; slot=$1; [ "$slot" = last ] && slot=$n
    sp=$(( n <= 6 ? 98 : (n <= 8 ? 85 : 680 / n) ))
    cx=$(( 488 + (2 * slot - n - 1) * sp / 2 )); cy=575
    case $2 in
      self) tx=250; ty=370 ;;
      enemy|enemy1) tx=${ENEMY1:-750}; ty=370 ;;
      enemy2) tx=${ENEMY2:-820}; ty=330 ;;
      enemy3) tx=${ENEMY3:-900}; ty=330 ;;
      *,*) tx=${2%,*}; ty=${2#*,} ;;
      *) tx=500; ty=300 ;;
    esac
    self drag "$cx" "$cy" "$tx" "$ty" "${3:-500}" ;;
  endturn) self click 890 517 ;;
  rec-start)
    front; d="$CAP/$1"; mkdir -p "$d"; echo "$d" > "$CAP/.rec"; : > "$d/events.txt"
    s=$(printf %.0f "$SC")
    printf '%s %s %s %s %s\n' "$X" "$CY" "$W" "$CH" "$SC" > "$d/window.txt"
    nohup ffmpeg -hide_banner -loglevel error -y -f avfoundation -pixel_format nv12 -capture_cursor 1 \
      -framerate 60 -use_wallclock_as_timestamps 1 -i "${STS_SCREEN:-3}:none" \
      -vf "crop=$((${W%.*}*s)):$((${CH%.*}*s)):$((${X%.*}*s)):$((${CY%.*}*s)),scale=1280:-2" \
      -c:v libx264 -preset ultrafast -crf 16 -copyts -fps_mode passthrough "$d/video.mkv" \
      >"$d/ffmpeg.log" 2>&1 &
    echo $! > "$d/ffmpeg.pid"
    nohup bash -c 'tail -n0 -F "$0" | perl -MTime::HiRes=time -ne "\$|=1; printf \"%.3f %s\", time, \$_"' "$LOG" \
      >> "$d/events.txt" 2>/dev/null &  # append mode: mark() writes INPUT lines into the same file
    echo $! > "$d/tail.pid"; sleep 0.8; echo "recording -> $d" ;;
  rec-stop)
    d=$(cat "$CAP/.rec"); kill -INT "$(cat "$d/ffmpeg.pid")" 2>/dev/null
    pkill -P "$(cat "$d/tail.pid")" 2>/dev/null; kill "$(cat "$d/tail.pid")" 2>/dev/null
    for _ in $(seq 20); do kill -0 "$(cat "$d/ffmpeg.pid")" 2>/dev/null || break; sleep 0.25; done
    ffprobe -v error -select_streams v -show_entries frame=pts_time -of csv=p=0 "$d/video.mkv" | tr -d , > "$d/frames.txt"
    echo "$d: $(wc -l < "$d/frames.txt") frames, $(grep -c . "$d/events.txt") log lines" ;;
  back) open -a Claude ;;
  *) sed -n '2,24p' "$0"; exit 1 ;;
esac
