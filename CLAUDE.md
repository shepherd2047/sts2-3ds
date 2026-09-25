# StS2 → Nintendo 3DS port (personal project)

Slay the Spire 2 re-implemented in C++20 for the New 3DS, translated from the
game's decompiled C#. **Personal use only**: never commit or distribute anything
derived from the game (`romfs/`, `icon.png`, `.3dsx`, screenshots, decompiled
code). `.gitignore` is a whitelist; keep it that way.

The owner works on this from a Mac and a Windows PC. Talk to them in Chinese.

## Working agreement

- Delegate simple, mechanical work (bulk card/monster translation from C#,
  greps, file shuffling, test loops) to subagents on cheaper models
  (haiku / sonnet); keep design, debugging and verification yourself, and
  review their output.
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
  path, so 3DS builds are done from an ASCII-path copy (rsync source over).
- Desktop preview: SDL2 + clang/g++. `Makefile.sdl` uses `sdl2-config`
  (macOS: `brew install sdl2`; Windows: MSYS2 UCRT64 with
  `mingw-w64-ucrt-x86_64-{gcc,SDL2,pkgconf}` + `make`, built from the UCRT64
  shell; `Makefile.sdl` strips SDL's `main` wrapper there).
- Windows gotcha: Python writes text as CRLF unless told otherwise; romfs text
  files must be LF (the Spine atlas parser keeps `\r` in texture paths), so
  pass `newline='\n'` to any new `open(..., 'w')` in `tools/`.
- Emulator: Azahar. Its SD card: macOS `~/Library/Application Support/Azahar/sdmc`,
  Windows `%APPDATA%\Azahar\sdmc`.

## Build & test

```bash
make -f Makefile.sdl            # build/sts2-preview + build/sim
./build/sim 200                 # headless fights; SIM_ALLCARDS=1 plays every card
make                            # sts2-3ds.3dsx (devkitPro env)
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
`STS_SHOTS="435:build/a.bmp,..."`, `STS_ALLCARDS=1`, `STS_AUTOPLAY=1`.
From title: 40:A, 100:A reaches the map, 300:T30x190 enters the first fight.

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
  Status strip on top (block, HP bar with damage preview, total intent).
  Hand animation: draw from pile with stagger, ease to slots, discard ghosts.

## Status / TODO

Done: Ironclad full card pool, Act 1 fights (weak/normal/elite/boss), map,
card rewards (rarity odds), rest sites, Spine creature animation, touch UI.

Not done: events, shop, treasure, potions, Phrog elite, saves, other
characters/acts, audio, RGDSplus-style two-screen map with drag/tap,
real-hardware performance test.
