# Animation / detail comparison against the original

The workflow for finding the details the port still misses (card motion, glows, hit reactions, ...):
record the same action in the original (Steam, macOS) and in the SDL preview, then put the
frames side by side at the same time offsets.

## Tools (tools/ref/)

- `setup.sh`: builds `bin/winb` (game window bounds). Needs `cliclick`, `ffmpeg`, Pillow, numpy.
- `game.sh`: drives the original. The game pauses on another Space, so every command brings it to
  the front; finish each burst with `game.sh back`. Coordinates are in a 1000-px-wide `shot` image.
  `con "CMD"` runs a dev-console command (`fight SHRINKER_BEETLE_WEAK`, `card BASH`, `energy 50`,
  `godmode`, `room`, `event`, `relic`, `potion`). The full command set needs `"full_console": true`
  in the game's settings.save (`~/Library/Application Support/SlayTheSpire2/steam/<id>/`), set while
  the game is closed. Record with mods disabled.
- `game.sh rec-start NAME` / `rec-stop`: 60 fps capture of the window with a wall-clock time per frame,
  plus the game log (`playing card ...`) and our own `INPUT press/release/move/click` lines, all in
  `../ref-captures/NAME/` (outside the repo: derived from the game).
- Preview side: `STS_RECORD="from-to:out.mkv"` (with `STS_FIXED_STEP=1` frame f = f/60 s) records
  frames of a scripted run (`STS_SCRIPT`, `STS_ENCOUNTER`).
- `sheet.py --orig CAP --event "INPUT release" --ours out.mkv --at SEC --times=0,50,100,... -o x.png`:
  one column per time offset, original on top. `--orig-shift` (default 0.13 s) is the measured
  input-to-capture latency of game.sh (cursor-jump calibration, 118-161 ms).

## Scenarios (status: todo / diff / ok)

Cards: draw, hover, drag + aim, attack play, skill play, power play, discard, exhaust, card created
in hand, shuffle discard -> draw, end-of-turn discard, glow states.
Creatures: hit (shake, flash, hurt anim), damage number, block gain / break, buff / debuff icon,
death, intent change. UI: energy orb, turn banner.

| Scenario | Status | Notes |
|---|---|---|
| Attack play (Strike vs beetle) | diff | Original: after release the card holds in the hand slot ~150 ms, then shows enlarged at screen centre (~0.25 s), then shrinks / turns and flies to the discard pile with an orange trail (~0.7 s). Ours: the card shrinks and spins away at once and is gone by +150 ms; no centre display, no trail. Lunge, slash and damage number timing match. |
