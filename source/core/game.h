// Core game model: a single-player port of the StS2 combat rules.
//
// Naming follows the decompiled C# so each piece can be checked against its
// source: Hook.* -> Combat::hook*, CreatureCmd/PowerCmd/CardPileCmd -> cmd::*,
// CombatManager -> Combat::run and friends.
#pragma once
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "dec.h"
#include "rng.h"
#include "task.h"

namespace sts {

struct Creature;
struct Power;
struct Card;
struct Monster;
struct Relic;
struct Combat;
struct Run;

enum class Side { Player, Enemy };
enum class CardType { Attack, Skill, Power, Status, Curse };
enum class Rarity { Basic, Common, Uncommon, Rare, Ancient, Token, Status, Curse };
enum class TargetType { None, Self, AnyEnemy, AllEnemies, RandomEnemy };
enum class PowerType { Buff, Debuff };
enum class StackType { Counter, Single };
enum class Pile { None, Draw, Hand, Discard, Exhaust, Play };

// ValueProp flags.
enum : int { kUnblockable = 2, kUnpowered = 4, kMove = 8, kSkipHurtAnim = 16 };
inline bool isPoweredAttack(int p) { return (p & kMove) && !(p & kUnpowered); }
inline bool isPoweredBlock(int p) { return (p & kMove) && !(p & kUnpowered); }

enum Keyword : int { kwExhaust = 1, kwUnplayable = 2, kwEthereal = 4, kwInnate = 8, kwRetain = 16 };
enum CardTag : int { tagStrike = 1, tagDefend = 2 };

struct DamageResult {
  Creature* receiver = nullptr;
  int props = 0;
  int blocked = 0;
  int unblocked = 0;
  int overkill = 0;
  bool killed = false;
  bool blockBroken = false;
  bool fullyBlocked = false;
};

struct CardPlay {
  Card* card = nullptr;
  Creature* target = nullptr;
  Pile resultPile = Pile::Discard;
  bool autoPlay = false;
  int energySpent = 0;
  int playIndex = 0, playCount = 1;
};

// AbstractModel: everything that can listen to hooks. Defaults do nothing;
// a default-constructed Task is already complete so they cost nothing.
struct Model {
  virtual ~Model() = default;

  virtual Dec modifyDamageAdditive(Creature*, Dec, int, Creature*, Card*) { return 0; }
  virtual Dec modifyDamageMultiplicative(Creature*, Dec, int, Creature*, Card*) { return 1; }
  virtual Dec modifyBlockAdditive(Creature*, Dec, int, Card*) { return 0; }
  virtual Dec modifyBlockMultiplicative(Creature*, Dec, int, Card*) { return 1; }
  virtual Dec modifyHpLostAfterOsty(Creature*, Dec amount, int, Creature*, Card*) { return amount; }
  virtual Dec modifyHandDraw(Dec amount) { return amount; }

  virtual Task<> beforeCombatStart() { return {}; }
  virtual Task<> afterCombatVictory() { return {}; }
  virtual Task<> beforeSideTurnStart(Side, const std::vector<Creature*>&) { return {}; }
  virtual Task<> afterSideTurnStart(Side, const std::vector<Creature*>&) { return {}; }
  virtual Task<> afterPlayerTurnStart() { return {}; }
  virtual Task<> beforeSideTurnEnd(Side, const std::vector<Creature*>&) { return {}; }
  virtual Task<> afterSideTurnEnd(Side, const std::vector<Creature*>&) { return {}; }
  virtual Task<> afterDamageReceived(Creature*, const DamageResult&, int, Creature*, Card*) { return {}; }
  virtual Task<> afterDeath(Creature*) { return {}; }
  virtual Task<> beforeCardPlayed(const CardPlay&) { return {}; }
  virtual Task<> afterCardPlayed(const CardPlay&) { return {}; }
  virtual Task<> afterPowerAmountChanged(Power*, Dec, Creature*, Card*) { return {}; }

