// Underdocks monsters 1/2 (package A11a), translated from MegaCrit.Sts2.Core.Models.Monsters /
// .Powers / .Encounters: CorpseSlug (+Weak), Cultists (CalcifiedCultist, DampCultist),
// FossilStalker, GremlinMerc (+FatGremlin, SneakyGremlin), HauntedShip, LivingFog (+GasBomb),
// PunchConstruct. Everything lives in an anonymous namespace (PunchConstruct also exists,
// privately, in content_act3a.cpp).
#include <algorithm>

#include "powers.h"

namespace sts {
namespace {

#define MONSTER_HEADER(Name, Key) \
  Name() { id = #Name; locKey = Key; }

using Targets = const std::vector<Creature*>&;

Intent attackIntent(int dmg, int hits = 1) { Intent i; i.kind = Intent::Attack; i.damage = dmg; i.hits = hits; return i; }
Intent kindIntent(Intent::Kind k, int count = 0) { Intent i; i.kind = k; i.count = count; return i; }

template <class M> std::unique_ptr<Monster> mk() { return std::make_unique<M>(); }
template <class... Ms> std::vector<std::unique_ptr<Monster>> list() {
  std::vector<std::unique_ptr<Monster>> v;
  (v.push_back(mk<Ms>()), ...);
  return v;
}

// Powers registered by other files (Ritual, Minion, Artifact) are applied by id.
Task<> applyById(const char* powerId, Creature* target, Dec amount, Creature* applier) {
  co_await cmd::applyPower(db::power(powerId), target, amount, applier, nullptr);
}

// CreatureCmd.Escape: the creature leaves the room alive (no death hooks).
// PORT NOTE (n/a: visual): the UI has no escape animation; the death animation is played instead.
Task<> escapeCreature(Creature* c) {
  c->combat->push({VisualEvent::Death, c, 0});
  c->hp = 0;
  c->removed = true;
  co_return;
}

// ================================================================ powers

// CorpseSlug: when another enemy dies the slug devours it: it is stunned for a turn and
// gains Amount Strength. (IsRavenous only drives the animation; dropped.)
struct RavenousPower : Power {
  POWER_HEADER(RavenousPower, "RAVENOUS_POWER")
  Task<> afterDeath(Creature* target) override {
    if (target == owner || target->side != owner->side || owner->dead() || !owner->monster) co_return;
    flash = 1.f;
    owner->monster->stun();  // the C# StunnedMove only resets IsRavenous
    co_await applyPower<StrengthPower>(owner, amount, owner, nullptr);
  }
};

// FossilStalker: gains Amount Strength for every hit of its attack that deals unblocked damage
// (AfterAttack over AttackCommand.Results; a hit on a pet drops its owner's overflow result).
struct SuckPower : Power {
  POWER_HEADER(SuckPower, "SUCK_POWER")
  Task<> afterAttack(const cmd::Attack& a) override {
    if (a.attacker != owner || a.targetSide() == owner->side || !isPoweredAttack(a.props)) co_return;
    int n = 0;
    for (std::vector<DamageResult> hit : a.results) {
      std::vector<Creature*> petOwners;
      for (auto& r : hit) if (r.receiver && r.receiver->petOwner) petOwners.push_back(r.receiver->petOwner);
      hit.erase(std::remove_if(hit.begin(), hit.end(), [&](const DamageResult& r) { return contains(petOwners, r.receiver); }), hit.end());
      if (std::any_of(hit.begin(), hit.end(), [](const DamageResult& r) { return r.unblocked > 0; })) ++n;
    }
    if (n <= 0) co_return;
    flash = 1.f;
    co_await applyPower<StrengthPower>(owner, amount * n, owner, nullptr);
  }
};

// GremlinMerc: Instanced, one per player (Target): Amount gold is stolen from that player by each
// attack (Steal), tracked in `stolen` (DynamicVars.Gold).
struct ThieveryPower : Power {
  POWER_HEADER(ThieveryPower, "THIEVERY_POWER")
  PowerInstanceType instanceType() const override { return PowerInstanceType::Instanced; }
  int stolen = 0;
  Task<> steal() {
    Combat* c = owner->combat;
    if (!c || !c->run || !target || target->dead() || c->run->gold <= 0) co_return;
    int n = std::min(amount, c->run->gold);
    c->run->gold -= n;  // PlayerCmd.LoseGold(GoldLossType.Stolen)
    stolen += n;
  }
};

// FatGremlin: the stolen gold, returned when it is killed.
// The room's loot gets an extra GoldReward (wasGoldStolenBack) of it (CombatRoom.AddExtraReward).
struct HeistPower : Power {
  POWER_HEADER(HeistPower, "HEIST_POWER")
  PowerInstanceType instanceType() const override { return PowerInstanceType::Instanced; }
  Task<> afterDeath(Creature* c) override {
    if (c != owner || amount <= 0 || !owner->combat || !owner->combat->run) co_return;
    Run::RewardItem item;
    item.kind = Run::RewardKind::Gold;
    item.gold = amount;
    item.goldStolenBack = true;
    owner->combat->run->roomExtraRewards.push_back(std::move(item));
  }
};

// GremlinMerc: when it dies a Sneaky Gremlin and a Fat Gremlin (carrying the stolen gold)
// take its place, so the fight goes on.
// PORT NOTE: missing engine feature a per-combat gold proportion consumed by Run::combatRewards
// (EncounterModel.CalculateGoldProportion with CombatState.EscapedCreatures: half the gold if Fat
// Gremlin escapes, none if gold was stolen); GremlinMercNormal's is not implemented. The slots ("merc"/"sneaky"/"fat") are dropped too.
struct SurprisePower : Power {
  POWER_HEADER(SurprisePower, "SURPRISE_POWER")
  StackType stackType() const override { return StackType::Single; }
  bool shouldStopCombatFromEnding() override { return true; }
  Task<> afterDeath(Creature* target) override;
};

// LivingFog: after a Skill card is played this turn, every Skill card is afflicted with
// Smog (unplayable) until the end of the player's turn.
// SmoggyPower.cs: the Smog affliction (A4) blocks the card; cleared from every card at the end
// of the owner's turn.
struct SmoggyPower : Power {
  POWER_HEADER(SmoggyPower, "SMOGGY_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  StackType stackType() const override { return StackType::Single; }
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (ownerOf(p.card) != owner || p.card->type != CardType::Skill) co_return;
    flash = 1.f;
    for (Card* c : owner->combat->allCards())
      if (c->type == CardType::Skill && !c->affliction) cmd::afflict(c, "Smog", 1);
  }
  Task<> afterCardEnteredCombat(Card* c) override {
    // Only once a Skill play has started this turn (CombatHistory.CardPlaysStarted).
    if (ownerOf(c) == owner && !c->affliction && c->type == CardType::Skill && owner->combat->currentSide == Side::Player &&
        owner->combat->skillPlaysStartedThisTurn > 0)
      cmd::afflict(c, "Smog", 1);
    co_return;
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner)) co_return;
    for (Card* c : owner->combat->allCards())
      if (c->afflictedWith("Smog")) cmd::clearAffliction(c);
  }
  bool shouldPlay(Card* c) override { return ownerOf(c) != owner || !c->afflictedWith("Smog"); }
};

// ================================================================ Corpse Slugs

struct CorpseSlug : Monster {
  MONSTER_HEADER(CorpseSlug, "CORPSE_SLUG")
  int starterMoveIdx = 0;  // set by the encounter (EnsureCorpseSlugsStartWithDifferentMoves)
  int minHp() const override { return asc(kToughEnemies, 27, 25); }
  int maxHp() const override { return asc(kToughEnemies, 29, 27); }
  Task<> afterAddedToRoom() override { co_await applyToSelf<RavenousPower>(asc(kDeadlyEnemies, 5, 4)); }
  void buildMoves() override {
    auto* whip = machine.add<MoveState>("WHIP_SLAP_MOVE");
    whip->perform = [this](Targets) { return attack(3, 2); };
    whip->intents = {attackIntent(3, 2)};
    auto* glomp = machine.add<MoveState>("GLOMP_MOVE");
    glomp->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 9, 8)); };
    glomp->intents = {attackIntent(asc(kDeadlyEnemies, 9, 8))};
    auto* goop = machine.add<MoveState>("GOOP_MOVE");
    goop->perform = [this](Targets t) { return applyToTargets<FrailPower>(t, 2); };
    goop->intents = {kindIntent(Intent::Debuff)};
    whip->followUp = glomp;
    glomp->followUp = goop;
    goop->followUp = whip;
    switch (starterMoveIdx % 3) {
      case 0: machine.start(whip); break;
      case 1: machine.start(glomp); break;
      default: machine.start(goop); break;
    }
  }
};

