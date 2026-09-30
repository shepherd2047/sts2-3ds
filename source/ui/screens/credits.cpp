// S26: the credits screen (RGDSplus U31; C# NCreditsScreen).
//
// The C# fills one tall scene (Init* per section) from the "credits" loc table and scrolls it up
// at 80 px/s (_Process), the mouse wheel / pan moves it by hand, B / click closes it. Here the
// same table (baked into loc.txt by tools/build_assets.py, never committed) is laid out as one
// strip in the two-screen virtual canvas: the top screen is y 0..240, the bottom (320 wide, x
// 40..360 of the canvas) is y 255..495 below the hinge gap, and everything is centred on x 200 and
// kept within 300 px so it fits the narrower bottom screen (CLAUDE.md layout rule).
//
// Order = the C# Init order: Mega Crit (+ team), composer, additional programming / VFX,
// marketing, consultants, voices, localisation (per language), Twitch, modding, playtesters,
// trailer, FMOD, Spine, Godot. Mega Crit's SplitTwoColumn "role||name" lines become two columns;
// the long modding / playtester name lists are joined into wrapped paragraphs (the C# shows them
// in three columns). A closing 移植 section names this port and the RGDSplus port it follows.
//
// Controls: it scrolls slowly on its own; hold A (or Down) to speed up, Up to go back, drag on
// the bottom screen to scroll by hand; B / 返回 leaves. It stops with the last section on screen.
// Debug: STS_OPEN_CREDITS=1 opens it at once. State lives here (one credits screen exists); openCredits() is called from 设置 -> 数据.
#include "../ui_common.h"

