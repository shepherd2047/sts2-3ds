// Translated content: powers, Ironclad cards, act 1 monsters and encounters.
// Each class mirrors MegaCrit.Sts2.Core.Models.* of the same name; values are
// the non-ascension ones.
#include <algorithm>

#include "cards.h"

namespace sts {
struct StrikeIronclad : IroncladT<StrikeIronclad> {
  CARD_HEADER(StrikeIronclad, "STRIKE_IRONCLAD", 1, Attack, Basic, AnyEnemy)
    tags = tagStrike;
    addVar("Damage", 6);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

struct DefendIronclad : IroncladT<DefendIronclad> {
  CARD_HEADER(DefendIronclad, "DEFEND_IRONCLAD", 1, Skill, Basic, Self)
    tags = tagDefend;
    addVar("Block", 5);
  }
  Task<> onPlay(CardPlay&) override { co_await block(val("Block")); }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

struct Bash : IroncladT<Bash> {
  CARD_HEADER(Bash, "BASH", 2, Attack, Basic, AnyEnemy)
    addVar("Damage", 8);
    addVar("VulnerablePower", 2);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<VulnerablePower>(p.target, val("VulnerablePower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 2); upgradeVar("VulnerablePower", 1); }
};

struct Anger : IroncladT<Anger> {
  CARD_HEADER(Anger, "ANGER", 0, Attack, Common, AnyEnemy)
    addVar("Damage", 6);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    auto copy = clone();
    Card* c = combat->addCard(std::move(copy));
    co_await cmd::moveCard(*combat, c, Pile::Discard);
  }
  void onUpgrade() override { upgradeVar("Damage", 2); }
};

struct TwinStrike : IroncladT<TwinStrike> {
  CARD_HEADER(TwinStrike, "TWIN_STRIKE", 1, Attack, Common, AnyEnemy)
    tags = tagStrike;
    addVar("Damage", 5);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage"), 2); }
  void onUpgrade() override { upgradeVar("Damage", 2); }
};

struct SwordBoomerang : IroncladT<SwordBoomerang> {
  CARD_HEADER(SwordBoomerang, "SWORD_BOOMERANG", 1, Attack, Common, RandomEnemy)
    addVar("Damage", 3);
    addVar("Repeat", 3);
  }
  Task<> onPlay(CardPlay&) override { co_await attackRandom(val("Damage"), val("Repeat").toInt()); }
  void onUpgrade() override { upgradeVar("Repeat", 1); }
};

struct Breakthrough : IroncladT<Breakthrough> {
  CARD_HEADER(Breakthrough, "BREAKTHROUGH", 1, Attack, Common, AllEnemies)
    addVar("Damage", 9);
    addVar("HpLoss", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await loseHp(val("HpLoss"));
    co_await attackAll(val("Damage"));
  }
  void onUpgrade() override { upgradeVar("Damage", 4); }
};

struct Headbutt : IroncladT<Headbutt> {
  CARD_HEADER(Headbutt, "HEADBUTT", 1, Attack, Common, AnyEnemy)
    addVar("Damage", 9);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    auto picked = co_await cmd::selectCards(*combat, "HEADBUTT", combat->discard, 1, 1);
    if (!picked.empty()) co_await cmd::moveCard(*combat, picked[0], Pile::Draw, true);
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

struct Thunderclap : IroncladT<Thunderclap> {
  CARD_HEADER(Thunderclap, "THUNDERCLAP", 1, Attack, Common, AllEnemies)
    addVar("Damage", 4);
    addVar("VulnerablePower", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await attackAll(val("Damage"));
    for (Creature* e : combat->hittableEnemies()) co_await applyPower<VulnerablePower>(e, val("VulnerablePower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

struct BodySlam : IroncladT<BodySlam> {
  CARD_HEADER(BodySlam, "BODY_SLAM", 1, Attack, Common, AnyEnemy)
    addVar("CalculationBase", 0);
    addVar("ExtraDamage", 1);
    addVar("CalculatedDamage", 0);
    calcMultiplier = [](Card* c) { return c->combat ? c->combat->player->block : 0; };
  }
  Task<> onPlay(CardPlay& p) override { co_await attackCalculated(p.target); }
  void onUpgrade() override { cost -= 1; }
};

struct IronWave : IroncladT<IronWave> {
  CARD_HEADER(IronWave, "IRON_WAVE", 1, Attack, Common, AnyEnemy)
    addVar("Damage", 5);
    addVar("Block", 5);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await block(val("Block"));
    co_await attack(p.target, val("Damage"));
  }
  void onUpgrade() override { upgradeVar("Damage", 2); upgradeVar("Block", 2); }
};

struct PommelStrike : IroncladT<PommelStrike> {
  CARD_HEADER(PommelStrike, "POMMEL_STRIKE", 1, Attack, Common, AnyEnemy)
    tags = tagStrike;
    addVar("Damage", 9);
    addVar("Cards", 1);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await drawCards(val("Cards"));
  }
  void onUpgrade() override { upgradeVar("Damage", 1); upgradeVar("Cards", 1); }
};

struct Cinder : IroncladT<Cinder> {
  CARD_HEADER(Cinder, "CINDER", 2, Attack, Common, AnyEnemy)
    addVar("Damage", 18);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    Card* c = combat->rng("CombatCardSelection").nextItem(combat->hand);
    if (c) co_await cmd::exhaustCard(*combat, c);
  }
  void onUpgrade() override { upgradeVar("Damage", 6); }
};

struct SetupStrike : IroncladT<SetupStrike> {
  CARD_HEADER(SetupStrike, "SETUP_STRIKE", 1, Attack, Common, AnyEnemy)
    tags = tagStrike;
    addVar("Damage", 7);
    addVar("StrengthPower", 3);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<SetupStrikePower>(me(), val("StrengthPower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 2); upgradeVar("StrengthPower", 1); }
};

struct MoltenFist : IroncladT<MoltenFist> {
  CARD_HEADER(MoltenFist, "MOLTEN_FIST", 1, Attack, Common, AnyEnemy)
    keywords = kwExhaust;
    addVar("Damage", 10);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    int n = p.target->alive() ? p.target->powerAmount<VulnerablePower>() : 0;
    if (n > 0) co_await applyPower<VulnerablePower>(p.target, n, me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 4); }
};

struct PerfectedStrike : IroncladT<PerfectedStrike> {
  CARD_HEADER(PerfectedStrike, "PERFECTED_STRIKE", 2, Attack, Common, AnyEnemy)
    tags = tagStrike;
    addVar("CalculationBase", 6);
    addVar("ExtraDamage", 2);
    addVar("CalculatedDamage", 0);
    calcMultiplier = [](Card* c) {
      if (!c->combat) return 0;
      int n = 0;
      for (Card* x : c->combat->allCards()) if (x->tags & tagStrike) ++n;
      return n;
    };
  }
  Task<> onPlay(CardPlay& p) override { co_await attackCalculated(p.target); }
  void onUpgrade() override { upgradeVar("ExtraDamage", 1); }
};

struct Havoc : IroncladT<Havoc> {
  CARD_HEADER(Havoc, "HAVOC", 1, Skill, Common, Self)
  }
  Task<> onPlay(CardPlay&) override { co_await cmd::autoPlayFromDrawPile(*combat, 1, true); }
  void onUpgrade() override { cost -= 1; }
};

struct Tremble : IroncladT<Tremble> {
  CARD_HEADER(Tremble, "TREMBLE", 1, Skill, Common, AnyEnemy)
    keywords = kwExhaust;
    addVar("VulnerablePower", 3);
  }
  Task<> onPlay(CardPlay& p) override { co_await applyPower<VulnerablePower>(p.target, val("VulnerablePower"), me(), this); }
  void onUpgrade() override { upgradeVar("VulnerablePower", 1); }
};

struct ShrugItOff : IroncladT<ShrugItOff> {
  CARD_HEADER(ShrugItOff, "SHRUG_IT_OFF", 1, Skill, Common, Self)
    addVar("Block", 8);
    addVar("Cards", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    co_await drawCards(val("Cards"));
  }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

struct Armaments : IroncladT<Armaments> {
  CARD_HEADER(Armaments, "ARMAMENTS", 1, Skill, Common, Self)
    addVar("Block", 5);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    if (upgraded()) {
      for (Card* c : combat->hand) if (c->upgradable()) c->upgrade();
      co_return;
    }
    std::vector<Card*> opts;
    for (Card* c : combat->hand) if (c->upgradable()) opts.push_back(c);
    auto picked = co_await cmd::selectCards(*combat, "ARMAMENTS", opts, 1, 1);
    if (!picked.empty()) picked[0]->upgrade();
  }
};

struct Taunt : IroncladT<Taunt> {
  CARD_HEADER(Taunt, "TAUNT", 1, Skill, Common, AnyEnemy)
    addVar("Block", 6);
    addVar("VulnerablePower", 1);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await block(val("Block"));
    co_await applyPower<VulnerablePower>(p.target, val("VulnerablePower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Block", 1); upgradeVar("VulnerablePower", 1); }
};

struct BloodWall : IroncladT<BloodWall> {
  CARD_HEADER(BloodWall, "BLOOD_WALL", 2, Skill, Common, Self)
    addVar("HpLoss", 2);
    addVar("Block", 16);
  }
  Task<> onPlay(CardPlay&) override {
    co_await loseHp(val("HpLoss"));
    co_await block(val("Block"));
  }
  void onUpgrade() override { upgradeVar("Block", 4); }
};

