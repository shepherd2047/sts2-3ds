// Act 2 (Hive) monsters, package 4b: Hunter Killer, Louse Progenitor, Ovicopter (+ Tough
// Egg), Slumbering Beetle, The Obscura (+ Parafright), Thieving Hopper
// (MegaCrit.Sts2.Core.Models.Monsters / .Powers / .Encounters).
// Values are the non-ascension ones (see docs/PORTING.md).
#include <algorithm>

#include "cards.h"
#include "powers.h"

namespace sts {

// content_act2a.cpp: "BowlbugRock" / "BowlbugSilk" for the Slumbering Beetle encounter.
std::unique_ptr<Monster> makeAct2Monster(const std::string& id);

namespace {

#define MONSTER_HEADER(Name, Key) \
  Name() { id = #Name; locKey = Key; }

using Targets = const std::vector<Creature*>&;

Intent attackIntent(int dmg, int hits = 1) { Intent i; i.kind = Intent::Attack; i.damage = dmg; i.hits = hits; return i; }
Intent kindIntent(Intent::Kind k, int count = 0) { Intent i; i.kind = k; i.count = count; return i; }

template <class M> std::unique_ptr<Monster> mk() { return std::make_unique<M>(); }

// CreatureCmd.Escape: the creature leaves the room alive (no death hooks).
// PORT NOTE: the UI has no escape animation; the death animation is played instead.
Task<> escapeCreature(Creature* c) {
  c->combat->push({VisualEvent::Death, c, 0});
  c->hp = 0;
  c->removed = true;
  co_return;
}

// ================================================================ powers

// Hunter Killer's goop: every card played by the owner costs a Strength and a Dexterity
// until the end of the turn.
struct TenderPower : Power {
  POWER_HEADER(TenderPower, "TENDER_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  int cardsPlayedThisTurn = 0;
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (ownerOf(p.card) != owner) co_return;
    ++cardsPlayedThisTurn;
    flash = 1.f;
    co_await applyPower<StrengthPower>(owner, -1, applier, nullptr, true);
    co_await applyPower<DexterityPower>(owner, -1, applier, nullptr, true);
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner)) co_return;
    co_await applyPower<StrengthPower>(owner, cardsPlayedThisTurn, applier, nullptr, true);
    co_await applyPower<DexterityPower>(owner, cardsPlayedThisTurn, applier, nullptr, true);
    cardsPlayedThisTurn = 0;
  }
};

// Block after the first powered attack card that damaged the owner has finished resolving.
struct CurlUpPower : Power {
  POWER_HEADER(CurlUpPower, "CURL_UP_POWER")
  Card* playedCard = nullptr;
  Task<> afterDamageReceived(Creature* target, const DamageResult&, int props, Creature*, Card* src) override;
  Task<> afterCardPlayed(const CardPlay& p) override;
};

// Visual timer for the Tough Egg's hatching.
struct HatchPower : Power {
  POWER_HEADER(HatchPower, "HATCH_POWER")
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await cmd::decrement(this);
  }
};

// The Slumbering Beetle wakes after three hits or three turns.
struct SlumberPower : Power {
  POWER_HEADER(SlumberPower, "SLUMBER_POWER")
  Task<> afterDamageReceived(Creature* target, const DamageResult& r, int, Creature*, Card*) override;
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override;
};

// Just a visual timer for when the Thieving Hopper escapes.
struct EscapeArtistPower : Power {
  POWER_HEADER(EscapeArtistPower, "ESCAPE_ARTIST_POWER")
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner)) co_return;
    if (amount > 1) co_await cmd::decrement(this);
    if (amount == 1) flash = 1.f;  // StartPulsing
  }
};

// Holds the card the Hopper stole; it comes back if the Hopper is killed.
// PORT NOTE: the C# adds a SpecialCardReward to the room's loot; here the card goes
// straight back into the deck.
struct SwipePower : Power {
  POWER_HEADER(SwipePower, "SWIPE_POWER")
  StackType stackType() const override { return StackType::Single; }
  std::unique_ptr<Card> stolenCard;  // the deck version, taken out of the run's deck
  Task<> afterDeath(Creature* c) override {
    if (c != owner || !stolenCard || !owner->combat || !owner->combat->run) co_return;
    owner->combat->run->addCardToDeck(std::move(stolenCard));
  }
};

