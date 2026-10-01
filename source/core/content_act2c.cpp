// Act 2 (Hive) elites and bosses, package 4c: Decimillipede, Entomancer, Infested Prisms,
// The Insatiable, Knowledge Demon, Kaiser Crab (MegaCrit.Sts2.Core.Models.Monsters /
// .Powers / .Cards / .Encounters). Values are the non-ascension ones (see docs/PORTING.md).
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

// CardPileCmd.AddGeneratedCardToCombat(card, PileType.Draw, null, CardPilePosition.Random).
Task<> addToDrawRandom(Combat& c, const char* cardId) {
  Card* card = c.addCard(db::card(cardId));
  card->createdByPlayer = false;  // creator null
  c.removeFromPiles(card);
  int at = c.rng("Shuffle").nextInt((int)c.draw.size() + 1);
  c.draw.insert(c.draw.begin() + at, card);
  for (Model* m : c.listeners()) co_await m->afterCardEnteredCombat(card);
}

// ================================================================ powers

// Every time the owner takes powered attack damage, Dazed cards are shuffled into the
// attacker's draw pile.
struct PersonalHivePower : Power {
  POWER_HEADER(PersonalHivePower, "PERSONAL_HIVE_POWER")
  Task<> afterDamageReceived(Creature* target, const DamageResult&, int props, Creature* dealer, Card*) override {
    if (target != owner || !dealer || !isPoweredAttack(props) || !owner->combat) co_return;
    flash = 1.f;
    for (int i = 0; i < amount; ++i) co_await addToDrawRandom(*owner->combat, "Dazed");
  }
};

// After the player plays a Skill, its next enemy-turn attacks deal more damage
// (Tainted, stacking with each Skill played).
struct TaintedPower : Power {
  POWER_HEADER(TaintedPower, "TAINTED_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  Dec modifyDamageAdditive(Creature* target, Dec, int props, Creature*, Card*) override {
    if (target != owner || !isPoweredAttack(props)) return 0;
    return Dec(amount);
  }
  Task<> afterSideTurnEnd(Side side, const std::vector<Creature*>&) override {
    if (side != Side::Enemy) co_return;
    flash = 1.f;
    co_await cmd::removePower(this);
  }
};

// VitalSparkPower.cs: every Skill card of the player is Tainted (A4 afflictions; BeforeCombatStart,
// AfterCardEnteredCombat); playing a Tainted card applies TaintedPower. The afflictions follow
// the power's amount and go with it.
struct VitalSparkPower : Power {
  POWER_HEADER(VitalSparkPower, "VITAL_SPARK_POWER")
  Task<> beforeCombatStart() override {
    for (Card* c : owner->combat->allCards())
      if (c->type == CardType::Skill) cmd::afflict(c, "Tainted", amount);
    co_return;
  }
  Task<> afterCardEnteredCombat(Card* c) override {
    if (!c->affliction && c->type == CardType::Skill) cmd::afflict(c, "Tainted", amount);
    co_return;
  }
  Task<> afterCardPlayed(const CardPlay& play) override {
    if (!play.card || !play.card->afflictedWith("Tainted") || !owner->combat) co_return;
    flash = 1.f;
    co_await cmd::applyPower(std::make_unique<TaintedPower>(), owner->combat->player, amount, nullptr, nullptr);
  }
  Task<> afterRemoved(Creature* oldOwner) override {
    if (oldOwner->combat)
      for (Card* c : oldOwner->combat->allCards())
        if (c->afflictedWith("Tainted")) cmd::clearAffliction(c);
    co_return;
  }
  Task<> afterPowerAmountChanged(Power* p, Dec, Creature*, Card*) override {
    if (p != this || !owner->combat) co_return;
    for (Card* c : owner->combat->allCards())
      if (c->afflictedWith("Tainted")) c->affliction->amount = amount;
  }
};

