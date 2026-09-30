# UI style guide (F0)

The numbers here are in `source/ui/style.h`; widgets (F3) read them from there. The
mock-ups (`STS_MOCK=1|2|3`, `source/ui/style_mock.cpp`) show them on screen: 1 reward list,
2 combat bottom screen, 3 event. Change a number in the header, not in a screen.

## Screens and layout

- Top 400 x 240 is **look**, bottom 320 x 240 is **touch**. Nothing the player must tap is on the
  top screen. A continuous scene (map, tall background) stays inside x 40..360 of the virtual canvas
  (CLAUDE.md layout rule).
- Outer margin **8 px**, gap between controls **4 px** (8 between groups). Text never touches an edge.
- Bottom **action bar**: buttons 34 px tall, top at y = 198. Primary (confirm, proceed) at the right,
  secondary (details, back, deck) at the left, at most three buttons. Back is always bottom-left.
  **Combat exception:** the combat bottom screen keeps the owner's measured RGDSplus layout (CLAUDE.md):
  the hand is centred with card text visible, energy on the left and 结束 on the right just below the
  hand, the piles (and potions) in the corners, no status strip. It has more than three controls; each
  is still at least 32 px to touch and none overlaps a card's text.
- Top **status bar** (S19, `source/ui/topbar.cpp`, C# NTopBar): 18 px, black at 72 %, teal rule on its
  bottom row, `ui/tb_*` icons. Left to right as in the C#: HP (red), gold, potion belt, floor + act
  (阶段N), ascension badge; the relic strip fills the middle (16 px icons with their counters, scrolls
  sideways with "+N" chips at the ends; a new or flashing relic scrolls into view); right: run timer
  (always with the ShowRunTimer setting, else only on the map / under a page), deck + count, map, pause.
  The top screen is not touchable, so **ZL / ZR** (New 3DS; preview keys E / R, scripts `ZL` / `ZR`)
  or **L + R together** (every 3DS; preview Q + W, scripts `L+R`) enters its focus mode: ←→ walk,
  L / R jump groups, the focused item's hover tip shows under the bar, A opens (potion → the use /
  discard popup on that slot, relic → detail, deck / map / pause), B / ZL / ZR / L+R / a touch leave.
  The chord fires on the frame the second of L / R goes down; `App::update` consumes that frame's L and
  R, so no screen also takes it as a single L or R. Pages opened from it return to it. Shared by every room.
- Screen title: F16 at 1.25x (about 20 px), gold, with a 2 px teal line under it. Only one title per
  screen, on the top screen except for pure list pages.
- Panels: fill `kPanel`, 1 px teal border, a faint 1 px highlight inside the top edge. Square
  corners (the art is plated).
- Scrims: a scene background under a page is dimmed with `kScrim`-family black: 0.25 (combat top),
  0.4-0.55 (pages), 0.6 (placeholders).

## Touch and controller

- Minimum touch target **32 x 32** (a stylus is precise, a fingertip is not). Rows are 36 px, standard
  buttons 34 px, icon buttons 32 px, list rows and option buttons are the full width of their panel.
  Cards in a grid may be smaller but always have a hit box of at least 32 px wide (grid mini card = 55 px).
- Every screen can be played with the D-pad + A/B. **One input model**: a focus ring on exactly one
  control; the D-pad moves it to the nearest control in that direction, A presses, B is back / cancel,
  L/R switch tab or page, X opens details or inspect, Y is the secondary action (potions in combat),
  START pauses / settings, SELECT is the developer menu.
- Touch: a control is **pressed on release** if the stylus is still on it; it highlights on touch-down.
  Dragging more than 5 px off a control cancels the press. Scroll lists start scrolling after 5 px.
  **One tap picks** (owner decision 2026-09-30): a tap on an event / Ancient option, a shop item, a
  rest-site option, an offered relic or a potion target chooses it at once (a refused shop tap plays
  the merchant's line and leaves the item focused; a single-card deck choice goes to its 确认 / 返回
  review). The D-pad focuses first, with the preview on the top screen, and A picks. Buttons activate
  on the first tap. Exceptions: multi-select grids (a tap toggles a pick) and inspection lists (card
  library, collections, deck view: a tap focuses a card to look at it, a second opens its detail).
