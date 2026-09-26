// Common relics (RelicRarity.Common) from SharedRelicPool / IroncladRelicPool.
#include "cards.h"

namespace sts {

namespace {
template <class R> void reg() { db::registerRelic(R::kId, [] { return std::unique_ptr<Relic>(new R()); }); }
}  // namespace

struct Anchor : Relic {
  RELIC_HEADER(Anchor, "ANCHOR", Common)
    addVar("Block", 10);
  }
  Task<> beforeCombatStart() override {
    doFlash();
    co_await cmd::gainBlock(owner(), val("Block"), kUnpowered, nullptr);
  }
};

// BagOfMarbles.cs: at the start of turn 1, apply Vulnerable to all enemies.
struct BagOfMarbles : Relic {
  RELIC_HEADER(BagOfMarbles, "BAG_OF_MARBLES", Common)
    addVar("VulnerablePower", 1);
  }
  Task<> beforeSideTurnStart(Side side, const std::vector<Creature*>& participants) override {
    if (side != Side::Player || !combat || combat->turnNumber > 1 || !contains(participants, owner())) co_return;
    doFlash();
    for (Creature* e : combat->hittableEnemies())
      co_await applyPower<VulnerablePower>(e, val("VulnerablePower"), owner(), nullptr);
  }
};

// BagOfPreparation.cs: draw 2 extra cards on turn 1.
struct BagOfPreparation : Relic {
  RELIC_HEADER(BagOfPreparation, "BAG_OF_PREPARATION", Common)
    addVar("Cards", 2);
  }
  Dec modifyHandDraw(Dec amount) override {
    if (!combat || combat->turnNumber > 1) return amount;
    return amount + val("Cards");
  }
};

// BloodVial.cs: heal 2 at the start of combat (turn 1).
struct BloodVial : Relic {
  RELIC_HEADER(BloodVial, "BLOOD_VIAL", Common)
    addVar("Heal", 2);
  }
  Task<> afterPlayerTurnStart() override {
    if (!combat || combat->turnNumber > 1) co_return;
    doFlash();
    co_await cmd::heal(owner(), val("Heal"));
  }
};

// CentennialPuzzle.cs: the first time you take unblocked damage each combat, draw 3.
struct CentennialPuzzle : Relic {
  RELIC_HEADER(CentennialPuzzle, "CENTENNIAL_PUZZLE", Common)
    addVar("Cards", 3);
  }
  bool usedThisCombat = false;
  Task<> afterDamageReceived(Creature* target, const DamageResult& result, int, Creature*, Card*) override {
    if (!combat || !combat->inProgress || target != owner() || result.unblocked <= 0 || usedThisCombat) co_return;
    doFlash();
    usedThisCombat = true;
    co_await cmd::drawCards(*combat, val("Cards"));
  }
  Task<> afterCombatEnd() override {
    usedThisCombat = false;
    return {};
  }
};

// FestivePopper.cs: deal 9 damage to all enemies on turn 1.
struct FestivePopper : Relic {
  RELIC_HEADER(FestivePopper, "FESTIVE_POPPER", Common)
    addVar("Damage", 9);
  }
  Task<> afterPlayerTurnStart() override {
    if (!combat || combat->turnNumber != 1) co_return;
    doFlash();
    co_await cmd::damage(combat->hittableEnemies(), val("Damage"), kUnpowered, owner(), nullptr);
  }
};

// Gorget.cs: gain 4 Plating on room entry (combat rooms).
struct Gorget : Relic {
  RELIC_HEADER(Gorget, "GORGET", Common)
    addVar("PlatingPower", 4);
  }
  Task<> afterRoomEntered(RoomType room) override {
    if (room != RoomType::Monster && room != RoomType::Elite && room != RoomType::Boss) co_return;
    doFlash();
    co_await applyPower<PlatingPower>(owner(), val("PlatingPower"), owner(), nullptr);
  }
};

// HappyFlower.cs: gain 1 energy every 3rd turn seen.
struct HappyFlower : Relic {
  RELIC_HEADER(HappyFlower, "HAPPY_FLOWER", Common)
    addVar("Energy", 1);
    addVar("Turns", 3);
  }
  int turnsSeen = 0;
  void persist(Archive& a) override { a.io(turnsSeen); }
  bool showCounter() const override { return true; }
  int displayAmount() const override { return turnsSeen; }
  Task<> afterSideTurnStart(Side side, const std::vector<Creature*>& participants) override {
    if (side != Side::Player || !contains(participants, owner())) co_return;
    int turns = val("Turns").toInt();
    turnsSeen = (turnsSeen + 1) % turns;
    if (turnsSeen == 0) {
      doFlash();
      co_await cmd::gainEnergy(*combat, val("Energy").toInt());
    }
  }
  Task<> afterCombatEnd() override {
    turnsSeen = 0;
    return {};
  }
};

