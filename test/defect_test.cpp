// Defect systems checks (X2.0): the orb queue (channel/evoke order, a full queue evoking the
// oldest, addOrbSlots/removeOrbSlots), Focus, and each orb's passive + evoke (Lightning, Frost,
// Dark's growth, Plasma, Glass).
// X2.1 additions: the starter deck (StrikeDefect/DefendDefect/Zap/Dualcast), the 8 character
// relics and the 3 potions.
// X2.2 additions: the Common card pool (char_defect_cards.cpp).
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

  // ================================================================ X2.2: Common cards

  {  // Barrage: hit count == orbs currently queued (0 with an empty queue, 2 with two queued).
    Fight f;
    Card* barrage = f.c->addCard(db::card("Barrage"));
    f.toHand(barrage);
    int hpSum = f.enemyHpSum();
    f.play(barrage, f.enemy());
    CHECK(hpSum == f.enemyHpSum());  // no orbs: 0 hits, no damage
    runTask(cmd::channelOrb(*f.c, std::make_unique<LightningOrb>()));
    runTask(cmd::channelOrb(*f.c, std::make_unique<FrostOrb>()));
    barrage = f.c->addCard(db::card("Barrage"));
    f.toHand(barrage);
    hpSum = f.enemyHpSum();
    f.play(barrage, f.enemy());
    CHECK(hpSum - f.enemyHpSum() == 10);  // 2 hits x Damage(5)
  }
  {  // Claw: playing one raises the Damage var of every Claw in the fight, including itself
    // and copies not yet drawn.
    Fight f;
    Card* claw1 = f.c->addCard(db::card("Claw"));
    Card* claw2 = f.c->addCard(db::card("Claw"));  // stays in the draw pile, never played
    runTask(cmd::moveCard(*f.c, claw2, Pile::Draw, true));
    f.toHand(claw1);
    int hpSum = f.enemyHpSum();
    f.play(claw1, f.enemy());
    CHECK(hpSum - f.enemyHpSum() == 3);  // base Damage(3)
    CHECK(claw1->var("Damage")->base == Dec(5));   // 3 + Increase(2)
    CHECK(claw2->var("Damage")->base == Dec(5));   // buffed even though never played
    f.toHand(claw2);
    hpSum = f.enemyHpSum();
    f.play(claw2, f.enemy());
    CHECK(hpSum - f.enemyHpSum() == 5);
    CHECK(claw1->var("Damage")->base == Dec(7));   // buffed again by claw2's play
  }
  {  // CompileDriver: draws one card per *distinct* orb type queued, not per orb.
    Fight f;
    runTask(cmd::channelOrb(*f.c, std::make_unique<LightningOrb>()));
    runTask(cmd::addOrbSlots(*f.c, 5));
    runTask(cmd::channelOrb(*f.c, std::make_unique<FrostOrb>()));
    runTask(cmd::channelOrb(*f.c, std::make_unique<FrostOrb>()));  // duplicate type
    Card* driver = f.c->addCard(db::card("CompileDriver"));
    f.toHand(driver);
    size_t handBefore = f.c->hand.size() - 1;  // exclude the card about to be played away
    f.play(driver, f.enemy());
    CHECK(f.c->hand.size() - handBefore == 2);  // Lightning + Frost = 2 distinct types
  }
  {  // ChargeBattery: block now, +Energy at the start of the player's next turn.
    Fight f;
    Card* battery = f.c->addCard(db::card("ChargeBattery"));
    f.toHand(battery);
    int block = f.c->player->block;
    f.play(battery);
    CHECK(f.c->player->block - block == 7);
    f.endTurn();
    CHECK(f.c->energy == f.c->maxEnergy + 1);  // EnergyNextTurnPower fired on the reset
  }
  {  // FocusedStrike: attack + temporary Focus that is removed (reverted) at end of turn.
    Fight f;
    Card* strike = f.c->addCard(db::card("FocusedStrike"));
    f.toHand(strike);
    int hpSum = f.enemyHpSum();
    f.play(strike, f.enemy());
    CHECK(hpSum - f.enemyHpSum() == 9);
    CHECK(f.c->player->powerAmount<FocusPower>() == 1);
    f.endTurn();
    CHECK(f.c->player->powerAmount<FocusPower>() == 0);  // TemporaryFocusPower reverts itself
  }
  {  // GoForTheEyes: Weak only applies when the target currently intends to attack. Against the
    // Nibbits, the front enemy's first move is an attack (SLICE_MOVE), the back one a Buff (HISS).
    Fight f;
    CHECK(f.enemy(0)->monster->nextMove && f.enemy(0)->monster->nextMove->intents[0].kind == Intent::Attack);
    CHECK(f.enemy(1)->monster->nextMove && f.enemy(1)->monster->nextMove->intents[0].kind != Intent::Attack);
    Card* eyes1 = f.c->addCard(db::card("GoForTheEyes"));
    f.toHand(eyes1);
    f.play(eyes1, f.enemy(0));
    CHECK(f.enemy(0)->powerAmount<WeakPower>() == 1);
    Card* eyes2 = f.c->addCard(db::card("GoForTheEyes"));
    f.toHand(eyes2);
    f.play(eyes2, f.enemy(1));
    CHECK(f.enemy(1)->powerAmount<WeakPower>() == 0);  // not attacking: no Weak
  }
  {  // GunkUp: 3-hit attack, then a Slimed lands in the discard pile.
    Fight f;
    Card* gunk = f.c->addCard(db::card("GunkUp"));
    f.toHand(gunk);
    int hpSum = f.enemyHpSum();
    f.play(gunk, f.enemy());
    CHECK(hpSum - f.enemyHpSum() == 12);  // 3 x Damage(4)
    bool sawSlimed = false;
    for (Card* c : f.c->discard) sawSlimed |= c->id == "Slimed";
    CHECK(sawSlimed);
  }
  {  // Hologram: block, then optionally pulls one chosen card back from discard into hand.
    Fight f;
    Card* toDiscard = f.c->addCard(db::card("Zap"));
    runTask(cmd::moveCard(*f.c, toDiscard, Pile::Discard));
    Card* holo = f.c->addCard(db::card("Hologram"));
    f.toHand(holo);
    size_t handBefore = f.c->hand.size() - 1;
    int block = f.c->player->block;
    PlayerAction a;
    a.kind = PlayerAction::PlayCard;
    a.card = holo;
    f.c->energy = 10;
    f.c->actions.fire(a);
    pump([&] { return f.c->choice.active && f.c->choice.result.waiting(); });
    f.c->choice.result.fire({toDiscard});
    pump([&] { return f.c->playerPhase && f.c->actions.waiting(); });
    CHECK(f.c->player->block - block == 3);
    CHECK(f.c->hand.size() - handBefore == 1);
    bool inHand = false;
    for (Card* c : f.c->hand) inHand |= c == toDiscard;
    CHECK(inHand);
  }
  {  // LightningRod: block now; channels a Lightning orb on each of the next 2 energy resets.
    Fight f;
    Card* rod = f.c->addCard(db::card("LightningRod"));
    f.toHand(rod);
    int block = f.c->player->block;
    f.play(rod);
    CHECK(f.c->player->block - block == 4);
    Power* lrp = f.c->player->power("LightningRodPower");
    CHECK(lrp && lrp->amount == 2);
    f.endTurn();
    CHECK(f.c->orbQueue.size() == 1 && f.c->orbQueue[0]->id == "LightningOrb");
    lrp = f.c->player->power("LightningRodPower");
    CHECK(lrp && lrp->amount == 1);
  }
  {  // MomentumStrike: attack, then becomes 0-cost for the rest of the fight.
    Fight f;
    Card* strike = f.c->addCard(db::card("MomentumStrike"));
    CHECK(strike->cost == 1);
    f.toHand(strike);
    int hpSum = f.enemyHpSum();
    f.play(strike, f.enemy());
    CHECK(hpSum - f.enemyHpSum() == 11);
    CHECK(strike->cost == 0);
  }
  {  // Turbo: gains energy, then a Void lands in the discard pile; drawing it later costs energy.
    Fight f;
    Card* turbo = f.c->addCard(db::card("Turbo"));
    f.toHand(turbo);
    f.play(turbo);  // Fight::play sets energy to 10 right before firing the play action
    CHECK(f.c->energy == 12);  // 10 + Turbo's Energy(2); Turbo itself costs 0
    Card* voidCard = nullptr;
    for (Card* c : f.c->discard) if (c->id == "Void") voidCard = c;
    CHECK(voidCard != nullptr);
    runTask(cmd::moveCard(*f.c, voidCard, Pile::Draw, true));
    int energyBefore = f.c->energy;
    runTask([](Combat* c) -> Task<> { co_await cmd::drawCards(*c, Dec(1)); }(f.c));
    CHECK(energyBefore - f.c->energy == 1);  // Void's own Energy var, lost once drawn
  }
  {  // Uproar: a 2-hit attack, then auto-plays a random Attack card from the draw pile.
    Fight f;
    for (Card* c : f.c->draw) c->keywords |= kwUnplayable;  // force everything unplayable but one
    Card* onlyAttack = f.c->addCard(db::card("StrikeDefect"));
    runTask(cmd::moveCard(*f.c, onlyAttack, Pile::Draw, true));
    Card* uproar = f.c->addCard(db::card("Uproar"));
    f.toHand(uproar);
    int hpSum = f.enemyHpSum();
    f.play(uproar, f.enemy());
    CHECK(hpSum - f.enemyHpSum() == 12 + 6);  // 2x Damage(6) + auto-played StrikeDefect(6)
  }

  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
