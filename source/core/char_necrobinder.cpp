// The Necrobinder's shared systems (X4.0), see char_necrobinder.h. The Soul token card lives
// here because SoulboundPower generates it directly.
#include "cards.h"
#include "char_necrobinder.h"

namespace sts {

namespace {

// Soul.cs: 0 cost Skill token, Exhaust, targets self, draws Cards (2, +1 upgraded).
struct Soul : IroncladT<Soul> {
  CARD_HEADER(Soul, "SOUL", 0, Skill, Token, Self)
    keywords = kwExhaust;
    addVar("Cards", 2);
  }
  Task<> onPlay(CardPlay&) override { co_await drawCards(val("Cards")); }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

// StrikeNecrobinder.cs / DefendNecrobinder.cs: same numbers as the Ironclad's Strike/Defend --
// "The only difference between the starting Strike cards are portrait, attack vfx, and color."
struct StrikeNecrobinder : IroncladT<StrikeNecrobinder> {
  CARD_HEADER(StrikeNecrobinder, "STRIKE_NECROBINDER", 1, Attack, Basic, AnyEnemy)
    tags = tagStrike;
    addVar("Damage", 6);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

struct DefendNecrobinder : IroncladT<DefendNecrobinder> {
  CARD_HEADER(DefendNecrobinder, "DEFEND_NECROBINDER", 1, Skill, Basic, Self)
    tags = tagDefend;
    addVar("Block", 5);
  }
  Task<> onPlay(CardPlay&) override { co_await block(val("Block")); }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

// Bodyguard.cs: summon Osty with 5 (+2 upgraded) HP, or raise his max HP by that amount if he's
// already alive (summonOsty handles both cases). PORT NOTE: TriggerAnim's "summonTrigger" anim
// (Necrobinder.GetSummonAnimIfApplicable) is UI, not ported.
struct Bodyguard : IroncladT<Bodyguard> {
  CARD_HEADER(Bodyguard, "BODYGUARD", 1, Skill, Basic, Self)
    addVar("Summon", 5);
  }
  Task<> onPlay(CardPlay&) override { co_await summonOsty(*combat, val("Summon").toInt()); }
  void onUpgrade() override { upgradeVar("Summon", 2); }
};

// Unleash.cs: Osty attacks for 6 (+3 upgraded) plus Osty's current HP. The dealer is Osty
// (attacker = combat->osty, not the player): Attack::execute's `!attacker || attacker->dead()`
// guard already no-ops the whole card when Osty is missing or dead, matching
// `Osty.CheckMissingWithAnim` skipping OnPlay entirely.
struct Unleash : IroncladT<Unleash> {
  CARD_HEADER(Unleash, "UNLEASH", 1, Attack, Basic, AnyEnemy)
    tags = tagOstyAttack;
    addVar("CalculationBase", 6);
    addVar("ExtraDamage", 1);
    addVar("CalculatedDamage", 0);
    calcMultiplier = [](Card* c) { return c->combat && c->combat->osty && c->combat->osty->alive() ? c->combat->osty->hp : 0; };
  }
  Task<> onPlay(CardPlay& p) override {
    cmd::Attack a;
    a.calcFrom = this;
    a.attacker = combat->osty;
    a.source = this;
    a.single = p.target;
    co_await a.execute(*combat);
  }
  void onUpgrade() override { upgradeVar("CalculationBase", 3); }
};

// BoundPhylactery.cs (Starter relic): summon Osty with 1 HP before combat starts; every turn
// after the first, summon him again for 1 (raising his max HP by 1, or resummoning him at 1 HP
// if he died) via AfterEnergyResetLate. PORT NOTE: AfterEnergyResetLate vs. AfterEnergyReset only
// matters for ordering against other Osty-existence checks (e.g. Friendship), not ported yet;
// this engine's single afterEnergyReset() hook is used instead.
struct BoundPhylactery : Relic {
  RELIC_HEADER(BoundPhylactery, "BOUND_PHYLACTERY", Starter)
    addVar("Summon", 1);
  }
  Task<> beforeCombatStart() override { co_await summonOsty(*combat, val("Summon").toInt()); }
  Task<> afterEnergyReset() override {
    if (!combat || combat->turnNumber == 1) co_return;
    co_await summonOsty(*combat, val("Summon").toInt());
  }
};

}  // namespace

// ---------------------------------------------------------------- Osty

Task<Creature*> summonOsty(Combat& c, int amount) {
  if (amount == 0) co_return c.osty;
  if (c.osty && c.osty->alive()) {
    co_await cmd::gainMaxHp(c.osty, amount);
  } else {
    bool isReviving = c.osty != nullptr;
    if (!isReviving) {
      auto cr = std::make_unique<Creature>();
      cr->side = Side::Player;
      cr->petOwner = c.player;
      cr->name = "Osty";
      cr->combat = &c;
      c.osty = cr.get();
      c.ownedOsty = std::move(cr);
      // PowerCmd.Apply<DieForYouPower>(choiceContext, osty, 1, null, null)
      co_await applyPower<DieForYouPower>(c.osty, Dec(1), nullptr, nullptr);
    }
    c.osty->maxHp = amount;  // CreatureCmd.SetMaxHp
    co_await cmd::heal(c.osty, Dec(amount));
    if (isReviving)
      for (Model* m : c.listeners()) co_await m->afterOstyRevived(c.osty);
  }
  co_return c.osty;
}

// ---------------------------------------------------------------- Doom