  // Added for the full Ironclad pool (names follow Hook.*).
  virtual Task<> beforeSideTurnEndEarly(Side, const std::vector<Creature*>&) { return {}; }
  virtual Task<> afterAutoPostPlayPhaseEntered() { return {}; }  // player's turn is about to end
  virtual Task<> afterCardExhausted(Card*, bool /*causedByEthereal*/) { return {}; }
  virtual Task<> afterCardDrawn(Card*, bool /*fromHandDraw*/) { return {}; }
  virtual Task<> afterBlockGained(Creature*, Dec /*amount*/, int /*props*/, Card*) { return {}; }
  virtual Task<> afterCardEnteredCombat(Card*) { return {}; }
  virtual Task<> afterModifyingCardPlayCount(Card*) { return {}; }
  virtual Task<> afterEnergySpent(Card*, int) { return {}; }
  virtual bool shouldDraw(bool /*fromHandDraw*/) { return true; }
  virtual bool shouldClearBlock(Creature*) { return true; }
  virtual Dec modifyMaxEnergy(Dec amount) { return amount; }
  virtual int modifyCardPlayCount(Card*, Creature*, int count) { return count; }
  virtual Pile modifyCardPlayResultLocation(Card*, bool /*autoPlay*/, Pile pile) { return pile; }
  // TryModifyEnergyCostInCombat / ...Late: return the new cost (or `cost` unchanged).
  virtual int modifyEnergyCost(Card*, int cost) { return cost; }
  virtual int modifyEnergyCostLate(Card*, int cost) { return cost; }
  // Added for act 1 monsters.
  virtual bool shouldStopCombatFromEnding() { return false; }
  virtual Task<> afterCreatureAddedToCombat(Creature*) { return {}; }
  virtual bool shouldPlay(Card*) { return true; }  // Hook.ShouldPlay (RingingPower, ...)
  // Illusions stay in the room (dead) with their buffs and revive.
  virtual bool shouldCreatureBeRemovedFromCombatAfterDeath(Creature*) { return true; }
  virtual bool shouldPowerBeRemovedOnDeath(Power*) { return true; }
  // Hook.ModifyPowerAmountReceived (ArtifactPower): return true and set `out` to change
  // the amount a creature is about to receive; the modifier then gets the After... call.
  virtual bool tryModifyPowerAmountReceived(Power* /*incoming*/, Creature* /*target*/, Dec /*amount*/,
                                            Creature* /*applier*/, Dec& /*out*/) { return false; }
  virtual Task<> afterModifyingPowerAmountReceived(Power*) { return {}; }
};

// ---------------------------------------------------------------- powers

struct Power : Model {
  std::string id;      // class name, e.g. "StrengthPower"
  std::string locKey;  // e.g. "STRENGTH_POWER"
  Creature* owner = nullptr;
  Creature* applier = nullptr;
  int amount = 0;
  int amountOnTurnStart = 0;
  bool skipNextDurationTick = false;
  float flash = 0;  // UI

  virtual PowerType type() const { return PowerType::Buff; }
  virtual StackType stackType() const { return StackType::Counter; }
  virtual bool allowNegative() const { return false; }
  virtual bool ownerIsSecondaryEnemy() const { return false; }  // MinionPower
  virtual bool removedAfterOwnerDeath() const { return true; }  // ShouldPowerBeRemovedAfterOwnerDeath
  virtual Task<> beforeApplied(Creature*, Dec, Creature*, Card*) { return {}; }
  virtual Task<> afterApplied(Creature*, Card*) { return {}; }
  virtual Task<> afterRemoved(Creature*) { return {}; }

  // PowerModel.GetTypeForAmount: a negative stack of an allow-negative buff is a debuff.
  PowerType typeForAmount(Dec a) const {
    if (allowNegative() && a < Dec(0)) return type() == PowerType::Buff ? PowerType::Debuff : PowerType::Buff;
    return type();
  }
  bool shouldRemoveDueToAmount() const {
    if (allowNegative() || amount > 0) return allowNegative() ? amount == 0 : false;
    return true;
  }
};

using PowerFactory = std::unique_ptr<Power> (*)();

// ---------------------------------------------------------------- cards

struct DynVar {
  std::string name;
  Dec base;       // current (after upgrades)
  Dec canonical;  // before upgrades, for diff() colouring
};

struct Card : Model {
  std::string id;      // class name
  std::string locKey;  // e.g. "STRIKE_IRONCLAD"
  std::string portrait;  // atlas key
  int cost = 1;
  int canonicalCost = 1;
  CardType type = CardType::Attack;
  Rarity rarity = Rarity::Common;
  TargetType target = TargetType::AnyEnemy;
  int keywords = 0;
  int tags = 0;
  int upgradeLevel = 0;
  int maxUpgradeLevel = 1;
  bool isDupe = false;
  bool costsX = false;   // HasEnergyCostX
  int xValue = 0;        // captured X when played (ResolveEnergyXValue)
  // CardEnergyCost local modifiers.
  enum CostExpiry : int { kEndOfTurn = 1, kWhenPlayed = 2, kEndOfCombat = 4 };
  struct CostMod { int value; bool absolute; int expiry; bool reduceOnly; };
  std::vector<CostMod> costMods;
  std::vector<DynVar> vars;
  Combat* combat = nullptr;

