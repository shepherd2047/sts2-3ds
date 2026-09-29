// Enchantments 2/2 (A3c): Instinct, Momentum, RoyallyApproved, TezcatarasEmber.
// (Models.Enchantments.) Nimble lives in events_shared2.cpp, Spiral / Steady in
// events_underdocks.cpp, the A3b set in enchantments_b.cpp; not repeated here.
#include "game.h"

namespace sts {
namespace {

// Instinct.cs: an Attack deals double damage.
struct Instinct : EnchantmentT<Instinct> {
  ENCHANTMENT_HEADER(Instinct, "INSTINCT")
  }
  bool canEnchantCardType(CardType t) const override { return t == CardType::Attack; }
  Dec enchantDamageMultiplicative(Dec, int props) override { return isPoweredAttack(props) ? Dec(2) : Dec(1); }
};

// Momentum.cs: every play adds Amount damage to the card.
// (_extraDamage is not a saved property in the C#; it lives on this copy of the enchantment,
// so a combat card starts from the deck card's value again next fight.)
struct Momentum : EnchantmentT<Momentum> {
  int extraDamage = 0;
  ENCHANTMENT_HEADER(Momentum, "MOMENTUM")
  }
  bool hasExtraCardText() const override { return true; }
  bool showAmount() const override { return true; }
  bool canEnchantCardType(CardType t) const override { return t == CardType::Attack; }
  Task<> onPlay(CardPlay&) override { extraDamage += amount; co_return; }
  Dec enchantDamageAdditive(Dec, int props) override { return isPoweredAttack(props) ? Dec(extraDamage) : Dec(0); }
};

// RoyallyApproved.cs: an Attack or Skill gains Innate and Retain.
struct RoyallyApproved : EnchantmentT<RoyallyApproved> {
  ENCHANTMENT_HEADER(RoyallyApproved, "ROYALLY_APPROVED")
  }
  bool canEnchantCardType(CardType t) const override { return t == CardType::Attack || t == CardType::Skill; }
  void onEnchant() override {
    card->addKeyword(kwInnate);
    card->addKeyword(kwRetain);
  }
};

// TezcatarasEmber.cs: the card costs 0, gains Eternal (it can never leave the deck) and +3 damage.
struct TezcatarasEmber : EnchantmentT<TezcatarasEmber> {
  ENCHANTMENT_HEADER(TezcatarasEmber, "TEZCATARAS_EMBER")
    addVar("Damage", 3);
  }
  void onEnchant() override {
    if (card->cost > 0 && !card->costsX) card->cost = 0;  // EnergyCost.UpgradeBy(-cost)
    card->addKeyword(kwEternal);
  }
  Dec enchantDamageAdditive(Dec, int props) override { return isPoweredAttack(props) ? val("Damage") : Dec(0); }
};

template <class E> void reg() { db::registerEnchantment(E::kId, [] { return std::unique_ptr<Enchantment>(new E()); }); }

}  // namespace

void registerEnchantmentsC() {
  reg<Instinct>();
  reg<Momentum>();
  reg<RoyallyApproved>();
  reg<TezcatarasEmber>();
}

}  // namespace sts
