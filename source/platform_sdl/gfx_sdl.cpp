// Desktop preview backend: both 3DS screens stacked in one SDL window, the
// mouse standing in for the stylus on the bottom screen.
//
// Keys: Z=A  X=B  A=Y  S=X  Q=L  W=R  arrows=D-pad  Enter=START  Backspace=SELECT
#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#include "../gfx/gfx.h"

namespace gfx {

struct Texture {
  SDL_Texture* tex;
  int w, h;
};

namespace {
SDL_Window* win;
SDL_Renderer* ren;
bool quit = false;
int scale = 2;
Uint64 lastTicks;
double frameDt = 1.0 / 60;
bool fixedStep = getenv("STS_FIXED_STEP") != nullptr;  // deterministic timing for scripted runs
Input cur;
float ox = 0, oy = 0;  // current screen origin in window pixels / scale
const char* screenshotPath = nullptr;
int screenshotAfter = -1;
int frameCount = 0;

// STS_SCRIPT="30:A,60:T160x120,90:X" presses a button or taps the bottom
// screen at the given frame, for automated screenshots. Drags: P160x200 presses
// and holds, M160x120 moves the held touch, U releases.
struct Scripted { int frame; uint32_t btn; int tx, ty; char kind = 'T'; };
bool scriptHeld = false;
int heldX = 0, heldY = 0;
std::vector<Scripted> script;
std::vector<std::pair<int, std::string>> shots;  // STS_SHOTS="120:a.bmp,300:b.bmp"

void parseScript() {
  if (const char* s = getenv("STS_SCRIPT")) {
    std::string str(s);
    size_t p = 0;
    while (p < str.size()) {
      size_t e = str.find(',', p);
      if (e == std::string::npos) e = str.size();
      std::string item = str.substr(p, e - p);
      p = e + 1;
      size_t c = item.find(':');
      if (c == std::string::npos) continue;
      Scripted sc{atoi(item.c_str()), 0, -1, -1};
      std::string k = item.substr(c + 1);
      if (k[0] == 'T' || k[0] == 'P' || k[0] == 'M') { sc.kind = k[0]; sscanf(k.c_str() + 1, "%dx%d", &sc.tx, &sc.ty); }
      else if (k == "U") sc.kind = 'U';
      else if (k == "A") sc.btn = BTN_A; else if (k == "B") sc.btn = BTN_B; else if (k == "X") sc.btn = BTN_X;
      else if (k == "Y") sc.btn = BTN_Y; else if (k == "L") sc.btn = BTN_L; else if (k == "R") sc.btn = BTN_R;
      else if (k == "LEFT") sc.btn = BTN_LEFT; else if (k == "RIGHT") sc.btn = BTN_RIGHT;
      else if (k == "UP") sc.btn = BTN_UP; else if (k == "DOWN") sc.btn = BTN_DOWN;
      else if (k == "START") sc.btn = BTN_START;
      script.push_back(sc);
    }
  }
  if (const char* s = getenv("STS_SHOTS")) {
    std::string str(s);
    size_t p = 0;
    while (p < str.size()) {
      size_t e = str.find(',', p);
      if (e == std::string::npos) e = str.size();
      std::string item = str.substr(p, e - p);
      p = e + 1;
      size_t c = item.find(':');
      if (c != std::string::npos) shots.push_back({atoi(item.c_str()), item.substr(c + 1)});
    }
  }
}

SDL_Color col(uint32_t c) { return SDL_Color{Uint8(c >> 24), Uint8(c >> 16), Uint8(c >> 8), Uint8(c)}; }

uint32_t mapKey(SDL_Keycode k) {
  switch (k) {
    case SDLK_z: return BTN_A;
    case SDLK_x: return BTN_B;
    case SDLK_a: return BTN_Y;
    case SDLK_s: return BTN_X;
    case SDLK_q: return BTN_L;
    case SDLK_w: return BTN_R;
    case SDLK_UP: return BTN_UP;
    case SDLK_DOWN: return BTN_DOWN;
    case SDLK_LEFT: return BTN_LEFT;
    case SDLK_RIGHT: return BTN_RIGHT;
    case SDLK_RETURN: return BTN_START;
    case SDLK_BACKSPACE: return BTN_SELECT;
    default: return 0;
  }
}
}  // namespace

bool init() {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) return false;
  if (const char* s = getenv("STS_SCALE")) scale = atoi(s) > 0 ? atoi(s) : 2;
  screenshotPath = getenv("STS_SCREENSHOT");
  if (const char* f = getenv("STS_SCREENSHOT_FRAME")) screenshotAfter = atoi(f);
  win = SDL_CreateWindow("StS2 3DS preview", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kTopW * scale,
                         kScreenH * 2 * scale, getenv("STS_HIDDEN") ? SDL_WINDOW_HIDDEN : 0);
  if (!win) return false;
  ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  if (!ren) return false;
  SDL_RenderSetScale(ren, (float)scale, (float)scale);
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  lastTicks = SDL_GetPerformanceCounter();
  parseScript();
  return true;
}

