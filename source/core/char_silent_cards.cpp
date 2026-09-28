// The Silent's Common cards (X1.2), translated from MegaCrit.Sts2.Core.Models.Cards.* of the same
// name (SilentCardPool.cs, CardRarity.Common). Starters (X1.1) are in char_silent.cpp; shared
// systems (Poison, Shiv, Accuracy, Fan of Knives) are in char_silent.h (X1.0).
#include "cards.h"
#include "char_silent.h"

namespace sts {

namespace {

// ---------------------------------------------------------------- powers card-local to this package

// AnticipatePower: TemporaryDexterityPower (Anticipate.cs is its OriginModel) -- applies Dexterity
// immediately, reverses it at the end of the turn it was played (a "this turn only" buff, like the
// Ironclad's SetupStrikePower/StrengthPower pair in content.cpp).
struct AnticipatePower : Power {
  POWER_HEADER(AnticipatePower, "TEMPORARY_DEXTERITY_POWER")
  Task<> beforeApplied(Creature* target, Dec amt, Creature* app, Card* src) override {
    co_await applyPower<DexterityPower>(target, amt, app, src, true);
  }
  Task<> afterPowerAmountChanged(Power* p, Dec amt, Creature* app, Card* src) override {
    if (!(amt == Dec(amount)) && p == this) co_await applyPower<DexterityPower>(owner, amt, app, src, true);
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) {
      flash = 1.f;
      Creature* o = owner;
      int a = amount;
      co_await cmd::removePower(this);
      co_await applyPower<DexterityPower>(o, -a, o, nullptr);
    }
  }
};

// PiercingWailPower: TemporaryStrengthPower, negative (PiercingWail.cs's IsPositive => false) --
// takes Strength away for the rest of the turn, restored at the end of it.
struct PiercingWailPower : Power {
  POWER_HEADER(PiercingWailPower, "TEMPORARY_STRENGTH_DOWN")
  PowerType type() const override { return PowerType::Debuff; }
  Task<> beforeApplied(Creature* target, Dec amt, Creature* app, Card* src) override {
    co_await applyPower<StrengthPower>(target, -amt, app, src, true);
  }
  Task<> afterPowerAmountChanged(Power* p, Dec amt, Creature* app, Card* src) override {
    if (!(amt == Dec(amount)) && p == this) co_await applyPower<StrengthPower>(owner, -amt, app, src, true);
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) {
      flash = 1.f;
      Creature* o = owner;
      int a = amount;
      co_await cmd::removePower(this);
      co_await applyPower<StrengthPower>(o, a, o, nullptr);
    }
  }
};

// BlockNextTurnPower (DodgeAndRoll) and DrawCardsNextTurnPower (Predator) are the shared ones in powers.h.

// ---------------------------------------------------------------- cards

// Anticipate.cs: 0 cost, gain Dexterity for the rest of the turn.
struct Anticipate : IroncladT<Anticipate> {
  CARD_HEADER(Anticipate, "ANTICIPATE", 0, Skill, Common, Self)
    addVar("DexterityPower", 2);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<AnticipatePower>(me(), val("DexterityPower"), me(), this); }
  void onUpgrade() override { upgradeVar("DexterityPower", 2); }
};

// Backflip.cs: gain block, draw 2.
struct Backflip : IroncladT<Backflip> {
  CARD_HEADER(Backflip, "BACKFLIP", 1, Skill, Common, Self)
    addVar("Block", 5);
    addVar("Cards", 2);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    co_await drawCards(val("Cards"));
  }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

// BladeDance.cs: Exhaust, put 3 Shivs in hand.
struct BladeDance : IroncladT<BladeDance> {
  CARD_HEADER(BladeDance, "BLADE_DANCE", 1, Skill, Common, Self)
    keywords = kwExhaust;
    addVar("Cards", 3);
  }
  Task<> onPlay(CardPlay&) override { co_await createShivsInHand(*combat, val("Cards").toInt()); }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

