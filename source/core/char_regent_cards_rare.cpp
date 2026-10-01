// The Regent's rare cards (X3.4), translated from Models.Cards\<Name>.cs in RegentCardPool
// order: Arsenal .. VoidForm, plus the token MinionSacrifice (Guards). Registered from
// registerRegent() (char_regent.cpp) via registerRegentRareCards(). Same style as
// char_regent_cards_uncommon1.cpp.
#include "cards.h"
#include "char_regent.h"
#include "colorless.h"

namespace sts {

namespace {

// ================================================================ powers used by one card here

// ArsenalPower.cs: gain Amount Strength whenever the owner generates a card for combat.
struct ArsenalPower : Power {
  POWER_HEADER(ArsenalPower, "ARSENAL_POWER")
  // cmd::addGeneratedCard fires afterCardEnteredCombat only for generated cards, matching
  // Hook.AfterCardGeneratedForCombat (see Regalite in char_regent_relics.cpp).
  Task<> afterCardEnteredCombat(Card* card) override {
    if (!card->createdByPlayer) co_return;  // creator == Owner
    flash = 1.f;
    co_await applyPower<StrengthPower>(owner, amount, owner, nullptr);
  }
};

// DyingStarPower.cs: TemporaryStrengthPower with IsPositive == false (the negative-Strength
// mirror of SetupStrikePower, same as ManglePower in powers_ironclad.h).
struct DyingStarPower : Power {
  POWER_HEADER(DyingStarPower, "DYING_STAR_POWER")
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

// MonarchsGazeStrengthDownPower.cs: same temporary negative Strength, origin Monarch's Gaze.
struct MonarchsGazeStrengthDownPower : Power {
  POWER_HEADER(MonarchsGazeStrengthDownPower, "MONARCHS_GAZE_STRENGTH_DOWN_POWER")
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

// ForegoneConclusionPower.cs: before the next hand draw, put Amount cards of your choice from
// the draw pile into the hand, then remove itself.
struct ForegoneConclusionPower : Power {
  POWER_HEADER(ForegoneConclusionPower, "FOREGONE_CONCLUSION_POWER")
  Task<> beforeHandDraw() override {
    Combat& c = *owner->combat;
    // CardPileCmd.ShuffleIfNecessary
    if (c.draw.empty() && !c.discard.empty()) co_await cmd::shuffle(c);
    int n = std::min<int>(amount, (int)c.draw.size());
    auto picked = co_await cmd::selectCards(c, "FOREGONE_CONCLUSION", c.draw, n, n);
    for (Card* k : picked) co_await cmd::moveCard(c, k, Pile::Hand);
    co_await cmd::removePower(this);
  }
};

// GenesisPower.cs: gain Amount stars at the start of every turn (AfterEnergyReset).
struct GenesisPower : Power {
  POWER_HEADER(GenesisPower, "GENESIS_POWER")
  Task<> afterEnergyReset() override {
    flash = 1.f;
    co_await cmd::gainStars(*owner->combat, amount);
  }
};

// MonarchsGazePower.cs: whenever the owner deals powered attack damage to a creature, that
// creature gets Amount temporary Strength loss.
struct MonarchsGazePower : Power {
  POWER_HEADER(MonarchsGazePower, "MONARCHS_GAZE_POWER")
  Task<> afterDamageGiven(Creature* dealer, const DamageResult&, int props, Creature* target, Card*) override {
    if (dealer == owner && isPoweredAttack(props))
      co_await applyPower<MonarchsGazeStrengthDownPower>(target, amount, owner, nullptr);
  }
};

// RoyaltiesPower.cs: after combat, an extra GoldReward of Amount.
// PORT NOTE: AfterCombatEnd + CombatRoom.AddExtraReward is Combat::extraRewardGold here
// (a one-field hook read by Run::combatRewards).
struct RoyaltiesPower : Power {
  POWER_HEADER(RoyaltiesPower, "ROYALTIES_POWER")
  Task<> afterPowerAmountChanged(Power* p, Dec amt, Creature*, Card*) override {
    if (p == this && owner->combat) owner->combat->extraRewardGold += amt.toInt();
    co_return;
  }
};

// SwordSagePower.cs: every Sovereign Blade replays Amount extra times (BaseReplayCount).
// PORT NOTE: IsClone (CardModel.CreateClone results, which already carry the replay count) is
// approximated by Card::isDupe.
struct SwordSagePower : Power {
  POWER_HEADER(SwordSagePower, "SWORD_SAGE_POWER")
  static void tryAddReplays(Card* card, int n) {
    if (card->id == "SovereignBlade") card->baseReplayCount += n;
  }
  Task<> afterPowerAmountChanged(Power* p, Dec amt, Creature*, Card*) override {
    if (p != this) co_return;
    for (Card* k : owner->combat->allCards()) tryAddReplays(k, amt.toInt());
  }
  Task<> afterCardEnteredCombat(Card* card) override {
    if (!card->isDupe) tryAddReplays(card, amount);
    co_return;
  }
  Task<> afterRemoved(Creature* oldOwner) override {
    if (!oldOwner->combat) co_return;
    for (Card* k : oldOwner->combat->allCards()) tryAddReplays(k, -amount);
  }
};

// TyrannyPower.cs: draw Amount more cards each turn, then exhaust Amount cards of your choice
// from the hand at the start of the turn.
struct TyrannyPower : Power {
  POWER_HEADER(TyrannyPower, "TYRANNY_POWER")
  Dec modifyHandDraw(Dec count) override { return count + Dec(amount); }
  Task<> afterPlayerTurnStart() override {
    Combat& c = *owner->combat;
    int n = std::min<int>(amount, (int)c.hand.size());
    auto picked = co_await cmd::selectCards(c, "TYRANNY", c.hand, n, n);
    for (Card* k : picked) co_await cmd::exhaustCard(c, k);
  }
};

// VoidFormPower.cs: the first Amount cards played each turn cost 0 energy and 0 stars.
// Data.cardsPlayedThisTurn is a plain member.
// PORT NOTE: the C# maxes cardsPlayedThisTurn out on apply / amount change (HideTemporaryZero-
// CostVisual) because VoidForm ends the turn; done in afterPowerAmountChanged, which also fires
// for the first application.
struct VoidFormPower : Power {
  POWER_HEADER(VoidFormPower, "VOID_FORM_POWER")
  int cardsPlayedThisTurn = 0;
  bool shouldSkip(Card* card) {
    if (ownerOf(card) != owner) return true;
    Pile p = card->combat->pileOf(card);
    if (p != Pile::Hand && p != Pile::Play) return true;
    return cardsPlayedThisTurn >= amount;
  }
  Task<> beforeApplied(Creature*, Dec, Creature*, Card*) override {
    cardsPlayedThisTurn = 999999999;
    co_return;
  }
  Task<> afterPowerAmountChanged(Power* p, Dec, Creature*, Card*) override {
    if (p == this) cardsPlayedThisTurn = 999999999;
    co_return;
  }
  int modifyEnergyCostLate(Card* card, int cost) override { return shouldSkip(card) ? cost : 0; }
  int modifyStarCost(Card* card, int cost) override { return shouldSkip(card) ? cost : 0; }
  Task<> afterCardPlayed(const CardPlay& cp) override {
    if (ownerOf(cp.card) == owner && !cp.autoPlay && cp.playIndex == cp.playCount - 1) ++cardsPlayedThisTurn;
    co_return;
  }
  Task<> beforeSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) cardsPlayedThisTurn = 0;
    co_return;
  }
};

// ================================================================ token cards

// MinionSacrifice.cs: created by Guards. 0 cost, Skill, Token, Exhaust, tag Minion. Block 7.
struct MinionSacrifice : IroncladT<MinionSacrifice> {
  CARD_HEADER(MinionSacrifice, "MINION_SACRIFICE", 0, Skill, Token, Self)
    keywords = kwExhaust;
    tags = tagMinion;
    addVar("Block", 7);
  }
  bool gainsBlock() const override { return true; }
  Task<> onPlay(CardPlay&) override { co_await block(val("Block")); }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

// ================================================================ rare cards

// Arsenal.cs: 1 cost, Power, Self. ArsenalPower 1. Upgrade: Innate.
struct Arsenal : IroncladT<Arsenal> {
  CARD_HEADER(Arsenal, "ARSENAL", 1, Power, Rare, Self)
    addVar("ArsenalPower", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<ArsenalPower>(me(), val("ArsenalPower"), me(), this);
  }
  void onUpgrade() override { keywords |= kwInnate; }
};

// BeatIntoShape.cs: 1 cost, Attack, AnyEnemy. Damage 5, then Forge CalculationBase 5 +
// CalculationExtra 5 per powered attack damage the player dealt this turn to the target,
// not counting the hit(s) this card just made.
// PORT NOTE: the multiplier needs the target (Card::calcMultiplier has none); before the card is
// played (card text preview) it has none and counts 0.
struct BeatIntoShape : IroncladT<BeatIntoShape> {
  CARD_HEADER(BeatIntoShape, "BEAT_INTO_SHAPE", 1, Attack, Rare, AnyEnemy)
    addVar("Damage", 5);
    addVar("CalculationBase", 5);
    addVar("CalculationExtra", 5);
    addVar("CalculatedForge", 0);
    calcMultiplier = [](Card* c) { return static_cast<BeatIntoShape*>(c)->hitsOn(static_cast<BeatIntoShape*>(c)->lastTarget); };
  }
  Creature* lastTarget = nullptr;
  int hitsOn(Creature* target) const {
    if (!target || !combat) return 0;
    return combat->history.countThisTurn(*combat, CombatHistoryEntry::DamageReceived, [&](const CombatHistoryEntry& e) {
      return e.actor == target && e.other == combat->player && isPoweredAttack(e.props);
    });
  }
  Task<> onPlay(CardPlay& p) override {
    lastTarget = p.target;
    cmd::Attack a;
    a.damagePerHit = val("Damage");
    a.attacker = me();
    a.source = this;
    a.single = p.target;
    co_await a.execute(*combat);
    // CalculatedVar.Calculate(target) - Results.Count * CalculationExtra
    Dec amount = calculatedBlock() - Dec((int)a.results.size()) * val("CalculationExtra");
    co_await cmd::forge(*combat, amount, this);
  }
  void onUpgrade() override {
    upgradeVar("Damage", 2);
    upgradeVar("CalculationBase", 2);
    upgradeVar("CalculationExtra", 2);
  }
};

// BigBang.cs: 0 cost, Skill, Self, Exhaust. Draw 1, gain 1 star, gain 1 energy, Forge 5.
// Upgrade: Innate.
struct BigBang : IroncladT<BigBang> {
  CARD_HEADER(BigBang, "BIG_BANG", 0, Skill, Rare, Self)
    keywords = kwExhaust;
    addVar("Cards", 1);
    addVar("Energy", 1);
    addVar("Stars", 1);
    addVar("Forge", 5);
  }
  Task<> onPlay(CardPlay&) override {
    co_await drawCards(val("Cards"));
    co_await cmd::gainStars(*combat, val("Stars").toInt());
    co_await cmd::gainEnergy(*combat, val("Energy").toInt());
    co_await cmd::forge(*combat, val("Forge"), this);
  }
  void onUpgrade() override { keywords |= kwInnate; }
};

