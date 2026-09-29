#include "res.h"

#include <algorithm>
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

bool Res::load(const std::function<void(float)>& progress) {
  std::string data;
  if (!gfx::readFile("gfx/atlas.txt", data)) return false;
  int pageCount = 1;  // for progress only
  if (progress) {
    std::istringstream in(data);
    std::string name;
    int page, x, y, w, h, ax, ay;
    while (in >> name >> page >> x >> y >> w >> h >> ax >> ay) pageCount = std::max(pageCount, page + 1);
  }
  {
    std::istringstream in(data);
    std::string name;
    int page, x, y, w, h, ax, ay;
    while (in >> name >> page >> x >> y >> w >> h >> ax >> ay) {
      while ((int)atlasPages_.size() <= page) {
        atlasPages_.push_back(gfx::loadTexture("gfx/atlas_" + std::to_string(atlasPages_.size()) + ".t3t"));
        if (progress) progress(std::min(0.95f, (float)atlasPages_.size() / (float)pageCount));
      }
      Sprite s;
      s.tex = atlasPages_[page];
      s.x = x; s.y = y; s.w = w; s.h = h; s.ax = ax; s.ay = ay;
      sprites_[name] = s;
    }
  }
  // 9-slice margins (gfx/nine.txt: name left top right bottom, in baked pixels).
  if (gfx::readFile("gfx/nine.txt", data)) {
    std::istringstream in(data);
    std::string name;
    int l, t, r, b;
    while (in >> name >> l >> t >> r >> b) {
      auto it = sprites_.find(name);
      if (it != sprites_.end()) { it->second.nl = l; it->second.nt = t; it->second.nr = r; it->second.nb = b; }
    }
  }

  // One glyph page per size (font_<index>.t3t).
  for (int i = 0; i < 2; ++i) fontTex_[i] = gfx::loadTexture("font/font_" + std::to_string(i) + ".t3t");
  if (!fontTex_[1]) fontTex_[1] = fontTex_[0];
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
  if (progress) progress(1.f);
  return fontTex_[0] && !atlasPages_.empty();
}

Sprite Res::sprite(const std::string& name) const {
  auto it = sprites_.find(name);
  return it == sprites_.end() ? Sprite{} : it->second;
}

