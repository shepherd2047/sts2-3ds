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
  BTN_ZL = 1u << 14, BTN_ZR = 1u << 15,  // New 3DS only (S19: the top bar's focus mode)
};

struct Texture;

struct Input {
  uint32_t down = 0, held = 0, up = 0;
  bool touching = false, touchDown = false, touchUp = false;
  int tx = 0, ty = 0;  // bottom-screen coordinates
};

bool init();
void shutdown();
bool running();  // false = quit (3DS: HOME menu -> Close, or the power button)
double dt();  // seconds since the previous frame
// Y5: the system suspends the game (3DS: HOME menu, lid closed -> sleep): fn(true) before,
// fn(false) after it resumes (it may run on the system's APT thread: keep it to flag / audio
// calls). The time spent suspended never reaches dt(). The SDL preview never calls it.
void onSystemPause(void (*fn)(bool paused));
Input input();
// Clears `mask` from this frame's `down` presses: every later input() call in the same frame
// reports them as not pressed (the L+R top-bar chord consumes L and R, so no screen also sees
// a single L or R that frame).
void consumeButtons(uint32_t mask);

void beginFrame();
void screen(Screen s, uint32_t clearRgba);
void endFrame();

// Reads romfs:/<path> (3DS) or ./romfs/<path> (desktop).
bool readFile(const std::string& path, std::string& out);
// Save files (not in romfs): PC saves/<name>, 3DS sdmc:/3ds/sts2-3ds/<name>. Written to a
// temporary file first, then renamed, so a power cut never leaves half a save. `name` may have
// one subdirectory (Y4 profiles: "profile1/run.sav"), created on write.
bool readSave(const std::string& name, std::string& out);
bool writeSave(const std::string& name, const std::string& data);
void deleteSave(const std::string& name);
// Text entry (S03 profile names). Blocks until the player confirms or cancels; returns true
// with the UTF-8 text in `out` (at most maxBytes bytes, whole characters) on OK. 3DS: the
// system software keyboard (libctru swkbd). Preview: STS_TEXT_INPUT=<text> answers at once
// (automated tests; also read from sts2-debug.txt on 3DS); otherwise the text is typed into the
// window title (Enter = OK, Esc = cancel), hidden windows cancel. Call from update, not while
// drawing a screen.
bool textInput(const char* hint, const std::string& initial, std::string& out, int maxBytes);
Texture* loadTexture(const std::string& path);
// Only between frames' draws (e.g. from App::update): the GPU is done with the last frame.
void freeTexture(Texture* t);
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

// ---- F2 renderer features (gfx_common.cpp; identical on both backends) -------------------
// 9-slice: the source rect (sx, sy, sw, sh) has margins l/t/r/b (source pixels) that keep their size;
// the edges and the centre stretch to fill (dx, dy, dw, dh). Margins shrink if the target is smaller.
void nineSlice(Texture* t, float sx, float sy, float sw, float sh, float l, float tp, float r, float b,
               float dx, float dy, float dw, float dh, uint32_t tint = 0xFFFFFFFF, float blend = 0.f);
// A four-corner gradient (colours 0xRRGGBBAA, bilinear inside): vertical, horizontal or anything.
void gradient(float x, float y, float w, float h, uint32_t topLeft, uint32_t topRight, uint32_t bottomLeft,
              uint32_t bottomRight);
// Image centred on (cx, cy), w x h big, rotated by `radians` (positive = clockwise on screen).
void imageRotated(Texture* t, float sx, float sy, float sw, float sh, float cx, float cy, float w, float h,
                  float radians, uint32_t tint = 0xFFFFFFFF, float blend = 0.f);
// Scissor: draws inside pushClip/popClip are cut to the rectangle (screen coordinates of the current
// screen, nested clips intersect). Applies to image, rect, text, gradient and nineSlice; transformed
// (rotated / scaled) draws and lines / circles / meshes are only skipped when entirely outside.
void pushClip(float x, float y, float w, float h);
void popClip();
// Global alpha: every draw inside pushAlpha/popAlpha is multiplied by `a` (0..1); nests by multiplying.
// Screen fades, dimmed pages and disabled widgets use it. Both stacks reset when a screen begins.
void pushAlpha(float a);
void popAlpha();

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
// Backend primitives; the public draw functions add the clip and global alpha and call these.
void rawImage(Texture* t, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh,
              uint32_t tint, float blend);
void rawRect(float x, float y, float w, float h, uint32_t rgba);
void rawLine(float x0, float y0, float x1, float y1, float thickness, uint32_t rgba);
void rawCircle(float x, float y, float r, uint32_t rgba);
void rawTriangles(Texture* t, const Vert* verts, int count, const uint16_t* indices, int indexCount, bool additive);
void resetState();  // clip and alpha stacks; the backends call it from screen()
Texture* whiteTexture();  // provided by each backend
// Draws a transformed textured quad (tex may be the white texture) via triangles().
void texQuad(Texture* t, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh, uint32_t rgba);
}  // namespace detail

}  // namespace gfx
