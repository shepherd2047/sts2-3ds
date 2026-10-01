// The Necrobinder's Rare cards (X4.4): BansheesCry ... Undeath, in NecrobinderCardPool order, from
// Models.Cards / Models.Powers, plus the SweepingGaze token (SentryMode). Same style as
// char_necrobinder_cards_uncommon1.cpp: IroncladT<> + CARD_HEADER, DynVars named like the C#'s,
// cards that deal damage through Osty build a raw cmd::Attack with `attacker = combat->osty`.
// The powers used only by these cards are defined here (one class per power id); NecroMasteryPower
// and DoomPower come from char_necrobinder.h.
#include "cards.h"
#include "char_necrobinder.h"

namespace sts {

namespace {

// ---------------------------------------------------------------- powers

// CallOfTheVoidPower.cs: Buff, Counter. Before each hand draw, add Amount random Ethereal cards
// from the character's pool (not Basic / Ancient) to the hand: one GetDistinctForCombat(pool, 1,
// CombatCardGeneration) per card (a full shuffle each), so the cards may repeat.
struct CallOfTheVoidPower : Power {
  POWER_HEADER(CallOfTheVoidPower, "CALL_OF_THE_VOID_POWER")
  Task<> beforeHandDraw() override {
    Combat* c = owner->combat;
    auto ids = db::characterPool(c->run->characterId, [](const Card& k) {
      return k.rarity != Rarity::Basic && k.rarity != Rarity::Ancient;
    });
    if (ids.empty()) co_return;
    std::vector<std::unique_ptr<Card>> made;
    for (int i = 0; i < amount; ++i)
      for (auto& card : distinctForCombat(*c, ids, 1)) {
        card->addKeyword(kwEthereal);  // CardCmd.ApplyKeyword
        made.push_back(std::move(card));
      }
    flash = 1.f;
    for (auto& card : made) co_await cmd::addGeneratedCard(*c, std::move(card), Pile::Hand);
  }
};

// DemesnePower.cs: Buff, Counter. +Amount cards drawn each turn and +Amount max energy.
struct DemesnePower : Power {
  POWER_HEADER(DemesnePower, "DEMESNE_POWER")
  Dec modifyHandDraw(Dec count) override { return count + Dec(amount); }
  Dec modifyMaxEnergy(Dec amt) override { return amt + Dec(amount); }
};

// DevourLifePower.cs: Buff, Counter. Whenever the owner plays a Soul, summon Osty for Amount HP.
struct DevourLifePower : Power {
  POWER_HEADER(DevourLifePower, "DEVOUR_LIFE_POWER")
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (p.card->id != "Soul" || ownerOf(p.card) != owner) co_return;
    co_await summonOsty(*owner->combat, amount);
  }
};

// HangPower.cs: Debuff, Counter. Damage dealt to the owner by Hang is multiplied by Amount.
struct HangPower : Power {
  POWER_HEADER(HangPower, "HANG_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  Dec modifyDamageMultiplicative(Creature* target, Dec, int, Creature*, Card* src) override {
    if (target != owner || !src || src->id != "Hang") return 1;
    return Dec(amount);
  }
};

// NeurosurgePower.cs: Debuff, Counter. At the start of the owner's turn, the owner gains Amount Doom.
struct NeurosurgePower : Power {
  POWER_HEADER(NeurosurgePower, "NEUROSURGE_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await applyPower<DoomPower>(owner, Dec(amount), owner, nullptr);
  }
};

// OblivionPower.cs: Debuff, Counter, InstancedPerApplier. Every card the applier plays while it is on
// the enemy gives that enemy Doom equal to the power's amount when the card was played (the Oblivion that applied it does not
// trigger itself: its beforeCardPlayed ran before the power existed). Removed at the end of the
// player's turn.
struct OblivionPower : Power {
  POWER_HEADER(OblivionPower, "OBLIVION_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  PowerInstanceType instanceType() const override { return PowerInstanceType::InstancedPerApplier; }
  std::vector<std::pair<Card*, int>> amountsForPlayedCards;
  Task<> beforeCardPlayed(const CardPlay& p) override {
    if (!applier || applier != owner->combat->player || ownerOf(p.card) != applier) co_return;
    amountsForPlayedCards.push_back({p.card, amount});
  }
  Task<> afterCardPlayed(const CardPlay& p) override {
    for (size_t i = 0; i < amountsForPlayedCards.size(); ++i) {
      if (amountsForPlayedCards[i].first != p.card) continue;
      int value = amountsForPlayedCards[i].second;
      amountsForPlayedCards.erase(amountsForPlayedCards.begin() + i);
      flash = 1.f;
      co_await applyPower<DoomPower>(owner, Dec(value), applier, nullptr);
      break;
    }
  }
  Task<> afterSideTurnEnd(Side side, const std::vector<Creature*>&) override {
    if (side == Side::Player) co_await cmd::removePower(this);
  }
};

// ReaperFormPower.cs: Buff, Counter. Whenever the owner (or its Osty) deals powered attack damage,
// apply Doom equal to the total damage (blocked + unblocked) times Amount to the target.
struct ReaperFormPower : Power {
  POWER_HEADER(ReaperFormPower, "REAPER_FORM_POWER")
  Task<> afterDamageGiven(Creature* dealer, const DamageResult& r, int props, Creature* target, Card*) override {
    if (!dealer || !(dealer == owner || dealer->petOwner == owner) || !isPoweredAttack(props)) co_return;
    int total = r.blocked + r.unblocked;
    if (total <= 0) co_return;
    co_await applyPower<DoomPower>(target, Dec(total * amount), owner, nullptr);
  }
};

// SentryModePower.cs: Buff, Counter. Before each hand draw, add Amount Sweeping Gazes to the hand.
struct SentryModePower : Power {
  POWER_HEADER(SentryModePower, "SENTRY_MODE_POWER")
  Task<> beforeHandDraw() override {
    for (int i = 0; i < amount; ++i) co_await cmd::addGeneratedCard(*owner->combat, db::card("SweepingGaze"), Pile::Hand);
  }
};

// SpiritOfAshPower.cs: Buff, Counter. Before the owner plays an Ethereal card, gain Amount
// Unpowered Block.
struct SpiritOfAshPower : Power {
  POWER_HEADER(SpiritOfAshPower, "SPIRIT_OF_ASH_POWER")
  Task<> beforeCardPlayed(const CardPlay& p) override {
    if (ownerOf(p.card) != owner || !p.card->has(kwEthereal)) co_return;
    co_await cmd::gainBlock(owner, Dec(amount), kUnpowered, nullptr);
  }
};

// ---------------------------------------------------------------- cards

bool ostyMissing(Combat& c) { return !c.osty || c.osty->dead(); }  // Osty.CheckMissingWithAnim

// SweepingGaze.cs (token): 0 cost Ethereal Exhaust OstyAttack, Osty hits a random enemy for 10 (+5
// upgraded). Created by SentryMode.
struct SweepingGaze : IroncladT<SweepingGaze> {
  CARD_HEADER(SweepingGaze, "SWEEPING_GAZE", 0, Attack, Token, RandomEnemy)
    tags = tagOstyAttack;
    keywords = kwEthereal | kwExhaust;
    addVar("OstyDamage", 10);
  }
  Task<> onPlay(CardPlay&) override {
    if (ostyMissing(*combat)) co_return;
    cmd::Attack a;
    a.damagePerHit = val("OstyDamage");
    a.attacker = combat->osty;
    a.source = this;
    a.random = true;
    co_await a.execute(*combat);
  }
  void onUpgrade() override { upgradeVar("OstyDamage", 5); }
};

// BansheesCry.cs: 9 cost, deal 33 to all enemies. Costs 2 (Energy var) less for the rest of the
// combat each time an Ethereal card is played (also counting those already played when this card
// enters combat). Upgrade: -2 cost.
// The earlier plays are the CardPlaysFinished entries with WasEthereal.
// IsClone: a clone (createClone) already carries the reduction.
struct BansheesCry : IroncladT<BansheesCry> {
  CARD_HEADER(BansheesCry, "BANSHEES_CRY", 9, Attack, Rare, AllEnemies)
    addVar("Damage", 33);
    addVar("Energy", 2);
  }
  Task<> onPlay(CardPlay&) override { co_await attackAll(val("Damage")); }
  void onUpgrade() override { cost -= 2; }
  Task<> afterCardEnteredCombat(Card* card) override {
    if (card != this || !combat || isClone()) co_return;
    int n = combat->history.count([](const CombatHistoryEntry& e) { return e.kind == CombatHistoryEntry::CardPlayFinished && e.flag; });
    addThisCombat(-n * val("Energy").toInt());
  }
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (!p.card->has(kwEthereal)) co_return;
    addThisCombat(-val("Energy").toInt());
  }
};