// Lantern.cs: gain 1 energy at the start of turn 1.
struct Lantern : Relic {
  RELIC_HEADER(Lantern, "LANTERN", Common)
    addVar("Energy", 1);
  }
  Task<> afterSideTurnStart(Side side, const std::vector<Creature*>& participants) override {
    if (side != Side::Player || !combat || combat->turnNumber > 1 || !contains(participants, owner())) co_return;
    doFlash();
    co_await cmd::gainEnergy(*combat, val("Energy").toInt());
  }
};

// OddlySmoothStone.cs: gain 1 Dexterity on room entry (combat rooms).
struct OddlySmoothStone : Relic {
  RELIC_HEADER(OddlySmoothStone, "ODDLY_SMOOTH_STONE", Common)
    addVar("DexterityPower", 1);
  }
  Task<> afterRoomEntered(RoomType room) override {
    if (room != RoomType::Monster && room != RoomType::Elite && room != RoomType::Boss) co_return;
    doFlash();
    co_await applyPower<DexterityPower>(owner(), val("DexterityPower"), owner(), nullptr);
  }
};

// Pendulum.cs: draw 1 extra card every 3rd turn (counting via BeforeHandDraw).
struct Pendulum : Relic {
  RELIC_HEADER(Pendulum, "PENDULUM", Common)
    addVar("Cards", 1);
    addVar("Turns", 3);
  }
  int turnsSeen = 0;
  void persist(Archive& a) override { a.io(turnsSeen); }
  bool showCounter() const override { return true; }
  int displayAmount() const override { return turnsSeen; }
  Task<> beforeHandDraw() override {
    int turns = val("Turns").toInt();
    turnsSeen = (turnsSeen + 1) % turns;
    if (turnsSeen == 0) doFlash();
    return {};
  }
  Dec modifyHandDraw(Dec amount) override {
    if (turnsSeen != 0) return amount;
    return amount + val("Cards");
  }
  Task<> afterCombatEnd() override {
    turnsSeen = 0;
    return {};
  }
};

// RedMask.cs: apply 1 Weak to all enemies on turn 1.
struct RedMask : Relic {
  RELIC_HEADER(RedMask, "RED_MASK", Common)
    addVar("WeakPower", 1);
  }
  Task<> beforeSideTurnStart(Side side, const std::vector<Creature*>& participants) override {
    if (side != Side::Player || !combat || combat->turnNumber > 1 || !contains(participants, owner())) co_return;
    doFlash();
    for (Creature* e : combat->hittableEnemies())
      co_await applyPower<WeakPower>(e, val("WeakPower"), owner(), nullptr);
  }
};

// RedSkull.cs: gain 3 Strength while below 50% HP.
struct RedSkull : Relic {
  RELIC_HEADER(RedSkull, "RED_SKULL", Common)
    addVar("HpThreshold", 50);
    addVar("StrengthPower", 3);
  }
  bool strengthApplied = false;
  Task<> modifyStrengthIfNecessary() {
    Creature* c = owner();
    bool above = Dec(c->hp) > Dec(c->maxHp) * (val("HpThreshold") / Dec(100));
    Dec amt = val("StrengthPower");
    if (above && strengthApplied) {
      doFlash();
      co_await applyPower<StrengthPower>(c, -amt, c, nullptr);
      strengthApplied = false;
    } else if (!above && !strengthApplied) {
      doFlash();
      co_await applyPower<StrengthPower>(c, amt, c, nullptr);
      strengthApplied = true;
    }
  }
  Task<> afterRoomEntered(RoomType room) override {
    if (room != RoomType::Monster && room != RoomType::Elite && room != RoomType::Boss) co_return;
    co_await modifyStrengthIfNecessary();
  }
  Task<> afterCurrentHpChanged(Creature*, Dec) override {
    if (!combat || !combat->inProgress) co_return;
    co_await modifyStrengthIfNecessary();
  }
  Task<> afterCombatEnd() override {
    strengthApplied = false;
    return {};
  }
};

// RegalPillow.cs: heal 15 extra HP when resting.
struct RegalPillow : Relic {
  RELIC_HEADER(RegalPillow, "REGAL_PILLOW", Common)
    addVar("Heal", 15);
  }
  Dec modifyRestSiteHealAmount(Creature* creature, Dec amount) override {
    if (creature != owner()) return amount;
    return amount + val("Heal");
  }
  Task<> afterRestSiteHeal() override {
    doFlash();
    return {};
  }
};

