// The Regent's uncommon cards, first half (X3.3a), translated from Models.Cards\<Name>.cs in
// RegentCardPool order: Alignment .. Monologue. Registered from registerRegent()
// (char_regent.cpp) via registerRegentUncommonCards1(). Same style as char_regent_cards.cpp.
#include "cards.h"
#include "char_regent.h"
#include "colorless.h"

namespace sts {

namespace {

// ================================================================ powers used by one card here

// BlackHolePower.cs: whenever the owner gains stars, or after a card that spent stars finished
// its last play, deals Amount unpowered damage to every hittable enemy.
struct BlackHolePower : Power {
  POWER_HEADER(BlackHolePower, "BLACK_HOLE_POWER")
  // Done in AfterCardPlayed rather than AfterStarsSpent: stars are spent at the start of the
  // card play, but Black Hole triggers after the card is played.
  Task<> afterCardPlayed(const CardPlay& cp) override {
    if (cp.card->lastStarsSpent > 0 && ownerOf(cp.card) == owner && cp.playIndex == cp.playCount - 1)
      co_await dealDamageToAllEnemies();
  }
  Task<> afterStarsGained(int n) override {
    if (n > 0) co_await dealDamageToAllEnemies();
  }
  Task<> dealDamageToAllEnemies() {
    flash = 1.f;
    co_await cmd::damage(owner->combat->hittableEnemies(), Dec(amount), kUnpowered, owner, nullptr);
  }
};

// ChildOfTheStarsPower.cs: gain Amount * (stars spent) unpowered block whenever stars are spent.
struct ChildOfTheStarsPower : Power {
  POWER_HEADER(ChildOfTheStarsPower, "CHILD_OF_THE_STARS_POWER")
  Task<> afterStarsSpent(int n) override {
    if (n > 0) {
      flash = 1.f;
      co_await cmd::gainBlock(owner, Dec(amount * n), kUnpowered, nullptr);
    }
  }
};

// ConquerorPower.cs (debuff): Sovereign Blade's powered attacks deal double damage to the
// owner; ticks down at the end of the owner's side turn.
struct ConquerorPower : Power {
  POWER_HEADER(ConquerorPower, "CONQUEROR_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  Dec modifyDamageMultiplicative(Creature* target, Dec, int props, Creature*, Card* src) override {
    if (!src || src->id != "SovereignBlade") return 1;
    if (!isPoweredAttack(props)) return 1;
    if (target != owner) return 1;
    return 2;
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await cmd::decrement(this);
  }
};

// FurnacePower.cs: Forge Amount at the start of the owner's side turn.
struct FurnacePower : Power {
  POWER_HEADER(FurnacePower, "FURNACE_POWER")
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await cmd::forge(*owner->combat, Dec(amount), this);
  }
};

// MonologuePower.cs: Instanced (one per Monologue played). Every card played after it was applied
// grants `strength` Strength (PowerVar, set by the card) for the rest of the turn (removed, and the
// Strength taken back, at the end of the turn). The HUD shows StrengthApplied, no number while 0.
struct MonologuePower : Power {
  POWER_HEADER(MonologuePower, "MONOLOGUE_POWER")
  PowerInstanceType instanceType() const override { return PowerInstanceType::Instanced; }
  StackType stackType() const override { return strengthApplied != 0 ? StackType::Counter : StackType::Single; }
  int displayAmount() const override { return strengthApplied; }
  int strength = 1;  // DynamicVars.Strength (PowerVar<StrengthPower>(1))
  int strengthApplied = 0;
  // Data.amountsForPlayedCards: the Strength each card that started playing after this was applied
  // will grant (Monologue itself began before, so it doesn't trigger it).
  std::vector<std::pair<Card*, int>> amountsForPlayedCards;

  Task<> beforeCardPlayed(const CardPlay& cp) override {
    if (ownerOf(cp.card) != owner) co_return;
    for (auto& e : amountsForPlayedCards) if (e.first == cp.card) co_return;
    amountsForPlayedCards.push_back({cp.card, strength});
  }
  Task<> afterCardPlayed(const CardPlay& cp) override {
    if (ownerOf(cp.card) != owner) co_return;
    for (size_t i = 0; i < amountsForPlayedCards.size(); ++i) {
      if (amountsForPlayedCards[i].first != cp.card) continue;
      int value = amountsForPlayedCards[i].second;
      amountsForPlayedCards.erase(amountsForPlayedCards.begin() + i);
      flash = 1.f;
      co_await applyPower<StrengthPower>(owner, value, owner, nullptr, true);
      strengthApplied += strength;
      co_return;
    }
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) {
      Creature* o = owner;
      int applied = strengthApplied;
      co_await cmd::removePower(this);
      co_await applyPower<StrengthPower>(o, -applied, o, nullptr, true);
    }
  }
};

