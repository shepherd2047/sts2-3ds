// Core game model: a single-player port of the StS2 combat rules.
//
// Naming follows the decompiled C# so each piece can be checked against its
// source: Hook.* -> Combat::hook*, CreatureCmd/PowerCmd/CardPileCmd -> cmd::*,
// CombatManager -> Combat::run and friends.
#pragma once
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "combat_history.h"
#include "dec.h"
#include "history.h"
#include "rng.h"
#include "task.h"

namespace sts {

struct Creature;
struct Power;
struct Card;
struct Monster;
struct Relic;
struct Orb;
struct Combat;
struct Run;
struct Modifier;  // M11 run modifiers (modifiers.h)
struct Affliction;  // A4 (afflictions.cpp)
namespace cmd { struct Attack; }  // AttackCommand (Model::beforeAttack / afterAttack)

// AscensionLevel (Entities.Ascension): a run at level N has every level <= N (Run::hasAscension).
enum AscensionLevel : int {
  kAscNone, kSwarmingElites, kWearyTraveler, kPoverty, kTightBelt, kAscendersBane,
  kInflation, kScarcity, kToughEnemies, kDeadlyEnemies, kDoubleBoss
};

enum class Side { Player, Enemy };
enum class CardType { Attack, Skill, Power, Status, Curse, Quest };  // Quest: E2 (quests.cpp)
enum class Rarity { Basic, Common, Uncommon, Rare, Ancient, Event, Token, Status, Curse, Quest };  // CardRarity order (no None)
enum class TargetType { None, Self, AnyEnemy, AllEnemies, RandomEnemy };
enum class PowerType { Buff, Debuff };
enum class StackType { Counter, Single };
// PowerInstanceType: Instanced powers add a new instance on every apply; InstancedPerApplier stacks onto
// the instance from the same applier (PowerCmd.FindExistingInstanceForStacking).
enum class PowerInstanceType { None, Instanced, InstancedPerApplier };
enum class Pile { None, Draw, Hand, Discard, Exhaust, Play };
// CardPilePosition (Random is not used by any result location): Top = index 0 of the pile list
// (the draw pile's top, the hand's left end), Bottom = appended.
enum class PilePosition { Bottom, Top };
// CardLocation (single player: no Player field): where a played card goes (Hook.ModifyCardPlayResultLocation).
struct CardLocation {
  Pile pile = Pile::Discard;
  PilePosition position = PilePosition::Bottom;
  bool operator==(const CardLocation& o) const { return pile == o.pile && position == o.position; }
  bool operator!=(const CardLocation& o) const { return !(*this == o); }
};
enum class RoomType { Monster, Elite, Rest, Treasure, Unknown, Boss, Start, Shop, Ancient };
inline int roomBit(RoomType t) { return 1 << (int)t; }  // a set of room types (modifyUnknownMapPointRoomTypes)
enum class RelicRarity { None, Starter, Common, Uncommon, Rare, Shop, Event, Ancient };
enum class PotionRarity { None, Common, Uncommon, Rare, Event, Token };
enum class PotionUsage { CombatOnly, AnyTime, Automatic };

// ValueProp flags.
enum : int { kUnblockable = 2, kUnpowered = 4, kMove = 8, kSkipHurtAnim = 16 };
inline bool isPoweredAttack(int p) { return (p & kMove) && !(p & kUnpowered); }
inline bool isPoweredBlock(int p) { return (p & kMove) && !(p & kUnpowered); }
// Model::modifyDamageCap's "no cap" (decimal.MaxValue).
inline constexpr Dec kNoDamageCap = Dec::fromRaw(INT64_MAX);

enum Keyword : int { kwExhaust = 1, kwUnplayable = 2, kwEthereal = 4, kwInnate = 8, kwRetain = 16, kwSly = 32, kwEternal = 64 };
enum CardTag : int { tagStrike = 1, tagDefend = 2, tagMinion = 4, tagOstyAttack = 8, tagShiv = 16, tagSovereignBlade = 32 };

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
  virtual Dec modifyHpLostBeforeOsty(Creature*, Dec amount, int, Creature*, Card*) { return amount; }
  virtual Dec modifyHpLostAfterOsty(Creature*, Dec amount, int, Creature*, Card*) { return amount; }
  virtual Dec modifyHpLostBeforeOstyLate(Creature*, Dec amount, int, Creature*, Card*) { return amount; }  // HardenedShellPower
  virtual Dec modifyHpLostAfterOstyLate(Creature*, Dec amount, int, Creature*, Card*) { return amount; }   // BufferPower, TheBoot
  // Hook.ModifyDamageCap (Intangible, Hard to Kill): the lowest cap applies, after the multipliers.
  virtual Dec modifyDamageCap(Creature* /*target*/, int /*props*/, Creature* /*dealer*/, Card*) { return kNoDamageCap; }
  // Hook.ModifyUnblockedDamageTarget: redirect unblocked damage to a different creature
  // (DieForYouPower redirects a powered hit meant for the player onto Osty).
  virtual Creature* modifyUnblockedDamageTarget(Creature* target, Dec /*unblocked*/, int /*props*/, Creature* /*dealer*/) { return target; }
  virtual Dec modifyHandDraw(Dec amount) { return amount; }

  virtual Task<> beforeCombatStart() { return {}; }
  virtual Task<> beforeCombatStartLate() { return {}; }
  virtual Task<> afterCombatVictoryEarly() { return {}; }
  virtual Task<> afterCombatVictory() { return {}; }
  virtual Task<> beforeSideTurnStart(Side, const std::vector<Creature*>&) { return {}; }
  virtual Task<> afterSideTurnStart(Side, const std::vector<Creature*>&) { return {}; }
  virtual Task<> afterSideTurnStartLate(Side, const std::vector<Creature*>&) { return {}; }
  virtual Task<> afterPlayerTurnStart() { return {}; }
  virtual Task<> afterPlayerTurnStartLate() { return {}; }
  virtual Task<> beforeSideTurnEnd(Side, const std::vector<Creature*>&) { return {}; }
  virtual Task<> afterSideTurnEnd(Side, const std::vector<Creature*>&) { return {}; }
  virtual Task<> afterSideTurnEndLate(Side, const std::vector<Creature*>&) { return {}; }
  // Hook.BeforeDamageReceived: after ModifyDamage, before block is taken (ThornsPower).
  virtual Task<> beforeDamageReceived(Creature*, Dec /*amount*/, int, Creature* /*dealer*/, Card*) { return {}; }
  virtual Task<> afterDamageReceived(Creature*, const DamageResult&, int, Creature*, Card*) { return {}; }
  // Hook.AfterBlockBroken: per damage result, before AfterCurrentHpChanged; still runs while combat is ending.
  virtual Task<> afterBlockBroken(Creature* /*target*/, Creature* /*breaker*/) { return {}; }
  virtual Task<> afterDeath(Creature*) { return {}; }
  virtual Task<> beforeCardPlayed(const CardPlay&) { return {}; }
  virtual Task<> afterCardPlayed(const CardPlay&) { return {}; }
  virtual Task<> afterPowerAmountChanged(Power*, Dec, Creature*, Card*) { return {}; }

  // Added for the full Ironclad pool (names follow Hook.*).
  virtual Task<> beforeSideTurnEndVeryEarly(Side, const std::vector<Creature*>&) { return {}; }  // AsleepPower
  virtual Task<> beforeSideTurnEndEarly(Side, const std::vector<Creature*>&) { return {}; }
  virtual Task<> afterAutoPostPlayPhaseEntered() { return {}; }  // player's turn is about to end
  virtual Task<> afterCardExhausted(Card*, bool /*causedByEthereal*/) { return {}; }
  virtual Task<> afterCardDrawnEarly(Card*, bool /*fromHandDraw*/) { return {}; }
  virtual Task<> afterCardDrawn(Card*, bool /*fromHandDraw*/) { return {}; }
  virtual Task<> afterBlockGained(Creature*, Dec /*amount*/, int /*props*/, Card*) { return {}; }
  virtual Task<> afterCardEnteredCombat(Card*) { return {}; }
  virtual Task<> afterModifyingCardPlayCount(Card*) { return {}; }
  virtual Task<> afterEnergySpent(Card*, int) { return {}; }
  virtual bool shouldDraw(bool /*fromHandDraw*/) { return true; }
  virtual bool shouldFlush() { return true; }  // Hook.ShouldFlush (RetainHandPower)
  virtual bool shouldClearBlock(Creature*) { return true; }
  virtual Dec modifyMaxEnergy(Dec amount) { return amount; }
  virtual int modifyCardPlayCount(Card*, Creature*, int count) { return count; }
  // Hook.ModifyCardPlayResultLocation. Override the CardLocation form to set a position too; the
  // older Pile-only form is called by its default (a changed pile gets Top for the draw pile, as every
  // C# model that sends a card there does, else Bottom).
  virtual Pile modifyCardPlayResultLocation(Card*, bool /*autoPlay*/, Pile pile) { return pile; }
  virtual CardLocation modifyCardPlayResult(Card* card, bool autoPlay, CardLocation loc) {
    Pile p = modifyCardPlayResultLocation(card, autoPlay, loc.pile);
    if (p != loc.pile) loc = {p, p == Pile::Draw ? PilePosition::Top : PilePosition::Bottom};
    return loc;
  }
  virtual Task<> afterModifyingCardPlayResultLocation(Card*, Pile) { return {}; }  // only the models that changed it
  // TryModifyEnergyCostInCombat / ...Late: return the new cost (or `cost` unchanged).
  virtual int modifyEnergyCost(Card*, int cost) { return cost; }
  virtual int modifyEnergyCostLate(Card*, int cost) { return cost; }
  // Added for act 1 monsters.
  virtual bool shouldStopCombatFromEnding() { return false; }
  // Hook.ShouldTakeExtraTurn / AfterTakingExtraTurn (AmbergrisPower, PaelsEye): another player turn instead of the enemies'.
  virtual bool shouldTakeExtraTurn() { return false; }
  virtual Task<> afterTakingExtraTurn() { return {}; }
  virtual Task<> afterCreatureAddedToCombat(Creature*) { return {}; }
  virtual bool shouldPlay(Card*) { return true; }  // Hook.ShouldPlay (RingingPower, ...)
  virtual bool shouldAfflict(Card*, Affliction*) { return true; }  // Hook.ShouldAfflict (A4)
  // Illusions stay in the room (dead) with their buffs and revive.
  virtual bool shouldCreatureBeRemovedFromCombatAfterDeath(Creature*) { return true; }
  virtual bool shouldAllowHitting(Creature*) { return true; }  // Hook.ShouldAllowHitting (Creature.CanReceivePowers)
  virtual bool shouldPowerBeRemovedOnDeath(Power*) { return true; }
  // Hook.ModifyPowerAmountReceived (ArtifactPower): return true and set `out` to change
  // the amount a creature is about to receive; the modifier then gets the After... call.
  virtual bool tryModifyPowerAmountReceived(Power* /*incoming*/, Creature* /*target*/, Dec /*amount*/,
                                            Creature* /*applier*/, Dec& /*out*/) { return false; }
  virtual Task<> afterModifyingPowerAmountReceived(Power*) { return {}; }
  // Hook.BeforePowerAmountChanged / ModifyPowerAmountGiven{Additive,Multiplicative} / AfterModifyingPowerAmountGiven
  // (PowerCmd.Apply, before ModifyPowerAmountReceived; only run when the applier is in the combat).
  virtual Task<> beforePowerAmountChanged(Power*, Dec /*amount*/, Creature* /*target*/, Creature* /*applier*/, Card*) { return {}; }
  virtual Dec modifyPowerAmountGivenAdditive(Power*, Creature* /*giver*/, Dec /*amount*/, Creature* /*target*/, Card*) { return Dec(0); }
  virtual Dec modifyPowerAmountGivenMultiplicative(Power*, Creature* /*giver*/, Dec /*amount*/, Creature* /*target*/, Card*) { return Dec(1); }
  virtual Task<> afterModifyingPowerAmountGiven(Power*) { return {}; }

