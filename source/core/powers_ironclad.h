// Powers used by the uncommon/rare Ironclad cards (translated from MegaCrit.Sts2.Core.Models.Powers).
#pragma once
#include <map>

#include "powers.h"

namespace sts {

struct AggressionPower : Power {
  POWER_HEADER(AggressionPower, "AGGRESSION_POWER")
  Task<> beforeSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner)) co_return;
    std::vector<Card*> attacks;
    for (auto* c : owner->combat->discard) if (c->type == CardType::Attack) attacks.push_back(c);
    owner->combat->rng("CombatCardSelection").shuffle(attacks);
    int n = std::min<int>(amount, (int)attacks.size());
    for (int i = 0; i < n; ++i) {
      Card* card = attacks[i];
      co_await cmd::moveCard(*owner->combat, card, Pile::Hand);
      if (card->upgradable()) cmd::upgradeCard(card);
    }
  }
};

struct BarricadePower : Power {
  POWER_HEADER(BarricadePower, "BARRICADE_POWER")
  StackType stackType() const override { return StackType::Single; }
  bool shouldClearBlock(Creature* c) override { return owner != c; }
};

struct ColossusPower : Power {
  POWER_HEADER(ColossusPower, "COLOSSUS_POWER")
  Dec modifyDamageMultiplicative(Creature* target, Dec, int props, Creature* dealer, Card*) override {
    if (target != owner || !isPoweredAttack(props) || !dealer) return 1;
    if (!dealer->get<VulnerablePower>()) return 1;
    return Dec::lit(0.5);
  }
  Task<> afterSideTurnEnd(Side side, const std::vector<Creature*>&) override {
    if (side == Side::Enemy) co_await cmd::tickDownDuration(this);
  }
};

struct CorruptionPower : Power {
  POWER_HEADER(CorruptionPower, "CORRUPTION_POWER")
  StackType stackType() const override { return StackType::Single; }
  int modifyEnergyCostLate(Card* card, int cost) override {
    if (ownerOf(card) != owner || card->type != CardType::Skill) return cost;
    return 0;
  }
  Pile modifyCardPlayResultLocation(Card* card, bool, Pile pile) override {
    if (ownerOf(card) != owner || card->type != CardType::Skill) return pile;
    return Pile::Exhaust;
  }
};

// CrimsonMantlePower and InfernoPower carry a "SelfDamage" counter, bumped by
// their card (IncrementSelfDamage) each time it upgrades the self-hit amount.
struct CrimsonMantlePower : Power {
  POWER_HEADER(CrimsonMantlePower, "CRIMSON_MANTLE_POWER")
  int selfDamage = 0;
  void incrementSelfDamage() { ++selfDamage; }
  Task<> afterPlayerTurnStart() override {
    flash = 1.f;
    co_await cmd::damage(owner, Dec(selfDamage), kUnblockable | kUnpowered, owner, nullptr);
    co_await cmd::gainBlock(owner, Dec(amount), kUnpowered, nullptr);
  }
};

// CrueltyPower has no hooks of its own here: its ModifyVulnerableMultiplier
// effect is folded directly into VulnerablePower::modifyDamageMultiplicative
// in powers.h (looked up on the dealer by id, per docs/PORTING.md).
struct CrueltyPower : Power {
  POWER_HEADER(CrueltyPower, "CRUELTY_POWER")
};

struct DarkEmbracePower : Power {
  POWER_HEADER(DarkEmbracePower, "DARK_EMBRACE_POWER")
  int etherealCount = 0;
  Task<> afterCardExhausted(Card* card, bool causedByEthereal) override {
    if (ownerOf(card) != owner) co_return;
    if (causedByEthereal) {
      ++etherealCount;
      co_return;
    }
    co_await cmd::drawCards(*owner->combat, Dec(amount));
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner)) co_return;
    co_await cmd::drawCards(*owner->combat, Dec(amount * etherealCount));
    etherealCount = 0;
  }
};

struct DemonFormPower : Power {
  POWER_HEADER(DemonFormPower, "DEMON_FORM_POWER")
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await applyPower<StrengthPower>(owner, amount, owner, nullptr);
  }
};