// Decimillipede segments do not die while another segment lives: they wither, then
// reattach with `amount` HP two turns later. Killing the last segment kills them all.
struct ReattachPower : Power {
  POWER_HEADER(ReattachPower, "REATTACH_POWER")
  StackType stackType() const override { return StackType::Single; }
  bool removedAfterOwnerDeath() const override { return false; }
  bool shouldCreatureBeRemovedFromCombatAfterDeath(Creature* c) override { return c != owner; }
  // Killing a segment only counts as fatal (Feed, ...) once every other segment is dead.
  bool shouldOwnerDeathTriggerFatal() const override { return allOtherSegmentsDead(); }
  bool allOtherSegmentsDead() const {
    if (!owner->combat) return true;
    for (Creature* e : owner->combat->enemies)
      if (e != owner && e->get<ReattachPower>() && e->alive()) return false;
    return true;
  }
  // Executed when the segment is reattaching at the end of its turn.
  Task<> doReattach() {
    if (allOtherSegmentsDead()) co_return;
    co_await cmd::heal(owner, amount);
  }
  Task<> afterDeath(Creature* c) override {
    if (c != owner || allOtherSegmentsDead() || !owner->monster) co_return;
    auto it = owner->monster->machine.states.find("DEAD_MOVE");
    if (it != owner->monster->machine.states.end())
      owner->monster->setMoveImmediate(static_cast<MoveState*>(it->second.get()));
  }
  // ShouldAllowHitting: no powers while reviving (IsReviving: from its death until it reattaches).
  bool shouldAllowHitting(Creature* c) override { return c != owner || owner->alive(); }
};

// The Insatiable's sandpit: the player has `amount` enemy turns to escape (each Frantic
// Escape played adds one); at 0 the player is eaten.
struct SandpitPower : Power {
  POWER_HEADER(SandpitPower, "SANDPIT_POWER")
  Creature* target = nullptr;
  Task<> afterSideTurnStartLate(Side side, const std::vector<Creature*>&) override {
    if (side == Side::Enemy) co_await cmd::decrement(this);
  }
  Task<> afterRemoved(Creature* oldOwner) override {
    if (oldOwner->dead() || !target || target->dead()) co_return;
    co_await wait(0.5);
    co_await cmd::kill({target});
  }
};

struct BackAttackLeftPower : Power {
  POWER_HEADER(BackAttackLeftPower, "BACK_ATTACK_LEFT_POWER")
  StackType stackType() const override { return StackType::Single; }
};

struct BackAttackRightPower : Power {
  POWER_HEADER(BackAttackRightPower, "BACK_ATTACK_RIGHT_POWER")
  StackType stackType() const override { return StackType::Single; }
};

// When the other crab claw dies, the survivor enrages: +6 Strength and 99 Block.
struct CrabRagePower : Power {
  POWER_HEADER(CrabRagePower, "CRAB_RAGE_POWER")
  StackType stackType() const override { return StackType::Single; }
  Task<> afterDeath(Creature* c) override {
    if (c == owner || c->side != owner->side || owner->dead()) co_return;
    flash = 1.f;
    co_await applyPower<StrengthPower>(owner, 6, owner, nullptr);
    co_await cmd::gainBlock(owner, 99, kUnpowered, nullptr);
    co_await cmd::removePower(this);
  }
};

