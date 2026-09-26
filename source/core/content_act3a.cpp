// Act 3 (Glory) monsters, package 6a: Axebots, Construct Menagerie, Devoted Sculptor,
// Fabricator (+ its bots), Frog Knight, Globe Head, Owl Magistrate, Scrolls of Biting,
// Slimed Berserker, The Lost and Forgotten, Turret Operator (+ Living Shield).
// Translated from MegaCrit.Sts2.Core.Models.Monsters / .Powers / .Encounters.
// Values are the non-ascension ones (see docs/PORTING.md).
#include <algorithm>
#include <map>

#include "cards.h"
#include "powers.h"

namespace sts {

std::unique_ptr<Monster> makeCubexConstruct();  // content_act1.cpp

namespace {

#define MONSTER_HEADER(Name, Key) \
  Name() { id = #Name; locKey = Key; }

using Targets = const std::vector<Creature*>&;

Intent attackIntent(int dmg, int hits = 1) { Intent i; i.kind = Intent::Attack; i.damage = dmg; i.hits = hits; return i; }
Intent kindIntent(Intent::Kind k, int count = 0) { Intent i; i.kind = k; i.count = count; return i; }

// Powers registered by other files are applied by id.
Task<> applyById(const char* powerId, Creature* target, Dec amount, Creature* applier) {
  co_await cmd::applyPower(db::power(powerId), target, amount, applier, nullptr);
}

// ================================================================ powers

// StockPower.cs: when the Axebot dies, a new one takes its place with one less stock.
// PORT NOTE: no respawn animation / delayed reveal.
struct StockPower : Power {
  POWER_HEADER(StockPower, "STOCK_POWER")
  Task<> afterDeath(Creature* target) override;
  bool shouldStopCombatFromEnding() override { return true; }
};

// GalvanicPower.cs: every Power card is afflicted with Galvanized and hurts its owner
// when played. PORT NOTE: no per-card affliction system; every Power card counts, which is
// what the source ends up doing anyway (BeforeCombatStart + AfterCardEnteredCombat).
struct GalvanicPower : Power {
  POWER_HEADER(GalvanicPower, "GALVANIC_POWER")
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (p.card->type != CardType::Power) co_return;
    Creature* pl = owner->combat->player;
    flash = 1.f;
    co_await cmd::damage(pl, amount, kUnpowered | kMove, nullptr, nullptr);
  }
};

// PaperCutsPower.cs: unblocked powered attack damage also removes max HP.
struct PaperCutsPower : Power {
  POWER_HEADER(PaperCutsPower, "PAPER_CUTS_POWER")
  Task<> afterDamageReceived(Creature* target, const DamageResult& r, int props, Creature* dealer, Card*) override {
    if (dealer == owner && target->isPlayer && isPoweredAttack(props) && r.unblocked > 0)
      co_await cmd::loseMaxHp(target, amount);
  }
};

// PossessStrengthPower.cs / PossessSpeedPower.cs: Strength / Dexterity stolen from the
// player is given back when the owner dies.
struct PossessBase : Power {
  const char* stat = "";
  std::map<Creature*, int> stolen;
  StackType stackType() const override { return StackType::Single; }
  Task<> afterPowerAmountChanged(Power* p, Dec delta, Creature* applier, Card*) override {
    if (applier != owner || !p->owner || !p->owner->isPlayer || p->id != stat || !(delta < Dec(0))) co_return;
    stolen[p->owner] += delta.toInt();
  }
  Task<> afterDeath(Creature* c) override {
    if (c != owner) co_return;
    for (auto& [cr, v] : stolen)
      if (v != 0) co_await cmd::applyPower(db::power(stat), cr, Dec(-v), nullptr, nullptr);
  }
};
struct PossessStrengthPower : PossessBase {
  POWER_HEADER(PossessStrengthPower, "POSSESS_STRENGTH_POWER")
  void init() { stat = "StrengthPower"; }
};
struct PossessSpeedPower : PossessBase {
  POWER_HEADER(PossessSpeedPower, "POSSESS_SPEED_POWER")
  void init() { stat = "DexterityPower"; }
};

// RampartPower.cs: at the start of the player's turn every Turret Operator gains block.
struct RampartPower : Power {
  POWER_HEADER(RampartPower, "RAMPART_POWER")
  Task<> afterSideTurnStart(Side side, const std::vector<Creature*>&) override {
    if (side != Side::Player) co_return;
    for (Creature* e : owner->combat->aliveEnemies())
      if (e->monster && e->monster->id == "TurretOperator") co_await cmd::gainBlock(e, Dec(amount), kUnpowered, nullptr);
  }
};

// SoarPower.cs: takes half damage from powered attacks.
struct SoarPower : Power {
  POWER_HEADER(SoarPower, "SOAR_POWER")
  StackType stackType() const override { return StackType::Single; }
  Dec modifyDamageMultiplicative(Creature* target, Dec, int props, Creature*, Card*) override {
    if (target != owner || !isPoweredAttack(props)) return 1;
    return Dec::lit(0.5);  // DamageDecrease 50 / 100
  }
};

// RitualPower.cs: +Amount Strength at the end of the owner's turn (not the turn it was applied).
struct RitualPower : Power {
  POWER_HEADER(RitualPower, "RITUAL_POWER")
  bool wasJustAppliedByEnemy = false;
  Task<> afterApplied(Creature*, Card*) override {
    if (owner->side == Side::Enemy) wasJustAppliedByEnemy = true;
    return {};
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner)) co_return;
    if (wasJustAppliedByEnemy) { wasJustAppliedByEnemy = false; co_return; }
    flash = 1.f;
    co_await applyPower<StrengthPower>(owner, Dec(amount), owner, nullptr);
  }
};

