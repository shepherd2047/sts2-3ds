// Translated uncommon Ironclad cards (see docs/PORTING.md).
#include <algorithm>

#include "cards.h"

namespace sts {

struct AshenStrike : IroncladT<AshenStrike> {
  CARD_HEADER(AshenStrike, "ASHEN_STRIKE", 1, Attack, Uncommon, AnyEnemy)
    tags = tagStrike;
    addVar("CalculationBase", 6);
    addVar("ExtraDamage", 3);
    addVar("CalculatedDamage", 0);
    calcMultiplier = [](Card* c) { return c->combat ? (int)c->combat->exhaust.size() : 0; };
  }
  Task<> onPlay(CardPlay& p) override { co_await attackCalculated(p.target); }
  void onUpgrade() override { upgradeVar("ExtraDamage", 1); }
};

struct BattleTrance : IroncladT<BattleTrance> {
  CARD_HEADER(BattleTrance, "BATTLE_TRANCE", 0, Skill, Uncommon, Self)
    addVar("Cards", 3);
  }
  Task<> onPlay(CardPlay&) override {
    co_await drawCards(val("Cards"));
    co_await applyPower<NoDrawPower>(me(), 1, me(), this);
  }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

// PORT NOTE: C# targets TargetType.AnyAlly and is MultiplayerOnly; this build
// is single-player, so it targets and buffs the caster instead.
struct Blaze : IroncladT<Blaze> {
  CARD_HEADER(Blaze, "BLAZE", 2, Skill, Uncommon, Self)
    addVar("StrengthPower", 5);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<StrengthPower>(me(), val("StrengthPower"), me(), this); }
  void onUpgrade() override { upgradeVar("StrengthPower", 2); }
};

struct Bloodletting : IroncladT<Bloodletting> {
  CARD_HEADER(Bloodletting, "BLOODLETTING", 0, Skill, Uncommon, Self)
    addVar("HpLoss", 3);
    addVar("Energy", 2);
  }
  Task<> onPlay(CardPlay&) override {
    co_await loseHp(val("HpLoss"));
    co_await cmd::gainEnergy(*combat, val("Energy").toInt());
  }
  void onUpgrade() override { upgradeVar("Energy", 1); }
};

struct Bludgeon : IroncladT<Bludgeon> {
  CARD_HEADER(Bludgeon, "BLUDGEON", 3, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 32);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
  void onUpgrade() override { upgradeVar("Damage", 10); }
};

struct Bully : IroncladT<Bully> {
  CARD_HEADER(Bully, "BULLY", 0, Attack, Uncommon, AnyEnemy)
    addVar("CalculationBase", 4);
    addVar("ExtraDamage", 2);
    addVar("CalculatedDamage", 0);
    calcMultiplierT = [](Card*, Creature* t) { return t ? t->powerAmount<VulnerablePower>() : 0; };
  }
  Task<> onPlay(CardPlay& p) override { co_await attackCalculated(p.target); }
  void onUpgrade() override { upgradeVar("ExtraDamage", 1); }
};

struct BurningPact : IroncladT<BurningPact> {
  CARD_HEADER(BurningPact, "BURNING_PACT", 1, Skill, Uncommon, Self)
    addVar("Cards", 2);
  }
  Task<> onPlay(CardPlay&) override {
    auto picked = co_await cmd::selectCards(*combat, "BURNING_PACT", combat->hand, 1, 1);
    if (!picked.empty()) co_await cmd::exhaustCard(*combat, picked[0]);
    co_await drawCards(val("Cards"));
  }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