// CallOfTheVoid.cs: Power, apply 1 Call of the Void. Upgrade: Innate.
struct CallOfTheVoid : IroncladT<CallOfTheVoid> {
  CARD_HEADER(CallOfTheVoid, "CALL_OF_THE_VOID", 1, Power, Rare, Self)
    addVar("Cards", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<CallOfTheVoidPower>(me(), val("Cards"), me(), this); }
  void onUpgrade() override { addKeyword(kwInnate); }
};

// Demesne.cs: Ethereal Power, apply Cards (1) Demesne (+1 draw and +1 max energy). Upgrade: -1 cost.
struct Demesne : IroncladT<Demesne> {
  CARD_HEADER(Demesne, "DEMESNE", 3, Power, Rare, Self)
    keywords = kwEthereal;
    addVar("Energy", 1);
    addVar("Cards", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<DemesnePower>(me(), val("Cards"), me(), this); }
  void onUpgrade() override { cost -= 1; }
};

// DevourLife.cs: Power, apply 1 (+1 upgraded) Devour Life.
struct DevourLife : IroncladT<DevourLife> {
  CARD_HEADER(DevourLife, "DEVOUR_LIFE", 1, Power, Rare, Self)
    addVar("DevourLifePower", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<DevourLifePower>(me(), val("DevourLifePower"), me(), this); }
  void onUpgrade() override { upgradeVar("DevourLifePower", 1); }
};

