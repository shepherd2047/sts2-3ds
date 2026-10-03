# UI audit and work packages (2026-10-02/03)

Audit of the SDL preview against screenshots of the original game, taken on the Mac from the owner's
copy (kept locally only, never committed). Owner feedback from the same session is folded in.
Status (2026-10-03): the branches below, the potion belt and the details after them are **merged into main** (`ui-integration`), checked in Azahar (combat, shop, rest, map). Tint-blend users left: only the map's ink icons (map.cpp, they read fine in Azahar). Card dimming is an overlay since 79cf59e. Item 2 done (yellow triangle arrows = settings_tiny_*_arrow, no lone last character, portraits / disabled buttons / rest icons dimmed without tint blend). Item 12 done (ribbon title, rarity counts as NDeckHistory / NRelicHistory); 10 and 14's arrow tip: no change needed (the owned-relic page has no parchment; the C# arrow tip follows the cursor). The text below is the original backlog. Work in lanes on disjoint files.

## Owner feedback / rules learned this session

- No debug-looking UI: no plain text buttons, no flat colour blocks, no square-cornered placeholders.
  Use the original's art (pck) for every button, plate, ribbon and icon.
- **Never rely on citro2d tint `blend`** (it is passed through a proctex unit, Azahar draws it as 0).
  Bake pre-coloured sprites in `tools/build_assets.py` (see `ui/hp_fill_<RRGGBB>`) or draw with blend 0.
  A global rework of the tint path (mesh TEV) scrambled every texture on the 3DS and was reverted.
- Verify any 3DS rendering change in Azahar (debug file + `STS_SHOTS`, see CLAUDE.md) before handing
  the build to the owner.
- Combat selections auto-pick when every option must be taken (merged, 30bda5a).
- Potions on the bottom screen should be small and quiet (they must not compete with the cards).

## Branches pushed to origin

- `ui/potion-belt` (54114a3, three commits on top of 30bda5a): bottom-screen potion belt in the
  original PotionContainer look (top_bar_char_backdrop 9-slice, potion_placeholder for empty slots),
  20 px bottles (`potion_s/<KEY>` baked at 20 px), drag a bottle up to use it like a card, tap opens
  the potion page. Owner asked for it smaller (done in 54114a3); **awaiting owner OK, then merge,
  run build_assets, check in Azahar**. Files: combat_ui.cpp, ui.h, build_assets.py.
  WP5 builds on top of this branch.

- `combat-fx` (branch on origin, on top of `ui/potion-belt`, 2026-10-03, awaiting owner OK; run
  `python tools/build_assets.py` after merging): WP5 items 13 (playable-card outline: `card/glow_cyan|gold|red`
  baked from `card_frame_sdf.exr` by `bake_card_glow`, behind every playable hand card and the held card; gold /
  red from the enchantment's `shouldGlowGold/Red`), 14 (no play line or hint boxes while dragging, only the red
  energy line), 15 (keyword tips of the raised / dragged card stack on the top screen, `drawCardTipColumn`
  shared with the detail page), 16 (inspect chips on the hover_tip art, no repeated name), 17 (tap on a belt
  bottle opens a Drink / Discard popup, potion text on the top screen). Also: damage numbers follow
  NDamageNumVfx (hop with gravity, 2.5x -> 1x pop, red -> cream, 2 s fade) and the low-HP hurt vignette
  (NLowHpBorderVfx, plays when a hit leaves the player at <= 25% HP; `STS_HURT_DEMO=1` replays it every 2 s).
  `tools/gdpck.py` reads RGBAH textures. 3DS `make` passed on this PC (devkitPro in MSYS2).

- More branches on origin, each from `main`, awaiting owner OK (2026-10-03, preview-checked only):
  `map-ui` (WP2 items 3-4: no Dev button or stray label, art Back button, coloured legend, nodes 1.6x),
  `choice-ui` (WP4 items 7-9: ribbon titles via `widgets::title`, card reward without Details/Select,
  rest site without empty box / Confirm), `relic-row` (WP6 item 18: relics in a row under the bar on room
  screens except events; boss preview and Defect orbs move down by `relicRowH()`). Merging them will
  conflict slightly in rest.cpp / combat_ui.cpp titles: keep the ribbon title and the `+ relicRowH()` offsets.

- **Review `ui-integration` first** (origin, 2026-10-03): `main` + potion-belt + combat-fx + map-ui + choice-ui +
  relic-row + card-face (type plaque) + shop-fix, conflicts resolved, preview-checked on combat / rest / shop / map,
  3DS `make` passed and the combat screens were checked in Azahar (glow colours, vignette, tips, belt all draw
  correctly; `sdmc:/sts2-debug.txt` with STS_HURT_DEMO / STS_RELICS / STS_POTIONS). Needs `python tools/build_assets.py` (new glow / vignette / plaque art). Not done: item 5
  (grid mini-card text; the body stays empty below s 0.6), 10 (relic page columns: unclear which art is the
  parchment), 11-12 (deck view first focus, history titles), 14's arrow tip (the C# aims the arrow at the mouse; the
  port already matches its head offset). Combat debug: `STS_HURT_DEMO=1` replays the hurt vignette.

