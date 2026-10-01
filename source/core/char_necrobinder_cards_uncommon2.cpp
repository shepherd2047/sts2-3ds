// The Necrobinder's Uncommon cards, second half (X4.3b): Haunt ... Veilpiercer, in
// NecrobinderCardPool order, from Models.Cards / Models.Powers. Same style as
// char_necrobinder_cards_uncommon1.cpp (X4.3a). The powers used only by these cards are defined
// here (one class per power id).
// Engine hooks added for this package (game.h / combat.cpp): Model::afterDamageGiven (SicEmPower)
// and Model::afterCardPlayedLate (RightHandHand).
#include "cards.h"
#include "char_necrobinder.h"

namespace sts {

namespace {

bool ostyMissing(Combat& c) { return !c.osty || c.osty->dead(); }  // Osty.CheckMissingWithAnim

bool inHandOrPlay(Card* card) {
  Pile p = card->combat->pileOf(card);
  return p == Pile::Hand || p == Pile::Play;
}

// ---------------------------------------------------------------- powers

// HauntPower.cs: Buff, Counter. After the owner plays a Soul, deal Amount Unblockable/Unpowered
// damage to a random hittable enemy (Rng.CombatTargets).
struct HauntPower : Power {
  POWER_HEADER(HauntPower, "HAUNT_POWER")
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (p.card->id != "Soul" || ownerOf(p.card) != owner) co_return;
    Combat* c = owner->combat;
    auto enemies = c->hittableEnemies();
    if (enemies.empty()) co_return;
    Creature* target = c->rng("CombatTargets").nextItem(enemies);
    co_await cmd::damage(target, Dec(amount), kUnblockable | kUnpowered, nullptr, nullptr);
  }
};

// LethalityPower.cs: Buff, Counter. The first Attack played each turn deals Amount% more damage.
// Counts the Attack CardPlaysStarted this turn (the card in play counts itself) and skips a card
// replayed past its first play (CurrentPlayIndex > 0, read from its current CardPlayStarted entry).
struct LethalityPower : Power {
  POWER_HEADER(LethalityPower, "LETHALITY_POWER")
  Dec modifyDamageMultiplicative(Creature*, Dec, int props, Creature*, Card* src) override {
    if (!isPoweredAttack(props) || !src || ownerOf(src) != owner) return 1;
    Combat* c = owner->combat;
    bool inPlay = c->pileOf(src) == Pile::Play;
    if (inPlay)
      if (auto* p = c->history.currentPlay(*c, src); p && p->playIndex > 0) return 1;
    int n = c->history.countThisTurn(*c, CombatHistoryEntry::CardPlayStarted,
                                     [](const CombatHistoryEntry& e) { return e.card->type == CardType::Attack; });
    if (n > (inPlay ? 1 : 0)) return 1;
    return Dec(1) + Dec(amount) / Dec(100);
  }
};

// PagestormPower.cs: Buff, Counter. Whenever the owner draws an Ethereal card, draw Amount cards.
struct PagestormPower : Power {
  POWER_HEADER(PagestormPower, "PAGESTORM_POWER")
  Task<> afterCardDrawn(Card* card, bool) override {
    if (ownerOf(card) != owner || !card->has(kwEthereal)) co_return;
    flash = 1.f;
    co_await cmd::drawCards(*owner->combat, Dec(amount));
  }
};

// ShroudPower.cs: Buff, Counter. Whenever the owner applies Doom (any change to a Doom power it
// applied), gain Amount Unpowered Block.
struct ShroudPower : Power {
  POWER_HEADER(ShroudPower, "SHROUD_POWER")
  Task<> afterPowerAmountChanged(Power* p, Dec, Creature* app, Card*) override {
    if (app == owner && p->id == "DoomPower") co_await cmd::gainBlock(owner, Dec(amount), kUnpowered, nullptr);
  }
};

// SicEmPower.cs: Debuff, Counter. When the applier's Osty damages the owner, summon Osty for
// Amount HP (or raise his max HP); removed at the end of the owner's side turn.
struct SicEmPower : Power {
  POWER_HEADER(SicEmPower, "SIC_EM_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  Task<> afterDamageGiven(Creature* dealer, const DamageResult&, int, Creature* target, Card*) override {
    Combat* c = owner->combat;
    if (!dealer || dealer != c->osty || !dealer->petOwner || !applier || dealer->petOwner != applier || target != owner)
      co_return;
    co_await summonOsty(*c, amount);
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await cmd::removePower(this);
  }
};

// SleightOfFleshPower.cs: Buff, Counter. Whenever the owner applies a (non-temporary) debuff to an
// enemy, deal Amount Unpowered damage to that enemy.
struct SleightOfFleshPower : Power {
  POWER_HEADER(SleightOfFleshPower, "SLEIGHT_OF_FLESH_POWER")
  Task<> afterPowerAmountChanged(Power* p, Dec amt, Creature* app, Card*) override {
    if (amt == Dec(0) || p->typeForAmount(amt) != PowerType::Debuff || p->owner->side != Side::Enemy || app != owner ||
        p->isTemporary())
      co_return;
    flash = 1.f;
    co_await cmd::damage(p->owner, Dec(amount), kUnpowered, owner, nullptr);
  }
};

// VeilpiercerPower.cs: Buff, Counter. Ethereal cards in the hand (or being played) cost 0; playing
// one uses up a stack.
struct VeilpiercerPower : Power {
  POWER_HEADER(VeilpiercerPower, "VEILPIERCER_POWER")
  int modifyEnergyCostLate(Card* card, int cost) override {
    if (ownerOf(card) != owner || !card->has(kwEthereal) || !inHandOrPlay(card)) return cost;
    return 0;
  }
  Task<> beforeCardPlayed(const CardPlay& p) override {
    if (ownerOf(p.card) == owner && p.card->has(kwEthereal) && inHandOrPlay(p.card)) co_await cmd::decrement(this);
  }
};

// ---------------------------------------------------------------- cards

// Haunt.cs: Power, apply Haunt (HpLoss var, 7 (+2 upgraded)).
struct Haunt : IroncladT<Haunt> {
  CARD_HEADER(Haunt, "HAUNT", 1, Power, Uncommon, Self)
    addVar("HpLoss", 7);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<HauntPower>(me(), val("HpLoss"), me(), this); }
  void onUpgrade() override { upgradeVar("HpLoss", 2); }
};

