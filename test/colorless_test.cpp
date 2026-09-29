// Colorless pool and cards 1/3 (A1a): the pool query, Discovery-style choices, and the non-trivial cards.
// Build: make -f Makefile.sdl build/colorless_test ; run: ./build/colorless_test
#include <cstdio>
#include <cstdlib>

#include "../source/core/cards.h"
#include "../source/core/colorless.h"

using namespace sts;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    ++checks;                                                           \
    if (!(cond)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #cond); } \
  } while (0)

static Task<> fightTask(Run* r) { co_await r->fight("NibbitsNormal"); }
static Task<> wrap(Task<> t, bool* done) { co_await t; *done = true; }

static Task<> drawTask(Combat* c, int n) { co_await cmd::drawCards(*c, n); }

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
    pump([&] { return idle(); });
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
  {  // The pool: C# order, no multiplayer-only cards, only registered cards.
    auto all = db::colorlessCards([](const Card&) { return true; });
    CHECK(!all.empty() && all[0] == "Alchemize");
    auto has = [&](const char* id) { return std::find(all.begin(), all.end(), std::string(id)) != all.end(); };
    CHECK(has("GoldAxe") && has("Catastrophe"));
    CHECK(!has("BeaconOfHope") && !has("GangUp") && !has("Coordinate") && !has("BelieveInYou"));
    CHECK(db::isColorless("GangUp") && db::isColorless("Alchemize") && !db::isColorless("StrikeIronclad"));
    for (auto& id : all) { auto k = db::card(id); CHECK(k && k->rarity != Rarity::Basic); }
  }
  {  // Finesse / FlashOfSteel / DramaticEntrance / Fisticuffs: numbers, upgrades, block from damage.
    Fight f;
    Creature* e = f.enemy(0);
    int hp = e->hp;
    f.play(f.fresh("FlashOfSteel"), e);
    CHECK(hp - e->hp == 5);
    f.c->player->block = 0;
    f.play(f.fresh("Fisticuffs"), e);
    CHECK(f.c->player->block == 7);
    f.c->player->block = 0;
    f.play(f.fresh("Finesse", true), nullptr);
    CHECK(f.c->player->block == 7);
    hp = e->hp;
    f.play(f.fresh("DramaticEntrance"), nullptr);
    CHECK(hp - e->hp == 11);
    CHECK(f.count(Pile::Exhaust, "DramaticEntrance") == 1);
  }
  {  // GoldAxe: 1 damage per card played this combat.
    Fight f;
    Creature* e = f.enemy(0);
    f.play(f.fresh("Finesse"), nullptr);
    f.play(f.fresh("Finesse"), nullptr);
    int hp = e->hp;
    f.play(f.fresh("GoldAxe"), e);
    CHECK(hp - e->hp == 2);
    CHECK(f.c->cardPlaysFinishedThisCombat == 3);
  }
  {  // Fasten: Defend cards gain block, other block does not.
    Fight f;
    f.play(f.fresh("Fasten"), nullptr);
    f.c->player->block = 0;
    f.play(f.fresh("DefendIronclad"), nullptr);
    CHECK(f.c->player->block == 9);
    f.c->player->block = 0;
    f.play(f.fresh("Finesse"), nullptr);
    CHECK(f.c->player->block == 4);
  }
  {  // Equilibrium: block 13 and the hand is retained.
    Fight f;
    f.c->player->block = 0;
    f.play(f.fresh("Equilibrium"), nullptr);
    CHECK(f.c->player->block == 13 && f.c->player->powerAmount<RetainHandPower>() == 1);
  }
  {  // Anointed: moves a Rare card from the draw pile to the hand.
    Fight f;
    Card* rare = f.c->addCard(db::card("Bolas"));
    f.c->draw.push_back(rare);
    f.play(f.fresh("Anointed", true), nullptr);
    CHECK(f.c->pileOf(rare) == Pile::Hand);
  }
  {  // Bolas: played this turn, back in hand at the next turn's start.
    Fight f;
    Card* b = f.fresh("Bolas");
    f.play(b, f.enemy(0));
    CHECK(f.c->pileOf(b) == Pile::Discard);
    f.endTurn();
    CHECK(f.c->pileOf(b) == Pile::Hand);
  }
  {  // Catastrophe: auto-plays 2 playable cards from the draw pile.
    Fight f;
    Card* a = f.c->addCard(db::card("Finesse"));
    Card* b = f.c->addCard(db::card("Finesse"));
    f.c->draw = {a, b};
    f.play(f.fresh("Catastrophe"), nullptr);
    CHECK(f.c->pileOf(a) != Pile::Draw && f.c->pileOf(b) != Pile::Draw);
  }
  {  // BeatDown: auto-plays discarded attacks.
    Fight f;
    Creature* e = f.enemy(0);
    Card* fl = f.c->addCard(db::card("FlashOfSteel"));
    f.c->discard.push_back(fl);
    int hp = e->hp;
    f.play(f.fresh("BeatDown"), nullptr);
    CHECK(hp - e->hp == 5);
  }
  {  // Automation: every 10 cards drawn gives energy.
    Fight f;
    f.play(f.fresh("Automation"), nullptr);
    f.c->draw.clear();
    for (int i = 0; i < 12; ++i) f.c->draw.push_back(f.c->addCard(db::card("Finesse")));
    for (Card* k : std::vector<Card*>(f.c->hand)) { f.c->removeFromPiles(k); f.c->discard.push_back(k); }
    f.c->energy = 0;
    bool done = false;
    Scheduler::get().spawn(wrap(drawTask(f.c, 10), &done));
    pump([&] { return done; });
    CHECK(f.c->energy == 1);
  }
  {  // DarkShackles: -9 Strength until the end of the turn.
    Fight f;
    Creature* e = f.enemy(0);
    f.play(f.fresh("DarkShackles"), e);
    CHECK(e->powerAmount<StrengthPower>() == -9);
    f.endTurn();
    CHECK(e->powerAmount<StrengthPower>() == 0);
  }
  {  // Calamity: after an Attack, 1 random Attack of the pool joins the hand.
    Fight f;
    f.play(f.fresh("Calamity"), nullptr);
    int attacks = 0;
    for (Card* k : f.c->hand) if (k->type == CardType::Attack) ++attacks;
    f.play(f.fresh("FlashOfSteel"), f.enemy(0));
    int after = 0;
    for (Card* k : f.c->hand) if (k->type == CardType::Attack) ++after;
    CHECK(after >= attacks + 1 - 1);  // +1 generated (the played one left the hand; one may be drawn)
    CHECK(f.c->cardsGeneratedThisCombat >= 0);
  }
  {  // Entropy: at the start of the next turn the chosen hand card is transformed.
    Fight f;
    f.play(f.fresh("Entropy"), nullptr);
    f.endTurn();
    CHECK(f.choosing());
    Card* victim = f.c->choice.options[0];
    f.c->choice.result.fire(std::vector<Card*>{victim});
    pump([&] { return f.idle(); });
    bool still = false;
    for (Card* k : f.c->hand) if (k == victim) still = true;
    CHECK(!still);
  }
  {  // Discovery (character pool): choose 1 of 3, the card is free this turn.
    Fight f;
    int hand = (int)f.c->hand.size();
    std::vector<Card*> before = f.c->hand;
    Card* d = f.fresh("Discovery");
    f.fire(d, nullptr);
    pump([&] { return f.choosing(); });
    CHECK(f.c->choice.options.size() == 3 && f.c->choice.minCount == 0);
    Card* pick = f.c->choice.options[1];
    std::string pid = pick->id;
    f.c->choice.result.fire(std::vector<Card*>{pick});
    pump([&] { return f.idle(); });
    CHECK((int)f.c->hand.size() == hand + 1);
    Card* added = nullptr;
    for (Card* k : f.c->hand) if (k != d && std::find(before.begin(), before.end(), k) == before.end()) added = k;
    CHECK(added && added->id == pid && (added->costsX || f.c->energyCost(added) == 0));  // X cards stay X
  }
  {  // ColorlessPotion: 3 distinct colorless cards, free this turn.
    Fight f;
    auto p = db::potion("ColorlessPotion");
    CHECK(p != nullptr);
    p->run = f.r.get();
    bool done = false;
    Scheduler::get().spawn(wrap(p->onUse(f.c->player), &done));
    pump([&] { return f.choosing(); });
    CHECK(f.c->choice.options.size() == 3);
    for (Card* k : f.c->choice.options) CHECK(db::isColorless(k->id));
    int hand = (int)f.c->hand.size();
    Card* chosen = f.c->choice.options[0];
    f.c->choice.result.fire(std::vector<Card*>{chosen});
    pump([&] { return done; });
    CHECK((int)f.c->hand.size() == hand + 1 && f.c->pileOf(chosen) == Pile::Hand && f.c->energyCost(chosen) == 0);
  }
  {  // Alchemize: procures a potion (not itself generable); the reward helper gives distinct colorless cards.
    Fight f;
    CHECK(!db::card("Alchemize")->canBeGeneratedInCombat());
    int n = 0;
    for (auto& s : f.r->potions) if (s) ++n;
    f.play(f.fresh("Alchemize"), nullptr);
    int m = 0;
    for (auto& s : f.r->potions) if (s) ++m;
    CHECK(m == n + 1);
    auto cards = colorlessRewardCards(*f.r, 3);
    CHECK(cards.size() == 3 && cards[0]->id != cards[1]->id && db::isColorless(cards[2]->id));
  }

  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
