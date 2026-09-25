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

// STS_SCRIPT, same syntax as the SDL preview: "30:A,60:T160x120,90:P160x200,95:M160x150,100:U".
struct Scripted { int frame; uint32_t btn; int tx, ty; char kind; };
std::vector<Scripted> script;
bool scriptHeld = false;
int heldX = 0, heldY = 0;

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
    else if (k == "A") sc.btn = BTN_A; else if (k == "B") sc.btn = BTN_B; else if (k == "X") sc.btn = BTN_X;
    else if (k == "Y") sc.btn = BTN_Y; else if (k == "L") sc.btn = BTN_L; else if (k == "R") sc.btn = BTN_R;
    else if (k == "LEFT") sc.btn = BTN_LEFT; else if (k == "RIGHT") sc.btn = BTN_RIGHT;
    else if (k == "UP") sc.btn = BTN_UP; else if (k == "DOWN") sc.btn = BTN_DOWN;
    else if (k == "START") sc.btn = BTN_START;
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
}  // namespace

void flushMesh();

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
  return true;
}

void shutdown() {
  C2D_Fini();
  C3D_Fini();
  gfxExit();
  romfsExit();
}

bool running() { return aptMainLoop(); }
double dt() { return frameDt; }

Input input() {
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
  cur.down &= 0xFFF;
  cur.held &= 0xFFF;
  cur.up &= 0xFFF;
  return cur;
}

void beginFrame() {
  u64 now = osGetTime();
  frameDt = (now - lastMs) / 1000.0;
  if (frameDt > 0.1) frameDt = 0.1;
  lastMs = now;
  C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
  meshVertUsed = meshIndexUsed = 0;  // the GPU finished last frame's meshes
}

void screen(Screen s, uint32_t clear) {
  flushMesh();
  current = s;
  C2D_TargetClear(targets[s], c2d(clear | 0xFF));
  C2D_SceneBegin(targets[s]);
}

void endFrame() {
  flushMesh();
  C3D_FrameEnd(0);
  ++frameCount;
  for (auto& sh : shots) {
    if (sh.first != frameCount) continue;
    gspWaitForVBlank();
    gspWaitForVBlank();
    writeShot(sh.second);
  }
}

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

Texture* loadTexture(const std::string& path) {
  std::string data;
  if (!readFile(path, data) || data.size() < 12 || memcmp(data.data(), "T3T1", 4) != 0) return nullptr;
  u16 w, h;
  memcpy(&w, data.data() + 4, 2);
  memcpy(&h, data.data() + 6, 2);
  auto* t = new Texture{};
  t->w = w;
  t->h = h;
  if (!C3D_TexInit(&t->tex, w, h, GPU_RGBA8)) {
    delete t;
    return nullptr;
  }
  const u8* src = (const u8*)data.data() + 12;
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


// Vertex colour that reproduces image()'s tint/blend on the triangle path.
static uint32_t tintColor(uint32_t tint, float blend) {
  auto mix = [&](int sh) { int v = (tint >> sh) & 255; return (uint32_t)(255 + (v - 255) * blend + 0.5f) & 255; };
  return (mix(24) << 24) | (mix(16) << 16) | (mix(8) << 8) | (tint & 255);
}

void image(Texture* t, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh, uint32_t tint,
           float blend) {
  if (!t) return;
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

void rect(float x, float y, float w, float h, uint32_t rgba) {
  if (detail::transformed()) { detail::texQuad(detail::whiteTexture(), 0, 0, 1, 1, x, y, w, h, rgba); return; }
  flushMesh();
  C2D_DrawRectSolid(x, y, 0.5f, w, h, c2d(rgba));
}

void rectGradient(float x, float y, float w, float h, uint32_t top, uint32_t bottom) {
  flushMesh();
  C2D_DrawRectangle(x, y, 0.5f, w, h, c2d(top), c2d(top), c2d(bottom), c2d(bottom));
}

void line(float x0, float y0, float x1, float y1, float th, uint32_t rgba) {
  flushMesh();
  C2D_DrawLine(x0, y0, c2d(rgba), x1, y1, c2d(rgba), th, 0.5f);
}

void circle(float x, float y, float r, uint32_t rgba) {
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
static struct { Texture* tex = nullptr; bool additive = false; int vStart = 0, iStart = 0, iCount = 0; } batch;

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
  C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR);
  C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
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

void triangles(Texture* t, const Vert* verts, int count, const uint16_t* indices, int indexCount, bool additive) {
  if (!t || count == 0 || indexCount == 0) return;
  if (batch.tex && (batch.tex != t || batch.additive != additive || meshVertUsed - batch.vStart + count > 65000)) flushMesh();
  if (meshVertUsed + count > kMeshVerts || meshIndexUsed + indexCount > kMeshIndices) return;
  if (!batch.tex) {
    batch.tex = t;
    batch.additive = additive;
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
