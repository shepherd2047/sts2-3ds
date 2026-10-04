// Fire comets for cards flying to a pile (see card_fly.h). Numbers from NCardFlyVfx, NCardFlyShuffleVfx,
// NCardTrailVfx, NCardTrail and card_trail_ironclad.tscn, in 1080p units scaled by kU.
#include "card_fly.h"

#include "ui_common.h"

namespace ui::cardfly {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kU = 0.24f;            // 1080p units -> pixels for sizes (the 3DS screens are 240 px high)
constexpr float kArc = 480.f / 1080;   // arc heights: the comet's path spans both screens (~480 px of height)
// The ribbons are drawn wider than kU alone gives: on the small screens the comets cross the whole canvas, and at
// the plain scale the trail read as a thin line next to the original's (sheet comparison).
constexpr float kRibbonBoost = 1.4f;
constexpr float kPointLife = 0.8f;     // NCardTrail._pointDuration
constexpr float kMinDist = 12 * kU, kMaxDist = 48 * kU;
constexpr int kMaxPts = 48;
constexpr int kEmbers = 24, kSparks = 12;

struct Pt { float x, y, age; };
struct Part { float x, y, vx, vy, t, life, size, rot, vrot; bool live; };

struct Comet {
  bool active = false, shuffle = false;
  sts::Card* card = nullptr;
  float delay = 0, age = 0;
  float sx = 0, sy = 0, ex = 0, ey = 0, cx = 0, cy = 0;
  float time = 0, speed = 1, accel = 2, dur = 1;
  int phase = 0;              // 0 flying, 1 arrived (body shrinking away)
  bool bodyDone = false;
  float x = 0, y = 0, rot = 0, s0 = 1, bodyS = 1, dark = 0;
  bool fading = false;
  float fadeT = 0;            // NCardTrailVfx.FadeOut: alpha -> 0 in 0.5 s
  Pt pts[kMaxPts];
  int npts = 0;
  bool hasLast = false;
  float lastX = 0, lastY = 0;
  Part embers[kEmbers], sparks[kSparks];
  int nextEmber = 0, nextSpark = 0;
  float emberAcc = 0, sparkAcc = 0;
};

Comet comets[kMaxComets];
uint32_t seed = 0x6C8E9CF5u;  // own LCG: previews stay deterministic, the rules' RNG is untouched

float rnd() {
  seed = seed * 1664525u + 1013904223u;
  return (float)(seed >> 8) * (1.f / 16777216.f);
}
float rr(float a, float b) { return a + (b - a) * rnd(); }
float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }
float easeOutCubic(float t) { t = 1 - clamp01(t); return 1 - t * t * t; }
float easeInCubic(float t) { t = clamp01(t); return t * t * t; }

void bezier(const Comet& c, float t, float& x, float& y) {  // MathHelper.BezierCurve (quadratic)
  const float u = 1 - t;
  x = u * u * c.sx + 2 * u * t * c.cx + t * t * c.ex;
  y = u * u * c.sy + 2 * u * t * c.cy + t * t * c.ey;
}

// Piecewise-linear Godot Curve / Gradient samples.
struct Key { float at, v; };
template <int N>
float sample(const Key (&k)[N], float f) {
  if (f <= k[0].at) return k[0].v;
  for (int i = 1; i < N; ++i)
    if (f <= k[i].at) return lerp(k[i - 1].v, k[i].v, (f - k[i - 1].at) / (k[i].at - k[i - 1].at));
  return k[N - 1].v;
}
// OuterTrail / InnerTrail width curves and the (grey-ramp) gradients: brightness b, alpha a.
constexpr Key kOuterW[] = {{0, 0.111f}, {0.84f, 0.664f}, {0.922f, 0.943f}, {1, 0.686f}};
constexpr Key kInnerW[] = {{0, 0}, {0.801f, 0.571f}, {0.901f, 0.367f}, {0.938f, 0.931f}, {1, 0.742f}};
constexpr Key kOuterB[] = {{0, 0}, {0.141f, 0.25f}, {0.522f, 1}};
constexpr Key kOuterA[] = {{0, 0}, {0.141f, 0.306f}, {0.522f, 1}};
constexpr Key kInnerB[] = {{0.261f, 0}, {0.647f, 0.25f}, {0.822f, 1}};
constexpr Key kInnerA[] = {{0.261f, 0}, {0.647f, 0.306f}, {0.822f, 1}};
// BigSparks colour ramp (r, g, b, a over the life).
constexpr Key kEmR[] = {{0, 1}, {0.554f, 0.811f}, {0.806f, 0.595f}, {1, 0.639f}};
constexpr Key kEmG[] = {{0, 0.259f}, {0.554f, 0.185f}, {0.806f, 0}, {1, 0}};
constexpr Key kEmB[] = {{0, 0.2f}, {0.554f, 0}, {1, 0}};
constexpr Key kEmA[] = {{0, 1}, {0.554f, 0.447f}, {0.806f, 0.482f}, {1, 0}};

