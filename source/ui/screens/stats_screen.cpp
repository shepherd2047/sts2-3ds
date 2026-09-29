// M6 (S25, RGDSplus U30): the stats screen and the run history viewer.
//
// NStatsScreen (Nodes.Screens.StatsScreen): NGeneralStatsGrid's entries (icon + two lines, the
// stats_screen loc) and one NCharacterStats block per character. NRunHistory
// (Nodes.Screens.RunHistoryScreen): one stored run at a time -- character, ascension, date, seed,
// the death / victory quote (LoadDeathQuote), the path per act as room icons
// (NMapPointHistoryEntry, ImageHelper.GetRoomIconPath), the deck and the relics; the port adds
// the score and the badges stored in the record (M7) and a list of every stored run.
//
// RGDSplus U30: lists and controls on the bottom screen (character rows, run list, path nodes,
// cards, relics, badges, paging, back), the focused item's details on the top screen (the stats
// grid, the run summary with a path preview, the card / relic / badge / floor picked below).
// Keys: D-pad focus, A open, B back, L/R page (run list) or switch tab (a run), X 历史记录 from
// the stats page. Everything is tappable (tap a focused row again to open it).
//
// PORT NOTE: progress.sav keeps no playtime, kill count, event discovery or overall streak (see
// progress.h), so playtime and fastest win are summed from the stored run history (the last 50
// runs), the overall best streak is the best character streak, and the kill / event / achievement /
// unlock entries are left out. "Discovered" counts have no denominator (everything is unlocked).
#include <cstring>

#include "../../core/badges.h"
#include "../../core/history.h"
#include "../../core/profiles.h"
#include "../../core/progress.h"
#include "../ui_common.h"

