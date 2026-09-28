// The Silent's shared systems (X1.0): Poison, Shivs and what depends on them. Cards and powers of the
// Silent's pool (X1.1-X1.4) include this and use `db::card("Shiv")`, `createShivsInHand`, PoisonPower.
#pragma once
#include "powers.h"

namespace sts {

// PoisonPower: at the start of its owner's turn it deals its amount as Unblockable / Unpowered damage and
// loses 1, repeated 1 + (the opponents' AccelerantPower amounts) times, never more than the stacks.
struct AccelerantPower : Power {
  POWER_HEADER(AccelerantPower, "ACCELERANT_POWER")  // only read by PoisonPower
};

struct PoisonPower : Power {
  POWER_HEADER(PoisonPower, "POISON_POWER")
  PowerType type() const override { return PowerType::Debuff; }

  int triggerCount() const {
    int extra = 0;
    if (owner && owner->combat) {
      Combat* c = owner->combat;
      if (owner->isPlayer) {
        for (auto* e : c->aliveEnemies()) extra += e->powerAmount<AccelerantPower>();
      } else if (c->player->alive()) {
        extra += c->player->powerAmount<AccelerantPower>();
      }
    }
    return std::min(amount, 1 + extra);
  }
  // CalculateTotalDamageNextTurn: what the next turn start will deal, with damage modifiers (intent-style preview).
  int calculateTotalDamageNextTurn() const {
    Dec total = 0;
    int n = std::min(amount, triggerCount());
    for (int i = 0; i < n; ++i) {
      Dec damage = Dec(amount - i);
      if (owner->combat) damage = owner->combat->modifyDamage(owner, nullptr, damage, kUnblockable | kUnpowered, nullptr);
      total += damage;
    }
    return total.toInt();
  }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await trigger();
  }
  Task<> trigger() {
    int iterations = triggerCount();
    for (int i = 0; i < iterations; ++i) {
      co_await cmd::damage(owner, Dec(amount), kUnblockable | kUnpowered, nullptr, nullptr);
      if (owner->alive()) co_await cmd::decrement(this);
      else co_await wait(0.1);
    }
  }
};

// AccuracyPower: Shivs deal Amount more.
struct AccuracyPower : Power {
  POWER_HEADER(AccuracyPower, "ACCURACY_POWER")
  Dec modifyDamageAdditive(Creature*, Dec, int props, Creature* dealer, Card* card) override {
    if (owner != dealer || !isPoweredAttack(props) || !card || !(card->tags & tagShiv)) return 0;
    return amount;
  }
};

// FanOfKnivesPower: does nothing itself; Shivs target every enemy while their owner has it
// (the C# Shiv reads the power each time, here the Shivs' target is kept in step).
struct FanOfKnivesPower : Power {
  POWER_HEADER(FanOfKnivesPower, "FAN_OF_KNIVES_POWER")
  StackType stackType() const override { return StackType::Single; }
  void sync(TargetType t) {
    if (!owner || !owner->combat) return;
    for (Card* c : owner->combat->allCards()) if (c->tags & tagShiv) c->target = t;
  }
  Task<> afterApplied(Creature*, Card*) override { sync(TargetType::AllEnemies); co_return; }
  Task<> afterRemoved(Creature*) override { sync(TargetType::AnyEnemy); co_return; }
  Task<> afterCardEnteredCombat(Card* c) override {
    if (c->tags & tagShiv) c->target = TargetType::AllEnemies;
    co_return;
  }
};

// Shiv.CreateInHand(owner, count): the Shivs join the combat in the hand (overflow goes to the discard pile).
Task<std::vector<Card*>> createShivsInHand(Combat& c, int count);

}  // namespace sts