- Long-press (0.35 s) on a card, relic, potion or keyword shows its tooltip / detail; releasing closes it.
- The focus ring is not shown while the player is using the stylus, and appears at once when a D-pad key
  is pressed.

## Type

Two font pages, F12 (12 px) and F16 (16 px). Scaling is allowed between 0.8 and 1.6 only where noted.

| Use | Font | Colour |
|---|---|---|
| Screen title | F16 x 1.25 (x 1.6 for victory / death banners) | gold |
| Button label, row label, card title | F16 (shrinks to fit, min 0.75) | cream |
| Body text, descriptions, tooltips | F12 | cream |
| Numbers on cards (cost) | F16 | cream, green when reduced, red when raised |
| Small counters (HP under bars, relic counters) | F12 x 0.85 (the smallest allowed) | cream |
| Disabled | same size | grey `col::gray` |
| Hint / reason line under a locked control | F12 | red |
| Section labels, rarity, sub-titles | F12 | gold |

Colour words in loc text use the rich-text tags (`[gold]`, `[blue]`, `[green]`, `[red]`, `[purple]`).
Gold is for names, values and emphasis, blue for block / defence and cards, green for good,
red for damage / bad, purple for special. Shadow is on for all text (needed over scene art).
Max line length on the bottom screen is 300 px, on the top 380 px.

## Palette (`style.h` and `res.h col::`)

Sampled from the game's own plates (F1): slate-teal panels, red ribbon for the main action,
blue ribbon for OK, gold only for text and the focus ring.

| Token | Value | Use |
|---|---|---|
| `kClear` | 0B0B12 | screen clear |
| `kPanel` / `kPanelEdge` / `kPanelHi` | 22323B / 4F8790 / 8FC1C8 | panel fill, border, title underline |
| `kPlate` / `kPlateHover` / `kPlatePress` / `kPlateOff` | 2E4A57 / 3B6272 / 203641 / 262A2C | button states |
| `kPrimary` | 872420 | the one main action on a screen (red ribbon: proceed, end turn) |
| `kOk` | 36567D | blue OK ribbon in popups |
| `kDanger` | 821F16 | abandon, delete, remove (always behind a confirm) |
| `kEdge` / `kEdgeOff` | 6FA6AE / 555555 | button border |
| `kFocus` | FFD870 | focus ring, selected outline |
| `col::white` (cream) / `gold` / `blue` / `green` / `red` / `purple` / `gray` | FFF6E2 / EFC851 / 87CEEB / 7FFF00 / FF6563 / EE82EE / 9A9A9A | text |

The flat fills are the fallback; the widget kit (F3) draws the game's own art: `ui/btn_proceed`
(primary), `ui/btn_confirm` and `ui/btn_row` (secondary, list rows), `ui/btn_ok_s` / `btn_cancel_s`
(popups), `ui/btn_event` (event options), `ui/panel_*` and `ui/hover_tip` (panels, tooltips),
`ui/top_bar` and `ui/tb_*` (status bar), `ui/tab_*`, `ui/checkbox_*`, `ui/scroll_*`. Every baked
sprite and its 9-slice margins (`gfx/nine.txt`) can be browsed with `STS_MOCK=4` (`STS_MOCK_PAGE=n`,
`STS_MOCK_PREFIX=power/`).

## Button families

1. **Primary**: red-ribbon plate, one per screen (Proceed, Confirm, End turn, Start).
2. **Secondary**: brown plate (Details, Deck, Back, Skip).
3. **Danger**: red plate, always behind a confirm modal.
4. **Icon button**: 32 x 32, art only (piles, potion, deck, map, settings); a 4 px hit padding.
5. **List row**: full-width 36 px, 32 px icon at the left, F16 label, right-aligned F12 value or state
   ("已领取", rarity). Used by rewards, shop lists, settings, run history.
6. **Option button**: full-width 44 px with a centred F16 label, used by events and Ancients; a locked one
   is drawn as disabled with the reason on a red line under it.
7. **Tab**: 32 px tall, selected tab has a teal underline and a lighter plate; L/R switch.
8. **Toggle / checkbox**: 32 x 32 hit box, tick art from `checkbox_*`; **slider**: 32 px tall track, 16 px knob,
   D-pad left/right steps by 10 %.