// Kaiser Crab: the player is flanked. The claws hit for 1.5x while the player is
// facing away from them; targeting a claw turns the player towards it.
// PORT NOTE: the body flip and the "kaiser_crab_direction" music parameter are dropped.
struct SurroundedPower : Power {
  POWER_HEADER(SurroundedPower, "SURROUNDED_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  StackType stackType() const override { return StackType::Single; }
  bool facingLeft = false;  // Direction.Right by default
  Dec modifyDamageMultiplicative(Creature* target, Dec, int, Creature* dealer, Card*) override {
    if (!dealer || target != owner) return 1;
    if (!facingLeft && dealer->get<BackAttackLeftPower>()) return Dec::lit(1.5);
    if (facingLeft && dealer->get<BackAttackRightPower>()) return Dec::lit(1.5);
    return 1;
  }
  Task<> beforeCardPlayed(const CardPlay& play) override {
    if (play.target) updateDirection(play.target);
    co_return;
  }
  Task<> afterDeath(Creature* c) override {
    if (c->side == owner->side || !owner->combat) co_return;
    auto hittable = owner->combat->hittableEnemies();
    if (hittable.empty()) co_return;
    bool allLeft = true, allRight = true;
    for (Creature* e : hittable) {
      allLeft = allLeft && e->get<BackAttackLeftPower>();
      allRight = allRight && e->get<BackAttackRightPower>();
    }
    if (allLeft || allRight) updateDirection(hittable[0]);
    co_return;
  }
  void updateDirection(Creature* target) {
    if (!facingLeft && target->get<BackAttackLeftPower>()) facingLeft = true;
    else if (facingLeft && target->get<BackAttackRightPower>()) facingLeft = false;
  }
};

// Knowledge Demon's curses.
struct DisintegrationPower : Power {
  POWER_HEADER(DisintegrationPower, "DISINTEGRATION_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  Task<> afterSideTurnEndLate(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await cmd::damage(owner, amount, kUnpowered, owner, nullptr);
  }
};

struct MindRotPower : Power {
  POWER_HEADER(MindRotPower, "MIND_ROT_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  Dec modifyHandDraw(Dec count) override {
    flash = 1.f;
    return dmax(Dec(0), count - Dec(amount));
  }
};

struct SlothPower : Power {
  POWER_HEADER(SlothPower, "SLOTH_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  int cardsPlayedThisTurn = 0;
  bool shouldPlay(Card* card) override {
    (void)card;  // the player is the only card owner
    return cardsPlayedThisTurn < amount;
  }
  Task<> beforeCardPlayed(const CardPlay&) override { ++cardsPlayedThisTurn; co_return; }
  Task<> beforeSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) cardsPlayedThisTurn = 0;
    co_return;
  }
};

struct WasteAwayPower : Power {
  POWER_HEADER(WasteAwayPower, "WASTE_AWAY_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  Dec modifyMaxEnergy(Dec amount_) override { return amount_ - Dec(amount); }
};

// ================================================================ cards

// The four curses Knowledge Demon offers are picked on a card screen and take effect when
// chosen (KnowledgeDemon.IChoosable.OnChosen); they never enter a pile.
struct Disintegration : IroncladT<Disintegration> {
  bool canBeGeneratedInCombat() const override { return false; }
  CARD_HEADER(Disintegration, "DISINTEGRATION", -1, Status, Status, None)
    maxUpgradeLevel = 0;
    addVar("DisintegrationPower", 6);
  }
};

struct MindRot : IroncladT<MindRot> {
  bool canBeGeneratedInCombat() const override { return false; }
  CARD_HEADER(MindRot, "MIND_ROT", -1, Status, Status, None)
    maxUpgradeLevel = 0;
    addVar("MindRotPower", 1);
  }
};

struct Sloth : IroncladT<Sloth> {
  bool canBeGeneratedInCombat() const override { return false; }
  CARD_HEADER(Sloth, "SLOTH", -1, Status, Status, None)
    maxUpgradeLevel = 0;
    addVar("SlothPower", 3);
  }
};

struct WasteAway : IroncladT<WasteAway> {
  bool canBeGeneratedInCombat() const override { return false; }
  CARD_HEADER(WasteAway, "WASTE_AWAY", -1, Status, Status, None)
    maxUpgradeLevel = 0;
    addVar("WasteAwayPower", 1);
  }
};

// Playing it buys one more turn in the Insatiable's sandpit and makes it cost 1 more.
struct FranticEscape : IroncladT<FranticEscape> {
  bool canBeGeneratedInCombat() const override { return false; }
  CARD_HEADER(FranticEscape, "FRANTIC_ESCAPE", 1, Status, Status, Self)
    maxUpgradeLevel = 0;
  }
  Task<> onPlay(CardPlay&) override {
    for (Creature* e : combat->enemies) {
      if (e->removed) continue;
      for (auto& p : e->powers) {
        if (p->id != "SandpitPower") continue;
        auto* sand = static_cast<SandpitPower*>(p.get());
        if (sand->target != me()) continue;
        co_await cmd::modifyPowerAmount(sand, 1, e, this);
        break;
      }
    }
    addThisCombat(1);
  }
};

// ================================================================ Decimillipede

