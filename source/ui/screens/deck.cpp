// Split from ui.cpp (F3). S13: the deck grid select and choose-one screens (RGDSplus U15/U16).
// S11: the deck view and the combat pile views (RGDSplus U13), at the end of the file.
#include <map>

#include "../ui_common.h"

namespace ui {

// ================================================================ deck grid select (S13, U15)

// S13 (RGDSplus U15, C# NCardGridSelectionScreen and its deck subclasses NDeckUpgradeSelectScreen /
// NDeckCardSelectScreen / NDeckTransformSelectScreen / NDeckEnchantSelectScreen): the offered deck
// cards as a scrolling grid of grid-mini cards (F5, five a row) on the bottom screen with an action
// bar (取消 when the selection can be cancelled, 详情, the "已选 k/N" counter, 确认); the focused card
// previewed on the top screen: upgrade prompts show it before -> after side by side (C#
// NUpgradePreview), the others show it large (a transform's random result is never shown, RGDSplus U15).
// One card (MaxSelect 1): the first tap focuses, a tap on the focused card (or A, or 确认) picks it
// (UI_STYLE fast play; on the 3DS the top screen already is the C# confirm preview). N cards: a tap
// (or A) toggles a pick; reaching N opens the review stage as the C# PreviewSelection does (every
// picked card on top, upgraded for upgrade prompts; 确认 / 返回), and an "up to N" choice (minCount <
// count) can go there early with 确认. Keys: D-pad moves the focus through the grid and down into the
// bar, A picks / presses, X shows the focused card's detail, B leaves the review / cancels (when
// allowed) / drops the focus. The rules (DeckChoice, Run::upgradeChoice) are unchanged.
// PORT NOTE: the enchant prompt shows the card large, not enchanted (DeckChoice carries no id).
namespace {
constexpr float kGS = 0.46f, kGW = 120 * kGS, kGH = 169 * kGS, kGRow = kGH + 8;  // F5 grid mini
constexpr int kGPerRow = 5;
constexpr float kGY0 = 0, kGY1 = 194;                              // grid area (bottom screen)
constexpr float kGCellGap = (kBot - kGPerRow * kGW) / (kGPerRow + 1);
constexpr float kTapSlop = 5;
constexpr int kSelCancel = 1301, kSelDetail = 1302, kSelConfirm = 1303, kSelBack = 1304;
constexpr int kOneSkip = 1311, kOneDetail = 1312, kOnePick = 1313, kOneCard0 = 1320;

// The frame's input as App::update saw it, for the action-bar widgets drawn later in the frame
// (a second gfx::input() in the same frame would not report the key presses again).
gfx::Input frameIn;
bool frameInSet = false;
gfx::Input takeFrameInput() {
  gfx::Input in = frameInSet ? frameIn : gfx::Input{};
  frameInSet = false;
  return in;
}

struct GridSel {
  const void* key = nullptr;  // the options vector's storage + size + prompt identify one selection
  size_t n = 0;
  std::string prompt;
  int sel = -1;
  std::vector<int> picks;
  bool review = false;
  int zone = 0;  // 0 grid, 1 action bar
  float scroll = 0;
  bool touchDown = false, dragged = false;
  float touchY0 = 0, touchLastY = 0, touchX = 0;  // last touching point (touchUp has no position)
  int touchCard = -1;
  int pending = 0;  // action-bar widget pressed during the last draw
  std::map<Card*, std::unique_ptr<Card>> upgraded;  // upgrade previews, made on first need
};
GridSel& GS() {
  static GridSel g;
  return g;
}

void gridSync(const App::GridSelectSpec& s) {
  GridSel& g = GS();
  const void* key = s.cards && !s.cards->empty() ? (const void*)s.cards->data() : nullptr;
  size_t n = s.cards ? s.cards->size() : 0;
  if (key == g.key && n == g.n && s.prompt == g.prompt) return;
  g = GridSel{};
  g.key = key;
  g.n = n;
  g.prompt = s.prompt;
}

Card* upgradedCopy(Card* c) {
  auto& slot = GS().upgraded[c];
  if (!slot) {
    slot = c->clone();
    slot->upgrade();
  }
  return slot.get();
}

int gridRows(int n) { return (n + kGPerRow - 1) / kGPerRow; }
float gridMaxScroll(int n, float h = kGY1 - kGY0) { return std::max(0.f, gridRows(n) * kGRow + 8 - h); }
float gridCellX(int i) { return kGCellGap + (i % kGPerRow) * (kGW + kGCellGap); }
float gridCellY(int i, float scroll, float y0 = kGY0) { return y0 + 6 + (i / kGPerRow) * kGRow - scroll; }

void gridScrollToSel(int n) {
  GridSel& g = GS();
  if (g.sel < 0) return;
  float top = (g.sel / kGPerRow) * kGRow, h = kGY1 - kGY0;
  if (top < g.scroll) g.scroll = top;
  if (top + kGRow + 4 > g.scroll + h) g.scroll = top + kGRow + 4 - h;
  g.scroll = std::clamp(g.scroll, 0.f, gridMaxScroll(n));
}

int gridCardAt(float tx, float ty, int n, float scroll, float y0 = kGY0, float y1 = kGY1) {
  if (ty < y0 || ty >= y1) return -1;
  for (int i = 0; i < n; ++i) {
    float x = gridCellX(i), y = gridCellY(i, scroll, y0);
    if (tx >= x && tx < x + kGW && ty >= y && ty < y + kGH) return i;
  }
  return -1;
}

// The grid's scrollbar at the right edge (only when the grid overflows).
void gridScrollbar(float y0, float y1, float scroll, float maxScroll) {
  if (maxScroll <= 0) return;
  float h = y1 - y0, th = std::max(16.f, h * h / (h + maxScroll));
  float ty = y0 + (h - th) * (scroll / maxScroll);
  gfx::rect(kBot - 4, y0, 3, h, 0x00000080);
  gfx::rect(kBot - 4, ty, 3, th, 0xC8B080FF);
}

std::string promptText(const std::string& key, int count) {
  std::string p = res().hasLoc(key) ? L(key) : key;
  for (size_t at; (at = p.find("{Amount}")) != std::string::npos;) p.replace(at, 8, num(count));
  return p;
}

void outlineBox(float x, float y, float w, float h, uint32_t c, float t = 2) {
  gfx::rect(x - t, y - t, w + 2 * t, t, c);
  gfx::rect(x - t, y + h, w + 2 * t, t, c);
  gfx::rect(x - t, y, t, h, c);
  gfx::rect(x + w, y, t, h, c);
}

// Screen title (UI_STYLE): F16 x 1.25, gold, a 2 px teal line under it.
void screenTitle(float cx, float y, const std::string& text) {
  TextStyle t = ts(F16, col::gold, CENTER, kTop - 20, 1.25f);
  R().text(cx, y, text, t);
  float w = std::min(kTop - 40.f, R().measure(text, t) + 24);
  gfx::rect(cx - w / 2, y + 23, w, 2, style::kPanelHi);
}

// The widget kit's input for this frame: in the grid zone the D-pad / A / B belong to the grid
// (App::gridSelectUpdate), in the bar zone UP leads back to the grid.
gfx::Input barInput(int zone) {
  gfx::Input in = takeFrameInput();
  if (zone == 0) in.down &= ~(gfx::BTN_LEFT | gfx::BTN_RIGHT | gfx::BTN_UP | gfx::BTN_DOWN | gfx::BTN_A);
  else in.down &= ~gfx::BTN_UP;
  return in;
}
}  // namespace

void App::gridSelectReset() { GS() = GridSel{}; }

int App::gridSelectUpdate(const GridSelectSpec& s, const gfx::Input& in, std::vector<int>& picked) {
  gridSync(s);
  frameIn = in;
  frameInSet = true;
  GridSel& g = GS();
  const int n = s.cards ? (int)s.cards->size() : 0;
  const bool multi = s.count > 1 || s.minCount == 0;
  const int need = std::min(s.count, n);
  const int least = s.minCount >= 0 ? std::min(s.minCount, need) : need;
  if (g.sel >= n) g.sel = -1;
  auto done = [&](int result, std::vector<int> out) {
    picked = std::move(out);
    gridSelectReset();
    return result;
  };
  auto confirm = [&]() -> int {
    if (!multi) return g.sel >= 0 ? done(1, {g.sel}) : 0;
    if ((int)g.picks.size() < least || (int)g.picks.size() > need) return 0;
    if (g.review || g.picks.empty()) return done(1, g.picks);
    g.review = true;
    return 0;
  };
  auto toggle = [&](int i) -> int {
    auto it = std::find(g.picks.begin(), g.picks.end(), i);
    if (it != g.picks.end()) { g.picks.erase(it); sfx::click(); return 0; }
    if ((int)g.picks.size() >= need) return 0;
    g.picks.push_back(i);
    sfx::click();
    if ((int)g.picks.size() == need) g.review = true;  // C# OnCardClicked: MaxSelect reached -> PreviewSelection
    return 0;
  };
  auto detail = [&](int i) {
    if (i < 0 || i >= n) return;
    inspectCard(*s.cards, i, s.upgrade);
  };

  // The action bar (pressed in the last draw).
  if (int id = g.pending) {
    g.pending = 0;
    if (id == kSelCancel && s.canCancel) return done(-1, {});
    if (id == kSelBack) { g.review = false; return 0; }
    if (id == kSelDetail) { detail(g.sel); return 0; }
    if (id == kSelConfirm) return confirm();
  }
  const uint32_t d = in.down;
  if (g.review) {  // the review stage: only 确认 / 返回
    if (d & gfx::BTN_B) g.review = false;
    else if (d & gfx::BTN_A) return confirm();
    return 0;
  }
  if (d & gfx::BTN_B) {
    if (s.canCancel) return done(-1, {});
    g.sel = -1;
    g.zone = 0;
    return 0;
  }
  if (d & gfx::BTN_X) { detail(g.sel); return 0; }

  // Touch: the grid scrolls on drag; a tap focuses, and picks (one card: the focused one again).
  if (in.touchDown && in.ty < kGY1) {
    g.touchDown = true;
    g.dragged = false;
    g.touchY0 = g.touchLastY = (float)in.ty;
    g.touchCard = gridCardAt((float)in.tx, (float)in.ty, n, g.scroll);
    g.touchX = (float)in.tx;
  } else if (g.touchDown && in.touching) {
    g.touchX = (float)in.tx;
    if (std::fabs(in.ty - g.touchY0) > kTapSlop) g.dragged = true;
    if (g.dragged) g.scroll = std::clamp(g.scroll - (in.ty - g.touchLastY), 0.f, gridMaxScroll(n));
    g.touchLastY = (float)in.ty;
  }
  if (in.touchUp && g.touchDown) {
    g.touchDown = false;
    int i = g.touchCard;
    if (!g.dragged && i >= 0 && gridCardAt(g.touchX, g.touchLastY, n, g.scroll) == i) {
      g.zone = 0;
      widgets::setFocus(-1);
      if (multi) { g.sel = i; return toggle(i); }
      if (g.sel == i) return confirm();
      g.sel = i;
    }
    return 0;
  }

  if (g.zone == 1) {  // the action bar: the widget kit moves the focus and presses; UP goes back
    if ((d & gfx::BTN_UP) && n > 0) { g.zone = 0; widgets::setFocus(-1); gridScrollToSel(n); }
    return 0;
  }
  if (n == 0) { if (d & (gfx::BTN_DOWN | gfx::BTN_A)) { g.zone = 1; widgets::setFocus(kSelConfirm); } return 0; }
  if (d & (gfx::BTN_LEFT | gfx::BTN_RIGHT | gfx::BTN_UP | gfx::BTN_DOWN)) {
    if (g.sel < 0) g.sel = 0;
    else if (d & gfx::BTN_RIGHT) g.sel = std::min(n - 1, g.sel + 1);
    else if (d & gfx::BTN_LEFT) g.sel = std::max(0, g.sel - 1);
    else if (d & gfx::BTN_UP) g.sel = std::max(0, g.sel - kGPerRow);
    else if (d & gfx::BTN_DOWN) {
      if (g.sel / kGPerRow == (n - 1) / kGPerRow) {
        g.zone = 1;
        widgets::setFocus(kSelConfirm);
      } else {
        g.sel = std::min(n - 1, g.sel + kGPerRow);
      }
    }
    gridScrollToSel(n);
  }
  if ((d & gfx::BTN_A) && g.sel >= 0) return multi ? toggle(g.sel) : confirm();
  return 0;
}

void App::gridSelectDraw(const GridSelectSpec& s, bool top) {
  gridSync(s);
  GridSel& g = GS();
  const int n = s.cards ? (int)s.cards->size() : 0;
  const bool multi = s.count > 1 || s.minCount == 0;
  const int need = std::min(s.count, n);
  const int least = s.minCount >= 0 ? std::min(s.minCount, need) : need;
  const std::string counter = "已选 " + num((int)g.picks.size()) + "/" + num(need);
  if (top) {
    screenTitle(kTop / 2, 24, promptText(s.prompt, s.count));
    if (g.review) {  // every picked card (upgraded for upgrade prompts), side by side
      int k = (int)g.picks.size();
      float sc = std::min({1.05f, (kTop - 16.f - (k - 1) * 8.f) / (k * 120.f), (kH - 66.f) / 169.f});
      float w = 120 * sc, x0 = (kTop - (k * w + (k - 1) * 8)) / 2;
      for (int j = 0; j < k; ++j) {
        Card* c = (*s.cards)[g.picks[j]];
        drawCard(s.upgrade && c->upgradable() ? upgradedCopy(c) : c, x0 + j * (w + 8), 62, sc, false, true);
      }
      return;
    }
    if (g.sel >= 0 && g.sel < n) {
      Card* c = (*s.cards)[g.sel];
      if (s.upgrade && c->upgradable()) {  // NUpgradePreview: before -> after
        drawCard(c, 50, 62, 1.0f, false, true);
        Sprite arrow = R().sprite("ui/arrow_right");
        if (arrow) spr(arrow, kTop / 2 - 14, 134, 28, 28 * arrow.h / std::max(1.f, arrow.w));
        else R().text(kTop / 2, 128, "→", ts(F16, col::gold, CENTER, 0, 1.6f));
        drawCard(upgradedCopy(c), kTop - 50 - 120, 62, 1.0f, false, true);
      } else {
        drawCard(c, (kTop - 120.f) / 2, 62, 1.0f, false, true);
      }
      bool picked = std::find(g.picks.begin(), g.picks.end(), g.sel) != g.picks.end();
      if (multi) R().text(kTop - 10, kH - 18, picked ? counter + "  已选择这张" : counter, ts(F12, picked ? col::gold : col::white, RIGHT));
      return;
    }
    R().text(kTop / 2, 110, multi ? "点选 " + num(need) + " 张牌" : "点选一张牌，再点一次确认",
             ts(F16, col::white, CENTER));
    if (multi) R().text(kTop / 2, 136, counter, ts(F12, col::gold, CENTER));
    return;
  }

  if (g.review) {  // the review stage: the picks small in a row, 返回 / 确认
    R().text(kBot / 2, 8, "确认选择？", ts(F16, col::gold, CENTER));
    int k = (int)g.picks.size();
    float sc = std::min(0.62f, (kBot - 16.f - (k - 1) * 6.f) / (k * 120.f));
    float w = 120 * sc, x0 = (kBot - (k * w + (k - 1) * 6)) / 2;
    for (int j = 0; j < k; ++j) {
      Card* c = (*s.cards)[g.picks[j]];
      drawCard(s.upgrade && c->upgradable() ? upgradedCopy(c) : c, x0 + j * (w + 6), 40, sc, false, true);
    }
    gfx::rect(0, kGY1, kBot, kH - kGY1, style::kScrim);
    widgets::beginFrame(barInput(1));
    if (widgets::button(kSelBack, style::kMargin, style::kActionY, 80, style::kButtonH, "返回")) g.pending = kSelBack;
    R().text(kBot / 2, style::kActionY + 10, counter, ts(F12, col::white, CENTER));
    if (widgets::button(kSelConfirm, kBot - style::kMargin - 90, style::kActionY, 90, style::kButtonH, "确认",
                        widgets::Kind::Primary))
      g.pending = kSelConfirm;
    widgets::endFrame();
    return;
  }

  // The grid: only visible rows; the focused card outlined, picks marked with a tick.
  gfx::pushClip(0, kGY0, kBot, kGY1 - kGY0);
  for (int i = 0; i < n; ++i) {
    float x = gridCellX(i), y = gridCellY(i, g.scroll);
    if (y + kGH < kGY0 || y > kGY1) continue;
    bool picked = std::find(g.picks.begin(), g.picks.end(), i) != g.picks.end();
    drawCard((*s.cards)[i], x, y, kGS, false, false, i == g.sel && !picked);
    if (picked) {
      outlineBox(x, y, kGW, kGH, style::kFocus);
      Sprite tick = R().sprite("ui/checkbox_on");
      if (tick) spr(tick, x + kGW - 18, y - 4, 22, 22);
      else gfx::circle(x + kGW - 6, y + 6, 6, style::kFocus);
    }
  }
  gfx::popClip();
  gridScrollbar(kGY0, kGY1, g.scroll, gridMaxScroll(n));
  gfx::rect(0, kGY1, kBot, kH - kGY1, style::kScrim);
  widgets::beginFrame(barInput(g.zone));
  if (g.zone == 0) widgets::setFocus(-1);
  float x = style::kMargin;
  if (s.canCancel) {
    if (widgets::button(kSelCancel, x, style::kActionY, 64, style::kButtonH, "取消")) g.pending = kSelCancel;
    x += 64 + style::kGap;
  }
  if (widgets::button(kSelDetail, x, style::kActionY, 60, style::kButtonH, "详情", widgets::Kind::Secondary,
                      g.sel >= 0 && g.sel < n))
    g.pending = kSelDetail;
  x += 60;
  const float cx = kBot - style::kMargin - 90;
  if (multi) R().text((x + cx) / 2, style::kActionY + 10, counter, ts(F12, (int)g.picks.size() >= least ? col::gold : col::white, CENTER));
  bool ready = multi ? (int)g.picks.size() >= least && (int)g.picks.size() <= need : g.sel >= 0 && g.sel < n;
  if (widgets::button(kSelConfirm, cx, style::kActionY, 90, style::kButtonH, "确认", widgets::Kind::Primary, ready))
    g.pending = kSelConfirm;
  widgets::endFrame();
}

// ---------------------------------------------------------------- DeckChoice (CardSelectCmd.FromDeck*)

namespace {
App::GridSelectSpec deckSpec(const DeckChoice& d) {
  App::GridSelectSpec s;
  s.cards = &d.options;
  s.prompt = d.prompt;
  s.count = d.count;
  s.minCount = d.minCount;
  s.canCancel = d.canCancel;
  s.upgrade = d.showUpgrade || d.prompt == "card_selection.TO_UPGRADE";
  return s;
}
}  // namespace

void App::drawDeckChoice(bool top) {
  drawSceneBg(top, top ? 0.7f : 0.65f);
  if (top) drawTopBar();
  gridSelectDraw(deckSpec(run_->deckChoice), top);
}

void App::updateDeckChoice(const gfx::Input& in) {
  DeckChoice& d = run_->deckChoice;
  if (!d.result.waiting()) return;
  std::vector<int> picked;
  int r = gridSelectUpdate(deckSpec(d), in, picked);
  if (r == 0) return;
  std::vector<Card*> out;
  if (r > 0)
    for (int i : picked) out.push_back(d.options[i]);
  d.result.fire(std::move(out));
}

// ================================================================ choose one (S13, U16)

// S13 (RGDSplus U16, C# NChooseACardSelectionScreen: CardSelectCmd.FromChooseACardScreen, e.g. an
// event's or relic's card choice, a potion's generated cards in combat; the combat card reward uses
// it too): the CHOOSE_CARD_HEADER banner and the offered cards in one row on the bottom, with
// 跳过 when the choice can be skipped (NChoiceSelectionSkipButton), 详情 and 选择; the focused card
// large on the top screen with its text and keywords. The first tap focuses, a tap on the focused
// card (or A, or 选择) takes it; D-pad left/right move the focus, down goes to the buttons; X detail;
// B skips when allowed.
namespace {
struct OneSel {
  const void* key = nullptr;
  size_t n = 0;
  int sel = -1, zone = 0, pending = 0;
};
OneSel& OS() {
  static OneSel o;
  return o;
}
void oneSync(const App::ChooseOneSpec& s) {
  const void* key = s.cards.empty() ? nullptr : (const void*)s.cards[0];
  if (key == OS().key && s.cards.size() == OS().n) return;
  OS() = OneSel{};
  OS().key = key;
  OS().n = s.cards.size();
}
float oneScale(int n) { return std::min(0.78f, (kBot - 16.f - (n - 1) * 8.f) / (std::max(1, n) * 120.f)); }
float oneX(int i, int n) {
  float w = 120 * oneScale(n), gap = std::min(24.f, (kBot - n * w) / (n + 1));
  float x0 = (kBot - (n * w + (n - 1) * gap)) / 2;
  return x0 + i * (w + gap);
}
constexpr float kOneY = 44;

std::vector<const char*> keywordKeys(const Card* c) {
  std::vector<const char*> keys;
  if (c->has(kwUnplayable)) keys.push_back("UNPLAYABLE");
  if (c->has(kwEthereal)) keys.push_back("ETHEREAL");
  if (c->has(kwInnate)) keys.push_back("INNATE");
  if (c->has(kwRetain)) keys.push_back("RETAIN");
  if (c->has(kwExhaust)) keys.push_back("EXHAUST");
  return keys;
}
}  // namespace

// The focused card inspected on the top screen: large at the left, title and keywords at the right.
void App::drawCardInspect(Card* c, float y) {
  const float cs = 1.05f, cx = 20;
  drawCard(c, cx, y, cs, false, true);
  const float rx = cx + 120 * cs + 14, rw = kTop - rx - 10;
  float ty = y;
  R().text(rx, ty, cardTitle(c), ts(F16, c->upgraded() ? col::green : col::gold, LEFT, rw));
  ty += 24;
  for (const char* k : keywordKeys(c)) {
    if (ty > kH - 40) break;
    std::string key = std::string("card_keywords.") + k;
    TextStyle kt = ts(F12, 0xD8D8D8FF, LEFT, rw - 12);
    float dh = 0;
    std::string desc = L(key + ".description");
    R().measure(desc, kt, &dh);
    float ph = dh + 26;
    widgets::panel("ui/hover_tip", rx - 4, ty, rw + 4, ph);
    R().text(rx + 4, ty + 5, L(key + ".title"), ts(F12, col::gold));
    R().text(rx + 4, ty + 20, desc, kt);
    ty += ph + 4;
  }
}

int App::chooseOneUpdate(const ChooseOneSpec& s, const gfx::Input& in) {
  oneSync(s);
  frameIn = in;
  frameInSet = true;
  OneSel& o = OS();
  const int n = (int)s.cards.size();
  if (o.sel >= n) o.sel = -1;
  auto take = [&](int i) { OS() = OneSel{}; return i; };
  if (int id = o.pending) {
    o.pending = 0;
    if (id == kOneSkip && s.canSkip) return take(-1);
    if (id == kOnePick && o.sel >= 0) return take(o.sel);
    if (id == kOneDetail && o.sel >= 0) inspectCard(s.cards, o.sel);
    if (id >= kOneCard0 && id < kOneCard0 + n) {
      int i = id - kOneCard0;
      if (o.sel == i) return take(i);
      o.sel = i;
    }
    return -2;
  }
  const uint32_t d = in.down;
  if ((d & gfx::BTN_B) && s.canSkip) return take(-1);
  if ((d & gfx::BTN_X) && o.sel >= 0) { inspectCard(s.cards, o.sel); return -2; }
  if (o.zone == 1) {
    if (d & gfx::BTN_UP) { o.zone = 0; widgets::setFocus(-1); if (o.sel < 0 && n) o.sel = 0; }
    return -2;
  }
  if (n == 0) return (d & gfx::BTN_A) && s.canSkip ? take(-1) : -2;
  if (d & (gfx::BTN_LEFT | gfx::BTN_RIGHT)) {
    if (o.sel < 0) o.sel = 0;
    else if (d & gfx::BTN_RIGHT) o.sel = std::min(n - 1, o.sel + 1);
    else o.sel = std::max(0, o.sel - 1);
  }
  if (d & gfx::BTN_DOWN) { o.zone = 1; widgets::setFocus(o.sel >= 0 ? kOnePick : s.canSkip ? kOneSkip : kOnePick); }
  if ((d & gfx::BTN_UP) && o.sel < 0) o.sel = 0;
  if ((d & gfx::BTN_A) && o.sel >= 0) return take(o.sel);
  return -2;
}

void App::chooseOneDraw(const ChooseOneSpec& s, bool top) {
  oneSync(s);
  OneSel& o = OS();
  const int n = (int)s.cards.size();
  if (top) {
    if (o.sel >= 0 && o.sel < n) { drawCardInspect(s.cards[o.sel], 34); return; }
    screenTitle(kTop / 2, 70, s.title.empty() ? L("gameplay_ui.CHOOSE_CARD_HEADER") : s.title);
    if (!s.sub.empty()) R().text(kTop / 2, 116, s.sub, ts(F16, col::white, CENTER, kTop - 40));
    R().text(kTop / 2, 150, s.canSkip ? "点选一张牌，再点一次拿取；也可以跳过" : "点选一张牌，再点一次拿取",
             ts(F12, col::gray, CENTER));
    return;
  }
  widgets::banner(kBot / 2, 1, "", 0.62f);  // the ribbon; its label at a readable size
  R().text(kBot / 2, 7, L("gameplay_ui.CHOOSE_CARD_HEADER"), ts(F16, col::dark, CENTER, 0, 0.9f));
  gfx::Input in = takeFrameInput();
  if (o.zone == 0) in.down &= ~(gfx::BTN_LEFT | gfx::BTN_RIGHT | gfx::BTN_UP | gfx::BTN_DOWN | gfx::BTN_A);
  else in.down &= ~gfx::BTN_UP;
  widgets::beginFrame(in);
  if (o.zone == 0) widgets::setFocus(-1);
  const float sc = oneScale(n), w = 120 * sc, h = 169 * sc;
  for (int i = 0; i < n; ++i) {
    float x = oneX(i, n), y = kOneY - (i == o.sel ? 4.f : 0.f);
    if (widgets::hit(kOneCard0 + i, x, kOneY - 4, w, h + 4)) o.pending = kOneCard0 + i;
    drawCard(s.cards[i], x, y, sc, false, true, i == o.sel);
  }
  gfx::rect(0, kGY1, kBot, kH - kGY1, style::kScrim);
  if (s.canSkip && widgets::button(kOneSkip, style::kMargin, style::kActionY, 84, style::kButtonH,
                                   L("gameplay_ui.CHOOSE_CARD_SKIP_BUTTON")))
    o.pending = kOneSkip;
  float dx = style::kMargin + (s.canSkip ? 84 + style::kGap : 0);
  if (widgets::button(kOneDetail, dx, style::kActionY, 60, style::kButtonH, "详情", widgets::Kind::Secondary,
                      o.sel >= 0 && o.sel < n))
    o.pending = kOneDetail;
  if (widgets::button(kOnePick, kBot - style::kMargin - 90, style::kActionY, 90, style::kButtonH, "选择",
                      widgets::Kind::Primary, o.sel >= 0 && o.sel < n))
    o.pending = kOnePick;
  widgets::endFrame();
}

// A combat CardChoice is the choose-one screen when it offers generated cards (none of them in a
// combat pile yet: CardSelectCmd.FromChooseACardScreen, e.g. Attack Potion); a choice from the
// hand / piles stays the combat grid (S12).
bool App::combatChooseOne() const {
  const Combat* cb = run_ ? run_->combat.get() : nullptr;
  if (!cb || !cb->choice.active || cb->choice.maxCount != 1 || cb->choice.options.empty() ||
      cb->choice.options.size() > 5)
    return false;
  for (Card* c : cb->choice.options)
    for (const std::vector<Card*>* pile : {&cb->hand, &cb->draw, &cb->discard, &cb->exhaust})
      if (std::find(pile->begin(), pile->end(), c) != pile->end()) return false;
  return true;
}

App::ChooseOneSpec App::combatChooseOneSpec() const {
  ChooseOneSpec s;
  const CardChoice& ch = run_->combat->choice;
  s.cards = ch.options;
  s.canSkip = ch.minCount == 0;
  s.title = R().hasLoc(ch.prompt) ? L(ch.prompt) : ch.prompt;
  return s;
}

// ================================================================ deck view and pile views (S11, U13)

// S11 (RGDSplus U13, C# NDeckViewScreen / NCardsViewScreen and NCardPileScreen): the listed cards
// as a scrolling grid of grid-mini cards (F5, five a row, drag to scroll) on the bottom screen, the
// focused card large on the top screen with its title and keywords (the overview -- title, count and
// the C# info line -- while nothing is focused).
// Deck (top bar / pause menu / map 牌组, combat Y): the four NCardViewSortButtons on top (获得顺序 /
// 类型 / 费用 / 拼音顺序). As in the C#, a press flips that button's direction (the first press sorts
// descending) and moves its order to the front of the sort priority, which starts obtained > type >
// cost > title; the bar has 返回, the 查看升级 tickbox (NCardsViewScreen: upgradable cards shown
// upgraded, and the detail opens on the upgrade), 详情 and 遗物.
// Combat piles (the pile corners): tabs 抽牌堆 / 弃牌堆 / 消耗堆 with their counts (L/R switch);
// the draw pile never shows its real order (NCardPileScreen.OnPileContentsChanged: rarity, then id
// entry), the discard and exhaust piles are in pile order.
// Keys: D-pad moves the focus through the strip / grid / bar, A or X on a card opens its detail
// (C# HolderPressed -> ShowCardDetail), B / Y close. Touch: the first tap focuses a card, a tap on
// the focused card opens its detail. Rules are untouched: the view only reads the deck / piles.
// PORT NOTE: 拼音顺序 compares titles by code point (the C# uses the zh-CN culture's collation), as M8.
namespace {
constexpr int kVTab0 = 1401, kVSort0 = 1411, kVBack = 1421, kVUpgrades = 1422, kVDetail = 1423, kVRelics = 1424;
constexpr float kVStripY = 2, kVStripH = 32;
constexpr float kVY0 = 36, kVY1 = kGY1;  // the grid area under the strip
// SortingOrders, the ones the deck view uses: index = sorter * 2 + descending.
enum VSort { kObtained, kType, kCost, kAlphabet };
const char* const kSortKeys[4] = {"gameplay_ui.SORT_OBTAINED", "gameplay_ui.SORT_TYPE", "gameplay_ui.SORT_COST",
                                  "gameplay_ui.SORT_ALPHABET"};

struct DeckView {
  int sel = -1, zone = 1, pending = 0;  // zone: 0 sort buttons / pile tabs, 1 grid, 2 action bar
  float scroll = 0;
  bool touchDown = false, dragged = false;
  float touchY0 = 0, touchLastY = 0, touchX = 0;
  int touchCard = -1;
  std::vector<int> priority{kObtained * 2, kType * 2, kCost * 2, kAlphabet * 2};  // NDeckViewScreen()
  bool desc[4] = {};  // NCardViewSortButton.IsDescending per sorter
  bool upgrades = false;
  std::map<Card*, std::unique_ptr<Card>> upgraded;
};
DeckView& DV() {
  static DeckView v;
  return v;
}

// A card as the grid shows it: upgraded while 查看升级 is ticked.
Card* viewCard(Card* c) {
  if (!DV().upgrades || !c->upgradable()) return c;
  auto& slot = DV().upgraded[c];
  if (!slot || slot->id != c->id || slot->upgradeLevel != c->upgradeLevel + 1) {
    slot = c->clone();
    slot->upgrade();
  }
  return slot.get();
}

void viewScrollToSel(int n) {
  DeckView& v = DV();
  if (v.sel < 0) return;
  float top = (v.sel / kGPerRow) * kGRow, h = kVY1 - kVY0;
  if (top < v.scroll) v.scroll = top;
  if (top + kGRow + 4 > v.scroll + h) v.scroll = top + kGRow + 4 - h;
  v.scroll = std::clamp(v.scroll, 0.f, gridMaxScroll(n, h));
}

// A small up / down triangle (the sort buttons' direction), pixel rows.
void sortArrow(float cx, float cy, bool down, uint32_t c) {
  for (int r = 0; r < 4; ++r) {
    float w = down ? 7 - 2 * r : 1 + 2 * r;
    gfx::rect(cx - w / 2, cy - 2 + r, w, 1, c);
  }
}
}  // namespace

std::vector<Card*> App::listedCards() {
  std::vector<Card*> cards;
  if (cardListMode_ == CardListMode::Deck || !run_->combat) {
    for (auto& c : run_->deck) cards.push_back(c.get());
    // NCardGrid.SetCards with NDeckViewScreen's sort priority.
    const std::vector<int>& pr = DV().priority;
    if (pr[0] == kObtained * 2 + 1) {
      std::reverse(cards.begin(), cards.end());
    } else if (pr[0] != kObtained * 2) {
      std::map<const Card*, int> index;
      std::map<const Card*, std::string> titles;
      for (int i = 0; i < (int)cards.size(); ++i) {
        index[cards[i]] = i;
        titles[cards[i]] = cardTitle(cards[i]);
      }
      auto cost = [](const Card* c) { return c->costsX ? c->xValue : std::max(0, c->costWithLocalMods()); };
      auto cmp = [](auto a, auto b) { return (a > b) - (a < b); };
      std::stable_sort(cards.begin(), cards.end(), [&](Card* a, Card* b) {
        for (int o : pr) {
          int d = 0;
          switch (o / 2) {
            case kObtained: d = cmp(index[a], index[b]); break;
            case kType: d = cmp((int)a->type, (int)b->type); break;
            case kCost: d = cmp(cost(a), cost(b)); break;
            case kAlphabet: d = cmp(titles[a].compare(titles[b]), 0); break;
          }
          if (o & 1) d = -d;
          if (d) return d < 0;
        }
        return a->locKey < b->locKey;
      });
    }
    return cards;
  }
  Combat& cb = *run_->combat;
  const std::vector<Card*>& pile = cardListMode_ == CardListMode::Draw ? cb.draw
                                   : cardListMode_ == CardListMode::Discard ? cb.discard : cb.exhaust;
  cards.assign(pile.begin(), pile.end());
  // The draw-pile page must not reveal its actual next-card order (rarity, then id entry).
  if (cardListMode_ == CardListMode::Draw)
    std::stable_sort(cards.begin(), cards.end(), [](Card* a, Card* b) {
      return a->rarity != b->rarity ? (int)a->rarity < (int)b->rarity : a->locKey < b->locKey;
    });
  return cards;
}

void App::openCardList(CardListMode mode) {
  // Switching pile tabs keeps the strip focus; every open starts fresh (the C# makes a new screen).
  const bool keepZone = deckOpen_ && mode != CardListMode::Deck && cardListMode_ != CardListMode::Deck;
  const int zone = DV().zone;
  DV() = DeckView{};
  if (keepZone) DV().zone = zone;
  cardListMode_ = mode;
  deckOpen_ = true;
  sel_ = -1;
  scroll_ = 0;
}

void App::drawDeck(bool top) {
  std::vector<Card*> cards = listedCards();
  DeckView& v = DV();
  const int n = (int)cards.size();
  const bool deck = cardListMode_ == CardListMode::Deck;
  const int tab = deck ? 0 : (int)cardListMode_ - (int)CardListMode::Draw;
  if (top) {
    drawSceneBg(true, 0.7f);
    drawTopBar();
    if (v.sel >= 0 && v.sel < n) {
      drawCardInspect(viewCard(cards[v.sel]), 30);
      R().text(kTop - 10, kH - 18, num(v.sel + 1) + " / " + num(n), ts(F12, col::gray, RIGHT));
      return;
    }
    static const char* const kTitles[4] = {"牌组", "抽牌堆", "弃牌堆", "消耗堆"};
    static const char* const kInfo[4] = {"gameplay_ui.DECK_PILE_INFO", "gameplay_ui.DRAW_PILE_INFO",
                                         "gameplay_ui.DISCARD_PILE_INFO", "gameplay_ui.EXHAUST_PILE_INFO"};
    const int m = deck ? 0 : 1 + tab;
    screenTitle(kTop / 2, 44, std::string(kTitles[m]) + "（" + num(n) + " 张）");
    R().text(kTop / 2, 88, L(kInfo[m]), ts(F12, col::white, CENTER, kTop - 40));
    R().text(kTop / 2, kH - 30, n ? "点选一张牌查看，再点一次打开详情" : "这里没有牌", ts(F12, col::gray, CENTER));
    return;
  }

  drawSceneBg(false, 0.65f);
  gfx::Input in = takeFrameInput();
  in.down &= ~(gfx::BTN_L | gfx::BTN_R);
  if (v.zone == 1) in.down &= ~(gfx::BTN_LEFT | gfx::BTN_RIGHT | gfx::BTN_UP | gfx::BTN_DOWN | gfx::BTN_A);
  else in.down &= ~(gfx::BTN_UP | gfx::BTN_DOWN);
  widgets::beginFrame(in);
  if (v.zone == 1) widgets::setFocus(-1);

  // The strip: the sort buttons (deck) or the pile tabs with their counts.
  if (deck) {
    const float w = (kBot - 2 * style::kMargin - 3 * style::kGap) / 4;
    for (int k = 0; k < 4; ++k) {
      float x = style::kMargin + k * (w + style::kGap);
      bool active = v.priority[0] / 2 == k;
      if (widgets::button(kVSort0 + k, x, kVStripY, w, kVStripH, "")) v.pending = kVSort0 + k;
      R().text(x + (w - 10) / 2, kVStripY + (kVStripH - R().lineHeight(F12)) / 2, L(kSortKeys[k]),
               ts(F12, active ? col::gold : col::white, CENTER, w - 14));
      sortArrow(x + w - 9, kVStripY + kVStripH / 2, v.desc[k], active ? col::gold : col::gray);
    }
  } else {
    const Combat* cb = run_->combat.get();
    auto count = [&](int t) {
      if (!cb) return std::string("0");
      return num((int)(t == 0 ? cb->draw : t == 1 ? cb->discard : cb->exhaust).size());
    };
    std::vector<std::string> labels = {"抽牌堆 " + count(0), "弃牌堆 " + count(1), "消耗堆 " + count(2)};
    int t = widgets::tabs(kVTab0, style::kMargin, kVStripY, kBot - 2 * style::kMargin, kVStripH, labels, tab);
    if (t != tab) v.pending = kVTab0 + t;
  }

  // The grid: only the visible rows; the focused card outlined.
  gfx::pushClip(0, kVY0, kBot, kVY1 - kVY0);
  for (int i = 0; i < n; ++i) {
    float x = gridCellX(i), y = gridCellY(i, v.scroll, kVY0);
    if (y + kGH < kVY0 || y > kVY1) continue;
    drawCard(viewCard(cards[i]), x, y, kGS, false, false, i == v.sel);
  }
  gfx::popClip();
  if (n == 0) R().text(kBot / 2, (kVY0 + kVY1) / 2 - 8, "（空）", ts(F16, col::gray, CENTER));
  gridScrollbar(kVY0, kVY1, v.scroll, gridMaxScroll(n, kVY1 - kVY0));

  // The action bar.
  gfx::rect(0, kGY1, kBot, kH - kGY1, style::kScrim);
  const bool focused = v.sel >= 0 && v.sel < n;
  if (widgets::button(kVBack, style::kMargin, style::kActionY, 64, style::kButtonH, "返回")) v.pending = kVBack;
  if (deck) {
    bool u = widgets::toggle(kVUpgrades, style::kMargin + 64 + 8, style::kActionY + 1, v.upgrades,
                             L("gameplay_ui.VIEW_UPGRADES"));
    if (u != v.upgrades) v.pending = kVUpgrades;
    if (widgets::button(kVDetail, kBot - style::kMargin - 64 - style::kGap - 60, style::kActionY, 60,
                        style::kButtonH, "详情", widgets::Kind::Secondary, focused))
      v.pending = kVDetail;
    if (widgets::button(kVRelics, kBot - style::kMargin - 64, style::kActionY, 64, style::kButtonH, "遗物"))
      v.pending = kVRelics;
  } else {
    if (widgets::button(kVDetail, kBot - style::kMargin - 64, style::kActionY, 64, style::kButtonH, "详情",
                        widgets::Kind::Secondary, focused))
      v.pending = kVDetail;
  }
  widgets::endFrame();
}

void App::updateDeck(const gfx::Input& in) {
  frameIn = in;
  frameInSet = true;
  std::vector<Card*> cards = listedCards();
  DeckView& v = DV();
  const int n = (int)cards.size();
  const bool deck = cardListMode_ == CardListMode::Deck;
  if (v.sel >= n) v.sel = n - 1;
  auto close = [&] {
    deckOpen_ = false;
    cardListMode_ = CardListMode::Deck;
    sel_ = -1;
    DV() = DeckView{};
  };
  auto detail = [&](int i) {
    if (i < 0 || i >= n) return;
    inspectCard(cards, i, v.upgrades);  // S20 popup, prev/next through this view
  };
  auto stripFocus = [&] {
    int c = v.sel >= 0 ? v.sel % kGPerRow : 0;
    return deck ? kVSort0 + std::min(3, c * 4 / kGPerRow) : kVTab0 + (int)cardListMode_ - (int)CardListMode::Draw;
  };
  auto pile = [&](int t) {
    openCardList((CardListMode)((int)CardListMode::Draw + t));
    if (DV().zone == 0) widgets::setFocus(kVTab0 + t);
  };

  if (int id = v.pending) {  // a strip / bar widget pressed in the last draw
    v.pending = 0;
    if (id == kVBack) { close(); return; }
    if (id == kVDetail) { detail(v.sel); return; }
    if (id == kVUpgrades) { v.upgrades = !v.upgrades; return; }
    if (id == kVRelics) {
      close();
      relicsOpen_ = true;
      sel_ = run_->relics.empty() ? -1 : 0;
      scroll_ = 0;
      return;
    }
    if (id >= kVSort0 && id < kVSort0 + 4) {  // NDeckViewScreen.On*Sort
      int k = id - kVSort0;
      v.desc[k] = !v.desc[k];
      v.priority.erase(std::remove_if(v.priority.begin(), v.priority.end(), [&](int o) { return o / 2 == k; }),
                       v.priority.end());
      v.priority.insert(v.priority.begin(), k * 2 + (v.desc[k] ? 1 : 0));
      v.sel = -1;
      v.scroll = 0;
      return;
    }
    if (id >= kVTab0 && id < kVTab0 + 3) { pile(id - kVTab0); return; }
  }
  const uint32_t d = in.down;
  if (!deck && (d & (gfx::BTN_L | gfx::BTN_R))) {
    int t = (int)cardListMode_ - (int)CardListMode::Draw;
    pile((t + ((d & gfx::BTN_R) ? 1 : 2)) % 3);
    return;
  }
  if (d & (gfx::BTN_B | gfx::BTN_Y)) { close(); return; }
  if (d & gfx::BTN_X) { detail(v.sel); return; }

  // Touch: the grid scrolls on drag; a tap focuses a card, a tap on the focused card opens its detail.
  if (in.touchDown && in.ty >= kVY0 && in.ty < kVY1) {
    v.touchDown = true;
    v.dragged = false;
    v.touchY0 = v.touchLastY = (float)in.ty;
    v.touchX = (float)in.tx;
    v.touchCard = gridCardAt((float)in.tx, (float)in.ty, n, v.scroll, kVY0, kVY1);
  } else if (v.touchDown && in.touching) {
    v.touchX = (float)in.tx;
    if (std::fabs(in.ty - v.touchY0) > kTapSlop) v.dragged = true;
    if (v.dragged) v.scroll = std::clamp(v.scroll - (in.ty - v.touchLastY), 0.f, gridMaxScroll(n, kVY1 - kVY0));
    v.touchLastY = (float)in.ty;
  }
  if (in.touchUp && v.touchDown) {
    v.touchDown = false;
    int i = v.touchCard;
    if (!v.dragged && i >= 0 && gridCardAt(v.touchX, v.touchLastY, n, v.scroll, kVY0, kVY1) == i) {
      v.zone = 1;
      widgets::setFocus(-1);
      if (v.sel == i) { detail(i); return; }
      v.sel = i;
      sfx::click();
    }
    return;
  }

  // D-pad: the strip and the bar belong to the widget kit (LEFT / RIGHT / A); UP / DOWN change zone.
  if (v.zone == 0 || v.zone == 2) {
    bool toGrid = v.zone == 0 ? (d & gfx::BTN_DOWN) != 0 : (d & gfx::BTN_UP) != 0;
    if (!toGrid) return;
    if (n > 0) {
      v.zone = 1;
      widgets::setFocus(-1);
      if (v.sel < 0) v.sel = 0;
      viewScrollToSel(n);
    } else {
      v.zone = 2 - v.zone;
      widgets::setFocus(v.zone == 2 ? kVBack : stripFocus());
    }
    return;
  }
  if (n == 0) {
    if (d & (gfx::BTN_DOWN | gfx::BTN_A)) { v.zone = 2; widgets::setFocus(kVBack); }
    else if (d & gfx::BTN_UP) { v.zone = 0; widgets::setFocus(stripFocus()); }
    return;
  }
  if (d & (gfx::BTN_LEFT | gfx::BTN_RIGHT | gfx::BTN_UP | gfx::BTN_DOWN)) {
    if (v.sel < 0) v.sel = 0;
    else if (d & gfx::BTN_RIGHT) v.sel = std::min(n - 1, v.sel + 1);
    else if (d & gfx::BTN_LEFT) v.sel = std::max(0, v.sel - 1);
    else if (d & gfx::BTN_UP) {
      if (v.sel < kGPerRow) { v.zone = 0; widgets::setFocus(stripFocus()); return; }
      v.sel -= kGPerRow;
    } else if (d & gfx::BTN_DOWN) {
      if (v.sel / kGPerRow == (n - 1) / kGPerRow) { v.zone = 2; widgets::setFocus(v.sel >= 0 ? kVDetail : kVBack); return; }
      v.sel = std::min(n - 1, v.sel + kGPerRow);
    }
    viewScrollToSel(n);
  }
  if ((d & gfx::BTN_A) && v.sel >= 0) detail(v.sel);
}

}  // namespace ui