struct Colossus : IroncladT<Colossus> {
  CARD_HEADER(Colossus, "COLOSSUS", 1, Skill, Uncommon, Self)
    addVar("Block", 4);
    addVar("Colossus", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    co_await applyPower<ColossusPower>(me(), val("Colossus"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

struct Cruelty : IroncladT<Cruelty> {
  CARD_HEADER(Cruelty, "CRUELTY", 1, Power, Uncommon, Self)
    addVar("CrueltyPower", 25);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<CrueltyPower>(me(), val("CrueltyPower"), me(), this); }
  void onUpgrade() override { upgradeVar("CrueltyPower", 25); }
};

// PORT NOTE: C# targets TargetType.AnyAlly and is MultiplayerOnly; this build
// is single-player, so it targets and blocks the caster instead.
struct DemonicShield : IroncladT<DemonicShield> {
  CARD_HEADER(DemonicShield, "DEMONIC_SHIELD", 0, Skill, Uncommon, Self)
    keywords = kwExhaust;
    addVar("CalculationBase", 0);
    addVar("HpLoss", 1);
    addVar("CalculationExtra", 1);
    addVar("CalculatedBlock", 0);
    calcMultiplier = [](Card* c) { return c->combat ? c->combat->player->block : 0; };
  }
  Task<> onPlay(CardPlay&) override {
    co_await loseHp(val("HpLoss"));
    co_await block(calculatedBlock());
  }
  void onUpgrade() override { keywords &= ~kwExhaust; }
};

struct Dismantle : IroncladT<Dismantle> {
  CARD_HEADER(Dismantle, "DISMANTLE", 1, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 8);
  }
  Task<> onPlay(CardPlay& p) override {
    int hits = p.target->get<VulnerablePower>() ? 2 : 1;
    co_await attack(p.target, val("Damage"), hits);
  }
  void onUpgrade() override { upgradeVar("Damage", 2); }
};

struct DrumOfBattle : IroncladT<DrumOfBattle> {
  CARD_HEADER(DrumOfBattle, "DRUM_OF_BATTLE", 1, Skill, Uncommon, Self)
    addVar("Cards", 2);
    addVar("Energy", 2);
  }
  Task<> onPlay(CardPlay&) override { co_await drawCards(val("Cards")); }
  // PORT NOTE: C# repeats this GeneratePlayCount() times for multiplayer
  // card-duplication effects; single player always plays once.
  Task<> afterCardExhausted(Card* card, bool) override {
    if (card != this) co_return;
    co_await cmd::gainEnergy(*combat, val("Energy").toInt());
  }
  void onUpgrade() override { upgradeVar("Energy", 1); }
};

struct EvilEye : IroncladT<EvilEye> {
  CARD_HEADER(EvilEye, "EVIL_EYE", 1, Skill, Uncommon, Self)
    addVar("Block", 8);
  }
  Task<> onPlay(CardPlay&) override {
    // A CardExhausted entry this turn.
    bool exhaustedThisTurn = combat->history.countThisTurn(*combat, CombatHistoryEntry::CardExhausted) > 0;
    int gains = exhaustedThisTurn ? 2 : 1;
    for (int i = 0; i < gains; ++i) co_await block(val("Block"));
  }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

struct ExpectAFight : IroncladT<ExpectAFight> {
  CARD_HEADER(ExpectAFight, "EXPECT_A_FIGHT", 3, Skill, Uncommon, Self)
    addVar("CalculationBase", 15);
    addVar("CalculationExtra", 5);
    addVar("CalculatedBlock", 0);
    calcMultiplier = [](Card* c) { return c->combat ? std::max(0, c->combat->player->powerAmount<StrengthPower>()) : 0; };
  }
  Task<> onPlay(CardPlay&) override { co_await block(calculatedBlock()); }
  void onUpgrade() override { upgradeVar("CalculationBase", 1); upgradeVar("CalculationExtra", 3); }
};

struct FeelNoPain : IroncladT<FeelNoPain> {
  CARD_HEADER(FeelNoPain, "FEEL_NO_PAIN", 1, Power, Uncommon, Self)
    addVar("Power", 3);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<FeelNoPainPower>(me(), val("Power"), me(), this); }
  void onUpgrade() override { upgradeVar("Power", 1); }
};