struct DecimillipedeSegment : Monster {
  int starterMoveIdx = 0;
  int minHp() const override { return asc(kToughEnemies, 46, 40); }
  int maxHp() const override { return asc(kToughEnemies, 52, 46); }
  void buildMoves() override {
    auto* writhe = machine.add<MoveState>("WRITHE_MOVE");
    writhe->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 6, 5), 2); };
    writhe->intents = {attackIntent(asc(kDeadlyEnemies, 6, 5), 2)};
    auto* bulk = machine.add<MoveState>("BULK_MOVE");
    bulk->perform = [this](Targets) { return bulkMove(); };
    bulk->intents = {attackIntent(asc(kDeadlyEnemies, 7, 6)), kindIntent(Intent::Buff)};
    auto* constrict = machine.add<MoveState>("CONSTRICT_MOVE");
    constrict->perform = [this](Targets t) { return constrictMove(t); };
    constrict->intents = {attackIntent(asc(kDeadlyEnemies, 9, 8)), kindIntent(Intent::Debuff)};
    auto* dead = machine.add<MoveState>("DEAD_MOVE");
    dead->perform = [](Targets) -> Task<> { co_return; };
    auto* reattach = machine.add<MoveState>("REATTACH_MOVE");
    reattach->perform = [this](Targets) { return reattachMove(); };
    reattach->intents = {kindIntent(Intent::Heal)};
    reattach->mustPerformOnce = true;
    auto* branch = machine.add<RandomBranchState>("RAND");
    constrict->followUp = bulk;
    bulk->followUp = writhe;
    writhe->followUp = constrict;
    dead->followUp = reattach;
    reattach->followUp = branch;
    branch->add(writhe, MoveRepeat::CannotRepeat);
    branch->add(bulk, MoveRepeat::CannotRepeat);
    branch->add(constrict, MoveRepeat::CannotRepeat);
    machine.start(starterMoveIdx % 3 == 0 ? writhe : starterMoveIdx % 3 == 1 ? bulk : constrict);
  }
  // Segments must not share a max HP: round up to even, then step by 2 within the range.
  Task<> afterAddedToRoom() override {
    int hp = creature->maxHp;
    if (hp % 2 == 1) ++hp;
    auto taken = [&](int v) {
      for (Creature* e : combat->enemies) if (e != creature && e->maxHp == v) return true;
      return false;
    };
    while (taken(hp)) {
      hp += 2;
      if (hp > maxHp()) hp = minHp();
    }
    creature->hp = creature->maxHp = hp;
    co_await applyToSelf<ReattachPower>(25);
  }
  Task<> bulkMove() {
    co_await attack(asc(kDeadlyEnemies, 7, 6));
    co_await applyToSelf<StrengthPower>(2);
  }
  Task<> constrictMove(Targets targets) {
    co_await attack(asc(kDeadlyEnemies, 9, 8));
    co_await applyToTargets<WeakPower>(targets, 1);
  }
  Task<> reattachMove() {
    if (auto* p = creature->get<ReattachPower>()) co_await p->doReattach();
  }
};

struct DecimillipedeSegmentFront : DecimillipedeSegment {
  MONSTER_HEADER(DecimillipedeSegmentFront, "DECIMILLIPEDE_SEGMENT_FRONT")
};
struct DecimillipedeSegmentMiddle : DecimillipedeSegment {
  MONSTER_HEADER(DecimillipedeSegmentMiddle, "DECIMILLIPEDE_SEGMENT_MIDDLE")
};
struct DecimillipedeSegmentBack : DecimillipedeSegment {
  MONSTER_HEADER(DecimillipedeSegmentBack, "DECIMILLIPEDE_SEGMENT_BACK")
};

// ================================================================ Entomancer

struct Entomancer : Monster {
  MONSTER_HEADER(Entomancer, "ENTOMANCER")
  int minHp() const override { return asc(kToughEnemies, 165, 145); }
  int maxHp() const override { return minHp(); }
  Task<> afterAddedToRoom() override { co_await applyToSelf<PersonalHivePower>(1); }
  void buildMoves() override {
    auto* spit = machine.add<MoveState>("PHEROMONE_SPIT_MOVE");
    spit->perform = [this](Targets) { return spitMove(); };
    spit->intents = {kindIntent(Intent::Buff)};
    auto* bees = machine.add<MoveState>("BEES_MOVE");
    bees->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 3, 3), asc(kDeadlyEnemies, 8, 7)); };
    bees->intents = {attackIntent(asc(kDeadlyEnemies, 3, 3), asc(kDeadlyEnemies, 8, 7))};
    auto* spear = machine.add<MoveState>("SPEAR_MOVE");
    spear->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 20, 18)); };
    spear->intents = {attackIntent(asc(kDeadlyEnemies, 20, 18))};
    bees->followUp = spear;
    spear->followUp = spit;
    spit->followUp = bees;
    machine.start(bees);
  }
  Task<> spitMove() {
    auto* hive = creature->get<PersonalHivePower>();
    if (!hive || hive->amount >= 3) {
      co_await applyToSelf<StrengthPower>(2);
      co_return;
    }
    co_await applyToSelf<PersonalHivePower>(1);
    co_await applyToSelf<StrengthPower>(1);
  }
};

