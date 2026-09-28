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

  // ---------------------------------------------------------------- X3.2: common cards

  {  // CrushUnder: 8 damage to all enemies, then a temporary Strength loss (CrushUnderPower)
     // that decays back to 0 at the end of the enemy's own turn.
    Fight f;
    Card* cu = f.c->addCard(db::card("CrushUnder"));
    f.toHand(cu);
    Creature* e = f.enemy(0);
    int hp = e->hp;
    f.play(cu, nullptr);
    CHECK(hp - e->hp == 8);
    Power* p = e->power("CrushUnderPower");
    CHECK(p && p->amount == 1);
    CHECK(e->powerAmount<StrengthPower>() == -1);
    f.endTurn();
    CHECK(e->power("CrushUnderPower") == nullptr);
    CHECK(e->powerAmount<StrengthPower>() == 0);
  }
  {  // Glitterstream: 11 block now, plus BlockNextTurnPower(5) (run through Hook.ModifyBlock at
     // cast time); at the start of the next turn's block clear it grants that block and removes
     // itself.
    Fight f;
    Card* g = f.c->addCard(db::card("Glitterstream"));
    f.toHand(g);
    f.play(g, nullptr);
    CHECK(f.c->player->block == 11);
    Power* p = f.c->player->power("BlockNextTurnPower");
    CHECK(p && p->amount == 5);
    f.endTurn();
    CHECK(f.c->player->block == 5);
    CHECK(f.c->player->power("BlockNextTurnPower") == nullptr);
  }
  {  // Patter: 8 block, VigorPower 2 (adds to the very next attack, then is consumed).
    Fight f;
    Card* patter = f.c->addCard(db::card("Patter"));
    f.toHand(patter);
    f.play(patter, nullptr);
    CHECK(f.c->player->block == 8);
    Power* p = f.c->player->power("VigorPower");
    CHECK(p && p->amount == 2);
    Card* strike = f.c->addCard(db::card("StrikeRegent"));
    f.toHand(strike);
    Creature* e = f.enemy(0);
    int hp = e->hp;
    f.play(strike, e);
    CHECK(hp - e->hp == 6 + 2);  // StrikeRegent's 6 + Vigor's 2
    CHECK(f.c->player->power("VigorPower") == nullptr);
  }
  {  // CrescentSpear: CalculatedDamage = 8 + 2 * (the player's cards, anywhere, with a star
     // cost or HasStarCostX) -- includes the starter deck's FallingStar and the card itself.
    Fight f;
    auto starCostCards = [&] {
      int n = 0;
      for (Card* k : f.c->allCards()) if (k->starCost >= 0 || k->costsStarsX) ++n;
      return n;
    };
    Card* spear = f.c->addCard(db::card("CrescentSpear"));
    f.toHand(spear);
    int n = starCostCards();
    f.c->stars = 5;
    Creature* e = f.enemy(0);
    int hp = e->hp;
    f.play(spear, e);
    CHECK(hp - e->hp == 8 + 2 * n);
    CHECK(f.c->stars == 4);  // spent its 1 star cost
  }
  {  // RefineBlade: Forge 8 (grows/creates a Sovereign Blade), then 1 extra energy next turn
     // (EnergyNextTurnPower, this file).
    Fight f;
    Card* rb = f.c->addCard(db::card("RefineBlade"));
    f.toHand(rb);
    f.play(rb, nullptr);
    Card* blade = nullptr;
    for (Card* k : f.c->hand) if (k->id == "SovereignBlade") blade = k;
    CHECK(blade && blade->val("Damage").toInt() == 18);  // base 10 + 8
    Power* p = f.c->player->power("EnergyNextTurnPower");
    CHECK(p && p->amount == 1);
    int maxE = f.c->maxEnergyNow();
    f.endTurn();
    CHECK(f.c->energy == maxE + 1);
    CHECK(f.c->player->power("EnergyNextTurnPower") == nullptr);
  }
  {  // Glow: 1 star, draw 1, and 1 extra card drawn next turn (DrawCardsNextTurnPower,
     // char_regent.h).
    Fight f;
    Card* glow = f.c->addCard(db::card("Glow"));
    f.toHand(glow);
    int stars = f.c->stars;
    f.play(glow, nullptr);
    CHECK(f.c->stars == stars + 1);
    Power* p = f.c->player->power("DrawCardsNextTurnPower");
    CHECK(p && p->amount == 1);
    f.endTurn();
    CHECK(f.c->hand.size() == 6);  // the normal 5 plus DrawCardsNextTurnPower's 1
    CHECK(f.c->player->power("DrawCardsNextTurnPower") == nullptr);
  }
  {  // GuidingStar: 12 damage (1 energy + 1 star), then 2 extra cards drawn next turn.
    Fight f;
    Card* gs = f.c->addCard(db::card("GuidingStar"));
    f.toHand(gs);
    f.c->stars = 5;
    Creature* e = f.enemy(0);
    int hp = e->hp;
    f.play(gs, e);
    CHECK(hp - e->hp == 12);
    CHECK(f.c->stars == 4);
    Power* p = f.c->player->power("DrawCardsNextTurnPower");
    CHECK(p && p->amount == 2);
  }
  {  // CollisionCourse: 10 damage, then a Debris token joins the hand.
    Fight f;
    Card* cc = f.c->addCard(db::card("CollisionCourse"));
    f.toHand(cc);
    Creature* e = f.enemy(0);
    int hp = e->hp;
    f.play(cc, e);
    CHECK(hp - e->hp == 10);
    bool found = false;
    for (Card* k : f.c->hand) if (k->id == "Debris") found = true;
    CHECK(found);
  }
  {  // KnowThyPlace: Weak 1 + Vulnerable 1, Exhaust; upgrading removes Exhaust (instead of
     // scaling a number, as in the C#).
    Fight f;
    Card* k = f.c->addCard(db::card("KnowThyPlace"));
    f.toHand(k);
    CHECK(k->has(kwExhaust));
    Creature* e = f.enemy(0);
    f.play(k, e);
    CHECK(e->powerAmount<WeakPower>() == 1);
    CHECK(e->powerAmount<VulnerablePower>() == 1);
    CHECK(f.c->pileOf(k) == Pile::Exhaust);
    k->upgrade();
    CHECK(!k->has(kwExhaust));
  }
  {  // Begone: pick a card from hand, transform it into a MinionStrike.
    Fight f;
    Card* begone = f.c->addCard(db::card("Begone"));
    f.toHand(begone);
    Card* strike = f.c->addCard(db::card("StrikeRegent"));
    f.toHand(strike);
    PlayerAction a;
    a.kind = PlayerAction::PlayCard;
    a.card = begone;
    f.c->energy = 10;
    f.c->actions.fire(a);
    pump([&] { return f.c->choice.active && f.c->choice.result.waiting(); });
    CHECK(f.c->choice.active && f.c->choice.minCount == 0 && f.c->choice.maxCount == 1);
    f.c->choice.result.fire({strike});
    pump([&] { return f.c->playerPhase && f.c->actions.waiting(); });
    CHECK(f.c->pileOf(strike) == Pile::None);  // replaced in place, not merely moved
    bool found = false;
    for (Card* k : f.c->hand) if (k->id == "MinionStrike") found = true;
    CHECK(found);
  }
  {  // CosmicIndifference: 6 block, then move a chosen discard-pile card to the top of the
     // draw pile.
    Fight f;
    Card* ci = f.c->addCard(db::card("CosmicIndifference"));
    f.toHand(ci);
    Card* discarded = f.c->addCard(db::card("StrikeRegent"));
    f.c->discard.push_back(discarded);
    PlayerAction a;
    a.kind = PlayerAction::PlayCard;
    a.card = ci;
    f.c->energy = 10;
    f.c->actions.fire(a);
    pump([&] { return f.c->choice.active && f.c->choice.result.waiting(); });
    f.c->choice.result.fire({discarded});
    pump([&] { return f.c->playerPhase && f.c->actions.waiting(); });
    CHECK(f.c->player->block == 6);
    CHECK(!f.c->draw.empty() && f.c->draw.front() == discarded);
  }
  {  // PhotonCut: 10 damage, draw 1, then put 1 chosen hand card back on top of the draw pile.
    Fight f;
    Card* pc = f.c->addCard(db::card("PhotonCut"));
    f.toHand(pc);
    Creature* e = f.enemy(0);
    int hp = e->hp;
    PlayerAction a;
    a.kind = PlayerAction::PlayCard;
    a.card = pc;
    a.target = e;
    f.c->energy = 10;
    f.c->actions.fire(a);
    pump([&] { return f.c->choice.active && f.c->choice.result.waiting(); });
    CHECK(f.c->choice.active && f.c->choice.minCount == 1 && f.c->choice.maxCount == 1);
    Card* chosen = f.c->choice.options[0];
    f.c->choice.result.fire({chosen});
    pump([&] { return f.c->playerPhase && f.c->actions.waiting(); });
    CHECK(hp - e->hp == 10);
    CHECK(!f.c->draw.empty() && f.c->draw.front() == chosen);
  }

  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
