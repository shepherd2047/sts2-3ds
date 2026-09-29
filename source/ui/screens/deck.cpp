// Split from ui.cpp (F3). S13: the deck grid select and choose-one screens (RGDSplus U15/U16).
#include <map>

#include "../ui_common.h"

namespace ui {

// ================================================================ deck view

std::vector<Card*> App::listedCards() {
  std::vector<Card*> cards;
  if (cardListMode_ == CardListMode::Deck || !run_->combat) {
    for (auto& c : run_->deck) cards.push_back(c.get());
    return cards;
  }
  Combat& cb = *run_->combat;
  const std::vector<Card*>& pile = cardListMode_ == CardListMode::Draw ? cb.draw
                                   : cardListMode_ == CardListMode::Discard ? cb.discard : cb.exhaust;
  cards.assign(pile.begin(), pile.end());
  // The draw-pile page must not reveal its actual next-card order.
  std::stable_sort(cards.begin(), cards.end(), [&](Card* a, Card* b) { return cardTitle(a) < cardTitle(b); });
  return cards;
}

void App::openCardList(CardListMode mode) {
  cardListMode_ = mode;
  deckOpen_ = true;
  sel_ = -1;
  scroll_ = 0;
}

void App::drawDeck(bool top) {
  std::vector<Card*> cards = listedCards();
  const char* title = cardListMode_ == CardListMode::Deck ? "牌组" :
                      cardListMode_ == CardListMode::Draw ? "抽牌堆" :
                      cardListMode_ == CardListMode::Discard ? "弃牌堆" : "消耗堆";
  if (top) {
    drawSceneBg(true, 0.7f);
    drawTopBar();
    if (sel_ >= 0 && sel_ < (int)cards.size()) drawCard(cards[sel_], (kTop - 132) / 2, 34, 1.1f, false, true);
    else R().text(kTop / 2, 100, std::string(title) + "（" + num((int)cards.size()) + " 张）", ts(F16, col::gold, CENTER));
    if (cardListMode_ == CardListMode::Draw && sel_ < 0)
      R().text(kTop / 2, 211, "不显示实际抽牌顺序", ts(F12, col::gray, CENTER));
    return;
  }
  drawSceneBg(false, 0.65f);
  drawCardGrid(cards, sel_, 0, 196, scroll_);
  gfx::rect(0, 196, kBot, 44, 0x000000A0);
  if (cardListMode_ == CardListMode::Deck) {
    button(10, 200, 100, 34, "返回", ID_BACK);
    button(118, 200, 84, 34, "详情", ID_DETAIL, sel_ >= 0 && sel_ < (int)cards.size());
    button(kBot - 110, 200, 100, 34, "遗物", ID_RELICS);
  } else {
    button(4, 200, 64, 34, "返回", ID_BACK);
    button(72, 200, 76, 34, "抽牌", ID_PILE_DRAW, true, cardListMode_ == CardListMode::Draw);
    button(154, 200, 76, 34, "弃牌", ID_PILE_DISCARD, true, cardListMode_ == CardListMode::Discard);
    button(236, 200, 76, 34, "消耗", ID_PILE_EXHAUST, true, cardListMode_ == CardListMode::Exhaust);
  }
}

void App::updateDeck(const gfx::Input& in) {
  std::vector<Card*> cards = listedCards();
  int m = (int)cards.size();
  if (cardListMode_ != CardListMode::Deck && (in.down & (gfx::BTN_L | gfx::BTN_R))) {
    int mode = (int)cardListMode_ - (int)CardListMode::Draw;
    mode = (mode + ((in.down & gfx::BTN_R) ? 1 : 2)) % 3;
    openCardList((CardListMode)((int)CardListMode::Draw + mode));
    return;
  }
  if (in.down & gfx::BTN_RIGHT) sel_ = std::min(m - 1, sel_ + 1);
  if (in.down & gfx::BTN_LEFT) sel_ = std::max(0, sel_ - 1);
  if (in.down & gfx::BTN_DOWN) sel_ = std::min(m - 1, sel_ + 5);
  if (in.down & gfx::BTN_UP) sel_ = std::max(0, sel_ - 5);
  if (sel_ >= 0) scroll_ = std::max(0, sel_ / 5 - 1);
  if (in.down & (gfx::BTN_B | gfx::BTN_Y)) { deckOpen_ = false; cardListMode_ = CardListMode::Deck; sel_ = -1; return; }
  if ((in.down & gfx::BTN_A) && sel_ >= 0 && sel_ < m) { detailCard_ = cards[sel_]; detailUpgrade_ = false; return; }
  if (in.touchDown) {
    int id = hitAt(in.tx, in.ty);
    if (id >= ID_GRID0 && id < ID_GRID0 + m) {
      int picked = id - ID_GRID0;
      if (sel_ == picked) { detailCard_ = cards[picked]; detailUpgrade_ = false; return; }
      sel_ = picked;
    }
    if (id == ID_DETAIL && sel_ >= 0 && sel_ < m) { detailCard_ = cards[sel_]; detailUpgrade_ = false; return; }
    if (id == ID_BACK) { deckOpen_ = false; cardListMode_ = CardListMode::Deck; sel_ = -1; }
    if (id == ID_RELICS) { deckOpen_ = false; relicsOpen_ = true; sel_ = run_->relics.empty() ? -1 : 0; scroll_ = 0; }
    if (id == ID_PILE_DRAW) openCardList(CardListMode::Draw);
    if (id == ID_PILE_DISCARD) openCardList(CardListMode::Discard);
    if (id == ID_PILE_EXHAUST) openCardList(CardListMode::Exhaust);
  }
}

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
float gridMaxScroll(int n) { return std::max(0.f, gridRows(n) * kGRow + 8 - (kGY1 - kGY0)); }
float gridCellX(int i) { return kGCellGap + (i % kGPerRow) * (kGW + kGCellGap); }
float gridCellY(int i, float scroll) { return kGY0 + 6 + (i / kGPerRow) * kGRow - scroll; }

void gridScrollToSel(int n) {
  GridSel& g = GS();
  if (g.sel < 0) return;
  float top = (g.sel / kGPerRow) * kGRow, h = kGY1 - kGY0;
  if (top < g.scroll) g.scroll = top;
  if (top + kGRow + 4 > g.scroll + h) g.scroll = top + kGRow + 4 - h;
  g.scroll = std::clamp(g.scroll, 0.f, gridMaxScroll(n));
}

int gridCardAt(float tx, float ty, int n) {
  if (ty < kGY0 || ty >= kGY1) return -1;
  for (int i = 0; i < n; ++i) {
    float x = gridCellX(i), y = gridCellY(i, GS().scroll);
    if (tx >= x && tx < x + kGW && ty >= y && ty < y + kGH) return i;
  }
  return -1;
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
    detailCard_ = (*s.cards)[i];
    detailUpgrade_ = s.upgrade && detailCard_->upgradable();
    detailKeyword_ = -1;
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
    g.touchCard = gridCardAt((float)in.tx, (float)in.ty, n);
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
    if (!g.dragged && i >= 0 && gridCardAt(g.touchX, g.touchLastY, n) == i) {
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
  if (gridMaxScroll(n) > 0) {  // scrollbar
    float h = kGY1 - kGY0, th = std::max(16.f, h * h / (h + gridMaxScroll(n)));
    float ty = kGY0 + (h - th) * (g.scroll / gridMaxScroll(n));
    gfx::rect(kBot - 4, kGY0, 3, h, 0x00000080);
    gfx::rect(kBot - 4, ty, 3, th, 0xC8B080FF);
  }
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
    if (id == kOneDetail && o.sel >= 0) { detailCard_ = s.cards[o.sel]; detailUpgrade_ = false; detailKeyword_ = -1; }
    if (id >= kOneCard0 && id < kOneCard0 + n) {
      int i = id - kOneCard0;
      if (o.sel == i) return take(i);
      o.sel = i;
    }
    return -2;
  }
  const uint32_t d = in.down;
  if ((d & gfx::BTN_B) && s.canSkip) return take(-1);
  if ((d & gfx::BTN_X) && o.sel >= 0) { detailCard_ = s.cards[o.sel]; detailUpgrade_ = false; detailKeyword_ = -1; return -2; }
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

}  // namespace ui