struct FightMe : IroncladT<FightMe> {
  CARD_HEADER(FightMe, "FIGHT_ME", 2, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 5);
    addVar("Repeat", 2);
    addVar("StrengthPower", 3);
    addVar("EnemyStrength", 1);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"), val("Repeat").toInt());
    co_await applyPower<StrengthPower>(me(), val("StrengthPower"), me(), this);
    co_await applyPower<StrengthPower>(p.target, val("EnemyStrength"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 1); upgradeVar("StrengthPower", 1); }
};

struct FlameBarrier : IroncladT<FlameBarrier> {
  CARD_HEADER(FlameBarrier, "FLAME_BARRIER", 2, Skill, Uncommon, Self)
    addVar("Block", 12);
    addVar("DamageBack", 4);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    co_await applyPower<FlameBarrierPower>(me(), val("DamageBack"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Block", 4); upgradeVar("DamageBack", 2); }
};

struct ForgottenRitual : IroncladT<ForgottenRitual> {
  CARD_HEADER(ForgottenRitual, "FORGOTTEN_RITUAL", 1, Skill, Uncommon, Self)
    keywords = kwExhaust;
    addVar("Energy", 3);
  }
  Task<> onPlay(CardPlay&) override { co_await cmd::gainEnergy(*combat, val("Energy").toInt()); }
  void onUpgrade() override { upgradeVar("Energy", 1); }
};

struct Hemokinesis : IroncladT<Hemokinesis> {
  CARD_HEADER(Hemokinesis, "HEMOKINESIS", 1, Attack, Uncommon, AnyEnemy)
    addVar("HpLoss", 2);
    addVar("Damage", 15);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await loseHp(val("HpLoss"));
    co_await attack(p.target, val("Damage"));
  }
  void onUpgrade() override { upgradeVar("Damage", 5); }
};

// HowlFromBeyond.cs: no Exhaust keyword of its own (the hover tip explains Exhaust): after the
// auto-post-play phase it plays itself again while it sits in the Exhaust pile.
struct HowlFromBeyond : IroncladT<HowlFromBeyond> {
  CARD_HEADER(HowlFromBeyond, "HOWL_FROM_BEYOND", 3, Attack, Uncommon, AllEnemies)
    addVar("Damage", 18);
  }
  Task<> onPlay(CardPlay&) override { co_await attackAll(val("Damage")); }
  Task<> afterAutoPostPlayPhaseEntered() override {
    if (combat->pileOf(this) == Pile::Exhaust) co_await cmd::autoPlay(*combat, this, nullptr);
  }
  void onUpgrade() override { upgradeVar("Damage", 6); }
};

struct InfernalBlade : IroncladT<InfernalBlade> {
  CARD_HEADER(InfernalBlade, "INFERNAL_BLADE", 1, Skill, Uncommon, Self)
    keywords = kwExhaust;
  }
  Task<> onPlay(CardPlay&) override {
    // GetDistinctForCombat(Owner.Character.CardPool.Where(Attack), 1, CombatCardGeneration).FirstOrDefault()
    auto card = oneDistinctForCombat(*combat, [](const Card& c) { return c.type == CardType::Attack; });
    if (card) {
      card->setThisTurnOrUntilPlayed(0);
      co_await cmd::addGeneratedCard(*combat, std::move(card), Pile::Hand);
    }
  }
  void onUpgrade() override { cost -= 1; }
};

struct Inferno : IroncladT<Inferno> {
  CARD_HEADER(Inferno, "INFERNO", 1, Power, Uncommon, Self)
    addVar("InfernoPower", 6);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<InfernoPower>(me(), val("InfernoPower"), me(), this);
    if (auto* p = me()->get<InfernoPower>()) p->incrementSelfDamage();
  }
  void onUpgrade() override { upgradeVar("InfernoPower", 3); }
};

struct Inflame : IroncladT<Inflame> {
  CARD_HEADER(Inflame, "INFLAME", 1, Power, Uncommon, Self)
    addVar("StrengthPower", 2);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<StrengthPower>(me(), val("StrengthPower"), me(), this); }
  void onUpgrade() override { upgradeVar("StrengthPower", 1); }
};