// Eidolon.cs: Exhaust, auto-play every Ethereal, non-Unplayable card in the exhaust pile.
struct Eidolon : IroncladT<Eidolon> {
  bool canBeGeneratedInCombat() const override { return false; }
  CARD_HEADER(Eidolon, "EIDOLON", 2, Skill, Rare, Self)
    keywords = kwExhaust;
  }
  Task<> onPlay(CardPlay&) override {
    std::vector<Card*> list;
    for (Card* c : combat->exhaust)
      if (c->has(kwEthereal) && !c->has(kwUnplayable)) list.push_back(c);
    for (Card* c : list) co_await cmd::autoPlay(*combat, c, nullptr);
  }
  void onUpgrade() override { cost -= 1; }
};

// EndOfDays.cs: 3 cost, apply 29 (+8 upgraded) Doom to every hittable enemy, then kill every
// hittable enemy that is now doomed (DoomPower.DoomKill).
struct EndOfDays : IroncladT<EndOfDays> {
  CARD_HEADER(EndOfDays, "END_OF_DAYS", 3, Skill, Rare, AllEnemies)
    addVar("DoomPower", 29);
  }
  Task<> onPlay(CardPlay&) override {
    for (Creature* e : combat->hittableEnemies()) co_await applyPower<DoomPower>(e, val("DoomPower"), me(), this);
    co_await doomKill(*combat, DoomPower::doomedOf(combat->hittableEnemies()));
  }
  void onUpgrade() override { upgradeVar("DoomPower", 8); }
};