// HighFive.cs: 2 cost, OstyAttack, AllEnemies, unplayable without Osty. Osty hits every enemy for 11
// (+2 upgraded), then 2 (+1 upgraded) Vulnerable to every hittable enemy.
struct HighFive : IroncladT<HighFive> {
  CARD_HEADER(HighFive, "HIGH_FIVE", 2, Attack, Uncommon, AllEnemies)
    tags = tagOstyAttack;
    addVar("OstyDamage", 11);
    addVar("VulnerablePower", 2);
  }
  bool shouldPlay(Card* c) override { return c != this || !ostyMissing(*combat); }  // IsPlayable
  Task<> onPlay(CardPlay&) override {
    if (ostyMissing(*combat)) co_return;
    cmd::Attack a;
    a.damagePerHit = val("OstyDamage");
    a.attacker = combat->osty;
    a.source = this;
    a.allOpponents = true;
    co_await a.execute(*combat);
    for (Creature* e : combat->hittableEnemies()) co_await applyPower<VulnerablePower>(e, val("VulnerablePower"), me(), this);
  }
  void onUpgrade() override {
    upgradeVar("OstyDamage", 2);
    upgradeVar("VulnerablePower", 1);
  }
};

// Lethality.cs: Ethereal Power, apply 50 (+25 upgraded) Lethality.
struct Lethality : IroncladT<Lethality> {
  CARD_HEADER(Lethality, "LETHALITY", 1, Power, Uncommon, Self)
    keywords = kwEthereal;
    addVar("LethalityPower", 50);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<LethalityPower>(me(), val("LethalityPower"), me(), this); }
  void onUpgrade() override { upgradeVar("LethalityPower", 25); }
};

