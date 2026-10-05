// Debug switches (core/debug_cmds.h: STS_POWERS / STS_ORBS / STS_STARS / STS_ENERGY / STS_HAND / STS_PILE_*,
// the STS_SCRIPT C<cmd> runtime commands) and the VisualEvents the animation work relies on (orb channel /
// evoke / passive, stars, card generated, power removed, block expired, forge, summon, unplayable).
// Build: make -f Makefile.sdl build/debug_events_test ; run: ./build/debug_events_test
#include <cstdio>
#include "portable_env.h"
#include <cstdlib>

#include "../source/core/char_necrobinder.h"
#include "../source/core/char_regent.h"
#include "../source/core/debug_cmds.h"
#include "../source/core/game.h"
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
static bool pump(const std::function<bool()>& done, int maxFrames = 4000) {
  for (int i = 0; i < maxFrames && !done(); ++i) Scheduler::get().update(0.05);
  return done();
}
static void runTask(Task<> t) {
  bool done = false;
  Scheduler::get().spawn(wrap(std::move(t), &done));
  pump([&] { return done; });
}
static Task<> commandTask(Run* r, std::string line, bool* ok) { *ok = co_await dbg::runCommand(*r, std::move(line)); }
static bool command(Run& r, const char* line) {
  bool ok = false;
  runTask(commandTask(&r, line, &ok));
  return ok;
}

static const char* const kSwitches[] = {"STS_POWERS", "STS_ORBS", "STS_STARS", "STS_ENERGY", "STS_HAND", "STS_PILE_DRAW",
                                         "STS_PILE_DISCARD"};

// The first player turn of a fight against the Nibbits with `character`.
struct Fight {
  std::unique_ptr<Run> r = std::make_unique<Run>();
  Combat* c = nullptr;
  explicit Fight(const char* character) {
    r->start(3, character);
    Scheduler::get().spawn(fightTask(r.get()));
    pump([&] { return r->combat && r->combat->playerPhase && r->combat->actions.waiting(); });
    c = r->combat.get();
    for (Creature* e : c->enemies) e->hp = e->maxHp = 500;
  }
  ~Fight() { Scheduler::get().clear(); }
  int count(VisualEvent::Kind k) const {
    int n = 0;
    for (auto& e : c->events) n += e.kind == k;
    return n;
  }
  const VisualEvent* last(VisualEvent::Kind k) const {
    for (auto it = c->events.rbegin(); it != c->events.rend(); ++it)
      if (it->kind == k) return &*it;
    return nullptr;
  }
};

static void parsing() {
  CHECK(dbg::powerId("Strength") == "StrengthPower");
  CHECK(dbg::powerId("STRENGTH_POWER") == "StrengthPower");
  CHECK(dbg::powerId("VulnerablePower") == "VulnerablePower");
  CHECK(dbg::powerId("NoSuchThing").empty());
  CHECK(dbg::orbId("Lightning") == "LightningOrb");
  CHECK(dbg::orbId("FROST_ORB") == "FrostOrb");
  CHECK(dbg::cardId("STRIKE_IRONCLAD") == "StrikeIronclad");
  CHECK(dbg::potionId("Fire") == "FirePotion");

  auto p = dbg::parsePowers("Strength:3:0, Vulnerable:2:1,Bogus:1,Weak");
  CHECK(p.size() == 3);
  if (p.size() == 3) {
    CHECK(p[0].id == "StrengthPower" && p[0].amount == 3 && p[0].idx == 0);
    CHECK(p[1].id == "VulnerablePower" && p[1].amount == 2 && p[1].idx == 1);
    CHECK(p[2].id == "WeakPower" && p[2].amount == 1 && p[2].idx == 0);
  }
  auto cards = dbg::parseCards("Bash+,STRIKE_IRONCLAD,,Nope");
  CHECK(cards.size() == 2);
  if (cards.size() == 2) {
    CHECK(cards[0].id == "Bash" && cards[0].upgraded);
    CHECK(cards[1].id == "StrikeIronclad" && !cards[1].upgraded);
  }
  auto orbs = dbg::parseOrbs("Lightning,Frost,Dark,Plasma,Glass,Fire");
  CHECK(orbs.size() == 5 && orbs[4] == "GlassOrb");
  CHECK(dbg::decodeCommand("power_Strength_3_0") == "power Strength 3 0");
  CHECK(dbg::splitList("a b  c", ' ').size() == 3);
}

