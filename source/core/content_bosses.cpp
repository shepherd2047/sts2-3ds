// Act 1 fights that need the newer engine features (summons, minions, stuns,
// illusions): Ceremonial Beast, The Kin, Fogmog.
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

// ================================================================ cards / powers

struct Dazed : IroncladT<Dazed> {
  CARD_HEADER(Dazed, "DAZED", -1, Status, Status, None)
    keywords = kwEthereal | kwUnplayable;
    maxUpgradeLevel = 0;
  }
};

struct MinionPower : Power {
  POWER_HEADER(MinionPower, "MINION_POWER")
  StackType stackType() const override { return StackType::Single; }
  bool ownerIsSecondaryEnemy() const override { return true; }
  bool removedAfterOwnerDeath() const override { return false; }
};

// Ringing affliction on every card: once a card has been played this turn, the rest
// cannot be. PORT NOTE: the per-card Ringing affliction (and its card overlay) is folded
// into the power; every card the player owns is afflicted while it lasts, as in C#.
struct RingingPower : Power {
  POWER_HEADER(RingingPower, "RINGING_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  StackType stackType() const override { return StackType::Single; }
  bool shouldPlay(Card*) override { return !owner->combat || owner->combat->cardsPlayedThisTurn == 0; }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) { flash = 1.f; co_await cmd::removePower(this); }
  }
};

struct CeremonialBeast;

// Below its amount of HP the Beast loses its Strength and is stunned.
struct PlowPower : Power {
  POWER_HEADER(PlowPower, "PLOW_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  Task<> afterDamageReceived(Creature* target, const DamageResult& r, int, Creature*, Card*) override;
};

// Illusions (EyeWithTeeth) die without leaving, keep their buffs and revive at full HP
// on their next turn. They are minions, so they vanish with the last primary enemy.
struct IllusionPower : Power {
  POWER_HEADER(IllusionPower, "ILLUSION_POWER")
  StackType stackType() const override { return StackType::Single; }
  std::string followUpStateId;
  bool shouldPowerBeRemovedOnDeath(Power* p) override { return p->type() == PowerType::Debuff; }
  bool shouldCreatureBeRemovedFromCombatAfterDeath(Creature* c) override { return c != owner; }
  Task<> afterApplied(Creature*, Card*) override {
    if (!owner->get<MinionPower>()) co_await applyPower<MinionPower>(owner, 1, nullptr, nullptr, true);
  }
  Task<> afterDeath(Creature* c) override {
    if (c != owner || !owner->monster) co_return;
    Monster* m = owner->monster.get();
    auto s = std::make_unique<MoveState>();
    s->id = "REVIVE_MOVE";
    Creature* o = owner;
    s->perform = [o](Targets) -> Task<> { co_await cmd::heal(o, o->maxHp - o->hp); };
    s->intents = {kindIntent(Intent::Heal)};
    s->followUpId = !followUpStateId.empty() ? followUpStateId
                    : (!m->machine.stateLog.empty() ? m->machine.stateLog.back()->id : std::string());
    s->mustPerformOnce = true;
    MoveState* raw = s.get();
    m->machine.transient.push_back(std::move(s));
    m->setMoveImmediate(raw);
    co_return;
  }
};

// ================================================================ Ceremonial Beast

struct CeremonialBeast : Monster {
  MONSTER_HEADER(CeremonialBeast, "CEREMONIAL_BEAST")
  bool stunnedByPlowRemoval = false, secondPhase = false;
  MoveState* beastCry = nullptr;
  int minHp() const override { return asc(kToughEnemies, 262, 252); }
  int maxHp() const override { return minHp(); }
  void buildMoves() override {
    auto* stamp = machine.add<MoveState>("STAMP_MOVE");
    stamp->perform = [this](Targets) { return applyToSelf<PlowPower>(asc(kDeadlyEnemies, 160, 150)); };
    stamp->intents = {kindIntent(Intent::Buff)};
    auto* plow = machine.add<MoveState>("PLOW_MOVE");
    plow->perform = [this](Targets) { return plowMove(); };
    plow->intents = {attackIntent(asc(kDeadlyEnemies, 20, 18)), kindIntent(Intent::Buff)};
    auto* stunState = machine.add<MoveState>("STUN_MOVE");  // bestiary only; stuns go through Monster::stun
    stunState->perform = [this](Targets t) { return stunnedMove(t); };
    stunState->intents = {kindIntent(Intent::Stun)};
    stunState->mustPerformOnce = true;
    beastCry = machine.add<MoveState>("BEAST_CRY_MOVE");
    beastCry->perform = [this](Targets t) { return applyToTargets<RingingPower>(t, 1); };
    beastCry->intents = {kindIntent(Intent::Debuff)};
    auto* stomp = machine.add<MoveState>("STOMP_MOVE");
    stomp->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 17, 15)); };
    stomp->intents = {attackIntent(asc(kDeadlyEnemies, 17, 15))};
    auto* crush = machine.add<MoveState>("CRUSH_MOVE");
    crush->perform = [this](Targets) { return crushMove(); };
    crush->intents = {attackIntent(asc(kDeadlyEnemies, 19, 17)), kindIntent(Intent::Buff)};
    stamp->followUp = plow;
    plow->followUp = plow;
    stunState->followUp = beastCry;
    beastCry->followUp = stomp;
    stomp->followUp = crush;
    crush->followUp = beastCry;
    machine.start(stamp);
  }
  Task<> plowMove() {
    co_await attack(asc(kDeadlyEnemies, 20, 18));
    co_await applyToSelf<StrengthPower>(2);
  }
  Task<> crushMove() {
    co_await attack(asc(kDeadlyEnemies, 19, 17));
    co_await applyToSelf<StrengthPower>(asc(kDeadlyEnemies, 4, 3));
  }
  Task<> setStunned() {
    stunnedByPlowRemoval = true;
    secondPhase = true;
    combat->push({VisualEvent::Anim, creature, 0, "Stun"});
    co_await wait(0.6);
  }
  Task<> stunnedMove(Targets) {
    stunnedByPlowRemoval = false;
    combat->push({VisualEvent::Anim, creature, 0, "Unstun"});
    co_await wait(0.6);
  }
};

