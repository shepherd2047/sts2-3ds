// Silent systems checks (X1.0): discard + Sly, single-turn Retain / Sly, Poison (+ Accelerant),
// Shivs (+ Accuracy, Fan of Knives), LoseBlock.
// Build: make -f Makefile.sdl build/silent_test ; run: ./build/silent_test
#include <cstdio>
#include <cstdlib>

#include "../source/core/char_silent.h"

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

// Watches the end of the player's turn: is `keep` still in the hand after the flush?
struct ProbeRelic : Relic {
  Card* keep = nullptr;
  bool keptInHand = false;
  ProbeRelic() { id = "ProbeRelic"; locKey = "PROBE"; }
  Task<> afterSideTurnEnd(Side s, const std::vector<Creature*>&) override {
    if (s == Side::Player && keep->combat) keptInHand = keep->combat->pileOf(keep) == Pile::Hand;
    co_return;
  }
};

// Counts AfterCardDiscarded calls (a relic is a hook listener).
struct DiscardRelic : Relic {
  std::vector<Card*> seen;
  DiscardRelic() { id = "DiscardRelic"; locKey = "DISCARD"; }
  Task<> afterCardDiscarded(Card* k) override { seen.push_back(k); co_return; }
};

// The first turn of a fight against the Nibbits, both with 500 HP.
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
  Card* find(const std::string& id) {
    for (Card* k : c->allCards()) if (k->id == id) return k;
    return nullptr;
  }
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

static Task<> makeShivs(Combat* c, int n, std::vector<Card*>* out) { *out = co_await createShivsInHand(*c, n); }
static Task<> removeFan(Creature* t) { co_await cmd::removePower(t->get<FanOfKnivesPower>()); }

