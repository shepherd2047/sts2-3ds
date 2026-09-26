# StS2 → Nintendo 3DS port (personal project)

Slay the Spire 2 re-implemented in C++20 for the New 3DS, translated from the
game's decompiled C#. **Personal use only**: never commit or distribute anything
derived from the game (`romfs/`, `icon.png`, `.3dsx`, screenshots, decompiled
code). `.gitignore` is a whitelist; keep it that way.

The owner works on this from a Mac and a Windows PC.

## What to work on

`docs/PLAN.md` has the goal (a finished port as complete as the RGDSplus one),
the session protocol and self-contained work packages with their status. Start
there: pick the first `todo` package whose dependencies are done. Read only the
files a package names — don't survey the whole codebase or decompiled tree.
The RGDSplus reference repo is cloned next to this one as `../rgds-ref`
(`gh repo clone LPF970915/Slay-the-Spire-for-RGDSplus ../rgds-ref -- --depth 1`).

## Working agreement

- Usage quota is shared by all the owner's sessions and subagents. Bulk,
  mechanical translation is cheapest in a Sonnet session or a Sonnet subagent
  with a tight brief — but say how many subagents and why, and get a yes,
  before starting any. Keep design, debugging and review in the main session.
- Install every dev tool globally (apps in /Applications or Program Files,
  CLIs via Homebrew / winget / dkp-pacman / `dotnet tool install -g`), never
  in a session scratchpad, temp dir or the project. Work copies that must
  survive go in stable paths too (Mac 3DS build copy: `~/dev/sts2-3ds-build`).
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
  (and is unreliable with non-ASCII); on the Mac the repo lives under a Chinese
  path, so 3DS builds are done from an ASCII-path copy (rsync source over to
  `~/dev/sts2-3ds-build`, excluding build/ and .git).
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
make -f Makefile.sdl            # build/sts2-preview + build/sim
./build/sim 200                 # headless fights; SIM_ALLCARDS=1 plays every card
make                            # sts2-3ds.3dsx (devkitPro env); packs romfs_3ds/
python tools/compress_romfs.py  # romfs/ -> romfs_3ds/ (GPU texture formats), after build_assets
make link                       # build + send to the 3DS over Wi-Fi (IP=... if needed)
```

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
`build/sim`). From title: 40:A reaches the map, 100:A enters the first room.
Headless checks: `SIM_FIGHTS=1 ./build/sim N` prints each fight; add a second
argument for a verbose event log.

On 3DS/Azahar the same keys go in `sdmc:/sts2-debug.txt` (KEY=VALUE lines);
shots land in `sdmc:/sts2-shots/<name>.bmp`. Emulator timing is real-time, so
scripts drift; delete the debug file afterwards.

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
  act 3's boss is Victory. Art per act: `gfx/bg_<act>.t3t`, `gfx/bg_map_<act>.t3t`.
- Ancients: `ancients.cpp` (Neow, its 19 relics and cards). Each map has a start node
  (`nodes[0]`, row -1, `RoomType::Ancient`) whose event runs before the first room
  (`Run::enterAncient`: full heal, then the event). `Event::ancient` switches the UI to the
  Ancient layout: `gfx/bg_<id>.t3t` scene + `ancients.*` dialogue on top, relic options
  below. `STS_NO_NEOW=1` (or any STS_ENCOUNTER/ROOM/EVENT) skips Neow.
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
  events and shops show a "not ported yet" page. START opens it for a look from any
  room, with a red 返回 in the bottom-left corner.
- `Res` frees monster Spine pages when a new fight starts (only IRONCLAD stays);
  3DS textures live in limited linear memory.

## Status / TODO

Done: Ironclad full card pool, all 22 Act 1 encounters (4 weak, 12 normal,
3 elites, 3 bosses), continuous two-screen map with drag/tap, card rewards
(rarity odds), rest sites, Spine creature animation, touch UI, relic system
with ~70 relics (common/uncommon/rare + Ironclad), elite relic rewards,
treasure rooms, relic page.

Not done: events, shop (and Shop-rarity relics), potions,
StS2's real map generator (unknown/treasure/shop rooms), saves, other
characters/acts, audio, real-hardware performance test. romfs is ~66 MB
(Spine pages are uncompressed RGBA8).