// Halves damage to the owner; after enough unblocked hits the Hopper falls out of the air.
struct FlutterPower : Power {
  POWER_HEADER(FlutterPower, "FLUTTER_POWER")
  Dec modifyDamageMultiplicative(Creature* target, Dec, int props, Creature*, Card*) override {
    if (target != owner || !isPoweredAttack(props)) return 1;
    return Dec(50) / Dec(100);  // DynamicVars["DamageDecrease"] = 50
  }
  Task<> afterDamageReceived(Creature* target, const DamageResult& r, int props, Creature*, Card*) override;
};

// ================================================================ Hunter Killer

struct HunterKiller : Monster {
  MONSTER_HEADER(HunterKiller, "HUNTER_KILLER")
  int minHp() const override { return asc(kToughEnemies, 126, 121); }
  int maxHp() const override { return minHp(); }
  void buildMoves() override {
    auto* goop = machine.add<MoveState>("TENDERIZING_GOOP_MOVE");
    goop->perform = [this](Targets t) { return applyToTargets<TenderPower>(t, 1); };
    goop->intents = {kindIntent(Intent::Debuff)};
    auto* bite = machine.add<MoveState>("BITE_MOVE");
    bite->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 19, 17)); };
    bite->intents = {attackIntent(asc(kDeadlyEnemies, 19, 17))};
    auto* puncture = machine.add<MoveState>("PUNCTURE_MOVE");
    puncture->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 8, 7), 3); };
    puncture->intents = {attackIntent(asc(kDeadlyEnemies, 8, 7), 3)};
    auto* rand = machine.add<RandomBranchState>("RAND");
    goop->followUp = rand;
    bite->followUp = rand;
    puncture->followUp = rand;
    rand->add(bite, MoveRepeat::CannotRepeat);
    rand->addMax(puncture, 2);
    machine.start(goop);
  }
};

// ================================================================ Louse Progenitor

struct LouseProgenitor : Monster {
  MONSTER_HEADER(LouseProgenitor, "LOUSE_PROGENITOR")
  bool curled = false;
  int minHp() const override { return asc(kToughEnemies, 138, 134); }
  int maxHp() const override { return asc(kToughEnemies, 141, 136); }
  Task<> afterAddedToRoom() override { co_await applyToSelf<CurlUpPower>(asc(kToughEnemies, 18, 14)); }
  void buildMoves() override {
    auto* web = machine.add<MoveState>("WEB_CANNON_MOVE");
    web->perform = [this](Targets t) { return webMove(t); };
    web->intents = {attackIntent(asc(kDeadlyEnemies, 10, 9)), kindIntent(Intent::Debuff)};
    auto* pounce = machine.add<MoveState>("POUNCE_MOVE");
    pounce->perform = [this](Targets) { return pounceMove(); };
    pounce->intents = {attackIntent(asc(kDeadlyEnemies, 16, 14))};
    auto* curl = machine.add<MoveState>("CURL_AND_GROW_MOVE");
    curl->perform = [this](Targets) { return curlAndGrowMove(); };
    curl->intents = {kindIntent(Intent::Defend), kindIntent(Intent::Buff)};
    web->followUp = curl;
    curl->followUp = pounce;
    pounce->followUp = web;
    machine.start(web);
  }
  Task<> webMove(Targets t) {
    curled = false;
    co_await attack(asc(kDeadlyEnemies, 10, 9));
    co_await applyToTargets<FrailPower>(t, 2);
  }
  Task<> curlAndGrowMove() {
    co_await gainBlock(asc(kToughEnemies, 18, 14));
    co_await applyToSelf<StrengthPower>(asc(kDeadlyEnemies, 7, 5));
    curled = true;
  }
  Task<> pounceMove() {
    curled = false;
    co_await attack(asc(kDeadlyEnemies, 16, 14));
  }
};

Task<> CurlUpPower::afterDamageReceived(Creature* target, const DamageResult&, int props, Creature*, Card* src) {
  if (target != owner || !isPoweredAttack(props) || !src) co_return;
  if (playedCard && src != playedCard) co_return;
  playedCard = src;
  co_return;
}

