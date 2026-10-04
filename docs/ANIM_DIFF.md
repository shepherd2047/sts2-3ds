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
| C1 | Play: card to play area (all types) | ok | Original: on release the card moves (0.25 s, ease-out cubic) to a play spot in the upper middle at 0.8 scale, keeps its glow, and stays there while the card resolves (~0.4-0.7 s). Ours (card_fly / combat_hand.cpp): same timing, the play spot is the centre of the top screen at a readable 0.8 scale with the cyan glow, held while the card is in the play pile (min 0.3 s); auto-played cards rise from the draw pile. Sheet `sheet_cardfly_strike.png`. |
| C2 | Played card to discard pile | ok | Original (NCardFlyVfx): the card shrinks / tilts / darkens and flies a high bezier arc (random 1-1.75 duration, accelerating) to the discard pile as an orange comet with the card trail (two additive ribbons, silhouette head, embers, sparks). Ours: `source/ui/card_fly.cpp`, same motion and trail from the game's own textures (vfx/trail, trail2, brush, cardsil), top screen down to the bottom-screen discard pile, ~0.7-1.5 s. Exhaust / powers keep the short flight after the hold (C3 / C4). |
| C3 | Exhaust | diff | Original: the card holds in the play area ~0.7 s, then dissolves in place into dark smoke and red embers (~0.7-1.2 s); exhaust pile counter appears bottom right. Ours: same fly-away as C2, and a stray "消耗：战斗" label stays over the hand. |
| C4 | Power play | diff | Original: card holds, shrinks, then an orange trail flies from it to the player (~0.4-1.2 s); Inflame shows a big flame VFX on the player (0.2-0.9 s), then green "Strength" text with green rising particles (~1.2 s). Ours: card flies to discard, "力量" text at +200 ms, no flame, no trail to the player. |
| C5 | Skill (block) | diff | Original: big translucent blue shield VFX in front of the player (0.2-0.5 s), block badge on the HP bar. Ours: small shield icon and a floating "+5"; original shows no floating number. |
| C6 | Aiming a no-target card | diff | Original: Self cards have no arrow, the card itself follows the cursor. Ours: an arc arrow even for Self skills / powers. |
| C7 | Keyword tips while dragging | diff | Ours shows keyword tooltips (格挡 / 力量 / 易伤 / 消耗) top left during the drag; original shows none while aiming. |
| C8 | Damage number | ok-ish | NDamageNumVfx: pops at the hit 2.5x -> 1x (1.2 s), hops up (vy -700..-800, g 2000) and falls through the HP bar while turning red -> cream and fading (2 s); no bounce. Ours follows it (spawn height, size and arc match within a few px, `ours/dmg_new`). Left: ours appears ~50 ms later (+300 vs +250 ms: the hit itself, AttackCommand anim delay vs our 0.15 s wait), so the fall trails the original by ~100-150 ms at +1.1 s; the original's random vy / tilt (+-5 deg) are fixed in ours. "Blocked" now follows NDamageBlockedVfx (rise 250 px, 1 -> 0.6, #21C0FF -> white, 1.5 s), not captured yet. |
| C9 | Debuff text | minor | "Vulnerable" red rising text in both; ours is pink / purple. |
| C10 | End turn discard | ok | Original: the hand flashes cyan, all cards leave together, tilt and darken, then fly as fire comets in staggered arcs to the discard pile (0.3-1.2 s). Ours: the cards flash cyan for 0.15 s and all launch the same comets at once (random speeds stagger them); the empty-hand frame is gone. Our arcs rise higher (into the hinge). Sheet `sheet_cardfly_endturn.png`. |
| C11 | Turn timing | diff | Original "Enemy Turn" banner ~0.3-1.6 s, enemy acts ~2.0-2.5 s, "Player Turn" at ~3.4 s, draw at ~4.0 s. Ours: banner 0.3-0.9 s, "Player Turn" at 1.6 s, draw at 2.5 s (whole enemy turn about twice as fast; enemy moves differ by seed, recheck with the same intent). |
| C12 | Draw | diff (verify) | Original: each card appears from the draw pile (bottom left) one at a time (~150 ms apart) and slides into the hand. Ours: cards appear from the right side of the hand area, several overlapping. |
| C13 | Shuffle | ok | Original (NCardFlyShuffleVfx): trail-only comets from the discard pile to the draw pile, min(0.045, 0.8/n) s apart, then a 0.2-0.5 s wait before drawing. Ours: the same comets (no card body); the core shuffle now waits min(0.045 n, 0.8) + 0.5 s instead of 0.3 s. Sheet `sheet_cardfly_shuffle.png` (ours shifted to line up: our enemy turn is shorter, C11). |
| C14 | Hover, card created in hand | todo | captured in the original (hover1, create1); ours needs a matching scenario. |
| C15 | Hit reaction, lunge, slash | ok-ish | Lunge and slash timing match; original hit adds red slash marks and shards. |

