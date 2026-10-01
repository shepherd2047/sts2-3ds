// Underdocks bosses (package A11d), translated from MegaCrit.Sts2.Core.Models.Monsters /
// .Powers / .Encounters: WaterfallGiant (+SteamEruption), SoulFysh (Beckon statuses,
// Intangible), LagavulinMatriarch (+Asleep, Plating). Everything lives in an anonymous
// namespace; the encounters are registered as RoomType::Boss (the Underdocks act itself is
// package A11f). Music parameters, sfx loops, Spine track animations (build-up, eyes) and the
// sleeping vfx are dropped.
// PORT NOTE (n/a: visual): the monster-specific animator triggers (Sleep/sleep_loop, Beckon, IntangibleStart,
// Erupt, die_loop) have no counterpart in the generic UI trigger table: moves play the default
// cast/attack animations and waking uses the "Unstun" trigger (wake_up).
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

Task<> applyById(const char* powerId, Creature* target, Dec amount, Creature* applier) {
  auto p = db::power(powerId);
  if (p) co_await cmd::applyPower(std::move(p), target, amount, applier, nullptr);
}

// CardPileCmd.AddGeneratedCardToCombat(card, PileType.Draw, null, CardPilePosition.Random).
Task<> addToDrawRandom(Combat& c, const char* cardId) {
  Card* card = c.addCard(db::card(cardId));
  card->createdByPlayer = false;  // creator null
  c.removeFromPiles(card);
  int at = c.rng("Shuffle").nextInt((int)c.draw.size() + 1);
  c.draw.insert(c.draw.begin() + at, card);
  for (Model* m : c.listeners()) co_await m->afterCardEnteredCombat(card);
}

// ================================================================ powers

struct WaterfallGiant;
struct LagavulinMatriarch;

// WaterfallGiant: while it has this power its death does not end the combat; instead it comes
// back with (practically) infinite HP and blows up for the stored amount two turns later.
struct SteamEruptionPower : Power {
  POWER_HEADER(SteamEruptionPower, "STEAM_ERUPTION_POWER")
  Task<> afterDeath(Creature* c) override;
  bool shouldStopCombatFromEnding() override { return true; }
  bool shouldCreatureBeRemovedFromCombatAfterDeath(Creature* c) override { return c != owner; }
  bool removedAfterOwnerDeath() const override { return false; }
};

// LagavulinMatriarch: asleep (with Plating) for Amount turns; unblocked damage wakes it at once
// (stunned, then SLASH_MOVE).
struct AsleepPower : Power {
  POWER_HEADER(AsleepPower, "ASLEEP_POWER")
  Task<> afterDamageReceived(Creature* target, const DamageResult& r, int, Creature*, Card*) override;
  Task<> beforeSideTurnEndVeryEarly(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner) || amount > 1) co_return;
    if (Power* p = owner->power("PlatingPower")) co_await cmd::removePower(p);
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override;
};

// ================================================================ Waterfall Giant

