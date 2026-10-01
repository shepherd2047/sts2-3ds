// The Defect's Uncommon cards, first half (X2.3a): BootSequence .. Null in DefectCardPool order.
// Orb types and Focus are in char_defect.h. The powers the cards need (Feral, Hailstorm,
// Iteration, Loop) and the Fuel token (Compact) are defined here.
#include "cards.h"
#include "char_defect.h"

namespace sts {

namespace {

// FeralPower (Models.Powers): the first N attacks each turn that cost 0 energy return to the
// top of the hand instead of being discarded.
// PORT NOTE: (1) this engine's modifyCardPlayResultLocation hook has no ResourceInfo, so "energy
// spent == 0" is derived as autoPlay || (X-cost ? xValue == 0 : energyCost(card) == 0); (2) there
// is no card position in a Pile result, so the card goes to the end of the hand (Pile::Hand)
// rather than CardPilePosition.Top; (3) DisplayAmount (remaining count) is not modeled.
// AfterApplied seeds the counter from the Attack CardPlaysStarted this turn that spent no energy.
struct FeralPower : Power {
  POWER_HEADER(FeralPower, "FERAL_POWER")
  int zeroCostAttacksPlayed = 0;
  Task<> afterApplied(Creature*, Card*) override {
    Combat* c = owner->combat;
    zeroCostAttacksPlayed = c->history.countThisTurn(*c, CombatHistoryEntry::CardPlayStarted, [](const CombatHistoryEntry& e) {
      return e.card->type == CardType::Attack && e.amount == 0;  // Resources.EnergyValue == 0
    });
    return {};
  }
  Pile modifyCardPlayResultLocation(Card* card, bool autoPlay, Pile pile) override {
    if (ownerOf(card) != owner) return pile;
    if (card->type != CardType::Attack) return pile;
    bool zeroEnergy = autoPlay || (card->costsX ? card->xValue == 0 : owner->combat->energyCost(card) <= 0);
    if (!zeroEnergy) return pile;
    if (card->isDupe) return pile;
    if (zeroCostAttacksPlayed >= amount) return pile;
    return Pile::Hand;
  }
  Task<> afterModifyingCardPlayResultLocation(Card*, Pile) override {
    flash = 1.f;
    ++zeroCostAttacksPlayed;
    co_return;
  }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) zeroCostAttacksPlayed = 0;
    co_return;
  }
};

// HailstormPower: at the end of the owner's turn, if at least FrostOrbs (const 1) Frost orbs
// are queued, deal Amount Unpowered damage to every hittable enemy.
struct HailstormPower : Power {
  POWER_HEADER(HailstormPower, "HAILSTORM_POWER")
  Task<> beforeSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner)) co_return;
    int n = 0;
    for (auto& o : owner->combat->orbQueue) if (o->id == "FrostOrb") ++n;
    if (n >= 1) {
      flash = 1.f;
      co_await cmd::damage(owner->combat->hittableEnemies(), Dec(amount), kUnpowered, owner, nullptr);
    }
  }
};

// IterationPower: the first Status card drawn each turn draws Amount cards.
// "First" = at most one Status CardDrawn entry this turn (the draw itself is already logged).
struct IterationPower : Power {
  POWER_HEADER(IterationPower, "ITERATION_POWER")
  Task<> afterCardDrawn(Card* card, bool) override {
    if (ownerOf(card) != owner || card->type != CardType::Status) co_return;
    Combat* c = owner->combat;
    int statusDraws = c->history.countThisTurn(*c, CombatHistoryEntry::CardDrawn,
                                               [](const CombatHistoryEntry& e) { return e.card->type == CardType::Status; });
    if (statusDraws <= 1) {
      flash = 1.f;
      co_await cmd::drawCards(*c, Dec(amount));
    }
  }
};

// LoopPower: at the start of the owner's turn, trigger the front orb's passive Amount times.
struct LoopPower : Power {
  POWER_HEADER(LoopPower, "LOOP_POWER")
  Task<> afterPlayerTurnStart() override {
    Combat* c = owner->combat;
    if (c->orbQueue.empty()) co_return;
    for (int i = 0; i < amount; ++i) {
      if (c->orbQueue.empty()) break;
      co_await cmd::orbPassive(*c, c->orbQueue[0].get(), nullptr);
      co_await wait(0.25);
    }
  }
};

// Fuel.cs (token, made by Compact): 0-cost Exhaust skill, gain Energy.
struct Fuel : IroncladT<Fuel> {
  CARD_HEADER(Fuel, "FUEL", 0, Skill, Token, Self)
    keywords = kwExhaust;
    addVar("Energy", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await cmd::gainEnergy(*combat, val("Energy").toInt()); }
  void onUpgrade() override { upgradeVar("Energy", 1); }
};

