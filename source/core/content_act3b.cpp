// Act 3 (Glory) elites and bosses, package 6b: Knights (FlailKnight, SpectralKnight,
// MagiKnight), MechaKnight, SoulNexus, Queen (+ TorchHeadAmalgam), TestSubject, Aeonglass,
// and their powers (Hex, Dampen, ChainsOfBinding, Intangible, Nemesis, PainfulStabs,
// WitheringPresence, Adaptable, Enrage) and cards (Burn, Wither).
// Translated from MegaCrit.Sts2.Core.Models.Monsters / .Powers / .Encounters.
// Values are the non-ascension ones (see docs/PORTING.md).
#include <algorithm>
#include <set>

#include "cards.h"
#include "powers.h"

namespace sts {

namespace {

#define MONSTER_HEADER(Name, Key) \
  Name() { id = #Name; locKey = Key; }

using Targets = const std::vector<Creature*>&;

Intent attackIntent(int dmg, int hits = 1) { Intent i; i.kind = Intent::Attack; i.damage = dmg; i.hits = hits; return i; }
Intent kindIntent(Intent::Kind k, int count = 0) { Intent i; i.kind = k; i.count = count; return i; }

Task<> applyById(const char* powerId, Creature* target, Dec amount, Creature* applier) {
  co_await cmd::applyPower(db::power(powerId), target, amount, applier, nullptr);
}

// ================================================================ cards

struct Burn : IroncladT<Burn> {
  CARD_HEADER(Burn, "BURN", -1, Status, Status, None)
    keywords = kwUnplayable;
    maxUpgradeLevel = 0;
    addVar("Damage", 2);
  }
  bool hasTurnEndInHandEffect() const override { return true; }
  Task<> onTurnEndInHand() override { co_await cmd::damage(me(), val("Damage"), kUnpowered | kMove, nullptr, this); }
};

// PORT NOTE: the portrait / "+N" title of a fake-upgraded Wither is not shown.
struct Wither : IroncladT<Wither> {
  CARD_HEADER(Wither, "WITHER", -1, Status, Status, None)
    keywords = kwUnplayable;
    maxUpgradeLevel = 0;
    addVar("Damage", 3);
  }
  bool hasTurnEndInHandEffect() const override { return true; }
  Task<> onTurnEndInHand() override { co_await cmd::damage(me(), val("Damage"), kUnpowered | kMove, nullptr, this); }
  void fakeUpgrade() { upgradeVar("Damage", 3); }
};

// ================================================================ powers

// IntangiblePower.cs: HP lost is capped at 1; ticks down at the end of the enemy turn.
// PORT NOTE: no damage-cap hook, so only the HP loss (after block) is capped.
struct IntangiblePower : Power {
  POWER_HEADER(IntangiblePower, "INTANGIBLE_POWER")
  Dec modifyHpLostAfterOsty(Creature* target, Dec amount, int, Creature*, Card*) override {
    if (target != owner || amount < Dec(1)) return amount;
    flash = 1.f;
    return Dec(1);
  }
  Task<> afterSideTurnEnd(Side side, const std::vector<Creature*>&) override {
    if (side == Side::Enemy) co_await cmd::decrement(this);
  }
};

// NemesisPower.cs: alternate turns of Intangible.
struct NemesisPower : Power {
  POWER_HEADER(NemesisPower, "NEMESIS_POWER")
  StackType stackType() const override { return StackType::Single; }
  bool shouldApply = false;
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner)) co_return;
    shouldApply = !shouldApply;
    if (shouldApply) {
      flash = 1.f;
      co_await applyById("IntangiblePower", owner, 1, owner);
    } else if (Power* p = owner->power("IntangiblePower")) {
      co_await cmd::removePower(p);
    }
  }
};

// EnragePower.cs: gains Strength whenever a Skill is played.
struct EnragePower : Power {
  POWER_HEADER(EnragePower, "ENRAGE_POWER")
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (p.card->type != CardType::Skill) co_return;
    flash = 1.f;
    co_await applyPower<StrengthPower>(owner, Dec(amount), owner, nullptr);
  }
};