std::vector<std::unique_ptr<Monster>> corpseSlugs(int count, Rng& rng) {
  std::vector<std::unique_ptr<Monster>> v;
  int n = rng.nextInt(3);  // EnsureCorpseSlugsStartWithDifferentMoves
  for (int i = 0; i < count; ++i) {
    auto s = std::make_unique<CorpseSlug>();
    s->starterMoveIdx = n % 3;
    ++n;
    v.push_back(std::move(s));
  }
  return v;
}

// ================================================================ Cultists

struct CalcifiedCultist : Monster {
  MONSTER_HEADER(CalcifiedCultist, "CALCIFIED_CULTIST")
  int minHp() const override { return asc(kToughEnemies, 39, 38); }
  int maxHp() const override { return asc(kToughEnemies, 42, 41); }
  void buildMoves() override {
    auto* incant = machine.add<MoveState>("INCANTATION_MOVE");
    incant->perform = [this](Targets) { return applyById("RitualPower", creature, 2, creature); };
    incant->intents = {kindIntent(Intent::Buff)};
    auto* strike = machine.add<MoveState>("DARK_STRIKE_MOVE");
    strike->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 11, 9)); };
    strike->intents = {attackIntent(asc(kDeadlyEnemies, 11, 9))};
    incant->followUp = strike;
    strike->followUp = strike;
    machine.start(incant);
  }
};