std::vector<std::string> Res::spriteNames(const std::string& prefix) const {
  std::vector<std::string> out;
  for (auto& kv : sprites_) if (kv.first.compare(0, prefix.size(), prefix) == 0) out.push_back(kv.first);
  std::sort(out.begin(), out.end());
  return out;
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

void Res::releaseTexture(const std::string& path) {
  auto t = textures_.find(path);
  if (t == textures_.end()) return;
  if (t->second) gfx::freeTexture(t->second);
  textures_.erase(t);
}

bool Res::skeletonLoaded(const std::string& key) const { return skeletons_.count(key) > 0; }

void Res::releaseSkeleton(const std::string& key) {
  auto it = skeletons_.find(key);
  if (it == skeletons_.end()) return;
  if (it->second)
    for (auto& p : it->second->pages) releaseTexture(p);
  skeletons_.erase(it);
}

void Res::releaseSkeletons(const std::vector<std::string>& keep) {
  for (auto it = skeletons_.begin(); it != skeletons_.end();) {
    if (std::find(keep.begin(), keep.end(), it->first) != keep.end()) { ++it; continue; }
    if (it->second)
      for (auto& p : it->second->pages) {
        auto t = textures_.find(p);
        if (t != textures_.end()) { gfx::freeTexture(t->second); textures_.erase(t); }
      }
    it = skeletons_.erase(it);
  }
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

// Short [icon:NAME] names -> atlas sprite path; anything with a '/' is used as the sprite
// name directly (e.g. [icon:relic/BURNING_BLOOD]).
// Punctuation that must not begin a line (GB/T 15834 避头): CJK closing marks and their ASCII kin.
static bool closingPunct(uint32_t cp) {
  switch (cp) {
    case 0xFF0C: case 0x3002: case 0x3001: case 0xFF1B: case 0xFF1A: case 0xFF01: case 0xFF1F:  // ，。、；：！？
    case 0xFF09: case 0x300D: case 0x300F: case 0x300B: case 0x3009: case 0x3011: case 0x201D:  // ）」』》〉】”
    case 0x2019: case 0x2026: case 0xFF05: case 0x00B7: case 0xFF5E:                            // ’…％·～
    case ',': case '.': case ';': case ':': case '!': case '?': case ')': case ']': case '%':
      return true;
    default:
      return false;
  }
}

static const char* iconSpritePath(const std::string& name) {
  if (name == "energy") return "card/energy";
  if (name == "gold") return "ui/reward_money";
  if (name == "hp") return "ui/tb_heart";
  if (name == "star") return "ui/star";
  if (name == "block") return "ui/block";
  return nullptr;
}

int Res::iconIndex(const std::string& name) {
  for (size_t i = 0; i < iconSprites_.size(); ++i)
    if (iconSprites_[i] == name) return (int)i;
  const char* path = iconSpritePath(name);
  iconSprites_.push_back(path ? path : name);
  return (int)iconSprites_.size() - 1;
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
        if (tag.rfind("icon:", 0) == 0) {
          flushWord();
          uint32_t cp = kIconBase + (uint32_t)iconIndex(tag.substr(5));
          float a = f.lineHeight;
          if (lines.back().width + a > maxW && !lines.back().glyphs.empty()) lines.emplace_back();
          lines.back().glyphs.push_back({cp, colorStack.back()});
          lines.back().width += a;
        } else if (!tag.empty() && tag[0] == '/') {
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
      // Keep closing punctuation off the start of a line (避头): carry the previous line's last
      // character (a whole ASCII word, plus any closing marks before it) down with it.
      if (closingPunct(cp)) {
        auto& prev = lines[lines.size() - 2].glyphs;
        auto glyphAdv = [&](uint32_t g) { return g >= kIconBase ? f.lineHeight : adv(g); };
        auto isWord = [](uint32_t g) { return g < 0x80 && g != ' '; };
        size_t k = prev.size();
        while (k > 0 && closingPunct(prev[k - 1].first)) --k;
        if (k > 0 && isWord(prev[k - 1].first))
          while (k > 0 && isWord(prev[k - 1].first)) --k;
        else if (k > 0)
          --k;
        if (k > 0) {  // never empty the previous line
          for (size_t j = k; j < prev.size(); ++j) {
            float ga = glyphAdv(prev[j].first);
            lines[lines.size() - 2].width -= ga;
            lines.back().glyphs.push_back(prev[j]);
            lines.back().width += ga;
          }
          prev.resize(k);
        }
      }
    }
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
      if (cp >= kIconBase) {
        float size = f.lineHeight * st.scale;
        size_t idx = cp - kIconBase;
        if (idx < iconSprites_.size()) {
          Sprite s2 = sprite(iconSprites_[idx]);
          if (s2) gfx::image(s2.tex, s2.x, s2.y, s2.w, s2.h, cx, cy, size, size);
        }
        cx += size;
        continue;
      }
      auto it = f.glyphs.find(cp);
      if (it == f.glyphs.end()) { cx += f.px * 0.5f * st.scale; continue; }
      const Glyph& g = it->second;
      if (g.w > 0) {
        float gx = cx + g.ox * st.scale, gy = cy + g.oy * st.scale;
        if (st.outline) {
          const float o = st.scale;
          const float offs[4][2] = {{-o, 0}, {o, 0}, {0, -o}, {0, o}};
          for (auto& d : offs)
            gfx::image(fontTex_[st.size], g.x, g.y, g.w, g.h, gx + d[0], gy + d[1], g.w * st.scale, g.h * st.scale,
                       st.outline, 1.f);
        }
        if (st.shadow)
          gfx::image(fontTex_[st.size], g.x, g.y, g.w, g.h, gx + st.shadowDx * st.scale, gy + st.shadowDy * st.scale,
                     g.w * st.scale, g.h * st.scale, st.shadowColor, 1.f);
        gfx::image(fontTex_[st.size], g.x, g.y, g.w, g.h, gx, gy, g.w * st.scale, g.h * st.scale, color, 1.f);
      }
      cx += g.adv * st.scale;
    }
    cy += lh;
  }
  return cy - y;
}

}  // namespace ui