// ================================================================ token cards

// MinionDiveBomb.cs: created by Charge. 0 cost, Attack, Token, Exhaust, tag Minion. Damage 13.
// PORT NOTE: drops the attacker anim / dive-bomb vfx.
struct MinionDiveBomb : IroncladT<MinionDiveBomb> {
  CARD_HEADER(MinionDiveBomb, "MINION_DIVE_BOMB", 0, Attack, Token, AnyEnemy)
    keywords = kwExhaust;
    tags = tagMinion;
    addVar("Damage", 13);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

// ================================================================ uncommon cards

// Alignment.cs: 0 cost, 2 stars, Skill, Self. Gain 2 energy.
struct Alignment : IroncladT<Alignment> {
  CARD_HEADER(Alignment, "ALIGNMENT", 0, Skill, Uncommon, Self)
    starCost = 2;
    addVar("Energy", 2);
  }
  Task<> onPlay(CardPlay&) override { co_await cmd::gainEnergy(*combat, val("Energy").toInt()); }
  void onUpgrade() override { upgradeVar("Energy", 1); }
};

// BlackHole.cs: 1 cost, Power, Self. BlackHolePower 3.
struct BlackHole : IroncladT<BlackHole> {
  CARD_HEADER(BlackHole, "BLACK_HOLE", 1, Power, Uncommon, Self)
    addVar("BlackHolePower", 3);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<BlackHolePower>(me(), val("BlackHolePower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("BlackHolePower", 1); }
};

// Bulwark.cs: 2 cost, Skill, Self. Block 12, then Forge 10.
struct Bulwark : IroncladT<Bulwark> {
  CARD_HEADER(Bulwark, "BULWARK", 2, Skill, Uncommon, Self)
    addVar("Block", 12);
    addVar("Forge", 10);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    co_await cmd::forge(*combat, val("Forge"), this);
  }
  void onUpgrade() override {
    upgradeVar("Block", 3);
    upgradeVar("Forge", 3);
  }
};

// Charge.cs: 1 cost, Skill, Self. Pick 2 cards from the draw pile and transform them into
// MinionDiveBombs (upgraded if Charge is upgraded).
struct Charge : IroncladT<Charge> {
  CARD_HEADER(Charge, "CHARGE", 1, Skill, Uncommon, Self)
    addVar("Cards", 2);
  }
  Task<> onPlay(CardPlay&) override {
    int n = std::min<int>(val("Cards").toInt(), (int)combat->draw.size());
    auto picked = co_await cmd::selectCards(*combat, "CHARGE", combat->draw, n, n);
    for (Card* c : picked) {
      Card* bomb = co_await cmd::transform(*combat, c, db::card("MinionDiveBomb"));
      if (upgraded() && bomb) cmd::upgradeCard(bomb);
    }
  }
};

// ChildOfTheStars.cs: 1 cost, Power, Self. ChildOfTheStarsPower 2 (BlockForStars).
struct ChildOfTheStars : IroncladT<ChildOfTheStars> {
  CARD_HEADER(ChildOfTheStars, "CHILD_OF_THE_STARS", 1, Power, Uncommon, Self)
    addVar("BlockForStars", 2);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<ChildOfTheStarsPower>(me(), val("BlockForStars"), me(), this);
  }
  void onUpgrade() override { upgradeVar("BlockForStars", 1); }
};

// Conqueror.cs: 1 cost, Skill, AnyEnemy. Forge 3, then ConquerorPower 1 on the target.
struct Conqueror : IroncladT<Conqueror> {
  CARD_HEADER(Conqueror, "CONQUEROR", 1, Skill, Uncommon, AnyEnemy)
    addVar("Forge", 3);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await cmd::forge(*combat, val("Forge"), this);
    co_await applyPower<ConquerorPower>(p.target, 1, me(), this);
  }
  void onUpgrade() override { upgradeVar("Forge", 2); }
};