struct TrueGrit : IroncladT<TrueGrit> {
  CARD_HEADER(TrueGrit, "TRUE_GRIT", 1, Skill, Common, Self)
    addVar("Block", 7);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    if (upgraded()) {
      auto picked = co_await cmd::selectCards(*combat, "TRUE_GRIT", combat->hand, 1, 1);
      if (!picked.empty()) co_await cmd::exhaustCard(*combat, picked[0]);
      co_return;
    }
    Card* c = combat->rng("CombatCardSelection").nextItem(combat->hand);
    if (c) co_await cmd::exhaustCard(*combat, c);
  }
  void onUpgrade() override { upgradeVar("Block", 2); }
};

struct GiantRock : IroncladT<GiantRock> {
  CARD_HEADER(GiantRock, "GIANT_ROCK", 1, Attack, Token, AnyEnemy)
    addVar("Damage", 20);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
  void onUpgrade() override { upgradeVar("Damage", 4); }
};

struct Slimed : IroncladT<Slimed> {
  CARD_HEADER(Slimed, "SLIMED", 1, Status, Status, None)
    keywords = kwExhaust;
    maxUpgradeLevel = 0;
    addVar("Cards", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await drawCards(val("Cards")); }
};

struct Wound : IroncladT<Wound> {
  CARD_HEADER(Wound, "WOUND", -1, Status, Status, None)
    keywords = kwUnplayable;
    maxUpgradeLevel = 0;
  }
};