struct DampCultist : Monster {
  MONSTER_HEADER(DampCultist, "DAMP_CULTIST")
  int minHp() const override { return asc(kToughEnemies, 52, 51); }
  int maxHp() const override { return asc(kToughEnemies, 54, 53); }
  void buildMoves() override {
    auto* incant = machine.add<MoveState>("INCANTATION_MOVE");
    incant->perform = [this](Targets) { return applyById("RitualPower", creature, asc(kDeadlyEnemies, 6, 5), creature); };
    incant->intents = {kindIntent(Intent::Buff)};
    auto* strike = machine.add<MoveState>("DARK_STRIKE_MOVE");
    strike->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 3, 1)); };
    strike->intents = {attackIntent(asc(kDeadlyEnemies, 3, 1))};
    incant->followUp = strike;
    strike->followUp = strike;
    machine.start(incant);
  }
};

// ================================================================ Fossil Stalker

struct FossilStalker : Monster {
  MONSTER_HEADER(FossilStalker, "FOSSIL_STALKER")
  int minHp() const override { return asc(kToughEnemies, 54, 51); }
  int maxHp() const override { return asc(kToughEnemies, 56, 53); }
  Task<> afterAddedToRoom() override { co_await applyToSelf<SuckPower>(3); }
  void buildMoves() override {
    auto* tackle = machine.add<MoveState>("TACKLE_MOVE");
    tackle->perform = [this](Targets t) { return tackleMove(t); };
    tackle->intents = {attackIntent(asc(kDeadlyEnemies, 11, 9)), kindIntent(Intent::Debuff)};
    auto* latch = machine.add<MoveState>("LATCH_MOVE");
    latch->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 14, 12)); };
    latch->intents = {attackIntent(asc(kDeadlyEnemies, 14, 12))};
    auto* lash = machine.add<MoveState>("LASH_MOVE");
    lash->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 4, 3), 2); };
    lash->intents = {attackIntent(asc(kDeadlyEnemies, 4, 3), 2)};
    auto* rand = machine.add<RandomBranchState>("RAND");
    tackle->followUp = rand;
    latch->followUp = rand;
    lash->followUp = rand;
    rand->addMax(latch, 2);
    rand->addMax(tackle, 2);
    rand->addMax(lash, 2);
    machine.start(latch);
  }
  Task<> tackleMove(Targets t) {
    co_await attack(asc(kDeadlyEnemies, 11, 9));
    co_await applyToTargets<FrailPower>(t, 1);
  }
};

// ================================================================ Gremlin Merc

