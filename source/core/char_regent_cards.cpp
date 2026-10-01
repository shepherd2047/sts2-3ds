// The Regent's common cards (X3.2), translated from Models.Cards\<Name>.cs (see
// CardPools\RegentCardPool.cs for the full 91-card pool; only the CardRarity.Common ones are
// here). Registered from registerRegent() (char_regent.cpp). Star-cost cards set Card::starCost
// like FallingStar/Alignment; the shared Forge/Sovereign-Blade plumbing and StarNextTurnPower
// live in char_regent.h (X3.0). Written like the Ironclad's content.cpp / content_uncommon.cpp.
#include "cards.h"
#include "char_regent.h"

namespace sts {

namespace {

// ================================================================ powers used by one card here

// CrushUnderPower: TemporaryStrengthPower(IsPositive: false) in the C#, i.e. the same "apply
// negative Strength now, restore it at the end of the turn" shape as ManglePower
// (powers_ironclad.h) under its own id (CrushUnder's own hover tip looks it up by name).
struct CrushUnderPower : Power {
  POWER_HEADER(CrushUnderPower, "CRUSH_UNDER_POWER")
  const char* internallyAppliedPower() const override { return "StrengthPower"; }  // ITemporaryPower
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

// ================================================================ token cards

// MinionStrike.cs: created by Begone (transform target). 0 cost, Attack, Token, Exhaust,
// tags Strike + Minion (VitruvianMinion/Regalite key off tagMinion). Damage 6, draw 1.
struct MinionStrike : IroncladT<MinionStrike> {
  CARD_HEADER(MinionStrike, "MINION_STRIKE", 0, Attack, Token, AnyEnemy)
    keywords = kwExhaust;
    tags = tagStrike | tagMinion;
    addVar("Damage", 6);
    addVar("Cards", 1);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await drawCards(val("Cards"));
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

// Debris.cs: created by CollisionCourse. 1 cost, Status, Exhaust, no upgrade, does nothing.
struct Debris : IroncladT<Debris> {
  CARD_HEADER(Debris, "DEBRIS", 1, Status, Status, None)
    keywords = kwExhaust;
    maxUpgradeLevel = 0;
  }
};

// ================================================================ common cards

// AstralPulse.cs: 0 cost, 3 stars, Attack, AllEnemies. Damage 6, 2 hits.
struct AstralPulse : IroncladT<AstralPulse> {
  CARD_HEADER(AstralPulse, "ASTRAL_PULSE", 0, Attack, Common, AllEnemies)
    starCost = 3;
    addVar("Damage", 6);
  }
  Task<> onPlay(CardPlay&) override { co_await attackAll(val("Damage"), 2); }
  void onUpgrade() override { upgradeVar("Damage", 2); }
};

// Begone.cs: 1 cost, Skill, Self. Select up to 1 card from hand and transform it into a
// MinionStrike (upgraded if Begone is upgraded).
struct Begone : IroncladT<Begone> {
  CARD_HEADER(Begone, "BEGONE", 1, Skill, Common, Self)
  }
  Task<> onPlay(CardPlay&) override {
    auto picked = co_await cmd::selectCards(*combat, "BEGONE", combat->hand, 0, 1);
    if (picked.empty()) co_return;
    auto minion = db::card("MinionStrike");
    if (upgraded()) minion->upgrade();
    co_await cmd::transform(*combat, picked[0], std::move(minion));
  }
};

// CelestialMight.cs: 2 cost, Attack, AnyEnemy. Damage 6, 3 hits.
struct CelestialMight : IroncladT<CelestialMight> {
  CARD_HEADER(CelestialMight, "CELESTIAL_MIGHT", 2, Attack, Common, AnyEnemy)
    addVar("Damage", 6);
    addVar("Repeat", 3);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage"), val("Repeat").toInt()); }
  void onUpgrade() override { upgradeVar("Repeat", 1); }
};

// CloakOfStars.cs: 0 cost, 1 star, Skill, Self. Block 7.
struct CloakOfStars : IroncladT<CloakOfStars> {
  CARD_HEADER(CloakOfStars, "CLOAK_OF_STARS", 0, Skill, Common, Self)
    starCost = 1;
    addVar("Block", 7);
  }
  Task<> onPlay(CardPlay&) override { co_await block(val("Block")); }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

// CollisionCourse.cs: 0 cost, Attack, AnyEnemy. Damage 10, then a Debris joins the hand.
// PORT NOTE (n/a: visual): drops Cmd.Wait(0.5f) (pure animation pacing).
struct CollisionCourse : IroncladT<CollisionCourse> {
  CARD_HEADER(CollisionCourse, "COLLISION_COURSE", 0, Attack, Common, AnyEnemy)
    addVar("Damage", 10);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await cmd::addGeneratedCard(*combat, db::card("Debris"), Pile::Hand);
  }
  void onUpgrade() override { upgradeVar("Damage", 4); }
};