// CloakAndDagger.cs: gain block, put 1 Shiv in hand.
struct CloakAndDagger : IroncladT<CloakAndDagger> {
  CARD_HEADER(CloakAndDagger, "CLOAK_AND_DAGGER", 1, Skill, Common, Self)
    addVar("Block", 6);
    addVar("Cards", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    co_await createShivsInHand(*combat, val("Cards").toInt());
  }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

// DaggerSpray.cs: hits every enemy twice.
struct DaggerSpray : IroncladT<DaggerSpray> {
  CARD_HEADER(DaggerSpray, "DAGGER_SPRAY", 1, Attack, Common, AllEnemies)
    addVar("Damage", 4);
  }
  Task<> onPlay(CardPlay&) override { co_await attackAll(val("Damage"), 2); }
  void onUpgrade() override { upgradeVar("Damage", 2); }
};

// DaggerThrow.cs: attack, draw 1, discard a chosen card.
struct DaggerThrow : IroncladT<DaggerThrow> {
  CARD_HEADER(DaggerThrow, "DAGGER_THROW", 1, Attack, Common, AnyEnemy)
    addVar("Damage", 9);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await drawCards(1);
    auto picked = co_await cmd::selectCards(*combat, "card_selection.TO_DISCARD", combat->hand, 1, 1);
    if (!picked.empty()) co_await cmd::discardCard(*combat, picked[0]);
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

// DeadlyPoison.cs: apply Poison to a single enemy.
struct DeadlyPoison : IroncladT<DeadlyPoison> {
  CARD_HEADER(DeadlyPoison, "DEADLY_POISON", 1, Skill, Common, AnyEnemy)
    addVar("PoisonPower", 5);
  }
  Task<> onPlay(CardPlay& p) override { co_await applyPower<PoisonPower>(p.target, val("PoisonPower"), me(), this); }
  void onUpgrade() override { upgradeVar("PoisonPower", 2); }
};

// Deflect.cs: 0 cost, small block.
struct Deflect : IroncladT<Deflect> {
  CARD_HEADER(Deflect, "DEFLECT", 0, Skill, Common, Self)
    addVar("Block", 4);
  }
  Task<> onPlay(CardPlay&) override { co_await block(val("Block")); }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

// DodgeAndRoll.cs: gain block, then gain the same amount of block again next turn.
struct DodgeAndRoll : IroncladT<DodgeAndRoll> {
  CARD_HEADER(DodgeAndRoll, "DODGE_AND_ROLL", 1, Skill, Common, Self)
    addVar("Block", 4);
  }
  Task<> onPlay(CardPlay&) override {
    Dec gained = co_await cmd::gainBlock(me(), val("Block"), kMove, this);
    co_await applyPower<BlockNextTurnPower>(me(), gained, me(), this);
  }
  void onUpgrade() override { upgradeVar("Block", 2); }
};

// FlickFlack.cs: Sly, hits every enemy once.
struct FlickFlack : IroncladT<FlickFlack> {
  CARD_HEADER(FlickFlack, "FLICK_FLACK", 1, Attack, Common, AllEnemies)
    keywords = kwSly;
    addVar("Damage", 7);
  }
  Task<> onPlay(CardPlay&) override { co_await attackAll(val("Damage")); }
  void onUpgrade() override { upgradeVar("Damage", 2); }
};

// LeadingStrike.cs: Strike, attack + put Shivs in hand.
struct LeadingStrike : IroncladT<LeadingStrike> {
  CARD_HEADER(LeadingStrike, "LEADING_STRIKE", 1, Attack, Common, AnyEnemy)
    tags = tagStrike;
    addVar("Shivs", 2);
    addVar("Damage", 3);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await createShivsInHand(*combat, val("Shivs").toInt());
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

// PiercingWail.cs: Exhaust, every enemy loses Strength for the rest of the turn.
struct PiercingWail : IroncladT<PiercingWail> {
  CARD_HEADER(PiercingWail, "PIERCING_WAIL", 1, Skill, Common, AllEnemies)
    keywords = kwExhaust;
    addVar("StrengthLoss", 6);
  }
  Task<> onPlay(CardPlay&) override {
    for (Creature* e : combat->hittableEnemies()) co_await applyPower<PiercingWailPower>(e, val("StrengthLoss"), me(), this);
  }
  void onUpgrade() override { upgradeVar("StrengthLoss", 2); }
};

