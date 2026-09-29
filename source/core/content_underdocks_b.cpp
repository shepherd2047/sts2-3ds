// Underdocks monsters 2/2 (package A11b): Seapunk, Sewer Clam, Sludge Spinner, Toadpole,
// Two-Tailed Rat (+ a local Calcified Cultist for SeapunkNormal) and their encounters
// (MegaCrit.Sts2.Core.Models.Monsters / .Encounters). The Underdocks act itself is A11f.
// Values are the non-ascension ones (see docs/PORTING.md).
#include <algorithm>

#include "cards.h"
#include "powers.h"

namespace sts {

std::unique_ptr<Monster> makeCalcifiedCultist();  // content_underdocks_a.cpp

namespace {

#define MONSTER_HEADER(Name, Key) \
  Name() { id = #Name; locKey = Key; }

using Targets = const std::vector<Creature*>&;

Intent attackIntent(int dmg, int hits = 1) { Intent i; i.kind = Intent::Attack; i.damage = dmg; i.hits = hits; return i; }
Intent kindIntent(Intent::Kind k, int count = 0) { Intent i; i.kind = k; i.count = count; return i; }

template <class M> std::unique_ptr<Monster> mk() { return std::make_unique<M>(); }
template <class... Ms> std::vector<std::unique_ptr<Monster>> list() {
  std::vector<std::unique_ptr<Monster>> v;
  (v.push_back(mk<Ms>()), ...);
  return v;
}

// Powers used here that are registered by other content files (ThornsPower in
// content_act2a.cpp, RitualPower in content_act3a.cpp) are applied by id.
Task<> applyById(Monster* m, const char* powerId, int amount) {
  co_await cmd::applyPower(db::power(powerId), m->creature, Dec(amount), m->creature, nullptr);
}

// ================================================================ Seapunk

struct Seapunk : Monster {
  MONSTER_HEADER(Seapunk, "SEAPUNK")
  int minHp() const override { return asc(kToughEnemies, 47, 44); }
  int maxHp() const override { return asc(kToughEnemies, 49, 46); }
  void buildMoves() override {
    auto* kick = machine.add<MoveState>("SEA_KICK_MOVE");
    kick->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 13, 11)); };
    kick->intents = {attackIntent(asc(kDeadlyEnemies, 13, 11))};
    auto* spin = machine.add<MoveState>("SPINNING_KICK_MOVE");
    spin->perform = [this](Targets) { return attack(2, 4); };
    spin->intents = {attackIntent(2, 4)};
    auto* burp = machine.add<MoveState>("BUBBLE_BURP_MOVE");
    burp->perform = [this](Targets) { return bubbleBurp(); };
    burp->intents = {kindIntent(Intent::Buff), kindIntent(Intent::Defend)};
    kick->followUp = spin;
    spin->followUp = burp;
    burp->followUp = kick;
    machine.start(kick);
  }
  Task<> bubbleBurp() {
    co_await gainBlock(asc(kToughEnemies, 8, 7));
    co_await applyToSelf<StrengthPower>(asc(kDeadlyEnemies, 2, 1));
  }
};


// ================================================================ Sewer Clam

struct SewerClam : Monster {
  MONSTER_HEADER(SewerClam, "SEWER_CLAM")
  int minHp() const override { return asc(kToughEnemies, 58, 56); }
  int maxHp() const override { return minHp(); }
  Task<> afterAddedToRoom() override { co_await applyToSelf<PlatingPower>(asc(kToughEnemies, 9, 8)); }
  void buildMoves() override {
    auto* pressurize = machine.add<MoveState>("PRESSURIZE_MOVE");
    pressurize->perform = [this](Targets) { return applyToSelf<StrengthPower>(4); };
    pressurize->intents = {kindIntent(Intent::Buff)};
    auto* jet = machine.add<MoveState>("JET_MOVE");
    jet->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 11, 10)); };
    jet->intents = {attackIntent(asc(kDeadlyEnemies, 11, 10))};
    pressurize->followUp = jet;
    jet->followUp = pressurize;
    machine.start(jet);
  }
};

// ================================================================ Sludge Spinner

