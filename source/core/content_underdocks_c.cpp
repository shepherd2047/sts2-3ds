// Underdocks elites (package A11c), translated from MegaCrit.Sts2.Core.Models.Monsters /
// .Powers / .Encounters: PhantasmalGardener (+Skittish), SkulkingColony (+HardenedShell),
// TerrorEel (+Shriek). Everything lives in an anonymous namespace; the elites are registered
// as RoomType::Elite (the Underdocks act itself is package A11f).
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

// ================================================================ powers

// PhantasmalGardener: the first time each turn a card's attack deals unblocked damage to the
// owner, it gains Amount Block (unpowered), once the whole attack has finished (AfterAttack).
// PORT NOTE (n/a: visual): the BlockStart/BlockEnd animations and sfx are dropped.
struct SkittishPower : Power {
  POWER_HEADER(SkittishPower, "SKITTISH_POWER")
  bool hasGainedBlockThisTurn = false;
  Task<> afterAttack(const cmd::Attack& a) override {
    if (hasGainedBlockThisTurn || !(a.props & kMove) || !a.source) co_return;
    const DamageResult* first = nullptr;
    for (auto& hit : a.results)
      for (auto& r : hit)
        if (!first && r.receiver == owner) first = &r;
    if (!first || first->unblocked == 0) co_return;
    hasGainedBlockThisTurn = true;
    co_await cmd::gainBlock(owner, Dec(amount), kUnpowered, nullptr);
  }
  Task<> afterSideTurnEnd(Side side, const std::vector<Creature*>&) override {
    if (side != owner->side) hasGainedBlockThisTurn = false;
    co_return;
  }
};

// SkulkingColony: at most Amount HP can be lost per turn; the rest of every hit is ignored.
// The counter resets when any side's turn starts.
// Once the cap is reached the owner's HpDisplay turns InfiniteWithNumbers until the next turn.
// PORT NOTE: the power's DisplayAmount (remaining cap) has no counterpart.
struct HardenedShellPower : Power {
  POWER_HEADER(HardenedShellPower, "HARDENED_SHELL_POWER")
  int damageReceivedThisTurn = 0;
  Dec modifyHpLostBeforeOstyLate(Creature* target, Dec a, int, Creature*, Card*) override {
    if (target != owner || a == Dec(0)) return a;
    Dec cap = Dec(amount - damageReceivedThisTurn);
    if (cap < a) flash = 1.f;
    return std::min(a, cap);
  }
  Task<> afterDamageReceived(Creature* target, const DamageResult& r, int, Creature*, Card*) override {
    if (target != owner || r.fullyBlocked) co_return;
    damageReceivedThisTurn += r.unblocked;
    if (damageReceivedThisTurn >= amount) owner->hpDisplay = HpDisplay::InfiniteWithNumbers;
    co_return;
  }
  Task<> beforeSideTurnStart(Side, const std::vector<Creature*>&) override {
    damageReceivedThisTurn = 0;
    owner->hpDisplay = HpDisplay::Normal;
    co_return;
  }
};

// TerrorEel: once its HP falls to Amount or below (from unblocked damage) it is stunned
// (next move TERROR_MOVE) and the power is removed. Negative amounts are allowed.
struct ShriekPower : Power {
  POWER_HEADER(ShriekPower, "SHRIEK_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  bool allowNegative() const override { return true; }
  Task<> afterDamageReceived(Creature* target, const DamageResult& r, int, Creature*, Card*) override {
    if (target != owner || r.unblocked <= 0 || target->hp > amount) co_return;
    flash = 1.f;
    if (owner->monster) owner->monster->stun(nullptr, "TERROR_MOVE");
    co_await cmd::removePower(this);
  }
};

// ================================================================ Phantasmal Gardeners

// The slot ("first".."fourth") picks the opening move: Flail, Bite, Lash, Enlarge.
struct PhantasmalGardener : Monster {
  MONSTER_HEADER(PhantasmalGardener, "PHANTASMAL_GARDENER")
  int slot = 0;  // 0..3 = first..fourth
  int minHp() const override { return asc(kToughEnemies, 27, 26); }
  int maxHp() const override { return asc(kToughEnemies, 32, 31); }
  Task<> afterAddedToRoom() override { co_await applyToSelf<SkittishPower>(asc(kToughEnemies, 7, 6)); }
  void buildMoves() override {
    // PORT NOTE (n/a: visual): CurrentScale / EnlargeTriggers only drive the sprite size; dropped.
    auto* bite = machine.add<MoveState>("BITE_MOVE");
    bite->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 5, 5)); };
    bite->intents = {attackIntent(asc(kDeadlyEnemies, 5, 5))};
    auto* lash = machine.add<MoveState>("LASH_MOVE");
    lash->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 7, 7)); };
    lash->intents = {attackIntent(asc(kDeadlyEnemies, 7, 7))};
    auto* flail = machine.add<MoveState>("FLAIL_MOVE");
    flail->perform = [this](Targets) { return attack(1, asc(kDeadlyEnemies, 3, 3)); };
    flail->intents = {attackIntent(1, asc(kDeadlyEnemies, 3, 3))};
    auto* enlarge = machine.add<MoveState>("ENLARGE_MOVE");
    enlarge->perform = [this](Targets) { return applyToSelf<StrengthPower>(asc(kDeadlyEnemies, 3, 2)); };
    enlarge->intents = {kindIntent(Intent::Buff)};
    bite->followUp = lash;
    lash->followUp = flail;
    flail->followUp = enlarge;
    enlarge->followUp = bite;
    switch (slot) {
      case 0: machine.start(flail); break;
      case 1: machine.start(bite); break;
      case 2: machine.start(lash); break;
      default: machine.start(enlarge); break;
    }
  }
};

