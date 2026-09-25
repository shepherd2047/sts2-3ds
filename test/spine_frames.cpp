// Renders a skeleton's animation frames to a PNG contact sheet (software
// rasteriser) so the runtime can be checked without the game UI.
// Usage: spine_frames KEY ANIM FRAMES OUT.ppm
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../source/spine/spine.h"

static std::string readFile(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

struct Tex { int w = 0, h = 0; std::vector<uint8_t> px; };

static Tex loadT3T(const std::string& p) {
  Tex t;
  std::string d = readFile(p);
  if (d.size() < 12) return t;
  uint16_t w, h;
  memcpy(&w, d.data() + 4, 2);
  memcpy(&h, d.data() + 6, 2);
  t.w = w; t.h = h;
  t.px.assign(d.begin() + 12, d.end());
  return t;
}

int main(int argc, char** argv) {
  if (argc < 5) { fprintf(stderr, "usage: KEY ANIM FRAMES OUT.ppm\n"); return 1; }
  std::string key = argv[1], anim = argv[2], out = argv[4];
  int frames = atoi(argv[3]);
  float zoom = argc > 5 ? (float)atof(argv[5]) : 1.6f;
  std::string err;
  auto sd = spine::loadSkeleton(readFile("romfs/spine/" + key + ".skel"), readFile("romfs/spine/" + key + ".txt"), &err);
  if (!sd) { fprintf(stderr, "load failed: %s\n", err.c_str()); return 1; }
  const spine::Animation* a = sd->animation(anim);
  if (!a) { fprintf(stderr, "no animation %s\n", anim.c_str()); return 1; }
  printf("%s: %zu bones %zu slots %zu anims, %s %.2fs, %zu timelines\n", key.c_str(), sd->bones.size(), sd->slots.size(),
         sd->animations.size(), anim.c_str(), a->duration, a->timelines.size());
  std::vector<Tex> pages;
  for (auto& p : sd->pages) pages.push_back(loadT3T("romfs/" + p));

  const int cell = 200, cols = std::min(frames, 6), rows = (frames + cols - 1) / cols;
  const int W = cell * cols, H = cell * rows;
  std::vector<float> img(W * H * 3, 0.25f);
  spine::Skeleton skel(sd.get());
  spine::AnimationState st(sd.get());
  st.play(anim, true);
  for (int f = 0; f < frames; ++f) {
    st.apply(skel);
    skel.updateWorldTransform();
    std::vector<spine::Batch> batches;
    const float white[4] = {1, 1, 1, 1};
    float ox = (f % cols) * cell + cell / 2.f, oy = (f / cols) * cell + cell * 0.85f;
    skel.render(batches, ox, oy, zoom, false, white);
    for (auto& b : batches) {
      const Tex& t = pages[b.page];
      for (size_t i = 0; i + 2 < b.indices.size(); i += 3) {
        const spine::Vertex* v[3] = {&b.vertices[b.indices[i]], &b.vertices[b.indices[i + 1]], &b.vertices[b.indices[i + 2]]};
        float minx = std::min({v[0]->x, v[1]->x, v[2]->x}), maxx = std::max({v[0]->x, v[1]->x, v[2]->x});
        float miny = std::min({v[0]->y, v[1]->y, v[2]->y}), maxy = std::max({v[0]->y, v[1]->y, v[2]->y});
        float den = (v[1]->y - v[2]->y) * (v[0]->x - v[2]->x) + (v[2]->x - v[1]->x) * (v[0]->y - v[2]->y);
        if (std::fabs(den) < 1e-6f) continue;
        for (int y = std::max(0, (int)miny); y <= std::min(H - 1, (int)maxy); ++y)
          for (int x = std::max(0, (int)minx); x <= std::min(W - 1, (int)maxx); ++x) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = ((v[1]->y - v[2]->y) * (px - v[2]->x) + (v[2]->x - v[1]->x) * (py - v[2]->y)) / den;
            float w1 = ((v[2]->y - v[0]->y) * (px - v[2]->x) + (v[0]->x - v[2]->x) * (py - v[2]->y)) / den;
            float w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float u = w0 * v[0]->u + w1 * v[1]->u + w2 * v[2]->u;
            float vv = w0 * v[0]->v + w1 * v[1]->v + w2 * v[2]->v;
            int tx = std::min(t.w - 1, std::max(0, (int)(u * t.w))), ty = std::min(t.h - 1, std::max(0, (int)(vv * t.h)));
            const uint8_t* p = &t.px[(ty * t.w + tx) * 4];
            uint32_t c = v[0]->rgba;
            float cr = (c >> 24) / 255.f, cg = ((c >> 16) & 255) / 255.f, cb = ((c >> 8) & 255) / 255.f, ca = (c & 255) / 255.f;
            float al = p[3] / 255.f * ca;
            float* d = &img[(y * W + x) * 3];
            float src[3] = {p[0] / 255.f * cr, p[1] / 255.f * cg, p[2] / 255.f * cb};
            for (int k = 0; k < 3; ++k) d[k] = b.blend == 1 ? std::min(1.f, d[k] + src[k] * al) : d[k] * (1 - al) + src[k] * al;
          }
      }
    }
    st.update(a->duration / frames);
  }
  FILE* fp = fopen(out.c_str(), "wb");
  fprintf(fp, "P6 %d %d 255\n", W, H);
  for (float v : img) fputc((int)(std::min(1.f, std::max(0.f, v)) * 255), fp);
  fclose(fp);
  return 0;
}