// Bombardment.cs: 3 cost, Attack, AnyEnemy, Exhaust. Damage 18. Whenever a turn starts with this
// in the Exhaust pile, it is played for free (AfterAutoPrePlayPhaseEnteredEarly).
// PORT NOTE: the C# uses the "Early" hook so another auto-pre-play effect (Mayhem) that also
// exhausts it can't double-trigger; this engine has only afterAutoPrePlayPhaseEntered.
struct Bombardment : IroncladT<Bombardment> {
  CARD_HEADER(Bombardment, "BOMBARDMENT", 3, Attack, Rare, AnyEnemy)
    keywords = kwExhaust;
    addVar("Damage", 18);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
  Task<> afterAutoPrePlayPhaseEntered() override {
    if (combat->pileOf(this) == Pile::Exhaust) co_await cmd::autoPlay(*combat, this, nullptr);
  }
  void onUpgrade() override { upgradeVar("Damage", 6); }
};

// BundleOfJoy.cs: 1 cost, Skill, Self, Exhaust. Add 3 distinct random colorless cards to the
// hand.
struct BundleOfJoy : IroncladT<BundleOfJoy> {
  CARD_HEADER(BundleOfJoy, "BUNDLE_OF_JOY", 1, Skill, Rare, Self)
    keywords = kwExhaust;
    addVar("Cards", 3);
  }
  Task<> onPlay(CardPlay&) override {
    for (auto& k : colorlessDistinctForCombat(*combat, val("Cards").toInt()))
      co_await cmd::addGeneratedCard(*combat, std::move(k), Pile::Hand);
  }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

