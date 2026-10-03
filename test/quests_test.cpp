// E2 checks: deck cards as run-level listeners, the Quest card type, the "?"-room / next-event /
// free-travel / act-map hooks and the cards and relics that use them (Guilty, Dowsing, LanternKey,
// SpoilsMap + SpoilsActMap, ByrdonisEgg, DowsingRod, WingedBoots, ScrollBoxes).
// Build: make -f Makefile.sdl build/quests_test ; run: ./build/quests_test
#include <cstdio>
#include "portable_env.h"
#include <cstdlib>
#include <set>

#include "../source/core/game.h"

using namespace sts;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    ++checks;                                                           \
    if (!(cond)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #cond); } \
  } while (0)

static bool pump(const std::function<bool()>& done, int maxFrames = 4000) {
  for (int i = 0; i < maxFrames && !done(); ++i) Scheduler::get().update(0.05);
  return done();
}

// Runs a task to completion.
static void runTask(Task<> t) {
  bool done = false;
  Scheduler::get().spawn([](Task<> inner, bool* d) -> Task<> { co_await inner; *d = true; }(std::move(t), &done));
  pump([&] { return done; });
}

static int count(Run& r, const char* id) {
  int n = 0;
  for (auto& c : r.deck) n += c->id == id;
  return n;
}
static Card* find(Run& r, const char* id) {
  for (auto& c : r.deck) if (c->id == id) return c.get();
  return nullptr;
}
static Relic* relic(Run& r, const char* id) {
  for (auto& x : r.relics) if (x->id == id) return x.get();
  return nullptr;
}
static int nodeOfType(Run& r, RoomType t) {
  for (int i = 0; i < (int)r.nodes.size(); ++i) if (r.nodes[i].type == t) return i;
  return -1;
}

static Task<> combatEnds(Run* r) {
  for (Model* m : r->listeners()) co_await m->afterCombatEnd();
}