## Issues (most visible first) and packages

### WP1 Button kit — legacy_widgets.cpp, title.cpp, custom_run.cpp, daily_run.cpp
1. Flat brown buttons: map HUD, relic view, card library, relic/potion collections, bestiary,
   character select, daily, custom, combat 信息, dev menu all use legacy `App::button` (rect + border).
   Draw it with the widgets art (back = blue arrow plate, proceed = red arrow, others = secondary plate).
2. Character select / custom / daily `<` `>` are grey squares; original uses yellow triangle arrows.
   Ascension text leaves a lone "玩。" on its last line.
   Plan: `App::button` (legacy_widgets.cpp): back/close/cancel or ID_BACK -> `ui/btn_back`, highlighted
   start/continue/confirm/use -> `ui/btn_proceed`, everything else -> `ui/btn_confirm`; highlighted
   toggles (dev menu, 查看升级) get gold text. Triangle arrow art still to be found in the pck.
   **Other tint-blend users that Azahar will draw wrong** (fix with pushAlpha / a dark overlay / baked
   sprites): `widgets::button` disabled state (widgets.cpp:155, blend 0.55), unselected character
   portraits in title.cpp (blend 0.35), the title menu reticle (blend 1). Grep the UI for any other
   non-zero blend argument.
   Preview hooks: `STS_NO_NEOW=1`, `STS_OPEN_CUSTOM=1`, `STS_OPEN_DAILY=1`.

### WP2 Map — map.cpp
3. 开发 (Dev) button visible in normal play (move behind SELECT); bottom text buttons for deck/relics
   duplicate the top bar; stray "敌人" label on a black box bottom-right (map.cpp ~316-320, 347-351).
4. Nodes ~8-10 px, too small to tap; original zooms to a few rows. ~1.6x nodes, more scrolling,
   touch targets >= 20 px. Legend icons are black ink and stick out of the panel; original is coloured.
   Findings: SELECT already opens the dev menu (ui.cpp ~364), so just delete the Deck/Relics/Dev
   buttons (map.cpp:316-318) and their ID_DECK / ID_RELICS / ID_DEVMENU handlers; keep Potions until
   the belt lands. Delete the focus label box (map.cpp:347-352). Room icons are already coloured: the
   legend draws them with ink 0x2E241A at **blend 1** (map.cpp:333) — draw white at blend 0 (another
   Azahar tint case). Legend panel art has ~10 px transparent margin: start icons at kLegendX+14, widen
   the panel to ~70 px. kMapS / kNodeScale live in ui_common.h but only map.cpp uses them: a local
   ~1.6x zoom in mapPos, the parchment draw, mapScrollRange and the auto-scroll target; tap radius >= 20 px.

### WP3 Card face — card_view.cpp (+ drawCard calls in card_library.cpp, shop_ui.cpp)
5. Grid mini-cards (deck, smith grid, shop, library; s ~0.42-0.46, body ~48x29 px) have an empty grey
   body and a ~5 px title. Plan: `mini` flag on drawCard; title at full F12 (now capped by
   `maxH = 23*s`), description at a scale floor ~0.7 cut with "…".
