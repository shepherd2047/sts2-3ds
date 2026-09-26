// Act 2 (Hive) elites and bosses, package 4c: Decimillipede, Entomancer, Infested
// Prism, The Insatiable, Knowledge Demon, Kaiser Crab (Crusher + Rocket)
// (MegaCrit.Sts2.Core.Models.Monsters / .Powers / .Cards / .Encounters).
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

// CardPileCmd.Add(..., CardPilePosition.Random): Rng.Shuffle.NextInt(count + 1).
Task<> addGeneratedRandom(Combat& c, const std::string& cardId, Pile to) {
  Card* card = co_await cmd::addGeneratedCard(c, db::card(cardId), to);
  auto& pile = c.pile(to);
  auto it = std::find(pile.begin(), pile.end(), card);
  if (it == pile.end()) co_return;
  pile.erase(it);
  int at = c.rng("Shuffle").nextInt((int)pile.size() + 1);
  pile.insert(pile.begin() + at, card);
}

// ================================================================ Decimillipede

// Segments die without leaving and reattach (heal 25) after one turn, unless the
// other segments are all dead too. Their deaths don't count as fatal (Feed).
struct ReattachPower : Power {
  POWER_HEADER(ReattachPower, "REATTACH_POWER")
  StackType stackType() const override { return StackType::Single; }
  bool isReviving = false;

  bool removedAfterOwnerDeath() const override { return false; }
  bool shouldCreatureBeRemovedFromCombatAfterDeath(Creature* c) override { return c != owner; }
  bool shouldOwnerDeathTriggerFatal() const override { return allOtherSegmentsDead(); }

  bool allOtherSegmentsDead() const {
    for (auto* e : owner->combat->enemies)
      if (e != owner && !e->removed && e->get<ReattachPower>() && e->alive()) return false;
    return true;
  }
  // Executed by the segment's REATTACH_MOVE.
  Task<> doReattach() {
    if (allOtherSegmentsDead()) co_return;
    isReviving = false;
    co_await cmd::heal(owner, amount);
  }
  Task<> afterDeath(Creature* c) override {
    if (c != owner) co_return;
    if (!allOtherSegmentsDead() || !owner->dead()) {
      isReviving = true;
      Monster* m = owner->monster.get();
      auto it = m->machine.states.find("DEAD_MOVE");
      if (it != m->machine.states.end()) m->setMoveImmediate(static_cast<MoveState*>(it->second.get()));
    }
    // else: the last segment fell; the combat ends through the win condition.
  }
};

struct DecimillipedeSegment : Monster {
  int starterMoveIdx = 0;
  int minHp() const override { return 40; }
  int maxHp() const override { return 46; }

