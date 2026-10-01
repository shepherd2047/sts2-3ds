// The Regent's uncommon cards, second half (X3.3b), translated from Models.Cards\<Name>.cs in
// RegentCardPool order: Orbit .. Terraforming. Registered from registerRegent() (char_regent.cpp)
// via registerRegentUncommonCards2(). Same style as char_regent_cards_uncommon1.cpp.
#include "cards.h"
#include "char_regent.h"
#include "colorless.h"

namespace sts {

namespace {

// ================================================================ powers used by one card here

// OrbitPower.cs: Instanced (each Orbit played counts its own energy). Every 4 energy spent grants Amount
// energy; the HUD shows the energy left to the next trigger.
struct OrbitPower : Power {
  POWER_HEADER(OrbitPower, "ORBIT_POWER")
  PowerInstanceType instanceType() const override { return PowerInstanceType::Instanced; }
  int displayAmount() const override { return 4 - energySpent % 4; }
  int energySpent = 0, triggerCount = 0;  // Data
  Task<> afterEnergySpent(Card* card, int spent) override {
    if (ownerOf(card) != owner || spent <= 0) co_return;
    energySpent += spent;
    int triggers = energySpent / 4 - triggerCount;
    if (triggers > 0) {
      flash = 1.f;
      co_await cmd::gainEnergy(*owner->combat, amount * triggers);
      triggerCount += triggers;
    }
  }
};

// PaleBlueDotPower.cs: once per turn, when the owner has finished playing 5 cards this turn
// (CardPlaysFinished, all types), draw Amount more cards next turn.
struct PaleBlueDotPower : Power {
  POWER_HEADER(PaleBlueDotPower, "PALE_BLUE_DOT_POWER")
  bool alreadyActivatedThisTurn = false;
  Task<> afterCardPlayed(const CardPlay& cp) override {
    if (ownerOf(cp.card) != owner) co_return;
    if (alreadyActivatedThisTurn) co_return;
    if (owner->combat->cardPlaysFinishedThisTurn >= 5) {
      alreadyActivatedThisTurn = true;
      co_await applyPower<DrawCardsNextTurnPower>(owner, amount, owner, nullptr);
    }
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) alreadyActivatedThisTurn = false;
    co_return;
  }
};

// PillarOfCreationPower.cs: gain Amount unpowered block whenever the owner generates a card.
struct PillarOfCreationPower : Power {
  POWER_HEADER(PillarOfCreationPower, "PILLAR_OF_CREATION_POWER")
  Task<> afterCardEnteredCombat(Card* c) override {
    if (!c->createdByPlayer) co_return;  // creator == Owner
    flash = 1.f;
    co_await cmd::gainBlock(owner, Dec(amount), kUnpowered, nullptr);
  }
};

// ReflectPower.cs: blocked damage from powered attacks is dealt back to the attacker (unpowered);
// loses one stack at the start of the owner's side turn.
struct ReflectPower : Power {
  POWER_HEADER(ReflectPower, "REFLECT_POWER")
  Task<> afterDamageReceived(Creature* target, const DamageResult& r, int props, Creature* dealer, Card*) override {
    if (target == owner && r.blocked > 0 && isPoweredAttack(props) && dealer)
      co_await cmd::damage(dealer, Dec(r.blocked), kUnpowered, owner, nullptr);
  }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await cmd::decrement(this);
  }
};

// SpectrumShiftPower.cs: before the hand draw each turn, add Amount distinct random colorless
// cards to the hand.
struct SpectrumShiftPower : Power {
  POWER_HEADER(SpectrumShiftPower, "SPECTRUM_SHIFT_POWER")
  Task<> beforeHandDraw() override {
    for (auto& k : colorlessDistinctForCombat(*owner->combat, amount))
      co_await cmd::addGeneratedCard(*owner->combat, std::move(k), Pile::Hand);
    flash = 1.f;
  }
};

// ================================================================ cards

// Orbit.cs: 2 cost, Power, Self. OrbitPower (Energy 1).
struct Orbit : IroncladT<Orbit> {
  CARD_HEADER(Orbit, "ORBIT", 2, Power, Uncommon, Self)
    addVar("Energy", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<OrbitPower>(me(), val("Energy"), me(), this);
  }
  void onUpgrade() override { cost -= 1; }
};

