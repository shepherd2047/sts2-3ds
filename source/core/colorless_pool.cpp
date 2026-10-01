// A1a: the colorless card pool (Models.CardPools/ColorlessCardPool.cs) as a db-level pool, the
// CardFactory helpers that draw from it, and ColorlessPotion. The cards themselves are in
// colorless_cards_a.cpp (and later _b / _c).
#include <algorithm>

#include "colorless.h"

namespace sts {

namespace {

// ColorlessCardPool.GenerateAllCards, in the C# order (registered or not; reward RNG depends on it).
const std::vector<std::string>& poolIds() {
  static const std::vector<std::string> v = {
      "Alchemize", "Anointed", "Automation", "BeaconOfHope", "BeatDown", "BelieveInYou", "Bolas", "Calamity",
      "Catastrophe", "Coordinate", "DarkShackles", "Discovery", "DramaticEntrance", "Entropy", "Equilibrium",
      "EternalArmor", "Fasten", "Finesse", "Fisticuffs", "FlashOfSteel", "GangUp", "GoldAxe", "HandOfGreed",
      "HiddenGem", "HuddleUp", "Impatience", "Intercept", "JackOfAllTrades", "Jackpot", "Knockdown", "Lift",
      "MasterOfStrategy", "Mayhem", "Mimic", "MindBlast", "Nostalgia", "Omnislice", "Panache", "PanicButton",
      "PrepTime", "Production", "Prolong", "Prowess", "Purity", "Rally", "Rend", "Restlessness", "RollingBoulder",
      "Salvo", "Scrawl", "SecretTechnique", "SecretWeapon", "SeekerStrike", "Shockwave", "Splash", "Stratagem",
      "TagTeam", "TheBall", "TheBomb", "TheGambit", "ThinkingAhead", "ThrummingHatchet", "UltimateDefend",
      "UltimateStrike", "Volley"};
  return v;
}

// Cards of the pool with MultiplayerConstraint.MultiplayerOnly (CardFactory.FilterForPlayerCount drops
// them in single player, like Character::multiplayerOnly for the character pools).
bool multiplayerOnly(const std::string& id) {
  static const char* const ids[] = {"BeaconOfHope", "BelieveInYou", "Coordinate", "GangUp", "HuddleUp", "Intercept",
                                    "Knockdown", "Lift", "Mimic", "Rally", "TagTeam", "TheBall"};
  for (const char* s : ids) if (id == s) return true;
  return false;
}

// ColorlessPotion.cs: choose 1 of 3 distinct colorless cards (skippable), free this turn, into the hand.
struct ColorlessPotion : Potion {
  POTION_HEADER(ColorlessPotion, "COLORLESS_POTION", Common, CombatOnly, Self) }
  Task<> onUse(Creature*) override {
    Combat& c = *run->combat;
    co_await chooseGeneratedToHand(c, colorlessDistinctForCombat(c, 3), true);
  }
};

}  // namespace

namespace db {

std::vector<std::string> colorlessCards(std::function<bool(const Card&)> filter) {
  std::vector<std::string> out;
  for (auto& id : poolIds()) {
    if (multiplayerOnly(id)) continue;
    auto c = card(id);
    if (c && filter(*c)) out.push_back(id);
  }
  return out;
}

bool isColorless(const std::string& id) {
  const auto& v = poolIds();
  return std::find(v.begin(), v.end(), id) != v.end();
}

}  // namespace db

Task<> chooseGeneratedToHand(Combat& c, std::vector<std::unique_ptr<Card>> options, bool free) {
  std::vector<Card*> opts;
  for (auto& k : options) opts.push_back(k.get());
  auto picked = co_await cmd::selectCards(c, "选择一张牌加入手牌", opts, 0, 1);
  if (picked.empty()) co_return;
  for (auto& k : options)
    if (k.get() == picked[0]) {
      if (free) k->setThisTurnOrUntilPlayed(0);  // SetToFreeThisTurn
      co_await cmd::addGeneratedCard(c, std::move(k), Pile::Hand);
      break;
    }
}

// CardFactory.CreateForReward with ForNonCombatWithDefaultOdds(ColorlessCardPool) (Run::createForReward):
// CardRarityOdds.RollWithBaseOdds, the next allowed rarity, no upgrade roll, then the card reward hooks.
std::vector<std::unique_ptr<Card>> colorlessRewardCards(Run& r, int n) {
  return r.createForReward(CardCreationOptions::forNonCombat({CardCreationOptions::kColorless}, false), n);
}

void registerColorlessPool() {
  db::registerPotion(ColorlessPotion::kId, [] { return std::unique_ptr<Potion>(new ColorlessPotion()); });
}

}  // namespace sts