// HighVoltagePower.cs: +Amount Strength at the end of every turn of the owner.
struct HighVoltagePower : Power {
  POWER_HEADER(HighVoltagePower, "HIGH_VOLTAGE_POWER")
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner)) co_return;
    flash = 1.f;
    co_await applyPower<StrengthPower>(owner, Dec(amount), owner, nullptr);
  }
};

// ================================================================ monsters

struct Axebot : Monster {
  MONSTER_HEADER(Axebot, "AXEBOT")
  int stock = 2;
  bool stockOverride = false;  // a respawn: StockAmount was set explicitly
  int respawnCount() const { return 2 - stock; }
  int minHp() const override { return 70 + respawnCount() * 10; }
  int maxHp() const override { return 78 + respawnCount() * 10; }
  Task<> afterAddedToRoom() override {
    if (stock > 0) co_await applyById("StockPower", creature, stock, nullptr);
  }
  void buildMoves() override {
    auto* bootUp = machine.add<MoveState>("BOOT_UP_MOVE");
    bootUp->perform = [this](Targets) { return bootUpMove(); };
    bootUp->intents = {kindIntent(Intent::Defend), kindIntent(Intent::Buff)};
    auto* oneTwo = machine.add<MoveState>("ONE_TWO_MOVE");
    oneTwo->perform = [this](Targets) { return attack(10, 2); };
    oneTwo->intents = {attackIntent(10, 2)};
    auto* hammer = machine.add<MoveState>("HAMMER_UPPERCUT_MOVE");
    hammer->perform = [this](Targets t) { return hammerMove(t); };
    hammer->intents = {attackIntent(14), kindIntent(Intent::Debuff)};
    bootUp->followUp = hammer;
    hammer->followUp = oneTwo;
    oneTwo->followUp = hammer;
    machine.start(stockOverride ? bootUp : hammer);
  }
  Task<> bootUpMove() {
    co_await gainBlock(10);
    co_await applyToSelf<StrengthPower>(3 * respawnCount());
  }
  Task<> hammerMove(Targets t) {
    co_await attack(14);
    co_await applyToTargets<WeakPower>(t, 2);
    co_await applyToTargets<FrailPower>(t, 2);
  }
};

