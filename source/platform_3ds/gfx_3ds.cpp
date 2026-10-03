// 3DS backend on citro2d. Textures are T3T1 files (linear RGBA) swizzled into
// the GPU's 8x8 Morton tiles at load time.
#include <3ds.h>
#include <citro2d.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../core/safe_file.h"
#include "../gfx/gfx.h"
#include "mesh_shbin.h"

namespace gfx {

struct Texture {
  C3D_Tex tex;
  int w, h;
};

namespace {
C3D_RenderTarget* targets[2];
Screen current = TOP;
u64 lastMs;
double frameDt = 1.0 / 60;
Input cur;

int frameCount = 0;

// Debug: sdmc:/sts2-debug.txt holds KEY=VALUE lines that become environment
// variables (STS_AUTOPLAY, STS_SEED, STS_SHOTS="frame:name,...").
// Shots are written to sdmc:/sts2-shots/<name>.bmp.
std::vector<std::pair<int, std::string>> shots;

// STS_SCRIPT, same syntax as the SDL preview: "30:A,60:T160x120,90:P160x200,95:M160x150,100:U",
// "110:L+R" presses both in one frame.
struct Scripted { int frame; uint32_t btn; int tx, ty; char kind; };
std::vector<Scripted> script;
bool scriptHeld = false;
int heldX = 0, heldY = 0;

// Button names for STS_SCRIPT; "L+R" (any names joined by '+') presses them in the same frame.
uint32_t scriptButtons(const std::string& keys) {
  uint32_t m = 0;
  size_t p = 0;
  while (p <= keys.size()) {
    size_t e = keys.find('+', p);
    if (e == std::string::npos) e = keys.size();
    std::string k = keys.substr(p, e - p);
    p = e + 1;
    if (k == "A") m |= BTN_A; else if (k == "B") m |= BTN_B; else if (k == "X") m |= BTN_X;
    else if (k == "Y") m |= BTN_Y; else if (k == "L") m |= BTN_L; else if (k == "R") m |= BTN_R;
    else if (k == "LEFT") m |= BTN_LEFT; else if (k == "RIGHT") m |= BTN_RIGHT;
    else if (k == "UP") m |= BTN_UP; else if (k == "DOWN") m |= BTN_DOWN;
    else if (k == "START") m |= BTN_START; else if (k == "SELECT") m |= BTN_SELECT;
    else if (k == "ZL") m |= BTN_ZL; else if (k == "ZR") m |= BTN_ZR;
  }
  return m;
}

void parseScript(const char* s) {
  std::string str(s);
  size_t p = 0;
  while (p < str.size()) {
    size_t e = str.find(',', p);
    if (e == std::string::npos) e = str.size();
    std::string item = str.substr(p, e - p);
    p = e + 1;
    size_t c = item.find(':');
    if (c == std::string::npos) continue;
    Scripted sc{atoi(item.c_str()), 0, -1, -1, 'B'};
    std::string k = item.substr(c + 1);
    if (k[0] == 'T' || k[0] == 'P' || k[0] == 'M') { sc.kind = k[0]; sscanf(k.c_str() + 1, "%dx%d", &sc.tx, &sc.ty); }
    else if (k == "U") sc.kind = 'U';
    else sc.btn = scriptButtons(k);
    script.push_back(sc);
  }
}

void loadDebugConfig() {
  FILE* f = fopen("sdmc:/sts2-debug.txt", "r");
  if (!f) return;
  char line[512];
  while (fgets(line, sizeof line, f)) {
    std::string l(line);
    while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
    size_t eq = l.find('=');
    if (eq != std::string::npos) setenv(l.substr(0, eq).c_str(), l.substr(eq + 1).c_str(), 1);
  }
  fclose(f);
  if (const char* s = getenv("STS_SCRIPT")) parseScript(s);
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

// Framebuffers are BGR8, rotated: stored as 240-pixel columns.
void writeShot(const std::string& name) {
  mkdir("sdmc:/sts2-shots", 0777);
  const int W = 400, H = 480;
  std::vector<u8> img(W * H * 3, 0);
  for (int scr = 0; scr < 2; ++scr) {
    u16 fw, fh;
    u8* fb = gfxGetFramebuffer(scr == 0 ? GFX_TOP : GFX_BOTTOM, GFX_LEFT, &fw, &fh);
    int sw = scr == 0 ? 400 : 320, ox = scr == 0 ? 0 : 40, oy = scr == 0 ? 0 : 240;
    for (int y = 0; y < 240; ++y)
      for (int x = 0; x < sw; ++x) {
        const u8* px = fb + (x * 240 + (239 - y)) * 3;
        u8* d = &img[((H - 1 - (oy + y)) * W + ox + x) * 3];  // BMP rows go bottom-up
        d[0] = px[0]; d[1] = px[1]; d[2] = px[2];
      }
  }
  FILE* f = fopen(("sdmc:/sts2-shots/" + name + ".bmp").c_str(), "wb");
  if (!f) return;
  u32 dataSize = W * H * 3, fileSize = 54 + dataSize;
  u8 hdr[54] = {'B', 'M'};
  memcpy(hdr + 2, &fileSize, 4);
  u32 off = 54, dib = 40, planesBpp = 1 | (24 << 16);
  int32_t w = W, h = H;
  memcpy(hdr + 10, &off, 4);
  memcpy(hdr + 14, &dib, 4);
  memcpy(hdr + 18, &w, 4);
  memcpy(hdr + 22, &h, 4);
  memcpy(hdr + 26, &planesBpp, 4);
  memcpy(hdr + 34, &dataSize, 4);
  fwrite(hdr, 1, 54, f);
  fwrite(img.data(), 1, dataSize, f);
  fclose(f);
}

// Spine meshes go through citro3d with our own shader; see triangles().
struct MeshVert { float x, y, u, v; u8 r, g, b, a; };
DVLB_s* meshDvlb;
shaderProgram_s meshProg;
int meshProjLoc;
C3D_AttrInfo meshAttr;
constexpr int kMeshVerts = 48000, kMeshIndices = 96000;
MeshVert* meshVerts;
u16* meshIndices;
int meshVertUsed, meshIndexUsed;
bool frameOpen = false;  // between beginFrame and endFrame (textInput closes it around the applet)

void initMesh() {
  meshDvlb = DVLB_ParseFile((u32*)mesh_shbin, mesh_shbin_size);
  shaderProgramInit(&meshProg);
  shaderProgramSetVsh(&meshProg, &meshDvlb->DVLE[0]);
  meshProjLoc = shaderInstanceGetUniformLocation(meshProg.vertexShader, "projection");
  AttrInfo_Init(&meshAttr);
  AttrInfo_AddLoader(&meshAttr, 0, GPU_FLOAT, 2);
  AttrInfo_AddLoader(&meshAttr, 1, GPU_FLOAT, 2);
  AttrInfo_AddLoader(&meshAttr, 2, GPU_UNSIGNED_BYTE, 4);
  meshVerts = (MeshVert*)linearAlloc(sizeof(MeshVert) * kMeshVerts);
  meshIndices = (u16*)linearAlloc(sizeof(u16) * kMeshIndices);
}

u32 c2d(uint32_t rgba) { return C2D_Color32(rgba >> 24, (rgba >> 16) & 255, (rgba >> 8) & 255, rgba & 255); }

// Offset of (x, y) inside an 8x8 tile, bits interleaved x0 y0 x1 y1 x2 y2.
inline u32 morton8(u32 x, u32 y) {
  return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
}
// ---- Y5: HOME menu / sleep (APT hooks)
// libctru calls these from aptMainLoop (HOME menu) or its APT thread (sleep); the game loop is
// blocked meanwhile. Suspend and sleep can overlap (the lid closed while in the HOME menu), so
// the game counts as paused while either lasts, and the callback only sees the transitions.
aptHookCookie aptCookie;
void (*pauseFn)(bool) = nullptr;
volatile bool inHome = false, asleep = false, pausedNow = false;
volatile bool resumed = false;  // the next beginFrame restarts the frame clock

void applyPause() {
  bool p = inHome || asleep;
  if (p == pausedNow) return;
  pausedNow = p;
  if (!p) resumed = true;
  if (pauseFn) pauseFn(p);
}

void onApt(APT_HookType hook, void*) {
  switch (hook) {
    case APTHOOK_ONSUSPEND: inHome = true; break;
    case APTHOOK_ONRESTORE: inHome = false; break;
    case APTHOOK_ONSLEEP: asleep = true; break;
    case APTHOOK_ONWAKEUP: asleep = false; break;
    case APTHOOK_ONEXIT: inHome = true; break;  // closing from the HOME menu: stay silent
    default: break;
  }
  applyPause();
}

// Saves are written synchronously on the main thread, so the HOME button (handled in
// aptMainLoop, same thread) can never interrupt one; sleep requests arrive on APT's thread, so
// they are refused while a save is being written. Re-allowing sleep with the lid already closed
// makes libctru put the system to sleep right then (APT_SleepIfShellClosed).
void saveGuard(bool writing) { aptSetSleepAllowed(!writing); }
}  // namespace

void flushMesh();

void onSystemPause(void (*fn)(bool)) { pauseFn = fn; }

bool init() {
  romfsInit();
  gfxInitDefault();
  osSetSpeedupEnable(true);  // New 3DS: 804 MHz + L2 cache
  C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
  C2D_Init(8192);
  C2D_Prepare();
  targets[TOP] = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
  targets[BOTTOM] = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
  lastMs = osGetTime();
  initMesh();
  loadDebugConfig();
  // Y5: closing the lid sleeps, HOME opens the HOME menu (both pause audio and the run timer via
  // onSystemPause); HOME -> Close makes aptMainLoop (running()) return false and main() exit
  // without writing anything: run.sav only changes at a map save point.
  aptSetSleepAllowed(true);
  aptSetHomeAllowed(true);
  aptHook(&aptCookie, onApt, nullptr);
  sts::safefile::setWriteGuard(saveGuard);
  return true;
}

void shutdown() {
  sts::safefile::setWriteGuard(nullptr);
  aptUnhook(&aptCookie);
  C2D_Fini();
  C3D_Fini();
  gfxExit();
  romfsExit();
}

bool running() { return aptMainLoop(); }
double dt() { return frameDt; }

// One reading per frame: main.cpp reads it before App::update, and screens that draw kit widgets
// read it again while drawing. A second scan would report no new presses or taps, so later
// calls in the same frame return the same Input (as scripted input always behaved).
static int inputFrame = -1;

Input input() {
  if (inputFrame == frameCount) return cur;
  inputFrame = frameCount;
  hidScanInput();
  bool prevTouch = cur.touching;
  cur.down = hidKeysDown();
  cur.held = hidKeysHeld();
  cur.up = hidKeysUp();
  cur.touching = (cur.held & KEY_TOUCH) != 0;
  if (cur.touching) {
    touchPosition tp;
    hidTouchRead(&tp);
    cur.tx = tp.px;
    cur.ty = tp.py;
  }
  if (scriptHeld) { cur.touching = true; cur.tx = heldX; cur.ty = heldY; }
  cur.touchDown = cur.touching && !prevTouch;
  cur.touchUp = !cur.touching && prevTouch;
  for (auto& sc : script) {
    if (sc.frame == frameCount) {
      cur.down |= sc.btn;
      if (sc.kind == 'T') { cur.touchDown = true; cur.touching = true; cur.tx = sc.tx; cur.ty = sc.ty; }
      if (sc.kind == 'P') { cur.touchDown = !prevTouch; scriptHeld = true; }
      if (sc.kind == 'P' || (sc.kind == 'M' && scriptHeld)) { cur.touching = true; cur.tx = heldX = sc.tx; cur.ty = heldY = sc.ty; }
      if (sc.kind == 'U' && scriptHeld) { scriptHeld = false; cur.touching = false; cur.touchUp = true; }
    }
    if (sc.frame == frameCount - 1 && sc.kind == 'T') { cur.touching = false; cur.touchUp = true; }
  }
  // Keep only the buttons gfx.h defines.
  cur.down &= 0xFFF | BTN_ZL | BTN_ZR;
  cur.held &= 0xFFF | BTN_ZL | BTN_ZR;
  cur.up &= 0xFFF | BTN_ZL | BTN_ZR;
  return cur;
}

void consumeButtons(uint32_t mask) {
  input();  // make sure this frame's reading exists
  cur.down &= ~mask;
}

void beginFrame() {
  u64 now = osGetTime();
  if (resumed) {  // Y5: back from the HOME menu / sleep: that time is not game time
    resumed = false;
    lastMs = now - 1000 / 60;
  }
  frameDt = (now - lastMs) / 1000.0;
  if (frameDt > 0.1) frameDt = 0.1;
  lastMs = now;
  C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
  meshVertUsed = meshIndexUsed = 0;  // the GPU finished last frame's meshes
  frameOpen = true;
}

void screen(Screen s, uint32_t clear) {
  flushMesh();
  detail::resetState();
  current = s;
  C2D_TargetClear(targets[s], c2d(clear | 0xFF));
  C2D_SceneBegin(targets[s]);
}

void endFrame() {
  flushMesh();
  C3D_FrameEnd(0);
  frameOpen = false;
  ++frameCount;
  for (auto& sh : shots) {
    if (sh.first != frameCount) continue;
    gspWaitForVBlank();
    gspWaitForVBlank();
    writeShot(sh.second);
  }
}

namespace {
std::string saveDir() { return "sdmc:/3ds/sts2-3ds/"; }
}  // namespace

// ---------------------------------------------------------------- text input (S03)

namespace {
// Cut to at most maxBytes bytes without splitting a UTF-8 character.
std::string clampUtf8(std::string s, int maxBytes) {
  if ((int)s.size() <= maxBytes) return s;
  size_t cut = maxBytes;
  while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) --cut;
  s.resize(cut);
  return s;
}
}  // namespace

bool textInput(const char* hint, const std::string& initial, std::string& out, int maxBytes) {
  if (const char* env = getenv("STS_TEXT_INPUT")) { out = clampUtf8(env, maxBytes); return true; }
  // swkbd counts UTF-16 units; 3 bytes each covers CJK, so a full-length name always fits.
  SwkbdState kb;
  swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, maxBytes / 3 > 0 ? maxBytes / 3 : 1);
  swkbdSetHintText(&kb, hint);
  swkbdSetInitialText(&kb, initial.c_str());
  swkbdSetValidation(&kb, SWKBD_ANYTHING, 0, 0);
  std::vector<char> buf(maxBytes + 16, 0);
  // The applet takes over the GPU and the screens: close the (still empty) frame App::update runs
  // in, and reopen it afterwards so the caller's draw pass proceeds as usual.
  const bool reopen = frameOpen;
  if (reopen) { flushMesh(); C3D_FrameEnd(0); frameOpen = false; }
  SwkbdButton b = swkbdInputText(&kb, buf.data(), buf.size());
  if (reopen) {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    meshVertUsed = meshIndexUsed = 0;
    frameOpen = true;
  }
  lastMs = osGetTime();  // the keyboard's time is not game time
  if (b != SWKBD_BUTTON_CONFIRM) return false;
  out = clampUtf8(buf.data(), maxBytes);
  return true;
}