// Convergence.cs: 1 cost, Skill, Self. Retain the hand this turn, +1 energy and +1 star next
// turn.
struct Convergence : IroncladT<Convergence> {
  CARD_HEADER(Convergence, "CONVERGENCE", 1, Skill, Uncommon, Self)
    addVar("Energy", 1);
    addVar("Stars", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<RetainHandPower>(me(), 1, me(), this);
    co_await applyPower<EnergyNextTurnPower>(me(), val("Energy"), me(), this);
    co_await applyPower<StarNextTurnPower>(me(), val("Stars"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Stars", 1); }
};

// Devastate.cs: 1 cost, 4 stars, Attack, AnyEnemy. Damage 35.
struct Devastate : IroncladT<Devastate> {
  CARD_HEADER(Devastate, "DEVASTATE", 1, Attack, Uncommon, AnyEnemy)
    starCost = 4;
    addVar("Damage", 35);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
  void onUpgrade() override { upgradeVar("Damage", 10); }
};

// Furnace.cs: 1 cost, Power, Self. FurnacePower 5 (Forge each turn).
struct Furnace : IroncladT<Furnace> {
  CARD_HEADER(Furnace, "FURNACE", 1, Power, Uncommon, Self)
    addVar("Forge", 5);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<FurnacePower>(me(), val("Forge"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Forge", 2); }
};

// GammaBlast.cs: 0 cost, 3 stars, Attack, AnyEnemy. Damage 13, then Weak 2 and Vulnerable 2.
struct GammaBlast : IroncladT<GammaBlast> {
  CARD_HEADER(GammaBlast, "GAMMA_BLAST", 0, Attack, Uncommon, AnyEnemy)
    starCost = 3;
    addVar("Damage", 13);
    addVar("VulnerablePower", 2);
    addVar("WeakPower", 2);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<WeakPower>(p.target, val("WeakPower"), me(), this);
    co_await applyPower<VulnerablePower>(p.target, val("VulnerablePower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 5); }
};

// Glimmer.cs: 1 cost, Skill, Self. Draw 3, then put 1 card from hand on top of the draw pile.
struct Glimmer : IroncladT<Glimmer> {
  CARD_HEADER(Glimmer, "GLIMMER", 1, Skill, Uncommon, Self)
    addVar("Cards", 3);
    addVar("PutBack", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await drawCards(val("Cards"));
    int n = val("PutBack").toInt();
    auto picked = co_await cmd::selectCards(*combat, "GLIMMER", combat->hand, n, n);
    for (Card* c : picked) co_await cmd::moveCard(*combat, c, Pile::Draw, true);
  }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

// Hegemony.cs: 2 cost, Attack, AnyEnemy. Damage 15, then 2 energy next turn.
struct Hegemony : IroncladT<Hegemony> {
  CARD_HEADER(Hegemony, "HEGEMONY", 2, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 15);
    addVar("Energy", 2);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<EnergyNextTurnPower>(me(), val("Energy"), me(), this);
  }
  void onUpgrade() override {
    upgradeVar("Damage", 3);
    upgradeVar("Energy", 1);
  }
};

// KinglyKick.cs: 4 cost, Attack, AnyEnemy. Damage 27; costs 1 less for the rest of the combat
// each time it is drawn.
struct KinglyKick : IroncladT<KinglyKick> {
  CARD_HEADER(KinglyKick, "KINGLY_KICK", 4, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 27);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
  void onUpgrade() override { upgradeVar("Damage", 8); }
  Task<> afterCardDrawn(Card* c, bool) override {
    if (c == this) addThisCombat(-1);
    co_return;
  }
};

// KinglyPunch.cs: 1 cost, Attack, AnyEnemy. Damage 8; gains Increase (4) damage each time it is
// drawn.
struct KinglyPunch : IroncladT<KinglyPunch> {
  CARD_HEADER(KinglyPunch, "KINGLY_PUNCH", 1, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 8);
    addVar("Increase", 4);
  }
  Dec extraDamage = 0;
  void afterDowngraded() override { upgradeVar("Damage", extraDamage); }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
  Task<> afterCardDrawn(Card* c, bool) override {
    if (c != this) co_return;
    Dec inc = val("Increase");
    upgradeVar("Damage", inc);
    extraDamage += inc;
    co_return;
  }
  void onUpgrade() override {
    upgradeVar("Damage", 2);
    upgradeVar("Increase", 2);
  }
};

// KnockoutBlow.cs: 3 cost, Attack, AnyEnemy. Damage 30; gain 5 stars if it killed the target.
struct KnockoutBlow : IroncladT<KnockoutBlow> {
  CARD_HEADER(KnockoutBlow, "KNOCKOUT_BLOW", 3, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 30);
    addVar("Stars", 5);
  }
  Task<> onPlay(CardPlay& p) override {
    cmd::Attack a;
    a.damagePerHit = val("Damage");
    a.attacker = me();
    a.source = this;
    a.single = p.target;
    co_await a.execute(*combat);
    bool killed = false;
    for (auto& hit : a.results) for (auto& r : hit) if (r.killed) killed = true;
    if (killed) co_await cmd::gainStars(*combat, val("Stars").toInt());
  }
  void onUpgrade() override { upgradeVar("Damage", 8); }
};

// LunarBlast.cs: 0 cost, Attack, AnyEnemy. Damage 4, hit once per Skill the player finished
// playing this turn (CalculatedHits = 0 + 1 * skills; Combat::skillsFinishedThisTurn is the
// CardPlaysFinished count, a one-counter hook added to combat.cpp for this card).
struct LunarBlast : IroncladT<LunarBlast> {
  CARD_HEADER(LunarBlast, "LUNAR_BLAST", 0, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 4);
    addVar("CalculationBase", 0);
    addVar("CalculationExtra", 1);
    addVar("CalculatedHits", 0);
    calcMultiplier = [](Card* c) { return c->combat ? c->combat->skillsFinishedThisTurn : 0; };
  }
  Task<> onPlay(CardPlay& p) override {
    // CalculatedVar.Calculate = CalculationBase + CalculationExtra * multiplier
    co_await attack(p.target, val("Damage"), calculatedBlock().toInt());
  }
  void onUpgrade() override { upgradeVar("Damage", 1); }
};

// ManifestAuthority.cs: 1 cost, Skill, Self. Block 7, then add a random colorless card to the
// hand (upgraded if this is): CardFactory.GetDistinctForCombat(ColorlessCardPool, 1, CombatCardGeneration).
struct ManifestAuthority : IroncladT<ManifestAuthority> {
  CARD_HEADER(ManifestAuthority, "MANIFEST_AUTHORITY", 1, Skill, Uncommon, Self)
    addVar("Block", 7);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    auto made = colorlessDistinctForCombat(*combat, 1);
    if (made.empty()) co_return;
    if (upgraded()) cmd::upgradeCard(made[0].get());
    co_await cmd::addGeneratedCard(*combat, std::move(made[0]), Pile::Hand);
  }
  void onUpgrade() override { upgradeVar("Block", 1); }
};

// Monologue.cs: 0 cost, Skill, Self. MonologuePower (Strength 1 per card played this turn).
// Upgrade adds Retain.
struct Monologue : IroncladT<Monologue> {
  CARD_HEADER(Monologue, "MONOLOGUE", 0, Skill, Uncommon, Self)
    addVar("Power", 1);
  }
  Task<> onPlay(CardPlay&) override {
    auto* pw = co_await applyPowerGet<MonologuePower>(me(), 1, me(), this);
    if (pw) pw->strength = val("Power").toInt();  // the new instance's Strength var = the card's Power var
  }
  void onUpgrade() override { keywords |= kwRetain; }
};

}  // namespace

void registerRegentUncommonCards1() {
  registerPowerType<BlackHolePower>();
  registerPowerType<ChildOfTheStarsPower>();
  registerPowerType<ConquerorPower>();
  registerPowerType<FurnacePower>();
  registerPowerType<MonologuePower>();

  registerCardType<MinionDiveBomb>();

  registerCardType<Alignment>();
  registerCardType<BlackHole>();
  registerCardType<Bulwark>();
  registerCardType<Charge>();
  registerCardType<ChildOfTheStars>();
  registerCardType<Conqueror>();
  registerCardType<Convergence>();
  registerCardType<Devastate>();
  registerCardType<Furnace>();
  registerCardType<GammaBlast>();
  registerCardType<Glimmer>();
  registerCardType<Hegemony>();
  registerCardType<KinglyKick>();
  registerCardType<KinglyPunch>();
  registerCardType<KnockoutBlow>();
  registerCardType<LunarBlast>();
  registerCardType<ManifestAuthority>();
  registerCardType<Monologue>();
}

}  // namespace sts