// Strawberry.cs: +7 max HP on pickup.
struct Strawberry : Relic {
  RELIC_HEADER(Strawberry, "STRAWBERRY", Common)
    addVar("MaxHp", 7);
  }
  Task<> afterObtained() override {
    co_await cmd::gainMaxHp(owner(), val("MaxHp").toInt());
  }
};

// StrikeDummy.cs: +3 damage on cards tagged Strike.
struct StrikeDummy : Relic {
  RELIC_HEADER(StrikeDummy, "STRIKE_DUMMY", Common)
    addVar("ExtraDamage", 3);
  }
  Dec modifyDamageAdditive(Creature*, Dec, int props, Creature* dealer, Card* src) override {
    if (!isPoweredAttack(props)) return 0;
    if (!src || !(src->tags & tagStrike)) return 0;
    if (dealer != owner()) return 0;
    return val("ExtraDamage");
  }
};

// Vajra.cs: gain 1 Strength on room entry (combat rooms).
struct Vajra : Relic {
  RELIC_HEADER(Vajra, "VAJRA", Common)
    addVar("StrengthPower", 1);
  }
  Task<> afterRoomEntered(RoomType room) override {
    if (room != RoomType::Monster && room != RoomType::Elite && room != RoomType::Boss) co_return;
    doFlash();
    co_await applyPower<StrengthPower>(owner(), val("StrengthPower"), owner(), nullptr);
  }
};

// VenerableTeaSet.cs: gain 2 extra energy in the first combat after resting.
struct VenerableTeaSet : Relic {
  RELIC_HEADER(VenerableTeaSet, "VENERABLE_TEA_SET", Common)
    addVar("Energy", 2);
  }
  bool gainEnergyInNextCombat = false;
  void persist(Archive& a) override { a.io(gainEnergyInNextCombat); }
  Task<> afterRoomEntered(RoomType room) override {
    if (room == RoomType::Rest) gainEnergyInNextCombat = true;
    return {};
  }
  Task<> afterEnergyReset() override {
    if (!gainEnergyInNextCombat || !combat) co_return;
    doFlash();
    co_await cmd::gainEnergy(*combat, val("Energy").toInt());
    gainEnergyInNextCombat = false;
  }
};

// WarPaint.cs: upgrade 2 random upgradable Skills on pickup.
struct WarPaint : Relic {
  RELIC_HEADER(WarPaint, "WAR_PAINT", Common)
    addVar("Cards", 2);
  }
  Task<> afterObtained() override {
    std::vector<Card*> candidates;
    for (auto& c : run->deck)
      if (c->type == CardType::Skill && c->upgradable()) candidates.push_back(c.get());
    run->rng("Niche").shuffle(candidates);
    int n = val("Cards").toInt();
    for (int i = 0; i < (int)candidates.size() && i < n; ++i) candidates[i]->upgrade();
    return {};
  }
};

// Whetstone.cs: upgrade 2 random upgradable Attacks on pickup.
struct Whetstone : Relic {
  RELIC_HEADER(Whetstone, "WHETSTONE", Common)
    addVar("Cards", 2);
  }
  Task<> afterObtained() override {
    std::vector<Card*> candidates;
    for (auto& c : run->deck)
      if (c->type == CardType::Attack && c->upgradable()) candidates.push_back(c.get());
    run->rng("Niche").shuffle(candidates);
    int n = val("Cards").toInt();
    for (int i = 0; i < (int)candidates.size() && i < n; ++i) candidates[i]->upgrade();
    return {};
  }
};

// MealTicket.cs: heal 15 on entering a merchant. PORT NOTE: IsAllowed
// (IsBeforeAct3TreasureChest) is not checked.
struct MealTicket : Relic {
  RELIC_HEADER(MealTicket, "MEAL_TICKET", Common)
    addVar("Heal", 15);
  }
  Task<> afterRoomEntered(RoomType room) override {
    Creature* p = owner();
    if (room != RoomType::Shop || p->dead()) co_return;
    doFlash();
    p->hp = std::min(p->maxHp, p->hp + val("Heal").toInt());
  }
};

void registerRelicsCommon() {
  reg<MealTicket>();
  reg<Anchor>();
  reg<BagOfMarbles>();
  reg<BagOfPreparation>();
  reg<BloodVial>();
  reg<CentennialPuzzle>();
  reg<FestivePopper>();
  reg<Gorget>();
  reg<HappyFlower>();
  reg<Lantern>();
  reg<OddlySmoothStone>();
  reg<Pendulum>();
  reg<RedMask>();
  reg<RedSkull>();
  reg<RegalPillow>();
  reg<Strawberry>();
  reg<StrikeDummy>();
  reg<Vajra>();
  reg<VenerableTeaSet>();
  reg<WarPaint>();
  reg<Whetstone>();
}

}  // namespace sts
