// CombatHistory checks (E3): the log's entries at each command site, HappenedThisTurn /
// HappenedLastPlayerTurn, and cards that read it (Tear Asunder, Spite, Evil Eye, Stomp, Midnight,
// FTL, Death March, Juggling, Unmovable, Lethality).
// Build: make -f Makefile.sdl build/combat_history_test ; run: ./build/combat_history_test
#include <cstdio>
#include <cstdlib>

#include "../source/core/game.h"

using namespace sts;
using E = CombatHistoryEntry;

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
  std::vector<Card*> cards(const std::string& id) {
    std::vector<Card*> out;
    for (Card* k : c->allCards()) if (k->id == id) out.push_back(k);
    return out;
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
  void hurtPlayer(int amount) {
    bool done = false;
    run([](Creature* t, Creature* from, int a, bool* d) -> Task<> {
      co_await cmd::damage(t, Dec(a), kUnpowered, from, nullptr);
      *d = true;
    }(c->player, enemy(), amount, &done), &done);
  }
  void draw(int n) {
    bool done = false;
    run([](Combat* cb, int k, bool* d) -> Task<> { co_await cmd::drawCards(*cb, k); *d = true; }(c, n, &done), &done);
  }
  void exhaust(Card* k) {
    bool done = false;
    run([](Combat* cb, Card* x, bool* d) -> Task<> { co_await cmd::exhaustCard(*cb, x); *d = true; }(c, k, &done), &done);
  }
  void discard(Card* k) {
    bool done = false;
    run([](Combat* cb, Card* x, bool* d) -> Task<> { co_await cmd::discardCard(*cb, x); *d = true; }(c, k, &done), &done);
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
  int count(E::Kind k) { return c->history.count(k); }
  int thisTurn(E::Kind k) { return c->history.countThisTurn(*c, k); }
  const E* last(E::Kind k) {
    for (auto it = c->history.entries.rbegin(); it != c->history.entries.rend(); ++it)
      if (it->kind == k) return &*it;
    return nullptr;
  }
};

int main() {
  db::init();
  {  // entries at the command sites
    Fight f(11);
    auto S = f.cards("StrikeIronclad"), D = f.cards("DefendIronclad");
    // the opening hand draw
    CHECK(f.thisTurn(E::CardDrawn) == (int)f.c->hand.size());
    CHECK(f.last(E::CardDrawn) && f.last(E::CardDrawn)->flag);  // FromHandDraw
    f.draw(1);
    CHECK(f.last(E::CardDrawn) && !f.last(E::CardDrawn)->flag);
    // a Strike: one play started + finished (same play), energy, the attack and its damage
    Card* strike = S[0];
    int hp = f.enemy()->hp;
    f.play(strike);
    const E* started = f.last(E::CardPlayStarted);
    const E* finished = f.last(E::CardPlayFinished);
    CHECK(started && finished && started->card == strike && finished->card == strike);
    CHECK(started && finished && started->playSeq != 0 && started->playSeq == finished->playSeq);
    CHECK(started && started->other == f.enemy() && started->amount == 1);  // target, energy spent
    CHECK(f.last(E::EnergySpent) && f.last(E::EnergySpent)->amount == 1);
    const E* attacked = f.last(E::CreatureAttacked);
    CHECK(attacked && attacked->actor == f.c->player && attacked->hits == 1 && attacked->amount == hp - f.enemy()->hp);
    const E* hit = f.last(E::DamageReceived);
    CHECK(hit && hit->actor == f.enemy() && hit->other == f.c->player && hit->card == strike && hit->unblocked == 6);
    // a Defend: BlockGained carries the play
    Card* defend = D[0];
    f.play(defend);
    const E* block = f.last(E::BlockGained);
    CHECK(block && block->actor == f.c->player && block->amount == 5 && block->playSeq == f.last(E::CardPlayStarted)->playSeq);
    // powers, exhaust, discard, generated cards
    f.apply("StrengthPower", f.c->player, 2, f.c->player);
    f.apply("StrengthPower", f.c->player, 1, f.c->player);  // stacks: ModifyAmount entry
    CHECK(f.count(E::PowerReceived) == 2 && f.last(E::PowerReceived)->id == "StrengthPower" && f.last(E::PowerReceived)->amount == 1);
    f.exhaust(f.c->hand[0]);
    CHECK(f.count(E::CardExhausted) == 1);
    f.discard(f.c->hand[0]);
    CHECK(f.c->discardsThisTurn() == 1 && f.count(E::CardDiscarded) == 1);
    Card* made = f.generate("Anger");
    CHECK(f.last(E::CardGenerated) && f.last(E::CardGenerated)->card == made);
    // next turn: this turn's entries become last turn's; the monsters' moves are logged
    int playsBefore = f.thisTurn(E::CardPlayStarted);
    CHECK(playsBefore == 2);
    f.endTurn();
    CHECK(f.thisTurn(E::CardPlayStarted) == 0 && f.count(E::CardPlayStarted) == playsBefore);
    CHECK(f.count(E::MonsterPerformedMove) >= 1);
    int lastTurn = f.c->history.count([&](const E& e) {
      return e.kind == E::CardPlayFinished && CombatHistory::happenedLastPlayerTurn(e, *f.c);
    });
    CHECK(lastTurn == 2);
  }
  {  // Tear Asunder: 1 + hits that got through this combat; Spite: lost HP this turn
    Fight f(12);
    auto S = f.cards("StrikeIronclad"), D = f.cards("DefendIronclad");
    Card* tear = f.generate("TearAsunder");
    Card* spite = f.generate("Spite");
    CHECK(tear && spite);
    CHECK(tear->calculatedBlock().toInt() == 1);
    f.c->player->block = 0;
    f.hurtPlayer(3);
    f.hurtPlayer(2);
    CHECK(tear->calculatedBlock().toInt() == 3);
    int hp = f.enemy()->hp;
    f.play(spite);  // 5 x 2 (Repeat) since HP was lost this turn
    CHECK(hp - f.enemy()->hp == 10);
    f.endTurn();
    Card* spite2 = f.generate("Spite");
    hp = f.enemy()->hp;
    f.c->player->block = 999;  // nothing gets through
    f.enemy()->block = 0;
    f.play(spite2);
    int dealt = hp - f.enemy()->hp;
    CHECK(dealt == 5);
  }
  {  // Evil Eye: double block once a card was exhausted this turn (also for a fresh copy)
    Fight f(13);
    auto S = f.cards("StrikeIronclad"), D = f.cards("DefendIronclad");
    f.exhaust(f.c->hand[0]);
    Card* eye = f.generate("EvilEye");
    f.c->player->block = 0;
    f.play(eye);
    CHECK(f.c->player->block == 16);
  }
  {  // Stomp / Midnight entering combat late: the reductions so far
    Fight f(14);
    auto S = f.cards("StrikeIronclad"), D = f.cards("DefendIronclad");
    f.play(S[0]);
    f.play(S[1]);
    Card* stomp = f.generate("Stomp");
    CHECK(f.c->energyCost(stomp) == 1);
    f.exhaust(D[0]);
    f.exhaust(D[1]);
    f.exhaust(D[2]);
    Card* midnight = f.generate("Midnight");
    CHECK(f.c->energyCost(midnight) == 9);
  }
  {  // FTL: draws while fewer than PlayMax cards finished this turn; Death March: off-turn draws
    Fight f(15);
    auto S = f.cards("StrikeIronclad"), D = f.cards("DefendIronclad");
    Card* march = f.generate("DeathMarch");
    CHECK(march->calculatedDamage().toInt() == 8);
    f.draw(2);
    CHECK(march->calculatedDamage().toInt() == 16);
    f.play(S[0]);
    f.play(S[1]);
    Card* ftl = f.generate("Ftl");
    int hand = (int)f.c->hand.size();
    f.play(ftl);  // 2 finished before it: draws
    CHECK((int)f.c->hand.size() == hand);  // -FTL +1 card
    f.play(S[2]);
    hand = (int)f.c->hand.size();
    Card* ftl2 = f.generate("Ftl");
    f.play(ftl2);  // 4 finished: no draw
    CHECK((int)f.c->hand.size() == hand);
  }
  {  // Juggling seeded from this turn's Attack plays; Unmovable counts card plays that gained block
    Fight f(16);
    auto S = f.cards("StrikeIronclad"), D = f.cards("DefendIronclad");
    f.play(S[0]);
    f.play(S[1]);
    f.apply("JugglingPower", f.c->player, 1, f.c->player);
    int hand = (int)f.c->hand.size();
    Card* third = S[2];
    bool inHand = f.c->pileOf(third) == Pile::Hand;
    f.play(third);  // the third Attack this turn: a copy goes to the hand
    CHECK((int)f.c->hand.size() == hand + (inHand ? 0 : 1));
    f.c->player->block = 0;
    f.apply("UnmovablePower", f.c->player, 1, f.c->player);
    f.play(D[0]);
    CHECK(f.c->player->block == 10);  // first block from a card play this turn: doubled
    f.play(D[1]);
    CHECK(f.c->player->block == 15);
  }
  {  // Lethality: only the first Attack play of the turn gets the bonus
    Fight f(17);
    auto S = f.cards("StrikeIronclad"), D = f.cards("DefendIronclad");
    f.apply("LethalityPower", f.c->player, 50, f.c->player);
    int hp = f.enemy()->hp;
    f.play(S[0]);
    CHECK(hp - f.enemy()->hp == 9);
    hp = f.enemy()->hp;
    f.play(S[1]);
    CHECK(hp - f.enemy()->hp == 6);
  }
  printf("combat_history_test: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