// Eradicate.cs: X cost, Retain, deal 11 (+3 upgraded) X times.
struct Eradicate : IroncladT<Eradicate> {
  CARD_HEADER(Eradicate, "ERADICATE", 0, Attack, Rare, AnyEnemy)
    costsX = true;
    keywords = kwRetain;
    addVar("Damage", 11);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage"), xValue); }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

// Hang.cs: deal 10 (+3 upgraded), then apply max(2, current Hang) Hang: the target's Hang doubles
// (2 -> 4 -> 8 ...) and multiplies the damage of later Hangs.
struct Hang : IroncladT<Hang> {
  CARD_HEADER(Hang, "HANG", 1, Attack, Rare, AnyEnemy)
    addVar("Damage", 10);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    int powerAmount = p.target->powerAmount<HangPower>();
    int num = std::max(2, powerAmount);
    if (powerAmount + num > 999999999) num = std::max(0, 999999999 - powerAmount);
    co_await applyPower<HangPower>(p.target, Dec(num), me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

// Misery.cs: 0 cost, deal 7 (+2 upgraded, +Retain), then copy every debuff the target had before
// the hit onto every other hittable enemy (stacking onto ones they already have).
// A temporary power's amount is merged into its InternallyAppliedPower's entry (`debuffAmounts[internal] +=
// temp.Amount`). PORT NOTE (n/a: equivalent): the copies are fresh powers from the registry, not
// ClonePreservingMutability clones; no debuff has per-instance state beyond amount/applier.
struct Misery : IroncladT<Misery> {
  CARD_HEADER(Misery, "MISERY", 0, Attack, Rare, AnyEnemy)
    addVar("Damage", 7);
  }
  struct Debuff { std::string id; int amount; Creature* applier; const char* inner; };
  Task<> onPlay(CardPlay& p) override {
    std::vector<Debuff> debuffs;
    for (auto& pw : p.target->powers)
      if (pw->typeForAmount(Dec(pw->amount)) == PowerType::Debuff)
        debuffs.push_back({pw->id, pw->amount, pw->applier, pw->internallyAppliedPower()});
    for (size_t i = 0; i < debuffs.size(); ++i) {
      const char* inner = debuffs[i].inner;
      if (!inner) continue;
      int add = debuffs[i].amount;
      for (auto& d : debuffs)
        if (d.id == inner) { d.amount += add; break; }
    }
    co_await attack(p.target, val("Damage"));
    std::vector<Creature*> enemies = combat->hittableEnemies();
    for (Creature* enemy : enemies) {
      if (enemy == p.target) continue;
      for (auto& d : debuffs) {
        if (d.amount == 0) continue;
        auto pw = db::power(d.id);
        if (!pw) continue;
        if (Power* existing = enemy->stackingInstance(*pw, d.applier)) {
          co_await cmd::modifyPowerAmount(existing, Dec(d.amount), d.applier, this);
        } else {
          co_await cmd::applyPower(std::move(pw), enemy, Dec(d.amount), d.applier, this);
        }
      }
    }
  }
  void onUpgrade() override {
    upgradeVar("Damage", 2);
    addKeyword(kwRetain);
  }
};

// NecroMastery.cs: summon Osty for 5 (+3 upgraded) HP, then apply 1 Necro Mastery.
struct NecroMastery : IroncladT<NecroMastery> {
  CARD_HEADER(NecroMastery, "NECRO_MASTERY", 2, Power, Rare, Self)
    addVar("Summon", 5);
  }
  Task<> onPlay(CardPlay&) override {
    co_await summonOsty(*combat, val("Summon").toInt());
    co_await applyPower<NecroMasteryPower>(me(), Dec(1), me(), this);
  }
  void onUpgrade() override { upgradeVar("Summon", 3); }
};

// Neurosurge.cs: 0 cost Power. Gain 3 (+1 upgraded) energy, draw 2, apply 3 Neurosurge (Doom on
// yourself every turn).
struct Neurosurge : IroncladT<Neurosurge> {
  CARD_HEADER(Neurosurge, "NEUROSURGE", 0, Power, Rare, Self)
    addVar("NeurosurgePower", 3);
    addVar("Energy", 3);
    addVar("Cards", 2);
  }
  Task<> onPlay(CardPlay&) override {
    co_await cmd::gainEnergy(*combat, val("Energy").toInt());
    co_await drawCards(val("Cards"));
    co_await applyPower<NeurosurgePower>(me(), val("NeurosurgePower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Energy", 1); }
};

// Oblivion.cs: 0 cost, apply 3 (+1 upgraded) Oblivion to the target.
struct Oblivion : IroncladT<Oblivion> {
  CARD_HEADER(Oblivion, "OBLIVION", 0, Skill, Rare, AnyEnemy)
    addVar("DoomPower", 3);
  }
  Task<> onPlay(CardPlay& p) override { co_await applyPower<OblivionPower>(p.target, val("DoomPower"), me(), this); }
  void onUpgrade() override { upgradeVar("DoomPower", 1); }
};

