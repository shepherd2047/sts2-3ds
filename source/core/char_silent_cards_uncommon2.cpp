// The Silent's Uncommon cards, second half (X1.3b), translated from MegaCrit.Sts2.Core.Models.Cards.*
// of the same name (SilentCardPool.cs, CardRarity.Uncommon). Shared systems (Poison, Shiv, Accuracy) are in
// char_silent.h (X1.0). StranglePower (Strangle) is the one in events_act3.cpp, applied by id.
#include "cards.h"
#include "char_silent.h"

namespace sts {

namespace {

// ---------------------------------------------------------------- powers

// InfiniteBladesPower.cs: before each hand draw, create Amount Shivs in hand.
struct InfiniteBladesPower : Power {
  POWER_HEADER(InfiniteBladesPower, "INFINITE_BLADES_POWER")
  Task<> beforeHandDraw() override {
    flash = 1.f;
    if (owner && owner->combat) co_await createShivsInHand(*owner->combat, amount);
  }
};

// NoxiousFumesPower.cs: at the start of its owner's turn, Poison on every hittable enemy.
struct NoxiousFumesPower : Power {
  POWER_HEADER(NoxiousFumesPower, "NOXIOUS_FUMES_POWER")
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner)) co_return;
    flash = 1.f;
    std::vector<Creature*> enemies = owner->combat->hittableEnemies();
    for (Creature* e : enemies) co_await applyPower<PoisonPower>(e, Dec(amount), owner, nullptr);
  }
};

// PhantomBladesPower.cs: Shivs Retain; the first Shiv played each turn deals Amount more.
struct PhantomBladesPower : Power {
  POWER_HEADER(PhantomBladesPower, "PHANTOM_BLADES_POWER")
  Task<> afterCardEnteredCombat(Card* card) override {
    if ((card->tags & tagShiv) && ownerOf(card) == owner) card->keywords |= kwRetain;  // CardCmd.ApplyKeyword
    co_return;
  }
  Task<> afterApplied(Creature*, Card*) override {
    if (owner && owner->combat)
      for (Card* c : owner->combat->allCards()) if (c->tags & tagShiv) c->keywords |= kwRetain;
    co_return;
  }
  Dec modifyDamageAdditive(Creature*, Dec, int props, Creature* dealer, Card* card) override {
    if (!isPoweredAttack(props) || !card || !(card->tags & tagShiv) || dealer != owner) return 0;
    if (owner->combat && owner->combat->shivPlaysFinishedThisTurn > 0) return 0;
    return amount;
  }
};

// FreeSkillPower.cs: the next Amount Skills cost 0 (mirror of FreeAttackPower).
struct FreeSkillPower : Power {
  POWER_HEADER(FreeSkillPower, "FREE_SKILL_POWER")
  static bool inHandOrPlay(Card* c) {
    if (!c->combat) return false;
    Pile p = c->combat->pileOf(c);
    return p == Pile::Hand || p == Pile::Play;
  }
  int modifyEnergyCostLate(Card* card, int cost) override {
    if (ownerOf(card) != owner || card->type != CardType::Skill || !inHandOrPlay(card)) return cost;
    return 0;
  }
  Task<> beforeCardPlayed(const CardPlay& p) override {
    if (ownerOf(p.card) != owner || p.card->type != CardType::Skill || !inHandOrPlay(p.card)) co_return;
    co_await cmd::decrement(this);
  }
};

// SpeedsterPower.cs: whenever its owner draws a card on their turn outside the hand draw, Unpowered
// damage to every hittable enemy.
struct SpeedsterPower : Power {
  POWER_HEADER(SpeedsterPower, "SPEEDSTER_POWER")
  Task<> afterCardDrawn(Card* card, bool fromHandDraw) override {
    if (fromHandDraw || ownerOf(card) != owner || !owner->combat || owner->combat->currentSide != Side::Player) co_return;
    std::vector<Creature*> enemies = owner->combat->hittableEnemies();
    for (Creature* e : enemies) co_await cmd::damage(e, Dec(amount), kUnpowered, owner, nullptr);
  }
};

// ---------------------------------------------------------------- cards