int main() {
  db::init();
  {  // Poison
    Fight f;
    Creature* e = f.enemy();
    f.apply<PoisonPower>(e, 5);
    CHECK(e->powerAmount<PoisonPower>() == 5);
    CHECK(e->get<PoisonPower>()->calculateTotalDamageNextTurn() == 5);
    f.endTurn();
    CHECK(e->hp == 495);  // 5 unblockable damage at the start of its turn
    CHECK(e->powerAmount<PoisonPower>() == 4);
    f.endTurn();
    CHECK(e->hp == 491 && e->powerAmount<PoisonPower>() == 3);
    // Accelerant on the player: Poison triggers twice per turn (never more than the stacks).
    f.apply<AccelerantPower>(f.c->player, 1);
    CHECK(e->get<PoisonPower>()->triggerCount() == 2);
    CHECK(e->get<PoisonPower>()->calculateTotalDamageNextTurn() == 3 + 2);
    f.endTurn();
    CHECK(e->hp == 491 - 3 - 2 && e->powerAmount<PoisonPower>() == 1);
    f.apply<AccelerantPower>(f.c->player, 5);
    CHECK(e->get<PoisonPower>()->triggerCount() == 1);  // capped by the stacks
    // Poison ignores block and kills.
    Fight g;
    Creature* n = g.enemy();
    n->hp = 4;
    n->block = 50;
    g.apply<PoisonPower>(n, 6);
    g.endTurn();
    CHECK(n->dead());
  }
  {  // Discard: order, hook, draw after, history, Sly
    Fight f;
    auto relic = std::make_unique<DiscardRelic>();
    DiscardRelic* w = relic.get();
    f.r->relics.push_back(std::move(relic));
    Card* a = f.c->hand[0];
    Card* b = f.c->hand[1];
    size_t handBefore = f.c->hand.size();
    runTask(cmd::discardCards(*f.c, {a, b}, 2));
    CHECK(f.c->hand.size() == handBefore);  // discarded two, drew two
    CHECK(f.c->pileOf(a) == Pile::Discard && f.c->pileOf(b) == Pile::Discard);
    CHECK(w->seen.size() == 2 && w->seen[0] == a && w->seen[1] == b);
    CHECK(f.c->discardsThisTurn() == 2);
    // Sly: discarding it plays it (a Strike here) for free, then it sits in the discard pile.
    Card* strike = f.find("StrikeIronclad");
    f.toHand(strike);
    strike->keywords |= kwSly;
    f.c->energy = 0;
    int hp = f.enemy(0)->hp + f.enemy(1)->hp;
    runTask(cmd::discardCard(*f.c, strike));
    CHECK(f.enemy(0)->hp + f.enemy(1)->hp == hp - 6);
    CHECK(f.c->pileOf(strike) == Pile::Discard && f.c->energy == 0);
    CHECK(f.c->discardsThisTurn() == 3);
    // A card that is not Sly is not played.
    Card* strike2 = nullptr;
    for (Card* k : f.c->allCards()) if (k->id == "StrikeIronclad" && k != strike) { strike2 = k; break; }
    f.toHand(strike2);
    hp = f.enemy(0)->hp + f.enemy(1)->hp;
    runTask(cmd::discardCard(*f.c, strike2));
    CHECK(f.enemy(0)->hp + f.enemy(1)->hp == hp);
    // Single-turn Sly and Retain last until the end of the turn.
    Card* d = f.c->hand[0];
    d->singleTurnSly = d->singleTurnRetain = true;
    CHECK(d->isSlyThisTurn() && d->shouldRetainThisTurn() && !d->has(kwSly) && !d->has(kwRetain));
    f.endTurn();
    CHECK(!d->singleTurnSly && !d->singleTurnRetain);
  }
  {  // single-turn Retain keeps a card in the hand at the end of the turn
    Fight f;
    Card* keep = f.c->hand[0];
    keep->singleTurnRetain = true;
    auto probe = std::make_unique<ProbeRelic>();
    probe->keep = keep;
    ProbeRelic* p = probe.get();
    f.r->relics.push_back(std::move(probe));
    f.endTurn();
    CHECK(p->keptInHand);
    Fight g;  // the same without Retain: flushed
    Card* drop = g.c->hand[0];
    auto probe2 = std::make_unique<ProbeRelic>();
    probe2->keep = drop;
    ProbeRelic* q = probe2.get();
    g.r->relics.push_back(std::move(probe2));
    g.endTurn();
    CHECK(!q->keptInHand);
  }
  {  // Shivs, Accuracy, Fan of Knives
    Fight f;
    std::vector<Card*> made;
    runTask(makeShivs(f.c, 2, &made));
    CHECK(made.size() == 2 && made[0]->id == "Shiv" && f.c->pileOf(made[0]) == Pile::Hand);
    CHECK((made[0]->tags & tagShiv) && made[0]->has(kwExhaust) && made[0]->cost == 0 && made[0]->target == TargetType::AnyEnemy);
    Creature* e0 = f.enemy(0);
    int hp0 = e0->hp;
    f.play(made[0], e0);
    CHECK(hp0 - e0->hp == 4);
    CHECK(f.c->pileOf(made[0]) == Pile::Exhaust);
    f.apply<AccuracyPower>(f.c->player, 3);
    hp0 = e0->hp;
    f.play(made[1], e0);
    CHECK(hp0 - e0->hp == 7);  // 4 + Accuracy 3
    // Strikes are not Shivs.
    Card* strike = f.find("StrikeIronclad");
    f.toHand(strike);
    hp0 = e0->hp;
    f.play(strike, e0);
    CHECK(hp0 - e0->hp == 6);
    // Fan of Knives: every enemy, also for Shivs created afterwards.
    std::vector<Card*> more, later;
    runTask(makeShivs(f.c, 1, &more));
    f.apply<FanOfKnivesPower>(f.c->player, 1);
    CHECK(more[0]->target == TargetType::AllEnemies);
    runTask(makeShivs(f.c, 1, &later));
    CHECK(later[0]->target == TargetType::AllEnemies);
    int a = f.enemy(0)->hp, b = f.enemy(1)->hp;
    f.play(more[0], nullptr);
    CHECK(a - f.enemy(0)->hp == 7 && b - f.enemy(1)->hp == 7);
    runTask(removeFan(f.c->player));
    CHECK(later[0]->target == TargetType::AnyEnemy);
  }
  {  // LoseBlock
    Fight f;
    f.c->player->block = 10;
    runTask(cmd::loseBlock(f.c->player, 4));
    CHECK(f.c->player->block == 6);
    runTask(cmd::loseBlock(f.c->player, 99));
    CHECK(f.c->player->block == 0);
  }
  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
