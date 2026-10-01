// CombatHistory (combat_history.h).
#include "combat_history.h"

#include "game.h"

namespace sts {

using E = CombatHistoryEntry;

bool CombatHistory::happenedThisTurn(const E& e, const Combat& c) {
  return e.round == c.roundNumber && e.side == c.currentSide && e.playerTurn == c.turnNumber;
}

bool CombatHistory::happenedLastPlayerTurn(const E& e, const Combat& c) { return e.playerTurn == c.turnNumber - 1; }

const E* CombatHistory::currentPlay(const Combat& c, const Card* card) const {
  if (!card) return nullptr;
  for (auto it = entries.rbegin(); it != entries.rend(); ++it)
    if (it->kind == E::CardPlayStarted && it->card == card) return &*it;
  (void)c;
  return nullptr;
}

E& CombatHistory::add(const Combat& c, E::Kind k, Creature* actor) {
  E e;
  e.kind = k;
  e.round = c.roundNumber;
  e.side = c.currentSide;
  e.playerTurn = c.turnNumber;
  e.actor = actor;
  entries.push_back(std::move(e));
  return entries.back();
}

static void fillPlay(E& e, const CardPlay& cp) {
  e.card = cp.card;
  e.other = cp.target;
  e.playIndex = cp.playIndex;
  e.playCount = cp.playCount;
  e.autoPlay = cp.autoPlay;
  e.amount = cp.energySpent;  // CardPlay.Resources.EnergyValue
}

void CombatHistory::cardPlayStarted(const Combat& c, const CardPlay& cp) {
  E& e = add(c, E::CardPlayStarted, c.player);
  fillPlay(e, cp);
  e.playSeq = ++lastPlaySeq;
}

void CombatHistory::cardPlayFinished(const Combat& c, const CardPlay& cp) {
  const E* started = currentPlay(c, cp.card);
  int seq = started ? started->playSeq : 0;
  E& e = add(c, E::CardPlayFinished, c.player);
  fillPlay(e, cp);
  e.playSeq = seq;
  e.flag = cp.card && cp.card->has(kwEthereal);  // WasEthereal
}

void CombatHistory::cardAfflicted(const Combat& c, Card* card, const std::string& afflictionId) {
  E& e = add(c, E::CardAfflicted, c.player);
  e.card = card;
  e.id = afflictionId;
}

void CombatHistory::cardDiscarded(const Combat& c, Card* card) { add(c, E::CardDiscarded, c.player).card = card; }

void CombatHistory::cardDrawn(const Combat& c, Card* card, bool fromHandDraw) {
  E& e = add(c, E::CardDrawn, c.player);
  e.card = card;
  e.flag = fromHandDraw;
}

void CombatHistory::cardExhausted(const Combat& c, Card* card) { add(c, E::CardExhausted, c.player).card = card; }

void CombatHistory::cardGenerated(const Combat& c, Card* card, bool byPlayer) {
  E& e = add(c, E::CardGenerated, c.player);
  e.card = card;
  e.flag = byPlayer;
}

void CombatHistory::creatureAttacked(const Combat& c, Creature* attacker, const std::vector<std::vector<DamageResult>>& results) {
  E& e = add(c, E::CreatureAttacked, attacker);
  for (auto& hit : results)
    for (auto& r : hit) {
      e.amount += r.unblocked;
      ++e.hits;
    }
}

void CombatHistory::damageReceived(const Combat& c, const DamageResult& r, Creature* dealer, Card* cardSource) {
  if (c.ending) return;  // CombatManager.IsInProgress && !IsEnding
  E& e = add(c, E::DamageReceived, r.receiver);
  e.other = dealer;
  e.card = cardSource;
  e.props = r.props;
  e.unblocked = r.unblocked;
  e.blocked = r.blocked;
  e.overkill = r.overkill;
  e.killed = r.killed;
  e.fullyBlocked = r.fullyBlocked;
  e.blockBroken = r.blockBroken;
}

void CombatHistory::blockGained(const Combat& c, Creature* receiver, int amount, int props, Card* cardSource) {
  E& e = add(c, E::BlockGained, receiver);
  e.amount = amount;
  e.props = props;
  e.card = cardSource;
  // The CardPlay the C# passes along: only a card resolving in the Play pile has one.
  if (cardSource && cardSource->combat && cardSource->combat->pileOf(cardSource) == Pile::Play)
    if (const E* p = currentPlay(c, cardSource)) e.playSeq = p->playSeq;
}

void CombatHistory::energySpent(const Combat& c, int amount) { add(c, E::EnergySpent, c.player).amount = amount; }

void CombatHistory::monsterPerformedMove(const Combat& c, Creature* monster, const std::string& moveId) {
  add(c, E::MonsterPerformedMove, monster).id = moveId;
}

void CombatHistory::orbChanneled(const Combat& c, const std::string& orbId) { add(c, E::OrbChanneled, c.player).id = orbId; }

void CombatHistory::potionUsed(const Combat& c, const std::string& potionId, Creature* target) {
  E& e = add(c, E::PotionUsed, c.player);
  e.id = potionId;
  e.other = target;
}

void CombatHistory::powerReceived(const Combat& c, Power* power, int amount, Creature* applier) {
  E& e = add(c, E::PowerReceived, power->owner);
  e.power = power;
  e.id = power->id;
  e.amount = amount;
  e.other = applier;
}

void CombatHistory::starsModified(const Combat& c, int amount) { add(c, E::StarsModified, c.player).amount = amount; }

void CombatHistory::summoned(const Combat& c, int amount) { add(c, E::Summoned, c.player).amount = amount; }

}  // namespace sts