struct Juggling : IroncladT<Juggling> {
  CARD_HEADER(Juggling, "JUGGLING", 1, Power, Uncommon, Self)
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<JugglingPower>(me(), 1, me(), this); }
  void onUpgrade() override { keywords |= kwInnate; }
};

// PORT NOTE: C# also clones this card into every (multiplayer) teammate's
// discard pile; this build is single-player, so that step is a no-op and is
// omitted.
struct Outrage : IroncladT<Outrage> {
  CARD_HEADER(Outrage, "OUTRAGE", 0, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 9);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
  void onUpgrade() override { upgradeVar("Damage", 4); }
};

struct Pillage : IroncladT<Pillage> {
  CARD_HEADER(Pillage, "PILLAGE", 1, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 6);
  }
  // PORT NOTE: CardPile.MaxCardsInHand (10) isn't exposed via game.h; hardcode it.
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    Card* drawn = nullptr;
    do {
      auto drawnVec = co_await cmd::drawCards(*combat, 1);
      drawn = drawnVec.empty() ? nullptr : drawnVec.back();
    } while (drawn && drawn->type == CardType::Attack && (int)combat->hand.size() < 10);
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

struct Rage : IroncladT<Rage> {
  CARD_HEADER(Rage, "RAGE", 0, Skill, Uncommon, Self)
    addVar("Power", 3);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<RagePower>(me(), val("Power"), me(), this); }
  void onUpgrade() override { upgradeVar("Power", 2); }
};

struct Rampage : IroncladT<Rampage> {
  CARD_HEADER(Rampage, "RAMPAGE", 1, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 10);
    addVar("Increase", 5);
  }
  Dec extraDamageFromPlays = 0;
  void afterDowngraded() override { upgradeVar("Damage", extraDamageFromPlays); }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    upgradeVar("Damage", val("Increase"));
    extraDamageFromPlays += val("Increase");
  }
  void onUpgrade() override { upgradeVar("Increase", 5); }
};

struct Rupture : IroncladT<Rupture> {
  CARD_HEADER(Rupture, "RUPTURE", 1, Power, Uncommon, Self)
    addVar("StrengthPower", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<RupturePower>(me(), val("StrengthPower"), me(), this); }
  void onUpgrade() override { upgradeVar("StrengthPower", 1); }
};

struct SecondWind : IroncladT<SecondWind> {
  CARD_HEADER(SecondWind, "SECOND_WIND", 1, Skill, Uncommon, Self)
    addVar("Block", 5);
  }
  Task<> onPlay(CardPlay&) override {
    std::vector<Card*> cards;
    for (Card* c : combat->hand) if (c->type != CardType::Attack) cards.push_back(c);
    for (Card* c : cards) {
      co_await cmd::exhaustCard(*combat, c);
      co_await block(val("Block"));
    }
  }
  void onUpgrade() override { upgradeVar("Block", 2); }
};

struct Spite : IroncladT<Spite> {
  CARD_HEADER(Spite, "SPITE", 0, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 5);
    addVar("Repeat", 2);
  }
  // A DamageReceived entry with unblocked damage on the owner this turn.
  bool lostHpThisTurn() const {
    return combat->history.countThisTurn(*combat, CombatHistoryEntry::DamageReceived, [&](const CombatHistoryEntry& e) {
      return e.actor == combat->player && e.unblocked > 0;
    }) > 0;
  }
  Task<> onPlay(CardPlay& p) override {
    int hits = lostHpThisTurn() ? val("Repeat").toInt() : 1;
    co_await attack(p.target, val("Damage"), hits);
  }
  void onUpgrade() override { upgradeVar("Repeat", 1); }
};

struct Stampede : IroncladT<Stampede> {
  CARD_HEADER(Stampede, "STAMPEDE", 2, Power, Uncommon, Self)
    addVar("Power", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<StampedePower>(me(), val("Power"), me(), this); }
  void onUpgrade() override { cost -= 1; }
};