namespace ui {

namespace {
enum Page { kClosed, kStatsPage, kListPage, kRunPage };
// Touch ids (the main menu's are 2001-2399).
constexpr int kStatRow0 = 3001;  // + 0 overview, 1.. characters
constexpr int kToHistory = 3020, kBackId = 3021, kPageUp = 3022, kPageDn = 3023, kOpenRun = 3024;
constexpr int kRunRow0 = 3100;   // + run index
constexpr int kTab0 = 3170;      // + tab
constexpr int kItem0 = 20000;    // + item index in the open tab
constexpr int kTabs = 4;         // 路线 / 牌组 / 遗物 / 徽章
constexpr float kListY = 29, kListRowH = 24;
constexpr int kListRows = 7;
constexpr float kBarY = 202, kBarH = 34;  // bottom action bar
constexpr float kContentY = 34, kContentH = kBarY - 4 - kContentY;
// Deck grid (cards at 0.4), relic grid, badge rows, path icons.
constexpr float kCardS = 0.4f, kCardW = 120 * kCardS, kCardH = 169 * kCardS, kCardPitchY = kCardH + 5;
constexpr int kCardCols = 6;
constexpr float kRelicCell = 38;
constexpr int kRelicCols = 8;
constexpr float kBadgeRowH = 38;
constexpr float kPathPitch = 18, kPathIcon = 17;

struct Floor {
  int act, index;  // path[act][index]
  int number;      // floor number (Ancient start points have none: 0)
};

struct State {
  Page page = kClosed;
  bool fromStats = false;  // the history list was opened from the stats page (B goes back there)
  int statSel = 0;         // 0 overview, 1.. characters
  std::vector<history::RunRecord> runs;  // newest first
  int runSel = 0, runTop = 0;
  int tab = 0;
  int item[kTabs] = {}, top[kTabs] = {};  // focused item and first visible row per tab
  // The open run's deck and relics as models (for drawCard / drawRelicIcon), and its floors.
  std::vector<std::unique_ptr<Card>> cards;
  std::vector<std::unique_ptr<Relic>> relics;
  std::vector<Floor> floors;
};
State& S() {
  static State s;
  return s;
}

std::string fill(std::string s, const std::string& name, const std::string& value) {
  std::string tag = "{" + name + "}";
  for (size_t p; (p = s.find(tag)) != std::string::npos;) s.replace(p, tag.size(), value);
  return s;
}

// VantomBoss -> VANTOM_BOSS (ModelId.Entry: the class name in upper snake case).
std::string upperSnake(const std::string& id) {
  std::string key;
  for (size_t i = 0; i < id.size(); ++i) {
    char c = id[i];
    if (i > 0 && std::isupper((unsigned char)c) && !std::isupper((unsigned char)id[i - 1])) key += '_';
    key += (char)std::toupper((unsigned char)c);
  }
  return key;
}
std::string lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

const Character& charOf(const std::string& id) { return db::character(id); }
std::string charTitle(const std::string& id) { return L("characters." + charOf(id).key + ".title"); }
std::string charIcon(const std::string& id) { return "ui/char_" + charOf(id).energyColor; }

std::string encounterTitle(const std::string& id) {
  std::string k = "encounters." + upperSnake(id) + ".title";
  return R().hasLoc(k) ? L(k) : id;
}
std::string eventTitle(const std::string& id) {
  std::string k = upperSnake(id);
  if (R().hasLoc("events." + k + ".title")) return L("events." + k + ".title");
  if (R().hasLoc("ancients." + k + ".title")) return L("ancients." + k + ".title");
  return id;
}

// TimeFormatting.Format: h:mm:ss, or m:ss under an hour.
std::string duration(int secs) {
  secs = std::max(0, secs);
  char b[32];
  if (secs >= 3600) snprintf(b, sizeof b, "%d:%02d:%02d", secs / 3600, secs / 60 % 60, secs % 60);
  else snprintf(b, sizeof b, "%d:%02d", secs / 60, secs % 60);
  return b;
}
std::string dateTime(int64_t t, bool withYear = true) {
  if (t <= 0) return "--";
  time_t tt = (time_t)t;
  struct tm* lt = localtime(&tt);
  if (!lt) return "--";
  char b[40];
  if (withYear) snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min);
  else snprintf(b, sizeof b, "%02d-%02d %02d:%02d", lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min);
  return b;
}

std::string resultText(const history::RunRecord& r) {
  if (r.win) return "[green]胜利[/green]";
  if (r.abandoned) return "[purple]放弃[/purple]";
  return "[red]败北[/red]";
}

// NRunHistory.LoadDeathQuote: the quote under the header, picked with the run's seed.
std::string deathQuote(const history::RunRecord& r, uint32_t* color) {
  auto pick = [&](const std::string& prefix, int n) {
    for (int i = n; i > 0; --i) {  // the first index that exists at or below seed % n
      std::string k = prefix + "." + num((int)((r.seed + i) % n));
      if (R().hasLoc(k)) return L(k);
    }
    return std::string();
  };
  const std::string who = charTitle(r.character);
  std::string q;
  *color = col::red;
  if (r.win) {
    *color = col::green;
    q = R().hasLoc("run_history.MAP_POINT_HISTORY.victory.1") ? L("run_history.MAP_POINT_HISTORY.victory.1")
                                                             : pick("run_history.MAP_POINT_HISTORY.falseVictory", 2);
  } else if (r.abandoned) {
    q = pick("run_history.MAP_POINT_HISTORY.abandon", 3);
  } else if (!r.killedByEncounter.empty()) {
    std::string k = "encounters." + upperSnake(r.killedByEncounter) + ".loss";
    q = R().hasLoc(k) ? L(k) : pick("run_history.MAP_POINT_HISTORY.defeat", 2);
    q = fill(fill(q, "encounter", encounterTitle(r.killedByEncounter)), "encounterName", encounterTitle(r.killedByEncounter));
  } else if (!r.killedByEvent.empty()) {
    std::string k = "events." + upperSnake(r.killedByEvent) + ".loss";
    q = fill(R().hasLoc(k) ? L(k) : L("run_history.DEFAULT_EVENT_LOSS_MESSAGE"), "event", eventTitle(r.killedByEvent));
  } else {
    q = pick("run_history.MAP_POINT_HISTORY.defeat", 2);
    q = fill(q, "encounterName", "???");
  }
  q = fill(q, "character", who);
  return "“" + q + "”";
}

// ImageHelper.GetRoomIconSuffix: bosses and Ancients by model id, "?" points by the room found.
std::string pointIcon(const history::MapPoint& p) {
  using history::PointType;
  using history::RoomKind;
  const history::Room* first = p.rooms.empty() ? nullptr : &p.rooms.front();
  if ((p.type == PointType::Boss || p.type == PointType::Ancient) && first && !first->model.empty()) {
    std::string k = lower(upperSnake(first->model));
    if (p.type == PointType::Boss && k.size() > 5 && k.compare(k.size() - 5, 5, "_boss") != 0) k += "_boss";
    if (R().sprite("hist/" + k)) return "hist/" + k;
    return p.type == PointType::Boss ? "map/elite" : "hist/ancient";
  }
  RoomKind rk = first ? first->type : RoomKind::Unassigned;
  if (rk == RoomKind::Unassigned) {
    switch (p.type) {
      case PointType::Monster: rk = RoomKind::Monster; break;
      case PointType::Elite: rk = RoomKind::Elite; break;
      case PointType::Shop: rk = RoomKind::Shop; break;
      case PointType::Treasure: rk = RoomKind::Treasure; break;
      case PointType::RestSite: rk = RoomKind::RestSite; break;
      default: rk = RoomKind::Event; break;
    }
  }
  const char* s = "event";
  switch (rk) {
    case RoomKind::Monster: s = "monster"; break;
    case RoomKind::Elite: s = "elite"; break;
    case RoomKind::Shop: s = "shop"; break;
    case RoomKind::Treasure: s = "treasure"; break;
    case RoomKind::RestSite: s = "rest_site"; break;
    default: break;
  }
  std::string name = s;
  if (p.type == PointType::Unknown && rk != RoomKind::Event) name = "unknown_" + name;
  return "hist/" + name;
}

std::string pointTypeName(history::PointType t) {
  using history::PointType;
  switch (t) {
    case PointType::Monster: return roomName(RoomType::Monster);
    case PointType::Elite: return roomName(RoomType::Elite);
    case PointType::RestSite: return roomName(RoomType::Rest);
    case PointType::Treasure: return roomName(RoomType::Treasure);
    case PointType::Shop: return roomName(RoomType::Shop);
    case PointType::Boss: return L("map.LEGEND_BOSS.title");
    case PointType::Ancient: return L("map.LEGEND_ANCIENT.title");
    default: return L("map.LEGEND_UNKNOWN.title");
  }
}
std::string roomKindName(history::RoomKind k) {
  using history::RoomKind;
  switch (k) {
    case RoomKind::Monster: return L("map.LEGEND_ENEMY.title");
    case RoomKind::Elite: return L("map.LEGEND_ELITE.title");
    case RoomKind::Boss: return L("map.LEGEND_BOSS.title");
    case RoomKind::Treasure: return L("map.LEGEND_TREASURE.title");
    case RoomKind::Shop: return L("map.LEGEND_MERCHANT.title");
    case RoomKind::Event: return L("map.LEGEND_EVENT.hoverTip.title");
    case RoomKind::RestSite: return L("map.LEGEND_REST.hoverTip.title");
    default: return "";
  }
}

void outline(float x, float y, float w, float h, uint32_t c = col::gold, float t = 2) {
  gfx::rect(x - t, y - t, w + 2 * t, t, c);
  gfx::rect(x - t, y + h, w + 2 * t, t, c);
  gfx::rect(x - t, y, t, h, c);
  gfx::rect(x + w, y, t, h, c);
}

// A badge: its art on the rarity plate (NBadge).
void drawBadge(const history::BadgeEntry& b, float x, float y, float size) {
  Sprite plate = R().sprite(std::string("badge/") + badges::rarityName(b.rarity));
  if (plate) spr(plate, x, y, size, size);
  Sprite art = R().sprite("badge/" + lower(b.id));
  if (art) spr(art, x, y, size, size);
  else R().text(x + size / 2, y + size / 2 - 7, "?", ts(F12, col::white, CENTER));
}
std::string badgeText(const history::BadgeEntry& b, bool title) {
  badges::LocKeys k = badges::locKeys(b);
  const std::string& a = title ? k.rarityTitle : k.rarityDescription;
  const std::string& p = title ? k.title : k.description;
  if (R().hasLoc(a)) return L(a);
  if (R().hasLoc(p)) return L(p);
  return b.id;
}

// Per-character numbers the port reads from the stored history (see PORT NOTE above).
struct HistoryStats {
  int runs = 0, playtime = 0, fastestWin = -1, bestScore = 0;
};
HistoryStats historyStats(const std::vector<history::RunRecord>& runs, const std::string& charId) {
  HistoryStats h;
  for (auto& r : runs) {
    if (!charId.empty() && r.character != charId) continue;
    ++h.runs;
    h.playtime += r.runTime;
    h.bestScore = std::max(h.bestScore, r.score);
    if (r.win && (h.fastestWin < 0 || r.runTime < h.fastestWin)) h.fastestWin = r.runTime;
  }
  return h;
}

// One NStatEntry: icon, then the top and bottom lines.
void statEntry(float x, float y, float w, const char* icon, const std::string& topLine, const std::string& bottomLine) {
  Sprite s = R().sprite(std::string("ui/") + icon);
  if (s) spr(s, x, y + 2, 28, 28);
  const float tx = x + 34;
  if (bottomLine.empty()) {
    R().text(tx, y + 8, topLine, ts(F12, col::white, LEFT, w - 34));
  } else {
    R().text(tx, y, topLine, ts(F12, col::white, LEFT, w - 34));
    R().text(tx, y + 16, bottomLine, ts(F12, col::white, LEFT, w - 34));
  }
}

std::string ss(const std::string& key) { return L("stats_screen." + key); }
}  // namespace

