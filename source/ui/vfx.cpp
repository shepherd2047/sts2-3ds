// F7: light combat VFX (see vfx.h). Everything lives in fixed arrays; a burst that finds the pool
// full recycles the particle nearest to its end, so a busy multi-hit never allocates or stalls.
#include "vfx.h"

#include "ui_common.h"

namespace ui::vfx {

namespace {

constexpr float kPi = 3.14159265f;

// Sprite slots. vfx/* come from add_vfx_art (one sheet on one atlas page); the block shield and
// the Regent's star reuse the combat HUD's own icons.
enum Spr : uint8_t { kSlash, kGlow, kRing, kStar, kSpark, kFlame, kArrow, kBubble, kDot, kShield, kUiStar, kSprCount };
const char* const kNames[kSprCount] = {"vfx/slash", "vfx/glow", "vfx/ring",   "vfx/star", "vfx/spark", "vfx/flame",
                                       "vfx/arrow", "vfx/bubble", "vfx/dot", "ui/block", "ui/star"};

enum : uint8_t {
  kAdd = 1,      // additive blend (sparks, glows)
  kAlign = 2,    // the sprite's long (y) axis follows the velocity
  kHold = 4,     // full alpha until 60 % of the life, then fade (default: fades from the start)
  kPop = 8,      // no fade-in
  kStretch = 16, // length grows with speed (streaks)
};

struct P {
  float x = 0, y = 0, vx = 0, vy = 0, ay = 0, drag = 0;
  float t = 0, life = 1, delay = 0;
  float w = 8, h = 8, grow = 1;  // size at birth, size multiplier at the end of the life
  float rot = 0, vrot = 0, alpha = 1;
  uint32_t rgb = 0xFFFFFF00;     // 0xRRGGBB00, multiplied with the texture
  uint8_t spr = kDot, flags = 0;
};

P pool[kMaxParticles];
int count = 0;
uint32_t seed = 0x2545F491u;  // own LCG: previews stay deterministic, the rules' RNG is untouched
int slashSide = 0;

float rnd() {
  seed = seed * 1664525u + 1013904223u;
  return (float)(seed >> 8) * (1.f / 16777216.f);
}
float rr(float a, float b) { return a + (b - a) * rnd(); }

P& spawn(uint8_t spr, float x, float y, float w, float h, float life, uint32_t rgb, uint8_t flags) {
  P* p = nullptr;
  if (count < kMaxParticles) {
    p = &pool[count++];
  } else {
    int best = 0;
    float left = 1e9f;
    for (int i = 0; i < kMaxParticles; ++i) {
      float l = pool[i].delay + pool[i].life - pool[i].t;
      if (l < left) { left = l; best = i; }
    }
    p = &pool[best];
  }
  *p = P{};
  p->spr = spr;
  p->x = x;
  p->y = y;
  p->w = w;
  p->h = h;
  p->life = life;
  p->rgb = rgb;
  p->flags = flags;
  return *p;
}

// A radial burst of streaks.
void streaks(float x, float y, int n, float speed, float life, uint32_t rgb, float gravity, float up = 0) {
  for (int i = 0; i < n; ++i) {
    P& p = spawn(kDot, x + rr(-3, 3), y + rr(-3, 3), 3, 6, rr(life * 0.7f, life), rgb, kAdd | kAlign | kStretch | kPop);
    float a = rr(0, 2 * kPi), sp = rr(speed * 0.55f, speed);
    p.vx = std::cos(a) * sp;
    p.vy = std::sin(a) * sp - up;
    p.drag = 3;
    p.ay = gravity;
  }
}

P& glow(float x, float y, float size, float life, uint32_t rgb, float grow) {
  P& g = spawn(kGlow, x, y, size, size, life, rgb, kAdd | kPop);
  g.grow = grow;
  return g;
}

P& ring(float x, float y, float size, float life, uint32_t rgb, float grow) {
  P& r = spawn(kRing, x, y, size, size, life, rgb, kAdd | kPop);
  r.grow = grow;
  return r;
}

}  // namespace

void clear() { count = 0; }

int live() { return count; }

void update(float dt) {
  if (dt <= 0) return;
  dt = std::min(dt, 0.1f);  // a hitch must not teleport particles
  for (int i = 0; i < count;) {
    P& p = pool[i];
    if (p.delay > 0) {
      p.delay -= dt;
      ++i;
      continue;
    }
    p.t += dt;
    if (p.t >= p.life) {
      pool[i] = pool[--count];
      continue;
    }
    if (p.drag > 0) {
      float k = 1.f / (1.f + p.drag * dt);
      p.vx *= k;
      p.vy *= k;
    }
    p.vy += p.ay * dt;
    p.x += p.vx * dt;
    p.y += p.vy * dt;
    p.rot += p.vrot * dt;
    ++i;
  }
}

void draw() {
  if (!count) return;
  struct Uv {
    gfx::Texture* tex;
    float u0, v0, u1, v1;
  };
  Uv uv[kSprCount];
  for (int s = 0; s < kSprCount; ++s) {
    Sprite sp = R().sprite(kNames[s]);  // short names: no heap (SSO); only while particles live
    uv[s].tex = sp.tex;
    if (!sp.tex) continue;
    const float tw = (float)gfx::texWidth(sp.tex), th = (float)gfx::texHeight(sp.tex);
    uv[s].u0 = sp.x / tw;
    uv[s].v0 = sp.y / th;
    uv[s].u1 = (sp.x + sp.w) / tw;
    uv[s].v1 = (sp.y + sp.h) / th;
  }
  static gfx::Vert verts[kMaxParticles * 4];
  static uint16_t idx[kMaxParticles * 6];
  static bool idxReady = false;
  if (!idxReady) {
    for (int i = 0; i < kMaxParticles; ++i) {
      const uint16_t b = (uint16_t)(i * 4);
      const uint16_t q[6] = {b, (uint16_t)(b + 1), (uint16_t)(b + 2), b, (uint16_t)(b + 2), (uint16_t)(b + 3)};
      for (int k = 0; k < 6; ++k) idx[i * 6 + k] = q[k];
    }
    idxReady = true;
  }
  // Normal-blend sprites first (shield, arrows, bubbles), then the additive glows over them; each
  // pass is one triangles() call per texture run (all vfx/* share a page).
  for (int pass = 0; pass < 2; ++pass) {
    const bool additive = pass == 1;
    gfx::Texture* cur = nullptr;
    int n = 0;
    auto flush = [&] {
      if (n) gfx::triangles(cur, verts, n * 4, idx, n * 6, additive);
      n = 0;
    };
    for (int i = 0; i < count; ++i) {
      const P& p = pool[i];
      if (p.delay > 0 || ((p.flags & kAdd) != 0) != additive) continue;
      const Uv& u = uv[p.spr];
      if (!u.tex) continue;
      const float f = p.t / p.life;
      float a = p.alpha * ((p.flags & kPop) ? 1.f : std::min(1.f, f / 0.12f));
      a *= (p.flags & kHold) ? (f < 0.6f ? 1.f : (1.f - f) / 0.4f) : 1.f - f * f;
      if (a <= 0.004f) continue;
      if (u.tex != cur) {
        flush();
        cur = u.tex;
      }
      const float e = 1.f - (1.f - f) * (1.f - f);  // ease-out size
      const float sc = 1.f + (p.grow - 1.f) * e;
      float hw = p.w * sc * 0.5f, hh = p.h * sc * 0.5f;
      float ang = p.rot;
      if (p.flags & (kAlign | kStretch)) {
        const float spd = std::sqrt(p.vx * p.vx + p.vy * p.vy);
        if (p.flags & kAlign) ang = std::atan2(p.vy, p.vx) + kPi / 2;
        if (p.flags & kStretch) hh *= std::min(3.f, 1.f + spd / 120.f);
      }
      const float c = std::cos(ang), s = std::sin(ang);
      const uint32_t rgba = (p.rgb & 0xFFFFFF00u) | (uint32_t)(std::min(1.f, a) * 255.f);
      const float lx[4] = {-hw, hw, hw, -hw}, ly[4] = {-hh, -hh, hh, hh};
      const float us[4] = {u.u0, u.u1, u.u1, u.u0}, vs[4] = {u.v0, u.v0, u.v1, u.v1};
      gfx::Vert* v = verts + n * 4;
      for (int k = 0; k < 4; ++k)
        v[k] = {p.x + lx[k] * c - ly[k] * s, p.y + lx[k] * s + ly[k] * c, us[k], vs[k], rgba};
      ++n;
    }
    flush();
  }
}

// ---------------------------------------------------------------- spawners

void hit(float x, float y, int amount, uint32_t rgb) {
  const float big = std::clamp(amount / 20.f, 0.f, 1.f);
  glow(x, y, 26 + 16 * big, 0.16f, 0xFFE8B000, 1.5f);
  slashSide ^= 1;
  P& s = spawn(kSlash, x + rr(-4, 4), y + rr(-4, 4), 9 + 3 * big, 56 + 28 * big, 0.2f, rgb, kAdd | kPop);
  s.rot = (slashSide ? 0.78f : -0.78f) + rr(-0.2f, 0.2f);
  s.grow = 1.3f;
  streaks(x, y, 5 + (int)(4 * big), 190, 0.38f, 0xFFD89000, 260, 40);
  for (int i = 0; i < 2; ++i) {
    P& k = spawn(kSpark, x + rr(-10, 10), y + rr(-10, 10), 13, 13, 0.24f, 0xFFF4D000, kAdd | kPop);
    k.rot = rr(0, kPi);
    k.grow = 0.4f;
  }
}

void blocked(float x, float y) {
  glow(x, y, 24, 0.18f, 0x8CC4F000, 1.4f);
  ring(x, y, 14, 0.25f, 0xB8E0FF00, 2.4f);
  streaks(x, y, 6, 150, 0.3f, 0xA8D8FF00, 200, 30);
}

void blockBroken(float x, float y) {
  ring(x, y, 18, 0.3f, 0x8CC4F000, 2.6f);
  for (int i = 0; i < 7; ++i) {
    P& p = spawn(kSpark, x + rr(-6, 6), y + rr(-6, 6), rr(6, 10), rr(6, 10), rr(0.35f, 0.5f), 0x9CCEF800, kAdd | kPop);
    float a = rr(0, 2 * kPi), sp = rr(70, 140);
    p.vx = std::cos(a) * sp;
    p.vy = std::sin(a) * sp - 50;
    p.ay = 320;
    p.vrot = rr(-8, 8);
    p.grow = 0.6f;
  }
}

void blockGain(float x, float y) {
  P& sh = spawn(kShield, x, y, 24, 24, 0.5f, 0xFFFFFF00, kHold | kPop);
  sh.grow = 1.45f;
  sh.alpha = 0.9f;
  ring(x, y, 20, 0.4f, 0x8CC4F000, 2.8f);
  glow(x, y, 34, 0.3f, 0x5A9EE000, 1.2f);
  for (int i = 0; i < 4; ++i) {
    P& p = spawn(kDot, x + rr(-16, 16), y + rr(-4, 12), 4, 4, rr(0.4f, 0.6f), 0xB8E0FF00, kAdd);
    p.vy = rr(-60, -35);
    p.delay = rr(0, 0.12f);
  }
}

void poisonTick(float x, float y, float h) {
  glow(x, y, 32, 0.32f, 0x76FF4000, 1.3f);
  for (int i = 0; i < 7; ++i) {
    const float s = rr(8, 13);
    P& p = spawn(kBubble, x + rr(-15, 15), y + rr(0, h * 0.3f), s, s, rr(0.5f, 0.8f), 0xB8FF9000, 0);
    p.vx = rr(-12, 12);
    p.vy = rr(-70, -35);
    p.delay = rr(0, 0.2f);
    p.grow = 1.25f;
  }
}

void burnTick(float x, float y, float h) {
  glow(x, y + h * 0.1f, 34, 0.3f, 0xFF702000, 1.3f);
  for (int i = 0; i < 7; ++i) {
    const float s = rr(10, 15);
    P& p = spawn(kFlame, x + rr(-14, 14), y + h * 0.25f, s, s, rr(0.45f, 0.65f), 0xFF7A2A00, kAdd);
    p.vx = rr(-10, 10);
    p.vy = rr(-80, -45);
    p.delay = rr(0, 0.15f);
    p.rot = rr(-0.4f, 0.4f);
    p.grow = 0.5f;
  }
  streaks(x, y + h * 0.2f, 4, 90, 0.5f, 0xFFC06000, -60, 60);
}

void heal(float x, float y, float h) {
  glow(x, y, 42, 0.45f, 0x60FF9000, 1.3f);
  for (int i = 0; i < 9; ++i) {
    const float s = rr(10, 16);
    P& p = spawn(kSpark, x + rr(-18, 18), y + rr(-h * 0.3f, h * 0.4f), s, s, rr(0.6f, 0.9f), 0xA0FFB000, kAdd);
    p.vy = rr(-55, -30);
    p.delay = rr(0, 0.35f);
    p.vrot = rr(-3, 3);
    p.grow = 0.6f;
  }
  for (int i = 0; i < 4; ++i) {
    P& p = spawn(kDot, x + rr(-16, 16), y + rr(0, h * 0.4f), 4, 4, rr(0.6f, 0.8f), 0x80FF9000, kAdd);
    p.vy = rr(-70, -40);
    p.delay = rr(0, 0.3f);
  }
}

void powerArrows(float x, float y, float h, bool buff) {
  const uint32_t c = buff ? 0xFFD24A00 : 0xC070FF00;
  glow(x, y, 30, 0.35f, buff ? 0xFFC04000 : 0xA050FF00, 1.2f);
  for (int i = 0; i < 3; ++i) {
    const float ox = (i - 1) * 17.f, lift = i == 1 ? -7.f : 0.f;
    P& p = spawn(kArrow, x + ox, (buff ? y + h * 0.2f : y - h * 0.2f) + lift, 16, 19, 0.65f, c, kHold);
    p.vy = buff ? -55.f : 45.f;
    p.drag = 1;
    p.rot = buff ? 0.f : kPi;
    p.delay = i * 0.07f;
  }
}

void orbEvoke(float x, float y, uint32_t rgb) {
  glow(x, y, 20, 0.35f, rgb, 2.6f);
  ring(x, y, 14, 0.4f, rgb, 3.f);
  streaks(x, y, 10, 120, 0.35f, rgb, 0);
  for (int i = 0; i < 3; ++i) {
    P& k = spawn(kSpark, x + rr(-8, 8), y + rr(-8, 8), 12, 12, 0.3f, 0xFFFFFF00, kAdd | kPop);
    k.rot = rr(0, kPi);
    k.grow = 0.5f;
  }
}

void stars(float x, float y, int gained) {
  const int n = std::clamp(2 + gained, 3, 7);
  glow(x, y, 36, 0.4f, 0xFFD06000, 1.4f);
  for (int i = 0; i < n; ++i) {
    P& p = spawn(kStar, x + rr(-6, 6), y + rr(-6, 6), 10, 15, rr(0.6f, 0.85f), 0xFFFFFF00, kAdd);
    const float a = -kPi / 2 + rr(-1.1f, 1.1f), sp = rr(70, 130);
    p.vx = std::cos(a) * sp;
    p.vy = std::sin(a) * sp;
    p.ay = 140;
    p.drag = 1.5f;
    p.vrot = rr(-4, 4);
  }
  for (int i = 0; i < 2; ++i) {
    P& p = spawn(kUiStar, x + (i ? 12.f : -12.f), y - 8, 14, 14, 0.7f, 0xFFFFFF00, kHold);
    p.vy = -40;
    p.grow = 1.2f;
    p.delay = i * 0.1f;
  }
}

void soul(float x, float y, float h, uint32_t rgb) {
  glow(x, y, 40, 0.5f, rgb, 1.3f);
  for (int i = 0; i < 8; ++i) {
    const float s = rr(9, 14);
    P& p = spawn(kFlame, x + rr(-16, 16), y + h * 0.3f, s, s, rr(0.6f, 0.9f), rgb, kAdd);
    p.vx = rr(-8, 8);
    p.vy = rr(-60, -30);
    p.delay = rr(0, 0.3f);
    p.rot = rr(-0.3f, 0.3f);
    p.grow = 0.6f;
  }
}

void puff(float x, float y, uint32_t rgb) {
  glow(x, y, 18, 0.15f, rgb, 1.4f);
  streaks(x, y, 4, 110, 0.3f, rgb, 200, 20);
}

}  // namespace ui::vfx
