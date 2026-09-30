// M10: the bestiary (compendium 怪物图鉴), NBestiary / NBestiaryEntry / NBestiaryLayoutDefault.
//
// C# (Nodes/Screens/Bestiary): one list with a divider per act (ModelDb.Acts order, each act's
// AllEncounters -> AllPossibleMonsters, first encounter wins, ShouldShowInCompendium: not the
// Decimillipede segments; the Hive also lists the DecimillipedeElite encounter itself), then an
// "events" divider with ModelDb.EventEncounters' monsters. Each group is sorted by room type
// (normal < elite < boss), bosses by their encounter's title, then by the entry's title. An entry
// is cream / purple (elite) / red (boss); an undiscovered one reads 【未知】, greyed out. The
// focused monster stands in the layout (idle animation) under its name, with its move list
// (MonsterModel.GenerateBestiaryMoveList: every MoveState in the state machine's order, minus
// ShouldShowMoveInBestiary's exclusions, named monsters.<KEY>.moves.<ID without _MOVE>.title,
// unnamed ones ending in 2/3/4 dropped; then revive / hurt / die when the skeleton has them).
// Pressing a move plays it.
// Port adaptations: "discovered" is progress.seenMonsters (met in a fight; the C# needs a kill),
// every act is shown (everything is unlocked), the stats mode / character filters (EnemyStats are
// not kept) and the few custom GenerateBestiaryMoveList overrides are left out, the Kaiser Crab and
// Decimillipede special layouts become the monster's own skeleton (the three segments side by
// side), titles sort by code point (the C# uses the zh-CN culture's collation), and the top screen
// adds what the port can show cheaply: the HP range (and at ascension 8, ToughEnemies) and each
// move's intents with their damage (the ascension 9, DeadlyEnemies, value in brackets). A move
// plays the animation named after it, else one picked from its first intent (as the combat's
// CreatureAnimator triggers do). Summon-only monsters come from db::monster.
// Layout (RGDSplus): the list and the controls on the bottom screen, the focused monster on top.
// Keys: D-pad moves through the list (two columns) and the bar, L R jump between groups, X plays
// the selected move, Y selects the next one, B back. Touch: tap an entry, drag to scroll, tap the
// bar's buttons (the move name plays it).
// Memory: only the focused monster's Spine is loaded; it is freed on switching and on leaving
// (unless the combat underneath already had it loaded).
#include <cstdlib>
#include <map>
#include <set>

#include "../../core/progress.h"
#include "../ui_common.h"

