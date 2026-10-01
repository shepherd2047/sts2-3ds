// M11 run modifiers (Models.Modifiers) and SeedHelper. See modifiers.h for where each one acts.
#include "modifiers.h"

#include <algorithm>
#include <cctype>

#include "powers.h"
#include "powers_ironclad.h"
#include "progress.h"

namespace sts {

namespace {

// ModelDb.GoodModifiers / BadModifiers, in the game's order.
const char* const kGood[] = {"Draft", "SealedDeck", "Hoarder", "Specialized", "Insanity",
                             "AllStar", "Flight", "Vintage", "CharacterCards"};
const char* const kBad[] = {"DeadlyEvents", "CursedRun", "BigGameHunter", "Midas", "Murderous", "NightTerrors", "Terminal"};

// "BigGameHunter" -> "BIG_GAME_HUNTER" (ModelId.Entry -> loc key).
std::string upperSnake(const std::string& id) {
  std::string out;
  for (size_t i = 0; i < id.size(); ++i) {
    char c = id[i];
    if (i > 0 && std::isupper((unsigned char)c)) out += '_';
    out += (char)std::toupper((unsigned char)c);
  }
  return out;
}

bool isCombatRoom(RoomType t) { return t == RoomType::Monster || t == RoomType::Elite || t == RoomType::Boss; }

// ---- CardFactory.CreateForReward (Run::createForReward) with the modifiers' CardCreationOptions ---
// The character's pool, or the colorless one; Source Other. Uniform: ForNonCombatWithUniformOdds |
// NoRarityModification (AllStar also NoCardPoolModifications); Draft: RegularEncounter | NoUpgradeRoll
// (a CardReward: IsCardReward); SealedDeck: RegularEncounter | NoUpgradeRoll | ForceRarityOddsChange |
// IsCardReward.
CardCreationOptions uniformOptions(const std::string& pool) {
  auto o = CardCreationOptions::forNonCombat({pool}, true);
  o.with(ccNoRarityModification);
  if (pool == CardCreationOptions::kColorless) o.with(ccNoCardPoolModifications);
  return o;
}

CardCreationOptions regularOptions(const std::string& pool) {
  CardCreationOptions o;
  o.pools = {pool};
  o.with(ccNoUpgradeRoll);
  return o;
}

// The deck-replacing modifiers take Pandora's Box out of both grab bags.
void removeFromBags(Run& r, const std::string& id) {
  for (auto* bag : {&r.relicBag, &r.sharedRelicBag})
    for (auto& [k, v] : *bag) v.erase(std::remove(v.begin(), v.end(), id), v.end());
}

// CardReward with CanSkip = false: the reward card screen until a card is taken.
Task<> pickOneCard(Run& r, std::vector<std::unique_ptr<Card>> cards) {
  if (cards.empty()) co_return;
  r.rewardCards = std::move(cards);
  r.screen = Screen::Reward;
  for (;;) {
    int pick = co_await r.rewardChoice.next();
    if (pick >= 0 && pick < (int)r.rewardCards.size()) {
      r.addCardToDeck(std::move(r.rewardCards[pick]));
      break;
    }
  }
  r.rewardCards.clear();
}

// ---- the modifiers ---------------------------------------------------------------------------

struct Draft : Modifier {
  bool clearsDeck() const override { return true; }
  bool hasNeowOption() const override { return true; }
  // OfferRewards: ten unskippable card rewards of three (RegularEncounter, base odds).
  Task<> neowOption() override {
    for (int i = 0; i < 10; ++i) co_await pickOneCard(*run, run->makeCardReward(regularOptions(run->characterId), 3).cards);
    removeFromBags(*run, "PandorasBox");
  }
};

struct SealedDeck : Modifier {
  bool clearsDeck() const override { return true; }
  bool hasNeowOption() const override { return true; }
  // ChooseCards: 30 cards (rarity odds change), pick 10 on a grid sorted by rarity then title.
  // PORT NOTE: ties sort by card id (the C# compares the localised titles).
  Task<> neowOption() override {
    auto cards = run->createForReward(regularOptions(run->characterId).with(ccForceRarityOddsChange | ccIsCardReward), 30);
    std::stable_sort(cards.begin(), cards.end(), [](const std::unique_ptr<Card>& a, const std::unique_ptr<Card>& b) {
      return a->rarity != b->rarity ? (int)a->rarity < (int)b->rarity : a->id < b->id;
    });
    DeckChoice& dc = run->deckChoice;
    dc.prompt = "modifiers.SEALED_DECK.selectionPrompt";
    dc.options.clear();
    for (auto& c : cards) dc.options.push_back(c.get());
    dc.count = std::min(10, (int)cards.size());
    dc.minCount = -1;
    dc.canCancel = false;
    dc.showUpgrade = false;
    dc.active = true;
    auto picked = co_await dc.result.next();
    dc.active = false;
    for (Card* p : picked)
      for (auto& c : cards)
        if (c.get() == p) { run->addCardToDeck(std::move(c)); break; }
    removeFromBags(*run, "PandorasBox");
  }
};

struct Hoarder : Modifier {};  // Run::addCardToDeck and the merchant check it by name

struct Specialized : Modifier {
  bool hasNeowOption() const override { return true; }
  // ObtainCards: one uniform card of the character's pool, five copies of it.
  Task<> neowOption() override {
    auto one = run->createForReward(uniformOptions(run->characterId), 1);
    if (one.empty()) co_return;
    for (int i = 0; i < 5; ++i) run->addCardToDeck(one[0]->clone());
    co_await wait(0.6);
  }
};

struct Insanity : Modifier {
  bool clearsDeck() const override { return true; }
  bool hasNeowOption() const override { return true; }
  // ObtainCards: thirty uniform cards of the character's pool (each its own CreateForReward).
  Task<> neowOption() override {
    for (int i = 0; i < 30; ++i)
      for (auto& c : run->createForReward(uniformOptions(run->characterId), 1)) run->addCardToDeck(std::move(c));
    co_await wait(0.6);
    removeFromBags(*run, "PandorasBox");
  }
};

struct AllStar : Modifier {
  bool hasNeowOption() const override { return true; }
  // ObtainCards: five uniform colorless cards.
  Task<> neowOption() override {
    for (int i = 0; i < 5; ++i)
      for (auto& c : run->createForReward(uniformOptions(CardCreationOptions::kColorless), 1)) run->addCardToDeck(std::move(c));
    co_await wait(0.6);
  }
};

struct Flight : Modifier {};          // Run::pathNodes: any node of the next row
struct Vintage : Modifier {};         // Run::combatRewards: card rewards of monster rooms become relics
struct CharacterCards : Modifier {};  // Run::cardReward, the merchant: `character`'s pool joins
struct DeadlyEvents : Modifier {};    // afterRunCreated, Run::rollUnknownRoom
struct CursedRun : Modifier {};       // afterActEntered
struct BigGameHunter : Modifier {};   // Run::generateMap, Run::cardReward
struct Midas : Modifier {};           // Run::combatRewards (gold x2), Run::restSite (no smith)

struct Murderous : Modifier {
  // AfterRoomEntered: 3 Strength to every creature of the fight; AfterCreatureAddedToCombat: to
  // every enemy that joins later.
  Task<> afterRoomEntered(RoomType t) override {
    if (!isCombatRoom(t) || !run->combat) co_return;
    Combat& c = *run->combat;
    std::vector<Creature*> all = {c.player};
    if (c.osty && c.osty->alive() && !c.osty->removed) all.push_back(c.osty);
    for (Creature* e : c.enemies) if (!e->removed) all.push_back(e);
    for (Creature* cr : all) co_await applyPower<StrengthPower>(cr, Dec(3), nullptr, nullptr);
  }
  Task<> afterCreatureAddedToCombat(Creature* cr) override {
    if (cr->side == Side::Player) co_return;
    co_await applyPower<StrengthPower>(cr, Dec(3), nullptr, nullptr);
  }
};

struct NightTerrors : Modifier {
  Dec modifyRestSiteHealAmount(Creature* c, Dec) override { return Dec(c->maxHp); }
  Task<> afterRestSiteHeal() override {
    int n = std::min(5, run->player->maxHp - 1);
    if (n > 0) co_await run->loseMaxHp(n);
  }
};

struct Terminal : Modifier {
  // AfterRoomEntered: the map point's own room (RunState.BaseRoom; not an event's fight) costs 1
  // max HP; every fight starts with 5 Plating.
  Task<> afterRoomEntered(RoomType t) override {
    bool combat = isCombatRoom(t);
    if (!(combat && run->currentEvent)) {
      Creature* p = run->player.get();
      if (p->hp > 1 || p->maxHp > 1) co_await run->loseMaxHp(1);
    }
    if (combat && run->combat) co_await applyPower<PlatingPower>(run->player.get(), Dec(5), nullptr, nullptr);
  }
};

template <class M> std::shared_ptr<Modifier> make(const char* id) {
  auto m = std::make_shared<M>();
  m->id = id;
  return m;
}

EventOption neowOption(Event& e, std::vector<Modifier*> mods, size_t i) {
  EventOption o;
  o.key = upperSnake(mods[i]->id);  // modifiers.<KEY>.title / .description (NeowOptionTitle = Title)
  o.action = [&e, mods, i]() -> Task<> {
    co_await mods[i]->neowOption();
    // OnModifierOptionSelected: the next modifier's option, or DONE after the last one.
    if (i + 1 >= mods.size()) {
      e.finished = true;
      e.options.clear();
      e.descKey.clear();
    } else {
      e.options = {neowOption(e, mods, i + 1)};
    }
  };
  return o;
}

}  // namespace

// ---- Run ---------------------------------------------------------------------------------------

void Run::setModifiers(const std::vector<std::string>& keys) {
  modifiers.clear();
  for (auto& k : keys) {
    bool dup = false;
    for (auto& m : modifiers) dup = dup || m->key() == k;
    if (dup) continue;
    if (auto m = modifiers::create(k)) {
      m->run = this;
      modifiers.push_back(std::move(m));
    }
  }
}

std::vector<std::string> Run::modifierKeys() const {
  std::vector<std::string> out;
  for (auto& m : modifiers) out.push_back(m->key());
  return out;
}

bool Run::hasModifier(const char* id) const {
  for (auto& m : modifiers) if (m->id == id) return true;
  return false;
}

bool Run::modifiersClearDeck() const {
  for (auto& m : modifiers) if (m->clearsDeck()) return true;
  return false;
}

std::vector<std::string> Run::modifierCardPools() const {
  std::vector<std::string> out;
  for (auto& m : modifiers) if (m->id == "CharacterCards" && !m->character.empty()) out.push_back(m->character);
  return out;
}

// ---- modifiers:: ---------------------------------------------------------------------------------

namespace modifiers {

const char kSeedChars[] = "0123456789ABCDEFGHJKLMNPQRSTUVWXYZ";

const std::vector<std::string>& allKeys() {
  static std::vector<std::string> keys = [] {
    std::vector<std::string> v;
    for (const char* id : kGood) {
      if (std::string(id) == "CharacterCards") {
        for (auto& ch : db::allCharacters()) v.push_back(std::string(id) + ":" + ch);
      } else {
        v.push_back(id);
      }
    }
    for (const char* id : kBad) v.push_back(id);
    return v;
  }();
  return keys;
}

bool isGood(const std::string& key) {
  std::string id = key.substr(0, key.find(':'));
  for (const char* g : kGood) if (id == g) return true;
  return false;
}

std::shared_ptr<Modifier> create(const std::string& key) {
  size_t colon = key.find(':');
  std::string id = key.substr(0, colon);
  if (id == "CharacterCards") {
    if (colon == std::string::npos) return nullptr;
    std::string ch = key.substr(colon + 1);
    const auto& all = db::allCharacters();
    if (std::find(all.begin(), all.end(), ch) == all.end()) return nullptr;
    auto m = make<CharacterCards>("CharacterCards");
    m->character = ch;
    return m;
  }
  if (id == "Draft") return make<Draft>("Draft");
  if (id == "SealedDeck") return make<SealedDeck>("SealedDeck");
  if (id == "Hoarder") return make<Hoarder>("Hoarder");
  if (id == "Specialized") return make<Specialized>("Specialized");
  if (id == "Insanity") return make<Insanity>("Insanity");
  if (id == "AllStar") return make<AllStar>("AllStar");
  if (id == "Flight") return make<Flight>("Flight");
  if (id == "Vintage") return make<Vintage>("Vintage");
  if (id == "DeadlyEvents") return make<DeadlyEvents>("DeadlyEvents");
  if (id == "CursedRun") return make<CursedRun>("CursedRun");
  if (id == "BigGameHunter") return make<BigGameHunter>("BigGameHunter");
  if (id == "Midas") return make<Midas>("Midas");
  if (id == "Murderous") return make<Murderous>("Murderous");
  if (id == "NightTerrors") return make<NightTerrors>("NightTerrors");
  if (id == "Terminal") return make<Terminal>("Terminal");
  return nullptr;
}

std::string titleKey(const std::string& key) {
  size_t colon = key.find(':');
  if (colon != std::string::npos)  // CharacterModel.CardsModifierTitle
    return "characters." + db::character(key.substr(colon + 1)).key + ".cardsModifierTitle";
  return "modifiers." + upperSnake(key) + ".title";
}

std::string descriptionKey(const std::string& key) {
  size_t colon = key.find(':');
  if (colon != std::string::npos)
    return "characters." + db::character(key.substr(colon + 1)).key + ".cardsModifierDescription";
  return "modifiers." + upperSnake(key) + ".description";
}

bool mutuallyExclusive(const std::string& a, const std::string& b) {
  auto in = [](const std::string& k) { return k == "SealedDeck" || k == "Draft" || k == "Insanity"; };
  return a != b && in(a) && in(b);
}

std::vector<EventOption> neowOptions(Event& neow) {
  std::vector<Modifier*> mods;
  for (auto& m : neow.run->modifiers) if (m->hasNeowOption()) mods.push_back(m.get());
  if (mods.empty()) {  // no option at all: Neow only heals (the port finishes the event)
    neow.finished = true;
    neow.descKey.clear();
    return {};
  }
  return {neowOption(neow, mods, 0)};
}

void afterRunCreated(Run& r) {
  // DeadlyEvents.AfterRunCreated: no Juzu Bracelet; elites in "?" rooms (10%, also the base).
  if (r.hasModifier("DeadlyEvents")) {
    removeFromBags(r, "JuzuBracelet");
    r.unknownEliteOdds = 0.1f;
  }
}

void afterActEntered(Run& r) {
  // CursedRun: a random curse of CurseCardPool (CanBeGeneratedByModifiers) from the Niche stream.
  if (!r.hasModifier("CursedRun")) return;
  std::vector<std::string> curses;
  for (const char* id : {"Clumsy", "Debt", "Decay", "Doubt", "Guilty", "Injury", "Normality", "Regret", "Shame", "Writhe"})
    if (db::card(id)) curses.push_back(id);
  if (curses.empty()) return;
  r.addCardToDeck(db::card(r.rng("Niche").nextItem(curses)));
}

std::vector<std::string> parseList(const std::string& csv) {
  std::vector<std::string> out;
  for (size_t a = 0; a <= csv.size();) {
    size_t b = csv.find(',', a);
    if (b == std::string::npos) b = csv.size();
    std::string item = csv.substr(a, b - a);
    while (!item.empty() && std::isspace((unsigned char)item.back())) item.pop_back();
    while (!item.empty() && std::isspace((unsigned char)item.front())) item.erase(item.begin());
    if (!item.empty()) out.push_back(item);
    a = b + 1;
  }
  return out;
}

std::string canonicalizeSeed(std::string seed) {
  for (char& c : seed) {
    c = (char)std::toupper((unsigned char)c);
    if (c == 'O') c = '0';
    if (c == 'I') c = '1';
  }
  while (!seed.empty() && std::isspace((unsigned char)seed.back())) seed.pop_back();
  while (!seed.empty() && std::isspace((unsigned char)seed.front())) seed.erase(seed.begin());
  return seed;
}

uint64_t seedFromString(const std::string& seed) {
  if (seed.rfind("old", 0) == 0) {  // StringHelper.GetDeterministicHashCodeOld, (uint)
    std::string s = seed.substr(3);
    uint32_t a = 352654597u, b = a;
    for (size_t i = 0; i < s.size(); i += 2) {
      a = ((a << 5) + a) ^ (uint32_t)(unsigned char)s[i];
      if (i == s.size() - 1) break;
      b = ((b << 5) + b) ^ (uint32_t)(unsigned char)s[i + 1];
    }
    return (uint64_t)(uint32_t)(a + b * 1566083941u);
  }
  return deterministicHash(seed);
}

std::string randomSeed(uint64_t& x) {
  if (x == 0) x = 0x9E3779B97F4A7C15ull;
  std::string s;
  for (int i = 0; i < kSeedLength; ++i) {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    s += kSeedChars[x % (sizeof(kSeedChars) - 1)];
  }
  return s;
}

}  // namespace modifiers
}  // namespace sts