// ================================================================ relics

struct BurningBlood : Relic {
  RELIC_HEADER(BurningBlood, "BURNING_BLOOD", Starter)
    addVar("Heal", 6);
  }
  Task<> afterCombatVictory() override {
    Creature* p = owner();
    if (p->dead()) co_return;
    doFlash();
    co_await cmd::heal(p, val("Heal"));
  }
};

// RelicFactory.FallbackRelic: handed out when a grab bag runs dry.
struct Circlet : Relic {
  RELIC_HEADER(Circlet, "CIRCLET", None)
  }
};

// ================================================================ monsters

#define MONSTER_HEADER(Name, Key) \
  Name() { id = #Name; locKey = Key; }

using Targets = const std::vector<Creature*>&;

inline Intent attackIntent(int dmg, int hits = 1) { Intent i; i.kind = Intent::Attack; i.damage = dmg; i.hits = hits; return i; }
inline Intent kindIntent(Intent::Kind k, int count = 0) { Intent i; i.kind = k; i.count = count; return i; }

struct Nibbit : Monster {
  MONSTER_HEADER(Nibbit, "NIBBIT")
  bool isFront = false, isAlone = false;
  int minHp() const override { return asc(kToughEnemies, 44, 42); }
  int maxHp() const override { return asc(kToughEnemies, 48, 46); }
  void buildMoves() override {
    auto* butt = machine.add<MoveState>("BUTT_MOVE");
    butt->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 13, 12)); };
    butt->intents = {attackIntent(asc(kDeadlyEnemies, 13, 12))};
    auto* slice = machine.add<MoveState>("SLICE_MOVE");
    slice->perform = [this](Targets) { return sliceMove(); };
    slice->intents = {attackIntent(asc(kDeadlyEnemies, 7, 6)), kindIntent(Intent::Defend)};
    auto* hiss = machine.add<MoveState>("HISS_MOVE");
    hiss->perform = [this](Targets) { return applyToSelf<StrengthPower>(asc(kDeadlyEnemies, 3, 2)); };
    hiss->intents = {kindIntent(Intent::Buff)};
    auto* init = machine.add<ConditionalBranchState>("INIT_MOVE");
    if (isAlone) {
      init->add(butt, [this] { return isAlone; });
    } else {
      init->add(hiss, [this] { return !isFront; });
      init->add(slice, [this] { return isFront; });
    }
    slice->followUp = hiss;
    butt->followUp = slice;
    hiss->followUp = butt;
    machine.start(init);
  }
  Task<> sliceMove() {
    co_await attack(asc(kDeadlyEnemies, 7, 6));
    co_await gainBlock(asc(kToughEnemies, 6, 5));
  }
};

struct LeafSlimeS : Monster {
  MONSTER_HEADER(LeafSlimeS, "LEAF_SLIME_S")
  int minHp() const override { return asc(kToughEnemies, 12, 11); }
  int maxHp() const override { return asc(kToughEnemies, 16, 15); }
  void buildMoves() override {
    auto* tackle = machine.add<MoveState>("TACKLE_MOVE");
    tackle->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 4, 3)); };
    tackle->intents = {attackIntent(asc(kDeadlyEnemies, 4, 3))};
    auto* goop = machine.add<MoveState>("GOOP_MOVE");
    goop->perform = [this](Targets) { return cmd::addStatusCards(*combat, "Slimed", Pile::Discard, 1); };
    goop->intents = {kindIntent(Intent::Status, 1)};
    auto* rand = machine.add<RandomBranchState>("RAND");
    tackle->followUp = goop->followUp = rand;
    rand->add(tackle, MoveRepeat::CannotRepeat);
    rand->add(goop, MoveRepeat::CannotRepeat);
    machine.start(rand);
  }
};

