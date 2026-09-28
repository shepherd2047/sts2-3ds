// The Defect's shared systems (X2.0): Focus, and the 5 orbs (Lightning, Frost, Dark, Plasma,
// Glass). Cards of the Defect's pool (X2.1-X2.4) include this and use `cmd::channelOrb`,
// `db::orb("LightningOrb")`, FocusPower, etc. Channel/Evoke/orb-slot machinery (OrbCmd, the
// queue on Combat) is generic engine plumbing and lives in game.h / combat.cpp instead, next to
// the Silent's `cmd::discardCards`.
#pragma once
#include "powers.h"

namespace sts {

// ================================================================ Focus

// FocusPower (Models.Powers): modifies every orb value the player's own orbs produce.
// PORT NOTE: the C# also checks `Owner.Player != orb.Owner`; this port is single-player, so the
// Defect is always both the orb's and the power's owner and the check always passes.
struct FocusPower : Power {
  POWER_HEADER(FocusPower, "FOCUS_POWER")
  bool allowNegative() const override { return true; }
  Dec modifyOrbValue(Orb*, Dec value) override { return dmax(value + Dec(amount), Dec(0)); }
};

// TemporaryFocusPower (abstract in the C#: a per-card/potion/relic Focus buff/debuff that is
// removed, converting itself back into permanent Focus, at the end of the owner's turn). Copies
// the SetupStrikePower / TemporaryStrengthPower pattern (see powers.h): a concrete X2.1+ power
// derives from this and overrides isPositive() for a Focus-down effect.
struct TemporaryFocusPower : Power {
  virtual bool isPositive() const { return true; }
  int sign() const { return isPositive() ? 1 : -1; }
  PowerType type() const override { return isPositive() ? PowerType::Buff : PowerType::Debuff; }
  Task<> beforeApplied(Creature* target, Dec amt, Creature* app, Card* src) override {
    co_await applyPower<FocusPower>(target, Dec(sign()) * amt, app, src, true);
  }
  Task<> afterPowerAmountChanged(Power* p, Dec amt, Creature* app, Card* src) override {
    if (!(amt == Dec(amount)) && p == this) co_await applyPower<FocusPower>(owner, Dec(sign()) * amt, app, src, true);
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) {
      flash = 1.f;
      Creature* o = owner;
      int a = amount, s = sign();
      co_await cmd::removePower(this);
      co_await applyPower<FocusPower>(o, Dec(-s * a), o, nullptr);
    }
  }
};

// ================================================================ orbs

// LightningOrb: passive/evoke hit one random hittable enemy (Unpowered damage).
struct LightningOrb : Orb {
  ORB_HEADER(LightningOrb, "LIGHTNING_ORB")
  Dec passiveVal() override { return owner->combat->modifyOrbValue(this, Dec(3)); }
  Dec evokeVal() override { return owner->combat->modifyOrbValue(this, Dec(8)); }
  Task<> beforeTurnEndOrbTrigger() override { co_await triggerPassive(nullptr); }
  Task<> passive(Creature* target) override { co_await applyLightningDamage(passiveVal(), target); }
  Task<std::vector<Creature*>> evoke() override { co_return co_await applyLightningDamage(evokeVal(), nullptr); }

 private:
  Task<std::vector<Creature*>> applyLightningDamage(Dec value, Creature* target) {
    std::vector<Creature*> list = owner->combat->hittableEnemies();
    if (list.empty()) co_return std::vector<Creature*>{};
    Creature* t = target ? target : owner->combat->rng("CombatTargets").nextItem(list);
    co_await cmd::damage(t, value, kUnpowered, owner, nullptr);
    co_return std::vector<Creature*>{t};
  }
};