uint32_t rgba(float r, float g, float b, float a) {
  auto c = [](float v) { return (uint32_t)(clamp01(v) * 255.f + 0.5f); };
  return c(r) << 24 | c(g) << 16 | c(b) << 8 | c(a);
}

void addPoint(Comet& c, float x, float y, float age) {
  if (c.npts == kMaxPts) {
    for (int i = 1; i < kMaxPts; ++i) c.pts[i - 1] = c.pts[i];
    --c.npts;
  }
  c.pts[c.npts++] = {x, y, age};
}

// NCardTrail.CreatePoint: a point every 12+ units, long jumps subdivided every 48.
void follow(Comet& c) {
  if (c.hasLast) {
    const float dx = c.x - c.lastX, dy = c.y - c.lastY, d = std::sqrt(dx * dx + dy * dy);
    if (d < kMinDist) return;
    if (c.npts > 2 && d > kMaxDist)
      for (float s = kMaxDist; s < d - kMinDist; s += kMaxDist) addPoint(c, c.lastX + dx * s / d, c.lastY + dy * s / d, 0);
  }
  addPoint(c, c.x, c.y, 0);
  c.hasLast = true;
  c.lastX = c.x;
  c.lastY = c.y;
}

void emit(Comet& c, float dt) {
  if (c.fading || c.delay > 0) return;
  // BigSparks: 64 over a 2 s life (32 / s), rect +-24, slow drift towards (5, -5), world space (left behind).
  c.emberAcc += dt * 26.f;
  while (c.emberAcc >= 1) {
    c.emberAcc -= 1;
    Part& p = c.embers[c.nextEmber];
    c.nextEmber = (c.nextEmber + 1) % kEmbers;
    const float a = -kPi / 4 + rr(-kPi / 4, kPi / 4), sp = rr(16, 32) * kU;
    p = {c.x + rr(-24, 24) * kU, c.y + rr(-24, 24) * kU, std::cos(a) * sp, std::sin(a) * sp, 0,
         std::max(0.3f, 2.f * (1 - rnd())), rr(0.2f, 0.8f), rr(-kPi, kPi), rr(-32, 32) * kPi / 180, true};
  }
  // LittleSparks: tiny bright sparks thrown up and falling (gravity 1960).
  c.sparkAcc += dt * 50.f;
  while (c.sparkAcc >= 1) {
    c.sparkAcc -= 1;
    Part& p = c.sparks[c.nextSpark];
    c.nextSpark = (c.nextSpark + 1) % kSparks;
    const float a = -kPi / 2 + rr(-kPi / 4, kPi / 4), sp = rr(0, 480) * kU;
    p = {c.x + rr(-24, 24) * kU, c.y + rr(-24, 24) * kU, std::cos(a) * sp, std::sin(a) * sp, 0,
         std::max(0.08f, 0.3f * (1 - rnd())), rr(0.02f, 0.05f), 0, 0, true};
  }
}

void stepParts(Comet& c, float dt) {
  for (Part& p : c.embers) {
    if (!p.live) continue;
    if ((p.t += dt) >= p.life) { p.live = false; continue; }
    const float k = 1.f / (1.f + 3.f * dt);  // damping 1-5
    p.vx *= k;
    p.vy *= k;
    p.x += p.vx * dt;
    p.y += p.vy * dt;
    p.rot += p.vrot * dt;
  }
  for (Part& p : c.sparks) {
    if (!p.live) continue;
    if ((p.t += dt) >= p.life) { p.live = false; continue; }
    p.vy += 1960 * kU * dt;
    p.x += p.vx * dt;
    p.y += p.vy * dt;
  }
}