// Haze.cs: Poison then Weak on every hittable enemy.
struct Haze : IroncladT<Haze> {
  CARD_HEADER(Haze, "HAZE", 2, Skill, Uncommon, AllEnemies)
    addVar("PoisonPower", 4);
    addVar("WeakPower", 1);
  }
  Task<> onPlay(CardPlay&) override {
    std::vector<Creature*> enemies = combat->hittableEnemies();
    for (Creature* e : enemies) co_await applyPower<PoisonPower>(e, val("PoisonPower"), me(), this);
    enemies = combat->hittableEnemies();
    for (Creature* e : enemies) co_await applyPower<WeakPower>(e, val("WeakPower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("PoisonPower", 2); upgradeVar("WeakPower", 1); }
};

// HiddenDaggers.cs: discard Cards cards, create Shivs Shivs in hand (upgraded when this card is upgraded).
struct HiddenDaggers : IroncladT<HiddenDaggers> {
  CARD_HEADER(HiddenDaggers, "HIDDEN_DAGGERS", 0, Skill, Uncommon, Self)
    addVar("Cards", 2);
    addVar("Shivs", 2);
  }
  Task<> onPlay(CardPlay&) override {
    int n = std::min(val("Cards").toInt(), (int)combat->hand.size());
    auto picked = co_await cmd::selectCards(*combat, "card_selection.TO_DISCARD", combat->hand, n, n);
    co_await cmd::discardCards(*combat, picked, 0);
    auto made = co_await createShivsInHand(*combat, val("Shivs").toInt());
    if (!upgraded()) co_return;
    for (Card* k : made) k->upgrade();
  }
};

// InfiniteBlades.cs: Power; upgrade adds Innate.
struct InfiniteBlades : IroncladT<InfiniteBlades> {
  CARD_HEADER(InfiniteBlades, "INFINITE_BLADES", 1, Power, Uncommon, Self)
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<InfiniteBladesPower>(me(), Dec(1), me(), this); }
  void onUpgrade() override { keywords |= kwInnate; }
};

// LegSweep.cs: block, then Weak on the target.
struct LegSweep : IroncladT<LegSweep> {
  CARD_HEADER(LegSweep, "LEG_SWEEP", 2, Skill, Uncommon, AnyEnemy)
    addVar("Block", 11);
    addVar("WeakPower", 2);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await block(val("Block"));
    co_await applyPower<WeakPower>(p.target, val("WeakPower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Block", 3); upgradeVar("WeakPower", 1); }
};

// MementoMori.cs: 9 + 4 per card discarded this turn.
struct MementoMori : IroncladT<MementoMori> {
  CARD_HEADER(MementoMori, "MEMENTO_MORI", 1, Attack, Uncommon, AnyEnemy)
    addVar("CalculationBase", 9);
    addVar("ExtraDamage", 4);
    addVar("CalculatedDamage", 0);
    calcMultiplier = [](Card* c) { return c->combat ? c->combat->discardsThisTurn() : 0; };
  }
  Task<> onPlay(CardPlay& p) override { co_await attackCalculated(p.target); }
  void onUpgrade() override { upgradeVar("CalculationBase", 2); upgradeVar("ExtraDamage", 1); }
};

// Mirage.cs: Exhaust; block equal to all Poison on living enemies.
struct Mirage : IroncladT<Mirage> {
  CARD_HEADER(Mirage, "MIRAGE", 1, Skill, Uncommon, Self)
    keywords = kwExhaust;
    addVar("CalculationBase", 0);
    addVar("CalculationExtra", 1);
    addVar("CalculatedBlock", 0);
    calcMultiplier = [](Card* c) {
      int n = 0;
      if (c->combat) for (Creature* e : c->combat->aliveEnemies()) n += e->powerAmount<PoisonPower>();
      return n;
    };
  }
  Task<> onPlay(CardPlay&) override { co_await block(calculatedBlock()); }
  void onUpgrade() override { keywords &= ~kwExhaust; }
};

// NoxiousFumes.cs: Power, Poison to all enemies each turn.
struct NoxiousFumes : IroncladT<NoxiousFumes> {
  CARD_HEADER(NoxiousFumes, "NOXIOUS_FUMES", 1, Power, Uncommon, Self)
    addVar("PoisonPerTurn", 2);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<NoxiousFumesPower>(me(), val("PoisonPerTurn"), me(), this); }
  void onUpgrade() override { upgradeVar("PoisonPerTurn", 1); }
};

// PhantomBlades.cs: Power, Shivs Retain and the first Shiv each turn deals more.
struct PhantomBlades : IroncladT<PhantomBlades> {
  CARD_HEADER(PhantomBlades, "PHANTOM_BLADES", 1, Power, Uncommon, Self)
    addVar("PhantomBladesPower", 9);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<PhantomBladesPower>(me(), val("PhantomBladesPower"), me(), this); }
  void onUpgrade() override { upgradeVar("PhantomBladesPower", 3); }
};

// Pinpoint.cs: costs 1 less for every Skill played this turn (including before it was created).
struct Pinpoint : IroncladT<Pinpoint> {
  CARD_HEADER(Pinpoint, "PINPOINT", 3, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 15);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
  void onUpgrade() override { upgradeVar("Damage", 4); }
  Task<> afterCardEnteredCombat(Card* card) override {
    if (card != this || isDupe || !combat) co_return;
    addThisTurn(-combat->skillsFinishedThisTurn);
  }
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (ownerOf(p.card) != ownerOf(this) || p.card->type != CardType::Skill) co_return;
    addThisTurn(-1);
  }
};

