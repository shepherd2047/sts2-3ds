// Pets checks: PlayerCmd.AddPet (cmd::addPet, Combat::pets), Byrdpip (ByrdonisEgg -> ByrdSwoop, the
// Hatch rest option, a pet every combat) and PaelsLegion (double block, then two turns asleep).
// Build: make -f Makefile.sdl build/pets_test ; run: ./build/pets_test
#include <cstdio>
#include "portable_env.h"
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
static int deckCount(Run& r, const char* id) {
  int n = 0;
  for (auto& c : r.deck) n += c->id == id;
  return n;
}
static bool hasRelic(Run& r, const char* id) {
  for (auto& x : r.relics) if (x->id == id) return true;
  return false;
}

// The first turn of a fight against two Nibbits (500 HP each) with `relics` owned and `cards` in the deck.
struct Fight {
  std::unique_ptr<Run> r = std::make_unique<Run>();
  Combat* c = nullptr;
  Fight(std::initializer_list<const char*> relics, std::initializer_list<const char*> cards = {}) {
    setenv("STS_NO_NEOW", "1", 1);
    r->start(5);
    for (const char* id : cards) r->addCardToDeck(db::card(id));
    for (const char* id : relics) {
      auto rel = db::relic(id);
      rel->run = r.get();
      r->relics.push_back(std::move(rel));
    }
    Scheduler::get().spawn(fightTask(r.get()));
    pump([&] { return r->combat && r->combat->playerPhase && r->combat->actions.waiting(); });
    c = r->combat.get();
    for (Creature* e : c->enemies) e->hp = e->maxHp = 500;
  }
  ~Fight() { Scheduler::get().clear(); }
  Card* toHand(const char* id) {
    Card* k = c->addCard(db::card(id));
    c->hand.push_back(k);
    return k;
  }
  void play(Card* k, Creature* t = nullptr) {
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
    c->player->hp = c->player->maxHp = 999;  // survive the Nibbits
    c->actions.fire(a);
    pump([&] { return c->over || (c->turnNumber > turn && c->playerPhase && c->actions.waiting()); });
  }
};

int main() {
  db::init();
  setenv("STS_NO_NEOW", "1", 1);

  {  // registered with their C# rarities
    auto b = db::relic("Byrdpip"), l = db::relic("PaelsLegion");
    CHECK(b && b->rarity == RelicRarity::Event);
    CHECK(l && l->rarity == RelicRarity::Ancient && l->val("Turns") == Dec(2));
  }

  {  // Byrdpip picked up outside combat: every ByrdonisEgg becomes a ByrdSwoop, no pet yet
    Run r;
    r.start(21);
    r.addCardToDeck(db::card("ByrdonisEgg"));
    r.addCardToDeck(db::card("ByrdonisEgg"));
    runTask(r.obtainRelic(db::relic("Byrdpip")));
    CHECK(hasRelic(r, "Byrdpip"));
    CHECK(deckCount(r, "ByrdonisEgg") == 0 && deckCount(r, "ByrdSwoop") == 2);
  }

  {  // the Hatch rest option (7) obtains Byrdpip
    Run r;
    r.start(22);
    r.addCardToDeck(db::card("ByrdonisEgg"));
    bool done = false;
    Scheduler::get().spawn(wrap(r.restSite(), &done));
    pump([&] { return r.screen == Screen::Rest && r.restChoice.waiting(); });
    CHECK(std::find(r.restOptions.begin(), r.restOptions.end(), 7) != r.restOptions.end());
    r.restChoice.fire(7);
    pump([&] { return done; });
    CHECK(done && hasRelic(r, "Byrdpip"));
    CHECK(deckCount(r, "ByrdonisEgg") == 0 && deckCount(r, "ByrdSwoop") == 1);
    Scheduler::get().clear();
  }

  {  // Byrdpip's pet: summoned at combat start, owned by the player, out of the monsters' reach
    Fight f({"Byrdpip"});
    CHECK(f.c->pets.size() == 1);
    Creature* pet = f.c->pet("Byrdpip");
    CHECK(pet && pet->side == Side::Player && pet->petOwner == f.c->player && pet->hp == 9999);
    CHECK(std::find(f.c->enemies.begin(), f.c->enemies.end(), pet) == f.c->enemies.end());
    CHECK(f.c->osty == nullptr);
    f.endTurn();
    CHECK(pet->hp == 9999 && pet->alive());  // the Nibbits only hit the player
    CHECK(f.c->pets.size() == 1);            // one pet per combat, not per turn
    // ByrdSwoop: 14 damage from the player.
    Creature* e = f.c->enemies[0];
    e->block = 0;
    f.c->player->powers.clear();  // whatever the Nibbits applied (Weak)
    int ehp = e->hp;
    f.play(f.toHand("ByrdSwoop"), e);
    if (ehp - e->hp != 14) printf("ByrdSwoop dealt %d\n", ehp - e->hp);
    CHECK(ehp - e->hp == 14);
  }

  {  // Byrdpip obtained mid-combat: the eggs in the combat piles change too, and the pet arrives now
    Fight f({}, {"ByrdonisEgg"});
    CHECK(f.c->pets.empty());
    runTask(f.r->obtainRelic(db::relic("Byrdpip")));
    CHECK(f.c->pet("Byrdpip") != nullptr && f.c->pets.size() == 1);
    int eggs = 0, swoops = 0;
    for (Card* k : f.c->allCards()) { eggs += k->id == "ByrdonisEgg"; swoops += k->id == "ByrdSwoop"; }
    CHECK(eggs == 0 && swoops == 1);
    CHECK(deckCount(*f.r, "ByrdonisEgg") == 0 && deckCount(*f.r, "ByrdSwoop") == 1);
  }

  {  // PaelsLegion: the first block card doubles, then it sleeps for two turn starts
    Fight f({"PaelsLegion"});
    CHECK(f.c->pet("PaelsLegion") != nullptr);
    Relic* legion = nullptr;
    for (auto& x : f.r->relics) if (x->id == "PaelsLegion") legion = x.get();
    CHECK(legion && legion->displayAmount() == -1);
    f.c->player->block = 0;
    f.play(f.toHand("DefendIronclad"));
    CHECK(f.c->player->block == 10);
    CHECK(legion->displayAmount() == 2 && legion->showCounter());
    f.play(f.toHand("DefendIronclad"));
    CHECK(f.c->player->block == 15);  // asleep: no double
    f.endTurn();
    CHECK(legion->displayAmount() == 1);
    f.c->player->block = 0;
    f.play(f.toHand("DefendIronclad"));
    CHECK(f.c->player->block == 5);
    f.endTurn();
    CHECK(legion->displayAmount() == -1);  // awake again
    f.c->player->block = 0;
    f.play(f.toHand("DefendIronclad"));
    CHECK(f.c->player->block == 10);
  }

  {  // pets belong to their combat: the next fight summons a fresh one
    Fight f({"Byrdpip", "PaelsLegion"});
    CHECK(f.c->pets.size() == 2 && f.c->pet("Byrdpip") && f.c->pet("PaelsLegion"));
  }

  printf("pets_test: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