Task<> StockPower::afterDeath(Creature* target) {
  if (target != owner || amount <= 0) co_return;
  auto ax = std::make_unique<Axebot>();
  ax->stock = amount - 1;
  ax->stockOverride = true;
  co_await cmd::addMonster(*owner->combat, std::move(ax));
}

struct PunchConstruct : Monster {
  MONSTER_HEADER(PunchConstruct, "PUNCH_CONSTRUCT")
  int minHp() const override { return 55; }
  int maxHp() const override { return 55; }
  Task<> afterAddedToRoom() override { co_await applyById("ArtifactPower", creature, 1, creature); }
  void buildMoves() override {
    auto* ready = machine.add<MoveState>("READY_MOVE");
    ready->perform = [this](Targets) { return gainBlock(10); };
    ready->intents = {kindIntent(Intent::Defend)};
    auto* fast = machine.add<MoveState>("FAST_PUNCH_MOVE");
    fast->perform = [this](Targets t) { return fastMove(t); };
    fast->intents = {attackIntent(5, 2), kindIntent(Intent::Debuff)};
    auto* strong = machine.add<MoveState>("STRONG_PUNCH_MOVE");
    strong->perform = [this](Targets) { return attack(14); };
    strong->intents = {attackIntent(14)};
    ready->followUp = fast;
    fast->followUp = strong;
    strong->followUp = ready;
    machine.start(ready);
  }
  Task<> fastMove(Targets t) {
    co_await attack(5, 2);
    co_await applyToTargets<FrailPower>(t, 1);
  }
};

struct DevotedSculptor : Monster {
  MONSTER_HEADER(DevotedSculptor, "DEVOTED_SCULPTOR")
  int minHp() const override { return 162; }
  int maxHp() const override { return 162; }
  void buildMoves() override {
    auto* incant = machine.add<MoveState>("FORBIDDEN_INCANTATION_MOVE");
    incant->perform = [this](Targets) { return applyToSelf<RitualPower>(9); };
    incant->intents = {kindIntent(Intent::Buff)};
    auto* savage = machine.add<MoveState>("SAVAGE_MOVE");
    savage->perform = [this](Targets) { return attack(12); };
    savage->intents = {attackIntent(12)};
    incant->followUp = savage;
    savage->followUp = savage;
    machine.start(incant);
  }
};

// Fabricator.cs and its bots (Zapbot, Stabbot, Guardbot, Noisebot).
struct Zapbot : Monster {
  MONSTER_HEADER(Zapbot, "ZAPBOT")
  int minHp() const override { return 18; }
  int maxHp() const override { return 23; }
  Task<> afterAddedToRoom() override { co_await applyToSelf<HighVoltagePower>(2); }
  void buildMoves() override {
    auto* zap = machine.add<MoveState>("ZAP");
    zap->perform = [this](Targets) { return attack(14); };
    zap->intents = {attackIntent(14)};
    zap->followUp = zap;
    machine.start(zap);
  }
};

struct Stabbot : Monster {
  MONSTER_HEADER(Stabbot, "STABBOT")
  int minHp() const override { return 18; }
  int maxHp() const override { return 23; }
  void buildMoves() override {
    auto* stab = machine.add<MoveState>("STAB_MOVE");
    stab->perform = [this](Targets t) { return stabMove(t); };
    stab->intents = {attackIntent(11), kindIntent(Intent::Debuff)};
    stab->followUp = stab;
    machine.start(stab);
  }
  Task<> stabMove(Targets t) {
    co_await attack(11);
    co_await applyToTargets<FrailPower>(t, 1);
  }
};

struct Guardbot : Monster {
  MONSTER_HEADER(Guardbot, "GUARDBOT")
  int minHp() const override { return 16; }
  int maxHp() const override { return 20; }
  void buildMoves() override {
    auto* guard = machine.add<MoveState>("GUARD_MOVE");
    guard->perform = [this](Targets) { return guardMove(); };
    guard->intents = {kindIntent(Intent::Defend)};
    guard->followUp = guard;
    machine.start(guard);
  }
  Task<> guardMove() {
    for (Creature* e : combat->aliveEnemies())
      if (e->monster && e->monster->id == "Fabricator") co_await cmd::gainBlock(e, 15, kUnpowered, nullptr);
  }
};

