# Development plan

## Handover (2026-09-28, PC -> Mac)

The reviewer session ("architech") ran on the Windows PC; the owner continues on the Mac from here.
Everything accepted is on `main`. State:

- **Accepted today:** A3a, X0, F0-F6, C10, X1.0-X4.0, X1.1-X4.1 (starter decks, character relics, potions),
  X1.2-X4.2 (all Common cards of the four characters), M1 (progress.sav core), Y1 (settings.sav core), S14
  (interactive reward list).
- **X1.5-X4.5 character visuals: accepted on the Mac and merged** (2026-09-28). Combat Spine for the four
  characters + Osty, select art (`gfx/bg_character_<key>`, `ui/<key>_select`), energy orb / cost gem, Osty,
  Defect orb row, Regent star counter. Review fixes: portraits were only a stale romfs; card frames now use the
  card's own pool material and the portrait border / title banner the rarity's banner material, both baked from
  the game's hsv.gdshader params (NCard.UpdateVisuals). The Regent's "dark tower" is his stone throne carried by
  two minions — the game's own art (checked against an offline render). Open: MAD_SCIENCE (event card) has one
  portrait per chosen type (`mad_science_attack/skill/power`), not baked yet.
- **S04 character select: accepted and merged** (2026-09-28).
- **X1.3a-X4.3a accepted and merged** (2026-09-28, four subagents). X*.3b, A7a, A7b, A11a, A11b accepted too. X*.4, A2, A7c, A11c, A11e accepted too. **Waiting on A1a (colorless pool):** Quasar, SpectrumShift, BundleOfJoy, ManifestAuthority/HeirloomHammer/EndlessConveyor parts. **Then:** the engine order (Y5 + H1/H3 need the real 3DS, A10, A11f, ...).
- **Rules learned today** (also in CLAUDE.md): one class per power id (shared powers in `powers.h`);
  no two .cpp files with the same basename (3DS object names are flattened); `Card::createdByPlayer` is the
  C#'s `creator == Owner` check; subagents in worktrees must export `OS=Windows_NT` on Windows.
- **Mac:** `bash tools/accept.sh --quick --3ds` now builds the .3dsx in `~/dev/sts2-3ds-build` on macOS
  (works on the Mac; `make check` needed `<unistd.h>` in settings_test). Rebuild assets after pulling (`python3 tools/build_assets.py`): new UI icons
  and many new card strings need the new font glyphs.
- Leftover worktrees on the PC (`.claude/worktrees/agent-*`, `../sts2-3ds-engine`, `../sts2-3ds-ui`) are all
  merged (the visuals one too); they can be deleted.

## Goal

A **finished product**: the complete Slay the Spire 2 on the New 3DS. That means
all 5 characters, all 4 acts, every card, relic, potion, event, enchantment, Ancient
and ascension level from the game, all menus (main menu, profiles, settings, pause,
compendium, stats, run history, achievements, timeline, custom and daily runs,
credits), music and sound, and a polished UI drawn with StS2's own art. The layout
follows the RGDSplus dual-screen port of StS1
(github.com/LPF970915/Slay-the-Spire-for-RGDSplus, cloned next to this repo as
`../rgds-ref`), whose page inventory `../rgds-ref/docs/R4_ALL_PAGES.zh-CN.md`
(U01-U33) and the owner's revisions in `R4_LAYOUT_REVISION.zh-CN.md` are the
checklist for the screens. Their code only re-routes StS1's own UI, so we copy the
**layout rules and proportions** and draw them with StS2's art. The owner's measured
rules that already apply are in CLAUDE.md.

**Out of scope (n/a):** online multiplayer and multiplayer-only cards, leaderboards,
mods, the feedback screen, patch notes, the Steam profile screen, and the deprecated
or test models (`Deprecated*`, `Mock*`, the `Deprived` character).

When every package below is `done`, the game is complete. The last package (H6)
is a release checklist that proves it.

## How to work

Work is split into **packages**. Each one fits in a single session. Pick the first
`todo` package in the **order** section whose dependencies are `done`, do it, then
set its status in the tables in the same commit. Legend:

- *Opus* = design-heavy, do it in an Opus session.
- *engine* = edits `game.h` / `combat.cpp` / `run.cpp` and must run alone.
- *parallel-safe* = only adds files plus one registration line.

The owner has decided that one Opus session working the packages in order is the
cheapest way to run this. Do not spawn subagents or other sessions unless the owner asks.

### Session protocol (every package)