// PaleBlueDot.cs: 1 cost, Power, Self. PaleBlueDotPower (Cards 1, CardPlay 5).
struct PaleBlueDot : IroncladT<PaleBlueDot> {
  CARD_HEADER(PaleBlueDot, "PALE_BLUE_DOT", 1, Power, Uncommon, Self)
    addVar("Cards", 1);
    addVar("CardPlay", 5);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<PaleBlueDotPower>(me(), val("Cards"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

// Parry.cs: 1 cost, Power, Self. ParryPower 10 (Sovereign Blade block).
struct Parry : IroncladT<Parry> {
  CARD_HEADER(Parry, "PARRY", 1, Power, Uncommon, Self)
    addVar("ParryPower", 10);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<ParryPower>(me(), val("ParryPower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("ParryPower", 4); }
};

// ParticleWall.cs: 0 cost, 2 stars, Skill, Self. Block 9; returns to the hand instead of the
// discard pile.
struct ParticleWall : IroncladT<ParticleWall> {
  CARD_HEADER(ParticleWall, "PARTICLE_WALL", 0, Skill, Uncommon, Self)
    starCost = 2;
    addVar("Block", 9);
  }
  Task<> onPlay(CardPlay&) override { co_await block(val("Block")); }
  bool gainsBlock() const override { return true; }
  // GetResultLocationForCardPlay override: Discard -> Hand.
  Pile modifyCardPlayResultLocation(Card* c, bool, Pile pile) override {
    return (c == this && pile == Pile::Discard) ? Pile::Hand : pile;
  }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

// PillarOfCreation.cs: 1 cost, Power, Self. PillarOfCreationPower (Block 2, unpowered).
struct PillarOfCreation : IroncladT<PillarOfCreation> {
  CARD_HEADER(PillarOfCreation, "PILLAR_OF_CREATION", 1, Power, Uncommon, Self)
    addVar("Block", 2);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<PillarOfCreationPower>(me(), val("Block"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Block", 1); }
};

// Prophesize.cs: 2 cost, Skill, Self. Draw 6.
struct Prophesize : IroncladT<Prophesize> {
  CARD_HEADER(Prophesize, "PROPHESIZE", 2, Skill, Uncommon, Self)
    addVar("Cards", 6);
  }
  Task<> onPlay(CardPlay&) override { co_await drawCards(val("Cards")); }
  void onUpgrade() override { upgradeVar("Cards", 3); }
};

// Quasar.cs: 0 cost, 2 stars, Skill, Self. Choose 1 of 3 distinct random colorless cards (upgraded
// if this is) to add to the hand, skippable.
struct Quasar : IroncladT<Quasar> {
  CARD_HEADER(Quasar, "QUASAR", 0, Skill, Uncommon, Self)
    starCost = 2;
  }
  Task<> onPlay(CardPlay&) override {
    auto options = colorlessDistinctForCombat(*combat, 3);
    if (upgraded()) for (auto& k : options) cmd::upgradeCard(k.get());
    co_await chooseGeneratedToHand(*combat, std::move(options), false);
  }
};

// Radiate.cs: 0 cost, Attack, AllEnemies. Damage 3, hit once per star gained this turn
// (CalculatedHits = 0 + 1 * StarsModifiedEntry amounts > 0 this turn).
struct Radiate : IroncladT<Radiate> {
  CARD_HEADER(Radiate, "RADIATE", 0, Attack, Uncommon, AllEnemies)
    addVar("Damage", 3);
    addVar("Stars", 1);
    addVar("CalculationBase", 0);
    addVar("CalculationExtra", 1);
    addVar("CalculatedHits", 0);
    calcMultiplier = [](Card* c) { return c->combat ? c->combat->starsGainedThisTurn : 0; };
  }
  Task<> onPlay(CardPlay&) override {
    // CalculatedVar.Calculate = CalculationBase + CalculationExtra * multiplier
    co_await attackAll(val("Damage"), calculatedBlock().toInt());
  }
  void onUpgrade() override { upgradeVar("Damage", 1); }
};

// Reflect.cs: 1 cost, 3 stars, Skill, Self. Block 15, then ReflectPower 1.
struct Reflect : IroncladT<Reflect> {
  CARD_HEADER(Reflect, "REFLECT", 1, Skill, Uncommon, Self)
    starCost = 3;
    addVar("Block", 15);
  }
  bool gainsBlock() const override { return true; }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    co_await applyPower<ReflectPower>(me(), 1, me(), this);
  }
  void onUpgrade() override { upgradeVar("Block", 5); }
};

// Resonance.cs: 1 cost, 2 stars, Skill, AllEnemies. Gain Strength 1, every hittable enemy loses 1.
struct Resonance : IroncladT<Resonance> {
  CARD_HEADER(Resonance, "RESONANCE", 1, Skill, Uncommon, AllEnemies)
    starCost = 2;
    addVar("StrengthPower", 1);
  }
  Task<> onPlay(CardPlay&) override {
    int n = val("StrengthPower").toInt();
    co_await applyPower<StrengthPower>(me(), n, me(), this);
    for (Creature* e : combat->hittableEnemies())
      co_await applyPower<StrengthPower>(e, -1, me(), this);
  }
  void onUpgrade() override { upgradeVar("StrengthPower", 1); }
};

// RoyalGamble.cs: 0 cost, 5 stars, Skill, Self, Exhaust. Gain 9 stars. Upgrade adds Retain.
struct RoyalGamble : IroncladT<RoyalGamble> {
  CARD_HEADER(RoyalGamble, "ROYAL_GAMBLE", 0, Skill, Uncommon, Self)
    starCost = 5;
    keywords = kwExhaust;
    addVar("Stars", 9);
  }
  Task<> onPlay(CardPlay&) override { co_await cmd::gainStars(*combat, val("Stars").toInt()); }
  void onUpgrade() override { keywords |= kwRetain; }
};