struct FeelNoPainPower : Power {
  POWER_HEADER(FeelNoPainPower, "FEEL_NO_PAIN_POWER")
  Task<> afterCardExhausted(Card* card, bool) override {
    if (ownerOf(card) != owner) co_return;
    co_await cmd::gainBlock(owner, Dec(amount), kUnpowered, nullptr);
  }
};

struct FlameBarrierPower : Power {
  POWER_HEADER(FlameBarrierPower, "FLAME_BARRIER_POWER")
  Task<> afterDamageReceived(Creature* target, const DamageResult&, int props, Creature* dealer, Card*) override {
    if (target != owner || !dealer || !isPoweredAttack(props)) co_return;
    co_await cmd::damage(dealer, Dec(amount), kUnpowered, owner, nullptr);
  }
  Task<> afterSideTurnEnd(Side side, const std::vector<Creature*>&) override {
    if (owner->side != side) co_await cmd::removePower(this);
  }
};

struct FreeAttackPower : Power {
  POWER_HEADER(FreeAttackPower, "FREE_ATTACK_POWER")
  static bool inHandOrPlay(Card* c) {
    if (!c->combat) return false;
    Pile p = c->combat->pileOf(c);
    return p == Pile::Hand || p == Pile::Play;
  }
  int modifyEnergyCostLate(Card* card, int cost) override {
    if (ownerOf(card) != owner || card->type != CardType::Attack || !inHandOrPlay(card)) return cost;
    return 0;
  }
  Task<> beforeCardPlayed(const CardPlay& p) override {
    if (ownerOf(p.card) != owner || p.card->type != CardType::Attack || !inHandOrPlay(p.card)) co_return;
    co_await cmd::decrement(this);
  }
};

// HellraiserPower autoplays drawn Strikes. The C# version only chains
// indefinitely when every hittable enemy has infinite HP (a concept this
// engine doesn't model), and otherwise resets a safety counter every draw;
// PORT NOTE: since we can't detect "infinite HP", we always keep the 9-per-turn
// safety cap that the original used to stop runaway chains in that case.
struct HellraiserPower : Power {
  POWER_HEADER(HellraiserPower, "HELLRAISER_POWER")
  StackType stackType() const override { return StackType::Single; }
  int infiniteAutoPlaysThisTurn = 0;
  Task<> afterCardDrawn(Card* card, bool) override {
    if (ownerOf(card) != owner || !(card->tags & tagStrike)) co_return;
    if (infiniteAutoPlaysThisTurn >= 9) co_return;
    ++infiniteAutoPlaysThisTurn;
    co_await cmd::autoPlay(*owner->combat, card, nullptr);
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) infiniteAutoPlaysThisTurn = 0;
    return {};
  }
};

struct InfernoPower : Power {
  POWER_HEADER(InfernoPower, "INFERNO_POWER")
  int selfDamage = 0;
  void incrementSelfDamage() { ++selfDamage; }
  Task<> afterPlayerTurnStart() override {
    co_await cmd::damage(owner, Dec(selfDamage), kUnblockable | kUnpowered, owner, nullptr);
  }
  Task<> afterDamageReceived(Creature* target, const DamageResult& r, int, Creature*, Card*) override {
    if (target != owner || r.unblocked <= 0 || owner->combat->currentSide != owner->side) co_return;
    co_await cmd::damage(owner->combat->hittableEnemies(), Dec(amount), kUnpowered, owner, nullptr);
  }
};

struct JuggernautPower : Power {
  POWER_HEADER(JuggernautPower, "JUGGERNAUT_POWER")
  Task<> afterBlockGained(Creature* creature, Dec amt, int, Card*) override {
    if (amt <= Dec(0) || creature != owner) co_return;
    auto enemies = owner->combat->hittableEnemies();
    if (enemies.empty()) co_return;
    Creature* target = owner->combat->rng("CombatTargets").nextItem(enemies);
    flash = 1.f;
    co_await cmd::damage(target, Dec(amount), kUnpowered, owner, nullptr);
  }
};

