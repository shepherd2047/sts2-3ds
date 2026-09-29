// M9: the relic collection (遗物收集, NRelicCollection) and the potion lab (药水研究所, NPotionLab).
//
// C# (Nodes/Screens/RelicCollection, Nodes/Screens/PotionLab): one scrolling page of categories,
// each a header (relic_collection.<RARITY> / potion_lab.<RARITY>: a gold title and a one-line
// description) over a grid of icons.
//  - Relics (NRelicCollectionCategory.LoadRelics): Starter, Common, Uncommon, Rare, Shop, Ancient,
//    Event. Starter is two sub-grids: every character's starting relics (ModelDb.AllCharacters
//    order), then their Touch of Orobas upgrades (TouchOfOrobas.RefinementUpgrades, fallback
//    Circlet). Ancient is one sub-grid per Ancient (the acts' Ancients in ModelDb.Acts order, then
//    the shared Darv) holding the Ancient-rarity relics among its options sorted by title, headed
//    "{Ancient}：" -- or ？？？ until the Ancient was met (AncientStats) or one of its relics seen.
//    The other rarities list the relics in no character pool sorted by title, then each
//    character's pool (AllCharacterRelicPools order), their icons outlined in the pool's
//    LabOutlineColor.
//  - Potions (NPotionLabCategory.LoadPotions): Common, Uncommon, Rare, Special (Event + Token),
//    each the same "shared sorted by title, then character pools" order and outline.
// Visibility (ModelVisibility): an entry not in progress.seenRelics / seenPotions is NotSeen (its
// icon 90% black, the tip "未知 / 你还没有见过……"); Locked never happens (everything unlocked).
//
// Layout (RGDSplus U29): the categories and grids on the bottom screen with a back / previous /
// next category bar; the focused entry on the top screen: large icon, name, rarity (and pool),
// description, flavour, and the category's description.
// 3DS adaptations: only registered (ported) relics / potions are listed; titles sort by code
// point (the C# uses the zh-CN culture's collation); a character pool's relics keep the pool
// list's order (the C# walks ModelDb.AllRelics); the Ancients' option lists are the relic types
// each C# event file references (Neow.cs, Orobas.cs, ...), intersected with the Ancient rarity.
// Keys: D-pad moves the focus (grid rows, down past the last row to the bar), L R jump to the
// previous / next category, B back. Touch: tap an icon to focus it, drag the list to scroll, tap
// the bar's buttons.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>

#include "../../core/progress.h"
#include "../ui_common.h"