void App::openStats(int page) {
  State& s = S();
  db::init();  // names, relic and card models need the registry (nothing runs before the first run)
  s.runs = history::load(profiles::current());
  s.runSel = s.runTop = 0;
  s.fromStats = false;
  s.page = page == 2 ? kListPage : kStatsPage;
  if (page != 2) s.statSel = 0;
}

namespace {
void openRun(State& s) {
  if (s.runs.empty()) return;
  const history::RunRecord& r = s.runs[std::clamp(s.runSel, 0, (int)s.runs.size() - 1)];
  s.cards.clear();
  s.relics.clear();
  s.floors.clear();
  for (auto& d : r.deck) {
    auto c = db::card(d.id);
    if (!c) continue;
    for (int i = 0; i < d.upgrades; ++i) c->upgrade();
    if (!d.enchantment.empty())
      if (auto e = db::enchantment(d.enchantment)) {
        e->card = c.get();
        e->amount = d.enchantAmount;
        c->enchantment.p = std::move(e);
      }
    s.cards.push_back(std::move(c));
  }
  for (auto& id : r.relics)
    if (auto rel = db::relic(id)) s.relics.push_back(std::move(rel));
  int n = 0;
  for (int a = 0; a < (int)r.path.size(); ++a)
    for (int i = 0; i < (int)r.path[a].size(); ++i)
      s.floors.push_back({a, i, r.path[a][i].type == history::PointType::Ancient ? 0 : ++n});
  s.tab = 0;
  for (int t = 0; t < kTabs; ++t) s.item[t] = s.top[t] = 0;
  s.item[0] = std::max(0, (int)s.floors.size() - 1);  // the floor the run ended on (its quote)
  s.page = kRunPage;
}

int tabCount(const State& s, const history::RunRecord& r, int tab) {
  switch (tab) {
    case 0: return (int)s.floors.size();
    case 1: return (int)s.cards.size();
    case 2: return (int)s.relics.size();
    default: return (int)r.badges.size();
  }
}
}  // namespace

// ================================================================ drawing

bool App::drawStats(bool top) {
  State& s = S();
  if (s.page == kClosed) return false;
  drawMenuBg(top, top ? 0.6f : 0.55f);  // B07: the menu's own background
  if (s.page == kStatsPage) drawStatsPage(top);
  else if (s.page == kListPage) drawHistoryList(top);
  else drawRunDetail(top);
  return true;
}

