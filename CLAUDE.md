# StS2 → Nintendo 3DS port (personal project)

Slay the Spire 2 re-implemented in C++20 for the New 3DS, translated from the
game's decompiled C#. **Personal use only**: never commit or distribute anything
derived from the game (`romfs/`, `icon.png`, `.3dsx`, screenshots, decompiled
code). `.gitignore` is a whitelist; keep it that way.

The owner works on this from a Mac and a Windows PC.

## What to work on

`docs/PLAN.md` has the goal (a finished port as complete as the RGDSplus one),
the session protocol, an inventory of what is still missing versus the C#, and
~117 small packages in tracks (A content, C rules, F UI kit, S screens, X
characters, M meta, Y system, U audio, H release). Start there: take the first
`todo` package in its **Order** section whose dependencies are done. Read only the
files a package names — don't survey the whole codebase or decompiled tree.
The RGDSplus reference repo is cloned next to this one as `../rgds-ref`
(`gh repo clone LPF970915/Slay-the-Spire-for-RGDSplus ../rgds-ref -- --depth 1`).

## Working agreement

- Usage quota is shared by all the owner's sessions and subagents. Bulk,
  mechanical translation is cheapest in a Sonnet session or a Sonnet subagent
  with a tight brief. The owner wants maximum throughput: run as many parallel
  lanes as the build capacity allows (see *Parallel agents*) without asking
  first, and say how many and why. Keep design, debugging, review and merging
  in the main session.
- Measure before scheduling around a cost. If a build, test or tool run takes
  minutes, find out why (flags, -j, swapping, what gets rebuilt) before adding
  queues or more agents.
- Install every dev tool globally (apps in /Applications or Program Files,
  CLIs via Homebrew / winget / dkp-pacman / `dotnet tool install -g`), never
  in a session scratchpad, temp dir or the project. Work copies that must
  survive go in stable paths too (e.g. the build scheduler in `~/dev`).
- Don't change system settings; commands that need `sudo` or passwords are
  given to the owner to run.
- Git: `git pull` before starting, commit + `git push` when done, so the other
  machine is never behind.

## Folder layout (both machines)

```
<parent>/
  sts2-3ds/          this repo
  sts2-decompiled/   python3 tools/decompile.py  (porting reference, not in git)
```

## Build rule: no RTTI, no exceptions on the 3DS

devkitARM builds with `-fno-rtti -fno-exceptions`: no `dynamic_cast`, `typeid`,
`throw` or `try`. Identify types by id fields (e.g. `Monster::id`) and handle
"impossible" cases with a fallback. Always check `make` (3DS) as well as the
preview build before committing.

## Layout rule: the bottom screen is narrower

The bottom screen is 320 px wide, the top 400 px; in the virtual two-screen
canvas the bottom sits centred (x 40..360). Anything drawn continuously across
both screens (map parchment, arrows, tall backgrounds) must keep its content
within that width so it doesn't overflow the bottom screen. Check both screens
in preview screenshots.

## Setup

New Windows PC: run `tools/setup_windows.ps1` (installs Git, gh, Python,
.NET + ilspycmd via winget, clones to `C:\dev\sts2-3ds`, decompiles, builds
assets); devkitPro and Azahar are manual installs it links to.

- Python 3 + Pillow + numpy. `python3 tools/gamepaths.py` must find the game
  (Steam; override with `STS2_DIR` / `STS2_PCK`).
- Assets: `python3 tools/build_assets.py` → `romfs/` (and `icon.png`). Rerun
  after any change to UI strings: the font only contains glyphs used by the
  game's loc text and by string literals in `source/`.
- Decompile: .NET SDK + `dotnet tool install -g ilspycmd`, then
  `python3 tools/decompile.py`.