// BootSequence.cs: Innate, Exhaust block.
struct BootSequence : IroncladT<BootSequence> {
  CARD_HEADER(BootSequence, "BOOT_SEQUENCE", 0, Skill, Uncommon, Self)
    keywords = kwInnate | kwExhaust;
    addVar("Block", 10);
  }
  Task<> onPlay(CardPlay&) override { co_await block(val("Block")); }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

// BulkUp.cs: lose orb slots, gain Strength and Dexterity.
struct BulkUp : IroncladT<BulkUp> {
  CARD_HEADER(BulkUp, "BULK_UP", 2, Power, Uncommon, Self)
    addVar("OrbSlots", 1);
    addVar("StrengthPower", 2);
    addVar("DexterityPower", 2);
  }
  Task<> onPlay(CardPlay&) override {
    cmd::removeOrbSlots(*combat, val("OrbSlots").toInt());
    co_await applyPower<StrengthPower>(me(), val("StrengthPower"), me(), this);
    co_await applyPower<DexterityPower>(me(), val("DexterityPower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("StrengthPower", 1); upgradeVar("DexterityPower", 1); }
};

// Capacitor.cs: gain orb slots.
struct Capacitor : IroncladT<Capacitor> {
  CARD_HEADER(Capacitor, "CAPACITOR", 1, Power, Uncommon, Self)
    addVar("Repeat", 2);
  }
  Task<> onPlay(CardPlay&) override { co_await cmd::addOrbSlots(*combat, val("Repeat").toInt()); }
  void onUpgrade() override { upgradeVar("Repeat", 1); }
};

// Chaos.cs: channel Repeat random orbs (CombatOrbGeneration stream).
struct Chaos : IroncladT<Chaos> {
  CARD_HEADER(Chaos, "CHAOS", 1, Skill, Uncommon, Self)
    addVar("Repeat", 1);
  }
  Task<> onPlay(CardPlay&) override {
    for (int i = 0; i < val("Repeat").toInt(); ++i)
      co_await cmd::channelOrb(*combat, db::randomOrb(combat->rng("CombatOrbGeneration")));
  }
  void onUpgrade() override { upgradeVar("Repeat", 1); }
};

// Chill.cs: channel a Frost orb per hittable enemy.
struct Chill : IroncladT<Chill> {
  CARD_HEADER(Chill, "CHILL", 0, Skill, Uncommon, Self)
    keywords = kwExhaust;
  }
  Task<> onPlay(CardPlay&) override {
    size_t n = combat->hittableEnemies().size();
    for (size_t i = 0; i < n; ++i) co_await cmd::channelOrb(*combat, std::make_unique<FrostOrb>());
  }
  void onUpgrade() override { keywords &= ~kwExhaust; }
};

// Compact.cs: block, then transform every transformable Status card in hand into Fuel
// (upgraded if Compact is).
struct Compact : IroncladT<Compact> {
  CARD_HEADER(Compact, "COMPACT", 1, Skill, Uncommon, Self)
    addVar("Block", 6);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    std::vector<Card*> list;
    for (Card* c : combat->hand)
      if (c && c->isTransformable() && c->type == CardType::Status) list.push_back(c);
    for (Card* c : list) {
      auto fuel = std::make_unique<Fuel>();
      if (upgraded()) fuel->upgrade();
      co_await cmd::transform(*combat, c, std::move(fuel));
    }
  }
  void onUpgrade() override { upgradeVar("Block", 1); }
};

// Darkness.cs: channel a Dark orb, then trigger every queued Dark orb's passive once (twice
// when upgraded).
struct Darkness : IroncladT<Darkness> {
  CARD_HEADER(Darkness, "DARKNESS", 1, Skill, Uncommon, Self) }
  Task<> onPlay(CardPlay&) override {
    co_await cmd::channelOrb(*combat, std::make_unique<DarkOrb>());
    std::vector<Orb*> dark;
    for (auto& o : combat->orbQueue) if (o->id == "DarkOrb") dark.push_back(o.get());
    int triggerCount = upgraded() ? 2 : 1;
    for (Orb* o : dark)
      for (int i = 0; i < triggerCount; ++i) co_await cmd::orbPassive(*combat, o, nullptr);
  }
};

// DoubleEnergy.cs: double the current energy.
struct DoubleEnergy : IroncladT<DoubleEnergy> {
  CARD_HEADER(DoubleEnergy, "DOUBLE_ENERGY", 1, Skill, Uncommon, Self)
    keywords = kwExhaust;
  }
  Task<> onPlay(CardPlay&) override { co_await cmd::gainEnergy(*combat, combat->energy); }
  void onUpgrade() override { cost -= 1; }
};

// Feral.cs: apply FeralPower.
struct Feral : IroncladT<Feral> {
  CARD_HEADER(Feral, "FERAL", 2, Power, Uncommon, Self)
    addVar("FeralPower", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<FeralPower>(me(), val("FeralPower"), me(), this); }
  void onUpgrade() override { cost -= 1; }
};