// Melancholy.cs: 3 cost, gain 13 (+4 upgraded) Block. Whenever a creature dies and leaves combat
// while this card is in a combat pile, it costs 1 (Energy var) less for the rest of the combat.
struct Melancholy : IroncladT<Melancholy> {
  CARD_HEADER(Melancholy, "MELANCHOLY", 3, Skill, Uncommon, Self)
    addVar("Block", 13);
    addVar("Energy", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await block(val("Block")); }
  Task<> afterDeath(Creature* dead) override {
    if (!combat) co_return;
    for (Model* m : combat->listeners())  // wasRemovalPrevented (Osty, illusions)
      if (!m->shouldCreatureBeRemovedFromCombatAfterDeath(dead)) co_return;
    if (combat->pileOf(this) == Pile::None) co_return;
    addThisCombat(-val("Energy").toInt());
  }
  void onUpgrade() override { upgradeVar("Block", 4); }
};

// NoEscape.cs: apply Doom to the target: CalculationBase 10 (+5 upgraded) + CalculationExtra 5 per
// full DoomThreshold (10) Doom it already has.
// PORT NOTE: the CalculatedDoom var is computed in onPlay; Card::calcMultiplier has no target, so
// the card text cannot preview it.
struct NoEscape : IroncladT<NoEscape> {
  CARD_HEADER(NoEscape, "NO_ESCAPE", 1, Skill, Uncommon, AnyEnemy)
    addVar("DoomThreshold", 10);
    addVar("CalculationBase", 10);
    addVar("CalculationExtra", 5);
    addVar("CalculatedDoom", 0);  // the multiplier needs the target's Doom
  }
  Task<> onPlay(CardPlay& p) override {
    int doom = p.target->powerAmount<DoomPower>();
    int mult = doom / val("DoomThreshold").toInt();  // Math.Floor of a non-negative ratio
    Dec calc = val("CalculationBase") + val("CalculationExtra") * Dec(mult);
    co_await applyPower<DoomPower>(p.target, calc, me(), this);
  }
  void onUpgrade() override { upgradeVar("CalculationBase", 5); }
};

// Pagestorm.cs: Power, apply Pagestorm (Cards var 1); upgrade: cost 0.
struct Pagestorm : IroncladT<Pagestorm> {
  CARD_HEADER(Pagestorm, "PAGESTORM", 1, Power, Uncommon, Self)
    addVar("Cards", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<PagestormPower>(me(), val("Cards"), me(), this); }
  void onUpgrade() override { cost -= 1; }
};

// Parse.cs: Ethereal, draw 3 (+1 upgraded) cards.
struct Parse : IroncladT<Parse> {
  CARD_HEADER(Parse, "PARSE", 1, Skill, Uncommon, Self)
    keywords = kwEthereal;
    addVar("Cards", 3);
  }
  Task<> onPlay(CardPlay&) override { co_await drawCards(val("Cards")); }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

// PullFromBelow.cs: deal 5 (+2 upgraded) damage once per Ethereal card played this combat
// (CalculationBase 0 + CalculationExtra 1 per CardPlayFinishedEntry with WasEthereal).
struct PullFromBelow : IroncladT<PullFromBelow> {
  CARD_HEADER(PullFromBelow, "PULL_FROM_BELOW", 1, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 5);
    addVar("CalculationBase", 0);
    addVar("CalculationExtra", 1);
    addVar("CalculatedHits", 0);
    calcMultiplier = [](Card* c) {
      return c->combat ? c->combat->history.count([](const CombatHistoryEntry& e) {
        return e.kind == CombatHistoryEntry::CardPlayFinished && e.flag;  // WasEthereal
      }) : 0;
    };
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage"), calculatedBlock().toInt()); }
  void onUpgrade() override { upgradeVar("Damage", 2); }
};