void shutdown() {
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
}

bool running() { return !quit; }
double dt() { return frameDt; }

Input input() {
  uint32_t prevHeld = cur.held;
  bool prevTouch = cur.touching;
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_QUIT) quit = true;
    if (e.type == SDL_KEYDOWN && !e.key.repeat) {
      if (e.key.keysym.sym == SDLK_ESCAPE) quit = true;
      cur.held |= mapKey(e.key.keysym.sym);
    }
    if (e.type == SDL_KEYUP) cur.held &= ~mapKey(e.key.keysym.sym);
  }
  int mx, my;
  Uint32 mb = SDL_GetMouseState(&mx, &my);
  mx /= scale;
  my /= scale;
  int bx = mx - (kTopW - kBottomW) / 2, by = my - kScreenH;
  bool inside = bx >= 0 && bx < kBottomW && by >= 0 && by < kScreenH;
  bool touch = ((mb & SDL_BUTTON_LMASK) && (inside || prevTouch)) || scriptHeld;
  cur.touching = touch;
  if (scriptHeld) { cur.tx = heldX; cur.ty = heldY; }
  else if (touch) {
    cur.tx = bx < 0 ? 0 : bx >= kBottomW ? kBottomW - 1 : bx;
    cur.ty = by < 0 ? 0 : by >= kScreenH ? kScreenH - 1 : by;
  }
  cur.touchDown = touch && !prevTouch;
  cur.touchUp = !touch && prevTouch;
  cur.down = cur.held & ~prevHeld;
  cur.up = prevHeld & ~cur.held;
  for (auto& sc : script) {
    if (sc.frame == frameCount) {
      cur.down |= sc.btn;
      if (sc.kind == 'T' && sc.tx >= 0) { cur.touchDown = true; cur.touching = true; cur.tx = sc.tx; cur.ty = sc.ty; }
      if (sc.kind == 'P') { cur.touchDown = !prevTouch; scriptHeld = true; }
      if (sc.kind == 'P' || (sc.kind == 'M' && scriptHeld)) {
        cur.touching = true; cur.tx = heldX = sc.tx; cur.ty = heldY = sc.ty;
      }
      if (sc.kind == 'U' && scriptHeld) { scriptHeld = false; cur.touching = false; cur.touchUp = true; }
    }
    if (sc.frame == frameCount - 1 && sc.kind == 'T' && sc.tx >= 0) { cur.touching = false; cur.touchUp = true; }
  }
  return cur;
}

void beginFrame() {
  Uint64 now = SDL_GetPerformanceCounter();
  frameDt = (double)(now - lastTicks) / SDL_GetPerformanceFrequency();
  if (frameDt > 0.1) frameDt = 0.1;
  if (fixedStep) frameDt = 1.0 / 60;
  lastTicks = now;
  SDL_RenderSetClipRect(ren, nullptr);
  SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
  SDL_RenderClear(ren);
}

void screen(Screen s, uint32_t clear) {
  ox = s == TOP ? 0.f : (float)(kTopW - kBottomW) / 2;
  oy = s == TOP ? 0.f : (float)kScreenH;
  SDL_Rect clip{(int)ox, (int)oy, s == TOP ? kTopW : kBottomW, kScreenH};
  SDL_RenderSetClipRect(ren, &clip);
  SDL_Color c = col(clear);
  SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 255);
  SDL_RenderFillRect(ren, &clip);
}

void endFrame() {
  ++frameCount;
  std::string shotPath;
  if (screenshotPath && frameCount == screenshotAfter) shotPath = screenshotPath;
  for (auto& sh : shots) if (sh.first == frameCount) shotPath = sh.second;
  if (!shotPath.empty()) {
    int w, h;
    SDL_GetRendererOutputSize(ren, &w, &h);
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
    SDL_RenderSetClipRect(ren, nullptr);
    SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ABGR8888, surf->pixels, surf->pitch);
    SDL_SaveBMP(surf, shotPath.c_str());
    SDL_FreeSurface(surf);
  }
  SDL_RenderPresent(ren);
}

namespace detail {
Texture* whiteTexture() {
  static Texture* white = nullptr;
  if (!white) {
    uint32_t px[16];
    for (auto& p : px) p = 0xFFFFFFFF;
    SDL_Texture* t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, 4, 4);
    SDL_UpdateTexture(t, nullptr, px, 16);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    white = new Texture{t, 4, 4};
  }
  return white;
}
}  // namespace detail

