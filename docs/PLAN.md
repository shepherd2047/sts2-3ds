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

0. **Local sessions only** (desktop app, folder `sts2-3ds` on the owner's PC or Mac).
   Cloud sessions only get the git repo: no decompiled C#, no game files, no
   devkitPro, so they can neither translate nor test. Never commit the decompiled
   code or game assets to make them work.
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
SIM_ALLCARDS=1 ./build/sim 20                          # full-run smoke test; record outcomes and investigate hangs or changes
STS_ROOM=Event STS_EVENT=<EventId> SIM_FIGHTS=1 ./build/sim 3   # an event, headless
# preview screenshots (see CLAUDE.md for STS_SCRIPT syntax):
STS_ENCOUNTER=<Id> STS_HIDDEN=1 STS_FIXED_STEP=1 STS_SEED=42 STS_SCRIPT="40:A,100:A" STS_SHOTS="400:build/x.bmp" ./build/sts2-preview.exe
```
New monsters/relics/events need `python tools/build_assets.py` (art and text are
collected from `MONSTER_HEADER` / `RELIC_HEADER` / `EVENT_HEADER` / `CARD_HEADER`).
PowerShell mangles quotes and pipes in `bash -lc '...'`: put commands in a `.sh`
file and run it with bash.

## Status

### Project checkpoint (2026-09-27)

- Local `main` and `origin/main` match. Three pre-existing local edits are in progress: `Makefile.sdl`, `source/core/content_act2c.cpp`, and `test/sim.cpp`; keep them separate from the UI work.
- A forced SDL build succeeds on macOS. A 3DS build also succeeds in the existing ASCII-path copy at `~/dev/sts2-3ds-build` with `/opt/devkitpro`; the `.3dsx` has not been run on hardware. `../sts2-decompiled/` is absent here; further faithful C# content translation needs that local source.
- Preview screenshots checked: title, Neow, map, and combat (`build/pm-*.png`, ignored local artifacts). This is an entry-point smoke check, not U01-U33 acceptance.
- `SIM_ALLCARDS=1 ./build/sim 20` completed without a hang but won 7/20. Win count alone is not a reliable regression gate; compare deterministic outcomes per seed and investigate unexpected changes or crashes.
- Next: finish the U01-U33 page audit below. U02/U04 have a first preview implementation from the owner's game art. U13 combat piles and U25 card/relic detail have first preview implementations; the shop card detail was checked in SDL. U26 settings and U28 end summaries have first implementations. Finish remaining contexts and owner visual review. Package 19 requires a real New 3DS run. Package 14 remains later.

| # | Package | Kind | Depends on | Status |
|---|---|---|---|---|
| 0 | Event text string vars | engine, small | – | done |
| 1 | Act 1 events (12 left) | content | 0 | done (locked options: Wellspring BOTTLE, WhisperingHollow GOLD = potions; SapphireSeed PLANT, WoodCarvings SNAKE = enchantments) |
| 2 | Shared events (18) | content | 0 | done (8 of 18, see events_shared.cpp header) |
| 3 | Multi-act run structure | engine, *Opus* | – | done (acts.cpp; TheArchitect ending not ported) |
| 4a | Act 2 monsters A | content | – | done |
| 4b | Act 2 monsters B | content | – | done |
| 4c | Act 2 elites + bosses | content (+engine likely) | – | done (content_act2c.cpp, sim-tested + screenshots of Kaiser Crab / Decimillipede; Tainted/VitalSpark are per-power approximations of the affliction system; Kaiser Crab claws are two plain creatures sharing the crab skeleton (`hide`/`shift` lines in the spine txt), no arm animations/background, no body flip; Knowledge Demon curses use the combat card-choice screen; sim bot plays Frantic Escape first, but with `SIM_ALLCARDS=1`'s ~200-card deck it rarely draws them, so The Insatiable often wins there (real decks are fine); dead Decimillipede segments now roll their next move in `startTurn`) |
| 5 | Act 2 events (10) | content | 0 | done (9 of 10: ColorfulPhilosophers needs a 2nd character; FieldOfManSizedHoles ENTER_YOUR_HOLE locked = PerfectFit enchantment; act pool list is `db::act2Events()` in events_act2.cpp, to be wired by package 3) |
| 6a | Act 3 monsters A | content | – | done |
| 6b | Act 3 elites + bosses | content (+engine likely) | – | done (content_act3b.cpp, sim-tested only, no screenshots; Hex/Dampen/Chains of Binding/Wither are per-power approximations of the affliction system, Intangible caps HP loss only; run build_assets once 4c's monsters have scenes) |
| 7 | Act 3 events (7) | content | 0 | done (sim-tested; not screenshot-checked; MadScience card text needs `choose()` support in App::describe; Grave of the Forgotten enchant = removes Exhaust; BattlewornDummy V1 potion reward skipped; `db::act3Events()` defined in events_act3.cpp) |
| 8 | Potions | engine + UI, *Opus* | – | done (potions.cpp: Ironclad + shared pools, 47 of 48 — ColorlessPotion needs a colorless pool; belt of 3, rewards with PotionRewardOdds, Fairy death prevention; UI: 药水 button in combat and on the map, list below / detail above, ◀ ▶ targeting; relics/events that need potions are still locked) |
| 9 | Shop (merchant) | engine + UI | 8 | done (shop.cpp: 5 character cards with a sale, 3 relics incl. a Shop-rarity slot, 3 potions, card removal 75+25n; prices and rng as in the C#; The Courier, Membership Card, Meal Ticket; colorless slots and the Foul Potion throw not ported) |
| 10 | Remaining relics (skipped ones + Shop rarity) | content | 8, 9 | done (relics_more.cpp: 42 relics incl. the Shop ones; rest site Lift/Dig/Miniature Tent, card reward/deck/potion hooks, Lizard Tail. Still skipped: DingyRug, FresnelLens, GnarledHammer, Kifuda, MysticLighter, PunchDagger, RoyalStamp, Toolbox, WingCharm (enchantments / colorless pool), UnsettlingLamp; IsAllowed(IsBeforeAct3TreasureChest) not checked) |
| 11a | Neow: act-1 Ancient start node, heal, relic choice | engine + UI + content | – | done |
| 11b | Act 2/3 Ancients (Orobas, Pael, Tezcatara, Nonupeipe, Tanx, Vakuu, Darv) | content | 3, 11a | done (ancients_later.cpp: all 7 events with their option rules, 58 of 72 relics, 12 cards, Blur/Confused/next-turn powers, Black Blood; per-ancient room art and map icons. Not offered until their systems exist: enchantment relics, Sea Glass / Prismatic Gem, Driftwood, Pael's Wing / Eye / Legion, Golden Compass, Fur Coat, Toy Box, Whispering Earring) |
| 12 | Saves | engine | 3 | done (save.cpp: autosave at every map choice, 继续 on the title, deleted on death/victory; RNG raw state, map, bags, deck, relics (+ Relic::persist), potions. `SIM_SAVELOAD=K ./build/sim` checks that save/load at floor K changes nothing) |
| 13 | Texture compression (romfs is ~70 MB) | tools + 3DS gfx | – | done (`tools/compress_romfs.py` → `romfs_3ds/` ETC1A4/ETC1/RGBA4 + LZ11, 146 → 15 MB; make runs it; needs a real-hardware check, package 19) |
| 14 | Other characters | big | 3 | later |
| 15 | RGDSplus page audit (U01-U33) | UI | 3, 8, 9, 11a, 11b | todo |
| 16 | Title / main menu / character select (U01-U04) | UI | – | in progress (native menu art and Ironclad selection previewed; profile, seed, ascension and hardware review remain) |
| 17 | Card and relic detail popups (U25), deck/pile views (U13) | UI | – | in progress (combat pile tabs, card/relic modal, upgrade preview and card keyword pages; shop card detail checked; owner and hardware review remain) |
| 18 | Settings + death/victory screens (U26, U28) | UI | 12 | in progress (map START settings, fast mode, shake switch, confirmed abandon, end stats and two return paths; victory and hardware review remain) |
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

## 11. Ancients (content + UI) — a core StS2 feature, not optional

Every act starts with an Ancient: the first room of each act (map row 0) is an
Ancient event that fully heals you and offers a choice of 3 Ancient-rarity relics
(the StS2 replacement for StS1's Neow blessings). Act 1 is always Neow.

Map: `mapgen.cpp` already builds the StandardActMap starting point as
`MPType::Ancient` but doesn't output it (see the header comment and line ~680).
It must become a real node below row 1 that the run starts on, drawn with the
ancient's map icon (`ActModel` map node asset paths / `_rooms.Ancient`).

Sources (`../sts2-decompiled`):
- `Models\AncientEventModel.cs`: `BeforeEventStarted` heals to full (Neow first
  sets HP to 0 so the heal animates from empty; the WearyTraveler ascension heals
  80%, ignore it); `GenerateInitialOptionsWrapper` + `Hook.ShouldAllowAncient`;
  `RelicOption<T>` = obtain that relic; dialogue (`DefineDialogues`,
  `Entities.Ancients\AncientDialogueSet.cs`: per-character lines by visit index;
  keep only the text, no audio).
- `Models.Events\Neow.cs` `GenerateInitialOptions`: 1 random curse option from
  `CurseOptions` (filtered by `RelicModel.IsAllowedAtNeow`) + 2 from
  `PositiveOptions`, with the exclusion pairs (CursedPearl↔GoldenPearl,
  HeftyTablet↔ArcaneScroll, LeafyPoultice↔NewLeaf, PrecariousShears↔PreciseScissors,
  NeowsSacrifice↔PhialHolster/LostCoffer) and the coin-flip extras (LavaRock or
  SmallCapsule unless the curse is LargeCapsule; NutritiousOyster or
  StoneHumidifier; NeowsTalisman; Pomander). Read the rest of that function for the
  final pick. ~30 relics, all `RelicRarity.Ancient` in `Models.Relics\`.
- Acts 2/3: `ActModel.cs:385` rolls `_rooms.Ancient` from `GetUnlockedAncients` +
  the shared subset. Hive: Orobas (7 relics, cross-character options), Pael (10),
  Tezcatara (10). Glory: Nonupeipe, Tanx, Vakuu (10 each). Shared: Darv
  (`ModelDb.AllSharedAncients`; `RunManager.GenerateRooms` hands it to a random
  later act via `Rng.UpFront`), offers relics from `_validRelicSets` + DustyTome.
- UI: `Nodes.Events\NAncientEventLayout.cs` (portrait + name banner + dialogue
  lines, then the relic choice). Follow RGDSplus' Neow page (U06, NEOW_SCREEN) for the layout;
  ancient art is in the game's `event`/`ancients` resources (extend build_assets).

Split:
- **11a Neow** (act 1): map start node, heal, dialogue, Neow's option roll, the
  Neow relics. Relics needing missing systems (potions: PhialHolster,
  shop: ...) stay out of the pool with a PORT NOTE until 8/9 land. Can be done
  now; touches `run.cpp` / `mapgen.cpp` / `ui.cpp`, so it runs alone.
- **11b other Ancients**: Orobas, Pael, Tezcatara, Nonupeipe, Tanx, Vakuu, Darv
  and their ~70 relics. Needs 3 (acts 2/3 exist). Content only after 11a, so the
  relic files are parallel-safe (one file per ancient).

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
bottom with the focused card on top. First SDL pass: draw/discard/exhaust tabs,
focused card, card/relic detail modal, upgrade preview and five card-keyword
explanations from the localized text. Combat hand opens details on a second tap;
the first tap previews it. Draw pile is sorted
for display so its actual order is not revealed. Checked pile selection,
upgrade toggle and return in SDL; relic detail, hand-card detail and reward-card
detail previews also checked. A single screenshot of the keyword page is complete; repeated
SDL readbacks in the same process intermittently omit layers, so use a fresh
process for each reference capture until that preview-path issue is resolved.
The incremental devkitARM build passes in the ASCII-path copy. Shop card detail
opened without a purchase in SDL. Remaining source contexts, owner visual review
and hardware remain.

## 18. Settings and end screens (U26, U28)

Settings page (fast mode, screen shake, volume when audio exists, abandon run),
opened with START on the map; death/victory screens with run stats, per U28.
First SDL pass: fast mode scales scheduler and visual animation time, shake
controls hit displacement, volume is disabled until audio exists, and abandon
requires confirmation then cancels suspended coroutines before returning to the
title. The settings page and abandon → title → new-run path were previewed.
In an isolated SDL save directory, toggling speed and shake wrote `v1 1 0`;
after relaunch the settings page displayed both saved values. The final
incremental devkitARM build passed in the ASCII-path copy.
The end summary shows act/floor, HP, gold, deck and relic counts with menu/restart
buttons. A natural first-floor defeat reached and previewed the death page.
The headless sim won a full three-act run with `SIM_ALLCARDS=1 SIM_SEED=1`.
In an isolated SDL run, `STS_ACT=3 STS_ROOM=Boss` with a forced weak encounter
reached Victory and rendered its summary; B returned to the title, after which
autoplay began a fresh run. The restart button also began a fresh fight. The
actual final-boss UI flow and hardware remain
unverified. The latest 3DSX was opened in Azahar at 60 FPS on the map screen.

## 19. Real-hardware performance pass

Measure frame time on the New 3DS (Spine skinning, text layout, atlas binds),
fix hot spots, check memory over a full three-act run.

## 14. Other characters (later)

Silent, Defect, Regent, Necrobinder: card pools, starter decks/relics, character
select, energy orbs, orbs/Osty systems. Plan separately when acts 1-3 are complete.

## U01-U33 page audit baseline (2026-09-27)

This is a route/code inventory against `../rgds-ref/docs/R4_ALL_PAGES.zh-CN.md`, not visual or hardware acceptance. `partial` means the route exists but its RGDSplus page requirements have not all been verified. `missing` means no dedicated page exists. Preview smoke screenshots exist for title, Ironclad selection, Neow, map, and combat; the U02/U04 after images are `build/title-new.png` and `build/character-new.png` (ignored local artifacts). Keep package 15 `todo` until each applicable page has before/after screenshots, interaction checks, and the owner's review.

| ID | Our route | Status | Next check or gap |
| --- | --- | --- | --- |
| U01 | Title | partial | Startup/logo and act transition presentation |
| U02 | Title | partial | Native tower/logo art and continuous preview checked; new-game selection and continue/load checked in an isolated save directory; hardware review remains |
| U03 | None | missing | Profile slots, naming, delete flow |
| U04 | Title/Character | partial | Ironclad selection and back/start preview checked; seed, ascension and other characters remain |
| U05 | None | missing | Custom run setup |
| U06 | Event/Ancient | partial | All dialogue and choice phases; screenshot captured for Neow |
| U07 | Map | partial | Vertical gesture threshold now matches vertical-only scrolling; physical drag/tap, legal-path selection, scroll limits, two-screen continuity and owner review remain |
| U08 | Combat | partial | Full hand and late-act layouts |
| U09 | Combat | partial | Targeting/cancel flows and physical touch feel |
| U10 | Combat | partial | Animation and effect layers |
| U11 | None | missing | Dedicated combat inspection view |
| U12 | Potion overlay | partial | Use, discard, replace and targeting |
| U13 | Deck/pile overlay | partial | Draw/discard/exhaust tabs and focused preview implemented in SDL; interaction/layout and hardware review remain |
| U14 | DeckChoice overlay | partial | Multi-pick and forced choice input |
| U15 | RestUpgrade/DeckChoice | partial | Upgrade, removal and transform preview flows |
| U16 | DeckChoice overlay | partial | Choose-one card flow |
| U17 | Reward | partial | Full reward list and continue flow |
| U18 | Reward | partial | Select, skip and variable reward counts |
| U19 | RelicOffer | partial | Relic detail, take/skip and chest variants |
| U20 | Shop | partial | Card/relic/potion focus and purchase result |
| U21 | Event | partial | Multi-option and special-event layout |
| U22 | Rest | partial | Extra relic actions and confirm/leave |
| U23 | RelicOffer | partial | Chest opening and variant flows |
| U24 | Top bar/overlays | partial | Focus, opening and returning to source page |
| U25 | Card/relic detail overlay | partial | Modal, upgrade preview and five card-keyword pages in SDL; shop card detail checked, remaining contexts and hardware review remain |
| U26 | Settings overlay | partial | Map START page, speed/shake and confirmed abandon previewed; isolated SDL save/reload verified, hardware review remains |
| U27 | None | missing | Tutorials, confirmation and error pages |
| U28 | GameOver/Victory | partial | Stats and menu/restart added; natural defeat and debug-forced victory previewed; actual final-boss and hardware review remain |
| U29 | None | missing | Compendium pages |
| U30 | None | missing | Stats and run history |
| U31 | None | missing | Credits and update notes |
| U32 | None | n/a for offline milestone | Online/daily/leaderboard features require separate scope |
| U33 | Placeholder | partial | Unknown room and error fallback |