// Putrefy.cs: Exhaust. Apply 2 (+1 upgraded) Weak, then the same Vulnerable.
struct Putrefy : IroncladT<Putrefy> {
  CARD_HEADER(Putrefy, "PUTREFY", 1, Skill, Uncommon, AnyEnemy)
    keywords = kwExhaust;
    addVar("Power", 2);
  }
  Task<> onPlay(CardPlay& p) override {
    int amount = val("Power").toInt();
    co_await applyPower<WeakPower>(p.target, amount, me(), this);
    co_await applyPower<VulnerablePower>(p.target, amount, me(), this);
  }
  void onUpgrade() override { upgradeVar("Power", 1); }
};

// Rattle.cs: OstyAttack. Osty hits 7 (+2 upgraded) damage 1 + (Osty attacks this turn) times
// (CalculationBase 0 + CalculationExtra 1 per CreatureAttackedEntry by Osty this turn, plus 1).
struct Rattle : IroncladT<Rattle> {
  CARD_HEADER(Rattle, "RATTLE", 1, Attack, Uncommon, AnyEnemy)
    tags = tagOstyAttack;
    addVar("OstyDamage", 7);
    addVar("CalculationBase", 0);
    addVar("CalculationExtra", 1);
    addVar("CalculatedHits", 0);
    calcMultiplier = [](Card* c) {
      Combat* cb = c->combat;
      if (!cb) return 1;
      return 1 + cb->history.countThisTurn(*cb, CombatHistoryEntry::CreatureAttacked,
                                           [cb](const CombatHistoryEntry& e) { return cb->osty && e.actor == cb->osty; });
    };
  }
  Task<> onPlay(CardPlay& p) override {
    if (ostyMissing(*combat)) co_return;
    cmd::Attack a;
    a.damagePerHit = val("OstyDamage");
    a.hits = calculatedBlock().toInt();
    a.attacker = combat->osty;
    a.source = this;
    a.single = p.target;
    co_await a.execute(*combat);
  }
  void onUpgrade() override { upgradeVar("OstyDamage", 2); }
};

// RightHandHand.cs: 0 cost, OstyAttack. Osty hits for 4 (+2 upgraded). Whenever you play a card that
// spent at least 2 (Energy var) energy while this card is in the discard pile, return it to the hand.
struct RightHandHand : IroncladT<RightHandHand> {
  CARD_HEADER(RightHandHand, "RIGHT_HAND_HAND", 0, Attack, Uncommon, AnyEnemy)
    tags = tagOstyAttack;
    addVar("OstyDamage", 4);
    addVar("Energy", 2);
  }
  Task<> onPlay(CardPlay& p) override {
    if (ostyMissing(*combat)) co_return;
    cmd::Attack a;
    a.damagePerHit = val("OstyDamage");
    a.attacker = combat->osty;
    a.source = this;
    a.single = p.target;
    co_await a.execute(*combat);
  }
  Task<> afterCardPlayedLate(const CardPlay& p) override {
    if (!combat || ownerOf(p.card) != combat->player || p.energySpent < val("Energy").toInt()) co_return;
    if (combat->pileOf(this) == Pile::Discard) co_await cmd::moveCard(*combat, this, Pile::Hand);
  }
  void onUpgrade() override { upgradeVar("OstyDamage", 2); }
};

// Severance.cs: deal 13 (+5 upgraded) damage, then add 3 Souls: one at a random spot of the draw
// pile, one to the discard pile, one to the hand.
struct Severance : IroncladT<Severance> {
  CARD_HEADER(Severance, "SEVERANCE", 2, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 13);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await addSoulToDrawPileRandom(*combat);
    co_await cmd::addGeneratedCard(*combat, db::card("Soul"), Pile::Discard);
    co_await cmd::addGeneratedCard(*combat, db::card("Soul"), Pile::Hand);
  }
  void onUpgrade() override { upgradeVar("Damage", 5); }
};