// Comet.cs: 0 cost, 5 stars, Attack, AnyEnemy. Damage 33, then Weak 3 and Vulnerable 3.
struct Comet : IroncladT<Comet> {
  CARD_HEADER(Comet, "COMET", 0, Attack, Rare, AnyEnemy)
    starCost = 5;
    addVar("Damage", 33);
    addVar("VulnerablePower", 3);
    addVar("WeakPower", 3);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<WeakPower>(p.target, val("WeakPower"), me(), this);
    co_await applyPower<VulnerablePower>(p.target, val("VulnerablePower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 11); }
};

// CrashLanding.cs: 1 cost, Attack, AllEnemies. Damage 21, then fill the hand with Debris.
struct CrashLanding : IroncladT<CrashLanding> {
  CARD_HEADER(CrashLanding, "CRASH_LANDING", 1, Attack, Rare, AllEnemies)
    addVar("Damage", 21);
  }
  Task<> onPlay(CardPlay&) override {
    co_await attackAll(val("Damage"));
    int n = 10 - (int)combat->hand.size();  // CardPile.MaxCardsInHand
    for (int i = 0; i < n; ++i) co_await cmd::addGeneratedCard(*combat, db::card("Debris"), Pile::Hand);
  }
  void onUpgrade() override { upgradeVar("Damage", 5); }
};

// DecisionsDecisions.cs: 0 cost, 6 stars, Skill, Self, Exhaust. Draw 3, pick a playable Skill
// from the hand and auto-play it 3 times.
struct DecisionsDecisions : IroncladT<DecisionsDecisions> {
  CARD_HEADER(DecisionsDecisions, "DECISIONS_DECISIONS", 0, Skill, Rare, Self)
    starCost = 6;
    keywords = kwExhaust;
    addVar("Cards", 3);
    addVar("Repeat", 3);
  }
  Task<> onPlay(CardPlay&) override {
    co_await drawCards(val("Cards"));
    std::vector<Card*> options;
    for (Card* k : combat->hand) if (k->type == CardType::Skill && !k->has(kwUnplayable)) options.push_back(k);
    auto picked = co_await cmd::selectCards(*combat, "DECISIONS_DECISIONS", options, 1, 1);
    if (!picked.empty()) {
      Card* card = picked[0];
      int n = val("Repeat").toInt();
      for (int i = 0; i < n; ++i) co_await cmd::autoPlay(*combat, card, nullptr);
    }
  }
  void onUpgrade() override { upgradeVar("Cards", 2); }
};

