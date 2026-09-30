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
    CHECK(f.c->energyCost(strike) == 0);  // EnergyCost.SetThisCombat(0)
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

  // ================================================================ X2.3a: Uncommon cards, first half
  {  // Capacitor / BulkUp: orb slots.
    Fight f;
    Card* cap = f.c->addCard(db::card("Capacitor"));
    f.toHand(cap);
    f.play(cap);
    CHECK(f.c->orbCapacity == 5);
    Card* bulk = f.c->addCard(db::card("BulkUp"));
    f.toHand(bulk);
    f.play(bulk);
    CHECK(f.c->orbCapacity == 4);
    CHECK(f.c->player->powerAmount<StrengthPower>() == 2);
    CHECK(f.c->player->powerAmount<DexterityPower>() == 2);
  }
  {  // Chill: one Frost orb per hittable enemy (2 Nibbits).
    Fight f;
    Card* k = f.c->addCard(db::card("Chill"));
    f.toHand(k);
    f.play(k);
    CHECK(f.c->orbQueue.size() == 2);
    CHECK(f.c->orbQueue[0]->id == "FrostOrb");
  }
  {  // Darkness: channel Dark, then every Dark orb's passive fires (Dark grows its evoke value).
    Fight f;
    runTask(cmd::channelOrb(*f.c, std::make_unique<DarkOrb>()));
    Card* k = f.c->addCard(db::card("Darkness"));
    f.toHand(k);
    f.play(k);
    CHECK(f.c->orbQueue.size() == 2);
    CHECK(f.c->orbQueue[0]->evokeVal() == Dec(12));
    CHECK(f.c->orbQueue[1]->evokeVal() == Dec(12));
  }
  {  // DoubleEnergy doubles current energy; Fuel from Compact turns a hand Status into energy.
    Fight f;
    Card* de = f.c->addCard(db::card("DoubleEnergy"));
    f.toHand(de);
    f.play(de);
    CHECK(f.c->energy == 10 - 1 + 9);
  }
  {
    Fight f;
    Card* w = f.c->addCard(db::card("Wound"));
    f.toHand(w);
    Card* cp = f.c->addCard(db::card("Compact"));
    f.toHand(cp);
    f.play(cp);
    bool hasWound = false, hasFuel = false;
    for (Card* c : f.c->hand) { if (c->id == "Wound") hasWound = true; if (c->id == "Fuel") hasFuel = true; }
    CHECK(!hasWound && hasFuel);
  }
  {  // FightThrough: 2 Wounds to discard. Glacier: 2 Frost. Glasswork: Glass. Fusion: Plasma. Null: Weak + Dark.
    Fight f;
    Card* ft = f.c->addCard(db::card("FightThrough"));
    f.toHand(ft);
    f.play(ft);
    int wounds = 0;
    for (Card* c : f.c->discard) if (c->id == "Wound") ++wounds;
    CHECK(wounds == 2);
    Card* nl = f.c->addCard(db::card("Null"));
    f.toHand(nl);
    int hp = f.enemy()->hp;
    f.play(nl, f.enemy());
    CHECK(hp - f.enemy()->hp == 10);
    CHECK(f.enemy()->powerAmount<WeakPower>() == 1);
    CHECK(f.c->orbQueue.size() == 1 && f.c->orbQueue[0]->id == "DarkOrb");
  }
  {  // Ftl: draws only while fewer than PlayMax cards were played before it this turn.
    Fight f;
    Card* a = f.c->addCard(db::card("Ftl"));
    f.toHand(a);
    size_t before = f.c->hand.size();
    f.play(a, f.enemy());
    CHECK(f.c->hand.size() == before);  // -1 played, +1 drawn
    f.c->cardsPlayedThisTurn = 3;
    Card* b = f.c->addCard(db::card("Ftl"));
    f.toHand(b);
    before = f.c->hand.size();
    f.play(b, f.enemy());
    CHECK(f.c->hand.size() == before - 1);  // 4th play this turn: no draw
  }
  {  // Hailstorm: end of turn, with a Frost orb queued, damages every enemy.
    Fight f;
    Card* h = f.c->addCard(db::card("Hailstorm"));
    f.toHand(h);
    f.play(h);
    runTask(cmd::channelOrb(*f.c, std::make_unique<FrostOrb>()));
    int hp = f.enemyHpSum();
    f.endTurn();
    CHECK(hp - f.enemyHpSum() >= 12);
  }
  {  // Loop: front orb's passive fires at turn start (Dark evoke value 6 -> 12).
    Fight f;
    runTask(cmd::channelOrb(*f.c, std::make_unique<DarkOrb>()));
    Card* l = f.c->addCard(db::card("Loop"));
    f.toHand(l);
    f.play(l);
    f.endTurn();
    Orb* dark = nullptr;
    for (auto& o : f.c->orbQueue) if (o->id == "DarkOrb") dark = o.get();
    CHECK(dark && dark->evokeVal() >= Dec(18));  // end-of-turn passive + Loop passive
  }
  {  // Iteration: first Status drawn each turn draws extra.
    Fight f;
    Card* it = f.c->addCard(db::card("Iteration"));
    f.toHand(it);
    f.play(it);
    Card* w1 = f.c->addCard(db::card("Wound"));
    Card* w2 = f.c->addCard(db::card("Wound"));
    runTask(cmd::moveCard(*f.c, w1, Pile::Draw, true));
    runTask(cmd::moveCard(*f.c, w2, Pile::Draw, true));
    size_t before = f.c->hand.size();
    runTask([](Combat* c) -> Task<> { co_await cmd::drawCards(*c, Dec(1)); }(f.c));
    CHECK(f.c->hand.size() == before + 3);  // Wound + Iteration's 2 extra cards (the 2nd Wound does not retrigger)
  }
  {  // Feral: the first 0-cost attack goes back to hand, the second does not (Amount 1).
    Fight f;
    Card* fe = f.c->addCard(db::card("Feral"));
    f.toHand(fe);
    f.play(fe);
    Card* ftl = f.c->addCard(db::card("Ftl"));
    f.toHand(ftl);
    f.play(ftl, f.enemy());
    bool inHand = std::find(f.c->hand.begin(), f.c->hand.end(), ftl) != f.c->hand.end();
    CHECK(inHand);
    f.play(ftl, f.enemy());
    inHand = std::find(f.c->hand.begin(), f.c->hand.end(), ftl) != f.c->hand.end();
    CHECK(!inHand);
  }

  {  // X2.3b: Sunder gains energy only on a kill.
    Fight f;
    Card* s = f.c->addCard(db::card("Sunder"));
    f.toHand(s);
    f.enemy()->hp = 5;
    f.play(s, f.enemy());
    CHECK(f.c->energy == 10 - 3 + 3);
    Card* s2 = f.c->addCard(db::card("Sunder"));
    f.toHand(s2);
    f.play(s2, f.enemy(1));
    CHECK(f.c->energy == 10 - 3);
  }
  {  // Storm: playing a Power channels a Lightning orb.
    Fight f;
    Card* st = f.c->addCard(db::card("Storm"));
    f.toHand(st);
    f.play(st);
    CHECK(f.c->orbQueue.empty());  // Storm itself started before the power existed
    Card* lp = f.c->addCard(db::card("Loop"));
    f.toHand(lp);
    f.play(lp);
    CHECK(f.c->orbQueue.size() == 1 && f.c->orbQueue[0]->id == "LightningOrb");
  }
  {  // Subroutine: playing a Power gains 1 energy.
    Fight f;
    Card* sr = f.c->addCard(db::card("Subroutine"));
    f.toHand(sr);
    f.play(sr);
    Card* lp = f.c->addCard(db::card("Loop"));
    f.toHand(lp);
    f.play(lp);
    CHECK(f.c->energy == 10 - 1 + 1);  // play() resets energy to 10 first
  }
  {  // Synchronize: Focus = number of distinct orb types (1 base extra, upgraded 2).
    Fight f;
    runTask(cmd::channelOrb(*f.c, std::make_unique<FrostOrb>()));
    runTask(cmd::channelOrb(*f.c, std::make_unique<FrostOrb>()));
    runTask(cmd::channelOrb(*f.c, std::make_unique<DarkOrb>()));
    Card* sy = f.c->addCard(db::card("Synchronize"));
    f.toHand(sy);
    f.play(sy);
    CHECK(f.c->player->powerAmount<FocusPower>() == 2);
    f.endTurn();
    CHECK(f.c->player->powerAmount<FocusPower>() == 0);
  }
  {  // Synthesis: the next Power costs 0.
    Fight f;
    Card* sn = f.c->addCard(db::card("Synthesis"));
    f.toHand(sn);
    f.play(sn, f.enemy());
    Card* th = f.c->addCard(db::card("Thunder"));
    f.toHand(th);
    CHECK(f.c->energyCost(th) == 0);
    Card* th2 = f.c->addCard(db::card("Thunder"));
    f.toHand(th2);
    f.play(th);
    CHECK(f.c->energyCost(th2) == 1);
  }
  {  // Tempest: X orbs (+1 upgraded); TeslaCoil triggers Lightning passives; Thunder hits on evoke.
    Fight f;
    Card* t = f.c->addCard(db::card("Tempest"));
    f.toHand(t);
    f.c->energy = 2;
    PlayerAction a;
    a.kind = PlayerAction::PlayCard;
    a.card = t;
    f.c->actions.fire(a);
    pump([&] { return f.c->playerPhase && f.c->actions.waiting(); });
    CHECK(f.c->orbQueue.size() == 2);
    Card* tc = f.c->addCard(db::card("TeslaCoil"));
    f.toHand(tc);
    int hp = f.enemy()->hp;
    f.play(tc, f.enemy());
    CHECK(hp - f.enemy()->hp == 3 + 3 + 3);
    Card* th = f.c->addCard(db::card("Thunder"));
    f.toHand(th);
    f.play(th);
    hp = f.enemy()->hp;
    int hp1 = f.enemy(1)->hp;
    runTask(cmd::evokeNextOrb(*f.c));
    CHECK(f.enemy(0)->hp < hp || f.enemy(1)->hp < hp1);
    CHECK((hp - f.enemy(0)->hp) + (hp1 - f.enemy(1)->hp) == 8 + 8);
  }
  {  // Overclock adds a Burn; Smokestack punishes it; RocketPunch gets cheaper.
    Fight f;
    Card* sm = f.c->addCard(db::card("Smokestack"));
    f.toHand(sm);
    f.play(sm);
    Card* rp = f.c->addCard(db::card("RocketPunch"));
    f.toHand(rp);
    Card* oc = f.c->addCard(db::card("Overclock"));
    f.toHand(oc);
    int hp = f.enemyHpSum();
    f.play(oc);
    CHECK(hp - f.enemyHpSum() == 10);  // 5 per enemy, two enemies
    CHECK(f.c->energyCost(rp) == 1);
  }
  {  // Scrape discards drawn cards that cost energy.
    Fight f;
    f.c->hand.clear();
    f.c->draw.clear();
    Card* z = f.c->addCard(db::card("Zap"));       // cost 1 -> discarded
    Card* fl = f.c->addCard(db::card("Ftl"));      // cost 0 -> kept
    f.c->draw.push_back(z);
    f.c->draw.push_back(fl);
    Card* sc = f.c->addCard(db::card("Scrape"));
    f.toHand(sc);
    f.play(sc, f.enemy());
    CHECK(std::find(f.c->hand.begin(), f.c->hand.end(), fl) != f.c->hand.end());
    CHECK(std::find(f.c->hand.begin(), f.c->hand.end(), z) == f.c->hand.end());
  }

  {  // X2.4 HelixDrill: one hit per energy spent this turn; Voltaic: one Lightning per one channeled so far.
    Fight f;
    Card* z = f.c->addCard(db::card("Zap"));
    f.toHand(z);
    f.play(z);
    Card* d = f.c->addCard(db::card("DefendDefect"));
    f.toHand(d);
    f.play(d);
    CHECK(f.c->energySpentThisTurn == 2);
    Card* hd = f.c->addCard(db::card("HelixDrill"));
    f.toHand(hd);
    int hp = f.enemy()->hp;
    f.play(hd, f.enemy());
    CHECK(hp - f.enemy()->hp == 3 * 2);
    int n0 = f.c->lightningOrbsChanneled;  // the Zap plus whatever the starter relic channeled
    CHECK(n0 >= 1);
    Card* v = f.c->addCard(db::card("Voltaic"));
    f.toHand(v);
    f.play(v);
    CHECK(f.c->lightningOrbsChanneled == 2 * n0);
  }
  {  // EchoForm doubles the first card each turn (EchoForm itself counts as the first this turn).
    Fight f;
    runTask(cmd::applyPower(db::power("EchoFormPower"), f.c->player, 1, f.c->player, nullptr));
    Card* s = f.c->addCard(db::card("StrikeDefect"));
    f.toHand(s);
    int hp = f.enemy()->hp;
    f.play(s, f.enemy());
    CHECK(hp - f.enemy()->hp == 12);
    Card* s2 = f.c->addCard(db::card("StrikeDefect"));
    f.toHand(s2);
    hp = f.enemy()->hp;
    f.play(s2, f.enemy());
    CHECK(hp - f.enemy()->hp == 6);
  }
  {  // GeneticAlgorithm grows on the copy and on its deck version; Buffer absorbs a hit.
    Fight f;
    auto deckCard = db::card("GeneticAlgorithm");
    Card* ga = f.c->addCard(db::card("GeneticAlgorithm"));
    ga->deckVersion.p = deckCard.get();
    f.toHand(ga);
    int blk = f.c->player->block;
    f.play(ga);
    CHECK(f.c->player->block - blk == 1);
    CHECK(deckCard->val("Block").toInt() == 4 && ga->val("Block").toInt() == 4);
    Card* bf = f.c->addCard(db::card("Buffer"));
    f.toHand(bf);
    f.play(bf);
    int buffers = 0;
    for (auto& p : f.c->player->powers) if (p->id == "BufferPower") buffers += p->amount;
    CHECK(buffers == 1);
  }
  {  // Reboot: hand to the draw pile, draw 4; Shatter: hit all, evoke each orb twice; AllForOne.
    Fight f;
    Card* rb = f.c->addCard(db::card("Reboot"));
    f.toHand(rb);
    f.play(rb);
    CHECK((int)f.c->hand.size() == 4);
    Fight g;
    runTask(cmd::channelOrb(*g.c, std::make_unique<LightningOrb>()));
    runTask(cmd::channelOrb(*g.c, std::make_unique<FrostOrb>()));
    Card* sh = g.c->addCard(db::card("Shatter"));
    g.toHand(sh);
    int hp = g.enemyHpSum();
    g.play(sh);
    CHECK(g.c->orbQueue.empty());
    CHECK(hp - g.enemyHpSum() == 14 + 16);
    Fight h;
    Card* ftl = h.c->addCard(db::card("Ftl"));
    Card* zp = h.c->addCard(db::card("Zap"));
    h.c->removeFromPiles(ftl);
    h.c->removeFromPiles(zp);
    h.c->discard.push_back(ftl);
    h.c->discard.push_back(zp);
    Card* afo = h.c->addCard(db::card("AllForOne"));
    h.toHand(afo);
    h.play(afo, h.enemy());
    CHECK(std::find(h.c->hand.begin(), h.c->hand.end(), ftl) != h.c->hand.end());
    CHECK(std::find(h.c->discard.begin(), h.c->discard.end(), zp) != h.c->discard.end());
  }
  {  // Hyperbeam's Focus loss ends with the turn; TrashToTreasure channels on a self-made Status;
     // FlakCannon hits once per Status and exhausts them; Modded costs 1 more afterwards.
    Fight f;
    Card* hb = f.c->addCard(db::card("Hyperbeam"));
    f.toHand(hb);
    f.play(hb);
    CHECK(f.c->player->powerAmount<FocusPower>() == -3);
    f.endTurn();
    CHECK(f.c->player->powerAmount<FocusPower>() == 0);
    Fight g;
    Card* tt = g.c->addCard(db::card("TrashToTreasure"));
    g.toHand(tt);
    g.play(tt);
    Card* oc = g.c->addCard(db::card("Overclock"));
    g.toHand(oc);
    g.play(oc);
    CHECK(g.c->orbQueue.size() == 1);
    Fight h;
    runTask(cmd::addStatusCards(*h.c, "Wound", Pile::Hand, 2, true));
    runTask(cmd::addStatusCards(*h.c, "Wound", Pile::Discard, 1, true));
    Card* fc = h.c->addCard(db::card("FlakCannon"));
    h.toHand(fc);
    int hp = h.enemyHpSum();
    h.play(fc);
    CHECK(hp - h.enemyHpSum() == 8 * 3);
    int wounds = 0;
    for (Card* k : h.c->exhaust) if (k->id == "Wound") ++wounds;
    CHECK(wounds == 3);
    Fight m;
    Card* md = m.c->addCard(db::card("Modded"));
    m.toHand(md);
    int cap = m.c->orbCapacity;
    m.play(md);
    CHECK(m.c->orbCapacity == cap + 1 && m.c->energyCost(md) == 1);
  }

  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