void App::drawStatsPage(bool top) {
  State& s = S();
  const auto& ids = db::characterIds();
  const int rows = 1 + (int)ids.size();
  s.statSel = std::clamp(s.statSel, 0, rows - 1);
  Progress& pr = progress::state();
  if (top) {
    TextStyle ht = ts(F16, col::gold, CENTER, 0, 1.15f);
    ht.outline = 0x000000FF;
    const float px = 12, py = 30, pw = kTop - 24, ph = kH - py - 8;
    widgets::panel("ui/hover_tip", px, py, pw, ph);
    if (s.statSel == 0) {  // NGeneralStatsGrid
      R().text(kTop / 2, 7, L("main_menu_ui.STATISTICS.OVERALL.title"), ht);
      int wins = 0, losses = 0, asc = 0, best = 0;
      for (auto& id : ids) {
        auto it = pr.characters.find(id);
        if (it == pr.characters.end()) continue;
        wins += it->second.wins;
        losses += it->second.losses;
        asc += it->second.maxAscension;
        best = std::max(best, it->second.bestStreak);
      }
      HistoryStats h = historyStats(s.runs, "");
      auto counter = [&](const char* k) { auto it = pr.counters.find(k); return it == pr.counters.end() ? 0LL : (long long)it->second; };
      const float cw = (pw - 24) / 2, x0 = px + 12, x1 = x0 + cw, y0 = py + 12, rh = 44;
      statEntry(x0, y0, cw, "stats_clock", fill(ss("ENTRY_PLAYTIME.top"), "Playtime", duration(h.playtime)),
                h.fastestWin >= 0 ? fill(ss("ENTRY_PLAYTIME.bottom"), "FastestWin", duration(h.fastestWin)) : "");
      statEntry(x1, y0, cw, "stats_swords", fill(ss("ENTRY_WIN_LOSS.top"), "Amount", num(asc) + "/" + num(10 * (int)ids.size())),
                fill(fill(ss("ENTRY_WIN_LOSS.bottom"), "Wins", num(wins)), "Losses", num(losses)));
      statEntry(x0, y0 + rh, cw, "stats_chain", fill(ss("ENTRY_STREAK.top"), "Amount", num(best)),
                "放弃 [blue]" + std::to_string(counter("runsAbandoned")) + "[/blue]");
      statEntry(x1, y0 + rh, cw, "stats_trophy", "最高分数 [blue]" + num(h.bestScore) + "[/blue]",
                "历史记录 [blue]" + num((int)s.runs.size()) + "[/blue]");
      statEntry(x0, y0 + 2 * rh, cw, "stats_cards", "卡牌", fill(ss("ENTRY_CARDS.bottom"), "Amount", num((int)pr.seenCards.size())));
      statEntry(x1, y0 + 2 * rh, cw, "stats_chest", "遗物", fill(ss("ENTRY_RELIC.bottom"), "Amount", num((int)pr.seenRelics.size())));
      statEntry(x0, y0 + 3 * rh, cw, "stats_potions_seen", "药水",
                fill(ss("ENTRY_POTION.bottom"), "Amount", num((int)pr.seenPotions.size())));
      statEntry(x1, y0 + 3 * rh, cw, "stats_monsters", "怪物",
                fill(ss("ENTRY_MONSTER.bottom"), "Amount", num((int)pr.seenMonsters.size())));
      R().text(kTop / 2, py + ph - 20, "游玩时间与最高分数来自最近 " + num(history::kMax) + " 局的历史记录",
               ts(F12, col::gray, CENTER));
      return;
    }
    // NCharacterStats
    const std::string& id = ids[s.statSel - 1];
    const Character& ch = charOf(id);
    R().text(kTop / 2, 7, L("main_menu_ui.STATISTICS.title"), ht);
    Sprite art = R().sprite("ui/" + ch.energyColor + "_select");
    const float ah = 150, aw = art ? ah * art.w / art.h : 0;
    if (art) spr(art, px + 14, py + (ph - ah) / 2, aw, ah);
    const float x = px + 28 + aw, w = pw - (x - px) - 12;
    TextStyle nt = ts(F16, col::gold, LEFT, 0, 1.2f);
    R().text(x, py + 10, L("characters." + ch.key + ".title"), nt);
    CharacterProgress cp;
    if (auto it = pr.characters.find(id); it != pr.characters.end()) cp = it->second;
    HistoryStats h = historyStats(s.runs, id);
    float y = py + 40;
    statEntry(x, y, w, "stats_clock", fill(ss("ENTRY_CHAR_PLAYTIME.top"), "Playtime", duration(h.playtime)),
              h.fastestWin >= 0 ? fill(ss("ENTRY_CHAR_PLAYTIME.bottom"), "FastestWin", duration(h.fastestWin)) : "");
    statEntry(x, y + 42, w, "stats_swords", fill(ss("ENTRY_CHAR_WIN_LOSS.top"), "Amount", num(cp.maxAscension)),
              fill(fill(ss("ENTRY_CHAR_WIN_LOSS.bottom"), "Wins", num(cp.wins)), "Losses", num(cp.losses)));
    statEntry(x, y + 84, w, "stats_chain", fill(ss("ENTRY_CHAR_STREAK.top"), "Amount", num(cp.currentStreak)),
              fill(ss("ENTRY_CHAR_STREAK.bottom"), "Amount", num(cp.bestStreak)));
    statEntry(x, y + 126, w, "stats_trophy", "最高分数 [blue]" + num(h.bestScore) + "[/blue]",
              "历史记录 [blue]" + num(h.runs) + "[/blue]");
    return;
  }

  // Bottom: the overview row and one row per character, then back / run history.
  const float rx = 10, rw = kBot - 20, rh = 28, y0 = 8;
  for (int i = 0; i < rows; ++i) {
    const float y = y0 + i * (rh + 3);
    const bool focus = i == s.statSel;
    widgets::panel("ui/hover_tip", rx, y, rw, rh, focus ? 0xFFFFFFFF : 0xB0B0B0FF);
    if (i == 0) {
      Sprite ic = R().sprite("ui/stats_trophy");
      if (ic) spr(ic, rx + 6, y + 3, 22, 22);
      R().text(rx + 34, y + (rh - R().lineHeight(F16)) / 2, L("main_menu_ui.STATISTICS.OVERALL.title"),
               ts(F16, focus ? col::gold : col::white));
      int w = 0, l = 0;
      for (auto& [cid, c] : pr.characters) { w += c.wins; l += c.losses; }
      R().text(rx + rw - 8, y + 7, "[green]" + num(w) + " 胜[/green]  [red]" + num(l) + " 败[/red]", ts(F12, col::white, RIGHT));
    } else {
      const std::string& id = ids[i - 1];
      Sprite ic = R().sprite(charIcon(id));
      if (ic) spr(ic, rx + 6, y + 3, 22, 22);
      R().text(rx + 34, y + (rh - R().lineHeight(F16)) / 2, charTitle(id), ts(F16, focus ? col::gold : col::white));
      CharacterProgress cp;
      if (auto it = pr.characters.find(id); it != pr.characters.end()) cp = it->second;
      R().text(rx + rw - 8, y + 7,
               "[green]" + num(cp.wins) + " 胜[/green]  [red]" + num(cp.losses) + " 败[/red]   进阶 [blue]" + num(cp.maxAscension) + "[/blue]",
               ts(F12, col::white, RIGHT));
    }
    if (focus) outline(rx, y, rw, rh);
    hits_.push_back({rx, y, rw, rh, kStatRow0 + i});
  }
  widgets::panel("ui/btn_back", 8, kBarY, 96, kBarH);
  R().text(8 + 48, kBarY + (kBarH - R().lineHeight(F16)) / 2, "返回", ts(F16, col::white, CENTER));
  hits_.push_back({8, kBarY, 96, kBarH, kBackId});
  const float hw = 124, hx = kBot - 8 - hw;
  spr(R().sprite("ui/btn_compendium"), hx, kBarY, hw, kBarH, 0xFFFFFFFF);
  Sprite hi = R().sprite("ui/sub_history");
  if (hi) spr(hi, hx + 6, kBarY + (kBarH - 20) / 2, 32, 20);
  R().text(hx + 42, kBarY + (kBarH - R().lineHeight(F16)) / 2, L("main_menu_ui.RUN_HISTORY.title"), ts(F16, col::white));
  hits_.push_back({hx, kBarY, hw, kBarH, kToHistory});
}