// DyingStar.cs: 1 cost, 3 stars, Attack, AllEnemies, Ethereal. Damage 9 to all, then every enemy
// that was hittable before the attack gets DyingStarPower 9 (temporary Strength loss).
struct DyingStar : IroncladT<DyingStar> {
  CARD_HEADER(DyingStar, "DYING_STAR", 1, Attack, Rare, AllEnemies)
    starCost = 3;
    keywords = kwEthereal;
    addVar("Damage", 9);
    addVar("StrengthLoss", 9);
  }
  Task<> onPlay(CardPlay&) override {
    std::vector<Creature*> enemies = combat->hittableEnemies();
    co_await attackAll(val("Damage"));
    for (Creature* e : enemies) {
      if (e->dead()) continue;
      co_await applyPower<DyingStarPower>(e, val("StrengthLoss"), me(), this);
    }
  }
  void onUpgrade() override {
    upgradeVar("Damage", 2);
    upgradeVar("StrengthLoss", 2);
  }
};

// ForegoneConclusion.cs: 1 cost, Skill, Self. ForegoneConclusionPower 2 (Cards).
struct ForegoneConclusion : IroncladT<ForegoneConclusion> {
  CARD_HEADER(ForegoneConclusion, "FOREGONE_CONCLUSION", 1, Skill, Rare, Self)
    addVar("Cards", 2);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<ForegoneConclusionPower>(me(), val("Cards"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

// Genesis.cs: 2 cost, Power, Self. GenesisPower 2 (StarsPerTurn).
struct Genesis : IroncladT<Genesis> {
  CARD_HEADER(Genesis, "GENESIS", 2, Power, Rare, Self)
    addVar("StarsPerTurn", 2);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<GenesisPower>(me(), val("StarsPerTurn"), me(), this);
  }
  void onUpgrade() override { upgradeVar("StarsPerTurn", 1); }
};

// Guards.cs: 2 cost, Skill, Self, Exhaust. Pick any number of cards from the hand and
// transform each into MinionSacrifice (upgraded if Guards is). No upgrade effect of its own.
struct Guards : IroncladT<Guards> {
  CARD_HEADER(Guards, "GUARDS", 2, Skill, Rare, Self)
    keywords = kwExhaust;
  }
  Task<> onPlay(CardPlay&) override {
    auto picked = co_await cmd::selectCards(*combat, "GUARDS", combat->hand, 0, (int)combat->hand.size());
    for (Card* k : picked) {
      auto sacrifice = db::card("MinionSacrifice");
      if (upgraded()) sacrifice->upgrade();
      co_await cmd::transform(*combat, k, std::move(sacrifice));
    }
  }
};

// HeavenlyDrill.cs: X cost, Attack, AnyEnemy. Damage 8, X hits; X doubles when X >= 4 (Energy).
struct HeavenlyDrill : IroncladT<HeavenlyDrill> {
  CARD_HEADER(HeavenlyDrill, "HEAVENLY_DRILL", 0, Attack, Rare, AnyEnemy)
    costsX = true;
    addVar("Damage", 8);
    addVar("Energy", 4);
  }
  Task<> onPlay(CardPlay& p) override {
    int n = xValue;
    if (n >= val("Energy").toInt()) n *= 2;
    co_await attack(p.target, val("Damage"), n);
  }
  void onUpgrade() override { upgradeVar("Damage", 2); }
};