struct SludgeSpinner : Monster {
  MONSTER_HEADER(SludgeSpinner, "SLUDGE_SPINNER")
  int minHp() const override { return asc(kToughEnemies, 41, 37); }
  int maxHp() const override { return asc(kToughEnemies, 42, 39); }
  void buildMoves() override {
    auto* oil = machine.add<MoveState>("OIL_SPRAY_MOVE");
    oil->perform = [this](Targets t) { return oilSpray(t); };
    oil->intents = {attackIntent(asc(kDeadlyEnemies, 9, 8)), kindIntent(Intent::Debuff)};
    auto* slam = machine.add<MoveState>("SLAM_MOVE");
    slam->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 12, 11)); };
    slam->intents = {attackIntent(asc(kDeadlyEnemies, 12, 11))};
    auto* rage = machine.add<MoveState>("RAGE_MOVE");
    rage->perform = [this](Targets) { return rageMove(); };
    rage->intents = {attackIntent(asc(kDeadlyEnemies, 7, 6)), kindIntent(Intent::Buff)};
    auto* rand = machine.add<RandomBranchState>("RAND");
    oil->followUp = rand;
    slam->followUp = rand;
    rage->followUp = rand;
    rand->add(oil, MoveRepeat::CannotRepeat);
    rand->add(slam, MoveRepeat::CannotRepeat);
    rand->add(rage, MoveRepeat::CannotRepeat);
    machine.start(oil);
  }
  Task<> oilSpray(Targets t) {
    co_await attack(asc(kDeadlyEnemies, 9, 8));
    co_await applyToTargets<WeakPower>(t, 1);
  }
  Task<> rageMove() {
    co_await attack(asc(kDeadlyEnemies, 7, 6));
    co_await applyToSelf<StrengthPower>(3);
  }
};

// ================================================================ Toadpole

struct Toadpole : Monster {
  MONSTER_HEADER(Toadpole, "TOADPOLE")
  bool isFront = false;
  int minHp() const override { return asc(kToughEnemies, 22, 21); }
  int maxHp() const override { return asc(kToughEnemies, 26, 25); }
  void buildMoves() override {
    auto* spit = machine.add<MoveState>("SPIKE_SPIT_MOVE");
    spit->perform = [this](Targets) { return spikeSpit(); };
    spit->intents = {attackIntent(asc(kDeadlyEnemies, 4, 3), 3)};
    auto* whirl = machine.add<MoveState>("WHIRL_MOVE");
    whirl->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 8, 7)); };
    whirl->intents = {attackIntent(asc(kDeadlyEnemies, 8, 7))};
    auto* spiken = machine.add<MoveState>("SPIKEN_MOVE");
    spiken->perform = [this](Targets) { return applyById(this, "ThornsPower", 2); };
    spiken->intents = {kindIntent(Intent::Buff)};
    auto* init = machine.add<ConditionalBranchState>("INIT_MOVE");
    whirl->followUp = spiken;
    spiken->followUp = spit;
    spit->followUp = whirl;
    init->add(whirl, [this] { return !isFront; });
    init->add(spiken, [this] { return isFront; });
    machine.start(init);
  }
  Task<> spikeSpit() {
    co_await applyById(this, "ThornsPower", -2);
    co_await attack(asc(kDeadlyEnemies, 4, 3), 3);
  }
};

// ================================================================ Two-Tailed Rat

struct TwoTailedRat : Monster {
  MONSTER_HEADER(TwoTailedRat, "TWO_TAILED_RAT")
  static constexpr int kSlotCount = 5;  // TwoTailedRatsNormal.Slots: first..fifth
  int starterMoveIndex = -1;
  int turnsUntilSummonable = 2;
  int callForBackupCount = 0;  // shared by every rat in the fight (see callForBackup)
  int slot = -1;               // index into the encounter's slots
  int minHp() const override { return asc(kToughEnemies, 18, 17); }
  int maxHp() const override { return asc(kToughEnemies, 22, 21); }

