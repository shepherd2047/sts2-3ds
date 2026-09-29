// Loaded assets: sprite atlas, bitmap font, localised strings.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../gfx/gfx.h"
#include "../spine/spine.h"

namespace ui {

struct Sprite {
  gfx::Texture* tex = nullptr;
  float x = 0, y = 0, w = 0, h = 0;
  int ax = 0, ay = 0;  // anchor (creature feet), in sprite pixels
  int nl = 0, nt = 0, nr = 0, nb = 0;  // 9-slice margins (0: not a 9-slice)
  explicit operator bool() const { return tex != nullptr; }
};

enum FontSize { F12 = 0, F16 = 1 };

// Colours for text; [gold] etc. in strings switch between them.
namespace col {
constexpr uint32_t white = 0xFFF6E2FF;
constexpr uint32_t gold = 0xEFC851FF;
constexpr uint32_t blue = 0x87CEEBFF;
constexpr uint32_t green = 0x7FFF00FF;
constexpr uint32_t red = 0xFF6563FF;
constexpr uint32_t purple = 0xEE82EEFF;
constexpr uint32_t gray = 0x9A9A9AFF;
constexpr uint32_t dark = 0x1E1E28FF;
}  // namespace col

enum Align { LEFT, CENTER, RIGHT };

struct TextStyle {
  FontSize size = F12;
  uint32_t color = col::white;
  Align align = LEFT;
  float maxWidth = 0;  // 0: no wrapping
  float scale = 1.f;
  float lineGap = 1.f;
  bool shadow = true;
  uint32_t shadowColor = 0x000000C0;  // drop shadow, offset (shadowDx, shadowDy) * scale
  float shadowDx = 1, shadowDy = 1;
  uint32_t outline = 0;  // colour of a 1 px outline (4 extra draws per glyph: use sparingly); 0 = none
};

class Res {
 public:
  // progress (S01 boot): called with 0..1 as the atlas pages load, so the boot can draw frames.
  bool load(const std::function<void(float)>& progress = nullptr);
  Sprite sprite(const std::string& name) const;
  // Names of all atlas sprites that start with prefix, sorted (asset gallery, tests).
  std::vector<std::string> spriteNames(const std::string& prefix) const;
  gfx::Texture* texture(const std::string& path);
  void releaseTexture(const std::string& path);  // frees a texture loaded by texture()
  // romfs/spine/KEY.*; null if missing or unreadable.
  const spine::SkeletonData* skeleton(const std::string& key);
  // Frees every loaded skeleton (and its texture pages) whose key is not in keep.
  // Callers must drop all Skeleton/AnimationState objects built on them first.
  void releaseSkeletons(const std::vector<std::string>& keep);

  const std::string& loc(const std::string& key) const;
  bool hasLoc(const std::string& key) const { return strings_.count(key) > 0; }

  // Rich text: [gold]..[/gold], [blue], [green], [red], [purple], [b], \n and inline icons
  // [icon:energy|gold|hp|star|block|<atlas/name>] (F4; square, sized to the line height).
  // Returns the height drawn.
  float text(float x, float y, const std::string& s, const TextStyle& st = {});
  float measure(const std::string& s, const TextStyle& st, float* outHeight = nullptr);
  float lineHeight(FontSize f) const { return fonts_[f].lineHeight; }

 private:
  struct Glyph { float x, y, w, h, ox, oy, adv; };
  struct Font {
    int px = 12;
    float lineHeight = 14, ascent = 11;
    std::unordered_map<uint32_t, Glyph> glyphs;
  };
  struct Run { std::string text; uint32_t color; };
  struct Line { std::vector<std::pair<uint32_t, uint32_t>> glyphs; float width = 0; };  // (codepoint, colour)

  std::vector<Line> layout(const std::string& s, const TextStyle& st);
  // [icon:NAME] codepoints are allocated from a private-use range above kIconBase; iconSprites_
  // maps the offset back to an atlas sprite name so text() can draw it instead of a font glyph.
  static constexpr uint32_t kIconBase = 0xF0000;
  int iconIndex(const std::string& name);
  std::vector<std::string> iconSprites_;

  std::vector<gfx::Texture*> atlasPages_;
  std::unordered_map<std::string, Sprite> sprites_;
  std::map<std::string, gfx::Texture*> textures_;
  std::map<std::string, std::unique_ptr<spine::SkeletonData>> skeletons_;
  gfx::Texture* fontTex_[2] = {nullptr, nullptr};  // per size
  Font fonts_[2];
  std::unordered_map<std::string, std::string> strings_;
  std::string missing_;
};

Res& res();

// Decode one UTF-8 code point starting at s[i]; advances i.
uint32_t nextCodepoint(const std::string& s, size_t& i);

}  // namespace ui