// HeirloomHammer.cs: 2 cost, Attack, AnyEnemy. Damage 20, then pick a colorless card in the hand
// and add Repeat (1) copies of it to the hand.
struct HeirloomHammer : IroncladT<HeirloomHammer> {
  CARD_HEADER(HeirloomHammer, "HEIRLOOM_HAMMER", 2, Attack, Rare, AnyEnemy)
    addVar("Damage", 20);
    addVar("Repeat", 1);
  }
  static bool isColorless(Card* k) { return db::isColorless(k->id); }  // VisualCardPool.IsColorless
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    std::vector<Card*> options;
    for (Card* k : combat->hand) if (isColorless(k)) options.push_back(k);
    auto picked = co_await cmd::selectCards(*combat, "HEIRLOOM_HAMMER", options, 1, 1);
    if (!picked.empty()) {
      int n = val("Repeat").toInt();
      for (int i = 0; i < n; ++i) co_await cmd::addGeneratedCard(*combat, picked[0]->clone(), Pile::Hand);
    }
  }
  void onUpgrade() override { upgradeVar("Damage", 5); }
};

// IAmInvincible.cs: 1 cost, Skill, Self. Block 10. If it is on top of the draw pile when the
// player's turn is about to end, it is played automatically.
struct IAmInvincible : IroncladT<IAmInvincible> {
  CARD_HEADER(IAmInvincible, "I_AM_INVINCIBLE", 1, Skill, Rare, Self)
    addVar("Block", 10);
  }
  bool gainsBlock() const override { return true; }
  Task<> onPlay(CardPlay&) override { co_await block(val("Block")); }
  Task<> afterAutoPostPlayPhaseEntered() override {
    if (!combat->draw.empty() && combat->draw.front() == this) co_await cmd::autoPlayFromDrawPile(*combat, 1, false);
  }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