Task<> CurlUpPower::afterCardPlayed(const CardPlay& p) {
  if (!playedCard || p.card != playedCard) co_return;
  playedCard = nullptr;
  co_await cmd::gainBlock(owner, amount, kUnpowered, nullptr);
  if (owner->monster && owner->monster->id == "LouseProgenitor") static_cast<LouseProgenitor*>(owner->monster.get())->curled = true;
  co_await cmd::removePower(this);
}

// ================================================================ Ovicopter

struct ToughEgg : Monster {
  MONSTER_HEADER(ToughEgg, "TOUGH_EGG")
  bool isHatched = false;
  int minHp() const override { return asc(kToughEnemies, 15, 14); }
  int maxHp() const override { return asc(kToughEnemies, 19, 18); }
  Task<> afterAddedToRoom() override {
    // PORT NOTE: the C# only ever creates unhatched eggs here (IsHatched is for save/restore).
    co_await applyToSelf<HatchPower>(combat->currentSide != Side::Enemy ? 1 : 2);
  }
  void buildMoves() override {
    auto* hatch = machine.add<MoveState>("HATCH_MOVE");
    hatch->perform = [this](Targets) { return hatchMove(); };
    hatch->intents = {kindIntent(Intent::Summon)};
    auto* nibble = machine.add<MoveState>("NIBBLE_MOVE");
    nibble->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 5, 4)); };
    nibble->intents = {attackIntent(asc(kDeadlyEnemies, 5, 4))};
    hatch->followUp = nibble;
    nibble->followUp = nibble;
    machine.start(hatch);
  }
  Task<> hatchMove() {
    isHatched = true;
    if (auto* p = creature->get<HatchPower>()) co_await cmd::removePower(p);
    creature->name = "HATCHLING";
    std::vector<Power*> drop;
    for (auto& p : creature->powers) if (p->id != "MinionPower") drop.push_back(p.get());
    for (Power* p : drop) co_await cmd::removePower(p);
    int hp = combat->rng("Niche").nextInt(asc(kToughEnemies, 20, 19), asc(kToughEnemies, 23, 22) + 1);  // HatchlingMinHp..HatchlingMaxHp
    creature->hp = creature->maxHp = hp;                // CreatureCmd.SetMaxAndCurrentHp
  }
};

struct Ovicopter : Monster {
  MONSTER_HEADER(Ovicopter, "OVICOPTER")
  int minHp() const override { return asc(kToughEnemies, 126, 124); }
  int maxHp() const override { return asc(kToughEnemies, 132, 130); }
  bool canLay() const {
    int alive = 0;
    for (auto* e : combat->enemies) if (e->alive() && !e->removed) ++alive;
    return alive <= 3;
  }
  void buildMoves() override {
    auto* lay = machine.add<MoveState>("LAY_EGGS_MOVE");
    lay->perform = [this](Targets) { return layEggsMove(); };
    lay->intents = {kindIntent(Intent::Summon)};
    auto* smash = machine.add<MoveState>("SMASH_MOVE");
    smash->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 17, 16)); };
    smash->intents = {attackIntent(asc(kDeadlyEnemies, 17, 16))};
    auto* tender = machine.add<MoveState>("TENDERIZER_MOVE");
    tender->perform = [this](Targets t) { return tenderizerMove(t); };
    tender->intents = {attackIntent(asc(kDeadlyEnemies, 8, 7)), kindIntent(Intent::Debuff)};
    auto* paste = machine.add<MoveState>("NUTRITIONAL_PASTE_MOVE");
    paste->perform = [this](Targets) { return applyToSelf<StrengthPower>(asc(kDeadlyEnemies, 4, 3)); };
    paste->intents = {kindIntent(Intent::Buff)};
    auto* branch = machine.add<ConditionalBranchState>("SUMMON_BRANCH_STATE");
    lay->followUp = smash;
    paste->followUp = smash;
    smash->followUp = tender;
    tender->followUp = branch;
    branch->add(lay, [this] { return canLay(); });
    branch->add(paste, [this] { return !canLay(); });
    machine.start(lay);
  }
  Task<> layEggsMove() {
    for (int i = 0; i < 3; ++i) {
      // The encounter has five egg slots; a slot is free once its egg has left the room.
      int eggs = 0;
      for (auto* e : combat->enemies)
        if (!e->removed && e->monster && e->monster->id == "ToughEgg") ++eggs;
      if (eggs >= 5) continue;
      Creature* egg = co_await cmd::addMonster(*combat, mk<ToughEgg>());
      co_await cmd::applyPower(db::power("MinionPower"), egg, 1, creature, nullptr);
    }
  }
  Task<> tenderizerMove(Targets t) {
    co_await attack(asc(kDeadlyEnemies, 8, 7));
    co_await applyToTargets<VulnerablePower>(t, 2);
  }
};