struct JugglingPower : Power {
  POWER_HEADER(JugglingPower, "JUGGLING_POWER")
  int attacksPlayedThisTurn = 0;
  // Seeded from the Attack CardPlaysStarted this turn.
  Task<> afterApplied(Creature*, Card*) override {
    Combat* c = owner->combat;
    attacksPlayedThisTurn = c->history.countThisTurn(*c, CombatHistoryEntry::CardPlayStarted, [&](const CombatHistoryEntry& e) {
      return e.actor == owner && e.card->type == CardType::Attack;
    });
    return {};
  }
  Task<> beforeCardPlayed(const CardPlay& p) override {
    if (ownerOf(p.card) != owner || p.card->type != CardType::Attack) co_return;
    ++attacksPlayedThisTurn;
    if (attacksPlayedThisTurn == 3) {
      flash = 1.f;
      for (int i = 0; i < amount; ++i) {
        auto clone = p.card->clone();
        co_await cmd::addGeneratedCard(*owner->combat, std::move(clone), Pile::Hand);
      }
    }
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) attacksPlayedThisTurn = 0;
    return {};
  }
};

// ManglePower: TemporaryStrengthPower with IsPositive == false, i.e. the
// negative-Strength mirror of SetupStrikePower (powers.h).
struct ManglePower : Power {
  POWER_HEADER(ManglePower, "MANGLE_POWER")
  const char* internallyAppliedPower() const override { return "StrengthPower"; }  // ITemporaryPower
  Task<> beforeApplied(Creature* target, Dec amt, Creature* app, Card* src) override {
    co_await applyPower<StrengthPower>(target, -amt, app, src, true);
  }
  Task<> afterPowerAmountChanged(Power* p, Dec amt, Creature* app, Card* src) override {
    if (!(amt == Dec(amount)) && p == this) co_await applyPower<StrengthPower>(owner, -amt, app, src, true);
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) {
      flash = 1.f;
      Creature* o = owner;
      int a = amount;
      co_await cmd::removePower(this);
      co_await applyPower<StrengthPower>(o, a, o, nullptr);
    }
  }
};

struct NoDrawPower : Power {
  POWER_HEADER(NoDrawPower, "NO_DRAW_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  StackType stackType() const override { return StackType::Single; }
  bool shouldDraw(bool fromHandDraw) override {
    if (fromHandDraw) return true;
    flash = 1.f;
    return false;
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await cmd::removePower(this);
  }
};

struct OneTwoPunchPower : Power {
  POWER_HEADER(OneTwoPunchPower, "ONE_TWO_PUNCH_POWER")
  int modifyCardPlayCount(Card* card, Creature*, int count) override {
    if (ownerOf(card) != owner || card->type != CardType::Attack) return count;
    return count + 1;
  }
  Task<> afterModifyingCardPlayCount(Card*) override { co_await cmd::decrement(this); }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await cmd::removePower(this);
  }
};

// PlatingPower: the C# version scales its per-turn decrement by the player
// count in multiplayer (GetScaledAmountForMultiplayer / the "Decrement" var);
// PORT NOTE: single player, so that scaling factor is always 1.
struct PlatingPower : Power {
  POWER_HEADER(PlatingPower, "PLATING_POWER")
  int decrementAmount = 1;
  Task<> afterApplied(Creature*, Card*) override {
    if (owner->side == Side::Enemy) decrementAmount = 1;
    return {};
  }
  // Enemies that start combat with Plating also start combat with block.
  Task<> beforeSideTurnStart(Side side, const std::vector<Creature*>&) override {
    if (side == Side::Player && !owner->isPlayer && owner->combat->roundNumber <= 1)
      co_await cmd::gainBlock(owner, Dec(amount), kUnpowered, nullptr);
  }
  Task<> beforeSideTurnEndEarly(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) {
      flash = 1.f;
      co_await cmd::gainBlock(owner, Dec(amount), kUnpowered, nullptr);
    }
  }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    bool notFirstTurn = !owner->isPlayer || owner->combat->turnNumber != 1;
    bool notFirstEnemyRound = owner->side != Side::Enemy || owner->combat->roundNumber != 1;
    if (contains(participants, owner) && notFirstTurn && notFirstEnemyRound) {
      if (owner->side == Side::Enemy) co_await cmd::modifyPowerAmount(this, Dec(-decrementAmount), nullptr, nullptr);
      else co_await cmd::decrement(this);
    }
  }
};