struct WaterfallGiant : Monster {
  MONSTER_HEADER(WaterfallGiant, "WATERFALL_GIANT")
  int currentPressureGunDamage = 0;
  int steamEruptionDamage = 0;
  bool isAboutToBlow = false;
  MoveState* aboutToBlow = nullptr;
  MoveState* pressureGun = nullptr;
  MoveState* explode = nullptr;
  int minHp() const override { return asc(kToughEnemies, 250, 240); }
  int maxHp() const override { return minHp(); }
  int siphonHeal() const { return asc(kToughEnemies, 15, 10); }
  int basePressureGunDamage() const { return asc(kDeadlyEnemies, 23, 20); }
  static constexpr int kPressureGunIncrease = 5;
  void buildMoves() override {
    currentPressureGunDamage = basePressureGunDamage();  // AfterAddedToRoom
    auto* pressurize = machine.add<MoveState>("PRESSURIZE_MOVE");
    pressurize->perform = [this](Targets) { return applyToSelf<SteamEruptionPower>(asc(kDeadlyEnemies, 20, 15)); };
    pressurize->intents = {kindIntent(Intent::Buff)};
    auto* stomp = machine.add<MoveState>("STOMP_MOVE");
    stomp->perform = [this](Targets t) { return stompMove(t); };
    stomp->intents = {attackIntent(asc(kDeadlyEnemies, 16, 15)), kindIntent(Intent::Debuff), kindIntent(Intent::Buff)};
    auto* ram = machine.add<MoveState>("RAM_MOVE");
    ram->perform = [this](Targets) { return attackThenPressure(asc(kDeadlyEnemies, 11, 10)); };
    ram->intents = {attackIntent(asc(kDeadlyEnemies, 11, 10)), kindIntent(Intent::Buff)};
    auto* siphon = machine.add<MoveState>("SIPHON_MOVE");
    siphon->perform = [this](Targets) { return siphonMove(); };
    siphon->intents = {kindIntent(Intent::Heal), kindIntent(Intent::Buff)};
    pressureGun = machine.add<MoveState>("PRESSURE_GUN_MOVE");
    pressureGun->perform = [this](Targets) { return pressureGunMove(); };
    pressureGun->intents = {attackIntent(currentPressureGunDamage), kindIntent(Intent::Buff)};
    auto* pressureUp = machine.add<MoveState>("PRESSURE_UP_MOVE");
    pressureUp->perform = [this](Targets) { return attackThenPressure(asc(kDeadlyEnemies, 14, 13)); };
    pressureUp->intents = {attackIntent(asc(kDeadlyEnemies, 14, 13)), kindIntent(Intent::Buff)};
    aboutToBlow = machine.add<MoveState>("ABOUT_TO_BLOW_MOVE");
    aboutToBlow->perform = [this](Targets) { return aboutToBlowMove(); };
    aboutToBlow->intents = {kindIntent(Intent::Stun)};
    aboutToBlow->mustPerformOnce = true;
    // PORT NOTE (n/a: visual): DeathBlowIntent (the skull intent) is shown as a plain attack intent.
    explode = machine.add<MoveState>("EXPLODE_MOVE");
    explode->perform = [this](Targets) { return explodeMove(); };
    explode->intents = {attackIntent(steamEruptionDamage)};
    pressurize->followUp = stomp;
    stomp->followUp = ram;
    ram->followUp = siphon;
    siphon->followUp = pressureGun;
    pressureGun->followUp = pressureUp;
    pressureUp->followUp = stomp;
    aboutToBlow->followUp = explode;
    explode->followUp = explode;
    machine.start(pressurize);
  }
  Task<> stompMove(std::vector<Creature*> targets) {
    co_await attack(asc(kDeadlyEnemies, 16, 15));
    co_await applyToTargets<WeakPower>(targets, 1);
    co_await applyToSelf<SteamEruptionPower>(3);
  }
  Task<> attackThenPressure(int damage) {
    co_await attack(damage);
    co_await applyToSelf<SteamEruptionPower>(3);
  }
  Task<> siphonMove() {
    // The intent reads CurrentPressureGunDamage; Pressure Gun always follows Siphon, so the
    // shown value is refreshed here (after the previous Pressure Gun has been checked).
    pressureGun->intents[0].damage = currentPressureGunDamage;
    co_await cmd::heal(creature, siphonHeal() * 1);  // SiphonHeal * Players.Count (single player)
    co_await applyToSelf<SteamEruptionPower>(3);
  }
  Task<> pressureGunMove() {
    co_await attack(currentPressureGunDamage);
    currentPressureGunDamage += kPressureGunIncrease;
    co_await applyToSelf<SteamEruptionPower>(3);
  }
  Task<> aboutToBlowMove() {
    steamEruptionDamage = creature->powerAmount<SteamEruptionPower>();
    explode->intents[0].damage = steamEruptionDamage;
    if (Power* p = creature->get<SteamEruptionPower>()) co_await cmd::removePower(p);
  }
  Task<> explodeMove() {
    co_await attack(steamEruptionDamage);
    if (creature->alive()) co_await cmd::kill({creature});
  }
  void triggerAboutToBlowState() {
    isAboutToBlow = true;
    // The animator's "Dead" trigger is gated on !IsAboutToBlow: drop the Death visual that
    // Kill just queued so the giant does not play its death animation and fade out.
    auto& ev = combat->events;
    for (auto it = ev.rbegin(); it != ev.rend(); ++it)
      if (it->kind == VisualEvent::Death && it->who == creature) { ev.erase(std::next(it).base()); break; }
    creature->maxHp = 999999999;  // CreatureCmd.SetMaxAndCurrentHp
    creature->hp = 999999999;
    creature->hpDisplay = HpDisplay::InfiniteWithoutNumbers;
    setMoveImmediate(aboutToBlow, true);
  }
};