  Task<> afterAddedToRoom() override {
    // An even max HP that no other segment has (stepping by 2, wrapping to the minimum).
    int hp = creature->maxHp;
    if (hp % 2 == 1) ++hp;
    auto taken = [&](int v) {
      for (auto* e : combat->enemies) if (e != creature && !e->removed && e->maxHp == v) return true;
      return false;
    };
    for (int guard = 0; taken(hp) && guard < 16; ++guard) {
      hp += 2;
      if (hp > maxHp()) hp = minHp();
    }
    creature->maxHp = creature->hp = hp;  // CreatureCmd.SetMaxAndCurrentHp
    co_await applyToSelf<ReattachPower>(25);
  }
  void buildMoves() override {
    auto* writhe = machine.add<MoveState>("WRITHE_MOVE");
    writhe->perform = [this](Targets) { return attack(5, 2); };
    writhe->intents = {attackIntent(5, 2)};
    auto* bulk = machine.add<MoveState>("BULK_MOVE");
    bulk->perform = [this](Targets) -> Task<> {
      co_await attack(6);
      co_await applyToSelf<StrengthPower>(2);
    };
    bulk->intents = {attackIntent(6), kindIntent(Intent::Buff)};
    auto* constrict = machine.add<MoveState>("CONSTRICT_MOVE");
    constrict->perform = [this](Targets t) -> Task<> {
      co_await attack(8);
      co_await applyToTargets<WeakPower>(t, 1);
    };
    constrict->intents = {attackIntent(8), kindIntent(Intent::Debuff)};
    auto* dead = machine.add<MoveState>("DEAD_MOVE");
    dead->perform = [](Targets) -> Task<> { co_return; };
    auto* reattach = machine.add<MoveState>("REATTACH_MOVE");
    reattach->perform = [this](Targets) -> Task<> {
      if (auto* p = creature->get<ReattachPower>()) co_await p->doReattach();
    };
    reattach->intents = {kindIntent(Intent::Heal)};
    reattach->mustPerformOnce = true;
    auto* rand = machine.add<RandomBranchState>("RAND");
    constrict->followUp = bulk;
    bulk->followUp = writhe;
    writhe->followUp = constrict;
    dead->followUp = reattach;
    reattach->followUp = rand;
    rand->add(writhe, MoveRepeat::CannotRepeat);
    rand->add(bulk, MoveRepeat::CannotRepeat);
    rand->add(constrict, MoveRepeat::CannotRepeat);
    int i = starterMoveIdx % 3;
    machine.start(i == 0 ? static_cast<MonsterState*>(writhe) : i == 1 ? static_cast<MonsterState*>(bulk) : constrict);
  }
};
struct DecimillipedeSegmentFront : DecimillipedeSegment { MONSTER_HEADER(DecimillipedeSegmentFront, "DECIMILLIPEDE_SEGMENT_FRONT") };
struct DecimillipedeSegmentMiddle : DecimillipedeSegment { MONSTER_HEADER(DecimillipedeSegmentMiddle, "DECIMILLIPEDE_SEGMENT_MIDDLE") };
struct DecimillipedeSegmentBack : DecimillipedeSegment { MONSTER_HEADER(DecimillipedeSegmentBack, "DECIMILLIPEDE_SEGMENT_BACK") };

// ================================================================ Entomancer

// Powered attacks on the owner shuffle Amount Dazed into the attacker's draw pile.
struct PersonalHivePower : Power {
  POWER_HEADER(PersonalHivePower, "PERSONAL_HIVE_POWER")
  Task<> afterDamageReceived(Creature* target, const DamageResult&, int props, Creature* dealer, Card*) override {
    if (target != owner || !dealer || !isPoweredAttack(props) || !dealer->isPlayer) co_return;
    flash = 1.f;
    for (int i = 0; i < amount; ++i) co_await addGeneratedRandom(*owner->combat, "Dazed", Pile::Draw);
    owner->combat->push({VisualEvent::Banner, owner->combat->player, amount, "Dazed"});
    co_await wait(0.5);
  }
};

struct Entomancer : Monster {
  MONSTER_HEADER(Entomancer, "ENTOMANCER")
  int minHp() const override { return 145; }
  int maxHp() const override { return 145; }
  Task<> afterAddedToRoom() override { co_await applyToSelf<PersonalHivePower>(1); }
  void buildMoves() override {
    auto* spit = machine.add<MoveState>("PHEROMONE_SPIT_MOVE");
    spit->perform = [this](Targets) -> Task<> {
      auto* hive = creature->get<PersonalHivePower>();
      if (!hive || hive->amount >= 3) {
        co_await applyToSelf<StrengthPower>(2);
        co_return;
      }
      co_await applyToSelf<PersonalHivePower>(1);
      co_await applyToSelf<StrengthPower>(1);
    };
    spit->intents = {kindIntent(Intent::Buff)};
    auto* bees = machine.add<MoveState>("BEES_MOVE");
    bees->perform = [this](Targets) { return attack(3, 7); };
    bees->intents = {attackIntent(3, 7)};
    auto* spear = machine.add<MoveState>("SPEAR_MOVE");
    spear->perform = [this](Targets) { return attack(18); };
    spear->intents = {attackIntent(18)};
    bees->followUp = spear;
    spear->followUp = spit;
    spit->followUp = bees;
    machine.start(bees);
  }
};

