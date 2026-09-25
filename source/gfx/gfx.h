// Thin platform layer: two screens, textured quads, rectangles, lines and
// input. Implemented by gfx_3ds.cpp (citro2d) and gfx_sdl.cpp (desktop preview).
#pragma once
#include <cstdint>
#include <string>

namespace gfx {

constexpr int kTopW = 400, kBottomW = 320, kScreenH = 240;
enum Screen { TOP = 0, BOTTOM = 1 };

// Button bits match libctru's KEY_* so the 3DS backend passes them straight through.
enum : uint32_t {
  BTN_A = 1u << 0, BTN_B = 1u << 1, BTN_SELECT = 1u << 2, BTN_START = 1u << 3,
  BTN_RIGHT = 1u << 4, BTN_LEFT = 1u << 5, BTN_UP = 1u << 6, BTN_DOWN = 1u << 7,
  BTN_R = 1u << 8, BTN_L = 1u << 9, BTN_X = 1u << 10, BTN_Y = 1u << 11,
};

struct Texture;

struct Input {
  uint32_t down = 0, held = 0, up = 0;
  bool touching = false, touchDown = false, touchUp = false;
  int tx = 0, ty = 0;  // bottom-screen coordinates
};

bool init();
void shutdown();
bool running();
double dt();  // seconds since the previous frame
Input input();

void beginFrame();
void screen(Screen s, uint32_t clearRgba);
void endFrame();

// Reads romfs:/<path> (3DS) or ./romfs/<path> (desktop).
bool readFile(const std::string& path, std::string& out);
Texture* loadTexture(const std::string& path);
int texWidth(Texture* t);
int texHeight(Texture* t);

// Colours are 0xRRGGBBAA. The texture colour is lerped towards tint.rgb by
// `blend` and its alpha multiplied by tint.a (citro2d's tint model).
void image(Texture* t, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh,
           uint32_t tint = 0xFFFFFFFF, float blend = 0.f);
void rect(float x, float y, float w, float h, uint32_t rgba);
void rectGradient(float x, float y, float w, float h, uint32_t top, uint32_t bottom);
void line(float x0, float y0, float x1, float y1, float thickness, uint32_t rgba);
void circle(float x, float y, float r, uint32_t rgba);

// Textured, vertex-coloured triangles (Spine meshes). Colours are 0xRRGGBBAA.
struct Vert {
  float x, y, u, v;
  uint32_t rgba;
};
void triangles(Texture* t, const Vert* verts, int count, const uint16_t* indices, int indexCount, bool additive = false);

}  // namespace gfx

namespace gfx {

// 2D affine transform applied to image/rect/text drawing while pushed:
// x' = a*x + c*y + tx, y' = b*x + d*y + ty. Transformed draws go through
// triangles(), so rotated cards and arrow segments work on both backends.
struct Affine {
  float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
  static Affine translate(float x, float y) { Affine m; m.tx = x; m.ty = y; return m; }
  // Rotate by `radians` and scale by `s` around (px, py).
  static Affine rotateAround(float px, float py, float radians, float s = 1.f);
  Affine operator*(const Affine& o) const;  // this after o
  void apply(float& x, float& y) const {
    float nx = a * x + c * y + tx, ny = b * x + d * y + ty;
    x = nx;
    y = ny;
  }
};
void pushTransform(const Affine& m);  // composed with the current transform
void popTransform();

namespace detail {
bool transformed();
const Affine& current();
Texture* whiteTexture();  // provided by each backend
// Draws a transformed textured quad (tex may be the white texture) via triangles().
void texQuad(Texture* t, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh, uint32_t rgba);
}  // namespace detail

}  // namespace gfx