// PainfulStabsPower.cs: each unblocked powered hit on the player adds Wounds to the discard.
struct PainfulStabsPower : Power {
  POWER_HEADER(PainfulStabsPower, "PAINFUL_STABS_POWER")
  bool removedAfterOwnerDeath() const override { return false; }
  Task<> afterDamageReceived(Creature* target, const DamageResult& r, int props, Creature* dealer, Card*) override {
    if (dealer != owner || !target->isPlayer || !isPoweredAttack(props) || r.unblocked <= 0) co_return;
    flash = 1.f;
    co_await cmd::addStatusCards(*owner->combat, "Wound", Pile::Discard, amount);
  }
};

// HexPower.cs: every card of the player is Ethereal while it lasts (the Hexed affliction).
// PORT NOTE: no affliction system; Ethereal is set on the cards directly and taken back
// off only from the cards that did not have it.
struct HexPower : Power {
  POWER_HEADER(HexPower, "HEX_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  StackType stackType() const override { return StackType::Single; }
  std::set<Card*> marked;
  void mark(Card* c) {
    if (c->has(kwEthereal) || marked.count(c)) return;
    c->keywords |= kwEthereal;
    marked.insert(c);
  }
  Task<> afterApplied(Creature*, Card*) override {
    for (Card* c : owner->combat->allCards()) mark(c);
    flash = 1.f;
    co_return;
  }
  Task<> afterCardEnteredCombat(Card* c) override { mark(c); co_return; }
  Task<> afterDeath(Creature* c) override {
    if (c == applier) co_await cmd::removePower(this);
  }
  Task<> afterRemoved(Creature*) override {
    for (Card* c : marked) c->keywords &= ~kwEthereal;
    marked.clear();
    co_return;
  }
};

// Card downgrade for DampenPower (CardCmd.Downgrade): back to a fresh card's numbers.
// PORT NOTE: keyword / target changes from the upgrade are restored from a fresh copy too.
void downgradeCard(Card* c) {
  auto fresh = db::card(c->id);
  c->vars = fresh->vars;
  c->cost = fresh->cost;
  c->keywords = fresh->keywords;
  c->target = fresh->target;
  c->upgradeLevel = 0;
}

// DampenPower.cs: upgraded cards are downgraded until the casters die.
struct DampenPower : Power {
  POWER_HEADER(DampenPower, "DAMPEN_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  StackType stackType() const override { return StackType::Single; }
  std::set<Creature*> casters;
  std::vector<std::pair<Card*, int>> downgraded;
  Task<> afterApplied(Creature*, Card*) override {
    for (Card* c : owner->combat->allCards()) {
      if (!c->upgraded()) continue;
      downgraded.push_back({c, c->upgradeLevel});
      downgradeCard(c);
    }
    flash = 1.f;
    co_return;
  }
  Task<> afterDeath(Creature* c) override {
    if (!casters.erase(c)) co_return;
    if (casters.empty()) co_await cmd::removePower(this);
  }
  Task<> afterRemoved(Creature*) override {
    for (auto& [card, n] : downgraded)
      for (int i = 0; i < n; ++i) cmd::upgradeCard(card);
    downgraded.clear();
    co_return;
  }
};

// ChainsOfBindingPower.cs: up to Amount cards drawn each turn are Bound; once one Bound
// card has been played the other Bound cards cannot be. Cleared at the end of the turn.
struct ChainsOfBindingPower : Power {
  POWER_HEADER(ChainsOfBindingPower, "CHAINS_OF_BINDING_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  std::set<Card*> bound;
  bool boundCardPlayed = false;
  Task<> afterCardDrawn(Card* c, bool) override {
    if (owner->combat->currentSide != Side::Player || (int)bound.size() >= amount) co_return;
    bound.insert(c);
    flash = 1.f;
    co_return;
  }
  Task<> beforeCardPlayed(const CardPlay& p) override {
    if (!p.card->isDupe && bound.count(p.card)) boundCardPlayed = true;
    co_return;
  }
  bool shouldPlay(Card* c) override { return !(bound.count(c) && boundCardPlayed); }
  Task<> beforeSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner)) co_return;
    boundCardPlayed = false;
    bound.clear();
    co_return;
  }
};

struct Aeonglass;

// WitheringPresencePower.cs: every 6 cards the player plays, a Wither lands in their hand.
// PORT NOTE: the C# power is per-target and instanced; there is one player, so a counter
// on the Aeonglass does the same (Amount = cards left).
struct WitheringPresencePower : Power {
  POWER_HEADER(WitheringPresencePower, "WITHERING_PRESENCE_POWER")
  Task<> afterCardPlayed(const CardPlay& p) override;
};