// ================================================================ Infested Prism

// Tainted affliction (on Skills): playing the card applies TaintedPower. Removed at the end
// of the enemy turn; adds its amount to powered attacks against the owner.
struct TaintedPower : Power {
  POWER_HEADER(TaintedPower, "TAINTED_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  Dec modifyDamageAdditive(Creature* target, Dec, int props, Creature*, Card*) override {
    if (target != owner || !isPoweredAttack(props)) return 0;
    return amount;
  }
  Task<> afterSideTurnEnd(Side side, const std::vector<Creature*>&) override {
    if (side != Side::Enemy) co_return;
    flash = 1.f;
    co_await cmd::removePower(this);
  }
};

// PORT NOTE: C# afflicts every Skill with Tainted (CardCmd.Afflict, stackable, shown on the
// card) and applies TaintedPower when a Tainted card is played. There is no per-card
// affliction system here, so every Skill the player plays counts as Tainted (the C#
// only skips Skills already carrying another affliction, which nothing in Act 2 applies).
struct VitalSparkPower : Power {
  POWER_HEADER(VitalSparkPower, "VITAL_SPARK_POWER")
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (!p.card || p.card->type != CardType::Skill || ownerOf(p.card) == nullptr) co_return;
    flash = 1.f;
    co_await applyPower<TaintedPower>(ownerOf(p.card), amount, nullptr, nullptr);
  }
};

struct InfestedPrism : Monster {
  MONSTER_HEADER(InfestedPrism, "INFESTED_PRISM")
  int minHp() const override { return 161; }
  int maxHp() const override { return 161; }
  Task<> afterAddedToRoom() override { co_await applyToSelf<VitalSparkPower>(2); }
  void buildMoves() override {
    auto* jab = machine.add<MoveState>("JAB_MOVE");
    jab->perform = [this](Targets) { return attack(15); };
    jab->intents = {attackIntent(15)};
    auto* radiate = machine.add<MoveState>("RADIATE_MOVE");
    radiate->perform = [this](Targets) -> Task<> {
      co_await attack(11);
      co_await gainBlock(11);
    };
    radiate->intents = {attackIntent(11), kindIntent(Intent::Defend)};
    auto* whirl = machine.add<MoveState>("WHIRLWIND_MOVE");
    whirl->perform = [this](Targets) { return attack(5, 3); };
    whirl->intents = {attackIntent(5, 3)};
    auto* pulsate = machine.add<MoveState>("PULSATE_MOVE");
    pulsate->perform = [this](Targets) -> Task<> {
      co_await attack(8);
      co_await gainBlock(20);
      co_await applyToSelf<VitalSparkPower>(2);
    };
    pulsate->intents = {attackIntent(8), kindIntent(Intent::Buff), kindIntent(Intent::Defend)};
    jab->followUp = radiate;
    radiate->followUp = whirl;
    whirl->followUp = pulsate;
    pulsate->followUp = jab;
    machine.start(jab);
  }
};

// ================================================================ The Insatiable

// On the Insatiable, aimed at the player: counts down at the start of each enemy turn;
// at 0 the player is eaten. Frantic Escape pushes it back up.
struct SandpitPower : Power {
  POWER_HEADER(SandpitPower, "SANDPIT_POWER")
  Creature* target = nullptr;
  Task<> afterSideTurnStart(Side side, const std::vector<Creature*>&) override {  // AfterSideTurnStartLate
    if (side == Side::Enemy) co_await cmd::decrement(this);
  }
  Task<> afterRemoved(Creature* oldOwner) override {
    if (oldOwner->dead() || !target || target->dead()) co_return;
    co_await wait(0.5);
    co_await cmd::kill({target});  // CreatureCmd.Kill(force: true)
  }
};