// FrostOrb: passive/evoke gain block for the player.
// PORT NOTE: the C# also gives every *other* player the same block under HibernatePower
// (multiplayer); single player has none, so that branch is dropped.
struct FrostOrb : Orb {
  ORB_HEADER(FrostOrb, "FROST_ORB")
  Dec passiveVal() override { return owner->combat->modifyOrbValue(this, Dec(2)); }
  Dec evokeVal() override { return owner->combat->modifyOrbValue(this, Dec(5)); }
  Task<> beforeTurnEndOrbTrigger() override { co_await triggerPassive(nullptr); }
  Task<> passive(Creature*) override { co_await cmd::gainBlock(owner, passiveVal(), kUnpowered, nullptr); }
  Task<std::vector<Creature*>> evoke() override {
    co_await cmd::gainBlock(owner, evokeVal(), kUnpowered, nullptr);
    co_return std::vector<Creature*>{owner};
  }
};

// DarkOrb: its passive grows a hidden evoke value (not itself run through ModifyOrbValue) that
// is spent on the weakest hittable enemy when evoked.
struct DarkOrb : Orb {
  ORB_HEADER(DarkOrb, "DARK_ORB")
  Dec _evokeVal = Dec(6);
  Dec passiveVal() override { return owner->combat->modifyOrbValue(this, Dec(6)); }
  Dec evokeVal() override { return _evokeVal; }
  Task<> beforeTurnEndOrbTrigger() override { co_await triggerPassive(nullptr); }
  Task<> passive(Creature*) override {
    _evokeVal += passiveVal();
    co_return;
  }
  Task<std::vector<Creature*>> evoke() override {
    std::vector<Creature*> hittable = owner->combat->hittableEnemies();
    if (hittable.empty()) co_return std::vector<Creature*>{};
    Creature* weakest = hittable[0];
    for (Creature* c : hittable) if (c->hp < weakest->hp) weakest = c;
    co_await cmd::damage(weakest, evokeVal(), kUnpowered, owner, nullptr);
    co_return std::vector<Creature*>{weakest};
  }
};

// PlasmaOrb: passive/evoke give energy. Note: unlike the other four, its values are never run
// through ModifyOrbValue (Focus does not affect energy), matching the C#.
struct PlasmaOrb : Orb {
  ORB_HEADER(PlasmaOrb, "PLASMA_ORB")
  Dec passiveVal() override { return Dec(1); }
  Dec evokeVal() override { return Dec(2); }
  Task<> afterTurnStartOrbTrigger() override { co_await triggerPassive(nullptr); }
  Task<> passive(Creature*) override { co_await cmd::gainEnergy(*owner->combat, passiveVal().toInt()); }
  Task<std::vector<Creature*>> evoke() override {
    co_await cmd::gainEnergy(*owner->combat, evokeVal().toInt());
    co_return std::vector<Creature*>{owner};
  }
};

// GlassOrb: passive hits every hittable enemy for a shrinking amount (floor 0, -1 per trigger);
// evoke hits everyone for double the current amount without spending it.
struct GlassOrb : Orb {
  ORB_HEADER(GlassOrb, "GLASS_ORB")
  Dec _passiveVal = Dec(4);
  Dec passiveVal() override { return owner->combat->modifyOrbValue(this, _passiveVal); }
  Dec evokeVal() override { return passiveVal() * Dec(2); }
  Task<> beforeTurnEndOrbTrigger() override { co_await triggerPassive(nullptr); }
  Task<> passive(Creature*) override {
    std::vector<Creature*> targets = owner->combat->hittableEnemies();
    Dec value = passiveVal();
    if (value <= Dec(0)) co_return;
    _passiveVal = dmax(Dec(0), _passiveVal - Dec(1));
    co_await cmd::damage(targets, value, kUnpowered, owner, nullptr);
  }
  Task<std::vector<Creature*>> evoke() override {
    std::vector<Creature*> targets = owner->combat->hittableEnemies();
    if (evokeVal() <= Dec(0)) co_return std::vector<Creature*>{};
    co_await cmd::damage(targets, evokeVal(), kUnpowered, owner, nullptr);
    co_return targets;
  }
};

}  // namespace sts
