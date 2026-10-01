// E1: CardFactory.FilterForCombat / GetDistinctForCombat / GetForCombat (see card_factory.h).
#include "card_factory.h"

#include <algorithm>

namespace sts {

namespace db {

bool canGenerateInCombat(const Card& c) {
  return c.canBeGeneratedInCombat() && c.rarity != Rarity::Basic && c.rarity != Rarity::Ancient &&
         c.rarity != Rarity::Event;
}

std::vector<std::string> filterForCombat(const std::vector<std::string>& ids) {
  std::vector<std::string> out;
  for (auto& id : ids) {
    if (std::find(out.begin(), out.end(), id) != out.end()) continue;  // Distinct()
    auto k = card(id);
    if (k && canGenerateInCombat(*k)) out.push_back(id);
  }
  return out;
}

std::vector<std::string> characterPool(const std::string& characterId, std::function<bool(const Card&)> filter) {
  if (!filter) return characterCards(characterId, [](const Card&) { return true; });
  return characterCards(characterId, std::move(filter));
}

std::vector<std::string> colorlessPool(std::function<bool(const Card&)> filter) {
  if (!filter) return colorlessCards([](const Card&) { return true; });
  return colorlessCards(std::move(filter));
}

std::vector<std::string> combatCardPool(const std::string& characterId, std::function<bool(const Card&)> filter) {
  return filterForCombat(characterPool(characterId, std::move(filter)));
}

}  // namespace db

namespace {

std::unique_ptr<Card> create(Combat& c, const std::string& id) {
  auto k = db::card(id);
  if (k) k->combat = &c;
  return k;
}

}  // namespace

std::vector<std::unique_ptr<Card>> getDistinctForCombat(Combat& c, const std::vector<std::string>& cards, int count, Rng& rng) {
  auto options = db::filterForCombat(cards);
  rng.shuffle(options);  // TakeRandom = ToList().UnstableShuffle(rng).Take(count)
  std::vector<std::unique_ptr<Card>> out;
  for (size_t i = 0; i < options.size() && (int)out.size() < count; ++i)
    if (auto k = create(c, options[i])) out.push_back(std::move(k));
  return out;
}

std::vector<std::unique_ptr<Card>> getForCombat(Combat& c, const std::vector<std::string>& cards, int count, Rng& rng) {
  auto options = db::filterForCombat(cards);
  std::vector<std::unique_ptr<Card>> out;
  if (options.empty()) return out;
  for (int i = 0; i < count; ++i)
    if (auto k = create(c, rng.nextItem(options))) out.push_back(std::move(k));
  return out;
}

std::vector<std::unique_ptr<Card>> distinctForCombat(Combat& c, const std::vector<std::string>& cards, int count) {
  return getDistinctForCombat(c, cards, count, c.rng("CombatCardGeneration"));
}

std::vector<std::unique_ptr<Card>> randomForCombat(Combat& c, const std::vector<std::string>& cards, int count) {
  return getForCombat(c, cards, count, c.rng("CombatCardGeneration"));
}

std::vector<std::unique_ptr<Card>> colorlessDistinctForCombat(Combat& c, int count) {
  return distinctForCombat(c, db::colorlessPool(), count);
}

std::unique_ptr<Card> oneDistinctForCombat(Combat& c, std::function<bool(const Card&)> filter) {
  auto made = distinctForCombat(c, db::characterPool(c.run->characterId, std::move(filter)), 1);
  if (made.empty()) return nullptr;
  return std::move(made[0]);
}

}  // namespace sts