  // Hand-view calculation for CalculatedDamageVar (Body Slam, Perfected Strike).
  std::function<int(Card*)> calcMultiplier;

  virtual ~Card() = default;
  virtual Task<> onPlay(CardPlay&) { return {}; }
  virtual void onUpgrade() {}
  // HasTurnEndInHandEffect / OnTurnEndInHand (Burn, Infection, ...)
  virtual bool hasTurnEndInHandEffect() const { return false; }
  virtual Task<> onTurnEndInHand() { return {}; }
  virtual std::unique_ptr<Card> clone() const = 0;

  // CardEnergyCost setters.
  void setUntilPlayed(int c, bool reduceOnly = false) { costMods.push_back({c, true, kWhenPlayed, reduceOnly}); }
  void setThisTurnOrUntilPlayed(int c, bool reduceOnly = false) { costMods.push_back({c, true, kEndOfTurn | kWhenPlayed, reduceOnly}); }
  void setThisTurn(int c, bool reduceOnly = false) { costMods.push_back({c, true, kEndOfTurn, reduceOnly}); }
  void setThisCombat(int c, bool reduceOnly = false) { costMods.push_back({c, true, kEndOfCombat, reduceOnly}); }
  void addUntilPlayed(int a, bool reduceOnly = false) { if (a) costMods.push_back({a, false, kWhenPlayed, reduceOnly}); }
  void addThisTurnOrUntilPlayed(int a, bool reduceOnly = false) { if (a) costMods.push_back({a, false, kEndOfTurn | kWhenPlayed, reduceOnly}); }
  void addThisTurn(int a, bool reduceOnly = false) { if (a) costMods.push_back({a, false, kEndOfTurn, reduceOnly}); }
  void addThisCombat(int a, bool reduceOnly = false) { if (a) costMods.push_back({a, false, kEndOfCombat, reduceOnly}); }
  void clearCostMods(int expiry) {
    costMods.erase(std::remove_if(costMods.begin(), costMods.end(), [&](const CostMod& m) { return (m.expiry & expiry) != 0; }), costMods.end());
  }
  int costWithLocalMods() const {
    int c = cost;
    if (c < 0 || costsX) return c;
    for (auto& m : costMods) {
      int n = m.absolute ? m.value : c + m.value;
      if (!m.reduceOnly || n < c) c = n;
    }
    return std::max(0, c);
  }

  bool upgraded() const { return upgradeLevel > 0; }
  bool upgradable() const { return upgradeLevel < maxUpgradeLevel; }
  void upgrade() { if (upgradable()) { ++upgradeLevel; onUpgrade(); } }
  bool has(int kw) const { return (keywords & kw) != 0; }

  DynVar* var(const char* n) { for (auto& v : vars) if (v.name == n) return &v; return nullptr; }
  Dec val(const char* n) { auto* v = var(n); return v ? v->base : Dec(0); }
  void upgradeVar(const char* n, Dec by) { if (auto* v = var(n)) v->base += by; }
  void addVar(const char* n, Dec v) { vars.push_back({n, v, v}); }

