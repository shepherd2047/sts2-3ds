// The Regent's shared systems (X3.0): Forge and the Sovereign Blade token card, plus the
// shared powers used by several cards of the Regent's pool. Cards of that pool (X3.1-X3.4)
// include this and use cmd::forge / db::card("SovereignBlade") / StarNextTurnPower. The Stars
// resource itself (Combat::stars, cmd::gainStars/loseStars/setStars, the star-cost hooks) is
// core engine plumbing in game.h/combat.cpp, like Combat::energy.
#pragma once
#include "powers.h"

namespace sts {

// StarNextTurnPower: at the start of the player's next turn (AfterEnergyReset), gain Amount
// stars, then remove itself. Used by several cards (Convergence, HiddenCache).
struct StarNextTurnPower : Power {
  POWER_HEADER(StarNextTurnPower, "STAR_NEXT_TURN_POWER")
  PowerType type() const override { return PowerType::Buff; }
  Task<> afterEnergyReset() override {
    co_await cmd::gainStars(*owner->combat, amount);
    co_await cmd::removePower(this);
  }
};

// SeekingEdgePower: does nothing itself; Sovereign Blade targets every enemy while its owner
// has it (mirrors FanOfKnivesPower / Shiv in char_silent.*).
struct SeekingEdgePower : Power {
  POWER_HEADER(SeekingEdgePower, "SEEKING_EDGE_POWER")
  StackType stackType() const override { return StackType::Single; }
  void sync(TargetType t) {
    if (!owner || !owner->combat) return;
    for (Card* c : owner->combat->allCards()) if (c->tags & tagSovereignBlade) c->target = t;
  }
  Task<> afterApplied(Creature*, Card*) override { sync(TargetType::AllEnemies); co_return; }
  Task<> afterRemoved(Creature*) override { sync(TargetType::AnyEnemy); co_return; }
  Task<> afterCardEnteredCombat(Card* c) override {
    if (c->tags & tagSovereignBlade) c->target = TargetType::AllEnemies;
    co_return;
  }
};

// ParryPower: does nothing itself; Sovereign Blade checks for it and gains block equal to its
// amount when played.
struct ParryPower : Power {
  POWER_HEADER(ParryPower, "PARRY_POWER")
};

// DrawCardsNextTurnPower: modifies the next hand draw by Amount, then removes itself once that
// draw has happened (AfterSideTurnStart, after AmountOnTurnStart has been captured). Shared by
// two X3.2 cards (Glow, GuidingStar); mirrors the private copy already in ancients_later.cpp
// (an Ancient uses the same power) since this engine keeps per-package local power definitions
// rather than a single shared one.
struct DrawCardsNextTurnPower : Power {
  POWER_HEADER(DrawCardsNextTurnPower, "DRAW_CARDS_NEXT_TURN_POWER")
  Dec modifyHandDraw(Dec count) override { return amountOnTurnStart == 0 ? count : count + Dec(amount); }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner) && amountOnTurnStart != 0) co_await cmd::removePower(this);
  }
};

// ForgeCmd.Forge: adds a Sovereign Blade to the player's hand if none of theirs is
// un-Exhausted, then adds `amount` damage to every Sovereign Blade they have (including
// exhausted ones). Returns the un-Exhausted blades; Hook.AfterForge fires after.
namespace cmd {
Task<std::vector<Card*>> forge(Combat& c, Dec amount, Model* source);
}

}  // namespace sts
