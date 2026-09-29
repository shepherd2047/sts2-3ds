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
    // DebilitatePower.ModifyVulnerableMultiplier (Necrobinder, held by the target): doubles the bonus.
    if (target->power("DebilitatePower")) mult += mult - Dec(1);
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
    // DebilitatePower.ModifyWeakMultiplier (held by the Weak attacker): doubles the penalty.
    if (owner->power("DebilitatePower")) mult -= Dec(1) - mult;
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


// ---------------------------------------------------------------- shared "next turn" / Vigor powers
// One class per power id: Creature::get<P>() finds powers by id and static_casts, so two classes
// with the same id would be undefined behaviour. Define shared powers here, not per file.

// VigorPower.cs: Amount extra damage on the owner's next powered, card-sourced attack; afterwards
// only the amount it had when that attack started is taken off. PORT NOTE: the C# keys on the
// AttackCommand (BeforeAttack/AfterAttack); here the attack is the card being played.
struct VigorPower : Power {
  POWER_HEADER(VigorPower, "VIGOR_POWER")
  Card* consuming = nullptr;
  int amountWhenStarted = 0;
  Dec modifyDamageAdditive(Creature*, Dec, int props, Creature* dealer, Card* src) override {
    if (owner != dealer || !isPoweredAttack(props)) return 0;
    if (consuming && src && src != consuming) return 0;
    return amount;
  }
  Task<> beforeCardPlayed(const CardPlay& p) override {
    if (p.card->type != CardType::Attack || ownerOf(p.card) != owner || consuming) return {};
    consuming = p.card;
    amountWhenStarted = amount;
    return {};
  }
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (!consuming || p.card != consuming) co_return;
    consuming = nullptr;
    co_await cmd::modifyPowerAmount(this, Dec(-amountWhenStarted), nullptr, nullptr);
  }
};

// DrawCardsNextTurnPower.cs: draw Amount more cards at the start of the next turn.
struct DrawCardsNextTurnPower : Power {
  POWER_HEADER(DrawCardsNextTurnPower, "DRAW_CARDS_NEXT_TURN_POWER")
  Dec modifyHandDraw(Dec count) override { return amountOnTurnStart == 0 ? count : count + Dec(amount); }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner) && amountOnTurnStart != 0) co_await cmd::removePower(this);
  }
};

// EnergyNextTurnPower.cs: gain Amount energy after the next energy reset.
struct EnergyNextTurnPower : Power {
  POWER_HEADER(EnergyNextTurnPower, "ENERGY_NEXT_TURN_POWER")
  Task<> afterEnergyReset() override {
    co_await cmd::gainEnergy(*owner->combat, amount);
    co_await cmd::removePower(this);
  }
};

// BlockNextTurnPower.cs: gain Amount block when the owner's block is next cleared.
struct BlockNextTurnPower : Power {
  POWER_HEADER(BlockNextTurnPower, "BLOCK_NEXT_TURN_POWER")
  Task<> afterBlockCleared(Creature* c) override {
    if (c != owner) co_return;
    flash = 1.f;
    co_await cmd::gainBlock(owner, Dec(amount), kUnpowered, nullptr);
    co_await cmd::removePower(this);
  }
};

}  // namespace sts