  // Added for relics (Hook.* of the same names).
  virtual Task<> afterCombatEnd() { return {}; }              // victory, before afterCombatVictory
  virtual Task<> afterRoomEntered(RoomType) { return {}; }
  // E2 (quests.cpp): map / "?"-room / deck-card run hooks. Run::listeners() puts the deck cards
  // first (RunState.IterateHookListeners), so a deck card hears every run-level hook, also during
  // a combat, with combat == nullptr: card overrides of run hooks check Card::inDeck().
  virtual Task<> beforeRoomEntered(RoomType) { return {}; }  // Hook.BeforeRoomEntered (resolved type; Unknown = event)
  virtual int modifyUnknownMapPointRoomTypes(int types) { return types; }  // roomBit set; Unknown = Event
  virtual std::string modifyNextEvent(const std::string& eventId) { return eventId; }  // Hook.ModifyNextEvent
  virtual bool shouldAllowFreeTravel() { return false; }  // Hook.ShouldAllowFreeTravel (MapTravel)
  virtual void modifyGeneratedMap(int /*actIndex*/) {}      // Hook.ModifyGeneratedMap: may replace Run::nodes
  virtual void modifyGeneratedMapLate(int /*actIndex*/) {}  // also run on a loaded map (SavedActMap)
  virtual void afterMapGenerated(int /*actIndex*/) {}       // Hook.AfterMapGenerated (quest markers)
  virtual void beforeCardRemoved(Card*) {}                  // Hook.BeforeCardRemoved (CardPileCmd.RemoveFromDeck)
  virtual bool tryModifyRestSiteOptions(std::vector<int>& /*options*/) { return false; }  // Run::restOptions ids
  virtual Task<> afterBlockCleared(Creature*) { return {}; }
  virtual Task<> afterEnergyReset() { return {}; }
  virtual Task<> afterEnergyResetLate() { return {}; }
  virtual Task<> beforeHandDraw() { return {}; }
  virtual Task<> afterCurrentHpChanged(Creature*, Dec /*delta*/) { return {}; }
  virtual Dec modifyRestSiteHealAmount(Creature*, Dec amount) { return amount; }
  virtual Task<> afterRestSiteHeal() { return {}; }
  virtual Dec modifyGoldGained(Dec amount) { return amount; }
  virtual Task<> afterGoldGained(int) { return {}; }
  // Merchant (Hook.ModifyMerchantPrice / ShouldRefillMerchantEntry / AfterItemPurchased).
  virtual Dec modifyMerchantPrice(Dec price) { return price; }
  virtual bool shouldRefillMerchantEntry() { return false; }
  virtual Task<> afterItemPurchased(int /*goldSpent*/) { return {}; }
  // Added for package 10 relics.
  virtual Task<> afterPotionUsed() { return {}; }
  virtual Task<> afterPotionProcured() { return {}; }
  virtual Task<> afterPotionDiscarded() { return {}; }
  virtual int modifyXValue(Card*, int x) { return x; }
  virtual bool shouldResetEnergy() { return true; }  // Hook.ShouldPlayerResetEnergy
  virtual Task<> afterShuffle() { return {}; }
  virtual bool shouldForcePotionReward(RoomType) { return false; }
  virtual bool shouldProcurePotion() { return true; }  // Sozu
  // Added for enchantments (Hook.AfterAutoPrePlayPhaseEntered / BeforeFlush / ModifyShuffleOrder).
  virtual Task<> afterAutoPrePlayPhaseEnteredEarly() { return {}; }
  virtual Task<> afterAutoPrePlayPhaseEntered() { return {}; }  // player turn set up, before the play phase
  virtual Task<> afterAutoPrePlayPhaseEnteredLate() { return {}; }
  virtual Task<> beforeFlush() { return {}; }                   // player turn ends, before the hand is discarded
  virtual void modifyShuffleOrder(std::vector<Card*>& /*cards*/, bool /*isInitialShuffle*/) {}  // index 0 = top
  virtual Task<> afterCardDiscarded(Card*) { return {}; }  // Hook.AfterCardDiscarded (CardCmd.Discard only, not the end-of-turn flush)
  // Added for the Necrobinder (X4.0): DoomPower.DoomKill / OstyCmd.Summon.
  virtual Task<> afterDiedToDoom(const std::vector<Creature*>&) { return {}; }
  virtual Task<> afterOstyRevived(Creature*) { return {}; }
  // Hook.AfterHandEmptied (CombatManager.CheckForEmptyHand: after a card play or potion use outside
  // any other card / potion effect, and on entering AutoPrePlay; Combat::checkForEmptyHand).
  virtual Task<> afterHandEmptied() { return {}; }
  // Hook.ModifySummonAmount (OstyCmd.Summon, before anything happens; `source` is the summoning card /
  // relic / potion, or null) and Hook.AfterSummon (after the summon, with the modified amount).
  // (Single player: the summoner is always the player.)
  virtual Dec modifySummonAmount(Dec amount, Model* /*source*/) { return amount; }
  virtual Task<> afterSummon(Dec /*amount*/) { return {}; }

  // Added for the Regent (X3.0): Stars, the second resource, and Forge / Sovereign Blade.
  virtual int modifyStarCost(Card*, int cost) { return cost; }  // Hook.ModifyStarCost (TryModifyStarCost)
  virtual bool shouldGainStars(int /*amount*/) { return true; }  // Hook.ShouldGainStars
  virtual bool shouldPayExcessEnergyCostWithStars() { return false; }  // AbstractModel.ShouldPayExcessEnergyCostWithStars
  virtual Task<> afterStarsGained(int) { return {}; }  // Hook.AfterStarsGained (PlayerCmd.GainStars only)
  virtual Task<> afterStarsSpent(int) { return {}; }   // Hook.AfterStarsSpent (paying a card's star cost only)
  virtual Task<> afterForge(Dec /*amount*/, Model* /*source*/) { return {}; }  // Hook.AfterForge (ForgeCmd.Forge)

  // Added for the Defect's orbs (X2.0; Hook.* of the same names).
  virtual Dec modifyOrbValue(Orb*, Dec value) { return value; }  // FocusPower
  virtual int modifyOrbPassiveTriggerCount(Orb*, int count) { return count; }
  virtual Task<> afterModifyingOrbPassiveTriggerCount(Orb*) { return {}; }
  virtual Task<> afterOrbChanneled(Orb*) { return {}; }
  virtual Task<> afterOrbEvoked(Orb*, const std::vector<Creature*>& /*targets*/) { return {}; }

  // Added for the Necrobinder's relics (X4.1; Hook.* of the same names).
  // Hook.BeforeAttack / AfterAttack: once per AttackCommand.Execute (or AttackContext, cmd::beginAttackContext).
  virtual Task<> beforeAttack(cmd::Attack&) { return {}; }
  virtual Task<> afterAttack(const cmd::Attack&) { return {}; }
  // Added for the Necrobinder's uncommons (X4.3b): Hook.AfterDamageGiven (per damage result, before AfterDamageReceived)
  // and Hook.AfterCardPlayedLate (after every AfterCardPlayed of that play).
  virtual Task<> afterDamageGiven(Creature* /*dealer*/, const DamageResult&, int /*props*/, Creature* /*target*/, Card*) { return {}; }
  virtual Task<> afterCardPlayedLate(const CardPlay&) { return {}; }
  // Hook.AfterFlush: hand cards discarded / retained at the end of the player's turn.
  virtual Task<> afterFlush(const std::vector<Card*>& /*flushed*/, const std::vector<Card*>& /*retained*/) { return {}; }
};

// ---------------------------------------------------------------- saves

// A save file is a stream of whitespace-separated tokens; the same io() calls write it
// and read it back (reading = true). Empty strings are written as "~".
struct Archive {
  bool reading = false;
  bool ok = true;
  std::vector<std::string> toks;
  size_t pos = 0;
  std::string out;
  void io(std::string& s) {
    if (!reading) { out += s.empty() ? std::string("~") : s; out += ' '; return; }
    if (pos >= toks.size()) { ok = false; s.clear(); return; }
    s = toks[pos++];
    if (s == "~") s.clear();
  }
  void io(int& v) { std::string t = reading ? "" : std::to_string(v); io(t); if (reading) v = std::atoi(t.c_str()); }
  void io(bool& v) { int i = v; io(i); v = i != 0; }
  void io(uint64_t& v) { std::string t = reading ? "" : std::to_string(v); io(t); if (reading) v = std::strtoull(t.c_str(), nullptr, 10); }
  void io(int64_t& v) { std::string t = reading ? "" : std::to_string(v); io(t); if (reading) v = std::strtoll(t.c_str(), nullptr, 10); }
  void io(float& v) {  // bit exact
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    uint64_t b = bits;
    io(b);
    bits = (uint32_t)b;
    std::memcpy(&v, &bits, 4);
  }
  void io(Dec& d) { io(d.raw); }
  // S22: a count read from a garbled file can be anything; every element takes at least one
  // token, so more than the tokens left is corrupt (and must not allocate billions of elements).
  int count(int n) {
    if (!reading) return n;
    size_t left = toks.size() > pos ? toks.size() - pos : 0;
    if (n < 0 || (size_t)n > left) { ok = false; return 0; }
    return n;
  }
  template <class T> void io(std::vector<T>& v) {
    int n = (int)v.size();
    io(n);
    n = count(n);
    if (reading) v.assign((size_t)n, T{});
    for (auto& x : v) io(x);
  }
  void tag(const char* t) {  // a marker that must match when reading
    std::string s = t;
    io(s);
    if (reading && s != t) ok = false;
  }
};

// ---------------------------------------------------------------- characters

// CharacterModel (Models.Characters) + its card / relic pools and the potions of its
// <Character>4Epoch. The table is generated (characters.inc). Everything that used to assume
// the Ironclad reads the run's character instead: Run::characterId / Run::character().
// A character's own mechanics (Poison, orbs, stars, Osty) live in source/core/char_<name>.cpp.
struct Character {
  std::string id;    // class name: Ironclad, Silent, Defect, Regent, Necrobinder
  std::string key;   // upper case: loc keys (characters.IRONCLAD.*), Spine / sprite id, Neow lines
  int startingHp = 0, startingGold = 99;
  int maxEnergy = 3;     // CharacterModel.MaxEnergy
  int orbSlots = 0;      // BaseOrbSlotCount (Defect: 3)
  bool alwaysShowStars = false;  // ShouldAlwaysShowStarCounter (Regent)
  std::string energyColor;  // CardPool.EnergyColorName ("ironclad", ...)
  std::string cardFrame;    // CardPool.CardFrameMaterialPath ("card_frame_red", ...)
  std::vector<std::string> starterDeck, startingRelics;
  std::vector<std::string> cardPool;  // in the C# pool order (registered or not); reward RNG depends on it
  std::vector<std::string> relicPool;
  std::vector<std::string> potions;   // the character's potions; Run rolls them before the shared pool
  std::vector<std::string> multiplayerOnly;  // dropped in single player (CardFactory.FilterForPlayerCount)
};

// ---------------------------------------------------------------- powers

struct Power : Model {
  std::string id;      // class name, e.g. "StrengthPower"
  std::string locKey;  // e.g. "STRENGTH_POWER"
  Creature* owner = nullptr;
  Creature* applier = nullptr;
  Creature* target = nullptr;  // PowerModel.Target (per-player instanced powers: Thievery, Heist, ...)
  int amount = 0;
  int amountOnTurnStart = 0;
  bool skipNextDurationTick = false;
  float flash = 0;  // UI

  virtual PowerType type() const { return PowerType::Buff; }
  virtual StackType stackType() const { return StackType::Counter; }
  virtual bool allowNegative() const { return false; }
  virtual PowerInstanceType instanceType() const { return PowerInstanceType::None; }
  // ITemporaryPower.InternallyAppliedPower: the id of the power a temporary power applies under the
  // hood (TemporaryStrength/Dexterity/FocusPower subclasses); null = not an ITemporaryPower.
  virtual const char* internallyAppliedPower() const { return nullptr; }
  bool isTemporary() const { return internallyAppliedPower() != nullptr; }  // `power is ITemporaryPower`
  virtual int displayAmount() const { return amount; }  // PowerModel.DisplayAmount (UI)
  virtual bool ownerIsSecondaryEnemy() const { return false; }  // MinionPower
  virtual bool removedAfterOwnerDeath() const { return true; }  // ShouldPowerBeRemovedAfterOwnerDeath
  // ShouldOwnerDeathTriggerFatal: false when the owner can come back (Decimillipede segments),
  // so "on kill" effects (Feed, ...) don't fire. See Creature::deathIsFatal.
  virtual bool shouldOwnerDeathTriggerFatal() const { return true; }
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

// ---------------------------------------------------------------- orbs

// OrbModel (Models.Orbs): a Defect orb living in Combat::orbQueue. Orbs are combat-only and,
// since this port is single-player, always owned by the player (the C#'s multiplayer
// "other players" branches, e.g. HibernatePower sharing Frost's block, are dropped).
// Subclasses (LightningOrb, FrostOrb, DarkOrb, PlasmaOrb, GlassOrb) are in char_defect.h.
struct Orb : Model {
  std::string id;      // class name, e.g. "LightningOrb"
  std::string locKey;  // e.g. "LIGHTNING_ORB"
  Creature* owner = nullptr;
  bool removedFromQueue = false;  // OrbModel.HasBeenRemovedFromState