// AdaptablePower.cs (Test Subject): the creature stays in the room when it dies and is
// revived by its RESPAWN move.
struct AdaptablePower : Power {
  POWER_HEADER(AdaptablePower, "ADAPTABLE_POWER")
  StackType stackType() const override { return StackType::Single; }
  bool removedAfterOwnerDeath() const override { return false; }
  bool shouldStopCombatFromEnding() override { return true; }
  bool shouldCreatureBeRemovedFromCombatAfterDeath(Creature* c) override { return c != owner; }
  Task<> afterDeath(Creature* c) override;
};

// ================================================================ Knights

struct FlailKnight : Monster {
  MONSTER_HEADER(FlailKnight, "FLAIL_KNIGHT")
  int minHp() const override { return 101; }
  int maxHp() const override { return 101; }
  void buildMoves() override {
    auto* chant = machine.add<MoveState>("WAR_CHANT");
    chant->perform = [this](Targets) { return applyToSelf<StrengthPower>(3); };
    chant->intents = {kindIntent(Intent::Buff)};
    auto* flail = machine.add<MoveState>("FLAIL_MOVE");
    flail->perform = [this](Targets) { return attack(9, 2); };
    flail->intents = {attackIntent(9, 2)};
    auto* ram = machine.add<MoveState>("RAM_MOVE");
    ram->perform = [this](Targets) { return attack(15); };
    ram->intents = {attackIntent(15)};
    auto* rand = machine.add<RandomBranchState>("RAND");
    chant->followUp = flail->followUp = ram->followUp = rand;
    rand->add(chant, MoveRepeat::CannotRepeat);
    rand->addMax(flail, 2);
    rand->addMax(ram, 2);
    machine.start(ram);
  }
};

struct SpectralKnight : Monster {
  MONSTER_HEADER(SpectralKnight, "SPECTRAL_KNIGHT")
  int minHp() const override { return 93; }
  int maxHp() const override { return 93; }
  void buildMoves() override {
    auto* hex = machine.add<MoveState>("HEX");
    hex->perform = [this](Targets t) { return applyToTargets<HexPower>(t, 2); };
    hex->intents = {kindIntent(Intent::Debuff)};
    auto* slash = machine.add<MoveState>("SOUL_SLASH");
    slash->perform = [this](Targets) { return attack(15); };
    slash->intents = {attackIntent(15)};
    auto* flame = machine.add<MoveState>("SOUL_FLAME");
    flame->perform = [this](Targets) { return attack(3, 3); };
    flame->intents = {attackIntent(3, 3)};
    auto* rand = machine.add<RandomBranchState>("RAND");
    hex->followUp = slash;
    slash->followUp = rand;
    flame->followUp = rand;
    rand->addMax(slash, 2);
    rand->add(flame, MoveRepeat::CannotRepeat);
    machine.start(hex);
  }
};

struct MagiKnight : Monster {
  MONSTER_HEADER(MagiKnight, "MAGI_KNIGHT")
  int minHp() const override { return 82; }
  int maxHp() const override { return 82; }
  void buildMoves() override {
    auto* shield = machine.add<MoveState>("POWER_SHIELD_MOVE");
    shield->perform = [this](Targets) { return powerShield(); };
    shield->intents = {attackIntent(6), kindIntent(Intent::Defend)};
    auto* dampen = machine.add<MoveState>("DAMPEN_MOVE");
    dampen->perform = [this](Targets t) { return dampenMove(t); };
    dampen->intents = {kindIntent(Intent::Debuff)};
    auto* prep = machine.add<MoveState>("PREP_MOVE");
    prep->perform = [this](Targets) { return gainBlock(5); };
    prep->intents = {kindIntent(Intent::Defend)};
    auto* bomb = machine.add<MoveState>("MAGIC_BOMB");
    bomb->perform = [this](Targets) { return attack(35); };
    bomb->intents = {attackIntent(35)};
    auto* ram = machine.add<MoveState>("RAM_MOVE");
    ram->perform = [this](Targets) { return attack(10); };
    ram->intents = {attackIntent(10)};
    shield->followUp = dampen;
    dampen->followUp = ram;
    ram->followUp = prep;
    prep->followUp = bomb;
    bomb->followUp = ram;
    machine.start(shield);
  }
  Task<> powerShield() {
    co_await attack(6);
    co_await gainBlock(5);
  }
  Task<> dampenMove(Targets targets) {
    for (Creature* t : targets) {
      auto* d = t->get<DampenPower>();
      if (d) { d->casters.insert(creature); continue; }
      auto p = std::make_unique<DampenPower>();
      p->casters.insert(creature);
      co_await cmd::applyPower(std::move(p), t, 1, creature, nullptr);
    }
  }
};

