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

Captures (2026-10-03, Ironclad vs SHRINKER_BEETLE_WEAK): original `strike4 skill1 power1 exhaust1
anger1 pommel1 endturn1 hover1 create1`, ours `ours/{strike,skill,power,exhaust,anger,pommel,endturn}`
(`tools/ref/ours.sh`), sheets `sheet_*.png` in ../ref-captures.

| # | Scenario | Status | Original vs ours |
|---|---|---|---|
| C1 | Play: card to play area (all types) | diff | Original: on release the card moves (~0-200 ms) to a play spot in the upper middle at full size, keeps its glow, and stays there while the card resolves (~0.4-0.7 s). Ours: the card shrinks and spins away at once, gone by +150 ms. |
| C2 | Played card to discard pile | diff | Original: after the hold the card shrinks and tilts (~550 ms), turns into an orange comet and flies in a high arc to the discard pile (bottom right) with a long fire trail (~0.7-1.6 s). Ours: a small dim card / red dot drops toward the bottom, no trail. |
| C3 | Exhaust | diff | Original: the card holds in the play area ~0.7 s, then dissolves in place into dark smoke and red embers (~0.7-1.2 s); exhaust pile counter appears bottom right. Ours: same fly-away as C2, and a stray "消耗：战斗" label stays over the hand. |
| C4 | Power play | diff | Original: card holds, shrinks, then an orange trail flies from it to the player (~0.4-1.2 s); Inflame shows a big flame VFX on the player (0.2-0.9 s), then green "Strength" text with green rising particles (~1.2 s). Ours: card flies to discard, "力量" text at +200 ms, no flame, no trail to the player. |
| C5 | Skill (block) | diff | Original: big translucent blue shield VFX in front of the player (0.2-0.5 s), block badge on the HP bar. Ours: small shield icon and a floating "+5"; original shows no floating number. |
| C6 | Aiming a no-target card | diff | Original: Self cards have no arrow, the card itself follows the cursor. Ours: an arc arrow even for Self skills / powers. |
| C7 | Keyword tips while dragging | diff | Ours shows keyword tooltips (格挡 / 力量 / 易伤 / 消耗) top left during the drag; original shows none while aiming. |
| C8 | Damage number | ok-ish | NDamageNumVfx: pops at the hit 2.5x -> 1x (1.2 s), hops up (vy -700..-800, g 2000) and falls through the HP bar while turning red -> cream and fading (2 s); no bounce. Ours follows it (spawn height, size and arc match within a few px, `ours/dmg_new`). Left: ours appears ~50 ms later (+300 vs +250 ms: the hit itself, AttackCommand anim delay vs our 0.15 s wait), so the fall trails the original by ~100-150 ms at +1.1 s; the original's random vy / tilt (+-5 deg) are fixed in ours. "Blocked" now follows NDamageBlockedVfx (rise 250 px, 1 -> 0.6, #21C0FF -> white, 1.5 s), not captured yet. |
| C9 | Debuff text | minor | "Vulnerable" red rising text in both; ours is pink / purple. |
| C10 | End turn discard | diff | Original: the hand flashes cyan, all cards drop together tilted and darkened (~200 ms), then fly as fire comets in staggered arcs to the discard pile (0.3-1.2 s). Ours: the hand vanishes for a frame, then the cards reappear one by one and slide right into the pile, no fire. |
| C11 | Turn timing | diff | Original "Enemy Turn" banner ~0.3-1.6 s, enemy acts ~2.0-2.5 s, "Player Turn" at ~3.4 s, draw at ~4.0 s. Ours: banner 0.3-0.9 s, "Player Turn" at 1.6 s, draw at 2.5 s (whole enemy turn about twice as fast; enemy moves differ by seed, recheck with the same intent). |
| C12 | Draw | diff (verify) | Original: each card appears from the draw pile (bottom left) one at a time (~150 ms apart) and slides into the hand. Ours: cards appear from the right side of the hand area, several overlapping. |
| C13 | Shuffle | diff | Original: discard pile cards fly as fire comets from the discard pile to the draw pile (~5.2-5.6 s). Ours: not captured yet (no shuffle in our run). |
| C14 | Hover, card created in hand | todo | captured in the original (hover1, create1); ours needs a matching scenario. |
| C15 | Hit reaction, lunge, slash | ok-ish | Lunge and slash timing match; original hit adds red slash marks and shards. |
