// Act 2 (Hive) monsters, package 4a: Bowlbugs, Chompers, Exoskeletons, Myte,
// Spiny Toad, Tunneler (MegaCrit.Sts2.Core.Models.Monsters / .Powers / .Encounters).
// Values are the non-ascension ones (see docs/PORTING.md).
#include <algorithm>

#include "cards.h"
#include "powers.h"

namespace sts {

namespace {

#define MONSTER_HEADER(Name, Key) \
  Name() { id = #Name; locKey = Key; }

using Targets = const std::vector<Creature*>&;

Intent attackIntent(int dmg, int hits = 1) { Intent i; i.kind = Intent::Attack; i.damage = dmg; i.hits = hits; return i; }
Intent kindIntent(Intent::Kind k, int count = 0) { Intent i; i.kind = k; i.count = count; return i; }

// ================================================================ cards / powers

struct Toxic : IroncladT<Toxic> {
  CARD_HEADER(Toxic, "TOXIC", 1, Status, Status, None)
    keywords = kwExhaust;
    maxUpgradeLevel = 0;
    addVar("Damage", 5);
  }
  bool hasTurnEndInHandEffect() const override { return true; }
  Task<> onTurnEndInHand() override { co_await cmd::damage(me(), val("Damage"), kUnpowered | kMove, nullptr, this); }
};

// PORT NOTE: Hook.ModifyDamageCap caps the damage before block; this build has no cap hook,
// so the cap is applied to the HP lost after block (identical while the owner has no block,
// which holds for Exoskeletons).
struct HardToKillPower : Power {
  POWER_HEADER(HardToKillPower, "HARD_TO_KILL_POWER")
  Dec modifyHpLostAfterOsty(Creature* target, Dec amount, int, Creature*, Card*) override {
    if (target != owner) return amount;
    if (amount > Dec(this->amount)) { flash = 1.f; return Dec(this->amount); }
    return amount;
  }
};

// PORT NOTE: BeforeDamageReceived is not a hook here; the reflected damage is dealt in
// afterDamageReceived instead (not at all when the hit kills the owner).
struct ThornsPower : Power {
  POWER_HEADER(ThornsPower, "THORNS_POWER")
  Task<> afterDamageReceived(Creature* target, const DamageResult&, int props, Creature* dealer, Card*) override {
    if (target != owner || !dealer || dealer->dead() || !isPoweredAttack(props)) co_return;
    flash = 1.f;
    co_await cmd::damage(dealer, amount, kUnpowered, owner, nullptr);
  }
};

// The Bowlbug Rock loses its footing when the player fully blocks one of its attacks.
struct ImbalancedPower : Power {
  POWER_HEADER(ImbalancedPower, "IMBALANCED_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  StackType stackType() const override { return StackType::Single; }
  Task<> afterDamageReceived(Creature*, const DamageResult& r, int, Creature* dealer, Card*) override;
};

// The Tunneler keeps its block while burrowed; breaking it stuns the Tunneler.
struct BurrowedPower : Power {
  POWER_HEADER(BurrowedPower, "BURROWED_POWER")
  StackType stackType() const override { return StackType::Single; }
  bool shouldClearBlock(Creature* c) override { return c != owner; }
  Task<> afterDamageReceived(Creature* target, const DamageResult& r, int, Creature*, Card*) override;
  Task<> afterRemoved(Creature* oldOwner) override {
    oldOwner->block = 0;  // CreatureCmd.LoseBlock(999999999)
    co_return;
  }
};

// ================================================================ monsters

struct BowlbugEgg : Monster {
  MONSTER_HEADER(BowlbugEgg, "BOWLBUG_EGG")
  int minHp() const override { return 21; }
  int maxHp() const override { return 22; }
  void buildMoves() override {
    auto* bite = machine.add<MoveState>("BITE_MOVE");
    bite->perform = [this](Targets) { return biteMove(); };
    bite->intents = {attackIntent(7), kindIntent(Intent::Defend)};
    bite->followUp = bite;
    machine.start(bite);
  }
  Task<> biteMove() {
    co_await attack(7);
    co_await gainBlock(7);
  }
};

struct BowlbugSilk : Monster {
  MONSTER_HEADER(BowlbugSilk, "BOWLBUG_SILK")
  int minHp() const override { return 40; }
  int maxHp() const override { return 43; }
  void buildMoves() override {
    auto* thrash = machine.add<MoveState>("THRASH_MOVE");
    thrash->perform = [this](Targets) { return attack(4, 2); };
    thrash->intents = {attackIntent(4, 2)};
    auto* spit = machine.add<MoveState>("TOXIC_SPIT_MOVE");
    spit->perform = [this](Targets t) { return applyToTargets<WeakPower>(t, 1); };
    spit->intents = {kindIntent(Intent::Debuff)};
    thrash->followUp = spit;
    spit->followUp = thrash;
    machine.start(spit);
  }
};

struct BowlbugNectar : Monster {
  MONSTER_HEADER(BowlbugNectar, "BOWLBUG_NECTAR")
  int minHp() const override { return 35; }
  int maxHp() const override { return 38; }
  void buildMoves() override {
    auto* thrash = machine.add<MoveState>("THRASH_MOVE");
    thrash->perform = [this](Targets) { return attack(3); };
    thrash->intents = {attackIntent(3)};
    auto* buff = machine.add<MoveState>("BUFF_MOVE");
    buff->perform = [this](Targets) { return applyToSelf<StrengthPower>(15); };
    buff->intents = {kindIntent(Intent::Buff)};
    auto* thrash2 = machine.add<MoveState>("THRASH2_MOVE");
    thrash2->perform = [this](Targets) { return attack(3); };
    thrash2->intents = {attackIntent(3)};
    thrash->followUp = buff;
    buff->followUp = thrash2;
    thrash2->followUp = thrash2;
    machine.start(thrash);
  }
};

struct BowlbugRock : Monster {
  MONSTER_HEADER(BowlbugRock, "BOWLBUG_ROCK")
  bool isOffBalance = false;
  int minHp() const override { return 45; }
  int maxHp() const override { return 48; }
  Task<> afterAddedToRoom() override { co_await applyToSelf<ImbalancedPower>(1); }
  void buildMoves() override {
    auto* headbutt = machine.add<MoveState>("HEADBUTT_MOVE");
    headbutt->perform = [this](Targets) { return headbuttMove(); };
    headbutt->intents = {attackIntent(15)};
    auto* dizzy = machine.add<MoveState>("DIZZY_MOVE");
    dizzy->perform = [this](Targets) { return dizzyMove(); };
    dizzy->intents = {kindIntent(Intent::Stun)};
    auto* post = machine.add<ConditionalBranchState>("POST_HEADBUTT");
    headbutt->followUp = post;
    dizzy->followUp = headbutt;
    post->add(dizzy, [this] { return isOffBalance; });
    post->add(headbutt, [this] { return !isOffBalance; });
    machine.start(headbutt);
  }
  Task<> headbuttMove() {
    co_await attack(15);
    if (isOffBalance) stun([this](Targets) { return dizzyMove(); });
  }
  Task<> dizzyMove() {
    isOffBalance = false;
    combat->push({VisualEvent::Anim, creature, 0, "Unstun"});
    co_return;
  }
};

Task<> ImbalancedPower::afterDamageReceived(Creature*, const DamageResult& r, int, Creature* dealer, Card*) {
  if (dealer != owner || !r.fullyBlocked) co_return;
  flash = 1.f;
  if (owner->monster && owner->monster->id == "BowlbugRock") static_cast<BowlbugRock*>(owner->monster.get())->isOffBalance = true;
  else if (owner->monster) owner->monster->stun();
}

struct Chomper : Monster {
  MONSTER_HEADER(Chomper, "CHOMPER")
  bool screamFirst = false;
  int minHp() const override { return 60; }
  int maxHp() const override { return 64; }
  Task<> afterAddedToRoom() override {
    co_await cmd::applyPower(db::power("ArtifactPower"), creature, 2, creature, nullptr);
  }
  void buildMoves() override {
    auto* clamp = machine.add<MoveState>("CLAMP_MOVE");
    clamp->perform = [this](Targets) { return attack(8, 2); };
    clamp->intents = {attackIntent(8, 2)};
    auto* screech = machine.add<MoveState>("SCREECH_MOVE");
    screech->perform = [this](Targets) { return cmd::addStatusCards(*combat, "Dazed", Pile::Discard, 3); };
    screech->intents = {kindIntent(Intent::Status, 3)};
    clamp->followUp = screech;
    screech->followUp = clamp;
    machine.start(screamFirst ? screech : clamp);
  }
};

struct Exoskeleton : Monster {
  MONSTER_HEADER(Exoskeleton, "EXOSKELETON")
  std::string slot;  // "first".."fourth": the slot picks the opening move
  int minHp() const override { return 24; }
  int maxHp() const override { return 28; }
  Task<> afterAddedToRoom() override { co_await applyToSelf<HardToKillPower>(9); }
  void buildMoves() override {
    auto* skitter = machine.add<MoveState>("SKITTER_MOVE");
    skitter->perform = [this](Targets) { return attack(1, 3); };
    skitter->intents = {attackIntent(1, 3)};
    auto* mandibles = machine.add<MoveState>("MANDIBLES_MOVE");
    mandibles->perform = [this](Targets) { return attack(8); };
    mandibles->intents = {attackIntent(8)};
    auto* enrage = machine.add<MoveState>("ENRAGE_MOVE");
    enrage->perform = [this](Targets) { return applyToSelf<StrengthPower>(2); };
    enrage->intents = {kindIntent(Intent::Buff)};
    auto* rand = machine.add<RandomBranchState>("RAND");
    rand->add(skitter, MoveRepeat::CannotRepeat);
    rand->add(mandibles, MoveRepeat::CannotRepeat);
    auto* init = machine.add<ConditionalBranchState>("INIT_MOVE");
    init->add(skitter, [this] { return slot == "first"; });
    init->add(mandibles, [this] { return slot == "second"; });
    init->add(enrage, [this] { return slot == "third"; });
    init->add(rand, [this] { return slot == "fourth"; });
    skitter->followUp = rand;
    mandibles->followUp = enrage;
    enrage->followUp = rand;
    machine.start(init);
  }
};

struct Myte : Monster {
  MONSTER_HEADER(Myte, "MYTE")
  std::string slot;
  int minHp() const override { return 61; }
  int maxHp() const override { return 67; }
  void buildMoves() override {
    auto* toxic = machine.add<MoveState>("TOXIC_MOVE");
    toxic->perform = [this](Targets) { return cmd::addStatusCards(*combat, "Toxic", Pile::Hand, 2); };
    toxic->intents = {kindIntent(Intent::Status, 2)};
    auto* bite = machine.add<MoveState>("BITE_MOVE");
    bite->perform = [this](Targets) { return attack(13); };
    bite->intents = {attackIntent(13)};
    auto* suck = machine.add<MoveState>("SUCK_MOVE");
    suck->perform = [this](Targets) { return suckMove(); };
    suck->intents = {attackIntent(4), kindIntent(Intent::Buff)};
    auto* init = machine.add<ConditionalBranchState>("INIT_MOVE");
    init->add(toxic, [this] { return slot == "first"; });
    init->add(suck, [this] { return slot == "second"; });
    toxic->followUp = bite;
    bite->followUp = suck;
    suck->followUp = toxic;
    machine.start(init);
  }
  Task<> suckMove() {
    co_await attack(4);
    co_await applyToSelf<StrengthPower>(2);
  }
};

struct SpinyToad : Monster {
  MONSTER_HEADER(SpinyToad, "SPINY_TOAD")
  bool isSpiny = false;
  int minHp() const override { return 116; }
  int maxHp() const override { return 119; }
  void buildMoves() override {
    auto* spikes = machine.add<MoveState>("PROTRUDING_SPIKES_MOVE");
    spikes->perform = [this](Targets) { return spikesMove(); };
    spikes->intents = {kindIntent(Intent::Buff)};
    auto* explosion = machine.add<MoveState>("SPIKE_EXPLOSION_MOVE");
    explosion->perform = [this](Targets) { return explosionMove(); };
    explosion->intents = {attackIntent(23)};
    auto* lash = machine.add<MoveState>("TONGUE_LASH_MOVE");
    lash->perform = [this](Targets) { return attack(17); };
    lash->intents = {attackIntent(17)};
    spikes->followUp = explosion;
    explosion->followUp = lash;
    lash->followUp = spikes;
    machine.start(spikes);
  }
  Task<> spikesMove() {
    isSpiny = true;
    combat->push({VisualEvent::Anim, creature, 0, "Spiked"});
    co_await applyToSelf<ThornsPower>(5);
  }
  Task<> explosionMove() {
    isSpiny = false;
    combat->push({VisualEvent::Anim, creature, 0, "Unspiked"});
    co_await attack(23);
    co_await applyToSelf<ThornsPower>(-5);
  }
};

struct Tunneler : Monster {
  MONSTER_HEADER(Tunneler, "TUNNELER")
  int minHp() const override { return 87; }
  int maxHp() const override { return 87; }
  void buildMoves() override {
    auto* bite = machine.add<MoveState>("BITE_MOVE");
    bite->perform = [this](Targets) { return attack(13); };
    bite->intents = {attackIntent(13)};
    auto* burrow = machine.add<MoveState>("BURROW_MOVE");
    burrow->perform = [this](Targets) { return burrowMove(); };
    burrow->intents = {kindIntent(Intent::Buff), kindIntent(Intent::Defend)};
    auto* below = machine.add<MoveState>("BELOW_MOVE");
    below->perform = [this](Targets) { return attack(23); };
    below->intents = {attackIntent(23)};
    auto* dizzy = machine.add<MoveState>("DIZZY_MOVE");
    dizzy->perform = [this](Targets) { return stillDizzyMove(); };
    dizzy->intents = {kindIntent(Intent::Stun)};
    bite->followUp = burrow;
    burrow->followUp = below;
    below->followUp = below;
    dizzy->followUp = bite;
    machine.start(bite);
  }
  Task<> burrowMove() {
    co_await applyToSelf<BurrowedPower>(1);
    co_await gainBlock(32);
  }
  Task<> stillDizzyMove() {
    combat->push({VisualEvent::Anim, creature, 0, "WakeUp"});
    co_return;
  }
};

Task<> BurrowedPower::afterDamageReceived(Creature* target, const DamageResult& r, int, Creature*, Card*) {
  if (target != owner || !r.blockBroken) co_return;
  if (owner->monster && owner->monster->id == "Tunneler") {
    auto* t = static_cast<Tunneler*>(owner->monster.get());
    t->stun([t](Targets) { return t->stillDizzyMove(); }, "BITE_MOVE");
    co_await cmd::removePower(this);
  }
}

// ================================================================ encounters

template <class M> std::unique_ptr<Monster> mk() { return std::make_unique<M>(); }
template <class M> std::unique_ptr<Monster> mkSlot(const char* slot) {
  auto m = std::make_unique<M>();
  m->slot = slot;
  return m;
}

// Encounter monsters by index into a name list (used for the random worker picks).
std::unique_ptr<Monster> mkWorker(const std::string& id) {
  if (id == "BowlbugEgg") return mk<BowlbugEgg>();
  if (id == "BowlbugSilk") return mk<BowlbugSilk>();
  return mk<BowlbugNectar>();
}

}  // namespace

// Used by content_act2b.cpp (Slumbering Beetle encounter).
std::unique_ptr<Monster> makeAct2Monster(const std::string& id) {
  if (id == "BowlbugRock") return mk<BowlbugRock>();
  return mkWorker(id);
}

void registerAct2A() {
  db::registerCard("Toxic", [] { return std::unique_ptr<Card>(new Toxic()); });
  db::registerPower(HardToKillPower::kId, [] { return std::unique_ptr<Power>(new HardToKillPower()); });
  db::registerPower(ThornsPower::kId, [] { return std::unique_ptr<Power>(new ThornsPower()); });
  db::registerPower(ImbalancedPower::kId, [] { return std::unique_ptr<Power>(new ImbalancedPower()); });
  db::registerPower(BurrowedPower::kId, [] { return std::unique_ptr<Power>(new BurrowedPower()); });

  // Rock first, then two distinct workers (each of Egg/Silk/Nectar may appear once).
  db::registerEncounter("BowlbugsNormal", RoomType::Monster, false, [](Rng& rng) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(mk<BowlbugRock>());
    std::vector<std::string> items = {"BowlbugEgg", "BowlbugSilk", "BowlbugNectar"};
    for (int i = 0; i < 2; ++i) {
      std::string pick = rng.nextItem(items);
      items.erase(std::find(items.begin(), items.end(), pick));
      v.push_back(mkWorker(pick));
    }
    return v;
  });
  db::registerEncounter("BowlbugsWeak", RoomType::Monster, true, [](Rng& rng) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(mk<BowlbugRock>());
    std::vector<std::string> bugs = {"BowlbugEgg", "BowlbugNectar"};
    v.push_back(mkWorker(rng.nextItem(bugs)));
    return v;
  });
  db::registerEncounter("ChompersNormal", RoomType::Monster, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(mk<Chomper>());
    auto second = std::make_unique<Chomper>();
    second->screamFirst = true;
    v.push_back(std::move(second));
    return v;
  });
  db::registerEncounter("ExoskeletonsNormal", RoomType::Monster, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    for (const char* s : {"first", "second", "third", "fourth"}) v.push_back(mkSlot<Exoskeleton>(s));
    return v;
  });
  db::registerEncounter("ExoskeletonsWeak", RoomType::Monster, true, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    for (const char* s : {"first", "second", "third"}) v.push_back(mkSlot<Exoskeleton>(s));
    return v;
  });
  db::registerEncounter("MytesNormal", RoomType::Monster, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(mkSlot<Myte>("first"));
    v.push_back(mkSlot<Myte>("second"));
    return v;
  });
  db::registerEncounter("SpinyToadNormal", RoomType::Monster, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(mk<SpinyToad>());
    return v;
  });
  db::registerEncounter("TunnelerWeak", RoomType::Monster, true, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(mk<Tunneler>());
    return v;
  });
}

}  // namespace sts