// ================================================================ Mecha Knight

struct MechaKnight : Monster {
  MONSTER_HEADER(MechaKnight, "MECHA_KNIGHT")
  int minHp() const override { return 300; }
  int maxHp() const override { return 300; }
  Task<> afterAddedToRoom() override { co_await applyById("ArtifactPower", creature, 3, creature); }
  void buildMoves() override {
    auto* charge = machine.add<MoveState>("CHARGE_MOVE");
    charge->perform = [this](Targets) { return attack(25); };
    charge->intents = {attackIntent(25)};
    auto* flame = machine.add<MoveState>("FLAMETHROWER_MOVE");
    flame->perform = [this](Targets) { return flameMove(); };
    flame->intents = {attackIntent(8), kindIntent(Intent::Status, 4)};
    auto* windup = machine.add<MoveState>("WINDUP_MOVE");
    windup->perform = [this](Targets) { return windupMove(); };
    windup->intents = {kindIntent(Intent::Defend), kindIntent(Intent::Buff)};
    auto* cleave = machine.add<MoveState>("HEAVY_CLEAVE_MOVE");
    cleave->perform = [this](Targets) { return attack(35); };
    cleave->intents = {attackIntent(35)};
    charge->followUp = flame;
    flame->followUp = windup;
    windup->followUp = cleave;
    cleave->followUp = flame;
    machine.start(charge);
  }
  Task<> flameMove() {
    co_await attack(8);
    co_await cmd::addStatusCards(*combat, "Burn", Pile::Hand, 4);
  }
  Task<> windupMove() {
    co_await gainBlock(15);
    co_await applyToSelf<StrengthPower>(5);
  }
};

// ================================================================ Soul Nexus

struct SoulNexus : Monster {
  MONSTER_HEADER(SoulNexus, "SOUL_NEXUS")
  int minHp() const override { return 234; }
  int maxHp() const override { return 234; }
  void buildMoves() override {
    auto* burn = machine.add<MoveState>("SOUL_BURN_MOVE");
    burn->perform = [this](Targets) { return attack(29); };
    burn->intents = {attackIntent(29)};
    auto* storm = machine.add<MoveState>("MAELSTROM_MOVE");
    storm->perform = [this](Targets) { return attack(6, 4); };
    storm->intents = {attackIntent(6, 4)};
    auto* drain = machine.add<MoveState>("DRAIN_LIFE_MOVE");
    drain->perform = [this](Targets t) { return drainMove(t); };
    drain->intents = {attackIntent(18), kindIntent(Intent::DebuffStrong)};
    auto* rand = machine.add<RandomBranchState>("RAND");
    burn->followUp = storm->followUp = drain->followUp = rand;
    rand->add(burn, MoveRepeat::CannotRepeat);
    rand->add(storm, MoveRepeat::CannotRepeat);
    rand->add(drain, MoveRepeat::CannotRepeat);
    machine.start(burn);
  }
  Task<> drainMove(Targets t) {
    co_await attack(18);
    co_await applyToTargets<VulnerablePower>(t, 2);
    co_await applyToTargets<WeakPower>(t, 2);
  }
};

// ================================================================ Queen