struct Noisebot : Monster {
  MONSTER_HEADER(Noisebot, "NOISEBOT")
  int minHp() const override { return 18; }
  int maxHp() const override { return 23; }
  void buildMoves() override {
    auto* noise = machine.add<MoveState>("NOISE_MOVE");
    noise->perform = [this](Targets) { return noiseMove(); };
    noise->intents = {kindIntent(Intent::Status, 2)};
    noise->followUp = noise;
    machine.start(noise);
  }
  Task<> noiseMove() {
    // One Dazed into the discard pile, one at a random position of the draw pile.
    co_await cmd::addStatusCards(*combat, "Dazed", Pile::Discard, 1);
    Card* c = combat->addCard(db::card("Dazed"));
    co_await cmd::moveCard(*combat, c, Pile::Draw);
    auto& d = combat->draw;
    d.erase(std::remove(d.begin(), d.end(), c), d.end());
    d.insert(d.begin() + combat->rng("CombatCardGeneration").nextInt((int)d.size() + 1), c);
    combat->push({VisualEvent::Banner, combat->player, 1, "Dazed"});
  }
};

struct Fabricator : Monster {
  MONSTER_HEADER(Fabricator, "FABRICATOR")
  std::string lastSpawned;
  int minHp() const override { return 150; }
  int maxHp() const override { return 150; }
  bool canFabricate() { return combat->aliveEnemies().size() < 4; }
  void buildMoves() override {
    auto* fabricate = machine.add<MoveState>("FABRICATE_MOVE");
    fabricate->perform = [this](Targets) { return fabricateMove(); };
    fabricate->intents = {kindIntent(Intent::Summon)};
    auto* strike = machine.add<MoveState>("FABRICATING_STRIKE_MOVE");
    strike->perform = [this](Targets) { return strikeMove(); };
    strike->intents = {attackIntent(18), kindIntent(Intent::Summon)};
    auto* disintegrate = machine.add<MoveState>("DISINTEGRATE_MOVE");
    disintegrate->perform = [this](Targets) { return attack(11); };
    disintegrate->intents = {attackIntent(11)};
    auto* rand = machine.add<RandomBranchState>("RAND");
    rand->add(fabricate, MoveRepeat::CanRepeatForever);
    rand->add(strike, MoveRepeat::CanRepeatForever);
    auto* branch = machine.add<ConditionalBranchState>("fabricateBranch");
    branch->add(rand, [this] { return canFabricate(); });
    branch->add(disintegrate, [this] { return !canFabricate(); });
    fabricate->followUp = branch;
    disintegrate->followUp = branch;
    strike->followUp = branch;
    machine.start(branch);
  }
  Task<> fabricateMove() {
    co_await spawnBot({"Guardbot", "Noisebot"});
    co_await spawnBot({"Zapbot", "Stabbot"});
  }
  Task<> strikeMove() {
    co_await attack(18);
    co_await spawnBot({"Zapbot", "Stabbot"});
  }
  Task<> spawnBot(std::vector<std::string> options) {
    if (combat->ending) co_return;
    options.erase(std::remove(options.begin(), options.end(), lastSpawned), options.end());
    lastSpawned = combat->rng("MonsterAi").nextItem(options);
    std::unique_ptr<Monster> m;
    if (lastSpawned == "Zapbot") m = std::make_unique<Zapbot>();
    else if (lastSpawned == "Stabbot") m = std::make_unique<Stabbot>();
    else if (lastSpawned == "Guardbot") m = std::make_unique<Guardbot>();
    else m = std::make_unique<Noisebot>();
    Creature* bot = co_await cmd::addMonster(*combat, std::move(m));
    co_await cmd::applyPower(db::power("MinionPower"), bot, 1, creature, nullptr);
  }
};