States for every control: **normal**, **focus** (lighter plate + 2 px `kFocus` ring), **pressed**
(darker plate, label moves 1 px down, no sheen), **disabled** (grey plate and text, not focusable,
not registered as a hit).

## Motion

| What | Time | Curve |
|---|---|---|
| Button press-in | 0.08 s | linear |
| Screen change | 0.18 s fade through black | linear |
| Panel / modal slide-in | 0.22 s from 12 px below | ease-out cubic |
| Toast | 1.6 s, fades in 0.15 s, out 0.3 s | linear |
| Tooltip delay | 0.35 s (long-press) | – |
| Gold / HP counters | 0.4 s | ease-out |
| Focus ring pulse | 1.2 s, alpha 0.7-1.0 | sine |
| Card flight (play, discard) | 0.25 s | ease-in for discards, ease-out for draws |
| Damage number | rises 24 px in 0.8 s, fades in the last 0.3 s | ease-out |

Fast mode (settings) speeds all combat timing up by 1.75x; UI timings above do not change. Screen shake
is a setting (default on) and never moves the bottom screen.

## Sound (wired in U4; names are the C# events)

| Control | Event |
|---|---|
| Press a button, pick a row | `event:/sfx/ui/clicks/ui_click` |
| Focus / hover change | `event:/sfx/ui/clicks/ui_hover` |
| Back / cancel / close | `event:/sfx/ui/clicks/ui_back` |
| Toggle on / off | `event:/sfx/ui/clicks/ui_checkbox_on` / `_off` |
| Open / close the map | `event:/sfx/ui/map/map_open` / `map_close`; node pick `map_select` |
| Pause open / close | `event:/sfx/ui/pause_open` / `pause_close` |
| Gold gained | `event:/sfx/ui/gold/gold_1..3` |
| Relic flash | `event:/sfx/ui/relic_activate_general` (`_draw` for draw relics) |
| Card moves | `event:/sfx/ui/cards/card_movement_*` |
| Energy gained | `event:/sfx/ui/gain_energy` |

Until U4, the widget kit only calls `ui::sfx(Sfx::Click)` style hooks that do nothing.

## Rules for screen authors

1. Use widgets and tokens; no raw colour or size literals in screen code except art positions.
2. Every screen has a focus order (top-to-bottom, then left-to-right) and works with the D-pad alone.
3. Anything longer than the space scrolls in a ScrollList; never shrink body text below F12 x 0.85.
4. Show why a control is locked; never silently disable it.
5. Check both screens in preview screenshots before committing (Chinese and, once Y3 lands, English).

## Renderer features (F2, `gfx.h`)

`nineSlice` (margins come from `Sprite::nl/nt/nr/nb`), `gradient` (four corners) and `rectGradient`,
`imageRotated`, `pushClip/popClip` (scroll lists; cuts image, rect, text, gradient; transformed draws are only
skipped when fully outside), `pushAlpha/popAlpha` (fades, disabled widgets), text `outline` (4 extra draws per
glyph: titles only) and `shadowColor/shadowDx/shadowDy`. Tint, blend, scale and rotation already existed
(`image` tint/blend, `pushTransform`). `STS_MOCK=5` shows all of it; the 3DS side is compile-checked only.

## Widget kit (F3, `source/ui/widgets.h`)

Immediate-mode, art-backed (F1 sprites + F2 9-slice/clip/alpha), one input model for touch and
D-pad/A/B/L/R with a focus ring (`widgets::beginFrame(in)` / `endFrame()` once per frame; each
widget takes a stable `id`, draws, registers its hit box, returns whether it fired). Controls:
`button` (Primary/Secondary/Danger/Row/Event/Ancient), `iconButton`, `row`, `optionButton`
(locked reason line), `tabs`, `toggle`, `slider`, `paginator`, `ScrollList` (drag + clip;
inertia and a thumb), `toast`, `banner`; confirmations and error popups use the one shared modal in
`source/ui/confirm.h` (S22). D-pad focus moves to the nearest
control in the pressed direction using the previous frame's hit boxes; touch always wins and
hides the ring. `STS_MOCK=6` exercises all of it. Screen files in `source/ui/screens/*.cpp`
adopt this kit as each S package rebuilds that screen; until then they keep using `App::panel`/
`App::button` (`source/ui/legacy_widgets.cpp`), so no screen is built twice.