struct TorchHeadAmalgam : Monster {
  MONSTER_HEADER(TorchHeadAmalgam, "TORCH_HEAD_AMALGAM")
  int minHp() const override { return 199; }
  int maxHp() const override { return 199; }
  Task<> afterAddedToRoom() override { co_await applyById("MinionPower", creature, 1, creature); }
  void buildMoves() override {
    auto* strong = machine.add<MoveState>("STRONG_TACKLE_MOVE");
    strong->perform = [this](Targets) { return attack(26); };
    strong->intents = {attackIntent(26)};
    auto* tackle2 = machine.add<MoveState>("TACKLE_2_MOVE");
    tackle2->perform = [this](Targets) { return attack(18); };
    tackle2->intents = {attackIntent(18)};
    auto* beam = machine.add<MoveState>("BEAM_MOVE");
    beam->perform = [this](Targets) { return attack(8, 3); };
    beam->intents = {attackIntent(8, 3)};
    auto* tackle3 = machine.add<MoveState>("TACKLE_3_MOVE");
    tackle3->perform = [this](Targets) { return attack(14); };
    tackle3->intents = {attackIntent(14)};
    auto* tackle4 = machine.add<MoveState>("TACKLE_4_MOVE");
    tackle4->perform = [this](Targets) { return attack(14); };
    tackle4->intents = {attackIntent(14)};
    strong->followUp = tackle2;
    tackle2->followUp = beam;
    beam->followUp = tackle3;
    tackle3->followUp = tackle4;
    tackle4->followUp = beam;
    machine.start(strong);
  }
};

struct Queen : Monster {
  MONSTER_HEADER(Queen, "QUEEN")
  bool hasAmalgamDied = false;
  MoveState* burnBright = nullptr;
  MoveState* enraged = nullptr;
  int minHp() const override { return 400; }
  int maxHp() const override { return 400; }
  void buildMoves() override {
    auto* puppet = machine.add<MoveState>("PUPPET_STRINGS_MOVE");
    puppet->perform = [this](Targets t) { return applyToTargets<ChainsOfBindingPower>(t, 3); };
    puppet->intents = {kindIntent(Intent::Debuff)};
    auto* mine = machine.add<MoveState>("YOU_ARE_MINE_MOVE");
    mine->perform = [this](Targets t) { return youAreMine(t); };
    mine->intents = {kindIntent(Intent::Debuff)};
    auto* b1 = machine.add<ConditionalBranchState>("YOURE_MINE_NOW_BRANCH");
    burnBright = machine.add<MoveState>("BURN_BRIGHT_FOR_ME_MOVE");
    burnBright->perform = [this](Targets) { return burnBrightMove(); };
    burnBright->intents = {kindIntent(Intent::Buff), kindIntent(Intent::Defend)};
    auto* b2 = machine.add<ConditionalBranchState>("BURN_BRIGHT_FOR_ME_BRANCH");
    auto* offHead = machine.add<MoveState>("OFF_WITH_YOUR_HEAD_MOVE");
    offHead->perform = [this](Targets) { return attack(3, 5); };
    offHead->intents = {attackIntent(3, 5)};
    auto* exec = machine.add<MoveState>("EXECUTION_MOVE");
    exec->perform = [this](Targets) { return attack(15); };
    exec->intents = {attackIntent(15)};
    enraged = machine.add<MoveState>("ENRAGE_MOVE");
    enraged->perform = [this](Targets) { return applyToSelf<StrengthPower>(2); };
    enraged->intents = {kindIntent(Intent::Buff)};
    puppet->followUp = mine;
    mine->followUp = b1;
    b1->add(burnBright, [this] { return !hasAmalgamDied; });
    b1->add(offHead, [this] { return hasAmalgamDied; });
    burnBright->followUp = b2;
    b2->add(burnBright, [this] { return !hasAmalgamDied; });
    b2->add(offHead, [this] { return hasAmalgamDied; });
    offHead->followUp = exec;
    exec->followUp = enraged;
    enraged->followUp = offHead;
    machine.start(puppet);
  }
  Task<> youAreMine(Targets t) {
    co_await applyToTargets<FrailPower>(t, 99);
    co_await applyToTargets<WeakPower>(t, 99);
    co_await applyToTargets<VulnerablePower>(t, 99);
  }
  Task<> burnBrightMove() {
    for (Creature* e : combat->aliveEnemies())
      if (e != creature) co_await applyPower<StrengthPower>(e, Dec(1), creature, nullptr);
    co_await gainBlock(20);
  }
  Task<> afterDeath(Creature* c) override {
    if (c->monster && c->monster->id == "TorchHeadAmalgam" && creature->alive()) {
      hasAmalgamDied = true;
      if (nextMove == burnBright) setMoveImmediate(enraged);
    }
    co_return;
  }
};

