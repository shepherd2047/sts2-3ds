// Shared power models (translated from MegaCrit.Sts2.Core.Models.Powers).
#pragma once
#include <algorithm>

#include "game.h"

namespace sts {

inline bool contains(const std::vector<Creature*>& v, Creature* c) { return std::find(v.begin(), v.end(), c) != v.end(); }
inline Creature* ownerOf(Card* c) { return c && c->combat ? c->combat->player : nullptr; }

// ================================================================ powers

#define POWER_HEADER(Name, Key)                              \
  static constexpr const char* kId = #Name;                  \
  Name() { id = #Name; locKey = Key; }

struct StrengthPower : Power {
  POWER_HEADER(StrengthPower, "STRENGTH_POWER")
  bool allowNegative() const override { return true; }
  Dec modifyDamageAdditive(Creature*, Dec, int props, Creature* dealer, Card*) override {
    if (owner != dealer || !isPoweredAttack(props)) return 0;
    return amount;
  }
};

struct DexterityPower : Power {
  POWER_HEADER(DexterityPower, "DEXTERITY_POWER")
  bool allowNegative() const override { return true; }
  Dec modifyBlockAdditive(Creature* target, Dec, int props, Card* src) override {
    if (src) { if (ownerOf(src) != owner) return 0; }
    else if (owner != target) return 0;
    if (!isPoweredBlock(props)) return 0;
    return amount;
  }
};

struct VulnerablePower : Power {
  POWER_HEADER(VulnerablePower, "VULNERABLE_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  Dec modifyDamageMultiplicative(Creature* target, Dec, int props, Creature* dealer, Card*) override {
    if (target != owner || !isPoweredAttack(props)) return 1;
    Dec mult = Dec::lit(1.5);
    // CrueltyPower.ModifyVulnerableMultiplier: dealer's Cruelty adds Amount/100.
    // (Cruelty never boosts damage against its own owner.)
    if (dealer && target != dealer) {
      if (Power* cruelty = dealer->power("CrueltyPower")) mult += Dec(cruelty->amount) / Dec(100);
    }
    // PaperPhrog.ModifyVulnerableMultiplier: +0.25 against anyone but its owner.
    if (!target->isPlayer && target->combat && target->combat->run->hasRelic("PaperPhrog")) mult += Dec::lit(0.25);
    return mult;
  }
  Task<> afterSideTurnEnd(Side side, const std::vector<Creature*>&) override {
    if (side == Side::Enemy) co_await cmd::tickDownDuration(this);
  }
};

struct WeakPower : Power {
  POWER_HEADER(WeakPower, "WEAK_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  Dec modifyDamageMultiplicative(Creature* target, Dec, int props, Creature* dealer, Card*) override {
    if (dealer != owner || !isPoweredAttack(props)) return 1;
    Dec mult = Dec::lit(0.75);
    // PaperKrane.ModifyWeakMultiplier (Silent relic, held by the target): a Weak attacker deals
    // 15% less on top when it hits the relic's owner.
    if (target && target->isPlayer && target->combat && target->combat->run->hasRelic("PaperKrane")) mult -= Dec::lit(0.15);
    return mult;
  }
  Task<> afterSideTurnEnd(Side side, const std::vector<Creature*>&) override {
    if (side == Side::Enemy) co_await cmd::tickDownDuration(this);
  }
};

struct FrailPower : Power {
  POWER_HEADER(FrailPower, "FRAIL_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  Dec modifyBlockMultiplicative(Creature* target, Dec, int props, Card*) override {
    if (owner != target || !isPoweredBlock(props)) return 1;
    return Dec::lit(0.75);
  }
  Task<> afterSideTurnEnd(Side side, const std::vector<Creature*>&) override {
    if (side == Side::Enemy) co_await cmd::tickDownDuration(this);
  }
};

struct ShrinkPower : Power {
  POWER_HEADER(ShrinkPower, "SHRINK_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  bool infinite() const { return amount < 0; }
  StackType stackType() const override { return infinite() ? StackType::Single : StackType::Counter; }
  bool allowNegative() const override { return true; }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (!infinite() && contains(participants, owner)) co_await cmd::decrement(this);
  }
  Task<> afterDeath(Creature* c) override {
    if (c == applier) co_await cmd::removePower(this);
  }
  Dec modifyDamageMultiplicative(Creature*, Dec, int props, Creature* dealer, Card*) override {
    if (owner != dealer || !isPoweredAttack(props)) return 1;
    return (Dec(100) - Dec(30)) / Dec(100);
  }
};

struct SlipperyPower : Power {
  POWER_HEADER(SlipperyPower, "SLIPPERY_POWER")
  Dec modifyHpLostAfterOsty(Creature* target, Dec amount, int, Creature*, Card*) override {
    if (target != owner || amount < Dec(1)) return amount;
    return 1;
  }
  Task<> afterDamageReceived(Creature* target, const DamageResult& r, int, Creature*, Card*) override {
    if (target == owner && r.unblocked >= 1) {
      flash = 1.f;
      co_await cmd::decrement(this);
    }
  }
};

struct TerritorialPower : Power {
  POWER_HEADER(TerritorialPower, "TERRITORIAL_POWER")
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) {
      flash = 1.f;
      co_await applyPower<StrengthPower>(owner, amount, owner, nullptr);
    }
  }
};

// EnergyNextTurnPower.cs: grants Amount energy at the start of next turn (AfterEnergyReset), then
// removes itself. Shared by an Ancients effect (ancients_later.cpp) and Necrobinder's Invoke
// (char_necrobinder_cards.cpp: X4.2) -- moved here so both register the same id once.
struct EnergyNextTurnPower : Power {
  POWER_HEADER(EnergyNextTurnPower, "ENERGY_NEXT_TURN_POWER")
  Task<> afterEnergyReset() override {
    co_await cmd::gainEnergy(*owner->combat, amount);
    co_await cmd::removePower(this);
  }
};

// TemporaryStrengthPower (via SetupStrikePower).
struct SetupStrikePower : Power {
  POWER_HEADER(SetupStrikePower, "TEMPORARY_STRENGTH_POWER")
  Task<> beforeApplied(Creature* target, Dec amt, Creature* app, Card* src) override {
    co_await applyPower<StrengthPower>(target, amt, app, src, true);
  }
  Task<> afterPowerAmountChanged(Power* p, Dec amt, Creature* app, Card* src) override {
    if (!(amt == Dec(amount)) && p == this) co_await applyPower<StrengthPower>(owner, amt, app, src, true);
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) {
      flash = 1.f;
      Creature* o = owner;
      int a = amount;
      co_await cmd::removePower(this);
      co_await applyPower<StrengthPower>(o, -a, o, nullptr);
    }
  }
};


}  // namespace sts
