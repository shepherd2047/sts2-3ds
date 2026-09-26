// Act 1 content translated from decompiled monsters/encounters not covered by
// content.cpp: MegaCrit.Sts2.Core.Models.Monsters / .Powers / .Encounters.
// Values are the non-ascension ones (see docs/PORTING.md).
#include <algorithm>
#include <map>

#include "powers.h"

namespace sts {

// ================================================================ powers

struct ConstrictPower : Power {
  POWER_HEADER(ConstrictPower, "CONSTRICT_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await cmd::damage(owner, amount, kUnpowered, owner, nullptr);
  }
  Task<> afterDeath(Creature* c) override {
    // PowerCmd.Remove is unconditional here (this build has no removal-prevention concept).
    if (c == applier) co_await cmd::removePower(this);
  }
};

// TangledPower afflicts every Attack card the owner holds/draws with a +Amount energy
// cost (CardCmd.Afflict<Entangled> in the source). This build has no per-card affliction
// system, so the effect is reproduced directly as a live cost modifier on Attack cards
// owned by `owner` — behaviourally identical since the source ends up afflicting every
// such card anyway (AfterApplied marks existing ones, AfterCardEnteredCombat marks new
// ones), and AfterRemoved just un-afflicts them (implicit here once the power is gone).
struct TangledPower : Power {
  POWER_HEADER(TangledPower, "TANGLED_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  int modifyEnergyCost(Card* card, int cost) override {
    if (card->type != CardType::Attack || ownerOf(card) != owner) return cost;
    return cost + amount;
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) {
      flash = 1.f;
      co_await cmd::removePower(this);
    }
  }
};

// Each stack cancels one debuff application to its owner.
struct ArtifactPower : Power {
  POWER_HEADER(ArtifactPower, "ARTIFACT_POWER")
  bool tryModifyPowerAmountReceived(Power* incoming, Creature* target, Dec amount, Creature*, Dec& out) override {
    if (target != owner || incoming->typeForAmount(amount) != PowerType::Debuff) return false;
    out = 0;
    return true;
  }
  Task<> afterModifyingPowerAmountReceived(Power*) override { co_await cmd::decrement(this); }
};

struct SlowPower : Power {
  POWER_HEADER(SlowPower, "SLOW_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  int slowAmount = 0;  // DynamicVars["SlowAmount"]; DisplayAmount (=slowAmount*10) is UI-only.
  Dec modifyDamageMultiplicative(Creature* target, Dec, int props, Creature*, Card*) override {
    if (target != owner || !isPoweredAttack(props)) return 1;
    return Dec(1) + Dec(slowAmount) / Dec(10);
  }
  Task<> afterCardPlayed(const CardPlay&) override {
    ++slowAmount;
    flash = 1.f;
    co_return;
  }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) slowAmount = 0;
    co_return;
  }
};

// ================================================================ monsters