// Reanimate.cs: 3 cost Exhaust, summon Osty for 20 (+5 upgraded) HP.
struct Reanimate : IroncladT<Reanimate> {
  CARD_HEADER(Reanimate, "REANIMATE", 3, Skill, Rare, Self)
    keywords = kwExhaust;
    addVar("Summon", 20);
  }
  Task<> onPlay(CardPlay&) override { co_await summonOsty(*combat, val("Summon").toInt()); }
  void onUpgrade() override { upgradeVar("Summon", 5); }
};

// ReaperForm.cs: 3 cost Power, apply 1 Reaper Form. Upgrade: Retain.
struct ReaperForm : IroncladT<ReaperForm> {
  CARD_HEADER(ReaperForm, "REAPER_FORM", 3, Power, Rare, Self)
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<ReaperFormPower>(me(), Dec(1), me(), this); }
  void onUpgrade() override { addKeyword(kwRetain); }
};

// Sacrifice.cs: Retain. If Osty is alive: block = 3 * Osty's max HP (CalculationExtra 1), Osty dies,
// then gain the block. The block is computed before Osty is killed. Upgrade: -1 cost.
struct Sacrifice : IroncladT<Sacrifice> {
  CARD_HEADER(Sacrifice, "SACRIFICE", 1, Skill, Rare, Self)
    keywords = kwRetain;
    addVar("CalculationBase", 0);
    addVar("CalculationExtra", 1);
    addVar("CalculatedBlock", 0);
    calcMultiplier = [](Card* c) { return c->combat && c->combat->osty && c->combat->osty->alive() ? c->combat->osty->maxHp * 3 : 0; };
  }
  Task<> onPlay(CardPlay&) override {
    if (ostyMissing(*combat)) co_return;
    Dec blockGain = calculatedBlock();
    co_await cmd::kill({combat->osty});
    co_await block(blockGain);
  }
  void onUpgrade() override { cost -= 1; }
};

// Seance.cs: Ethereal. Choose 1 card from the draw pile and transform it into a Soul. Upgrade: -1 cost.
struct Seance : IroncladT<Seance> {
  CARD_HEADER(Seance, "SEANCE", 1, Skill, Rare, Self)
    keywords = kwEthereal;
    addVar("Cards", 1);
  }
  Task<> onPlay(CardPlay&) override {
    int n = std::min(val("Cards").toInt(), (int)combat->draw.size());
    if (n <= 0) co_return;
    auto picked = co_await cmd::selectCards(*combat, "card_selection.TO_TRANSFORM", combat->draw, n, n);
    for (Card* c : picked) co_await cmd::transform(*combat, c, db::card("Soul"));
  }
  void onUpgrade() override { cost -= 1; }
};

// SentryMode.cs: Power, apply 1 Sentry Mode (a Sweeping Gaze in hand each turn). Upgrade: -1 cost.
struct SentryMode : IroncladT<SentryMode> {
  CARD_HEADER(SentryMode, "SENTRY_MODE", 2, Power, Rare, Self)
    addVar("SentryModePower", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<SentryModePower>(me(), val("SentryModePower"), me(), this); }
  void onUpgrade() override { cost -= 1; }
};

