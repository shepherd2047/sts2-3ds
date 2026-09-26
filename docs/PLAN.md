# Development plan

## Goal

A finished, fully playable Slay the Spire 2 on the New 3DS, as complete and
polished as the RGDSplus dual-screen port of StS1
(github.com/LPF970915/Slay-the-Spire-for-RGDSplus, cloned next to this repo as
`../rgds-ref`): every screen follows its top/bottom split and touch rules. Their
page inventory `../rgds-ref/docs/R4_ALL_PAGES.zh-CN.md` (U01-U33, with the owner's
revisions in `R4_LAYOUT_REVISION.zh-CN.md`) is the checklist for "done"; the numbers
behind their layout are in `prototype/r3/java/rgds/r3/` (`DualRender.java`,
`UiTransform.java`, `ScreenRoutes.java`, `MapTouch.java`). Their code only re-routes
StS1's own UI, so we copy the **layout rules and proportions**, drawn with StS2's
own art. The owner's measured rules that already apply are in CLAUDE.md.

Done means: three acts with all encounters, events, relics, potions and the shop;
Neow; saves; all RGDSplus pages (U01-U33) that exist in StS2 laid out; runs smoothly
on real hardware.

Work is split into **packages**. Each one is sized for a single session (Sonnet is
fine unless marked *Opus*) and says what to read, what to write, how to test and
when it is done. Pick the first package whose status is `todo` and whose
dependencies are `done`, do it, then update its status line here in the same commit.

## Session protocol (every package)

1. `git pull`. Read `CLAUDE.md`, this file, and `docs/PORTING.md`. Do **not** read the
   whole codebase or the whole decompiled tree: open only the files a package names
   and the C# classes it lists. Tokens are shared by all the owner's sessions.