// ================================================================ Infested Prisms

struct InfestedPrism : Monster {
  MONSTER_HEADER(InfestedPrism, "INFESTED_PRISM")
  int minHp() const override { return asc(kToughEnemies, 171, 161); }
  int maxHp() const override { return minHp(); }
  Task<> afterAddedToRoom() override { co_await applyToSelf<VitalSparkPower>(asc(kDeadlyEnemies, 3, 2)); }
  void buildMoves() override {
    auto* jab = machine.add<MoveState>("JAB_MOVE");
    jab->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 17, 15)); };
    jab->intents = {attackIntent(asc(kDeadlyEnemies, 17, 15))};
    auto* radiate = machine.add<MoveState>("RADIATE_MOVE");
    radiate->perform = [this](Targets) { return radiateMove(); };
    radiate->intents = {attackIntent(asc(kDeadlyEnemies, 13, 11)), kindIntent(Intent::Defend)};
    auto* whirlwind = machine.add<MoveState>("WHIRLWIND_MOVE");
    whirlwind->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 6, 5), 3); };
    whirlwind->intents = {attackIntent(asc(kDeadlyEnemies, 6, 5), 3)};
    auto* pulsate = machine.add<MoveState>("PULSATE_MOVE");
    pulsate->perform = [this](Targets) { return pulsateMove(); };
    pulsate->intents = {attackIntent(asc(kDeadlyEnemies, 10, 8)), kindIntent(Intent::Buff), kindIntent(Intent::Defend)};
    jab->followUp = radiate;
    radiate->followUp = whirlwind;
    whirlwind->followUp = pulsate;
    pulsate->followUp = jab;
    machine.start(jab);
  }
  Task<> radiateMove() {
    co_await attack(asc(kDeadlyEnemies, 13, 11));
    co_await gainBlock(asc(kDeadlyEnemies, 13, 11));
  }
  Task<> pulsateMove() {
    co_await attack(asc(kDeadlyEnemies, 10, 8));
    co_await gainBlock(asc(kToughEnemies, 22, 20));
    co_await applyToSelf<VitalSparkPower>(asc(kDeadlyEnemies, 3, 2));
  }
};

// ================================================================ The Insatiable

struct TheInsatiable : Monster {
  MONSTER_HEADER(TheInsatiable, "THE_INSATIABLE")
  int minHp() const override { return asc(kToughEnemies, 341, 321); }
  int maxHp() const override { return minHp(); }
  void buildMoves() override {
    auto* liquify = machine.add<MoveState>("LIQUIFY_GROUND_MOVE");
    liquify->perform = [this](Targets t) { return liquifyMove(t); };
    liquify->intents = {kindIntent(Intent::Buff), kindIntent(Intent::Status, 6)};
    auto* thrash = machine.add<MoveState>("THRASH_MOVE");
    thrash->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 9, 8), 2); };
    thrash->intents = {attackIntent(asc(kDeadlyEnemies, 9, 8), 2)};
    auto* thrash2 = machine.add<MoveState>("THRASH_MOVE_2");
    thrash2->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 9, 8), 2); };
    thrash2->intents = {attackIntent(asc(kDeadlyEnemies, 9, 8), 2)};
    auto* bite = machine.add<MoveState>("LUNGING_BITE_MOVE");
    bite->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 31, 28)); };
    bite->intents = {attackIntent(asc(kDeadlyEnemies, 31, 28))};
    auto* salivate = machine.add<MoveState>("SALIVATE_MOVE");
    salivate->perform = [this](Targets) { return applyToSelf<StrengthPower>(asc(kDeadlyEnemies, 3, 2)); };
    salivate->intents = {kindIntent(Intent::Buff)};
    liquify->followUp = thrash;
    thrash->followUp = bite;
    bite->followUp = salivate;
    salivate->followUp = thrash2;
    thrash2->followUp = thrash;
    machine.start(liquify);
  }
  Task<> liquifyMove(Targets targets) {
    combat->push({VisualEvent::Anim, creature, 0, "Cast"});
    co_await wait(0.5);
    for (Creature* t : targets) {
      auto sand = std::make_unique<SandpitPower>();
      sand->target = t;
      co_await cmd::applyPower(std::move(sand), creature, 4, creature, nullptr);
    }
    for (Creature* t : targets) {
      (void)t;
      for (int i = 0; i < 6; ++i) {
        if (i < 3) co_await addToDrawRandom(*combat, "FranticEscape");
        else {
          auto fe = db::card("FranticEscape");
          fe->createdByPlayer = false;  // creator null
          co_await cmd::addGeneratedCard(*combat, std::move(fe), Pile::Discard);
        }
      }
    }
  }
};