float trailAlpha(const Comet& c) { return c.fading ? std::max(0.f, 1 - c.fadeT / 0.5f) : 1.f; }

// ---- drawing ----
struct Uv { gfx::Texture* tex = nullptr; float u0 = 0, v0 = 0, u1 = 0, v1 = 0; };
Uv uvOf(const char* name) {
  Uv u;
  Sprite sp = R().sprite(name);
  if (!sp.tex) return u;
  const float tw = (float)gfx::texWidth(sp.tex), th = (float)gfx::texHeight(sp.tex);
  u.tex = sp.tex;
  u.u0 = sp.x / tw;
  u.v0 = sp.y / th;
  u.u1 = (sp.x + sp.w) / tw;
  u.v1 = (sp.y + sp.h) / th;
  return u;
}

constexpr int kMaxVerts = 12000, kMaxIdx = 18000;
gfx::Vert verts[kMaxVerts];
uint16_t idx[kMaxIdx];
int nv = 0, ni = 0;
gfx::Texture* curTex = nullptr;
bool curAdd = true;

void flush() {
  if (ni && curTex) gfx::triangles(curTex, verts, nv, idx, ni, curAdd);
  nv = ni = 0;
}
bool room(gfx::Texture* t, bool additive, int v, int i) {
  if (!t) return false;
  if (t != curTex || additive != curAdd || nv + v > kMaxVerts || ni + i > kMaxIdx) {
    flush();
    curTex = t;
    curAdd = additive;
  }
  return true;
}

void quad(const Uv& u, bool additive, float x, float y, float w, float h, float rot, uint32_t col) {
  if ((col & 0xFF) == 0 || !room(u.tex, additive, 4, 6)) return;
  const float c = std::cos(rot), s = std::sin(rot), hw = w / 2, hh = h / 2;
  const float lx[4] = {-hw, hw, hw, -hw}, ly[4] = {-hh, -hh, hh, hh};
  const float us[4] = {u.u0, u.u1, u.u1, u.u0}, vs[4] = {u.v0, u.v0, u.v1, u.v1};
  const uint16_t b = (uint16_t)nv;
  for (int k = 0; k < 4; ++k) verts[nv++] = {x + lx[k] * c - ly[k] * s, y + lx[k] * s + ly[k] * c, us[k], vs[k], col};
  const uint16_t q[6] = {b, (uint16_t)(b + 1), (uint16_t)(b + 2), b, (uint16_t)(b + 2), (uint16_t)(b + 3)};
  for (uint16_t i : q) idx[ni++] = i;
}

// A Line2D ribbon (texture_mode STRETCH): u along the line (the strip textures are uniform along it, so the cell's
// middle column), v across it; width curve and gradient sampled along the points, tail -> head.
template <int A, int B, int C>
void ribbon(const Comet& c, const Uv& u, float width, float mr, float mg, float mb, float ma, const Key (&wc)[A],
            const Key (&gb)[B], const Key (&ga)[C], float ox, float oy) {
  const int n = c.npts;
  if (n < 2 || !room(u.tex, true, n * 2, (n - 1) * 6)) return;
  const float um = (u.u0 + u.u1) / 2, alpha = trailAlpha(c);
  const uint16_t b = (uint16_t)nv;
  for (int i = 0; i < n; ++i) {
    const Pt& p0 = c.pts[std::max(0, i - 1)];
    const Pt& p1 = c.pts[std::min(n - 1, i + 1)];
    float dx = p1.x - p0.x, dy = p1.y - p0.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len > 1e-4f) { dx /= len; dy /= len; } else { dx = 1; dy = 0; }
    const float f = (float)i / (n - 1);
    const float w = width * kU * kRibbonBoost * sample(wc, f) / 2;
    const float g = sample(gb, f);
    const uint32_t col = rgba(g * mr, g * mg, g * mb, sample(ga, f) * ma * alpha);
    const float x = c.pts[i].x - ox, y = c.pts[i].y - oy;
    verts[nv++] = {x - dy * w, y + dx * w, um, u.v0, col};
    verts[nv++] = {x + dy * w, y - dx * w, um, u.v1, col};
  }
  for (int i = 0; i < n - 1; ++i) {
    const uint16_t a = (uint16_t)(b + i * 2);
    const uint16_t q[6] = {a, (uint16_t)(a + 1), (uint16_t)(a + 3), a, (uint16_t)(a + 3), (uint16_t)(a + 2)};
    for (uint16_t k : q) idx[ni++] = k;
  }
}

}  // namespace