// CosmicIndifference.cs: 1 cost, Skill, Self. Block 6, select 1 card from the discard pile and
// put it on top of the draw pile. PORT NOTE (n/a: single-player): drops the C#'s Pile==Draw||Discard re-check on the
// selected card (a guard against it moving mid-selection in multiplayer).
struct CosmicIndifference : IroncladT<CosmicIndifference> {
  CARD_HEADER(CosmicIndifference, "COSMIC_INDIFFERENCE", 1, Skill, Common, Self)
    addVar("Block", 6);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    auto picked = co_await cmd::selectCards(*combat, "COSMIC_INDIFFERENCE", combat->discard, 1, 1);
    if (!picked.empty()) co_await cmd::moveCard(*combat, picked[0], Pile::Draw, true);
  }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

// CrescentSpear.cs: 1 cost, 1 star, Attack, AnyEnemy. CalculatedDamage = 8 + 2 * (number of
// cards the player has, anywhere, with a star cost or HasStarCostX).
struct CrescentSpear : IroncladT<CrescentSpear> {
  CARD_HEADER(CrescentSpear, "CRESCENT_SPEAR", 1, Attack, Common, AnyEnemy)
    starCost = 1;
    addVar("CalculationBase", 8);
    addVar("ExtraDamage", 2);
    addVar("CalculatedDamage", 0);
    calcMultiplier = [](Card* c) {
      if (!c->combat) return 0;
      int n = 0;
      for (Card* k : c->combat->allCards()) if (k->starCost >= 0 || k->costsStarsX) ++n;
      return n;
    };
  }
  Task<> onPlay(CardPlay& p) override { co_await attackCalculated(p.target); }
  void onUpgrade() override { upgradeVar("ExtraDamage", 1); }
};

// CrushUnder.cs: 1 cost, Attack, AllEnemies. Damage 8, then CrushUnderPower 1 (temporary
// Strength loss) on the (pre-attack) hittable enemies. PORT NOTE (n/a: visual): drops the spike-splash vfx.
struct CrushUnder : IroncladT<CrushUnder> {
  CARD_HEADER(CrushUnder, "CRUSH_UNDER", 1, Attack, Common, AllEnemies)
    addVar("Damage", 8);
    addVar("StrengthLoss", 1);
  }
  Task<> onPlay(CardPlay&) override {
    std::vector<Creature*> enemies = combat->hittableEnemies();
    co_await attackAll(val("Damage"));
    for (Creature* e : enemies) co_await applyPower<CrushUnderPower>(e, val("StrengthLoss"), me(), this);
  }
  void onUpgrade() override {
    upgradeVar("Damage", 1);
    upgradeVar("StrengthLoss", 1);
  }
};

// GatherLight.cs: 1 cost, Skill, Self. Block 8, gain 1 star.
struct GatherLight : IroncladT<GatherLight> {
  CARD_HEADER(GatherLight, "GATHER_LIGHT", 1, Skill, Common, Self)
    addVar("Block", 8);
    addVar("Stars", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    co_await cmd::gainStars(*combat, val("Stars").toInt());
  }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

// Glitterstream.cs: 2 cost, Skill, Self. Block 11, plus BlockNextTurnPower for 5 block next
// turn (run through Hook.ModifyBlock now, same as the block it gains immediately, so Dexterity
// etc. apply once at cast time rather than when it triggers).
struct Glitterstream : IroncladT<Glitterstream> {
  CARD_HEADER(Glitterstream, "GLITTERSTREAM", 2, Skill, Common, Self)
    addVar("Block", 11);
    addVar("BlockNextTurn", 5);
  }
  Task<> onPlay(CardPlay&) override {
    Dec nextTurn = combat->modifyBlock(me(), val("BlockNextTurn"), kMove, this);
    co_await block(val("Block"));
    co_await applyPower<BlockNextTurnPower>(me(), nextTurn, me(), this);
  }
  void onUpgrade() override {
    upgradeVar("Block", 2);
    upgradeVar("BlockNextTurn", 2);
  }
};

