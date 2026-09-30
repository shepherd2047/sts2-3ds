// Affliction engine checks (A4): CanAfflict / stacking / clones / clearing, and each
// affliction with the power that hands it out (Hex, Tangled, Ringing, Galvanic, VitalSpark,
// Smoggy, ChainsOfBinding).
// Build: make -f Makefile.sdl build/afflict_test ; run: ./build/afflict_test
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

static Task<> fightTask(Run* r) { co_await r->fight("NibbitsNormal"); }

static bool pump(const std::function<bool()>& done, int maxFrames = 4000) {
  for (int i = 0; i < maxFrames && !done(); ++i) Scheduler::get().update(0.05);
  return done();
}

// A run in the first turn of a fight against the Nibbits.
struct Fight {
  std::unique_ptr<Run> r = std::make_unique<Run>();
  Combat* c = nullptr;
  explicit Fight(uint64_t seed) {
    r->start(seed);
    Scheduler::get().spawn(fightTask(r.get()));
    pump([&] { return r->combat && r->combat->playerPhase && r->combat->actions.waiting(); });
    c = r->combat.get();
    for (Creature* e : c->enemies) e->hp = e->maxHp = 500;  // nothing dies during a check
  }
  ~Fight() { Scheduler::get().clear(); }
  Card* card(const std::string& id, int nth = 0) {
    for (Card* k : c->allCards()) if (k->id == id && nth-- == 0) return k;
    return nullptr;
  }
  void toHand(Card* k) { if (c->pileOf(k) != Pile::Hand) { c->removeFromPiles(k); c->hand.push_back(k); } }
  Creature* enemy() { return c->enemies[0]; }
  void idle() { pump([&] { return c->playerPhase && c->actions.waiting(); }); }
  // Runs a command to completion (the player phase stays open while it runs).
  void run(Task<> t, bool* done) {
    Scheduler::get().spawn(std::move(t));
    pump([&] { return *done; });
    idle();
  }
  void apply(const char* powerId, Creature* target, int amount, Creature* applier) {
    bool done = false;
    run([](std::string id, Creature* t, int a, Creature* ap, bool* d) -> Task<> {
      co_await cmd::applyPower(db::power(id), t, Dec(a), ap, nullptr);
      *d = true;
    }(powerId, target, amount, applier, &done), &done);
  }
  void remove(Power* p) {
    bool done = false;
    run([](Power* pw, bool* d) -> Task<> { co_await cmd::removePower(pw); *d = true; }(p, &done), &done);
  }
  void draw(int n) {
    bool done = false;
    run([](Combat* cb, int k, bool* d) -> Task<> { co_await cmd::drawCards(*cb, k); *d = true; }(c, n, &done), &done);
  }
  Card* generate(const char* id, Pile to = Pile::Hand) {
    Card* out = nullptr;
    bool done = false;
    run([](Combat* cb, std::string cid, Pile p, Card** o, bool* d) -> Task<> {
      *o = co_await cmd::addGeneratedCard(*cb, db::card(cid), p);
      *d = true;
    }(c, id, to, &out, &done), &done);
    return out;
  }
  void play(Card* k) {
    toHand(k);
    PlayerAction a;
    a.kind = PlayerAction::PlayCard;
    a.card = k;
    a.target = k->target == TargetType::AnyEnemy ? enemy() : nullptr;
    c->energy = 10;
    c->actions.fire(a);
    idle();
  }
  void endTurn() {
    int t = c->turnNumber;
    PlayerAction a;
    a.kind = PlayerAction::EndTurn;
    c->actions.fire(a);
    pump([&] { return c->turnNumber > t && c->playerPhase && c->actions.waiting(); });
  }
};

static int countAfflicted(Combat* c, const char* id) {
  int n = 0;
  for (Card* k : c->allCards()) if (k->afflictedWith(id)) ++n;
  return n;
}

