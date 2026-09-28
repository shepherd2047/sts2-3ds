// Phrog Parasite elite: PhrogParasite, Wriggler, InfestedPower and the Infection
// status card. On death the parasite splits into four stunned Wrigglers.
#include <algorithm>

#include "cards.h"

namespace sts {

namespace {
#define MONSTER_HEADER(Name, Key) \
  Name() { id = #Name; locKey = Key; }

using Targets = const std::vector<Creature*>&;

Intent attackIntent(int dmg, int hits = 1) { Intent i; i.kind = Intent::Attack; i.damage = dmg; i.hits = hits; return i; }
Intent kindIntent(Intent::Kind k, int count = 0) { Intent i; i.kind = k; i.count = count; return i; }
}  // namespace

struct Infection : IroncladT<Infection> {
  CARD_HEADER(Infection, "INFECTION", -1, Status, Status, None)
    keywords = kwUnplayable;
    maxUpgradeLevel = 0;
    addVar("Damage", 3);
  }
  bool hasTurnEndInHandEffect() const override { return true; }
  Task<> onTurnEndInHand() override { co_await cmd::damage(me(), val("Damage"), kUnpowered | kMove, nullptr, this); }
};

struct Wriggler : Monster {
  MONSTER_HEADER(Wriggler, "WRIGGLER")
  bool startStunned = false;
  int slot = 1;  // "wriggler1".."wriggler4": the slot picks the opening move
  int minHp() const override { return asc(kToughEnemies, 18, 17); }
  int maxHp() const override { return asc(kToughEnemies, 22, 21); }
  void buildMoves() override {
    auto* bite = machine.add<MoveState>("NASTY_BITE_MOVE");
    bite->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 7, 6)); };
    bite->intents = {attackIntent(asc(kDeadlyEnemies, 7, 6))};
    auto* wriggle = machine.add<MoveState>("WRIGGLE_MOVE");
    wriggle->perform = [this](Targets) { return wriggleMove(); };
    wriggle->intents = {kindIntent(Intent::Buff), kindIntent(Intent::Status, 1)};
    auto* spawned = machine.add<MoveState>("SPAWNED_MOVE");
    spawned->perform = [](Targets) -> Task<> { co_return; };
    spawned->intents = {kindIntent(Intent::Stun)};
    auto* init = machine.add<ConditionalBranchState>("INIT_MOVE");
    init->add(bite, [this] { return slot == 1; });
    init->add(wriggle, [this] { return slot == 2; });
    init->add(bite, [this] { return slot == 3; });
    init->add(wriggle, [this] { return slot == 4; });
    spawned->followUp = init;
    bite->followUp = wriggle;
    wriggle->followUp = bite;
    machine.start(startStunned ? (MonsterState*)spawned : init);
  }
  Task<> wriggleMove() {
    co_await cmd::addStatusCards(*combat, "Infection", Pile::Discard, 1);
    co_await applyToSelf<StrengthPower>(2);
  }
};

struct InfestedPower : Power {
  POWER_HEADER(InfestedPower, "INFESTED_POWER")
  StackType stackType() const override { return StackType::Single; }
  Task<> afterDeath(Creature* target) override {
    if (target != owner) co_return;
    Combat& c = *owner->combat;
    for (int i = 0; i < 4; ++i) {
      auto w = std::make_unique<Wriggler>();
      w->startStunned = true;
      w->slot = i + 1;
      co_await cmd::addMonster(c, std::move(w));
    }
  }
  bool shouldStopCombatFromEnding() override { return true; }
};

struct PhrogParasite : Monster {
  MONSTER_HEADER(PhrogParasite, "PHROG_PARASITE")
  int minHp() const override { return asc(kToughEnemies, 66, 61); }
  int maxHp() const override { return asc(kToughEnemies, 68, 64); }
  Task<> afterAddedToRoom() override { co_await applyToSelf<InfestedPower>(4); }
  void buildMoves() override {
    auto* infect = machine.add<MoveState>("INFECT_MOVE");
    infect->perform = [this](Targets) { return cmd::addStatusCards(*combat, "Infection", Pile::Discard, 3); };
    infect->intents = {kindIntent(Intent::Status, 3)};
    auto* lash = machine.add<MoveState>("LASH_MOVE");
    lash->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 5, 4), 4); };
    lash->intents = {attackIntent(asc(kDeadlyEnemies, 5, 4), 4)};
    auto* rand = machine.add<RandomBranchState>("RAND");
    infect->followUp = lash;
    lash->followUp = infect;
    rand->add(infect, MoveRepeat::CannotRepeat);
    rand->add(lash, MoveRepeat::CannotRepeat);
    machine.start(infect);
  }
};

void registerPhrog() {
  db::registerCard("Infection", [] { return std::unique_ptr<Card>(new Infection()); });
  db::registerPower("InfestedPower", [] { return std::unique_ptr<Power>(new InfestedPower()); });
  db::registerEncounter("PhrogParasiteElite", RoomType::Elite, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(std::make_unique<PhrogParasite>());
    return v;
  });
  // DenseVegetationEventEncounter: four awake Wrigglers (event fight, no rewards).
  db::registerEncounter("DenseVegetationEventEncounter", RoomType::Monster, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    for (int i = 1; i <= 4; ++i) {
      auto w = std::make_unique<Wriggler>();
      w->slot = i;
      v.push_back(std::move(w));
    }
    return v;
  });
}

}  // namespace sts
