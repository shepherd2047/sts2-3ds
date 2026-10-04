#!/bin/bash
# Drives the original Slay the Spire 2 (Steam, macOS) and records it for side-by-side comparison
# with our port. Captures go to ../ref-captures (outside the repo: they are derived from the game).
#
#   game.sh shot NAME              screenshot of the game window -> $CAP/NAME.png (1000 px wide copy too)
#   game.sh click X Y              click at image coords of a 1000-px-wide shot
#   game.sh drag X1 Y1 X2 Y2 [ms]  press-drag-release (same coords), ms = drag duration
#   game.sh hover X Y
#   game.sh key CODE               macOS key code (36 return, 53 esc, 49 space)
#   game.sh type TEXT
#   game.sh con "CMD"              dev console: open, type CMD, enter, close
#   game.sh rec-start NAME         start recording window (60 fps, wall-clock frame times) + log events
#   game.sh rec-stop               stop it; events in $CAP/NAME/events.txt
#   game.sh back                   switch back to Claude (always do this after a burst)
# The game pauses on another Space, so every command brings it to the front first.
REF=$(cd "$(dirname "$0")" && pwd)
MAIN=$(cd "$(git -C "$REF" rev-parse --path-format=absolute --git-common-dir)/.." && pwd)  # main checkout, also from a worktree
CAP=${STS_REF_CAP:-$(cd "$MAIN/.." && pwd)/ref-captures}
LOG="$HOME/Library/Application Support/SlayTheSpire2/logs/godot.log"
mkdir -p "$CAP"
front() {
  osascript -e 'tell application "System Events" to set frontmost of (first process whose name contains "Slay the Spire") to true' >/dev/null
  sleep ${FRONT_WAIT:-0.4}
  read WID PID X Y W H SC < <("$REF/bin/winb") || { echo "game window not found"; exit 1; }
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
  # Keys go through System Events: cliclick's typing drops spaces and its Return is ignored by the
  # game's console. KEY is a macOS key code (36 return, 53 esc, 49 space, 51 delete).
  key) front; osascript -e "tell application \"System Events\" to key code $1" ;;
  type) front; osascript -e 'on run a' -e 'tell application "System Events" to keystroke (item 1 of a)' -e 'end run' "$1" ;;
  con) # the console survives an app switch with its line unfocused (keys then reach the game:
    # space ends the turn), so: open it only if closed, click the input line, clear, run, close.
    front; f=$(mktemp -t con).png; screencapture -x -R"$X,$CY,$W,$CH" "$f"
    open=$(python3 -c "
from PIL import Image; im=Image.open('$f').convert('RGB'); w,h=im.size
px=[im.getpixel((int(w*x),int(h*y))) for x in (.3,.5,.7,.9) for y in (.19,.32,.38)]
ok=all(max(p)<45 and p[2]-p[0]>=4 for p in px) and max(max(p) for p in px)-min(max(p) for p in px)<8
print(1 if ok else 0)"); rm -f "$f"
    [ "$open" = 1 ] || { osascript -e 'tell application "System Events" to keystroke "`"'; sleep 0.4; }
    p=$(pt 300 273); cliclick c:"$p"; sleep 0.1
    osascript -e 'on run a' -e 'tell application "System Events"' \
      -e 'repeat 80 times' -e 'key code 51' -e 'end repeat' -e 'keystroke (item 1 of a)' -e 'delay 0.15' \
      -e 'key code 36' -e 'delay 0.5' -e 'keystroke "`"' -e 'end tell' -e 'end run' "$1" ;;
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
  *) sed -n '2,16p' "$0"; exit 1 ;;
esac