// Shroud.cs: Power, apply Shroud (Unpowered Block var 3 (+1 upgraded)).
struct Shroud : IroncladT<Shroud> {
  CARD_HEADER(Shroud, "SHROUD", 1, Power, Uncommon, Self)
    addVar("Block", 3);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<ShroudPower>(me(), val("Block"), me(), this); }
  void onUpgrade() override { upgradeVar("Block", 1); }
};

// SicEm.cs: OstyAttack. Osty hits for 5 (+1 upgraded) (if alive), then apply 3 (+1 upgraded) Sic 'Em.
struct SicEm : IroncladT<SicEm> {
  CARD_HEADER(SicEm, "SIC_EM", 1, Attack, Uncommon, AnyEnemy)
    tags = tagOstyAttack;
    addVar("OstyDamage", 5);
    addVar("SicEmPower", 3);
  }
  Task<> onPlay(CardPlay& p) override {
    if (!ostyMissing(*combat)) {
      cmd::Attack a;
      a.damagePerHit = val("OstyDamage");
      a.attacker = combat->osty;
      a.source = this;
      a.single = p.target;
      co_await a.execute(*combat);
    }
    co_await applyPower<SicEmPower>(p.target, val("SicEmPower"), me(), this);
  }
  void onUpgrade() override {
    upgradeVar("OstyDamage", 1);
    upgradeVar("SicEmPower", 1);
  }
};

// SleightOfFlesh.cs: 2 cost Power, apply 9 (+4 upgraded) Sleight of Flesh.
struct SleightOfFlesh : IroncladT<SleightOfFlesh> {
  CARD_HEADER(SleightOfFlesh, "SLEIGHT_OF_FLESH", 2, Power, Uncommon, Self)
    addVar("SleightOfFleshPower", 9);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<SleightOfFleshPower>(me(), val("SleightOfFleshPower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("SleightOfFleshPower", 4); }
};

// Spur.cs: Retain. Summon Osty for 3 (+2 upgraded) HP, then heal him 5 (+2 upgraded).
struct Spur : IroncladT<Spur> {
  CARD_HEADER(Spur, "SPUR", 1, Skill, Uncommon, Self)
    keywords = kwRetain;
    addVar("Summon", 3);
    addVar("Heal", 5);
  }
  Task<> onPlay(CardPlay&) override {
    co_await summonOsty(*combat, val("Summon").toInt());
    if (combat->osty) co_await cmd::heal(combat->osty, val("Heal"));
  }
  void onUpgrade() override {
    upgradeVar("Summon", 2);
    upgradeVar("Heal", 2);
  }
};

// Veilpiercer.cs: deal 10 (+3 upgraded) damage, then gain 1 Veilpiercer.
struct Veilpiercer : IroncladT<Veilpiercer> {
  CARD_HEADER(Veilpiercer, "VEILPIERCER", 1, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 10);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<VeilpiercerPower>(me(), 1, me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

}  // namespace

void registerNecrobinderUncommonCards2() {
  registerPowerType<HauntPower>();
  registerPowerType<LethalityPower>();
  registerPowerType<PagestormPower>();
  registerPowerType<ShroudPower>();
  registerPowerType<SicEmPower>();
  registerPowerType<SleightOfFleshPower>();
  registerPowerType<VeilpiercerPower>();
  registerCardType<Haunt>();
  registerCardType<HighFive>();
  registerCardType<Lethality>();
  registerCardType<Melancholy>();
  registerCardType<NoEscape>();
  registerCardType<Pagestorm>();
  registerCardType<Parse>();
  registerCardType<PullFromBelow>();
  registerCardType<Putrefy>();
  registerCardType<Rattle>();
  registerCardType<RightHandHand>();
  registerCardType<Severance>();
  registerCardType<Shroud>();
  registerCardType<SicEm>();
  registerCardType<SleightOfFlesh>();
  registerCardType<Spur>();
  registerCardType<Veilpiercer>();
}

}  // namespace sts