  virtual Dec passiveVal() = 0;
  virtual Dec evokeVal() = 0;
  // Exactly one of these calls triggerPassive() for a given orb type (Lightning/Frost/Dark/Glass
  // at the end of the player's turn, Plasma at the start).
  virtual Task<> beforeTurnEndOrbTrigger() { return {}; }
  virtual Task<> afterTurnStartOrbTrigger() { return {}; }
  // OrbModel.Passive: the raw effect, no ModifyOrbPassiveTriggerCount. Normally reached only
  // through triggerPassive(); `target` is unused by every orb in this pool (always null in the
  // C#'s own turn-trigger calls) but kept for OrbCmd::orbPassive callers.
  virtual Task<> passive(Creature* target) { return {}; }
  virtual Task<std::vector<Creature*>> evoke() = 0;

  Task<> triggerPassive(Creature* target);  // OrbModel.TriggerPassive (defined in combat.cpp: needs Combat)
};
using OrbFactory = std::unique_ptr<Orb> (*)();

#define ORB_HEADER(Name, Key) static constexpr const char* kId = #Name; Name() { id = #Name; locKey = Key; }

// ---------------------------------------------------------------- cards

struct DynVar {
  std::string name;
  Dec base;       // current (after upgrades)
  Dec canonical;  // before upgrades, for diff() colouring
};

// ---------------------------------------------------------------- enchantments

enum class EnchantStatus { Normal, Disabled };

// EnchantmentModel: a permanent modifier attached to one card (Sharp, Vigorous, ...). It is
// a Model, so while its card is in combat it hears the combat hooks like the card does.
// `amount` is EnchantmentModel.Amount; the block/damage/play-count hooks run for the
// enchanted card only, before every other listener (Hook.ModifyDamage / ModifyBlock).
struct Enchantment : Model {
  std::string id;      // class name, e.g. "Sharp"
  std::string locKey;  // e.g. "SHARP" (enchantments.<key>.title / description / extraCardText)
  Card* card = nullptr;
  int amount = 0;
  EnchantStatus status = EnchantStatus::Normal;
  std::vector<DynVar> vars;  // CanonicalVars, for the description / extra card text

  virtual bool showAmount() const { return false; }
  virtual int displayAmount() const { return amount; }
  virtual bool hasExtraCardText() const { return false; }  // shown purple under the card text
  virtual bool isStackable() const { return false; }
  virtual bool shouldStartAtBottomOfDrawPile() const { return false; }
  virtual bool shouldGlowGold() const { return false; }
  virtual bool shouldGlowRed() const { return false; }
  virtual bool canEnchantCardType(CardType) const { return true; }
  // CanEnchant: subclasses call Enchantment::canEnchant first and only add restrictions.
  virtual bool canEnchant(const Card& c) const;

  virtual Task<> onPlay(CardPlay&) { return {}; }  // OnPlay, after the card's own effect, per play
  virtual void onEnchant() {}                      // OnEnchant: change keywords, cost, ...
  virtual void recalculateValues() {}              // RecalculateValues: refresh vars from `amount`
  virtual Dec enchantBlockAdditive(Dec) { return 0; }
  virtual Dec enchantBlockMultiplicative(Dec) { return 1; }
  virtual Dec enchantDamageAdditive(Dec, int /*props*/) { return 0; }
  virtual Dec enchantDamageMultiplicative(Dec, int /*props*/) { return 1; }
  virtual int enchantPlayCount(int n) { return n; }
  // State that lasts between rooms beyond amount/status/vars (saves; both directions).
  virtual void persist(Archive&) {}
  virtual std::unique_ptr<Enchantment> clone() const = 0;