std::vector<std::unique_ptr<Monster>> phantasmalGardeners() {
  std::vector<std::unique_ptr<Monster>> v;
  for (int i = 0; i < 4; ++i) {
    auto g = std::make_unique<PhantasmalGardener>();
    g->slot = i;
    v.push_back(std::move(g));
  }
  return v;
}

// ================================================================ Skulking Colony

struct SkulkingColony : Monster {
  MONSTER_HEADER(SkulkingColony, "SKULKING_COLONY")
  int minHp() const override { return asc(kToughEnemies, 80, 75); }
  int maxHp() const override { return minHp(); }
  Task<> afterAddedToRoom() override { co_await applyToSelf<HardenedShellPower>(20); }
  void buildMoves() override {
    auto* zoom = machine.add<MoveState>("ZOOM_MOVE");
    zoom->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 16, 14)); };
    zoom->intents = {attackIntent(asc(kDeadlyEnemies, 16, 14))};
    auto* zoom2 = machine.add<MoveState>("ZOOM_MOVE_2");
    zoom2->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 16, 14)); };
    zoom2->intents = {attackIntent(asc(kDeadlyEnemies, 16, 14))};
    auto* inertia = machine.add<MoveState>("INERTIA_MOVE");
    inertia->perform = [this](Targets) { return inertiaMove(); };
    inertia->intents = {attackIntent(asc(kDeadlyEnemies, 11, 9)), kindIntent(Intent::Buff)};
    auto* stabs = machine.add<MoveState>("PIERCING_STABS_MOVE");
    stabs->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 8, 7), 2); };
    stabs->intents = {attackIntent(asc(kDeadlyEnemies, 8, 7), 2)};
    zoom->followUp = zoom2;
    zoom2->followUp = inertia;
    inertia->followUp = stabs;
    stabs->followUp = zoom;
    machine.start(zoom);
  }
  Task<> inertiaMove() {
    co_await attack(asc(kDeadlyEnemies, 11, 9));
    co_await applyToSelf<StrengthPower>(asc(kDeadlyEnemies, 4, 2));
  }
};

// ================================================================ Terror Eel

struct TerrorEel : Monster {
  MONSTER_HEADER(TerrorEel, "TERROR_EEL")
  int minHp() const override { return asc(kToughEnemies, 150, 140); }
  int maxHp() const override { return minHp(); }
  Task<> afterAddedToRoom() override { co_await applyToSelf<ShriekPower>(asc(kToughEnemies, 75, 70)); }
  void buildMoves() override {
    auto* crash = machine.add<MoveState>("CRASH_MOVE");
    crash->perform = [this](Targets) { return crashMove(); };
    crash->intents = {attackIntent(asc(kDeadlyEnemies, 18, 16))};
    auto* thrash = machine.add<MoveState>("THRASH_MOVE");
    thrash->perform = [this](Targets) { return thrashMove(); };
    thrash->intents = {attackIntent(asc(kDeadlyEnemies, 4, 3), 3), kindIntent(Intent::Buff)};
    auto* stunMove = machine.add<MoveState>("STUN_MOVE");
    stunMove->perform = [](Targets) -> Task<> { co_return; };
    stunMove->intents = {kindIntent(Intent::Stun)};
    auto* terror = machine.add<MoveState>("TERROR_MOVE");
    terror->perform = [this](Targets t) { return applyToTargets<VulnerablePower>(t, 99); };
    terror->intents = {kindIntent(Intent::Debuff)};
    crash->followUp = thrash;
    thrash->followUp = crash;
    stunMove->followUp = terror;
    terror->followUp = crash;
    machine.start(crash);
  }
  // The Vigor from Thrash is spent by this attack (VigorPower.AfterAttack).
  Task<> crashMove() { co_await attack(asc(kDeadlyEnemies, 18, 16)); }
  Task<> thrashMove() {
    co_await attack(asc(kDeadlyEnemies, 4, 3), 3);
    co_await applyToSelf<VigorPower>(6);
  }
};

}  // namespace

void registerUnderdocksC() {
  db::registerPower(SkittishPower::kId, [] { return std::unique_ptr<Power>(new SkittishPower()); });
  db::registerPower(HardenedShellPower::kId, [] { return std::unique_ptr<Power>(new HardenedShellPower()); });
  db::registerPower(ShriekPower::kId, [] { return std::unique_ptr<Power>(new ShriekPower()); });

  db::registerEncounter("PhantasmalGardenersElite", RoomType::Elite, false, [](Rng&) { return phantasmalGardeners(); });
  db::registerEncounter("SkulkingColonyElite", RoomType::Elite, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(mk<SkulkingColony>());
    return v;
  });
  db::registerEncounter("TerrorEelElite", RoomType::Elite, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(mk<TerrorEel>());
    return v;
  });
}

}  // namespace sts