struct FrogKnight : Monster {
  MONSTER_HEADER(FrogKnight, "FROG_KNIGHT")
  bool hasBeetleCharged = false;
  int minHp() const override { return 191; }
  int maxHp() const override { return 191; }
  Task<> afterAddedToRoom() override {
    co_await applyToSelf<PlatingPower>(15);
    hasBeetleCharged = false;
  }
  void buildMoves() override {
    auto* queen = machine.add<MoveState>("FOR_THE_QUEEN");
    queen->perform = [this](Targets) { return applyToSelf<StrengthPower>(5); };
    queen->intents = {kindIntent(Intent::Buff)};
    auto* strike = machine.add<MoveState>("STRIKE_DOWN_EVIL");
    strike->perform = [this](Targets) { return attack(21); };
    strike->intents = {attackIntent(21)};
    auto* lash = machine.add<MoveState>("TONGUE_LASH");
    lash->perform = [this](Targets t) { return lashMove(t); };
    lash->intents = {attackIntent(13), kindIntent(Intent::Debuff)};
    auto* charge = machine.add<MoveState>("BEETLE_CHARGE");
    charge->perform = [this](Targets) { return chargeMove(); };
    charge->intents = {attackIntent(35)};
    auto* half = machine.add<ConditionalBranchState>("HALF_HEALTH");
    half->add(lash, [this] { return hasBeetleCharged || creature->hp >= creature->maxHp / 2; });
    half->add(charge, [this] { return !hasBeetleCharged && creature->hp < creature->maxHp / 2; });
    queen->followUp = half;
    strike->followUp = queen;
    lash->followUp = strike;
    charge->followUp = lash;
    machine.start(lash);
  }
  Task<> lashMove(Targets t) {
    co_await attack(13);
    co_await applyToTargets<FrailPower>(t, 2);
  }
  Task<> chargeMove() {
    hasBeetleCharged = true;
    co_await attack(35);
  }
};

struct GlobeHead : Monster {
  MONSTER_HEADER(GlobeHead, "GLOBE_HEAD")
  int minHp() const override { return 148; }
  int maxHp() const override { return 148; }
  Task<> afterAddedToRoom() override { co_await applyToSelf<GalvanicPower>(6); }
  void buildMoves() override {
    auto* thunder = machine.add<MoveState>("THUNDER_STRIKE");
    thunder->perform = [this](Targets) { return attack(6, 3); };
    thunder->intents = {attackIntent(6, 3)};
    auto* slap = machine.add<MoveState>("SHOCKING_SLAP");
    slap->perform = [this](Targets t) { return slapMove(t); };
    slap->intents = {attackIntent(13), kindIntent(Intent::Debuff)};
    auto* burst = machine.add<MoveState>("GALVANIC_BURST");
    burst->perform = [this](Targets) { return burstMove(); };
    burst->intents = {attackIntent(16), kindIntent(Intent::Buff)};
    slap->followUp = thunder;
    thunder->followUp = burst;
    burst->followUp = slap;
    machine.start(slap);
  }
  Task<> slapMove(Targets t) {
    co_await attack(13);
    co_await applyToTargets<FrailPower>(t, 2);
  }
  Task<> burstMove() {
    co_await attack(16);
    co_await applyToSelf<StrengthPower>(2);
  }
};

