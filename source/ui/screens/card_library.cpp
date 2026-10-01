// M8: the card library (compendium 卡牌总览), NCardLibrary / NCardLibraryGrid.
//
// C# (Nodes/Screens/CardLibrary): every card with ShouldShowInCardLibrary (all but MadScience and
// DeprecatedCard), one pool filter selected at a time -- Ironclad, Silent, Defect, Regent,
// Necrobinder (c.Pool is <Character>CardPool), Colorless, Ancients (Rarity == Ancient) and misc
// (Status, Curse, Event, Quest, Token) -- then type tickboxes (attack / skill / power / other)
// and rarity tickboxes (common / uncommon / rare / other; disabled while the Ancients or misc
// pool is selected), a "view upgrades" tickbox, and the sort priority rarity > type > cost >
// title > id. OnSubmenuOpened selects the run's character's pool (Ironclad from the main menu)
// and clears every tickbox. Card visibility (NCardLibraryGrid.GetCardVisibility): a card not in
// progress.seenCards is NotSeen (blurred art, 未知 title, "?" cost, ？？？ text) and cannot be
// inspected; Locked never happens here (the owner's decision: everything is unlocked).
//
// Layout (RGDSplus U29): the full list on the bottom screen -- the pool filter tabs on top, the
// grid of grid-mini cards (F5, s = 0.46, five a row) and a bar with back / type / rarity /
// upgrades -- and the focused card large on the top screen with its text and keywords.
// 3DS adaptations: the multi-select tickboxes are one cycling button per group (all -> each
// value), the cost filter, sort buttons, search bar, stats and multiplayer toggles are left out
// (single player: multiplayer-only cards are not listed), and the alphabetical sort compares
// titles by code point (the C# uses the zh-CN culture's pinyin collation).
// Keys: D-pad moves the focus through tabs / grid / bar, L R switch pool, Y type, X upgrades,
// A inspect (the card detail popup) or activate, B back. Touch: tap a tab / button, tap a card to
// focus it and again to inspect, drag the grid to scroll.
// Performance: the cards are created once (first open) and kept; filtering runs only when a
// filter changes; drawing touches only the visible rows.
#include <cstdlib>
#include <map>
#include <set>

#include "../../core/progress.h"
#include "../ui_common.h"