struct GremlinMerc : Monster {
  MONSTER_HEADER(GremlinMerc, "GREMLIN_MERC")
  int minHp() const override { return asc(kToughEnemies, 51, 47); }
  int maxHp() const override { return asc(kToughEnemies, 53, 49); }
  Task<> afterAddedToRoom() override {
    co_await applyToSelf<SurprisePower>(1);
    {  // one instance per player (CombatState.Players), Target = that player
      auto th = std::make_unique<ThieveryPower>();
      th->target = combat->player;
      co_await cmd::applyPower(std::move(th), creature, 20, creature, nullptr);
    }
  }
  void buildMoves() override {
    auto* gimme = machine.add<MoveState>("GIMME_MOVE");
    gimme->perform = [this](Targets) { return gimmeMove(); };
    gimme->intents = {attackIntent(asc(kToughEnemies, 8, 7), 2)};
    auto* smash = machine.add<MoveState>("DOUBLE_SMASH_MOVE");
    smash->perform = [this](Targets t) { return doubleSmashMove(t); };
    smash->intents = {attackIntent(asc(kToughEnemies, 7, 6), 2), kindIntent(Intent::Debuff)};
    auto* hehe = machine.add<MoveState>("HEHE_MOVE");
    hehe->perform = [this](Targets) { return heheMove(); };
    hehe->intents = {attackIntent(asc(kToughEnemies, 9, 8)), kindIntent(Intent::Buff)};
    gimme->followUp = smash;
    smash->followUp = hehe;
    hehe->followUp = gimme;
    machine.start(gimme);
  }
  Task<> steal() {
    for (auto* p : creature->instances<ThieveryPower>()) co_await p->steal();
  }
  Task<> gimmeMove() {
    co_await attack(asc(kToughEnemies, 8, 7), 2);
    co_await steal();
  }
  Task<> doubleSmashMove(Targets t) {
    co_await attack(asc(kToughEnemies, 7, 6), 2);
    co_await steal();
    co_await applyToTargets<WeakPower>(t, 2);
  }
  Task<> heheMove() {
    co_await attack(asc(kToughEnemies, 9, 8));
    co_await steal();
    co_await applyToSelf<StrengthPower>(2);
  }
};

struct FatGremlin : Monster {
  MONSTER_HEADER(FatGremlin, "FAT_GREMLIN")
  int minHp() const override { return asc(kToughEnemies, 14, 13); }
  int maxHp() const override { return asc(kToughEnemies, 18, 17); }
  void buildMoves() override {
    auto* spawned = machine.add<MoveState>("SPAWNED_MOVE");
    spawned->perform = [](Targets) -> Task<> { co_return; };  // wake-up animation only
    spawned->intents = {kindIntent(Intent::Stun)};
    auto* flee = machine.add<MoveState>("FLEE_MOVE");
    flee->perform = [this](Targets) { return escapeCreature(creature); };
    flee->intents = {kindIntent(Intent::Escape)};
    spawned->followUp = flee;
    flee->followUp = flee;
    machine.start(spawned);
  }
};

struct SneakyGremlin : Monster {
  MONSTER_HEADER(SneakyGremlin, "SNEAKY_GREMLIN")
  int minHp() const override { return asc(kToughEnemies, 11, 10); }
  int maxHp() const override { return asc(kToughEnemies, 15, 14); }
  void buildMoves() override {
    auto* spawned = machine.add<MoveState>("SPAWNED_MOVE");
    spawned->perform = [](Targets) -> Task<> { co_return; };  // wake-up animation only
    spawned->intents = {kindIntent(Intent::Stun)};
    auto* tackle = machine.add<MoveState>("TACKLE_MOVE");
    tackle->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 10, 9)); };
    tackle->intents = {attackIntent(asc(kDeadlyEnemies, 10, 9))};
    spawned->followUp = tackle;
    tackle->followUp = tackle;
    machine.start(spawned);
  }
};

Task<> SurprisePower::afterDeath(Creature* target) {
  if (target != owner || !owner->combat) co_return;
  Combat& c = *owner->combat;
  std::vector<std::pair<int, Creature*>> heists;  // per ThieveryPower instance: its gold and Target
  for (auto* th : owner->instances<ThieveryPower>()) heists.push_back({th->stolen, th->target});
  // PORT NOTE: the C# creates the Fat Gremlin first (its HP is rolled first) and adds it
  // after the Sneaky Gremlin; here it is created when added, so the HP rolls swap places.
  co_await cmd::addMonster(c, mk<SneakyGremlin>());
  Creature* fat = co_await cmd::addMonster(c, mk<FatGremlin>());
  for (auto& h : heists) {
    auto heist = std::make_unique<HeistPower>();
    heist->target = h.second;
    co_await cmd::applyPower(std::move(heist), fat, h.first, owner, nullptr);
  }
}

// ================================================================ Haunted Ship