// ---------------------------------------------------------------- saves

namespace {
bool readWhole(const std::string& path, std::string& out) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  out.resize(n > 0 ? (size_t)n : 0);
  size_t got = n > 0 ? fread(&out[0], 1, (size_t)n, f) : 0;
  fclose(f);
  return got == out.size();
}
}  // namespace

bool readSave(const std::string& name, std::string& out) { return readWhole(saveDir() + name, out); }

bool writeSave(const std::string& name, const std::string& data) {
  // Y5: atomic tmp -> (bak) -> rename (core/safe_file.h); per-profile names like
  // "profile1/run.sav" get their subdirectory created there.
  return sts::safefile::writeAtomic(saveDir() + name, data);
}

void deleteSave(const std::string& name) { sts::safefile::removeAll(saveDir() + name); }

bool readFile(const std::string& path, std::string& out) {
  FILE* f = fopen(("romfs:/" + path).c_str(), "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  out.resize(n);
  size_t got = fread(&out[0], 1, n, f);
  fclose(f);
  return got == (size_t)n;
}

namespace {
// tex3ds compression header (0x00 raw copy, 0x11 LZ11) followed by the payload. Decodes straight
// into `out` (texture memory) and returns false unless exactly `outSize` bytes come out.
bool unpackGpuData(const u8* in, size_t inSize, u8* out, size_t outSize) {
  if (inSize < 4) return false;
  size_t size = in[1] | (in[2] << 8) | (in[3] << 16), pos = 4;
  if (in[0] & 0x80) {
    if (inSize < 8) return false;
    size = in[4] | (in[5] << 8) | (in[6] << 16) | ((size_t)in[7] << 24);
    pos = 8;
  }
  if (size != outSize) return false;
  if (in[0] == 0x00) {
    if (inSize - pos < size) return false;
    memcpy(out, in + pos, size);
    return true;
  }
  if ((in[0] & 0x7F) != 0x11) return false;
  size_t o = 0;
  while (o < size) {
    if (pos >= inSize) return false;
    u8 flags = in[pos++];
    for (int bit = 0; bit < 8 && o < size; ++bit, flags <<= 1) {
      if (!(flags & 0x80)) {
        if (pos >= inSize) return false;
        out[o++] = in[pos++];
        continue;
      }
      if (pos + 2 > inSize) return false;
      u32 b0 = in[pos++], b1 = in[pos++], len, disp;
      if ((b0 >> 4) == 0) {
        if (pos >= inSize) return false;
        len = (((b0 & 0xF) << 4) | (b1 >> 4)) + 0x11;
        disp = (((b1 & 0xF) << 8) | in[pos++]) + 1;
      } else if ((b0 >> 4) == 1) {
        if (pos + 2 > inSize) return false;
        u32 b2 = in[pos++], b3 = in[pos++];
        len = (((b0 & 0xF) << 12) | (b1 << 4) | (b2 >> 4)) + 0x111;
        disp = (((b2 & 0xF) << 8) | b3) + 1;
      } else {
        len = (b0 >> 4) + 1;
        disp = (((b0 & 0xF) << 8) | b1) + 1;
      }
      if (disp > o || o + len > size) return false;
      for (u32 i = 0; i < len; ++i, ++o) out[o] = out[o - disp];
    }
  }
  return true;
}
}  // namespace

// T3T1 fmt byte: 0 = RGBA8 (linear, swizzled here), 1 = ETC1A4, 2 = ETC1, 3 = RGBA4444 (tools/compress_romfs.py;
// the GPU formats are stored ready to upload).
Texture* loadTexture(const std::string& path) {
  std::string data;
  if (!readFile(path, data) || data.size() < 12 || memcmp(data.data(), "T3T1", 4) != 0) return nullptr;
  u16 w, h;
  memcpy(&w, data.data() + 4, 2);
  memcpy(&h, data.data() + 6, 2);
  u8 fmt = (u8)data[8];
  auto* t = new Texture{};
  t->w = w;
  t->h = h;
  static const GPU_TEXCOLOR kFormats[] = {GPU_RGBA8, GPU_ETC1A4, GPU_ETC1, GPU_RGBA4};
  if (fmt > 3 || !C3D_TexInit(&t->tex, w, h, kFormats[fmt])) {
    delete t;
    return nullptr;
  }
  const u8* src = (const u8*)data.data() + 12;
  if (fmt != 0) {
    if (!unpackGpuData(src, data.size() - 12, (u8*)t->tex.data, t->tex.size)) {
      C3D_TexDelete(&t->tex);
      delete t;
      return nullptr;
    }
    C3D_TexFlush(&t->tex);
    C3D_TexSetFilter(&t->tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&t->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    return t;
  }
  u32* dst = (u32*)t->tex.data;
  for (u32 y = 0; y < h; ++y) {
    u32 ty = y;  // tile rows follow image rows; the subtexture v coordinates do the flip
    for (u32 x = 0; x < w; ++x) {
      const u8* p = src + (y * w + x) * 4;
      u32 off = (((ty >> 3) * (w >> 3) + (x >> 3)) << 6) + morton8(x & 7, ty & 7);
      dst[off] = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
    }
  }
  C3D_TexFlush(&t->tex);
  C3D_TexSetFilter(&t->tex, GPU_LINEAR, GPU_LINEAR);
  C3D_TexSetWrap(&t->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
  return t;
}

// Called from App::update, after C3D_FrameBegin(SYNCDRAW) waited for the previous frame.
void freeTexture(Texture* t) {
  if (!t) return;
  C3D_TexDelete(&t->tex);
  delete t;
}

int texWidth(Texture* t) { return t ? t->w : 0; }
int texHeight(Texture* t) { return t ? t->h : 0; }


// Tint blend for the triangles queued next (rawImage sets it around texQuad; 0 = plain modulate).
static float meshMix = 0.f;

// Vertex colour that reproduces image()'s tint/blend on the triangle path.
static uint32_t tintColor(uint32_t tint, float blend) {
  auto mix = [&](int sh) { int v = (tint >> sh) & 255; return (uint32_t)(255 + (v - 255) * blend + 0.5f) & 255; };
  return (mix(24) << 24) | (mix(16) << 16) | (mix(8) << 8) | (tint & 255);
}

void detail::rawImage(Texture* t, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh, uint32_t tint,
           float blend) {
  if (!t) return;
  // A tint with blend > 0 goes through our mesh with an INTERPOLATE stage (flushMesh) instead of
  // citro2d: citro2d carries the blend factor in a procedural-texture unit, which Azahar renders as
  // blend 0 (the grey HP-bar art showed untinted, silver). The mesh also does the mix on the
  // transformed path, where vertex-colour modulation alone could not.
  if (blend > 0.f) {
    meshMix = std::min(blend, 1.f);
    detail::texQuad(t, sx, sy, sw, sh, dx, dy, dw, dh, tint);
    meshMix = 0.f;
    return;
  }
  if (detail::transformed()) { detail::texQuad(t, sx, sy, sw, sh, dx, dy, dw, dh, tintColor(tint, blend)); return; }
  flushMesh();
  Tex3DS_SubTexture sub;
  sub.width = (u16)sw;
  sub.height = (u16)sh;
  sub.left = sx / t->w;
  sub.right = (sx + sw) / t->w;
  sub.top = 1.f - sy / t->h;
  sub.bottom = 1.f - (sy + sh) / t->h;
  C2D_Image img{&t->tex, &sub};
  C2D_ImageTint it;
  C2D_PlainImageTint(&it, c2d(tint), blend);
  C2D_DrawImageAt(img, dx, dy, 0.5f, &it, dw / sw, dh / sh);
}

void detail::rawRect(float x, float y, float w, float h, uint32_t rgba) {
  if (detail::transformed()) { detail::texQuad(detail::whiteTexture(), 0, 0, 1, 1, x, y, w, h, rgba); return; }
  flushMesh();
  C2D_DrawRectSolid(x, y, 0.5f, w, h, c2d(rgba));
}

void detail::rawLine(float x0, float y0, float x1, float y1, float th, uint32_t rgba) {
  flushMesh();
  C2D_DrawLine(x0, y0, c2d(rgba), x1, y1, c2d(rgba), th, 0.5f);
}

void detail::rawCircle(float x, float y, float r, uint32_t rgba) {
  flushMesh();
  C2D_DrawCircleSolid(x, y, 0.5f, r, c2d(rgba));
}

namespace detail {
Texture* whiteTexture() {
  static Texture* white = nullptr;
  if (!white) {
    white = new Texture{};
    white->w = white->h = 8;
    C3D_TexInit(&white->tex, 8, 8, GPU_RGBA8);
    u32* p = (u32*)white->tex.data;
    for (int i = 0; i < 64; ++i) p[i] = 0xFFFFFFFF;
    C3D_TexFlush(&white->tex);
  }
  return white;
}
}  // namespace detail

// Pending mesh batch: consecutive triangles() calls with the same texture and
// blend are drawn with one citro3d call; any citro2d draw flushes it first.
static struct { Texture* tex = nullptr; bool additive = false; float mix = 0.f; int vStart = 0, iStart = 0, iCount = 0; } batch;

void flushMesh() {
  if (!batch.tex || batch.iCount == 0) { batch.tex = nullptr; return; }
  C2D_Flush();
  C3D_BindProgram(&meshProg);
  C3D_SetAttrInfo(&meshAttr);
  C3D_Mtx proj;
  Mtx_OrthoTilt(&proj, 0.f, current == TOP ? 400.f : 320.f, 240.f, 0.f, 0.f, 1.f, true);
  C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, meshProjLoc, &proj);
  C3D_BufInfo* bi = C3D_GetBufInfo();
  BufInfo_Init(bi);
  BufInfo_Add(bi, meshVerts + batch.vStart, sizeof(MeshVert), 3, 0x210);
  C3D_TexBind(0, &batch.tex->tex);
  C3D_TexEnv* env = C3D_GetTexEnv(0);
  C3D_TexEnvInit(env);
  if (batch.mix > 0.f) {  // tinted image: rgb = mix(texture, vertex colour, blend), alpha modulated
    const u32 b = (u32)(batch.mix * 255.f + 0.5f);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_PRIMARY_COLOR, GPU_TEXTURE0, GPU_CONSTANT);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);
    C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);
    C3D_TexEnvColor(env, b | b << 8 | b << 16 | b << 24);
  } else {
    C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
  }
  for (int i = 1; i < 6; ++i) C3D_TexEnvInit(C3D_GetTexEnv(i));
  C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
  C3D_CullFace(GPU_CULL_NONE);
  if (batch.additive)
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_SRC_ALPHA, GPU_ONE);
  else
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA,
                   GPU_ONE_MINUS_SRC_ALPHA);
  C3D_DrawElements(GPU_TRIANGLES, batch.iCount, C3D_UNSIGNED_SHORT, meshIndices + batch.iStart);
  // citro2d does not reset blending in C2D_Prepare; leave it as citro2d expects.
  C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA,
                 GPU_ONE_MINUS_SRC_ALPHA);
  C2D_Prepare();  // hand the rest of the GPU state back to citro2d
  batch.tex = nullptr;
  batch.iCount = 0;
}