// SharedFate.cs: 0 cost Exhaust. Lose 2 Strength, the target loses 2 (+1 upgraded) Strength.
struct SharedFate : IroncladT<SharedFate> {
  CARD_HEADER(SharedFate, "SHARED_FATE", 0, Skill, Rare, AnyEnemy)
    keywords = kwExhaust;
    addVar("EnemyStrengthLoss", 2);
    addVar("PlayerStrengthLoss", 2);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await applyPower<StrengthPower>(me(), -val("PlayerStrengthLoss"), me(), this);
    co_await applyPower<StrengthPower>(p.target, -val("EnemyStrengthLoss"), me(), this);
  }
  void onUpgrade() override { upgradeVar("EnemyStrengthLoss", 1); }
};

// SoulStorm.cs: deal 9 + 4 (+2 upgraded) per Soul in the exhaust pile.
struct SoulStorm : IroncladT<SoulStorm> {
  CARD_HEADER(SoulStorm, "SOUL_STORM", 1, Attack, Rare, AnyEnemy)
    addVar("CalculationBase", 9);
    addVar("ExtraDamage", 4);
    addVar("CalculatedDamage", 0);
    calcMultiplier = [](Card* c) {
      int n = 0;
      if (c->combat) for (Card* k : c->combat->exhaust) if (k->id == "Soul") ++n;
      return n;
    };
  }
  Task<> onPlay(CardPlay& p) override { co_await attackCalculated(p.target); }
  void onUpgrade() override { upgradeVar("ExtraDamage", 2); }
};

// SpiritOfAsh.cs: Power, apply 4 (+1 upgraded) Spirit of Ash.
struct SpiritOfAsh : IroncladT<SpiritOfAsh> {
  CARD_HEADER(SpiritOfAsh, "SPIRIT_OF_ASH", 1, Power, Rare, Self)
    addVar("BlockOnExhaust", 4);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<SpiritOfAshPower>(me(), val("BlockOnExhaust"), me(), this); }
  void onUpgrade() override { upgradeVar("BlockOnExhaust", 1); }
};

// Squeeze.cs: 3 cost OstyAttack. If Osty is alive, Osty hits for 25 (+5 upgraded) + 5 (+1 upgraded)
// per other OstyAttack card in any of your piles.
struct Squeeze : IroncladT<Squeeze> {
  CARD_HEADER(Squeeze, "SQUEEZE", 3, Attack, Rare, AnyEnemy)
    tags = tagOstyAttack;
    addVar("CalculationBase", 25);
    addVar("ExtraDamage", 5);
    addVar("CalculatedDamage", 0);
    calcMultiplier = [](Card* c) {
      int n = 0;
      if (c->combat) for (Card* k : c->combat->allCards()) if ((k->tags & tagOstyAttack) && k != c) ++n;
      return n;
    };
  }
  Task<> onPlay(CardPlay& p) override {
    if (ostyMissing(*combat)) co_return;
    cmd::Attack a;
    a.calcFrom = this;
    a.attacker = combat->osty;
    a.source = this;
    a.single = p.target;
    co_await a.execute(*combat);
  }
  void onUpgrade() override {
    upgradeVar("CalculationBase", 5);
    upgradeVar("ExtraDamage", 1);
  }
};

// TheScythe.cs: 2 cost Exhaust, deal Damage (13 base + permanent increases); each play permanently
// raises this card's (and its deck version's) damage by Increase 5 (+2 upgraded).
// PORT NOTE (n/a: equivalent): the C# tracks CurrentDamage / IncreasedDamage as saved properties; here
// the Damage var itself is bumped (vars are saved by name), on this card and on Card::deckVersion.
struct TheScythe : IroncladT<TheScythe> {
  CARD_HEADER(TheScythe, "THE_SCYTHE", 2, Attack, Rare, AnyEnemy)
    keywords = kwExhaust;
    addVar("Damage", 13);
    addVar("Increase", 5);
  }
  // IncreasedDamage: CurrentDamage = 13 + IncreasedDamage (UpdateDamage, also after a downgrade).
  Dec increasedDamage = 0;
  void afterLoad() override { increasedDamage = val("Damage") - Dec(13); }
  void afterDowngraded() override { if (auto* v = var("Damage")) v->base = Dec(13) + increasedDamage; }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    Dec inc = val("Increase");
    buffFromPlay(this, inc);
    if (deckVersion.p && deckVersion.p != this && deckVersion.p->id == "TheScythe") buffFromPlay(deckVersion.p, inc);
  }
  static void buffFromPlay(Card* c, Dec inc) {
    c->upgradeVar("Damage", inc);
    static_cast<TheScythe*>(c)->increasedDamage += inc;
  }
  void onUpgrade() override { upgradeVar("Increase", 2); }
};