struct OwlMagistrate : Monster {
  MONSTER_HEADER(OwlMagistrate, "OWL_MAGISTRATE")
  int minHp() const override { return 231; }
  int maxHp() const override { return 231; }
  void buildMoves() override {
    auto* scrutiny = machine.add<MoveState>("MAGISTRATE_SCRUTINY");
    scrutiny->perform = [this](Targets) { return attack(16); };
    scrutiny->intents = {attackIntent(16)};
    auto* peck = machine.add<MoveState>("PECK_ASSAULT");
    peck->perform = [this](Targets) { return attack(4, 6); };
    peck->intents = {attackIntent(4, 6)};
    auto* flight = machine.add<MoveState>("JUDICIAL_FLIGHT");
    flight->perform = [this](Targets) { return applyById("SoarPower", creature, 1, creature); };
    flight->intents = {kindIntent(Intent::Buff)};
    auto* verdict = machine.add<MoveState>("VERDICT");
    verdict->perform = [this](Targets t) { return verdictMove(t); };
    verdict->intents = {attackIntent(33), kindIntent(Intent::Debuff)};
    scrutiny->followUp = peck;
    peck->followUp = flight;
    flight->followUp = verdict;
    verdict->followUp = scrutiny;
    machine.start(scrutiny);
  }
  Task<> verdictMove(Targets t) {
    co_await attack(33);
    co_await applyToTargets<VulnerablePower>(t, 4);
    if (Power* soar = creature->power("SoarPower")) co_await cmd::removePower(soar);
  }
};

struct ScrollOfBiting : Monster {
  MONSTER_HEADER(ScrollOfBiting, "SCROLL_OF_BITING")
  int starterMoveIdx = 0;
  int minHp() const override { return 30; }
  int maxHp() const override { return 37; }
  Task<> afterAddedToRoom() override { co_await applyById("PaperCutsPower", creature, 2, creature); }
  void buildMoves() override {
    auto* chomp = machine.add<MoveState>("CHOMP");
    chomp->perform = [this](Targets) { return attack(14); };
    chomp->intents = {attackIntent(14)};
    auto* chew = machine.add<MoveState>("CHEW");
    chew->perform = [this](Targets) { return attack(5, 2); };
    chew->intents = {attackIntent(5, 2)};
    auto* teeth = machine.add<MoveState>("MORE_TEETH");
    teeth->perform = [this](Targets) { return applyToSelf<StrengthPower>(2); };
    teeth->intents = {kindIntent(Intent::Buff)};
    auto* rand = machine.add<RandomBranchState>("rand");
    chomp->followUp = teeth;
    chew->followUp = rand;
    teeth->followUp = chew;
    rand->add(chomp, MoveRepeat::CannotRepeat);
    rand->addMax(chew, 2);
    machine.start(starterMoveIdx % 3 == 0 ? (MonsterState*)chomp : starterMoveIdx % 3 == 1 ? (MonsterState*)chew : (MonsterState*)teeth);
  }
};

struct SlimedBerserker : Monster {
  MONSTER_HEADER(SlimedBerserker, "SLIMED_BERSERKER")
  int minHp() const override { return 261; }
  int maxHp() const override { return 261; }
  void buildMoves() override {
    auto* vomit = machine.add<MoveState>("VOMIT_ICHOR_MOVE");
    vomit->perform = [this](Targets) { return cmd::addStatusCards(*combat, "Slimed", Pile::Discard, 10); };
    vomit->intents = {kindIntent(Intent::Status, 10)};
    auto* hug = machine.add<MoveState>("LEECHING_HUG_MOVE");
    hug->perform = [this](Targets t) { return hugMove(t); };
    hug->intents = {kindIntent(Intent::Debuff), kindIntent(Intent::Buff)};
    auto* smother = machine.add<MoveState>("SMOTHER_MOVE");
    smother->perform = [this](Targets) { return attack(30); };
    smother->intents = {attackIntent(30)};
    auto* pummel = machine.add<MoveState>("FURIOUS_PUMMELING_MOVE");
    pummel->perform = [this](Targets) { return attack(4, 4); };
    pummel->intents = {attackIntent(4, 4)};
    vomit->followUp = pummel;
    pummel->followUp = hug;
    hug->followUp = smother;
    smother->followUp = vomit;
    machine.start(vomit);
  }
  Task<> hugMove(Targets t) {
    co_await applyToTargets<WeakPower>(t, 3);
    co_await applyToSelf<StrengthPower>(3);
  }
};