namespace ui {
namespace {

enum Kind { kLogo, kHeader, kSub, kLine, kPara, kPair, kNote, kSpace };
struct Item {
  Kind kind;
  std::string a, b;
  float y = 0, h = 0;
};

constexpr float kCX = kTop / 2.f;          // canvas centre
constexpr float kW = 296;                  // text width: inside the bottom screen (320)
constexpr float kColW = 142;               // each column of a role / name pair
constexpr float kStart = 96;               // canvas y of the first item at scroll 0
constexpr float kAuto = 26;                // px / s
constexpr float kFast = 130;               // px / s while A / Down is held
constexpr int kBackId = 2760;

struct State {
  bool open = false;
  bool live = false;   // false on the frame it opens (the A that opened it must not speed it up)
  std::vector<Item> items;
  float total = 0;
  float scroll = 0;
  float maxScroll = 0;
  float lastTouchY = 0;
  bool dragging = false;
};
State& S() {
  static State s;
  return s;
}

std::string C(const char* key) { return L(std::string("credits.") + key); }

std::vector<std::string> split(const std::string& s, const char* sep) {
  std::vector<std::string> out;
  size_t p = 0, n = std::string(sep).size();
  for (;;) {
    size_t q = s.find(sep, p);
    std::string t = s.substr(p, q == std::string::npos ? q : q - p);
    while (!t.empty() && (t.back() == ' ' || t.back() == '\r')) t.pop_back();
    size_t f = t.find_first_not_of(' ');
    if (f != std::string::npos && !t.empty()) out.push_back(t.substr(f));
    if (q == std::string::npos) break;
    p = q + n;
  }
  return out;
}

void add(std::vector<Item>& v, Kind k, std::string a = "", std::string b = "") { v.push_back({k, std::move(a), std::move(b)}); }

void header(std::vector<Item>& v, const char* key) { add(v, kHeader, C(key)); }
void names(std::vector<Item>& v, const char* key) {
  for (auto& n : split(C(key), "\n")) add(v, kLine, n);
}
// "role||name" lines (SplitTwoColumn); a line of only a name stays a plain line.
void pairs(std::vector<Item>& v, const char* key) {
  for (auto& line : split(C(key), "\n")) {
    auto p = split(line, "||");
    if (p.size() == 2) add(v, kPair, p[0], p[1]);
    else if (p.size() == 1) add(v, kLine, p[0]);
  }
}
void paragraph(std::vector<Item>& v, const char* key) {  // "a||b||c" -> "a · b · c"
  std::string s;
  for (auto& n : split(C(key), "||")) s += (s.empty() ? "" : "  ·  ") + n;
  add(v, kPara, s);
}
void section(std::vector<Item>& v, const char* headerKey) {
  add(v, kSpace);
  header(v, headerKey);
}

void build() {
  State& st = S();
  std::vector<Item>& v = st.items;
  v.clear();
  add(v, kLogo);
  add(v, kSpace);
  header(v, "MEGA_CRIT.header");
  names(v, "MEGA_CRIT.names");
  add(v, kSpace);
  pairs(v, "MEGA_CRIT_TEAM.names");
  section(v, "COMPOSER.header");
  names(v, "COMPOSER.names");
  section(v, "ADDITIONAL_PROGRAMMING.header");
  names(v, "ADDITIONAL_PROGRAMMING.names");
  section(v, "ADDITIONAL_VFX.header");
  names(v, "ADDITIONAL_VFX.names");
  section(v, "MARKETING_SUPPORT.header");
  names(v, "MARKETING_SUPPORT.names");
  section(v, "CONSULTANTS.header");
  names(v, "CONSULTANTS.names");
  section(v, "VOICES.header");
  pairs(v, "VOICES.names");
  section(v, "LOC.header");
  struct Lang { const char* h; const char* n; const char* team; };
  static const Lang langs[] = {
      {"LOC_PTB.header", "LOC_PTB.names", nullptr}, {"LOC_ZHS.header", "LOC_ZHS.names", nullptr},
      {"LOC_ZHT.header", "LOC_ZHT.names", nullptr}, {"LOC_FRA.header", "LOC_FRA.names", nullptr},
      {"LOC_DEU.header", "LOC_DEU.names", nullptr}, {"LOC_IND.header", "LOC_IND.names", nullptr},
      {"LOC_ITA.header", "LOC_ITA.names", "LOC_ITA.team"}, {"LOC_JPN.header", "LOC_JPN.names", nullptr},
      {"LOC_KOR.header", "LOC_KOR.names", nullptr}, {"LOC_POL.header", "LOC_POL.names", nullptr},
      {"LOC_RUS.header", "LOC_RUS.names", nullptr}, {"LOC_SPA.header", "LOC_SPA.names", nullptr},
      {"LOC_ESP.header", "LOC_ESP.names", "LOC_ESP.team"}, {"LOC_THA.header", "LOC_THA.names", nullptr},
      {"LOC_TUR.header", "LOC_TUR.names", nullptr}};
  for (const Lang& l : langs) {
    add(v, kSpace);
    add(v, kSub, C(l.h));
    if (l.team) add(v, kNote, C(l.team));
    pairs(v, l.n);
  }
  section(v, "TWITCH.header");
  pairs(v, "TWITCH.names");
  section(v, "MODDING_SUPPORT.header");
  paragraph(v, "MODDING_SUPPORT.names");
  section(v, "PLAYTESTERS.header");
  paragraph(v, "PLAYTESTERS.names");
  section(v, "TRAILER.header");
  add(v, kSpace);
  add(v, kSub, C("TRAILER_ANIMATION.header"));
  add(v, kNote, C("TRAILER_ANIMATION.team"));
  pairs(v, "TRAILER_ANIMATION.names");
  add(v, kSpace);
  add(v, kSub, C("TRAILER_EDITOR.header"));
  pairs(v, "TRAILER_EDITOR.names");
  add(v, kSpace);
  add(v, kSpace);
  add(v, kPara, C("FMOD"));
  add(v, kSpace);
  add(v, kPara, C("SPINE"));
  add(v, kSpace);
  add(v, kPara, C("GODOT"));
  // The port's own section (literals: they also feed the font's glyph set).
  section(v, "MEGA_CRIT.header");
  v.back() = {kHeader, tr("移植", "Port"), ""};
  const size_t portIdx = v.size() - 1;
  add(v, kSpace);
  add(v, kPara, tr("这是《杀戮尖塔2》的个人爱好者 3DS 移植，非官方作品，仅供个人学习与研究，不用于商业用途。", "This is a personal fan port of Slay the Spire 2 to the 3DS. It is unofficial, for personal study only, and not for commercial use."));
  add(v, kSpace);
  add(v, kPara, tr("双屏界面的设计沿用 RGDSplus 移植版", "The dual-screen layout follows the RGDSplus port"));
  add(v, kLine, "Slay the Spire for RGDSplus");
  add(v, kNote, "LPF970915/Slay-the-Spire-for-RGDSplus");
  add(v, kPara, tr("《杀戮尖塔2》及其全部素材归 Mega Crit 所有。", "Slay the Spire 2 and all of its assets belong to Mega Crit."));

  // Heights.
  float y = 0;
  for (Item& it : v) {
    it.y = y;
    switch (it.kind) {
      case kLogo: {
        Sprite logo = R().sprite("ui/menu_logo");
        it.h = logo ? logo.h * std::min(1.f, 200.f / logo.w) + 6 : 40;
        break;
      }
      case kSpace: it.h = 14; break;
      case kHeader: it.h = R().lineHeight(F16) * 1.25f + 5; break;
      case kSub: it.h = R().lineHeight(F16) + 3; break;
      case kLine: it.h = R().lineHeight(F12) + 3; break;
      case kNote: it.h = R().lineHeight(F12) + 3; break;
      case kPara: {
        float h = 0;
        R().measure(it.a, ts(F12, col::white, CENTER, kW), &h);
        it.h = h + 4;
        break;
      }
      case kPair: {
        float h1 = 0, h2 = 0;
        R().measure(it.a, ts(F12, col::gray, RIGHT, kColW), &h1);
        R().measure(it.b, ts(F12, col::white, LEFT, kColW), &h2);
        it.h = std::max(h1, h2) + 3;
        break;
      }
    }
    y += it.h;
  }
  st.total = y;
  // Stops with the 移植 section filling the bottom screen (clear of the hinge gap).
  st.maxScroll = std::max(0.f, kStart + v[portIdx].y - (kBotOY + 6));
}

void drawItem(const Item& it, float vy, bool top) {
  const float oy = top ? 0 : kBotOY, ox = top ? 0 : kBotOX;
  const float y = vy - oy, cx = kCX - ox;
  if (y > kH || y + it.h < 0) return;
  switch (it.kind) {
    case kLogo: {
      Sprite logo = R().sprite("ui/menu_logo");
      if (logo) {
        float s = std::min(1.f, 200.f / logo.w);
        spr(logo, std::round(cx - logo.w * s / 2), y, logo.w * s, logo.h * s);
      }
      break;
    }
    case kHeader:
      R().text(cx, y, it.a, ts(F16, col::gold, CENTER, kW, 1.25f));
      break;
    case kSub:
      R().text(cx, y, it.a, ts(F16, col::gold, CENTER, kW));
      break;
    case kLine:
      R().text(cx, y, it.a, ts(F12, col::white, CENTER, kW));
      break;
    case kNote:
      R().text(cx, y, it.a, ts(F12, col::gray, CENTER, kW));
      break;
    case kPara:
      R().text(cx, y, it.a, ts(F12, col::white, CENTER, kW));
      break;
    case kPair:
      R().text(cx - 6, y, it.a, ts(F12, col::gray, RIGHT, kColW));
      R().text(cx + 6, y, it.b, ts(F12, col::white, LEFT, kColW));
      break;
    case kSpace: break;
  }
}

}  // namespace

void App::openCredits() {
  State& st = S();
  if (st.items.empty()) build();
  st.open = true;
  st.live = false;
  st.scroll = 0;
  st.dragging = false;
}

bool App::updateCredits(const gfx::Input& in) {
  State& st = S();
  static bool envDone = false;  // debug: STS_OPEN_CREDITS=1 opens the page at the first frame
  if (!envDone) {
    envDone = true;
    if (getenv("STS_OPEN_CREDITS")) openCredits();
  }
  if (!st.open) return false;
  if (!st.live) { st.live = true; return true; }
  if (in.down & gfx::BTN_B) { st.open = false; return true; }
  const float dt = std::min(0.1f, (float)gfx::dt());
  float speed = kAuto;
  if (in.held & (gfx::BTN_A | gfx::BTN_DOWN)) speed = kFast;
  if (in.held & gfx::BTN_UP) speed = -kFast;
  if (in.touchDown) { st.dragging = true; st.lastTouchY = (float)in.ty; }
  if (st.dragging && in.touching) {
    st.scroll -= (float)in.ty - st.lastTouchY;  // the text follows the stylus
    st.lastTouchY = (float)in.ty;
    speed = 0;
  } else {
    st.dragging = false;
  }
  st.scroll = std::clamp(st.scroll + speed * dt, 0.f, st.maxScroll);
  return true;
}

bool App::drawCredits(bool top) {
  State& st = S();
  if (!st.open) return false;
  drawMenuBg(top, 0.86f);
  for (const Item& it : st.items) drawItem(it, kStart + it.y - st.scroll, top);
  if (top) return true;

  // Bottom: a dark strip under the hint and the back button.
  gfx::rect(0, kH - 44, kBot, 44, 0x000000B0);
  R().text(kBot - style::kMargin, kH - 22, tr("A 加速　上下 滚动　B 返回", "A Speed up   Up/Down Scroll   B Back"), ts(F12, col::gray, RIGHT));
  gfx::Input in = gfx::input();
  if (!st.live) in = gfx::Input{};
  widgets::beginFrame(in);
  bool back = widgets::button(kBackId, style::kMargin, kH - 40, 70, 32, tr("返回", "Back"));
  widgets::endFrame();
  if (back) st.open = false;
  return true;
}

}  // namespace ui
