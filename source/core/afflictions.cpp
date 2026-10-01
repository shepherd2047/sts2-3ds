// Afflictions (A4): the CardModel.Affliction slot's rules (AfflictionModel.CanAfflict,
// CardCmd.Afflict / ClearAffliction), the registry and the seven afflictions of
// Models.Afflictions. Most of their logic lives in the powers that hand them out, as in the C#:
//   Bound      ChainsOfBindingPower (content_act3b.cpp, Queen)
//   Entangled  TangledPower (content_act1.cpp, Vine Shambler)
//   Galvanized GalvanicPower (content_act3a.cpp, Globe Head)
//   Hexed      HexPower (content_act3b.cpp, Knights)
//   Ringing    RingingPower (content_bosses.cpp, Ceremonial Beast)
//   Smog       SmoggyPower (content_underdocks_a.cpp, Living Fog)
//   Tainted    VitalSparkPower (content_act2c.cpp, Infested Prisms)
// Card-side wiring: combat.cpp (listeners, OnPlay after the enchantment's), game.h (Card::has
// adds the affliction's keywords, clone() keeps the affliction via adoptEnchantment). Not saved:
// afflictions only live on combat cards.
#include <cstdlib>
#include <map>

#include "game.h"

namespace sts {

// AfflictionModel.CanAfflict
bool Affliction::canAfflict(const Card& c) const {
  if (!canAfflictCardType(c.type)) return false;
  if (c.has(kwUnplayable) && !canAfflictUnplayableCards()) return false;
  if (c.affliction && (!isStackable() || c.affliction->id != id)) return false;
  return true;
}

// CardModel.DowngradeInternal (via CardCmd.Downgrade): back to a fresh card's base numbers, keeping
// the enchantment, affliction, local cost modifiers and counters; AfterDowngraded lets a card
// restore state it tracks outside the vars. A combat that is ending does not downgrade.
// PORT NOTE (n/a: owner): CardCmd.Downgrade's DowngradedCards history entry is not kept (the port's history format has no such list).
void Card::downgrade() {
  if (combat && combat->ending) return;
  auto fresh = db::card(id);
  vars = fresh->vars;
  cost = fresh->cost;
  starCost = fresh->starCost;
  keywords = fresh->keywords;
  target = fresh->target;
  upgradeLevel = 0;
  afterDowngraded();
  if (enchantment) enchantment->modifyCard();
  if (affliction) affliction->afterApplied();
}

namespace cmd {

Affliction* afflict(Card* card, std::unique_ptr<Affliction> a, int amount) {
  if (!card || !a) return nullptr;
  Combat* c = card->combat;
  // CombatManager.IsOverOrEnding with the card in a combat pile; no combat at all.
  if (!c || c->ending || c->over) return nullptr;
  for (Model* m : c->listeners())
    if (!m->shouldAfflict(card, a.get())) return nullptr;  // Hook.ShouldAfflict
  if (!a->canAfflict(*card)) return nullptr;
  std::string afflictionId = a->id;
  if (!card->affliction) {
    // CardModel.AfflictInternal, then AfterApplied.
    a->card = card;
    a->amount = amount;
    card->affliction.p = std::move(a);
    card->affliction->afterApplied();
  } else {
    card->affliction->amount += amount;  // the same stackable affliction (C# throws otherwise)
  }
  c->history.cardAfflicted(*c, card, afflictionId);  // History.CardAfflicted
  return card->affliction.get();
}

Affliction* afflict(Card* card, const char* afflictionId, int amount) {
  return afflict(card, db::affliction(afflictionId), amount);
}

// CardModel.ClearAfflictionInternal: BeforeRemoved, then the slot empties. The object is kept
// alive until the combat ends because a hook loop may still hold it.
void clearAffliction(Card* card) {
  if (!card || !card->affliction) return;
  card->affliction->beforeRemoved();
  card->affliction->card = nullptr;
  if (card->combat) card->combat->afflictGraveyard.push_back(std::move(card->affliction.p));
  card->affliction.p.reset();
}

void debugAfflictions(Combat& c) {
  if (c.debugAfflictDone) return;
  c.debugAfflictDone = true;
  const char* list = getenv("STS_AFFLICT");
  if (list) {
    std::string s = list;
    for (size_t a = 0; a <= s.size();) {
      size_t b = s.find(',', a);
      if (b == std::string::npos) b = s.size();
      std::string item = s.substr(a, b - a);
      size_t colon = item.find(':');
      int amount = colon == std::string::npos ? 1 : std::atoi(item.c_str() + colon + 1);
      std::string id = item.substr(0, colon);
      if (auto probe = db::affliction(id))
        for (Card* k : c.allCards())  // hand first
          if (!k->affliction && probe->canAfflict(*k)) { afflict(k, db::affliction(id), amount); break; }
      a = b + 1;
    }
  }
  if (getenv("SIM_AFFLICT")) {
    // Every registered affliction goes round the combat cards (amount 1-3), on each card it fits.
    const auto& ids = db::afflictionIds();
    size_t k = 0;
    for (Card* card : c.allCards())
      for (size_t t = 0; t < ids.size() && !card->affliction; ++t) {
        const std::string& id = ids[(k + t) % ids.size()];
        if (db::affliction(id)->canAfflict(*card)) { afflict(card, db::affliction(id), 1 + (int)(k % 3)); ++k; }
      }
  }
}

}  // namespace cmd

// ---------------------------------------------------------------- registry

namespace db {
namespace {
std::map<std::string, AfflictionFactory>& afflictReg() {
  static std::map<std::string, AfflictionFactory> m;
  return m;
}
}  // namespace

void registerAffliction(const std::string& id, AfflictionFactory f) { afflictReg()[id] = f; }

std::unique_ptr<Affliction> affliction(const std::string& id) {
  auto it = afflictReg().find(id);
  return it == afflictReg().end() ? nullptr : it->second();
}

const std::vector<std::string>& afflictionIds() {
  static std::vector<std::string> ids;
  if (ids.size() != afflictReg().size()) {
    ids.clear();
    for (auto& [k, f] : afflictReg()) ids.push_back(k);
  }
  return ids;
}
}  // namespace db

// ---------------------------------------------------------------- afflictions

namespace {

// Bound.cs: logic in ChainsOfBindingPower.
struct Bound : AfflictionT<Bound> {
  AFFLICTION_HEADER(Bound, "BOUND")
  }
  bool hasExtraCardText() const override { return true; }
};

// Entangled.cs: logic in TangledPower (+Amount energy cost).
struct Entangled : AfflictionT<Entangled> {
  AFFLICTION_HEADER(Entangled, "ENTANGLED")
  }
};

