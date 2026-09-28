// Defect systems checks (X2.0): the orb queue (channel/evoke order, a full queue evoking the
// oldest, addOrbSlots/removeOrbSlots), Focus, and each orb's passive + evoke (Lightning, Frost,
// Dark's growth, Plasma, Glass).
// X2.1 additions: the starter deck (StrikeDefect/DefendDefect/Zap/Dualcast), the 8 character
// relics and the 3 potions.
// Build: make -f Makefile.sdl build/defect_test ; run: ./build/defect_test
#include <cstdio>
#include <cstdlib>

#include "../source/core/char_defect.h"

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

// The first turn of a fight against the Nibbits (two enemies), Defect character (orbSlots = 3).
// CrackedCore (the Defect's starting relic, X2.1) channels 1 Lightning orb on turn 1; the plain
// orb-queue engine checks below (X2.0) don't care about it, so it's cleared by default -- pass
// clearStarterOrbs=false to see it (used by the CrackedCore test itself).
struct Fight {
  std::unique_ptr<Run> r = std::make_unique<Run>();
  Combat* c = nullptr;
  // `extraRelics` are wired in before the fight starts, so a relic whose effect fires from
  // beforeSideTurnStart/afterRoomEntered on turn 1 (CrackedCore, PowerCell, RunicCapacitor,
  // SymbioticVirus, DataDisk) can be observed; one added after construction (addRelic()) is too
  // late for those but fine for anything triggered by a command run during the test body.
  explicit Fight(bool clearStarterOrbs = true, std::vector<std::string> extraRelics = {}) {
    r->start(3, "Defect");
    for (auto& id : extraRelics) {
      auto rel = db::relic(id);
      if (!rel) continue;
      rel->run = r.get();
      r->relics.push_back(std::move(rel));
    }
    Scheduler::get().spawn(fightTask(r.get()));
    pump([&] { return r->combat && r->combat->playerPhase && r->combat->actions.waiting(); });
    c = r->combat.get();
    for (Creature* e : c->enemies) e->hp = e->maxHp = 500;
    if (clearStarterOrbs) c->orbQueue.clear();
  }
  ~Fight() { Scheduler::get().clear(); }
  Creature* enemy(int i = 0) { return c->enemies[i]; }
  int enemyHpSum() { int s = 0; for (Creature* e : c->enemies) s += e->hp; return s; }
  Card* find(const std::string& id) {
    for (Card* k : c->allCards()) if (k->id == id) return k;
    return nullptr;
  }
  void toHand(Card* k) { c->removeFromPiles(k); c->hand.push_back(k); }
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
    c->actions.fire(a);
    pump([&] { return c->over || (c->turnNumber > turn && c->playerPhase && c->actions.waiting()); });
  }
  template <class P> void apply(Creature* t, int amount) {
    runTask([](Creature* t, int amount, Creature* by) -> Task<> { co_await applyPower<P>(t, amount, by, nullptr); }(t, amount, c->player));
  }
  // Adds a fresh relic of type R (from the registry, so DynVars/RELIC_HEADER match), wired to
  // this run/combat as Run::fight() would for a starting relic.
  Relic* addRelic(const std::string& id) {
    auto rel = db::relic(id);
    rel->run = r.get();
    rel->combat = c;
    Relic* w = rel.get();
    r->relics.push_back(std::move(rel));
    return w;
  }
};