// PoisonedStab.cs: attack + apply Poison.
struct PoisonedStab : IroncladT<PoisonedStab> {
  CARD_HEADER(PoisonedStab, "POISONED_STAB", 1, Attack, Common, AnyEnemy)
    addVar("Damage", 6);
    addVar("PoisonPower", 3);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<PoisonPower>(p.target, val("PoisonPower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 2); upgradeVar("PoisonPower", 1); }
};

// Predator.cs: big hit, draw 2 extra cards next turn.
struct Predator : IroncladT<Predator> {
  CARD_HEADER(Predator, "PREDATOR", 2, Attack, Common, AnyEnemy)
    addVar("Damage", 15);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<DrawCardsNextTurnPower>(me(), Dec(2), me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 5); }
};

// Prepared.cs: 0 cost, draw N then discard N chosen cards.
struct Prepared : IroncladT<Prepared> {
  CARD_HEADER(Prepared, "PREPARED", 0, Skill, Common, Self)
    addVar("Cards", 1);
  }
  Task<> onPlay(CardPlay&) override {
    int n = val("Cards").toInt();
    co_await drawCards(n);
    auto picked = co_await cmd::selectCards(*combat, "card_selection.TO_DISCARD", combat->hand, n, n);
    co_await cmd::discardCards(*combat, picked);
  }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

// Ricochet.cs: Sly, hits a random enemy 4 times.
struct Ricochet : IroncladT<Ricochet> {
  CARD_HEADER(Ricochet, "RICOCHET", 2, Attack, Common, RandomEnemy)
    keywords = kwSly;
    addVar("Damage", 3);
    addVar("Repeat", 4);
  }
  Task<> onPlay(CardPlay&) override { co_await attackRandom(val("Damage"), val("Repeat").toInt()); }
  void onUpgrade() override { upgradeVar("Repeat", 1); }
};

// Slice.cs: 0 cost attack.
struct Slice : IroncladT<Slice> {
  CARD_HEADER(Slice, "SLICE", 0, Attack, Common, AnyEnemy)
    addVar("Damage", 6);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

// Snakebite.cs: Retain, apply a big Poison stack.
struct Snakebite : IroncladT<Snakebite> {
  CARD_HEADER(Snakebite, "SNAKEBITE", 2, Skill, Common, AnyEnemy)
    keywords = kwRetain;
    addVar("PoisonPower", 7);
  }
  Task<> onPlay(CardPlay& p) override { co_await applyPower<PoisonPower>(p.target, val("PoisonPower"), me(), this); }
  void onUpgrade() override { upgradeVar("PoisonPower", 3); }
};

// SuckerPunch.cs: attack + Weak.
struct SuckerPunch : IroncladT<SuckerPunch> {
  CARD_HEADER(SuckerPunch, "SUCKER_PUNCH", 1, Attack, Common, AnyEnemy)
    addVar("Damage", 8);
    addVar("WeakPower", 1);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<WeakPower>(p.target, val("WeakPower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 2); upgradeVar("WeakPower", 1); }
};

// Untouchable.cs: Sly, block.
struct Untouchable : IroncladT<Untouchable> {
  CARD_HEADER(Untouchable, "UNTOUCHABLE", 2, Skill, Common, Self)
    keywords = kwSly;
    addVar("Block", 6);
  }
  Task<> onPlay(CardPlay&) override { co_await block(val("Block")); }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

}  // namespace

void registerSilentCards() {
  registerPowerType<AnticipatePower>();
  registerPowerType<PiercingWailPower>();
  registerPowerType<BlockNextTurnPower>();
  registerPowerType<DrawCardsNextTurnPower>();

  registerCardType<Anticipate>();
  registerCardType<Backflip>();
  registerCardType<BladeDance>();
  registerCardType<CloakAndDagger>();
  registerCardType<DaggerSpray>();
  registerCardType<DaggerThrow>();
  registerCardType<DeadlyPoison>();
  registerCardType<Deflect>();
  registerCardType<DodgeAndRoll>();
  registerCardType<FlickFlack>();
  registerCardType<LeadingStrike>();
  registerCardType<PiercingWail>();
  registerCardType<PoisonedStab>();
  registerCardType<Predator>();
  registerCardType<Prepared>();
  registerCardType<Ricochet>();
  registerCardType<Slice>();
  registerCardType<Snakebite>();
  registerCardType<SuckerPunch>();
  registerCardType<Untouchable>();
}

}  // namespace sts