// ================================================================ Test Subject

struct TestSubject : Monster {
  MONSTER_HEADER(TestSubject, "TEST_SUBJECT")
  MoveState* deadState = nullptr;
  MoveState* multiClaw = nullptr;
  int respawns = 0;
  int extraMultiClaw = 0;
  int minHp() const override { return 100; }
  int maxHp() const override { return 100; }
  Task<> afterAddedToRoom() override {
    co_await applyToSelf<AdaptablePower>(1);
    co_await applyToSelf<EnragePower>(2);
  }
  void triggerDeadState() { setMoveImmediate(deadState, true); }
  void buildMoves() override {
    deadState = machine.add<MoveState>("RESPAWN_MOVE");
    deadState->perform = [this](Targets) { return respawnMove(); };
    deadState->intents = {kindIntent(Intent::Heal), kindIntent(Intent::Buff)};
    deadState->mustPerformOnce = true;
    auto* bite = machine.add<MoveState>("BITE_MOVE");
    bite->perform = [this](Targets) { return attack(20); };
    bite->intents = {attackIntent(20)};
    auto* bash = machine.add<MoveState>("SKULL_BASH_MOVE");
    bash->perform = [this](Targets t) { return skullBash(t); };
    bash->intents = {attackIntent(14), kindIntent(Intent::Debuff)};
    multiClaw = machine.add<MoveState>("MULTI_CLAW_MOVE");
    multiClaw->perform = [this](Targets) { return multiClawMove(); };
    multiClaw->intents = {attackIntent(10, 3)};
    auto* lacerate = machine.add<MoveState>("PHASE3_LACERATE_MOVE");
    lacerate->perform = [this](Targets) { return attack(10, 3); };
    lacerate->intents = {attackIntent(10, 3)};
    auto* pounce = machine.add<MoveState>("BIG_POUNCE");
    pounce->perform = [this](Targets) { return attack(45); };
    pounce->intents = {attackIntent(45)};
    auto* growl = machine.add<MoveState>("BURNING_GROWL_MOVE");
    growl->perform = [this](Targets) { return growlMove(); };
    growl->intents = {kindIntent(Intent::Status, 3), kindIntent(Intent::Buff)};
    auto* revive = machine.add<ConditionalBranchState>("REVIVE_BRANCH");
    bite->followUp = bash;
    bash->followUp = bite;
    multiClaw->followUp = multiClaw;
    lacerate->followUp = pounce;
    pounce->followUp = growl;
    growl->followUp = lacerate;
    deadState->followUp = revive;
    revive->add(multiClaw, [this] { return respawns < 2; });
    revive->add(lacerate, [this] { return respawns >= 2; });
    machine.start(bite);
  }
  Task<> skullBash(Targets t) {
    co_await attack(14);
    co_await applyToTargets<VulnerablePower>(t, 1);
  }
  Task<> multiClawMove() {
    co_await attack(10, 3 + extraMultiClaw);
    ++extraMultiClaw;
    multiClaw->intents[0].hits = 3 + extraMultiClaw;
  }
  Task<> growlMove() {
    co_await cmd::addStatusCards(*combat, "Burn", Pile::Discard, 3);
    co_await applyToSelf<StrengthPower>(2);
  }
  Task<> revive(int hp) {
    creature->maxHp = hp;
    co_await cmd::heal(creature, hp);
  }
  Task<> respawnMove() {
    ++respawns;
    if (respawns == 1) {
      co_await revive(200);
      co_await applyToSelf<PainfulStabsPower>(1);
    } else if (respawns == 2) {
      co_await revive(300);
      co_await applyToSelf<NemesisPower>(1);
      if (Power* p = creature->power("AdaptablePower")) co_await cmd::removePower(p);
      if (Power* p = creature->power("PainfulStabsPower")) co_await cmd::removePower(p);
    }
  }
};

Task<> AdaptablePower::afterDeath(Creature* c) {
  if (c == owner && owner->monster) static_cast<TestSubject*>(owner->monster.get())->triggerDeadState();
  co_return;
}

// ================================================================ Aeonglass

