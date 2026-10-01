// Hook checks (E7): damage cap (Hard to Kill, Intangible), BeforeDamageReceived (Thorns),
// AfterBlockBroken (Hand Drill), Before/AfterAttack (Vigor), AfterModifyingCardPlayResultLocation
// (Rebound), extra turns (Ambergris, Pael's Eye) and ShouldAllowHitting (Reattach).
// Build: make -f Makefile.sdl build/hooks_test ; run: ./build/hooks_test
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
    c->player->hp = c->player->maxHp = 500;
  }
  ~Fight() { Scheduler::get().clear(); }
  Card* card(const std::string& id, int nth = 0) {
    for (Card* k : c->allCards()) if (k->id == id && nth-- == 0) return k;
    return nullptr;
  }
  void toHand(Card* k) { if (c->pileOf(k) != Pile::Hand) { c->removeFromPiles(k); c->hand.push_back(k); } }
  Creature* enemy() { return c->enemies[0]; }
  void idle() { pump([&] { return c->playerPhase && c->actions.waiting(); }); }
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
  void damage(Creature* target, int amount, int props, Creature* dealer) {
    bool done = false;
    run([](Creature* t, int a, int p, Creature* dl, bool* d) -> Task<> {
      co_await cmd::damage(t, Dec(a), p, dl, nullptr);
      *d = true;
    }(target, amount, props, dealer, &done), &done);
  }
  void relic(const char* id) {
    bool done = false;
    run([](Run* rr, std::string rid, bool* d) -> Task<> {
      co_await rr->obtainRelic(db::relic(rid));
      *d = true;
    }(r.get(), id, &done), &done);
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

int main() {
  db::init();
  {  // Hook.ModifyDamageCap: Hard to Kill caps the damage before block is taken.
    Fight f(11);
    Creature* e = f.enemy();
    f.apply("HardToKillPower", e, 5, e);
    e->block = 3;
    CHECK(f.c->modifyDamage(e, f.c->player, Dec(20), kMove, nullptr) == Dec(5));
    f.damage(e, 20, kMove, f.c->player);
    CHECK(e->block == 0);
    CHECK(e->hp == 498);  // 5 capped, 3 blocked
  }
  {  // Intangible: the cap is 1 (block untouched beyond 1), and the HP loss is 1.
    Fight f(12);
    Creature* e = f.enemy();
    f.apply("IntangiblePower", e, 1, e);
    e->block = 4;
    f.damage(e, 30, kMove, f.c->player);
    CHECK(e->block == 3 && e->hp == 500);
    f.damage(e, 30, kMove | kUnblockable, f.c->player);
    CHECK(e->hp == 499);
  }
  {  // Hook.BeforeDamageReceived: Thorns hits back before block, even through the defender's block.
    Fight f(13);
    Creature* e = f.enemy();
    f.apply("ThornsPower", e, 3, e);
    e->block = 50;
    f.damage(e, 6, kMove, f.c->player);
    CHECK(f.c->player->hp == 497);
    f.damage(e, 6, kMove | kUnpowered, f.c->player);  // unpowered: no thorns
    CHECK(f.c->player->hp == 497);
  }
  {  // Hook.AfterBlockBroken: Hand Drill applies Vulnerable when the player breaks an enemy's block.
    Fight f(14);
    f.relic("HandDrill");
    Creature* e = f.enemy();
    e->block = 2;
    f.damage(e, 1, kMove, f.c->player);
    CHECK(!e->power("VulnerablePower"));  // block left
    f.damage(e, 5, kMove, f.c->player);
    CHECK(e->power("VulnerablePower") && e->power("VulnerablePower")->amount == 2);
  }
  {  // Hook.BeforeAttack / AfterAttack: Vigor adds to the pinned attack, then loses its amount.
    Fight f(15);
    Creature* e = f.enemy();
    f.apply("VigorPower", f.c->player, 4, f.c->player);
    Card* strike = f.card("StrikeIronclad");
    CHECK(strike != nullptr);
    if (strike) {
      int before = e->hp;
      f.play(strike);
      CHECK(before - e->hp == 6 + 4);
      CHECK(!f.c->player->power("VigorPower"));
    }
  }
  {  // AfterModifyingCardPlayResultLocation: Rebound sends a Strike to the top of the draw pile and decrements.
    Fight f(16);
    f.apply("ReboundPower", f.c->player, 1, f.c->player);
    Card* strike = f.card("StrikeIronclad");
    if (strike) {
      f.play(strike);
      CHECK(!f.c->draw.empty() && f.c->draw.front() == strike);
      CHECK(!f.c->player->power("ReboundPower"));
    }
  }
  {  // Hook.ShouldTakeExtraTurn: Ambergris gives one more player turn in the same round.
    Fight f(17);
    f.apply("AmbergrisPower", f.c->player, 1, f.c->player);
    int round = f.c->roundNumber, turn = f.c->turnNumber, hp = f.c->player->hp;
    f.endTurn();
    CHECK(f.c->currentSide == Side::Player);
    CHECK(f.c->roundNumber == round && f.c->turnNumber == turn + 1);
    CHECK(f.c->player->hp == hp);  // the enemies did not act
    CHECK(!f.c->player->power("AmbergrisPower"));
    CHECK(f.c->extraTurn);
    f.endTurn();  // a normal turn switch: the enemies act, a new round starts
    CHECK(!f.c->extraTurn && f.c->roundNumber == round + 1 && f.c->turnNumber == turn + 2);
  }
  {  // Pael's Eye: a turn without a card played exhausts the hand and takes another turn, once per combat.
    Fight f(18);
    f.relic("PaelsEye");
    int round = f.c->roundNumber;
    size_t handSize = f.c->hand.size();
    size_t exhausted = f.c->exhaust.size();
    f.endTurn();
    CHECK(f.c->roundNumber == round);  // extra turn
    CHECK(f.c->exhaust.size() == exhausted + handSize);
    f.endTurn();
    CHECK(f.c->roundNumber == round + 1);  // used up this combat
  }
  {  // ShouldAllowHitting: a dead Decimillipede segment that is reattaching can't receive powers.
    Fight f(19);
    Creature* e = f.enemy();
    f.apply("ReattachPower", e, 1, e);
    e->hp = 0;  // dead but still in the room
    f.apply("WeakPower", e, 1, f.c->player);
    CHECK(!e->power("WeakPower"));
    CHECK(!f.c->canReceivePowers(e));
    e->hp = 10;
    CHECK(f.c->canReceivePowers(e));
  }
  printf("hooks_test: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