// ================================================================ Slumbering Beetle

struct SlumberingBeetle : Monster {
  MONSTER_HEADER(SlumberingBeetle, "SLUMBERING_BEETLE")
  bool isAwake = false;
  int minHp() const override { return asc(kToughEnemies, 89, 86); }
  int maxHp() const override { return minHp(); }
  Task<> afterAddedToRoom() override {
    co_await applyToSelf<PlatingPower>(asc(kToughEnemies, 18, 15));
    co_await applyToSelf<SlumberPower>(3);
  }
  Task<> wakeUpMove(Targets) {
    isAwake = true;
    if (auto* p = creature->get<PlatingPower>()) co_await cmd::removePower(p);
  }
  void buildMoves() override {
    auto* snore = machine.add<MoveState>("SNORE_MOVE");
    snore->perform = [](Targets) -> Task<> { co_return; };
    snore->intents = {kindIntent(Intent::Sleep)};
    auto* roll = machine.add<MoveState>("ROLL_OUT_MOVE");
    roll->perform = [this](Targets) { return rolloutMove(); };
    roll->intents = {attackIntent(asc(kDeadlyEnemies, 18, 16)), kindIntent(Intent::Buff)};
    auto* next = machine.add<ConditionalBranchState>("SNORE_NEXT");
    snore->followUp = next;
    next->add(snore, [this] { return creature->get<SlumberPower>() != nullptr; });
    next->add(roll, [this] { return creature->get<SlumberPower>() == nullptr; });
    roll->followUp = roll;
    machine.start(snore);
  }
  Task<> rolloutMove() {
    co_await attack(asc(kDeadlyEnemies, 18, 16));
    co_await applyToSelf<StrengthPower>(2);
  }
};

Task<> SlumberPower::afterDamageReceived(Creature* target, const DamageResult& r, int, Creature*, Card*) {
  if (target != owner || r.unblocked == 0) co_return;
  co_await cmd::decrement(this);
  if (amount <= 0 && owner->monster && owner->monster->id == "SlumberingBeetle") {
    auto* b = static_cast<SlumberingBeetle*>(owner->monster.get());
    b->stun([b](Targets t) { return b->wakeUpMove(t); }, "ROLL_OUT_MOVE");
  }
}

Task<> SlumberPower::afterSideTurnEnd(Side, const std::vector<Creature*>& participants) {
  if (!contains(participants, owner)) co_return;
  co_await cmd::decrement(this);
  if (amount <= 0 && owner->monster && owner->monster->id == "SlumberingBeetle")
    co_await static_cast<SlumberingBeetle*>(owner->monster.get())->wakeUpMove({});
}

// ================================================================ The Obscura

struct Parafright : Monster {
  MONSTER_HEADER(Parafright, "PARAFRIGHT")
  int minHp() const override { return 21; }
  int maxHp() const override { return minHp(); }
  Task<> afterAddedToRoom() override {
    co_await cmd::applyPower(db::power("IllusionPower"), creature, 1, creature, nullptr);
  }
  void buildMoves() override {
    auto* slam = machine.add<MoveState>("SLAM_MOVE");
    slam->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 17, 16)); };
    slam->intents = {attackIntent(asc(kDeadlyEnemies, 17, 16))};
    slam->followUp = slam;
    machine.start(slam);
  }
};

