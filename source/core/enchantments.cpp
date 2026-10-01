// Enchantments (A3a): the CardModel.Enchantment slot's rules, CardCmd.Enchant, the registry
// and the deck helpers events use, plus the first enchantments (the ones that exercise every
// new hook). The rest are in enchantments_b.cpp (A3b), enchantments_c.cpp (A3c), events_underdocks.cpp (Spiral, Steady) and events_shared2.cpp (Nimble).
//
// Card-side wiring lives with the code it changes: combat.cpp (Hook.ModifyDamage/ModifyBlock
// call the enchantment first, GetEnchantedReplayCount, OnPlay, the Imbued / PerfectFit /
// SlumberingEssence hooks), save.cpp (ioCard) and run.cpp (DeckVersion).
#include <map>

#include "game.h"

namespace sts {

// EnchantmentModel.CanEnchant
bool Enchantment::canEnchant(const Card& c) const {
  if (c.type == CardType::Status || c.type == CardType::Curse || c.type == CardType::Quest) return false;
  if (!canEnchantCardType(c.type)) return false;
  // Deck cards (not part of a combat) that cannot be played can't be enchanted.
  if (!c.combat && c.has(kwUnplayable)) return false;
  if (c.enchantment && (!isStackable() || c.enchantment->id != id)) return false;
  return true;
}

namespace cmd {

Enchantment* enchant(Card* card, std::unique_ptr<Enchantment> e, int amount) {
  if (!card || !e || !e->canEnchant(*card)) return nullptr;
  if (!card->enchantment) {
    e->card = card;
    e->amount = amount;  // ApplyInternal
    card->enchantment.p = std::move(e);
    card->enchantment->modifyCard();
  } else {
    card->enchantment->amount += amount;  // stacking the same enchantment
  }
  return card->enchantment.get();
}

void clearEnchantment(Card* card) {
  if (!card || !card->enchantment) return;
  card->enchantment->card = nullptr;
  if (card->combat) card->combat->enchantGraveyard.push_back(std::move(card->enchantment.p));
  card->enchantment.p.reset();
}

}  // namespace cmd

// ---------------------------------------------------------------- registry

namespace db {
namespace {
std::map<std::string, EnchantmentFactory>& enchantReg() {
  static std::map<std::string, EnchantmentFactory> m;
  return m;
}
}  // namespace

void registerEnchantment(const std::string& id, EnchantmentFactory f) { enchantReg()[id] = f; }

std::unique_ptr<Enchantment> enchantment(const std::string& id) {
  auto it = enchantReg().find(id);
  return it == enchantReg().end() ? nullptr : it->second();
}

const std::vector<std::string>& enchantmentIds() {
  static std::vector<std::string> ids;
  if (ids.size() != enchantReg().size()) {
    ids.clear();
    for (auto& [k, f] : enchantReg()) ids.push_back(k);
  }
  return ids;
}
}  // namespace db

// ---------------------------------------------------------------- deck helpers

bool Run::canEnchantAny(const std::string& id, std::function<bool(Card*)> filter) {
  auto e = db::enchantment(id);
  if (!e) return false;
  for (auto& c : deck)
    if (e->canEnchant(*c) && (!filter || filter(c.get()))) return true;
  return false;
}

Task<std::vector<Card*>> Run::selectForEnchantment(const std::string& id, int count, std::function<bool(Card*)> filter,
                                                   int amount) {
  auto e = db::enchantment(id);
  std::vector<Card*> picked;
  if (!e) co_return picked;
  std::vector<Card*> options;
  for (auto& c : deck)
    if (e->canEnchant(*c) && (!filter || filter(c.get()))) options.push_back(c.get());
  if (options.empty()) co_return picked;
  if ((int)options.size() <= count) co_return options;  // nothing to choose (cards.Count <= MinSelect)
  deckChoice.enchantId = id;  // the picker previews the card enchanted
  deckChoice.enchantAmount = amount;
  auto picked2 = co_await selectFromDeck("card_selection.TO_ENCHANT",
                                         [options](Card* c) { return std::find(options.begin(), options.end(), c) != options.end(); },
                                         count);
  deckChoice.enchantId.clear();
  deckChoice.enchantAmount = 0;
  co_return picked2;
}

Enchantment* Run::enchantCard(Card* c, const std::string& id, int amount) {
  return cmd::enchant(c, db::enchantment(id), amount);
}

// ---------------------------------------------------------------- enchantments

namespace {

// Sharp.cs: +Amount damage on an attack.
struct Sharp : EnchantmentT<Sharp> {
  ENCHANTMENT_HEADER(Sharp, "SHARP")
  }
  bool showAmount() const override { return true; }
  bool canEnchantCardType(CardType t) const override { return t == CardType::Attack; }
  Dec enchantDamageAdditive(Dec, int props) override { return isPoweredAttack(props) ? Dec(amount) : Dec(0); }
};

// Vigorous.cs: +Amount damage on the first play (per combat), then disabled.
struct Vigorous : EnchantmentT<Vigorous> {
  ENCHANTMENT_HEADER(Vigorous, "VIGOROUS")
  }
  bool showAmount() const override { return true; }
  bool canEnchantCardType(CardType t) const override { return t == CardType::Attack; }
  Dec enchantDamageAdditive(Dec, int props) override {
    if (status != EnchantStatus::Normal || !isPoweredAttack(props)) return 0;
    return Dec(amount);
  }
  Task<> afterCardPlayed(const CardPlay& cp) override {
    if (cp.card == card) status = EnchantStatus::Disabled;
    co_return;
  }
};

