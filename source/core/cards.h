// Base class and helpers for translated cards.
#pragma once
#include "powers_ironclad.h"

namespace sts {

template <class C> void registerCardType() { db::registerCard(C().id, [] { return std::unique_ptr<Card>(new C()); }); }
template <class P> void registerPowerType() { db::registerPower(P::kId, [] { return std::unique_ptr<Power>(new P()); }); }

// ================================================================ cards

// Shared helpers for translated card bodies.
struct IroncladCard : Card {
  Creature* me() { return combat->player; }

  Task<> attack(Creature* target, Dec dmg, int hits = 1) {
    cmd::Attack a;
    a.damagePerHit = dmg;
    a.hits = hits;
    a.attacker = me();
    a.source = this;
    a.single = target;
    co_await a.execute(*combat);
  }
  Task<> attackAll(Dec dmg, int hits = 1) {
    cmd::Attack a;
    a.damagePerHit = dmg;
    a.hits = hits;
    a.attacker = me();
    a.source = this;
    a.allOpponents = true;
    co_await a.execute(*combat);
  }
  Task<> attackRandom(Dec dmg, int hits) {
    cmd::Attack a;
    a.damagePerHit = dmg;
    a.hits = hits;
    a.attacker = me();
    a.source = this;
    a.random = true;
    co_await a.execute(*combat);
  }
  Task<> attackCalculated(Creature* target) {
    cmd::Attack a;
    a.calcFrom = this;
    a.attacker = me();
    a.source = this;
    a.single = target;
    co_await a.execute(*combat);
  }
  Task<> block(Dec amount) { co_await cmd::gainBlock(me(), amount, kMove, this); }
  Task<> loseHp(Dec amount) { co_await cmd::damage(me(), amount, kUnblockable | kUnpowered | kMove, me(), this); }
  Task<> drawCards(Dec n) { co_await cmd::drawCards(*combat, n); }
};

#define CARD_HEADER(Name, Key, Cost, Type, Rar, Tgt) \
  Name() {                                           \
    id = #Name;                                      \
    locKey = Key;                                    \
    portrait = Key;                                  \
    cost = canonicalCost = Cost;                     \
    type = CardType::Type;                           \
    rarity = Rarity::Rar;                            \
    target = TargetType::Tgt;

template <class Derived> struct IroncladT : IroncladCard {
  std::unique_ptr<Card> clone() const override {
    auto c = std::make_unique<Derived>(static_cast<const Derived&>(*this));
    c->adoptEnchantment();
    return c;
  }
};


}  // namespace sts
