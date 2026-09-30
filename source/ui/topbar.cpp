// S19 (RGDSplus U24; C# NTopBar + NRelicInventory): the run's status bar across the top of the top
// screen, shared by every room. One 18 px row (no taller than the old text bar), left to right as
// in the C#: HP (NTopBarHp), gold (NTopBarGold), the potion belt (NPotionContainer), floor + act
// (NTopBarFloorIcon, ACT_NUMBER), the ascension badge; the relic strip (NRelicInventory, a second
// row in the C#) fills the middle and scrolls sideways when the relics overflow; on the right the
// run timer (NRunTimer: always with the ShowRunTimer setting, otherwise only while the map or a
// page over the room is open), deck with its card count (NTopBarDeckButton), map and pause
// (NTopBarMapButton, NTopBarPauseButton).
//
// The top screen cannot be touched, so, as on RGDSplus ("top bar and its local menus stay on the
// upper screen, the controller is the main entry"), ZL / ZR puts the bar into a focus mode:
// ←→ walk the items, L / R jump between the groups, the focused item shows its hover tip under
// the bar (the C#'s static_hover_tips / relic / potion tips), A opens it -- a potion opens the
// existing use / discard popup on that slot, a relic its detail page, deck / map / pause their
// pages (which fall back to the bar when closed) -- and B, ZL / ZR or a touch on the bottom screen
// leave it. Touch play keeps its own bottom-screen entries (药水 / 遗物 / 牌组 / 暂停), so nothing
// here is duplicated there.
#include "../core/settings_store.h"
#include "ui_common.h"

