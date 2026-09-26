// Combat engine: Hook.*, CreatureCmd/PowerCmd/CardPileCmd and the
// CombatManager turn loop, reduced to one local player.
#include <algorithm>

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
  co_await move->perform(targets);
  machine.performedFirstMove = true;
}

Task<> Monster::attack(int damage, int hits) {
  cmd::Attack a;
  a.damagePerHit = damage;
  a.hits = hits;
  a.attacker = creature;
  a.allOpponents = true;  // FromMonster -> TargetingAllOpponents
  co_await a.execute(*combat);
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
  for (auto& r : run->relics) out.push_back(r.get());
  for (Card* c : allCards()) out.push_back(c);
  for (auto* e : enemies) {
    if (e->removed) continue;
    for (auto& p : e->powers) out.push_back(p.get());
    out.push_back(e->monster.get());
  }
  return out;
}

Dec Combat::modifyDamage(Creature* target, Creature* dealer, Dec dmg, int props, Card* src) {
  // Hook.ModifyDamageInternal: all additive, then all multiplicative.
  auto ls = listeners();
  for (Model* m : ls) dmg += m->modifyDamageAdditive(target, dmg, props, dealer, src);
  for (Model* m : ls) dmg *= m->modifyDamageMultiplicative(target, dmg, props, dealer, src);
  return dmg;
}