#define MONSTER_HEADER(Name, Key) \
  Name() { id = #Name; locKey = Key; }

using Targets = const std::vector<Creature*>&;

inline Intent attackIntent(int dmg, int hits = 1) { Intent i; i.kind = Intent::Attack; i.damage = dmg; i.hits = hits; return i; }
inline Intent kindIntent(Intent::Kind k, int count = 0) { Intent i; i.kind = k; i.count = count; return i; }

struct Flyconid : Monster {
  MONSTER_HEADER(Flyconid, "FLYCONID")
  int minHp() const override { return 47; }
  int maxHp() const override { return 49; }
  void buildMoves() override {
    auto* vuln = machine.add<MoveState>("VULNERABLE_SPORES_MOVE");
    vuln->perform = [this](Targets t) { return applyToTargets<VulnerablePower>(t, 2); };
    vuln->intents = {kindIntent(Intent::Debuff)};
    auto* frail = machine.add<MoveState>("FRAIL_SPORES_MOVE");
    frail->perform = [this](Targets t) { return frailSporesMove(t); };
    frail->intents = {attackIntent(8), kindIntent(Intent::Debuff)};
    auto* smash = machine.add<MoveState>("SMASH_MOVE");
    smash->perform = [this](Targets) { return attack(11); };
    smash->intents = {attackIntent(11)};
    auto* rand = machine.add<RandomBranchState>("RAND");
    auto* init = machine.add<RandomBranchState>("INITIAL");
    vuln->followUp = rand;
    frail->followUp = rand;
    smash->followUp = rand;
    // AddBranch(state, cooldown, MoveRepeatType.CannotRepeat) — cooldown 3/2/0.
    rand->branches.push_back({vuln->id, MoveRepeat::CannotRepeat, 0, 3, [] { return 1.f; }});
    rand->branches.push_back({frail->id, MoveRepeat::CannotRepeat, 0, 2, [] { return 1.f; }});
    rand->add(smash, MoveRepeat::CannotRepeat);
    init->branches.push_back({frail->id, MoveRepeat::CannotRepeat, 0, 2, [] { return 1.f; }});
    init->add(smash, MoveRepeat::CannotRepeat);
    machine.start(init);
  }
  Task<> frailSporesMove(Targets t) {
    co_await attack(8);
    co_await applyToTargets<FrailPower>(t, 2);
  }
};

struct SnappingJaxfruit : Monster {
  MONSTER_HEADER(SnappingJaxfruit, "SNAPPING_JAXFRUIT")
  int minHp() const override { return 31; }
  int maxHp() const override { return 33; }
  void buildMoves() override {
    auto* orb = machine.add<MoveState>("ENERGY_ORB_MOVE");
    orb->perform = [this](Targets) { return energyOrb(); };
    orb->intents = {attackIntent(3), kindIntent(Intent::Buff)};
    orb->followUp = orb;
    machine.start(orb);
  }
  Task<> energyOrb() {
    co_await attack(3);
    co_await applyToSelf<StrengthPower>(2);
  }
};

struct SlitheringStrangler : Monster {
  MONSTER_HEADER(SlitheringStrangler, "SLITHERING_STRANGLER")
  int minHp() const override { return 53; }
  int maxHp() const override { return 55; }
  void buildMoves() override {
    auto* constrict = machine.add<MoveState>("CONSTRICT");
    constrict->perform = [this](Targets t) { return applyToTargets<ConstrictPower>(t, 3); };
    constrict->intents = {kindIntent(Intent::Debuff)};
    auto* thwack = machine.add<MoveState>("THWACK");
    thwack->perform = [this](Targets) { return thwackMove(); };
    thwack->intents = {attackIntent(7), kindIntent(Intent::Defend)};
    auto* lash = machine.add<MoveState>("LASH");
    lash->perform = [this](Targets) { return attack(12); };
    lash->intents = {attackIntent(12)};
    auto* rand = machine.add<RandomBranchState>("rand");
    constrict->followUp = rand;
    thwack->followUp = constrict;
    lash->followUp = constrict;
    rand->add(thwack, MoveRepeat::CanRepeatForever);
    rand->add(lash, MoveRepeat::CanRepeatForever);
    machine.start(constrict);
  }
  Task<> thwackMove() {
    co_await attack(7);
    co_await gainBlock(5);
  }
};

struct VineShambler : Monster {
  MONSTER_HEADER(VineShambler, "VINE_SHAMBLER")
  int minHp() const override { return 61; }
  int maxHp() const override { return 61; }
  void buildMoves() override {
    auto* vines = machine.add<MoveState>("GRASPING_VINES_MOVE");
    vines->perform = [this](Targets t) { return graspingVinesMove(t); };
    vines->intents = {attackIntent(8), kindIntent(Intent::DebuffStrong)};
    auto* swipe = machine.add<MoveState>("SWIPE_MOVE");
    swipe->perform = [this](Targets) { return attack(6, 2); };
    swipe->intents = {attackIntent(6, 2)};
    auto* chomp = machine.add<MoveState>("CHOMP_MOVE");
    chomp->perform = [this](Targets) { return attack(16); };
    chomp->intents = {attackIntent(16)};
    swipe->followUp = vines;
    vines->followUp = chomp;
    chomp->followUp = swipe;
    machine.start(swipe);
  }
  Task<> graspingVinesMove(Targets t) {
    co_await attack(8);
    co_await applyToTargets<TangledPower>(t, 1);
  }
};

struct CubexConstruct : Monster {
  MONSTER_HEADER(CubexConstruct, "CUBEX_CONSTRUCT")
  int minHp() const override { return 65; }
  int maxHp() const override { return 65; }
  Task<> afterAddedToRoom() override {
    co_await gainBlock(13);
    co_await applyToSelf<ArtifactPower>(1);
  }
  void buildMoves() override {
    auto* chargeUp = machine.add<MoveState>("CHARGE_UP_MOVE");
    chargeUp->perform = [this](Targets) { return applyToSelf<StrengthPower>(2); };
    chargeUp->intents = {kindIntent(Intent::Buff)};
    auto* blast1 = machine.add<MoveState>("REPEATER_BLAST_MOVE");
    blast1->perform = [this](Targets) { return repeaterBlastMove(); };
    blast1->intents = {attackIntent(7), kindIntent(Intent::Buff)};
    auto* blast2 = machine.add<MoveState>("REPEATER_BLAST_MOVE_2");
    blast2->perform = [this](Targets) { return repeaterBlastMove(); };
    blast2->intents = {attackIntent(7), kindIntent(Intent::Buff)};
    auto* expel = machine.add<MoveState>("EXPEL_MOVE");
    expel->perform = [this](Targets) { return attack(5, 2); };
    expel->intents = {attackIntent(5, 2)};
    chargeUp->followUp = blast1;
    blast1->followUp = blast2;
    blast2->followUp = expel;
    expel->followUp = blast1;
    machine.start(chargeUp);
  }
  Task<> repeaterBlastMove() {
    co_await attack(7);
    co_await applyToSelf<StrengthPower>(2);
  }
};

// Used by the Act 3 Construct Menagerie (content_act3a.cpp).
std::unique_ptr<Monster> makeCubexConstruct() { return std::make_unique<CubexConstruct>(); }

struct AssassinRubyRaider : Monster {
  MONSTER_HEADER(AssassinRubyRaider, "ASSASSIN_RUBY_RAIDER")
  int minHp() const override { return 18; }
  int maxHp() const override { return 23; }
  void buildMoves() override {
    auto* killshot = machine.add<MoveState>("KILLSHOT_MOVE");
    killshot->perform = [this](Targets) { return attack(10); };
    killshot->intents = {attackIntent(10)};
    killshot->followUp = killshot;
    machine.start(killshot);
  }
};

struct AxeRubyRaider : Monster {
  MONSTER_HEADER(AxeRubyRaider, "AXE_RUBY_RAIDER")
  int minHp() const override { return 20; }
  int maxHp() const override { return 22; }
  void buildMoves() override {
    auto* swing1 = machine.add<MoveState>("SWING_1");
    swing1->perform = [this](Targets) { return swingMove(); };
    swing1->intents = {attackIntent(5), kindIntent(Intent::Defend)};
    auto* swing2 = machine.add<MoveState>("SWING_2");
    swing2->perform = [this](Targets) { return swingMove(); };
    swing2->intents = {attackIntent(5), kindIntent(Intent::Defend)};
    auto* bigSwing = machine.add<MoveState>("BIG_SWING");
    bigSwing->perform = [this](Targets) { return attack(12); };
    bigSwing->intents = {attackIntent(12)};
    swing1->followUp = swing2;
    swing2->followUp = bigSwing;
    bigSwing->followUp = swing1;
    machine.start(swing1);
  }
  Task<> swingMove() {
    co_await attack(5);
    co_await gainBlock(5);
  }
};

struct BruteRubyRaider : Monster {
  MONSTER_HEADER(BruteRubyRaider, "BRUTE_RUBY_RAIDER")
  int minHp() const override { return 30; }
  int maxHp() const override { return 33; }
  void buildMoves() override {
    auto* beat = machine.add<MoveState>("BEAT_MOVE");
    beat->perform = [this](Targets) { return attack(7); };
    beat->intents = {attackIntent(7)};
    auto* roar = machine.add<MoveState>("ROAR_MOVE");
    roar->perform = [this](Targets) { return applyToSelf<StrengthPower>(3); };
    roar->intents = {kindIntent(Intent::Buff)};
    beat->followUp = roar;
    roar->followUp = beat;
    machine.start(beat);
  }
};

struct CrossbowRubyRaider : Monster {
  MONSTER_HEADER(CrossbowRubyRaider, "CROSSBOW_RUBY_RAIDER")
  int minHp() const override { return 18; }
  int maxHp() const override { return 21; }
  void buildMoves() override {
    auto* fire = machine.add<MoveState>("FIRE_MOVE");
    fire->perform = [this](Targets) { return attack(14); };
    fire->intents = {attackIntent(14)};
    auto* reload = machine.add<MoveState>("RELOAD_MOVE");
    reload->perform = [this](Targets) { return gainBlock(3); };
    reload->intents = {kindIntent(Intent::Defend)};
    fire->followUp = reload;
    reload->followUp = fire;
    machine.start(reload);
  }
};

struct TrackerRubyRaider : Monster {
  MONSTER_HEADER(TrackerRubyRaider, "TRACKER_RUBY_RAIDER")
  int minHp() const override { return 21; }
  int maxHp() const override { return 25; }
  void buildMoves() override {
    auto* track = machine.add<MoveState>("TRACK_MOVE");
    track->perform = [this](Targets t) { return applyToTargets<FrailPower>(t, 2); };
    track->intents = {kindIntent(Intent::Debuff)};
    auto* hounds = machine.add<MoveState>("HOUNDS_MOVE");
    hounds->perform = [this](Targets) { return attack(1, 8); };
    hounds->intents = {attackIntent(1, 8)};
    track->followUp = hounds;
    hounds->followUp = hounds;
    machine.start(track);
  }
};

struct BygoneEffigy : Monster {
  MONSTER_HEADER(BygoneEffigy, "BYGONE_EFFIGY")
  int minHp() const override { return 127; }
  int maxHp() const override { return 127; }
  Task<> afterAddedToRoom() override { co_await applyToSelf<SlowPower>(1); }
  void buildMoves() override {
    // SLEEP_MOVE_2 in the source is built but unreachable (never an initial state or
    // anyone's follow-up) — dropped here as dead code, same as e.g. Inklet's unused RAND.
    auto* sleep = machine.add<MoveState>("SLEEP_MOVE");
    sleep->perform = [](Targets) -> Task<> { co_return; };  // ThinkCmd dialogue only.
    sleep->intents = {kindIntent(Intent::Sleep)};
    auto* wake = machine.add<MoveState>("WAKE_MOVE");
    wake->perform = [this](Targets) { return applyToSelf<StrengthPower>(10); };
    wake->intents = {kindIntent(Intent::Buff)};
    auto* slashes = machine.add<MoveState>("SLASHES_MOVE");
    slashes->perform = [this](Targets) { return attack(13); };
    slashes->intents = {attackIntent(13)};
    sleep->followUp = wake;
    wake->followUp = slashes;
    slashes->followUp = slashes;
    machine.start(sleep);
  }
};

// ================================================================ encounters

namespace {
template <class M> std::unique_ptr<Monster> mk() { return std::make_unique<M>(); }
template <class... Ms> std::vector<std::unique_ptr<Monster>> list() {
  std::vector<std::unique_ptr<Monster>> v;
  (v.push_back(mk<Ms>()), ...);
  return v;
}
}  // namespace

// Defined in content.cpp: factory wrappers for existing monster types this file's
// encounters need to place alongside the new monsters (ShrinkerBeetle, FuzzyWurmCrawler
// and the four slimes are private to content.cpp).
std::unique_ptr<Monster> makeShrinkerBeetle();
std::unique_ptr<Monster> makeFuzzyWurmCrawler();
std::unique_ptr<Monster> makeLeafSlimeS();
std::unique_ptr<Monster> makeTwigSlimeS();
std::unique_ptr<Monster> makeLeafSlimeM();
std::unique_ptr<Monster> makeTwigSlimeM();

void registerAct1Monsters() {
  db::registerPower(ConstrictPower::kId, [] { return std::unique_ptr<Power>(new ConstrictPower()); });
  db::registerPower(TangledPower::kId, [] { return std::unique_ptr<Power>(new TangledPower()); });
  db::registerPower(ArtifactPower::kId, [] { return std::unique_ptr<Power>(new ArtifactPower()); });
  db::registerPower(SlowPower::kId, [] { return std::unique_ptr<Power>(new SlowPower()); });

  db::registerEncounter("OvergrowthCrawlers", RoomType::Monster, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(makeShrinkerBeetle());
    v.push_back(makeFuzzyWurmCrawler());
    return v;
  });

  db::registerEncounter("FlyconidNormal", RoomType::Monster, false, [](Rng& rng) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(rng.nextBool() ? makeLeafSlimeM() : makeTwigSlimeM());
    v.push_back(mk<Flyconid>());
    return v;
  });

  db::registerEncounter("SnappingJaxfruitNormal", RoomType::Monster, false, [](Rng&) {
    return list<SnappingJaxfruit, Flyconid>();
  });

  db::registerEncounter("SlitheringStranglerNormal", RoomType::Monster, false, [](Rng& rng) {
    std::vector<std::unique_ptr<Monster>> v;
    // SecondaryEnemyType: SnappingJaxfruit, MediumSlime, SmallSlimes (uniform pick of 3).
    switch (rng.nextInt(3)) {
      case 0:
        v.push_back(mk<SnappingJaxfruit>());
        break;
      case 1:
        v.push_back(rng.nextBool() ? makeLeafSlimeM() : makeTwigSlimeM());
        break;
      default:
        v.push_back(rng.nextBool() ? makeLeafSlimeS() : makeTwigSlimeS());
        v.push_back(rng.nextBool() ? makeLeafSlimeS() : makeTwigSlimeS());
        break;
    }
    v.push_back(mk<SlitheringStrangler>());
    return v;
  });

  db::registerEncounter("VineShamblerNormal", RoomType::Monster, false, [](Rng&) { return list<VineShambler>(); });

  db::registerEncounter("CubexConstructNormal", RoomType::Monster, false, [](Rng&) { return list<CubexConstruct>(); });

  db::registerEncounter("RubyRaidersNormal", RoomType::Monster, false, [](Rng& rng) {
    // 3 distinct raiders drawn from the 5, insertion order Axe/Assassin/Brute/Crossbow/Tracker.
    std::vector<int> pool = {0, 1, 2, 3, 4};
    std::vector<std::unique_ptr<Monster>> v;
    for (int i = 0; i < 3; ++i) {
      int idx = rng.nextItem(pool);
      switch (idx) {
        case 0: v.push_back(mk<AxeRubyRaider>()); break;
        case 1: v.push_back(mk<AssassinRubyRaider>()); break;
        case 2: v.push_back(mk<BruteRubyRaider>()); break;
        case 3: v.push_back(mk<CrossbowRubyRaider>()); break;
        default: v.push_back(mk<TrackerRubyRaider>()); break;
      }
      pool.erase(std::find(pool.begin(), pool.end(), idx));
    }
    return v;
  });

  db::registerEncounter("BygoneEffigyElite", RoomType::Elite, false, [](Rng&) { return list<BygoneEffigy>(); });
}

}  // namespace sts