namespace ui {

namespace {

constexpr float kBarH = 18;         // bar height (the old text bar's)
constexpr float kIcon = 16;         // stat / potion / relic icon size
constexpr float kPotionPitch = 17;  // belt slot spacing
constexpr float kRelicPitch = 18;   // relic strip spacing
constexpr float kChipW = 15;        // the "+N" overflow chip at either end of the strip
constexpr float kGroupGap = 7;      // between the stat groups

enum class Kind { Hp, Gold, Potion, Floor, Ascension, Relic, Deck, Map, Pause };
struct Item {
  Kind kind;
  int index;       // belt slot / relic index, else 0
  float x, w;      // bar-space box (relics: strip-content x, before the scroll)
  int group;       // 0 stats, 1 potions, 2 floor/ascension, 3 relics, 4 buttons (L / R jumps)
  bool focusable;  // the map button is inert on the map itself
};

struct Layout {
  std::vector<Item> items;
  float stripX = 0, stripW = 0;  // relic strip viewport
  float timerX = 0;              // right edge of the timer text (0 = hidden)
  std::string hp, gold, floor, act, timer, deck;
};

// Relic strip scroll (px into the strip content), eased towards scrollTarget.
float scroll = 0, scrollTarget = 0;
int seenRelics = -1;  // relic count last frame: a new relic scrolls the strip to the end
int sel = -1;         // focused item (index into Layout::items) while in focus mode

TextStyle barText(uint32_t c) { return ts(F12, c); }
TextStyle smallText(uint32_t c) { return ts(F12, c, LEFT, 0, 0.85f); }

std::string runClock(double seconds) {  // TimeFormatting.Format: m:ss, or h:mm:ss past an hour
  long t = (long)seconds;
  char buf[24];
  if (t >= 3600) std::snprintf(buf, sizeof buf, "%ld:%02ld:%02ld", t / 3600, t / 60 % 60, t % 60);
  else std::snprintf(buf, sizeof buf, "%02ld:%02ld", t / 60, t % 60);
  return buf;
}

std::string stripHotkey(std::string s) {  // "牌组{Hotkey:...}" / "设置（ESC）" -> the bare title
  size_t c = s.find('{');
  if (c != std::string::npos) s.resize(c);
  c = s.find(tr("（", " ("));
  if (c != std::string::npos) s.resize(c);
  while (!s.empty() && s.back() == ' ') s.pop_back();  // English: "Deck {Hotkey...}"
  return s;
}

bool barScreen(Screen s) {
  switch (s) {
    case Screen::Map: case Screen::Combat: case Screen::Reward: case Screen::Rest: case Screen::RestUpgrade:
    case Screen::RelicOffer: case Screen::PotionOffer: case Screen::Shop: case Screen::Event: case Screen::Placeholder:
      return true;
    default: return false;
  }
}

Layout layout(Run& r, int shownHp, int shownGold, bool timerShown) {
  Layout L;
  Res& res = R();
  L.hp = num(shownHp) + "/" + num(r.player->maxHp);
  L.gold = num(shownGold);
  L.floor = num(r.floor);
  std::string act = ui::L("gameplay_ui.ACT_NUMBER");
  size_t v = act.find("{actNumber}");
  L.act = v == std::string::npos ? num(r.actIndex + 1) : act.replace(v, 11, num(r.actIndex + 1));
  L.deck = num((int)r.deck.size());
  float x = 3;
  auto add = [&](Kind k, int i, float w, int g, bool f = true) {
    L.items.push_back({k, i, x, w, g, f});
    x += w;
  };
  add(Kind::Hp, 0, kIcon + 2 + res.measure(L.hp, barText(col::red)), 0);
  x += kGroupGap;
  add(Kind::Gold, 0, kIcon + 2 + res.measure(L.gold, barText(col::gold)), 0);
  x += kGroupGap;
  for (int i = 0; i < (int)r.potions.size(); ++i) add(Kind::Potion, i, kPotionPitch, 1);
  x += kGroupGap - 1;
  add(Kind::Floor, 0, kIcon + 1 + res.measure(L.floor, barText(col::white)) + 3 + res.measure(L.act, smallText(col::gray)), 2);
  if (r.ascension > 0) { x += 4; add(Kind::Ascension, 0, 12, 2); }
  const float left = x + kGroupGap;
  // Right cluster, from the edge inwards: pause, map, deck (+ count), then the timer.
  float rx = kTop - 2;
  auto addRight = [&](Kind k, float w, bool f = true) {
    rx -= w;
    L.items.push_back({k, 0, rx, w, 4, f});
  };
  addRight(Kind::Pause, 17);
  addRight(Kind::Map, 19, r.screen != Screen::Map);
  addRight(Kind::Deck, 19 + res.measure(L.deck, smallText(col::white)));
  // The right-hand items were pushed right-to-left; keep the item list in screen order.
  std::reverse(L.items.end() - 3, L.items.end());
  if (timerShown) {
    L.timer = runClock(r.runTime);
    rx -= 5;
    L.timerX = rx;
    rx -= res.measure(L.timer, smallText(col::gray)) + 13;  // text + the timer icon
  }
  L.stripX = left;
  L.stripW = std::max(0.f, rx - 5 - left);
  // Relics go between the stats and the right cluster (strip-content coordinates).
  std::vector<Item> right(L.items.end() - 3, L.items.end());
  L.items.resize(L.items.size() - 3);
  for (int i = 0; i < (int)r.relics.size(); ++i) L.items.push_back({Kind::Relic, i, i * kRelicPitch, kRelicPitch, 3, true});
  L.items.insert(L.items.end(), right.begin(), right.end());
  return L;
}

// Screen x of an item (relics are offset by the strip and its scroll).
float itemX(const Layout& L, const Item& it) { return it.kind == Kind::Relic ? L.stripX + it.x - scroll : it.x; }

void ensureVisible(const Layout& L, int relic, int n) {
  const float maxScroll = std::max(0.f, n * kRelicPitch - L.stripW);
  float lo = relic * kRelicPitch - (relic > 0 ? kChipW : 0);
  float hi = (relic + 1) * kRelicPitch + (relic < n - 1 ? kChipW : 0);
  if (lo < scrollTarget) scrollTarget = lo;
  if (hi > scrollTarget + L.stripW) scrollTarget = hi - L.stripW;
  scrollTarget = std::clamp(scrollTarget, 0.f, maxScroll);
}

int firstFocusable(const Layout& L, int group) {
  for (int i = 0; i < (int)L.items.size(); ++i)
    if (L.items[i].focusable && L.items[i].group == group) return i;
  return -1;
}

}  // namespace

bool App::topBarActive() const {
  return topBarFocus_ && barScreen(run_->screen) && !settingsOpen_ && !devOpen_ && !detailOpen() &&
         !run_->deckChoice.active && !mapView_ && !relicsOpen_ && !deckOpen_ && !potionsOpen_ && !pauseOpen_;
}

void App::drawTopBar() {
  Run& r = *run_;
  Creature* p = r.player.get();
  if (!p) return;
  // F6: the shown numbers tick towards the real value instead of snapping (App::update).
  int shownHp = shownHp_ >= 0 ? (int)std::lround(shownHp_) : p->hp;
  int shownGold = shownGold_ >= 0 ? (int)std::lround(shownGold_) : r.gold;
  const bool timerShown = settings::state().runTimerEnabled || r.screen == Screen::Map || mapView_ || pauseOpen_ ||
                          deckOpen_ || relicsOpen_;
  Layout L = layout(r, shownHp, shownGold, timerShown);
  const bool active = topBarActive();
  const int n = (int)r.relics.size();

  // Relic strip scroll: follow the focus; else show a newly obtained relic, or one that flashes
  // while scrolled out of view.
  if (seenRelics >= 0 && n > seenRelics) ensureVisible(L, n - 1, n);
  seenRelics = n;
  if (active && sel >= 0 && sel < (int)L.items.size() && L.items[sel].kind == Kind::Relic) {
    ensureVisible(L, L.items[sel].index, n);
  } else {
    for (int i = 0; i < n; ++i) {
      float x = i * kRelicPitch - scrollTarget;
      if (r.relics[i]->flash > 0.5f && (x < 0 || x + kRelicPitch > L.stripW)) { ensureVisible(L, i, n); break; }
    }
  }
  scrollTarget = std::clamp(scrollTarget, 0.f, std::max(0.f, n * kRelicPitch - L.stripW));
  scroll += (scrollTarget - scroll) * 0.25f;
  if (std::fabs(scrollTarget - scroll) < 0.5f) scroll = scrollTarget;

  // Bar: black at 72 % with a teal rule along its bottom edge (docs/UI_STYLE.md status bar).
  gfx::rect(0, 0, kTop, kBarH, 0x000000B8);
  gfx::rect(0, kBarH - 1, kTop, 1, (style::kPanelEdge & 0xFFFFFF00u) | 0xA0);
  const float ty = (kBarH - 1 - R().lineHeight(F12)) / 2;
  const float sty = ty + 1.5f;  // the 0.85x labels, on the same baseline

  for (const Item& it : L.items) {
    const float x = it.x;
    switch (it.kind) {
      case Kind::Hp:
        spr(R().sprite("ui/tb_heart"), x, 1, 17, 15);
        R().text(x + kIcon + 2, ty, L.hp, barText(p->hp * 4 <= p->maxHp ? 0xFF3A30FF : col::red));
        break;
      case Kind::Gold:
        spr(R().sprite("ui/tb_gold"), x, 1, 16, 15);
        R().text(x + kIcon + 2, ty, L.gold, barText(col::gold));
        break;
      case Kind::Potion:
        drawPotionIcon(r.potions[it.index].get(), x, 0.5f, kIcon);
        break;
      case Kind::Floor: {
        spr(R().sprite("ui/tb_floor"), x, 1, 16, 15);
        float fx = x + kIcon + 1;
        R().text(fx, ty, L.floor, barText(col::white));
        R().text(fx + R().measure(L.floor, barText(col::white)) + 3, sty, L.act, smallText(col::gray));
        break;
      }
      case Kind::Ascension:  // NTopBar _ascensionIcon + _ascensionLabel
        spr(R().sprite("ui/tb_ascension"), x, 0, 11, 16);
        R().text(x + 6, sty + 1, num(r.ascension), ts(F12, col::white, CENTER, 0, 0.85f));
        break;
      case Kind::Relic: break;  // in the strip below
      case Kind::Deck: {
        spr(R().sprite("ui/tb_deck"), x, 0, 19, 17);
        R().text(x + 17, sty + 2, L.deck, smallText(col::white));
        break;
      }
      case Kind::Map:
        spr(R().sprite("ui/tb_map"), x + 1, 0, 18, 17, it.focusable ? 0xFFFFFFFF : 0xFFFFFF90);
        break;
      case Kind::Pause:
        spr(R().sprite("ui/tb_settings"), x, 0, 17, 16);
        break;
    }
  }
  if (L.timerX > 0) {
    float w = R().measure(L.timer, smallText(col::gray));
    spr(R().sprite("ui/tb_timer"), L.timerX - w - 13, 2, 12, 11);
    R().text(L.timerX - w, sty, L.timer, smallText(0xC8C0B0FF));
  }

  // Relic strip (clipped); "+N" chips over either end while relics are scrolled out that way.
  if (L.stripW > 0 && n > 0) {
    gfx::pushClip(L.stripX, 0, L.stripW, kBarH);
    for (int i = 0; i < n; ++i) {
      float x = L.stripX + i * kRelicPitch - scroll;
      if (x + kRelicPitch < L.stripX || x > L.stripX + L.stripW) continue;
      drawRelicIcon(r.relics[i].get(), x + 1, 0.5f, kIcon);
    }
    gfx::popClip();
    const float maxScroll = std::max(0.f, n * kRelicPitch - L.stripW);
    auto chip = [&](float x, int count) {
      gfx::rect(x, 0, kChipW, kBarH - 1, 0x000000E0);
      R().text(x + kChipW / 2, sty, "+" + num(count), ts(F12, col::gold, CENTER, 0, 0.8f));
    };
    if (scroll > 0.5f) chip(L.stripX, std::max(1, (int)((scroll + kChipW) / kRelicPitch)));
    if (scroll < maxScroll - 0.5f)
      chip(L.stripX + L.stripW - kChipW, std::max(1, n - (int)((scroll + L.stripW - kChipW) / kRelicPitch)));
  }

  if (!active) return;
  if (sel < 0 || sel >= (int)L.items.size() || !L.items[sel].focusable) sel = std::max(0, firstFocusable(L, 3) >= 0 ? firstFocusable(L, 3) : firstFocusable(L, 1));
  if (sel < 0 || sel >= (int)L.items.size()) return;
  const Item& it = L.items[sel];
  float fx = itemX(L, it), fw = it.w;
  if (it.kind == Kind::Relic) {  // keep the ring inside the strip
    fx = std::max(fx, L.stripX);
    fw = std::min(fx + it.w, L.stripX + L.stripW) - fx;
  }
  // Focus ring (style::kFocus, pulsing like the widget kit's) inside the bar.
  float pulse = 0.75f + 0.25f * std::sin((float)time_ * 6.2831853f / style::kFocusPulse);
  uint32_t c = (style::kFocus & 0xFFFFFF00u) | (uint32_t)(0xFF * pulse);
  gfx::rect(fx - 1, 0, fw + 1, 1.5f, c);
  gfx::rect(fx - 1, kBarH - 2, fw + 1, 1.5f, c);
  gfx::rect(fx - 1, 0, 1.5f, kBarH - 1, c);
  gfx::rect(fx + fw - 1.5f, 0, 1.5f, kBarH - 1, c);

  // Its hover tip, under the bar (NHoverTipSet at the control's bottom + 20).
  std::string title, desc, hint = tr("[gold]A[/gold] 打开    [gold]B[/gold] 返回", "[gold]A[/gold] Open    [gold]B[/gold] Back");
  auto tip = [&](const char* key) {
    title = stripHotkey(ui::L(std::string("static_hover_tips.") + key + ".title"));
    desc = ui::L(std::string("static_hover_tips.") + key + ".description");
  };
  switch (it.kind) {
    case Kind::Hp: tip("HIT_POINTS"); hint = tr("[gold]B[/gold] 返回", "[gold]B[/gold] Back"); break;
    case Kind::Gold: tip("MONEY_POUCH"); hint = tr("[gold]B[/gold] 返回", "[gold]B[/gold] Back"); break;
    case Kind::Floor:
      tip("FLOOR");
      title += "  " + L.floor + " · " + L.act;
      hint = tr("[gold]B[/gold] 返回", "[gold]B[/gold] Back");
      break;
    case Kind::Ascension: {
      char key[40];
      std::snprintf(key, sizeof key, "ascension.LEVEL_%02d", std::clamp(r.ascension, 0, 10));
      title = tr("进阶 ", "Ascension ") + num(r.ascension);
      desc = ui::L(std::string(key) + ".title") + tr("：", ": ") + ui::L(std::string(key) + ".description");
      hint = tr("[gold]B[/gold] 返回", "[gold]B[/gold] Back");
      break;
    }
    case Kind::Potion: {
      Potion* q = r.potions[it.index].get();
      if (q) {
        title = ui::L("potions." + q->locKey + ".title");
        desc = describePotion(q);
        hint = tr("[gold]A[/gold] 使用 / 丢弃    [gold]B[/gold] 返回", "[gold]A[/gold] Use / Discard    [gold]B[/gold] Back");
      } else {
        tip("POTION_SLOT");
        hint = tr("[gold]B[/gold] 返回", "[gold]B[/gold] Back");
      }
      break;
    }
    case Kind::Relic: {
      Relic* rel = r.relics[it.index].get();
      title = ui::L("relics." + rel->locKey + ".title");
      desc = describeRelic(rel);
      hint = tr("[gold]A[/gold] 详情    [gold]B[/gold] 返回", "[gold]A[/gold] Details    [gold]B[/gold] Back");
      break;
    }
    case Kind::Deck: tip("DECK"); title += tr("（", " (") + L.deck + tr("）", ")"); break;
    case Kind::Map: tip("MAP"); break;
    case Kind::Pause: tip("SETTINGS"); break;
  }
  widgets::keywordTip(title, desc.empty() ? hint : desc + "\n" + hint, fx + fw / 2, 0, true);
}

bool App::updateTopBar(const gfx::Input& in) {
  Run& r = *run_;
  if (!barScreen(r.screen) || !r.player) { topBarFocus_ = false; return false; }
  const uint32_t toggle = gfx::BTN_ZL | gfx::BTN_ZR;
  if (!topBarFocus_) {
    if (!(in.down & toggle)) return false;
    topBarFocus_ = true;
    drag_ = {};  // the bar takes over: a card being dragged or aimed is dropped (P0D-09)
    aiming_ = false;
    sfx::click();
    widgets::suspendInput(true);
    return true;
  }
  widgets::suspendInput(true);
  // A touch (the bottom screen always wins) or B / ZL / ZR leaves; the touch is consumed.
  if (in.touchDown || (in.down & (toggle | gfx::BTN_B))) {
    topBarFocus_ = false;
    return true;
  }
  int shownHp = shownHp_ >= 0 ? (int)std::lround(shownHp_) : r.player->hp;
  Layout L = layout(r, shownHp, r.gold, false);
  const int m = (int)L.items.size();
  if (sel < 0 || sel >= m || !L.items[sel].focusable) sel = firstFocusable(L, 3) >= 0 ? firstFocusable(L, 3) : firstFocusable(L, 1);
  if (sel < 0) sel = 0;
  auto step = [&](int d) {
    for (int i = sel + d; i >= 0 && i < m; i += d)
      if (L.items[i].focusable) { sel = i; return; }
  };
  if (in.down & gfx::BTN_RIGHT) step(1);
  if (in.down & gfx::BTN_LEFT) step(-1);
  if (in.down & (gfx::BTN_L | gfx::BTN_R)) {  // jump to the previous / next group's first item
    const int g = L.items[sel].group, d = (in.down & gfx::BTN_R) ? 1 : -1;
    for (int ng = g + d; ng >= 0 && ng <= 4; ng += d) {
      int f = firstFocusable(L, ng);
      if (f >= 0) { sel = f; break; }
    }
  }
  if (!(in.down & (gfx::BTN_A | gfx::BTN_X))) return true;
  const Item& it = L.items[sel];
  switch (it.kind) {
    case Kind::Potion:
      if (!(in.down & gfx::BTN_A)) break;
      sfx::click();
      potionsOpen_ = true;
      potionAim_ = false;
      potionSel_ = it.index;
      break;
    case Kind::Relic:
      sfx::click();
      inspectRelics(r.relics, it.index);  // S20 popup, prev/next through the owned relics
      break;
    case Kind::Deck:
      if (!(in.down & gfx::BTN_A)) break;
      sfx::click();
      openCardList(CardListMode::Deck);
      break;
    case Kind::Map:
      if (!(in.down & gfx::BTN_A) || r.screen == Screen::Map) break;
      sfx::click();
      mapView_ = true;
      mapTouch_ = {};
      mapUserScroll_ = false;
      break;
    case Kind::Pause:
      if (!(in.down & gfx::BTN_A)) break;
      openPause();
      break;
    default: break;
  }
  return true;
}

void App::drawRelicIcon(Relic* r, float x, float y, float size) {
  float pulse = r->flash > 0 ? 1.f + 0.25f * r->flash : 1.f;
  float s = size * pulse, off = (s - size) / 2;
  if (r->flash > 0) gfx::circle(x + size / 2, y + size / 2, s * 0.6f, 0xFFE07000 | (uint32_t)(r->flash * 0x90));
  spr(R().sprite("relic/" + r->icon), x - off, y - off, s, s, r->usedUp ? 0x000000FF : 0xFFFFFFFF, r->usedUp ? 0.55f : 0.f);
  r->flash = std::max(0.f, r->flash - 0.02f);
  if (r->showCounter()) {  // RelicModel.ShowCounter / DisplayAmount, bottom-right like NRelic's label
    std::string c = num(r->displayAmount());
    R().text(x + size + 1, y + size - 10, c, ts(F12, col::white, RIGHT, 0, size < 24 ? 0.85f : 1.f));
  }
}

}  // namespace ui