  Dec calculatedDamage();  // CalculatedDamageVar: CalculationBase + ExtraDamage * multiplier
  Dec calculatedBlock();   // CalculatedBlockVar: CalculationBase + CalculationExtra * multiplier
};

template <class Derived> struct CardT : Card {
  std::unique_ptr<Card> clone() const override { return std::make_unique<Derived>(static_cast<const Derived&>(*this)); }
};

using CardFactory = std::unique_ptr<Card> (*)();

// ---------------------------------------------------------------- monsters

struct Intent {
  enum Kind { Attack, Defend, Buff, Debuff, DebuffStrong, Status, Stun, Escape, Unknown, Summon, Heal, Sleep } kind = Unknown;
  int damage = 0;  // per hit, before modifiers
  int hits = 1;
  int count = 0;   // status cards
};

enum class MoveRepeat { CanRepeatForever, CannotRepeat, CanRepeatXTimes, UseOnlyOnce };

struct MonsterState {
  std::string id;
  virtual ~MonsterState() = default;
  virtual bool isMove() const { return false; }
  virtual bool shouldAppearInLogs() const { return true; }
  virtual bool canTransitionAway() const { return true; }
  virtual std::string nextState(Monster& m, Rng& rng) = 0;
  virtual void onExit() {}
};

struct MoveState : MonsterState {
  std::function<Task<>(const std::vector<Creature*>&)> perform;
  std::vector<Intent> intents;
  MonsterState* followUp = nullptr;
  std::string followUpId;
  bool mustPerformOnce = false;
  bool performedAtLeastOnce = false;

  bool isMove() const override { return true; }
  bool canTransitionAway() const override { return !mustPerformOnce || performedAtLeastOnce; }
  std::string nextState(Monster&, Rng&) override { return followUp ? followUp->id : followUpId; }
  void onExit() override { performedAtLeastOnce = false; }
};

struct RandomBranchState : MonsterState {
  struct Branch {
    std::string stateId;
    MoveRepeat repeat = MoveRepeat::CanRepeatForever;
    int maxTimes = 0;
    int cooldown = 0;
    std::function<float()> weight;
  };
  std::vector<Branch> branches;
  bool shouldAppearInLogs() const override { return false; }
  void add(MonsterState* s, MoveRepeat r, float w = 1.f) { branches.push_back({s->id, r, 0, 0, [w] { return w; }}); }
  void addMax(MonsterState* s, int maxRepeats, float w = 1.f) {
    branches.push_back({s->id, MoveRepeat::CanRepeatXTimes, maxRepeats, 0, [w] { return w; }});
  }
  std::string nextState(Monster& m, Rng& rng) override;
};

struct ConditionalBranchState : MonsterState {
  std::vector<std::pair<std::string, std::function<bool()>>> branches;
  bool shouldAppearInLogs() const override { return false; }
  void add(MonsterState* s, std::function<bool()> c) { branches.push_back({s->id, std::move(c)}); }
  std::string nextState(Monster&, Rng&) override {
    for (auto& b : branches) if (b.second()) return b.first;
    return {};
  }
};

struct MoveStateMachine {
  std::map<std::string, std::unique_ptr<MonsterState>> states;
  std::vector<MonsterState*> stateLog;
  MonsterState* initial = nullptr;
  MonsterState* current = nullptr;
  bool performedFirstMove = false;

  template <class S> S* add(const std::string& id) {
    auto s = std::make_unique<S>();
    s->id = id;
    S* raw = s.get();
    states[id] = std::move(s);
    return raw;
  }
  void start(MonsterState* init) {
    initial = current = init;
    if (current->shouldAppearInLogs()) stateLog.push_back(current);
  }
  MoveState* rollMove(Monster& m, Rng& rng);
  void setCurrent(MonsterState* s) { current->onExit(); current = s; }
  // States made at runtime (STUNNED) are not registered but must outlive their turn.
  std::vector<std::unique_ptr<MonsterState>> transient;
};

struct Monster : Model {
  std::string id;
  std::string locKey;  // e.g. "NIBBIT"
  Creature* creature = nullptr;
  Combat* combat = nullptr;
  MoveStateMachine machine;
  MoveState* nextMove = nullptr;
  bool spawnedThisTurn = false;

  virtual int minHp() const = 0;
  virtual int maxHp() const = 0;
  virtual void buildMoves() = 0;  // GenerateMoveStateMachine
  virtual Task<> afterAddedToRoom() { return {}; }