void App::drawHistoryList(bool top) {
  State& s = S();
  const int n = (int)s.runs.size();
  if (top) {
    TextStyle ht = ts(F16, col::gold, CENTER, 0, 1.15f);
    ht.outline = 0x000000FF;
    R().text(kTop / 2, 7, L("main_menu_ui.RUN_HISTORY.title"), ht);
    const float px = 12, py = 30, pw = kTop - 24, ph = kH - py - 8;
    widgets::panel("ui/hover_tip", px, py, pw, ph);
    if (n == 0) {
      R().text(kTop / 2, py + ph / 2 - 8, "还没有完成的游戏", ts(F16, col::gray, CENTER));
      return;
    }
    const history::RunRecord& r = s.runs[std::clamp(s.runSel, 0, n - 1)];
    float y = py + 8;
    Sprite ic = R().sprite(charIcon(r.character));
    if (ic) spr(ic, px + 10, y, 26, 26);
    std::string head = charTitle(r.character);
    if (r.ascension > 0) head += "   进阶 " + num(r.ascension);
    R().text(px + 42, y + 4, head, ts(F16, col::gold));
    R().text(px + pw - 12, y + 4, resultText(r), ts(F16, col::white, RIGHT));
    y += 30;
    uint32_t qc;
    std::string q = deathQuote(r, &qc);
    y += R().text(px + 12, y, q, ts(F12, qc, LEFT, pw - 24)) + 4;
    R().text(px + 12, y, "[gold]" + dateTime(r.startTime) + "[/gold]    " + fill(L("run_history.INFO.SEED"), "Seed", std::to_string((unsigned long long)r.seed)),
             ts(F12, col::white));
    y += 18;
    R().text(px + 12, y,
             "第 [blue]" + num(r.floorReached) + "[/blue] 层   用时 [blue]" + duration(r.runTime) + "[/blue]   分数 [gold]" + num(r.score) +
                 "[/gold]   [icon:hp] " + num(r.hp) + "/" + num(r.maxHp) + "   [icon:gold] " + num(r.gold),
             ts(F12, col::white));
    y += 22;
    // The path preview: one row of small room icons per act.
    for (int a = 0; a < (int)r.path.size() && y < py + ph - 50; ++a) {
      R().text(px + 12, y + 1, "第" + num(a + 1) + "幕", ts(F12, col::gray));
      float x = px + 50;
      for (auto& p : r.path[a]) {
        Sprite si = R().sprite(pointIcon(p));
        if (si) spr(si, x, y, 15, 15);
        x += 16;
        if (x > px + pw - 16) break;
      }
      y += 17;
    }
    // Deck / relic counts and the badges.
    y = std::max(y + 4, py + ph - 48);
    R().text(px + 12, y, fill(L("run_history.DECK_HISTORY.header"), "totalCards", num((int)r.deck.size())) + "   " +
                             fill(L("run_history.RELIC_HISTORY.header"), "totalRelics", num((int)r.relics.size())),
             ts(F12, col::white));
    float bx = px + 12;
    for (auto& b : r.badges) {
      if (bx > px + pw - 30) break;
      drawBadge(b, bx, y + 16, 24);
      bx += 26;
    }
    if (r.badges.empty()) R().text(px + 12, y + 20, r.abandoned ? "放弃的游戏没有徽章" : "没有徽章", ts(F12, col::gray));
    return;
  }

  // Bottom: header, the rows, the action bar.
  R().text(10, 6, L("main_menu_ui.RUN_HISTORY.title") + "（" + num(n) + "）", ts(F16, col::gold));
  if (n > 0) R().text(kBot - 10, 8, num(s.runSel + 1) + " / " + num(n), ts(F12, col::gray, RIGHT));
  s.runSel = std::clamp(s.runSel, 0, std::max(0, n - 1));
  if (s.runSel < s.runTop) s.runTop = s.runSel;
  if (s.runSel >= s.runTop + kListRows) s.runTop = s.runSel - kListRows + 1;
  s.runTop = std::clamp(s.runTop, 0, std::max(0, n - kListRows));
  const float rx = 8, rw = kBot - 16;
  for (int i = s.runTop; i < std::min(n, s.runTop + kListRows); ++i) {
    const history::RunRecord& r = s.runs[i];
    const float y = kListY + (i - s.runTop) * kListRowH;
    const bool focus = i == s.runSel;
    gfx::rect(rx, y, rw, kListRowH - 2, focus ? 0x3B6272F0 : (i % 2 ? 0x1A2A33D8 : 0x22323BD8));
    Sprite ic = R().sprite(charIcon(r.character));
    if (ic) spr(ic, rx + 3, y + 2, 18, 18);
    R().text(rx + 25, y + 4, resultText(r), ts(F12, col::white));
    R().text(rx + 60, y + 4, r.ascension > 0 ? "进阶" + num(r.ascension) : std::string("-"), ts(F12, col::blue));
    R().text(rx + 104, y + 4, "第" + num(r.floorReached) + "层", ts(F12, col::white));
    R().text(rx + 158, y + 4, dateTime(r.startTime, false), ts(F12, col::gray));
    R().text(rx + rw - 6, y + 4, num(r.score), ts(F12, col::gold, RIGHT));
    if (focus) outline(rx, y, rw, kListRowH - 2);
    hits_.push_back({rx, y, rw, kListRowH - 2, kRunRow0 + i});
  }
  if (n == 0) R().text(kBot / 2, 90, "还没有完成的游戏", ts(F16, col::gray, CENTER));
  widgets::panel("ui/btn_back", 8, kBarY, 96, kBarH);
  R().text(8 + 48, kBarY + (kBarH - R().lineHeight(F16)) / 2, "返回", ts(F16, col::white, CENTER));
  hits_.push_back({8, kBarY, 96, kBarH, kBackId});
  // Page buttons (L / R) and 查看 (A).
  const float pb = 40, px0 = 116;
  for (int k = 0; k < 2; ++k) {
    const float x = px0 + k * (pb + 6);
    const bool on = k == 0 ? s.runTop > 0 : s.runTop + kListRows < n;
    widgets::panel("ui/btn_ok_s", x, kBarY, pb, kBarH, on ? 0xFFFFFFFF : 0x707070FF);
    R().text(x + pb / 2, kBarY + (kBarH - R().lineHeight(F16)) / 2, k == 0 ? "▲" : "▼", ts(F16, on ? col::white : col::gray, CENTER));
    R().text(x + pb - 3, kBarY + kBarH - 13, k == 0 ? "L" : "R", ts(F12, col::gray, RIGHT, 0, 0.8f));
    hits_.push_back({x, kBarY, pb, kBarH, k == 0 ? kPageUp : kPageDn});
  }
  const float ow = 96, ox = kBot - 8 - ow;
  widgets::panel("ui/btn_proceed", ox, kBarY, ow, kBarH, n ? 0xFFFFFFFF : 0x707070FF);
  R().text(ox + ow / 2, kBarY + (kBarH - R().lineHeight(F16)) / 2, "查看", ts(F16, n ? col::white : col::gray, CENTER));
  hits_.push_back({ox, kBarY, ow, kBarH, kOpenRun});
}