  static TwoTailedRat* asRat(Creature* e) {
    if (e->removed || !e->monster || e->monster->id != "TwoTailedRat") return nullptr;
    return static_cast<TwoTailedRat*>(e->monster.get());
  }
  // EncounterModel.GetNextSlot (last = false: first free slot) / Slots.LastOrDefault(free).
  static int freeSlot(Combat* c, bool last) {
    int found = -1;
    for (int s = 0; s < kSlotCount; ++s) {
      bool taken = false;
      for (auto* e : c->enemies) {
        TwoTailedRat* r = asRat(e);
        if (r && r->slot == s) taken = true;
      }
      if (!taken) {
        found = s;
        if (!last) break;
      }
    }
    return found;
  }
  bool canSummon() {
    if (turnsUntilSummonable > 0) return false;
    if (callForBackupCount >= 3) return false;
    if (freeSlot(combat, false) < 0) return false;
    for (auto* e : combat->enemies) {
      if (e == creature || e->removed || !e->monster) continue;
      if (e->monster->nextMove && e->monster->nextMove->id == "CALL_FOR_BACKUP_MOVE") return false;
    }
    return true;
  }
  void buildMoves() override {
    auto* scratch = machine.add<MoveState>("SCRATCH_MOVE");
    scratch->perform = [this](Targets) { return scratchMove(); };
    scratch->intents = {attackIntent(asc(kDeadlyEnemies, 9, 8))};
    auto* bite = machine.add<MoveState>("DISEASE_BITE_MOVE");
    bite->perform = [this](Targets) { return biteMove(); };
    bite->intents = {attackIntent(asc(kDeadlyEnemies, 7, 6))};
    auto* screech = machine.add<MoveState>("SCREECH_MOVE");
    screech->perform = [this](Targets t) { return screechMove(t); };
    screech->intents = {kindIntent(Intent::Debuff)};
    auto* backup = machine.add<MoveState>("CALL_FOR_BACKUP_MOVE");
    backup->perform = [this](Targets) { return callForBackup(); };
    backup->intents = {kindIntent(Intent::Summon)};
    auto* rand = machine.add<RandomBranchState>("RAND");
    scratch->followUp = rand;
    bite->followUp = rand;
    screech->followUp = rand;
    backup->followUp = rand;
    auto normalW = [this] { return canSummon() ? 1.f / 12.f : 1.f; };
    rand->branches.push_back({scratch->id, MoveRepeat::CannotRepeat, 0, 0, normalW});
    rand->branches.push_back({bite->id, MoveRepeat::CannotRepeat, 0, 0, normalW});
    rand->branches.push_back({screech->id, MoveRepeat::CannotRepeat, 0, 3, normalW});
    rand->branches.push_back({backup->id, MoveRepeat::UseOnlyOnce, 0, 0, [this] { return canSummon() ? 0.75f : 0.f; }});
    MonsterState* start = rand;
    if (starterMoveIndex != -1) {
      int k = starterMoveIndex % 3;
      start = k == 0 ? static_cast<MonsterState*>(scratch) : k == 1 ? static_cast<MonsterState*>(bite) : screech;
    }
    machine.start(start);
  }
  Task<> scratchMove() {
    --turnsUntilSummonable;
    co_await attack(asc(kDeadlyEnemies, 9, 8));
  }
  Task<> biteMove() {
    --turnsUntilSummonable;
    co_await attack(asc(kDeadlyEnemies, 7, 6));
  }
  Task<> screechMove(Targets t) {
    --turnsUntilSummonable;
    co_await applyToTargets<FrailPower>(t, 1);
  }
  Task<> callForBackup() {
    if (combat->ending) co_return;  // IsLiveCombat
    int next = freeSlot(combat, true);
    if (next >= 0) {
      auto rat = std::make_unique<TwoTailedRat>();
      rat->slot = next;
      co_await cmd::addMonster(*combat, std::move(rat));
    }
    int maxCount = 0;
    for (auto* e : combat->enemies)
      if (TwoTailedRat* r = asRat(e)) maxCount = std::max(maxCount, r->callForBackupCount + 1);
    for (auto* e : combat->enemies)
      if (TwoTailedRat* r = asRat(e)) r->callForBackupCount = maxCount;
  }
};

}  // namespace

// ================================================================ encounters

void registerUnderdocksB() {
  db::registerEncounter("SeapunkNormal", RoomType::Monster, false, [](Rng&) {
    auto v = list<Seapunk>();
    v.insert(v.begin(), makeCalcifiedCultist());  // content_underdocks_a.cpp
    return v;
  });
  db::registerEncounter("SeapunkWeak", RoomType::Monster, true, [](Rng&) { return list<Seapunk>(); });
  db::registerEncounter("SewerClamNormal", RoomType::Monster, false, [](Rng&) { return list<SewerClam>(); });
  db::registerEncounter("SludgeSpinnerWeak", RoomType::Monster, true, [](Rng&) { return list<SludgeSpinner>(); });
  db::registerEncounter("ToadpolesWeak", RoomType::Monster, true, [](Rng&) {
    auto front = std::make_unique<Toadpole>();
    front->isFront = true;
    auto back = std::make_unique<Toadpole>();
    back->isFront = false;
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(std::move(front));
    v.push_back(std::move(back));
    return v;
  });
  db::registerEncounter("TwoTailedRatsNormal", RoomType::Monster, false, [](Rng& rng) {
    auto a = std::make_unique<TwoTailedRat>();
    auto b = std::make_unique<TwoTailedRat>();
    auto c = std::make_unique<TwoTailedRat>();
    int n = rng.nextInt(3);
    a->starterMoveIndex = n;
    b->starterMoveIndex = (n + 1) % 3;
    c->starterMoveIndex = (n + 2) % 3;
    a->slot = 2;  // Slots[2..4]: third, fourth, fifth
    b->slot = 3;
    c->slot = 4;
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(std::move(a));
    v.push_back(std::move(b));
    v.push_back(std::move(c));
    return v;
  });
}

}  // namespace sts
