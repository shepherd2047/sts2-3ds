// Ascension rules (C10): what each level changes outside the monsters' own numbers
// (tools/ascension_check.py covers those).
// Build: make -f Makefile.sdl build/ascension_test ; run: ./build/ascension_test
#include <cstdio>
#include <cstdlib>

#include "../source/core/game.h"

using namespace sts;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    ++checks;                                                           \
    if (!(cond)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #cond); } \
  } while (0)

static int eliteCount(Run& r) {
  int n = 0;
  for (auto& node : r.nodes) if (node.type == RoomType::Elite) ++n;
  return n;
}

int main() {
  db::init();
  {  // levels are cumulative
    Run r;
    r.start(1, "Ironclad", 7);
    CHECK(r.ascension == 7);
    CHECK(r.hasAscension(kSwarmingElites) && r.hasAscension(kScarcity) && !r.hasAscension(kToughEnemies));
    CHECK(r.ascValue(kScarcity, 1, 2) == 1 && r.ascValue(kToughEnemies, 1, 2) == 2);
    Run clamp;
    clamp.start(1, "Ironclad", 99);
    CHECK(clamp.ascension == 10);
  }
  {  // TightBelt (4): one potion slot fewer; AscendersBane (5): the curse in the starting deck
    for (int level = 0; level <= 10; ++level) {
      Run r;
      r.start(3, "Ironclad", level);
      CHECK(r.potions.size() == (level >= 4 ? 2u : 3u));
      int banes = 0;
      for (auto& c : r.deck) if (c->id == "AscendersBane") ++banes;
      CHECK(banes == (level >= 5 ? 1 : 0));
      CHECK(r.deck.size() == (level >= 5 ? 11u : 10u));
    }
    Run r;
    r.start(3, "Ironclad", 5);
    Card* bane = r.deck.back().get();
    CHECK(bane->id == "AscendersBane" && bane->type == CardType::Curse && bane->has(kwUnplayable) && bane->has(kwEthereal));
    CHECK(!bane->isRemovable() && !bane->isTransformable() && !bane->upgradable());
    CHECK(r.transformCard(bane, db::card("StrikeIronclad")) == bane);  // CardCmd.Transform skips it
    CHECK(r.deck.back().get() == bane);
  }
  {  // SwarmingElites (1): 8 elites on the map instead of 5 (seed 1 places them all; the map rules can leave fewer)
    int normal[3], swarming[3];
    for (int act = 0; act < 3; ++act) {
      Run a;
      a.start(1, "Ironclad", 0);
      a.enterAct(act);
      normal[act] = eliteCount(a);
      Run b;
      b.start(1, "Ironclad", 1);
      b.enterAct(act);
      swarming[act] = eliteCount(b);
    }
    for (int act = 0; act < 3; ++act) {
      if (normal[act] != 5 || swarming[act] != 8) printf("act %d: elites %d / swarming %d\n", act, normal[act], swarming[act]);
      CHECK(normal[act] == 5 && swarming[act] == 8);
    }
  }
  {  // Inflation (6): card removal 100 + 50 per use instead of 75 + 25
    ShopItem removal;
    removal.kind = ShopItem::Removal;
    for (int level : {0, 5, 6, 10}) {
      Run r;
      r.start(1, "Ironclad", level);
      bool inflated = level >= 6;
      CHECK(r.shopPrice(removal) == (inflated ? 100 : 75));
      r.shopRemovalsUsed = 2;
      CHECK(r.shopPrice(removal) == (inflated ? 200 : 125));
    }
  }
  {  // Scarcity (7): upgraded reward cards are half as likely per act, rare offsets grow half as fast
    int upgraded[2] = {0, 0};
    for (int i = 0; i < 2; ++i) {
      Run r;
      r.start(5, "Ironclad", i == 0 ? 0 : 7);
      r.actIndex = 2;  // odds = act index * 0.25 (0.125)
      for (int k = 0; k < 4000; ++k) {
        auto c = db::card("Bash");
        r.rollCardUpgrade(*c, 0);
        if (c->upgraded()) ++upgraded[i];
      }
    }
    CHECK(upgraded[0] > 1800 && upgraded[0] < 2200);  // 0.5
    CHECK(upgraded[1] > 800 && upgraded[1] < 1200);   // 0.25
    Run r;
    r.start(5, "Ironclad", 0);
    r.actIndex = 0;
    int first = 0;
    for (int k = 0; k < 500; ++k) {
      auto c = db::card("Bash");
      r.rollCardUpgrade(*c, 0);
      first += c->upgraded();
    }
    CHECK(first == 0);  // act 1: nothing upgrades on its own
    // The shop rolls with -999999999: never upgraded, but the Rewards stream still advances.
    auto c = db::card("Bash");
    r.rollCardUpgrade(*c, -999999999);
    CHECK(!c->upgraded());
    Run one, two;
    one.start(9, "Ironclad", 0);
    two.start(9, "Ironclad", 0);
    auto d = db::card("Bash");
    two.rollCardUpgrade(*d, -999999999);
    CHECK(one.rng("Rewards").nextFloat() != two.rng("Rewards").nextFloat());
  }
  {  // DoubleBoss (10): a second boss in the last act, different from the first
    for (int level : {9, 10}) {
      Run r;
      r.start(4, "Ironclad", level);
      r.enterAct(0);
      CHECK(r.secondBossId.empty());
      r.enterAct(2);
      if (level == 10) CHECK(!r.secondBossId.empty() && r.secondBossId != r.bossId && db::encounter(r.secondBossId));
      else CHECK(r.secondBossId.empty());
    }
  }
  {  // monsters read the run's ascension: Nibbit 42-46 -> 44-48 (ToughEnemies), Butt 12 -> 13 (DeadlyEnemies)
    for (int level : {0, 7, 8, 9}) {
      Run r;
      r.start(1, "Ironclad", level);
      Combat c;
      c.run = &r;
      c.player = r.player.get();
      Rng rng(1, "t");
      auto monsters = db::encounter("NibbitsNormal")->generate(rng);
      Monster* m = monsters[0].get();
      m->combat = &c;
      CHECK(m->minHp() == (level >= 8 ? 44 : 42) && m->maxHp() == (level >= 8 ? 48 : 46));
      Creature* cr = c.createEnemy(std::move(monsters[0]));
      auto* butt = static_cast<MoveState*>(cr->monster->machine.states["BUTT_MOVE"].get());
      CHECK(butt->intents[0].damage == (level >= 9 ? 13 : 12));
    }
  }
  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