Task<> PlowPower::afterDamageReceived(Creature* target, const DamageResult& r, int, Creature*, Card*) {
  if (target != owner || r.unblocked <= 0 || target->hp > amount) co_return;
  flash = 1.f;
  // PORT NOTE: TemporaryStrengthPower instances would be removed here too; monsters never
  // carry them in this build.
  if (Power* s = owner->get<StrengthPower>()) co_await cmd::removePower(s);
  // No RTTI on the 3DS build: identify the Beast by its model id.
  if (owner->monster && owner->monster->id == "CeremonialBeast") {
    auto* beast = static_cast<CeremonialBeast*>(owner->monster.get());
    co_await beast->setStunned();
    beast->stun([beast](Targets t) { return beast->stunnedMove(t); }, beast->beastCry->id);
  } else if (owner->monster) {
    owner->monster->stun();
  }
  co_await cmd::removePower(this);
}

// ================================================================ The Kin

struct KinFollower : Monster {
  MONSTER_HEADER(KinFollower, "KIN_FOLLOWER")
  bool startsWithDance = false;
  int minHp() const override { return asc(kToughEnemies, 62, 58); }
  int maxHp() const override { return asc(kToughEnemies, 63, 59); }
  Task<> afterAddedToRoom() override { co_await applyToSelf<MinionPower>(1); }
  void buildMoves() override {
    auto* slash = machine.add<MoveState>("QUICK_SLASH_MOVE");
    slash->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 5, 5)); };
    slash->intents = {attackIntent(asc(kDeadlyEnemies, 5, 5))};
    auto* boomerang = machine.add<MoveState>("BOOMERANG_MOVE");
    boomerang->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 2, 2), 2); };
    boomerang->intents = {attackIntent(asc(kDeadlyEnemies, 2, 2), 2)};
    auto* dance = machine.add<MoveState>("POWER_DANCE_MOVE");
    dance->perform = [this](Targets) { return applyToSelf<StrengthPower>(asc(kDeadlyEnemies, 3, 2)); };
    dance->intents = {kindIntent(Intent::Buff)};
    slash->followUp = boomerang;
    boomerang->followUp = dance;
    dance->followUp = slash;
    machine.start(startsWithDance ? dance : slash);
  }
};

struct KinPriest : Monster {
  MONSTER_HEADER(KinPriest, "KIN_PRIEST")
  int minHp() const override { return asc(kToughEnemies, 199, 190); }
  int maxHp() const override { return minHp(); }
  void buildMoves() override {
    auto* frailty = machine.add<MoveState>("ORB_OF_FRAILTY_MOVE");
    frailty->perform = [this](Targets t) { return orbMove<FrailPower>(t); };
    frailty->intents = {attackIntent(asc(kDeadlyEnemies, 9, 8)), kindIntent(Intent::Debuff)};
    auto* weakness = machine.add<MoveState>("ORB_OF_WEAKNESS_MOVE");
    weakness->perform = [this](Targets t) { return orbMove<WeakPower>(t); };
    weakness->intents = {attackIntent(asc(kDeadlyEnemies, 9, 8)), kindIntent(Intent::Debuff)};
    auto* beam = machine.add<MoveState>("BEAM_MOVE");
    beam->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 3, 3), 3); };
    beam->intents = {attackIntent(asc(kDeadlyEnemies, 3, 3), 3)};
    auto* ritual = machine.add<MoveState>("RITUAL_MOVE");
    ritual->perform = [this](Targets) { return applyToSelf<StrengthPower>(asc(kDeadlyEnemies, 3, 2)); };
    ritual->intents = {kindIntent(Intent::Buff)};
    frailty->followUp = weakness;
    weakness->followUp = beam;
    beam->followUp = ritual;
    ritual->followUp = frailty;
    machine.start(frailty);
  }
  template <class P> Task<> orbMove(std::vector<Creature*> targets) {
    co_await attack(asc(kDeadlyEnemies, 9, 8));
    co_await applyToTargets<P>(targets, 1);
  }
};

