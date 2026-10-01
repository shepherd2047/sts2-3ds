// Saves (package 12). The run is saved where it waits for a map choice (Run::main calls
// onSavePoint), so no fight, event, shop or reward is in progress: the state is the run
// itself: player, deck, relics (+ Relic::persist), potions, map, queues, grab bags, odds
// and every RNG stream's raw state. Loading builds a fresh run (Run::start for the
// constant parts), overwrites it from the file and resumes Run::main at the map.
#include <cmath>
#include <sstream>

#include "game.h"

namespace sts {

namespace {

constexpr int kSaveVersion = 10;  // 10: every act's RoomSet (older: the current act's queues, later acts regenerated), 2: card enchantments, 3: character id, 4: ascension (older saves load as Ironclad / ascension 0), 5: act list (older: Overgrowth, Hive, Glory), 6: run history path + times (M2; older: empty path), 7: badge inputs per map point + CCCCOMBO (M7; older: untracked points), 8: modifiers, custom mode, seed text, "?" elite odds (M11; older: none), 9: daily date (M12; older: not daily)

void ioCard(Archive& a, std::unique_ptr<Card>& c) {
  std::string id = c ? c->id : "";
  int level = c ? c->upgradeLevel : 0, keywords = c ? c->keywords : 0, cost = c ? c->cost : 0;
  int replay = c ? c->baseReplayCount : 0;
  std::vector<std::string> names;
  std::vector<int64_t> values;
  if (c) for (auto& v : c->vars) { names.push_back(v.name); values.push_back(v.base.raw); }
  a.io(id);
  a.io(level);
  a.io(keywords);
  a.io(cost);
  a.io(replay);
  a.io(names);
  a.io(values);
  // The enchantment: id ("" = none), amount, status, vars, then its own state. The card's
  // keywords / cost / vars above already include what OnEnchant changed, so it is not rerun.
  std::string enchId = c && c->enchantment ? c->enchantment->id : "";
  int enchAmount = c && c->enchantment ? c->enchantment->amount : 0;
  int enchStatus = c && c->enchantment ? (int)c->enchantment->status : 0;
  std::vector<std::string> enchNames;
  std::vector<int64_t> enchValues;
  if (c && c->enchantment) for (auto& v : c->enchantment->vars) { enchNames.push_back(v.name); enchValues.push_back(v.base.raw); }
  a.io(enchId);
  if (!enchId.empty()) {
    a.io(enchAmount);
    a.io(enchStatus);
    a.io(enchNames);
    a.io(enchValues);
  }
  if (!a.reading) {
    if (c && c->enchantment) c->enchantment->persist(a);
    return;
  }
  c = db::card(id);
  if (!c) { a.ok = false; return; }
  for (int i = 0; i < level; ++i) c->upgrade();
  c->keywords = keywords;
  c->cost = cost;
  c->baseReplayCount = replay;
  for (size_t i = 0; i < names.size() && i < values.size(); ++i)
    if (auto* v = c->var(names[i].c_str())) v->base = Dec::fromRaw(values[i]);
  if (!enchId.empty()) {
    auto e = db::enchantment(enchId);
    if (!e) { a.ok = false; return; }
    e->card = c.get();
    e->amount = enchAmount;
    e->status = (EnchantStatus)enchStatus;
    for (size_t i = 0; i < enchNames.size() && i < enchValues.size(); ++i)
      if (auto* v = e->var(enchNames[i].c_str())) v->base = Dec::fromRaw(enchValues[i]);
    c->enchantment.p = std::move(e);
    c->enchantment->persist(a);
  }
  c->afterLoad();
}

void ioRelic(Archive& a, Run& r, std::unique_ptr<Relic>& rel) {
  std::string id = rel ? rel->id : "";
  bool usedUp = rel && rel->usedUp;
  std::vector<std::string> names;
  std::vector<int64_t> values;
  if (rel) for (auto& v : rel->vars) { names.push_back(v.name); values.push_back(v.base.raw); }
  a.io(id);
  a.io(usedUp);
  a.io(names);
  a.io(values);
  if (a.reading) {
    rel = db::relic(id);
    if (!rel) { a.ok = false; return; }
    rel->run = &r;
    rel->usedUp = usedUp;
    for (size_t i = 0; i < names.size() && i < values.size(); ++i)
      if (auto* v = rel->var(names[i].c_str())) v->base = Dec::fromRaw(values[i]);
  }
  rel->persist(a);
}

void ioNode(Archive& a, MapNode& n) {
  int type = (int)n.type;
  a.io(n.col); a.io(n.row); a.io(type); a.io(n.next); a.io(n.visited);
  a.io(n.x); a.io(n.y); a.io(n.jx); a.io(n.jy); a.io(n.angle);
  n.type = (RoomType)type;
}

void ioBag(Archive& a, std::map<RelicRarity, std::vector<std::string>>& bag) {
  std::vector<int> keys;
  for (auto& [k, v] : bag) keys.push_back((int)k);
  a.io(keys);
  for (int k : keys) a.io(bag[(RelicRarity)k]);
}

// Everything the run needs after Run::start. Reading and writing share this code.
void ioRun(Archive& a, Run& r) {
  a.tag("STS2SAVE");
  int version = kSaveVersion;
  a.io(version);
  if (version < 2 || version > kSaveVersion) { a.ok = false; return; }
  a.io(r.seed);
  if (version >= 3) a.io(r.characterId);
  if (version >= 4) a.io(r.ascension);
  if (version >= 5) a.io(r.actIds);
  else if (a.reading) r.actIds = {"Overgrowth", "Hive", "Glory"};
  if (a.reading && (r.actIds.size() != (size_t)Run::kActs || !std::all_of(r.actIds.begin(), r.actIds.end(),
                                                                          [](auto& id) { return db::act(id); }))) {
    a.ok = false;
    return;
  }
  a.io(r.player->hp); a.io(r.player->maxHp);
  a.io(r.gold); a.io(r.floor); a.io(r.actIndex);
  // Before v10: the current act's shuffled queues (weak / regular fights cycled by a fight count,
  // elites rotated, events erased when used). They become the current act's RoomSet; the later
  // acts keep the RoomSets Run::start just generated.
  int oldFights = 0;
  std::vector<std::string> oldElites, oldNormal, oldWeak, oldEvents, oldShared[Run::kActs];
  if (version < 10) { a.io(oldFights); a.io(oldElites); a.io(oldNormal); a.io(oldWeak); }
  a.io(r.bossId);
  a.io(r.currentNode);
  a.io(r.rarityOffset); a.io(r.potionRewardOdds);
  a.io(r.unknownMonsterOdds); a.io(r.unknownTreasureOdds); a.io(r.unknownShopOdds);
  if (version < 10) a.io(oldEvents);
  a.io(r.visitedEvents);
  if (version >= 4) a.io(r.secondBossId);
  a.io(r.ancientId);
  if (version < 10) {
    for (auto& s : oldShared) a.io(s);
    if (a.reading && r.actIndex >= 0 && r.actIndex < Run::kActs) {
      Run::RoomSet& rs = r.rooms[r.actIndex];
      int weakCount = r.act().weakCount;
      rs.normal.clear();
      for (int i = 0; i < weakCount && !oldWeak.empty(); ++i) rs.normal.push_back(oldWeak[(size_t)i % oldWeak.size()]);
      rs.normal.insert(rs.normal.end(), oldNormal.begin(), oldNormal.end());
      rs.normalVisited = oldFights;
      rs.elites = oldElites;
      rs.elitesVisited = 0;
      rs.events = oldEvents;
      rs.eventsVisited = 0;
      rs.boss = r.bossId;
      rs.secondBoss = r.secondBossId;
      rs.ancient = r.ancientId;
    }
  } else {
    for (auto& rs : r.rooms) {
      a.io(rs.events); a.io(rs.eventsVisited);
      a.io(rs.normal); a.io(rs.normalVisited);
      a.io(rs.elites); a.io(rs.elitesVisited);
      a.io(rs.boss); a.io(rs.secondBoss); a.io(rs.ancient);
    }
  }
  a.io(r.shopRemovalsUsed);
  a.tag("MAP");
  int n = (int)r.nodes.size();
  a.io(n);
  n = a.count(n);
  if (a.reading) r.nodes.assign((size_t)n, MapNode{});
  for (auto& node : r.nodes) ioNode(a, node);
  a.tag("BAGS");
  ioBag(a, r.relicBag);
  ioBag(a, r.sharedRelicBag);
  a.tag("RNG");
  std::vector<std::string> names;
  for (auto& [k, v] : r.rngs) names.push_back(k);
  a.io(names);
  for (auto& name : names) {
    Rng& g = r.rng(name.c_str());
    uint64_t st[4];
    g.raw().getState(st);
    for (auto& x : st) a.io(x);
    a.io(g.counter);
    if (a.reading) g.raw().setState(st);
  }
  a.tag("DECK");
  n = (int)r.deck.size();
  a.io(n);
  n = a.count(n);
  if (a.reading) { r.deck.clear(); r.deck.resize((size_t)n); }
  for (auto& c : r.deck) ioCard(a, c);
  a.tag("RELICS");
  n = (int)r.relics.size();
  a.io(n);
  n = a.count(n);
  if (a.reading) { r.relics.clear(); r.relics.resize((size_t)n); }
  for (auto& rel : r.relics) ioRelic(a, r, rel);
  a.tag("POTIONS");
  std::vector<std::string> belt;
  for (auto& p : r.potions) belt.push_back(p ? p->id : "");
  a.io(belt);
  if (a.reading) {
    r.potions.clear();
    for (auto& id : belt) {
      auto p = id.empty() ? nullptr : db::potion(id);
      if (p) p->run = &r;
      r.potions.push_back(std::move(p));
    }
  }
  if (version >= 6) {  // run history (history.h): the path so far, start time, milliseconds played
    a.tag("HISTORY");
    a.io(r.startTime);
    int64_t ms = std::llround(r.runTime * 1000.0);
    a.io(ms);
    if (a.reading) r.runTime = (double)ms / 1000.0;
    history::ioPath(a, r.mapHistory);
  }
  if (version >= 7) {  // badge inputs (badges.h), as in the history record: a resumed run counts them all
    a.tag("BADGES");
    a.io(r.cccCombo);
    for (auto& act : r.mapHistory)
      for (auto& p : act) {
        a.io(p.tracked);
        a.io(p.goldSpent);
        a.io(p.damageTaken);
        a.io(p.restChoices);
      }
  }
  if (version >= 8) {  // M11: RunState.Modifiers (keys, modifiers.h), GameMode.Custom, the seed text
    a.tag("MODIFIERS");
    std::vector<std::string> keys = r.modifierKeys();
    a.io(keys);
    if (a.reading) r.setModifiers(keys);  // ModifierModel.OnRunLoaded: nothing else to redo
    a.io(r.customRun);
    a.io(r.seedText);
    a.io(r.unknownEliteOdds);
  }
  if (version >= 9) {  // M12: GameMode.Daily (RunManager.DailyTime): the daily's date ("" when not daily)
    a.tag("DAILY");
    a.io(r.dailyDate);
  }
  a.tag("END");
}

// S22: indices the rest of the game uses unchecked must point inside the loaded run, so a garbled
// save that still parses is rejected here (-> the corrupt-save dialog) instead of crashing later.
bool loadedStateValid(const Run& r) {
  const int n = (int)r.nodes.size();
  if (r.currentNode < -1 || r.currentNode >= n) return false;
  for (auto& node : r.nodes)
    for (int next : node.next)
      if (next < 0 || next >= n) return false;
  if (r.actIndex < 0 || r.actIndex >= Run::kActs) return false;
  if (!r.player || r.player->maxHp <= 0 || r.player->hp <= 0 || r.player->hp > r.player->maxHp) return false;
  if (r.gold < 0 || r.floor < 0) return false;
  return true;
}

}  // namespace

std::string Run::save() {
  Archive a;
  ioRun(a, *this);
  return a.out;
}

bool Run::load(const std::string& data) {
  Archive a;
  a.reading = true;
  std::istringstream in(data);
  for (std::string t; in >> t;) a.toks.push_back(t);
  // The seed comes first after the header: start that run, then overwrite it.
  if (a.toks.size() < 3 || a.toks[0] != "STS2SAVE") return false;
  // Version 3 puts the character right after the seed, version 4 the ascension after that;
  // version 2 saves are Ironclad runs at ascension 0.
  int version = std::atoi(a.toks[1].c_str());
  start(std::strtoull(a.toks[2].c_str(), nullptr, 10), version >= 3 && a.toks.size() > 3 ? a.toks[3] : "Ironclad",
        version >= 4 && a.toks.size() > 4 ? std::atoi(a.toks[4].c_str()) : 0);
  ioRun(a, *this);
  if (!a.ok || !loadedStateValid(*this)) return false;
  // Saves from before C11 (DoubleBoss fights chained, no map node): give the last act's map its
  // second boss node (StandardActMap.SecondBossMapPoint takes no Rng, so the rest is the same).
  if (!secondBossId.empty() && secondBossNode() < 0) {
    int b = bossNode();
    if (b >= 0) {
      MapNode second = nodes[b];
      second.row = nodes[b].row + 1;
      second.y = (float)second.row;  // Run::generateMap: layout (col, row)
      second.next.clear();
      second.visited = false;
      nodes[b].next = {(int)nodes.size()};
      nodes.push_back(second);
    }
  }
  runLateMapHooks();  // E2: SavedActMap -> ModifyGeneratedMapLate + AfterMapGenerated (quest markers)
  ancientPending = false;
  died = false;
  screen = Screen::Map;
  return true;
}

}  // namespace sts