2. Source of truth is `../sts2-decompiled/` (C#). Translate faithfully: same numbers
   (non-ascension value = last argument of `GetValueIfAscension`), same move order,
   same RNG call order. Drop VFX/SFX/animation code.
3. Mark anything you cannot express with `// PORT NOTE: <what is missing>`.
4. Test (commands below). Build **both** targets before committing:
   `make -f Makefile.sdl` (UCRT64 shell) and `make` (devkitPro MSYS shell). The 3DS
   build has **no RTTI and no exceptions**.
5. For UI changes: screenshot the preview (`STS_SHOTS`), check both screens, and show
   the owner before sending anything to the 3DS. Never `make link` unless asked.
6. Commit with a clear message, `git push`, and set the package's status here.
   Talk to the owner in Chinese.
7. Do not start subagents without telling the owner how many and why (shared quota).
8. Parallel sessions: only run two packages at the same time if both are marked
   *parallel-safe*. Packages marked *engine* edit `game.h` / `combat.cpp` / `run.cpp`
   and must run alone. Keep engine edits small and in the style of the existing hooks.

### Test commands (UCRT64 shell, repo root)

```bash
make -f Makefile.sdl                                   # preview + sim
STS_ENCOUNTER=<EncounterId> SIM_FIGHTS=1 ./build/sim 4   # a fight, headless
STS_ENCOUNTER=<EncounterId> SIM_ALLCARDS=1 ./build/sim 1 v   # verbose event log, 999 HP
SIM_ALLCARDS=1 ./build/sim 20                          # whole runs must all say WIN
STS_ROOM=Event STS_EVENT=<EventId> SIM_FIGHTS=1 ./build/sim 3   # an event, headless
# preview screenshots (see CLAUDE.md for STS_SCRIPT syntax):
STS_ENCOUNTER=<Id> STS_HIDDEN=1 STS_FIXED_STEP=1 STS_SEED=42 STS_SCRIPT="40:A,100:A" STS_SHOTS="400:build/x.bmp" ./build/sts2-preview.exe
```
New monsters/relics/events need `python tools/build_assets.py` (art and text are
collected from `MONSTER_HEADER` / `RELIC_HEADER` / `EVENT_HEADER` / `CARD_HEADER`).
PowerShell mangles quotes and pipes in `bash -lc '...'`: put commands in a `.sh`
file and run it with bash.

## Status

| # | Package | Kind | Depends on | Status |
|---|---|---|---|---|
| 0 | Event text string vars | engine, small | – | done |
| 1 | Act 1 events (12 left) | content | 0 | todo |
| 2 | Shared events (18) | content | 0 | todo |
| 3 | Multi-act run structure | engine, *Opus* | – | todo |
| 4a | Act 2 monsters A | content | – | todo |
| 4b | Act 2 monsters B | content | – | todo |
| 4c | Act 2 elites + bosses | content (+engine likely) | – | todo |
| 5 | Act 2 events (10) | content | 0 | todo |
| 6a | Act 3 monsters A | content | – | todo |
| 6b | Act 3 elites + bosses | content (+engine likely) | – | todo |
| 7 | Act 3 events (7) | content | 0 | todo |
| 8 | Potions | engine + UI, *Opus* | – | todo |
| 9 | Shop (merchant) | engine + UI | 8 | todo |
| 10 | Remaining relics (skipped ones + Shop rarity) | content | 8, 9 | todo |
| 11 | Ancients (Neow, act-start events) | content + UI | 3 | todo |
| 12 | Saves | engine | 3 | todo |
| 13 | Texture compression (romfs is ~70 MB) | tools + 3DS gfx | – | todo |
| 14 | Other characters | big | 3 | later |
| 15 | RGDSplus page audit (U01-U33) | UI | 3, 8, 9, 11 | todo |
| 16 | Title / main menu / character select (U01-U04) | UI | – | todo |
| 17 | Card and relic detail popups (U25), deck/pile views (U13) | UI | – | todo |
| 18 | Settings + death/victory screens (U26, U28) | UI | 12 | todo |
| 19 | Real-hardware performance pass | 3DS | 13 | todo |

*parallel-safe* pairs: any two of 1, 2, 4a, 4b, 5, 6a, 7 (they only add files and
one registration line each). 0, 3, 4c, 6b, 8, 9, 12 run alone.

---

## 0. Event text string vars (small, engine)

Event/relic text can contain string placeholders filled from other loc tables, e.g.
AromaOfChaos' MAINTAIN_CONTROL page adds `{AromaPrinciple}` =
`characters.IRONCLAD.aromaPrinciple`, and WoodCarvings uses StringVar card titles.
Today `expandSmart` (source/ui/ui.cpp) prints `?` for them.

- Add string vars to `Event` (e.g. `std::map<std::string, std::string> strVars` +
  `setStr(name, locKeyOrText)`), resolve them in `expandSmart` (look up the loc key,
  fall back to the literal). Set AromaOfChaos' `AromaPrinciple` to
  `characters.IRONCLAD.aromaPrinciple`.
- `tools/build_assets.py`: make sure the `characters` table keys used are exported.
- Done when: `STS_ROOM=Event STS_EVENT=AromaOfChaos` preview, choose 维持理智, pick a
  card: the result page shows no `?`.

## 1. Act 1 events (content)

Files: `source/core/events_act1.cpp` only (+ new cards/relics they give, defined in
the same file and registered with `db::registerCard` / `db::registerRelic`).
Read: the `AromaOfChaos` example there; the events section of `game.h` (Event,
EventOption, DeckChoice, Run event helpers); `run.cpp` functions `runEvent`,
`selectFromDeck`, `transformCard`, `randomTransformFor`, `eventFight`, `loseHp`.
C#: `MegaCrit.Sts2.Core.Models.Events\<Name>.cs`, base `Models\EventModel.cs`.

Events: ByrdonisNest, DenseVegetation, JungleMazeAdventure, LuminousChoir,
MorphicGrove, SapphireSeed, SunkenStatue, TabletOfTruth, UnrestSite, Wellspring,
WhisperingHollow, WoodCarvings.

Mapping (C# -> C++):
- `new EventOption(this, Fn, "K.pages.P.options.O")` -> `option("P", "O", [this]{ return fn(); })`;
  null action / `_LOCKED` -> `EventOption{page("P") + ".options.O_LOCKED", nullptr}`.
- `SetEventFinished(L10NLookup("K.pages.X.description"))` -> `setFinished("X")`;
  a new page -> `setPage("X", {...})`. `IsAllowed` -> `isAllowed(Run&)`.
  `CanonicalVars`/`CalculateVars` -> `calculateVars()` with `addVar/setVar` using the
  C# var names (the UI fills `{Name}` from them).
- `CardSelectCmd.FromDeck*` -> `co_await run->selectFromDeck("card_selection.TO_X", filter, n [, canCancel, showUpgrade])`.
- `CardCmd.TransformToRandom` -> `run->transformCard(c, run->randomTransformFor(c, rng()))`;
  `TransformTo<X>` -> `run->transformCard(c, db::card("X"))`; remove -> `run->removeCardFromDeck(c)`;
  add -> `run->addCardToDeck(db::card("X"))`; upgrade -> `c->upgrade()`.
- Gold: `co_await run->gainGold(n)` / `run->gold -= n`. HP outside combat:
  `co_await run->loseHp(n)`, heal = `owner()->hp = min(maxHp, hp + n)`,
  `run->gainMaxHp/loseMaxHp`. Relic: `co_await run->obtainRelic(db::relic("X"))` or a
  random one via `run->offerRelic(run->pullRelicFromFront(run->relicBag, run->rollRelicRarity(run->rng("Rewards"))), false)`.
- Combat: `bool won = co_await run->eventFight("EncounterId"); if (!won) co_return;`.
- Needs enchantments / potions / custom card grids / shops: lock that option with a
  PORT NOTE. If an event is unusable without them, don't register it; list it here.

Test: `STS_ROOM=Event STS_EVENT=<Id> SIM_FIGHTS=1 ./build/sim 6` for each event (the
sim rotates through options), then screenshot two or three events in the preview.
Done when every registered event reaches its finished page in the sim without a hang.

## 2. Shared events (content)

Same as package 1, new file `source/core/events_shared.cpp` with
`registerSharedEvents()` (add the call next to `registerAct1Events()` in
`events.cpp`), and add the ids to the event pool: `ActModel.GenerateRooms` uses
`AllEvents.Concat(ModelDb.AllSharedEvents)`, so extend `db::act1Events()` usage in
`Run::start` to also include a `db::sharedEvents()` list (define it in events.cpp).

Events: BrainLeech, CrystalSphere, DollRoom, FakeMerchant, PotionCourier,
RanwidTheElder, RelicTrader, RoomFullOfCheese, SelfHelpBook, SlipperyBridge,
StoneOfAllTime, Symbiote, TeaMaster, TheFutureOfPotions, TheLegendsWereTrue,
ThisOrThat, WarHistorianRepy, WelcomeToWongos. (Several need potions/shops: lock
those options; CrystalSphere has its own minigame UI in C# — skip it and note it.)

## 3. Multi-act run structure (engine, Opus)

Today the run is act 1 only: beating the boss is Victory. Make it three acts.
C#: `Models\ActModel.cs` (GetNumberOfRooms, GenerateRooms, weak/normal/elite/boss
pools, PullNextEvent), `Models.Acts\Overgrowth.cs`, `Hive.cs`, `Glory.cs`,
`Runs\RunManager.cs` (act transitions), rewards after bosses (`Rewards\RewardsSet.cs`:
boss gold + card, and whatever happens between acts: heal, ancient event).

- `Run`: `actIndex`, per-act encounter queues/boss/event queue, `generateMap()` per
  act (mapgen already takes the room count from Overgrowth: make it an argument;
  Hive = 14 rooms, Glory = 13; their `GetMapPointTypes` for rest/unknown counts).
- Central act lists in a new `source/core/acts.cpp`: for each act the weak/normal/
  elite/boss encounter ids and event ids **as in the C#** (content packages only
  register encounters/events; unregistered ids are skipped at runtime, like events).
- Map art per act (`map_bgs/hive`, `map_bgs/glory`), room backgrounds per act
  (`images/rooms/<act>/...`), boss map icons; `build_assets.py` gains these.
- After the act 3 boss: Victory. Dev menu: "跳到下一幕".
- Done when: `SIM_ALLCARDS=1 ./build/sim 10` plays through three acts (acts 2/3 may
  only have a few encounters registered; the pools must tolerate that).

## 4. Act 2 (Hive) monsters (content)

For each encounter below, translate its `Models.Encounters\<Id>.cs` and every monster
it uses (`Models.Monsters\<Name>.cs`) plus new powers (`Models.Powers\`). New file per
package: `source/core/content_act2a.cpp` / `act2b` / `act2c` with
`registerAct2A()` etc., called from `db::init()` in content.cpp (one line).
Style: copy `content_act1.cpp` (plain monsters), `content_phrog.cpp` (spawning,
stun), `content_bosses.cpp` (minions, illusions, custom powers). Monster loc key =
UPPER_SNAKE of the class name. Register with `db::registerEncounter`.

- 4a: BowlbugsNormal, BowlbugsWeak, ChompersNormal, ExoskeletonsNormal,
  ExoskeletonsWeak, MytesNormal, SpinyToadNormal, TunnelerWeak.
- 4b: HunterKillerNormal, LouseProgenitorNormal, OvicopterNormal,
  SlumberingBeetleNormal, TheObscuraNormal, ThievingHopperWeak.
- 4c (elites/bosses, engine work likely): DecimillipedeElite, EntomancerElite,
  InfestedPrismsElite, TheInsatiableBoss, KnowledgeDemonBoss, KaiserCrabBoss.

Engine features that exist: `cmd::addMonster` (spawn), `Monster::stun`,
`MinionPower`-style secondary enemies, illusions (die without leaving),
`shouldStopCombatFromEnding`, `shouldPlay`, `tryModifyPowerAmountReceived`,
turn-end-in-hand cards, intents incl. Summon/Heal/Sleep/Stun/Escape.
Test per encounter: `STS_ENCOUNTER=<Id> SIM_FIGHTS=1 ./build/sim 4` and
`SIM_ALLCARDS=1 ... ./build/sim 1 v` to see the mechanics fire; screenshot with the
preview (creature art comes from `scenes/creature_visuals/<snake>.tscn`; if a monster
has no scene or its skeleton resource differs, fix `build_assets.py`). Check HP and
damage against C# (see the hpcheck idea in git history). If an animation name is
unusual, extend the fallback in `App::trigger` (ui.cpp).
Done when all listed encounters run in the sim without STUCK, render in the preview,
and numbers match C#.

## 5. Act 2 events (content)

Like package 1, file `source/core/events_act2.cpp`: Amalgamator, Bugslayer,
ColorfulPhilosophers, ColossalFlower, FieldOfManSizedHoles, InfestedAutomaton,
LostWisp, SpiritGrafter, TheLanternKey, ZenWeaver. (Until package 3 lands, test them
with `STS_ROOM=Event STS_EVENT=<Id>`.)

## 6. Act 3 (Glory) monsters (content)

As package 4, files `content_act3a.cpp` / `content_act3b.cpp`.
- 6a: AxebotsNormal, ConstructMenagerieNormal, DevotedSculptorWeak, FabricatorNormal,
  FrogKnightNormal, GlobeHeadNormal, OwlMagistrateNormal, ScrollsOfBitingNormal,
  ScrollsOfBitingWeak, SlimedBerserkerNormal, TheLostAndForgottenNormal,
  TurretOperatorWeak.
- 6b (engine likely): KnightsElite, MechaKnightElite, SoulNexusElite, QueenBoss,
  TestSubjectBoss (423 lines of C#), AeonglassBoss.

## 7. Act 3 events (content)

File `source/core/events_act3.cpp`: BattlewornDummy, GraveOfTheForgotten,
HungryForMushrooms, Reflections, RoundTeaParty, Trial, TinkerTime.

## 8. Potions (engine + UI, Opus)

C#: `Models\PotionModel.cs`, `Models.Potions\`, pools, `PotionCmd`, potion rewards
(`RewardsSet.RollForPotionAndAddTo`, `PlayerOdds.PotionReward`), belt size.
UI decision to confirm with the owner first: RGDSplus keeps potions in the top bar,
used with the controller (U12); on the 3DS the top screen is not touchable, so
propose e.g. a potion button on the combat bottom screen opening a potion list
(detail on top). Includes potion rewards after fights and discarding.

## 9. Shop (merchant) (engine + UI)

C#: `Rooms\MerchantRoom.cs`, `Entities.Merchant\` (inventory, prices, card removal
service), `MerchantCost` of relics. Layout per RGDSplus U20: goods and prices on the
bottom screen, the focused item and its text on top, buy = select then confirm.
Replace the "商店（尚未实现）" placeholder in `Run::main`.

## 10. Remaining relics (content)

Each `relics_*.cpp` ends with the relics that were skipped and why (potions, shop,
card-reward hooks, death prevention, Thorns, ...). After 8 and 9, add the missing
hooks and port them, plus the Shop-rarity relics (`relics_shop.cpp`).

## 11. Ancients (content + UI)

Neow at the start of the run (Overgrowth `AllAncients`), and the act-start ancients
of Hive (Orobas, Pael, Tezcatara) and Glory (Nonupeipe, Tanx, Vakuu):
`Models\AncientEventModel.cs`, `Models.Events\<Name>.cs`. They reuse the event UI.

## 12. Saves (engine)

Save at the map screen (after each room): deck (ids + upgrades), relics (+ their
counters), gold, HP, map + visited nodes, act, queues and every RNG's state. The
simplest robust option is a seed + action log replay; decide with the owner.
SD path `sdmc:/3ds/sts2-3ds/save.dat`.

## 13. Texture compression (tools + 3DS gfx)

romfs is ~70 MB because every Spine page and the atlases are RGBA8. Add ETC1(A4)
(`tex3ds`, devkitPro) or RGBA4444 for creature pages in `build_assets.py` and load
them in `gfx_3ds.cpp` (the SDL preview can keep RGBA8 or decode). Check quality on
the 3DS; target < 30 MB.

## 15. RGDSplus page audit (UI)

Go through `../rgds-ref/docs/R4_ALL_PAGES.zh-CN.md` row by row. For each U-page
that exists in our game: note which screen shows what in RGDSplus, compare with
ours (preview screenshot), fix the layout, and record the result in a table at the
end of this file (U-id, our screen, status). Pages that do not exist in StS2 or are
online-only (U32) are marked n/a. Show the owner before/after screenshots.

## 16. Title / main menu / character select (U01-U04)

U02: RGDSplus spans one tall background across both screens with the logo on top
and the menu buttons enlarged (1.65x) on the bottom. Use StS2's main-menu art
(`images/ui/main_menu/...`, search the pck). U04: character art and details on top,
selection/start/back on the bottom (only the Ironclad is playable for now).
Replaces the current placeholder title screen.

## 17. Detail popups and pile views (U25, U13)

U25: tapping a card anywhere (reward, deck, shop) opens it large on the top screen
with its keywords explained; controls (upgrade preview toggle, close) on the bottom.
Same for relics. U13: in combat, tapping the draw/discard piles lists them on the
bottom with the focused card on top.

## 18. Settings and end screens (U26, U28)

Settings page (fast mode, screen shake, volume when audio exists, abandon run),
opened with START on the map; death/victory screens with run stats, per U28.

## 19. Real-hardware performance pass

Measure frame time on the New 3DS (Spine skinning, text layout, atlas binds),
fix hot spots, check memory over a full three-act run.

## 14. Other characters (later)

Silent, Defect, Regent, Necrobinder: card pools, starter decks/relics, character
select, energy orbs, orbs/Osty systems. Plan separately when acts 1-3 are complete.