// Galvanized.cs: logic in GalvanicPower (damage when played).
struct Galvanized : AfflictionT<Galvanized> {
  AFFLICTION_HEADER(Galvanized, "GALVANIZED")
  }
  bool isStackable() const override { return true; }
  bool hasExtraCardText() const override { return true; }
};

// Hexed.cs: HexPower makes Hexed cards Ethereal while it lasts (TryModifyKeywordsInCombat);
// a Hexed card entering combat once the power is gone loses the affliction.
struct Hexed : AfflictionT<Hexed> {
  AFFLICTION_HEADER(Hexed, "HEXED")
  }
  static bool hexActive(const Card* k) { return k && k->combat && k->combat->player && k->combat->player->power("HexPower"); }
  int addedKeywords() const override { return hexActive(card) ? kwEthereal : 0; }
  Task<> afterCardEnteredCombat(Card* k) override {
    if (k != card || hexActive(card)) co_return;
    cmd::clearAffliction(card);
  }
};

// Ringing.cs: logic in RingingPower (one card a turn).
struct Ringing : AfflictionT<Ringing> {
  AFFLICTION_HEADER(Ringing, "RINGING")
  }
  bool hasExtraCardText() const override { return true; }
};

// Smog.cs: logic in SmoggyPower (no more Skills this turn).
struct Smog : AfflictionT<Smog> {
  AFFLICTION_HEADER(Smog, "SMOG")
  }
};

// Tainted.cs: logic in VitalSparkPower (TaintedPower when played). Skills only.
struct Tainted : AfflictionT<Tainted> {
  AFFLICTION_HEADER(Tainted, "TAINTED")
  }
  bool isStackable() const override { return true; }
  bool hasExtraCardText() const override { return true; }
  bool canAfflictCardType(CardType t) const override { return t == CardType::Skill; }
};

template <class A> void reg() { db::registerAffliction(A::kId, [] { return std::unique_ptr<Affliction>(new A()); }); }

}  // namespace

void registerAfflictions() {
  reg<Bound>();
  reg<Entangled>();
  reg<Galvanized>();
  reg<Hexed>();
  reg<Ringing>();
  reg<Smog>();
  reg<Tainted>();
}

}  // namespace sts
