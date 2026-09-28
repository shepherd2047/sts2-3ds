// Backend-independent parts of the gfx layer: the transform stack.
#include <cmath>
#include <vector>

#include "gfx.h"

namespace gfx {

namespace {
std::vector<Affine> stack;

// Clip rectangles (already intersected with their parents) and the global alpha stack.
struct Box { float x0, y0, x1, y1; };
std::vector<Box> clips;
std::vector<float> alphas;
float alphaMul = 1.f;

uint32_t mulAlpha(uint32_t rgba) {
  if (alphaMul >= 1.f) return rgba;
  uint32_t a = (uint32_t)((rgba & 255) * alphaMul + 0.5f);
  return (rgba & 0xFFFFFF00u) | a;
}

// Cuts a source/destination pair to the active clip; false when nothing is left.
bool clipQuad(float& sx, float& sy, float& sw, float& sh, float& dx, float& dy, float& dw, float& dh) {
  if (clips.empty()) return true;
  const Box& c = clips.back();
  float x0 = dx, y0 = dy, x1 = dx + dw, y1 = dy + dh;
  if (x1 <= c.x0 || x0 >= c.x1 || y1 <= c.y0 || y0 >= c.y1) return false;
  if (dw <= 0 || dh <= 0) return true;
  float kx = sw / dw, ky = sh / dh;
  if (x0 < c.x0) { float d = c.x0 - x0; sx += d * kx; sw -= d * kx; x0 = c.x0; }
  if (x1 > c.x1) { sw -= (x1 - c.x1) * kx; x1 = c.x1; }
  if (y0 < c.y0) { float d = c.y0 - y0; sy += d * ky; sh -= d * ky; y0 = c.y0; }
  if (y1 > c.y1) { sh -= (y1 - c.y1) * ky; y1 = c.y1; }
  dx = x0; dy = y0; dw = x1 - x0; dh = y1 - y0;
  return dw > 0 && dh > 0;
}

// Bounding box of a (possibly transformed) rectangle against the clip: only whole-outside is culled.
bool insideClip(float x, float y, float w, float h) {
  if (clips.empty()) return true;
  const Box& c = clips.back();
  float xs[4] = {x, x + w, x + w, x}, ys[4] = {y, y, y + h, y + h};
  float lo_x = 1e9f, hi_x = -1e9f, lo_y = 1e9f, hi_y = -1e9f;
  for (int i = 0; i < 4; ++i) {
    float px = xs[i], py = ys[i];
    detail::current().apply(px, py);
    lo_x = px < lo_x ? px : lo_x; hi_x = px > hi_x ? px : hi_x;
    lo_y = py < lo_y ? py : lo_y; hi_y = py > hi_y ? py : hi_y;
  }
  return !(hi_x <= c.x0 || lo_x >= c.x1 || hi_y <= c.y0 || lo_y >= c.y1);
}
}  // namespace

void pushClip(float x, float y, float w, float h) {
  Box b{x, y, x + w, y + h};
  if (!clips.empty()) {
    const Box& p = clips.back();
    b.x0 = b.x0 > p.x0 ? b.x0 : p.x0;
    b.y0 = b.y0 > p.y0 ? b.y0 : p.y0;
    b.x1 = b.x1 < p.x1 ? b.x1 : p.x1;
    b.y1 = b.y1 < p.y1 ? b.y1 : p.y1;
  }
  clips.push_back(b);
}
void popClip() { if (!clips.empty()) clips.pop_back(); }

void pushAlpha(float a) {
  alphas.push_back(alphaMul);
  alphaMul *= a < 0 ? 0 : a > 1 ? 1 : a;
}
void popAlpha() {
  if (alphas.empty()) return;
  alphaMul = alphas.back();
  alphas.pop_back();
}
void detail::resetState() {
  clips.clear();
  alphas.clear();
  alphaMul = 1.f;
}

void image(Texture* t, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh, uint32_t tint,
           float blend) {
  if (!t) return;
  if (detail::transformed()) {
    if (!insideClip(dx, dy, dw, dh)) return;
  } else if (!clipQuad(sx, sy, sw, sh, dx, dy, dw, dh)) {
    return;
  }
  detail::rawImage(t, sx, sy, sw, sh, dx, dy, dw, dh, mulAlpha(tint), blend);
}

void rect(float x, float y, float w, float h, uint32_t rgba) {
  if (detail::transformed()) {
    if (!insideClip(x, y, w, h)) return;
  } else {
    float sx = 0, sy = 0, sw = 1, sh = 1;
    if (!clipQuad(sx, sy, sw, sh, x, y, w, h)) return;
  }
  detail::rawRect(x, y, w, h, mulAlpha(rgba));
}

void line(float x0, float y0, float x1, float y1, float th, uint32_t rgba) {
  detail::rawLine(x0, y0, x1, y1, th, mulAlpha(rgba));
}

void circle(float x, float y, float r, uint32_t rgba) { detail::rawCircle(x, y, r, mulAlpha(rgba)); }

void triangles(Texture* t, const Vert* verts, int count, const uint16_t* indices, int indexCount, bool additive) {
  if (alphaMul >= 1.f) { detail::rawTriangles(t, verts, count, indices, indexCount, additive); return; }
  static std::vector<Vert> tmp;
  tmp.assign(verts, verts + count);
  for (auto& v : tmp) v.rgba = mulAlpha(v.rgba);
  detail::rawTriangles(t, tmp.data(), count, indices, indexCount, additive);
}

namespace {
uint32_t lerpColor(uint32_t a, uint32_t b, float t) {
  uint32_t out = 0;
  for (int sh = 0; sh < 32; sh += 8) {
    float va = (float)((a >> sh) & 255), vb = (float)((b >> sh) & 255);
    out |= (uint32_t)(va + (vb - va) * t + 0.5f) << sh;
  }
  return out;
}
}  // namespace

void gradient(float x, float y, float w, float h, uint32_t tl, uint32_t tr, uint32_t bl, uint32_t br) {
  if (w <= 0 || h <= 0) return;
  float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
  // Clip the rectangle and evaluate the corner colours at the cut edges.
  if (!detail::transformed() && !clips.empty()) {
    float sx = 0, sy = 0, sw = 1, sh = 1, dx = x, dy = y, dw = w, dh = h;
    if (!clipQuad(sx, sy, sw, sh, dx, dy, dw, dh)) return;
    float u0 = sx, v0 = sy, u1 = sx + sw, v1 = sy + sh;
    auto at = [&](float u, float v) { return lerpColor(lerpColor(tl, tr, u), lerpColor(bl, br, u), v); };
    uint32_t ntl = at(u0, v0), ntr = at(u1, v0), nbl = at(u0, v1), nbr = at(u1, v1);
    tl = ntl; tr = ntr; bl = nbl; br = nbr;
    x0 = dx; y0 = dy; x1 = dx + dw; y1 = dy + dh;
  } else if (detail::transformed() && !insideClip(x, y, w, h)) {
    return;
  }
  float xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1};
  uint32_t cs[4] = {tl, tr, br, bl};
  Vert v[4];
  for (int i = 0; i < 4; ++i) {
    float px = xs[i], py = ys[i];
    if (detail::transformed()) detail::current().apply(px, py);
    v[i] = {px, py, 0.5f, 0.5f, mulAlpha(cs[i])};
  }
  static const uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
  detail::rawTriangles(detail::whiteTexture(), v, 4, idx, 6, false);
}