## Rich text (F4)

Inline icons: `[icon:energy|gold|hp|star|block|<atlas/name>]` in any `Res::text` string, square
and sized to the line's font height (`Res::iconIndex` allocates a private-use codepoint per
unique name; `Res::text` draws the sprite instead of a glyph). Keyword colour is already baked
into the game's own loc strings (`[gold]牌组[/gold]` etc.) -- nothing to add there. `choose()`:
`expandSmart` (relics/potions/events; `App::describe` is the separate card-only path and keeps
its own DynVar-diff colouring) now supports `{Name:choose(V1|V2|...):alt1|alt2|...[|fallback]}`,
matching Name's DynVar value (numeric) or an event's string var against V1.. and expanding the
matching alt (a bare `{}` inside it is replaced with Name's value); an extra trailing alt is the
no-match fallback. Also fixed: `{IfUpgraded:...}` was hardcoded to `false` in `expandSmart`
(cards were fine; relics/potions/events were not) -- it now takes an `upgraded` parameter.
MadScience's card text additionally needs per-card string/bool extra description args (C#
`CardModel.AddExtraArgsToDescription`) that `Card` does not model yet; that is an engine hook for
whichever package ports TinkerTime (A7d), not a UI-lane change. `widgets::keywordTip(title, desc,
x, y, top)` draws a `ui/hover_tip` glossary popover anchored to a point, flipped to stay on
screen -- the reusable piece for a HoverTips-style glossary; wiring it to a press-and-hold on
keyword words in card/relic text is left to the S packages that rebuild those screens.
`STS_MOCK=7` shows all of it.

## Card renderer v2 (F5)

`App::drawCard` now draws: the correct frame (`card/frame_ancient` for Rarity::Ancient, the
type frame otherwise), a rarity-coloured title outline (`rarityOutline`, StsColors
`cardTitleOutline*`; skipped below `s = 0.55` where it stops reading), the unplayable icon
(`card/unplayable`) in place of the cost gem when `kwUnplayable`, and an enchantment badge
(`card/enchant_badge` + `enchant/<KEY>`, dimmed while disabled, its amount shown when
`showAmount()`) at every size including the ~0.5-scale 5-card hand -- the badge is the only sign
an enchantment is there, so it never disappears, just floors at a legible pixel size. The
existing single `drawCard(..., s)` already covers all three sizes (hand ~0.5-0.62, grid mini
0.46, large detail 1.1-1.3); F5 did not need a second code path. Affliction overlays wait for A4
(no affliction cards exist yet to test against). New F1 art baked for this: `card/frame_ancient`,
`card/ancient_banner`, `card/enchant_badge`, `enchant/<ENCHANTMENT_KEY>` (one per registered
enchantment, `missing_enchantment.png` fallback).

Also fixed while touching card/event text: a plain `{Name:a|b}` conditional on a flag this port
doesn't model (`IsMultiplayer`, per-card extra args like MadScience's riders) now defaults to
false and picks the empty/second alt, instead of printing "?" for the whole clause.

## Motion (F6)

New this package: a `style::kFade`-second fade through black on every `run_->screen` change
(`App::transitionT_`, drawn in `App::draw` after the screen's own content, both screens) and the
top bar's HP/gold numbers easing towards their real value (`App::shownGold_/shownHp_`,
exponential approach over `style::kTick`, reset to snap on `startRun`). Everything else the
package lists was already in place from earlier work and needed no new code: card draw/discard/
exhaust flights and the fanned-hand animation (`animateHand`/`drawGhosts`/`startFlight`/
`drawFlights`, `source/ui/screens/combat_hand.cpp`), damage/block/heal floating numbers
(`App::Float`, `trigger()`), relic flash (`Relic::flash`, `drawRelicIcon`), and per-creature hit
shake gated by the `screenShake_` setting (`combat_scene.cpp`). Button press feedback exists in
the new widget kit (F3, `states[id].press`); the still-unmigrated screens' `App::button` gets it
when their S package moves them onto the kit.
