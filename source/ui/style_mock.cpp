// F0 style mock-ups: three screens drawn only from style.h tokens, so the guide in
// docs/UI_STYLE.md can be judged on screen (STS_MOCK=1 reward list, 2 combat, 3 event).
// F3's widget kit replaces the helpers below; the numbers stay.
#include <cmath>
#include <cstdio>
#include <string>

#include "res.h"
#include "style.h"

namespace ui {

namespace {

using namespace style;
constexpr int kTop = gfx::kTopW, kBot = gfx::kBottomW, kH = gfx::kScreenH;

Res& R() { return res(); }

TextStyle ts(FontSize f = F12, uint32_t c = col::white, Align a = LEFT, float maxW = 0, float scale = 1.f) {
  TextStyle t;
  t.size = f;
  t.color = c;
  t.align = a;
  t.maxWidth = maxW;
  t.scale = scale;
  return t;
}

void spr(const Sprite& s, float x, float y, float w = -1, float h = -1, uint32_t tint = 0xFFFFFFFF) {
  if (!s) return;
  gfx::image(s.tex, s.x, s.y, s.w, s.h, x, y, w < 0 ? s.w : w, h < 0 ? s.h : h, tint);
}

void frame(float x, float y, float w, float h, uint32_t edge, float t = 1) {
  gfx::rect(x, y, w, t, edge);
  gfx::rect(x, y + h - t, w, t, edge);
  gfx::rect(x, y, t, h, edge);
  gfx::rect(x + w - t, y, t, h, edge);
}

void panel(float x, float y, float w, float h) {
  gfx::rect(x, y, w, h, kPanel);
  frame(x, y, w, h, kPanelEdge);
  gfx::rect(x + 1, y + 1, w - 2, 1, 0xFFFFFF14);
}

enum class BtnState { Normal, Focus, Pressed, Disabled };

// A plated button: fill by state, brass border, top sheen; Focus adds the 2 px ring.
void button(float x, float y, float w, float h, const char* label, BtnState st = BtnState::Normal,
            uint32_t fill = kPlate) {
  bool off = st == BtnState::Disabled;
  if (st == BtnState::Focus && fill == kPlate) fill = kPlateHover;
  if (st == BtnState::Pressed) fill = kPlatePress;
  if (off) fill = kPlateOff;
  float oy = st == BtnState::Pressed ? 1.f : 0.f;
  gfx::rect(x, y, w, h, fill);
  frame(x, y, w, h, off ? kEdgeOff : kEdge);
  if (!off && st != BtnState::Pressed) gfx::rect(x + 1, y + 1, w - 2, 2, 0xFFFFFF22);
  if (st == BtnState::Focus) frame(x - 2, y - 2, w + 4, h + 4, kFocus, 2);
  TextStyle t = ts(F16, off ? col::gray : col::white, CENTER);
  float th;
  float tw = R().measure(label, t, &th);
  if (tw > w - 8) t.scale = (w - 8) / tw, th *= t.scale;
  R().text(x + w / 2, y + (h - th) / 2 + oy, label, t);
}

void topBar(bool full = true) {
  gfx::rect(0, 0, kTop, kTopBarH, 0x000000B8);
  gfx::rect(0, kTopBarH - 1, kTop, 1, kPanelEdge);
  R().text(kMargin, 3, "生命 80/80", ts(F12, col::red));
  R().text(88, 3, "金币 99", ts(F12, col::gold));
  R().text(148, 3, "第 3 层", ts(F12, col::white));
  R().text(198, 3, "牌组 12", ts(F12, col::white));
  if (full) {
    for (int i = 0; i < 3; ++i) gfx::rect(262 + i * 20, 3, 16, 14, i == 0 ? 0x4A86C8FF : 0x2A2A34FF);
    spr(R().sprite("relic/BURNING_BLOOD"), kTop - 24, 1, 18, 18);
  }
}

void sceneBg(bool top, float dim) {
  const float w = top ? kTop : kBot;
  gfx::image(R().texture("gfx/bg_overgrowth.t3t"), top ? 0 : (kTop - kBot) / 2.f, 0, w, kH, 0, 0, w, kH, 0x000000FF, 0.15f);
  gfx::rect(0, 0, w, kH, (uint32_t)(dim * 255));
}

void titleText(float cx, float y, const char* s, float scale = 1.25f) {
  R().text(cx, y, s, ts(F16, col::gold, CENTER, 0, scale));
}

// A list row: icon slot, label, optional right-aligned value; Focus draws the ring.
void row(float x, float y, float w, const char* label, const char* value, const char* iconKey,
         BtnState st, uint32_t valueCol = col::gold) {
  bool off = st == BtnState::Disabled;
  gfx::rect(x, y, w, kRowH, off ? kPlateOff : st == BtnState::Focus ? kPlateHover : kPlate);
  if (st == BtnState::Focus) gfx::rect(x, y, w, kRowH, kSelectedFill);
  frame(x, y, w, kRowH, off ? kEdgeOff : kEdge);
  gfx::rect(x + 1, y + 1, w - 2, 2, 0xFFFFFF1C);
  if (st == BtnState::Focus) frame(x - 2, y - 2, w + 4, kRowH + 4, kFocus, 2);
  spr(R().sprite(iconKey), x + 4, y + 2, 32, 32, off ? 0x888888FF : 0xFFFFFFFF);
  R().text(x + 44, y + (kRowH - R().lineHeight(F16)) / 2, label, ts(F16, off ? col::gray : col::white));
  if (value) R().text(x + w - 8, y + (kRowH - R().lineHeight(F12)) / 2, value, ts(F12, off ? col::gray : valueCol, RIGHT));
}

// ---------------------------------------------------------------- 1: reward list (S14)
void mockReward(bool top) {
  sceneBg(top, 0.55f);
  if (top) {
    topBar();
    titleText(kTop / 2.f, 46, "战斗胜利！", 1.6f);
    gfx::rect(kTop / 2.f - 70, 82, 140, 2, kPanelHi);
    // The focused reward, described: relic icon, name, rarity, text.
    panel(60, 100, 280, 110);
    spr(R().sprite("relic/BURNING_BLOOD"), 72, 116, 48, 48);
    R().text(132, 112, "燃烧之血", ts(F16, col::white));
    R().text(132, 132, "起始遗物", ts(F12, col::gold));
    R().text(132, 152, "战斗结束时，回复 6 点生命。", ts(F12, col::white, LEFT, 196));
    return;
  }
  const float x = kMargin, w = kBot - 2 * kMargin;
  row(x, 10, w, "42 金币", "已领取", "map/chest", BtnState::Disabled);
  row(x, 10 + (kRowH + kGap), w, "火焰药水", nullptr, "potion/FIRE_POTION", BtnState::Normal);
  row(x, 10 + 2 * (kRowH + kGap), w, "燃烧之血", "遗物", "relic/BURNING_BLOOD", BtnState::Focus);
  row(x, 10 + 3 * (kRowH + kGap), w, "将一张牌加入牌组", nullptr, "portrait/BASH", BtnState::Normal);
  button(kMargin, kActionY, 96, kButtonH, "详情");
  button(kBot - kMargin - 120, kActionY, 120, kButtonH, "前进", BtnState::Normal, kPrimary);
}

// ---------------------------------------------------------------- 2: combat (S08)
void card(float cx, float cy, float ang, float s, const char* portrait, const char* name, int cost, const char* kind) {
  gfx::Affine m = gfx::Affine::rotateAround(cx, cy + 60, ang, 1.f);
  gfx::pushTransform(m);
  float x = cx - 60 * s, y = cy - 20 * s;
  spr(R().sprite(std::string("portrait/") + portrait), x + 8 * s, y + 16 * s, 104 * s, 78 * s);
  spr(R().sprite(std::string("card/frame_") + kind), x, y, 120 * s, 169 * s);
  spr(R().sprite(std::string("card/border_") + kind), x - 3 * s, y + 4 * s, 126 * s, 96 * s);
  spr(R().sprite("card/banner"), x - 3 * s, y + 3 * s, 126 * s, 28 * s);
  TextStyle t = ts(F12, col::white, CENTER);
  t.scale = 0.8f;
  R().text(x + 60 * s, y + 6 * s, name, t);
  spr(R().sprite("card/energy"), x - 7 * s, y - 7 * s, 30 * s, 30 * s);
  char b[4];
  snprintf(b, sizeof b, "%d", cost);
  TextStyle c = ts(F16, col::white, CENTER);
  c.scale = s;
  R().text(x + 8 * s, y + 1 * s, b, c);
  gfx::popTransform();
}

void mockCombat(bool top) {
  sceneBg(top, top ? 0.25f : 0.5f);
  if (top) {
    topBar();
    spr(R().sprite("creature/IRONCLAD"), 110 - R().sprite("creature/IRONCLAD").ax, 196 - R().sprite("creature/IRONCLAD").ay);
    Sprite e = R().sprite("creature/NIBBIT");
    spr(e, 290 - e.ax, 196 - e.ay);
    // Enemy plate: intent above the head, HP bar under the feet.
    spr(R().sprite("intent/attack_2"), 278, 62, 30, 30);
    R().text(293, 92, "12", ts(F16, col::white, CENTER));
    gfx::rect(250, 204, 80, 8, 0x000000C0);
    gfx::rect(251, 205, 62, 6, kHpBar);
    R().text(290, 213, "31/42", ts(F12, col::white, CENTER, 0, 0.85f));
    gfx::rect(70, 204, 80, 8, 0x000000C0);
    gfx::rect(71, 205, 78, 6, kHpBar);
    R().text(110, 213, "80/80", ts(F12, col::white, CENTER, 0, 0.85f));
    return;
  }
  // Hand: fanned, centred, cards ~ 60 px wide so five fit; the tapped one is enlarged elsewhere.
  const char* names[] = {"打击", "防御", "重击", "防御", "打击"};
  const char* pics[] = {"STRIKE_IRONCLAD", "DEFEND_IRONCLAD", "BASH", "DEFEND_IRONCLAD", "STRIKE_IRONCLAD"};
  const char* kinds[] = {"attack", "skill", "attack", "skill", "attack"};
  const int costs[] = {1, 1, 2, 1, 1};
  for (int i = 0; i < 5; ++i) {
    float t = i - 2.f;
    card(kBot / 2.f + t * 50, 100 + std::abs(t) * 5, t * 0.11f, 0.62f, pics[i], names[i], costs[i], kinds[i]);
  }
  // Energy orb left, piles in the corners, end turn right: each >= 32 px to touch.
  spr(R().sprite("card/energy"), kMargin, 158, 36, 36);
  R().text(kMargin + 18, 166, "3/3", ts(F16, col::white, CENTER));
  button(kMargin, kActionY + 4, 48, 30, "抽 5");
  button(kBot - kMargin - 48, kActionY + 4, 48, 30, "弃 0");
  button(kBot / 2.f - 42, kActionY + 2, 84, kButtonH, "结束回合", BtnState::Normal, kPrimary);
  button(kMargin + 54, kActionY + 4, 32, 30, "药");
  button(kBot - kMargin - 54 - 32, kActionY + 4, 32, 30, "息");
}

// ---------------------------------------------------------------- 3: event (S17)
void mockEvent(bool top) {
  sceneBg(top, 0.4f);
  if (top) {
    topBar(false);
    Sprite ev = R().sprite("event/AROMA_OF_CHAOS");
    if (!ev) ev = R().sprite("event/WELLSPRING");
    spr(ev, 20, 30, 170, 96);
    frame(20, 30, 170, 96, kPanelEdge);
    R().text(200, 34, "清泉", ts(F16, col::gold, LEFT, 0, 1.25f));
    gfx::rect(200, 58, 180, 1, kPanelHi);
    panel(20, 136, 360, 92);
    R().text(30, 144, "一汪清水从岩缝里涌出，水面映着微光。你觉得口渴，但也隐约觉得这水不太对劲。", ts(F12, col::white, LEFT, 340));
    return;
  }
  const float x = kMargin, w = kBot - 2 * kMargin, h = 44;
  button(x, 12, w, h, "喝下  回复 15 点生命", BtnState::Normal);
  button(x, 12 + h + 8, w, h, "装满瓶子  获得一瓶药水", BtnState::Focus);
  button(x, 12 + 2 * (h + 8), w, h, "献上药水", BtnState::Disabled);
  R().text(kBot / 2.f, 12 + 2 * (h + 8) + h + 2, "需要一瓶药水", ts(F12, col::red, CENTER));
  button(kBot - kMargin - 96, kActionY + 4, 96, 30, "牌组");
}

}  // namespace

void drawStyleMock(int which, bool top) {
  switch (which) {
    case 1: mockReward(top); break;
    case 2: mockCombat(top); break;
    default: mockEvent(top); break;
  }
}

}  // namespace ui