6. Missing type plate. Art: `images/atlases/ui_atlas.sprites/card/card_portrait_border_plaque_s.tres`
   (cyan, 123x75). The original colours it with the rarity banner material (NCard.UpdateTypePlaque):
   bake `card/plaque_<rarity>` with hsv_shader + material_hsv(card_banner_<r>_mat) like `card/border_*`.
   Geometry (card.tscn): horizontal 9-patch, margins 13/12, width max(label+17, 61), height 37 game
   units; our frame maps 300x422 -> 120x169 (0.4): centred at x 60*s, top y+84.8*s, height 14.8*s,
   min width 24.4*s; draw as a manual 3-slice with gfx::image sub-rects (nineSlice keeps fixed margins).
   Label: original 16-unit Kreon, black 75% alpha; use F12 ~9 px, hide at grid sizes.
   `typeName(CardType)` exists in card_library.cpp — move it where card_view.cpp can use it.
   Move the affliction tag up to ~84*s (it overlaps the plate).

### WP4 Choice screens — deck.cpp, reward.cpp, rest.cpp, relics_ui.cpp, settings.cpp, stats_screen.cpp
7. Titles crossed by their 2 px teal underline. Ribbon art already exists: `ui/reward_banner`
   (build_assets.py ~914, drawn by `widgets::banner`). Make `screenTitle` (deck.cpp, file-local now)
   a shared ribbon title (declare in ui_common.h) and use it in reward.cpp:60, rest.cpp:166,
   relics_ui.cpp:81, settings.cpp:177.
8. Card reward: remove 详情 and 选择 (chooseOneDraw/chooseOneUpdate), centre 跳过, show the focused
   card large on the top screen; DOWN only moves to the button row when a button is shown.
9. Rest site: delete the empty-state box (rest.cpp ~223-227) and the disabled 确认 (A on a focused
   option already picks via widgets::hit).
10. Relic view: drawRelics uses 6 columns of 48 px (~288 px) but the parchment is ~225 px: use ~4
    columns or size columns to the parchment (confirm what draws the parchment first).
11. Deck view: focus card 0 on open (top screen shows it at once), open details on the first tap,
    "牌组 i / N" bottom-right of the top screen.
12. History: "历史记录" title hits the panel border; empty message repeated on both screens.

### WP5 Combat — combat_ui.cpp, combat_scene.cpp, combat_hand.cpp (on top of ui/potion-belt)
13. **Playable-card glow (owner request, top priority).** Original: "Highlight" TextureRect in
    card.tscn drawing `images/packed/card_template/card_frame_sdf.exr` with
    `shaders/card_ripple.gdshader`; NCardHighlight tweens shader width 0 -> 0.075 over 0.5 s
    (AnimHide back over 0.5 s, AnimFlash 0.15 then 0.075). Colours: playable cyan (0, 0.957, 0.988,
    0.98), gold (1, 0.784, 0, 0.98), red (0.83, 0, 0.33, 0.98); NHandCardHolder.cs ~620-640 picks
    them. Rules side already has `shouldGlowGold()` / `shouldGlowRed()` (game.h). Plan: bake a soft
    glow from the SDF in each colour, draw behind every playable hand card in the fan loop
    (combat_ui.cpp ~687), fade via vertex alpha (no tint blend).
14. Targeting overlay: remove the boxed green hints and the grey play line (combat_ui.cpp ~710-787,
    card and potion drags); the arrowhead stops 88*k before the target centre (combat_hand.cpp
    drawArrow) and covers the body — end it at the reticle edge like the original.
15. Enlarged card shows no keyword tips (e.g. 痛击 -> 易伤); original shows them beside the zoomed
    card. Show them on the top screen.
16. 信息 screen: "敌人" subtitle overlaps the name; name block repeated (combat_scene.cpp ~720);
    flat tiles; empty bottom half.
17. Potion tap: original opens a small popup by the bottle with Drink / Discard + the tooltip, not a
    page (tap currently opens the full page, combat_ui.cpp ~1043).

