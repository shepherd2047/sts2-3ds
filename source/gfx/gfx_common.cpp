// Backend-independent parts of the gfx layer: the transform stack.
#include <cmath>
#include <vector>

#include "gfx.h"

namespace gfx {

namespace {
std::vector<Affine> stack;
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
  triangles(t, v, 4, idx, 6, false);
}

}  // namespace detail
}  // namespace gfx