// TimesUp.cs: 2 cost, deal 1 per Doom on the target. Upgrade: Retain.
struct TimesUp : IroncladT<TimesUp> {
  CARD_HEADER(TimesUp, "TIMES_UP", 2, Attack, Rare, AnyEnemy)
    addVar("CalculationBase", 0);
    addVar("ExtraDamage", 1);
    addVar("CalculatedDamage", 0);
    calcMultiplierT = [](Card*, Creature* t) { return t ? t->powerAmount<DoomPower>() : 0; };
  }
  Task<> onPlay(CardPlay& p) override { co_await attackCalculated(p.target); }
  void onUpgrade() override { addKeyword(kwRetain); }
};

// Transfigure.cs: Exhaust (removed on upgrade). Choose a card in hand: it costs 1 more this combat
// (unless X-cost / Unplayable) and gains +1 replay.
// The prompt key is this card's own (SelectionScreenPrompt).
struct Transfigure : IroncladT<Transfigure> {
  bool canBeGeneratedInCombat() const override { return false; }
  CARD_HEADER(Transfigure, "TRANSFIGURE", 1, Skill, Rare, Self)
    keywords = kwExhaust;
    addVar("Energy", 1);
  }
  Task<> onPlay(CardPlay&) override {
    if (combat->hand.empty()) co_return;
    auto picked = co_await cmd::selectCards(*combat, "TRANSFIGURE", combat->hand, 1, 1);
    for (Card* c : picked) {
      if (!c->costsX && c->cost >= 0) c->addThisCombat(1);
      ++c->baseReplayCount;
    }
  }
  void onUpgrade() override { removeKeyword(kwExhaust); }
};

// Undeath.cs: 0 cost, gain 7 (+2 upgraded) Block, add a copy of this card to the discard pile.
struct Undeath : IroncladT<Undeath> {
  CARD_HEADER(Undeath, "UNDEATH", 0, Skill, Rare, Self)
    addVar("Block", 7);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    co_await cmd::addGeneratedCard(*combat, createClone(), Pile::Discard);
  }
  void onUpgrade() override { upgradeVar("Block", 2); }
};

}  // namespace

void registerNecrobinderRareCards() {
  registerPowerType<CallOfTheVoidPower>();
  registerPowerType<DemesnePower>();
  registerPowerType<DevourLifePower>();
  registerPowerType<HangPower>();
  registerPowerType<NeurosurgePower>();
  registerPowerType<OblivionPower>();
  registerPowerType<ReaperFormPower>();
  registerPowerType<SentryModePower>();
  registerPowerType<SpiritOfAshPower>();
  registerCardType<SweepingGaze>();
  registerCardType<BansheesCry>();
  registerCardType<CallOfTheVoid>();
  registerCardType<Demesne>();
  registerCardType<DevourLife>();
  registerCardType<Eidolon>();
  registerCardType<EndOfDays>();
  registerCardType<Eradicate>();
  registerCardType<Hang>();
  registerCardType<Misery>();
  registerCardType<NecroMastery>();
  registerCardType<Neurosurge>();
  registerCardType<Oblivion>();
  registerCardType<Reanimate>();
  registerCardType<ReaperForm>();
  registerCardType<Sacrifice>();
  registerCardType<Seance>();
  registerCardType<SentryMode>();
  registerCardType<SharedFate>();
  registerCardType<SoulStorm>();
  registerCardType<SpiritOfAsh>();
  registerCardType<Squeeze>();
  registerCardType<TheScythe>();
  registerCardType<TimesUp>();
  registerCardType<Transfigure>();
  registerCardType<Undeath>();
}

}  // namespace sts