struct TheLost : Monster {
  MONSTER_HEADER(TheLost, "THE_LOST")
  int minHp() const override { return 93; }
  int maxHp() const override { return 93; }
  Task<> afterAddedToRoom() override {
    auto p = std::make_unique<PossessStrengthPower>();
    p->init();
    co_await cmd::applyPower(std::move(p), creature, 1, nullptr, nullptr);
  }
  void buildMoves() override {
    auto* smog = machine.add<MoveState>("DEBILITATING_SMOG");
    smog->perform = [this](Targets t) { return smogMove(t); };
    smog->intents = {kindIntent(Intent::Debuff), kindIntent(Intent::Buff)};
    auto* lasers = machine.add<MoveState>("EYE_LASERS");
    lasers->perform = [this](Targets) { return attack(4, 2); };
    lasers->intents = {attackIntent(4, 2)};
    smog->followUp = lasers;
    lasers->followUp = smog;
    machine.start(smog);
  }
  Task<> smogMove(Targets t) {
    co_await applyToTargets<StrengthPower>(t, -2);
    co_await applyToSelf<StrengthPower>(2);
  }
};

struct TheForgotten : Monster {
  MONSTER_HEADER(TheForgotten, "THE_FORGOTTEN")
  MoveState* dread = nullptr;
  int minHp() const override { return 106; }
  int maxHp() const override { return 106; }
  int dreadDamage() { return 13 + creature->powerAmount<DexterityPower>(); }
  Task<> afterAddedToRoom() override {
    auto p = std::make_unique<PossessSpeedPower>();
    p->init();
    co_await cmd::applyPower(std::move(p), creature, 1, nullptr, nullptr);
  }
  void buildMoves() override {
    auto* miasma = machine.add<MoveState>("MIASMA");
    miasma->perform = [this](Targets t) { return miasmaMove(t); };
    miasma->intents = {kindIntent(Intent::Debuff), kindIntent(Intent::Defend), kindIntent(Intent::Buff)};
    dread = machine.add<MoveState>("DREAD");
    dread->perform = [this](Targets) { return attack(dreadDamage()); };
    dread->intents = {attackIntent(13)};
    miasma->followUp = dread;
    dread->followUp = miasma;
    machine.start(miasma);
  }
  Task<> miasmaMove(Targets t) {
    co_await applyToTargets<DexterityPower>(t, -2);
    co_await gainBlock(8);
    co_await applyToSelf<DexterityPower>(2);
    dread->intents = {attackIntent(dreadDamage())};  // the intent follows the Dexterity gain
  }
};

struct LivingShield : Monster {
  MONSTER_HEADER(LivingShield, "LIVING_SHIELD")
  int minHp() const override { return 55; }
  int maxHp() const override { return 55; }
  int allyCount() {
    int n = 0;
    for (Creature* e : combat->aliveEnemies()) if (e != creature) ++n;
    return n;
  }
  Task<> afterAddedToRoom() override { co_await applyById("RampartPower", creature, 25, creature); }
  void buildMoves() override {
    auto* slam = machine.add<MoveState>("SHIELD_SLAM_MOVE");
    slam->perform = [this](Targets) { return attack(6); };
    slam->intents = {attackIntent(6)};
    auto* branch = machine.add<ConditionalBranchState>("SHIELD_SLAM_BRANCH");
    auto* smash = machine.add<MoveState>("SMASH_MOVE");
    smash->perform = [this](Targets) { return smashMove(); };
    smash->intents = {attackIntent(16), kindIntent(Intent::Buff)};
    slam->followUp = branch;
    branch->add(slam, [this] { return allyCount() > 0; });
    branch->add(smash, [this] { return allyCount() == 0; });
    smash->followUp = smash;
    machine.start(slam);
  }
  Task<> smashMove() {
    co_await attack(16);
    co_await applyToSelf<StrengthPower>(3);
  }
};