struct TwigSlimeS : Monster {
  MONSTER_HEADER(TwigSlimeS, "TWIG_SLIME_S")
  int minHp() const override { return asc(kToughEnemies, 8, 7); }
  int maxHp() const override { return asc(kToughEnemies, 12, 11); }
  void buildMoves() override {
    auto* tackle = machine.add<MoveState>("TACKLE_MOVE");
    tackle->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 5, 4)); };
    tackle->intents = {attackIntent(asc(kDeadlyEnemies, 5, 4))};
    tackle->followUp = tackle;
    machine.start(tackle);
  }
};

struct LeafSlimeM : Monster {
  MONSTER_HEADER(LeafSlimeM, "LEAF_SLIME_M")
  int minHp() const override { return asc(kToughEnemies, 33, 32); }
  int maxHp() const override { return asc(kToughEnemies, 36, 35); }
  void buildMoves() override {
    auto* clump = machine.add<MoveState>("CLUMP_SHOT");
    clump->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 9, 8)); };
    clump->intents = {attackIntent(asc(kDeadlyEnemies, 9, 8))};
    auto* sticky = machine.add<MoveState>("STICKY_SHOT");
    sticky->perform = [this](Targets) { return cmd::addStatusCards(*combat, "Slimed", Pile::Discard, 2); };
    sticky->intents = {kindIntent(Intent::Status, 2)};
    sticky->followUp = clump;
    clump->followUp = sticky;
    machine.start(sticky);
  }
};

struct TwigSlimeM : Monster {
  MONSTER_HEADER(TwigSlimeM, "TWIG_SLIME_M")
  int minHp() const override { return asc(kToughEnemies, 27, 26); }
  int maxHp() const override { return asc(kToughEnemies, 29, 28); }
  void buildMoves() override {
    auto* pounce = machine.add<MoveState>("POKEY_POUNCE_MOVE");
    pounce->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 12, 11)); };
    pounce->intents = {attackIntent(asc(kDeadlyEnemies, 12, 11))};
    auto* sticky = machine.add<MoveState>("STICKY_SHOT_MOVE");
    sticky->perform = [this](Targets) { return cmd::addStatusCards(*combat, "Slimed", Pile::Discard, 1); };
    sticky->intents = {kindIntent(Intent::Status, 1)};
    auto* rand = machine.add<RandomBranchState>("RAND");
    pounce->followUp = sticky->followUp = rand;
    rand->addMax(pounce, 2);
    rand->add(sticky, MoveRepeat::CannotRepeat);
    machine.start(sticky);
  }
};

struct ShrinkerBeetle : Monster {
  MONSTER_HEADER(ShrinkerBeetle, "SHRINKER_BEETLE")
  int minHp() const override { return asc(kToughEnemies, 40, 38); }
  int maxHp() const override { return asc(kToughEnemies, 42, 40); }
  void buildMoves() override {
    auto* shrink = machine.add<MoveState>("SHRINKER_MOVE");
    shrink->perform = [this](Targets t) { return applyToTargets<ShrinkPower>(t, -1); };
    shrink->intents = {kindIntent(Intent::DebuffStrong)};
    auto* chomp = machine.add<MoveState>("CHOMP_MOVE");
    chomp->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 8, 7)); };
    chomp->intents = {attackIntent(asc(kDeadlyEnemies, 8, 7))};
    auto* stomp = machine.add<MoveState>("STOMP_MOVE");
    stomp->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 14, 13)); };
    stomp->intents = {attackIntent(asc(kDeadlyEnemies, 14, 13))};
    shrink->followUp = chomp;
    chomp->followUp = stomp;
    stomp->followUp = chomp;
    machine.start(shrink);
  }
};

struct Inklet : Monster {
  MONSTER_HEADER(Inklet, "INKLET")
  bool middle = false;
  int minHp() const override { return asc(kToughEnemies, 12, 11); }
  int maxHp() const override { return asc(kToughEnemies, 18, 17); }
  Task<> afterAddedToRoom() override { co_await applyToSelf<SlipperyPower>(1); }
  void buildMoves() override {
    auto* jab = machine.add<MoveState>("JAB_MOVE");
    jab->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 4, 3)); };
    jab->intents = {attackIntent(asc(kDeadlyEnemies, 4, 3))};
    auto* whirl = machine.add<MoveState>("WHIRLWIND_MOVE");
    whirl->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 3, 2), 3); };
    whirl->intents = {attackIntent(asc(kDeadlyEnemies, 3, 2), 3)};
    auto* gaze = machine.add<MoveState>("PIERCING_GAZE_MOVE");
    gaze->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 11, 10)); };
    gaze->intents = {attackIntent(asc(kDeadlyEnemies, 11, 10))};
    // INIT_RAND is built but never registered or used as the initial state.
    auto* rand = machine.add<RandomBranchState>("RAND");
    rand->add(gaze, MoveRepeat::CannotRepeat);
    rand->add(whirl, MoveRepeat::CannotRepeat);
    jab->followUp = rand;
    whirl->followUp = jab;
    gaze->followUp = jab;
    machine.start(middle ? (MonsterState*)whirl : jab);
  }
};