// Swift.cs: the first play (per combat) draws Amount cards.
struct Swift : EnchantmentT<Swift> {
  ENCHANTMENT_HEADER(Swift, "SWIFT")
  }
  bool hasExtraCardText() const override { return true; }
  bool showAmount() const override { return true; }
  Task<> onPlay(CardPlay&) override {
    if (status == EnchantStatus::Normal && card->combat) {
      status = EnchantStatus::Disabled;
      co_await cmd::drawCards(*card->combat, amount);
    }
  }
};

// Glam.cs: the first play (per combat) is played one extra time.
struct Glam : EnchantmentT<Glam> {
  bool usedThisCombat = false;
  ENCHANTMENT_HEADER(Glam, "GLAM")
    addVar("Times", 1);
  }
  int enchantPlayCount(int n) override { return usedThisCombat ? n : n + val("Times").toInt(); }
  Task<> afterCardPlayed(const CardPlay& cp) override {
    if (usedThisCombat || cp.card != card) co_return;
    usedThisCombat = true;
    status = EnchantStatus::Disabled;
  }
};

// Imbued.cs: a Skill that starts at the bottom of the draw pile and is played automatically
// at the start of the first turn.
struct Imbued : EnchantmentT<Imbued> {
  ENCHANTMENT_HEADER(Imbued, "IMBUED")
  }
  bool shouldStartAtBottomOfDrawPile() const override { return true; }
  bool canEnchantCardType(CardType t) const override { return t == CardType::Skill; }
  Task<> afterAutoPrePlayPhaseEntered() override {
    if (card && card->combat && card->combat->turnNumber <= 1) co_await cmd::autoPlay(*card->combat, card);
  }
};

// PerfectFit.cs: after every shuffle but the first the card goes on top of the draw pile.
struct PerfectFit : EnchantmentT<PerfectFit> {
  ENCHANTMENT_HEADER(PerfectFit, "PERFECT_FIT")
  }
  bool hasExtraCardText() const override { return true; }
  void modifyShuffleOrder(std::vector<Card*>& cards, bool isInitialShuffle) override {
    auto it = std::find(cards.begin(), cards.end(), card);
    if (isInitialShuffle || it == cards.end()) return;
    cards.erase(it);
    cards.insert(cards.begin(), card);
  }
};

// SlumberingEssence.cs: a card still in hand when the turn ends gets cheaper until played.
struct SlumberingEssence : EnchantmentT<SlumberingEssence> {
  ENCHANTMENT_HEADER(SlumberingEssence, "SLUMBERING_ESSENCE")
  }
  Task<> beforeFlush() override {
    if (card && card->combat && card->combat->pileOf(card) == Pile::Hand) card->addUntilPlayed(-1);
    co_return;
  }
};

template <class E> void reg() { db::registerEnchantment(E::kId, [] { return std::unique_ptr<Enchantment>(new E()); }); }

}  // namespace

void registerEnchantments() {
  reg<Sharp>();
  reg<Vigorous>();
  reg<Swift>();
  reg<Glam>();
  reg<Imbued>();
  reg<PerfectFit>();
  reg<SlumberingEssence>();
}

}  // namespace sts
