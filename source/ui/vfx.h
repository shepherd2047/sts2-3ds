// F7: light combat VFX. A tiny pooled particle system (at most kMaxParticles live, fixed arrays,
// no allocation per frame) drawing the game's own small VFX textures (tools/build_assets.py
// add_vfx_art: vfx/<name>, all on one atlas page) as batched quads, additive where the C# uses
// additive sparks. Pure presentation: positions are top-screen pixels, nothing reads the rules.
// The combat screen triggers the effects from its VisualEvent hook (App::vfxEvent) and from a few
// UI-side diffs (orbs evoked, stars gained, Osty summoned; App::drawVfx, combat_scene.cpp).
#pragma once
#include <cstdint>

namespace ui::vfx {

constexpr int kMaxParticles = 64;

void clear();            // a new fight / leaving combat
void update(float dt);   // visual seconds (fast mode already folded in by the caller)
void draw();             // top screen, over the creatures and under the damage numbers
int live();              // particles alive (tests / debug)

// Spawners. (x, y) is the creature's body centre; `h` its body height where the spread matters.
void hit(float x, float y, int amount, uint32_t rgb);  // NHitSparkVfx + slash streak
void blocked(float x, float y);                         // NBlockSparkVfx (attack fully blocked)
void blockBroken(float x, float y);                     // NBlockBrokenVfx
void blockGain(float x, float y);                       // shield flash when block is gained
void poisonTick(float x, float y, float h);             // NPoisonImpactVfx bubbles
void burnTick(float x, float y, float h);               // Burn at the end of the turn
void heal(float x, float y, float h);                   // green sparkles rising
void powerArrows(float x, float y, float h, bool buff);  // NPowerAppliedBuff/DebuffVfx arrows
void orbEvoke(float x, float y, uint32_t rgb);          // Defect orb evoke burst
void stars(float x, float y, int gained);               // Regent stars gained
void soul(float x, float y, float h, uint32_t rgb);     // Necrobinder Osty summon / death wisps
void puff(float x, float y, uint32_t rgb);              // any other hit (self damage, orbs, thorns)

}  // namespace ui::vfx