### Batch 2: creatures, more cards, combat UI (2026-10-03)

Original captures: `fightstart_cubex intenthover endturn_cubex hit_blockbreak death_victory enemyblock
enemyblock_hit enemyblock_break bladedance survivor armaments(_confirm) potion_throw potion_drink noenergy
pileview endturn_ethereal`. Ours (`ours/*.mkv`, same names): fightstart_cubex, endturn_cubex,
hit_blockbreak, death_victory (`STS_ENEMY_HP=5`), bladedance, noenergy, pileview, endturn_ethereal.
Not yet recorded on our side: intent hover, enemy block (needs a block-gaining move or a debug hook),
survivor / armaments selection, potions (`STS_POTIONS=FirePotion,BlockPotion`, drag from the belt).
Driving notes: hover targets (creatures for potions, some buttons) only register when the pointer
glides onto them (`game.sh` now glides); the upgrade confirm button ignored synthetic clicks once.

| # | Scenario | Status | Original vs ours |
|---|---|---|---|
| M1 | Enemy attacks the player | diff | Original: big red slash "V" mark and red shards on the player at the hit, big red number that then falls. Ours: small grey number, no slash VFX on the player. |
| M2 | Turn pacing | diff | Same as C11 with another enemy: original "Enemy Turn" 0.3-1.6 s, enemy acts ~2.0 s, "Player Turn" ~3.4 s; ours "Player Turn" ~2.0 s. Ours is about twice as fast. |
| M3 | Fight start | diff | Original: "Battle Start" banner with a sword icon ~0.3-2.0 s, energy orb greyed 0/3, cards drawn one by one from the draw pile at ~1.6-2.5 s, then "Player Turn / Turn 1" at ~3.0 s, End Turn button appears ~4.0 s. Ours: "战斗开始" only ~0.3 s, "玩家回合" at 0.9 s, then the draw: the order differs (original draws before the Player Turn banner) and ours is ~3x faster. |
| M4 | Enemy death and victory | diff | Original: hit -> "Dead" label under the HP bar, the enemy collapses and its body stays on the ground (no fade), HP bar and intent go away; Burning Blood heals with green number and green particles on the player (~0.9-1.6 s); hand slides down; screen dims and the Loot panel opens at ~4.0 s. Ours: the enemy fades out by ~0.9 s (no corpse), rewards at ~2.0 s. |
| M5 | Buff / debuff text colours | diff | Original buff names are green with green rising particles ("Strength"), debuffs red; ours buff names are yellow. |
| M6 | Status text on exhaust | diff | Ours shows a "消耗: <card>" label over the hand when a card is exhausted (end-of-turn ethereal, Blade Dance, Tremble = C3's stray label); the original has no text, only the smoke dissolve. |
| K2 | Card created in hand (Blade Dance) | diff | Original: Blade Dance holds in the play area, the three Shivs appear and slide into the hand (~0.4-0.6 s), then Blade Dance exhausts in place as smoke (~1.0-1.3 s). Ours: shivs enter the hand from the lower left, the played card vanishes at once (C1/C3). |
| K5 | Ethereal at end of turn | diff | Original: Dazed dissolves into smoke at the hand while the rest fly to the discard pile. Ours: label text only (M6), no smoke. |
| U1 | Playing without energy | diff | Original: no visible reaction to dragging an unaffordable card in 1.7 s (to recheck: the original may show a thought bubble). Ours: the card lifts with an arrow and a red "我没有足够的能量。" line appears. End Turn button glows cyan in the original once energy is 0. |
| U2 | Draw pile view | ok-ish | Different layout by design (3DS list + detail); original opens a dimmed full-screen grid with a back button and a footer hint. |
| S1 | Fast mode | diff | All captures here are at normal speed on both sides (original profile2 `fast_mode: normal`, preview without settings = fastMode off). The original's Fast mode does not speed everything up: it shortens specific waits (`Cmd.Wait` per-mode timings, CardPileCmd 0.3 -> 0.2 s tweens, CombatManager end-of-turn card 0.8 -> 0.4 s, NCreature / health bar). Ours scales the whole scheduler by 1.75. Compare in fast mode separately (owner plays with it on). |