void detail::rawTriangles(Texture* t, const Vert* verts, int count, const uint16_t* indices, int indexCount, bool additive) {
  if (!t || count == 0 || indexCount == 0) return;
  if (batch.tex && (batch.tex != t || batch.additive != additive || batch.mix != meshMix || meshVertUsed - batch.vStart + count > 65000)) flushMesh();
  if (meshVertUsed + count > kMeshVerts || meshIndexUsed + indexCount > kMeshIndices) return;
  if (!batch.tex) {
    batch.tex = t;
    batch.additive = additive;
    batch.mix = meshMix;
    batch.vStart = meshVertUsed;
    batch.iStart = meshIndexUsed;
    batch.iCount = 0;
  }
  MeshVert* vb = meshVerts + meshVertUsed;
  u16* ib = meshIndices + meshIndexUsed;
  u16 base = (u16)(meshVertUsed - batch.vStart);
  for (int i = 0; i < count; ++i) {
    const Vert& s = verts[i];
    vb[i] = {s.x, s.y, s.u, 1.f - s.v,  // GPU v runs bottom-up
             (u8)(s.rgba >> 24), (u8)(s.rgba >> 16), (u8)(s.rgba >> 8), (u8)s.rgba};
  }
  for (int i = 0; i < indexCount; ++i) ib[i] = (u16)(indices[i] + base);
  meshVertUsed += count;
  meshIndexUsed += indexCount;
  batch.iCount += indexCount;
}

}  // namespace gfx
