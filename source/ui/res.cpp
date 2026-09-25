#include "res.h"

#include <cstdio>
#include <sstream>

namespace ui {

Res& res() {
  static Res r;
  return r;
}

uint32_t nextCodepoint(const std::string& s, size_t& i) {
  unsigned char c = s[i++];
  if (c < 0x80) return c;
  int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
  uint32_t cp = c & (0x3F >> extra);
  for (int k = 0; k < extra && i < s.size(); ++k) cp = (cp << 6) | (s[i++] & 0x3F);
  return cp;
}

bool Res::load() {
  std::string data;
  if (!gfx::readFile("gfx/atlas.txt", data)) return false;
  {
    std::istringstream in(data);
    std::string name;
    int page, x, y, w, h, ax, ay;
    while (in >> name >> page >> x >> y >> w >> h >> ax >> ay) {
      while ((int)atlasPages_.size() <= page) {
        atlasPages_.push_back(gfx::loadTexture("gfx/atlas_" + std::to_string(atlasPages_.size()) + ".t3t"));
      }
      Sprite s;
      s.tex = atlasPages_[page];
      s.x = x; s.y = y; s.w = w; s.h = h; s.ax = ax; s.ay = ay;
      sprites_[name] = s;
    }
  }

  fontTex_ = gfx::loadTexture("font/font_0.t3t");
  if (!gfx::readFile("font/font.txt", data)) return false;
  {
    std::istringstream in(data);
    std::string tag;
    while (in >> tag) {
      if (tag == "size") {
        int idx, px, lh, asc;
        in >> idx >> px >> lh >> asc;
        fonts_[idx].px = px;
        fonts_[idx].lineHeight = (float)lh;
        fonts_[idx].ascent = (float)asc;
      } else {
        int idx;
        uint32_t cp;
        Glyph g;
        in >> idx >> cp >> g.x >> g.y >> g.w >> g.h >> g.ox >> g.oy >> g.adv;
        fonts_[idx].glyphs[cp] = g;
      }
    }
  }

  if (!gfx::readFile("loc.txt", data)) return false;
  {
    size_t pos = 0;
    while (pos < data.size()) {
      size_t nl = data.find('\n', pos);
      if (nl == std::string::npos) nl = data.size();
      std::string line = data.substr(pos, nl - pos);
      pos = nl + 1;
      size_t tab = line.find('\t');
      if (tab == std::string::npos) continue;
      std::string v;
      for (size_t i = tab + 1; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size()) {
          ++i;
          v += line[i] == 'n' ? '\n' : line[i];
        } else {
          v += line[i];
        }
      }
      strings_[line.substr(0, tab)] = v;
    }
  }
  return fontTex_ && !atlasPages_.empty();
}

Sprite Res::sprite(const std::string& name) const {
  auto it = sprites_.find(name);
  return it == sprites_.end() ? Sprite{} : it->second;
}

gfx::Texture* Res::texture(const std::string& path) {
  auto it = textures_.find(path);
  if (it != textures_.end()) return it->second;
  return textures_[path] = gfx::loadTexture(path);
}

const std::string& Res::loc(const std::string& key) const {
  auto it = strings_.find(key);
  if (it != strings_.end()) return it->second;
  const_cast<std::string&>(missing_) = key;
  return missing_;
}

const spine::SkeletonData* Res::skeleton(const std::string& key) {
  auto it = skeletons_.find(key);
  if (it != skeletons_.end()) return it->second.get();
  std::string skel, atlas, err;
  std::unique_ptr<spine::SkeletonData> sd;
  if (gfx::readFile("spine/" + key + ".skel", skel) && gfx::readFile("spine/" + key + ".txt", atlas)) {
    sd = spine::loadSkeleton(skel, atlas, &err);
    if (sd)
      for (auto& p : sd->pages) texture(p);  // load pages up front
  }
  auto* raw = sd.get();
  skeletons_[key] = std::move(sd);
  return raw;
}

// ---------------------------------------------------------------- text