  void modifyCard() { onEnchant(); recalculateValues(); }  // EnchantmentModel.ModifyCard
  bool disabled() const { return status == EnchantStatus::Disabled; }
  DynVar* var(const char* n) { for (auto& v : vars) if (v.name == n) return &v; return nullptr; }
  Dec val(const char* n) { auto* v = var(n); return v ? v->base : Dec(0); }
  void addVar(const char* n, Dec v) { vars.push_back({n, v, v}); }
};

template <class Derived> struct EnchantmentT : Enchantment {
  std::unique_ptr<Enchantment> clone() const override { return std::make_unique<Derived>(static_cast<const Derived&>(*this)); }
};

// ENCHANTMENT_HEADER(Sharp, "SHARP") { ...vars... }
#define ENCHANTMENT_HEADER(Name, Key)  \
  static constexpr const char* kId = #Name; \
  Name() { id = #Name; locKey = Key;
using EnchantmentFactory = std::unique_ptr<Enchantment> (*)();

// Card::enchantment: copying a card deep-copies its enchantment (the back pointer is
// fixed by CardT::clone).
struct EnchantSlot {
  std::unique_ptr<Enchantment> p;
  EnchantSlot() = default;
  EnchantSlot(const EnchantSlot& o) : p(o.p ? o.p->clone() : nullptr) {}
  EnchantSlot& operator=(const EnchantSlot& o) { p = o.p ? o.p->clone() : nullptr; return *this; }
  Enchantment* get() const { return p.get(); }
  Enchantment* operator->() const { return p.get(); }
  explicit operator bool() const { return p != nullptr; }
};

// ---------------------------------------------------------------- afflictions

// AfflictionModel (A4): the combat-scoped counterpart of an enchantment (Bound, Entangled,
// Galvanized, Hexed, Ringing, Smog, Tainted). Monsters' powers put them on the player's combat
// cards (cmd::afflict) and read them back (most of the logic lives in those powers, as in the
// C#); like an enchantment it hears the combat hooks while its card is in combat. Afflictions
// only exist on combat cards, which die with the combat, so they are never saved (the C#'s
// SerializableCard has no affliction either).
struct Affliction : Model {
  std::string id;      // class name, e.g. "Hexed"
  std::string locKey;  // e.g. "HEXED" (afflictions.<key>.title / description / extraCardText)
  Card* card = nullptr;
  int amount = 0;

  virtual bool hasExtraCardText() const { return false; }  // shown purple under the card text
  virtual bool isStackable() const { return false; }
  virtual bool canAfflictUnplayableCards() const { return true; }
  virtual bool canAfflictCardType(CardType) const { return true; }
  // CanAfflict: subclasses call Affliction::canAfflict first and only add restrictions.
  virtual bool canAfflict(const Card& c) const;
  virtual void afterApplied() {}   // AfterApplied (also after a downgrade)
  virtual void beforeRemoved() {}  // BeforeRemoved (ClearInternal)
  virtual Task<> onPlay(CardPlay&) { return {}; }  // OnPlay, after the enchantment's, per play
  // Keywords the affliction's power adds while the card is afflicted (HexPower's
  // TryModifyKeywordsInCombat); Card::has() reads them.
  virtual int addedKeywords() const { return 0; }
  virtual std::unique_ptr<Affliction> clone() const = 0;
};

template <class Derived> struct AfflictionT : Affliction {
  std::unique_ptr<Affliction> clone() const override { return std::make_unique<Derived>(static_cast<const Derived&>(*this)); }
};

// AFFLICTION_HEADER(Hexed, "HEXED") { ... }
#define AFFLICTION_HEADER(Name, Key)  \
  static constexpr const char* kId = #Name; \
  Name() { id = #Name; locKey = Key;
using AfflictionFactory = std::unique_ptr<Affliction> (*)();

// Card::affliction: copying a card deep-copies its affliction (CardModel.DeepCloneFields);
// the back pointer is fixed by Card::adoptEnchantment.
struct AfflictSlot {
  std::unique_ptr<Affliction> p;
  AfflictSlot() = default;
  AfflictSlot(const AfflictSlot& o) : p(o.p ? o.p->clone() : nullptr) {}
  AfflictSlot& operator=(const AfflictSlot& o) { p = o.p ? o.p->clone() : nullptr; return *this; }
  Affliction* get() const { return p.get(); }
  Affliction* operator->() const { return p.get(); }
  explicit operator bool() const { return p != nullptr; }
};

// CardModel.DeckVersion: the run's deck card a combat card was made from. A copy of a card
// never inherits it (CardModel.DeepCloneFields clears it).
struct DeckLink {
  Card* p = nullptr;
  DeckLink() = default;
  DeckLink(const DeckLink&) {}
  DeckLink& operator=(const DeckLink&) { return *this; }
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
  int baseReplayCount = 0;  // BaseReplayCount (Soldier's Stew): extra plays
  int maxUpgradeLevel = 1;
  bool isDupe = false;
  // CardModel.CloneOf: the card createClone() copied (combat clones only; clone() copies it as is,
  // like MemberwiseClone). Non-owning: combat cards live in Combat::cardStore until the fight ends.
  Card* cloneOf = nullptr;
  bool isClone() const { return cloneOf != nullptr; }
  // CardModel.CreateClone: clone() + CloneOf = this (ExhaustOnNextPlay is not a port field).
  std::unique_ptr<Card> createClone() const {
    auto c = clone();
    c->cloneOf = const_cast<Card*>(this);
    return c;
  }
  bool costsX = false;   // HasEnergyCostX
  int xValue = 0;        // captured X when played (ResolveEnergyXValue)
  int starCost = -1;         // CanonicalStarCost (Regent's Stars resource); -1 = no star cost
  bool costsStarsX = false;  // HasStarCostX
  int lastStarsSpent = 0;    // LastStarsSpent, captured when played
  int starXValue = 0;        // captured stars spent, with modifyXValue applied (ResolveStarXValue)
  // CardEnergyCost local modifiers.
  enum CostExpiry : int { kEndOfTurn = 1, kWhenPlayed = 2, kEndOfCombat = 4 };
  struct CostMod { int value; bool absolute; int expiry; bool reduceOnly; };
  std::vector<CostMod> costMods;
  std::vector<DynVar> vars;
  Combat* combat = nullptr;
  EnchantSlot enchantment;  // CardModel.Enchantment (null = none)
  AfflictSlot affliction;   // CardModel.Affliction (null = none; combat cards only)
  DeckLink deckVersion;     // CardModel.DeckVersion (combat cards only)
  // Owner.RunState for run-level hooks: set on the deck cards by Run::listeners() (a combat copy
  // keeps its deck card's value). inDeck() = CardModel.Pile.Type == PileType.Deck (quests.cpp).
  Run* run = nullptr;
  bool inDeck() const;

  // Hand-view calculation for CalculatedDamageVar (Body Slam, Perfected Strike).
  std::function<int(Card*)> calcMultiplier;
  // Target-aware multiplier (CalculatedVar.WithMultiplier((card, target) => ...)); target may be null
  // (no target hovered: the C# counts 0). Preferred over calcMultiplier when set.
  std::function<int(Card*, Creature*)> calcMultiplierT;
  Creature* previewTarget = nullptr;  // UI only: the creature a hand card is being aimed at (hand preview numbers)

  virtual ~Card() = default;
  virtual Task<> onPlay(CardPlay&) { return {}; }
  virtual void onUpgrade() {}
  virtual void afterDowngraded() {}  // CardModel.AfterDowngraded: restore state kept outside the vars
  virtual bool canBeGeneratedInCombat() const { return true; }  // CardModel.CanBeGeneratedInCombat (colorless pool helpers)
  // After a save is read back (vars, cost, keywords restored): rebuild state kept outside the vars.
  virtual void afterLoad() {}
  // HasTurnEndInHandEffect / OnTurnEndInHand (Burn, Infection, ...)
  virtual bool hasTurnEndInHandEffect() const { return false; }
  virtual Task<> onTurnEndInHand() { return {}; }
  // A quest card's reward when its map quest is completed (SpoilsMap.OnQuestComplete): the gold given.
  virtual Task<int> onQuestComplete() { co_return 0; }
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
  void downgrade();  // CardModel.DowngradeInternal (afflictions.cpp)
  // CardModel.Keywords: the card's own keywords plus those its affliction's power adds (Hexed).
  bool has(int kw) const { return (keywords & kw) != 0 || (affliction && (affliction->addedKeywords() & kw) != 0); }
  bool afflictedWith(const char* afflictionId) const { return affliction && affliction->id == afflictionId; }
  void addKeyword(int kw) { keywords |= kw; }      // CardModel.AddKeyword
  void removeKeyword(int kw) { keywords &= ~kw; }  // CardCmd.RemoveKeyword
  // A copied card owns a copy of the enchantment (and affliction) that still points at the
  // original card: every clone() (CardT, IroncladT, ...) must call this on the new card.
  void adoptEnchantment() {
    if (enchantment) enchantment->card = this;
    if (affliction) affliction->card = this;
  }
  // CardModel.GetEnchantedReplayCount: extra plays from BaseReplayCount and the enchantment.
  // CardModel.HasSingleTurnRetain / HasSingleTurnSly: set by effects, cleared at the end of the turn.
  bool singleTurnRetain = false, singleTurnSly = false;
  // CardPileCmd.AddGeneratedCardToCombat's `creator`: false for cards a monster adds (creator null);
  // AfterCardGeneratedForCombat listeners that check `creator == Owner` read it (Regalite).
  bool createdByPlayer = true;
  // CardModel.IsRemovable / IsTransformable: Eternal cards cannot leave the deck (removal, transform).
  bool isRemovable() const { return !has(kwEternal); }
  bool isTransformable() const { return isRemovable(); }
  bool shouldRetainThisTurn() const { return has(kwRetain) || singleTurnRetain; }
  bool isSlyThisTurn() const { return has(kwSly) || singleTurnSly; }
  int enchantedReplayCount() const { return enchantment ? enchantment->enchantPlayCount(baseReplayCount) : baseReplayCount; }
  // CardModel.GainsBlock (false in the C# base): true for the 78 cards whose C# override is `=> true`
  // (an id list in combat.cpp); a conditional one (SovereignBlade: Parry) overrides this.
  virtual bool gainsBlock() const;

  DynVar* var(const char* n) { for (auto& v : vars) if (v.name == n) return &v; return nullptr; }
  Dec val(const char* n) { auto* v = var(n); return v ? v->base : Dec(0); }
  void upgradeVar(const char* n, Dec by) { if (auto* v = var(n)) v->base += by; }
  void addVar(const char* n, Dec v) { vars.push_back({n, v, v}); }

  Dec calculatedDamage(Creature* target = nullptr);  // CalculatedDamageVar: CalculationBase + ExtraDamage * multiplier
  Dec calculatedBlock(Creature* target = nullptr);   // CalculatedBlockVar: CalculationBase + CalculationExtra * multiplier
  int calcMult(Creature* target) { return calcMultiplierT ? calcMultiplierT(this, target ? target : previewTarget) : calcMultiplier ? calcMultiplier(this) : 0; }
};

template <class Derived> struct CardT : Card {
  std::unique_ptr<Card> clone() const override {
    auto c = std::make_unique<Derived>(static_cast<const Derived&>(*this));
    c->adoptEnchantment();
    return c;
  }
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
  // State ids in the order they were added (the C#'s States dictionary order; M10's bestiary).
  std::vector<std::string> order;

  template <class S> S* add(const std::string& id) {
    auto s = std::make_unique<S>();
    s->id = id;
    S* raw = s.get();
    if (!states.count(id)) order.push_back(id);
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
  std::vector<std::pair<int, int>> attackLog;  // (damage, hits) of Monster::attack calls in the current move (STS_ASC_CHECK)

  virtual int minHp() const = 0;
  virtual int maxHp() const = 0;
  virtual void buildMoves() = 0;  // GenerateMoveStateMachine
  virtual Task<> afterAddedToRoom() { return {}; }

  void rollMove(Rng& rng) { nextMove = machine.rollMove(*this, rng); }
  Task<> performMove();
  // AscensionHelper.GetValueIfAscension for monsters: `ascValue` from that ascension level on, else `base`.
  // (The value read while a monster is built or acts comes from the run the combat belongs to.)
  int asc(AscensionLevel level, int ascValue, int base) const;
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

// HpDisplay (Entities.Creatures): how the HP bar shows; IsInfinite = either Infinite* value.
enum class HpDisplay { Normal, InfiniteWithNumbers, InfiniteWithoutNumbers };

struct Creature {
  std::string name;  // display
  int hp = 0, maxHp = 0, block = 0;
  HpDisplay hpDisplay = HpDisplay::Normal;  // Creature.HpDisplay (Hardened Shell, Waterfall Giant)
  bool hpInfinite() const { return hpDisplay != HpDisplay::Normal; }  // HpDisplay.IsInfinite()
  Side side = Side::Enemy;
  std::vector<std::unique_ptr<Power>> powers;
  std::unique_ptr<Monster> monster;
  bool isPlayer = false;
  Combat* combat = nullptr;
  bool removed = false;  // gone from the room after dying
  // Creature.PetOwner: the player this creature is a pet of (Osty). Block for a pet's own
  // damage is taken from its owner (CreatureCmd.Damage); see DieForYouPower (char_necrobinder.h).
  Creature* petOwner = nullptr;

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
  // GetPower / GetPower<T>: the first instance (instanced powers can have several, see powerInstances).
  template <class P> P* get() { return static_cast<P*>(power(P::kId)); }
  // GetPowerInstances(id) / GetPowerInstances<T>: every instance with that id, in application order.
  std::vector<Power*> powerInstances(const std::string& id) {
    std::vector<Power*> v;
    for (auto& p : powers) if (p->id == id) v.push_back(p.get());
    return v;
  }
  template <class P> std::vector<P*> instances() {
    std::vector<P*> v;
    for (auto& p : powers) if (p->id == P::kId) v.push_back(static_cast<P*>(p.get()));
    return v;
  }
  // PowerCmd.FindExistingInstanceForStacking: the instance an application of `p` from `applier` stacks onto.
  Power* stackingInstance(const Power& p, Creature* applier) {
    switch (p.instanceType()) {
      case PowerInstanceType::Instanced: return nullptr;
      case PowerInstanceType::InstancedPerApplier:
        for (auto& q : powers) if (q->id == p.id && q->applier == applier) return q.get();
        return nullptr;
      default: return power(p.id);
    }
  }
  template <class P> int powerAmount() { auto* p = get<P>(); return p ? p->amount : 0; }
  // `Powers.All(p => p.ShouldOwnerDeathTriggerFatal())`, checked before the killing blow.
  bool deathIsFatal() const {
    for (auto& p : powers) if (!p->shouldOwnerDeathTriggerFatal()) return false;
    return true;
  }
  bool isSecondaryEnemy() const {
    if (side != Side::Enemy) return false;
    for (auto& p : powers) if (p->ownerIsSecondaryEnemy()) return true;
    return false;
  }
  bool isPrimaryEnemy() const { return side == Side::Enemy && !isSecondaryEnemy(); }
};

// ---------------------------------------------------------------- card creation options (E8)

// CardCreationOptions (Runs/CardCreationOptions.cs): the pools, odds and flags CardFactory.CreateForReward
// (Run::createForReward, card_rewards.cpp) draws with, and which hooks may change them.
enum class CardSource { None, Encounter, Shop, Other };                                   // CardCreationSource
enum class RarityOdds { RegularEncounter, EliteEncounter, BossEncounter, Shop, Uniform };  // CardRarityOddsType
enum CardCreationFlag : uint32_t {                                                         // CardCreationFlags
  ccNoRarityModification = 1, ccNoUpgradeRoll = 2, ccNoHookUpgrades = 4, ccNoModifyHooks = 8,
  ccNoCardPoolModifications = 0x10, ccNoCardModelModifications = 0x20, ccForceRarityOddsChange = 0x40,
  ccIsCardReward = 0x80, ccIsFromCombat = 0x100
};
struct CardCreationOptions {
  static constexpr const char* kColorless = "Colorless";  // ColorlessCardPool in `pools`
  std::vector<std::string> pools;           // CardPools: character ids (their CardPool) or kColorless
  std::function<bool(const Card&)> filter;  // CardPoolFilter
  CardSource source = CardSource::Other;
  RarityOdds odds = RarityOdds::RegularEncounter;
  uint32_t flags = 0;
  Rng* rng = nullptr;                       // RngOverride (null: the Rewards stream)
  RoomType room = RoomType::Unknown;        // port-only: the room offering it (LavaLamp's CurrentRoom check)
  bool has(uint32_t f) const { return (flags & f) == f; }
  CardCreationOptions& with(uint32_t f) { flags |= f; return *this; }
  // CardCreationOptions.ForRoom (combat / shop), ForNonCombatWithDefaultOdds / ...UniformOdds.
  static CardCreationOptions forRoom(const std::string& characterId, RoomType room);
  static CardCreationOptions forNonCombat(std::vector<std::string> pools, bool uniform,
                                          std::function<bool(const Card&)> filter = nullptr);
};

// ---------------------------------------------------------------- relics

struct Relic : Model {
  std::string id, locKey, icon;
  RelicRarity rarity = RelicRarity::Common;
  Combat* combat = nullptr;
  Run* run = nullptr;
  float flash = 0;
  bool usedUp = false;           // IsUsedUp: greyed out, does nothing more
  std::vector<DynVar> vars;      // CanonicalVars, for the description

  virtual bool showCounter() const { return false; }
  virtual int displayAmount() const { return 0; }
  virtual bool allowedInShops() const { return true; }
  // Hook.ModifyCardRewardCreationOptions (DingyRug, PrismaticGem), per card of CardFactory.CreateForReward.
  virtual void modifyCardRewardCreationOptions(CardCreationOptions&) {}
  // TryModifyRewardsLate setting CardReward.CanReroll (Driftwood), for every RewardsSet.
  virtual bool makesCardRewardsRerollable() { return false; }
  // TryModifyCardRewardAlternatives (PaelsWing): the CardRewardAlternative OptionId it adds (an
  // EndSelectionAndCompleteReward option), and its OnSelect.
  virtual const char* cardRewardAlternative() { return nullptr; }
  virtual Task<> onCardRewardAlternative() { return {}; }
  // TryModifyRestSiteHealRewards (DreamCatcher): adds rows through Run::addRestSiteHealReward.
  virtual void modifyRestSiteHealRewards() {}
  // RelicModel.IsAllowed: pruned from the grab bags on every pull (Run::removeDisallowedRelics).
  // The IsBeforeAct3TreasureChest relics are handled by an id list in run.cpp.
  virtual bool isAllowed(Run&) { return true; }
  virtual Task<> afterObtained() { return {}; }  // AfterObtained (pickup effects)
  virtual void persist(Archive&) {}  // state that lasts between rooms (saves)
  virtual int bonusRelicRewards(RoomType) { return 0; }  // TryModifyRewards: extra RelicRewards
  // TryModifyRewards for the other reward kinds (Amethyst Aubergine, Prayer Wheel, White Star):
  virtual int extraCombatGold(RoomType) { return 0; }
  virtual std::vector<RoomType> extraCardRewards(RoomType) { return {}; }  // odds of each extra reward
  // TryModifyCardRewardOptions (late = ...Late): Lasting Candy, Lava Lamp, the eggs.
  virtual void modifyCardReward(std::vector<std::unique_ptr<Card>>&, const CardCreationOptions&, bool /*late*/) {}
  virtual bool shouldGenerateTreasure() { return true; }  // Hook.ShouldGenerateTreasure (SilverCrucible)
  // TryModifyCardBeingAddedToDeck / ModifyMerchantCardCreationResults (the eggs).
  virtual bool upgradesNewCard(const Card&) { return false; }
  virtual void afterCardAddedToDeck(Card*) {}  // AfterCardChangedPiles(Deck) for new cards
  virtual Task<> afterUnknownRoomEntered() { return {}; }  // Planisphere
  virtual void restSiteAction(int /*option*/) {}  // Girya's Lift

  Creature* owner() const;  // the player
  void doFlash() { flash = 1.f; }
  DynVar* var(const char* n) { for (auto& v : vars) if (v.name == n) return &v; return nullptr; }
  Dec val(const char* n) { auto* v = var(n); return v ? v->base : Dec(0); }
  void addVar(const char* n, Dec v) { vars.push_back({n, v, v}); }
};

// Relic class boilerplate: RELIC_HEADER(Anchor, "ANCHOR", Common) { ...vars... }
#define RELIC_HEADER(Name, Key, Rar)  \
  static constexpr const char* kId = #Name; \
  Name() { id = #Name; locKey = Key; icon = Key; rarity = RelicRarity::Rar;
using RelicFactoryFn = std::unique_ptr<Relic> (*)();

// ---------------------------------------------------------------- potions

// PotionModel. `target` uses the card TargetType: Self for the game's AnyPlayer/Self
// (single player), AnyEnemy, AllEnemies or None (TargetedNoCreature).
struct Potion : Model {
  std::string id, locKey;
  PotionRarity rarity = PotionRarity::Common;
  PotionUsage usage = PotionUsage::CombatOnly;
  TargetType target = TargetType::Self;
  Run* run = nullptr;
  Combat* combat = nullptr;  // set while it is being used in combat
  std::vector<DynVar> vars;

  virtual bool canBeGeneratedInCombat() const { return true; }
  virtual Task<> onUse(Creature* target) = 0;  // OnUse
  virtual bool passesCustomUsabilityCheck() const { return true; }  // PassesCustomUsabilityCheck (out of combat)

  Creature* owner() const;
  DynVar* var(const char* n) { for (auto& v : vars) if (v.name == n) return &v; return nullptr; }
  Dec val(const char* n) { auto* v = var(n); return v ? v->base : Dec(0); }
  void addVar(const char* n, Dec v) { vars.push_back({n, v, v}); }
};

// POTION_HEADER(FirePotion, "FIRE_POTION", Common, CombatOnly, AnyEnemy) { ...vars... }
#define POTION_HEADER(Name, Key, Rar, Use, Tgt)   static constexpr const char* kId = #Name;      Name() { id = #Name; locKey = Key; rarity = PotionRarity::Rar; usage = PotionUsage::Use; target = TargetType::Tgt;
using PotionFactoryFn = std::unique_ptr<Potion> (*)();

// ---------------------------------------------------------------- UI plumbing

// Things the renderer should animate; commands push them, the UI drains them.
struct VisualEvent {
  enum Kind { Damage, Blocked, Block, Heal, PowerUp, PowerDown, Death, CardExhaust, Shuffle, Banner, Anim,
              Hit, CardPlayed, BlockBroken } kind;  // Hit/CardPlayed/BlockBroken: only the audio router reads them
  Creature* who = nullptr;
  int amount = 0;
  std::string text;
};

struct PlayerAction {
  enum Kind { PlayCard, EndTurn, DevKillAll, UsePotion } kind = EndTurn;  // DevKillAll: developer menu
  Card* card = nullptr;
  int potionSlot = -1;  // UsePotion
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
  // The Necrobinder's Osty (X4.0, char_necrobinder.h): a single player-side creature, not in
  // `enemies`. Non-null once summoned this combat, even while dead (ready for revival).
  Creature* osty = nullptr;
  std::unique_ptr<Creature> ownedOsty;
  // PlayerCombatState.Pets other than Osty (Byrdpip, PaelsLegion: pets.cpp): player-side creatures a
  // relic adds each combat (PlayerCmd.AddPet in BeforeCombatStart), never in `enemies`, never hit
  // by monster attacks (AttackCommand targets PlayerCreatures). Creature::name is the MonsterModel id.
  // They live and die with the combat; nothing is kept between rooms.
  std::vector<Creature*> pets;
  std::vector<std::unique_ptr<Creature>> ownedPets;
  Creature* pet(const std::string& monsterId) const {  // PlayerCombatState.GetPet<T>
    for (Creature* p : pets) if (p->name == monsterId) return p;
    return nullptr;
  }
  std::vector<std::unique_ptr<Card>> cardStore;
  std::vector<std::unique_ptr<Power>> graveyard;  // removed powers, freed with the combat
  std::vector<std::unique_ptr<Enchantment>> enchantGraveyard;  // cleared enchantments (listener snapshots may still hold them)
  std::vector<std::unique_ptr<Affliction>> afflictGraveyard;   // cleared afflictions (same reason)
  CombatHistory history;  // CombatManager.History (combat_history.h)
  // CardAfflicted entries this turn (ChainsOfBindingPower's per-turn count).
  int afflictionsThisTurn(const char* afflictionId) const {
    return history.countThisTurn(*this, CombatHistoryEntry::CardAfflicted, [&](const CombatHistoryEntry& e) { return e.id == afflictionId; });
  }
  int skillPlaysStartedThisTurn = 0;  // CombatHistory.CardPlaysStarted of Skills this turn (SmoggyPower)
  bool debugAfflictDone = false;      // STS_AFFLICT / SIM_AFFLICT applied (combat.cpp)
  std::vector<Creature*> stayingDead;             // being killed but not leaving (illusions)
  std::vector<Card*> draw, hand, discard, exhaust, play;
  int cardsDrawnThisCombat = 0;  // CombatHistory CardDrawnEntry count (Murder, X1.4)
  int discardsThisTurn() const { return history.countThisTurn(*this, CombatHistoryEntry::CardDiscarded); }

  // EncounterModel.SpawnedEnemies (distinct monster ids, set by createEnemy), CombatState.EscapedCreatures
  // (monster ids, pushed by each escapeCreature) and GremlinMercNormal.GoldWasStolen.
  std::vector<std::string> spawnedEnemyIds, escapedEnemyIds;
  bool goldWasStolen = false;
  float goldProportion() const;  // EncounterModel.CalculateGoldProportion (combat.cpp); Run::combatRewards scales monster-room gold
  int extraRewardGold = 0;  // CombatRoom.AddExtraReward(GoldReward) (RoyaltiesPower); Run::combatRewards adds the row
  int energy = 0, maxEnergy = 3;
  int stars = 0;  // PlayerCombatState.Stars (Regent's second resource; char_regent.h/.cpp)
  // The Defect's orb queue (Entities.Players.PlayerCombatState.OrbQueue): capacity comes from
  // Character::orbSlots (Run::fight); OrbQueue.maxCapacity = 10.
  std::vector<std::unique_ptr<Orb>> orbQueue;
  int orbCapacity = 0;
  int turnNumber = 1, roundNumber = 1;
  int cardsPlayedThisTurn = 0;  // CombatHistory.CardPlaysStarted this turn (player)
  int skillsFinishedThisTurn = 0;  // CardPlaysFinished of Skills this turn (LunarBlast, X3.3a)
  int attackPlaysFinishedThisTurn = 0;  // CombatHistory.CardPlaysFinished this turn: Attack plays (Finisher, X1.3a)
  int cardPlaysFinishedThisCombat = 0;  // CombatHistory.CardPlaysFinished, whole combat (GoldAxe, A1a)
  int cardPlaysFinishedThisTurn = 0;  // CardPlaysFinished of all types this turn, bumped before AfterCardPlayed (PaleBlueDot, X3.3b)
  int starsGainedThisTurn = 0;  // StarsModifiedEntry amounts > 0 this turn (Radiate, X3.3b)
  int cardsGeneratedThisCombat = 0;  // CardGeneratedEntry count (Supermassive, X3.3b)
  int shivPlaysFinishedThisTurn = 0;  // CardPlaysFinished of Shivs this turn (PhantomBlades, X1.3b)
  int energySpentThisTurn = 0;  // EnergySpentEntry amounts this turn (HelixDrill, X2.4)
  int lightningOrbsChanneled = 0;  // OrbChanneledEntry of LightningOrbs this combat (Voltaic, X2.4)
  Side currentSide = Side::Player;
  bool extraTurn = false;  // CombatTurnState.PlayersTakingExtraTurn is not empty (single player)
  bool inProgress = false, ending = false, over = false, won = false;
  bool playerPhase = false;  // UI may submit actions
  // PlayerCombatState.Phase (PlayerTurnPhase): None outside the player's turn, Start (turn start
  // hooks, the hand draw), AutoPrePlay, Play, AutoPostPlay, End (the end-of-turn hooks and flush).
  enum class TurnPhase { None, Start, AutoPrePlay, Play, AutoPostPlay, End } phase = TurnPhase::None;
  int cardOrPotionEffectDepth = 0;  // CombatManager.BeginCardOrPotionEffect nesting
  Task<> checkForEmptyHand();       // CombatManager.CheckForEmptyHand -> Hook.AfterHandEmptied
  bool autoSelectFirst = false;  // VakuuCardSelector: cmd::selectCards takes the first options (Whispering Earring)
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
  int starCost(Card* c);    // CardModel.GetStarCostWithModifiers
  int maxEnergyNow();
  bool isValidTarget(Card* c, Creature* t);
  std::vector<Creature*> hittableEnemies();
  bool canReceivePowers(Creature* c);  // Creature.CanReceivePowers (Hook.ShouldAllowHitting)
  std::vector<Creature*> aliveEnemies();

  // hooks (Hook.*)
  std::vector<Model*> listeners();
  Dec modifyDamage(Creature* target, Creature* dealer, Dec dmg, int props, Card* src);
  Dec modifyBlock(Creature* target, Dec block, int props, Card* src);
  Dec modifyOrbValue(Orb* orb, Dec amount);                      // Hook.ModifyOrbValue
  int modifyOrbPassiveTriggerCount(Orb* orb, int count);         // Hook.ModifyOrbPassiveTriggerCount

  // flow (CombatManager)
  Task<> startTurn();
  Task<> setupPlayerTurn();
  Task<> executeEnemyTurn();
  Task<> endEnemyTurn();
  Task<> endPlayerTurnPhaseOne();
  Task<> endPlayerTurnPhaseTwo();
  void switchSides();
  Task<bool> checkWinCondition();
  // `spendResources` with autoPlay: CardModel.SpendResources first, then CardCmd.AutoPlay(skipXCapture)
  // (Whispering Earring): an auto-play (IsAutoPlay hooks) that paid its cost and keeps the X it paid.
  Task<> playCard(Card* c, Creature* target, bool autoPlay = false, bool forceExhaust = false,
                  bool spendResources = false);

  std::vector<Card*>& pile(Pile p);
  Pile pileOf(Card* c);
  void removeFromPiles(Card* c);
  std::vector<Card*> allCards();
  Card* addCard(std::unique_ptr<Card> c);
  // CombatState.CreateCreature + monster SetUpForCombat: unique HP, move machine.
  // CombatState.CreateCreature (HP rolled now) + AddCreature; join = false leaves it out of `enemies` until
  // cmd::joinMonster (SurprisePower creates the Fat Gremlin first, adds it last).
  Creature* createEnemy(std::unique_ptr<Monster> m, bool join = true);
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
// CardCmd.Discard / DiscardAndDraw: the cards go to the discard pile one by one (AfterCardDiscarded each),
// then `drawAfter` cards are drawn, then every card that was Sly is played automatically.
// Use the list form for several cards, as the C# asks.
Task<> discardCards(Combat& c, std::vector<Card*> cards, int drawAfter = 0);
Task<> discardCard(Combat& c, Card* card);
Task<> loseBlock(Creature* target, Dec amount);  // CreatureCmd.LoseBlock (no AfterBlockBroken hook yet)
Task<> gainEnergy(Combat& c, int amount);
// PlayerCmd.GainStars / LoseStars / SetStars (Regent's Stars resource). GainStars is the only
// one that fires a hook (AfterStarsGained); LoseStars does not (spending a card's star cost
// fires AfterStarsSpent instead, from Combat::playCard).
Task<> gainStars(Combat& c, int amount);
Task<> loseStars(Combat& c, int amount);
Task<> setStars(Combat& c, int amount);
Task<> gainMaxHp(Creature* cr, int amount);
Task<> loseMaxHp(Creature* cr, int amount);
// CardPileCmd.AddGeneratedCardToCombat: returns the card now owned by the combat.
Task<Card*> addGeneratedCard(Combat& c, std::unique_ptr<Card> card, Pile to, bool top = false);
Task<> autoPlay(Combat& c, Card* card, Creature* target = nullptr);  // CardCmd.AutoPlay
Task<Card*> transform(Combat& c, Card* card, std::unique_ptr<Card> into);  // CardCmd.Transform
// PlayerCmd.AddPet<T>: a new player-side creature (`monsterId`, 9999 HP) owned by the player, then
// CreatureCmd.Add (Hook.AfterCreatureAddedToCombat). Null outside a live combat.
Task<Creature*> addPet(Combat& c, const std::string& monsterId);
void upgradeCard(Card* card);  // CardCmd.Upgrade
void downgradeCard(Card* card);  // CardCmd.Downgrade
void completeQuest(Run& run, Card* questCard);  // PlayerCmd.CompleteQuest (quests.cpp): the map point's CompletedQuests
Task<> addStatusCards(Combat& c, std::string cardId, Pile to, int count, bool byPlayer = false);  // byPlayer: creator == Owner
Task<std::vector<Card*>> selectCards(Combat& c, std::string prompt, std::vector<Card*> options, int minCount, int maxCount);
Task<> autoPlayFromDrawPile(Combat& c, int count, bool forceExhaust);
// CreatureCmd.Add: a monster joins mid-combat (summons, splits).
Task<Creature*> addMonster(Combat& c, std::unique_ptr<Monster> m);
Task<> joinMonster(Combat& c, Creature* created);  // CreatureCmd.Add(creature) for one made by createEnemy(m, false)

// CardCmd.Enchant: null if the enchantment can't go on this card (the C# throws). Adding the
// same stackable enchantment again adds to its amount. CardCmd.ClearEnchantment.
Enchantment* enchant(Card* card, std::unique_ptr<Enchantment> e, int amount);
void clearEnchantment(Card* card);
// CardCmd.Afflict (A4, afflictions.cpp): null if it can't (combat ending, not a combat card,
// Hook.ShouldAfflict, CanAfflict); the same stackable affliction adds to its amount.
// CardCmd.ClearAffliction.
Affliction* afflict(Card* card, std::unique_ptr<Affliction> a, int amount);
Affliction* afflict(Card* card, const char* afflictionId, int amount);
void clearAffliction(Card* card);
// Debug (A4): STS_AFFLICT=Hexed:2,Tainted puts each listed affliction on the first fitting combat
// card (hand first) at the start of the first turn; SIM_AFFLICT=1 spreads every registered one.
void debugAfflictions(Combat& c);

// OrbCmd (X2.0): channel/evoke and orb slots. `channelOrb` takes ownership of a freshly made
// orb (e.g. `cmd::channelOrb(*combat, std::make_unique<LightningOrb>())`), evicting the oldest
// queued orb first if the queue is already full.
Task<> addOrbSlots(Combat& c, int amount);                       // OrbCmd.AddSlots
void removeOrbSlots(Combat& c, int amount);                      // OrbCmd.RemoveSlots
Task<> channelOrb(Combat& c, std::unique_ptr<Orb> orb);          // OrbCmd.Channel
Task<> evokeNextOrb(Combat& c, bool dequeue = true);             // OrbCmd.EvokeNext
Task<> evokeLastOrb(Combat& c, bool dequeue = true);             // OrbCmd.EvokeLast
// OrbCmd.Passive: `countAffectedByHooks` = true runs it through ModifyOrbPassiveTriggerCount
// (Orb::triggerPassive) instead of the raw Orb::passive.
Task<> orbPassive(Combat& c, Orb* orb, Creature* target = nullptr, bool countAffectedByHooks = false);

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
  Side targetSide() const;  // AttackCommand.TargetSide
};
// AttackContext: groups plain damage calls into one attack for Hook.BeforeAttack / AfterAttack.
// Fill `a` (attacker, source, allOpponents), begin, push each hit into a.results, end.
Task<> beginAttackContext(Combat& c, Attack& a);
Task<> endAttackContext(Combat& c, Attack& a);
}  // namespace cmd

template <class P> Task<> applyPower(Creature* target, Dec amount, Creature* applier, Card* src, bool silent = false);

// ---------------------------------------------------------------- run / map

struct MapNode {
  int col = 0, row = 0;
  RoomType type = RoomType::Monster;
  std::vector<int> next;  // indices into Run::nodes
  bool visited = false;
  float x = 0, y = 0;     // layout in map space
  float jx = 0, jy = 0;   // NMapScreen jitter in native map units (±21, ±25)
  float angle = 0;        // icon tilt in degrees (NextGaussianFloat(0, 8))
  // MapPoint.Quests: ids of the models that marked this point (SpoilsMap). Not saved: a loaded map
  // gets them back from the ModifyGeneratedMapLate / AfterMapGenerated hooks, as in the C#.
  std::vector<std::string> quests;
};

// StandardActMap (+ MapPathPruning, MapPostProcessing, Overgrowth.GetMapPointTypes):
// the act's rooms in our indexing (row 0 = the game's row 1, the start point is
// dropped; then the boss, row 15, col 3, and with hasSecondBoss the DoubleBoss node, row 16,
// the boss's only child). next = child indices. Implemented in mapgen.cpp.
std::vector<MapNode> generateStandardActMap(Rng& mapRng, int actIndex, int numOfElites = 5, bool hasSecondBoss = false);
// M11 (BigGameHunter): StandardActMap with a MapPointTypeCounts override -- the unknown / rest counts
// are given (so GetMapPointTypes draws nothing) and elites may ignore the placement rules.
struct MapTypeCounts { int unknowns = 0, rests = 0, elites = 5; bool elitesIgnoreRules = false; };
std::vector<MapNode> generateStandardActMap(Rng& mapRng, int actIndex, const MapTypeCounts& counts, bool hasSecondBoss);
// E2: SpoilsActMap (SpoilsMap's act map, every path through one centred treasure), same indexing.
// `counts` (BigGameHunter) replaces GetMapPointTypes; null = roll them.
std::vector<MapNode> generateSpoilsActMap(Rng& mapRng, int actIndex, int numOfElites, const MapTypeCounts* counts);

struct Encounter {
  std::string id;
  RoomType room = RoomType::Monster;
  bool weak = false;
  std::function<std::vector<std::unique_ptr<Monster>>(Rng&)> generate;
};

enum class Screen { Title, Map, Combat, Reward, Rest, RestUpgrade, GameOver, Victory, DeckView, RelicOffer, Placeholder, Event, PotionOffer, Shop };

// ---------------------------------------------------------------- events

// EventOption: `key` is the loc base "<EVENT>.pages.<PAGE>.options.<NAME>" (the UI shows
// key.title / key.description, formatted with the event's vars). No action = locked.
struct EventOption {
  std::string key;
  std::function<Task<>()> action;
  std::shared_ptr<Relic> relic;  // AncientEventModel.RelicOption: the UI shows this relic
  // Per-option dynamic text (EventOption with explicit title / description LocStrings): `title`
  // (a loc key or literal text) replaces key.title when set; `strVars` add to / override the
  // event's placeholders when this option's text is expanded ("desc.<Name>" only for the description).
  std::string title;
  std::map<std::string, std::string> strVars;
  bool locked() const { return !action; }
};

// EventModel: the current page is `descKey` (loc key of its text) plus `options`;
// SetEventFinished clears the options and the UI offers "proceed".
struct Event {
  std::string id, locKey;      // class name / UPPER_SNAKE loc key ("AromaOfChaos", "AROMA_OF_CHAOS")
  Run* run = nullptr;
  std::unique_ptr<Rng> rngPtr;  // EventModel.Rng: seed + hash(id)
  std::vector<DynVar> vars;
  std::map<std::string, std::string> strVars;  // string placeholders: a loc key, or literal text if none matches
  std::string descKey;
  std::vector<EventOption> options;
  bool finished = false;
  // AncientEventModel: the portrait scene (gfx/bg_<ancient>.t3t) and the dialogue lines
  // (loc keys in the ancients table) shown one by one before the options.
  bool ancient = false;
  std::vector<std::string> dialogue;
  size_t dialogueLine = 0;

  virtual ~Event() = default;
  virtual bool isAllowed(Run&) { return true; }                 // IsAllowed
  virtual std::vector<EventOption> initialOptions() = 0;        // GenerateInitialOptions
  virtual void calculateVars() {}                               // CalculateVars (before the first page)
  virtual Task<> onStart() { return {}; }                       // custom-layout events (FakeMerchant): runs before the option loop; setting `finished` ends the event
  virtual void onEventFinished() {}                             // OnEventFinished (EnsureCleanup from SetEventFinished)

  Rng& rng() { return *rngPtr; }
  Creature* owner();
  std::string page(const std::string& p) const { return locKey + ".pages." + p; }
  EventOption option(const std::string& pageName, const std::string& name, std::function<Task<>()> action) {
    return {page(pageName) + ".options." + name, std::move(action)};
  }
  void setPage(const std::string& pageName, std::vector<EventOption> opts) {
    descKey = page(pageName) + ".description";
    options = std::move(opts);
  }
  void setFinished(const std::string& pageName) {  // SetEventFinished(L10NLookup(page.description))
    descKey = page(pageName) + ".description";
    options.clear();
    if (!finished) onEventFinished();
    finished = true;
  }
  DynVar* var(const char* n) { for (auto& v : vars) if (v.name == n) return &v; return nullptr; }
  Dec val(const char* n) { auto* v = var(n); return v ? v->base : Dec(0); }
  void addVar(const char* n, Dec v) { vars.push_back({n, v, v}); }
  void setVar(const char* n, Dec v) { if (auto* d = var(n)) d->base = v; else addVar(n, v); }
  void setStr(const std::string& n, std::string locKeyOrText) { strVars[n] = std::move(locKeyOrText); }
};
using EventFactory = std::unique_ptr<Event> (*)();

// Picking cards from the deck outside combat (CardSelectCmd.FromDeck*), answered by the UI.
struct DeckChoice {
  std::string prompt;          // loc key of the prompt, or plain text
  std::vector<Card*> options;
  int count = 1;
  int minCount = -1;           // -1: exactly `count`; else between minCount and count
  bool canCancel = false;
  bool showUpgrade = false;    // preview the upgraded card (upgrade prompts)
  // CardSelectCmd.FromDeckForEnchantment: the enchantment (and amount) the picked card will get,
  // so the picker can preview the card enchanted; "" for every other prompt.
  std::string enchantId;
  int enchantAmount = 0;
  bool active = false;
  Signal<std::vector<Card*>> result;
};

namespace db { struct ActDef; }

// MerchantInventory entries: five character cards (one on sale), three relics, three
// potions and the card removal service. `cost` is before Hook.ModifyMerchantPrice.
// shopChoice value: a Foul Potion was thrown at the FakeMerchant (its fight starts).
constexpr int kFoulPotionThrow = -2;

struct ShopItem {
  enum Kind { CardItem, RelicItem, PotionItem, Removal } kind = CardItem;
  CardType cardType = CardType::Attack;  // the character card slot's type
  Rarity cardRarity = Rarity::Uncommon;  // a colorless slot's rarity (Uncommon, Rare)
  bool colorless = false;                // one of the two colorless card slots
  std::unique_ptr<Card> card;
  std::unique_ptr<Relic> relic;
  std::unique_ptr<Potion> potion;
  int cost = 0;
  bool onSale = false;
  bool used = false;  // the removal service, once per shop
  bool stocked() const { return kind == Removal ? !used : (card || relic || potion); }
};

struct Run {
  uint64_t seed = 1;
  std::string characterId = "Ironclad";  // Player.Character: db::character(characterId)
  int ascension = 0;                     // RunState.AscensionLevel (0-10)
  bool hasAscension(AscensionLevel l) const { return ascension >= (int)l; }
  int ascValue(AscensionLevel l, int ascValue, int base) const { return hasAscension(l) ? ascValue : base; }
  const Character& character() const;
  std::unique_ptr<Creature> player;
  std::vector<std::unique_ptr<Card>> deck;
  std::vector<std::unique_ptr<Relic>> relics;
  int gold = 99;
  int floor = 0;                   // total floors climbed (RunState.TotalFloor)
  int actIndex = 0;                // RunState.CurrentActIndex: 0 Overgrowth/Underdocks, 1 Hive, 2 Glory
  static constexpr int kActs = 3;
  // RunState.Acts: the run's act per index (db::ActDef names), rolled in Run::start.
  std::vector<std::string> actIds = {"Overgrowth", "Hive", "Glory"};
  // RoomSet (ActModel._rooms) of each act, all made at the run start by generateRooms
  // (RunManager.GenerateRooms + ActModel.GenerateRooms, Rng.UpFront). A room of a type takes
  // list[visited % size]; `visited` counts the rooms entered (RoomSet.MarkVisited).
  struct RoomSet {
    std::vector<std::string> events;
    int eventsVisited = 0;
    std::vector<std::string> normal;  // NumberOfWeakEncounters weak fights, then regular ones
    int normalVisited = 0;
    std::vector<std::string> elites;  // 15
    int elitesVisited = 0;
    std::string boss, secondBoss, ancient;  // ancient: "" if none registered
  };
  RoomSet rooms[kActs];
  void generateRooms();
  std::vector<MapNode> nodes;
  int currentNode = -1;
  std::unique_ptr<Combat> combat;
  std::string bossId;  // the current act's RoomSet boss (and secondBossId below), set by enterAct
  // DoubleBoss (ascension 10, last act only): the act's second boss (ActModel.SecondBossEncounter),
  // fought at its own map node after the first boss (StandardActMap.SecondBossMapPoint).
  std::string secondBossId;
  // The boss's map node (first Boss node) and the DoubleBoss node after it (-1 if none).
  int bossNode() const;
  int secondBossNode() const;
  // The encounter of a boss node: secondBossId at the second boss node, else bossId.
  const std::string& bossIdAt(int node) const;
  std::map<std::string, std::unique_ptr<Rng>> rngs;
  float rarityOffset = -0.05f;  // CardRarityOdds.CurrentValue
  Rarity rollRarity(RoomType room);
  // CardFactory.RollForUpgrade: one Rewards float; the card upgrades when it is <= baseChance plus the
  // act index times the scaling (0.25, 0.125 with Scarcity; not for Rare cards).
  void rollCardUpgrade(Card& c, double baseChance);
  std::vector<std::unique_ptr<Card>> cardReward(RoomType room, int count);

  Screen screen = Screen::Title;
  // UI requests
  Signal<int> mapChoice;             // node index
  // S14 (RGDSplus U17, C# RewardsSet): rewardItems holds every reward of the room, generated
  // up front (see the ordering comment on combatRewards), with its real payload -- nothing is
  // granted yet. The UI claims rows in any order via rewardListChoice (row index, or -1/out of
  // range to Proceed and forfeit whatever is left); claiming a row is what actually performs the
  // C# Reward.OnSelect() side effect (gainGold, procurePotion, obtainRelic, addCardToDeck) and
  // only then erases the row. A Card row reuses rewardCards/rewardChoice (the existing card grid)
  // as a nested sub-screen; skipping it puts the same (un-rerolled) options back and the row stays,
  // matching CardReward.OnSelect returning false. A Potion row whose claim fails (belt full, C#
  // PotionReward.OnSelect / PotionProcureFailureReason.TooFull) also stays.
  enum class RewardKind { Gold, Potion, Relic, Card, SpecialCard };
  struct RewardItem {
    RewardKind kind;
    int gold = 0;                              // Gold: the amount, not yet granted
    bool goldStolenBack = false;               // Gold: GoldReward.wasGoldStolenBack (COMBAT_REWARD_GOLD_STOLEN)
    std::unique_ptr<Potion> potion;            // Potion: the rolled potion, not yet in the belt
    std::unique_ptr<Relic> relic;              // Relic: the rolled relic, not yet obtained
    std::vector<std::unique_ptr<Card>> cards;  // Card: the options currently on offer
    std::unique_ptr<Card> card;                // SpecialCard: the card it adds (SpecialCardReward)
    // Card (E8, CardReward): the options it was populated with (count 0 = fixed cards, no reroll),
    // CanReroll / CanSkip, and AfterGenerated (run after every populate, rerolls included).
    CardCreationOptions cardOptions;
    int cardCount = 0;
    bool canReroll = false, canSkip = true;
    std::function<void(std::vector<std::unique_ptr<Card>>&)> afterGenerated;
  };
  // CardReward(options, count): a Card row populated now (CardFactory.CreateForReward with
  // IsCardReward, then AfterGenerated).
  RewardItem makeCardReward(CardCreationOptions o, int count,
                            std::function<void(std::vector<std::unique_ptr<Card>>&)> afterGenerated = nullptr);
  // RewardsCmd.OfferCustom / RewardsSet.Offer: Hook.ModifyRewards (Driftwood), the sort by
  // RewardsSetIndex, then the claimable list. `terminal` (a combat room's set) keeps the list up
  // until Proceed; a custom set also closes once every reward is taken.
  Task<> offerRewards(std::vector<RewardItem> items, bool terminal = false);
  // CombatRoom.AddExtraReward for rows with a payload (SpecialCardReward, a stolen-back GoldReward):
  // added after the room's own rewards by the next combatRewards, then cleared.
  std::vector<RewardItem> roomExtraRewards;
  // The open Card row's CardRewardAlternative ids besides Skip ("REROLL", "SACRIFICE"): the UI
  // fires rewardChoice with rewardCards.size() + i for alternative i.
  std::vector<std::string> rewardAlternatives;
  // CardFactory.CreateForReward (card_rewards.cpp): `count` distinct cards, the creation-options
  // hooks per card, the upgrade roll, then TryModifyCardRewardOptions(Late) unless NoModifyHooks.
  std::vector<std::unique_ptr<Card>> createForReward(const CardCreationOptions& o, int count);
  void runCardRewardHooks(std::vector<std::unique_ptr<Card>>& cards, const CardCreationOptions& o);
  // Player.CanUseOrRemovePotions: false while some events run (the belt can't be used or emptied).
  bool canUseOrRemovePotions = true;
  // HealRestSiteOption.ExecuteRestSiteHeal: the rows Hook.ModifyRestSiteHealRewards adds (DreamCatcher).
  void addRestSiteHealReward(RewardItem item) { restSiteHealRewards.push_back(std::move(item)); }
  std::vector<RewardItem> restSiteHealRewards;
  // CombatRoom.ExtraRewards: rewards added after the room's own by the event that started the fight
  // (PunchOff: Relic, Potion). Populated (rolled) by the next combatRewards, then cleared.
  std::vector<RewardKind> extraRewards;
  std::vector<RewardItem> rewardItems;
  int bonusCardRewards = 0;  // extra card rewards a card added this fight (TheHunt, X1.4); consumed by combatRewards
  Signal<int> rewardListChoice;       // row index to claim, or -1/out of range = Proceed
  std::vector<std::unique_ptr<Card>> rewardCards;  // the open Card row's options (empty = list mode)
  Signal<int> rewardChoice;           // index into rewardCards, or -1 skip (row stays)
  Signal<int> restChoice;            // an id from restOptions, or -1 to leave (after one, Miniature Tent)
  // RestSiteOption ids offered now: 0 heal, 1 smith, 2 lift (Girya), 3 dig (Shovel).
  std::vector<int> restOptions;
  std::vector<int> restUsed;
  std::vector<Card*> upgradeOptions;
  Signal<int> upgradeChoice;         // index or -1 back
  int lastHeal = 0;
  // Events: the events seen this run (RunState.VisitedEventIds), the running event and its UI answers.
  std::vector<std::string> visitedEvents;
  std::unique_ptr<Event> currentEvent;
  Signal<int> eventChoice;         // option index; any value proceeds once finished
  DeckChoice deckChoice;
  Task<> runEvent(std::unique_ptr<Event> e);
  std::unique_ptr<Event> pullNextEvent();  // ActModel.PullNextEvent (null if none ported)
  // Deck commands used by events and relics (CardCmd / CardPileCmd on the deck).
  Task<std::vector<Card*>> selectFromDeck(std::string prompt, std::function<bool(Card*)> filter, int count,
                                          bool canCancel = false, bool showUpgrade = false, int minCount = -1);
  Card* addCardToDeck(std::unique_ptr<Card> c);
  // Enchantments on deck cards (events, relics): CardSelectCmd.FromDeckForEnchantment lists the
  // deck cards `id` can go on (and `filter`, if given); enchantCard is CardCmd.Enchant<T>. The
  // picker gets DeckChoice::enchantId / enchantAmount (`amount`) to preview the enchanted card.
  Task<std::vector<Card*>> selectForEnchantment(const std::string& id, int count = 1,
                                                std::function<bool(Card*)> filter = nullptr, int amount = 1);
  bool canEnchantAny(const std::string& id, std::function<bool(Card*)> filter = nullptr);
  Enchantment* enchantCard(Card* c, const std::string& id, int amount);
  void removeCardFromDeck(Card* c);
  // CardCmd.Transform on a deck card: the original leaves, the replacement goes through
  // Hook.ModifyCardBeingAddedToDeck and joins the end of the deck, then AfterCardChangedPiles.
  Card* transformCard(Card* c, std::unique_ptr<Card> into);
  std::unique_ptr<Card> randomTransformFor(Card* c, Rng& rng); // CardFactory transform target
  Task<> loseHp(int amount);   // outside combat; 0 HP ends the run
  Task<> gainMaxHp(int amount);
  Task<> loseMaxHp(int amount);
  Task<bool> eventFight(const std::string& encounterId);  // fight + monster rewards, back to the event
  // EnterCombatWithoutExitingEvent extras for the next combatRewards: the encounter's fixed gold
  // (Min/MaxGoldReward, -1 = the room default) and preset RelicRewards (FakeMerchant).
  int rewardGold = -1;
  std::vector<std::unique_ptr<Relic>> extraRewardRelics;
  Task<> combatRewards(RoomType type);  // RewardsSet after a won fight
  Task<> restSite();
  bool died = false;
  // RunManager.WinRun (TheArchitect's PROCEED, content_architect.cpp): the run is won; Run::main
  // records it and shows Screen::Victory once the event returns.
  bool runWon = false;
  void winRun() { runWon = true; }
  bool progressRecorded = false;  // guards progress::onRunEnded against firing twice (see abandon())
  // Run history (history.h, M2): the path so far (RunState.MapPointHistory, saved in run.sav),
  // when the run started (unix seconds) and the seconds played (the app adds real frame time
  // while the run is on screen; the headless sim leaves it at 0).
  history::Path mapHistory;
  int64_t startTime = 0;
  double runTime = 0;
  bool cccCombo = false;  // ExtraFields.CccomboBadgeUnlocked (badges.h; run.sav version 7)
  void historyPoint(history::PointType type);  // RunState.AppendToMapPointHistory (rooms follow)
  void historyRoom(history::RoomKind type, const std::string& model = "");  // CurrentMapPointHistoryEntry.Rooms.Add
  // A room that is not ported yet (events, shops): Screen::Placeholder shows this text.
  std::string placeholderText;
  std::string ancientId;            // this act's Ancient event ("Neow" in act 1), empty if none
  bool ancientPending = false;      // the act starts with its Ancient (Run::main runs it first)
  Task<> enterAncient();
  Task<> chooseCardFor(std::vector<std::unique_ptr<Card>> options);  // CardSelectCmd.FromChooseACardScreen -> deck
  // E2 (quests.cpp): CardSelectCmd.FromChooseABundleScreen -> deck. The bundles go to rewardCards one
  // after the other, rewardBundleSize cards each; rewardChoice answers with the bundle index.
  Task<> chooseBundleFor(std::vector<std::vector<std::unique_ptr<Card>>> bundles);
  int rewardBundleSize = 1;
  // E2: the map point before the current one (RunState.VisitedMapCoords[^2], WingedBoots) and
  // RunState.CurrentRoomCount (1 in a map point's room, 2 in an event's fight).
  int previousNode = -1;
  int currentRoomCount = 0;
  bool spoilsActMap = false;     // the map being generated is a SpoilsActMap (BigGameHunter keeps it)
  void runLateMapHooks();       // ModifyGeneratedMapLate + AfterMapGenerated on Run::nodes (new or loaded map)
  Task<> beforeRoomEntered(RoomType type);  // Hook.BeforeRoomEntered
  Task<int> handleSpoilsMap();   // OneOffSynchronizer.TryHandleSpoilsMap (treasure rooms)
  Signal<int> placeholderDone;
  // UnknownMapPointOdds: current odds of the non-event outcomes of a "?" room.
  float unknownMonsterOdds = 0.1f, unknownTreasureOdds = 0.02f, unknownShopOdds = 0.03f;
  RoomType rollUnknownRoom();      // returns Unknown for "event"
  // Screen::RelicOffer: one relic (RelicReward, treasure chest, Dig) or a choose-one of several
  // (RelicSelectCmd.FromChooseARelicScreen). relicChoice: 0 skip, k >= 1 takes relicOffers[k-1].
  std::vector<std::unique_ptr<Relic>> relicOffers;
  bool relicOfferFromChest = false;
  Signal<int> relicChoice;

  // RelicGrabBag: per-rarity relic ids, shuffled once per run (player bag = shared +
  // Ironclad pools; the shared bag feeds treasure chests).
  std::map<RelicRarity, std::vector<std::string>> relicBag, sharedRelicBag;
  void populateRelicBags();
  void removeDisallowedRelics();  // RelicGrabBag.RemoveDisallowedRelicsFromDeques
  RelicRarity rollRelicRarity(Rng& rng);  // RelicFactory.RollRarity
  std::unique_ptr<Relic> pullRelicFromFront(std::map<RelicRarity, std::vector<std::string>>& bag, RelicRarity r);
  Task<> obtainRelic(std::unique_ptr<Relic> r);   // RelicCmd.Obtain
  Task<> offerRelic(std::unique_ptr<Relic> r, bool fromChest);
  Task<> chooseRelic(std::vector<std::unique_ptr<Relic>> rs, bool fromChest);
  Task<> gainGold(int amount);                     // PlayerCmd.GainGold
  bool hasRelic(const std::string& id) const;

  // Potions (Player.PotionSlots): a fixed belt, null = empty slot.
  std::vector<std::unique_ptr<Potion>> potions = std::vector<std::unique_ptr<Potion>>(3);
  float potionRewardOdds = 0.4f;  // PotionRewardOdds.CurrentValue
  std::unique_ptr<Potion> potionOffer;  // PotionReward on Screen::PotionOffer
  Signal<int> potionOfferChoice;         // 1 take, 0 skip (the UI discards to make room)
  bool hasOpenPotionSlot() const;
  bool procurePotion(std::unique_ptr<Potion> p);  // PotionCmd.TryToProcure: false when full
  void discardPotion(int slot);                   // PotionCmd.Discard
  bool canUsePotion(int slot) const;              // usage allowed right now
  Task<> usePotion(int slot, Creature* target);   // PotionModel.OnUseWrapper
  std::unique_ptr<Potion> randomPotion(Rng& rng, bool inCombat);  // PotionFactory
  bool rollPotionReward(RoomType room);           // PotionRewardOdds.Roll
  Task<> offerPotion(std::unique_ptr<Potion> p);
  bool preventDeath();  // FairyInABottle / LizardTail: returns true if the player was saved
  std::vector<std::unique_ptr<Potion>> randomPotions(int count, Rng& rng);  // distinct

  // Merchant (MerchantRoom / MerchantInventory).
  std::vector<ShopItem> shop;
  int shopRemovalsUsed = 0;       // ExtraFields.CardShopRemovalsUsed
  Signal<int> shopChoice;         // item index to buy, -1 leaves
  std::string shopMessage;        // merchant line after a failed purchase (loc key)
  Task<> enterShop();
  int shopPrice(const ShopItem& it);
  std::unique_ptr<Relic> pullRelicFromBack(RelicRarity k);  // RelicFactory.PullNextRelicFromBack (shops)
  std::vector<Model*> listeners();                 // run-level hook listeners (relics)

  Rng& rng(const char* stream) {
    auto& r = rngs[stream];
    if (!r) r = std::make_unique<Rng>(seed, stream);
    return *r;
  }
  // characterId may be kRandomCharacter (the character select's Random): resolveCharacter picks one.
  void start(uint64_t seed, const std::string& characterId = "Ironclad", int ascension = 0);
  static constexpr const char* kRandomCharacter = "RandomCharacter";
  static std::string resolveCharacter(uint64_t seed, const std::string& characterId);
  void enterAct(int index);        // RunManager.EnterAct: new map, encounters and events
  const db::ActDef& act() const;
  // Music hook for the audio lane: NRunMusicController.ResolveMusic picks the act's
  // BgMusicOptions entry with Rng(seed, "bg_music"). Returns the FMOD event id ("" if none).
  std::string actMusic() const;
  void generateMap();
  bool devSkipAct = false;         // developer menu: leave for the next act at the next map choice
  // Development build: every node can be entered, not only the next ones on the path.
  // STS_PATH_ONLY=1 restores the normal rule.
  bool freeMap = true;
  // Developer menu state.
  bool devGod = false;             // the player loses no HP
  std::string devNextEncounter;    // the next fight is this encounter (then cleared)
  std::vector<int> pathNodes() const;       // the game's rule: the next row along the paths
  std::vector<int> reachableNodes() const;  // pathNodes first, then (freeMap) every other node
  Task<> main();
  Task<bool> fight(const std::string& encounterId);
  // Saves (save.cpp): taken where the run waits for a map choice, the only save point.
  std::function<void(Run&)> onSavePoint;
  // Hook effects started from synchronous code (Lucky Fysh's gold, potion hooks) run as
  // side tasks; the save point waits for them so none is cut in half.
  int pendingSide = 0;
  // Deck cards / relics taken out of the run: a side task (spawnSide) may still hold them in a
  // listeners() snapshot, e.g. Lucky Fysh's gainGold suspended in Dragon Fruit's max-HP gain while the
  // deck changes. Freed on the map once no side task is pending (like Combat::graveyard).
  std::vector<std::unique_ptr<Model>> graveyard;
  void spawnSide(Task<> t);
  std::string save();
  bool load(const std::string& data);  // on a fresh Run; then spawn main()
  // Profile progress (progress.cpp, package M1): call when the player abandons this run from
  // the pause menu (a run that ends by winning or losing records itself from Run::main). A
  // no-op if the run already ended (died or screen is already GameOver/Victory).
  // Called by the pause menu's abandon confirm (ui/screens/settings.cpp) before returnTitle.
  void abandon();

  // M11 custom runs (modifiers.h / modifiers.cpp). RunState.Modifiers: hook listeners after the
  // relics (Run::listeners, Combat::listeners), saved in run.sav (version 8) and the run history.
  // Set them with setModifiers before start(); a load reads them back from the save.
  std::vector<std::shared_ptr<Modifier>> modifiers;
  bool customRun = false;         // GameMode.Custom: no ascension unlock at the end
  std::string seedText;           // RunRngSet.StringSeed ("" when started from a numeric seed)
  std::string dailyDate;          // M12 GameMode.Daily: the day ("YYYY-MM-DD", daily.h); "" otherwise
  float unknownEliteOdds = -1.f;  // UnknownMapPointOdds Elite (-1 = never; DeadlyEvents 0.1)
  void setModifiers(const std::vector<std::string>& keys);  // "Draft", "CharacterCards:Silent", ...
  std::vector<std::string> modifierKeys() const;
  bool hasModifier(const char* id) const;  // by the C# class name
  bool modifiersClearDeck() const;         // ModifierModel.ClearsPlayerDeck (Draft, SealedDeck, Insanity)
  std::vector<std::string> modifierCardPools() const;  // CharacterCards: the extra characters' pools
  bool hoarding = false;                   // Hoarder: adding its own copies (CardPileAdd clonedBy)
};

// ---------------------------------------------------------------- registry

namespace db {
void init();
std::unique_ptr<Card> card(const std::string& id);
std::vector<std::string> cardIds();  // every registered card id, sorted (M8's card library)
std::unique_ptr<Power> power(const std::string& id);
const Encounter* encounter(const std::string& id);
std::vector<std::string> encounterIds();  // every registered encounter (tests, tools)
// Monsters that only enter a fight as summons (in no encounter's generate), so the bestiary (M10)
// can build them; null if `id` (the class name) is not registered.
void registerMonster(const std::string& id, std::function<std::unique_ptr<Monster>()> make);
std::unique_ptr<Monster> monster(const std::string& id);
std::unique_ptr<Relic> relic(const std::string& id);
std::unique_ptr<Potion> potion(const std::string& id);
std::vector<std::string> relicIds();   // every registered relic id, sorted (M9's relic collection)
std::vector<std::string> potionIds();  // every registered potion id, sorted (M9's potion lab)
void registerPotion(const std::string& id, PotionFactoryFn f);
// IroncladPotionPool + SharedPotionPool ids in the game's order (registered or not).
const std::vector<std::string>& potionPool();  // = potionPool("Ironclad")
std::vector<std::string> ironcladRewardPool();
std::vector<std::string> ironcladStarterDeck();
// acts.cpp: Overgrowth, Underdocks, Hive, Glory (ModelDb.Acts order) with their encounter
// and event ids as in the C#.
struct ActDef {
  const char* name;  // Overgrowth / Underdocks / Hive / Glory (ActModel id)
  const char* key;   // file path identifier: art is gfx/bg_<key>.t3t, gfx/bg_map_<key>.t3t
  int index;         // ActModel.Index: which act of the run it can be (Underdocks: 0)
  bool isDefault;    // ActModel.IsDefault (Underdocks: false, the alternative act 1)
  int weakCount;     // ActModel.NumberOfWeakEncounters
  std::vector<std::string> weak, normal, elites, bosses, events;
  // Audio hooks for the audio lane (no audio yet): ActModel.BgMusicOptions (FMOD event ids,
  // one picked by Run::actMusic) and ActModel.AmbientSfx.
  std::vector<std::string> music;
  const char* ambience;
  int baseRooms;  // ActModel.BaseNumberOfRooms (weak + regular fights in the RoomSet)
};
// EncounterModel.Tags (EncounterTag names; most encounters have none), for SharesTagsWith.
const std::vector<std::string>& encounterTags(const std::string& encounterId);
const std::vector<ActDef>& acts();          // every act, ModelDb.Acts order
const ActDef* act(const std::string& name);  // by ActModel id; null if unknown
// ActModel.GetRandomList (StartRunLobby.BeginRunLocally, Rng(seed, "act_selection")): one act
// per act index, picked among that index's acts in ModelDb.Acts order. Returns the ids.
std::vector<std::string> randomActList(Rng& rng);
std::vector<std::string> act1Weak();
std::vector<std::string> act1Normal();
std::vector<std::string> act1Elites();
std::vector<std::string> act1Bosses();
void registerCard(const std::string& id, CardFactory f);
void registerPower(const std::string& id, PowerFactory f);
void registerRelic(const std::string& id, RelicFactoryFn f);
void registerEvent(const std::string& id, EventFactory f);
// Enchantments (enchantments.cpp). `enchantmentIds()` lists the registered ones.
void registerEnchantment(const std::string& id, EnchantmentFactory f);
std::unique_ptr<Enchantment> enchantment(const std::string& id);
const std::vector<std::string>& enchantmentIds();
// Afflictions (afflictions.cpp, A4).
void registerAffliction(const std::string& id, AfflictionFactory f);
std::unique_ptr<Affliction> affliction(const std::string& id);
const std::vector<std::string>& afflictionIds();
std::unique_ptr<Event> event(const std::string& id);
// Overgrowth.AllEvents in the game's order (registered or not).
const std::vector<std::string>& act1Events();
const std::vector<std::string>& sharedEvents();  // ModelDb.AllSharedEvents
// SharedRelicPool / IroncladRelicPool ids in the game's order (registered or not).
const std::vector<std::string>& sharedRelicPool();
const std::vector<std::string>& ironcladRelicPool();
bool relicRegistered(const std::string& id);
void registerEncounter(const std::string& id, RoomType room, bool weak, std::function<std::vector<std::unique_ptr<Monster>>(Rng&)> gen);
// Characters (characters.cpp). `character` falls back to the Ironclad for an unknown id.
const Character& character(const std::string& id);
const std::vector<std::string>& characterIds();  // playable characters in the game's order
// ModelDb.AllCharacters order (Ironclad, Silent, Regent, Necrobinder, Defect): use it wherever the
// C# draws from the character list with an rng (Random character, Orobas, Kaleidoscope, ...).
const std::vector<std::string>& allCharacters();
// An Ancient's dialogue for this visit (ancient_dialogues.cpp): AncientDialogueSet.GetValidDialogues
// then NextItem on `rng` (Rng.Chaotic: pass a cosmetic stream). The line keys are in the ancients table.
std::vector<std::string> ancientDialogue(const std::string& ancientId, const std::string& characterId, int charVisits,
                                         int totalVisits, Rng& rng);
// The same for this run: the visits come from the profile (progress::ancientVisits), the pick from a
// private stream of the run seed (Rng.Chaotic in the C#; it never touches the game's rngs).
std::vector<std::string> ancientDialogueFor(const Run& run, const std::string& ancientId);
// True when the character's starter deck and starting relic are all registered (ported).
bool characterPlayable(const std::string& id);
// Registered cards of the character's pool matching a filter, in pool order, without the
// multiplayer-only cards: e.g. for "add a random Attack". Pass the run's characterId.
std::vector<std::string> characterCards(const std::string& characterId, std::function<bool(const Card&)> filter);
// The character's potions, then the shared pool (registered or not).
std::vector<std::string> potionPool(const std::string& characterId);
// The colorless pool (colorless_pool.cpp, C# ColorlessCardPool order): registered cards matching a filter, without the
// multiplayer-only ones. `isColorless` = the id is in the pool (VisualCardPool.IsColorless). Helpers: colorless.h.
std::vector<std::string> colorlessCards(std::function<bool(const Card&)> filter);
bool isColorless(const std::string& id);
// Ironclad-only shortcuts, kept for old callers: prefer the character versions above.
const std::vector<std::string>& ironcladPool();
std::vector<std::string> ironcladCards(std::function<bool(const Card&)> filter);
// Orbs (char_defect.cpp registers Lightning/Frost/Dark/Plasma/Glass).
void registerOrb(const std::string& id, OrbFactory f);
std::unique_ptr<Orb> orb(const std::string& id);
std::unique_ptr<Orb> randomOrb(Rng& rng);  // OrbModel.GetRandomOrb: uniform among the 5 orbs
}  // namespace db

template <class P> Task<> applyPower(Creature* target, Dec amount, Creature* applier, Card* src, bool silent) {
  // PowerCmd.Apply<T>: cmd::applyPower stacks onto an existing instance or adds this one
  // (after Hook.ModifyPowerAmountReceived either way).
  if (target->combat && target->combat->ending) co_return;
  co_await cmd::applyPower(std::make_unique<P>(), target, amount, applier, src, silent);
}

// PowerCmd.Apply<T> returning the power: the new instance, or the one it stacked onto (null when the
// application was blocked, e.g. by Artifact, or the stack dropped to 0). Needed for instanced powers,
// whose new instance is not `target->get<P>()`.
template <class P> Task<P*> applyPowerGet(Creature* target, Dec amount, Creature* applier, Card* src, bool silent = false) {
  if (target->combat && target->combat->ending) co_return nullptr;
  auto fresh = std::make_unique<P>();
  P* raw = fresh.get();
  Power* stack = target->stackingInstance(*raw, applier);
  co_await cmd::applyPower(std::move(fresh), target, amount, applier, src, silent);
  Power* want = stack ? stack : raw;
  for (auto& q : target->powers) if (q.get() == want) co_return static_cast<P*>(want);
  co_return nullptr;
}

template <class P> Task<> Monster::applyToSelf(Dec amount) {
  co_await applyPower<P>(creature, amount, creature, nullptr);
}
template <class P> Task<> Monster::applyToTargets(std::vector<Creature*> targets, Dec amount) {
  for (auto* t : targets) co_await applyPower<P>(t, amount, creature, nullptr);
}

}  // namespace sts