  void rollMove(Rng& rng) { nextMove = machine.rollMove(*this, rng); }
  Task<> performMove();
  // CreatureCmd.Stun: the next move becomes STUNNED (running stunMove, if any), then
  // nextMoveId (default: the last logged state).
  void stun(std::function<Task<>(const std::vector<Creature*>&)> stunMove = nullptr, std::string nextMoveId = "");
  // MonsterModel.SetMoveImmediate
  void setMoveImmediate(MoveState* s, bool force = false);
  bool stunned() const { return nextMove && nextMove->id == "STUNNED"; }

  // Helpers used by the translated monster code.
  Task<> attack(int damage, int hits = 1);
  Task<> gainBlock(int amount);
  template <class P> Task<> applyToSelf(Dec amount);
  template <class P> Task<> applyToTargets(std::vector<Creature*> targets, Dec amount);
};

// ---------------------------------------------------------------- creatures

struct Creature {
  std::string name;  // display
  int hp = 0, maxHp = 0, block = 0;
  Side side = Side::Enemy;
  std::vector<std::unique_ptr<Power>> powers;
  std::unique_ptr<Monster> monster;
  bool isPlayer = false;
  Combat* combat = nullptr;
  bool removed = false;  // gone from the room after dying

  // UI state
  float hitFlash = 0;
  float shake = 0;
  float displayHp = -1;
  float lunge = 0;

  bool alive() const { return hp > 0; }
  bool dead() const { return !alive(); }
  Power* power(const std::string& id) {
    for (auto& p : powers) if (p->id == id) return p.get();
    return nullptr;
  }
  template <class P> P* get() { return static_cast<P*>(power(P::kId)); }
  template <class P> int powerAmount() { auto* p = get<P>(); return p ? p->amount : 0; }
  bool isSecondaryEnemy() const {
    if (side != Side::Enemy) return false;
    for (auto& p : powers) if (p->ownerIsSecondaryEnemy()) return true;
    return false;
  }
  bool isPrimaryEnemy() const { return side == Side::Enemy && !isSecondaryEnemy(); }
};

// ---------------------------------------------------------------- relics

struct Relic : Model {
  std::string id, locKey, icon;
  Combat* combat = nullptr;
  Run* run = nullptr;
  float flash = 0;
};

// ---------------------------------------------------------------- UI plumbing

// Things the renderer should animate; commands push them, the UI drains them.
struct VisualEvent {
  enum Kind { Damage, Blocked, Block, Heal, PowerUp, PowerDown, Death, CardExhaust, Shuffle, Banner, Anim } kind;
  Creature* who = nullptr;
  int amount = 0;
  std::string text;
};

struct PlayerAction {
  enum Kind { PlayCard, EndTurn } kind = EndTurn;
  Card* card = nullptr;
  Creature* target = nullptr;
};

// A request the logic makes and the UI answers (CardSelectCmd).
struct CardChoice {
  std::string prompt;
  std::vector<Card*> options;
  int minCount = 1, maxCount = 1;
  bool active = false;
  Signal<std::vector<Card*>> result;
};

// ---------------------------------------------------------------- combat

struct Encounter;

struct Combat {
  Run* run = nullptr;
  Creature* player = nullptr;
  std::vector<Creature*> enemies;
  std::vector<std::unique_ptr<Creature>> ownedEnemies;
  std::vector<std::unique_ptr<Card>> cardStore;
  std::vector<std::unique_ptr<Power>> graveyard;  // removed powers, freed with the combat
  std::vector<Creature*> stayingDead;             // being killed but not leaving (illusions)
  std::vector<Card*> draw, hand, discard, exhaust, play;

  int energy = 0, maxEnergy = 3;
  int turnNumber = 1, roundNumber = 1;
  int cardsPlayedThisTurn = 0;  // CombatHistory.CardPlaysStarted this turn (player)
  Side currentSide = Side::Player;
  bool inProgress = false, ending = false, over = false, won = false;
  bool playerPhase = false;  // UI may submit actions
  std::string encounterId;
  bool isBoss = false, isElite = false;

  Signal<PlayerAction> actions;
  CardChoice choice;
  std::vector<VisualEvent> events;
  std::string banner;
  float bannerTime = 0;

  // entry points
  Task<> runCombat();
  bool canPlay(Card* c, std::string* reason = nullptr);
  int energyCost(Card* c);  // CardEnergyCost.GetWithModifiers(All)
  int maxEnergyNow();
  bool isValidTarget(Card* c, Creature* t);
  std::vector<Creature*> hittableEnemies();
  std::vector<Creature*> aliveEnemies();

