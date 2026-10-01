// The Defect's Common cards (X2.2): DefectCardPool.cs entries with Rarity Common. Starters
// (StrikeDefect/DefendDefect/Zap/Dualcast) are in char_defect.cpp; Uncommon/Rare are not ported
// here. Orb types and Focus are in char_defect.h.
#include <set>

#include "cards.h"
#include "char_defect.h"

namespace sts {

namespace {

// FocusedStrikePower / HotfixPower (Models.Powers): concrete TemporaryFocusPower buffs, both
// positive (the default isPositive()), that exist only so the amount shown/removed is tagged
// with the right origin card; behaviour is entirely TemporaryFocusPower's (char_defect.h).
struct FocusedStrikePower : TemporaryFocusPower {
  POWER_HEADER(FocusedStrikePower, "FOCUSED_STRIKE_POWER")
  const char* internallyAppliedPower() const override { return "FocusPower"; }  // ITemporaryPower
};

struct HotfixPower : TemporaryFocusPower {
  POWER_HEADER(HotfixPower, "HOTFIX_POWER")
  const char* internallyAppliedPower() const override { return "FocusPower"; }  // ITemporaryPower
};

// LightningRodPower (Models.Powers): a Counter stack; each of the owner's own energy resets
// (i.e. the start of their next turn) channels a Lightning orb and ticks the counter down.
// PORT NOTE: drops the C#'s `player == Owner.Player` multiplayer check (single player: always
// true) -- see this engine's single afterEnergyReset() hook (no Player parameter).
struct LightningRodPower : Power {
  POWER_HEADER(LightningRodPower, "LIGHTNING_ROD_POWER")
  Task<> afterEnergyReset() override {
    co_await cmd::channelOrb(*owner->combat, std::make_unique<LightningOrb>());
    co_await cmd::decrement(this);
  }
};

// EnergyNextTurnPower (ChargeBattery) is the shared one in powers.h.

// Void.cs: an unplayable, Ethereal status card that, once drawn, costs the player Energy on a
// short delay (matching the vfx-timed AfterCardDrawn in the C#).
struct Void : IroncladT<Void> {
  CARD_HEADER(Void, "VOID", -1, Status, Status, None)
    keywords = kwUnplayable | kwEthereal;
    maxUpgradeLevel = 0;
    addVar("Energy", 1);
  }
  Task<> afterCardDrawn(Card* c, bool) override {
    if (c != this) co_return;
    co_await wait(0.25);
    co_await cmd::gainEnergy(*combat, -val("Energy").toInt());
  }
};

// GoForTheEyes.cs: helper to read a monster's current intent (IntendsToAttack).
bool intendsToAttack(Creature* c) {
  if (!c || !c->monster || !c->monster->nextMove) return false;
  for (auto& in : c->monster->nextMove->intents) if (in.kind == Intent::Attack) return true;
  return false;
}

// BallLightning.cs: attack, then channel a Lightning orb.
struct BallLightning : IroncladT<BallLightning> {
  CARD_HEADER(BallLightning, "BALL_LIGHTNING", 1, Attack, Common, AnyEnemy)
    addVar("Damage", 7);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await cmd::channelOrb(*combat, std::make_unique<LightningOrb>());
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

// Barrage.cs: attack once per orb currently queued (0 hits, and so no damage, with an empty
// queue -- matches the C#'s CalculatedVar multiplier).
struct Barrage : IroncladT<Barrage> {
  CARD_HEADER(Barrage, "BARRAGE", 1, Attack, Common, AnyEnemy)
    addVar("Damage", 5);
    addVar("CalculationBase", 0);
    addVar("CalculationExtra", 1);
    addVar("CalculatedHits", 0);
    calcMultiplier = [](Card* c) { return c->combat ? (int)c->combat->orbQueue.size() : 0; };
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage"), (int)combat->orbQueue.size()); }
  void onUpgrade() override { upgradeVar("Damage", 2); }
};

// BeamCell.cs: 0-cost attack + Vulnerable.
struct BeamCell : IroncladT<BeamCell> {
  CARD_HEADER(BeamCell, "BEAM_CELL", 0, Attack, Common, AnyEnemy)
    addVar("Damage", 3);
    addVar("VulnerablePower", 1);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<VulnerablePower>(p.target, val("VulnerablePower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 1); upgradeVar("VulnerablePower", 1); }
};

// BoostAway.cs: block, then a Dazed goes to the discard pile.
struct BoostAway : IroncladT<BoostAway> {
  CARD_HEADER(BoostAway, "BOOST_AWAY", 0, Skill, Common, Self)
    addVar("Block", 6);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    co_await cmd::addStatusCards(*combat, "Dazed", Pile::Discard, 1, true);
  }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

// ChargeBattery.cs: block + EnergyNextTurnPower.
struct ChargeBattery : IroncladT<ChargeBattery> {
  CARD_HEADER(ChargeBattery, "CHARGE_BATTERY", 1, Skill, Common, Self)
    addVar("Block", 7);
    addVar("Energy", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    co_await applyPower<EnergyNextTurnPower>(me(), val("Energy"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

// Claw.cs: attack, then every Claw currently in the fight (including this one) has its Damage
// var permanently raised by the Increase var's base amount (AfterDowngraded adds it back).
struct Claw : IroncladT<Claw> {
  CARD_HEADER(Claw, "CLAW", 0, Attack, Common, AnyEnemy)
    addVar("Damage", 3);
    addVar("Increase", 2);
  }
  Dec extraDamageFromClawPlays = 0;
  void afterDowngraded() override { upgradeVar("Damage", extraDamageFromClawPlays); }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    Dec inc = val("Increase");
    for (Card* c : combat->allCards())
      if (c->id == "Claw") {
        if (auto* v = c->var("Damage")) v->base += inc;
        static_cast<Claw*>(c)->extraDamageFromClawPlays += inc;  // BuffFromClawPlay
      }
  }
  void onUpgrade() override { upgradeVar("Damage", 1); upgradeVar("Increase", 1); }
};

// ColdSnap.cs: attack, then channel a Frost orb.
struct ColdSnap : IroncladT<ColdSnap> {
  CARD_HEADER(ColdSnap, "COLD_SNAP", 1, Attack, Common, AnyEnemy)
    addVar("Damage", 6);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await cmd::channelOrb(*combat, std::make_unique<FrostOrb>());
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

// CompileDriver.cs: attack, then draw one card per *distinct* orb type currently queued.
struct CompileDriver : IroncladT<CompileDriver> {
  CARD_HEADER(CompileDriver, "COMPILE_DRIVER", 1, Attack, Common, AnyEnemy)
    addVar("Damage", 7);
    addVar("CalculationBase", 0);
    addVar("CalculationExtra", 1);
    addVar("CalculatedCards", 0);
    calcMultiplier = [](Card* c) {
      std::set<std::string> distinct;
      if (c->combat) for (auto& o : c->combat->orbQueue) distinct.insert(o->id);
      return (int)distinct.size();
    };
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    std::set<std::string> distinct;
    for (auto& o : combat->orbQueue) distinct.insert(o->id);
    co_await drawCards(Dec((int)distinct.size()));
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

// Coolheaded.cs: channel a Frost orb, then draw.
struct Coolheaded : IroncladT<Coolheaded> {
  CARD_HEADER(Coolheaded, "COOLHEADED", 1, Skill, Common, Self)
    addVar("Cards", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await cmd::channelOrb(*combat, std::make_unique<FrostOrb>());
    co_await drawCards(val("Cards"));
  }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

// FocusedStrike.cs: Strike-tagged attack that also grants temporary Focus for the turn.
struct FocusedStrike : IroncladT<FocusedStrike> {
  CARD_HEADER(FocusedStrike, "FOCUSED_STRIKE", 1, Attack, Common, AnyEnemy)
    tags = tagStrike;
    addVar("Damage", 9);
    addVar("FocusPower", 1);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<FocusedStrikePower>(me(), val("FocusPower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 2); upgradeVar("FocusPower", 1); }
};

// GoForTheEyes.cs: 0-cost attack; Weak only lands if the target currently intends to attack.
struct GoForTheEyes : IroncladT<GoForTheEyes> {
  CARD_HEADER(GoForTheEyes, "GO_FOR_THE_EYES", 0, Attack, Common, AnyEnemy)
    addVar("Damage", 3);
    addVar("WeakPower", 1);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    if (intendsToAttack(p.target)) co_await applyPower<WeakPower>(p.target, val("WeakPower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 1); upgradeVar("WeakPower", 1); }
};

// GunkUp.cs: a multi-hit attack, then a Slimed goes to the discard pile.
struct GunkUp : IroncladT<GunkUp> {
  CARD_HEADER(GunkUp, "GUNK_UP", 1, Attack, Common, AnyEnemy)
    addVar("Damage", 4);
    addVar("Repeat", 3);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"), val("Repeat").toInt());
    co_await cmd::addStatusCards(*combat, "Slimed", Pile::Discard, 1, true);
  }
  void onUpgrade() override { upgradeVar("Damage", 1); }
};

// Hologram.cs: block, then optionally return one card from the discard pile to hand.
struct Hologram : IroncladT<Hologram> {
  CARD_HEADER(Hologram, "HOLOGRAM", 1, Skill, Common, Self)
    keywords = kwExhaust;
    addVar("Block", 3);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    auto picked = co_await cmd::selectCards(*combat, "HOLOGRAM", combat->discard, 0, 1);
    if (!picked.empty()) co_await cmd::moveCard(*combat, picked[0], Pile::Hand);
  }
  void onUpgrade() override { upgradeVar("Block", 2); keywords &= ~kwExhaust; }
};

// Hotfix.cs: 0-cost, Exhausts, grants a larger temporary Focus for the turn.
struct Hotfix : IroncladT<Hotfix> {
  CARD_HEADER(Hotfix, "HOTFIX", 0, Skill, Common, Self)
    keywords = kwExhaust;
    addVar("FocusPower", 2);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<HotfixPower>(me(), val("FocusPower"), me(), this); }
  void onUpgrade() override { keywords &= ~kwExhaust; }
};

// Leap.cs: plain block.
struct Leap : IroncladT<Leap> {
  CARD_HEADER(Leap, "LEAP", 1, Skill, Common, Self)
    addVar("Block", 9);
  }
  Task<> onPlay(CardPlay&) override { co_await block(val("Block")); }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

// LightningRod.cs: block + LightningRodPower (channels a Lightning orb on each of the owner's
// next N energy resets).
struct LightningRod : IroncladT<LightningRod> {
  CARD_HEADER(LightningRod, "LIGHTNING_ROD", 1, Skill, Common, Self)
    addVar("Block", 4);
    addVar("LightningRodPower", 2);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    co_await applyPower<LightningRodPower>(me(), val("LightningRodPower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

// MomentumStrike.cs: Strike-tagged attack that becomes 0-cost for the rest of combat
// (EnergyCost.SetThisCombat(0)).
struct MomentumStrike : IroncladT<MomentumStrike> {
  CARD_HEADER(MomentumStrike, "MOMENTUM_STRIKE", 1, Attack, Common, AnyEnemy)
    tags = tagStrike;
    addVar("Damage", 11);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    setThisCombat(0);
  }
  void onUpgrade() override { upgradeVar("Damage", 4); }
};

// SweepingBeam.cs: hits every enemy, then draws.
struct SweepingBeam : IroncladT<SweepingBeam> {
  CARD_HEADER(SweepingBeam, "SWEEPING_BEAM", 1, Attack, Common, AllEnemies)
    addVar("Damage", 6);
    addVar("Cards", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await attackAll(val("Damage"));
    co_await drawCards(val("Cards"));
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

// Turbo.cs: gain energy, then a Void goes to the discard pile.
struct Turbo : IroncladT<Turbo> {
  CARD_HEADER(Turbo, "TURBO", 0, Skill, Common, Self)
    addVar("Energy", 2);
  }
  Task<> onPlay(CardPlay&) override {
    co_await cmd::gainEnergy(*combat, val("Energy").toInt());
    co_await cmd::addStatusCards(*combat, "Void", Pile::Discard, 1, true);
  }
  void onUpgrade() override { upgradeVar("Energy", 1); }
};

// Uproar.cs: a 2-hit attack, then auto-plays a random Attack card from the draw pile (excluding
// Unplayable ones; if none qualify, any Attack card at all, matching the C#'s fallback query).
struct Uproar : IroncladT<Uproar> {
  CARD_HEADER(Uproar, "UPROAR", 2, Attack, Common, AnyEnemy)
    addVar("Damage", 6);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"), 2);
    std::vector<Card*> items;
    for (Card* c : combat->draw)
      if (c->type == CardType::Attack && !c->has(kwUnplayable)) items.push_back(c);
    if (items.empty())
      for (Card* c : combat->draw)
        if (c->type == CardType::Attack) items.push_back(c);
    if (!items.empty()) co_await cmd::autoPlay(*combat, combat->rng("Shuffle").nextItem(items));
  }
  void onUpgrade() override { upgradeVar("Damage", 2); }
};

}  // namespace

void registerDefectCommonCards() {
  registerPowerType<FocusedStrikePower>();
  registerPowerType<HotfixPower>();
  registerPowerType<LightningRodPower>();
  registerPowerType<EnergyNextTurnPower>();
  registerCardType<Void>();
  registerCardType<BallLightning>();
  registerCardType<Barrage>();
  registerCardType<BeamCell>();
  registerCardType<BoostAway>();
  registerCardType<ChargeBattery>();
  registerCardType<Claw>();
  registerCardType<ColdSnap>();
  registerCardType<CompileDriver>();
  registerCardType<Coolheaded>();
  registerCardType<FocusedStrike>();
  registerCardType<GoForTheEyes>();
  registerCardType<GunkUp>();
  registerCardType<Hologram>();
  registerCardType<Hotfix>();
  registerCardType<Leap>();
  registerCardType<LightningRod>();
  registerCardType<MomentumStrike>();
  registerCardType<SweepingBeam>();
  registerCardType<Turbo>();
  registerCardType<Uproar>();
}

}  // namespace sts