struct PyrePower : Power {
  POWER_HEADER(PyrePower, "PYRE_POWER")
  Dec modifyMaxEnergy(Dec amt) override { return amt + Dec(amount); }
};

struct RagePower : Power {
  POWER_HEADER(RagePower, "RAGE_POWER")
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (ownerOf(p.card) == owner && p.card->type == CardType::Attack)
      co_await cmd::gainBlock(owner, Dec(amount), kUnpowered, nullptr);
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await cmd::removePower(this);
  }
};

struct RupturePower : Power {
  POWER_HEADER(RupturePower, "RUPTURE_POWER")
  std::map<Card*, int> playedCards;
  Task<> beforeCardPlayed(const CardPlay& p) override {
    if (ownerOf(p.card) == owner && owner->combat->currentSide == owner->side) playedCards[p.card] = 0;
    return {};
  }
  Task<> afterDamageReceived(Creature* target, const DamageResult& r, int, Creature*, Card* src) override {
    if (target != owner || r.unblocked <= 0 || owner->combat->currentSide != owner->side) co_return;
    auto it = src ? playedCards.find(src) : playedCards.end();
    if (!src || it == playedCards.end()) {
      co_await applyPower<StrengthPower>(owner, Dec(amount), owner, nullptr);
    } else {
      it->second += amount;
    }
  }
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (ownerOf(p.card) != owner) co_return;
    auto it = playedCards.find(p.card);
    if (it != playedCards.end()) {
      int value = it->second;
      playedCards.erase(it);
      co_await applyPower<StrengthPower>(owner, Dec(value), owner, nullptr);
    }
  }
};

struct StampedePower : Power {
  POWER_HEADER(StampedePower, "STAMPEDE_POWER")
  Task<> afterAutoPostPlayPhaseEntered() override {
    for (int i = 0; i < amount; ++i) {
      std::vector<Card*> items;
      for (auto* c : owner->combat->hand)
        if (c->type == CardType::Attack && !c->has(kwUnplayable)) items.push_back(c);
      if (items.empty()) continue;
      Card* card = owner->combat->rng("Shuffle").nextItem(items);
      if (card) co_await cmd::autoPlay(*owner->combat, card, nullptr);
    }
  }
};

// TankPower: the AfterApplied teammate-sharing loop (GuardedPower on other
// players) is multiplayer-only and not modeled in this single-player build.
struct TankPower : Power {
  POWER_HEADER(TankPower, "TANK_POWER")
  StackType stackType() const override { return StackType::Single; }
  Dec modifyDamageMultiplicative(Creature* target, Dec, int props, Creature*, Card*) override {
    if (target != owner || !isPoweredAttack(props)) return 1;
    return Dec::lit(1.5);
  }
};

// UnmovablePower: block from card or monster moves is doubled until the owner's card plays have
// gained block Amount times this turn (BlockGainedEntry with a CardPlay, not counting the card
// play in progress; a card's CardPlay is its current play, CombatHistory::currentPlay).
struct UnmovablePower : Power {
  POWER_HEADER(UnmovablePower, "UNMOVABLE_POWER")
  Dec modifyBlockMultiplicative(Creature* target, Dec, int props, Card* src) override {
    if (!target->isPlayer) return 1;
    if (!(props & kMove)) return 1;  // IsCardOrMonsterMove
    if (src && ownerOf(src) != owner) return 1;
    Combat* c = owner->combat;
    int current = 0;
    if (src && c->pileOf(src) == Pile::Play)
      if (auto* p = c->history.currentPlay(*c, src)) current = p->playSeq;
    int n = c->history.countThisTurn(*c, CombatHistoryEntry::BlockGained, [&](const CombatHistoryEntry& e) {
      return e.playSeq != 0 && (e.props & kMove) && e.playSeq != current;
    });
    if (n >= amount) return 1;
    return 2;
  }
};

struct ViciousPower : Power {
  POWER_HEADER(ViciousPower, "VICIOUS_POWER")
  Task<> afterPowerAmountChanged(Power* p, Dec amt, Creature* applier, Card*) override {
    if (amt <= Dec(0) || applier != owner || p->id != VulnerablePower::kId) co_return;
    flash = 1.f;
    co_await cmd::drawCards(*owner->combat, Dec(amount));
  }
};

}  // namespace sts