  // hooks (Hook.*)
  std::vector<Model*> listeners();
  Dec modifyDamage(Creature* target, Creature* dealer, Dec dmg, int props, Card* src);
  Dec modifyBlock(Creature* target, Dec block, int props, Card* src);

  // flow (CombatManager)
  Task<> startTurn();
  Task<> setupPlayerTurn();
  Task<> executeEnemyTurn();
  Task<> endEnemyTurn();
  Task<> endPlayerTurnPhaseOne();
  Task<> endPlayerTurnPhaseTwo();
  void switchSides();
  Task<bool> checkWinCondition();
  Task<> playCard(Card* c, Creature* target, bool autoPlay = false, bool forceExhaust = false);

  std::vector<Card*>& pile(Pile p);
  Pile pileOf(Card* c);
  void removeFromPiles(Card* c);
  std::vector<Card*> allCards();
  Card* addCard(std::unique_ptr<Card> c);
  // CombatState.CreateCreature + monster SetUpForCombat: unique HP, move machine.
  Creature* createEnemy(std::unique_ptr<Monster> m);
  void push(VisualEvent e) { events.push_back(std::move(e)); }
  Rng& rng(const char* stream);
};

// Commands. Each is a coroutine so it can pace itself for the renderer.
namespace cmd {
Task<std::vector<DamageResult>> damage(std::vector<Creature*> targets, Dec amount, int props, Creature* dealer, Card* src);
Task<std::vector<DamageResult>> damage(Creature* target, Dec amount, int props, Creature* dealer, Card* src);
Task<> kill(std::vector<Creature*> creatures);
Task<Dec> gainBlock(Creature* c, Dec amount, int props, Card* src, bool fast = false);
Task<> heal(Creature* c, Dec amount);
Task<> applyPower(std::unique_ptr<Power> p, Creature* target, Dec amount, Creature* applier, Card* src, bool silent = false);
Task<int> modifyPowerAmount(Power* p, Dec offset, Creature* applier, Card* src, bool silent = false);
Task<> removePower(Power* p);
Task<> tickDownDuration(Power* p);
Task<> decrement(Power* p);
Task<std::vector<Card*>> drawCards(Combat& c, Dec count, bool fromHandDraw = false);
Task<> shuffle(Combat& c);
Task<> moveCard(Combat& c, Card* card, Pile to, bool top = true);
Task<> exhaustCard(Combat& c, Card* card, bool causedByEthereal = false);
Task<> gainEnergy(Combat& c, int amount);
Task<> gainMaxHp(Creature* cr, int amount);
Task<> loseMaxHp(Creature* cr, int amount);
// CardPileCmd.AddGeneratedCardToCombat: returns the card now owned by the combat.
Task<Card*> addGeneratedCard(Combat& c, std::unique_ptr<Card> card, Pile to, bool top = false);
Task<> autoPlay(Combat& c, Card* card, Creature* target = nullptr);  // CardCmd.AutoPlay
Task<Card*> transform(Combat& c, Card* card, std::unique_ptr<Card> into);  // CardCmd.Transform
void upgradeCard(Card* card);  // CardCmd.Upgrade
Task<> addStatusCards(Combat& c, std::string cardId, Pile to, int count);
Task<std::vector<Card*>> selectCards(Combat& c, std::string prompt, std::vector<Card*> options, int minCount, int maxCount);
Task<> autoPlayFromDrawPile(Combat& c, int count, bool forceExhaust);
// CreatureCmd.Add: a monster joins mid-combat (summons, splits).
Task<Creature*> addMonster(Combat& c, std::unique_ptr<Monster> m);

// Attack builder (AttackCommand), covering the targeting modes in this build.
struct Attack {
  Dec damagePerHit = 0;
  Card* calcFrom = nullptr;  // CalculatedDamageVar source
  int hits = 1;
  Creature* attacker = nullptr;
  Card* source = nullptr;
  Creature* single = nullptr;
  bool allOpponents = false;
  bool random = false;
  int props = kMove;
  std::vector<std::vector<DamageResult>> results;
  Task<> execute(Combat& c);
};
}  // namespace cmd

template <class P> Task<> applyPower(Creature* target, Dec amount, Creature* applier, Card* src, bool silent = false);

// ---------------------------------------------------------------- run / map

enum class RoomType { Monster, Elite, Rest, Treasure, Unknown, Boss, Start };

struct MapNode {
  int col = 0, row = 0;
  RoomType type = RoomType::Monster;
  std::vector<int> next;  // indices into Run::nodes
  bool visited = false;
  float x = 0, y = 0;     // layout in map space
};

struct Encounter {
  std::string id;
  RoomType room = RoomType::Monster;
  bool weak = false;
  std::function<std::vector<std::unique_ptr<Monster>>(Rng&)> generate;
};

enum class Screen { Title, Map, Combat, Reward, Rest, RestUpgrade, GameOver, Victory, DeckView };

struct Run {
  uint64_t seed = 1;
  std::unique_ptr<Creature> player;
  std::vector<std::unique_ptr<Card>> deck;
  std::vector<std::unique_ptr<Relic>> relics;
  int gold = 99;
  int floor = 0;
  std::vector<MapNode> nodes;
  int currentNode = -1;
  std::unique_ptr<Combat> combat;
  std::vector<std::string> normalQueue, weakQueue;
  std::string bossId;
  std::map<std::string, std::unique_ptr<Rng>> rngs;
  float rarityOffset = -0.05f;  // CardRarityOdds.CurrentValue
  Rarity rollRarity(RoomType room);
  std::vector<std::unique_ptr<Card>> cardReward(RoomType room, int count);