void rectGradient(float x, float y, float w, float h, uint32_t top, uint32_t bottom) {
  gradient(x, y, w, h, top, top, bottom, bottom);
}

void nineSlice(Texture* t, float sx, float sy, float sw, float sh, float l, float tp, float r, float b, float dx,
               float dy, float dw, float dh, uint32_t tint, float blend) {
  if (!t || dw <= 0 || dh <= 0) return;
  // Margins never exceed half the target in either direction.
  float dl = l, dr = r, dt = tp, db = b;
  if (dl + dr > dw) { float k = dw / (dl + dr); dl *= k; dr *= k; }
  if (dt + db > dh) { float k = dh / (dt + db); dt *= k; db *= k; }
  float sxs[4] = {sx, sx + l, sx + sw - r, sx + sw};
  float sys_[4] = {sy, sy + tp, sy + sh - b, sy + sh};
  float dxs[4] = {dx, dx + dl, dx + dw - dr, dx + dw};
  float dys[4] = {dy, dy + dt, dy + dh - db, dy + dh};
  for (int j = 0; j < 3; ++j)
    for (int i = 0; i < 3; ++i) {
      float w = dxs[i + 1] - dxs[i], h = dys[j + 1] - dys[j];
      float ssw = sxs[i + 1] - sxs[i], ssh = sys_[j + 1] - sys_[j];
      if (w <= 0 || h <= 0 || ssw <= 0 || ssh <= 0) continue;
      image(t, sxs[i], sys_[j], ssw, ssh, dxs[i], dys[j], w, h, tint, blend);
    }
}

void imageRotated(Texture* t, float sx, float sy, float sw, float sh, float cx, float cy, float w, float h,
                  float radians, uint32_t tint, float blend) {
  pushTransform(Affine::rotateAround(cx, cy, radians));
  image(t, sx, sy, sw, sh, cx - w / 2, cy - h / 2, w, h, tint, blend);
  popTransform();
}

Affine Affine::rotateAround(float px, float py, float r, float s) {
  float cs = std::cos(r) * s, sn = std::sin(r) * s;
  Affine m;
  m.a = cs;
  m.b = sn;
  m.c = -sn;
  m.d = cs;
  m.tx = px - (cs * px - sn * py);
  m.ty = py - (sn * px + cs * py);
  return m;
}

Affine Affine::operator*(const Affine& o) const {
  Affine m;
  m.a = a * o.a + c * o.b;
  m.b = b * o.a + d * o.b;
  m.c = a * o.c + c * o.d;
  m.d = b * o.c + d * o.d;
  m.tx = a * o.tx + c * o.ty + tx;
  m.ty = b * o.tx + d * o.ty + ty;
  return m;
}

void pushTransform(const Affine& m) { stack.push_back(stack.empty() ? m : stack.back() * m); }
void popTransform() { if (!stack.empty()) stack.pop_back(); }

namespace detail {

bool transformed() { return !stack.empty(); }
const Affine& current() {
  static Affine identity;
  return stack.empty() ? identity : stack.back();
}

void texQuad(Texture* t, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh, uint32_t rgba) {
  if (!t) return;
  float tw = (float)texWidth(t), th = (float)texHeight(t);
  float xs[4] = {dx, dx + dw, dx + dw, dx}, ys[4] = {dy, dy, dy + dh, dy + dh};
  float us[4] = {sx / tw, (sx + sw) / tw, (sx + sw) / tw, sx / tw};
  float vs[4] = {sy / th, sy / th, (sy + sh) / th, (sy + sh) / th};
  Vert v[4];
  const Affine& m = current();
  for (int i = 0; i < 4; ++i) {
    float x = xs[i], y = ys[i];
    m.apply(x, y);
    v[i] = {x, y, us[i], vs[i], rgba};
  }
  static const uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
  rawTriangles(t, v, 4, idx, 6, false);
}

void resetState();

}  // namespace detail
}  // namespace gfx