- 3DS: devkitPro with `3ds-dev` (Windows: devkitPro installer, or — as on the
  current PC — the dkp pacman repos added to a plain MSYS2 at `C:\msys64`,
  toolchain in `/opt/devkitpro`; macOS: pkg + `sudo dkp-pacman -S 3ds-dev`). devkitPro's make breaks on paths with spaces
  (and is unreliable with non-ASCII), so keep the repo on a path without either: Mac
  `/Users/m/projects/sts2-3ds-work/sts2-3ds` (renamed from `sts2 3ds` on 2026-10-03; the old
  `~/dev/sts2-3ds-build` copy is obsolete), Windows `C:\dev\sts2-3ds`. 3DS builds run in the repo.
- Desktop preview: SDL2 + clang/g++. `Makefile.sdl` uses `sdl2-config`
  (macOS: `brew install sdl2`; Windows: MSYS2 UCRT64 with
  `mingw-w64-ucrt-x86_64-{gcc,SDL2,pkgconf}` + `make`, built from the UCRT64
  shell; `Makefile.sdl` strips SDL's `main` wrapper there).
- Windows gotcha: Python writes text as CRLF unless told otherwise; romfs text
  files must be LF (the Spine atlas parser keeps `\r` in texture paths), so
  pass `newline='\n'` to any new `open(..., 'w')` in `tools/`.
- Emulator: Azahar (Mac: /Applications/Azahar.app). Its SD card: macOS `~/Library/Application Support/Azahar/sdmc`,
  Windows `%APPDATA%\Azahar\sdmc`.

## Build & test

```bash
make -f Makefile.sdl -j4        # build/sts2-preview + build/sim (release: -O2 -g)
make -f Makefile.sdl DEV=1 -j4 build/dev/sim   # fast iteration build (-O1) in build/dev/
./build/sim 200                 # headless fights; SIM_ALLCARDS=1 plays every card
bash tools/soak.sh [N] [jobs]   # memory soak: sanitizer sim (make -f Makefile.sdl sim-asan / sim-ubsan) x all characters/mixes, one line per config
make                            # sts2-3ds.3dsx (devkitPro env); packs romfs_3ds/
python tools/compress_romfs.py  # romfs/ -> romfs_3ds/ (GPU texture formats), after build_assets
                                # lossless: RGB8 opaque / RGBA8 / fonts A8->LA8 (pick_format; LOSSY= prefixes fall back to ETC1)
                                # 16 MB app heap, ~103 MB linear; STS_MEM_LOG=1 writes sdmc:/sts2-mem.txt
make link                       # build + send to the 3DS over Wi-Fi (IP=... if needed)
make cia                        # also sts2-3ds.cia (title id 000400000FA57200, tools/sts2-3ds.rsf)
```

### Build discipline (both machines)

- `Makefile.sdl` never forces `-j`; pass it yourself (`-j4`). It used to add `-j<ncpu>`, which with
  several builds at once swapped the 8 GB Mac to a crawl (load 300+, rebuilds of many minutes) and
  froze a session. Never add it back.
- Iterate with `make -f Makefile.sdl DEV=1 -j4 build/dev/sim build/dev/<x>_test` (`-O1`, no debug
  info, own objects in `build/dev/`), run `./build/dev/...`. Before merging, run the release
  `make -f Makefile.sdl -j4 check` once (`-O2 -g` in `build/`); the 3DS build is separate.
- Build only the targets you need; `check` once at the end, not per edit.
- Adding a virtual hook to `Model` (game.h) recompiles every card/power/relic file (~70 of 94):
  batch all header edits before building, then iterate on .cpp files only.
- Builds go through ccache (Mac: Homebrew `ccache`; config in `~/Library/Preferences/ccache/ccache.conf`,
  `base_dir` = the main repo, `hash_dir = false`, so all worktrees share one cache).
- Long CPU runs (sim soaks, sanitizer passes, `tools/soak.sh`) only when no builds are running, at
  most 2 sim processes, `nice`d.

### Parallel agents (Mac, lead session)

- Every agent build/test goes through `/Users/m/dev/sts2-build-lock.sh <timeout-s> <command>`:
  a `make` gets 1-4 tokens from a pool of 6 (= at most 6 compilers machine-wide), starts as soon as
  it has one, and builds of more than 8 files may only use 4 tokens so small builds never queue;
  it strips any forced `-j`, adds ccache, and logs waits to `/Users/m/dev/.sts2-build-wait.log`.
  Other commands run immediately at `nice 15`. Source: `tools/build_lock.sh`; the installed copy at
  `/Users/m/dev/sts2-build-lock.sh` is replaced with `cp` + `mv` (atomic: running instances keep the
  old file). Run it with `run_in_background` and wait for the
  notification; no sleep-polling. Check the wait log: the target is under 1 minute.
- Agents work in `isolation: worktree` and start from a clean `build/` (no copying or touching
  another tree's objects: that makes make skip changed files and tests run stale code; ccache makes
  the first build fast anyway), commit on their branch, never push, never edit PLAN.md / CLAUDE.md, and end with a
  report: what changed, PORT NOTEs removed/left, shared core files touched, save-version change, tests.
- Packages that edit the same core files (game.h, combat.cpp, run.cpp) can run in parallel, but the
  lead merges them one by one: preview with `git merge-tree --write-tree --name-only main <branch>`,
  merge small conflicts itself (Makefile.sdl test lists: keep the union of TEST_SRC, rules and
  `check` lines), and hand bigger ones back to the agent to rebase onto main. Batch merges that
  each change game.h and run one release `check` + `SIM_ALLCARDS=1 ./build/sim 200` +
  `./build/sim 60` vs `SIM_SAVELOAD=3 ./build/sim 60` for the batch before pushing.
- UI changes: the agent leaves headless screenshots; the lead shows them to the owner and merges
  only after the owner's OK.
- After a merge, remove the agent's worktree (`git worktree remove`); keep the branch.

Soak on macOS 27: ASan hangs at start-up, so it falls back to UBSan + libmalloc scribble, `leaks --atExit` and Guard Malloc (logs in build/soak/).

`make cia` needs `bannertool` + `makerom` on PATH (Mac: `~/.local/bin`; makerom/ctrtool from
3DSGuy/Project_CTR releases, bannertool built from diasurgical/bannertool). Banner art/sound come
from `python3 tools/build_assets.py --packaging` (run automatically when missing). Test: Azahar → File → Install CIA.

Mac 3DS build in one step: `bash tools/build_3ds_mac.sh` (`--assets` also reruns build_assets first):
runs make in the repo and copies the 3dsx to the Desktop (hash checked). After a `git pull` the
Desktop file is stale until this runs.

At the end of every conversation with the owner, push all ready project
changes (do not create an empty commit when there are none), then rebuild the
pushed revision (`bash tools/build_3ds_mac.sh`, which copies `sts2-3ds.3dsx` to
`~/Desktop/sts2-3ds.3dsx`). Verify the
desktop copy matches the rebuilt file and report the push/build result. Keep
that derived binary out of Git.

Real 3DS: the owner's preferred loop is `make link` with the 3DS in Homebrew
Launcher → Y (NetLoader). hbmenu writes the file to `sdmc:/3ds/` and runs it,
so it stays installed. The 3DS SD card is also reachable over SMB1 as
`\\3DS-5341\microSD` while System Settings → microSD Management is open
(slow, ~180 KB/s).

Preview automation (env vars): `STS_HIDDEN=1 STS_FIXED_STEP=1 STS_SEED=42`,
`STS_SCRIPT="40:A,100:A,300:T30x190,420:P200x200,425:M200x160,440:U,500:X"`
(button / tap T / press-hold P / move M / release U at a frame),
`STS_SHOTS="435:build/a.bmp,..."`, `STS_ALLCARDS=1`, `STS_AUTOPLAY=1`,
`STS_ENCOUNTER=<EncounterId>` (first fight is that encounter; also works for
`build/sim`). From title: 40:A opens character select and 100:A (or START) starts the run; with STS_ENCOUNTER / STS_ROOM, `40:A,100:A,160:A,220:A` reaches the first room (Neow otherwise runs first).
Headless checks: `SIM_FIGHTS=1 ./build/sim N` prints each fight; add a second
argument for a verbose event log.

3DS rendering: never rely on citro2d tint `blend` (Azahar drops it: the grey HP-bar art showed
silver). Bake pre-coloured sprites in build_assets (e.g. `ui/hp_fill_<RRGGBB>`) or use blend 0, and
check 3DS-visible changes in Azahar before handing a build over. UI work in progress and the audit
backlog: docs/UI_AUDIT.md.
Animation/detail comparison against the original (drive + record the Steam game, STS_RECORD in the
preview, film strips): tools/ref/ and docs/ANIM_DIFF.md (findings and status).

On 3DS/Azahar the same keys go in `sdmc:/sts2-debug.txt` (KEY=VALUE lines);
shots land in `sdmc:/sts2-shots/<name>.bmp`. Emulator timing is real-time, so
scripts drift; delete the debug file afterwards. Azahar ignores SIGTERM, so `timeout` does not
stop it: run it in the background and `pkill -9 -f Azahar.app/Contents/MacOS/azahar` once the shots exist.

## Architecture

- `source/core/` rules engine mirroring StS2 (coroutines `Task<>` for C#
  async, `Dec` fixed point, xoshiro256** RNG with the game's stream seeds,
  hooks on `Model`). `docs/PORTING.md` is the C#→C++ cheat sheet.
- `source/spine/` minimal Spine 4.2 runtime (no physics/clipping).
- `source/gfx/gfx.h` platform API (+ transform stack in `gfx_common.cpp`);
  `platform_3ds/` citro2d + citro3d mesh shader, `platform_sdl/` preview.
- `source/ui/` all screens. Combat bottom screen follows the RGDSplus
  dual-screen design (github.com/LPF970915/Slay-the-Spire-for-RGDSplus,
  docs/R4_DRAG_LOCK): fanned hand (HandPosHelper tables), tap = enlarge,
  drag above the play line (top of the fan) = arm + lock nearest enemy in
  virtual two-screen space, sideways 20 px = switch target, back into the
  hand / screen edge / B = cancel, cross-screen bezier arrow, card flight.
  Layout measured from a photo of the RGDSplus port (the owner's reference):
  creatures at native scale, bottom = room bg, no status strip, hand centred
  with card text, energy left / "结束" right below it, piles in the corners.
  Hand animation: draw from pile with stagger, ease to slots, discard ghosts.
- Content files: `content.cpp` (starter/common cards, first act 1 monsters,
  registry), `content_act1.cpp` (remaining normals + Bygone Effigy),
  `content_phrog.cpp` (Phrog Parasite split into Wrigglers),
  `content_bosses.cpp` (Ceremonial Beast, The Kin, Fogmog + illusions).
  Engine supports mid-combat spawns (`cmd::addMonster`), stuns
  (`Monster::stun`), minions / secondary enemies, creatures that die without
  leaving (illusions revive), turn-end-in-hand cards, `shouldPlay` and
  `tryModifyPowerAmountReceived` (Artifact) hooks.
- Powers: one class per power id. `Creature::get<P>()` finds powers by id and static_casts, so a second class
  with the same id is undefined behaviour; shared powers (Vigor, *NextTurn) live in `powers.h`. The 3DS Makefile
  flattens object names, so two .cpp files with the same basename anywhere under source/ break the 3DS link.
- Relics: `Relic` + `RELIC_HEADER(Name, "KEY", Rarity)` with DynVars named like
  the C# vars (the UI formats descriptions from them). `relics.cpp` registers
  `relics_common/uncommon/rare.cpp`; only registered relics enter the grab bags
  (`Run::populateRelicBags`, rarity roll 50/33/17, fallback Circlet). Elites
  drop a relic, the treasure row (map row 8) gives 42-52 gold + a relic from the
  shared bag. Skipped relics are listed at the end of each file with the missing
  feature (potions, shop, card-reward hooks, death prevention, ...).
- Acts: `acts.cpp` lists Overgrowth / Hive / Glory encounters and events as in the C#
  (content files only register them; unregistered ids are skipped, and empty elite /
  boss pools fall back to the act's normal fights). `Run::enterAct` builds each act's
  map (`generateStandardActMap(rng, act)`: 15 / 14 / 13 rooms), queues and events; a boss
  gives 100 gold + a card and leads to the next act (full heal until Ancients exist);
  act 3's boss leads to TheArchitect (`content_architect.cpp`, Ancient layout, `gfx/bg_thearchitect.t3t`),
  whose PROCEED is `Run::winRun` -> Victory; `STS_ROOM=Event STS_EVENT=TheArchitect` jumps there. Art per act: `gfx/bg_<act>.t3t`, `gfx/bg_map_<act>.t3t`.
  Underdocks is the alternative act 1: `Run::start` rolls `Run::actIds` (RunState.Acts) with
  `db::randomActList` (ActModel.GetRandomList, `Rng(seed, "act_selection")`); `Run::act()` reads it,
  `STS_ACT1=Underdocks|Overgrowth` forces act 1, `Run::actMusic()` is the audio lane's music hook.
- Ancients: `ancients.cpp` (Neow, its 19 relics and cards). Each map has a start node
  (`nodes[0]`, row -1, `RoomType::Ancient`) whose event runs before the first room
  (`Run::enterAncient`: full heal, then the event). `Event::ancient` switches the UI to the
  Ancient layout: `gfx/bg_<id>.t3t` scene + `ancients.*` dialogue on top, relic options
  below. `STS_NO_NEOW=1` (or any STS_ENCOUNTER/ROOM/EVENT) skips Neow. Acts 2/3 roll their
  Ancient in `Run::enterAct` (`ancients_later.cpp`: Orobas/Pael/Tezcatara, Nonupeipe/Tanx/Vakuu,
  shared Darv); `STS_ANCIENT=Tanx` forces one (also in act 1). Room art `gfx/bg_<id>.t3t`
  (`bake_ancient` in build_assets), map icons `map/ancient_<id>`. Dialogue: `db::ancientDialogueFor`
  (`ancient_dialogues.cpp`, tables generated by `tools/gen_ancient_dialogues.py`) picks by the profile's
  visits (`progress::ancientVisits`, recorded at run end); `STS_ANCIENT_VISITS=N` fakes N visits.
- Font: one glyph page per size (`font/font_0.t3t` 12 px, `font_1.t3t` 16 px = layout metrics; `font_2..5` 7-10 px,
  drawn 1:1 when a scaled style lands near them, `Res::renderFont`). Spine pages are repacked per region at
  `SPINE_TEXELS_PER_PIXEL`; event art is `gfx/event_<KEY>.t3t` (400x240 full-bleed, freed off the Event screen).
- Languages (Y3): 简体中文 = `loc.txt` + `font/font_*`, English = `loc_eng.txt` + `font/eng_*` (Kreon, CJK
  fallback glyphs only for literals not yet in tr()); only the active language's pages are loaded.
  `Res::setLanguage` switches (settings 语言选项, main menu only); `STS_LANG=en|zh` for previews.
  Port-only UI text goes through `tr("中文", "English")` (res.h), never a `static` array of literals.
  `build_assets.py --text-only` rebuilds just the loc tables and fonts.
- Potions: `potions.cpp` (POTION_HEADER like relics; registry `db::potion`, pool order
  `db::potionPool()`). Belt = `Run::potions` (3 slots, null = empty); `Run::usePotion`,
  `procurePotion`, `rollPotionReward` (after every fight), `offerPotion` (Screen::PotionOffer).
  In combat a potion is a `PlayerAction::UsePotion`. UI: 药水 on the combat bottom screen and
  the map HUD. Debug: `STS_POTIONS=FirePotion,BlockPotion` fills the belt,
  `STS_POTION_REWARD=1` makes every fight drop one; `SIM_ALLPOTIONS=1 ./build/sim` cycles
  every potion through the fights.
- More relics: `relics_more.cpp` (package 10). Relic hooks for rewards: `extraCombatGold`,
  `extraCardRewards`, `modifyCardReward`, `upgradesNewCard`, `afterCardAddedToDeck` (all deck
  additions go through `Run::addCardToDeck`); `Run::combatRewards` is the whole post-fight
  flow; `Run::restSite` offers `restOptions` (heal, smith, Lift, Dig). Debug:
  `STS_RELICS=Girya,Shovel` adds relics at the start; `SIM_ALLRELICS=1` (or a comma list)
  runs the sim with them.
- Saves: `save.cpp` (`Run::save` / `Run::load`, token stream via `Archive`). Saved only at the
  map choice (`Run::onSavePoint`, after side tasks from `Run::spawnSide` finish). A relic whose
  state lasts between rooms overrides `persist(Archive&)`; new ones must too. Files (root PC
  `saves/`, 3DS `sdmc:/3ds/sts2-3ds/`): `profile<N>/run.sav` + `profile<N>/progress.sav` per profile
  (`profiles.h`, 3 slots, current slot and names in `profile.sav`), `settings.sav` global. An old
  top-level run.sav / progress.sav moves into profile 1 on first launch. Run history (M2, `history.h`):
  the last 50 finished runs in `profile<N>/history/00..49.run` (ring), written once per run end by
  `recordRunEnd` in run.cpp; `history::load(profile)` for screens, `SIM_HISTORY_ROOT=<scratch>` in the sim.
  STS_HIDDEN / STS_NO_SAVE turn all of them off.
  S22: every "are you sure" and save error goes through the shared modal `ui/confirm.h` (`confirm::ask`/`notice`); a garbled file is moved to `<name>.corrupt` and reported (`core/save_errors.h`); `STS_FAKE_SAVE_ERROR=run|progress|settings|write` forces each dialog in previews.
  Y5: every save file goes through `core/safe_file.h` (`writeAtomic`: `.tmp` -> fsync -> rename, or `.bak` swap where rename
  can't replace, as on the 3DS SD; `recover` / `saveerr::loadChecked` bring back an interrupted write's `.tmp`/`.bak`;
  deleting a save = `safefile::removeAll`). The startup SD check (`probeWritable`) failing = one notice, no saves that session
  (`STS_FAKE_SAVE_ERROR=sd`). 3DS HOME menu / sleep: APT hooks in `gfx_3ds.cpp` -> `gfx::onSystemPause` -> `audio::setPaused`;
  suspended time never reaches `gfx::dt()`; HOME -> Close just ends the loop (no save written).
  `SIM_SAVELOAD=K ./build/sim N` must print the same results as without it.
- Characters: `Character` table in `characters.inc` (generated by `tools/gen_characters.py`), `Run::start(seed,
  characterId)`, `run.character()`. Pools go through `db::characterCards(run.characterId, ...)`, never the
  `ironclad*` shortcuts. `STS_CHAR=Silent` / `SIM_CHAR=Silent` pick one;
  `Run::kRandomCharacter` is resolved from the seed (`Run::resolveCharacter`). RNG draws over characters use
  `db::allCharacters()` (C# order), not `characterIds()`. Saves are version 6 (3 character id, 4 ascension, 5 act list, 6 history path).
- Ascension: `Run::ascension` / `hasAscension(kToughEnemies)`, monsters use `asc(kToughEnemies, a, b)` for
  every C# `GetValueIfAscension`; `tools/ascension_sweep.py` + `ascension_check.py` (in `make check`) keep the
  numbers honest. `STS_ASCENSION=10`, `SIM_ASC=10`. Details in docs/PLAN.md (C10 notes).
- Custom runs (M11+S05): `modifiers.h/.cpp` (the 16 modifiers as Models in `Run::modifiers`, run.sav v8, history v3, SeedHelper), `screens/custom_run.cpp`; debug `STS_MODIFIERS=Draft,Midas,CharacterCards:Silent` / `SIM_MODIFIERS=...`, `STS_OPEN_CUSTOM=1` opens the screen.
- Daily run (M12): `daily.h/.cpp` (date -> character/ascension/seed/modifiers as NDailyRunScreen, local best in progress.sav v2, run.sav v9, history v4), `screens/daily_run.cpp`; `STS_DAILY_DATE=YYYY-MM-DD` fakes today, `STS_OPEN_DAILY=1` opens the screen, `SIM_DAILY=YYYY-MM-DD` in the sim.
- Tutorials (M13): `ui/tutorials.h/.cpp` (queue, seen flags = settings::tutorialSeen, the C# ftue ids) + `ui/tutorials_ui.cpp` (popup, screen watcher), one-line hooks `showTip(Tip::X)` / `tip*()`; never shown under STS_HIDDEN / STS_SCRIPT / STS_SHOTS / STS_AUTOPLAY unless `STS_TIPS=1` (`=2` also asks 要看教程吗？ on 出发, `=0` off).
- Achievements (M5): `achievements.h/.cpp` (22, checks in `Combat::listeners` / after a won fight / `recordRunEnd`, progress.sav v3, locked in custom/daily), `screens/achievements_ui.cpp` (toast + list page, 成就 / Y on the 统计 page); debug `STS_ACHIEVE_TOAST=DefeatOneBoss` toasts, `STS_ACHIEVEMENTS=IroncladWin,..` unlocks in memory (with STS_HIDDEN), `STS_OPEN_ACHIEVEMENTS=1` opens the list, `STS_OPEN_HISTORY=1` the run history (needs saved runs: `SIM_HISTORY_ROOT=saves ./build/sim 4`, without STS_HIDDEN).
- Silent systems: `char_silent.h/.cpp` (Poison, Accuracy, Fan of Knives, Shiv), `cmd::discardCards` (Sly, hooks),
  single-turn Retain / Sly flags on `Card`. Details in docs/PLAN.md (X1.0 notes).
- Enchantments: `enchantments.cpp` (`Enchantment` Model in `Card::enchantment`, `ENCHANTMENT_HEADER`,
  `cmd::enchant`, `Run::selectForEnchantment`; hooks in `combat.cpp`, saved by `save.cpp`). Debug:
  `STS_ENCHANT=Sharp:3,Glam`, `SIM_ENCHANT=1`; `make -f Makefile.sdl check` runs `test/enchant_test.cpp`.
  Every card `clone()` must call `adoptEnchantment()`. Details in docs/PLAN.md (A3a notes).
- Merchant: `shop.cpp` (`Run::enterShop`, `Run::shop` items, `shopChoice`; prices through the
  `modifyMerchantPrice` hook). Art `gfx/bg_merchant.t3t`. `STS_ROOM=Shop` makes the first room a shop.
- Debug: `STS_ROOM=Treasure|Rest|Elite|Boss|Event` makes the first room that type,
  `STS_ACT=2|3` starts in that act; the dev menu has 跳到下一幕.
- Development build: `Run::freeMap` (default on; `STS_PATH_ONLY=1` off) lets any
  map node be entered. SELECT (Backspace in the preview) or 开发 on the map opens
  the developer menu: god mode, heal, gold, max HP, obtain any relic, add any
  card, upgrade deck, pick the next encounter, kill all enemies, free map toggle.
- Map: `mapgen.cpp` is the game's StandardActMap (paths without crossings, pruning,
  centring/spreading/straightening, room types incl. ? and shops). Drawn with the
  game's own parchment and NMapScreen geometry (columns 150, rows 155 units, ±21/±25
  jitter, tilt) scaled by `kMapS` = 0.17 for the RGDSplus look; icons ~9 px, dotted
  paths every 22 units, legend on the right. "?" rooms roll UnknownMapPointOdds;
  events and shops show a "not ported yet" page. From any other room the pause menu's 地图
  opens it for a look, with a red 返回 in the bottom-left corner.
- Pause menu (Y2, `screens/pause.cpp`): START during a run (or 暂停 top-right on the map HUD):
  继续, 地图, 牌组, 设置, 百科大全 (card library, M8), 放弃 (confirm, `Run::abandon`), 保存并退出
  (keeps run.sav: 继续 resumes at the last map save point). It freezes the scheduler and the run
  timer; map / deck / settings open over it and return to it. Scripts: `START` then `UP`/`DOWN`/`A`.
- `Res` frees monster Spine pages when a new fight starts (only IRONCLAD stays);
  3DS textures live in limited linear memory.

## Status / TODO

See `docs/PLAN.md` (Done table, inventory, tracks and order).
