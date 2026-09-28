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

// ---------------------------------------------------------------- Souls

Task<std::vector<Card*>> createSoulsInHand(Combat& c, int count) {
  std::vector<Card*> made;
  if (count <= 0 || c.over || c.ending) co_return made;
  for (int i = 0; i < count; ++i)
    made.push_back(co_await cmd::addGeneratedCard(c, db::card("Soul"), Pile::Hand));
  co_return made;
}

Task<Card*> addSoulToDrawPileRandom(Combat& c) {
  Card* raw = c.addCard(db::card("Soul"));
  int idx = c.rng("Shuffle").nextInt((int)c.draw.size() + 1);  // CardPilePosition.Random
  c.draw.insert(c.draw.begin() + idx, raw);
  for (Model* m : c.listeners()) co_await m->afterCardEnteredCombat(raw);
  co_return raw;
}

Task<> SoulboundPower::afterCardEnteredCombat(Card* card) {
  if (isAddingSoul || !owner || !owner->combat) co_return;
  if (card->id == "Soul" || card->rarity == Rarity::Status || card->rarity == Rarity::Curse) co_return;
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

void registerNecrobinder() {
  registerPowerType<DieForYouPower>();
  registerPowerType<DoomPower>();
  registerPowerType<SoulboundPower>();
  registerPowerType<NecroMasteryPower>();
  registerCardType<Soul>();
}

}  // namespace sts
