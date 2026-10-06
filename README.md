# Slay the Spire 2 · 3DS minimal playable build (personal port)

For personal use only: the assets and text are extracted on your own machine from your own legitimate copy of the game. Do not distribute `romfs/`, `icon.png` or a built `.3dsx`.

## Current progress

- The Ironclad, the three-act map, and the combats and events ported so far; cards, relics, potions, the shop, Ancients and map nodes are all wired in.
- Combat rules are ported from the decompiled C#; a headless simulator checks the flow, the RNG and save/load round trips.
- The game autosaves at each map choice and can be continued from the title screen; the desktop preview shows both screens stacked top and bottom.
- Each act still has unported options and approximations. Screen-by-screen acceptance of the dual-screen UI, the other characters, settings, sound effects and performance checks on real hardware are not finished yet. See the [development plan](docs/PLAN.md) for the exact scope and status.

## Layout

```
source/core/        game logic (C++20 coroutines, mirroring the original's async/await)
source/ui/          screens, text layout, card description formatting
source/gfx/gfx.h    platform interface
source/platform_3ds 3DS backend (citro2d)
source/platform_sdl desktop preview backend (SDL2, the two screens stacked, mouse as stylus)
tools/              asset extraction: PCK unpacking, offline Spine skeleton rendering, font and atlas generation
test/sim.cpp        headless automated fights, for catching crashes and rule bugs
```

## Building

1. Extract the assets (needs Python 3 + Pillow + numpy; the game is found automatically in your Steam library on Mac/Windows/Linux, or set `STS2_DIR`):

   ```bash
   python3 tools/build_assets.py
   ```

2. Desktop preview (needs SDL2):

   ```bash
   make -f Makefile.sdl && ./build/sts2-preview
   ```

   Keys: Z=A, X=B, S=X, A=Y, Q/W=L/R, arrow keys=D-pad, mouse=touch.

3. 3DS (needs devkitPro's `3ds-dev`):

   ```bash
   make
   ```

   Copy `sts2-3ds.3dsx` to `/3ds/` on the SD card and start it from the Homebrew Launcher. The assets live in romfs and are packed into the `.3dsx`.

## 3DS controls

- Touch: tap a card in your hand to enlarge it; hold and drag it up past the hand area to play it (the nearest enemy is locked on automatically, swipe left/right to switch targets); drag it back into the hand or press B to cancel
- ←→ select a card / target, A confirm, B cancel, L/R cycle through the hand, X end turn, Y view your deck
- START pause menu (Continue, Map, Deck, Settings, Abandon, Save & Quit)
- START + SELECT quit
