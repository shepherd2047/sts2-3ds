// Enchantments 1/2 (A3b): Sown, Slither, Adroit, Clone, Corrupted, Goopy, Inky, SoulsPower.
// (Models.Enchantments.) Spiral / Steady / Nimble live with their events; A3c reuses them.
// PORT NOTE: Slither's TestEnergyCostOverride (test mode only) is not ported.
#include "game.h"
#include "powers.h"

namespace sts {
namespace {

// Sown.cs: the first play (per combat) gains Amount energy.
struct Sown : EnchantmentT<Sown> {
  ENCHANTMENT_HEADER(Sown, "SOWN")
  }
  bool hasExtraCardText() const override { return true; }
  Task<> onPlay(CardPlay&) override {
    if (status == EnchantStatus::Normal && card->combat) {
      status = EnchantStatus::Disabled;
      co_await cmd::gainEnergy(*card->combat, amount);
    }
  }
};

// Slither.cs: when drawn, the card's cost becomes a random 0-3 for the rest of the combat.
struct Slither : EnchantmentT<Slither> {
  ENCHANTMENT_HEADER(Slither, "SLITHER")
  }
  bool canEnchant(const Card& c) const override {
    return Enchantment::canEnchant(c) && !c.has(kwUnplayable) && !c.costsX;
  }
  Task<> afterCardDrawn(Card* k, bool) override {
    if (k != card || !card->combat || card->combat->pileOf(card) != Pile::Hand) co_return;
    card->setThisCombat(card->combat->rng("CombatEnergyCosts").nextInt(4));
  }
};

// Adroit.cs: gain Amount Block on every play.
struct Adroit : EnchantmentT<Adroit> {
  ENCHANTMENT_HEADER(Adroit, "ADROIT")
    addVar("Block", 0);
  }
  bool hasExtraCardText() const override { return true; }
  bool showAmount() const override { return true; }
  void recalculateValues() override { if (auto* v = var("Block")) v->base = v->canonical = Dec(amount); }
  Task<> onPlay(CardPlay&) override {
    if (card->combat) co_await cmd::gainBlock(card->combat->player, val("Block"), kMove, card);
  }
};

// Clone.cs: does nothing itself; the CLONE rest site option (Pael's Growth) looks for it.
// PORT NOTE: CloneRestSiteOption and the PaelsGrowth relic that offers it are not ported yet.
struct Clone : EnchantmentT<Clone> {
  ENCHANTMENT_HEADER(Clone, "CLONE")
  }
};

// Corrupted.cs: an Attack deals 50% more damage but costs 2 HP per play.
struct Corrupted : EnchantmentT<Corrupted> {
  ENCHANTMENT_HEADER(Corrupted, "CORRUPTED")
  }
  bool hasExtraCardText() const override { return true; }
  bool canEnchantCardType(CardType t) const override { return t == CardType::Attack; }
  Dec enchantDamageMultiplicative(Dec, int props) override { return isPoweredAttack(props) ? Dec::lit(1.5) : Dec(1); }
  Task<> onPlay(CardPlay&) override {
    if (card->combat)
      co_await cmd::damage(card->combat->player, Dec(2), kUnblockable | kUnpowered | kMove, card->combat->player, card);
  }
};

// Goopy.cs: a Defend gains Exhaust and +1 Block (permanently) each time it is played.
struct Goopy : EnchantmentT<Goopy> {
  ENCHANTMENT_HEADER(Goopy, "GOOPY")
  }
  bool hasExtraCardText() const override { return true; }
  bool canEnchant(const Card& c) const override { return Enchantment::canEnchant(c) && (c.tags & tagDefend); }
  void onEnchant() override { card->addKeyword(kwExhaust); }
  Task<> afterCardPlayed(const CardPlay& cp) override {
    if (cp.card != card) co_return;
    ++amount;
    if (card->deckVersion.p && card->deckVersion.p->enchantment) ++card->deckVersion.p->enchantment->amount;
    co_return;
  }
  Dec enchantBlockAdditive(Dec) override { return Dec(amount - 1); }
};

// Inky.cs: the card also applies 1 Weak (to all enemies for an AllEnemies card).
// PORT NOTE: the "to ALL enemies" part of the extra card text needs {TargetType:choose(...)}, which the
// card text expander does not know; it shows only "Apply N Weak".
struct Inky : EnchantmentT<Inky> {
  ENCHANTMENT_HEADER(Inky, "INKY")
    addVar("WeakPower", 1);
  }
  bool hasExtraCardText() const override { return true; }
  Task<> onPlay(CardPlay& cp) override {
    Combat* c = card->combat;
    if (!c) co_return;
    std::vector<Creature*> targets;
    if (card->target != TargetType::AllEnemies) {
      if (cp.target) targets.push_back(cp.target);
    } else {
      targets = c->hittableEnemies();
    }
    for (Creature* t : targets) co_await applyPower<WeakPower>(t, val("WeakPower"), c->player, card);
  }
};

// SoulsPower.cs: the card loses Exhaust.
struct SoulsPower : EnchantmentT<SoulsPower> {
  ENCHANTMENT_HEADER(SoulsPower, "SOULS_POWER")
  }
  bool canEnchant(const Card& c) const override { return Enchantment::canEnchant(c) && c.has(kwExhaust); }
  void onEnchant() override { card->removeKeyword(kwExhaust); }
};

template <class E> void reg() { db::registerEnchantment(E::kId, [] { return std::unique_ptr<Enchantment>(new E()); }); }

}  // namespace

void registerEnchantmentsB() {
  reg<Sown>();
  reg<Slither>();
  reg<Adroit>();
  reg<Clone>();
  reg<Corrupted>();
  reg<Goopy>();
  reg<Inky>();
  reg<SoulsPower>();
}

}  // namespace sts