// ================================================================ Knowledge Demon

struct KnowledgeDemon : Monster {
  MONSTER_HEADER(KnowledgeDemon, "KNOWLEDGE_DEMON")
  int curseOfKnowledgeCounter = 0;
  int minHp() const override { return asc(kToughEnemies, 399, 379); }
  int maxHp() const override { return minHp(); }
  void buildMoves() override {
    auto* curse = machine.add<MoveState>("CURSE_OF_KNOWLEDGE_MOVE");
    curse->perform = [this](Targets t) { return curseMove(t); };
    curse->intents = {kindIntent(Intent::Debuff)};
    auto* slap = machine.add<MoveState>("SLAP_MOVE");
    slap->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 18, 17)); };
    slap->intents = {attackIntent(asc(kDeadlyEnemies, 18, 17))};
    auto* overwhelming = machine.add<MoveState>("KNOWLEDGE_OVERWHELMING_MOVE");
    overwhelming->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 9, 8), 3); };
    overwhelming->intents = {attackIntent(asc(kDeadlyEnemies, 9, 8), 3)};
    auto* ponder = machine.add<MoveState>("PONDER_MOVE");
    ponder->perform = [this](Targets) { return ponderMove(); };
    ponder->intents = {attackIntent(asc(kDeadlyEnemies, 13, 11)), kindIntent(Intent::Heal), kindIntent(Intent::Buff)};
    auto* branch = machine.add<ConditionalBranchState>("CurseOfKnowledgeBranch");
    curse->followUp = slap;
    slap->followUp = overwhelming;
    overwhelming->followUp = ponder;
    ponder->followUp = branch;
    branch->add(curse, [this] { return curseOfKnowledgeCounter < 3; });
    branch->add(slap, [this] { return curseOfKnowledgeCounter >= 3; });
    machine.start(curse);
  }
  // Each round the player picks one of two curses; Disintegration is always on offer and
  // gets stronger every time.
  Task<> curseMove(Targets targets) {
    static const int kDisintegration[3] = {6, 7, 8};
    static const char* kOther[3] = {"MindRot", "Sloth", "WasteAway"};
    static const char* kOtherVar[3] = {"MindRotPower", "SlothPower", "WasteAwayPower"};
    int set = std::min(curseOfKnowledgeCounter, 2);
    for (Creature* t : targets) {
      if (t->dead()) continue;
      Card* dis = combat->addCard(db::card("Disintegration"));
      if (auto* v = dis->var("DisintegrationPower")) v->base = v->canonical = Dec(kDisintegration[set]);
      Card* other = combat->addCard(db::card(kOther[set]));
      auto picked = co_await cmd::selectCards(*combat, "DISINTEGRATION", {dis, other}, 1, 1);
      if (picked.empty()) continue;
      if (picked[0] == dis) {
        co_await applyPower<DisintegrationPower>(t, dis->val("DisintegrationPower"), t, nullptr);
      } else if (set == 0) {
        co_await applyPower<MindRotPower>(t, other->val(kOtherVar[set]), t, nullptr);
      } else if (set == 1) {
        co_await applyPower<SlothPower>(t, other->val(kOtherVar[set]), t, nullptr);
      } else {
        co_await applyPower<WasteAwayPower>(t, other->val(kOtherVar[set]), t, nullptr);
      }
    }
    if (!combat->ending) ++curseOfKnowledgeCounter;
  }
  Task<> ponderMove() {
    co_await attack(asc(kDeadlyEnemies, 13, 11));
    co_await cmd::heal(creature, 30);
    co_await applyToSelf<StrengthPower>(asc(kDeadlyEnemies, 3, 2));
  }
};

