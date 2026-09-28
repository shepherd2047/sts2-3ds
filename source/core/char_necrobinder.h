// The Necrobinder's shared systems (X4.0): Osty (the companion creature), Doom, Souls. Cards of
// the Necrobinder's pool (X4.1-X4.4) include this and use `summonOsty`, `DoomPower`,
// `SoulboundPower`, `NecroMasteryPower`, `createSoulsInHand` / `db::card("Soul")`.
#pragma once
#include "powers.h"

namespace sts {

// DieForYouPower (DieForYouPower.cs): applied to Osty the moment it is first summoned. While
// Osty is alive, a powered attack aimed at its owner (the player) is redirected onto Osty
// instead (ModifyUnblockedDamageTarget); Osty stays in combat (dead) rather than leaving the
// room when it dies, ready to be revived (ShouldCreatureBeRemovedFromCombatAfterDeath), and the
// power itself survives Osty's death so it can re-arm on revival (ShouldPowerBeRemovedAfterOwnerDeath).
// PORT NOTE: ShouldAllowHitting (a dead Osty can't receive powers) is not ported; no system in
// this package applies a power to a dead Osty.
struct DieForYouPower : Power {
  POWER_HEADER(DieForYouPower, "DIE_FOR_YOU_POWER")
  StackType stackType() const override { return StackType::Single; }
  bool removedAfterOwnerDeath() const override { return false; }
  bool shouldCreatureBeRemovedFromCombatAfterDeath(Creature* creature) override { return creature != owner; }
  Creature* modifyUnblockedDamageTarget(Creature* target, Dec, int props, Creature*) override {
    if (!owner || target != owner->petOwner || owner->dead() || !isPoweredAttack(props)) return target;
    return owner;
  }
};

// OstyCmd.Summon: summon Osty with `amount` HP (the player is always the summoner and owner; no
// multiplayer), or -- if Osty is already alive -- raise its max HP by `amount` instead. Returns
// Osty's creature (only null if amount == 0 and Osty has never been summoned this combat).
// PORT NOTE: Hook.ModifySummonAmount / Hook.AfterSummon and the CombatHistory entry are not
// ported; no relic or card in this package needs them yet.
Task<Creature*> summonOsty(Combat& c, int amount);

// DoomPower (DoomPower.cs): a Counter debuff that kills its owner once CurrentHp <= Amount, at
// the end of the enemy's turn (BeforeSideTurnEnd, for monsters) or the end of the player's turn
// (AfterSideTurnEnd, for the player / Osty). All doomed creatures on that side die in the same
// trigger (so Fatal-style effects can't be missed); only the first-doomed creature on a side
// actually fires the kill, to avoid killing the same set twice.
struct DoomPower : Power {
  POWER_HEADER(DoomPower, "DOOM_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  bool isOwnerDoomed() const { return owner && owner->hp <= amount; }
  static std::vector<Creature*> doomedOf(const std::vector<Creature*>& creatures);
  std::vector<Creature*> creaturesOnSide(Side side) const;
  bool shouldTrigger(const std::vector<Creature*>& participants) const;
  Task<> beforeSideTurnEnd(Side side, const std::vector<Creature*>& participants) override;
  Task<> afterSideTurnEnd(Side side, const std::vector<Creature*>& participants) override;
};

// DoomPower.DoomKill: kills `creatures` (an already-selected doomed set) and fires
// Model::afterDiedToDoom. Exposed (like the C#) for effects that force Doom to trigger early
// (EndOfDays), which are not ported in this package.
Task<> doomKill(Combat& c, std::vector<Creature*> creatures);

// SoulboundPower (SoulboundPower.cs): whenever a card its applier generated enters combat, also
// add `Amount` Soul cards to a random spot in the draw pile.
// The C#'s `creator == Applier` check reads Card::createdByPlayer (false for monster-added cards).
struct SoulboundPower : Power {
  POWER_HEADER(SoulboundPower, "SOULBOUND_POWER")
  bool isAddingSoul = false;
  Task<> afterCardEnteredCombat(Card* card) override;
};

// NecroMasteryPower (NecroMasteryPower.cs): whenever Osty loses HP, deal Amount * (HP lost)
// Unblockable/Unpowered damage to every hittable enemy.
struct NecroMasteryPower : Power {
  POWER_HEADER(NecroMasteryPower, "NECRO_MASTERY_POWER")
  Task<> afterCurrentHpChanged(Creature* creature, Dec delta) override;
};

// SummonNextTurnPower (SummonNextTurnPower.cs): at the start of next turn, summons Osty for
// `Amount` HP (or raises his max HP, summonOsty handles both), then removes itself. Invoke
// (X4.2) is its only user. Counter stack, Buff type (Power defaults), matching the C#.
struct SummonNextTurnPower : Power {
  POWER_HEADER(SummonNextTurnPower, "SUMMON_NEXT_TURN_POWER")
  Task<> afterPlayerTurnStart() override;
};

// Soul.CreateInHand: new Soul cards (Exhaust, draws `Cards`) joining the hand.
Task<std::vector<Card*>> createSoulsInHand(Combat& c, int count);
// Soul.Create + CardPilePosition.Random: a single Soul card inserted at a random spot in the
// draw pile (used by SoulboundPower). `upgraded` pre-upgrades the card before it enters combat
// (Reave, X4.2: CardCmd.Upgrade happens before AddGeneratedCardsToCombat in the C#).
Task<Card*> addSoulToDrawPileRandom(Combat& c, bool upgraded = false);

}  // namespace sts
