// E1: CardFactory's in-combat card generation (Factories/CardFactory.cs). Every effect that makes random
// cards in combat (Discovery, InfernalBlade, Attack Potion, Toolbox, CreativeAI, ...) goes through these,
// never a raw db::characterCards / db::colorlessCards + rng pick, so that FilterForCombat always applies.
#pragma once
#include <functional>

#include "game.h"

namespace sts {

namespace db {

// CardFactory.FilterForCombat's per-card test: CanBeGeneratedInCombat and not Basic / Ancient / Event.
// The port has no Event rarity (balance_check maps the C# Event cards to Token or Ancient), so Token is
// excluded as well; no pool that reaches these helpers holds a real C# Token card.
bool canGenerateInCombat(const Card& c);
// CardFactory.FilterForCombat: the ids passing canGenerateInCombat, then Distinct() (first occurrence
// kept, pool order preserved).
std::vector<std::string> filterForCombat(const std::vector<std::string>& ids);
// Owner.Character.CardPool.GetUnlockedCards(...).Where(filter) (single player: no multiplayer-only cards).
// Not yet filtered for combat: pass it to getDistinctForCombat / getForCombat.
std::vector<std::string> characterPool(const std::string& characterId, std::function<bool(const Card&)> filter = nullptr);
// ModelDb.CardPool<ColorlessCardPool>().GetUnlockedCards(...).Where(filter).
std::vector<std::string> colorlessPool(std::function<bool(const Card&)> filter = nullptr);
// The pool after FilterForCombat: what a generator can actually produce (tests, "is it empty?" checks).
std::vector<std::string> combatCardPool(const std::string& characterId, std::function<bool(const Card&)> filter = nullptr);

}  // namespace db

// CardFactory.GetDistinctForCombat(player, cards, count, rng): FilterForCombat, then
// TakeRandom(count, rng) = UnstableShuffle of the whole filtered list (size - 1 NextInt draws, whatever
// `count` is) then Take(count). Cards are created for `c` (CombatState.CreateCard).
std::vector<std::unique_ptr<Card>> getDistinctForCombat(Combat& c, const std::vector<std::string>& cards, int count, Rng& rng);
// CardFactory.GetForCombat(player, cards, count, rng): FilterForCombat, then `count` independent
// rng.NextItem picks (duplicates allowed). An empty pool makes nothing and draws nothing.
std::vector<std::unique_ptr<Card>> getForCombat(Combat& c, const std::vector<std::string>& cards, int count, Rng& rng);

// The same with the stream every C# caller passes: RunState.Rng.CombatCardGeneration.
std::vector<std::unique_ptr<Card>> distinctForCombat(Combat& c, const std::vector<std::string>& cards, int count);
std::vector<std::unique_ptr<Card>> randomForCombat(Combat& c, const std::vector<std::string>& cards, int count);
// GetDistinctForCombat over the ColorlessCardPool (Quasar, BundleOfJoy, Toolbox, ColorlessPotion, ...).
std::vector<std::unique_ptr<Card>> colorlessDistinctForCombat(Combat& c, int count);
// GetDistinctForCombat(Owner.Character.CardPool.Where(filter), 1, CombatCardGeneration).FirstOrDefault():
// null when the filtered pool is empty.
std::unique_ptr<Card> oneDistinctForCombat(Combat& c, std::function<bool(const Card&)> filter = nullptr);

}  // namespace sts
