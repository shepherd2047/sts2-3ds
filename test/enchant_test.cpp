// Enchantment engine checks (A3a): CanEnchant, damage / play count / draw / cost hooks,
// Imbued's auto-play, PerfectFit's shuffle, clones, and the save round trip.
// Build: make -f Makefile.sdl build/enchant_test ; run: ./build/enchant_test
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

// A run in the first turn of a fight against the Nibbits. `setup` enchants the deck first.
struct Fight {
  std::unique_ptr<Run> r = std::make_unique<Run>();
  Combat* c = nullptr;
  explicit Fight(uint64_t seed, const std::function<void(Run&)>& setup) {
    r->start(seed);
    setup(*r);
    Scheduler::get().spawn(fightTask(r.get()));
    pump([&] { return r->combat && r->combat->playerPhase && r->combat->actions.waiting(); });
    c = r->combat.get();
    for (Creature* e : c->enemies) e->hp = e->maxHp = 500;  // nothing dies during a check
  }
  ~Fight() { Scheduler::get().clear(); }
  Card* find(const std::string& enchantmentId) {
    for (Card* k : c->allCards()) if (k->enchantment && k->enchantment->id == enchantmentId) return k;
    return nullptr;
  }
  void toHand(Card* k) { c->removeFromPiles(k); c->hand.push_back(k); }
  Creature* target() { c->enemies[0]->block = 0; return c->enemies[0]; }
  // Play `k` on the first enemy and wait for the next input; returns the HP the enemy lost.
  int play(Card* k) {
    Creature* t = target();
    int before = t->hp;
    PlayerAction a;
    a.kind = PlayerAction::PlayCard;
    a.card = k;
    a.target = t;
    c->energy = 10;
    c->actions.fire(a);
    pump([&] { return c->playerPhase && c->actions.waiting(); });
    return before - t->hp;
  }
};

static Card* deckCard(Run& r, const std::string& id, int nth = 0) {
  for (auto& k : r.deck) if (k->id == id && nth-- == 0) return k.get();
  return nullptr;
}