struct FranticEscape : IroncladT<FranticEscape> {
  CARD_HEADER(FranticEscape, "FRANTIC_ESCAPE", 1, Status, Status, Self)
    maxUpgradeLevel = 0;
  }
  Task<> onPlay(CardPlay&) override {
    for (auto* e : combat->enemies) {
      if (e->removed) continue;
      if (auto* s = e->get<SandpitPower>()) {
        co_await cmd::modifyPowerAmount(s, 1, e, this);
        break;
      }
    }
    addThisCombat(1);
  }
};

struct TheInsatiable : Monster {
  MONSTER_HEADER(TheInsatiable, "THE_INSATIABLE")
  int minHp() const override { return 321; }
  int maxHp() const override { return 321; }
  void buildMoves() override {
    auto* liquify = machine.add<MoveState>("LIQUIFY_GROUND_MOVE");
    liquify->perform = [this](Targets t) -> Task<> {
      for (Creature* target : t) {
        auto p = std::make_unique<SandpitPower>();
        p->target = target;
        co_await cmd::applyPower(std::move(p), creature, 4, creature, nullptr);
      }
      for (int i = 0; i < 6; ++i)
        co_await addGeneratedRandom(*combat, "FranticEscape", i < 3 ? Pile::Draw : Pile::Discard);
      combat->push({VisualEvent::Banner, combat->player, 6, "FranticEscape"});
      co_await wait(0.3);
    };
    liquify->intents = {kindIntent(Intent::Buff), kindIntent(Intent::Status, 6)};
    auto* thrash = machine.add<MoveState>("THRASH_MOVE");
    thrash->perform = [this](Targets) { return attack(8, 2); };
    thrash->intents = {attackIntent(8, 2)};
    auto* thrash2 = machine.add<MoveState>("THRASH_MOVE_2");
    thrash2->perform = [this](Targets) { return attack(8, 2); };
    thrash2->intents = {attackIntent(8, 2)};
    auto* bite = machine.add<MoveState>("LUNGING_BITE_MOVE");
    bite->perform = [this](Targets) { return attack(28); };
    bite->intents = {attackIntent(28)};
    auto* salivate = machine.add<MoveState>("SALIVATE_MOVE");
    salivate->perform = [this](Targets) { return applyToSelf<StrengthPower>(2); };
    salivate->intents = {kindIntent(Intent::Buff)};
    liquify->followUp = thrash;
    thrash->followUp = bite;
    bite->followUp = salivate;
    salivate->followUp = thrash2;
    thrash2->followUp = thrash;
    machine.start(liquify);
  }
};

// ================================================================ Knowledge Demon

struct DisintegrationPower : Power {
  POWER_HEADER(DisintegrationPower, "DISINTEGRATION_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {  // ...Late
    if (contains(participants, owner)) co_await cmd::damage(owner, amount, kUnpowered, owner, nullptr);
  }
};

struct MindRotPower : Power {
  POWER_HEADER(MindRotPower, "MIND_ROT_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  Dec modifyHandDraw(Dec count) override {
    flash = 1.f;
    return std::max(Dec(0), count - Dec(amount));
  }
};

struct SlothPower : Power {
  POWER_HEADER(SlothPower, "SLOTH_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  int cardsPlayedThisTurn = 0;  // DisplayAmount
  bool shouldPlay(Card* card) override { return ownerOf(card) != owner || cardsPlayedThisTurn < amount; }
  Task<> beforeCardPlayed(const CardPlay& p) override {
    if (ownerOf(p.card) == owner) ++cardsPlayedThisTurn;
    co_return;
  }
  Task<> beforeSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) cardsPlayedThisTurn = 0;
    co_return;
  }
};