struct HauntedShip : Monster {
  MONSTER_HEADER(HauntedShip, "HAUNTED_SHIP")
  int minHp() const override { return asc(kToughEnemies, 67, 63); }
  int maxHp() const override { return minHp(); }
  void buildMoves() override {
    auto* swipe = machine.add<MoveState>("SWIPE_MOVE");
    swipe->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 14, 13)); };
    swipe->intents = {attackIntent(asc(kDeadlyEnemies, 14, 13))};
    auto* stomp = machine.add<MoveState>("STOMP_MOVE");
    stomp->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 5, 4), 3); };
    stomp->intents = {attackIntent(asc(kDeadlyEnemies, 5, 4), 3)};
    auto* haunt = machine.add<MoveState>("HAUNT_MOVE");
    haunt->perform = [this](Targets t) { return hauntMove(t); };
    haunt->intents = {kindIntent(Intent::Debuff), kindIntent(Intent::Status, 5)};
    haunt->followUp = swipe;
    swipe->followUp = stomp;
    stomp->followUp = swipe;
    machine.start(haunt);
  }
  Task<> hauntMove(Targets t) {
    co_await applyToTargets<WeakPower>(t, 3);
    co_await cmd::addStatusCards(*combat, "Dazed", Pile::Discard, 5);
  }
};

// ================================================================ Living Fog

struct GasBomb : Monster {
  MONSTER_HEADER(GasBomb, "GAS_BOMB")
  int minHp() const override { return asc(kToughEnemies, 8, 7); }
  int maxHp() const override { return minHp(); }
  Task<> afterAddedToRoom() override { co_await applyById("MinionPower", creature, 1, creature); }
  void buildMoves() override {
    // PORT NOTE (n/a: visual): DeathBlowIntent has no counterpart; shown as a plain attack intent.
    auto* explode = machine.add<MoveState>("EXPLODE_MOVE");
    explode->perform = [this](Targets) { return explodeMove(); };
    explode->intents = {attackIntent(asc(kDeadlyEnemies, 9, 8))};
    machine.start(explode);
  }
  Task<> explodeMove() {
    co_await attack(asc(kDeadlyEnemies, 9, 8));
    co_await cmd::kill({creature});
  }
};

struct LivingFog : Monster {
  MONSTER_HEADER(LivingFog, "LIVING_FOG")
  int minHp() const override { return asc(kToughEnemies, 82, 80); }
  int maxHp() const override { return minHp(); }
  void buildMoves() override {
    auto* gas = machine.add<MoveState>("ADVANCED_GAS_MOVE");
    gas->perform = [this](Targets t) { return advancedGasMove(t); };
    gas->intents = {attackIntent(asc(kDeadlyEnemies, 9, 8)), kindIntent(Intent::Debuff)};  // CardDebuffIntent
    auto* bloat = machine.add<MoveState>("BLOAT_MOVE");
    bloat->perform = [this](Targets) { return bloatMove(); };
    bloat->intents = {attackIntent(asc(kDeadlyEnemies, 6, 5)), kindIntent(Intent::Summon)};
    auto* blast = machine.add<MoveState>("SUPER_GAS_BLAST_MOVE");
    blast->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 9, 8)); };
    blast->intents = {attackIntent(asc(kDeadlyEnemies, 9, 8))};
    gas->followUp = bloat;
    bloat->followUp = blast;
    blast->followUp = bloat;
    machine.start(gas);
  }
  Task<> advancedGasMove(Targets t) {
    co_await attack(asc(kDeadlyEnemies, 9, 8));
    co_await applyToTargets<SmoggyPower>(t, 1);
  }
  Task<> bloatMove() {
    // Encounter.GetNextSlot: the encounter has 5 bomb slots ("bomb1".."bomb5"); a slot is
    // free when no enemy in the room holds it. Bombs that are dead or gone free theirs.
    int bombs = 0;
    for (Creature* e : combat->enemies)
      if (e->alive() && !e->removed && e->monster && e->monster->id == "GasBomb") ++bombs;
    if (bombs < 5) co_await cmd::addMonster(*combat, mk<GasBomb>());  // BloatAmount = 1
    co_await attack(asc(kDeadlyEnemies, 6, 5));
  }
};

// ================================================================ Punch Construct