// FightThrough.cs: block, then two Wounds go to the discard pile.
struct FightThrough : IroncladT<FightThrough> {
  CARD_HEADER(FightThrough, "FIGHT_THROUGH", 1, Skill, Uncommon, Self)
    addVar("Block", 13);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    co_await cmd::addStatusCards(*combat, "Wound", Pile::Discard, 2, true);
  }
  void onUpgrade() override { upgradeVar("Block", 4); }
};

// Ftl.cs: 0-cost attack; draws while fewer than PlayMax cards were played this turn.
// Counts the CardPlaysFinished this turn (the FTL in progress is not finished yet).
// PORT NOTE: ShouldGlowGoldInternal (card glow while a draw is still possible) has no Card hook
// in this engine and is dropped (UI).
struct Ftl : IroncladT<Ftl> {
  CARD_HEADER(Ftl, "FTL", 0, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 5);
    addVar("PlayMax", 3);
    addVar("Cards", 1);
  }
  bool canDrawCard() {
    return combat->history.countThisTurn(*combat, CombatHistoryEntry::CardPlayFinished) < val("PlayMax").toInt();
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    if (canDrawCard()) co_await drawCards(val("Cards"));
  }
  void onUpgrade() override { upgradeVar("Damage", 1); upgradeVar("PlayMax", 1); }
};

// Fusion.cs: channel a Plasma orb.
struct Fusion : IroncladT<Fusion> {
  CARD_HEADER(Fusion, "FUSION", 1, Skill, Uncommon, Self)
    keywords = kwExhaust;
  }
  Task<> onPlay(CardPlay&) override { co_await cmd::channelOrb(*combat, std::make_unique<PlasmaOrb>()); }
  void onUpgrade() override { keywords &= ~kwExhaust; }
};

// Glacier.cs: block, then two Frost orbs.
struct Glacier : IroncladT<Glacier> {
  CARD_HEADER(Glacier, "GLACIER", 2, Skill, Uncommon, Self)
    addVar("Block", 6);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    for (int i = 0; i < 2; ++i) co_await cmd::channelOrb(*combat, std::make_unique<FrostOrb>());
  }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

// Glasswork.cs: block, then a Glass orb.
struct Glasswork : IroncladT<Glasswork> {
  CARD_HEADER(Glasswork, "GLASSWORK", 1, Skill, Uncommon, Self)
    addVar("Block", 5);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    co_await cmd::channelOrb(*combat, std::make_unique<GlassOrb>());
  }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

// Hailstorm.cs: apply HailstormPower.
struct Hailstorm : IroncladT<Hailstorm> {
  CARD_HEADER(Hailstorm, "HAILSTORM", 1, Power, Uncommon, Self)
    addVar("HailstormPower", 6);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<HailstormPower>(me(), val("HailstormPower"), me(), this); }
  void onUpgrade() override { upgradeVar("HailstormPower", 2); }
};

// Iteration.cs: apply IterationPower.
struct Iteration : IroncladT<Iteration> {
  CARD_HEADER(Iteration, "ITERATION", 1, Power, Uncommon, Self)
    addVar("IterationPower", 2);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<IterationPower>(me(), val("IterationPower"), me(), this); }
  void onUpgrade() override { upgradeVar("IterationPower", 1); }
};

// Loop.cs: apply LoopPower.
struct Loop : IroncladT<Loop> {
  CARD_HEADER(Loop, "LOOP", 1, Power, Uncommon, Self)
    addVar("Loop", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<LoopPower>(me(), val("Loop"), me(), this); }
  void onUpgrade() override { upgradeVar("Loop", 1); }
};

// Null.cs: attack, Weak, then a Dark orb.
struct Null : IroncladT<Null> {
  CARD_HEADER(Null, "NULL", 2, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 10);
    addVar("WeakPower", 1);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<WeakPower>(p.target, val("WeakPower"), me(), this);
    co_await cmd::channelOrb(*combat, std::make_unique<DarkOrb>());
  }
  void onUpgrade() override { upgradeVar("Damage", 3); upgradeVar("WeakPower", 1); }
};

}  // namespace

void registerDefectUncommonCards1() {
  registerPowerType<FeralPower>();
  registerPowerType<HailstormPower>();
  registerPowerType<IterationPower>();
  registerPowerType<LoopPower>();
  registerCardType<Fuel>();
  registerCardType<BootSequence>();
  registerCardType<BulkUp>();
  registerCardType<Capacitor>();
  registerCardType<Chaos>();
  registerCardType<Chill>();
  registerCardType<Compact>();
  registerCardType<Darkness>();
  registerCardType<DoubleEnergy>();
  registerCardType<Feral>();
  registerCardType<FightThrough>();
  registerCardType<Ftl>();
  registerCardType<Fusion>();
  registerCardType<Glacier>();
  registerCardType<Glasswork>();
  registerCardType<Hailstorm>();
  registerCardType<Iteration>();
  registerCardType<Loop>();
  registerCardType<Null>();
}

}  // namespace sts