// Entering combat mid-turn, the cost drops by the Attack CardPlaysFinished this turn (not for a
// clone, which already carries the reduction).
struct Stomp : IroncladT<Stomp> {
  CARD_HEADER(Stomp, "STOMP", 3, Attack, Uncommon, AllEnemies)
    addVar("Damage", 12);
  }
  Task<> onPlay(CardPlay&) override { co_await attackAll(val("Damage")); }
  Task<> afterCardEnteredCombat(Card* card) override {
    if (card != this || !combat || isClone()) return {};
    addThisTurn(-combat->history.countThisTurn(*combat, CombatHistoryEntry::CardPlayFinished,
                                               [](const CombatHistoryEntry& e) { return e.card->type == CardType::Attack; }));
    return {};
  }
  Task<> beforeCardPlayed(const CardPlay& p) override {
    if (ownerOf(p.card) == me() && p.card->type == CardType::Attack) addThisTurn(-1);
    return {};
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

struct StoneArmor : IroncladT<StoneArmor> {
  CARD_HEADER(StoneArmor, "STONE_ARMOR", 1, Power, Uncommon, Self)
    addVar("PlatingPower", 4);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<PlatingPower>(me(), val("PlatingPower"), me(), this); }
  void onUpgrade() override { upgradeVar("PlatingPower", 2); }
};

struct Unrelenting : IroncladT<Unrelenting> {
  CARD_HEADER(Unrelenting, "UNRELENTING", 2, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 14);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<FreeAttackPower>(me(), 1, me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 6); }
};

struct Uppercut : IroncladT<Uppercut> {
  CARD_HEADER(Uppercut, "UPPERCUT", 2, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 13);
    addVar("Power", 1);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<WeakPower>(p.target, val("Power"), me(), this);
    co_await applyPower<VulnerablePower>(p.target, val("Power"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Power", 1); }
};

struct Vicious : IroncladT<Vicious> {
  CARD_HEADER(Vicious, "VICIOUS", 1, Power, Uncommon, Self)
    addVar("Cards", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<ViciousPower>(me(), val("Cards"), me(), this); }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

struct Whirlwind : IroncladT<Whirlwind> {
  CARD_HEADER(Whirlwind, "WHIRLWIND", 0, Attack, Uncommon, AllEnemies)
    costsX = true;
    addVar("Damage", 5);
  }
  Task<> onPlay(CardPlay&) override { co_await attackAll(val("Damage"), xValue); }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

void registerIroncladUncommon() {
  registerCardType<AshenStrike>();
  registerCardType<BattleTrance>();
  registerCardType<Blaze>();
  registerCardType<Bloodletting>();
  registerCardType<Bludgeon>();
  registerCardType<Bully>();
  registerCardType<BurningPact>();
  registerCardType<Colossus>();
  registerCardType<Cruelty>();
  registerCardType<DemonicShield>();
  registerCardType<Dismantle>();
  registerCardType<DrumOfBattle>();
  registerCardType<EvilEye>();
  registerCardType<ExpectAFight>();
  registerCardType<FeelNoPain>();
  registerCardType<FightMe>();
  registerCardType<FlameBarrier>();
  registerCardType<ForgottenRitual>();
  registerCardType<Hemokinesis>();
  registerCardType<HowlFromBeyond>();
  registerCardType<InfernalBlade>();
  registerCardType<Inferno>();
  registerCardType<Inflame>();
  registerCardType<Juggling>();
  registerCardType<Outrage>();
  registerCardType<Pillage>();
  registerCardType<Rage>();
  registerCardType<Rampage>();
  registerCardType<Rupture>();
  registerCardType<SecondWind>();
  registerCardType<Spite>();
  registerCardType<Stampede>();
  registerCardType<Stomp>();
  registerCardType<StoneArmor>();
  registerCardType<Unrelenting>();
  registerCardType<Uppercut>();
  registerCardType<Vicious>();
  registerCardType<Whirlwind>();
}

}  // namespace sts