Dec Combat::modifyBlock(Creature* target, Dec block, int props, Card* src) {
  auto ls = listeners();
  for (Model* m : ls) block += m->modifyBlockAdditive(target, block, props, src);
  for (Model* m : ls) block *= m->modifyBlockMultiplicative(target, block, props, src);
  return block;
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

bool Combat::canPlay(Card* c, std::string* reason) {
  if (!playerPhase || over || ending) return false;
  if (c->has(kwUnplayable) || (c->cost < 0 && !c->costsX)) { if (reason) *reason = "UNPLAYABLE"; return false; }
  if (!c->costsX && energyCost(c) > energy) { if (reason) *reason = "ENERGY"; return false; }
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

// CreatureCmd.Damage (no pets/Osty in this build, so unblocked damage always
// lands on the original target).
Task<std::vector<DamageResult>> damage(std::vector<Creature*> targets, Dec amount, int props, Creature* dealer, Card* src) {
  std::vector<DamageResult> results;
  if (targets.empty()) co_return results;
  if (dealer && dealer->dead()) {
    for (auto* t : targets) results.push_back({t, props});
    co_return results;
  }
  Combat& c = *targets[0]->combat;
  for (Creature* target : targets) {
    if (target->dead()) continue;
    Dec modified = c.modifyDamage(target, dealer, amount, props, src);
    Dec blocked = (props & kUnblockable) ? Dec(0) : dmin(Dec(target->block), modified);
    target->block -= blocked.toInt();
    Dec unblocked = dmax(modified - blocked, 0);
    for (Model* m : c.listeners()) unblocked = m->modifyHpLostAfterOsty(target, unblocked, props, dealer, src);

    // Creature.LoseHpInternal
    DamageResult r;
    r.receiver = target;
    r.props = props;
    int before = target->hp;
    if (target->isPlayer && c.run->devGod) unblocked = 0;  // developer menu: invincible
    bool killed = target->hp > 0 && unblocked >= Dec(target->hp);
    int n = std::clamp(unblocked.toInt(), 0, 999999999);
    target->hp = std::max(target->hp - n, 0);
    r.unblocked = before - target->hp;
    r.killed = killed;
    r.overkill = killed ? std::max(n - before, 0) : 0;
    r.blocked = blocked.toInt();
    r.blockBroken = target->block <= 0 && blocked > Dec(0);
    r.fullyBlocked = !(props & kUnblockable) && (blocked > Dec(0) || target->block > 0) && unblocked.toInt() == 0;

    if (r.fullyBlocked) {
      c.push({VisualEvent::Blocked, target, 0});
    } else if (r.unblocked + r.overkill > 0 || modified == Dec(0)) {
      c.push({VisualEvent::Damage, target, r.unblocked});
      if (r.unblocked > 0 && target != dealer) { target->hitFlash = 1.f; target->shake = 1.f; }
    }
    results.push_back(r);
  }

  for (auto& r : results)
    if (r.unblocked > 0)
      for (Model* m : c.listeners()) co_await m->afterCurrentHpChanged(r.receiver, Dec(-r.unblocked));
  std::vector<Creature*> killedCreatures;
  for (auto& r : results) {
    Creature* t = r.receiver;
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
  // Hook.ModifyPowerAmountReceived (Artifact blocks debuffs).
  std::vector<Model*> receivedModifiers;
  for (Model* m : c->listeners()) {
    Dec out = amount;
    if (m->tryModifyPowerAmountReceived(p, target, amount, applier, out)) { amount = out; receivedModifiers.push_back(m); }
  }
  if (amount == Dec(0)) {
    for (Model* m : receivedModifiers) co_await m->afterModifyingPowerAmountReceived(p);
    c->graveyard.push_back(std::move(power));  // listeners may still hold it
    co_return;
  }
  if (Power* existing = target->power(p->id)) {
    // PowerCmd.Apply -> ModifyAmount on the stack already there.
    co_await modifyPowerAmount(existing, amount, applier, src, silent);
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
  c.discard.clear();
  c.draw = list;
  c.push({VisualEvent::Shuffle, nullptr, (int)list.size()});
  co_await wait(0.3);
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

Task<> gainEnergy(Combat& c, int amount) {
  c.energy = std::max(0, c.energy + amount);
  co_await wait(0.1);
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

Task<> addStatusCards(Combat& c, std::string cardId, Pile to, int count) {
  for (int i = 0; i < count; ++i) {
    Card* card = c.addCard(db::card(cardId));
    co_await moveCard(c, card, to);
  }
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
        if (a.kind == PlayerAction::DevKillAll) {
          playerPhase = false;
          co_await cmd::kill(aliveEnemies());
          co_await checkWinCondition();
          playerPhase = !over;
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
    co_await checkWinCondition();
  } else {
    co_await checkWinCondition();
    if (!over) co_await executeEnemyTurn();
  }
}

Task<> Combat::setupPlayerTurn() {
  // Hook.ShouldPlayerResetEnergy -> ResetEnergy
  energy = maxEnergyNow();
  cardsPlayedThisTurn = 0;
  for (Model* m : listeners()) co_await m->afterEnergyReset();
  for (Model* m : listeners()) co_await m->beforeHandDraw();
  Dec handDraw = 5;
  for (Model* m : listeners()) handDraw = m->modifyHandDraw(handDraw);
  if (turnNumber == 1) {
    // Innate cards go on top of the draw pile.
    std::vector<Card*> innate;
    for (Card* c : draw) if (c->has(kwInnate)) innate.push_back(c);
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
  co_await checkWinCondition();
}

Task<> Combat::endPlayerTurnPhaseTwo() {
  // FlushPlayerHand
  std::vector<Card*> flush;
  for (Card* c : hand) if (!c->has(kwRetain)) flush.push_back(c);
  for (Card* c : flush) { removeFromPiles(c); discard.push_back(c); }
  if (!flush.empty()) co_await wait(0.25);
  for (Card* c : allCards()) c->clearCostMods(Card::kEndOfTurn);  // PlayerCombatState.EndOfTurnCleanup
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
  int spent = 0;
  if (!autoPlay) {
    spent = card->costsX ? energy : energyCost(card);
    energy -= std::max(spent, 0);
  }
  if (card->costsX) card->xValue = spent;
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
  int playCount = 1;
  std::vector<Model*> countModifiers;
  for (Model* m : listeners()) {
    int n = m->modifyCardPlayCount(card, target, playCount);
    if (n != playCount) countModifiers.push_back(m);
    playCount = n;
  }
  for (Model* m : countModifiers) co_await m->afterModifyingCardPlayCount(card);
  if (spent > 0) for (Model* m : listeners()) co_await m->afterEnergySpent(card, spent);

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
    if (player->alive() && !over)
      for (Model* m : listeners()) co_await m->afterCardPlayed(cp);
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