### WP6 Top bar and shop — topbar.cpp, shop_ui.cpp (not its drawCard calls)
18. Relic strip in the bar is ~1-2 icons wide with "+N" chips over relics. Original: relics in their
    own row under the bar. Plan: row at y ~19-35, 16 px icons 18 px apart from x 3, "+N" chips in
    reserved end slots, ZL/ZR focus ring in the row with the tooltip under it; only on room screens
    (map, combat, reward, rest, offers, shop, event), not over deck/relics/potions pages. Check event
    and Ancient titles (event.cpp, y ~20-28) and move the shop focus panel down to ~40.
19. Shop: relic counter "0" over its price (`drawRelicIcon` always draws the counter — draw the sprite
    directly); full-width "点选商品即可购买" bar covers the merchant (x ~255-310) — small hint instead.
    `STS_RELICS=<ClassNames>` gives relics for screenshots.

Not covered by the audit: game over / victory, treasure, in-combat power tooltips.

## Original-game observations (PC, 2026-10-03, Ironclad A1 new run; shots kept in ../sts2-ref-shots, not in git)

- Operating the original on the PC (computer-use): see memory `operate-original-game-pc`. Card plays by synthetic
  mouse never worked; the dev console (settings.save `"full_console": true`, backtick) jumps rooms: `win`, `room Shop|RestSite|Treasure|Event|Elite`.
- Top bar (items 18): portrait+ascension badge, HP, gold, 3 potion slots in a dark rounded plate, floor/act
  counters, run timer, map and deck(count) buttons, settings gear. Relics sit in their **own row under the bar
  at the left** (icons ~28 px at 1456 wide, ~34 px gap), no "+N" chip seen with 2 relics.
- Map (items 3-4): parchment fills most of the screen; only ~4 rows visible; nodes are large (~7% of the
  screen height), coloured for the next choices, grey/faint for others; Legend panel (blue paper, coloured icons,
  English labels) on the right; back arrow bottom-left, a "Share" button and 3 drawing tools bottom-right/left.
- Combat (13-15): playable cards have a cyan outline glow; hovering a card lifts it and shows the keyword tip
  (e.g. Vulnerable) as a dark panel to its right; aiming draws a segmented grey arrow ending in a big
  arrowhead at the enemy and the card stays raised in the hand; enemy name appears under its HP bar on hover;
  intent icon + number above the enemy; draw pile bottom-left, discard bottom-right, energy orb left,
  End Turn plate right.
- Character select (2): big portrait, HP/gold line, relic text, yellow triangle arrow beside the ascension
  line, row of 5 portraits + "?" below, back (red) bottom-left, check (blue) bottom-right.
- Card library (5): 5 columns of full-size cards (type plaque visible), left filter panel with search, card
  type, rarity, cost, A-Z; red back arrow bottom-left; click opens a large card with yellow side arrows.
- Reward list (item 8): "Loot!" ribbon over a dark teal panel with rows (gold, potion, "Add a card to your deck."), each a
  full-width teal plate with a small icon; red "Skip" arrow bottom-right. Card choice: "Choose a Card" ribbon, three
  full cards side by side, hovered card enlarged with cyan glow, **no Details/Select buttons**, one teal "Skip" plate
  centred under the cards.
- Rest site (item 9): only two big square option cards (Rest, Smith) with captions; no empty box, no confirm button.
- Shop: merchant scene with a red "Proceed" arrow bottom-right; clicking the merchant opens the inventory over the
  whole screen: cards (price under each, % sale badge), potions and relics in a loose grid, card removal coin; red back
  arrow bottom-left; the relic row stays visible under the top bar.
- Treasure: "What's Inside?" ribbon, relic floating over the open chest, red Skip arrow. Event: illustration on the
  left, title + text + choice plates on the right (gold name, effects coloured).
- Top bar with many relics (item 18): 9 relics fit in one row, ~34 px apart (at 1019 px width), tooltips open
  below the hovered relic with keyword tips stacked under it. Potion tap opens a small popup under the belt slot:
  Drink (disabled out of combat) / Discard, plus the potion tooltip to its right (item 17).
- Combat: block shown as a shield+number on the left end of the HP bar; powers as small icons under the HP bar
  (hover shows a tooltip beside it); sleeping elite shows "zz".
- Deck view (item 11): sort header bar (Obtained / Card Type / Cost / A-Z), 5 columns of full cards, red back arrow
  bottom-left, "View Upgrades" tick bottom-left, hint line at the bottom centre.