struct Mawler : Monster {
  MONSTER_HEADER(Mawler, "MAWLER")
  int minHp() const override { return asc(kToughEnemies, 76, 72); }
  int maxHp() const override { return minHp(); }
  void buildMoves() override {
    auto* rip = machine.add<MoveState>("RIP_AND_TEAR_MOVE");
    rip->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 16, 14)); };
    rip->intents = {attackIntent(asc(kDeadlyEnemies, 16, 14))};
    auto* roar = machine.add<MoveState>("ROAR_MOVE");
    roar->perform = [this](Targets t) { return applyToTargets<VulnerablePower>(t, 3); };
    roar->intents = {kindIntent(Intent::Debuff)};
    auto* claw = machine.add<MoveState>("CLAW_MOVE");
    claw->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 5, 4), 2); };
    claw->intents = {attackIntent(asc(kDeadlyEnemies, 5, 4), 2)};
    auto* rand = machine.add<RandomBranchState>("RAND");
    rip->followUp = roar->followUp = claw->followUp = rand;
    rand->add(rip, MoveRepeat::CannotRepeat);
    rand->add(roar, MoveRepeat::UseOnlyOnce);
    rand->add(claw, MoveRepeat::CannotRepeat);
    machine.start(claw);
  }
};

struct FuzzyWurmCrawler : Monster {
  MONSTER_HEADER(FuzzyWurmCrawler, "FUZZY_WURM_CRAWLER")
  int minHp() const override { return asc(kToughEnemies, 58, 55); }
  int maxHp() const override { return asc(kToughEnemies, 59, 57); }
  void buildMoves() override {
    auto* first = machine.add<MoveState>("FIRST_ACID_GOOP");
    first->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 6, 4)); };
    first->intents = {attackIntent(asc(kDeadlyEnemies, 6, 4))};
    auto* goop = machine.add<MoveState>("ACID_GOOP");
    goop->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 6, 4)); };
    goop->intents = {attackIntent(asc(kDeadlyEnemies, 6, 4))};
    auto* inhale = machine.add<MoveState>("INHALE");
    inhale->perform = [this](Targets) { return applyToSelf<StrengthPower>(7); };
    inhale->intents = {kindIntent(Intent::Buff)};
    first->followUp = inhale;
    inhale->followUp = goop;
    goop->followUp = first;
    machine.start(first);
  }
};

struct Byrdonis : Monster {
  MONSTER_HEADER(Byrdonis, "BYRDONIS")
  int minHp() const override { return asc(kToughEnemies, 90, 81); }
  int maxHp() const override { return asc(kToughEnemies, 90, 84); }
  Task<> afterAddedToRoom() override { co_await applyToSelf<TerritorialPower>(1); }
  void buildMoves() override {
    auto* peck = machine.add<MoveState>("PECK_MOVE");
    peck->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 4, 3), asc(kDeadlyEnemies, 3, 3)); };
    peck->intents = {attackIntent(asc(kDeadlyEnemies, 4, 3), asc(kDeadlyEnemies, 3, 3))};
    auto* swoop = machine.add<MoveState>("SWOOP_MOVE");
    swoop->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 19, 17)); };
    swoop->intents = {attackIntent(asc(kDeadlyEnemies, 19, 17))};
    swoop->followUp = peck;
    peck->followUp = swoop;
    machine.start(swoop);
  }
};

struct Vantom : Monster {
  MONSTER_HEADER(Vantom, "VANTOM")
  int minHp() const override { return asc(kToughEnemies, 183, 173); }
  int maxHp() const override { return minHp(); }
  Task<> afterAddedToRoom() override { co_await applyToSelf<SlipperyPower>(asc(kToughEnemies, 9, 8)); }
  void buildMoves() override {
    auto* blot = machine.add<MoveState>("INK_BLOT_MOVE");
    blot->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 8, 7)); };
    blot->intents = {attackIntent(asc(kDeadlyEnemies, 8, 7))};
    auto* lance = machine.add<MoveState>("INKY_LANCE_MOVE");
    lance->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 7, 6), 2); };
    lance->intents = {attackIntent(asc(kDeadlyEnemies, 7, 6), 2)};
    auto* dismember = machine.add<MoveState>("DISMEMBER_MOVE");
    dismember->perform = [this](Targets) { return dismemberMove(); };
    dismember->intents = {attackIntent(asc(kDeadlyEnemies, 30, 26)), kindIntent(Intent::Status, 3)};
    auto* prepare = machine.add<MoveState>("PREPARE_MOVE");
    prepare->perform = [this](Targets) { return applyToSelf<StrengthPower>(2); };
    prepare->intents = {kindIntent(Intent::Buff)};
    blot->followUp = lance;
    lance->followUp = dismember;
    dismember->followUp = prepare;
    prepare->followUp = blot;
    machine.start(blot);
  }
  Task<> dismemberMove() {
    co_await wait(0.25);
    co_await attack(asc(kDeadlyEnemies, 30, 26));
    co_await wait(0.5);
    co_await cmd::addStatusCards(*combat, "Wound", Pile::Discard, 3);
  }
};