static uint32_t tagColor(const std::string& tag, uint32_t base) {
  if (tag == "gold") return col::gold;
  if (tag == "blue") return col::blue;
  if (tag == "green") return col::green;
  if (tag == "red") return col::red;
  if (tag == "purple") return col::purple;
  return base;
}

std::vector<Res::Line> Res::layout(const std::string& s, const TextStyle& st) {
  const Font& f = fonts_[st.size];
  std::vector<Line> lines(1);
  std::vector<uint32_t> colorStack{st.color};
  float maxW = st.maxWidth > 0 ? st.maxWidth / st.scale : 1e9f;

  // Pending ASCII word, kept together when wrapping.
  std::vector<std::pair<uint32_t, uint32_t>> word;
  float wordW = 0;
  auto flushWord = [&] {
    if (word.empty()) return;
    if (lines.back().width + wordW > maxW && !lines.back().glyphs.empty()) lines.emplace_back();
    for (auto& g : word) lines.back().glyphs.push_back(g);
    lines.back().width += wordW;
    word.clear();
    wordW = 0;
  };
  auto adv = [&](uint32_t cp) {
    auto it = f.glyphs.find(cp);
    return it == f.glyphs.end() ? (float)f.px * 0.5f : it->second.adv;
  };

  for (size_t i = 0; i < s.size();) {
    if (s[i] == '[') {
      size_t close = s.find(']', i);
      if (close != std::string::npos && close - i < 24) {
        std::string tag = s.substr(i + 1, close - i - 1);
        if (!tag.empty() && tag[0] == '/') {
          if (colorStack.size() > 1) colorStack.pop_back();
        } else {
          colorStack.push_back(tagColor(tag, colorStack.back()));
        }
        i = close + 1;
        continue;
      }
    }
    uint32_t cp = nextCodepoint(s, i);
    if (cp == '\n') {
      flushWord();
      lines.emplace_back();
      continue;
    }
    uint32_t c = colorStack.back();
    bool wordChar = cp < 0x80 && cp != ' ';
    if (wordChar) {
      word.push_back({cp, c});
      wordW += adv(cp);
      continue;
    }
    flushWord();
    float a = adv(cp);
    if (lines.back().width + a > maxW && !lines.back().glyphs.empty()) {
      lines.emplace_back();
      if (cp == ' ') continue;
    }
    // Keep closing punctuation off the start of a line.
    lines.back().glyphs.push_back({cp, c});
    lines.back().width += a;
  }
  flushWord();
  return lines;
}

float Res::measure(const std::string& s, const TextStyle& st, float* outHeight) {
  auto lines = layout(s, st);
  float w = 0;
  for (auto& l : lines) w = std::max(w, l.width);
  if (outHeight) *outHeight = lines.size() * fonts_[st.size].lineHeight * st.lineGap * st.scale;
  return w * st.scale;
}

float Res::text(float x, float y, const std::string& s, const TextStyle& st) {
  const Font& f = fonts_[st.size];
  auto lines = layout(s, st);
  float lh = f.lineHeight * st.lineGap * st.scale;
  float cy = y;
  for (auto& line : lines) {
    float w = line.width * st.scale;
    float cx = x;
    if (st.align == CENTER) cx = x - w / 2;
    else if (st.align == RIGHT) cx = x - w;
    for (auto& [cp, color] : line.glyphs) {
      auto it = f.glyphs.find(cp);
      if (it == f.glyphs.end()) { cx += f.px * 0.5f * st.scale; continue; }
      const Glyph& g = it->second;
      if (g.w > 0) {
        float gx = cx + g.ox * st.scale, gy = cy + g.oy * st.scale;
        if (st.shadow)
          gfx::image(fontTex_, g.x, g.y, g.w, g.h, gx + st.scale, gy + st.scale, g.w * st.scale, g.h * st.scale,
                     0x000000C0, 1.f);
        gfx::image(fontTex_, g.x, g.y, g.w, g.h, gx, gy, g.w * st.scale, g.h * st.scale, color, 1.f);
      }
      cx += g.adv * st.scale;
    }
    cy += lh;
  }
  return cy - y;
}

}  // namespace ui