// MakeItSo.cs: 0 cost, Attack, AnyEnemy. Damage 6. Whenever a Skill finishes playing and this
// card is not in the hand, every 3rd Skill finished this turn returns it to the hand.
// This engine bumps Combat::skillsFinishedThisTurn after Hook.AfterCardPlayedLate, so the
// finishing play is added.
struct MakeItSo : IroncladT<MakeItSo> {
  CARD_HEADER(MakeItSo, "MAKE_IT_SO", 0, Attack, Rare, AnyEnemy)
    addVar("Damage", 6);
    addVar("Cards", 3);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
  Task<> afterCardPlayedLate(const CardPlay& cp) override {
    if (cp.card->type != CardType::Skill || combat->pileOf(this) == Pile::Hand) co_return;
    if (combat->pileOf(this) == Pile::None || combat->pileOf(this) == Pile::Play) co_return;
    int n = combat->skillsFinishedThisTurn + 1;
    if (n % val("Cards").toInt() == 0) co_await cmd::moveCard(*combat, this, Pile::Hand);
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

// MonarchsGaze.cs: 2 cost, Power, Self. MonarchsGazePower 1 (StrengthLoss). Upgrade: cost 1.
struct MonarchsGaze : IroncladT<MonarchsGaze> {
  CARD_HEADER(MonarchsGaze, "MONARCHS_GAZE", 2, Power, Rare, Self)
    addVar("StrengthLoss", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<MonarchsGazePower>(me(), val("StrengthLoss"), me(), this);
  }
  void onUpgrade() override { cost -= 1; }
};

// NeutronAegis.cs: 1 cost, 5 stars, Power, Self. Plating 8.
struct NeutronAegis : IroncladT<NeutronAegis> {
  CARD_HEADER(NeutronAegis, "NEUTRON_AEGIS", 1, Power, Rare, Self)
    starCost = 5;
    addVar("PlatingPower", 8);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<PlatingPower>(me(), val("PlatingPower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("PlatingPower", 3); }
};

// Royalties.cs: 1 cost, Power, Self. RoyaltiesPower 30 (Gold): extra gold after combat.
// (CanBeGeneratedInCombat is false in the C#; this engine has no such flag for cards.)
struct Royalties : IroncladT<Royalties> {
  bool canBeGeneratedInCombat() const override { return false; }
  CARD_HEADER(Royalties, "ROYALTIES", 1, Power, Rare, Self)
    addVar("Gold", 30);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<RoyaltiesPower>(me(), val("Gold"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Gold", 10); }
};

// SeekingEdge.cs: 1 cost, Power, Self. SeekingEdgePower 1, then Forge 7.
struct SeekingEdge : IroncladT<SeekingEdge> {
  CARD_HEADER(SeekingEdge, "SEEKING_EDGE", 1, Power, Rare, Self)
    addVar("Forge", 7);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<SeekingEdgePower>(me(), 1, me(), this);
    co_await cmd::forge(*combat, val("Forge"), this);
  }
  void onUpgrade() override { upgradeVar("Forge", 4); }
};

// SevenStars.cs: 2 cost, 7 stars, Attack, AllEnemies. Damage 7, 7 hits. Upgrade: cost 1.
struct SevenStars : IroncladT<SevenStars> {
  CARD_HEADER(SevenStars, "SEVEN_STARS", 2, Attack, Rare, AllEnemies)
    starCost = 7;
    addVar("Damage", 7);
    addVar("Repeat", 7);
  }
  Task<> onPlay(CardPlay&) override { co_await attackAll(val("Damage"), val("Repeat").toInt()); }
  void onUpgrade() override { cost -= 1; }
};

// SwordSage.cs: 2 cost, Power, Self. SwordSagePower 1. Upgrade: cost 1.
struct SwordSage : IroncladT<SwordSage> {
  CARD_HEADER(SwordSage, "SWORD_SAGE", 2, Power, Rare, Self)
    addVar("SwordSagePower", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<SwordSagePower>(me(), val("SwordSagePower"), me(), this);
  }
  void onUpgrade() override { cost -= 1; }
};

// TheSmith.cs: 1 cost, 4 stars, Skill, Self. Forge 30.
struct TheSmith : IroncladT<TheSmith> {
  CARD_HEADER(TheSmith, "THE_SMITH", 1, Skill, Rare, Self)
    starCost = 4;
    addVar("Forge", 30);
  }
  Task<> onPlay(CardPlay&) override { co_await cmd::forge(*combat, val("Forge"), this); }
  void onUpgrade() override { upgradeVar("Forge", 10); }
};

// Tyranny.cs: 1 cost, Power, Self. TyrannyPower 1. Upgrade: Innate.
struct Tyranny : IroncladT<Tyranny> {
  CARD_HEADER(Tyranny, "TYRANNY", 1, Power, Rare, Self)
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<TyrannyPower>(me(), 1, me(), this); }
  void onUpgrade() override { keywords |= kwInnate; }
};

// VoidForm.cs: 3 cost, Power, Self, Ethereal. VoidFormPower 2, then the turn ends.
// Upgrade removes Ethereal.
struct VoidForm : IroncladT<VoidForm> {
  CARD_HEADER(VoidForm, "VOID_FORM", 3, Power, Rare, Self)
    keywords = kwEthereal;
    addVar("VoidFormPower", 2);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<VoidFormPower>(me(), val("VoidFormPower"), me(), this);
    // PlayerCmd.EndTurn: queue the end-turn action; the player loop reads it once this play ends.
    PlayerAction a;
    a.kind = PlayerAction::EndTurn;
    combat->actions.fire(a);
  }
  void onUpgrade() override { keywords &= ~kwEthereal; }
};

}  // namespace