struct Aeonglass : Monster {
  MONSTER_HEADER(Aeonglass, "AEONGLASS")
  int additionalStrength = 0;
  int witherUpgrades = 0;
  int minHp() const override { return 512; }
  int maxHp() const override { return 512; }
  Task<> afterAddedToRoom() override {
    co_await applyById("WitheringPresencePower", creature, 6, creature);
    co_await applyById("ArtifactPower", creature, 3, creature);
  }
  void buildMoves() override {
    auto* ebb = machine.add<MoveState>("EBB_MOVE");
    ebb->perform = [this](Targets) { return ebbMove(); };
    ebb->intents = {attackIntent(22), kindIntent(Intent::Defend)};
    auto* lasers = machine.add<MoveState>("EYE_LASERS_MOVE");
    lasers->perform = [this](Targets) { return attack(11, 2); };
    lasers->intents = {attackIntent(11, 2)};
    auto* intensity = machine.add<MoveState>("INCREASING_INTENSITY_MOVE");
    intensity->perform = [this](Targets) { return intensityMove(); };
    intensity->intents = {kindIntent(Intent::Status, 1), kindIntent(Intent::Buff)};
    ebb->followUp = lasers;
    lasers->followUp = intensity;
    intensity->followUp = ebb;
    machine.start(ebb);
  }
  Task<> ebbMove() {
    co_await attack(22);
    co_await gainBlock(33);
  }
  Task<> intensityMove() {
    for (Card* c : combat->allCards())
      if (c->id == "Wither") static_cast<Wither*>(c)->fakeUpgrade();
    ++witherUpgrades;
    co_await addWither(Pile::Discard);
    co_await applyToSelf<StrengthPower>(3 + additionalStrength);
    ++additionalStrength;
  }
  // AddToCombatAndPreview<Wither> + AfterCardGeneratedForCombat (MatchWitherToUpgradeCount).
  Task<> addWither(Pile to) {
    Card* c = combat->addCard(db::card("Wither"));
    for (int i = 0; i < witherUpgrades; ++i) static_cast<Wither*>(c)->fakeUpgrade();
    co_await cmd::moveCard(*combat, c, to);
    combat->push({VisualEvent::Banner, combat->player, 1, "Wither"});
  }
};

Task<> WitheringPresencePower::afterCardPlayed(const CardPlay&) {
  if (--amount > 0) co_return;
  flash = 1.f;
  if (owner->monster) co_await static_cast<Aeonglass*>(owner->monster.get())->addWither(Pile::Hand);
  amount = 6;
}

template <class P> void regPower() { db::registerPower(P::kId, [] { return std::unique_ptr<Power>(new P()); }); }
template <class C> void regCard() { db::registerCard(C::kId, [] { return std::unique_ptr<Card>(new C()); }); }

template <class... Ms> std::vector<std::unique_ptr<Monster>> list() {
  std::vector<std::unique_ptr<Monster>> v;
  (v.push_back(std::make_unique<Ms>()), ...);
  return v;
}

}  // namespace

void registerAct3B() {
  db::registerCard("Burn", [] { return std::unique_ptr<Card>(new Burn()); });
  db::registerCard("Wither", [] { return std::unique_ptr<Card>(new Wither()); });
  regPower<IntangiblePower>();
  regPower<NemesisPower>();
  regPower<EnragePower>();
  regPower<PainfulStabsPower>();
  regPower<HexPower>();
  regPower<DampenPower>();
  regPower<ChainsOfBindingPower>();
  regPower<WitheringPresencePower>();
  regPower<AdaptablePower>();

  db::registerEncounter("KnightsElite", RoomType::Elite, false, [](Rng&) { return list<FlailKnight, SpectralKnight, MagiKnight>(); });
  db::registerEncounter("MechaKnightElite", RoomType::Elite, false, [](Rng&) { return list<MechaKnight>(); });
  db::registerEncounter("SoulNexusElite", RoomType::Elite, false, [](Rng&) { return list<SoulNexus>(); });
  db::registerEncounter("QueenBoss", RoomType::Boss, false, [](Rng&) { return list<TorchHeadAmalgam, Queen>(); });
  db::registerEncounter("TestSubjectBoss", RoomType::Boss, false, [](Rng&) { return list<TestSubject>(); });
  db::registerEncounter("AeonglassBoss", RoomType::Boss, false, [](Rng&) { return list<Aeonglass>(); });
}

}  // namespace sts