void launch(sts::Card* card, float x, float y, float s, float angle, float ex, float ey, float delay, bool shuffle) {
  Comet* c = nullptr;
  for (Comet& k : comets)
    if (!k.active) { c = &k; break; }
  if (!c) {  // full: recycle the oldest
    c = &comets[0];
    for (Comet& k : comets)
      if (k.age > c->age) c = &k;
  }
  *c = Comet{};
  c->active = true;
  c->shuffle = shuffle;
  c->card = card;
  c->delay = delay;
  c->sx = c->x = x;
  c->sy = c->y = y;
  c->ex = ex;
  c->ey = ey;
  c->s0 = c->bodyS = s;
  c->rot = angle;
  // NCardFlyVfx._Ready / NCardFlyShuffleVfx._Ready (Rng.Chaotic).
  const float offset = shuffle ? rr(-300, 400) : rr(100, 400);
  c->speed = rr(1.1f, 1.25f);
  c->accel = rr(2.f, 2.5f);
  c->dur = rr(1.f, 1.75f);
  const float arcDir = (ey < kH * 0.5f ? -500.f : 500.f + offset) * kArc;
  c->cx = (x + ex) / 2;
  c->cy = (y + ey) / 2 - arcDir;
}

void clear() {
  for (Comet& c : comets) c.active = false;
}

int live() {
  int n = 0;
  for (const Comet& c : comets) n += c.active;
  return n;
}

bool flying(const sts::Card* card) {
  if (!card) return false;
  for (const Comet& c : comets)
    if (c.active && c.card == card && !c.bodyDone) return true;
  return false;
}

void update(float dt) {
  if (dt <= 0) return;
  dt = std::min(dt, 0.1f);
  for (Comet& c : comets) {
    if (!c.active) continue;
    if (c.delay > 0) {
      c.delay -= dt;
      continue;
    }
    c.age += dt;
    // Trail points age out (NCardTrail._Process).
    int drop = 0;
    for (int i = 0; i < c.npts; ++i) {
      c.pts[i].age += dt;
      if (c.pts[i].age > kPointLife) drop = i + 1;
    }
    if (drop) {
      for (int i = drop; i < c.npts; ++i) c.pts[i - drop] = c.pts[i];
      c.npts -= drop;
    }
    if (c.phase == 0) {
      c.time += c.speed * dt;
      c.speed += c.accel * dt;
      const float u = c.time / c.dur;
      if (u > 1) {
        c.x = c.ex;
        c.y = c.ey;
        c.phase = 1;
        c.time = 0;
      } else {
        float nx, ny;
        bezier(c, u, c.x, c.y);
        bezier(c, (c.time + 0.05f) / c.dur, nx, ny);
        const float want = std::atan2(ny - c.y, nx - c.x) + kPi / 2;
        c.rot += std::remainder(want - c.rot, 2 * kPi) * std::min(1.f, dt * 12.f);  // Mathf.LerpAngle
        const float f = clamp01(c.time * 3 / c.dur);
        c.dark = f;
        // Body.Scale 1 -> 0.1 of the original's 0.8 play-pile card. Ours is held larger (readable on the 3DS), so it
        // shrinks to the same absolute size, eased out so it reads as a comet as early as the original's.
        c.bodyS = lerp(c.s0, 0.05f, 1 - (1 - f) * (1 - f));
      }
    } else if (!c.bodyDone) {
      c.time += c.speed * dt;
      const float u = c.time / c.dur;
      if (u > 0.25f && !c.fading) c.fading = true;
      c.bodyS = std::max(0.f, lerp(0.05f, c.shuffle ? -0.05f : -0.075f, u));
      if (u > 1) c.bodyDone = true;
    }
    if (c.fading) c.fadeT += dt;
    if (!c.fading) follow(c);
    emit(c, dt);
    stepParts(c, dt);
    if (c.bodyDone && c.fading && c.fadeT >= 0.5f) c.active = false;
  }
}