// ShiningStrike.cs: 1 cost, Attack, AnyEnemy, tag Strike. Damage 8, then gain 2 stars; goes on top
// of the draw pile instead of the discard pile.
struct ShiningStrike : IroncladT<ShiningStrike> {
  CARD_HEADER(ShiningStrike, "SHINING_STRIKE", 1, Attack, Uncommon, AnyEnemy)
    tags = tagStrike;
    addVar("Damage", 8);
    addVar("Stars", 2);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await cmd::gainStars(*combat, val("Stars").toInt());
  }
  // GetResultLocationForCardPlay override: Discard -> top of the draw pile (Combat::playCard
  // moves a Pile::Draw result to the top).
  Pile modifyCardPlayResultLocation(Card* c, bool, Pile pile) override {
    return (c == this && pile == Pile::Discard) ? Pile::Draw : pile;
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

// SpectrumShift.cs: 2 cost, Power, Self. SpectrumShiftPower (Cards 1).
struct SpectrumShift : IroncladT<SpectrumShift> {
  CARD_HEADER(SpectrumShift, "SPECTRUM_SHIFT", 2, Power, Uncommon, Self)
    addVar("Cards", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<SpectrumShiftPower>(me(), val("Cards"), me(), this);
  }
  void onUpgrade() override { cost -= 1; }
};

// Stardust.cs: 0 cost, X stars, Attack, RandomEnemy. Damage 5 per star spent, each hit at a
// random enemy.
struct Stardust : IroncladT<Stardust> {
  CARD_HEADER(Stardust, "STARDUST", 0, Attack, Uncommon, RandomEnemy)
    costsStarsX = true;
    addVar("Damage", 5);
  }
  Task<> onPlay(CardPlay&) override { co_await attackRandom(val("Damage"), starXValue); }
  void onUpgrade() override { upgradeVar("Damage", 2); }
};

// SummonForth.cs: 1 cost, Skill, Self. Every Sovereign Blade outside the hand (draw, discard,
// exhaust...) goes to the hand, then Forge 8.
struct SummonForth : IroncladT<SummonForth> {
  CARD_HEADER(SummonForth, "SUMMON_FORTH", 1, Skill, Uncommon, Self)
    addVar("Forge", 8);
  }
  Task<> onPlay(CardPlay&) override {
    std::vector<Card*> blades;
    for (Card* c : combat->allCards())
      if (c->id == "SovereignBlade" && combat->pileOf(c) != Pile::Hand) blades.push_back(c);
    for (Card* c : blades) co_await cmd::moveCard(*combat, c, Pile::Hand);
    co_await cmd::forge(*combat, val("Forge"), this);
  }
  void onUpgrade() override { upgradeVar("Forge", 3); }
};

// Supermassive.cs: 1 cost, Attack, AnyEnemy. Damage 5 + 3 per card the player generated this
// combat (CardGeneratedEntry count, Combat::cardsGeneratedThisCombat).
struct Supermassive : IroncladT<Supermassive> {
  CARD_HEADER(Supermassive, "SUPERMASSIVE", 1, Attack, Uncommon, AnyEnemy)
    addVar("CalculationBase", 5);
    addVar("ExtraDamage", 3);
    addVar("CalculatedDamage", 0);
    calcMultiplier = [](Card* c) { return c->combat ? c->combat->cardsGeneratedThisCombat : 0; };
  }
  Task<> onPlay(CardPlay& p) override { co_await attackCalculated(p.target); }
  void onUpgrade() override { upgradeVar("ExtraDamage", 1); }
};

// Terraforming.cs: 1 cost, Skill, Self. Gain 7 Vigor.
struct Terraforming : IroncladT<Terraforming> {
  CARD_HEADER(Terraforming, "TERRAFORMING", 1, Skill, Uncommon, Self)
    addVar("VigorPower", 7);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<VigorPower>(me(), val("VigorPower").toInt(), me(), this);
  }
  void onUpgrade() override { upgradeVar("VigorPower", 3); }
};

}  // namespace

void registerRegentUncommonCards2() {
  registerPowerType<OrbitPower>();
  registerPowerType<PaleBlueDotPower>();
  registerPowerType<PillarOfCreationPower>();
  registerPowerType<ReflectPower>();
  registerPowerType<SpectrumShiftPower>();

  registerCardType<Orbit>();
  registerCardType<PaleBlueDot>();
  registerCardType<Parry>();
  registerCardType<ParticleWall>();
  registerCardType<PillarOfCreation>();
  registerCardType<Prophesize>();
  registerCardType<Quasar>();
  registerCardType<Radiate>();
  registerCardType<Reflect>();
  registerCardType<Resonance>();
  registerCardType<RoyalGamble>();
  registerCardType<ShiningStrike>();
  registerCardType<SpectrumShift>();
  registerCardType<Stardust>();
  registerCardType<SummonForth>();
  registerCardType<Supermassive>();
  registerCardType<Terraforming>();
}

}  // namespace sts
