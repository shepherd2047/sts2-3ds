// E6: instanced powers (PowerInstanceType) and the ITemporaryPower marker.
// Build: make -f Makefile.sdl build/instanced_test ; run: ./build/instanced_test
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../source/core/cards.h"
#include "../source/core/powers.h"

using namespace sts;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    ++checks;                                                           \
    if (!(cond)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #cond); } \
  } while (0)

static Task<> fightTask(Run* r) { co_await r->fight("NibbitsNormal"); }
static Task<> wrap(Task<> t, bool* done) { co_await t; *done = true; }
static Task<> applyById(const char* id, Creature* target, int amount, Creature* applier) {
  co_await cmd::applyPower(db::power(id), target, Dec(amount), applier, nullptr);
}
static Task<> applyStrength(Creature* target, int amount, StrengthPower** out) {
  *out = co_await applyPowerGet<StrengthPower>(target, Dec(amount), target, nullptr);
}

static bool pump(const std::function<bool()>& done, int maxFrames = 4000) {
  for (int i = 0; i < maxFrames && !done(); ++i) Scheduler::get().update(0.05);
  return done();
}

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
  Card* fresh(const char* id, bool up = false) {
    Card* k = c->addCard(db::card(id));
    if (up) k->upgrade();
    c->removeFromPiles(k);
    c->hand.push_back(k);
    return k;
  }
  bool idle() { return c->over || (c->playerPhase && c->actions.waiting()); }
  bool choosing() { return c->choice.active && c->choice.result.waiting(); }
  void fire(Card* k, Creature* t) {
    PlayerAction a;
    a.kind = PlayerAction::PlayCard;
    a.card = k;
    a.target = t;
    c->energy = 10;
    c->actions.fire(a);
  }
  void play(Card* k, Creature* t) {
    fire(k, t);
    pump([&] { return idle() || choosing(); });
  }
  void run(Task<> t) {
    bool done = false;
    Scheduler::get().spawn(wrap(std::move(t), &done));
    pump([&] { return done; });
  }
  void endTurn() {
    PlayerAction a;
    a.kind = PlayerAction::EndTurn;
    int turn = c->turnNumber;
    c->actions.fire(a);
    pump([&] { return c->over || choosing() || (c->turnNumber > turn && c->playerPhase && c->actions.waiting()); });
  }
  int count(Pile p, const char* id) {
    int n = 0;
    for (Card* k : c->pile(p)) if (k->id == id) ++n;
    return n;
  }
};