// The combat-start switches on a Defect fight (it also gets CrackedCore's Lightning).
static void combatStartSwitches() {
  setenv("STS_POWERS", "Strength:3:0,Vulnerable:2:1", 1);
  setenv("STS_ORBS", "Frost,Dark", 1);
  setenv("STS_STARS", "4", 1);
  setenv("STS_ENERGY", "7", 1);
  setenv("STS_HAND", "Zap+,Dualcast,Bash", 1);
  setenv("STS_PILE_DRAW", "Inflame", 1);
  setenv("STS_PILE_DISCARD", "Anger,Anger", 1);
  {
    Fight f("Defect");
    Combat& c = *f.c;
    CHECK(c.player->powerAmount<StrengthPower>() == 3);
    CHECK(c.enemies[0]->powerAmount<VulnerablePower>() == 2);
    CHECK(c.orbQueue.size() == 3);  // Frost, Dark, then CrackedCore's Lightning on turn 1
    if (c.orbQueue.size() == 3) CHECK(c.orbQueue[0]->id == "FrostOrb" && c.orbQueue[1]->id == "DarkOrb");
    CHECK(c.stars == 4);
    CHECK(c.energy == 7);
    CHECK(c.hand.size() == 3);
    if (c.hand.size() == 3) {
      CHECK(c.hand[0]->id == "Zap" && c.hand[0]->upgraded());
      CHECK(c.hand[1]->id == "Dualcast" && c.hand[2]->id == "Bash");
    }
    CHECK(!c.draw.empty() && c.draw.front()->id == "Inflame");
    CHECK(c.discard.size() == 2 && c.discard[0]->id == "Anger");
    // Events: the three channels with their slots, the star gain.
    CHECK(f.count(VisualEvent::OrbChannel) == 3);
    const VisualEvent* ch = f.last(VisualEvent::OrbChannel);
    CHECK(ch && ch->text == "LightningOrb" && ch->slot == 2 && ch->who == c.player);
    const VisualEvent* st = f.last(VisualEvent::StarsGain);
    CHECK(st && st->amount == 4);
  }
  for (const char* k : kSwitches) unsetenv(k);
}

// Runtime commands and the events they (and the rules) push.
static void commandsAndEvents() {
  for (const char* k : kSwitches) unsetenv(k);
  Fight f("Defect");
  Combat& c = *f.c;
  c.orbQueue.clear();
  c.events.clear();

  // orb: channel, then a full queue evokes the oldest.
  CHECK(command(*f.r, "orb_Lightning"));
  CHECK(command(*f.r, "orb Frost"));
  CHECK(command(*f.r, "orb_Dark"));
  CHECK(f.count(VisualEvent::OrbChannel) == 3);
  CHECK(command(*f.r, "orb_Plasma"));
  const VisualEvent* ev = f.last(VisualEvent::OrbEvoke);
  CHECK(ev && ev->text == "LightningOrb" && ev->slot == 0);
  CHECK(c.orbQueue.size() == 3 && c.orbQueue.back()->id == "PlasmaOrb");
  runTask(cmd::evokeNextOrb(c));
  CHECK(f.count(VisualEvent::OrbEvoke) == 2);
  runTask(cmd::orbPassive(c, c.orbQueue[0].get(), nullptr, true));
  const VisualEvent* pv = f.last(VisualEvent::OrbPassive);
  CHECK(pv && pv->slot == 0 && pv->text == c.orbQueue[0]->id);

  // stars: gain and spend.
  CHECK(command(*f.r, "stars_3"));
  CHECK(c.stars == 3 && f.last(VisualEvent::StarsGain) && f.last(VisualEvent::StarsGain)->amount == 3);
  runTask(cmd::loseStars(c, 2));
  CHECK(f.last(VisualEvent::StarsSpend) && f.last(VisualEvent::StarsSpend)->amount == 2);

  // card: generated into the named pile.
  size_t hand = c.hand.size();
  CHECK(command(*f.r, "card_Bash+"));
  CHECK(c.hand.size() == hand + 1 && c.hand.back()->id == "Bash" && c.hand.back()->upgraded());
  const VisualEvent* cg = f.last(VisualEvent::CardGenerated);
  CHECK(cg && cg->card == c.hand.back() && cg->pile == Pile::Hand);
  CHECK(command(*f.r, "card_Inflame_draw"));
  CHECK(c.draw.front()->id == "Inflame" && f.last(VisualEvent::CardGenerated)->pile == Pile::Draw);
  runTask([](Combat* cb) -> Task<> { co_await cmd::addStatusCards(*cb, "Dazed", Pile::Discard, 1); }(&c));
  CHECK(f.last(VisualEvent::CardGenerated)->text == "Dazed" && f.last(VisualEvent::CardGenerated)->pile == Pile::Discard);
  cmd::upgradeCard(c.draw.front());
  CHECK(f.last(VisualEvent::CardUpgraded) && f.last(VisualEvent::CardUpgraded)->card == c.draw.front());

  // power: applied by index (0 player, 1.. enemies: no Osty here), then removed.
  CHECK(command(*f.r, "power_Strength_2_0"));
  CHECK(command(*f.r, "power_Weak_1_2"));
  CHECK(c.player->powerAmount<StrengthPower>() == 2);
  CHECK(c.enemies[1]->powerAmount<WeakPower>() == 1);
  runTask(cmd::removePower(c.player->get<StrengthPower>()));
  const VisualEvent* pr = f.last(VisualEvent::PowerRemoved);
  CHECK(pr && pr->who == c.player && pr->text == "STRENGTH_POWER");

  // block / damage / energy.
  int blk = c.player->block, hp0 = c.enemies[0]->hp;  // the Frost / Lightning evokes above already hit
  CHECK(command(*f.r, "block_9"));
  CHECK(c.player->block == blk + 9);
  CHECK(command(*f.r, "damage_5_1"));
  CHECK(c.enemies[0]->hp == hp0 - 5);
  int e = c.energy;
  CHECK(command(*f.r, "energy_2"));
  CHECK(c.energy == e + 2);
  CHECK(!command(*f.r, "frobnicate"));
  CHECK(!command(*f.r, "power_NoSuchPower_1_0"));

  // Unplayable: a refused play pushes the reason.
  c.energy = 0;
  Card* bash = c.hand.back();
  CHECK(c.notePlayRejected(bash));
  const VisualEvent* un = f.last(VisualEvent::Unplayable);
  CHECK(un && un->text == "NOT_ENOUGH_ENERGY" && un->card == bash);

  // Block expires at the next turn start (the enemies' block too: they gained none here).
  c.events.clear();
  PlayerAction a;
  a.kind = PlayerAction::EndTurn;
  c.player->hp = c.player->maxHp = 999;
  c.actions.fire(a);
  pump([&] { return c.over || (c.turnNumber > 1 && c.playerPhase && c.actions.waiting()); });
  CHECK(f.count(VisualEvent::BlockExpired) >= 1);

  // kill all ends the fight.
  CHECK(command(*f.r, "kill_all"));
  CHECK(c.over || c.ending);
}