// Pounce.cs: attack, then the next Skill costs 0.
struct Pounce : IroncladT<Pounce> {
  CARD_HEADER(Pounce, "POUNCE", 2, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 14);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<FreeSkillPower>(me(), Dec(1), me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 6); }
};

// PreciseCut.cs: 13 minus 2 per other card in hand.
struct PreciseCut : IroncladT<PreciseCut> {
  CARD_HEADER(PreciseCut, "PRECISE_CUT", 0, Attack, Uncommon, AnyEnemy)
    addVar("CalculationBase", 13);
    addVar("ExtraDamage", 2);
    addVar("CalculatedDamage", 0);
    calcMultiplier = [](Card* c) {
      if (!c->combat) return 0;
      int n = (int)c->combat->hand.size();
      if (c->combat->pileOf(c) == Pile::Hand) --n;
      return -n;
    };
  }
  Task<> onPlay(CardPlay& p) override { co_await attackCalculated(p.target); }
  void onUpgrade() override { upgradeVar("CalculationBase", 3); }
};

// Reflex.cs: Sly, draw cards.
struct Reflex : IroncladT<Reflex> {
  CARD_HEADER(Reflex, "REFLEX", 3, Skill, Uncommon, Self)
    keywords = kwSly;
    addVar("Cards", 2);
  }
  Task<> onPlay(CardPlay&) override { co_await drawCards(val("Cards")); }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

// Skewer.cs: X hits.
struct Skewer : IroncladT<Skewer> {
  CARD_HEADER(Skewer, "SKEWER", 0, Attack, Uncommon, AnyEnemy)
    costsX = true;
    addVar("Damage", 8);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage"), xValue); }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

// Speedster.cs: Power; upgrade adds Innate.
struct Speedster : IroncladT<Speedster> {
  CARD_HEADER(Speedster, "SPEEDSTER", 2, Power, Uncommon, Self)
    addVar("SpeedsterPower", 2);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<SpeedsterPower>(me(), Dec(val("SpeedsterPower").toInt()), me(), this); }
  void onUpgrade() override { keywords |= kwInnate; }
};

// Strangle.cs: attack, then Strangle on the target (it loses HP whenever a card is played).
struct Strangle : IroncladT<Strangle> {
  CARD_HEADER(Strangle, "STRANGLE", 1, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 8);
    addVar("StranglePower", 2);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    auto pw = db::power("StranglePower");
    if (pw) co_await cmd::applyPower(std::move(pw), p.target, val("StranglePower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 2); upgradeVar("StranglePower", 1); }
};

// Tactician.cs: Sly, gain energy.
struct Tactician : IroncladT<Tactician> {
  CARD_HEADER(Tactician, "TACTICIAN", 3, Skill, Uncommon, Self)
    keywords = kwSly;
    addVar("Energy", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await cmd::gainEnergy(*combat, val("Energy").toInt()); }
  void onUpgrade() override { upgradeVar("Energy", 1); }
};

// UpMySleeve.cs: Cards Shivs in hand, then this card costs 1 less for the rest of the combat.
// (The C#'s TimesPlayedThisCombat counter is only written, never read, so it is not kept.)
struct UpMySleeve : IroncladT<UpMySleeve> {
  CARD_HEADER(UpMySleeve, "UP_MY_SLEEVE", 2, Skill, Uncommon, Self)
    addVar("Cards", 3);
  }
  Task<> onPlay(CardPlay&) override {
    int n = val("Cards").toInt();
    for (int i = 0; i < n; ++i) {
      co_await createShivsInHand(*combat, 1);
      co_await wait(0.1);
    }
    addThisCombat(-1);
  }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

}  // namespace

void registerSilentUncommonCards2() {
  registerPowerType<InfiniteBladesPower>();
  registerPowerType<NoxiousFumesPower>();
  registerPowerType<PhantomBladesPower>();
  registerPowerType<FreeSkillPower>();
  registerPowerType<SpeedsterPower>();
  registerCardType<Haze>();
  registerCardType<HiddenDaggers>();
  registerCardType<InfiniteBlades>();
  registerCardType<LegSweep>();
  registerCardType<MementoMori>();
  registerCardType<Mirage>();
  registerCardType<NoxiousFumes>();
  registerCardType<PhantomBlades>();
  registerCardType<Pinpoint>();
  registerCardType<Pounce>();
  registerCardType<PreciseCut>();
  registerCardType<Reflex>();
  registerCardType<Skewer>();
  registerCardType<Speedster>();
  registerCardType<Strangle>();
  registerCardType<Tactician>();
  registerCardType<UpMySleeve>();
}

}  // namespace sts