// Factory functions so content_act1.cpp (Overgrowth/Flyconid/etc. encounters) can place
// these monsters in its own encounters without duplicating their definitions.
std::unique_ptr<Monster> makeShrinkerBeetle() { return std::make_unique<ShrinkerBeetle>(); }
std::unique_ptr<Monster> makeFuzzyWurmCrawler() { return std::make_unique<FuzzyWurmCrawler>(); }
std::unique_ptr<Monster> makeLeafSlimeS() { return std::make_unique<LeafSlimeS>(); }
std::unique_ptr<Monster> makeTwigSlimeS() { return std::make_unique<TwigSlimeS>(); }
std::unique_ptr<Monster> makeLeafSlimeM() { return std::make_unique<LeafSlimeM>(); }
std::unique_ptr<Monster> makeTwigSlimeM() { return std::make_unique<TwigSlimeM>(); }

// ================================================================ registry

namespace {
std::map<std::string, CardFactory>& cardReg() { static std::map<std::string, CardFactory> m; return m; }
std::map<std::string, PowerFactory>& powerReg() { static std::map<std::string, PowerFactory> m; return m; }
std::map<std::string, Encounter>& encounterReg() { static std::map<std::string, Encounter> m; return m; }
std::map<std::string, RelicFactoryFn>& relicReg() { static std::map<std::string, RelicFactoryFn> m; return m; }
std::map<std::string, OrbFactory>& orbReg() { static std::map<std::string, OrbFactory> m; return m; }

template <class C> void regCard() { cardReg()[C().id] = [] { return std::unique_ptr<Card>(new C()); }; }
template <class P> void regPower() { powerReg()[P::kId] = [] { return std::unique_ptr<Power>(new P()); }; }

template <class M> std::unique_ptr<Monster> mk() { return std::make_unique<M>(); }

void regEncounter(const std::string& id, RoomType room, bool weak,
                  std::function<std::vector<std::unique_ptr<Monster>>(Rng&)> gen) {
  encounterReg()[id] = Encounter{id, room, weak, std::move(gen)};
}

template <class... Ms> std::vector<std::unique_ptr<Monster>> list() {
  std::vector<std::unique_ptr<Monster>> v;
  (v.push_back(mk<Ms>()), ...);
  return v;
}
}  // namespace

// Filled in by content_powers.cpp, content_uncommon.cpp, content_rare.cpp.
void registerIroncladPowers();
void registerIroncladUncommon();
void registerIroncladRare();
// Filled in by content_act1.cpp.
void registerAct1Monsters();
void registerPhrog();        // content_phrog.cpp
void registerAct2A();        // content_act2a.cpp
void registerAct2B();        // content_act2b.cpp
void registerAct2C();        // content_act2c.cpp: elites + bosses
void registerAct3A();        // content_act3a.cpp
void registerAct3B();        // content_act3b.cpp
void registerUnderdocksB();  // content_underdocks_b.cpp
void registerUnderdocksA();  // content_underdocks_a.cpp
void registerUnderdocksC();  // content_underdocks_c.cpp: Underdocks elites
void registerUnderdocksD();  // content_underdocks_d.cpp: Underdocks bosses
void registerAct1Bosses();   // content_bosses.cpp: Ceremonial Beast, The Kin, Fogmog
void registerRelics();       // relics*.cpp
void registerEvents();       // events.cpp
void registerAncients();     // ancients.cpp: Neow
void registerAncientsLater(); // ancients_later.cpp: acts 2-3 and Darv
void registerPotions();      // potions.cpp
void registerEnchantments(); // enchantments.cpp
void registerEnchantmentsB(); // enchantments_b.cpp (A3b)
void registerEnchantmentsC(); // enchantments_c.cpp (A3c)
void registerSilent();       // char_silent.cpp
void registerNecrobinder();  // char_necrobinder.cpp
void registerRegent();       // char_regent.cpp
void registerDefect();       // char_defect.cpp
void registerAscension();    // content_ascension.cpp
void registerMiscCards();    // content_cards_misc.cpp
void registerColorlessA();   // colorless_cards_a.cpp (+ colorless_pool.cpp)
void registerColorlessB();   // colorless_cards_b.cpp
void registerColorlessC();   // colorless_cards_c.cpp