// ================================================================ Kaiser Crab

// PORT NOTE: the fight's arm animations live in a shared background scene in the C#
// (NKaiserCrabBossBackground); here the claws are two plain creatures.
struct Crusher : Monster {
  MONSTER_HEADER(Crusher, "CRUSHER")
  int minHp() const override { return asc(kToughEnemies, 219, 209); }
  int maxHp() const override { return minHp(); }
  Task<> afterAddedToRoom() override {
    co_await applyToSelf<BackAttackLeftPower>(1);
    co_await applyToSelf<CrabRagePower>(1);
  }
  void buildMoves() override {
    auto* thrash = machine.add<MoveState>("THRASH_MOVE");
    thrash->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 14, 12)); };
    thrash->intents = {attackIntent(asc(kDeadlyEnemies, 14, 12))};
    auto* enlarging = machine.add<MoveState>("ENLARGING_STRIKE_MOVE");
    enlarging->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 4, 4)); };
    enlarging->intents = {attackIntent(asc(kDeadlyEnemies, 4, 4))};
    auto* sting = machine.add<MoveState>("BUG_STING_MOVE");
    sting->perform = [this](Targets t) { return stingMove(t); };
    sting->intents = {attackIntent(asc(kDeadlyEnemies, 7, 6), 2), kindIntent(Intent::Debuff)};
    auto* adapt = machine.add<MoveState>("ADAPT_MOVE");
    adapt->perform = [this](Targets) { return applyToSelf<StrengthPower>(asc(kDeadlyEnemies, 3, 2)); };
    adapt->intents = {kindIntent(Intent::Buff)};
    auto* guarded = machine.add<MoveState>("GUARDED_STRIKE_MOVE");
    guarded->perform = [this](Targets) { return guardedMove(); };
    guarded->intents = {attackIntent(asc(kDeadlyEnemies, 14, 12)), kindIntent(Intent::Defend)};
    thrash->followUp = enlarging;
    enlarging->followUp = sting;
    sting->followUp = adapt;
    adapt->followUp = guarded;
    guarded->followUp = thrash;
    machine.start(thrash);
  }
  Task<> stingMove(Targets targets) {
    co_await attack(asc(kDeadlyEnemies, 7, 6), 2);
    co_await applyToTargets<WeakPower>(targets, 2);
    co_await applyToTargets<FrailPower>(targets, 2);
  }
  Task<> guardedMove() {
    co_await attack(asc(kDeadlyEnemies, 14, 12));
    co_await gainBlock(18);
  }
};

struct Rocket : Monster {
  MONSTER_HEADER(Rocket, "ROCKET")
  int minHp() const override { return asc(kToughEnemies, 209, 199); }
  int maxHp() const override { return minHp(); }
  Task<> afterAddedToRoom() override {
    co_await applyPower<SurroundedPower>(combat->player, 1, creature, nullptr);
    co_await applyToSelf<BackAttackRightPower>(1);
    co_await applyToSelf<CrabRagePower>(1);
  }
  void buildMoves() override {
    auto* reticle = machine.add<MoveState>("TARGETING_RETICLE_MOVE");
    reticle->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 4, 3)); };
    reticle->intents = {attackIntent(asc(kDeadlyEnemies, 4, 3))};
    auto* beam = machine.add<MoveState>("PRECISION_BEAM_MOVE");
    beam->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 20, 18)); };
    beam->intents = {attackIntent(asc(kDeadlyEnemies, 20, 18))};
    auto* charge = machine.add<MoveState>("CHARGE_UP_MOVE");
    charge->perform = [this](Targets) { return applyToSelf<StrengthPower>(asc(kDeadlyEnemies, 3, 2)); };
    charge->intents = {kindIntent(Intent::Buff)};
    auto* laser = machine.add<MoveState>("LASER_MOVE");
    laser->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 35, 31)); };
    laser->intents = {attackIntent(asc(kDeadlyEnemies, 35, 31))};
    auto* recharge = machine.add<MoveState>("RECHARGE_MOVE");
    recharge->perform = [](Targets) -> Task<> { co_return; };
    recharge->intents = {kindIntent(Intent::Sleep)};
    reticle->followUp = beam;
    beam->followUp = charge;
    charge->followUp = laser;
    laser->followUp = recharge;
    recharge->followUp = reticle;
    machine.start(reticle);
  }
};

}  // namespace