// C# checks !wasRemovalPrevented; nothing in this engine prevents an enemy's removal while it
// holds this power, so the check always passes.
Task<> SteamEruptionPower::afterDeath(Creature* c) {
  if (c != owner || !owner->monster || owner->monster->id != "WaterfallGiant") co_return;
  static_cast<WaterfallGiant*>(owner->monster.get())->triggerAboutToBlowState();
}

// ================================================================ Soul Fysh

struct SoulFysh : Monster {
  MONSTER_HEADER(SoulFysh, "SOUL_FYSH")
  bool isInvisible = false;  // drives the intangible animations only
  int minHp() const override { return asc(kToughEnemies, 221, 211); }
  int maxHp() const override { return minHp(); }
  void buildMoves() override {
    auto* beckon = machine.add<MoveState>("BECKON_MOVE");
    beckon->perform = [this](Targets) { return beckonMove(); };
    beckon->intents = {kindIntent(Intent::Status, 2)};
    auto* deGas = machine.add<MoveState>("DE_GAS_MOVE");
    deGas->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 18, 16)); };
    deGas->intents = {attackIntent(asc(kDeadlyEnemies, 18, 16))};
    auto* gaze = machine.add<MoveState>("GAZE_MOVE");
    gaze->perform = [this](Targets) { return gazeMove(); };
    gaze->intents = {attackIntent(asc(kDeadlyEnemies, 8, 7)), kindIntent(Intent::Status, 1)};
    auto* fade = machine.add<MoveState>("FADE_MOVE");
    fade->perform = [this](Targets) { return fadeMove(); };
    fade->intents = {kindIntent(Intent::Buff)};
    auto* scream = machine.add<MoveState>("SCREAM_MOVE");
    scream->perform = [this](Targets t) { return screamMove(t); };
    scream->intents = {attackIntent(asc(kDeadlyEnemies, 15, 13)), kindIntent(Intent::Debuff)};
    beckon->followUp = deGas;
    deGas->followUp = gaze;
    gaze->followUp = fade;
    fade->followUp = scream;
    scream->followUp = beckon;
    machine.start(beckon);
  }
  // One Beckon at a random position of the draw pile, one into the discard pile.
  Task<> beckonMove() {
    co_await addToDrawRandom(*combat, "Beckon");
    co_await cmd::addStatusCards(*combat, "Beckon", Pile::Discard, 1);
  }
  Task<> gazeMove() {
    co_await attack(asc(kDeadlyEnemies, 8, 7));
    co_await cmd::addStatusCards(*combat, "Beckon", Pile::Discard, 1);
  }
  Task<> fadeMove() {
    isInvisible = true;
    co_await applyById("IntangiblePower", creature, 2, creature);
  }
  Task<> screamMove(std::vector<Creature*> targets) {
    isInvisible = false;
    co_await attack(asc(kDeadlyEnemies, 15, 13));
    co_await applyToTargets<VulnerablePower>(targets, 3);
  }
};

// ================================================================ Lagavulin Matriarch