namespace db {

void registerCard(const std::string& id, CardFactory f) { cardReg()[id] = f; }
void registerPower(const std::string& id, PowerFactory f) { powerReg()[id] = f; }
void registerRelic(const std::string& id, RelicFactoryFn f) { relicReg()[id] = f; }
bool relicRegistered(const std::string& id) { return relicReg().count(id) > 0; }
std::vector<std::string> relicIds() {
  std::vector<std::string> ids;
  for (auto& [k, f] : relicReg()) ids.push_back(k);
  return ids;
}
void registerOrb(const std::string& id, OrbFactory f) { orbReg()[id] = f; }

const std::vector<std::string>& sharedRelicPool() {
  static const std::vector<std::string> pool = {"Akabeko", "AmethystAubergine", "Anchor", "ArtOfWar", "BagOfMarbles", "BagOfPreparation", "BeatingRemnant", "Bellows", "BeltBuckle", "BloodVial", "BookOfFiveRings", "BowlerHat", "Bread", "BronzeScales", "BurningSticks", "Candelabra", "CaptainsWheel", "Cauldron", "CentennialPuzzle", "Chandelier", "ChemicalX", "CloakClasp", "DingyRug", "DollysMirror", "DragonFruit", "EternalFeather", "FestivePopper", "FresnelLens", "FrozenEgg", "GamblingChip", "GamePiece", "GhostSeed", "Girya", "GnarledHammer", "Gorget", "GremlinHorn", "HappyFlower", "HornCleat", "IceCream", "IntimidatingHelmet", "JossPaper", "JuzuBracelet", "Kifuda", "Kunai", "Kusarigama", "Lantern", "LastingCandy", "LavaLamp", "LeesWaffle", "LetterOpener", "LizardTail", "LoomingFruit", "LuckyFysh", "Mango", "MealTicket", "MeatOnTheBone", "MembershipCard", "MercuryHourglass", "MiniatureCannon", "MiniatureTent", "MoltenEgg", "MummifiedHand", "MysticLighter", "Nunchaku", "OddlySmoothStone", "OldCoin", "Orichalcum", "OrnamentalFan", "Orrery", "Pantograph", "ParryingShield", "Pear", "PenNib", "Pendulum", "Permafrost", "PetrifiedToad", "Planisphere", "Pocketwatch", "PotionBelt", "PrayerWheel", "PunchDagger", "RainbowRing", "RazorTooth", "RedMask", "RegalPillow", "ReptileTrinket", "RingingTriangle", "RippleBasin", "RoyalStamp", "ScreamingFlagon", "Shovel", "Shuriken", "SlingOfCourage", "SparklingRouge", "StoneCalendar", "StoneCracker", "Strawberry", "StrikeDummy", "SturdyClamp", "TheAbacus", "TheCourier", "TinyMailbox", "Toolbox", "ToxicEgg", "TungstenRod", "TuningFork", "UnceasingTop", "UnsettlingLamp", "Vajra", "Vambrace", "VenerableTeaSet", "VeryHotCocoa", "VexingPuzzlebox", "WarPaint", "Whetstone", "WhiteBeastStatue", "WhiteStar", "WingCharm"};
  return pool;
}
void registerEncounter(const std::string& id, RoomType room, bool weak,
                        std::function<std::vector<std::unique_ptr<Monster>>(Rng&)> gen) {
  regEncounter(id, room, weak, std::move(gen));
}



void init() {
  static bool done = false;
  if (done) return;
  done = true;
  registerIroncladPowers();
  registerIroncladUncommon();
  registerIroncladRare();

  regCard<StrikeIronclad>(); regCard<DefendIronclad>(); regCard<Bash>();
  regCard<Anger>(); regCard<TwinStrike>(); regCard<SwordBoomerang>(); regCard<Breakthrough>();
  regCard<Headbutt>(); regCard<Thunderclap>(); regCard<BodySlam>(); regCard<IronWave>();
  regCard<PommelStrike>(); regCard<Cinder>(); regCard<SetupStrike>(); regCard<MoltenFist>();
  regCard<PerfectedStrike>(); regCard<Havoc>(); regCard<Tremble>(); regCard<ShrugItOff>();
  regCard<Armaments>(); regCard<Taunt>(); regCard<BloodWall>(); regCard<TrueGrit>();
  regCard<Slimed>(); regCard<Wound>(); regCard<GiantRock>();

  regPower<StrengthPower>(); regPower<DexterityPower>(); regPower<VulnerablePower>();
  regPower<WeakPower>(); regPower<FrailPower>(); regPower<ShrinkPower>();
  regPower<SlipperyPower>(); regPower<TerritorialPower>(); regPower<SetupStrikePower>();

  regEncounter("NibbitsWeak", RoomType::Monster, true, [](Rng&) {
    auto n = std::make_unique<Nibbit>();
    n->isAlone = true;
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(std::move(n));
    return v;
  });
  regEncounter("SlimesWeak", RoomType::Monster, true, [](Rng& rng) {
    // Two different small slimes around one medium slime.
    std::vector<int> smalls{0, 1};
    int a = rng.nextItem(smalls);
    smalls.erase(std::find(smalls.begin(), smalls.end(), a));
    int b = rng.nextItem(smalls);
    auto small = [](int i) -> std::unique_ptr<Monster> { return i == 0 ? mk<LeafSlimeS>() : mk<TwigSlimeS>(); };
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(small(a));
    v.push_back(rng.nextInt(2) == 0 ? mk<LeafSlimeM>() : mk<TwigSlimeM>());
    v.push_back(small(b));
    return v;
  });
  regEncounter("ShrinkerBeetleWeak", RoomType::Monster, true, [](Rng&) { return list<ShrinkerBeetle>(); });
  regEncounter("FuzzyWurmCrawlerWeak", RoomType::Monster, true, [](Rng&) { return list<FuzzyWurmCrawler>(); });
  regEncounter("InkletsNormal", RoomType::Monster, false, [](Rng&) {
    auto v = list<Inklet, Inklet, Inklet>();
    static_cast<Inklet*>(v[1].get())->middle = true;
    return v;
  });
  regEncounter("MawlerNormal", RoomType::Monster, false, [](Rng&) { return list<Mawler>(); });
  regEncounter("NibbitsNormal", RoomType::Monster, false, [](Rng&) {
    auto v = list<Nibbit, Nibbit>();
    static_cast<Nibbit*>(v[0].get())->isFront = true;
    return v;
  });
  regEncounter("SlimesNormal", RoomType::Monster, false, [](Rng& rng) {
    bool flag = rng.nextBool();
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(mk<TwigSlimeM>());
    v.push_back(mk<LeafSlimeM>());
    v.push_back(flag ? mk<LeafSlimeS>() : mk<TwigSlimeS>());
    v.push_back(flag ? mk<TwigSlimeS>() : mk<LeafSlimeS>());
    return v;
  });
  regEncounter("ByrdonisElite", RoomType::Elite, false, [](Rng&) { return list<Byrdonis>(); });
  regEncounter("VantomBoss", RoomType::Boss, false, [](Rng&) { return list<Vantom>(); });

  registerAct1Monsters();
  registerPhrog();
  registerAct1Bosses();
  registerAct2A();
  registerAct2B();
  registerAct2C();
  registerAct3A();
  registerAct3B();
  registerUnderdocksB();
  registerUnderdocksA();
  registerUnderdocksC();
  registerUnderdocksD();
  registerRelic("BurningBlood", [] { return std::unique_ptr<Relic>(new BurningBlood()); });
  registerRelic("Circlet", [] { return std::unique_ptr<Relic>(new Circlet()); });
  registerEnchantments();
  registerEnchantmentsB();
  registerEnchantmentsC();
  registerSilent();
  registerNecrobinder();
  registerRegent();
  registerDefect();
  registerAscension();
  registerMiscCards();
  registerColorlessA();
  registerColorlessB();
  registerColorlessC();
  registerRelics();
  registerEvents();
  registerAncients();
  registerAncientsLater();
  registerPotions();
}

std::unique_ptr<Card> card(const std::string& id) {
  auto it = cardReg().find(id);
  return it == cardReg().end() ? nullptr : it->second();
}
std::unique_ptr<Power> power(const std::string& id) {
  auto it = powerReg().find(id);
  return it == powerReg().end() ? nullptr : it->second();
}
std::vector<std::string> cardIds() {
  std::vector<std::string> ids;
  ids.reserve(cardReg().size());
  for (auto& [k, f] : cardReg()) ids.push_back(k);
  return ids;
}
std::vector<std::string> encounterIds() {
  std::vector<std::string> ids;
  for (auto& [k, e] : encounterReg()) ids.push_back(k);
  return ids;
}
const Encounter* encounter(const std::string& id) {
  auto it = encounterReg().find(id);
  return it == encounterReg().end() ? nullptr : &it->second;
}
std::unique_ptr<Orb> orb(const std::string& id) {
  auto it = orbReg().find(id);
  return it == orbReg().end() ? nullptr : it->second();
}
std::unique_ptr<Orb> randomOrb(Rng& rng) {
  // OrbModel.GetRandomOrb: uniform among the 5 orbs, in ModelDb registration order.
  static const std::vector<std::string> ids = {"LightningOrb", "FrostOrb", "DarkOrb", "PlasmaOrb", "GlassOrb"};
  return orb(rng.nextItem(ids));
}
std::unique_ptr<Relic> relic(const std::string& id) {
  auto it = relicReg().find(id);
  return it == relicReg().end() ? nullptr : it->second();
}

std::vector<std::string> ironcladRewardPool() {
  return {"Anger", "TwinStrike", "SwordBoomerang", "Breakthrough", "Headbutt", "Thunderclap",
          "BodySlam", "IronWave", "PommelStrike", "Cinder", "SetupStrike", "MoltenFist",
          "PerfectedStrike", "Havoc", "Tremble", "ShrugItOff", "Armaments", "Taunt",
          "BloodWall", "TrueGrit"};
}

}  // namespace db
}  // namespace sts