int main() {
  db::init();
  {  // ITemporaryPower: the marker and the power applied under the hood.
    auto pw = db::power("PiercingWailPower");
    CHECK(pw && pw->isTemporary() && std::strcmp(pw->internallyAppliedPower(), "StrengthPower") == 0);
    auto hot = db::power("HotfixPower");
    CHECK(hot && std::strcmp(hot->internallyAppliedPower(), "FocusPower") == 0);
    auto ant = db::power("AnticipatePower");
    CHECK(ant && std::strcmp(ant->internallyAppliedPower(), "DexterityPower") == 0);
    auto flex = db::power("FlexPotionPower");
    CHECK(flex && std::strcmp(flex->internallyAppliedPower(), "StrengthPower") == 0);
    CHECK(!db::power("StrengthPower")->isTemporary());
    CHECK(!db::power("PlowPower") || !db::power("PlowPower")->isTemporary());
    CHECK(!db::power("VulnerablePower")->isTemporary());
  }
  {  // A non-instanced power stacks; applyPowerGet returns the stack it landed on.
    Fight f;
    StrengthPower* a = nullptr;
    StrengthPower* b = nullptr;
    f.run(applyStrength(f.c->player, 2, &a));
    f.run(applyStrength(f.c->player, 3, &b));
    CHECK(a && a == b && a->amount == 5);
    CHECK(f.c->player->powerInstances("StrengthPower").size() == 1);
    StrengthPower* gone = a;
    f.run(applyStrength(f.c->player, -5, &gone));
    CHECK(gone == nullptr && !f.c->player->get<StrengthPower>());
  }
  {  // Instanced: every Automation played is its own power with its own 10-card counter.
    Fight f;
    f.play(f.fresh("Automation"), nullptr);
    f.play(f.fresh("Automation"), nullptr);
    auto inst = f.c->player->powerInstances("AutomationPower");
    CHECK(inst.size() == 2 && inst[0] != inst[1] && inst[0]->amount == 1 && inst[1]->amount == 1);
    CHECK(inst[0]->displayAmount() == 10);
    f.c->draw.clear();
    for (int i = 0; i < 12; ++i) f.c->draw.push_back(f.c->addCard(db::card("Finesse")));
    for (Card* k : std::vector<Card*>(f.c->hand)) { f.c->removeFromPiles(k); f.c->discard.push_back(k); }
    f.c->energy = 0;
    bool done = false;
    Scheduler::get().spawn(wrap([](Combat* c) -> Task<> { co_await cmd::drawCards(*c, 10); }(f.c), &done));
    pump([&] { return done; });
    CHECK(f.c->energy == 2);  // both counters reach 0 on the 10th card
  }
  {  // Instanced: two Bombs tick separately, each with its own damage.
    Fight f;
    f.play(f.fresh("TheBomb"), nullptr);
    f.endTurn();
    f.play(f.fresh("TheBomb", true), nullptr);
    auto inst = f.c->player->powerInstances("TheBombPower");
    CHECK(inst.size() == 2 && inst[0]->amount == 2 && inst[1]->amount == 3);
    Creature* e = f.enemy(0);
    f.endTurn();
    f.endTurn();
    int hp = e->hp;
    CHECK(f.c->player->powerInstances("TheBombPower").size() == 1);  // the first one went off
    f.endTurn();
    CHECK(hp - e->hp >= 50);  // the upgraded one: 50 damage
    CHECK(f.c->player->powerInstances("TheBombPower").empty());
  }
  {  // InstancedPerApplier: the same applier stacks, another applier gets its own instance.
    Fight f;
    Creature* e = f.enemy(0);
    f.run(applyById("OblivionPower", e, 2, f.c->player));
    f.run(applyById("OblivionPower", e, 3, f.c->player));
    CHECK(e->powerInstances("OblivionPower").size() == 1 && e->power("OblivionPower")->amount == 5);
    f.run(applyById("OblivionPower", e, 4, e));
    auto inst = e->powerInstances("OblivionPower");
    CHECK(inst.size() == 2 && inst[1]->applier == e && inst[1]->amount == 4);
  }
  {  // Nightmare twice: each instance keeps its own card.
    Fight f;
    Card* first = f.fresh("Finesse");
    f.play(f.fresh("Nightmare"), nullptr);
    CHECK(f.choosing());
    f.c->choice.result.fire({first});
    pump([&] { return f.idle(); });
    Card* second = f.fresh("TheBomb");
    f.play(f.fresh("Nightmare"), nullptr);
    CHECK(f.choosing());
    f.c->choice.result.fire({second});
    pump([&] { return f.idle(); });
    CHECK(f.c->player->powerInstances("NightmarePower").size() == 2);
    for (Card* k : std::vector<Card*>(f.c->hand)) { f.c->removeFromPiles(k); f.c->exhaust.push_back(k); }
    f.endTurn();
    CHECK(f.count(Pile::Hand, "Finesse") >= 3 && f.count(Pile::Hand, "TheBomb") >= 3);
    CHECK(f.c->player->powerInstances("NightmarePower").empty());
  }
  {  // Toric Toughness twice: two instances, each with the block of its own play.
    Fight f;
    f.play(f.fresh("ToricToughness"), nullptr);
    f.play(f.fresh("ToricToughness", true), nullptr);
    CHECK(f.c->player->powerInstances("ToricToughnessPower").size() == 2);
    f.endTurn();
    CHECK(f.c->player->block == 5 + 7);
  }
  {  // Orbit: its own energy counter, shown as energy left to the next trigger.
    Fight f;
    f.play(f.fresh("Orbit"), nullptr);
    Power* o = f.c->player->power("OrbitPower");
    CHECK(o && o->displayAmount() == 4);
  }
  printf("instanced_test: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