struct TheObscura : Monster {
  MONSTER_HEADER(TheObscura, "THE_OBSCURA")
  bool hasSummoned = false;
  int minHp() const override { return asc(kToughEnemies, 129, 123); }
  int maxHp() const override { return minHp(); }
  void buildMoves() override {
    auto* illusion = machine.add<MoveState>("ILLUSION_MOVE");
    illusion->perform = [this](Targets) { return illusionMove(); };
    illusion->intents = {kindIntent(Intent::Summon)};
    auto* gaze = machine.add<MoveState>("PIERCING_GAZE_MOVE");
    gaze->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 11, 10)); };
    gaze->intents = {attackIntent(asc(kDeadlyEnemies, 11, 10))};
    auto* wail = machine.add<MoveState>("SAIL_MOVE");  // sic: the C# id
    wail->perform = [this](Targets) { return wailMove(); };
    wail->intents = {kindIntent(Intent::Buff)};
    auto* strike = machine.add<MoveState>("HARDENING_STRIKE_MOVE");
    strike->perform = [this](Targets) { return hardeningStrikeMove(); };
    strike->intents = {attackIntent(asc(kDeadlyEnemies, 7, 6)), kindIntent(Intent::Defend)};
    auto* rand = machine.add<RandomBranchState>("RAND");
    illusion->followUp = rand;
    gaze->followUp = rand;
    wail->followUp = rand;
    strike->followUp = rand;
    rand->add(gaze, MoveRepeat::CannotRepeat);
    rand->add(wail, MoveRepeat::CannotRepeat);
    rand->add(strike, MoveRepeat::CannotRepeat);
    machine.start(illusion);
  }
  Task<> illusionMove() {
    co_await cmd::addMonster(*combat, mk<Parafright>());
    hasSummoned = true;
  }
  Task<> wailMove() {
    std::vector<Creature*> mates;  // GetTeammatesOf(Creature): includes itself
    for (auto* e : combat->enemies) if (e->alive() && !e->removed) mates.push_back(e);
    co_await applyToTargets<StrengthPower>(mates, 3);
  }
  Task<> hardeningStrikeMove() {
    co_await attack(asc(kDeadlyEnemies, 7, 6));
    co_await gainBlock(asc(kDeadlyEnemies, 7, 6));
  }
};

// ================================================================ Thieving Hopper

struct ThievingHopper : Monster {
  MONSTER_HEADER(ThievingHopper, "THIEVING_HOPPER")
  bool isHovering = false;
  int minHp() const override { return asc(kToughEnemies, 84, 79); }
  int maxHp() const override { return minHp(); }
  Task<> afterAddedToRoom() override { co_await applyToSelf<EscapeArtistPower>(5); }
  void buildMoves() override {
    auto* thievery = machine.add<MoveState>("THIEVERY_MOVE");
    thievery->perform = [this](Targets t) { return thieveryMove(t); };
    thievery->intents = {attackIntent(asc(kDeadlyEnemies, 19, 17)), kindIntent(Intent::Debuff)};  // CardDebuffIntent
    auto* nab = machine.add<MoveState>("NAB_MOVE");
    nab->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 16, 14)); };
    nab->intents = {attackIntent(asc(kDeadlyEnemies, 16, 14))};
    auto* hat = machine.add<MoveState>("HAT_TRICK_MOVE");
    hat->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 23, 21)); };
    hat->intents = {attackIntent(asc(kDeadlyEnemies, 23, 21))};
    auto* flutter = machine.add<MoveState>("FLUTTER_MOVE");
    flutter->perform = [this](Targets) { return flutterMove(); };
    flutter->intents = {kindIntent(Intent::Buff)};
    auto* escape = machine.add<MoveState>("ESCAPE_MOVE");
    escape->perform = [this](Targets) { return escapeMove(); };
    escape->intents = {kindIntent(Intent::Escape)};
    thievery->followUp = flutter;
    flutter->followUp = hat;
    hat->followUp = nab;
    nab->followUp = escape;
    escape->followUp = escape;
    machine.start(thievery);
  }

  // Steal priorities: Uncommon, then Common/Rare, then Basic, then Ancient.
  // PORT NOTE: combat cards are clones of the deck cards with no link back, so a card is
  // stealable only if the run's deck holds a copy with the same id and upgrade level;
  // enchantments (Imbued) do not exist here.
  static int stealTier(Rarity r) {
    switch (r) {
      case Rarity::Uncommon: return 0;
      case Rarity::Common: case Rarity::Rare: return 1;
      case Rarity::Basic: return 2;
      case Rarity::Ancient: return 3;
      default: return -1;
    }
  }
  static Card* deckCopy(Run& run, Card* c) {
    for (auto& d : run.deck) if (d->id == c->id && d->upgradeLevel == c->upgradeLevel) return d.get();
    return nullptr;
  }
  Task<> thieveryMove(Targets targets) {
    Run* run = combat->run;
    std::unique_ptr<Card> stolen;
    if (combat->player->alive() && run) {
      std::vector<Card*> pool;
      for (Card* c : combat->draw) pool.push_back(c);
      for (Card* c : combat->discard) pool.push_back(c);
      std::vector<Card*> chosen;
      for (int tier = 0; tier < 4 && chosen.empty(); ++tier)
        for (Card* c : pool)
          if (stealTier(c->rarity) == tier && deckCopy(*run, c)) chosen.push_back(c);
      if (chosen.empty())
        for (Card* c : pool) if (deckCopy(*run, c)) chosen.push_back(c);
      if (!chosen.empty()) {
        Card* pick = combat->rng("CombatCardGeneration").nextItem(chosen);
        Card* deckCard = deckCopy(*run, pick);
        for (auto it = run->deck.begin(); it != run->deck.end(); ++it)
          if (it->get() == deckCard) { stolen = std::move(*it); run->deck.erase(it); break; }
        combat->removeFromPiles(pick);  // CardPileCmd.RemoveFromCombat
      }
    }
    if (stolen) {
      auto swipe = std::make_unique<SwipePower>();
      swipe->stolenCard = std::move(stolen);
      co_await cmd::applyPower(std::move(swipe), creature, 1, creature, nullptr);
    }
    co_await attack(asc(kDeadlyEnemies, 19, 17));
  }
  Task<> flutterMove() {
    isHovering = true;
    co_await applyToSelf<FlutterPower>(5);
  }
  Task<> escapeMove() {
    isHovering = false;
    co_await escapeCreature(creature);
  }
};