struct PunchConstruct : Monster {
  MONSTER_HEADER(PunchConstruct, "PUNCH_CONSTRUCT")
  bool startsWithFastPunch = false;  // set by the events / encounters that reuse it
  int startingHpReduction = 0;
  int minHp() const override { return asc(kToughEnemies, 60, 55); }
  int maxHp() const override { return minHp(); }
  Task<> afterAddedToRoom() override {
    co_await applyById("ArtifactPower", creature, 1, creature);
    if (startingHpReduction > 0) creature->hp = std::max(1, creature->hp - startingHpReduction);
  }
  void buildMoves() override {
    auto* ready = machine.add<MoveState>("READY_MOVE");
    ready->perform = [this](Targets) { return gainBlock(10); };
    ready->intents = {kindIntent(Intent::Defend)};
    auto* strong = machine.add<MoveState>("STRONG_PUNCH_MOVE");
    strong->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 16, 14)); };
    strong->intents = {attackIntent(asc(kDeadlyEnemies, 16, 14))};
    auto* fast = machine.add<MoveState>("FAST_PUNCH_MOVE");
    fast->perform = [this](Targets t) { return fastPunchMove(t); };
    fast->intents = {attackIntent(asc(kDeadlyEnemies, 6, 5), 2), kindIntent(Intent::Debuff)};
    ready->followUp = fast;
    fast->followUp = strong;
    strong->followUp = ready;
    machine.start(startsWithFastPunch ? fast : ready);
  }
  Task<> fastPunchMove(Targets t) {
    co_await attack(asc(kDeadlyEnemies, 6, 5), 2);
    co_await applyToTargets<FrailPower>(t, 1);
  }
};

}  // namespace

// Shared with content_underdocks_b.cpp (SeapunkNormal).
std::unique_ptr<Monster> makeCalcifiedCultist() { return std::make_unique<CalcifiedCultist>(); }

// Shared with events_underdocks.cpp (PunchOffEventEncounter: StartsWithFastPunch / StartingHpReduction).
std::unique_ptr<Monster> makePunchConstruct(bool startsWithFastPunch, int startingHpReduction) {
  auto m = std::make_unique<PunchConstruct>();
  m->startsWithFastPunch = startsWithFastPunch;
  m->startingHpReduction = startingHpReduction;
  return m;
}

void registerUnderdocksA() {
  db::registerMonster("FatGremlin", [] { return std::unique_ptr<Monster>(new FatGremlin()); });  // M10 bestiary: summons
  db::registerMonster("SneakyGremlin", [] { return std::unique_ptr<Monster>(new SneakyGremlin()); });
  db::registerMonster("GasBomb", [] { return std::unique_ptr<Monster>(new GasBomb()); });
  db::registerPower(RavenousPower::kId, [] { return std::unique_ptr<Power>(new RavenousPower()); });
  db::registerPower(SuckPower::kId, [] { return std::unique_ptr<Power>(new SuckPower()); });
  db::registerPower(ThieveryPower::kId, [] { return std::unique_ptr<Power>(new ThieveryPower()); });
  db::registerPower(HeistPower::kId, [] { return std::unique_ptr<Power>(new HeistPower()); });
  db::registerPower(SurprisePower::kId, [] { return std::unique_ptr<Power>(new SurprisePower()); });
  db::registerPower(SmoggyPower::kId, [] { return std::unique_ptr<Power>(new SmoggyPower()); });

  db::registerEncounter("CorpseSlugsNormal", RoomType::Monster, false, [](Rng& rng) { return corpseSlugs(3, rng); });
  db::registerEncounter("CorpseSlugsWeak", RoomType::Monster, true, [](Rng& rng) { return corpseSlugs(2, rng); });
  db::registerEncounter("CultistsNormal", RoomType::Monster, false,
                        [](Rng&) { return list<CalcifiedCultist, DampCultist>(); });
  db::registerEncounter("FossilStalkerNormal", RoomType::Monster, false, [](Rng&) { return list<FossilStalker>(); });
  db::registerEncounter("GremlinMercNormal", RoomType::Monster, false, [](Rng&) { return list<GremlinMerc>(); });
  db::registerEncounter("HauntedShipNormal", RoomType::Monster, false, [](Rng&) { return list<HauntedShip>(); });
  db::registerEncounter("LivingFogNormal", RoomType::Monster, false, [](Rng&) { return list<LivingFog>(); });
  db::registerEncounter("PunchConstructNormal", RoomType::Monster, false, [](Rng&) { return list<PunchConstruct>(); });
}

}  // namespace sts