void App::drawRunDetail(bool top) {
  State& s = S();
  if (s.runs.empty()) { s.page = kListPage; return; }
  const history::RunRecord& r = s.runs[std::clamp(s.runSel, 0, (int)s.runs.size() - 1)];
  const int count = tabCount(s, r, s.tab);
  int& sel = s.item[s.tab];
  sel = std::clamp(sel, 0, std::max(0, count - 1));
  if (top) {
    // Header: character, ascension, result, date, seed, score (NRunHistory's top rows).
    const float px = 8, pw = kTop - 16;
    widgets::panel("ui/hover_tip", px, 4, pw, 58);
    Sprite ic = R().sprite(charIcon(r.character));
    if (ic) spr(ic, px + 8, 9, 22, 22);
    std::string head = charTitle(r.character);
    if (r.ascension > 0) head += "  进阶 " + num(r.ascension);
    R().text(px + 36, 10, head, ts(F16, col::gold));
    R().text(px + pw - 10, 10, resultText(r) + "   分数 [gold]" + num(r.score) + "[/gold]", ts(F16, col::white, RIGHT));
    R().text(px + 10, 34, "[gold]" + dateTime(r.startTime) + "[/gold]   " + fill(L("run_history.INFO.SEED"), "Seed", std::to_string((unsigned long long)r.seed)) +
                              "   第 " + num(r.floorReached) + " 层   " + duration(r.runTime),
             ts(F12, col::white));
    // The focused item.
    const float by = 68, bh = kH - by - 4;
    widgets::panel("ui/hover_tip", px, by, pw, bh);
    if (count == 0) {
      uint32_t qc;
      R().text(kTop / 2, by + 20, deathQuote(r, &qc), ts(F12, qc, CENTER, pw - 30));
      R().text(kTop / 2, by + bh / 2, s.tab == 3 ? (r.abandoned ? "放弃的游戏没有徽章" : "没有徽章") : "（空）", ts(F12, col::gray, CENTER));
      return;
    }
    if (s.tab == 0) {  // a floor: NMapPointHistoryEntry's hover tip
      const Floor& f = s.floors[sel];
      const history::MapPoint& p = r.path[f.act][f.index];
      Sprite si = R().sprite(pointIcon(p));
      if (si) spr(si, px + 12, by + 10, 36, 36);
      std::string title = f.number ? fill(L("run_history.MAP_POINT_HISTORY.header"), "FloorNum", num(f.number)) : pointTypeName(p.type);
      R().text(px + 56, by + 10, title, ts(F16, col::gold));
      R().text(px + 56, by + 30, "第" + num(f.act + 1) + "幕 · " + pointTypeName(p.type), ts(F12, col::gray));
      float y = by + 54;
      for (auto& room : p.rooms) {
        std::string line = roomKindName(room.type);
        if (!room.model.empty()) {
          bool fight = room.type == history::RoomKind::Monster || room.type == history::RoomKind::Elite || room.type == history::RoomKind::Boss;
          line += "：[gold]" + (fight ? encounterTitle(room.model) : eventTitle(room.model)) + "[/gold]";
        }
        R().text(px + 16, y, line, ts(F12, col::white, LEFT, pw - 32));
        y += 17;
      }
      std::string st;
      if (p.goldGained > 0) st += "[gold]+" + num(p.goldGained) + " 金币[/gold]   ";
      if (p.goldSpent > 0) st += fill(L("run_history.HISTORY_ENTRY.goldSpent"), "Amount", num(p.goldSpent)) + "   ";
      if (p.damageTaken > 0) st += fill(L("run_history.MAP_POINT_HISTORY.damageTaken"), "Damage", num(p.damageTaken)) + "   ";
      if (!st.empty()) { R().text(px + 16, y + 2, st, ts(F12, col::white, LEFT, pw - 32)); y += 19; }
      for (auto& c : p.restChoices) {
        std::string k = "rest_site_ui.OPTION_" + c + ".name";
        R().text(px + 16, y + 2, fill(L("run_history.MAP_POINT_HISTORY.chose"), "Choice", R().hasLoc(k) ? L(k) : c), ts(F12, col::white));
        y += 17;
      }
      // The last point of a lost run: the quote.
      if (sel == (int)s.floors.size() - 1) {
        uint32_t qc;
        R().text(px + 16, std::max(y + 6, by + bh - 40), deathQuote(r, &qc), ts(F12, qc, LEFT, pw - 32));
      }
    } else if (s.tab == 1) {  // a card, big, with its text
      Card* c = s.cards[sel].get();
      const float cs = 0.95f;
      drawCard(c, kTop / 2 - 60 * cs, by + 6, cs, false, true);
    } else if (s.tab == 2) {  // a relic
      drawRelicDetail(s.relics[sel].get(), by + 50);
    } else {  // a badge
      const history::BadgeEntry& b = r.badges[sel];
      drawBadge(b, kTop / 2 - 32, by + 14, 64);
      TextStyle nt = ts(F16, col::gold, CENTER, 0, 1.2f);
      R().text(kTop / 2, by + 86, badgeText(b, true), nt);
      static const char* rar[] = {"", "铜", "银", "金"};
      R().text(kTop / 2, by + 110, std::string(rar[std::clamp(b.rarity, 0, 3)]) + "徽章", ts(F12, col::gray, CENTER));
      R().text(kTop / 2, by + 128, badgeText(b, false), ts(F12, col::white, CENTER, pw - 40));
    }
    return;
  }

  // Bottom: the tab strip, the tab's items, the action bar.
  const std::string labels[kTabs] = {"路线", "牌组 " + num((int)s.cards.size()), "遗物 " + num((int)s.relics.size()),
                                     "徽章 " + num((int)r.badges.size())};
  const float tw = (kBot - 16 - 3 * 4) / kTabs;
  for (int t = 0; t < kTabs; ++t) {
    const float x = 8 + t * (tw + 4);
    const bool on = t == s.tab;
    gfx::rect(x, 4, tw, 26, on ? 0x3B6272F0 : 0x1A2A33E0);
    gfx::rect(x, 28, tw, 2, on ? col::gold : 0x4F8790FF);
    R().text(x + tw / 2, 9, labels[t], ts(F12, on ? col::gold : col::white, CENTER));
    hits_.push_back({x, 4, tw, 26, kTab0 + t});
  }
  gfx::pushClip(0, kContentY, kBot, kContentH);
  if (s.tab == 0) {
    // One block per act: its name, then the room icons in order (wrapping).
    float y = kContentY + 2;
    const int perRow = (int)((kBot - 16) / kPathPitch);
    for (int a = 0; a < (int)r.path.size(); ++a) {
      std::string an = a < (int)r.acts.size() ? r.acts[a] : "";
      std::string ak = "acts." + upperSnake(an) + ".title";
      R().text(10, y, "第" + num(a + 1) + "幕" + (R().hasLoc(ak) ? " · " + L(ak) : std::string()), ts(F12, col::gray));
      y += 15;
      for (int i = 0; i < (int)s.floors.size(); ++i) {
        const Floor& f = s.floors[i];
        if (f.act != a) continue;
        const float x = 8 + (f.index % perRow) * kPathPitch, yy = y + (f.index / perRow) * (kPathIcon + 3);
        Sprite si = R().sprite(pointIcon(r.path[a][f.index]));
        if (i == sel) gfx::rect(x - 1, yy - 1, kPathIcon + 2, kPathIcon + 2, 0xFFD87050);
        if (si) spr(si, x, yy, kPathIcon, kPathIcon);
        if (i == sel) outline(x - 1, yy - 1, kPathIcon + 2, kPathIcon + 2, col::gold, 1);
        hits_.push_back({x - 1, yy - 1, kPathPitch, kPathIcon + 3, kItem0 + i});
      }
      const int np = (int)r.path[a].size();
      y += std::max(1, (np + perRow - 1) / perRow) * (kPathIcon + 3) + 4;
    }
  } else if (s.tab == 1 || s.tab == 2) {
    const bool cards = s.tab == 1;
    const int cols = cards ? kCardCols : kRelicCols;
    const float pitchY = cards ? kCardPitchY : kRelicCell, cellW = cards ? kCardW : kRelicCell;
    const int visRows = std::max(1, (int)(kContentH / pitchY));
    int& topRow = s.top[s.tab];
    const int row = sel / cols;
    if (row < topRow) topRow = row;
    if (row >= topRow + visRows) topRow = row - visRows + 1;
    const float gap = (kBot - cols * cellW) / (cols + 1);
    for (int i = topRow * cols; i < count; ++i) {
      const int rr = i / cols - topRow;
      const float x = gap + (i % cols) * (cellW + gap), y = kContentY + 3 + rr * pitchY;
      if (y > kContentY + kContentH) break;
      if (cards) {
        drawCard(s.cards[i].get(), x, y, kCardS, false, false, i == sel);
        hits_.push_back({x, y, kCardW, kCardH, kItem0 + i});
      } else {
        if (i == sel) gfx::rect(x, y, kRelicCell - 2, kRelicCell - 2, 0xFFD87040);
        drawRelicIcon(s.relics[i].get(), x + 2, y + 2, kRelicCell - 6);
        if (i == sel) outline(x, y, kRelicCell - 2, kRelicCell - 2, col::gold, 1);
        hits_.push_back({x, y, kRelicCell - 2, kRelicCell - 2, kItem0 + i});
      }
    }
    const int totalRows = (count + cols - 1) / cols;
    if (totalRows > visRows) {  // scroll thumb
      const float th = kContentH * visRows / totalRows, ty = kContentY + (kContentH - th) * topRow / std::max(1, totalRows - visRows);
      gfx::rect(kBot - 4, ty, 3, th, 0xFFFFFF60);
    }
  } else {
    const int visRows = (int)(kContentH / kBadgeRowH);
    int& topRow = s.top[3];
    if (sel < topRow) topRow = sel;
    if (sel >= topRow + visRows) topRow = sel - visRows + 1;
    for (int i = topRow; i < std::min(count, topRow + visRows); ++i) {
      const float y = kContentY + 2 + (i - topRow) * kBadgeRowH;
      gfx::rect(8, y, kBot - 16, kBadgeRowH - 3, i == sel ? 0x3B6272F0 : 0x1A2A33D8);
      drawBadge(r.badges[i], 12, y + 2, kBadgeRowH - 7);
      R().text(14 + kBadgeRowH, y + (kBadgeRowH - 3 - R().lineHeight(F16)) / 2, badgeText(r.badges[i], true),
               ts(F16, i == sel ? col::gold : col::white));
      if (i == sel) outline(8, y, kBot - 16, kBadgeRowH - 3, col::gold, 1);
      hits_.push_back({8, y, kBot - 16, kBadgeRowH - 3, kItem0 + i});
    }
    if (count == 0) R().text(kBot / 2, kContentY + 60, r.abandoned ? "放弃的游戏没有徽章" : "没有徽章", ts(F16, col::gray, CENTER));
  }
  gfx::popClip();
  widgets::panel("ui/btn_back", 8, kBarY, 96, kBarH);
  R().text(8 + 48, kBarY + (kBarH - R().lineHeight(F16)) / 2, "返回", ts(F16, col::white, CENTER));
  hits_.push_back({8, kBarY, 96, kBarH, kBackId});
  R().text(kBot - 10, kBarY + 10, "L / R 切换", ts(F12, col::gray, RIGHT));
}