struct TurretOperator : Monster {
  MONSTER_HEADER(TurretOperator, "TURRET_OPERATOR")
  int minHp() const override { return 41; }
  int maxHp() const override { return 41; }
  void buildMoves() override {
    auto* unload = machine.add<MoveState>("UNLOAD_MOVE");
    unload->perform = [this](Targets) { return attack(3, 5); };
    unload->intents = {attackIntent(3, 5)};
    auto* unload2 = machine.add<MoveState>("UNLOAD_MOVE_2");
    unload2->perform = [this](Targets) { return attack(3, 5); };
    unload2->intents = {attackIntent(3, 5)};
    auto* reload = machine.add<MoveState>("RELOAD_MOVE");
    reload->perform = [this](Targets) { return applyToSelf<StrengthPower>(1); };
    reload->intents = {kindIntent(Intent::Buff)};
    unload->followUp = unload2;
    unload2->followUp = reload;
    reload->followUp = unload;
    machine.start(unload);
  }
};

template <class... Ms> std::vector<std::unique_ptr<Monster>> list() {
  std::vector<std::unique_ptr<Monster>> v;
  (v.push_back(std::make_unique<Ms>()), ...);
  return v;
}

template <class P> void regPower() { db::registerPower(P::kId, [] { return std::unique_ptr<Power>(new P()); }); }

}  // namespace

void registerAct3A() {
  regPower<StockPower>();
  regPower<GalvanicPower>();
  regPower<PaperCutsPower>();
  db::registerPower("PossessStrengthPower", [] {
    auto p = std::make_unique<PossessStrengthPower>();
    p->init();
    return std::unique_ptr<Power>(std::move(p));
  });
  db::registerPower("PossessSpeedPower", [] {
    auto p = std::make_unique<PossessSpeedPower>();
    p->init();
    return std::unique_ptr<Power>(std::move(p));
  });
  regPower<RampartPower>();
  regPower<SoarPower>();
  regPower<RitualPower>();
  regPower<HighVoltagePower>();

  db::registerEncounter("AxebotsNormal", RoomType::Monster, false, [](Rng&) { return list<Axebot>(); });
  db::registerEncounter("ConstructMenagerieNormal", RoomType::Monster, false, [](Rng&) {
    auto v = list<PunchConstruct>();
    v.push_back(makeCubexConstruct());
    v.push_back(makeCubexConstruct());
    return v;
  });
  db::registerEncounter("DevotedSculptorWeak", RoomType::Monster, true, [](Rng&) { return list<DevotedSculptor>(); });
  db::registerEncounter("FabricatorNormal", RoomType::Monster, false, [](Rng&) { return list<Fabricator>(); });
  db::registerEncounter("FrogKnightNormal", RoomType::Monster, false, [](Rng&) { return list<FrogKnight>(); });
  db::registerEncounter("GlobeHeadNormal", RoomType::Monster, false, [](Rng&) { return list<GlobeHead>(); });
  db::registerEncounter("OwlMagistrateNormal", RoomType::Monster, false, [](Rng&) { return list<OwlMagistrate>(); });
  auto scrolls = [](Rng& rng, int n, bool lastFixed) {
    auto v = list<>();
    int start = rng.nextInt(3);
    for (int i = 0; i < n; ++i) {
      auto s = std::make_unique<ScrollOfBiting>();
      s->starterMoveIdx = (lastFixed && i == n - 1) ? 2 : (start + i) % 3;
      v.push_back(std::move(s));
    }
    return v;
  };
  db::registerEncounter("ScrollsOfBitingNormal", RoomType::Monster, false, [scrolls](Rng& r) { return scrolls(r, 4, true); });
  db::registerEncounter("ScrollsOfBitingWeak", RoomType::Monster, true, [scrolls](Rng& r) { return scrolls(r, 3, false); });
  db::registerEncounter("SlimedBerserkerNormal", RoomType::Monster, false, [](Rng&) { return list<SlimedBerserker>(); });
  db::registerEncounter("TheLostAndForgottenNormal", RoomType::Monster, false, [](Rng&) { return list<TheLost, TheForgotten>(); });
  db::registerEncounter("TurretOperatorWeak", RoomType::Monster, true, [](Rng&) { return list<LivingShield, TurretOperator>(); });
}

}  // namespace sts