int main() {
  db::init();
  {  // CanEnchant rules
    Run r;
    r.start(1);
    Card* strike = deckCard(r, "StrikeIronclad");
    Card* defend = deckCard(r, "DefendIronclad");
    auto sharp = db::enchantment("Sharp");
    auto imbued = db::enchantment("Imbued");
    CHECK(sharp && imbued);
    CHECK(sharp->canEnchant(*strike));
    CHECK(!sharp->canEnchant(*defend));   // attacks only
    CHECK(imbued->canEnchant(*defend));
    CHECK(!imbued->canEnchant(*strike));  // skills only
    CHECK(cmd::enchant(strike, db::enchantment("Sharp"), 3) != nullptr);
    CHECK(strike->enchantment && strike->enchantment->amount == 3);
    CHECK(!db::enchantment("Sharp")->canEnchant(*strike));  // not stackable, already enchanted
    CHECK(!db::enchantment("Vigorous")->canEnchant(*strike));
    CHECK(cmd::enchant(strike, db::enchantment("Vigorous"), 4) == nullptr);
    auto twin = strike->clone();  // a copy keeps its own enchantment, pointing at the copy
    CHECK(twin->enchantment && twin->enchantment.get() != strike->enchantment.get());
    CHECK(twin->enchantment->card == twin.get() && strike->enchantment->card == strike);
    CHECK(twin->enchantment->amount == 3 && twin->enchantment->id == "Sharp");
    twin->enchantment->amount = 9;
    CHECK(strike->enchantment->amount == 3);
    cmd::clearEnchantment(twin.get());
    CHECK(!twin->enchantment && strike->enchantment);
    Card* curse = nullptr;
    for (auto& id : {"AscendersBane", "Regret", "Doubt", "Injury", "Shame"})
      if (auto k = db::card(id)) { curse = r.addCardToDeck(std::move(k)); break; }
    if (curse) CHECK(!db::enchantment("Glam")->canEnchant(*curse));
  }
  {  // damage hooks: Sharp adds before Vulnerable multiplies; Vigorous is used up after one play
    Fight f(7, [](Run& r) {
      cmd::enchant(deckCard(r, "StrikeIronclad", 0), db::enchantment("Sharp"), 3);
      cmd::enchant(deckCard(r, "StrikeIronclad", 1), db::enchantment("Vigorous"), 4);
      cmd::enchant(deckCard(r, "StrikeIronclad", 2), db::enchantment("Glam"), 1);
      cmd::enchant(deckCard(r, "StrikeIronclad", 3), db::enchantment("Swift"), 2);
    });
    CHECK(f.c != nullptr);
    Card* sharp = f.find("Sharp");
    Card* vig = f.find("Vigorous");
    Card* glam = f.find("Glam");
    Card* swift = f.find("Swift");
    CHECK(sharp && vig && glam && swift);
    CHECK(sharp->deckVersion.p && sharp->deckVersion.p->enchantment && sharp->deckVersion.p->enchantment.get() != sharp->enchantment.get());
    CHECK(sharp->clone()->deckVersion.p == nullptr);
    f.toHand(sharp);
    CHECK(f.play(sharp) == 9);
    f.toHand(vig);
    CHECK(f.play(vig) == 10);
    CHECK(vig->enchantment->disabled());
    CHECK(!vig->deckVersion.p->enchantment->disabled());  // the deck card is untouched
    f.toHand(vig);
    CHECK(f.play(vig) == 6);
    f.toHand(glam);
    CHECK(f.play(glam) == 12);  // played twice the first time
    f.toHand(glam);
    int g2 = f.play(glam);
    CHECK(g2 == 6);
    f.toHand(swift);
    int handBefore = (int)f.c->hand.size();
    f.play(swift);
    CHECK((int)f.c->hand.size() == handBefore - 1 + 2);  // played one, drew Amount
    CHECK(swift->enchantment->disabled());
    CHECK(f.c->modifyDamage(nullptr, f.c->player, 6, kMove, sharp).toInt() == 9);
    CHECK(f.c->modifyDamage(nullptr, f.c->player, 6, kMove | kUnpowered, sharp).toInt() == 6);  // powered attacks only
  }
  {  // Imbued: bottom of the draw pile, played for free at the start of turn 1
    Fight f(3, [](Run& r) { cmd::enchant(deckCard(r, "DefendIronclad", 0), db::enchantment("Imbued"), 1); });
    Card* k = f.find("Imbued");
    CHECK(k != nullptr);
    CHECK(f.c->pileOf(k) == Pile::Discard);
    CHECK(f.c->player->block == 5);
    CHECK(f.c->energy == 3);
  }
  {  // PerfectFit: on top after a shuffle (not the first)
    for (uint64_t seed : {2, 5, 11}) {
      Fight f(seed, [](Run& r) { cmd::enchant(deckCard(r, "StrikeIronclad", 0), db::enchantment("PerfectFit"), 1); });
      Card* k = f.find("PerfectFit");
      CHECK(k != nullptr);
      f.c->removeFromPiles(k);
      f.c->discard.push_back(k);
      Scheduler::get().spawn([](Combat* c) -> Task<> { co_await cmd::shuffle(*c); }(f.c));
      pump([&] { return !f.c->discard.empty() ? false : true; });
      CHECK(!f.c->draw.empty() && f.c->draw.front() == k);
    }
  }
  {  // SlumberingEssence: a card left in hand at the end of the turn costs 1 less until played
    Fight f(4, [](Run& r) { cmd::enchant(deckCard(r, "StrikeIronclad", 0), db::enchantment("SlumberingEssence"), 1); });
    Card* k = f.find("SlumberingEssence");
    CHECK(k != nullptr);
    f.toHand(k);
    CHECK(k->costWithLocalMods() == 1);
    PlayerAction a;
    a.kind = PlayerAction::EndTurn;
    f.c->actions.fire(a);
    pump([&] { return f.c->turnNumber >= 2 && f.c->playerPhase && f.c->actions.waiting(); });
    CHECK(k->costWithLocalMods() == 0);
  }
  {  // A3b: Sown / Adroit / Corrupted / Inky / Goopy / SoulsPower / Slither
    Fight f(6, [](Run& r) {
      cmd::enchant(deckCard(r, "StrikeIronclad", 0), db::enchantment("Sown"), 2);
      cmd::enchant(deckCard(r, "StrikeIronclad", 1), db::enchantment("Adroit"), 3);
      cmd::enchant(deckCard(r, "StrikeIronclad", 2), db::enchantment("Corrupted"), 1);
      cmd::enchant(deckCard(r, "StrikeIronclad", 3), db::enchantment("Inky"), 1);
      cmd::enchant(deckCard(r, "DefendIronclad", 0), db::enchantment("Goopy"), 1);
      CHECK(!db::enchantment("SoulsPower")->canEnchant(*deckCard(r, "StrikeIronclad", 4)));  // no Exhaust
      CHECK(!db::enchantment("Goopy")->canEnchant(*deckCard(r, "StrikeIronclad", 4)));       // not a Defend
      CHECK(!db::enchantment("Corrupted")->canEnchant(*deckCard(r, "DefendIronclad", 1)));   // Attacks only
      CHECK(db::enchantment("Slither")->canEnchant(*deckCard(r, "StrikeIronclad", 4)));
    });
    Card* sown = f.find("Sown");
    Card* adroit = f.find("Adroit");
    Card* corr = f.find("Corrupted");
    Card* inky = f.find("Inky");
    Card* goopy = f.find("Goopy");
    CHECK(sown && adroit && corr && inky && goopy);
    CHECK(goopy->has(kwExhaust));
    f.toHand(sown);
    f.play(sown);
    CHECK(f.c->energy == 10 - 1 + 2 && sown->enchantment->disabled());
    f.toHand(adroit);
    f.c->player->block = 0;
    CHECK(f.play(adroit) == 6 && f.c->player->block == 3);
    f.toHand(corr);
    int hp = f.c->player->hp;
    CHECK(f.play(corr) == 9 && f.c->player->hp == hp - 2);
    f.toHand(inky);
    f.play(inky);
    Power* weak = f.c->enemies[0]->power("WeakPower");
    CHECK(weak && weak->amount == 1);
    f.toHand(goopy);
    f.c->player->block = 0;
    Creature* t = f.target();
    PlayerAction a;
    a.kind = PlayerAction::PlayCard;
    a.card = goopy;
    a.target = t;
    f.c->energy = 10;
    f.c->actions.fire(a);
    pump([&] { return f.c->playerPhase && f.c->actions.waiting(); });
    CHECK(f.c->player->block == 5);
    CHECK(goopy->enchantment->amount == 2 && goopy->deckVersion.p->enchantment->amount == 2);
    // Slither: drawing the card into the hand randomizes its cost to 0-3
    Card* k = nullptr;
    for (Card* c2 : f.c->allCards()) if (c2->id == "StrikeIronclad" && !c2->enchantment) { k = c2; break; }
    CHECK(k && cmd::enchant(k, db::enchantment("Slither"), 1));
    f.c->removeFromPiles(k);
    f.c->draw.insert(f.c->draw.begin(), k);
    Scheduler::get().spawn([](Combat* c) -> Task<> { co_await cmd::drawCards(*c, 1); }(f.c));
    pump([&] { return f.c->pileOf(k) == Pile::Hand; });
    CHECK(f.c->pileOf(k) == Pile::Hand && k->costWithLocalMods() >= 0 && k->costWithLocalMods() <= 3);
  }
  {  // saves keep enchantments (id, amount, status, vars)
    Run r;
    r.start(9);
    cmd::enchant(deckCard(r, "StrikeIronclad", 0), db::enchantment("Sharp"), 3);
    cmd::enchant(deckCard(r, "StrikeIronclad", 1), db::enchantment("Vigorous"), 5);
    deckCard(r, "StrikeIronclad", 1)->enchantment->status = EnchantStatus::Disabled;
    cmd::enchant(deckCard(r, "StrikeIronclad", 2), db::enchantment("Glam"), 1);
    std::string text = r.save();
    Run back;
    CHECK(back.load(text));
    CHECK(back.save() == text);
    Card* a = deckCard(back, "StrikeIronclad", 0);
    Card* b = deckCard(back, "StrikeIronclad", 1);
    Card* g = deckCard(back, "StrikeIronclad", 2);
    CHECK(a->enchantment && a->enchantment->id == "Sharp" && a->enchantment->amount == 3 && a->enchantment->card == a);
    CHECK(b->enchantment && b->enchantment->id == "Vigorous" && b->enchantment->amount == 5 && b->enchantment->disabled());
    CHECK(g->enchantment && g->enchantment->id == "Glam" && g->enchantment->val("Times") == Dec(1));
    CHECK(!deckCard(back, "StrikeIronclad", 3)->enchantment);
  }
  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
