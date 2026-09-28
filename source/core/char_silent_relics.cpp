// The Silent's 8 relics (X1.1), RelicPools\SilentRelicPool.cs. Translated from
// MegaCrit.Sts2.Core.Models.Relics\<Name>.cs; see relics_common.cpp for the shared style
// (`reg<T>()`, `RELIC_HEADER`, `DynVar`s named like the C#'s CanonicalVars).
#include "cards.h"
#include "char_silent.h"

namespace sts {

namespace {
template <class R> void reg() { db::registerRelic(R::kId, [] { return std::unique_ptr<Relic>(new R()); }); }

// RingOfTheSnake.cs (starting relic): draw 2 extra cards on turn 1.
struct RingOfTheSnake : Relic {
  RELIC_HEADER(RingOfTheSnake, "RING_OF_THE_SNAKE", Starter)
    addVar("Cards", 2);
  }
  Dec modifyHandDraw(Dec amount) override {
    if (!combat || combat->turnNumber > 1) return amount;
    return amount + val("Cards");
  }
};

// SneckoSkull.cs: Poison the owner applies is increased by 1. PORT NOTE: this engine has no
// separate Hook.ModifyPowerAmountGiven family (relics_more.cpp skips UnsettlingLamp for the same
// reason); `tryModifyPowerAmountReceived` runs at the same point in `cmd::applyPower` (before the
// power's amount is set, for both a fresh application and stacking onto an existing one), so
// reusing it here for the giver's side has the same net effect.
struct SneckoSkull : Relic {
  RELIC_HEADER(SneckoSkull, "SNECKO_SKULL", Common)
    addVar("PoisonPower", 1);
  }
  bool tryModifyPowerAmountReceived(Power* incoming, Creature*, Dec amount, Creature* applier, Dec& out) override {
    if (!incoming || incoming->id != "PoisonPower" || applier != owner()) return false;
    out = amount + val("PoisonPower");
    return true;
  }
  Task<> afterModifyingPowerAmountReceived(Power* p) override {
    if (p->id == "PoisonPower") doFlash();
    return {};
  }
};

// HelicalDart.cs: playing a Shiv gives 1 Dexterity for the rest of combat... actually just this
// turn (TemporaryDexterityPower ends at the owner's turn end, see HelicalDartPower below).
struct HelicalDartPower : Power {
  POWER_HEADER(HelicalDartPower, "TEMPORARY_DEXTERITY_POWER")  // same display text family as the Speed Potion's
  PowerType type() const override { return PowerType::Buff; }
  // TemporaryDexterityPower.cs: BeforeApplied / AfterPowerAmountChanged / AfterSideTurnEnd apply
  // and unwind an equal amount of real DexterityPower (silent, so it doesn't double-flash).
  Task<> beforeApplied(Creature* target, Dec amt, Creature* app, Card* src) override {
    co_await applyPower<DexterityPower>(target, amt, app, src, true);
  }
  Task<> afterPowerAmountChanged(Power* p, Dec amt, Creature* app, Card* src) override {
    if (!(amt == Dec(amount)) && p == this) co_await applyPower<DexterityPower>(owner, amt, app, src, true);
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner)) co_return;
    flash = 1.f;
    Creature* o = owner;
    int a = amount;
    co_await cmd::removePower(this);
    co_await applyPower<DexterityPower>(o, Dec(-a), o, nullptr);
  }
};

struct HelicalDart : Relic {
  RELIC_HEADER(HelicalDart, "HELICAL_DART", Rare)
    addVar("DexterityPower", 1);
  }
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (ownerOf(p.card) != owner() || !(p.card->tags & tagShiv)) co_return;
    doFlash();
    co_await applyPower<HelicalDartPower>(owner(), val("DexterityPower"), owner(), nullptr);
  }
};

// NinjaScroll.cs: on turn 1, 3 Shivs to hand before the draw.
struct NinjaScroll : Relic {
  RELIC_HEADER(NinjaScroll, "NINJA_SCROLL", Shop)
    addVar("Shivs", 3);
  }
  Task<> beforeHandDraw() override {
    if (!combat || combat->turnNumber > 1) co_return;
    doFlash();
    co_await createShivsInHand(*combat, val("Shivs").toInt());
  }
};

// Tingsha.cs: discarding a card (on your own turn) throws 3 unpowered damage at a random enemy.
struct Tingsha : Relic {
  RELIC_HEADER(Tingsha, "TINGSHA", Uncommon)
    addVar("Damage", 3);
  }
  Task<> afterCardDiscarded(Card* card) override {
    if (!combat || combat->currentSide != Side::Player || ownerOf(card) != owner()) co_return;
    doFlash();
    Creature* t = combat->rng("CombatTargets").nextItem(combat->hittableEnemies());
    if (t) co_await cmd::damage(t, val("Damage"), kUnpowered, owner(), nullptr);
  }
};

// ToughBandages.cs: discarding a card (on your own turn) gives 3 unpowered block.
struct ToughBandages : Relic {
  RELIC_HEADER(ToughBandages, "TOUGH_BANDAGES", Rare)
    addVar("Block", 3);
  }
  Task<> afterCardDiscarded(Card* card) override {
    if (!combat || combat->currentSide != Side::Player || ownerOf(card) != owner()) co_return;
    doFlash();
    co_await cmd::gainBlock(owner(), val("Block"), kUnpowered, nullptr);
  }
};

// PaperKrane.cs (Rare): +15% extra damage reduction from the owner's own Weak, on top of the
// usual 25%. The relic itself has no hooks; the effect is special-cased directly in
// WeakPower::modifyDamageMultiplicative (powers.h), the same way VulnerablePower there
// special-cases PaperPhrog / CrueltyPower, instead of adding a new hook family.
struct PaperKrane : Relic {
  RELIC_HEADER(PaperKrane, "PAPER_KRANE", Rare) }
};

// TwistedFunnel.cs: at the start of turn 1, Poison every enemy for 4.
struct TwistedFunnel : Relic {
  RELIC_HEADER(TwistedFunnel, "TWISTED_FUNNEL", Uncommon)
    addVar("PoisonPower", 4);
  }
  Task<> beforeSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!combat || !contains(participants, owner()) || combat->turnNumber > 1) co_return;
    doFlash();
    for (Creature* e : combat->hittableEnemies()) co_await applyPower<PoisonPower>(e, val("PoisonPower"), owner(), nullptr);
  }
};

}  // namespace

void registerSilentRelics() {
  reg<RingOfTheSnake>();
  reg<SneckoSkull>();
  registerPowerType<HelicalDartPower>();
  reg<HelicalDart>();
  reg<NinjaScroll>();
  reg<PaperKrane>();
  reg<Tingsha>();
  reg<ToughBandages>();
  reg<TwistedFunnel>();
}

}  // namespace sts