// ================================================================ input

bool App::updateStats(const gfx::Input& in) {
  State& s = S();
  if (s.page == kClosed) return false;
  int id = in.touchDown ? hitAt(in.tx, in.ty) : ID_NONE;
  const uint32_t d = in.down;

  if (s.page == kStatsPage) {
    const int rows = 1 + (int)db::characterIds().size();
    if (d & gfx::BTN_UP) s.statSel = (s.statSel + rows - 1) % rows;
    if (d & gfx::BTN_DOWN) s.statSel = (s.statSel + 1) % rows;
    if (id >= kStatRow0 && id < kStatRow0 + rows) s.statSel = id - kStatRow0;
    if (id == kBackId || (d & gfx::BTN_B)) { s.page = kClosed; return true; }
    if (id == kToHistory || (d & gfx::BTN_X)) {
      s.page = kListPage;
      s.fromStats = true;
      s.runSel = s.runTop = 0;
    }
    return true;
  }

  if (s.page == kListPage) {
    const int n = (int)s.runs.size();
    if (id == kBackId || (d & gfx::BTN_B)) {
      s.page = s.fromStats ? kStatsPage : kClosed;
      return true;
    }
    if (n == 0) return true;
    if (d & gfx::BTN_UP) s.runSel = std::max(0, s.runSel - 1);
    if (d & gfx::BTN_DOWN) s.runSel = std::min(n - 1, s.runSel + 1);
    if ((d & gfx::BTN_L) || (d & gfx::BTN_LEFT) || id == kPageUp) {
      s.runSel = std::max(0, s.runSel - kListRows);
      s.runTop = std::max(0, s.runTop - kListRows);
    }
    if ((d & gfx::BTN_R) || (d & gfx::BTN_RIGHT) || id == kPageDn) {
      s.runSel = std::min(n - 1, s.runSel + kListRows);
      s.runTop = std::min(std::max(0, n - kListRows), s.runTop + kListRows);
    }
    if (id >= kRunRow0 && id < kRunRow0 + n) {
      if (s.runSel == id - kRunRow0) openRun(s);  // a second tap opens it
      else s.runSel = id - kRunRow0;
      return true;
    }
    if (id == kOpenRun || (d & gfx::BTN_A)) openRun(s);
    return true;
  }

  // A run's details.
  if (id == kBackId || (d & gfx::BTN_B)) {
    s.page = kListPage;
    s.cards.clear();
    s.relics.clear();
    return true;
  }
  if (s.runs.empty()) return true;
  const history::RunRecord& r = s.runs[std::clamp(s.runSel, 0, (int)s.runs.size() - 1)];
  if (d & gfx::BTN_L) s.tab = (s.tab + kTabs - 1) % kTabs;
  if (d & gfx::BTN_R) s.tab = (s.tab + 1) % kTabs;
  if (id >= kTab0 && id < kTab0 + kTabs) { s.tab = id - kTab0; return true; }
  const int count = tabCount(s, r, s.tab);
  if (count == 0) return true;
  int& sel = s.item[s.tab];
  if (id >= kItem0 && id < kItem0 + count) { sel = id - kItem0; return true; }
  int dx = (d & gfx::BTN_RIGHT) ? 1 : (d & gfx::BTN_LEFT) ? -1 : 0;
  int dy = (d & gfx::BTN_DOWN) ? 1 : (d & gfx::BTN_UP) ? -1 : 0;
  if (!dx && !dy) return true;
  if (s.tab == 0) {
    if (dx) sel = std::clamp(sel + dx, 0, count - 1);
    if (dy) {  // the same position in the previous / next act
      const Floor f = s.floors[sel];
      const int act = std::clamp(f.act + dy, 0, (int)r.path.size() - 1);
      if (act != f.act && !r.path[act].empty()) {
        const int idx = std::min(f.index, (int)r.path[act].size() - 1);
        for (int i = 0; i < count; ++i)
          if (s.floors[i].act == act && s.floors[i].index == idx) sel = i;
      }
    }
  } else if (s.tab == 3) {
    sel = std::clamp(sel + dy + dx, 0, count - 1);
  } else {
    const int cols = s.tab == 1 ? kCardCols : kRelicCols;
    sel = std::clamp(sel + dx + dy * cols, 0, count - 1);
  }
  return true;
}

}  // namespace ui