0. **Local sessions only** (desktop app, folder `sts2-3ds` on the owner's PC or Mac).
   Cloud sessions only get the git repo: no decompiled C#, no game files and no
   devkitPro, so they can neither translate nor test. Never commit the decompiled
   code or game assets to make them work.
1. `git pull`. Read `CLAUDE.md`, this file, and `docs/PORTING.md`. Do **not** read the
   whole codebase or the whole decompiled tree. Open only the files a package names
   and the C# classes it lists.
2. The source of truth is `../sts2-decompiled/` (C#). Translate faithfully:
   - use the same numbers, including ascension: `GetValueIfAscension(level, a, b)` in a monster is
     `asc(kToughEnemies, a, b)` / `asc(kDeadlyEnemies, a, b)` (`Monster::asc`, see the C10 notes);
   - keep the same move order and the same RNG call order;
   - drop VFX, SFX and animation code unless a package asks for it.
3. Mark anything you cannot express with `// PORT NOTE: <what is missing>`.
4. Build **both** targets before committing: `make -f Makefile.sdl` (UCRT64) and
   `make` (devkitPro). The 3DS build has **no RTTI and no exceptions**.
5. For UI changes, take preview screenshots (`STS_SHOTS`) of both screens and show
   the owner before and after. Send a build to the 3DS only when the owner says so
   ("发").
6. Commit, `git push`, and update the status here. Talk to the owner in Chinese.

### Test commands (UCRT64 shell, repo root)

```bash
make -f Makefile.sdl                                   # preview + sim
STS_ENCOUNTER=<EncounterId> SIM_FIGHTS=1 ./build/sim 4   # a fight, headless
STS_ENCOUNTER=<EncounterId> SIM_ALLCARDS=1 ./build/sim 1 v   # verbose event log, 999 HP
SIM_ALLCARDS=1 ./build/sim 20                          # full-run smoke test; compare outcomes per seed, investigate hangs or changes
STS_ROOM=Event STS_EVENT=<EventId> SIM_FIGHTS=1 ./build/sim 3   # an event, headless
SIM_ALLRELICS=1 ./build/sim 10 ; SIM_ALLPOTIONS=1 ./build/sim 10 ; SIM_SAVELOAD=17 ./build/sim 10
# preview screenshots (see CLAUDE.md for STS_SCRIPT syntax):
STS_ENCOUNTER=<Id> STS_HIDDEN=1 STS_FIXED_STEP=1 STS_SEED=42 STS_SCRIPT="40:A,100:A" STS_SHOTS="400:build/x.bmp" ./build/sts2-preview.exe
```

New monsters, relics, events, potions and cards need `python tools/build_assets.py`
(their art and text are collected from the `*_HEADER` macros). PowerShell mangles
quotes and pipes inside `bash -lc '...'`, so put commands in a `.sh` file and run
that file with bash.

To see what is still missing compared with the C#, compare our `*_HEADER` ids
against the pool files. For example, for cards:

```bash
grep -rhoE 'CARD_HEADER\(\w+' source/core | sed 's/CARD_HEADER(//' | sort -u > /tmp/ours
grep -oE 'ModelDb.Card<\w+' ../sts2-decompiled/MegaCrit.Sts2.Core.Models.CardPools/ColorlessCardPool.cs | sed 's/.*<//' | sort -u | comm -23 - /tmp/ours
```

---

## Done (packages 0-13)

| # | Package | Notes that still matter |
|---|---|---|
| 0 | Event string vars | – |
| 1 | Act 1 events | Locked options: Wellspring BOTTLE and WhisperingHollow GOLD (need potions, now available, so unlock them in A8); SapphireSeed PLANT and WoodCarvings SNAKE (need enchantments, A3). |
| 2 | Shared events, 8 of 18 | The remaining 10 are in A7. |
| 3 | Three acts | `acts.cpp`. TheArchitect ending is in A10. |
| 4a/4b/4c | Act 2 monsters, elites, bosses | Tainted and VitalSpark approximate afflictions (A4). |
| 5 | Act 2 events, 9 of 10 | ColorfulPhilosophers is in X6. FieldOfManSizedHoles ENTER_YOUR_HOLE needs PerfectFit (A3). |
| 6a/6b | Act 3 monsters, elites, bosses | Hex, Dampen, Chains of Binding and Wither are approximations (A4). |
| 7 | Act 3 events | MadScience text needs `choose()` in App::describe (B4). BattlewornDummy V1 skips its potion reward (A8). |
| 8 | Potions, 47 | ColorlessPotion needs the colorless pool (A1). |
| 9 | Shop | Colorless slots and the Foul Potion throw are missing (A9). |
| 10 | Relics, 42 more | 10 shared relics still skipped (A5). |
| 11a/11b | Neow and the act 2/3 Ancients | 14 Ancient relics are locked until their systems exist (A6). |
| 12 | Saves | Every new stateful relic or system must go through `persist` / `ioRun` and pass `SIM_SAVELOAD`. |
| 13 | romfs compression | Checked on real hardware in H1. |

Package 15 (the RGDSplus page audit) is folded into track S. Its findings:
- U06, U07, U08, U09, U12, U15, U16, U18, U20, U21, U22 and U23 match RGDSplus in function.
- U17 lacks the native reward list; it is in S14.
- U11 has no power inspection; it is in S10.
- The DenseVegetation heal value was fixed during the audit.

Packages 16-18 got a first SDL pass on the Mac (2026-09-27, commit e2c9023):
- title with native menu art and Ironclad selection (U02/U04);
- combat pile tabs, a card/relic detail modal with upgrade preview and keyword pages (U13/U25);
- a settings page on START with fast mode, shake and a confirmed abandon, saved to a settings file (U26);
- end-of-run summaries (U28).
The S/Y packages below start from that code and restyle it with the F kit. They do not
rewrite it from scratch.

Packages 14 and 16-19 are replaced by the tracks below.

---

## Inventory: what the C# has and what is missing (2026-09)

| Content | C# (excluding deprecated/mock) | Ported | Missing, and the package that adds it |
|---|---|---|---|
| Characters | Ironclad, Silent, Defect, Regent, Necrobinder (+ Random) | Ironclad | 4 characters → X1-X4 |
| Acts | Overgrowth, Underdocks (alternative act 1), Hive, Glory | 3 | Underdocks → A11 |
| Encounters | 90 | 64 | 20 Underdocks → A11; 4 event fights → A7/A10/A11 |
| Character cards | 5 × ~91 | Ironclad 90 | 4 × 91 → X*; Silent/Defect/Regent/Necrobinder tokens |
| Colorless cards | 65 | 0 | A1 |
| Curse / status / event / quest / token cards | 18 / 12 / 28 / 4 / 14 | most | AscendersBane, Debt, Writhe, Beckon, Debris, Void, 13 event cards, Dowsing, SpoilsMap → A2; character tokens → X* |
| Relics | shared 118, character 5 × 8, event pool 142 | 203 | 10 shared → A5; about 55 event / Ancient relics → A6 / A7 / A11; 32 character relics → X* |
| Potions | shared 45 + 3 event + 3 per character | 49 | ColorlessPotion → A1; Ambergris, Glowwater, Foul → A9; 12 character potions → X* |
| Events | 66 | 46 | 10 shared → A7; 9 Underdocks → A11; TheArchitect → A10; ColorfulPhilosophers → X6 |
| Enchantments | 22 | 0 | A3 |
| Afflictions | 7 | approximated as powers | A4 |
| Ascension | 10 levels | 0 | C10 |
| Modifiers (custom runs) | 16 | 0 | M11 |
| Achievements | 22 | 0 | M5 |
| Badges (end of run) | 28 | 0 | M7 |
| Timeline epochs (unlocks) | 60 | 0 | M3, M4 |
| Audio | 12 FMOD banks + 50 mp3 | none | track U |

---

## Tracks and packages

The tracks are content completion (A), UI foundation (F), screens (S), characters (X),
meta and progression (M), system (Y), audio (U) and hardware/release (H).

### Track A: content for a complete Ironclad game

| id | Package | Kind | Needs | Status |
|---|---|---|---|---|
| A1a | Colorless cards 1/3: Alchemize … GoldAxe (22) + the colorless pool, and ColorlessPotion | content | – | done (subagent), accepted 2026-09-28: pool in colorless_pool.cpp (db::colorlessCards / isColorless, helpers in colorless.h); 18 cards (4 multiplayer-only skipped); released the A1a locks (Regent Quasar/SpectrumShift/BundleOfJoy/ManifestAuthority/HeirloomHammer, EndlessConveyor, BrainLeech RIP) |
| A1b | Colorless cards 2/3: HandOfGreed … Purity (22) | content, parallel-safe | A1a | done (subagent), accepted 2026-09-28: colorless_cards_b.cpp (17) |
| A1c | Colorless cards 3/3: Rally … Volley (19); skip cards the C# marks multiplayer-only | content, parallel-safe | A1a | done (subagent), accepted 2026-09-28: colorless_cards_c.cpp (18); every single-player colorless card is ported |
| A2 | Missing curses/status (AscendersBane exists since C10, Debt, Writhe, Beckon, Debris, Void), 13 event cards, quest cards Dowsing and SpoilsMap | content | – | done (subagent), accepted 2026-09-28: Debt/Writhe/Beckon + 13 event cards in content_cards_misc.cpp; Dowsing/SpoilsMap registered but locked (need CardType::Quest, a "?"-room hook, act-map rewrite) |
| A3a | Enchantment engine: enchantment slot on Card, hooks (`Models\EnchantmentModel.cs`), save, card badge + text in the UI, `enchant` event helpers | engine, *Opus* | – | done (engine), accepted 2026-09-28 |
| A3b | Enchantments 1/2: Sown, Slither, Adroit, Clone (used by CloneRestSiteOption), Corrupted, Goopy, Inky, SoulsPower + unlock SapphireSeed PLANT, WoodCarvings SNAKE, FieldOfManSizedHoles ENTER_YOUR_HOLE (PerfectFit exists), Grave of the Forgotten | content | A3a | done (subagent), accepted 2026-09-28: enchantments_b.cpp; also Symbiote APPROACH; Clone's rest option waits for PaelsGrowth (A6) |
| A3c | Enchantments 2/2: Instinct, Momentum, Nimble, RoyallyApproved, Spiral, Steady, TezcatarasEmber (needs the Eternal keyword, see notes) + relics that enchant (DingyRug, GnarledHammer, Kifuda, RoyalStamp …) | content | A3b | done (subagent), accepted 2026-09-28: enchantments_c.cpp + relics_enchant.cpp (14 relics); PaelsGrowth waits for CloneRestSiteOption; Relic::shouldGenerateTreasure hook (SilverCrucible) |
| A4 | Affliction engine + all 7 (Bound, Entangled, Galvanized, Hexed, Ringing, Smog, Tainted); replace the per-power approximations in acts 2/3 | engine | – | todo |
| A5 | 10 skipped shared relics (DingyRug, FresnelLens, GnarledHammer, Kifuda, MysticLighter, PunchDagger, RoyalStamp, Toolbox, UnsettlingLamp, WingCharm) + `IsBeforeAct3TreasureChest` | content | A1a, A3c | done (subagent), accepted 2026-09-28: relics_shared2.cpp (DingyRug, Toolbox, UnsettlingLamp), Relic::isAllowed + IsBeforeAct3TreasureChest, power-amount-given hooks; every shared and character relic registered |
| A6 | Locked Ancient relics (Sea Glass, Prismatic Gem, Driftwood, Pael's Wing/Eye/Legion, Golden Compass, Fur Coat, Toy Box, Whispering Earring …) and remaining Neow options (PhialHolster, LostCoffer, NeowsSacrifice …) | content | A3c | done (subagent), accepted 2026-09-28: relics_ancient2.cpp (SeaGlass, PrismaticGem, PaelsGrowth + Clone rest option, LeadPaperweight, Kaleidoscope, WhisperingEarring); still locked: Driftwood, PaelsWing (reward reroll/sacrifice), PaelsEye (extra turn), PaelsLegion/Byrdpip (pets), GoldenCompass, FurCoat, ToyBox, WingedBoots, DowsingRod, ScrollBoxes |
| A7a | Shared events 1/3: DollRoom, PotionCourier, SelfHelpBook, StoneOfAllTime, TheFutureOfPotions + their relics | content, parallel-safe | – | done (subagent), accepted 2026-09-28 |
| A7b | Shared events 2/3: TheLegendsWereTrue, WarHistorianRepy, WelcomeToWongos (+ Wongo relics/badge) | content, parallel-safe | – | done (subagent), accepted 2026-09-28 |
| A7c | Shared events 3/3: FakeMerchant (+ FakeMerchantEventEncounter, 10 Fake* relics, fake shop UI reuse) | content | – | done (subagent), accepted 2026-09-28: fake shop reuses the shop screen (relics only; D-pad up/down assumes card slots); the fight needs a Foul Potion throw from the shop (STS_FAKE_FIGHT=1 for tests) |
| A7d | CrystalSphere: its minigame (`Events.Custom.CrystalSphereEvent`) + custom bottom-screen UI | content + UI, *Opus* | – | in progress (subagent) |
| A8 | Remaining event-pool relics whose source already exists (run the pool diff), unlock Wellspring BOTTLE / WhisperingHollow GOLD, BattlewornDummy potion | content | A7a-c | done (subagent), accepted 2026-09-28: relics_event.cpp (LostCoffer, PhialHolster, NeowsSacrifice, Ambergris without its extra turn, the 4 refined starters for Touch of Orobas); the rest of the event pool waits for A1a / A3c / pets / map systems (list in the file header) |
| A9 | Shop leftovers: 2 colorless slots, Foul Potion throw at the merchant, event potions Ambergris / Glowwater / Foul | content | A1a | done (subagent), accepted 2026-09-28: 2 colorless shop slots (7 cards in one row, owner to confirm the look), Foul Potion throw (Merchant 100 gold, FakeMerchant fight); Ambergris extra turn still missing |
| A10 | TheArchitect: the true ending after the act 3 boss (event + TheArchitectEventEncounter), victory flow | content + engine | – | done (subagent, Opus), accepted 2026-09-28: content_architect.cpp; act 3 boss -> TheArchitect (Ancient layout) -> Run::winRun -> Victory; the combat-room staging and score-based damage numbers dropped (PORT NOTE) |
| A11a | Underdocks monsters 1/2: CorpseSlugs N/W, Cultists, FossilStalker, GremlinMerc, HauntedShip, LivingFog, PunchConstruct | content, parallel-safe | – | done (subagent), accepted 2026-09-28 |
| A11b | Underdocks monsters 2/2: Seapunk N/W, SewerClam, SludgeSpinner, Toadpoles, TwoTailedRats | content, parallel-safe | – | done (subagent), accepted 2026-09-28 |
| A11c | Underdocks elites: PhantasmalGardeners, SkulkingColony, TerrorEel | content | – | done (subagent), accepted 2026-09-28 |
| A11d | Underdocks bosses: WaterfallGiant, SoulFysh, LagavulinMatriarch | content (+engine) | – | done (subagent, Opus), accepted 2026-09-28: content_underdocks_d.cpp; hook beforeSideTurnEndVeryEarly |
| A11e | Underdocks events (AbyssalBaths, DrowningBeacon, EndlessConveyor, PunchOff + encounter, SpiralingWhirlpool, SunkenTreasury, DoorsOfLightAndDark, TrashHeap, WaterloggedScriptorium) | content | – | done (subagent), accepted 2026-09-28: also FresnelLens, DarkstonePeriapt, DreamCatcher, HandDrill, MawBank, TheBoot, GlowwaterPotion, Spiral/Steady enchantments (A3c must reuse them); EndlessConveyor FRIED_EEL waits for A1a |
| A11f | Underdocks as act 1: act choice as in `ActModel` / `RunManager` discovery order, map bg, room art, boss icons, per-act music hook | engine | A11a-e | in progress (subagent) |

**A3a notes (enchantment engine, done).** `Enchantment` (game.h) is a `Model` owned by
`Card::enchantment`; write one like `Sharp` in `enchantments.cpp` (`ENCHANTMENT_HEADER(Name, "KEY")`,
`EnchantmentT<Name>`, register in `registerEnchantments()`; loc comes from `enchantments.<KEY>.*`,
`build_assets.py` collects it from the header). Already there: Sharp, Vigorous, Swift, Glam, Imbued,
PerfectFit, SlumberingEssence (they exercise every hook). What the engine gives you:
- hooks: `enchantDamage/BlockAdditive/Multiplicative`, `enchantPlayCount`, `onPlay`, `onEnchant`,
  `recalculateValues`, `shouldStartAtBottomOfDrawPile`, plus the Model hooks `afterAutoPrePlayPhaseEntered`,
  `beforeFlush` and `modifyShuffleOrder` (all other combat hooks reach an enchanted card's enchantment
  as usual); `status` (Normal/Disabled), `amount`, `vars`; `persist(Archive&)` for extra state (saves
  keep id, amount, status and vars on their own; save version is now 2, so old `run.sav` files are refused);
- `cmd::enchant(card, db::enchantment("Id"), amount)` (null if it can't; same stackable type adds up),
  `cmd::clearEnchantment`, `Run::enchantCard`, `Run::selectForEnchantment(id, count, filter)` (the deck
  picker for events, no preview screen yet), `Run::canEnchantAny(id, filter)` (for locked options);
- `Card::deckVersion` (Goopy), `Card::addKeyword/removeKeyword`, `Card::gainsBlock()` (approximation: a
  Block/CalculatedBlock var; override it on cards that gain block another way), `Card::adoptEnchantment()`
  (every new card `clone()` template must call it);
- debug: `STS_ENCHANT=Sharp:3,Glam` enchants the first fitting deck cards, `SIM_ENCHANT=1 ./build/sim N`
  spreads every registered enchantment over the deck, `make -f Makefile.sdl check` runs
  `test/enchant_test.cpp` (add a case there for each new enchantment with a rule of its own).
Open points for the next packages: `Enchantment::canEnchant` does not refuse Quest cards yet (A2 adds
that type); TezcatarasEmber and the `IsRemovable` rule need the Eternal card keyword (add `kwEternal` and make
deck removal skip such cards in A3c); enchantments do not hear run-level hooks outside combat (the C#
also lists deck cards there; none of the 22 needs it). The card text shows the enchantment's extra text and
replay line (`App::describe`); the badge, glow and the enchant preview screen are F5 / S13 (data:
`card->enchantment->showAmount()/displayAmount()/shouldGlowGold()/shouldGlowRed()/locKey`).

### Track C: rules that span the game

| id | Package | Kind | Needs | Status |
|---|---|---|---|---|
| C10 | Ascension 1-10 (SwarmingElites … DoubleBoss): `AscensionManager`, every `GetValueIfAscension` in ported content (script to list them), AscendersBane at A5, double boss at A10 | engine + content sweep, *Opus* | – | done (engine), accepted 2026-09-28 (merged by the reviewer: the lane session went offline before pushing) |
| C11 | Map extras: boss preview, the act's second boss as a map node at ascension 10 (C10 chains the fights, see notes), map legend | engine + UI | C10 | todo |

**C10 notes (ascension, done).** `Run::ascension` (0-10, `Run::start(seed, character, ascension)`; debug
`STS_ASCENSION=`, `SIM_ASC=`), `Run::hasAscension(kToughEnemies)`, `Run::ascValue(level, a, b)`. Saved (save
version 4). What each level does now:
- 1 SwarmingElites: 8 elites per map (`generateStandardActMap(rng, act, 8)`; the map rules can leave fewer);
  2 WearyTraveler: an Ancient heals 80% (Neow starts from 0 HP); 3 Poverty: fight gold x0.75 (truncated);
  4 TightBelt: 2 potion slots; 5 AscendersBane: the curse (`content_ascension.cpp`, keywords Eternal / Unplayable /
  Ethereal) starts in the deck; 6 Inflation: card removal 100 + 50 per use; 7 Scarcity: rare odds and their growth
  (`rollRarity`, shop) and the upgrade chance of reward cards per act (0.25 -> 0.125); 8 ToughEnemies / 9
  DeadlyEnemies: monster HP / damage and other numbers; 10 DoubleBoss: the last act's second boss.
- **New base rule found on the way:** reward and shop cards go through `Run::rollCardUpgrade` like
  `CardFactory.RollForUpgrade` (one Rewards float per card; from act 2 a reward card can arrive upgraded at
  `actIndex * 0.25`). This changes the Rewards RNG stream for every run, ascension or not.
- **Eternal cards** (`kwEternal`, `Card::isRemovable()`): `Run::selectFromDeck` does not offer them for
  `TO_REMOVE` / `TO_TRANSFORM` and `transformCard` skips them. The Eternal keyword text in the card body is UI (F5).
- **Monsters:** every ported monster's HP and damage / block / power / status numbers were swept
  (`tools/ascension_sweep.py` finds the C# values and edits the C++; `tools/ascension_check.py` compares HP and
  attack intents with the C# at levels 0 / 8 / 9 / 10 from `build/ascension_dump`; `make check` runs both when python
  and `../sts2-decompiled` exist). `STS_ASC_CHECK=1` prints a line when a move's damage differs from its intent.
  **A monster that is not ported yet must use `asc(...)` from the start: A11a-e (Underdocks), A7c
  (FakeMerchantMonster) and every later monster read the C# `GetValueIfAscension` lines.** The sweep script
  can list a new monster's values (`python tools/ascension_sweep.py --json`).
- **DoubleBoss:** `Run::secondBossId` (another boss of act 3, UpFront stream) is fought right after the first
  boss, which gives no rewards, as in the C# (RewardsSet skips every boss of the last act). PORT NOTE: the C#
  makes the second boss a map node; C11 replaces the chained fight with that node and the boss preview.
- Not ported: the modifier-run rules. (The character select shows `ascension.LEVEL_nn`, S04.)

### Track F: UI foundation (do this before redoing any screen)

The current UI is placeholder rectangles. Track F builds a small UI kit with StS2's
own art, and every screen in track S is then rebuilt with it.

| id | Package | Kind | Needs | Status |
|---|---|---|---|---|
| F0 | UI style guide `docs/UI_STYLE.md`: palette, font sizes per screen, margins, minimum touch target 32 px, button families, focus ring, animation timings, which sound each control makes. Mock three screens in the preview and get the owner's approval. | design, *Opus* | – | done (UI), accepted 2026-09-28 with notes (combat keeps the measured RGDSplus layout) |
| F1 | UI art extraction (`images/ui/**`, 255 files): buttons (proceed, confirm, cancel, back, end turn), panels, banners, tooltip frame, top bar, checkboxes, sliders, tabs, scrollbar, reward rows, shop tags, map legend, all intent and power icons, rarity gems, character energy orbs → a UI atlas | tools | F0 | done (UI), accepted 2026-09-28 |
| F2 | Renderer features: 9-slice, tint and alpha, scale and rotation, scissor clipping for scroll lists, text outline and shadow, gradients and fades (SDL and 3DS) | gfx | – | done (UI), accepted 2026-09-28 |
| F3 | Split `ui.cpp` into one file per screen (`source/ui/screens/*.cpp`), then the widget kit `source/ui/widgets.*`: Button (normal/focus/pressed/disabled, press animation), IconButton, Panel, ScrollList (drag, inertia, scrollbar), Grid, Tabs, Toggle, Slider, Paginator, Tooltip, Modal (confirm), Toast, Banner. One input model for touch and for D-pad/A/B/L/R with a focus ring. | UI, *Opus* | F1, F2 | done (UI), accepted 2026-09-28 with notes (tabs, art plates, row gap) |
| F4 | Rich text: inline icons (energy per character, gold, star, HP), keyword colours, keyword glossary tooltips from `HoverTips`, `choose()` / plural formatters for all loc strings (fixes MadScience) | UI | F3 | done (UI), accepted 2026-09-28 (fix {IsMultiplayer} "?" in F5) |
| F5 | Card renderer v2: faithful frames per type/rarity/character, portrait crop, cost gem (X, unplayable), type banner, green upgrade text, enchantment badge, affliction overlay. Three sizes: hand, large, grid mini. | UI, *Opus* | F1, F4 | done (UI), accepted 2026-09-28 |
| F6 | Motion: screen transitions (fade/slide), card draw/discard/exhaust flights, damage/block numbers, power icon pop, relic flash, gold/HP counters that tick, screen shake (setting), button feedback | UI | F3 | done (UI), accepted 2026-09-28 |
| F7 | Light VFX: hit sparks, slash, block shield, poison/burn ticks, heal, buff/debuff arrows, orb evoke (X3), stars (X4), Osty (X5). Cheap sprite effects only. | UI | F6 | todo |

### Track S: every screen rebuilt with the kit (RGDSplus U01-U33)

Each package: read the U row in `R4_ALL_PAGES` + `R4_LAYOUT_REVISION`, the matching
StS2 node (`Nodes.Screens.*`), rebuild the screen with the F kit, take before/after
screenshots of both screens, and tick the U table at the end of this file.

| id | U | Screen | Needs | Status |
|---|---|---|---|---|
| S01 | U01 | Boot splash, loading, act transition title card ("第二幕 蜂巢") | F3 | in progress (subagent) |
| S02 | U02 | Main menu: tall background across both screens, logo on top, buttons at 1.65× on the bottom (继续 / 单人 / 图鉴 / 统计 / 设置 / 退出), submenus | F3 | partial (native art, e2c9023) |
| S03 | U03 | Profiles: 3 slots, rename (3DS software keyboard), delete + warning | Y4 | todo |
| S04 | U04 | Character select: art and description on top; the 5 characters + Random, ascension, seed and start/back on the bottom; locked characters | F3, C10 | done, accepted 2026-09-28 (Mac): `drawCharacterSelect` in `title.cpp`; ascension 0-10 all open and the seed shown only (Y / tap re-rolls), both owner decisions; no locked characters (everything unlocked) |
| S05 | U05 | Custom run: modifier list, character, seed, confirm | M11 | todo |
| S06 | U06 | Neow / Ancient dialogue and relic choice (polish only) | F4 | todo |
| S07 | U07 | Map: legend, boss icon, path highlight, scroll bounds, top-screen preview, node pulse | F3 | todo |
| S08 | U08/U10 | Combat HUD: hand fan, energy orb, piles with counts, end turn, HP/block bars, power icons, intents with numbers, turn banner | F5, F6 | todo |
| S09 | U09/U12 | Targeting arrow and potion aim; enemy highlight | S08 | todo |
| S10 | U11 | Combat inspect: a 信息 button, cycle through creatures, power list with descriptions on top | S08 | todo |
| S11 | U13 | Draw, discard and exhaust piles and deck view: grid on the bottom, focused card on top, sort | F5 | partial (combat pile tabs) |
| S12 | U14 | Hand select (discard/exhaust/retain N): counter, confirm/cancel | F5 | todo |
| S13 | U15/U16 | Deck grid select (upgrade/remove/transform with preview) and choose-one | F5 | todo |
| S14 | U17/U18 | **Reward list** (gold, potion, relic, card rows: claim or skip, then proceed) + card reward with skip / Singing Bowl | F5 | done (subagent), accepted 2026-09-28: RewardsSet generate-then-offer (C# order gold, potion, card, relic), rows claimed in any order, Proceed forfeits the rest; CardRewardAlternative options besides Skip not yet |
| S15 | U19/U23 | Relic choice and treasure chest (chest opening, relic on top) | F3 | todo |
| S16 | U20 | Shop: goods grid, price tags, sale, removal service, focused item on top | F5 | todo |
| S17 | U21 | Events: art and text on top, large option buttons on the bottom, locked options shown with the reason | F4 | todo |
| S18 | U22 | Rest site: campfire and character on top, option buttons with descriptions | F3 | todo |
| S19 | U24 | Top bar: HP, gold, potion belt, relic strip with scroll, floor/act, run timer, deck/map buttons | F3 | todo |
| S20 | U25 | Detail popups for card/relic/potion: large on top with keywords, controls (upgrade preview, close) on the bottom | F5 | partial (modal + keyword pages) |
| S21 | U26 | Settings and pause menus (screens only; logic in Y1/Y2) | Y1 | partial (START page) |
| S22 | U27 | Tutorials, confirmations, errors | M13 | todo |
| S23 | U28 | Death / victory: score, badges, continue | M7 | partial (summary, menu/restart) |
| S24 | U29 | Compendium: cards, relics, potions, bestiary | M8-M10 | todo |
| S25 | U30 | Stats and run history | M6 | todo |
| S26 | U31 | Credits (scroll across both screens) | F3 | todo |
| – | U32 | Daily run: offline only (M12); leaderboards n/a | – | n/a |
| – | U33 | Unknown or mod pages | – | n/a |

### Track X: the other four characters

Each character follows the same seven packages. The C# sources are
`Models.Characters\<C>.cs`, `CardPools\<C>CardPool.cs`, `RelicPools\<C>RelicPool.cs`,
the epoch that lists their potions (`Timeline.Epochs\<C>4Epoch.cs`), `Models.Cards\`
and `Models.Powers\`.

| Step | What it covers |
|---|---|
| 0 | Systems (*engine, Opus*) |
| 1 | Starter deck and starting relic, the 8 character relics, the 3 potions, energy orb |
| 2 | Common cards |
| 3a | Uncommon cards, first half |
| 3b | Uncommon cards, second half |
| 4 | Rare cards, then tokens |
| 5 | Visuals: Spine in combat, rest and shop; character select art; card frame colour; win/lose poses; `SIM_ALLCARDS` whole runs |

| id | Character | Step 0 systems | Status |
|---|---|---|---|
| X0 | All | Character plumbing (see Engine lane order #1) | done (engine), accepted 2026-09-28 |
| X1.0-X1.5 | Silent | Shiv tokens, Poison, discard triggers (Sly), Retain, Accuracy-style powers | X1.0 done (engine), accepted 2026-09-28; X1.1 done (subagent), accepted 2026-09-28; X1.2 done (subagent), accepted 2026-09-28; X1.5 done, accepted 2026-09-28; X1.3a done (subagent), accepted 2026-09-28 (engine hook Combat::attackPlaysFinishedThisTurn (Finisher)); X1.3b done (subagent), accepted 2026-09-28 (engine hook shivPlaysFinishedThisTurn); X1.4 done (subagent), accepted 2026-09-28 (Envenom on afterDamageGiven; BladeOfInk waits for Inky (A3b)) |
| X2.0-X2.5 | Defect | Orbs (Lightning, Frost, Dark, Plasma, Glass), channel/evoke, Focus, orb slots and their rendering on the top screen | X2.0 done (subagent), accepted 2026-09-28; X2.1 done (subagent), accepted 2026-09-28; X2.2 done (subagent), accepted 2026-09-28; X2.5 done, accepted 2026-09-28; X2.3a done (subagent), accepted 2026-09-28 (Feral/Iteration/Ftl approximate the missing card-play history (PORT NOTEs)); X2.3b done (subagent), accepted 2026-09-28 (status cards now fire the generated-card hook (reviewer fix)); X2.4 done (subagent), accepted 2026-09-28 |
| X3.0-X3.5 | Regent | Stars (second resource with a HUD counter), Forge and Sovereign Blade, summons | X3.0 done (subagent), accepted 2026-09-28; X3.1 done (subagent), accepted 2026-09-28; X3.2 done (subagent), accepted 2026-09-28; X3.5 done, accepted 2026-09-28; X3.3a done (subagent), accepted 2026-09-28 (engine hook Combat::skillsFinishedThisTurn (LunarBlast); ManifestAuthority's colorless card waits for A1a); X3.3b done (subagent), accepted 2026-09-28 (Quasar/SpectrumShift wait for A1a; hooks cardPlaysFinished/starsGained/cardsGenerated); X3.4 done (subagent), accepted 2026-09-28 (BundleOfJoy / HeirloomHammer pick wait for A1a) |
| X4.0-X4.5 | Necrobinder | Osty (companion creature with its own HP, targeting, death), Doom, Souls | X4.0 done (subagent), accepted 2026-09-28; X4.1 done (subagent), accepted 2026-09-28; X4.2 done (subagent), accepted 2026-09-28; X4.5 done, accepted 2026-09-28; X4.3a done (subagent), accepted 2026-09-28 (Debilitate doubles Vulnerable/Weak in powers.h (C# order); DeathMarch/DeathsDoor/Fetch keep their own per-turn records); X4.3b done (subagent), accepted 2026-09-28 (hooks afterDamageGiven, afterCardPlayedLate); X4.4 done (subagent), accepted 2026-09-28 (SweepingGaze token) |
| X6 | All characters | ColorfulPhilosophers, cross-character Orobas options, per-character Ancient dialogue lines, Random character, per-character act-transition quotes | todo |

That is 7 packages per character (X*.0, .1, .2, .3a, .3b, .4, .5), X1-X4 in order. X6
needs X1-X4. Every character needs track F done first, so their cards are drawn
with the new renderer.

**X0 notes (character plumbing, done).** `Character` (game.h) is one row per playable character
(HP, gold, energy, orb slots, starter deck, starting relic, card / relic / potion pools in the C# order,
multiplayer-only cards); the rows are generated into `source/core/characters.inc` by
`tools/gen_characters.py` from the decompiled C# (rerun it after a decompiler update). `Run::start(seed,
characterId)` reads it; `Run::characterId` / `Run::character()` replace every Ironclad assumption:
- pools: `db::characterCards(run.characterId, filter)` (pool order, no multiplayer-only cards, only
  registered cards), `db::potionPool(characterId)`, `run.character().cardPool / relicPool`; the old
  `db::ironcladCards / ironcladPool / ironcladRelicPool / ironcladStarterDeck` are Ironclad-only shortcuts, do not use
  them in new code. Reward, transform, shop, Neow / Ancient, events, potions and relics already go through the
  character. LargeCapsule adds the character's Basic Strike and Defend;
- loc keys use `Character::key` (`NEOW.talk.<KEY>.0-0.ancient`, `characters.<KEY>.aromaPrinciple`); `build_assets`
  now bakes the `characters` table for all five;
- combat: `Combat::maxEnergy` comes from the character. **Not wired yet (X1.0-X4.0 do it):** `orbSlots`,
  `alwaysShowStars`, energy orb colour, card frame colour (`energyColor`, `cardFrame` are in the table for F5);
- UI: the player's Spine and portrait use `Character::key` and fall back to the Ironclad's art until that
  character's art is baked (X*.5); `STS_CHAR=Silent` picks the character in the preview (the select screen
  is S04); `SIM_CHAR=Silent ./build/sim N` does the same headless. A character whose cards are not registered
  yet starts with the cards that exist (`db::characterPlayable(id)` tells whether it is complete);
- saves: version 3 stores the character right after the seed; version 2 saves load as Ironclad runs;
- tests: `make -f Makefile.sdl check` now also runs `test/character_test.cpp`.
A new character's per-character systems go in `source/core/char_<name>.cpp` (a hook in a core file only when
it cannot be avoided). New card base classes must set the card frame / pool the same way `IroncladT` does and
their `clone()` must call `adoptEnchantment()`.

**X2.0-X4.0 notes (Defect, Regent, Necrobinder systems, done).** Engine pieces the card packages build on:
- **Defect:** `Orb` (game.h) and the 5 orbs in `char_defect.h`; `Combat::orbQueue` / `orbCapacity` (from
  `Character::orbSlots` in `Run::fight`); `cmd::channelOrb / evokeNextOrb / evokeLastOrb / orbPassive / addOrbSlots /
  removeOrbSlots`; `db::randomOrb`; FocusPower / TemporaryFocusPower; hooks `modifyOrbValue`,
  `modifyOrbPassiveTriggerCount`, `afterOrbChanneled`, `afterOrbEvoked`. Orbs are not drawn yet (UI).
- **Regent:** `Combat::stars`, `Combat::starCost(card)`, `Card::starCost / costsStarsX / starXValue`,
  `cmd::gainStars / loseStars / setStars`, star checks in `canPlay` / `playCard`; `cmd::forge` and the Sovereign
  Blade token (`tagSovereignBlade`); StarNextTurnPower, SeekingEdgePower, ParryPower. ChildOfTheStars / DyingStar
  powers are left for their cards. No star counter in the HUD yet (UI).
- **Necrobinder:** `summonOsty(combat, amount)` (Osty is `Combat::osty`, a player-side creature with
  `petOwner`, not in `enemies`); `cmd::damage` now has the C# Before/After-Osty phases and
  `modifyUnblockedDamageTarget` (DieForYouPower) with overkill spilling onto the owner; DoomPower + `doomKill`,
  SoulboundPower, NecroMasteryPower, the Soul token (`createSoulsInHand`). Osty is not drawn yet (UI).

**X1.0 notes (Silent systems, done).** Shared engine pieces the Silent's cards (X1.1-X1.4) build on:
- **Discard:** `cmd::discardCards(combat, cards, drawAfter)` / `discardCard` = `CardCmd.Discard` /
  `DiscardAndDraw`: cards move one by one (`Model::afterCardDiscarded` for each), then the draw, then every card
  that was Sly is auto-played. Use the list form for several cards. The end-of-turn flush is not a discard (no hook,
  no Sly), as in the C#. GamblersBrew and GamblingChip now use it. `Combat::discardsThisTurn()` is the
  `CardDiscardedEntry` count of this turn (MementoMori).
- **Keywords:** `kwSly`; `Card::singleTurnSly / singleTurnRetain` (`giveSingleTurnSly`-style effects just set the flag;
  cleared at the end of the turn), read through `isSlyThisTurn()` / `shouldRetainThisTurn()` (the flush uses the
  latter). **The card text does not show Sly / Retain / Innate yet** (the C# adds them from the keyword lists):
  that is UI (F5).
- **Tags:** `tagMinion`, `tagOstyAttack`, `tagShiv` next to `tagStrike` / `tagDefend`.
- **`char_silent.h/.cpp`:** `PoisonPower` (turn-start damage, Accelerant, `calculateTotalDamageNextTurn()` for a
  future intent preview), `AccelerantPower`, `AccuracyPower`, `FanOfKnivesPower` (keeps every Shiv's target in step
  because `Card::target` is a field, not a property), the `Shiv` token and `createShivsInHand(combat, n)`.
  Cards include `char_silent.h`; no Silent card is registered yet (the character is not `characterPlayable`).
- `cmd::loseBlock` (no AfterBlockBroken hook yet).
- Tests: `test/silent_test.cpp` (in `make check`). Not done here on purpose: the powers only one card needs
  (Envenom, NoxiousFumes, ...) belong to the content packages that port those cards.

### Track M: meta and progression

| id | Package | Kind | Needs | Status |
|---|---|---|---|---|
| M1 | Profile save `progress.sav` (versioned, atomic write): per-character wins, losses, best streak, max ascension; seen/unlocked cards, relics, potions and monsters; counters for stats and achievements | engine, *Opus* | – | done (subagent), accepted 2026-09-28: `progress.h/.cpp` (ProgressSaveManager rules, seen sets, counters); not persisted yet (Y4 picks the path and calls `progress::save/load`), `Run::abandon()` still to be called from the UI abandon button |
| M2 | Run history store: the last 50 runs (seed, character, ascension, path, deck, relics, floor reached, killed by, time, score) | engine | M1 | todo |
| M3 | Timeline / epochs engine | – | – | n/a (owner: everything unlocked) |
| M4 | Timeline screen + unlock reveals | – | – | n/a (owner: everything unlocked) |
| M5 | Achievements (22, `Achievements\`): checks, toast, achievement list page | content + UI | M1 | todo |
| M6 | Stats screen: general and per-character stats (`Nodes.Screens.StatsScreen`) + run history viewer | UI | M2, F3 | todo |
| M7 | Score and badges at the end of a run (28 badges, `Models.Badges`), used by S23 | content | M2 | todo |
| M8 | Card library: filters by character, type and rarity; upgrade toggle; seen/locked | UI | M1, F5 | todo |
| M9 | Relic collection + potion lab | UI | M1 | todo |
| M10 | Bestiary: monster list, Spine viewer, moves | UI | M1 | todo |
| M11 | Custom run: 16 modifiers (`Models.Modifiers`), seed entry with the 3DS keyboard, seeded runs | engine + UI | M1 | todo |
| M12 | Daily run, offline: seed and modifiers from the date as in `Daily\`, local best score only | engine | M11 | todo |
| M13 | Tutorials (`Nodes.Ftue`): first-run tips, reset from settings | UI | F3 | todo |

### Track Y: system

| id | Package | Kind | Needs | Status |
|---|---|---|---|---|
| Y1 | Settings store `settings.sav` + logic: fast mode, screen shake, BGM/SFX/ambience volume, language, run timer, text effects, long-press confirm, common tooltips, hand card count, reset tutorials, delete data | engine | – | done (subagent), accepted 2026-09-28: `settings_store.h/.cpp` (C# SettingsSave + PrefsSave merged into settings.sav), loaded in App::init; volumes not wired to audio yet (U track), no settings screen yet (S21) |
| Y2 | Pause menu (START during a run): resume, settings, deck, compendium, save & quit, abandon run (with confirm) | UI | Y1, F3 | todo |
| Y3 | Languages: bake English + 简体中文 loc and fonts, switch at runtime | tools + UI | Y1 | todo |
| Y4 | Profiles: 3 slots on SD, rename and delete; the run save and progress files are per profile | engine | M1 | todo |
| Y5 | 3DS system behaviour: sleep when the lid is closed, HOME menu, safe saves on power loss, SD errors shown in a dialog | 3DS | – | todo |

### Track U: audio

| id | Package | Kind | Needs | Status |
|---|---|---|---|---|
| U1 | Extraction: FMOD `banks/desktop/*.bank` (FSB5) and `*.mp3` → DSP-ADPCM in romfs, with a list of events and names; keep the size in budget | tools, *Opus* | – | in progress (subagent) |
| U2 | Audio engine: 3DS ndsp streaming of ADPCM music from romfs, SFX voices; SDL backend decodes the same files; SDL backend | platform | U1 | todo |
| U3 | Music routing: title, each act's map and fights, elite, boss, shop, rest, Ancient, victory, death; crossfades | UI | U2 | todo |
| U4 | SFX: cards, hits, block, buffs, gold, relics, potions and UI controls, mapped from the C# `SfxCmd` names | content | U2 | todo |
| U5 | Ambience per room, and wiring the volume sliders | UI | U3, Y1 | todo |

### Track H: hardware and release

| id | Package | Kind | Needs | Status |
|---|---|---|---|---|
| H1 | Performance on the New 3DS: frame time (Spine skinning, text layout, atlas binds), load times, hot spots | 3DS | – | todo |
| H2 | Memory: a full run with each character on hardware and a sim soak (1000 runs per character), no leaks, linear memory within limits | 3DS | X* | todo |
| H3 | Romfs budget with all characters and audio; texture and audio quality checks on the device | tools | U1, X* | todo |
| H4 | Packaging: icon, banner (with its sound), title id, `.3dsx` + `.cia` builds | tools | – | todo |
| H5 | Balance and bugs: compare numbers with the C# for every character (script), fix known PORT NOTEs | content | X* | todo |
| H6 | Release checklist: every character wins a run on the device, every U page ticked, every package done, no PORT NOTE left without an n/a reason | QA | all | todo |

---

## Order

With one session: each phase starts when the one before it is done. With lanes (below):
each lane takes its own packages in this order and only waits on the listed dependencies. Within a phase the packages are
listed in order.

1. **UI foundation:** F0 → F1 → F2 → F3 → F4 → F5 → F6.
   Everything visible after this uses the kit, so no screen gets built twice.
2. **The screens that exist today:** S14 (reward list), S08, S09, S10, S11, S12, S13,
   S16, S17, S18, S19, S20, S06, S07, S15, then S01, S02, F7.
3. **Ironclad content complete:** A1a-c, A2, A3a-c, A5, A6, A7a-d, A8, A9, A10,
   C10, C11, then A11a-f. (A4 afflictions moves to the end of phase 6.)
4. **System:** Y1, Y2 + S21, Y5, Y3, M1, Y4 + S03.
5. **Characters:** X1 (Silent), X2 (Defect), X3 (Regent), X4 (Necrobinder), then X6 + S04.
6. **Meta:** M2, M5, M6, M7 + S23, M8, M9, M10 + S24, S25, M11 + S05, M12,
   M13 + S22, S26, then A4.
7. **Audio:** U1 → U5.
8. **Release:** H1, H3, H2, H4, H5, H6.

Rough size: 23 content, 2 rules, 8 UI foundation, 26 screens, 29 character,
11 meta, 5 system, 5 audio and 6 release packages, about **115 packages** in all.

## Lanes (running several sessions at once)

Up to four sessions can work at the same time, one per lane. Each lane owns different
files, so they rarely conflict. Each session works in its own git worktree on the
same PC (`git worktree add ../sts2-3ds-<lane> main`), with its own `build/`. Copy
`romfs/` and `romfs_3ds/` into the worktree once; `build_assets.py` can also be rerun there.

| Lane | Model | Packages | Owns |
|---|---|---|---|
| **UI** | Opus | F0-F7, then S01-S26, X*.5 (character visuals), M6/M8-M10 screens, Y2 | `source/ui/**`, `source/gfx/**`, `source/platform_*/gfx*`, UI parts of `build_assets.py` |
| **Engine** | Opus | see *Engine lane order* below | `game.h`, `combat.cpp`, `run.cpp`, `save.cpp`, `mapgen.cpp`, `acts.cpp` |
| **Content** | Sonnet | A1a-c, A2, A3b-c, A5-A9, A11a-e, M5, M7, then X*.1-X*.4 for each character after its X*.0 | new `content_*.cpp` / `relics_*.cpp` / `events_*.cpp` files, one registration line each |
| **Audio** | Sonnet (U1: Opus) | U1-U5, then H4 | `source/audio/**`, `source/platform_*/audio*`, `tools/audio*.py` |

**Engine lane order** (owner, 2026-09-28: character mechanics first and in parallel;
3DS/hardware work after ascension; afflictions last):

| # | Package | Who | Why this position |
|---|---|---|---|
| 1 | **X0 Character plumbing** | engine lane | A small package that must come first. `Run::start(seed, characterId)`; a `Character` table built from `Models.Characters\*.cs` (HP, gold, starter deck, starting relic, card, relic and potion pools, energy orb, Spine id). Everything that assumes the Ironclad must read it from the character: the IRONCLAD Spine, `Res` keeping the IRONCLAD pages, the `_ironclad` icon fallback, card frame colour, `db::cardPool`, Neow/Ancient per-character lines, saves (character id, save version 3), and sim `SIM_CHAR=Silent`. Each character's system hooks then live in their own file. |
| 2 | **X1.0 Silent · X2.0 Defect · X3.0 Regent · X4.0 Necrobinder, in parallel** | engine lane does X1.0; three separate sessions that the owner opens ("角色线 Defect / Regent / Necrobinder", Sonnet, one worktree each) do X2.0-X4.0; the reviewer session reviews them and does not spawn subagents | Each system goes in `source/core/char_<name>.cpp` with the smallest possible hooks in core files. The content lane starts each character's cards (X*.1-X*.4) as soon as that character's X*.0 is accepted. |
| 3 | C10 Ascension | engine lane | Before the Underdocks monsters are ported (no second sweep). |
| 4 | Y5 3DS system behaviour + H1 performance pass + H3 romfs budget check on real hardware | engine lane | The owner wants the 3DS optimisation and hardware-limit tests right after ascension. |
| 5 | M1 Profile / progress save | engine lane | Unblocks achievements and the compendium/stats screens. |
| 6 | Y1 Settings store | engine lane | Unblocks S21 and Y3. |
| 7 | A10 TheArchitect ending | engine lane | – |
| 8 | A11f Underdocks as act 1 | engine lane | Once A11a-e are done. |
| 9 | C11, Y4, M2, M11, M12 | engine lane | – |
| 10 | X6 Cross-character content | engine lane | Needs all X*.1-X*.4. |
| 11 | A4 Afflictions | engine lane | Last, by the owner's decision. |
| 12 | H2, H4-H6 | – | Release packages. |

Waiting points: content A3b needs A3a, and X*.1 needs X*.0. The engine lane does those
first, so content never waits. The UI lane stubs anything that does not exist yet (the
enchantment badge in F5, characters in S04) and fills it in when it lands. The audio
lane is independent until U3/U4, which only add play calls at existing places.

Several agents in one lane. A lane leader session may run Sonnet subagents
(`isolation: worktree`, one package each, tight brief, and the leader reviews and
merges), or the owner opens more sessions for the same lane. Only packages that touch
disjoint files can run side by side:
- **Content:** up to 3-4 at once.
  - A1b, A1c, A2, A7a, A7b and A11a-e are parallel-safe, but A1b and A1c start only
    after A1a has created the colorless pool.
  - For each character, X*.2, X*.3a, X*.3b and X*.4 can all run together once its
    X*.1 is done.
- **UI:** one agent until F3. F3 splits `ui.cpp` into one file per screen, and after
  that two S packages can run at once. The owner's reviews are the real limit.
- **Engine:** one agent only; its packages all edit the same core files.
- **Audio:** one agent. U4 (mapping the sound effects) can be split in two.

Rules:
1. **Claim before starting.** Set the package status to `in progress (<lane>)`, then
   commit and push that one line. If the push is rejected, pull, and pick another package.
2. **Small commits, pull often.** Run `git pull --rebase` before every commit. Never
   force-push, and never revert another lane's files.
3. **Engine edits from other lanes.** A lane that needs a hook in an engine file adds
   the smallest possible hook in the style of the existing ones. It says so in its
   commit message.
4. **A content package that needs a missing engine feature** does not build the
   feature itself. It locks that card, relic or option with a PORT NOTE naming the
   package that will unlock it, or it waits.
5. **UI changes need the owner's review.** The UI lane shows screenshots to the owner.
   Other lanes do not change how screens look, except to add text for new content.
6. **Talk to the owner in Chinese.** At the end of each package, report what was done,
   the tests, and the next package.

## Acceptance (reviewer session)

Every finished package is checked before the next one builds on it. A lane can run the
same checks itself first. The reviewer runs:

1. **Read the diff against the C#.** Check numbers, hook order, RNG call order and
   the conditions in each hook.
2. **Build both targets.** `make -f Makefile.sdl` and `make` must both pass (the 3DS
   `.3dsx` timestamp must change), plus `make -f Makefile.sdl check`.
3. **Save/load matrix.** For each of the env sets `SIM_ALLCARDS=1`,
   `SIM_ALLRELICS=1 SIM_ALLCARDS=1`, `SIM_ALLPOTIONS=1`, `SIM_ENCHANT=1`, and plain,
   run `./build/sim 12` with and without `SIM_SAVELOAD=K` for K = 3, 9, 17, 30. The
   `seed` lines must be identical.
   `bash tools/accept.sh --3ds` (UCRT64 shell) runs steps 2-3 and prints `ACCEPT: PASS` /
   `FAIL`. Use `--quick` between commits. Set `ACCEPT_CHARS="Silent"` to add a character's
   ALLCARDS runs once its cards exist.
4. **Screenshots**, if the package changes the UI.
5. **Record the result** in the status column (`accepted <date>`) and fix what was
   found, or send it back to the lane.

A3a review (2026-09-28): the engine matches `EnchantmentModel` / `Hook` / `CardModel`
and passes all checks. The review also found two older bugs, both now fixed:
- BoneTea, EmberTea, TeaOfDiscourtesy, SwordOfStone and PollinousCore did not save their
  counters (`[SavedProperty]` in the C#).
- The sim did not keep its free-map setting after a load.
Any new relic with a C# `[SavedProperty]` needs `persist`.

## Owner decisions (2026-09-28)

- **Everything is unlocked from the start.** There is no timeline or epoch progression:
  all characters, acts (incl. Underdocks), cards, relics and potions are available.
  M3/M4 are n/a. Where the C# asks `UnlockState`, answer "unlocked".
- **Languages: 简体中文 + English** (Y3).
- **Audio: ADPCM** (3DS DSP-ADPCM, played by ndsp without a decoder). Watch romfs size:
  use mono or a lower sample rate for long music tracks if needed (U1, H3).
- **New 3DS only.**
- **Ascension 0-10 is open for every character** from the start (no win-to-unlock); the character
  select shows the run's seed but has no seed entry (custom runs, U05, get one).

---

## Porting cheat sheets

### Events (C# → C++)

C#: `Models.Events\<Name>.cs`, base `Models\EventModel.cs`. Read the `AromaOfChaos`
example in `events_act1.cpp` and the events section of `game.h`.

| C# | C++ |
|---|---|
| `new EventOption(this, Fn, "K.pages.P.options.O")` | `option("P", "O", [this]{ return fn(); })` |
| null action / `_LOCKED` | `EventOption{page("P") + ".options.O_LOCKED", nullptr}` |
| `SetEventFinished(L10NLookup("K.pages.X.description"))` | `setFinished("X")` |
| a new page | `setPage("X", {...})` |
| `IsAllowed` | `isAllowed(Run&)` |
| `CanonicalVars` / `CalculateVars` | `calculateVars()` with `addVar` / `setVar`, using the C# names |
| `CardSelectCmd.FromDeck*` | `co_await run->selectFromDeck("card_selection.TO_X", filter, n [, canCancel, showUpgrade])` |
| transform to random | `run->transformCard(c, run->randomTransformFor(c, rng()))` |
| add / remove / upgrade a card | `run->addCardToDeck`, `removeCardFromDeck`, `c->upgrade()` |
| gold | `co_await run->gainGold(n)` |
| HP | `co_await run->loseHp(n)`, `gainMaxHp` / `loseMaxHp` |
| obtain a relic | `co_await run->obtainRelic(db::relic("X"))` |
| an event fight | `bool won = co_await run->eventFight("EncounterId");` |

### Monsters

For each encounter:
- translate `Models.Encounters\<Id>.cs`, its monsters (`Models.Monsters\`) and any new
  powers, in a new `content_*.cpp` registered from `db::init()`;
- follow the style of `content_act1.cpp` (plain monsters), `content_phrog.cpp` (spawns and
  stun) and `content_bosses.cpp` (minions and illusions);
- the monster loc key is the UPPER_SNAKE form of the class name.

Test with `STS_ENCOUNTER=<Id> SIM_FIGHTS=1 ./build/sim 4` and take a preview
screenshot. The art comes from `scenes/creature_visuals/<snake>.tscn`. If an
animation name is unusual, extend the fallback in `App::trigger`.

### Characters (track X)

- The character's loc key is `characters.<UPPER>`.
- Cards go in `content_<char>_*.cpp` with `CARD_HEADER`; the pool order must be the
  order of the C# `CardPool` file, because the reward RNG depends on it.
- The starting HP, gold, deck and relic come from `Models.Characters\<C>.cs`.
- Character selection makes `Run::start` take a character id. Every place that
  assumes the Ironclad (`IRONCLAD` Spine, the red card frame, the `_ironclad` icon
  fallback, `Res` keeping only the IRONCLAD pages) must read it from the character
  instead. X1.0 finds and fixes all of them.

---

## RGDSplus page table (U01-U33)

| U | Our screen | Package | Status |
|---|---|---|---|
| U01 | Boot, act transition | S01 | todo |
| U02 | Main menu | S02 | partial |
| U03 | Profiles | S03 | todo |
| U04 | Character select | S04 | done |
| U05 | Custom run | S05 | todo |
| U06 | Neow / Ancients | S06 | works, polish in S06 |
| U07 | Map | S07 | works, polish in S07 |
| U08 | Combat layout | S08 | works, polish in S08 |
| U09 | Drag targeting | S09 | works |
| U10 | Creatures on top | S08 | works |
| U11 | Combat inspect | S10 | missing |
| U12 | Potions | S09 | works |
| U13 | Pile / deck views | S11 | partial (combat tabs in SDL) |
| U14 | Hand select | S12 | works, polish |
| U15 | Deck grid select | S13 | works |
| U16 | Choose one | S13 | works |
| U17 | Reward list | S14 | missing (sequential popups today) |
| U18 | Card reward | S14 | works |
| U19 | Relic choice | S15 | works |
| U20 | Shop | S16 | works |
| U21 | Events | S17 | works |
| U22 | Rest site | S18 | works |
| U23 | Treasure | S15 | works |
| U24 | Top bar | S19 | partial |
| U25 | Detail popups | S20 | partial (modal, keywords) |
| U26 | Settings | S21 | partial (START page) |
| U27 | Tutorials / dialogs | S22 | missing |
| U28 | Death / victory | S23 | partial (summary) |
| U29 | Compendium | S24 | missing |
| U30 | Stats / history | S25 | missing |
| U31 | Credits | S26 | missing |
| U32 | Daily / leaderboards | M12 | offline daily only; leaderboards n/a |
| U33 | Unknown pages | – | n/a |