// Regent's Forge and Necrobinder's Osty (summon / revive; STS_POWERS index 1 = Osty once summoned).
static void forgeAndSummon() {
  {
    Fight f("Regent");
    f.c->events.clear();
    runTask([](Combat* cb) -> Task<> { co_await cmd::forge(*cb, Dec(5), nullptr); }(f.c));
    runTask([](Combat* cb) -> Task<> { co_await cmd::forge(*cb, Dec(3), nullptr); }(f.c));
    CHECK(f.count(VisualEvent::Forge) == 2);
    const VisualEvent* fe = f.last(VisualEvent::Forge);
    CHECK(fe && fe->text == "Increased" && fe->amount == 3 && fe->card && fe->card->id == "SovereignBlade");
    CHECK(f.c->events.size() > 0 && f.count(VisualEvent::CardGenerated) == 1);
  }
  {
    setenv("STS_POWERS", "Strength:2:1", 1);
    Fight f("Necrobinder");
    unsetenv("STS_POWERS");
    CHECK(f.c->osty != nullptr);
    if (f.c->osty) CHECK(f.c->osty->powerAmount<StrengthPower>() == 2);
    CHECK(dbg::creatures(*f.c).size() == 1 + 1 + f.c->enemies.size());
    CHECK(f.count(VisualEvent::Summon) >= 1);
    if (f.c->osty) {
      runTask(cmd::kill({f.c->osty}));
      f.c->events.clear();
      runTask([](Combat* cb) -> Task<> { co_await summonOsty(*cb, 4); }(f.c));
      CHECK(f.count(VisualEvent::OstyRevive) == 1);
      const VisualEvent* se = f.last(VisualEvent::Summon);
      CHECK(se && se->who == f.c->osty && se->amount == 4 && se->text == "Revive");
    }
  }
}

int main() {
  setenv("STS_NO_NEOW", "1", 1);
  db::init();  // the registries (Run::start does it too)
  parsing();
  combatStartSwitches();
  commandsAndEvents();
  forgeAndSummon();
  printf("debug_events_test: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