bool readFile(const std::string& path, std::string& out) {
  std::ifstream f("romfs/" + path, std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

Texture* loadTexture(const std::string& path) {
  std::string data;
  if (!readFile(path, data) || data.size() < 12 || memcmp(data.data(), "T3T1", 4) != 0) {
    fprintf(stderr, "cannot load texture %s\n", path.c_str());
    return nullptr;
  }
  uint16_t w, h;
  memcpy(&w, data.data() + 4, 2);
  memcpy(&h, data.data() + 6, 2);
  SDL_Texture* t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
  SDL_UpdateTexture(t, nullptr, data.data() + 12, w * 4);
  SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
  return new Texture{t, w, h};
}

int texWidth(Texture* t) { return t ? t->w : 0; }
int texHeight(Texture* t) { return t ? t->h : 0; }


// Vertex colour that reproduces image()'s tint/blend on the triangle path.
static uint32_t tintColor(uint32_t tint, float blend) {
  auto mix = [&](int sh) { int v = (tint >> sh) & 255; return (uint32_t)(255 + (v - 255) * blend + 0.5f) & 255; };
  return (mix(24) << 24) | (mix(16) << 16) | (mix(8) << 8) | (tint & 255);
}

void image(Texture* t, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh, uint32_t tint,
           float blend) {
  if (!t) return;
  if (detail::transformed()) { detail::texQuad(t, sx, sy, sw, sh, dx, dy, dw, dh, tintColor(tint, blend)); return; }
  SDL_Color c = col(tint);
  auto mix = [&](Uint8 v) { return Uint8(255 + (v - 255) * blend); };
  SDL_SetTextureColorMod(t->tex, mix(c.r), mix(c.g), mix(c.b));
  SDL_SetTextureAlphaMod(t->tex, c.a);
  SDL_Rect src{(int)sx, (int)sy, (int)sw, (int)sh};
  SDL_FRect dst{dx + ox, dy + oy, dw, dh};
  SDL_RenderCopyF(ren, t->tex, &src, &dst);
}

void rect(float x, float y, float w, float h, uint32_t rgba) {
  if (detail::transformed()) { detail::texQuad(detail::whiteTexture(), 0, 0, 1, 1, x, y, w, h, rgba); return; }
  SDL_Color c = col(rgba);
  SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
  SDL_FRect r{x + ox, y + oy, w, h};
  SDL_RenderFillRectF(ren, &r);
}

void rectGradient(float x, float y, float w, float h, uint32_t top, uint32_t bottom) {
  SDL_Color a = col(top), b = col(bottom);
  SDL_Vertex v[4] = {
      {{x + ox, y + oy}, a, {0, 0}}, {{x + ox + w, y + oy}, a, {0, 0}},
      {{x + ox + w, y + oy + h}, b, {0, 0}}, {{x + ox, y + oy + h}, b, {0, 0}}};
  int idx[6] = {0, 1, 2, 0, 2, 3};
  SDL_RenderGeometry(ren, nullptr, v, 4, idx, 6);
}

void line(float x0, float y0, float x1, float y1, float th, uint32_t rgba) {
  float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
  if (len < 0.001f) return;
  float nx = -dy / len * th / 2, ny = dx / len * th / 2;
  SDL_Color c = col(rgba);
  SDL_Vertex v[4] = {{{x0 + nx + ox, y0 + ny + oy}, c, {0, 0}}, {{x1 + nx + ox, y1 + ny + oy}, c, {0, 0}},
                     {{x1 - nx + ox, y1 - ny + oy}, c, {0, 0}}, {{x0 - nx + ox, y0 - ny + oy}, c, {0, 0}}};
  int idx[6] = {0, 1, 2, 0, 2, 3};
  SDL_RenderGeometry(ren, nullptr, v, 4, idx, 6);
}

void circle(float x, float y, float r, uint32_t rgba) {
  constexpr int N = 24;
  SDL_Color c = col(rgba);
  std::vector<SDL_Vertex> v;
  std::vector<int> idx;
  v.push_back({{x + ox, y + oy}, c, {0, 0}});
  for (int i = 0; i <= N; ++i) {
    float a = (float)i / N * 6.2831853f;
    v.push_back({{x + ox + std::cos(a) * r, y + oy + std::sin(a) * r}, c, {0, 0}});
    if (i > 0) { idx.push_back(0); idx.push_back(i); idx.push_back(i + 1); }
  }
  SDL_RenderGeometry(ren, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

void triangles(Texture* t, const Vert* verts, int count, const uint16_t* indices, int indexCount, bool additive) {
  if (!t || count == 0) return;
  static std::vector<SDL_Vertex> v;
  static std::vector<int> idx;
  v.resize(count);
  for (int i = 0; i < count; ++i) {
    const Vert& s = verts[i];
    v[i].position = {s.x + ox, s.y + oy};
    v[i].color = col(s.rgba);
    v[i].tex_coord = {s.u, s.v};
  }
  idx.assign(indices, indices + indexCount);
  SDL_SetTextureColorMod(t->tex, 255, 255, 255);
  SDL_SetTextureAlphaMod(t->tex, 255);
  SDL_SetTextureBlendMode(t->tex, additive ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
  SDL_RenderGeometry(ren, t->tex, v.data(), count, idx.data(), indexCount);
  SDL_SetTextureBlendMode(t->tex, SDL_BLENDMODE_BLEND);
}

}  // namespace gfx
