// Run history checks (M2): record file round trip, the ScoreUtility formula, the 50-run ring per
// profile, the path kept in run.sav, recording exactly once at a run's end, and deleting a profile.
// Build: make -f Makefile.sdl build/history_test ; run: ./build/history_test
//
// Never touches a real save: profiles::init() below uses a root under build/.
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

#include "../source/core/game.h"
#include "../source/core/history.h"
#include "../source/core/profiles.h"
#include "../source/core/progress.h"

using namespace sts;
using history::MapPoint;
using history::PointType;
using history::RoomKind;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                               \
  do {                                                                            \
    ++checks;                                                                     \
    if (!(cond)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #cond); } \
  } while (0)

static void makeDir(const std::string& d) {
#ifdef _WIN32
  _mkdir(d.c_str());
#else
  mkdir(d.c_str(), 0777);
#endif
}

static bool fileExists(const std::string& path) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  fclose(f);
  return true;
}

static const std::string kRoot = "build/history_fixtures/";

static void clean() {
  for (int id = 1; id <= profiles::kCount; ++id) {
    history::removeAll(id);
    for (const char* f : {"run.sav", "progress.sav"}) remove((kRoot + "profile" + std::to_string(id) + "/" + f).c_str());
  }
  remove((kRoot + "profile.sav").c_str());
}

static history::RunRecord sample() {
  history::RunRecord r;
  r.seed = 123456789012345ull;
  r.character = "Silent";
  r.ascension = 7;
  r.acts = {"Underdocks", "Hive", "Glory"};
  r.startTime = 1790000000;
  r.runTime = 3725;
  r.killedByEncounter = "KnowledgeDemonBoss";
  r.floorReached = 33;
  r.path = {{{PointType::Ancient, {{RoomKind::Event, "Neow"}}, 0},
             {PointType::Monster, {{RoomKind::Monster, "ShrinkerBeetleWeak"}}, 14},
             {PointType::Unknown, {{RoomKind::Event, "DenseVegetation"}, {RoomKind::Monster, "Wrigglers"}}, 12},
             {PointType::Shop, {{RoomKind::Shop, ""}}, 0}},
            {}};
  r.deck = {{"StrikeSilent", 0, "", 0}, {"Neutralize", 1, "Sharp", 3}};
  r.relics = {"RingOfTheSnake", "Anchor"};
  r.potions = {"FirePotion"};
  r.maxPotionSlots = 2;
  r.gold = 250;
  r.hp = 0;
  r.maxHp = 70;
  r.score = 999;
  return r;
}

