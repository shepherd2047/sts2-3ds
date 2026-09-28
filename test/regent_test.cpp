// Regent systems checks (X3.0): Stars (gain/lose/set, hooks, star-cost and star-cost-X cards),
// Forge + Sovereign Blade, StarNextTurnPower.
// Build: make -f Makefile.sdl build/regent_test ; run: ./build/regent_test
#include <cstdio>
#include <cstdlib>

#include "../source/core/cards.h"
#include "../source/core/char_regent.h"

using namespace sts;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    ++checks;                                                           \
    if (!(cond)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #cond); } \
  } while (0)

static Task<> fightTask(Run* r) { co_await r->fight("NibbitsNormal"); }
static Task<> wrap(Task<> t, bool* done) { co_await t; *done = true; }

static bool pump(const std::function<bool()>& done, int maxFrames = 4000) {
  for (int i = 0; i < maxFrames && !done(); ++i) Scheduler::get().update(0.05);
  return done();
}
static void runTask(Task<> t) {
  bool done = false;
  Scheduler::get().spawn(wrap(std::move(t), &done));
  pump([&] { return done; });
}

// Watches AfterStarsGained / AfterStarsSpent / AfterForge (a relic is a hook listener).
struct StarsRelic : Relic {
  int gainedAmount = -1, spentAmount = -1;
  Dec forgedAmount = Dec(-1);
  StarsRelic() { id = "StarsRelic"; locKey = "STARS_RELIC"; }
  Task<> afterStarsGained(int amount) override { gainedAmount = amount; co_return; }
  Task<> afterStarsSpent(int amount) override { spentAmount = amount; co_return; }
  Task<> afterForge(Dec amount, Model*) override { forgedAmount = amount; co_return; }
};

// A star-cost test card, standing in for a Regent card (FallingStar: 0 energy, 2 stars) that
// isn't ported yet (that's X3.1+, content, not this systems package).
struct StarCostStrike : IroncladT<StarCostStrike> {
  CARD_HEADER(StarCostStrike, "TEST_STAR_COST_STRIKE", 0, Attack, Common, AnyEnemy)
    starCost = 2;
    addVar("Damage", 5);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
};

// A star-cost-X test card: spends every current star and deals that much damage.
struct StarCostXStrike : IroncladT<StarCostXStrike> {
  CARD_HEADER(StarCostXStrike, "TEST_STAR_COST_X_STRIKE", 0, Attack, Common, AnyEnemy)
    costsStarsX = true;
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, Dec(starXValue)); }
};

static Task<> doForge(Combat* c, Dec amount, std::vector<Card*>* out) { *out = co_await cmd::forge(*c, amount, nullptr); }

// The first turn of a fight against the Nibbits, both with 500 HP (see silent_test.cpp).
struct Fight {
  std::unique_ptr<Run> r = std::make_unique<Run>();
  Combat* c = nullptr;
  Fight() {
    r->start(3);
    Scheduler::get().spawn(fightTask(r.get()));
    pump([&] { return r->combat && r->combat->playerPhase && r->combat->actions.waiting(); });
    c = r->combat.get();
    for (Creature* e : c->enemies) e->hp = e->maxHp = 500;
  }
  ~Fight() { Scheduler::get().clear(); }
  Creature* enemy(int i = 0) { return c->enemies[i]; }
  void toHand(Card* k) { c->removeFromPiles(k); c->hand.push_back(k); }
  void play(Card* k, Creature* t) {
    PlayerAction a;
    a.kind = PlayerAction::PlayCard;
    a.card = k;
    a.target = t;
    c->energy = 10;
    c->actions.fire(a);
    pump([&] { return c->playerPhase && c->actions.waiting(); });
  }
  void endTurn() {
    PlayerAction a;
    a.kind = PlayerAction::EndTurn;
    int turn = c->turnNumber;
    c->actions.fire(a);
    pump([&] { return c->over || (c->turnNumber > turn && c->playerPhase && c->actions.waiting()); });
  }
  template <class P> void apply(Creature* t, int amount) {
    runTask([](Creature* t, int amount, Creature* by) -> Task<> { co_await applyPower<P>(t, amount, by, nullptr); }(t, amount, c->player));
  }
};