namespace ui {

namespace {
constexpr int kBtnBack = 2701, kBtnPrev = 2702, kBtnNext = 2703;
constexpr float kCell = 40, kIcon = 32;
constexpr int kPerRow = 7;
constexpr float kGridX0 = (kBot - kPerRow * kCell) / 2;
constexpr float kListY0 = 0, kListY1 = 203, kBarY = 207, kBarH = 31;
constexpr float kHeadH = 24, kSubH = 20, kDividerH = 10;
constexpr float kTapSlop = 5;
constexpr float kBarX[3] = {4, 116, 222}, kBarW[3] = {70, 94, 94};
constexpr int kBarId[3] = {kBtnBack, kBtnPrev, kBtnNext};

// StsColors.red / green / blue / orange / pink at 66% alpha: the pools' LabOutlineColor.
uint32_t poolColor(const std::string& characterId) {
  if (characterId == "Ironclad") return 0xFF6563A8;
  if (characterId == "Silent") return 0x7FFF00A8;
  if (characterId == "Defect") return 0x87CEEBA8;
  if (characterId == "Regent") return 0xFFA500A8;
  if (characterId == "Necrobinder") return 0xFF69B4A8;
  return 0;
}

struct Entry {
  std::unique_ptr<Relic> relic;
  std::unique_ptr<Potion> potion;
  std::string id, title, poolChar;  // poolChar: the character pool it belongs to, or empty
  bool seen = false;
};

// One line of the scrolling list: a category header, a sub-grid header, a divider, or a row of
// up to kPerRow entries.
struct Line {
  enum Kind { Head, Sub, Divider, Row } kind;
  float y = 0, h = 0;
  int section = 0;
  std::string label, icon, ancientId;  // Sub: the Ancient's id (its name shows once known)
  std::vector<int> items;              // Row: indices into Book::all
  std::vector<int> subItems;           // Sub: every entry of the sub-grid (for "seen any")
};

struct Book {
  bool built = false;
  std::vector<Entry> all;
  std::vector<Line> lines;
  std::vector<std::string> sectionKeys;  // "relic_collection.COMMON", ...
  std::vector<int> sectionLine;          // the header line of each section
  std::vector<std::pair<int, int>> pos;  // flat focus order: (line, column)
  float height = 0;
};

struct State {
  bool open = false;
  bool potions = false;  // false: relic collection, true: potion lab
  Book relics, lab;
  int sel = 0;           // index into Book::pos
  int zone = 1;          // 1 the list, 2 the bottom bar
  int bar = 0;
  float scroll = 0;
  bool touchDown = false, dragged = false, touchList = false;
  float touchY0 = 0, touchLastY = 0;
  int touchItem = -1;
};
State& S() {
  static State s;
  return s;
}
Book& book() { return S().potions ? S().lab : S().relics; }

std::string relicTitle(const Relic& r) { return R().loc("relics." + r.locKey + ".title"); }
std::string potionTitle(const Potion& p) { return R().loc("potions." + p.locKey + ".title"); }

// A header string "[gold][font_size=28][b]普通：[/b][/font_size][/gold]随处可见的弱小遗物。" split into
// its plain title (before the full-width colon) and the description after it.
std::string stripTags(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size();) {
    if (s[i] == '[') {
      size_t c = s.find(']', i);
      if (c != std::string::npos) { i = c + 1; continue; }
    }
    out += s[i++];
  }
  return out;
}
void splitHeader(const std::string& key, std::string* title, std::string* desc) {
  std::string s = stripTags(L(key));
  const std::string colon = "：";
  size_t p = s.find(colon);
  if (p == std::string::npos) { *title = s; *desc = ""; return; }
  *title = s.substr(0, p);
  *desc = s.substr(p + colon.size());
}

// The Ancients in ModelDb.Acts order (Overgrowth / Underdocks: Neow, Hive: Orobas Pael Tezcatara,
// Glory: Nonupeipe Tanx Vakuu), then ModelDb.AllSharedAncients (Darv), with the relic types their
// event files reference (AllPossibleOptions and the option builders).
struct AncientDef { const char* id; const char* key; std::vector<const char*> relics; };
const std::vector<AncientDef>& ancientDefs() {
  static const std::vector<AncientDef> v = {
      {"Neow", "NEOW", {"ArcaneScroll", "BoomingConch", "CursedPearl", "DowsingRod", "FishingRod", "GoldenPearl",
                        "HeftyTablet", "Kaleidoscope", "LargeCapsule", "LavaRock", "LeadPaperweight", "LeafyPoultice",
                        "LostCoffer", "MassiveScroll", "NeowsBones", "NeowsSacrifice", "NeowsTalisman", "NeowsTorment",
                        "NewLeaf", "NutritiousOyster", "PhialHolster", "Pomander", "PrecariousShears",
                        "PreciseScissors", "ScrollBoxes", "SilkenTress", "SilverCrucible", "SmallCapsule",
                        "StoneHumidifier", "WingedBoots"}},
      {"Orobas", "OROBAS", {"AlchemicalCoffer", "ArchaicTooth", "Driftwood", "ElectricShrymp", "GlassEye",
                            "PrismaticGem", "RadiantPearl", "SandCastle", "SeaGlass", "TouchOfOrobas"}},
      {"Pael", "PAEL", {"PaelsBlood", "PaelsClaw", "PaelsEye", "PaelsFlesh", "PaelsGrowth", "PaelsHorn", "PaelsLegion",
                        "PaelsTears", "PaelsTooth", "PaelsWing"}},
      {"Tezcatara", "TEZCATARA", {"BiiigHug", "GoldenCompass", "NutritiousSoup", "PumpkinCandle", "SealOfGold",
                                  "Storybook", "ToastyMittens", "ToyBox", "VeryHotCocoa", "YummyCookie"}},
      {"Nonupeipe", "NONUPEIPE", {"BeautifulBracelet", "BlessedAntler", "BrilliantScarf", "DelicateFrond",
                                  "DiamondDiadem", "FurCoat", "Glitter", "JewelryBox", "LoomingFruit", "SignetRing"}},
      {"Tanx", "TANX", {"Claws", "Crossbow", "IronClub", "MeatCleaver", "Sai", "SpikedGauntlets", "TanxsWhistle",
                        "ThrowingAxe", "TriBoomerang", "WarHammer"}},
      {"Vakuu", "VAKUU", {"BloodSoakedRose", "ChoicesParadox", "DistinguishedCape", "Fiddle", "JeweledMask",
                          "LordsParasol", "MusicBox", "PreservedFog", "SereTalon", "WhisperingEarring"}},
      {"Darv", "DARV", {"Astrolabe", "BlackStar", "CallingBell", "DustyTome", "Ectoplasm", "EmptyCage", "PandorasBox",
                        "PhilosophersStone", "RunicPyramid", "SneckoEye", "Sozu", "VelvetChoker"}},
  };
  return v;
}

// TouchOfOrobas.GetUpgradedStarterRelic.
std::string upgradedStarter(const std::string& id) {
  static const std::pair<const char*, const char*> kMap[] = {{"BurningBlood", "BlackBlood"},
                                                             {"RingOfTheSnake", "RingOfTheDrake"},
                                                             {"DivineRight", "DivineDestiny"},
                                                             {"BoundPhylactery", "PhylacteryUnbound"},
                                                             {"CrackedCore", "InfusedCore"}};
  for (auto& [a, b] : kMap)
    if (id == a) return b;
  return "Circlet";
}

// Layout: appends lines for a list of entries, kPerRow a row.
struct Builder {
  Book& b;
  int section = 0;
  void head(const std::string& key) {
    b.sectionKeys.push_back(key);
    section = (int)b.sectionKeys.size() - 1;
    b.sectionLine.push_back((int)b.lines.size());
    Line l{Line::Head};
    l.section = section;
    l.h = kHeadH;
    b.lines.push_back(l);
  }
  void sub(const std::string& ancientId, const std::string& icon, const std::vector<int>& items) {
    Line l{Line::Sub};
    l.section = section;
    l.h = kSubH;
    l.ancientId = ancientId;
    l.icon = icon;
    l.subItems = items;
    b.lines.push_back(l);
  }
  void divider() {
    Line l{Line::Divider};
    l.section = section;
    l.h = kDividerH;
    b.lines.push_back(l);
  }
  void grid(const std::vector<int>& items) {
    for (size_t i = 0; i < items.size(); i += kPerRow) {
      Line l{Line::Row};
      l.section = section;
      l.h = kCell;
      for (size_t j = i; j < items.size() && j < i + kPerRow; ++j) l.items.push_back(items[j]);
      b.lines.push_back(l);
    }
  }
  void finish() {
    float y = 4;
    for (int i = 0; i < (int)b.lines.size(); ++i) {
      Line& l = b.lines[i];
      if (l.kind == Line::Head && i > 0) y += 6;
      l.y = y;
      y += l.h;
      if (l.kind == Line::Row)
        for (int c = 0; c < (int)l.items.size(); ++c) b.pos.push_back({i, c});
    }
    b.height = y + 6;
  }
};

// Adds an entry; returns its index.
int addRelic(Book& b, const std::string& id) {
  auto r = db::relic(id);
  if (!r) return -1;
  Entry e;
  e.id = id;
  e.title = relicTitle(*r);
  e.relic = std::move(r);
  b.all.push_back(std::move(e));
  return (int)b.all.size() - 1;
}
int addPotion(Book& b, const std::string& id) {
  auto p = db::potion(id);
  if (!p) return -1;
  Entry e;
  e.id = id;
  e.title = potionTitle(*p);
  e.potion = std::move(p);
  b.all.push_back(std::move(e));
  return (int)b.all.size() - 1;
}

// "Shared sorted by title, then each character pool" (LoadRelics / LoadPotions). `ids` are the
// registered ids of the category, `pools` each character's list in pool order.
template <class Add>
std::vector<int> poolOrder(Book& b, const std::vector<std::string>& ids,
                           const std::vector<std::pair<std::string, std::vector<std::string>>>& pools, Add add) {
  std::vector<int> shared, own;
  std::vector<std::string> inPool;
  for (auto& [ch, list] : pools)
    for (auto& id : list)
      if (std::find(ids.begin(), ids.end(), id) != ids.end()) {
        int i = add(b, id);
        if (i < 0) continue;
        b.all[i].poolChar = ch;
        own.push_back(i);
        inPool.push_back(id);
      }
  for (auto& id : ids) {
    if (std::find(inPool.begin(), inPool.end(), id) != inPool.end()) continue;
    int i = add(b, id);
    if (i >= 0) shared.push_back(i);
  }
  std::sort(shared.begin(), shared.end(), [&](int x, int y) {
    return b.all[x].title != b.all[y].title ? b.all[x].title < b.all[y].title : b.all[x].id < b.all[y].id;
  });
  shared.insert(shared.end(), own.begin(), own.end());
  return shared;
}

void buildRelics(Book& b) {
  if (b.built) return;
  b.built = true;
  db::init();
  std::map<RelicRarity, std::vector<std::string>> byRarity;
  for (auto& id : db::relicIds())
    if (auto r = db::relic(id)) byRarity[r->rarity].push_back(id);
  std::vector<std::pair<std::string, std::vector<std::string>>> pools;
  for (auto& ch : db::allCharacters()) pools.push_back({ch, db::character(ch).relicPool});
  Builder bl{b};

  bl.head("relic_collection.STARTER");
  std::vector<int> starters, upgraded;
  for (auto& ch : db::allCharacters())
    for (auto& id : db::character(ch).startingRelics) {
      int i = addRelic(b, id);
      if (i >= 0) starters.push_back(i);
      int u = addRelic(b, upgradedStarter(id));
      if (u >= 0) upgraded.push_back(u);
    }
  bl.grid(starters);
  bl.divider();
  bl.grid(upgraded);

  const std::pair<RelicRarity, const char*> kPlain[] = {{RelicRarity::Common, "COMMON"},
                                                        {RelicRarity::Uncommon, "UNCOMMON"},
                                                        {RelicRarity::Rare, "RARE"},
                                                        {RelicRarity::Shop, "SHOP"}};
  for (auto& [rar, key] : kPlain) {
    bl.head(std::string("relic_collection.") + key);
    bl.grid(poolOrder(b, byRarity[rar], pools, addRelic));
  }

  bl.head("relic_collection.ANCIENT");
  const auto& ancient = byRarity[RelicRarity::Ancient];
  for (auto& a : ancientDefs()) {
    std::vector<int> items;
    for (const char* id : a.relics)
      if (std::find(ancient.begin(), ancient.end(), id) != ancient.end()) {
        int i = addRelic(b, id);
        if (i >= 0) items.push_back(i);
      }
    std::sort(items.begin(), items.end(), [&](int x, int y) { return b.all[x].title < b.all[y].title; });
    std::string icon = std::string("map/ancient_") + a.id;
    std::transform(icon.begin(), icon.end(), icon.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    bl.sub(a.id, icon, items);
    bl.grid(items);
  }

  bl.head("relic_collection.EVENT");
  bl.grid(poolOrder(b, byRarity[RelicRarity::Event], pools, addRelic));
  bl.finish();
}

void buildPotions(Book& b) {
  if (b.built) return;
  b.built = true;
  db::init();
  std::map<PotionRarity, std::vector<std::string>> byRarity;
  for (auto& id : db::potionIds())
    if (auto p = db::potion(id)) byRarity[p->rarity].push_back(id);
  std::vector<std::pair<std::string, std::vector<std::string>>> pools;
  for (auto& ch : db::allCharacters()) pools.push_back({ch, db::character(ch).potions});
  Builder bl{b};
  const std::pair<PotionRarity, const char*> kCats[] = {{PotionRarity::Common, "COMMON"},
                                                        {PotionRarity::Uncommon, "UNCOMMON"},
                                                        {PotionRarity::Rare, "RARE"}};
  for (auto& [rar, key] : kCats) {
    bl.head(std::string("potion_lab.") + key);
    bl.grid(poolOrder(b, byRarity[rar], pools, addPotion));
  }
  bl.head("potion_lab.SPECIAL");  // PotionRarity.Event, then Token
  std::vector<std::string> special = byRarity[PotionRarity::Event];
  special.insert(special.end(), byRarity[PotionRarity::Token].begin(), byRarity[PotionRarity::Token].end());
  bl.grid(poolOrder(b, special, pools, addPotion));
  bl.finish();
}

float listH() { return kListY1 - kListY0; }
float maxScroll() { return std::max(0.f, book().height - listH()); }

Entry* focused() {
  Book& b = book();
  State& st = S();
  if (st.sel < 0 || st.sel >= (int)b.pos.size()) return nullptr;
  auto [li, c] = b.pos[st.sel];
  return &b.all[b.lines[li].items[c]];
}
int sectionOf(int sel) {
  Book& b = book();
  if (sel < 0 || sel >= (int)b.pos.size()) return 0;
  return b.lines[b.pos[sel].first].section;
}

void scrollToSel() {
  State& st = S();
  Book& b = book();
  if (st.sel < 0 || st.sel >= (int)b.pos.size()) return;
  int li = b.pos[st.sel].first;
  float top = b.lines[li].y, bottom = top + b.lines[li].h;
  // Keep the headers right above the row in view when it is a section's first row.
  for (int k = li - 1; k >= 0 && b.lines[k].kind != Line::Row; --k) top = b.lines[k].y;
  if (top < st.scroll) st.scroll = top - 2;
  if (bottom + 4 > st.scroll + listH()) st.scroll = bottom + 4 - listH();
  st.scroll = std::clamp(st.scroll, 0.f, maxScroll());
}

// Focus index of the first entry of a section (or -1 if it lists none).
int firstOf(int section) {
  Book& b = book();
  for (int i = 0; i < (int)b.pos.size(); ++i)
    if (b.lines[b.pos[i].first].section == section) return i;
  return -1;
}

void jumpSection(int dir) {
  State& st = S();
  Book& b = book();
  int n = (int)b.sectionKeys.size();
  int s = sectionOf(st.sel);
  for (int k = 0; k < n; ++k) {
    s = (s + dir + n) % n;
    int f = firstOf(s);
    if (f >= 0) {
      st.sel = f;
      st.zone = 1;
      st.scroll = std::clamp(b.lines[b.sectionLine[s]].y - 2, 0.f, maxScroll());
      return;
    }
  }
}

// Row / column moves: the same column in the previous / next row, clamped to its length (the C#
// FocusNeighborTop / Bottom). Returns false when there is no row that way.
bool moveRow(int dir) {
  State& st = S();
  Book& b = book();
  if (st.sel < 0 || st.sel >= (int)b.pos.size()) return false;
  auto [li, c] = b.pos[st.sel];
  for (int k = li + dir; k >= 0 && k < (int)b.lines.size(); k += dir) {
    if (b.lines[k].kind != Line::Row) continue;
    int col = std::min(c, (int)b.lines[k].items.size() - 1);
    for (int i = 0; i < (int)b.pos.size(); ++i)
      if (b.pos[i].first == k && b.pos[i].second == col) { st.sel = i; return true; }
  }
  return false;
}

// Focus index under a bottom-screen point, or -1.
int itemAt(float tx, float ty) {
  State& st = S();
  Book& b = book();
  if (ty < kListY0 || ty >= kListY1) return -1;
  float ly = ty - kListY0 + st.scroll;
  for (int i = 0; i < (int)b.pos.size(); ++i) {
    const Line& l = b.lines[b.pos[i].first];
    float x = kGridX0 + b.pos[i].second * kCell;
    if (ly >= l.y && ly < l.y + l.h && tx >= x && tx < x + kCell) return i;
  }
  return -1;
}

bool ancientKnown(const Line& l) {
  if (progress::ancientTotalVisits(l.ancientId) > 0) return true;
  for (int i : l.subItems)
    if (book().all[i].seen) return true;
  return false;
}

std::string ancientName(const Line& l) {
  for (auto& a : ancientDefs())
    if (l.ancientId == a.id) return L(std::string("ancients.") + a.key + ".title");
  return l.ancientId;
}

void drawEntryIcon(const Entry& e, float x, float y, float size) {
  Sprite s = e.relic ? R().sprite("relic/" + e.relic->icon) : R().sprite("potion/" + e.potion->locKey);
  if (!s) {
    gfx::circle(x + size / 2, y + size / 2, size * 0.4f, 0x40404080);
    return;
  }
  float k = std::min(size / s.w, size / s.h), w = s.w * k, h = s.h * k;
  float sx = x + (size - w) / 2, sy = y + (size - h) / 2;
  // The icon's outline is approximated by a disc behind it (tinted silhouettes differ between
  // the SDL and citro2d tint models): half-transparent white for NotSeen, the pool's
  // LabOutlineColor for a character's relic / potion.
  const float cx = x + size / 2, cy = y + size / 2, r = size * 0.46f;
  if (!e.seen) {
    gfx::circle(cx, cy, r, 0xFFFFFF1C);
    spr(s, sx, sy, w, h, 0x000000FF, 0.9f);  // StsColors.ninetyPercentBlack
    return;
  }
  if (uint32_t c = poolColor(e.poolChar)) gfx::circle(cx, cy, r, (c & 0xFFFFFF00) | 0x58);
  spr(s, sx, sy, w, h);
}

void outline(float x, float y, float w, float h, uint32_t c = col::gold, float t = 2) {
  gfx::rect(x - t, y - t, w + 2 * t, t, c);
  gfx::rect(x - t, y + h, w + 2 * t, t, c);
  gfx::rect(x - t, y, t, h, c);
  gfx::rect(x + w, y, t, h, c);
}

std::string characterName(const std::string& id) {
  return id.empty() ? std::string() : L("characters." + db::character(id).key + ".title");
}
}  // namespace

// NRelicCollection / NPotionLab.OnSubmenuOpened: the grids are rebuilt with the current seen sets
// and the focus goes to the first entry, scrolled to the top.
void App::openRelicCollection(bool potions) {
  State& st = S();
  st.potions = potions;
  Book& b = book();
  if (potions) buildPotions(b);
  else buildRelics(b);
  st.open = true;
  st.sel = 0;
  st.zone = 1;
  st.bar = 0;
  st.scroll = 0;
  st.touchDown = false;
  const auto& seen = potions ? progress::state().seenPotions : progress::state().seenRelics;
  const bool all = getenv("STS_SEEN_ALL") != nullptr;  // debug: preview every entry as seen
  for (auto& e : b.all) e.seen = all || seen.count(e.id) > 0;
  if (const char* f = getenv("STS_COLLECTION_FOCUS")) {  // debug: start focused on an id
    for (int i = 0; i < (int)b.pos.size(); ++i)
      if (b.all[b.lines[b.pos[i].first].items[b.pos[i].second]].id == f) { st.sel = i; break; }
    scrollToSel();
  }
}

bool App::drawRelicCollection(bool top) {
  State& st = S();
  if (!st.open) return false;
  Book& b = book();
  const bool title = run_->screen == Screen::Title;
  const std::string what = st.potions ? "POTION_LAB_COLLECTION" : "COMPENDIUM_RELIC_COLLECTION";
  if (top) {
    if (title) drawMenuBg(true, 0.75f);
    else drawSceneBg(true, 0.8f);
    R().text(8, 4, L(st.potions ? "main_menu_ui.COMPENDIUM_POTION_LAB.title" : "main_menu_ui.COMPENDIUM_RELIC_COLLECTION.title"),
             ts(F16, col::gold));
    int seen = 0;
    for (auto& e : b.all) seen += e.seen;
    R().text(kTop - 8, 6, "已发现 " + num(seen) + "/" + num((int)b.all.size()), ts(F12, col::white, RIGHT));
    Entry* e = focused();
    if (e) {
      const float big = 72, ix = 22, iy = 34;
      gfx::circle(ix + big / 2, iy + big / 2, big * 0.62f, e->seen ? 0xFFE07030 : 0xFFFFFF14);
      drawEntryIcon(*e, ix, iy, big);
      const float rx = ix + big + 22, rw = kTop - rx - 10;
      std::string name = e->seen ? e->title : L("main_menu_ui." + what + ".unknown.title");
      TextStyle nt = ts(F16, e->seen ? col::gold : col::gray, LEFT, rw);
      nt.scale = 1.2f;
      float y = 40;
      y += R().text(rx, y, name, nt) + 4;
      std::string head, headDesc;
      splitHeader(b.sectionKeys[sectionOf(st.sel)], &head, &headDesc);
      std::string meta = head;
      if (e->seen && !e->poolChar.empty()) meta += " · " + characterName(e->poolChar);
      R().text(rx, y, meta, ts(F12, col::gray, LEFT, rw));
      y = std::max(y + 22, iy + big + 14);
      const float tx = 20, tw = kTop - 40;
      if (!e->seen) {
        R().text(tx, y, L("main_menu_ui." + what + ".unknown.description"), ts(F16, col::white, LEFT, tw));
      } else {
        std::string desc = e->relic ? describeRelic(e->relic.get()) : describePotion(e->potion.get());
        y += R().text(tx, y, desc, ts(F16, col::white, LEFT, tw)) + 10;
        std::string fk = (e->relic ? "relics." + e->relic->locKey : "potions." + e->potion->locKey) + ".flavor";
        if (R().hasLoc(fk) && y < kH - 50) R().text(tx, y, L(fk), ts(F12, 0xB8A888FF, LEFT, tw));
      }
      // The category's description (the C# header line) along the bottom.
      gfx::rect(0, kH - 36, kTop, 36, 0x00000090);
      R().text(8, kH - 33, head + "：" + headDesc, ts(F12, 0xD8D8D8FF, LEFT, kTop - 16));
    }
    R().text(kTop - 6, kH - 15, "L R 分类  B 返回", ts(F12, col::gray, RIGHT));
    return true;
  }

  if (title) drawMenuBg(false, 0.6f);
  else drawSceneBg(false, 0.7f);
  gfx::pushClip(0, kListY0, kBot, kListY1 - kListY0);
  for (int li = 0; li < (int)b.lines.size(); ++li) {
    const Line& l = b.lines[li];
    float y = kListY0 + l.y - st.scroll;
    if (y + l.h < kListY0 || y > kListY1) continue;
    switch (l.kind) {
      case Line::Head: {
        std::string head, desc;
        splitHeader(b.sectionKeys[l.section], &head, &desc);
        gfx::rect(6, y + kHeadH - 1, kBot - 12, 1, 0xC8B08060);
        R().text(8, y + 2, head, ts(F16, col::gold));
        int seen = 0, total = 0;
        for (const Line& o : b.lines)
          if (o.kind == Line::Row && o.section == l.section)
            for (int i : o.items) { ++total; seen += b.all[i].seen; }
        R().text(kBot - 10, y + 5, num(seen) + "/" + num(total), ts(F12, col::gray, RIGHT));
        break;
      }
      case Line::Sub: {
        Sprite ic = R().sprite(l.icon);
        if (ic) spr(ic, 10, y + 1, 18, 18);
        R().text(32, y + 3, (ancientKnown(l) ? ancientName(l) : L("relic_collection.UNKNOWN_ANCIENT")) + "：",
                 ts(F12, col::blue));
        break;
      }
      case Line::Divider:
        gfx::rect(kGridX0 + 8, y + kDividerH / 2, kPerRow * kCell - 16, 1, 0xFFFFFF30);
        break;
      case Line::Row:
        for (int c = 0; c < (int)l.items.size(); ++c) {
          float x = kGridX0 + c * kCell;
          const Entry& e = b.all[l.items[c]];
          bool on = st.zone == 1 && st.sel >= 0 && st.sel < (int)b.pos.size() && b.pos[st.sel].first == li &&
                    b.pos[st.sel].second == c;
          if (on) gfx::rect(x + 1, y + 1, kCell - 2, kCell - 2, 0xFFE07050);
          float sz = on ? kIcon * 1.2f : kIcon;
          drawEntryIcon(e, x + (kCell - sz) / 2, y + (kCell - sz) / 2, sz);
          if (on) outline(x + 1, y + 1, kCell - 2, kCell - 2, col::gold, 1.5f);
        }
        break;
    }
  }
  gfx::popClip();
  if (maxScroll() > 0) {  // scrollbar
    float h = listH(), th = std::max(16.f, h * h / (h + maxScroll()));
    float ty = kListY0 + (h - th) * (st.scroll / maxScroll());
    gfx::rect(kBot - 4, kListY0, 3, h, 0x00000080);
    gfx::rect(kBot - 4, ty, 3, th, 0xC8B080FF);
  }

  gfx::rect(0, kBarY - 3, kBot, kH - kBarY + 3, 0x000000A0);
  button(kBarX[0], kBarY, kBarW[0], kBarH, "返回", kBtnBack);
  button(kBarX[1], kBarY, kBarW[1], kBarH, "L 上一类", kBtnPrev);
  button(kBarX[2], kBarY, kBarW[2], kBarH, "R 下一类", kBtnNext);
  if (st.zone == 2 && !st.touchList) outline(kBarX[st.bar], kBarY, kBarW[st.bar], kBarH);
  return true;
}

bool App::updateRelicCollection(const gfx::Input& in) {
  State& st = S();
  if (!st.open) return false;
  Book& b = book();
  const int n = (int)b.pos.size();
  auto activate = [&](int id) {
    if (id == kBtnBack) st.open = false;
    else if (id == kBtnPrev) jumpSection(-1);
    else if (id == kBtnNext) jumpSection(1);
  };

  // Touch: buttons on press; the list scrolls on drag, a tap focuses an icon.
  if (in.touchDown) {
    int id = hitAt(in.tx, in.ty);
    st.touchList = true;
    if (id != ID_NONE) {
      for (int i = 0; i < 3; ++i) if (kBarId[i] == id) st.bar = i;
      activate(id);
      return true;
    }
    if (in.ty >= kListY0 && in.ty < kListY1) {
      st.touchDown = true;
      st.dragged = false;
      st.touchY0 = st.touchLastY = (float)in.ty;
      st.touchItem = itemAt((float)in.tx, (float)in.ty);
    }
  } else if (st.touchDown && in.touching) {
    if (std::fabs(in.ty - st.touchY0) > kTapSlop) st.dragged = true;
    if (st.dragged) st.scroll = std::clamp(st.scroll - (in.ty - st.touchLastY), 0.f, maxScroll());
    st.touchLastY = (float)in.ty;
  }
  if (in.touchUp && st.touchDown) {
    st.touchDown = false;
    if (!st.dragged && st.touchItem >= 0 && itemAt((float)in.tx, (float)in.ty) == st.touchItem) {
      st.zone = 1;
      st.sel = st.touchItem;
    }
    return true;
  }

  const uint32_t d = in.down;
  if (d) st.touchList = false;
  if (d & gfx::BTN_B) { st.open = false; return true; }
  if (d & gfx::BTN_L) { jumpSection(-1); return true; }
  if (d & gfx::BTN_R) { jumpSection(1); return true; }
  if (st.zone == 1) {
    if (n == 0) { st.zone = 2; return true; }
    if (d & gfx::BTN_RIGHT) st.sel = std::min(n - 1, st.sel + 1);
    if (d & gfx::BTN_LEFT) st.sel = std::max(0, st.sel - 1);
    if ((d & gfx::BTN_DOWN) && !moveRow(1)) st.zone = 2;
    if (d & gfx::BTN_UP) moveRow(-1);
    if (d & (gfx::BTN_LEFT | gfx::BTN_RIGHT | gfx::BTN_UP | gfx::BTN_DOWN)) scrollToSel();
  } else {
    if (d & gfx::BTN_LEFT) st.bar = (st.bar + 2) % 3;
    if (d & gfx::BTN_RIGHT) st.bar = (st.bar + 1) % 3;
    if ((d & gfx::BTN_UP) && n) { st.zone = 1; scrollToSel(); }
    if (d & gfx::BTN_A) activate(kBarId[st.bar]);
  }
  return true;
}

}  // namespace ui
