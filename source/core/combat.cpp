// Combat engine: Hook.*, CreatureCmd/PowerCmd/CardPileCmd and the
// CombatManager turn loop, reduced to one local player.
#include <algorithm>
#include <cstdio>

#include "game.h"

namespace sts {

namespace {
constexpr int kMaxHand = 10;  // CardPile.MaxCardsInHand

// Cmd.CustomScaledWait(fast, normal): we always run at "normal" pacing.
WaitFor scaledWait(double /*fast*/, double normal) { return wait(normal); }
}  // namespace

// ---------------------------------------------------------------- state machine

std::string RandomBranchState::nextState(Monster& m, Rng& rng) {
  auto& sm = m.machine;
  auto weightOf = [&](const Branch& b) -> float {
    float w = 1.f;
    MonsterState* st = sm.states[b.stateId].get();
    if (b.repeat == MoveRepeat::UseOnlyOnce) {
      if (std::find(sm.stateLog.begin(), sm.stateLog.end(), st) != sm.stateLog.end()) w = 0.f;
    } else if (b.repeat != MoveRepeat::CanRepeatForever) {
      float limit = b.repeat == MoveRepeat::CannotRepeat ? 1.f : (float)b.maxTimes;
      int n = (int)sm.stateLog.size();
      w = (float)n < limit ? 1.f : 0.f;
      for (int i = 0; (float)n >= limit && (float)i < limit && n - i > 0; ++i) {
        if (sm.stateLog[n - 1 - i] != st) { w = 1.f; break; }
      }
    }
    if (b.cooldown > 0) {
      int seen = 0;
      for (int i = (int)sm.stateLog.size() - 1; i >= 0 && seen < b.cooldown; --i) {
        if (!sm.stateLog[i]->isMove()) continue;
        ++seen;
        if (sm.stateLog[i]->id == b.stateId) return 0.f;
      }
    }
    return w * b.weight();
  };
  float total = 0;
  for (auto& b : branches) total += weightOf(b);
  float r = rng.nextFloat(total);
  for (auto& b : branches) {
    r -= weightOf(b);
    if (r <= 0.f) return b.stateId;
  }
  return branches.back().stateId;
}

MoveState* MoveStateMachine::rollMove(Monster& m, Rng& rng) {
  // MonsterMoveStateMachine.FindNextMoveState
  if (current->canTransitionAway() && !(!performedFirstMove && current->isMove())) {
    MonsterState* logged = nullptr;
    do {
      std::string next = current->nextState(m, rng);
      setCurrent(next.empty() ? initial : states[next].get());
      if (!logged && current->shouldAppearInLogs()) logged = current;
    } while (!current->isMove());
    if (logged) stateLog.push_back(logged);
  }
  return static_cast<MoveState*>(current);
}

Task<> Monster::performMove() {
  MoveState* move = nextMove;
  if (!move) co_return;
  std::vector<Creature*> targets;
  if (combat->player->alive()) targets.push_back(combat->player);
  // Moves without an attack play the creature's cast animation up front;
  // attacks trigger theirs from Attack::execute. A stunned creature does nothing visible.
  bool attacks = false, onlyStun = !move->intents.empty();
  for (auto& in : move->intents) { attacks |= in.kind == Intent::Attack; onlyStun &= in.kind == Intent::Stun; }
  if (!attacks && !onlyStun) {
    bool debuff = false;
    for (auto& in : move->intents) debuff |= in.kind == Intent::Debuff || in.kind == Intent::DebuffStrong;
    combat->push({VisualEvent::Anim, creature, 0, debuff ? "Debuff" : "Cast"});
  }
  move->performedAtLeastOnce = true;
  attackLog.clear();
  co_await move->perform(targets);
  // Debug (STS_ASC_CHECK=1): the damage dealt by the move must be the damage its intent showed.
  static const bool check = getenv("STS_ASC_CHECK") != nullptr;
  if (check && !attackLog.empty())
    for (auto& in : move->intents)
      if (in.kind == Intent::Attack) {
        if (attackLog[0].first != in.damage || attackLog[0].second != in.hits)
          printf("ASC CHECK %s %s: intent %dx%d, attack %dx%d\n", id.c_str(), move->id.c_str(), in.damage, in.hits,
                 attackLog[0].first, attackLog[0].second);
        break;
      }
  machine.performedFirstMove = true;
}

Task<> Monster::attack(int damage, int hits) {
  attackLog.push_back({damage, hits});
  cmd::Attack a;
  a.damagePerHit = damage;
  a.hits = hits;
  a.attacker = creature;
  a.allOpponents = true;  // FromMonster -> TargetingAllOpponents
  co_await a.execute(*combat);
}

int Monster::asc(AscensionLevel level, int ascValue, int base) const {
  return combat && combat->run && combat->run->hasAscension(level) ? ascValue : base;
}

Task<> Monster::gainBlock(int amount) { co_await cmd::gainBlock(creature, amount, kMove, nullptr); }

void Monster::setMoveImmediate(MoveState* s, bool force) {
  if (!nextMove || nextMove->canTransitionAway() || force) {
    nextMove = s;
    machine.setCurrent(s);
  }
}

// Creature.StunInternal
void Monster::stun(std::function<Task<>(const std::vector<Creature*>&)> stunMove, std::string nextMoveId) {
  if (!creature || creature->dead()) return;
  if (nextMoveId.empty() && !machine.stateLog.empty()) nextMoveId = machine.stateLog.back()->id;
  auto s = std::make_unique<MoveState>();
  s->id = "STUNNED";
  if (stunMove) s->perform = std::move(stunMove);
  else s->perform = [](const std::vector<Creature*>&) -> Task<> { co_return; };
  Intent i;
  i.kind = Intent::Stun;
  s->intents = {i};
  s->followUpId = nextMoveId;
  s->mustPerformOnce = true;
  MoveState* raw = s.get();
  machine.transient.push_back(std::move(s));
  setMoveImmediate(raw);
}

// ---------------------------------------------------------------- cards

Dec Card::calculatedDamage() {
  // CalculatedDamageVar: CalculationBase + ExtraDamage * multiplier.
  Dec base = val("CalculationBase");
  Dec extra = val("ExtraDamage");
  int mult = calcMultiplier ? calcMultiplier(this) : 0;
  return base + extra * Dec(mult);
}

Dec Card::calculatedBlock() {
  int mult = calcMultiplier ? calcMultiplier(this) : 0;
  return val("CalculationBase") + val("CalculationExtra") * Dec(mult);
}

// ---------------------------------------------------------------- combat helpers

Rng& Combat::rng(const char* stream) { return run->rng(stream); }

std::vector<Card*>& Combat::pile(Pile p) {
  switch (p) {
    case Pile::Draw: return draw;
    case Pile::Hand: return hand;
    case Pile::Discard: return discard;
    case Pile::Exhaust: return exhaust;
    default: return play;
  }
}

Pile Combat::pileOf(Card* c) {
  for (Pile p : {Pile::Draw, Pile::Hand, Pile::Discard, Pile::Exhaust, Pile::Play}) {
    auto& v = pile(p);
    if (std::find(v.begin(), v.end(), c) != v.end()) return p;
  }
  return Pile::None;
}

void Combat::removeFromPiles(Card* c) {
  for (Pile p : {Pile::Draw, Pile::Hand, Pile::Discard, Pile::Exhaust, Pile::Play}) {
    auto& v = pile(p);
    v.erase(std::remove(v.begin(), v.end(), c), v.end());
  }
}

std::vector<Card*> Combat::allCards() {
  std::vector<Card*> out;
  for (Pile p : {Pile::Hand, Pile::Draw, Pile::Discard, Pile::Exhaust, Pile::Play})
    for (Card* c : pile(p)) out.push_back(c);
  return out;
}

Card* Combat::addCard(std::unique_ptr<Card> c) {
  c->combat = this;
  cardStore.push_back(std::move(c));
  return cardStore.back().get();
}

Creature* Combat::createEnemy(std::unique_ptr<Monster> m) {
  auto cr = std::make_unique<Creature>();
  cr->side = Side::Enemy;
  cr->combat = this;
  m->combat = this;  // ascension-dependent values (Monster::asc) need the run
  // Creature.SetUniqueMonsterHpValue: prefer an HP no other enemy on the side has.
  std::vector<int> options;
  for (int hp = m->minHp(); hp <= m->maxHp(); ++hp) {
    bool taken = false;
    for (auto* other : enemies) if (!other->removed && other->maxHp == hp) taken = true;
    if (!taken) options.push_back(hp);
  }
  Rng& hpRng = rng("Niche");
  int hp = options.empty() ? hpRng.nextInt(m->minHp(), m->maxHp() + 1) : hpRng.nextItem(options);
  cr->hp = cr->maxHp = hp;
  m->creature = cr.get();
  m->combat = this;
  m->buildMoves();          // MonsterModel.SetUpForCombat
  m->spawnedThisTurn = true;
  cr->name = m->locKey;
  cr->monster = std::move(m);
  enemies.push_back(cr.get());
  ownedEnemies.push_back(std::move(cr));
  return enemies.back();
}

std::vector<Creature*> Combat::aliveEnemies() {
  std::vector<Creature*> out;
  for (auto* e : enemies) if (e->alive() && !e->removed) out.push_back(e);
  return out;
}
std::vector<Creature*> Combat::hittableEnemies() { return aliveEnemies(); }

// CombatState.IterateHookListeners: per creature, allies then enemies: its
// powers, then relics and every combat card (player) or the monster model.
std::vector<Model*> Combat::listeners() {
  std::vector<Model*> out;
  // Hook.IterateCombatHookListeners: nothing listens once combat is ending.
  if (ending) return out;
  out.reserve(64);
  for (auto& p : player->powers) out.push_back(p.get());
  if (osty && !osty->removed) for (auto& p : osty->powers) out.push_back(p.get());
  for (auto& r : run->relics) out.push_back(r.get());
  for (Card* c : allCards()) {
    out.push_back(c);
    if (c->enchantment) out.push_back(c->enchantment.get());  // card, (affliction,) enchantment
  }
  for (auto* e : enemies) {
    if (e->removed) continue;
    for (auto& p : e->powers) out.push_back(p.get());
    out.push_back(e->monster.get());
  }
  return out;
}

Dec Combat::modifyDamage(Creature* target, Creature* dealer, Dec dmg, int props, Card* src) {
  // Hook.ModifyDamageInternal: the card's enchantment first (additive, then multiplicative),
  // then all listeners additive, then all multiplicative.
  if (src && src->enchantment) {
    dmg += src->enchantment->enchantDamageAdditive(dmg, props);
    dmg *= src->enchantment->enchantDamageMultiplicative(dmg, props);
  }
  auto ls = listeners();
  for (Model* m : ls) dmg += m->modifyDamageAdditive(target, dmg, props, dealer, src);
  for (Model* m : ls) dmg *= m->modifyDamageMultiplicative(target, dmg, props, dealer, src);
  return dmg;
}

Dec Combat::modifyBlock(Creature* target, Dec block, int props, Card* src) {
  if (src && src->enchantment) {
    block += src->enchantment->enchantBlockAdditive(block);
    block *= src->enchantment->enchantBlockMultiplicative(block);
  }
  auto ls = listeners();
  for (Model* m : ls) block += m->modifyBlockAdditive(target, block, props, src);
  for (Model* m : ls) block *= m->modifyBlockMultiplicative(target, block, props, src);
  return block;
}

Dec Combat::modifyOrbValue(Orb* orb, Dec amount) {
  for (Model* m : listeners()) amount = m->modifyOrbValue(orb, amount);
  return amount;
}

int Combat::modifyOrbPassiveTriggerCount(Orb* orb, int count) {
  for (Model* m : listeners()) count = m->modifyOrbPassiveTriggerCount(orb, count);
  return count;
}

// OrbModel.TriggerPassive.
Task<> Orb::triggerPassive(Creature* target) {
  if (!owner || !owner->combat) co_return;
  Combat* c = owner->combat;
  int triggerCount = c->modifyOrbPassiveTriggerCount(this, 1);
  for (Model* m : c->listeners()) co_await m->afterModifyingOrbPassiveTriggerCount(this);
  for (int i = 0; i < triggerCount; ++i) {
    co_await passive(target);
    // PORT NOTE: CustomScaledWait(0.1, 0.25) collapses to a fixed wait (single player: always "IsMe").
    co_await scaledWait(0.1, 0.25);
  }
}

int Combat::energyCost(Card* c) {
  if (c->cost < 0 || c->costsX) return c->cost;
  int cost = c->costWithLocalMods();
  auto ls = listeners();
  for (Model* m : ls) cost = m->modifyEnergyCost(c, cost);
  for (Model* m : ls) cost = m->modifyEnergyCostLate(c, cost);
  return std::max(0, cost);
}

int Combat::maxEnergyNow() {
  Dec e = maxEnergy;
  for (Model* m : listeners()) e = m->modifyMaxEnergy(e);
  return std::max(0, e.toInt());
}

// CardModel.GetStarCostWithModifiers.
int Combat::starCost(Card* c) {
  if (c->costsStarsX) return stars;
  if (c->starCost < 0) return c->starCost;
  int cost = c->starCost;
  for (Model* m : listeners()) cost = m->modifyStarCost(c, cost);
  return cost;
}

bool Combat::canPlay(Card* c, std::string* reason) {
  if (!playerPhase || over || ending) return false;
  if (c->has(kwUnplayable) || (c->cost < 0 && !c->costsX)) { if (reason) *reason = "UNPLAYABLE"; return false; }
  // PlayerCombatState.HasEnoughResourcesFor: excess energy cost can be paid with stars (never
  // true today; the hook exists for a future Regent relic/power).
  int need = c->costsX ? 0 : energyCost(c);
  int starsNeed = std::max(0, starCost(c));
  if (!c->costsX && need > energy) {
    bool payExcess = false;
    for (Model* m : listeners()) if (m->shouldPayExcessEnergyCostWithStars()) { payExcess = true; break; }
    if (payExcess) { starsNeed += (need - energy) * 2; need = energy; }
  }
  if (!c->costsX && need > energy) { if (reason) *reason = "ENERGY"; return false; }
  if (starsNeed > stars) { if (reason) *reason = "STARS"; return false; }
  if (c->target == TargetType::AnyEnemy && aliveEnemies().empty()) return false;
  for (Model* m : listeners())
    if (!m->shouldPlay(c)) { if (reason) *reason = "UNPLAYABLE"; return false; }
  return true;
}

bool Combat::isValidTarget(Card* c, Creature* t) {
  if (c->target != TargetType::AnyEnemy) return t == nullptr;
  return t && t->side == Side::Enemy && t->alive() && !t->removed;
}

// ---------------------------------------------------------------- commands

namespace cmd {

Task<std::vector<DamageResult>> damage(Creature* target, Dec amount, int props, Creature* dealer, Card* src) {
  co_return co_await damage(std::vector<Creature*>{target}, amount, props, dealer, src);
}

// CreatureCmd.Damage. Block is taken from a pet's owner (Creature::petOwner), not the pet
// itself; the unblocked amount runs the BeforeOsty hp-loss phase, is then possibly redirected
// to a different creature (Hook.ModifyUnblockedDamageTarget -- DieForYouPower sends a powered
// hit meant for the player to Osty instead), then the AfterOsty phase. If the damage was
// redirected, the redirected creature's overkill (e.g. Osty dying to a bigger hit than its HP)
// spills over onto the original target as its own AfterOsty-phased hit, exactly as the C# does.
Task<std::vector<DamageResult>> damage(std::vector<Creature*> targets, Dec amount, int props, Creature* dealer, Card* src) {
  std::vector<DamageResult> results;
  if (targets.empty()) co_return results;
  if (dealer && dealer->dead()) {
    for (auto* t : targets) results.push_back({t, props});
    co_return results;
  }
  Combat& c = *targets[0]->combat;

  // Creature.LoseHpInternal
  auto loseHpInternal = [&](Creature* target, Dec unblocked, int blockedAmt, bool blockBroken, bool fullyBlocked) {
    DamageResult r;
    r.receiver = target;
    r.props = props;
    int before = target->hp;
    if (target->isPlayer && c.run->devGod) unblocked = 0;  // developer menu: invincible
    bool killed = target->hp > 0 && unblocked >= Dec(target->hp);
    int n = std::clamp(unblocked.toInt(), 0, 999999999);
    target->hp = std::max(target->hp - n, 0);
    // Hook.ShouldDie (Fairy in a Bottle): the player survives and heals.
    bool saved = killed && target->isPlayer && c.run->preventDeath();
    if (saved) killed = false;
    r.unblocked = saved ? before : before - target->hp;
    r.killed = killed;
    r.overkill = killed ? std::max(n - before, 0) : 0;
    r.blocked = blockedAmt;
    r.blockBroken = blockBroken;
    r.fullyBlocked = fullyBlocked;
    return r;
  };
  auto pushVisual = [&](const DamageResult& r, Creature* target, Dec modified) {
    if (r.fullyBlocked) {
      c.push({VisualEvent::Blocked, target, 0});
    } else if (r.unblocked + r.overkill > 0 || modified == Dec(0)) {
      c.push({VisualEvent::Damage, target, r.unblocked});
      if (r.unblocked > 0 && target != dealer) { target->hitFlash = 1.f; target->shake = 1.f; }
    }
  };

  for (Creature* originalTarget : targets) {
    if (originalTarget->dead()) continue;
    Dec modified = c.modifyDamage(originalTarget, dealer, amount, props, src);
    Creature* blockOwner = originalTarget->petOwner ? originalTarget->petOwner : originalTarget;
    Dec blocked = (props & kUnblockable) ? Dec(0) : dmin(Dec(blockOwner->block), modified);
    blockOwner->block -= blocked.toInt();
    Dec unblocked = dmax(modified - blocked, 0);
    for (Model* m : c.listeners()) unblocked = m->modifyHpLostBeforeOsty(originalTarget, unblocked, props, dealer, src);
    Creature* redirected = originalTarget;
    for (Model* m : c.listeners()) redirected = m->modifyUnblockedDamageTarget(redirected, unblocked, props, dealer);
    for (Model* m : c.listeners()) unblocked = m->modifyHpLostAfterOsty(redirected, unblocked, props, dealer, src);

    bool wasBlockBroken = originalTarget->block <= 0 && blocked > Dec(0);
    bool wasFullyBlocked = !(props & kUnblockable) && (blocked > Dec(0) || originalTarget->block > 0) && unblocked.toInt() == 0;

    DamageResult r = loseHpInternal(redirected, unblocked, blocked.toInt(), wasBlockBroken, wasFullyBlocked);
    pushVisual(r, redirected, modified);
    results.push_back(r);
    if (redirected != originalTarget) {
      Dec overflow = Dec(r.overkill);
      for (Model* m : c.listeners()) overflow = m->modifyHpLostAfterOsty(originalTarget, overflow, props, dealer, src);
      DamageResult r2;
      if (overflow > Dec(0)) {
        r2 = loseHpInternal(originalTarget, overflow, blocked.toInt(), wasBlockBroken, wasFullyBlocked);
      } else {
        r2.receiver = originalTarget;
        r2.props = props;
        r2.blocked = blocked.toInt();
        r2.blockBroken = wasBlockBroken;
        r2.fullyBlocked = wasFullyBlocked;
      }
      pushVisual(r2, originalTarget, modified);
      results.push_back(r2);
    }
  }

  for (auto& r : results)
    if (r.unblocked > 0)
      for (Model* m : c.listeners()) co_await m->afterCurrentHpChanged(r.receiver, Dec(-r.unblocked));
  std::vector<Creature*> killedCreatures;
  for (auto& r : results) c.damageHistory.push_back({c.roundNumber, c.currentSide, r.receiver, dealer, props});
  for (auto& r : results) {
    Creature* t = r.receiver;
    for (Model* m : c.listeners()) co_await m->afterDamageGiven(dealer, r, props, t, src);
    if (r.killed && t->dead()) {
      killedCreatures.push_back(t);
    } else {
      for (Model* m : c.listeners()) co_await m->afterDamageReceived(t, r, props, dealer, src);
    }
  }
  co_await kill(killedCreatures);
  co_await scaledWait(0.1, 0.2);
  co_return results;
}

Task<> kill(std::vector<Creature*> creatures) {
  for (Creature* cr : creatures) {
    Combat& c = *cr->combat;
    cr->hp = 0;  // Kill works on living creatures too (minions dying with their leader)
    c.push({VisualEvent::Death, cr, 0});
    for (Model* m : c.listeners()) co_await m->afterDeath(cr);
    std::vector<Creature*> teammates;
    if (cr->side == Side::Enemy)
      for (auto* e : c.enemies) if (e != cr && e->alive() && !e->removed) teammates.push_back(e);
    bool primary = cr->isPrimaryEnemy();
    bool leaves = !cr->isPlayer;
    for (Model* m : c.listeners()) leaves = leaves && m->shouldCreatureBeRemovedFromCombatAfterDeath(cr);
    // Creature.RemoveAllPowersAfterDeath. Hook snapshots may still point at
    // these, so they are parked rather than freed.
    auto ls = c.listeners();
    std::vector<Power*> removed;
    for (auto it = cr->powers.begin(); it != cr->powers.end();) {
      bool drop = (*it)->removedAfterOwnerDeath();
      for (Model* m : ls) drop = drop && m->shouldPowerBeRemovedOnDeath(it->get());
      if (!drop) { ++it; continue; }
      removed.push_back(it->get());
      c.graveyard.push_back(std::move(*it));
      it = cr->powers.erase(it);
    }
    for (Power* p : removed) co_await p->afterRemoved(cr);
    if (!leaves && !cr->isPlayer) c.stayingDead.push_back(cr);
    if (cr->isPlayer) {
      c.over = true;
      c.won = false;
      c.ending = true;
    } else if (primary && !teammates.empty() &&
               std::all_of(teammates.begin(), teammates.end(), [](Creature* t) { return t->isSecondaryEnemy(); })) {
      co_await kill(teammates);  // minions leave with the last primary enemy
    }
  }
  if (!creatures.empty()) co_await wait(0.35);
  for (Creature* cr : creatures) {
    if (cr->isPlayer) continue;
    auto& stay = cr->combat->stayingDead;
    auto it = std::find(stay.begin(), stay.end(), cr);
    if (it != stay.end()) stay.erase(it);  // stays in the room, dead
    else cr->removed = true;
  }
}

Task<Dec> gainBlock(Creature* cr, Dec amount, int props, Card* src, bool fast) {
  Combat* c = cr->combat;
  if (!c || c->ending || cr->dead()) co_return Dec(0);
  Dec modified = dmax(c->modifyBlock(cr, amount, props, src), 0);
  if (modified > Dec(0)) {
    cr->block = std::min(cr->block + modified.toInt(), 999999999);
    c->push({VisualEvent::Block, cr, modified.toInt()});
    co_await (fast ? scaledWait(0, 0.03) : scaledWait(0.1, 0.25));
  }
  for (Model* m : c->listeners()) co_await m->afterBlockGained(cr, modified, props, src);
  co_return modified;
}

Task<> heal(Creature* cr, Dec amount) {
  int before = cr->hp;
  cr->hp = std::min(cr->hp + amount.toInt(), cr->maxHp);
  if (cr->combat) cr->combat->push({VisualEvent::Heal, cr, cr->hp - before});
  if (cr->hp != before && cr->combat)
    for (Model* m : cr->combat->listeners()) co_await m->afterCurrentHpChanged(cr, Dec(cr->hp - before));
}

// PowerCmd.Apply(PowerModel, ...) for a fresh instance.
Task<> applyPower(std::unique_ptr<Power> power, Creature* target, Dec amount, Creature* applier, Card* src, bool silent) {
  Combat* c = target->combat;
  if (!c || c->ending || amount == Dec(0)) co_return;
  Power* p = power.get();
  p->applier = applier;
  for (Model* m : c->listeners()) co_await m->beforePowerAmountChanged(p, amount, target, applier, src);
  // Hook.ModifyPowerAmountGiven: additive pass, then multiplicative pass.
  std::vector<Model*> givenModifiers;
  if (applier && applier->combat == c) {
    for (Model* m : c->listeners()) {
      Dec d = m->modifyPowerAmountGivenAdditive(p, applier, amount, target, src);
      amount = amount + d;
      if (!(d == Dec(0))) givenModifiers.push_back(m);
    }
    for (Model* m : c->listeners()) {
      Dec f = m->modifyPowerAmountGivenMultiplicative(p, applier, amount, target, src);
      amount = amount * f;
      if (!(f == Dec(1))) givenModifiers.push_back(m);
    }
  }
  // Hook.ModifyPowerAmountReceived (Artifact blocks debuffs).
  std::vector<Model*> receivedModifiers;
  for (Model* m : c->listeners()) {
    Dec out = amount;
    if (m->tryModifyPowerAmountReceived(p, target, amount, applier, out)) { amount = out; receivedModifiers.push_back(m); }
  }
  if (amount == Dec(0)) {
    for (Model* m : givenModifiers) co_await m->afterModifyingPowerAmountGiven(p);
    for (Model* m : receivedModifiers) co_await m->afterModifyingPowerAmountReceived(p);
    c->graveyard.push_back(std::move(power));  // listeners may still hold it
    co_return;
  }
  if (Power* existing = target->power(p->id)) {
    // PowerCmd.Apply -> ModifyAmount on the stack already there.
    co_await modifyPowerAmount(existing, amount, applier, src, silent);
    for (Model* m : givenModifiers) co_await m->afterModifyingPowerAmountGiven(existing);
    for (Model* m : receivedModifiers) co_await m->afterModifyingPowerAmountReceived(existing);
    co_return;
  }
  co_await p->beforeApplied(target, amount, applier, src);
  if (target->power(p->id)) {
    // beforeApplied can itself stack the same power (it never does for the
    // powers in this build, but keep PowerCmd's contract).
    co_await modifyPowerAmount(target->power(p->id), amount, applier, src, silent);
    co_return;
  }
  p->owner = target;
  p->amount = amount.toInt();
  target->powers.push_back(std::move(power));
  if (!silent) {
    p->flash = 1.f;
    c->push({p->type() == PowerType::Buff ? VisualEvent::PowerUp : VisualEvent::PowerDown, target, p->amount, p->locKey});
    co_await scaledWait(0.1, 0.25);
  }
  if (target->side == Side::Player && p->type() == PowerType::Debuff) p->skipNextDurationTick = true;
  for (Model* m : givenModifiers) co_await m->afterModifyingPowerAmountGiven(p);
  co_await p->afterApplied(applier, src);
  for (Model* m : c->listeners()) co_await m->afterPowerAmountChanged(p, amount, applier, src);
}

Task<int> modifyPowerAmount(Power* p, Dec offset, Creature* applier, Card* src, bool silent) {
  Creature* owner = p->owner;
  Combat* c = owner ? owner->combat : nullptr;
  if (!c || c->ending) co_return 0;
  int newAmount = p->amount + offset.toInt();
  int change = newAmount - p->amount;
  p->amount = newAmount;
  if (change != 0 && !silent) {
    p->flash = 1.f;
    if (change > 0) c->push({VisualEvent::PowerUp, owner, change, p->locKey});
  }
  if (offset.toInt() != 0)
    for (Model* m : c->listeners()) co_await m->afterPowerAmountChanged(p, offset, applier, src);
  if (p->shouldRemoveDueToAmount()) co_await removePower(p);
  if (!silent) co_await scaledWait(0.1, 0.25);
  co_return newAmount;
}

Task<> removePower(Power* p) {
  Creature* owner = p->owner;
  if (!owner) co_return;
  std::unique_ptr<Power> keep;
  for (auto it = owner->powers.begin(); it != owner->powers.end(); ++it) {
    if (it->get() == p) { keep = std::move(*it); owner->powers.erase(it); break; }
  }
  if (!keep) co_return;
  Power* raw = keep.get();
  owner->combat->graveyard.push_back(std::move(keep));
  co_await scaledWait(0.2, 0.4);
  co_await raw->afterRemoved(owner);
}

Task<> decrement(Power* p) { co_await modifyPowerAmount(p, -1, nullptr, nullptr); }

Task<> tickDownDuration(Power* p) {
  if (p->skipNextDurationTick) { p->skipNextDurationTick = false; co_return; }
  co_await decrement(p);
}

Task<> shuffle(Combat& c) {
  // CardPileCmd.Shuffle: discard + draw, StableShuffle (sort, then Fisher-Yates).
  std::vector<Card*> list = c.discard;
  list.insert(list.end(), c.draw.begin(), c.draw.end());
  std::stable_sort(list.begin(), list.end(), [](Card* a, Card* b) { return a->id < b->id; });
  c.rng("Shuffle").shuffle(list);
  for (Model* m : c.listeners()) m->modifyShuffleOrder(list, false);  // Hook.ModifyShuffleOrder (PerfectFit)
  c.discard.clear();
  c.draw = list;
  c.push({VisualEvent::Shuffle, nullptr, (int)list.size()});
  co_await wait(0.3);
  for (Model* m : c.listeners()) co_await m->afterShuffle();
}

Task<std::vector<Card*>> drawCards(Combat& c, Dec count, bool fromHandDraw) {
  std::vector<Card*> result;
  if (c.ending) co_return result;
  for (Model* m : c.listeners())
    if (!m->shouldDraw(fromHandDraw)) co_return result;
  int requested = count > Dec(0) ? count.ceilInt() : 0;
  for (int i = 0; i < requested; ++i) {
    if ((int)c.hand.size() >= kMaxHand || c.ending) break;
    if (c.draw.empty() && c.discard.empty()) break;
    if (c.draw.empty()) co_await shuffle(c);
    if (c.draw.empty()) break;
    Card* card = c.draw.front();
    c.draw.erase(c.draw.begin());
    c.hand.push_back(card);
    result.push_back(card);
    ++c.cardsDrawnThisCombat;
    co_await wait(0.08);
    for (Model* m : c.listeners()) co_await m->afterCardDrawn(card, fromHandDraw);
  }
  co_return result;
}

Task<> moveCard(Combat& c, Card* card, Pile to, bool top) {
  c.removeFromPiles(card);
  Pile dest = to;
  if (dest == Pile::Hand && (int)c.hand.size() >= kMaxHand) dest = Pile::Discard;
  auto& v = c.pile(dest);
  // The draw pile's "top" is its front; everything else appends.
  if (dest == Pile::Draw && top) v.insert(v.begin(), card); else v.push_back(card);
  co_return;
}

Task<> exhaustCard(Combat& c, Card* card, bool causedByEthereal) {
  c.removeFromPiles(card);
  c.exhaust.push_back(card);
  c.push({VisualEvent::CardExhaust, nullptr, 0, card->locKey});
  co_await wait(0.2);
  for (Model* m : c.listeners()) co_await m->afterCardExhausted(card, causedByEthereal);
}

Task<> discardCards(Combat& c, std::vector<Card*> cards, int drawAfter) {
  if (c.over || c.ending || cards.empty()) co_return;
  std::vector<Card*> sly;
  for (Card* k : cards) {
    if (k->isSlyThisTurn()) sly.push_back(k);
    c.removeFromPiles(k);
    c.discard.push_back(k);
    c.discardHistory.push_back({c.roundNumber, c.currentSide, k});
    for (Model* m : c.listeners()) co_await m->afterCardDiscarded(k);
  }
  if (drawAfter > 0) co_await drawCards(c, drawAfter);
  for (Card* k : sly) co_await autoPlay(c, k);  // AutoPlayType.SlyDiscard
}

Task<> discardCard(Combat& c, Card* card) { co_await discardCards(c, {card}); }

Task<> loseBlock(Creature* target, Dec amount) {
  if (!target || target->dead() || amount <= Dec(0) || (target->combat && (target->combat->over || target->combat->ending))) co_return;
  target->block = std::max(0, target->block - amount.toInt());
}

// OrbCmd.AddSlots / RemoveSlots.
Task<> addOrbSlots(Combat& c, int amount) {
  if (c.over || c.ending) co_return;
  amount = std::min(10 - c.orbCapacity, amount);  // OrbQueue.maxCapacity = 10
  c.orbCapacity += amount;
  co_return;
}

void removeOrbSlots(Combat& c, int amount) {
  if (c.over || c.ending) return;
  amount = std::min(c.orbCapacity, amount);
  c.orbCapacity = std::max(0, c.orbCapacity - amount);
  // OrbQueue.RemoveCapacity: excess orbs are dropped from the back (no AfterOrbEvoked/RemoveInternal).
  while ((int)c.orbQueue.size() > c.orbCapacity) c.orbQueue.pop_back();
}

// OrbCmd.Evoke (private in the C#; shared by evokeNextOrb/evokeLastOrb below).
static Task<> evokeOrbInternal(Combat& c, Orb* orbPtr, bool dequeue) {
  if (c.over || c.ending) co_return;
  if (c.orbQueue.empty()) co_return;
  std::unique_ptr<Orb> holder;  // keeps `orbPtr` alive across the awaits below when dequeued
  bool removed = false;
  if (dequeue) {
    auto it = std::find_if(c.orbQueue.begin(), c.orbQueue.end(), [&](auto& p) { return p.get() == orbPtr; });
    if (it != c.orbQueue.end()) { holder = std::move(*it); c.orbQueue.erase(it); removed = true; }
  }
  std::vector<Creature*> targets = co_await orbPtr->evoke();
  if (orbPtr->owner && orbPtr->owner->combat) {  // still in combat (CombatState != null)
    for (Model* m : c.listeners()) co_await m->afterOrbEvoked(orbPtr, targets);
    if (removed) orbPtr->removedFromQueue = true;
  }
}

Task<> evokeNextOrb(Combat& c, bool dequeue) {
  if (c.orbQueue.empty()) co_return;
  co_await evokeOrbInternal(c, c.orbQueue.front().get(), dequeue);
}

Task<> evokeLastOrb(Combat& c, bool dequeue) {
  if (c.orbQueue.empty()) co_return;
  co_await evokeOrbInternal(c, c.orbQueue.back().get(), dequeue);
}

// OrbCmd.Channel(orb, player): `orb` is a freshly made, not-yet-owned instance.
Task<> channelOrb(Combat& c, std::unique_ptr<Orb> orb) {
  if (c.over || c.ending) co_return;
  // A character with no base orb slots (e.g. a relic granting one orb to a non-Defect) gets a
  // slot the first time it channels one.
  if (c.run->character().orbSlots == 0 && c.orbCapacity == 0) co_await addOrbSlots(c, 1);
  orb->owner = c.player;
  if ((int)c.orbQueue.size() >= c.orbCapacity) co_await evokeNextOrb(c);
  if (c.orbCapacity == 0) co_return;  // OrbQueue.TryEnqueue: Capacity == 0 -> false, nothing else happens
  Orb* raw = orb.get();
  c.orbQueue.push_back(std::move(orb));
  // PORT NOTE: CustomScaledWait(0.1, 0.25) collapses to a fixed wait (single player: always "IsMe").
  co_await scaledWait(0.1, 0.25);
  if (raw->id == "LightningOrb") ++c.lightningOrbsChanneled;
  for (Model* m : c.listeners()) co_await m->afterOrbChanneled(raw);
}

// OrbCmd.Passive.
Task<> orbPassive(Combat& c, Orb* orb, Creature* target, bool countAffectedByHooks) {
  if (c.over || c.ending) co_return;
  if (countAffectedByHooks) co_await orb->triggerPassive(target);
  else co_await orb->passive(target);
}

Task<> gainEnergy(Combat& c, int amount) {
  c.energy = std::max(0, c.energy + amount);
  co_await wait(0.1);
}

// PlayerCmd.GainStars: the only star command that checks ShouldGainStars and fires AfterStarsGained.
Task<> gainStars(Combat& c, int amount) {
  if (c.ending) co_return;
  for (Model* m : c.listeners()) if (!m->shouldGainStars(amount)) co_return;
  int before = c.stars;
  c.stars = std::max(0, c.stars + amount);
  if (c.stars > before) c.starsGainedThisTurn += c.stars - before;
  for (Model* m : c.listeners()) co_await m->afterStarsGained(amount);
}

// PlayerCmd.LoseStars: no hook (AfterStarsSpent only fires when a card's star cost is paid).
Task<> loseStars(Combat& c, int amount) {
  if (c.ending) co_return;
  c.stars = std::max(0, c.stars - amount);
  co_return;
}

// PlayerCmd.SetStars.
Task<> setStars(Combat& c, int amount) {
  if (c.ending) co_return;
  if (c.stars < amount) co_await gainStars(c, amount - c.stars);
  else if (c.stars > amount) co_await loseStars(c, c.stars - amount);
}

Task<> gainMaxHp(Creature* cr, int amount) {
  cr->maxHp += amount;
  cr->hp += amount;
  if (cr->combat) cr->combat->push({VisualEvent::Heal, cr, amount});
  co_await wait(0.2);
}

Task<> loseMaxHp(Creature* cr, int amount) {
  cr->maxHp = std::max(1, cr->maxHp - amount);
  cr->hp = std::min(cr->hp, cr->maxHp);
  co_await wait(0.2);
}

Task<Card*> addGeneratedCard(Combat& c, std::unique_ptr<Card> card, Pile to, bool top) {
  Card* raw = c.addCard(std::move(card));
  ++c.cardsGeneratedThisCombat;  // CombatHistory.CardGenerated
  co_await moveCard(c, raw, to, top);
  for (Model* m : c.listeners()) co_await m->afterCardEnteredCombat(raw);
  co_return raw;
}

Task<> autoPlay(Combat& c, Card* card, Creature* target) {
  if (card->target == TargetType::AnyEnemy && (!target || target->dead())) target = c.rng("CombatTargets").nextItem(c.aliveEnemies());
  co_await c.playCard(card, target, true, false);
}

Task<Card*> transform(Combat& c, Card* card, std::unique_ptr<Card> into) {
  Pile p = c.pileOf(card);
  Card* raw = c.addCard(std::move(into));
  auto& v = c.pile(p);
  auto it = std::find(v.begin(), v.end(), card);
  if (it != v.end()) *it = raw;
  co_await wait(0.2);
  co_return raw;
}

void upgradeCard(Card* card) { card->upgrade(); }

Task<> addStatusCards(Combat& c, std::string cardId, Pile to, int count, bool byPlayer) {
  // CardPileCmd.AddGeneratedCardsToCombat: add them all, then Hook.AfterCardGeneratedForCombat for
  // each (Smokestack, RocketPunch, ...). byPlayer: the player's own card made them (creator == Owner).
  std::vector<Card*> added;
  for (int i = 0; i < count; ++i) {
    Card* card = c.addCard(db::card(cardId));
    card->createdByPlayer = byPlayer;
    co_await moveCard(c, card, to);
    added.push_back(card);
  }
  for (Card* card : added)
    for (Model* m : c.listeners()) co_await m->afterCardEnteredCombat(card);
  c.push({VisualEvent::Banner, c.player, count, cardId});
  co_await wait(0.3);
}

Task<std::vector<Card*>> selectCards(Combat& c, std::string prompt, std::vector<Card*> options, int minCount, int maxCount) {
  if (options.empty()) co_return std::vector<Card*>{};
  c.choice.prompt = std::move(prompt);
  c.choice.options = std::move(options);
  c.choice.minCount = minCount;
  c.choice.maxCount = maxCount;
  c.choice.active = true;
  auto picked = co_await c.choice.result.next();
  c.choice.active = false;
  co_return picked;
}

Task<> autoPlayFromDrawPile(Combat& c, int count, bool forceExhaust) {
  for (int i = 0; i < count; ++i) {
    if (c.draw.empty()) co_await shuffle(c);
    if (c.draw.empty()) co_return;
    Card* card = c.draw.front();
    Creature* target = nullptr;
    if (card->target == TargetType::AnyEnemy) target = c.rng("CombatTargets").nextItem(c.aliveEnemies());
    co_await c.playCard(card, target, true, forceExhaust);
  }
}

Task<Creature*> addMonster(Combat& c, std::unique_ptr<Monster> m) {
  Creature* cr = c.createEnemy(std::move(m));
  // CombatManager.AfterCreatureAdded
  co_await cr->monster->afterAddedToRoom();
  if (c.currentSide == Side::Player) cr->monster->rollMove(c.rng("MonsterAi"));
  for (Model* l : c.listeners()) co_await l->afterCreatureAddedToCombat(cr);
  co_return cr;
}

Task<> Attack::execute(Combat& c) {
  if (!attacker || attacker->dead() || c.ending) co_return;
  for (int i = 0; i < hits; ++i) {
    if (attacker->dead() || c.ending) break;
    std::vector<Creature*> valid;
    if (single) {
      if (single->alive()) valid.push_back(single);
    } else if (attacker->side == Side::Enemy) {
      if (c.player->alive()) valid.push_back(c.player);
    } else {
      valid = c.aliveEnemies();
    }
    if (valid.empty()) break;
    if (i == 0) {
      // amount carries hits * 1000 + damage so the UI can pick heavy/multi-hit animations.
      c.push({VisualEvent::Anim, attacker, hits * 1000 + std::min(999, damagePerHit.toInt()),
              source && source->cost >= 2 ? "AttackHeavy" : "Attack"});
      co_await wait(0.15);
    }
    Creature* target = nullptr;
    if (random) target = c.rng("CombatTargets").nextItem(valid);
    else if (valid.size() == 1) target = valid[0];
    Dec amount = calcFrom ? calcFrom->calculatedDamage() : damagePerHit;
    std::vector<Creature*> ts = target ? std::vector<Creature*>{target} : valid;
    results.push_back(co_await damage(ts, amount, props, attacker, source));
  }
  for (Model* m : c.listeners()) co_await m->afterAttack(attacker);  // Hook.AfterAttack (once per Execute)
}

}  // namespace cmd

// ---------------------------------------------------------------- turn loop

Task<> Combat::runCombat() {
  // CombatManager.StartCombatInternal
  inProgress = true;
  for (auto* e : enemies) co_await e->monster->afterAddedToRoom();
  for (Model* m : listeners()) co_await m->beforeCombatStart();
  banner = "战斗开始";
  bannerTime = 1.2f;
  co_await wait(0.8);
  while (!over) {
    Side side = currentSide;
    co_await startTurn();
    if (over) break;
    if (side == Side::Player) {
      playerPhase = true;
      for (;;) {
        PlayerAction a = co_await actions.next();
        if (over) break;
        if (a.kind == PlayerAction::EndTurn) break;
        if (a.kind == PlayerAction::UsePotion) {
          playerPhase = false;
          co_await run->usePotion(a.potionSlot, a.target);
          playerPhase = !over;
          if (over) break;
          continue;
        }
        if (a.kind == PlayerAction::DevKillAll) {
          playerPhase = false;
          co_await cmd::kill(aliveEnemies());
          co_await checkWinCondition();
          playerPhase = !over;
          if (over) break;
          continue;
        }
        if (a.card && canPlay(a.card) && (a.card->target != TargetType::AnyEnemy || isValidTarget(a.card, a.target))) {
          playerPhase = false;
          co_await playCard(a.card, a.target);
          playerPhase = !over;
        }
        if (over) break;
      }
      playerPhase = false;
      if (over) break;
      co_await endPlayerTurnPhaseOne();
      if (over) break;
      co_await endPlayerTurnPhaseTwo();
      if (over) break;
      switchSides();
    }
    // Enemy side: StartTurn runs the whole enemy turn and switches back.
  }
  inProgress = false;
  if (won) {
    // CombatManager.EndCombatInternal: run-level hooks, relics still listen.
    for (Model* m : run->listeners()) co_await m->afterCombatEnd();
    for (Model* m : run->listeners()) co_await m->afterCombatVictory();
  }
}

Task<> Combat::startTurn() {
  std::vector<Creature*> starting;
  if (currentSide == Side::Player) starting.push_back(player);
  else for (auto* e : enemies) if (!e->removed) starting.push_back(e);

  for (auto* cr : starting)
    for (auto& p : cr->powers) p->amountOnTurnStart = p->amount;
  for (Model* m : listeners()) co_await m->beforeSideTurnStart(currentSide, starting);

  if (currentSide == Side::Player) {
    if (turnNumber > 1) { banner = "玩家回合"; bannerTime = 1.0f; }
    for (auto* e : enemies)
      // Dead creatures still in the room (Decimillipede segments) roll too: they reattach.
      if (!e->removed) e->monster->rollMove(rng("MonsterAi"));
  } else {
    banner = "敌人回合";
    bannerTime = 1.0f;
  }
  co_await scaledWait(0.5, 0.8);

  // Creature.AfterTurnStart: block clears at the start of your own turn,
  // except on the player's first turn.
  for (auto* cr : starting) {
    if (cr->isPlayer && turnNumber == 1) continue;
    bool clear = true;
    for (Model* m : listeners()) clear = clear && m->shouldClearBlock(cr);
    if (clear) {
      cr->block = 0;
      for (Model* m : listeners()) co_await m->afterBlockCleared(cr);
    }
  }

  if (currentSide == Side::Player) co_await setupPlayerTurn();
  for (Model* m : listeners()) co_await m->afterSideTurnStart(currentSide, starting);

  if (currentSide == Side::Player) {
    // PlayerCombatState.OrbQueue.AfterTurnStart (Plasma's energy passive). Snapshot the queue
    // first, as the C# does (`Orbs.ToList()`), and bail if the combat ends mid-loop.
    std::vector<Orb*> orbSnapshot;
    for (auto& o : orbQueue) orbSnapshot.push_back(o.get());
    for (Orb* o : orbSnapshot) {
      if (over || ending) break;
      co_await o->afterTurnStartOrbTrigger();
    }
    co_await checkWinCondition();
    // CombatManager.RunAutoPrePlayPhase: Hook.AfterAutoPrePlayPhaseEntered (Imbued auto-plays).
    if (!over) for (Model* m : listeners()) co_await m->afterAutoPrePlayPhaseEntered();
  } else {
    co_await checkWinCondition();
    if (!over) co_await executeEnemyTurn();
  }
}

Task<> Combat::setupPlayerTurn() {
  // Hook.ShouldPlayerResetEnergy -> ResetEnergy, else AddMaxEnergyToCurrent (Ice Cream)
  bool resetEnergy = true;
  for (Model* m : listeners()) resetEnergy = resetEnergy && m->shouldResetEnergy();
  energy = resetEnergy ? maxEnergyNow() : energy + maxEnergyNow();
  cardsPlayedThisTurn = 0;
  skillsFinishedThisTurn = 0;
  attackPlaysFinishedThisTurn = 0;
  cardPlaysFinishedThisTurn = 0;
  starsGainedThisTurn = 0;
  shivPlaysFinishedThisTurn = 0;
  energySpentThisTurn = 0;
  for (Model* m : listeners()) co_await m->afterEnergyReset();
  for (Model* m : listeners()) co_await m->beforeHandDraw();
  Dec handDraw = 5;
  for (Model* m : listeners()) handDraw = m->modifyHandDraw(handDraw);
  if (turnNumber == 1) {
    // Enchanted cards that start at the bottom (Imbued), then innate cards on top of the draw pile.
    std::vector<Card*> bottom;
    for (Card* c : draw) if (c->enchantment && c->enchantment->shouldStartAtBottomOfDrawPile()) bottom.push_back(c);
    for (Card* c : bottom) { draw.erase(std::find(draw.begin(), draw.end(), c)); draw.push_back(c); }
    std::vector<Card*> innate;
    for (Card* c : draw) if (c->has(kwInnate) && std::find(bottom.begin(), bottom.end(), c) == bottom.end()) innate.push_back(c);
    for (Card* c : innate) { draw.erase(std::find(draw.begin(), draw.end(), c)); draw.insert(draw.begin(), c); }
    handDraw = Dec(std::min(std::max(handDraw.toInt(), (int)innate.size()), kMaxHand));
  }
  co_await cmd::drawCards(*this, handDraw, true);
  for (Model* m : listeners()) co_await m->afterPlayerTurnStart();
}

Task<> Combat::executeEnemyTurn() {
  auto list = enemies;
  for (Creature* e : list) {
    // Dead creatures still in the room (illusions) take their turn too: they revive.
    if (e->removed) continue;
    if (!e->monster->spawnedThisTurn) co_await e->monster->performMove();
    co_await wait(0.25);
    co_await checkWinCondition();
    if (over) co_return;
  }
  co_await endEnemyTurn();
}

Task<> Combat::endEnemyTurn() {
  std::vector<Creature*> es;
  for (auto* e : enemies) if (!e->removed) es.push_back(e);
  for (Model* m : listeners()) co_await m->beforeSideTurnEndEarly(Side::Enemy, es);
  for (Model* m : listeners()) co_await m->beforeSideTurnEnd(Side::Enemy, es);
  for (Model* m : listeners()) co_await m->afterSideTurnEnd(Side::Enemy, es);
  co_await checkWinCondition();
  if (!over) switchSides();
}

Task<> Combat::endPlayerTurnPhaseOne() {
  std::vector<Creature*> ps{player};
  for (Model* m : listeners()) co_await m->afterAutoPostPlayPhaseEntered();
  for (Model* m : listeners()) co_await m->beforeSideTurnEndEarly(Side::Player, ps);
  for (Model* m : listeners()) co_await m->beforeSideTurnEnd(Side::Player, ps);
  // DoTurnEnd: PlayerCombatState.OrbQueue.BeforeTurnEnd first (Lightning/Frost/Dark/Glass passives).
  {
    std::vector<Orb*> orbSnapshot;
    for (auto& o : orbQueue) orbSnapshot.push_back(o.get());
    for (Orb* o : orbSnapshot) {
      if (over || ending) break;
      co_await o->beforeTurnEndOrbTrigger();
    }
  }
  if (co_await checkWinCondition()) co_return;
  // DoTurnEnd: ethereal cards exhaust, then turn-end-in-hand cards resolve one by one
  // (through the play pile) and go to the bottom of the discard, or exhaust if ethereal.
  std::vector<Card*> ethereal, turnEnd;
  for (Card* c : hand) {
    if (c->hasTurnEndInHandEffect()) turnEnd.push_back(c);
    else if (c->has(kwEthereal)) ethereal.push_back(c);
  }
  for (Card* c : ethereal) co_await cmd::exhaustCard(*this, c, true);
  for (Card* c : turnEnd) {
    if (over || ending) break;
    removeFromPiles(c);
    play.push_back(c);
    co_await wait(0.3);
    co_await c->onTurnEndInHand();
    if (pileOf(c) != Pile::Play) continue;
    if (c->has(kwEthereal)) co_await cmd::exhaustCard(*this, c, true);
    else { removeFromPiles(c); discard.push_back(c); }
  }
  if (!(co_await checkWinCondition())) for (Model* m : listeners()) co_await m->beforeFlush();  // Hook.BeforeFlush
}

Task<> Combat::endPlayerTurnPhaseTwo() {
  // FlushPlayerHand
  std::vector<Card*> flush, retained;
  bool flushHand = true;
  for (Model* m : listeners()) flushHand = flushHand && m->shouldFlush();
  for (Card* c : hand) {
    if (!flushHand || c->shouldRetainThisTurn()) retained.push_back(c);
    else flush.push_back(c);
  }
  for (Card* c : flush) { removeFromPiles(c); discard.push_back(c); }
  if (!flush.empty()) co_await wait(0.25);
  for (Model* m : listeners()) co_await m->afterFlush(flush, retained);  // Hook.AfterFlush
  for (Card* c : allCards()) {  // PlayerCombatState.EndOfTurnCleanup -> CardModel.EndOfTurnCleanup
    c->clearCostMods(Card::kEndOfTurn);
    c->singleTurnRetain = c->singleTurnSly = false;
  }
  std::vector<Creature*> ps{player};
  for (Model* m : listeners()) co_await m->afterSideTurnEnd(Side::Player, ps);
}

void Combat::switchSides() {
  if (currentSide == Side::Player) {
    currentSide = Side::Enemy;
  } else {
    currentSide = Side::Player;
    ++roundNumber;
    ++turnNumber;
  }
  for (auto* e : enemies) if (e->monster) e->monster->spawnedThisTurn = false;
}

Task<bool> Combat::checkWinCondition() {
  if (over) co_return true;
  bool anyPrimary = false;
  for (auto* e : enemies) if (!e->removed && e->alive() && e->isPrimaryEnemy()) anyPrimary = true;
  if (!anyPrimary) {
    for (Model* m : listeners())
      if (m->shouldStopCombatFromEnding()) co_return false;  // Hook.ShouldStopCombatFromEnding
    ending = true;
    over = true;
    won = true;
    co_return true;
  }
  co_return false;
}

Task<> Combat::playCard(Card* card, Creature* target, bool autoPlay, bool forceExhaust) {
  // CardModel.SpendResources: X-cost cards spend everything and capture X.
  int spent = 0, starsSpent = 0;
  if (!autoPlay) {
    spent = card->costsX ? energy : energyCost(card);
    starsSpent = card->costsStarsX ? stars : std::max(0, starCost(card));
    if (!card->costsX && spent > energy) {
      bool payExcess = false;
      for (Model* m : listeners()) if (m->shouldPayExcessEnergyCostWithStars()) { payExcess = true; break; }
      if (payExcess) { starsSpent += (spent - energy) * 2; spent = energy; }
    }
    energy -= std::max(spent, 0);
    stars = std::max(0, stars - starsSpent);
  }
  card->lastStarsSpent = starsSpent;
  if (card->costsX) {
    card->xValue = spent;
    for (Model* m : listeners()) card->xValue = m->modifyXValue(card, card->xValue);  // Hook.ModifyXValue
  }
  if (card->costsStarsX) {
    card->starXValue = starsSpent;
    for (Model* m : listeners()) card->starXValue = m->modifyXValue(card, card->starXValue);  // Hook.ModifyXValue
  }
  ++cardsPlayedThisTurn;
  removeFromPiles(card);
  play.push_back(card);

  // GetResultLocationForCardPlay + Hook.ModifyCardPlayResultLocation
  Pile result = Pile::Discard;
  if (card->type == CardType::Power) result = Pile::None;
  else if (card->has(kwExhaust) || forceExhaust) result = Pile::Exhaust;
  if (card->isDupe) result = Pile::None;
  for (Model* m : listeners()) result = m->modifyCardPlayResultLocation(card, autoPlay, result);

  // Hook.ModifyCardPlayCount
  int playCount = 1 + card->enchantedReplayCount();  // GetEnchantedReplayCount + 1
  std::vector<Model*> countModifiers;
  for (Model* m : listeners()) {
    int n = m->modifyCardPlayCount(card, target, playCount);
    if (n != playCount) countModifiers.push_back(m);
    playCount = n;
  }
  for (Model* m : countModifiers) co_await m->afterModifyingCardPlayCount(card);
  if (spent > 0) energySpentThisTurn += spent;
  if (spent > 0) for (Model* m : listeners()) co_await m->afterEnergySpent(card, spent);
  if (starsSpent > 0) for (Model* m : listeners()) co_await m->afterStarsSpent(starsSpent);  // CardModel.SpendStars

  if (card->type != CardType::Attack) push({VisualEvent::Anim, player, 0, "Cast"});
  co_await wait(autoPlay ? 0.3 : 0.1);
  for (int i = 0; i < playCount; ++i) {
    if (over || ending || player->dead()) break;
    if (card->target == TargetType::AnyEnemy && (!target || target->dead())) {
      if (i == 0) break;
      target = rng("CombatTargets").nextItem(aliveEnemies());
      if (!target) break;
    }
    CardPlay cp{card, target, result, autoPlay, spent, i, playCount};
    for (Model* m : listeners()) co_await m->beforeCardPlayed(cp);
    co_await card->onPlay(cp);
    // CardModel.OnPlayWrapper: the enchantment's OnPlay follows the card's own effect.
    if (card->enchantment && player->alive()) co_await card->enchantment->onPlay(cp);
    ++cardPlaysFinishedThisCombat;
    ++cardPlaysFinishedThisTurn;  // CardPlayFinished entry precedes Hook.AfterCardPlayed
    if (player->alive() && !over) {
      for (Model* m : listeners()) co_await m->afterCardPlayed(cp);
      for (Model* m : listeners()) co_await m->afterCardPlayedLate(cp);  // Hook.AfterCardPlayedLate
    }
    if (card->type == CardType::Skill) ++skillsFinishedThisTurn;  // CardPlayFinishedEntry (LunarBlast)
    if (card->type == CardType::Attack) ++attackPlaysFinishedThisTurn;
    if (card->tags & tagShiv) ++shivPlaysFinishedThisTurn;
    if (card->has(kwEthereal)) ++etherealPlaysFinished;  // BansheesCry
  }
  card->clearCostMods(Card::kWhenPlayed);  // AfterCardPlayedCleanup

  // Move to the result pile if nothing else moved it.
  if (pileOf(card) == Pile::Play) {
    removeFromPiles(card);
    if (result == Pile::Exhaust) co_await cmd::exhaustCard(*this, card);
    else if (result == Pile::Discard) discard.push_back(card);
    else if (result == Pile::Hand) co_await cmd::moveCard(*this, card, Pile::Hand);
    else if (result == Pile::Draw) co_await cmd::moveCard(*this, card, Pile::Draw);
  }
  co_await checkWinCondition();
}

}  // namespace sts