int main() {
  db::init();
  {  // GainStars fires AfterStarsGained and clamps at 0; LoseStars fires no hook; SetStars.
    Fight f;
    auto relic = std::make_unique<StarsRelic>();
    StarsRelic* w = relic.get();
    f.r->relics.push_back(std::move(relic));
    runTask(cmd::gainStars(*f.c, 3));
    CHECK(f.c->stars == 3 && w->gainedAmount == 3);
    runTask(cmd::loseStars(*f.c, 1));
    CHECK(f.c->stars == 2 && w->spentAmount == -1);  // LoseStars never fires AfterStarsSpent
    runTask(cmd::loseStars(*f.c, 99));
    CHECK(f.c->stars == 0);  // clamped at 0, not negative
    runTask(cmd::setStars(*f.c, 5));
    CHECK(f.c->stars == 5 && w->gainedAmount == 5);
    runTask(cmd::setStars(*f.c, 2));
    CHECK(f.c->stars == 2);
  }
  {  // A star-cost card: unplayable without enough stars; spending it fires AfterStarsSpent
     // (not AfterStarsGained) and captures LastStarsSpent.
    Fight f;
    auto relic = std::make_unique<StarsRelic>();
    StarsRelic* w = relic.get();
    f.r->relics.push_back(std::move(relic));
    Card* sc = f.c->addCard(std::make_unique<StarCostStrike>());
    f.toHand(sc);
    f.c->stars = 1;
    CHECK(!f.c->canPlay(sc));
    Creature* e = f.enemy(0);
    int hp = e->hp;
    f.play(sc, e);  // still unplayable: the action is silently dropped
    CHECK(e->hp == hp && f.c->pileOf(sc) == Pile::Hand);
    f.c->stars = 4;
    CHECK(f.c->canPlay(sc));
    f.play(sc, e);
    CHECK(hp - e->hp == 5);
    CHECK(f.c->stars == 2);  // 4 - 2 (star cost)
    CHECK(w->spentAmount == 2 && w->gainedAmount == -1);
    CHECK(sc->lastStarsSpent == 2);
  }
  {  // costsStarsX: spends every current star and captures it (ResolveStarXValue).
    Fight f;
    Card* sc = f.c->addCard(std::make_unique<StarCostXStrike>());
    f.toHand(sc);
    f.c->stars = 6;
    Creature* e = f.enemy(0);
    int hp = e->hp;
    f.play(sc, e);
    CHECK(f.c->stars == 0);
    CHECK(sc->lastStarsSpent == 6 && sc->starXValue == 6);
    CHECK(hp - e->hp == 6);
  }
  {  // Forge: creates Sovereign Blade in hand, grows it; forging again before it is played (or
     // after it is merely discarded, since Retain means it's never Exhausted here) grows the
     // same card instead of making a new one.
    Fight f;
    auto relic = std::make_unique<StarsRelic>();
    StarsRelic* w = relic.get();
    f.r->relics.push_back(std::move(relic));
    std::vector<Card*> blades;
    runTask(doForge(f.c, Dec(5), &blades));
    CHECK(blades.size() == 1 && blades[0]->id == "SovereignBlade");
    CHECK(f.c->pileOf(blades[0]) == Pile::Hand);
    CHECK(blades[0]->val("Damage").toInt() == 15);  // base 10 + 5
    CHECK(w->forgedAmount == Dec(5));
    std::vector<Card*> blades2;
    runTask(doForge(f.c, Dec(3), &blades2));
    CHECK(blades2.size() == 1 && blades2[0] == blades[0]);
    CHECK(blades[0]->val("Damage").toInt() == 18);
    CHECK(blades[0]->has(kwRetain));
    Creature* e = f.enemy(0);
    int hp = e->hp;
    f.play(blades[0], e);
    CHECK(hp - e->hp == 18);
    CHECK(f.c->pileOf(blades[0]) == Pile::Discard);  // an Attack without Exhaust: normal discard
    std::vector<Card*> blades3;
    runTask(doForge(f.c, Dec(1), &blades3));
    CHECK(blades3.size() == 1 && blades3[0] == blades[0]);  // discarded still counts as un-Exhausted
  }
  {  // StarNextTurnPower: gains its Amount in stars at the start of the next turn (AfterEnergyReset),
     // then removes itself.
    Fight f;
    f.apply<StarNextTurnPower>(f.c->player, 4);
    CHECK(f.c->player->powerAmount<StarNextTurnPower>() == 4);
    CHECK(f.c->stars == 0);
    f.endTurn();
    CHECK(f.c->stars == 4);
    CHECK(f.c->player->get<StarNextTurnPower>() == nullptr);
  }
  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
