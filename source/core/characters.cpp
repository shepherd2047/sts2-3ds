// Characters (X0): the Character table (characters.inc, generated from the C#) and the lookups
// that replace the old Ironclad-only pool functions. Nothing here is specific to one character.
#include <map>

#include "game.h"

#include "characters.inc"

namespace sts {

namespace {
const std::vector<Character>& table() {
  static const std::vector<Character> v = makeCharacters();
  return v;
}
}  // namespace

const Character& Run::character() const { return db::character(characterId); }

namespace db {

const Character& character(const std::string& id) {
  for (auto& c : table()) if (c.id == id) return c;
  return table()[0];  // Ironclad
}

const std::vector<std::string>& characterIds() {
  static const std::vector<std::string> ids = [] {
    std::vector<std::string> v;
    for (auto& c : table()) v.push_back(c.id);
    return v;
  }();
  return ids;
}

bool characterPlayable(const std::string& id) {
  const Character& c = character(id);
  if (c.id != id) return false;
  for (auto& k : c.starterDeck) if (!card(k)) return false;
  for (auto& r : c.startingRelics) if (!relic(r)) return false;
  return true;
}

std::vector<std::string> characterCards(const std::string& characterId, std::function<bool(const Card&)> filter) {
  const Character& ch = character(characterId);
  std::vector<std::string> out;
  for (auto& id : ch.cardPool) {
    if (std::find(ch.multiplayerOnly.begin(), ch.multiplayerOnly.end(), id) != ch.multiplayerOnly.end()) continue;
    auto c = card(id);
    if (c && filter(*c)) out.push_back(id);
  }
  return out;
}

const std::vector<std::string>& ironcladPool() { return character("Ironclad").cardPool; }
std::vector<std::string> ironcladCards(std::function<bool(const Card&)> filter) { return characterCards("Ironclad", std::move(filter)); }
const std::vector<std::string>& ironcladRelicPool() { return character("Ironclad").relicPool; }
std::vector<std::string> ironcladStarterDeck() { return character("Ironclad").starterDeck; }

}  // namespace db
}  // namespace sts