int main() {
  makeDir("build");
  makeDir(kRoot);

  {  // record format round trip; garbled and future versions rejected
    history::RunRecord r = sample(), g;
    CHECK(g.load(r.save()));
    CHECK(g == r);
    CHECK(g.path[0][2].rooms[1].model == "Wrigglers");
    CHECK(g.path[0][3].rooms[0].model.empty());
    CHECK(g.deck[1].enchantment == "Sharp" && g.deck[1].enchantAmount == 3);
    CHECK(!g.load(""));
    CHECK(!g.load("garbage"));
    std::string future = r.save();
    future.replace(future.find(" 1 "), 3, " 99 ");
    history::RunRecord h = sample();
    h.character = "unchanged";
    CHECK(!h.load(future));
    CHECK(h.character == "unchanged");
  }

  {  // run history lists (record version 5): choices, downgraded cards, completed quests
    history::RunRecord r = sample(), g;
    auto& p = r.path[0][1];
    p.cardChoices = {{"Bash", 1, true}, {"Cleave", 0, false}};
    p.relicChoices = {{"Anchor", 0, true}, {"Vajra", 0, false}};
    p.downgradedCards = {"Neutralize"};
    p.completedQuests = {"Dowsing", "SpoilsMap"};
    CHECK(g.load(r.save()));
    const auto& q = g.path[0][1];
    CHECK(q.cardChoices == p.cardChoices && q.relicChoices == p.relicChoices);
    CHECK(q.downgradedCards == p.downgradedCards && q.completedQuests == p.completedQuests);
    CHECK(g.path[0][0].cardChoices.empty() && g.path[0][3].completedQuests.empty());
    CHECK(g.save() == r.save());
    // A version 4 record (no CHOICES block) still loads, with empty lists.
    std::string v5 = history::RunRecord(sample()).save(), old = v5;
    size_t a = old.find(" CHOICES "), b = old.find(" DECK ");
    CHECK(a != std::string::npos && b != std::string::npos && a < b);
    old.erase(a, b - a);
    size_t sp = old.find(' '), sp2 = old.find(' ', sp + 1);
    old.replace(sp + 1, sp2 - sp - 1, "4");
    history::RunRecord o;
    CHECK(o.load(old));
    CHECK(o == sample() && o.path[0][1].cardChoices.empty() && o.path[0][1].relicChoices.empty());
    CHECK(o.deck.size() == 2 && o.relics.size() == 2 && o.maxPotionSlots == 2);
  }

  {  // ScoreUtility.CalculateScore
    history::Path p = {
        {{PointType::Ancient, {{RoomKind::Event, "Neow"}}, 0},
         {PointType::Monster, {{RoomKind::Monster, "A"}}, 15},
         {PointType::Elite, {{RoomKind::Elite, "E"}}, 40},
         {PointType::Boss, {{RoomKind::Boss, "B1"}}, 100}},
        {{PointType::Ancient, {{RoomKind::Event, "Orobas"}}, 0},
         {PointType::Elite, {{RoomKind::Elite, "E2"}}, 50}},
    };
    CHECK(history::floorScore(p) == 4 * 10 * 1 + 2 * 10 * 2);  // 80
    CHECK(history::goldScore(p) == 205 / 100);
    CHECK(history::elitesKilled(p) == 1);  // the last room was the elite that killed the player
    CHECK(history::bossesSlain(p, false) == 1);
    CHECK(history::score(p, 0, false) == 80 + 2 + 50 + 100);
    CHECK(history::score(p, 5, false) == (int)(232 * 1.5));
    p.pop_back();  // died to the act 1 boss
    CHECK(history::elitesKilled(p) == 1);
    CHECK(history::bossesSlain(p, false) == 0);
    CHECK(history::bossesSlain(p, true) == 1);
    CHECK(history::score({}, 10, false) == 0);
  }

  {  // nothing on disk without profiles::init
    profiles::reset();
    history::RunRecord r = sample();
    CHECK(!history::append(1, r));
    CHECK(history::load(1).empty());
  }

  profiles::init(kRoot);
  clean();
  profiles::init(kRoot);

  {  // the ring: 55 runs keep the newest 50, newest first; other profiles untouched
    for (int i = 1; i <= 55; ++i) {
      history::RunRecord r = sample();
      r.floorReached = i;
      CHECK(history::append(1, r));
      CHECK(r.seq == (uint64_t)i);
    }
    auto all = history::load(1);
    CHECK(all.size() == (size_t)history::kMax);
    CHECK(all.front().seq == 55 && all.front().floorReached == 55);
    CHECK(all.back().seq == 6 && all.back().floorReached == 6);
    bool ordered = true;
    for (size_t i = 1; i < all.size(); ++i) ordered = ordered && all[i - 1].seq == all[i].seq + 1;
    CHECK(ordered);
    CHECK(history::count(1) == 50);
    CHECK(history::load(2).empty());
    CHECK(fileExists(history::slotPath(1, 0)));
    CHECK(fileExists(history::slotPath(1, 49)));
    CHECK(!fileExists(history::slotPath(1, 0) + ".tmp"));
  }

  {  // a run: path recorded as it goes, kept by run.sav, entry written once at the end
    history::clearLast();
    CHECK(profiles::select(2));
    Run run;
    run.start(42, "Ironclad", 3);
    CHECK(run.mapHistory.empty());
    CHECK(run.startTime > 0);
    run.historyPoint(PointType::Ancient);
    run.historyRoom(RoomKind::Event, "Neow");
    run.historyPoint(PointType::Unknown);
    run.historyRoom(RoomKind::Event, "DenseVegetation");
    run.historyRoom(RoomKind::Monster, "Wrigglers");
    run.mapHistory.back().back().goldGained = 17;
    run.runTime = 61.25;
    std::string saved = run.save();
    Run loaded;
    CHECK(loaded.load(saved));
    CHECK(loaded.mapHistory == run.mapHistory);
    CHECK(loaded.startTime == run.startTime);
    CHECK(loaded.runTime == 61.25);
    CHECK(loaded.save() == saved);

    {  // the history lists ride in run.sav (version 11); a version 10 save loads without them
      Run lists;
      CHECK(lists.load(saved));
      history::noteRelicChoice(lists, "Vajra", true);
      history::noteRelicChoice(lists, "Anchor", false);
      history::noteCardChoice(lists, "Bash", 1, false);
      history::noteDowngraded(lists, "Strike");
      history::noteQuestCompleted(lists, "Dowsing");
      std::string s2 = lists.save();
      Run again;
      CHECK(again.load(s2));
      const auto& pt = again.mapHistory.back().back();
      CHECK(pt.relicChoices.size() == 2 && pt.relicChoices[0].id == "Vajra" && pt.relicChoices[0].picked && !pt.relicChoices[1].picked);
      CHECK(pt.cardChoices.size() == 1 && pt.cardChoices[0].id == "Bash" && pt.cardChoices[0].upgrades == 1);
      CHECK(pt.downgradedCards == std::vector<std::string>{"Strike"} && pt.completedQuests == std::vector<std::string>{"Dowsing"});
      CHECK(again.save() == s2);
      std::string v10 = saved;
      size_t a = v10.find(" CHOICES "), b = v10.find(" END");
      CHECK(a != std::string::npos && b != std::string::npos && a < b);
      v10.erase(a, b - a);
      size_t sp = v10.find(' '), sp2 = v10.find(' ', sp + 1);
      v10.replace(sp + 1, sp2 - sp - 1, "10");
      Run old;
      CHECK(old.load(v10));
      CHECK(old.mapHistory == run.mapHistory && old.mapHistory.back().back().relicChoices.empty());
    }

    loaded.abandon();
    loaded.abandon();  // a second call records nothing
    auto list = history::load(2);
    CHECK(list.size() == 1);
    if (!list.empty()) {
      const auto& r = list[0];
      CHECK(r.abandoned && !r.win);
      CHECK(r.character == "Ironclad" && r.ascension == 3 && r.seed == 42);
      CHECK(r.killedByEncounter == "Wrigglers");  // the last room entered
      CHECK(r.killedByEvent.empty());
      CHECK(r.runTime == 61);
      CHECK(r.deck.size() == loaded.deck.size());
      CHECK(r.relics.size() == loaded.relics.size());
      CHECK(r.path == run.mapHistory);
      CHECK(r.score == history::score(r.path, 3, false));
      CHECK(r.score == (int)((2 * 10 + 0) * 1.3));
    }
    CHECK(history::last() && history::last()->seq == 1);
    CHECK(history::load(1).size() == 50);  // profile 1 untouched
  }

  {  // deleting a profile deletes its history
    CHECK(profiles::remove(1));
    CHECK(history::load(1).empty());
    CHECK(!fileExists(history::slotPath(1, 7)));
    CHECK(history::load(2).size() == 1);
    CHECK(profiles::remove(2));
    CHECK(history::load(2).empty());
  }

  clean();
  profiles::reset();
  progress::reset();
  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