Task<> FlutterPower::afterDamageReceived(Creature* target, const DamageResult& r, int props, Creature*, Card*) {
  if (target != owner || r.unblocked == 0 || !isPoweredAttack(props)) co_return;
  co_await cmd::decrement(this);
  if (amount > 0 || !owner->monster || owner->monster->id != "ThievingHopper") co_return;
  auto* h = static_cast<ThievingHopper*>(owner->monster.get());
  std::string next;
  if (!h->machine.stateLog.empty()) next = h->machine.stateLog.back()->nextState(*h, h->combat->rng("MonsterAi"));
  h->stun(nullptr, next);
  h->isHovering = false;
  flash = 1.f;
}

}  // namespace

void registerAct2B() {
  db::registerPower(TenderPower::kId, [] { return std::unique_ptr<Power>(new TenderPower()); });
  db::registerPower(CurlUpPower::kId, [] { return std::unique_ptr<Power>(new CurlUpPower()); });
  db::registerPower(HatchPower::kId, [] { return std::unique_ptr<Power>(new HatchPower()); });
  db::registerPower(SlumberPower::kId, [] { return std::unique_ptr<Power>(new SlumberPower()); });
  db::registerPower(EscapeArtistPower::kId, [] { return std::unique_ptr<Power>(new EscapeArtistPower()); });
  db::registerPower(SwipePower::kId, [] { return std::unique_ptr<Power>(new SwipePower()); });
  db::registerPower(FlutterPower::kId, [] { return std::unique_ptr<Power>(new FlutterPower()); });

  db::registerEncounter("HunterKillerNormal", RoomType::Monster, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(mk<HunterKiller>());
    return v;
  });
  db::registerEncounter("LouseProgenitorNormal", RoomType::Monster, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(mk<LouseProgenitor>());
    return v;
  });
  db::registerEncounter("OvicopterNormal", RoomType::Monster, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(mk<Ovicopter>());
    return v;
  });
  db::registerEncounter("SlumberingBeetleNormal", RoomType::Monster, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(makeAct2Monster("BowlbugRock"));
    v.push_back(makeAct2Monster("BowlbugSilk"));
    v.push_back(mk<SlumberingBeetle>());
    return v;
  });
  db::registerEncounter("TheObscuraNormal", RoomType::Monster, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(mk<TheObscura>());
    return v;
  });
  db::registerEncounter("ThievingHopperWeak", RoomType::Monster, true, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(mk<ThievingHopper>());
    return v;
  });
}

}  // namespace sts