void registerRegentRareCards() {
  registerPowerType<ArsenalPower>();
  registerPowerType<DyingStarPower>();
  registerPowerType<MonarchsGazeStrengthDownPower>();
  registerPowerType<ForegoneConclusionPower>();
  registerPowerType<GenesisPower>();
  registerPowerType<MonarchsGazePower>();
  registerPowerType<RoyaltiesPower>();
  registerPowerType<SwordSagePower>();
  registerPowerType<TyrannyPower>();
  registerPowerType<VoidFormPower>();

  registerCardType<MinionSacrifice>();

  registerCardType<Arsenal>();
  registerCardType<BeatIntoShape>();
  registerCardType<BigBang>();
  registerCardType<Bombardment>();
  registerCardType<BundleOfJoy>();
  registerCardType<Comet>();
  registerCardType<CrashLanding>();
  registerCardType<DecisionsDecisions>();
  registerCardType<DyingStar>();
  registerCardType<ForegoneConclusion>();
  registerCardType<Genesis>();
  registerCardType<Guards>();
  registerCardType<HeavenlyDrill>();
  registerCardType<HeirloomHammer>();
  registerCardType<IAmInvincible>();
  registerCardType<MakeItSo>();
  registerCardType<MonarchsGaze>();
  registerCardType<NeutronAegis>();
  registerCardType<Royalties>();
  registerCardType<SeekingEdge>();
  registerCardType<SevenStars>();
  registerCardType<SwordSage>();
  registerCardType<TheSmith>();
  registerCardType<Tyranny>();
  registerCardType<VoidForm>();
}

}  // namespace sts