int main() {
  db::init();
  {  // Character plumbing: orb capacity comes from Character::orbSlots.
    Fight f;
    CHECK(f.c->orbCapacity == 3);
    CHECK(f.c->orbQueue.empty());
  }
  {  // Channel order + evoke order (oldest first / newest first).
    Fight f;
    runTask(cmd::channelOrb(*f.c, std::make_unique<LightningOrb>()));
    runTask(cmd::channelOrb(*f.c, std::make_unique<FrostOrb>()));
    CHECK(f.c->orbQueue.size() == 2);
    CHECK(f.c->orbQueue[0]->id == "LightningOrb" && f.c->orbQueue[1]->id == "FrostOrb");
    // evokeNextOrb evokes the oldest (front, Lightning) and dequeues it.
    int hpSum = f.enemyHpSum();
    runTask(cmd::evokeNextOrb(*f.c));
    CHECK(f.c->orbQueue.size() == 1 && f.c->orbQueue[0]->id == "FrostOrb");
    CHECK(hpSum - f.enemyHpSum() == 8);  // LightningOrb::evokeVal()
    // evokeLastOrb (== only orb left, Frost) grants block and dequeues it too.
    int block = f.c->player->block;
    runTask(cmd::evokeLastOrb(*f.c));
    CHECK(f.c->orbQueue.empty());
    CHECK(f.c->player->block - block == 5);  // FrostOrb::evokeVal()
  }
  {  // A full queue (capacity 3) evokes the oldest to make room for a new channel.
    Fight f;
    runTask(cmd::channelOrb(*f.c, std::make_unique<PlasmaOrb>()));
    runTask(cmd::channelOrb(*f.c, std::make_unique<FrostOrb>()));
    runTask(cmd::channelOrb(*f.c, std::make_unique<GlassOrb>()));
    CHECK(f.c->orbQueue.size() == 3);
    int energy = f.c->energy;
    runTask(cmd::channelOrb(*f.c, std::make_unique<LightningOrb>()));  // queue full: evokes Plasma first
    CHECK(f.c->orbQueue.size() == 3);
    CHECK(f.c->energy - energy == 2);  // PlasmaOrb::evokeVal() from being auto-evoked
    CHECK(f.c->orbQueue[0]->id == "FrostOrb" && f.c->orbQueue[1]->id == "GlassOrb" && f.c->orbQueue[2]->id == "LightningOrb");
  }
  {  // addOrbSlots / removeOrbSlots.
    Fight f;
    runTask(cmd::addOrbSlots(*f.c, 3));
    CHECK(f.c->orbCapacity == 6);
    runTask(cmd::addOrbSlots(*f.c, 100));
    CHECK(f.c->orbCapacity == 10);  // OrbQueue.maxCapacity
    runTask(cmd::channelOrb(*f.c, std::make_unique<FrostOrb>()));
    runTask(cmd::channelOrb(*f.c, std::make_unique<GlassOrb>()));
    runTask(cmd::channelOrb(*f.c, std::make_unique<PlasmaOrb>()));
    CHECK(f.c->orbQueue.size() == 3);
    cmd::removeOrbSlots(*f.c, 8);
    CHECK(f.c->orbCapacity == 2);
    CHECK(f.c->orbQueue.size() == 2);  // excess removed from the back (Plasma dropped)
    CHECK(f.c->orbQueue[0]->id == "FrostOrb" && f.c->orbQueue[1]->id == "GlassOrb");
  }
  {  // Focus modifies passive/evoke values (Lightning), floored at 0, but never Plasma's energy.
    Fight f;
    LightningOrb lightning;
    lightning.owner = f.c->player;
    CHECK(lightning.passiveVal() == Dec(3) && lightning.evokeVal() == Dec(8));
    f.apply<FocusPower>(f.c->player, 2);
    CHECK(lightning.passiveVal() == Dec(5) && lightning.evokeVal() == Dec(10));
    f.apply<FocusPower>(f.c->player, -100);
    CHECK(lightning.passiveVal() == Dec(0) && lightning.evokeVal() == Dec(0));  // dmax floor, not negative
    PlasmaOrb plasma;
    plasma.owner = f.c->player;
    CHECK(plasma.passiveVal() == Dec(1) && plasma.evokeVal() == Dec(2));  // unaffected by Focus
  }
  {  // Lightning: passive/evoke hit one random hittable enemy.
    Fight f;
    runTask(cmd::channelOrb(*f.c, std::make_unique<LightningOrb>()));
    int hpSum = f.enemyHpSum();
    runTask(cmd::orbPassive(*f.c, f.c->orbQueue[0].get()));
    CHECK(hpSum - f.enemyHpSum() == 3);  // LightningOrb::passiveVal()
    CHECK((f.enemy(0)->hp == f.enemy(0)->maxHp) != (f.enemy(1)->hp == f.enemy(1)->maxHp));  // exactly one hit
    hpSum = f.enemyHpSum();
    runTask(cmd::evokeNextOrb(*f.c));
    CHECK(hpSum - f.enemyHpSum() == 8);  // LightningOrb::evokeVal()
  }
  {  // Frost: passive/evoke gain block; beforeTurnEndOrbTrigger fires it at the end of the turn.
    // (Checked via a relic's afterSideTurnEnd, before the block clears at the *next* turn start.)
    Fight f;
    struct BlockProbe : Relic {
      int seen = -1;
      BlockProbe() { id = "BlockProbe"; locKey = "BLOCK_PROBE"; }
      Task<> afterSideTurnEnd(Side s, const std::vector<Creature*>& p) override {
        if (s == Side::Player) seen = p[0]->block;
        co_return;
      }
    };
    auto probe = std::make_unique<BlockProbe>();
    BlockProbe* w = probe.get();
    f.r->relics.push_back(std::move(probe));
    runTask(cmd::channelOrb(*f.c, std::make_unique<FrostOrb>()));
    f.endTurn();
    CHECK(w->seen == 2);  // FrostOrb::passiveVal(), triggered by beforeTurnEndOrbTrigger
  }
  {  // Dark: passive grows the evoke value; evoke hits the weakest hittable enemy for it.
    Fight f;
    runTask(cmd::channelOrb(*f.c, std::make_unique<DarkOrb>()));
    auto* dark = static_cast<DarkOrb*>(f.c->orbQueue[0].get());
    CHECK(dark->evokeVal() == Dec(6));
    f.endTurn();  // one BeforeTurnEndOrbTrigger: 6 -> 12
    CHECK(dark->evokeVal() == Dec(12));
    f.enemy(0)->hp = 200; f.enemy(1)->hp = 100;  // enemy(1) is weakest
    int hp1 = f.enemy(1)->hp;
    runTask(cmd::evokeNextOrb(*f.c));
    CHECK(hp1 - f.enemy(1)->hp == 12 && f.enemy(0)->hp == 200);
  }
  {  // Plasma: passive/evoke give energy; AfterTurnStartOrbTrigger fires it at the start of the turn.
    Fight f;
    runTask(cmd::channelOrb(*f.c, std::make_unique<PlasmaOrb>()));
    int energy = f.c->energy;
    runTask(cmd::evokeNextOrb(*f.c, false));  // dequeue=false: stays queued, evoke only
    CHECK(f.c->energy - energy == 2);  // PlasmaOrb::evokeVal()
    CHECK(f.c->orbQueue.size() == 1);
    f.endTurn();  // AfterTurnStartOrbTrigger of the *next* player turn (enemy turn is empty here)
    CHECK(f.c->energy == f.c->maxEnergy + 1);  // energy reset to max, then + PlasmaOrb::passiveVal()
  }
  {  // Glass: passive hits every enemy for a shrinking amount (floor 0); evoke doubles it, unspent.
    Fight f;
    runTask(cmd::channelOrb(*f.c, std::make_unique<GlassOrb>()));
    auto* glass = static_cast<GlassOrb*>(f.c->orbQueue[0].get());
    CHECK(glass->passiveVal() == Dec(4) && glass->evokeVal() == Dec(8));
    int hpSum = f.enemyHpSum();
    runTask(cmd::orbPassive(*f.c, glass));
    CHECK(hpSum - f.enemyHpSum() == 8);  // both enemies hit for 4 each
    CHECK(glass->passiveVal() == Dec(3));  // shrunk by 1
    hpSum = f.enemyHpSum();
    runTask(cmd::evokeLastOrb(*f.c, false));  // evokeVal() = 3*2 = 6 each, orb stays queued
    CHECK(hpSum - f.enemyHpSum() == 12);
    CHECK(glass->passiveVal() == Dec(3));  // evoke does not consume the value
  }

  // ================================================================ X2.1: starter deck

  {  // StrikeDefect / DefendDefect: identical numbers to the Ironclad's Strike/Defend.
    Fight f;
    Card* strike = f.find("StrikeDefect");
    Card* defend = f.find("DefendDefect");
    CHECK(strike && defend);
    f.toHand(strike);
    int hpSum = f.enemyHpSum();
    f.play(strike, f.enemy());
    CHECK(hpSum - f.enemyHpSum() == 6);
    f.toHand(defend);
    int block = f.c->player->block;
    f.play(defend);
    CHECK(f.c->player->block - block == 5);
  }
  {  // Zap: channels a Lightning orb.
    Fight f;
    Card* zap = f.find("Zap");
    CHECK(zap);
    f.toHand(zap);
    f.play(zap);
    CHECK(f.c->orbQueue.size() == 1 && f.c->orbQueue[0]->id == "LightningOrb");
  }
  {  // Dualcast: evokes the front orb twice, dequeuing on the second call; the C#'s
    // `Orbs.Count > 0` guard means an empty queue does nothing at all.
    Fight f;
    Card* dualcast = f.find("Dualcast");
    CHECK(dualcast);
    f.toHand(dualcast);
    f.play(dualcast);
    CHECK(f.c->orbQueue.empty());
    runTask(cmd::channelOrb(*f.c, std::make_unique<LightningOrb>()));
    int hpSum = f.enemyHpSum();
    f.toHand(dualcast);
    f.play(dualcast);
    CHECK(f.c->orbQueue.empty());  // evoked (and dequeued) twice
    CHECK(hpSum - f.enemyHpSum() == 16);  // two evokes at LightningOrb::evokeVal() == 8 each
  }

  // ================================================================ X2.1: relics

  {  // CrackedCore (starting relic): channels 1 Lightning orb at the start of turn 1.
    Fight f(false);
    CHECK(f.c->orbQueue.size() == 1 && f.c->orbQueue[0]->id == "LightningOrb");
  }
  {  // DataDisk: gain Focus on entering a combat room. Wired in before the fight starts (see
    // Fight's extraRelics) so AfterRoomEntered fires for it, as it would for a run relic.
    Fight f(true, {"DataDisk"});
    CHECK(f.c->player->powerAmount<FocusPower>() == 1);
  }
  {  // EmotionChip: if unblocked damage was received since the last player-turn start, every
    // queued orb's passive triggers (countAffectedByHooks) at the next player-turn start.
    Fight f;
    auto* chip = f.addRelic("EmotionChip");
    runTask(cmd::channelOrb(*f.c, std::make_unique<LightningOrb>()));
    int hpSum = f.enemyHpSum();
    runTask(chip->afterPlayerTurnStart());  // no damage taken yet: no trigger
    CHECK(hpSum == f.enemyHpSum());
    runTask([](Creature* t) -> Task<> { co_await cmd::damage(t, Dec(5), kUnblockable, nullptr, nullptr); }(f.c->player));
    runTask(chip->afterPlayerTurnStart());
    CHECK(hpSum - f.enemyHpSum() == 3);  // LightningOrb::passiveVal(); orb stays queued
    CHECK(f.c->orbQueue.size() == 1);
    hpSum = f.enemyHpSum();
    runTask(chip->afterPlayerTurnStart());  // flag consumed by the previous call: no repeat
    CHECK(hpSum == f.enemyHpSum());
  }
  {  // GoldPlatedCables: the front (oldest queued) orb's passive triggers one extra time.
    Fight f;
    f.addRelic("GoldPlatedCables");
    runTask(cmd::channelOrb(*f.c, std::make_unique<LightningOrb>()));
    runTask(cmd::channelOrb(*f.c, std::make_unique<FrostOrb>()));
    int hpSum = f.enemyHpSum();
    runTask(cmd::orbPassive(*f.c, f.c->orbQueue[0].get(), nullptr, true));  // front: triggers twice
    CHECK(hpSum - f.enemyHpSum() == 6);  // 2x LightningOrb::passiveVal() (3 each)
    int block = f.c->player->block;
    runTask(cmd::orbPassive(*f.c, f.c->orbQueue[1].get(), nullptr, true));  // not front: once
    CHECK(f.c->player->block - block == 2);  // 1x FrostOrb::passiveVal()
  }
  {  // PowerCell: adds 2 random free (0-cost, non-X) cards from the draw pile to the hand.
    // Direct invocation of beforeSideTurnStart (added after Fight's own turn-1 start already
    // fired with the starter deck's normal, non-zero costs); see the Fight comment above.
    Fight f;
    auto* cell = f.addRelic("PowerCell");
    for (Card* k : f.c->draw) k->cost = 0;  // make every drawn card free for a deterministic pick
    size_t handBefore = f.c->hand.size();
    runTask(cell->beforeSideTurnStart(Side::Player, {f.c->player}));
    CHECK(f.c->hand.size() - handBefore == 2);  // DynamicVars.Cards
  }
  {  // Metronome: every 7th orb channeled this combat hits every hittable enemy for 30 (Unpowered).
    Fight f;
    auto* metro = f.addRelic("Metronome");
    int hpSum = f.enemyHpSum();
    for (int i = 0; i < 6; ++i) runTask(metro->afterOrbChanneled(nullptr));
    CHECK(hpSum == f.enemyHpSum());  // not yet
    runTask(metro->afterOrbChanneled(nullptr));  // 7th
    CHECK(hpSum - f.enemyHpSum() == 60);  // both enemies hit for 30 each
  }
  {  // RunicCapacitor: adds 3 orb slots (direct invocation; see PowerCell above).
    Fight f;
    auto* cap = f.addRelic("RunicCapacitor");
    int capacity = f.c->orbCapacity;
    runTask(cap->afterSideTurnStart(Side::Player, {f.c->player}));
    CHECK(f.c->orbCapacity - capacity == 3);
  }
  {  // SymbioticVirus: channels 1 Dark orb (direct invocation; see PowerCell above).
    Fight f;
    auto* virus = f.addRelic("SymbioticVirus");
    runTask(virus->afterSideTurnStart(Side::Player, {f.c->player}));
    CHECK(f.c->orbQueue.size() == 1 && f.c->orbQueue[0]->id == "DarkOrb");
  }

  // ================================================================ X2.1: potions

  {  // FocusPotion: gain Focus.
    Fight f;
    auto p = db::potion("FocusPotion");
    CHECK(p != nullptr);
    p->run = f.r.get();
    runTask(p->onUse(f.c->player));
    CHECK(f.c->player->powerAmount<FocusPower>() == 2);
  }
  {  // EssenceOfDarkness: channels a Dark orb per orb slot.
    Fight f;
    auto p = db::potion("EssenceOfDarkness");
    CHECK(p != nullptr);
    p->run = f.r.get();
    runTask(p->onUse(f.c->player));
    CHECK(f.c->orbQueue.size() == (size_t)f.c->orbCapacity);
    for (auto& o : f.c->orbQueue) CHECK(o->id == "DarkOrb");
  }
  {  // PotionOfCapacity: adds 2 orb slots.
    Fight f;
    auto p = db::potion("PotionOfCapacity");
    CHECK(p != nullptr);
    p->run = f.r.get();
    int capacity = f.c->orbCapacity;
    runTask(p->onUse(f.c->player));
    CHECK(f.c->orbCapacity - capacity == 2);
  }

  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
