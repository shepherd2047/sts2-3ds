// The colorless card pool (ColorlessCardPool) helpers: CardFactory over the pool for combat effects
// (Quasar, BundleOfJoy, SpectrumShift, OrangeDough, ColorlessPotion, ...) and for rewards (EndlessConveyor,
// BrainLeech). The pool list itself is db::colorlessCards / db::isColorless (game.h, colorless_pool.cpp).
#pragma once
#include "game.h"

namespace sts {

// CardFactory.GetDistinctForCombat(cards, n, CombatCardGeneration): FilterForCombat (no Basic / Ancient /
// Event, CanBeGeneratedInCombat) then TakeRandom(n) = shuffle + take. `ids` are card ids in pool order.
std::vector<std::unique_ptr<Card>> distinctForCombat(Combat& c, std::vector<std::string> ids, int n);
// The same over the ColorlessCardPool (single-player: no multiplayer-only cards).
std::vector<std::unique_ptr<Card>> colorlessDistinctForCombat(Combat& c, int n);

// CardSelectCmd.FromChooseACardScreen(canSkip) over `options` + (`free`: SetToFreeThisTurn) +
// AddGeneratedCardToCombat(Hand). Nothing is added when the player skips.
Task<> chooseGeneratedToHand(Combat& c, std::vector<std::unique_ptr<Card>> options, bool free);

// CardFactory.CreateForReward(player, n, ForNonCombatWithDefaultOdds(ColorlessCardPool)): `n` distinct cards,
// each with a base-odds rarity roll (Rewards stream), no upgrade roll.
std::vector<std::unique_ptr<Card>> colorlessRewardCards(Run& r, int n);

}  // namespace sts