struct LagavulinMatriarch : Monster {
  MONSTER_HEADER(LagavulinMatriarch, "LAGAVULIN_MATRIARCH")
  bool isAwake = false;
  int minHp() const override { return asc(kToughEnemies, 233, 222); }
  int maxHp() const override { return minHp(); }
  Task<> afterAddedToRoom() override {
    // Sleep()
    isAwake = false;
    co_await applyById("PlatingPower", creature, 12, creature);
    co_await applyToSelf<AsleepPower>(3);
  }
  void buildMoves() override {
    auto* sleep = machine.add<MoveState>("SLEEP_MOVE");
    sleep->perform = [](Targets) -> Task<> { co_return; };
    sleep->intents = {kindIntent(Intent::Sleep)};
    auto* slash = machine.add<MoveState>("SLASH_MOVE");
    slash->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 21, 19)); };
    slash->intents = {attackIntent(asc(kDeadlyEnemies, 21, 19))};
    auto* slash2 = machine.add<MoveState>("SLASH2_MOVE");
    slash2->perform = [this](Targets) { return slash2Move(); };
    slash2->intents = {attackIntent(asc(kDeadlyEnemies, 14, 12)), kindIntent(Intent::Defend)};
    auto* disembowel = machine.add<MoveState>("DISEMBOWEL_MOVE");
    disembowel->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 10, 9), 2); };
    disembowel->intents = {attackIntent(asc(kDeadlyEnemies, 10, 9), 2)};
    auto* siphon = machine.add<MoveState>("SOUL_SIPHON_MOVE");
    siphon->perform = [this](Targets t) { return soulSiphonMove(t); };
    siphon->intents = {kindIntent(Intent::Debuff), kindIntent(Intent::Buff)};
    auto* branch = machine.add<ConditionalBranchState>("SLEEP_BRANCH");
    sleep->followUp = branch;
    slash->followUp = disembowel;
    disembowel->followUp = slash2;
    slash2->followUp = siphon;
    siphon->followUp = slash;
    branch->add(sleep, [this] { return creature->get<AsleepPower>() != nullptr; });
    branch->add(slash, [this] { return creature->get<AsleepPower>() == nullptr; });
    machine.start(sleep);
  }
  Task<> wakeUpMove(std::vector<Creature*>) {
    if (isAwake) co_return;
    combat->push({VisualEvent::Anim, creature, 0, "Unstun"});  // plays wake_up
    co_await wait(0.6);
    isAwake = true;
  }
  Task<> slash2Move() {
    co_await attack(asc(kDeadlyEnemies, 14, 12));
    co_await cmd::gainBlock(creature, asc(kToughEnemies, 14, 12), kMove, nullptr);
  }
  Task<> soulSiphonMove(std::vector<Creature*> targets) {
    co_await applyToTargets<StrengthPower>(targets, -2);
    co_await applyToTargets<DexterityPower>(targets, -2);
    co_await applyToSelf<StrengthPower>(2);
  }
};

Task<> AsleepPower::afterDamageReceived(Creature* target, const DamageResult& r, int, Creature*, Card*) {
  if (target != owner || r.unblocked == 0) co_return;
  Creature* o = owner;
  if (Power* p = o->power("PlatingPower")) co_await cmd::removePower(p);
  if (o->monster && o->monster->id == "LagavulinMatriarch") {
    auto* m = static_cast<LagavulinMatriarch*>(o->monster.get());
    o->combat->push({VisualEvent::Anim, o, 0, "Unstun"});  // plays wake_up
    co_await wait(0.6);
    m->isAwake = true;
    m->stun([m](Targets t) { return m->wakeUpMove(t); }, "SLASH_MOVE");
  }
  if (o->get<AsleepPower>() == this) co_await cmd::removePower(this);
}

Task<> AsleepPower::afterSideTurnEnd(Side, const std::vector<Creature*>& participants) {
  if (!contains(participants, owner)) co_return;
  Creature* o = owner;
  co_await cmd::decrement(this);
  if (o->powerAmount<AsleepPower>() <= 0 && o->monster && o->monster->id == "LagavulinMatriarch")
    co_await static_cast<LagavulinMatriarch*>(o->monster.get())->wakeUpMove({});
}

}  // namespace

void registerUnderdocksD() {
  db::registerPower(SteamEruptionPower::kId, [] { return std::unique_ptr<Power>(new SteamEruptionPower()); });
  db::registerPower(AsleepPower::kId, [] { return std::unique_ptr<Power>(new AsleepPower()); });

  db::registerEncounter("WaterfallGiantBoss", RoomType::Boss, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(mk<WaterfallGiant>());
    return v;
  });
  db::registerEncounter("SoulFyshBoss", RoomType::Boss, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(mk<SoulFysh>());
    return v;
  });
  db::registerEncounter("LagavulinMatriarchBoss", RoomType::Boss, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(mk<LagavulinMatriarch>());
    return v;
  });
}

}  // namespace sts
