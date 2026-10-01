// CombatHistory (Combat.History/CombatHistory.cs + Entries/*): a log of most events of one combat.
// Each entry is recorded right after its event and before the matching AfterX hook, as in the C#.
// Combat owns one (Combat::history); a new Combat starts empty, so CombatHistory.Clear has no caller.
//
// The C#'s entry classes become one tagged struct (no RTTI); `Kind` names the class and the
// fields below say which ones each kind fills. Cards, creatures and powers are combat-lifetime
// pointers (Combat::cardStore / graveyard keep them alive), read live like the C#'s references.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sts {

struct Card;
struct Combat;
struct CardPlay;
struct Creature;
struct DamageResult;
struct Power;
enum class Side;

struct CombatHistoryEntry {
  enum Kind : uint8_t {
    CardPlayStarted,       // card, target=other, play*, autoPlay, energy=amount
    CardPlayFinished,      // same as started + flag = WasEthereal
    CardAfflicted,         // card, id = affliction id
    CardDiscarded,         // card
    CardDrawn,             // card, flag = FromHandDraw
    CardExhausted,         // card
    CardGenerated,         // card, flag = created by the player (Creator != null)
    CreatureAttacked,      // actor = attacker, amount = total unblocked, hits = damage results
    DamageReceived,        // actor = receiver, other = dealer, card = card source, damage fields
    BlockGained,           // actor = receiver, amount, props, playSeq (0 = no CardPlay)
    EnergySpent,           // amount
    MonsterPerformedMove,  // actor = monster creature, id = move id
    OrbChanneled,          // id = orb id
    PotionUsed,            // id = potion id, other = target
    PowerReceived,         // actor = power owner, power, id = power id, amount, other = applier
    StarsModified,         // amount (signed)
    Summoned,              // amount
  };
  Kind kind;
  // CombatHistoryEntry: RoundNumber, CurrentSide and the (single) player's TurnNumber.
  int round = 0;
  Side side;
  int playerTurn = 0;
  Creature* actor = nullptr;  // Actor (the player's creature for player-specific entries)
  Creature* other = nullptr;  // see Kind
  Card* card = nullptr;
  Power* power = nullptr;
  std::string id;
  int amount = 0;
  int props = 0;
  bool flag = false;
  // CardPlay identity (CardPlayStarted/Finished share one; BlockGained's CardPlay) and fields.
  int playSeq = 0, playIndex = 0, playCount = 1;
  bool autoPlay = false;
  // DamageResult (DamageReceived).
  int unblocked = 0, blocked = 0, overkill = 0;
  bool killed = false, fullyBlocked = false, blockBroken = false;
  int hits = 0;  // CreatureAttacked: number of DamageResults
};

struct CombatHistory {
  std::vector<CombatHistoryEntry> entries;
  int lastPlaySeq = 0;

  // CombatHistoryEntry.HappenedThisTurn / HappenedLastPlayerTurn (single player).
  static bool happenedThisTurn(const CombatHistoryEntry& e, const Combat& c);
  static bool happenedLastPlayerTurn(const CombatHistoryEntry& e, const Combat& c);

  template <class F> int count(F&& pred) const {
    int n = 0;
    for (auto& e : entries) if (pred(e)) ++n;
    return n;
  }
  template <class F> bool any(F&& pred) const {
    for (auto& e : entries) if (pred(e)) return true;
    return false;
  }
  int count(CombatHistoryEntry::Kind k) const { return count([k](const CombatHistoryEntry& e) { return e.kind == k; }); }
  // Entries of kind `k` that happened this turn and pass `pred`.
  template <class F> int countThisTurn(const Combat& c, CombatHistoryEntry::Kind k, F&& pred) const {
    return count([&](const CombatHistoryEntry& e) { return e.kind == k && happenedThisTurn(e, c) && pred(e); });
  }
  int countThisTurn(const Combat& c, CombatHistoryEntry::Kind k) const {
    return countThisTurn(c, k, [](const CombatHistoryEntry&) { return true; });
  }
  // The CardPlay a card in the Play pile is currently resolving: the last CardPlayStarted entry of
  // that card (null if none). Stands in for CardModel.CurrentPlayIndex and the CardPlay argument
  // the C# passes to block hooks / BlockGained.
  const CombatHistoryEntry* currentPlay(const Combat& c, const Card* card) const;

  // Recorders (CombatHistory.CardPlayStarted etc.).
  void cardPlayStarted(const Combat& c, const CardPlay& cp);
  void cardPlayFinished(const Combat& c, const CardPlay& cp);
  void cardAfflicted(const Combat& c, Card* card, const std::string& afflictionId);
  void cardDiscarded(const Combat& c, Card* card);
  void cardDrawn(const Combat& c, Card* card, bool fromHandDraw);
  void cardExhausted(const Combat& c, Card* card);
  void cardGenerated(const Combat& c, Card* card, bool byPlayer);
  void creatureAttacked(const Combat& c, Creature* attacker, const std::vector<std::vector<DamageResult>>& results);
  void damageReceived(const Combat& c, const DamageResult& r, Creature* dealer, Card* cardSource);
  void blockGained(const Combat& c, Creature* receiver, int amount, int props, Card* cardSource);
  void energySpent(const Combat& c, int amount);
  void monsterPerformedMove(const Combat& c, Creature* monster, const std::string& moveId);
  void orbChanneled(const Combat& c, const std::string& orbId);
  void potionUsed(const Combat& c, const std::string& potionId, Creature* target);
  void powerReceived(const Combat& c, Power* power, int amount, Creature* applier);
  void starsModified(const Combat& c, int amount);
  void summoned(const Combat& c, int amount);

 private:
  CombatHistoryEntry& add(const Combat& c, CombatHistoryEntry::Kind k, Creature* actor);
};

}  // namespace sts