struct WasteAwayPower : Power {
  POWER_HEADER(WasteAwayPower, "WASTE_AWAY_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  Dec modifyMaxEnergy(Dec amountIn) override { return amountIn - Dec(amount); }
};

// Curse of Knowledge choices: never enter a pile; choosing one applies its power.
struct KnowledgeChoice : Card {
  virtual Task<> onChosen() = 0;
};
template <class T> struct ChoiceT : KnowledgeChoice {
  std::unique_ptr<Card> clone() const override { return std::make_unique<T>(static_cast<const T&>(*this)); }
};
#define CHOICE_CARD(Name, Key, PowerT, Var, Amt)                                       \
  struct Name : ChoiceT<Name> {                                                        \
    Name() {                                                                           \
      id = #Name; locKey = Key; portrait = Key;                                        \
      cost = canonicalCost = -1; type = CardType::Status; rarity = Rarity::Status;     \
      target = TargetType::None; maxUpgradeLevel = 0;                                  \
      addVar(Var, Amt);                                                                \
    }                                                                                  \
    Task<> onChosen() override {                                                       \
      Creature* p = combat ? combat->player : nullptr;                                 \
      if (p) co_await applyPower<PowerT>(p, val(Var), p, this);                        \
    }                                                                                  \
  };
CHOICE_CARD(Disintegration, "DISINTEGRATION", DisintegrationPower, "DisintegrationPower", 6)
CHOICE_CARD(MindRot, "MIND_ROT", MindRotPower, "MindRotPower", 1)
CHOICE_CARD(Sloth, "SLOTH", SlothPower, "SlothPower", 3)
CHOICE_CARD(WasteAway, "WASTE_AWAY", WasteAwayPower, "WasteAwayPower", 1)
#undef CHOICE_CARD

struct KnowledgeDemon : Monster {
  MONSTER_HEADER(KnowledgeDemon, "KNOWLEDGE_DEMON")
  int curseOfKnowledgeCounter = 0;
  int minHp() const override { return 379; }
  int maxHp() const override { return 379; }

  Task<> chooseCurse(Creature* target) {
    if (target->dead()) co_return;
    static const int disintegration[3] = {6, 7, 8};
    static const char* second[3] = {"MindRot", "Sloth", "WasteAway"};
    int k = std::min(curseOfKnowledgeCounter, 2);
    Card* a = combat->addCard(db::card("Disintegration"));
    if (DynVar* v = a->var("DisintegrationPower")) v->base = disintegration[k];
    Card* b = combat->addCard(db::card(second[k]));
    auto picked = co_await cmd::selectCards(*combat, "monsters.KNOWLEDGE_DEMON.moves.CURSE_OF_KNOWLEDGE.title", {a, b}, 1, 1);
    if (!picked.empty()) co_await static_cast<KnowledgeChoice*>(picked[0])->onChosen();
  }

