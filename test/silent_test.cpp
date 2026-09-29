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

// The first turn of a fight against the Nibbits, both with 500 HP. `extraRelics` are added
// before the fight starts (so turn-1-only hooks like RingOfTheSnake / NinjaScroll / TwistedFunnel
// see them) and get their `combat` pointer set the same way Run::fight sets it for the rest.
struct Fight {
  std::unique_ptr<Run> r = std::make_unique<Run>();
  Combat* c = nullptr;
  Fight(const std::string& charId = "Ironclad", const std::vector<std::string>& extraRelics = {}) {
    r->start(3, charId);
    for (auto& id : extraRelics) {
      if (auto rel = db::relic(id)) { rel->run = r.get(); r->relics.push_back(std::move(rel)); }
    }
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
  {  // X1.1: starter deck + RingOfTheSnake (draw 2 extra cards, turn 1 only)
    Fight f("Silent");
    CHECK(f.c->hand.size() == 7);  // 5 + RingOfTheSnake's 2
    for (Card* k : f.c->hand)
      CHECK(k->id == "StrikeSilent" || k->id == "DefendSilent" || k->id == "Neutralize" || k->id == "Survivor");
    f.endTurn();
    CHECK(f.c->hand.size() == 5);  // no bonus after turn 1
  }
  {  // Neutralize: 3 damage + 1 Weak
    Fight f("Silent");
    Card* k = f.find("Neutralize");
    f.toHand(k);
    Creature* e = f.enemy(0);
    int hp = e->hp;
    f.play(k, e);
    CHECK(hp - e->hp == 3);
    CHECK(e->powerAmount<WeakPower>() == 1);
  }
  {  // Survivor: 8 block, then discard a chosen card from hand
    Fight f("Silent");
    Card* survivor = f.find("Survivor");
    f.toHand(survivor);
    Card* other = f.c->hand[0] == survivor ? f.c->hand[1] : f.c->hand[0];
    int block = f.c->player->block;
    PlayerAction a;
    a.kind = PlayerAction::PlayCard;
    a.card = survivor;
    f.c->energy = 10;
    f.c->actions.fire(a);
    pump([&] { return f.c->choice.active && f.c->choice.result.waiting(); });
    CHECK(f.c->choice.active && f.c->choice.minCount == 1 && f.c->choice.maxCount == 1);
    f.c->choice.result.fire({other});
    pump([&] { return f.c->playerPhase && f.c->actions.waiting(); });
    CHECK(f.c->player->block - block == 8);
    CHECK(f.c->pileOf(other) == Pile::Discard);
  }
  {  // SneckoSkull: Poison the owner applies is increased by the relic's amount, fresh and stacking
    Fight f("Silent", {"SneckoSkull"});
    Creature* e = f.enemy(0);
    f.apply<PoisonPower>(e, 3);
    CHECK(e->powerAmount<PoisonPower>() == 4);  // 3 + 1
    f.apply<PoisonPower>(e, 2);
    CHECK(e->powerAmount<PoisonPower>() == 7);  // 4 + (2 + 1)
  }
  {  // HelicalDart: playing a Shiv gives Dexterity for the rest of the turn only
    Fight f("Silent", {"HelicalDart"});
    std::vector<Card*> made;
    runTask(makeShivs(f.c, 1, &made));
    f.play(made[0], f.enemy(0));
    CHECK(f.c->player->powerAmount<DexterityPower>() == 1);
    f.endTurn();
    CHECK(f.c->player->powerAmount<DexterityPower>() == 0);
  }
  {  // Tingsha + ToughBandages: discarding on your own turn hits a random enemy / gives block
    Fight f("Silent", {"Tingsha", "ToughBandages"});
    Card* k = f.c->hand[0];
    int hpSum = f.enemy(0)->hp + f.enemy(1)->hp;
    int block = f.c->player->block;
    runTask(cmd::discardCard(*f.c, k));
    CHECK(f.enemy(0)->hp + f.enemy(1)->hp == hpSum - 3);
    CHECK(f.c->player->block - block == 3);
  }
  {  // TwistedFunnel: Poisons every enemy at the start of turn 1, not turn 2
    Fight f("Silent", {"TwistedFunnel"});
    CHECK(f.enemy(0)->powerAmount<PoisonPower>() == 4 && f.enemy(1)->powerAmount<PoisonPower>() == 4);
    f.endTurn();
    f.endTurn();
    CHECK(f.enemy(0)->powerAmount<PoisonPower>() == 2);  // ticked down twice, not re-applied
  }
  {  // NinjaScroll: 3 Shivs in hand before the turn-1 draw, not turn 2
    Fight f("Silent", {"NinjaScroll"});
    int shivs = 0;
    for (Card* k : f.c->hand) if (k->id == "Shiv") ++shivs;
    CHECK(shivs == 3);
    f.endTurn();
    shivs = 0;
    for (Card* k : f.c->hand) if (k->id == "Shiv") ++shivs;
    CHECK(shivs == 0);
  }
  {  // PaperKrane (held by the target): a Weak enemy's attack on the owner deals 40% less, not 25%
    Fight f("Silent", {"PaperKrane"});
    Creature* e = f.enemy(0);
    f.apply<WeakPower>(e, 1);
    f.c->player->block = 0;
    int hp = f.c->player->hp;
    runTask([](Creature* t, Creature* by) -> Task<> { co_await cmd::damage(t, Dec(10), kMove, by, nullptr); }(f.c->player, e));
    CHECK(hp - f.c->player->hp == 6);  // 10 * (0.75 - 0.15)
    f.apply<WeakPower>(f.c->player, 1);  // the owner's own Weak is unchanged (25%)
    Card* strike = f.find("StrikeSilent");
    f.toHand(strike);
    int ehp = e->hp;
    f.play(strike, e);
    CHECK(ehp - e->hp == 4);  // 6 * 0.75 = 4.5 -> 4
  }
  {  // Potions (Silent4Epoch): PoisonPotion, GhostInAJar (Intangible), CunningPotion (upgraded Shivs)
    Fight f("Silent");
    Creature* e = f.enemy(0);
    auto poison = db::potion("PoisonPotion");
    poison->run = f.r.get();
    poison->combat = f.c;
    runTask(poison->onUse(e));
    CHECK(e->powerAmount<PoisonPower>() == 6);

    auto ghost = db::potion("GhostInAJar");
    ghost->run = f.r.get();
    ghost->combat = f.c;
    runTask(ghost->onUse(f.c->player));
    CHECK(f.c->player->power("IntangiblePower") != nullptr);

    auto cunning = db::potion("CunningPotion");
    cunning->run = f.r.get();
    cunning->combat = f.c;
    size_t before = f.c->hand.size();
    runTask(cunning->onUse(f.c->player));
    int upgradedShivs = 0;
    for (size_t i = before; i < f.c->hand.size(); ++i) if (f.c->hand[i]->id == "Shiv" && f.c->hand[i]->upgraded()) ++upgradedShivs;
    CHECK(upgradedShivs == 3);
  }
  {  // X1.2 Anticipate: Dexterity for the rest of the turn only, reversed at end of turn
    Fight f("Silent");
    Card* k = f.c->addCard(db::card("Anticipate"));
    f.toHand(k);
    f.play(k, nullptr);
    CHECK(f.c->player->powerAmount<DexterityPower>() == 2);
    f.endTurn();
    CHECK(f.c->player->powerAmount<DexterityPower>() == 0);
  }
  {  // X1.2 PiercingWail: every enemy loses Strength for the rest of the turn, restored after
    Fight f("Silent");
    Card* k = f.c->addCard(db::card("PiercingWail"));
    f.toHand(k);
    f.play(k, nullptr);
    CHECK(f.enemy(0)->powerAmount<StrengthPower>() == -6);
    CHECK(f.enemy(1)->powerAmount<StrengthPower>() == -6);
    CHECK(f.c->pileOf(k) == Pile::Exhaust);
    f.endTurn();
    // enemy(0) (isFront, SLICE_MOVE) doesn't touch its own Strength, so the debuff cleanly
    // reverses to 0; enemy(1) (HISS_MOVE) gives itself +2 Strength on this same turn, so it
    // nets -6 (ours) + 2 (its own) + 6 (our reversal) = 2 -- the reversal still fired correctly.
    CHECK(f.enemy(0)->powerAmount<StrengthPower>() == 0);
    CHECK(f.enemy(1)->powerAmount<StrengthPower>() == 2);
    CHECK(f.enemy(0)->power("PiercingWailPower") == nullptr);
    CHECK(f.enemy(1)->power("PiercingWailPower") == nullptr);
  }
  {  // X1.2 DodgeAndRoll: block, then the same amount again the next time block clears (turn 2+)
    Fight f("Silent");
    Card* k = f.c->addCard(db::card("DodgeAndRoll"));
    f.toHand(k);
    f.play(k, nullptr);
    CHECK(f.c->player->block == 4);
    f.endTurn();  // player's turn 1 -> 2: block clears (not turn 1 anymore) and BlockNextTurnPower fires
    CHECK(f.c->player->block == 4);
  }
  {  // X1.2 Predator: draw 2 extra cards next turn only
    Fight f("Silent");
    Card* k = f.c->addCard(db::card("Predator"));
    f.toHand(k);
    f.play(k, f.enemy(0));
    f.endTurn();
    CHECK(f.c->hand.size() == 7);  // normal draw of 5 + 2 from Predator
    f.endTurn();
    CHECK(f.c->hand.size() == 5);  // no bonus on turn 3
  }
  {  // X1.2 Ricochet: 4 hits of 3 damage at a random enemy (Sly)
    Fight f("Silent");
    Card* k = f.c->addCard(db::card("Ricochet"));
    f.toHand(k);
    CHECK(k->has(kwSly));
    int before = f.enemy(0)->hp + f.enemy(1)->hp;
    f.play(k, nullptr);
    CHECK(before - (f.enemy(0)->hp + f.enemy(1)->hp) == 12);
  }
  {  // X1.2 DaggerSpray: hits every enemy twice
    Fight f("Silent");
    Card* k = f.c->addCard(db::card("DaggerSpray"));
    f.toHand(k);
    int a = f.enemy(0)->hp, b = f.enemy(1)->hp;
    f.play(k, nullptr);
    CHECK(a - f.enemy(0)->hp == 8 && b - f.enemy(1)->hp == 8);
  }
  {  // X1.2 DaggerThrow: attack, draw 1, discard a chosen card
    Fight f("Silent");
    Card* k = f.c->addCard(db::card("DaggerThrow"));
    f.toHand(k);
    Card* victim = f.c->hand[0] == k ? f.c->hand[1] : f.c->hand[0];
    Creature* e = f.enemy(0);
    int hp = e->hp;
    PlayerAction a;
    a.kind = PlayerAction::PlayCard;
    a.card = k;
    a.target = e;
    f.c->energy = 10;
    f.c->actions.fire(a);
    pump([&] { return f.c->choice.active && f.c->choice.result.waiting(); });
    CHECK(f.c->choice.active && f.c->choice.minCount == 1 && f.c->choice.maxCount == 1);
    f.c->choice.result.fire({victim});
    pump([&] { return f.c->playerPhase && f.c->actions.waiting(); });
    CHECK(hp - e->hp == 9);
    CHECK(f.c->pileOf(victim) == Pile::Discard);
  }
  {  // X1.2 Prepared: draw N then discard N chosen cards (upgrade raises N)
    Fight f("Silent");
    Card* k = f.c->addCard(db::card("Prepared"));
    f.toHand(k);
    size_t before = f.c->hand.size();
    PlayerAction a;
    a.kind = PlayerAction::PlayCard;
    a.card = k;
    f.c->energy = 10;
    f.c->actions.fire(a);
    pump([&] { return f.c->choice.active && f.c->choice.result.waiting(); });
    CHECK(f.c->choice.minCount == 1 && f.c->choice.maxCount == 1);
    Card* pick = f.c->hand[0];
    f.c->choice.result.fire({pick});
    pump([&] { return f.c->playerPhase && f.c->actions.waiting(); });
    CHECK(f.c->hand.size() == before - 1);  // -1 (played) +1 (drew) -1 (discarded)
    CHECK(f.c->pileOf(pick) == Pile::Discard);
  }
  {  // X1.2 Snakebite: Retain keyword, big Poison
    Fight f("Silent");
    Card* k = f.c->addCard(db::card("Snakebite"));
    CHECK(k->has(kwRetain));
    f.toHand(k);
    Creature* e = f.enemy(0);
    f.play(k, e);
    CHECK(e->powerAmount<PoisonPower>() == 7);
  }
  {  // X1.3a Finisher: one hit per Attack play finished this turn (itself excluded), resets next turn
    Fight f("Silent");
    Creature* e = f.enemy(0);
    Card* n1 = f.c->addCard(db::card("Neutralize"));
    Card* n2 = f.c->addCard(db::card("Neutralize"));
    Card* fin = f.c->addCard(db::card("Finisher"));
    f.toHand(n1); f.toHand(n2); f.toHand(fin);
    f.play(n1, e);
    f.play(n2, e);
    CHECK(fin->calcMultiplier(fin) == 2);
    int hp = e->hp;
    f.play(fin, e);
    CHECK(hp - e->hp == 12);  // 2 hits x 6
    f.endTurn();
    CHECK(f.c->attackPlaysFinishedThisTurn == 0);
  }
  {  // X1.3a Flechettes: one hit per Skill in hand
    Fight f("Silent");
    Creature* e = f.enemy(0);
    Card* fl = f.c->addCard(db::card("Flechettes"));
    f.toHand(fl);
    int skills = 0;
    for (Card* k : f.c->hand) if (k->type == CardType::Skill) ++skills;
    int hp = e->hp;
    f.play(fl, e);
    CHECK(skills > 0 && hp - e->hp == 5 * skills);
  }
  {  // X1.3a Expose: strips Block and Artifact, applies Vulnerable
    Fight f("Silent");
    Creature* e = f.enemy(0);
    e->block = 20;
    runTask(cmd::applyPower(db::power("ArtifactPower"), e, 1, f.c->player, nullptr));
    CHECK(e->power("ArtifactPower"));
    Card* k = f.c->addCard(db::card("Expose"));
    f.toHand(k);
    f.play(k, e);
    CHECK(e->block == 0 && !e->power("ArtifactPower") && e->powerAmount<VulnerablePower>() == 2);
  }
  {  // X1.3a BubbleBubble: Poison only if the target already has Poison
    Fight f("Silent");
    Creature* e = f.enemy(0);
    Card* k = f.c->addCard(db::card("BubbleBubble"));
    f.toHand(k);
    f.play(k, e);
    CHECK(e->powerAmount<PoisonPower>() == 0);
    f.apply<PoisonPower>(e, 2);
    Card* k2 = f.c->addCard(db::card("BubbleBubble"));
    f.toHand(k2);
    f.play(k2, e);
    CHECK(e->powerAmount<PoisonPower>() == 11);
  }
  {  // X1.3a Blur: block survives into the next turn
    Fight f("Silent");
    Card* k = f.c->addCard(db::card("Blur"));
    f.toHand(k);
    f.play(k, nullptr);
    CHECK(f.c->player->block >= 5);
    f.c->player->block = 100;  // survive the enemy's attack with room to spare
    f.endTurn();
    CHECK(f.c->player->block > 50);  // not cleared at the start of the new turn
  }
  {  // X1.3a CalculatedGamble: discard the hand, draw that many; upgraded keeps Retain
    Fight f("Silent");
    Card* k = f.c->addCard(db::card("CalculatedGamble"));
    f.toHand(k);
    size_t before = f.c->hand.size() - 1;  // cards left after playing it
    f.play(k, nullptr);
    CHECK(f.c->hand.size() == before);
    CHECK(f.c->pileOf(k) == Pile::Exhaust);
    Card* up = f.c->addCard(db::card("CalculatedGamble"));
    up->upgrade();
    CHECK(up->has(kwRetain));
  }
  {  // X1.3a Expertise: drawn cards are Retained this turn
    Fight f("Silent");
    Card* k = f.c->addCard(db::card("Expertise"));
    f.toHand(k);
    size_t before = f.c->hand.size();
    f.play(k, nullptr);
    CHECK(f.c->hand.size() == before - 1 + 2);
    int retained = 0;
    for (Card* c : f.c->hand) if (c->singleTurnRetain) ++retained;
    CHECK(retained == 2);
  }
  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