// Glow.cs: 1 cost, Skill, Self. Gain 1 star, draw 1, and draw 1 extra card next turn
// (DrawCardsNextTurnPower, char_regent.h).
struct Glow : IroncladT<Glow> {
  CARD_HEADER(Glow, "GLOW", 1, Skill, Common, Self)
    addVar("Stars", 1);
    addVar("Cards", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await cmd::gainStars(*combat, val("Stars").toInt());
    co_await drawCards(val("Cards"));
    co_await applyPower<DrawCardsNextTurnPower>(me(), val("Cards"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Stars", 1); }
};

// GuidingStar.cs: 1 cost, 1 star, Attack, AnyEnemy. Damage 12, then draw 2 extra cards next
// turn (DrawCardsNextTurnPower). PORT NOTE (n/a: visual): drops the magic-missile vfx/sfx and
// WithNoAttackerAnim; the damage still counts as a normal powered attack.
struct GuidingStar : IroncladT<GuidingStar> {
  CARD_HEADER(GuidingStar, "GUIDING_STAR", 1, Attack, Common, AnyEnemy)
    starCost = 1;
    addVar("Damage", 12);
    addVar("Cards", 2);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<DrawCardsNextTurnPower>(me(), val("Cards"), me(), this);
  }
  void onUpgrade() override {
    upgradeVar("Damage", 1);
    upgradeVar("Cards", 1);
  }
};

// HiddenCache.cs: 1 cost, Skill, Self. Gain 1 star, StarNextTurnPower 3 (char_regent.h).
struct HiddenCache : IroncladT<HiddenCache> {
  CARD_HEADER(HiddenCache, "HIDDEN_CACHE", 1, Skill, Common, Self)
    addVar("Stars", 1);
    addVar("StarNextTurnPower", 3);
  }
  Task<> onPlay(CardPlay&) override {
    co_await cmd::gainStars(*combat, val("Stars").toInt());
    co_await applyPower<StarNextTurnPower>(me(), val("StarNextTurnPower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("StarNextTurnPower", 1); }
};

// KnowThyPlace.cs: 0 cost, Skill, AnyEnemy, Exhaust. Weak 1, Vulnerable 1; upgrading removes
// Exhaust instead of scaling a number.
struct KnowThyPlace : IroncladT<KnowThyPlace> {
  CARD_HEADER(KnowThyPlace, "KNOW_THY_PLACE", 0, Skill, Common, AnyEnemy)
    keywords = kwExhaust;
    addVar("WeakPower", 1);
    addVar("VulnerablePower", 1);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await applyPower<WeakPower>(p.target, val("WeakPower"), me(), this);
    co_await applyPower<VulnerablePower>(p.target, val("VulnerablePower"), me(), this);
  }
  void onUpgrade() override { removeKeyword(kwExhaust); }
};

// Patter.cs: 1 cost, Skill, Self. Block 8, VigorPower 2 (this file).
struct Patter : IroncladT<Patter> {
  CARD_HEADER(Patter, "PATTER", 1, Skill, Common, Self)
    addVar("Block", 8);
    addVar("VigorPower", 2);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    co_await applyPower<VigorPower>(me(), val("VigorPower"), me(), this);
  }
  void onUpgrade() override {
    upgradeVar("Block", 2);
    upgradeVar("VigorPower", 1);
  }
};

// PhotonCut.cs: 1 cost, Attack, AnyEnemy. Damage 10, draw 1, then put 1 card from hand back on
// top of the draw pile.
struct PhotonCut : IroncladT<PhotonCut> {
  CARD_HEADER(PhotonCut, "PHOTON_CUT", 1, Attack, Common, AnyEnemy)
    addVar("Damage", 10);
    addVar("Cards", 1);
    addVar("PutBack", 1);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await drawCards(val("Cards"));
    int n = val("PutBack").toInt();
    auto picked = co_await cmd::selectCards(*combat, "PHOTON_CUT", combat->hand, n, n);
    for (Card* c : picked) co_await cmd::moveCard(*combat, c, Pile::Draw, true);
  }
  void onUpgrade() override {
    upgradeVar("Damage", 3);
    upgradeVar("Cards", 1);
  }
};

// RefineBlade.cs: 1 cost, Skill, Self. Forge 8, then 1 extra energy next turn
// (EnergyNextTurnPower, this file).
struct RefineBlade : IroncladT<RefineBlade> {
  CARD_HEADER(RefineBlade, "REFINE_BLADE", 1, Skill, Common, Self)
    addVar("Forge", 8);
    addVar("Energy", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await cmd::forge(*combat, val("Forge"), this);
    co_await applyPower<EnergyNextTurnPower>(me(), val("Energy"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Forge", 4); }
};

// SolarStrike.cs: 1 cost, Attack, AnyEnemy, tag Strike. Damage 9, gain 1 star.
struct SolarStrike : IroncladT<SolarStrike> {
  CARD_HEADER(SolarStrike, "SOLAR_STRIKE", 1, Attack, Common, AnyEnemy)
    tags = tagStrike;
    addVar("Damage", 9);
    addVar("Stars", 1);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await cmd::gainStars(*combat, val("Stars").toInt());
  }
  void onUpgrade() override {
    upgradeVar("Damage", 1);
    upgradeVar("Stars", 1);
  }
};

// SpoilsOfBattle.cs: 1 cost, Skill, Self. Forge 6, draw 2.
struct SpoilsOfBattle : IroncladT<SpoilsOfBattle> {
  CARD_HEADER(SpoilsOfBattle, "SPOILS_OF_BATTLE", 1, Skill, Common, Self)
    addVar("Forge", 6);
    addVar("Cards", 2);
  }
  Task<> onPlay(CardPlay&) override {
    co_await cmd::forge(*combat, val("Forge"), this);
    co_await drawCards(val("Cards"));
  }
  void onUpgrade() override { upgradeVar("Forge", 3); }
};

// WroughtInWar.cs: 1 cost, Attack, AnyEnemy. Damage 7, Forge 7.
struct WroughtInWar : IroncladT<WroughtInWar> {
  CARD_HEADER(WroughtInWar, "WROUGHT_IN_WAR", 1, Attack, Common, AnyEnemy)
    addVar("Damage", 7);
    addVar("Forge", 7);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await cmd::forge(*combat, val("Forge"), this);
  }
  void onUpgrade() override {
    upgradeVar("Damage", 2);
    upgradeVar("Forge", 2);
  }
};

}  // namespace

void registerRegentCards() {
  registerPowerType<CrushUnderPower>();
  registerPowerType<BlockNextTurnPower>();
  registerPowerType<EnergyNextTurnPower>();
  registerPowerType<VigorPower>();

  registerCardType<MinionStrike>();
  registerCardType<Debris>();

  registerCardType<AstralPulse>();
  registerCardType<Begone>();
  registerCardType<CelestialMight>();
  registerCardType<CloakOfStars>();
  registerCardType<CollisionCourse>();
  registerCardType<CosmicIndifference>();
  registerCardType<CrescentSpear>();
  registerCardType<CrushUnder>();
  registerCardType<GatherLight>();
  registerCardType<Glitterstream>();
  registerCardType<Glow>();
  registerCardType<GuidingStar>();
  registerCardType<HiddenCache>();
  registerCardType<KnowThyPlace>();
  registerCardType<Patter>();
  registerCardType<PhotonCut>();
  registerCardType<RefineBlade>();
  registerCardType<SolarStrike>();
  registerCardType<SpoilsOfBattle>();
  registerCardType<WroughtInWar>();
}

}  // namespace sts