// ================================================================ Fogmog

struct EyeWithTeeth : Monster {
  MONSTER_HEADER(EyeWithTeeth, "EYE_WITH_TEETH")
  int minHp() const override { return 6; }
  int maxHp() const override { return minHp(); }
  Task<> afterAddedToRoom() override { co_await applyToSelf<IllusionPower>(1); }
  void buildMoves() override {
    auto* distract = machine.add<MoveState>("DISTRACT_MOVE");
    distract->perform = [this](Targets) { return cmd::addStatusCards(*combat, "Dazed", Pile::Discard, 3); };
    distract->intents = {kindIntent(Intent::Status, 3)};
    distract->followUp = distract;
    machine.start(distract);
  }
};

struct Fogmog : Monster {
  MONSTER_HEADER(Fogmog, "FOGMOG")
  int minHp() const override { return asc(kToughEnemies, 78, 74); }
  int maxHp() const override { return minHp(); }
  void buildMoves() override {
    auto* illusion = machine.add<MoveState>("ILLUSION_MOVE");
    illusion->perform = [this](Targets) { return illusionMove(); };
    illusion->intents = {kindIntent(Intent::Summon)};
    auto* swipe = machine.add<MoveState>("SWIPE_MOVE");
    swipe->perform = [this](Targets) { return swipeMove(); };
    swipe->intents = {attackIntent(asc(kDeadlyEnemies, 9, 8)), kindIntent(Intent::Buff)};
    auto* swipeRandom = machine.add<MoveState>("SWIPE_RANDOM_MOVE");
    swipeRandom->perform = [this](Targets) { return swipeMove(); };
    swipeRandom->intents = {attackIntent(asc(kDeadlyEnemies, 9, 8)), kindIntent(Intent::Buff)};
    auto* headbutt = machine.add<MoveState>("HEADBUTT_MOVE");
    headbutt->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 16, 14)); };
    headbutt->intents = {attackIntent(asc(kDeadlyEnemies, 16, 14))};
    auto* branch = machine.add<RandomBranchState>("BRANCH");
    branch->add(swipeRandom, MoveRepeat::CannotRepeat, 0.4f);
    branch->add(headbutt, MoveRepeat::CannotRepeat, 0.6f);
    illusion->followUp = swipe;
    swipe->followUp = branch;
    swipeRandom->followUp = headbutt;
    headbutt->followUp = swipe;
    machine.start(illusion);
  }
  Task<> illusionMove() {
    combat->push({VisualEvent::Anim, creature, 0, "Summon"});
    co_await wait(0.75);
    if (!combat->ending) co_await cmd::addMonster(*combat, std::make_unique<EyeWithTeeth>());
  }
  Task<> swipeMove() {
    co_await attack(asc(kDeadlyEnemies, 9, 8));
    co_await applyToSelf<StrengthPower>(1);
  }
};

// ================================================================ registry

void registerAct1Bosses() {
  db::registerCard("Dazed", [] { return std::unique_ptr<Card>(new Dazed()); });
  db::registerPower("MinionPower", [] { return std::unique_ptr<Power>(new MinionPower()); });
  db::registerPower("RingingPower", [] { return std::unique_ptr<Power>(new RingingPower()); });
  db::registerPower("PlowPower", [] { return std::unique_ptr<Power>(new PlowPower()); });
  db::registerPower("IllusionPower", [] { return std::unique_ptr<Power>(new IllusionPower()); });
  db::registerEncounter("CeremonialBeastBoss", RoomType::Boss, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(std::make_unique<CeremonialBeast>());
    return v;
  });
  db::registerEncounter("TheKinBoss", RoomType::Boss, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    auto dancer = std::make_unique<KinFollower>();
    dancer->startsWithDance = true;
    v.push_back(std::move(dancer));
    v.push_back(std::make_unique<KinFollower>());
    v.push_back(std::make_unique<KinPriest>());
    return v;
  });
  db::registerEncounter("FogmogNormal", RoomType::Monster, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(std::make_unique<Fogmog>());
    return v;
  });
}

}  // namespace sts