void drawTrails(bool top) {
  bool any = false;
  for (const Comet& c : comets) any |= c.active && c.delay <= 0;
  if (!any) return;
  const Uv tOuter = uvOf("vfx/trail"), tInner = uvOf("vfx/trail2"), sil = uvOf("vfx/cardsil"),
           brush = uvOf("vfx/brush"), spark = uvOf("vfx/spark");
  const float ox = top ? 0.f : kBotOX, oy = top ? 0.f : kBotOY;
  nv = ni = 0;
  curTex = nullptr;
  for (const Comet& c : comets) {
    if (!c.active || c.delay > 0) continue;
    const float alpha = trailAlpha(c);
    // OuterTrail: modulate (1, 0.17, 0, 0.75) x default_color alpha 0.75; InnerTrail: (1, 0.83, 0, 0.5).
    ribbon(c, tOuter, 96, 1.f, 0.168627f, 0.f, 0.752941f * 0.752941f, kOuterW, kOuterB, kOuterA, ox, oy);
    ribbon(c, tInner, 64, 1.f, 0.827451f, 0.f, 0.501961f, kInnerW, kInnerB, kInnerA, ox, oy);
    for (const Part& p : c.embers) {
      if (!p.live) continue;
      const float f = p.t / p.life;
      quad(brush, true, p.x - ox, p.y - oy, 16 * kU * p.size, 32 * kU * p.size, p.rot,
           rgba(sample(kEmR, f), sample(kEmG, f), sample(kEmB, f), sample(kEmA, f) * alpha));
    }
    for (const Part& p : c.sparks) {
      if (!p.live) continue;
      const float f = p.t / p.life, sz = 512 * kU * p.size;
      quad(spark, true, p.x - ox, p.y - oy, sz * 0.6f, sz, std::atan2(p.vy, p.vx) + kPi / 2,
           rgba(lerp(1, 0.878f, f), lerp(0.976f, 0.852f, f), lerp(0.851f, 0, f), (1 - f) * alpha));
    }
    // The comet head: the card silhouette twice (x1.1 red-orange, x0.9 orange), fading in over 1 s and shrinking to
    // half after 0.25 s; it stays where the card landed while the trail fades.
    const float ha = easeOutCubic(c.age / 1.f) * alpha;
    const float hs = 1.f - 0.5f * easeInCubic((c.age - 0.25f) / 0.5f);
    const float sz = 64 * kU * hs;
    quad(sil, true, c.x - ox, c.y - oy, sz * 1.1f, sz * 1.054f, c.rot, rgba(0.972549f, 0.176471f, 0, 0.752941f * ha));
    quad(sil, true, c.x - ox, c.y - oy, sz * 0.9f, sz * 0.9f, c.rot, rgba(0.901961f, 0.490196f, 0, 0.501961f * ha));
  }
  flush();
  curTex = nullptr;
}

int bodies(Body* out, int max) {
  int n = 0;
  for (const Comet& c : comets) {
    if (!c.active || !c.card || c.bodyDone || c.bodyS <= 0.002f || n >= max) continue;
    // NCardTrailVfx tweens the card's modulate alpha to 0.75 over 0.5 s. A delayed card waits where it was.
    out[n++] = {c.card, c.x, c.y, c.bodyS, c.rot, c.dark, 1.f - 0.25f * easeOutCubic(c.age / 0.5f), c.delay > 0};
  }
  return n;
}

void shade(float x, float y, float w, float h, float rot, float a) {
  const Uv sil = uvOf("vfx/cardsil");
  if (!sil.tex || a <= 0.004f) return;
  nv = ni = 0;
  curTex = nullptr;
  // The silhouette fills 42x59 of its 64x64 cell: scale the quad so the shape covers the card.
  quad(sil, false, x, y, w * 64 / 42, h * 64 / 59, rot, rgba(0, 0, 0, a));
  flush();
  curTex = nullptr;
}

}  // namespace ui::cardfly