int main() {
  db::init();
  setenv("STS_NO_NEOW", "1", 1);

  {  // the Quest type and the quest cards
    for (const char* id : {"Dowsing", "SpoilsMap", "LanternKey", "ByrdonisEgg"}) {
      auto c = db::card(id);
      CHECK(c && c->type == CardType::Quest && c->rarity == Rarity::Quest && c->has(kwUnplayable));
      CHECK(!db::enchantment("Sharp")->canEnchant(*c));  // EnchantmentModel.CanEnchant refuses Quest cards
    }
    for (const char* id : {"DowsingRod", "WingedBoots", "ScrollBoxes"}) CHECK(db::relicRegistered(id));
  }

  {  // deck cards are run-level listeners, before the relics
    Run r;
    r.start(11);
    auto ls = r.listeners();
    CHECK(!r.deck.empty() && ls.size() >= r.deck.size() + r.relics.size());
    CHECK(ls[0] == r.deck[0].get());
    CHECK(r.deck[0]->run == &r && r.deck[0]->inDeck());
    auto loose = db::card("Guilty");
    CHECK(!loose->inDeck());
  }

  {  // Guilty: leaves the deck after its fifth combat
    Run r;
    r.start(12);
    r.addCardToDeck(db::card("Guilty"));
    for (int i = 0; i < 4; ++i) runTask(combatEnds(&r));
    CHECK(count(r, "Guilty") == 1 && find(r, "Guilty")->val("Combats") == Dec(1));
    runTask(combatEnds(&r));
    CHECK(count(r, "Guilty") == 0);
  }

  {  // Dowsing: five "?" map points (first room only) -> Abundance
    Run r;
    r.start(13);
    r.addCardToDeck(db::card("Dowsing"));
    int unknown = nodeOfType(r, RoomType::Unknown), monster = nodeOfType(r, RoomType::Monster);
    CHECK(unknown > 0 && monster > 0);
    r.currentNode = monster;
    r.currentRoomCount = 1;
    runTask(r.beforeRoomEntered(RoomType::Monster));
    CHECK(find(r, "Dowsing")->val("Rooms") == Dec(5));
    r.currentNode = unknown;
    r.currentRoomCount = 2;  // an event's fight: not counted
    runTask(r.beforeRoomEntered(RoomType::Monster));
    CHECK(find(r, "Dowsing")->val("Rooms") == Dec(5));
    r.currentRoomCount = 1;
    for (int i = 0; i < 4; ++i) runTask(r.beforeRoomEntered(RoomType::Unknown));
    CHECK(find(r, "Dowsing") && find(r, "Dowsing")->val("Rooms") == Dec(1));
    runTask(r.beforeRoomEntered(RoomType::Monster));  // a "?" that rolled a fight counts too
    CHECK(count(r, "Dowsing") == 0 && count(r, "Abundance") == 1);
  }

  {  // DowsingRod gives the card
    Run r;
    r.start(14);
    runTask(r.obtainRelic(db::relic("DowsingRod")));
    CHECK(count(r, "Dowsing") == 1);
  }

  {  // LanternKey: act 3's "?" rooms are all events, and every event is WarHistorianRepy
    Run r;
    r.start(15);
    r.addCardToDeck(db::card("LanternKey"));
    bool nonEvent = false;
    for (int i = 0; i < 200; ++i) nonEvent = nonEvent || r.rollUnknownRoom() != RoomType::Unknown;
    CHECK(nonEvent);  // acts 1-2: the usual odds
    auto e = r.pullNextEvent();
    CHECK(!e || e->id != "WarHistorianRepy");
    r.enterAct(2);
    bool allEvents = true;
    for (int i = 0; i < 200; ++i) allEvents = allEvents && r.rollUnknownRoom() == RoomType::Unknown;
    CHECK(allEvents);
    e = r.pullNextEvent();
    CHECK(e && e->id == "WarHistorianRepy");
  }

  {  // JuzuBracelet through the same hook: no fights
    Run r;
    r.start(16);
    runTask(r.obtainRelic(db::relic("JuzuBracelet")));
    bool fight = false;
    for (int i = 0; i < 300; ++i) fight = fight || r.rollUnknownRoom() == RoomType::Monster;
    CHECK(!fight);
  }

  {  // SpoilsActMap: one centred treasure every path goes through, adjacency, a reachable boss
    for (uint64_t seed = 1; seed <= 40; ++seed) {
      Rng rng(seed, "spoils_map");
      auto nodes = generateSpoilsActMap(rng, 1, 5, nullptr);
      CHECK(nodes.size() > 10 && nodes[0].type == RoomType::Ancient && nodes.back().type == RoomType::Boss);
      int treasures = 0, treasureRow = -1;
      for (auto& n : nodes)
        if (n.type == RoomType::Treasure) { ++treasures; treasureRow = n.row; CHECK(n.col == 3); }
      CHECK(treasures == 1);
      int inRow = 0;
      for (auto& n : nodes) inRow += n.row == treasureRow;
      CHECK(inRow == 1);  // the hourglass waist
      bool adjacent = true, deadEnd = false;
      for (size_t i = 1; i + 1 < nodes.size(); ++i) {
        deadEnd = deadEnd || nodes[i].next.empty();
        for (int c : nodes[i].next)
          if (nodes[c].type != RoomType::Boss) adjacent = adjacent && std::abs(nodes[c].col - nodes[i].col) <= 1;
      }
      CHECK(adjacent && !deadEnd);
    }
  }

  {  // SpoilsMap: act 2 is a SpoilsActMap with the quest on its treasure, kept through a save
    Run r;
    r.start(17);
    r.addCardToDeck(db::card("SpoilsMap"));
    r.enterAct(1);
    int t = nodeOfType(r, RoomType::Treasure);
    CHECK(t > 0 && r.nodes[t].quests.size() == 1 && r.nodes[t].quests[0] == "SpoilsMap");
    int treasures = 0;
    for (auto& n : r.nodes) treasures += n.type == RoomType::Treasure;
    CHECK(treasures == 1);
    std::string data = r.save();
    Run l;
    CHECK(l.load(data));
    int lt = nodeOfType(l, RoomType::Treasure);
    CHECK(lt == t && l.nodes[lt].quests.size() == 1);
    // Opening the chest: 600 gold, the card leaves the deck and the mark goes.
    int gold = r.gold;
    r.currentNode = t;
    int got = 0;
    runTask([](Run* run, int* out) -> Task<> { *out = co_await run->handleSpoilsMap(); }(&r, &got));
    CHECK(got == 600 && r.gold == gold + 600 && count(r, "SpoilsMap") == 0 && r.nodes[t].quests.empty());
    // Without the card the act map is a standard one.
    Run s;
    s.start(17);
    s.enterAct(1);
    int st = 0;
    for (auto& n : s.nodes) st += n.type == RoomType::Treasure;
    CHECK(st > 1);
  }

  {  // ByrdonisEgg: rest sites offer Hatch (obtain Byrdpip, pets_test.cpp)
    Run r;
    r.start(18);
    r.addCardToDeck(db::card("ByrdonisEgg"));
    std::vector<int> opts = {0, 1};
    bool added = false;
    for (Model* m : r.listeners()) added = m->tryModifyRestSiteOptions(opts) || added;
    CHECK(added && db::relicRegistered("Byrdpip"));
    CHECK(std::find(opts.begin(), opts.end(), 7) != opts.end());
  }

  {  // WingedBoots: free travel to the next row, three off-path moves
    Run r;
    r.start(19);
    runTask(r.obtainRelic(db::relic("WingedBoots")));
    Relic* boots = relic(r, "WingedBoots");
    CHECK(boots && boots->showCounter() && boots->displayAmount() == 3);
    // A point of row 0, and a row-1 point that is not its child.
    int from = -1, to = -1;
    for (int i = 0; i < (int)r.nodes.size() && to < 0; ++i) {
      if (r.nodes[i].row != 0) continue;
      for (int j = 0; j < (int)r.nodes.size(); ++j)
        if (r.nodes[j].row == 1 && std::find(r.nodes[i].next.begin(), r.nodes[i].next.end(), j) == r.nodes[i].next.end()) {
          from = i; to = j; break;
        }
    }
    CHECK(from > 0 && to > 0);
    r.currentNode = from;
    auto path = r.pathNodes();
    CHECK(std::find(path.begin(), path.end(), to) != path.end());
    for (int k = 0; k < 3; ++k) {
      r.previousNode = from;
      r.currentNode = to;
      r.currentRoomCount = 1;
      runTask(boots->afterRoomEntered(r.nodes[to].type));
    }
    CHECK(boots->usedUp && boots->val("Rooms") == Dec(0) && !boots->showCounter());
    r.currentNode = from;
    path = r.pathNodes();
    CHECK(path == r.nodes[from].next);
    // An on-path move never counts.
    Run q;
    q.start(19);
    runTask(q.obtainRelic(db::relic("WingedBoots")));
    Relic* b2 = relic(q, "WingedBoots");
    q.previousNode = from;
    q.currentNode = q.nodes[from].next[0];
    q.currentRoomCount = 1;
    runTask(b2->afterRoomEntered(RoomType::Monster));
    CHECK(b2->val("Rooms") == Dec(3));
  }

  {  // ScrollBoxes: two bundles of 2 Commons + 1 Uncommon, six distinct cards, the pick goes to the deck
    Run r;
    r.start(20);
    size_t before = r.deck.size();
    Scheduler::get().spawn(r.obtainRelic(db::relic("ScrollBoxes")));
    CHECK(pump([&] { return r.rewardChoice.waiting(); }));
    CHECK(r.rewardBundleSize == 3 && r.rewardCards.size() == 6);
    std::set<std::string> ids;
    for (auto& c : r.rewardCards) ids.insert(c->id);
    CHECK(ids.size() == 6);
    CHECK(r.rewardCards[0]->rarity == Rarity::Common && r.rewardCards[1]->rarity == Rarity::Common &&
          r.rewardCards[2]->rarity == Rarity::Uncommon && r.rewardCards[5]->rarity == Rarity::Uncommon);
    std::string a = r.rewardCards[3]->id, b = r.rewardCards[4]->id, c = r.rewardCards[5]->id;
    r.rewardChoice.fire(1);
    CHECK(pump([&] { return r.deck.size() == before + 3; }));
    CHECK(r.deck.size() == before + 3 && r.deck[before]->id == a && r.deck[before + 1]->id == b && r.deck[before + 2]->id == c);
    CHECK(r.rewardBundleSize == 1 && r.rewardCards.empty());
    Scheduler::get().clear();
  }

  {  // Quest cards are never offered for a transform
    Run r;
    r.start(21);
    r.addCardToDeck(db::card("LanternKey"));
    Scheduler::get().spawn([](Run* run) -> Task<> { co_await run->selectFromDeck("card_selection.TO_TRANSFORM", nullptr, 1); }(&r));
    pump([&] { return r.deckChoice.active; });
    bool offered = false;
    for (Card* c : r.deckChoice.options) offered = offered || c->type == CardType::Quest;
    CHECK(r.deckChoice.active && !offered);
    Scheduler::get().clear();
  }

  printf("quests_test: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
