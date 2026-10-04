// ANIM_DIFF C2 / C10 / C13: cards flying to a pile as fire comets (NCardFlyVfx, NCardFlyShuffleVfx) with the
// character's card trail (NCardTrailVfx + card_trail_<character>.tscn): two additive textured ribbons, the
// orange card-silhouette head, embers left behind and falling sparks. Pure presentation in virtual two-screen
// coordinates (top screen 0..400 x 0..240, bottom screen below it, see ui_common.h); fixed arrays, no
// allocation per frame. The card body itself is drawn by the combat screen from bodies().
#pragma once
#include <cstdint>

namespace sts {
struct Card;
}

namespace ui::cardfly {

constexpr int kMaxComets = 32;

// A card riding a comet: centre (x, y), drawCard scale s, rotation (radians), `dark` 0..1 (Body.Modulate white ->
// black), `alpha` (the trail fades the card to 0.75).
struct Body {
  sts::Card* card;
  float x, y, s, rot, dark, alpha;
  bool waiting;  // still in its launch delay (C10: the hand flashes cyan before the cards fly)
};

// Starts a comet at (x, y) towards (ex, ey). `card` may be null (shuffle: trail only). `s` / `angle` are the card's
// current drawCard scale and rotation. `shuffle` picks NCardFlyShuffleVfx's arc offsets and fade.
void launch(sts::Card* card, float x, float y, float s, float angle, float ex, float ey, float delay = 0,
            bool shuffle = false);
void clear();
void update(float dt);          // visual seconds
void drawTrails(bool top);      // ribbons, heads and particles of every comet on one screen
int bodies(Body* out, int max); // cards to draw over the trails (after drawTrails)
bool flying(const sts::Card* c);
// Body.Modulate towards black: the card silhouette in black over a w x h card centred on (x, y) (screen-local).
void shade(float x, float y, float w, float h, float rot, float a);
int live();

}  // namespace ui::cardfly