std::vector<Creature*> DoomPower::doomedOf(const std::vector<Creature*>& creatures) {
  std::vector<Creature*> out;
  for (Creature* cr : creatures) {
    auto* d = cr->get<DoomPower>();
    if (d && d->isOwnerDoomed()) out.push_back(cr);
  }
  return out;
}

std::vector<Creature*> DoomPower::creaturesOnSide(Side side) const {
  std::vector<Creature*> out;
  if (!owner || !owner->combat) return out;
  Combat* c = owner->combat;
  if (side == Side::Enemy) {
    for (auto* e : c->enemies) if (!e->removed) out.push_back(e);
  } else {
    if (c->player) out.push_back(c->player);
    if (c->osty && !c->osty->removed) out.push_back(c->osty);
  }
  return out;
}

bool DoomPower::shouldTrigger(const std::vector<Creature*>& participants) const {
  if (!owner || !owner->combat) return false;
  Combat* c = owner->combat;
  if (c->over || c->ending) return false;
  if (!contains(participants, owner)) return false;
  if (owner->dead()) return false;
  if (!isOwnerDoomed()) return false;
  std::vector<Creature*> doomedOnSide = doomedOf(creaturesOnSide(owner->side));
  return !doomedOnSide.empty() && doomedOnSide.front() == owner;
}

Task<> DoomPower::beforeSideTurnEnd(Side side, const std::vector<Creature*>& participants) {
  if (side != Side::Player && shouldTrigger(participants))
    co_await doomKill(*owner->combat, doomedOf(creaturesOnSide(side)));
}

Task<> DoomPower::afterSideTurnEnd(Side side, const std::vector<Creature*>& participants) {
  if (side != Side::Enemy && shouldTrigger(participants))
    co_await doomKill(*owner->combat, doomedOf(creaturesOnSide(side)));
}

Task<> doomKill(Combat& c, std::vector<Creature*> creatures) {
  if (creatures.empty()) co_return;
  co_await cmd::kill(creatures);
  for (Model* m : c.listeners()) co_await m->afterDiedToDoom(creatures);
}

// ---------------------------------------------------------------- SummonNextTurnPower (X4.2: Invoke)

Task<> SummonNextTurnPower::afterPlayerTurnStart() {
  if (amountOnTurnStart == 0 || !owner || !owner->combat) co_return;
  co_await summonOsty(*owner->combat, amount);
  co_await cmd::removePower(this);
}

// ---------------------------------------------------------------- Souls

Task<std::vector<Card*>> createSoulsInHand(Combat& c, int count) {
  std::vector<Card*> made;
  if (count <= 0 || c.over || c.ending) co_return made;
  for (int i = 0; i < count; ++i)
    made.push_back(co_await cmd::addGeneratedCard(c, db::card("Soul"), Pile::Hand));
  co_return made;
}

Task<Card*> addSoulToDrawPileRandom(Combat& c, bool upgraded) {
  Card* raw = c.addCard(db::card("Soul"));
  if (upgraded) raw->upgrade();
  int idx = c.rng("Shuffle").nextInt((int)c.draw.size() + 1);  // CardPilePosition.Random
  c.draw.insert(c.draw.begin() + idx, raw);
  for (Model* m : c.listeners()) co_await m->afterCardEnteredCombat(raw);
  co_return raw;
}

Task<> SoulboundPower::afterCardEnteredCombat(Card* card) {
  if (isAddingSoul || !owner || !owner->combat) co_return;
  if (card->id == "Soul" || !card->createdByPlayer) co_return;  // creator == Applier
  if (applier != owner->combat->player) co_return;
  isAddingSoul = true;
  for (int i = 0; i < amount; ++i) co_await addSoulToDrawPileRandom(*owner->combat);
  isAddingSoul = false;
}

// ---------------------------------------------------------------- NecroMastery

Task<> NecroMasteryPower::afterCurrentHpChanged(Creature* creature, Dec delta) {
  if (delta >= Dec(0) || !owner || !owner->combat) co_return;
  Combat* c = owner->combat;
  if (creature != c->osty || creature->petOwner != owner) co_return;
  co_await cmd::damage(c->hittableEnemies(), -delta * Dec(amount), kUnblockable | kUnpowered, owner, nullptr);
}

// ---------------------------------------------------------------- registry

void registerNecrobinderRelics();  // char_necrobinder_relics.cpp (X4.1: BigHat...UndyingSigil)
void registerNecrobinderCards();   // char_necrobinder_cards.cpp (X4.2: the Common card pool)
void registerNecrobinderUncommonCards1();  // char_necrobinder_cards_uncommon1.cpp (X4.3a)
void registerNecrobinderRareCards();       // char_necrobinder_cards_rare.cpp (X4.4)

void registerNecrobinder() {
  registerPowerType<DieForYouPower>();
  registerPowerType<DoomPower>();
  registerPowerType<SoulboundPower>();
  registerPowerType<NecroMasteryPower>();
  registerPowerType<SummonNextTurnPower>();
  registerCardType<Soul>();
  registerCardType<StrikeNecrobinder>();
  registerCardType<DefendNecrobinder>();
  registerCardType<Bodyguard>();
  registerCardType<Unleash>();
  db::registerRelic("BoundPhylactery", [] { return std::unique_ptr<Relic>(new BoundPhylactery()); });
  registerNecrobinderRelics();
  registerNecrobinderCards();
  registerNecrobinderUncommonCards1();
  registerNecrobinderRareCards();
}

}  // namespace sts