  Screen screen = Screen::Title;
  // UI requests
  Signal<int> mapChoice;             // node index
  std::vector<std::unique_ptr<Card>> rewardCards;
  Signal<int> rewardChoice;          // index or -1 skip
  Signal<int> restChoice;            // 0 heal, 1 smith
  std::vector<Card*> upgradeOptions;
  Signal<int> upgradeChoice;         // index or -1 back
  int lastHeal = 0;

  Rng& rng(const char* stream) {
    auto& r = rngs[stream];
    if (!r) r = std::make_unique<Rng>(seed, stream);
    return *r;
  }
  void start(uint64_t seed);
  void generateMap();
  std::vector<int> reachableNodes() const;
  Task<> main();
  Task<bool> fight(const std::string& encounterId);
};

// ---------------------------------------------------------------- registry

namespace db {
void init();
std::unique_ptr<Card> card(const std::string& id);
std::unique_ptr<Power> power(const std::string& id);
const Encounter* encounter(const std::string& id);
std::unique_ptr<Relic> relic(const std::string& id);
std::vector<std::string> ironcladRewardPool();
std::vector<std::string> ironcladStarterDeck();
std::vector<std::string> act1Weak();
std::vector<std::string> act1Normal();
std::vector<std::string> act1Elites();
std::vector<std::string> act1Bosses();
void registerCard(const std::string& id, CardFactory f);
void registerPower(const std::string& id, PowerFactory f);
void registerEncounter(const std::string& id, RoomType room, bool weak, std::function<std::vector<std::unique_ptr<Monster>>(Rng&)> gen);
// Every card in IroncladCardPool, in the pool's order (registered or not).
const std::vector<std::string>& ironcladPool();
// Registered pool cards matching a filter, e.g. for "add a random Attack".
std::vector<std::string> ironcladCards(std::function<bool(const Card&)> filter);
}  // namespace db

template <class P> Task<> applyPower(Creature* target, Dec amount, Creature* applier, Card* src, bool silent) {
  // PowerCmd.Apply<T>: cmd::applyPower stacks onto an existing instance or adds this one
  // (after Hook.ModifyPowerAmountReceived either way).
  if (target->combat && target->combat->ending) co_return;
  co_await cmd::applyPower(std::make_unique<P>(), target, amount, applier, src, silent);
}

template <class P> Task<> Monster::applyToSelf(Dec amount) {
  co_await applyPower<P>(creature, amount, creature, nullptr);
}
template <class P> Task<> Monster::applyToTargets(std::vector<Creature*> targets, Dec amount) {
  for (auto* t : targets) co_await applyPower<P>(t, amount, creature, nullptr);
}

}  // namespace sts