  void buildMoves() override {
    auto* curse = machine.add<MoveState>("CURSE_OF_KNOWLEDGE_MOVE");
    curse->perform = [this](Targets t) -> Task<> {
      if (curseOfKnowledgeCounter >= 3) co_return;  // C# throws; the branch never gets here
      for (Creature* target : t) co_await chooseCurse(target);
      if (!combat->ending) ++curseOfKnowledgeCounter;
    };
    curse->intents = {kindIntent(Intent::Debuff)};
    auto* slap = machine.add<MoveState>("SLAP_MOVE");
    slap->perform = [this](Targets) { return attack(17); };
    slap->intents = {attackIntent(17)};
    auto* overwhelm = machine.add<MoveState>("KNOWLEDGE_OVERWHELMING_MOVE");
    overwhelm->perform = [this](Targets) { return attack(8, 3); };
    overwhelm->intents = {attackIntent(8, 3)};
    auto* ponder = machine.add<MoveState>("PONDER_MOVE");
    ponder->perform = [this](Targets) -> Task<> {
      co_await attack(11);
      co_await cmd::heal(creature, 30);
      co_await applyToSelf<StrengthPower>(2);
    };
    ponder->intents = {attackIntent(11), kindIntent(Intent::Heal), kindIntent(Intent::Buff)};
    auto* branch = machine.add<ConditionalBranchState>("CurseOfKnowledgeBranch");
    curse->followUp = slap;
    slap->followUp = overwhelm;
    overwhelm->followUp = ponder;
    ponder->followUp = branch;
    branch->add(curse, [this] { return curseOfKnowledgeCounter < 3; });
    branch->add(slap, [this] { return curseOfKnowledgeCounter >= 3; });
    machine.start(curse);
  }
};

// ================================================================ Kaiser Crab

struct BackAttackLeftPower : Power {
  POWER_HEADER(BackAttackLeftPower, "BACK_ATTACK_LEFT_POWER")
  StackType stackType() const override { return StackType::Single; }
};
struct BackAttackRightPower : Power {
  POWER_HEADER(BackAttackRightPower, "BACK_ATTACK_RIGHT_POWER")
  StackType stackType() const override { return StackType::Single; }
};

// The player stands between the claws, facing one of them; the other hits their back for
// 50% more. Targeting a claw turns the player towards it. (The UI reads `facingLeft`.)
struct SurroundedPower : Power {
  POWER_HEADER(SurroundedPower, "SURROUNDED_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  StackType stackType() const override { return StackType::Single; }
  bool facingLeft = false;  // Direction.Right is the default

  Dec modifyDamageMultiplicative(Creature* target, Dec, int, Creature* dealer, Card*) override {
    if (!dealer || target != owner) return 1;
    bool behind = facingLeft ? dealer->get<BackAttackRightPower>() != nullptr : dealer->get<BackAttackLeftPower>() != nullptr;
    return behind ? Dec::lit(1.5) : Dec(1);
  }
  void updateDirection(Creature* target) {
    if (!facingLeft && target->get<BackAttackLeftPower>()) facingLeft = true;
    else if (facingLeft && target->get<BackAttackRightPower>()) facingLeft = false;
  }
  Task<> beforeCardPlayed(const CardPlay& p) override {
    if (p.target && ownerOf(p.card) == owner) updateDirection(p.target);
    co_return;
  }
  Task<> afterDeath(Creature* c) override {
    if (c->side == owner->side) co_return;
    auto hittable = owner->combat->hittableEnemies();
    if (hittable.empty()) co_return;
    bool allLeft = std::all_of(hittable.begin(), hittable.end(), [](Creature* e) { return e->get<BackAttackLeftPower>() != nullptr; });
    bool allRight = std::all_of(hittable.begin(), hittable.end(), [](Creature* e) { return e->get<BackAttackRightPower>() != nullptr; });
    if (allLeft || allRight) updateDirection(hittable[0]);
  }
};

// When the other claw dies: +6 Strength and 99 Block, once.
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

struct Crusher : Monster {
  MONSTER_HEADER(Crusher, "CRUSHER")
  int minHp() const override { return 209; }
  int maxHp() const override { return 209; }
  Task<> afterAddedToRoom() override {
    co_await applyToSelf<BackAttackLeftPower>(1);
    co_await applyToSelf<CrabRagePower>(1);
  }
  void buildMoves() override {
    auto* thrash = machine.add<MoveState>("THRASH_MOVE");
    thrash->perform = [this](Targets) { return attack(12); };
    thrash->intents = {attackIntent(12)};
    auto* enlarging = machine.add<MoveState>("ENLARGING_STRIKE_MOVE");
    enlarging->perform = [this](Targets) { return attack(4); };
    enlarging->intents = {attackIntent(4)};
    auto* sting = machine.add<MoveState>("BUG_STING_MOVE");
    sting->perform = [this](Targets t) -> Task<> {
      co_await attack(6, 2);
      co_await applyToTargets<WeakPower>(t, 2);
      co_await applyToTargets<FrailPower>(t, 2);
    };
    sting->intents = {attackIntent(6, 2), kindIntent(Intent::Debuff)};
    auto* adapt = machine.add<MoveState>("ADAPT_MOVE");
    adapt->perform = [this](Targets) { return applyToSelf<StrengthPower>(2); };
    adapt->intents = {kindIntent(Intent::Buff)};
    auto* guarded = machine.add<MoveState>("GUARDED_STRIKE_MOVE");
    guarded->perform = [this](Targets) -> Task<> {
      co_await attack(12);
      co_await gainBlock(18);
    };
    guarded->intents = {attackIntent(12), kindIntent(Intent::Defend)};
    thrash->followUp = enlarging;
    enlarging->followUp = sting;
    sting->followUp = adapt;
    adapt->followUp = guarded;
    guarded->followUp = thrash;
    machine.start(thrash);
  }
};

struct Rocket : Monster {
  MONSTER_HEADER(Rocket, "ROCKET")
  int minHp() const override { return 199; }
  int maxHp() const override { return 199; }
  Task<> afterAddedToRoom() override {
    co_await applyPower<SurroundedPower>(combat->player, 1, creature, nullptr);
    co_await applyToSelf<BackAttackRightPower>(1);
    co_await applyToSelf<CrabRagePower>(1);
  }
  void buildMoves() override {
    auto* reticle = machine.add<MoveState>("TARGETING_RETICLE_MOVE");
    reticle->perform = [this](Targets) { return attack(3); };
    reticle->intents = {attackIntent(3)};
    auto* beam = machine.add<MoveState>("PRECISION_BEAM_MOVE");
    beam->perform = [this](Targets) { return attack(18); };
    beam->intents = {attackIntent(18)};
    auto* charge = machine.add<MoveState>("CHARGE_UP_MOVE");
    charge->perform = [this](Targets) { return applyToSelf<StrengthPower>(2); };
    charge->intents = {kindIntent(Intent::Buff)};
    auto* laser = machine.add<MoveState>("LASER_MOVE");
    laser->perform = [this](Targets) { return attack(31); };
    laser->intents = {attackIntent(31)};
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

template <class P> void regPower() { db::registerPower(P::kId, [] { return std::unique_ptr<Power>(new P()); }); }
template <class C> void regCard(const char* id) { db::registerCard(id, [] { return std::unique_ptr<Card>(new C()); }); }

}  // namespace

void registerAct2C() {
  regPower<ReattachPower>();
  regPower<PersonalHivePower>();
  regPower<TaintedPower>();
  regPower<VitalSparkPower>();
  regPower<SandpitPower>();
  regPower<DisintegrationPower>();
  regPower<MindRotPower>();
  regPower<SlothPower>();
  regPower<WasteAwayPower>();
  regPower<BackAttackLeftPower>();
  regPower<BackAttackRightPower>();
  regPower<SurroundedPower>();
  regPower<CrabRagePower>();
  regCard<FranticEscape>("FranticEscape");
  regCard<Disintegration>("Disintegration");
  regCard<MindRot>("MindRot");
  regCard<Sloth>("Sloth");
  regCard<WasteAway>("WasteAway");

  db::registerEncounter("DecimillipedeElite", RoomType::Elite, false, [](Rng& rng) {
    // Consecutive starting moves so the three segments open with different attacks.
    int first = rng.nextInt(3);
    std::vector<std::unique_ptr<Monster>> v;
    auto front = std::make_unique<DecimillipedeSegmentFront>();
    auto middle = std::make_unique<DecimillipedeSegmentMiddle>();
    auto back = std::make_unique<DecimillipedeSegmentBack>();
    front->starterMoveIdx = first;
    middle->starterMoveIdx = (first + 1) % 3;
    back->starterMoveIdx = (first + 2) % 3;
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