namespace ui {

namespace {
constexpr int kPools = 8;  // 5 characters, colorless, Ancients, misc
constexpr int kColorless = 5, kAncients = 6, kMisc = 7;
constexpr int kTab0 = 2601, kBtnBack = 2611, kBtnType = 2612, kBtnRarity = 2613, kBtnUpgrade = 2614;
constexpr float kS = 0.46f, kCW = 120 * kS, kCH = 169 * kS, kRowH = kCH + 8;  // F5 grid mini
constexpr int kPerRow = 5;
constexpr float kTabW = 37, kTabH = 28, kTabGap = 2, kTabX0 = (kBot - (kPools * kTabW + (kPools - 1) * kTabGap)) / 2,
                kTabY = 2;
constexpr float kGridY0 = 34, kGridY1 = 204, kBarY = 207, kBarH = 31;
constexpr float kCellGap = (kBot - kPerRow * kCW) / (kPerRow + 1);
constexpr float kTapSlop = 5;
// The bottom bar: back, type, rarity, upgrades.
constexpr float kBarX[4] = {4, 66, 150, 234}, kBarW[4] = {58, 80, 80, 82};
constexpr int kBarId[4] = {kBtnBack, kBtnType, kBtnRarity, kBtnUpgrade};

struct Entry {
  std::unique_ptr<Card> card;
  std::unique_ptr<Card> upgraded;  // made on first need while "view upgrades" is on
  uint8_t pools = 0;               // bit per pool filter
  bool seen = false;
  std::string title;               // sort key
};

struct State {
  bool open = false, built = false;
  std::vector<Entry> all;       // every library card, sorted once (C# sort priority)
  std::vector<int> shown;       // indices into `all` passing the filters
  int pool = 0, type = 0, rarity = 0;  // type / rarity: 0 = all, then each value, last = other
  bool upgrades = false;
  int sel = 0;                  // index into `shown`
  int zone = 1;                 // focus: 0 pool tabs, 1 grid, 2 bottom bar
  int tab = 0, bar = 0;         // focused tab / bar button
  float scroll = 0;             // grid scroll, px
  bool touchDown = false, dragged = false, touchGrid = false;
  float touchY0 = 0, touchLastY = 0;
  int touchCard = -1;
};
State& S() {
  static State s;
  return s;
}

// GetCardRarityComparisonValue (NCardGrid): CardRarity's order, then Status, Curse, Event, Quest, Token.
int rarityOrder(Rarity r) {
  switch (r) {
    case Rarity::Basic: return 1;
    case Rarity::Common: return 2;
    case Rarity::Uncommon: return 3;
    case Rarity::Rare: return 4;
    case Rarity::Ancient: return 5;
    case Rarity::Status: return 6;
    case Rarity::Curse: return 7;
    case Rarity::Event: return 8;
    case Rarity::Quest: return 9;
    case Rarity::Token: return 10;
  }
  return 11;
}

bool rarityOk(const Card& c, int f) {
  switch (f) {
    case 1: return c.rarity == Rarity::Common;
    case 2: return c.rarity == Rarity::Uncommon;
    case 3: return c.rarity == Rarity::Rare;
    case 4: return c.rarity != Rarity::Common && c.rarity != Rarity::Uncommon && c.rarity != Rarity::Rare;
  }
  return true;
}
bool typeOk(const Card& c, int f) {
  switch (f) {
    case 1: return c.type == CardType::Attack;
    case 2: return c.type == CardType::Skill;
    case 3: return c.type == CardType::Power;
    case 4: return c.type != CardType::Attack && c.type != CardType::Skill && c.type != CardType::Power;
  }
  return true;
}
// NCardLibrary.UpdateCardPoolFilter: the rarity tickboxes are disabled unless a character or the
// colorless pool is selected.
bool rarityEnabled() { return S().pool != kAncients && S().pool != kMisc; }

void build() {
  State& st = S();
  if (st.built) return;
  st.built = true;
  db::init();
  const auto& chars = db::characterIds();
  auto all = [](const Card&) { return true; };
  std::map<std::string, uint8_t> pools;
  std::set<std::string> inSomePool;  // full pool lists incl. multiplayer-only cards
  for (int i = 0; i < (int)chars.size() && i < kColorless; ++i) {
    for (auto& id : db::character(chars[i]).cardPool) inSomePool.insert(id);
    for (auto& id : db::characterCards(chars[i], all)) pools[id] |= (uint8_t)(1u << i);
  }
  for (auto& id : db::colorlessCards(all)) pools[id] |= (uint8_t)(1u << kColorless);
  static const char* const kHidden[] = {"MadScience", "DeprecatedCard"};  // shouldShowInCardLibrary: false
  for (auto& id : db::cardIds()) {
    if (std::find(std::begin(kHidden), std::end(kHidden), id) != std::end(kHidden)) continue;
    auto card = db::card(id);
    if (!card) continue;
    uint8_t bits = 0;
    auto it = pools.find(id);
    if (it != pools.end()) bits = it->second;
    if (card->rarity == Rarity::Ancient) bits |= 1u << kAncients;
    bool miscRarity = card->rarity == Rarity::Token || card->rarity == Rarity::Status || card->rarity == Rarity::Curse ||
                      card->rarity == Rarity::Event || card->rarity == Rarity::Quest;
    // Event / quest cards sit in no pool: the misc filter.
    if (miscRarity || (!bits && !inSomePool.count(id) && !db::isColorless(id))) bits |= 1u << kMisc;
    if (!bits) continue;  // multiplayer-only
    Entry e;
    e.card = std::move(card);
    e.pools = bits;
    st.all.push_back(std::move(e));
  }
  for (auto& e : st.all) e.title = R().loc("cards." + e.card->locKey + ".title");
  // Sort priority RarityAscending, TypeAscending, CostAscending, AlphabetAscending, then the id.
  std::sort(st.all.begin(), st.all.end(), [](const Entry& a, const Entry& b) {
    const Card &x = *a.card, &y = *b.card;
    if (int d = rarityOrder(x.rarity) - rarityOrder(y.rarity)) return d < 0;
    if (x.type != y.type) return (int)x.type < (int)y.type;
    int cx = x.costsX ? 0 : std::max(0, x.canonicalCost), cy = y.costsX ? 0 : std::max(0, y.canonicalCost);
    if (cx != cy) return cx < cy;
    if (a.title != b.title) return a.title < b.title;
    return x.id < y.id;
  });
}

void refilter() {
  State& st = S();
  st.shown.clear();
  for (int i = 0; i < (int)st.all.size(); ++i) {
    const Entry& e = st.all[i];
    if (!(e.pools & (1u << st.pool))) continue;
    if (!typeOk(*e.card, st.type)) continue;
    if (rarityEnabled() && !rarityOk(*e.card, st.rarity)) continue;
    st.shown.push_back(i);
  }
  st.sel = 0;
  st.scroll = 0;
}

int rows() { return ((int)S().shown.size() + kPerRow - 1) / kPerRow; }
float maxScroll() { return std::max(0.f, rows() * kRowH + 8 - (kGridY1 - kGridY0)); }

// Keep the focused card's row inside the grid area.
void scrollToSel() {
  State& st = S();
  float top = (st.sel / kPerRow) * kRowH, h = kGridY1 - kGridY0;
  if (top < st.scroll) st.scroll = top;
  if (top + kRowH + 4 > st.scroll + h) st.scroll = top + kRowH + 4 - h;
  st.scroll = std::clamp(st.scroll, 0.f, maxScroll());
}

// The card to show for an entry: the upgraded copy while "view upgrades" is on.
Card* shownCard(Entry& e) {
  if (!S().upgrades || !e.card->upgradable()) return e.card.get();
  if (!e.upgraded) {
    e.upgraded = e.card->clone();
    e.upgraded->upgrade();
  }
  return e.upgraded.get();
}

Entry* focusedEntry() {
  State& st = S();
  if (st.sel < 0 || st.sel >= (int)st.shown.size()) return nullptr;
  return &st.all[st.shown[st.sel]];
}

// Card index under a bottom-screen point, or -1.
int cardAt(float tx, float ty) {
  State& st = S();
  if (ty < kGridY0 || ty >= kGridY1) return -1;
  float ly = ty - kGridY0 - 6 + st.scroll;
  int row = (int)std::floor(ly / kRowH);
  if (row < 0 || ly - row * kRowH > kCH) return -1;
  for (int c = 0; c < kPerRow; ++c) {
    float x = kCellGap + c * (kCW + kCellGap);
    if (tx >= x && tx < x + kCW) {
      int i = row * kPerRow + c;
      return i < (int)st.shown.size() ? i : -1;
    }
  }
  return -1;
}

const char* kTypeKeys[] = {nullptr, "gameplay_ui.CARD_TYPE.ATTACK", "gameplay_ui.CARD_TYPE.SKILL",
                           "gameplay_ui.CARD_TYPE.POWER", nullptr};
const char* kRarityKeys[] = {nullptr, "card_library.RARITY_COMMON", "card_library.RARITY_UNCOMMON",
                             "card_library.RARITY_RARE", "card_library.RARITY_OTHER"};

std::string typeName(CardType t) {
  switch (t) {
    case CardType::Attack: return L("gameplay_ui.CARD_TYPE.ATTACK");
    case CardType::Skill: return L("gameplay_ui.CARD_TYPE.SKILL");
    case CardType::Power: return L("gameplay_ui.CARD_TYPE.POWER");
    case CardType::Status: return tr("状态", "Status");
    case CardType::Curse: return tr("诅咒", "Curse");
    case CardType::Quest: return L("gameplay_ui.CARD_TYPE.QUEST");
  }
  return "";
}
std::string rarityName(Rarity r) {
  switch (r) {
    case Rarity::Basic: return tr("基础", "Basic");
    case Rarity::Common: return L("card_library.RARITY_COMMON");
    case Rarity::Uncommon: return L("card_library.RARITY_UNCOMMON");
    case Rarity::Rare: return L("card_library.RARITY_RARE");
    case Rarity::Ancient: return tr("先古", "Ancient");
    case Rarity::Token: return tr("衍生", "Token");
    case Rarity::Status: return tr("状态", "Status");
    case Rarity::Curse: return tr("诅咒", "Curse");
    case Rarity::Event: return L("gameplay_ui.CARD_RARITY.EVENT");
    case Rarity::Quest: return L("gameplay_ui.CARD_RARITY.QUEST");
  }
  return "";
}
std::string poolName(int pool) {
  if (pool < kColorless) {
    const auto& ids = db::characterIds();
    return pool < (int)ids.size() ? L("characters." + db::character(ids[pool]).key + ".title") : std::string();
  }
  return pool == kColorless ? tr("无色", "Colorless") : pool == kAncients ? tr("先古之民", "Ancients") : tr("其他", "Other");
}
// The pool's filter tip (card_library.POOL_*_TIP), for the top screen when nothing is listed.
std::string poolTip(int pool) {
  if (pool == kColorless) return L("card_library.POOL_COLORLESS_TIP");
  if (pool == kAncients) return L("card_library.POOL_ANCIENT_TIP");
  if (pool == kMisc) return L("card_library.POOL_MISC_TIP");
  return "";
}

void outline(float x, float y, float w, float h, uint32_t c = col::gold, float t = 2) {
  gfx::rect(x - t, y - t, w + 2 * t, t, c);
  gfx::rect(x - t, y + h, w + 2 * t, t, c);
  gfx::rect(x - t, y, t, h, c);
  gfx::rect(x + w, y, t, h, c);
}

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

// NCardLibrary.OnSubmenuOpened: the run's character's pool (Ironclad from the main menu), every
// tickbox cleared, "view upgrades" off; NCardLibraryGrid.RefreshVisibility re-reads the seen set.
void App::openCardLibrary() {
  build();
  State& st = S();
  st.open = true;
  st.pool = 0;
  if (run_ && run_->screen != Screen::Title) {
    const auto& ids = db::characterIds();
    for (int i = 0; i < (int)ids.size() && i < kColorless; ++i)
      if (ids[i] == run_->characterId) st.pool = i;
  }
  st.type = st.rarity = 0;
  st.upgrades = false;
  st.zone = 1;
  st.tab = st.pool;
  st.bar = 0;
  st.touchDown = false;
  const auto& seen = progress::state().seenCards;
  const bool all = getenv("STS_SEEN_ALL") != nullptr;  // debug: preview every card as seen
  for (auto& e : st.all) e.seen = all || seen.count(e.card->id) > 0;
  refilter();
  closeDetail();
}

bool App::drawCardLibrary(bool top) {
  State& st = S();
  if (!st.open) return false;
  const bool title = run_->screen == Screen::Title;
  Entry* fe = focusedEntry();
  if (top) {
    if (title) drawMenuBg(true, 0.75f);
    else drawSceneBg(true, 0.8f);
    TextStyle ht = ts(F16, col::gold);
    R().text(8, 4, L("main_menu_ui.COMPENDIUM_CARD_LIBRARY.title"), ht);
    std::string count = L("card_library.CARD_COUNT");
    size_t p = count.find("{Amount}");
    if (p != std::string::npos) count.replace(p, 8, num((int)st.shown.size()));
    R().text(kTop - 8, 6, poolName(st.pool) + "  " + count, ts(F12, col::white, RIGHT));
    if (!fe) {
      R().text(kTop / 2, 100, L("card_library.NO_RESULTS"), ts(F16, col::gray, CENTER));
      std::string tip = poolTip(st.pool);
      if (!tip.empty()) R().text(kTop / 2, 126, tip, ts(F12, col::white, CENTER, kTop - 60));
      return true;
    }
    Card* c = shownCard(*fe);
    const float cs = 1.2f, cx = 14, cy = 30;
    drawCard(c, cx, cy, cs, false, true, false, !fe->seen);
    // The right column: title, type / rarity / pool, the text at a readable size, keyword tips.
    const float rx = cx + 120 * cs + 16, rw = kTop - rx - 8;
    float y = 30;
    R().text(rx, y, fe->seen ? cardTitle(c) : L("card_library.UNKNOWN.title"),
             ts(F16, fe->seen && c->upgraded() ? col::green : col::gold, LEFT, rw));
    y += 22;
    std::string meta = fe->seen ? typeName(c->type) + " · " + rarityName(c->rarity) + " · " + poolName(st.pool)
                                : poolName(st.pool);
    R().text(rx, y, meta, ts(F12, col::gray, LEFT, rw));
    y += 18;
    if (st.upgrades && fe->seen && fe->card->upgradable()) {
      R().text(rx, y, L("gameplay_ui.VIEW_UPGRADES"), ts(F12, col::green));
      y += 16;
    }
    if (!fe->seen) {
      R().text(rx, y + 4, L("card_library.UNKNOWN.description"), ts(F16, col::white, LEFT, rw));
    } else {
      y += R().text(rx, y + 2, describe(c), ts(F12, col::white, LEFT, rw)) + 8;
      for (const char* k : keywordKeys(c)) {
        if (y > kH - 40) break;
        std::string key = std::string("card_keywords.") + k;
        TextStyle kt = ts(F12, 0xD8D8D8FF, LEFT, rw - 16);
        float dh;
        std::string d = L(key + ".description");
        R().measure(d, kt, &dh);
        float ph = dh + 26;
        widgets::panel("ui/hover_tip", rx - 4, y, rw + 4, ph);
        R().text(rx + 4, y + 5, L(key + ".title"), ts(F12, col::gold));
        R().text(rx + 4, y + 20, d, kt);
        y += ph + 4;
      }
    }
    R().text(kTop - 6, kH - 16, tr("L R 角色  Y 类型  X 升级  A 详情  B 返回", "L R Character  Y Type  X Upgrade  A Details  B Back"), ts(F12, col::gray, RIGHT));
    return true;
  }

  if (title) drawMenuBg(false, 0.6f);
  else drawSceneBg(false, 0.7f);
  const bool pad = st.zone != 1 || !st.touchGrid;
  // Pool filter tabs (NCardPoolFilter): character icons, the colorless energy gem, Neow, misc.
  const auto& ids = db::characterIds();
  for (int i = 0; i < kPools; ++i) {
    float x = kTabX0 + i * (kTabW + kTabGap);
    bool on = i == st.pool;
    if (on) widgets::panel("ui/tab_selected", x, kTabY, kTabW, kTabH);
    else gfx::rect(x, kTabY, kTabW, kTabH, 0x00000090);
    std::string icon = i < kColorless ? (i < (int)ids.size() ? "ui/char_" + db::character(ids[i]).energyColor : "")
                       : i == kColorless ? "card/energy_colorless" : i == kAncients ? "ui/lib_ancient" : "ui/lib_misc";
    Sprite s = R().sprite(icon);
    const float is = 22;
    if (s) spr(s, x + (kTabW - is) / 2, kTabY + (kTabH - is) / 2, is, is, on ? 0xFFFFFFFF : 0x000000FF, on ? 0.f : 0.35f);
    if (st.zone == 0 && st.tab == i) outline(x, kTabY, kTabW, kTabH);
    hits_.push_back({x, kTabY, kTabW, kTabH, kTab0 + i});
  }

  // The grid: only the visible rows are drawn.
  gfx::pushClip(0, kGridY0, kBot, kGridY1 - kGridY0);
  const int n = (int)st.shown.size();
  int r0 = std::max(0, (int)std::floor(st.scroll / kRowH)), r1 = (int)std::ceil((st.scroll + kGridY1 - kGridY0) / kRowH);
  for (int r = r0; r <= r1; ++r) {
    for (int c = 0; c < kPerRow; ++c) {
      int i = r * kPerRow + c;
      if (i >= n) break;
      Entry& e = st.all[st.shown[i]];
      float x = kCellGap + c * (kCW + kCellGap), y = kGridY0 + 6 + r * kRowH - st.scroll;
      drawCard(shownCard(e), x, y, kS, false, false, i == st.sel && st.zone == 1, !e.seen);
    }
  }
  gfx::popClip();
  if (n == 0) R().text(kBot / 2, 110, L("card_library.NO_RESULTS"), ts(F16, col::gray, CENTER));
  if (maxScroll() > 0) {  // scrollbar
    float h = kGridY1 - kGridY0, th = std::max(16.f, h * h / (h + maxScroll()));
    float ty = kGridY0 + (h - th) * (st.scroll / maxScroll());
    gfx::rect(kBot - 4, kGridY0, 3, h, 0x00000080);
    gfx::rect(kBot - 4, ty, 3, th, 0xC8B080FF);
  }

  // The bottom bar: back, type, rarity, upgrades.
  gfx::rect(0, kBarY - 3, kBot, kH - kBarY + 3, 0x000000A0);
  button(kBarX[0], kBarY, kBarW[0], kBarH, tr("返回", "Back"), kBtnBack);
  button(kBarX[1], kBarY, kBarW[1], kBarH,
         st.type == 0 ? tr("全部类型", "All types") : st.type == 4 ? L("card_library.RARITY_OTHER") : L(kTypeKeys[st.type]), kBtnType, true,
         st.type != 0);
  button(kBarX[2], kBarY, kBarW[2], kBarH, st.rarity == 0 ? tr("全部稀有度", "All rarities") : L(kRarityKeys[st.rarity]), kBtnRarity,
         rarityEnabled(), st.rarity != 0 && rarityEnabled());
  button(kBarX[3], kBarY, kBarW[3], kBarH, L("gameplay_ui.VIEW_UPGRADES"), kBtnUpgrade, true, st.upgrades);
  if (st.zone == 2 && pad) outline(kBarX[st.bar], kBarY, kBarW[st.bar], kBarH);
  return true;
}

bool App::updateCardLibrary(const gfx::Input& in) {
  State& st = S();
  if (!st.open) return false;
  const int n = (int)st.shown.size();
  auto close = [&] { st.open = false; };
  auto setPool = [&](int p) {
    if (p == st.pool) return;
    st.pool = p;
    st.tab = p;
    refilter();
  };
  auto inspect = [&](int i) {
    Entry& e = st.all[st.shown[i]];
    if (!e.seen) return;  // NotSeen cards cannot be inspected
    std::vector<Card*> list;  // S20: prev / next through the seen cards of the filtered grid
    int at = 0;
    for (int k : st.shown)
      if (st.all[k].seen) { if (k == st.shown[i]) at = (int)list.size(); list.push_back(st.all[k].card.get()); }
    inspectCard(list, at, st.upgrades);
  };
  auto activate = [&](int id) {
    if (id >= kTab0 && id < kTab0 + kPools) setPool(id - kTab0);
    else if (id == kBtnBack) close();
    else if (id == kBtnType) { st.type = (st.type + 1) % 5; refilter(); }
    else if (id == kBtnRarity && rarityEnabled()) { st.rarity = (st.rarity + 1) % 5; refilter(); }
    else if (id == kBtnUpgrade) st.upgrades = !st.upgrades;
  };

  // Touch: tabs and buttons on press; the grid scrolls on drag, a tap focuses / inspects.
  if (in.touchDown) {
    int id = hitAt(in.tx, in.ty);
    st.touchGrid = true;
    if (id != ID_NONE) {
      for (int i = 0; i < 4; ++i) if (kBarId[i] == id) st.bar = i;
      activate(id);
      return true;
    }
    if (in.ty >= kGridY0 && in.ty < kGridY1) {
      st.touchDown = true;
      st.dragged = false;
      st.touchY0 = st.touchLastY = (float)in.ty;
      st.touchCard = cardAt((float)in.tx, (float)in.ty);
    }
  } else if (st.touchDown && in.touching) {
    if (std::fabs(in.ty - st.touchY0) > kTapSlop) st.dragged = true;
    if (st.dragged) st.scroll = std::clamp(st.scroll - (in.ty - st.touchLastY), 0.f, maxScroll());
    st.touchLastY = (float)in.ty;
  }
  if (in.touchUp && st.touchDown) {
    st.touchDown = false;
    if (!st.dragged && st.touchCard >= 0 && cardAt((float)in.tx, (float)in.ty) == st.touchCard) {
      st.zone = 1;
      if (st.sel == st.touchCard) inspect(st.sel);
      else st.sel = st.touchCard;
    }
    return true;
  }

  const uint32_t d = in.down;
  if (d) st.touchGrid = false;
  if (d & gfx::BTN_B) { close(); return true; }
  if (d & gfx::BTN_L) { setPool((st.pool + kPools - 1) % kPools); return true; }
  if (d & gfx::BTN_R) { setPool((st.pool + 1) % kPools); return true; }
  if (d & gfx::BTN_Y) { activate(kBtnType); return true; }
  if (d & gfx::BTN_X) { activate(kBtnUpgrade); return true; }
  switch (st.zone) {
    case 0:  // pool tabs: left / right pick a pool straight away
      if (d & gfx::BTN_LEFT) setPool((st.pool + kPools - 1) % kPools);
      if (d & gfx::BTN_RIGHT) setPool((st.pool + 1) % kPools);
      if (d & (gfx::BTN_DOWN | gfx::BTN_A)) st.zone = n ? 1 : 2;
      if (d & gfx::BTN_UP) st.zone = 2;
      break;
    case 1:  // grid
      if (n == 0) { st.zone = (d & gfx::BTN_UP) ? 0 : 2; break; }
      if (d & gfx::BTN_RIGHT) st.sel = std::min(n - 1, st.sel + 1);
      if (d & gfx::BTN_LEFT) st.sel = std::max(0, st.sel - 1);
      if (d & gfx::BTN_DOWN) {
        if (st.sel / kPerRow == (n - 1) / kPerRow) st.zone = 2;
        else st.sel = std::min(n - 1, st.sel + kPerRow);
      }
      if (d & gfx::BTN_UP) {
        if (st.sel < kPerRow) st.zone = 0, st.tab = st.pool;
        else st.sel -= kPerRow;
      }
      if (d & (gfx::BTN_LEFT | gfx::BTN_RIGHT | gfx::BTN_UP | gfx::BTN_DOWN)) scrollToSel();
      if (d & gfx::BTN_A) inspect(st.sel);
      break;
    default:  // bottom bar
      if (d & gfx::BTN_LEFT) st.bar = (st.bar + 3) % 4;
      if (d & gfx::BTN_RIGHT) st.bar = (st.bar + 1) % 4;
      if (d & gfx::BTN_UP) { st.zone = n ? 1 : 0; if (n) scrollToSel(); }
      if (d & gfx::BTN_DOWN) st.zone = 0;
      if (d & gfx::BTN_A) activate(kBarId[st.bar]);
      break;
  }
  return true;
}

}  // namespace ui