// ================================================================ registry

void registerAct2C() {
  db::registerCard("Disintegration", [] { return std::unique_ptr<Card>(new Disintegration()); });
  db::registerCard("MindRot", [] { return std::unique_ptr<Card>(new MindRot()); });
  db::registerCard("Sloth", [] { return std::unique_ptr<Card>(new Sloth()); });
  db::registerCard("WasteAway", [] { return std::unique_ptr<Card>(new WasteAway()); });
  db::registerCard("FranticEscape", [] { return std::unique_ptr<Card>(new FranticEscape()); });
  db::registerPower("PersonalHivePower", [] { return std::unique_ptr<Power>(new PersonalHivePower()); });
  db::registerPower("TaintedPower", [] { return std::unique_ptr<Power>(new TaintedPower()); });
  db::registerPower("VitalSparkPower", [] { return std::unique_ptr<Power>(new VitalSparkPower()); });
  db::registerPower("ReattachPower", [] { return std::unique_ptr<Power>(new ReattachPower()); });
  db::registerPower("SandpitPower", [] { return std::unique_ptr<Power>(new SandpitPower()); });
  db::registerPower("BackAttackLeftPower", [] { return std::unique_ptr<Power>(new BackAttackLeftPower()); });
  db::registerPower("BackAttackRightPower", [] { return std::unique_ptr<Power>(new BackAttackRightPower()); });
  db::registerPower("CrabRagePower", [] { return std::unique_ptr<Power>(new CrabRagePower()); });
  db::registerPower("SurroundedPower", [] { return std::unique_ptr<Power>(new SurroundedPower()); });
  db::registerPower("DisintegrationPower", [] { return std::unique_ptr<Power>(new DisintegrationPower()); });
  db::registerPower("MindRotPower", [] { return std::unique_ptr<Power>(new MindRotPower()); });
  db::registerPower("SlothPower", [] { return std::unique_ptr<Power>(new SlothPower()); });
  db::registerPower("WasteAwayPower", [] { return std::unique_ptr<Power>(new WasteAwayPower()); });

  db::registerEncounter("DecimillipedeElite", RoomType::Elite, false, [](Rng& rng) {
    auto front = std::make_unique<DecimillipedeSegmentFront>();
    auto middle = std::make_unique<DecimillipedeSegmentMiddle>();
    auto back = std::make_unique<DecimillipedeSegmentBack>();
    int start = rng.nextInt(3);
    front->starterMoveIdx = start;
    middle->starterMoveIdx = (start + 1) % 3;
    back->starterMoveIdx = (start + 2) % 3;
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(std::move(front));
    v.push_back(std::move(middle));
    v.push_back(std::move(back));
    return v;
  });
  db::registerEncounter("EntomancerElite", RoomType::Elite, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(std::make_unique<Entomancer>());
    return v;
  });
  db::registerEncounter("InfestedPrismsElite", RoomType::Elite, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(std::make_unique<InfestedPrism>());
    return v;
  });
  db::registerEncounter("TheInsatiableBoss", RoomType::Boss, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(std::make_unique<TheInsatiable>());
    return v;
  });
  db::registerEncounter("KnowledgeDemonBoss", RoomType::Boss, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(std::make_unique<KnowledgeDemon>());
    return v;
  });
  db::registerEncounter("KaiserCrabBoss", RoomType::Boss, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(std::make_unique<Crusher>());
    v.push_back(std::make_unique<Rocket>());
    return v;
  });
}

}  // namespace sts