namespace ui {

namespace {
constexpr int kBtnBack = 4801, kBtnPrev = 4802, kBtnPlay = 4803, kBtnNext = 4804;
constexpr float kListY0 = 2, kListY1 = 204, kBarY = 208, kBarH = 28;
constexpr float kHeadH = 20, kRowH = 17, kColX[2] = {6, 162}, kColW = 150;
constexpr float kBarX[4] = {4, 66, 100, 288}, kBarW[4] = {58, 30, 184, 28};
constexpr int kBarId[4] = {kBtnBack, kBtnPrev, kBtnPlay, kBtnNext};
constexpr float kTapSlop = 5;
constexpr int kToughHp = 8, kDeadlyDmg = 9;  // AscensionLevel ToughEnemies / DeadlyEnemies

struct Move {
  std::string name, stateId, anim;  // anim: a FromAnim entry (revive / hurt / die)
  std::vector<Intent> intents[2];   // at ascension 0 and 10
};

struct MonInfo {
  std::string locKey;
  int hp[2][2] = {{0, 0}, {0, 0}};  // [asc 0 / 10][min / max]
  std::vector<Move> moves;
  bool built[2] = {false, false};
};

struct Entry {
  std::string monsterId, encounterId;  // monsterId empty: an encounter entry (Decimillipede)
  RoomType room = RoomType::Monster;
  int group = 0;
  std::string title, encTitle;
  std::vector<std::string> spine;  // skeleton keys drawn side by side
  std::string infoId;              // MonInfo for HP / moves
  bool seen = false;
  int row = 0, col = 0;
};

struct Row { bool header; int group; int e[2]; float y, h; };

struct Part {
  std::string key;
  const spine::SkeletonData* data = nullptr;
  std::unique_ptr<spine::Skeleton> skel;
  std::unique_ptr<spine::AnimationState> anim;
  bool owned = false;  // loaded by the bestiary: freed when it goes
};

struct State {
  bool open = false, built = false;
  std::vector<std::string> groups;  // divider titles
  std::vector<Entry> entries;
  std::vector<Row> rows;
  std::map<std::string, MonInfo> infos;
  int sel = 0, move = 0, zone = 0, bar = 2;  // zone 0 list, 1 bar
  float scroll = 0, contentH = 0;
  std::vector<Part> parts;
  std::vector<Move> moves;  // the focused entry's list incl. the animation entries
  int shownFor = -1;        // entry the parts / moves belong to
  double lastT = -1;
  bool touchDown = false, dragged = false;
  float touchY0 = 0, touchLastY = 0;
  int touchEntry = -1;
};
State& S() {
  static State s;
  return s;
}

// VantomBoss -> VANTOM_BOSS (ModelId.Entry).
std::string snakeKey(const std::string& id) {
  std::string key;
  for (size_t i = 0; i < id.size(); ++i) {
    char c = id[i];
    if (i > 0 && std::isupper((unsigned char)c) && !std::isupper((unsigned char)id[i - 1])) key += '_';
    key += (char)std::toupper((unsigned char)c);
  }
  return key;
}
std::string lowerStr(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}
std::string locOr(const std::string& key, const std::string& fallback) { return R().hasLoc(key) ? L(key) : fallback; }
std::string brackets(std::string s) {  // [lb] / [rb] are BBCode escapes; the port's text treats [..] as tags
  for (auto [from, to] : {std::pair<const char*, const char*>{"[lb]", "【"}, {"[rb]", "】"}})
    for (size_t p; (p = s.find(from)) != std::string::npos;) s.replace(p, 4, to);
  return s;
}

int roomRank(RoomType t) { return t == RoomType::Boss ? 3 : t == RoomType::Elite ? 2 : 1; }
uint32_t roomColor(RoomType t) { return t == RoomType::Boss ? col::red : t == RoomType::Elite ? col::purple : col::white; }

// ShouldShowMoveInBestiary overrides (all of them exclusion lists).
bool hiddenMove(const std::string& monster, const std::string& state) {
  static const std::map<std::string, std::vector<std::string>> kHidden = {
      {"AxeRubyRaider", {"SWING_1", "SWING_2"}}, {"BruteRubyRaider", {"ROAR_MOVE"}},
      {"CeremonialBeast", {"STUN_MOVE"}}, {"Fabricator", {"FABRICATING_STRIKE_MOVE"}},
      {"GlobeHead", {"GALVANIC_BURST"}}, {"FuzzyWurmCrawler", {"FIRST_ACID_GOOP"}},
      {"Inklet", {"PIERCING_GAZE_MOVE"}}, {"Fogmog", {"SWIPE_RANDOM_MOVE", "HEADBUTT_MOVE"}},
      {"MagiKnight", {"PREP_MOVE"}}, {"LagavulinMatriarch", {"SLEEP_MOVE"}},
      {"Queen", {"PUPPET_STRINGS_MOVE", "BURN_BRIGHT_FOR_ME_MOVE", "ENRAGE_MOVE", "EXECUTION_MOVE"}},
      {"Nibbit", {"SLICE_MOVE"}}, {"Ovicopter", {"TENDERIZER_MOVE"}}, {"SlimedBerserker", {"SMOTHER_MOVE"}},
      {"SoulNexus", {"MAELSTROM_MOVE"}}, {"ShrinkerBeetle", {"STOMP_MOVE"}}, {"TerrorEel", {"STUN_MOVE"}},
      {"SludgeSpinner", {"RAGE_MOVE"}}, {"TheObscura", {"PIERCING_GAZE_MOVE"}},
      {"TestSubject", {"BITE_MOVE", "SKULL_BASH_MOVE", "PHASE3_LACERATE_MOVE"}},
      {"TrackerRubyRaider", {"TRACK_MOVE"}}, {"Wriggler", {"NASTY_BITE_MOVE", "SPAWNED_MOVE"}},
      {"WaterfallGiant", {"SIPHON_MOVE"}}};
  auto it = kHidden.find(monster);
  return it != kHidden.end() && std::find(it->second.begin(), it->second.end(), state) != it->second.end();
}

// Summoned monsters in an encounter's AllPossibleMonsters that its generate() never returns.
const std::vector<std::string>& summons(const std::string& encounter) {
  static const std::map<std::string, std::vector<std::string>> kSummons = {
      {"FogmogNormal", {"EyeWithTeeth"}}, {"GremlinMercNormal", {"FatGremlin", "SneakyGremlin"}},
      {"LivingFogNormal", {"GasBomb"}}, {"TheObscuraNormal", {"Parafright"}}, {"OvicopterNormal", {"ToughEgg"}}};
  static const std::vector<std::string> none;
  auto it = kSummons.find(encounter);
  return it == kSummons.end() ? none : it->second;
}

// ModelDb.EventEncounters.
const char* const kEventEncounters[] = {"FakeMerchantEventEncounter", "MysteriousKnightEventEncounter",
                                        "BattlewornDummyEventV1Encounter", "BattlewornDummyEventV2Encounter",
                                        "BattlewornDummyEventV3Encounter"};

// Every monster an encounter can hold (AllPossibleMonsters): its generate() over a few seeds, then
// the summons. Monsters are returned built (not yet in a combat).
std::vector<std::unique_ptr<Monster>> possibleMonsters(const std::string& encId) {
  std::vector<std::unique_ptr<Monster>> out;
  std::set<std::string> ids;
  if (const Encounter* enc = db::encounter(encId)) {
    for (int seed = 1; seed <= 16; ++seed) {
      Rng rng((uint64_t)seed, "Bestiary");
      for (auto& m : enc->generate(rng))
        if (ids.insert(m->id).second) out.push_back(std::move(m));
    }
  }
  for (auto& id : summons(encId))
    if (auto m = db::monster(id))
      if (ids.insert(m->id).second) out.push_back(std::move(m));
  return out;
}

// HP and moves at ascension 0 (level 0) and 10 (level 1), from a throwaway combat (as the
// ascension dump does; nothing touches the profile).
void fillInfo(MonInfo& info, std::unique_ptr<Monster> m, int level) {
  if (info.built[level]) return;
  info.built[level] = true;
  Run run;
  run.ascension = level ? 10 : 0;
  run.player = std::make_unique<Creature>();
  run.player->isPlayer = true;
  run.player->side = Side::Player;
  run.player->hp = run.player->maxHp = 80;
  Combat c;
  c.run = &run;
  c.player = run.player.get();
  m->combat = &c;
  info.locKey = m->locKey;
  info.hp[level][0] = m->minHp();
  info.hp[level][1] = m->maxHp();
  const std::string monsterId = m->id;
  Creature* cr = c.createEnemy(std::move(m));
  auto& mc = cr->monster->machine;
  for (auto& id : mc.order) {
    auto it = mc.states.find(id);
    if (it == mc.states.end() || !it->second->isMove() || hiddenMove(monsterId, id)) continue;
    auto* ms = static_cast<MoveState*>(it->second.get());
    if (level == 0) {
      std::string text = id;
      if (text.size() > 5 && text.compare(text.size() - 5, 5, "_MOVE") == 0) text.resize(text.size() - 5);
      std::string key = "monsters." + info.locKey + ".moves." + text + ".title";
      Move mv;
      mv.stateId = id;
      if (R().hasLoc(key)) mv.name = L(key);
      else if (!text.empty() && (text.back() == '2' || text.back() == '3' || text.back() == '4')) continue;
      else mv.name = id;
      mv.intents[0] = ms->intents;
      info.moves.push_back(std::move(mv));
    } else {
      for (auto& mv : info.moves)
        if (mv.stateId == id) mv.intents[1] = ms->intents;
    }
  }
}

void build() {
  State& st = S();
  if (st.built) return;
  st.built = true;
  db::init();
  std::set<std::string> listed;  // per group
  auto addGroup = [&](const std::string& title, const std::vector<std::string>& encounters, bool hive) {
    int g = (int)st.groups.size();
    st.groups.push_back(title);
    listed.clear();
    std::vector<Entry> list;
    for (auto& encId : encounters) {
      const Encounter* enc = db::encounter(encId);
      if (!enc) continue;
      for (auto& m : possibleMonsters(encId)) {
        std::string id = m->id;
        if (!listed.insert(id).second || id.rfind("DecimillipedeSegment", 0) == 0) continue;  // ShouldShowInCompendium
        Entry e;
        e.monsterId = e.infoId = id;
        e.encounterId = encId;
        e.room = enc->room;
        e.group = g;
        e.spine = {m->locKey};
        e.title = locOr("monsters." + m->locKey + ".name", id);
        e.encTitle = locOr("encounters." + snakeKey(encId) + ".title", encId);
        if (!st.infos.count(id)) fillInfo(st.infos[id], std::move(m), 0);
        list.push_back(std::move(e));
      }
    }
    if (hive && db::encounter("DecimillipedeElite")) {  // BestiaryEntry.FromEncounter(DecimillipedeElite)
      Entry e;
      e.encounterId = "DecimillipedeElite";
      e.room = RoomType::Elite;
      e.group = g;
      e.title = e.encTitle = locOr("encounters.DECIMILLIPEDE_ELITE.title", "Decimillipede");
      auto segs = possibleMonsters("DecimillipedeElite");
      for (auto& m : segs) e.spine.push_back(m->locKey);
      if (!segs.empty()) {
        e.infoId = segs[0]->id;
        if (!st.infos.count(e.infoId)) fillInfo(st.infos[e.infoId], std::move(segs[0]), 0);
      }
      list.push_back(std::move(e));
    }
    std::sort(list.begin(), list.end(), [](const Entry& a, const Entry& b) {
      if (a.room != b.room) return roomRank(a.room) < roomRank(b.room);
      if (a.room == RoomType::Boss && a.encTitle != b.encTitle) return a.encTitle < b.encTitle;
      if (a.title != b.title) return a.title < b.title;
      return a.monsterId < b.monsterId;
    });
    for (auto& e : list) st.entries.push_back(std::move(e));
  };
  for (auto& act : db::acts()) {
    std::vector<std::string> encs;
    for (auto* v : {&act.weak, &act.normal, &act.elites, &act.bosses})
      for (auto& id : *v)
        if (std::find(encs.begin(), encs.end(), id) == encs.end()) encs.push_back(id);
    std::sort(encs.begin(), encs.end());  // GenerateAllEncounters lists them by class name
    addGroup(locOr("acts." + snakeKey(act.name) + ".title", act.name), encs, std::string(act.name) == "Hive");
  }
  addGroup(L("bestiary.EVENTS.title"), std::vector<std::string>(std::begin(kEventEncounters), std::end(kEventEncounters)),
           false);
  // Ascension 10 values for every listed monster.
  for (auto& e : st.entries) {
    MonInfo& info = st.infos[e.infoId];
    if (info.built[1] || e.infoId.empty()) continue;
    std::unique_ptr<Monster> m = db::monster(e.infoId);
    if (!m)
      for (auto& x : possibleMonsters(e.encounterId))
        if (x->id == e.infoId) { m = std::move(x); break; }
    if (m) fillInfo(info, std::move(m), 1);
  }
  // Rows: a divider per group, then its entries two to a row.
  float y = 0;
  int last = -1;
  for (int i = 0; i < (int)st.entries.size(); ++i) {
    Entry& e = st.entries[i];
    if (e.group != last) {
      for (int g = last + 1; g <= e.group; ++g) {
        st.rows.push_back({true, g, {-1, -1}, y, kHeadH});
        y += kHeadH;
      }
      last = e.group;
    }
    Row& r = st.rows.back();
    if (r.header || r.e[1] >= 0) {
      st.rows.push_back({false, e.group, {i, -1}, y, kRowH});
      y += kRowH;
    } else {
      r.e[1] = i;
    }
    e.row = (int)st.rows.size() - 1;
    e.col = st.rows.back().e[1] == i ? 1 : 0;
  }
  st.contentH = y + 4;
}

float maxScroll() { return std::max(0.f, S().contentH - (kListY1 - kListY0)); }

void scrollToSel() {
  State& st = S();
  if (st.entries.empty()) return;
  const Row& r = st.rows[st.entries[st.sel].row];
  float top = r.y, h = kListY1 - kListY0;
  // Show the group's divider too when the entry is the group's first row.
  if (st.entries[st.sel].row > 0 && st.rows[st.entries[st.sel].row - 1].header) top -= kHeadH;
  if (top < st.scroll) st.scroll = top;
  if (r.y + r.h > st.scroll + h) st.scroll = r.y + r.h - h;
  st.scroll = std::clamp(st.scroll, 0.f, maxScroll());
}

int entryAt(float tx, float ty) {
  State& st = S();
  if (ty < kListY0 || ty >= kListY1) return -1;
  float ly = ty - kListY0 + st.scroll;
  for (auto& r : st.rows) {
    if (ly < r.y || ly >= r.y + r.h) continue;
    if (r.header) return -1;
    for (int c = 0; c < 2; ++c)
      if (r.e[c] >= 0 && tx >= kColX[c] && tx < kColX[c] + kColW) return r.e[c];
    return -1;
  }
  return -1;
}

void outline(float x, float y, float w, float h, uint32_t c = col::gold, float t = 2) {
  gfx::rect(x - t, y - t, w + 2 * t, t, c);
  gfx::rect(x - t, y + h, w + 2 * t, t, c);
  gfx::rect(x - t, y, t, h, c);
  gfx::rect(x + w, y, t, h, c);
}

// ---- the focused monster's Spine: one entry's skeletons at a time.
void freeParts() {
  State& st = S();
  for (auto& p : st.parts) {
    p.anim.reset();
    p.skel.reset();
    if (p.owned) R().releaseSkeleton(p.key);
  }
  st.parts.clear();
  st.moves.clear();
  st.shownFor = -1;
}

void showEntry(int i) {
  State& st = S();
  if (st.shownFor == i) return;
  freeParts();
  st.shownFor = i;
  st.move = 0;
  if (i < 0 || i >= (int)st.entries.size()) return;
  const Entry& e = st.entries[i];
  auto it = st.infos.find(e.infoId);
  if (e.monsterId.empty()) {
    // NBestiaryLayoutDecimillipede.Setup: writhe (the segments' attack), reattach (revive), dead (die).
    auto name = [](const char* m) { return locOr(std::string("monsters.DECIMILLIPEDE_SEGMENT.moves.") + m + ".title", m); };
    Move writhe;
    writhe.name = name("WRITHE");
    writhe.stateId = "WRITHE_MOVE";
    if (it != st.infos.end())
      for (auto& mv : it->second.moves)
        if (mv.stateId == "WRITHE_MOVE") { writhe.intents[0] = mv.intents[0]; writhe.intents[1] = mv.intents[1]; }
    st.moves.push_back(writhe);
    Move reattach, dead;
    reattach.name = name("REATTACH");
    reattach.anim = "revive";
    dead.name = name("DEAD");
    dead.anim = "die";
    st.moves.push_back(reattach);
    st.moves.push_back(dead);
  } else if (it != st.infos.end()) {
    st.moves = it->second.moves;
  }
  if (!e.seen) return;  // an undiscovered entry shows nothing (the C# frees the layout)
  for (auto& key : e.spine) {
    Part p;
    p.key = key;
    p.owned = !R().skeletonLoaded(key);
    p.data = R().skeleton(key);
    if (p.data) {
      p.skel = std::make_unique<spine::Skeleton>(p.data);
      p.anim = std::make_unique<spine::AnimationState>(p.data);
      p.anim->play("idle_loop", true);
    }
    st.parts.push_back(std::move(p));
  }
  // GenerateBestiaryMoveList's animation entries.
  const spine::SkeletonData* d = st.parts.empty() || e.monsterId.empty() ? nullptr : st.parts[0].data;
  for (const char* a : {"revive", "hurt", "die"}) {
    if (!d || !d->animation(a)) continue;
    Move mv;
    mv.anim = a;
    mv.name = L(std::string("bestiary.ACTION_NAME.") + a);
    st.moves.push_back(std::move(mv));
  }
}

// The animation a move plays: its own name, else one for its first intent (CreatureAnimator).
std::string moveAnim(const spine::SkeletonData* d, const Move& mv) {
  if (!d) return "";
  auto has = [&](const std::string& a) { return d->animation(a) != nullptr; };
  if (!mv.anim.empty()) return has(mv.anim) ? mv.anim : "";
  std::string low = mv.stateId;
  if (low.size() > 5 && low.compare(low.size() - 5, 5, "_MOVE") == 0) low.resize(low.size() - 5);
  low = lowerStr(low);
  if (has(low)) return low;
  Intent::Kind k = mv.intents[0].empty() ? Intent::Unknown : mv.intents[0][0].kind;
  std::vector<const char*> names;
  switch (k) {
    case Intent::Attack: names = {"attack", "attack_heavy", "attack_double"}; break;
    case Intent::Debuff:
    case Intent::DebuffStrong: names = {"debuff", "cast", "buff", "shrill"}; break;
    case Intent::Summon: names = {"summon", "cast"}; break;
    case Intent::Defend: names = {"defend", "block", "cast", "buff"}; break;
    case Intent::Sleep: names = {"sleep", "sleep_loop"}; break;
    default: names = {"cast", "buff", "rally", "shrill", "summon"}; break;
  }
  for (const char* n : names) if (has(n)) return n;
  for (auto& a : d->animations)
    if (a.name.rfind(names[0], 0) == 0) return a.name;
  for (auto& a : d->animations)
    if (a.name.rfind("attack", 0) == 0 || a.name == "cast") return a.name;
  return "";
}

void playMove() {
  State& st = S();
  if (st.move < 0 || st.move >= (int)st.moves.size()) return;
  for (auto& p : st.parts) {
    if (!p.anim) continue;
    std::string a = moveAnim(p.data, st.moves[st.move]);
    if (!a.empty()) p.anim->play(a, false, "idle_loop");
  }
}

Sprite intentSprite(const Intent& in, int dmg) {
  switch (in.kind) {
    case Intent::Attack: {
      int total = dmg * std::max(1, in.hits);
      int tier = total < 5 ? 1 : total < 10 ? 2 : total < 20 ? 3 : total < 40 ? 4 : 5;
      return R().sprite("intent/attack_" + num(tier));
    }
    case Intent::Buff: return R().sprite("intent/buff");
    case Intent::Defend: return R().sprite("intent/defend");
    case Intent::Debuff:
    case Intent::DebuffStrong: return R().sprite("intent/debuff");
    case Intent::Status: return R().sprite("intent/status");
    case Intent::Stun: return R().sprite("intent/stun");
    case Intent::Summon: return R().sprite("intent/summon");
    case Intent::Heal: return R().sprite("intent/heal");
    case Intent::Escape: return R().sprite("intent/escape");
    case Intent::Sleep: return R().sprite("intent/sleep");
    default: return R().sprite("intent/unknown");
  }
}

std::string hpRange(int lo, int hi) { return lo == hi ? num(lo) : num(lo) + "–" + num(hi); }
}  // namespace

// NBestiary.OnSubmenuOpened: the list rebuilt from the progress, the first discovered entry focused.
void App::openBestiary() {
  build();
  State& st = S();
  st.open = true;
  const auto& seen = progress::state().seenMonsters;
  const bool all = getenv("STS_SEEN_ALL") != nullptr;  // debug: every monster as seen
  for (auto& e : st.entries) {
    if (e.monsterId.empty()) {  // the Decimillipede: any of its segments met
      e.seen = all;
      for (auto& m : {"DecimillipedeSegmentFront", "DecimillipedeSegmentMiddle", "DecimillipedeSegmentBack"})
        if (seen.count(m)) e.seen = true;
    } else {
      e.seen = all || seen.count(e.monsterId) > 0;
    }
  }
  st.sel = 0;
  for (int i = 0; i < (int)st.entries.size(); ++i)
    if (st.entries[i].seen) { st.sel = i; break; }
  if (const char* f = getenv("STS_BESTIARY_FOCUS"))  // debug: focus a monster (class name) on open
    for (int i = 0; i < (int)st.entries.size(); ++i)
      if (st.entries[i].monsterId == f || st.entries[i].encounterId == f) st.sel = i;
  st.zone = 0;
  st.bar = 2;
  st.scroll = 0;
  st.touchDown = false;
  st.lastT = -1;
  scrollToSel();
  showEntry(st.sel);
}

bool App::drawBestiary(bool top) {
  State& st = S();
  if (!st.open) return false;
  const bool title = run_->screen == Screen::Title;
  const Entry* fe = st.sel >= 0 && st.sel < (int)st.entries.size() ? &st.entries[st.sel] : nullptr;
  if (top) {
    if (title) drawMenuBg(true, 0.7f);
    else drawSceneBg(true, 0.8f);
    int seenN = 0;
    for (auto& e : st.entries) seenN += e.seen;
    R().text(8, 4, L("main_menu_ui.COMPENDIUM_BESTIARY.title"), ts(F16, col::gold));
    R().text(kTop - 8, 6, (fe ? st.groups[fe->group] + "  " : std::string()) + num(seenN) + "/" + num((int)st.entries.size()),
             ts(F12, col::white, RIGHT));
    if (!fe) return true;
    // The layout: the monster standing on the left, fitted to the box.
    const float boxX = 6, boxW = 200, boxTop = 30, feetY = 222;
    gfx::gradient(0, feetY - 6, 212, 14, 0x00000000, 0x00000000, 0x00000070, 0x00000070);
    if (!fe->seen) {
      R().text(boxX + boxW / 2, 96, "?", ts(F16, col::gray, CENTER, 0, 3.f));
    } else if (!st.parts.empty()) {
      const int n = (int)st.parts.size();
      const float cellW = boxW / n;
      auto dims = [](const Part& p, float& w, float& h, float& ax, float& ay) {  // the creature sprite's box
        Sprite s = R().sprite("creature/" + p.key);
        w = s ? s.w : 120, h = s ? s.h : 150, ax = s ? (float)s.ax : w / 2, ay = s ? (float)s.ay : h;
      };
      float sc = 1.5f;  // one scale for every part, fitted to its cell
      for (auto& p : st.parts) {
        float w, h, ax, ay;
        dims(p, w, h, ax, ay);
        sc = std::min({sc, (cellW - 4) / w, (feetY - boxTop) / h});
      }
      for (int i = 0; i < n; ++i) {
        Part& p = st.parts[i];
        if (!p.skel) continue;
        float w, h, ax, ay;
        dims(p, w, h, ax, ay);
        float cx = boxX + cellW * (i + 0.5f);
        float fx = cx + (ax - w / 2) * sc, fy = feetY - (h - ay) * sc;
        p.anim->apply(*p.skel);
        p.skel->updateWorldTransform();
        static std::vector<spine::Batch> batches;
        batches.clear();
        const float tint[4] = {1, 1, 1, 1};
        p.skel->render(batches, fx, fy, sc, false, tint);
        for (auto& b : batches)
          gfx::triangles(R().texture(p.data->pages[b.page]), reinterpret_cast<const gfx::Vert*>(b.vertices.data()),
                         (int)b.vertices.size(), b.indices.data(), (int)b.indices.size(), b.blend == 1);
      }
    }
    // The right column: name, room / encounter, HP, the moves.
    const float rx = 214, rw = kTop - rx - 6;
    float y = 28;
    R().text(rx, y, fe->seen ? fe->title : L("bestiary.LOCKED.monsterTitle"),
             ts(F16, fe->seen ? roomColor(fe->room) : col::gray, LEFT, rw));
    y += 21;
    std::string kind = fe->room == RoomType::Boss ? tr("首领", "Boss") : fe->room == RoomType::Elite ? tr("精英", "Elite") : tr("普通", "Normal");
    std::string meta = kind + " · " + st.groups[fe->group];
    if (fe->seen && fe->room == RoomType::Boss) meta += " · " + fe->encTitle;
    R().text(rx, y, meta, ts(F12, col::gray, LEFT, rw));
    y += 17;
    if (!fe->seen) {
      R().text(rx, y + 4, L("bestiary.DESCRIPTION.placeholder"), ts(F12, col::white, LEFT, rw));
    } else {
      auto it = st.infos.find(fe->infoId);
      if (it != st.infos.end()) {
        const MonInfo& info = it->second;
        std::string hp = "[icon:hp] " + hpRange(info.hp[0][0], info.hp[0][1]);
        if (info.built[1] && (info.hp[1][0] != info.hp[0][0] || info.hp[1][1] != info.hp[0][1]))
          hp += tr("   [gold]进阶", "   [gold]A") + num(kToughHp) + "+ " + hpRange(info.hp[1][0], info.hp[1][1]) + "[/gold]";
        R().text(rx, y, hp, ts(F12, col::white, LEFT, rw));
        y += 18;
      }
      R().text(rx, y, L("bestiary.ACTIONS.header"), ts(F12, col::gold));
      y += 15;
      gfx::rect(rx, y, rw, 1, 0xC8B08080);
      y += 3;
      // Rows scroll to keep the selected move in view.
      const float rowH = 16, listBottom = kH - 20;
      const int fit = std::max(1, (int)((listBottom - y) / rowH));
      int first = std::clamp(st.move - fit + 1, 0, std::max(0, (int)st.moves.size() - fit));
      for (int i = first; i < (int)st.moves.size() && i < first + fit; ++i) {
        const Move& mv = st.moves[i];
        float ry = y + (i - first) * rowH;
        if (i == st.move) gfx::rect(rx - 2, ry - 1, rw + 4, rowH, 0xEFC85140);
        R().text(rx, ry, mv.name, ts(F12, i == st.move ? col::gold : col::white, LEFT, 96));
        float ix = rx + 98;
        for (size_t k = 0; k < mv.intents[0].size() && ix < kTop - 20; ++k) {
          const Intent& in = mv.intents[0][k];
          const Intent* hi = k < mv.intents[1].size() ? &mv.intents[1][k] : nullptr;
          Sprite ic = intentSprite(in, in.damage);
          spr(ic, ix, ry, 14, 14);
          ix += 15;
          std::string label;
          if (in.kind == Intent::Attack) {
            label = in.hits > 1 ? num(in.damage) + "×" + num(in.hits) : num(in.damage);
            if (hi && hi->kind == Intent::Attack && hi->damage != in.damage) label += "[gold](" + num(hi->damage) + ")[/gold]";
          } else if (in.kind == Intent::Status && in.count > 0) {
            label = num(in.count);
          }
          if (!label.empty()) {
            R().text(ix, ry, label, ts(F12, col::white));
            ix += R().measure(label, ts(F12)) + 3;
          }
        }
      }
      if (st.moves.empty()) R().text(rx, y, "—", ts(F12, col::gray));
    }
    R().text(kTop - 6, kH - 15, tr("L R 分组  X 演示  Y 下一行动  B 返回  (括号: 进阶", "L R Group  X Animate  Y Next move  B Back  (brackets: A") + num(kDeadlyDmg) + "+)",
             ts(F12, col::gray, RIGHT, 0, 0.85f));
    return true;
  }

  // Bottom: the list, then the bar.
  if (title) drawMenuBg(false, 0.6f);
  else drawSceneBg(false, 0.7f);
  gfx::pushClip(0, kListY0, kBot, kListY1 - kListY0);
  for (auto& r : st.rows) {
    float y = kListY0 + r.y - st.scroll;
    if (y + r.h < kListY0 || y > kListY1) continue;
    if (r.header) {  // NBestiaryLabelDivider
      R().text(kBot / 2, y + 3, st.groups[r.group], ts(F12, col::gold, CENTER));
      gfx::rect(12, y + r.h / 2, 90, 1, 0xC8B08090);
      gfx::rect(kBot - 102, y + r.h / 2, 90, 1, 0xC8B08090);
      continue;
    }
    for (int c = 0; c < 2; ++c) {
      int i = r.e[c];
      if (i < 0) continue;
      const Entry& e = st.entries[i];
      bool focus = i == st.sel;
      if (focus) gfx::rect(kColX[c], y, kColW, r.h - 1, st.zone == 0 ? 0xEFC85150 : 0xEFC85128);
      std::string label = e.seen ? e.title : brackets(L("bestiary.UNSEEN.monsterName"));
      uint32_t colr = e.seen ? roomColor(e.room) : col::gray;
      R().text(kColX[c] + 6, y + 1, label, ts(F12, colr, LEFT, 0, focus ? 1.05f : 1.f));
      if (focus && st.zone == 0) {  // the selection arrow
        gfx::rect(kColX[c] + 1, y + 4, 3, 8, col::gold);
      }
    }
  }
  gfx::popClip();
  if (maxScroll() > 0) {  // scrollbar
    float h = kListY1 - kListY0, th = std::max(16.f, h * h / (h + maxScroll()));
    float ty = kListY0 + (h - th) * (st.scroll / maxScroll());
    gfx::rect(kBot - 4, kListY0, 3, h, 0x00000080);
    gfx::rect(kBot - 4, ty, 3, th, 0xC8B080FF);
  }
  gfx::rect(0, kBarY - 3, kBot, kH - kBarY + 3, 0x000000A0);
  button(kBarX[0], kBarY, kBarW[0], kBarH, tr("返回", "Back"), kBtnBack);
  const bool haveMoves = !st.moves.empty() && fe && fe->seen;
  button(kBarX[1], kBarY, kBarW[1], kBarH, "<", kBtnPrev, haveMoves);
  std::string name = haveMoves ? "▶ " + st.moves[std::clamp(st.move, 0, (int)st.moves.size() - 1)].name
                               : L("bestiary.ACTIONS.header");
  button(kBarX[2], kBarY, kBarW[2], kBarH, name, kBtnPlay, haveMoves, haveMoves);
  button(kBarX[3], kBarY, kBarW[3], kBarH, ">", kBtnNext, haveMoves);
  if (st.zone == 1) outline(kBarX[st.bar], kBarY, kBarW[st.bar], kBarH);
  return true;
}

bool App::updateBestiary(const gfx::Input& in) {
  State& st = S();
  if (!st.open) return false;
  // Spine time.
  float dt = st.lastT < 0 ? 0.f : (float)std::clamp(time_ - st.lastT, 0.0, 0.1);
  st.lastT = time_;
  for (auto& p : st.parts)
    if (p.anim) p.anim->update(dt);

  const int n = (int)st.entries.size();
  auto close = [&] {
    st.open = false;
    freeParts();  // back to the menu without the monster's pages
  };
  auto select = [&](int i) {
    if (n == 0) return;
    st.sel = std::clamp(i, 0, n - 1);
    scrollToSel();
    showEntry(st.sel);
  };
  const int nm = (int)st.moves.size();
  const bool movesOn = nm > 0 && st.sel < n && st.entries[st.sel].seen;
  auto stepMove = [&](int d) {
    if (movesOn) st.move = (st.move + d + nm) % nm;
  };
  auto activate = [&](int id) {
    if (id == kBtnBack) close();
    else if (id == kBtnPrev) stepMove(-1);
    else if (id == kBtnNext) stepMove(1);
    else if (id == kBtnPlay && movesOn) playMove();
  };
  auto jumpGroup = [&](int d) {
    if (n == 0) return;
    int g = std::clamp(st.entries[st.sel].group + d, 0, (int)st.groups.size() - 1);
    for (int i = 0; i < n; ++i)
      if (st.entries[i].group == g) { select(i); return; }
  };

  // Touch: bar buttons on press; the list scrolls on drag, a tap focuses.
  if (in.touchDown) {
    int id = hitAt(in.tx, in.ty);
    if (id != ID_NONE) {
      for (int i = 0; i < 4; ++i) if (kBarId[i] == id) st.bar = i;
      activate(id);
      return true;
    }
    if (in.ty >= kListY0 && in.ty < kListY1) {
      st.touchDown = true;
      st.dragged = false;
      st.touchY0 = st.touchLastY = (float)in.ty;
      st.touchEntry = entryAt((float)in.tx, (float)in.ty);
    }
  } else if (st.touchDown && in.touching) {
    if (std::fabs(in.ty - st.touchY0) > kTapSlop) st.dragged = true;
    if (st.dragged) st.scroll = std::clamp(st.scroll - (in.ty - st.touchLastY), 0.f, maxScroll());
    st.touchLastY = (float)in.ty;
  }
  if (in.touchUp && st.touchDown) {
    st.touchDown = false;
    if (!st.dragged && st.touchEntry >= 0 && entryAt((float)in.tx, (float)in.ty) == st.touchEntry) {
      st.zone = 0;
      if (st.sel != st.touchEntry) {
        st.sel = st.touchEntry;
        showEntry(st.sel);
      }
    }
    return true;
  }

  const uint32_t d = in.down;
  if (d & gfx::BTN_B) { close(); return true; }
  if (d & gfx::BTN_L) { jumpGroup(-1); return true; }
  if (d & gfx::BTN_R) { jumpGroup(1); return true; }
  if (d & gfx::BTN_X) { if (movesOn) playMove(); return true; }
  if (d & gfx::BTN_Y) { stepMove(1); return true; }
  if (st.zone == 0) {
    if (n == 0) { if (d) st.zone = 1; return true; }
    const Entry& e = st.entries[st.sel];
    if (d & gfx::BTN_LEFT) select(st.sel - 1);
    if (d & gfx::BTN_RIGHT) select(st.sel + 1);
    auto rowStep = [&](int dir) -> int {  // the entry in the next row with entries, same column if there
      for (int r = e.row + dir; r >= 0 && r < (int)st.rows.size(); r += dir) {
        const Row& row = st.rows[r];
        if (row.header) continue;
        return row.e[e.col] >= 0 ? row.e[e.col] : row.e[0];
      }
      return -1;
    };
    if (d & gfx::BTN_DOWN) {
      int i = rowStep(1);
      if (i < 0) st.zone = 1;
      else select(i);
    } else if (d & gfx::BTN_UP) {
      int i = rowStep(-1);
      if (i >= 0) select(i);
      else { st.scroll = 0; }
    }
    if (d & gfx::BTN_A) {  // NBestiaryEntry: focus = select; A plays the selected move
      if (movesOn) playMove();
    }
  } else {
    if (d & gfx::BTN_LEFT) st.bar = (st.bar + 3) % 4;
    if (d & gfx::BTN_RIGHT) st.bar = (st.bar + 1) % 4;
    if (d & gfx::BTN_UP) { st.zone = 0; scrollToSel(); }
    if (d & gfx::BTN_A) activate(kBarId[st.bar]);
  }
  return true;
}

}  // namespace ui