int main() {
  db::init();
  CHECK(db::afflictionIds().size() == 7);
  {  // CanAfflict, stacking, clones, clearing, combat-only
    Fight f(7);
    Card* strike = f.card("StrikeIronclad");
    Card* defend = f.card("DefendIronclad");
    CHECK(strike && defend);
    CHECK(!db::affliction("Tainted")->canAfflict(*strike));  // Skills only
    CHECK(db::affliction("Tainted")->canAfflict(*defend));
    CHECK(cmd::afflict(defend, "Tainted", 2) != nullptr);
    CHECK(defend->affliction->card == defend && defend->affliction->amount == 2);
    CHECK(cmd::afflict(defend, "Tainted", 1) != nullptr);  // stackable: amounts add up
    CHECK(defend->affliction->amount == 3);
    CHECK(cmd::afflict(defend, "Hexed", 1) == nullptr);  // one affliction per card
    CHECK(cmd::afflict(strike, "Bound", 3) != nullptr);
    CHECK(cmd::afflict(strike, "Bound", 3) == nullptr);  // not stackable
    CHECK(strike->affliction->amount == 3);
    CHECK(f.c->afflictionsThisTurn("Bound") == 1 && f.c->afflictionsThisTurn("Tainted") == 2);
    auto twin = strike->clone();  // CardModel.DeepCloneFields keeps the affliction
    CHECK(twin->affliction && twin->affliction.get() != strike->affliction.get());
    CHECK(twin->affliction->card == twin.get() && strike->affliction->card == strike);
    cmd::clearAffliction(twin.get());
    CHECK(!twin->affliction && strike->afflictedWith("Bound"));
    cmd::clearAffliction(strike);
    CHECK(!strike->affliction);
    Card* deck = f.r->deck[0].get();  // afflictions never go on deck cards
    CHECK(cmd::afflict(deck, "Bound", 1) == nullptr && !deck->affliction);
    Card* curse = f.generate("Injury");
    if (curse) CHECK(db::affliction("Hexed")->canAfflict(*curse));  // CanAfflictUnplayableCards
  }
  {  // HexPower: every card Hexed and Ethereal while it lasts; Ethereal cards exhaust at turn end
    Fight f(3);
    Card* defend = f.card("DefendIronclad");
    cmd::afflict(defend, "Tainted", 1);
    f.apply("HexPower", f.c->player, 2, f.enemy());
    Card* strike = f.card("StrikeIronclad");
    CHECK(strike->afflictedWith("Hexed") && strike->affliction->amount == 2);
    CHECK(strike->has(kwEthereal) && !(strike->keywords & kwEthereal));
    CHECK(defend->afflictedWith("Tainted") && !defend->has(kwEthereal));  // already afflicted
    Card* made = f.generate("DefendIronclad");  // AfterCardEnteredCombat
    CHECK(made && made->afflictedWith("Hexed") && made->has(kwEthereal));
    auto copy = strike->clone();
    Power* hex = f.c->player->power("HexPower");
    CHECK(hex != nullptr);
    f.remove(hex);
    CHECK(countAfflicted(f.c, "Hexed") == 0 && !strike->has(kwEthereal));
    CHECK(copy->afflictedWith("Hexed") && !copy->has(kwEthereal));  // the gate is the power
    f.apply("HexPower", f.c->player, 1, f.enemy());
    int handHexed = 0;
    for (Card* k : f.c->hand) if (k->afflictedWith("Hexed")) ++handHexed;
    int exhaustBefore = (int)f.c->exhaust.size();
    f.endTurn();
    CHECK((int)f.c->exhaust.size() >= exhaustBefore + handHexed);
  }
  {  // TangledPower: Attacks Entangled, +Amount cost; gone (with the afflictions) at turn end
    Fight f(4);
    Card* strike = f.card("StrikeIronclad");
    Card* other = f.card("StrikeIronclad", 1);
    cmd::afflict(other, "Bound", 1);
    f.apply("TangledPower", f.c->player, 1, f.enemy());
    CHECK(strike->afflictedWith("Entangled") && f.c->energyCost(strike) == 2);
    CHECK(other->afflictedWith("Bound") && f.c->energyCost(other) == 1);  // not Entangled
    CHECK(!f.card("DefendIronclad")->affliction);
    f.endTurn();
    CHECK(!f.c->player->power("TangledPower") && countAfflicted(f.c, "Entangled") == 0);
    CHECK(f.c->energyCost(strike) == 1);
  }
  {  // RingingPower: after one card, Ringing cards can't be played; cleared at turn end
    Fight f(5);
    f.apply("RingingPower", f.c->player, 1, f.enemy());
    CHECK(countAfflicted(f.c, "Ringing") == (int)f.c->allCards().size());
    Card* a = f.c->hand[0];
    Card* b = f.c->hand[1];
    CHECK(f.c->canPlay(b));
    f.play(a);
    CHECK(!f.c->canPlay(b));
    Card* free = f.generate("DefendIronclad");  // enters combat: Ringing too
    CHECK(free->afflictedWith("Ringing") && !f.c->canPlay(free));
    f.endTurn();
    CHECK(countAfflicted(f.c, "Ringing") == 0 && !f.c->player->power("RingingPower"));
  }
  {  // GalvanicPower: Power cards Galvanized; playing one hurts the player
    Fight f(6);
    f.apply("GalvanicPower", f.enemy(), 3, f.enemy());
    Card* inflame = f.generate("Inflame");
    CHECK(inflame && inflame->afflictedWith("Galvanized") && inflame->affliction->amount == 3);
    CHECK(!f.card("StrikeIronclad")->affliction);
    f.c->player->block = 0;
    int hp = f.c->player->hp;
    f.play(inflame);
    CHECK(f.c->player->hp == hp - 3);
  }
  {  // VitalSparkPower: Skills Tainted (amount follows the power), TaintedPower when played
    Fight f(8);
    f.apply("VitalSparkPower", f.enemy(), 2, f.enemy());
    Card* defend = f.generate("DefendIronclad");
    CHECK(defend && defend->afflictedWith("Tainted") && defend->affliction->amount == 2);
    CHECK(!f.card("StrikeIronclad")->affliction);
    f.apply("VitalSparkPower", f.enemy(), 2, f.enemy());  // stacks to 4
    CHECK(defend->affliction->amount == 4);
    f.play(defend);
    Power* t = f.c->player->power("TaintedPower");
    CHECK(t && t->amount == 4);
    Power* vs = f.enemy()->power("VitalSparkPower");
    f.remove(vs);
    CHECK(countAfflicted(f.c, "Tainted") == 0);
  }
  {  // SmoggyPower: after a Skill, every Skill is Smogged (unplayable) until the turn ends
    Fight f(9);
    f.apply("SmoggyPower", f.c->player, 1, f.enemy());
    CHECK(countAfflicted(f.c, "Smog") == 0);
    Card* d0 = f.card("DefendIronclad", 0);
    Card* d1 = f.card("DefendIronclad", 1);
    f.play(d0);
    CHECK(d1->afflictedWith("Smog") && !f.card("StrikeIronclad")->affliction);
    f.toHand(d1);
    CHECK(!f.c->canPlay(d1));
    Card* made = f.generate("DefendIronclad");
    CHECK(made->afflictedWith("Smog"));
    f.endTurn();
    CHECK(countAfflicted(f.c, "Smog") == 0);
  }
  {  // ChainsOfBindingPower: up to Amount drawn cards Bound per turn; one Bound card a turn
    Fight f(10);
    f.apply("ChainsOfBindingPower", f.c->player, 2, f.enemy());
    f.draw(3);
    CHECK(countAfflicted(f.c, "Bound") == 2);
    std::vector<Card*> bound;
    for (Card* k : f.c->hand) if (k->afflictedWith("Bound")) bound.push_back(k);
    CHECK(bound.size() == 2);
    if (bound.size() == 2) {
      CHECK(bound[0]->affliction->amount == 2);
      CHECK(f.c->canPlay(bound[1]));
      f.play(bound[0]);
      CHECK(!f.c->canPlay(bound[1]));
    }
    f.endTurn();
    // cleared at the end of the turn; the new hand's first 2 draws are Bound again
    CHECK(countAfflicted(f.c, "Bound") == 2);
  }
  printf("afflict_test: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
