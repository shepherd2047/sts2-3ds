// The Necrobinder's Uncommon cards, first half (X4.3a): BoneShards ... Friendship, in
// NecrobinderCardPool order, from Models.Cards / Models.Powers. Same style as
// char_necrobinder_cards.cpp (X4.2): IroncladT<> + CARD_HEADER, DynVars named like the C#'s, cards
// that deal damage through Osty build a raw cmd::Attack with `attacker = combat->osty`.
// The powers used only by these cards are defined here (one class per power id).
#include "cards.h"
#include "char_necrobinder.h"

namespace sts {

namespace {

// ---------------------------------------------------------------- powers

// BorrowedTimePower.cs: Debuff, Counter. Every card the owner plays costs Amount more this turn
// (TryModifyEnergyCostInCombat); removed at the end of the owner's side turn.
struct BorrowedTimePower : Power {
  POWER_HEADER(BorrowedTimePower, "BORROWED_TIME_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  int modifyEnergyCost(Card* card, int cost) override {
    if (ownerOf(card) != owner) return cost;
    return cost + amount;
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await cmd::removePower(this);
  }
};

// CalcifyPower.cs: Buff, Counter. Osty's powered attacks deal Amount more damage.
struct CalcifyPower : Power {
  POWER_HEADER(CalcifyPower, "CALCIFY_POWER")
  Dec modifyDamageAdditive(Creature*, Dec, int props, Creature* dealer, Card*) override {
    if (!dealer || dealer->petOwner != owner || dealer != owner->combat->osty) return 0;
    if (!isPoweredAttack(props)) return 0;
    return amount;
  }
};

// CountdownPower.cs: Buff, Counter. At the start of the owner's turn, apply Amount Doom to a random
// hittable enemy (Rng.CombatTargets).
struct CountdownPower : Power {
  POWER_HEADER(CountdownPower, "COUNTDOWN_POWER")
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner)) co_return;
    flash = 1.f;
    Combat* c = owner->combat;
    Creature* target = c->rng("CombatTargets").nextItem(c->hittableEnemies());
    if (target) co_await applyPower<DoomPower>(target, amount, owner, nullptr);
  }
};

// DanseMacabrePower.cs: Buff, Counter. Before the owner plays a card whose energy cost is at least
// its Energy var (2), gain Amount Unpowered Block.
struct DanseMacabrePower : Power {
  POWER_HEADER(DanseMacabrePower, "DANSE_MACABRE_POWER")
  Task<> beforeCardPlayed(const CardPlay& p) override {
    if (ownerOf(p.card) != owner || owner->combat->energyCost(p.card) < 2) co_return;
    flash = 1.f;
    co_await cmd::gainBlock(owner, Dec(amount), kUnpowered, nullptr);
  }
};

// DebilitatePower.cs: Debuff, Counter. Doubles the owner's Vulnerable bonus and Weak penalty for
// powered attacks (ModifyVulnerableMultiplier / ModifyWeakMultiplier), ticks down at the end of
// the owner's side turn.
// PORT NOTE: the two multiplier hooks are not virtual on Model here (Vulnerable/Weak hard-code
// Cruelty / PaperPhrog / PaperKrane), so VulnerablePower and WeakPower in powers.h look for this
// power by id ("DebilitatePower") in the same style and apply the C# formulas last.
struct DebilitatePower : Power {
  POWER_HEADER(DebilitatePower, "DEBILITATE_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await cmd::decrement(this);
  }
};

// EnfeeblingTouchPower.cs: TemporaryStrengthPower, IsPositive == false (mirror of SetupStrikePower).
struct EnfeeblingTouchPower : Power {
  POWER_HEADER(EnfeeblingTouchPower, "ENFEEBLING_TOUCH_POWER")
  const char* internallyAppliedPower() const override { return "StrengthPower"; }  // ITemporaryPower
  PowerType type() const override { return PowerType::Debuff; }
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

// FriendshipPower.cs: Buff, Counter. +Amount max energy.
struct FriendshipPower : Power {
  POWER_HEADER(FriendshipPower, "FRIENDSHIP_POWER")
  Dec modifyMaxEnergy(Dec amt) override { return amt + Dec(amount); }
};

// ---------------------------------------------------------------- cards

bool ostyMissing(Combat& c) { return !c.osty || c.osty->dead(); }  // Osty.CheckMissingWithAnim

// BoneShards.cs: OstyAttack, AllEnemies. If Osty is alive: Osty hits every enemy for 9 (+3
// upgraded), gain 9 (+3 upgraded) Block, then Osty dies.
struct BoneShards : IroncladT<BoneShards> {
  CARD_HEADER(BoneShards, "BONE_SHARDS", 1, Attack, Uncommon, AllEnemies)
    tags = tagOstyAttack;
    addVar("OstyDamage", 9);
    addVar("Block", 9);
  }
  Task<> onPlay(CardPlay&) override {
    if (ostyMissing(*combat)) co_return;
    cmd::Attack a;
    a.damagePerHit = val("OstyDamage");
    a.attacker = combat->osty;
    a.source = this;
    a.allOpponents = true;
    co_await a.execute(*combat);
    co_await block(val("Block"));
    if (!ostyMissing(*combat)) co_await cmd::kill({combat->osty});
  }
  void onUpgrade() override {
    upgradeVar("OstyDamage", 3);
    upgradeVar("Block", 3);
  }
};

// BorrowedTime.cs: gain 4 (+2 upgraded) energy, then every card costs 1 more for the rest of the
// turn (BorrowedTimePower with the "ExtraCost" var).
struct BorrowedTime : IroncladT<BorrowedTime> {
  CARD_HEADER(BorrowedTime, "BORROWED_TIME", 1, Skill, Uncommon, Self)
    addVar("Energy", 4);
    addVar("ExtraCost", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await cmd::gainEnergy(*combat, val("Energy").toInt());
    co_await applyPower<BorrowedTimePower>(me(), val("ExtraCost"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Energy", 2); }
};

// Bury.cs: 4 cost, deal 52 (+11 upgraded) damage.
struct Bury : IroncladT<Bury> {
  CARD_HEADER(Bury, "BURY", 4, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 52);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
  void onUpgrade() override { upgradeVar("Damage", 11); }
};

// Calcify.cs: Power, apply 4 (+2 upgraded) Calcify.
struct Calcify : IroncladT<Calcify> {
  CARD_HEADER(Calcify, "CALCIFY", 1, Power, Uncommon, Self)
    addVar("CalcifyPower", 4);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<CalcifyPower>(me(), val("CalcifyPower"), me(), this); }
  void onUpgrade() override { upgradeVar("CalcifyPower", 2); }
};

// CaptureSpirit.cs: deal 3 (+1 upgraded) Unblockable/Unpowered damage to the target, then add 3
// (+1 upgraded) Souls at random spots in the draw pile.
struct CaptureSpirit : IroncladT<CaptureSpirit> {
  CARD_HEADER(CaptureSpirit, "CAPTURE_SPIRIT", 1, Skill, Uncommon, AnyEnemy)
    addVar("Damage", 3);
    addVar("Cards", 3);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await cmd::damage(p.target, val("Damage"), kUnblockable | kUnpowered | kMove, me(), this);
    int n = val("Cards").toInt();
    for (int i = 0; i < n; ++i) co_await addSoulToDrawPileRandom(*combat);
  }
  void onUpgrade() override {
    upgradeVar("Damage", 1);
    upgradeVar("Cards", 1);
  }
};

// Cleanse.cs: summon Osty for 3 (+2 upgraded) HP, then exhaust a card of your choice from the draw
// pile.
// PORT NOTE: the prompt key is True Grit's (both use the C# ExhaustSelectionPrompt).
struct Cleanse : IroncladT<Cleanse> {
  CARD_HEADER(Cleanse, "CLEANSE", 1, Skill, Uncommon, Self)
    addVar("Summon", 3);
  }
  Task<> onPlay(CardPlay&) override {
    co_await summonOsty(*combat, val("Summon").toInt());
    auto picked = co_await cmd::selectCards(*combat, "TRUE_GRIT", combat->draw, 1, 1);
    if (!picked.empty()) co_await cmd::exhaustCard(*combat, picked[0]);
  }
  void onUpgrade() override { upgradeVar("Summon", 2); }
};

// Countdown.cs: Power, apply 6 (+3 upgraded) Countdown.
struct Countdown : IroncladT<Countdown> {
  CARD_HEADER(Countdown, "COUNTDOWN", 1, Power, Uncommon, Self)
    addVar("CountdownPower", 6);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<CountdownPower>(me(), val("CountdownPower"), me(), this); }
  void onUpgrade() override { upgradeVar("CountdownPower", 3); }
};

// DanseMacabre.cs: Power, apply 4 (+2 upgraded) Danse Macabre (power Energy var is 2).
struct DanseMacabre : IroncladT<DanseMacabre> {
  CARD_HEADER(DanseMacabre, "DANSE_MACABRE", 1, Power, Uncommon, Self)
    addVar("DanseMacabrePower", 4);
    addVar("Energy", 2);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<DanseMacabrePower>(me(), val("DanseMacabrePower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("DanseMacabrePower", 2); }
};

// DeathMarch.cs: deal 8 (+1 upgraded) + 4 (+2 upgraded) per card drawn this turn outside the
// hand draw (CardDrawnEntry with !FromHandDraw).
// PORT NOTE: the C# counts CombatHistory entries; this engine has no draw history, so each card
// counts the draws it has itself observed this turn (afterCardDrawn). A copy generated mid-turn
// misses the draws before it existed.
struct DeathMarch : IroncladT<DeathMarch> {
  CARD_HEADER(DeathMarch, "DEATH_MARCH", 1, Attack, Uncommon, AnyEnemy)
    addVar("CalculationBase", 8);
    addVar("ExtraDamage", 4);
    addVar("CalculatedDamage", 0);
    calcMultiplier = [](Card* c) { return static_cast<DeathMarch*>(c)->drawsThisTurn(); };
  }
  int drawTurn = -1, drawCount = 0;
  int drawsThisTurn() const { return combat && drawTurn == combat->turnNumber ? drawCount : 0; }
  Task<> afterCardDrawn(Card*, bool fromHandDraw) override {
    if (fromHandDraw || !combat) return {};
    if (drawTurn != combat->turnNumber) { drawTurn = combat->turnNumber; drawCount = 0; }
    ++drawCount;
    return {};
  }
  Task<> onPlay(CardPlay& p) override { co_await attackCalculated(p.target); }
  void onUpgrade() override {
    upgradeVar("CalculationBase", 1);
    upgradeVar("ExtraDamage", 2);
  }
};

// Deathbringer.cs: apply 21 (+5 upgraded) Doom, then 1 Weak, to every hittable enemy.
struct Deathbringer : IroncladT<Deathbringer> {
  CARD_HEADER(Deathbringer, "DEATHBRINGER", 2, Skill, Uncommon, AllEnemies)
    addVar("DoomPower", 21);
    addVar("WeakPower", 1);
  }
  Task<> onPlay(CardPlay&) override {
    for (Creature* e : combat->hittableEnemies()) co_await applyPower<DoomPower>(e, val("DoomPower"), me(), this);
    for (Creature* e : combat->hittableEnemies()) co_await applyPower<WeakPower>(e, val("WeakPower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("DoomPower", 5); }
};

// DeathsDoor.cs: gain 6 (+1 upgraded) Block, and Repeat (2) more times if you applied Doom this
// turn (a PowerReceivedEntry for DoomPower applied by the owner).
// PORT NOTE: no power-received history: each card records the turn it last saw the player apply
// Doom (afterPowerAmountChanged); a copy generated mid-turn misses earlier applications.
struct DeathsDoor : IroncladT<DeathsDoor> {
  CARD_HEADER(DeathsDoor, "DEATHS_DOOR", 1, Skill, Uncommon, Self)
    addVar("Block", 6);
    addVar("Repeat", 2);
  }
  int doomTurn = -1;
  bool wasDoomAppliedThisTurn() const { return combat && doomTurn == combat->turnNumber; }
  Task<> afterPowerAmountChanged(Power* p, Dec, Creature* applier, Card*) override {
    if (combat && p->id == "DoomPower" && applier == combat->player) doomTurn = combat->turnNumber;
    return {};
  }
  Task<> onPlay(CardPlay&) override {
    int gains = 1;
    if (wasDoomAppliedThisTurn()) gains += val("Repeat").toInt();
    for (int i = 0; i < gains; ++i) co_await block(val("Block"));
  }
  void onUpgrade() override { upgradeVar("Block", 1); }
};

// Debilitate.cs: deal 10 (+2 upgraded) damage, then apply 2 (+1 upgraded) Debilitate.
struct Debilitate : IroncladT<Debilitate> {
  CARD_HEADER(Debilitate, "DEBILITATE", 1, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 10);
    addVar("DebilitatePower", 2);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<DebilitatePower>(p.target, val("DebilitatePower"), me(), this);
  }
  void onUpgrade() override {
    upgradeVar("Damage", 2);
    upgradeVar("DebilitatePower", 1);
  }
};

// Delay.cs: gain 11 (+2 upgraded) Block and 1 (+1 upgraded) energy next turn.
struct Delay : IroncladT<Delay> {
  CARD_HEADER(Delay, "DELAY", 2, Skill, Uncommon, Self)
    addVar("Block", 11);
    addVar("Energy", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    co_await applyPower<EnergyNextTurnPower>(me(), val("Energy").toInt(), me(), this);
  }
  void onUpgrade() override {
    upgradeVar("Block", 2);
    upgradeVar("Energy", 1);
  }
};

// Dirge.cs: X cost, Exhaust. Summon Osty for 3 (+1 upgraded) HP X times, then add X Souls to
// random spots in the draw pile (pre-upgraded if Dirge is upgraded).
struct Dirge : IroncladT<Dirge> {
  CARD_HEADER(Dirge, "DIRGE", 0, Skill, Uncommon, Self)
    costsX = true;
    keywords = kwExhaust;
    addVar("Summon", 3);
  }
  Task<> onPlay(CardPlay&) override {
    int x = xValue;
    for (int i = 0; i < x; ++i) co_await summonOsty(*combat, val("Summon").toInt());
    for (int i = 0; i < x; ++i) co_await addSoulToDrawPileRandom(*combat, upgraded());
  }
  void onUpgrade() override { upgradeVar("Summon", 1); }
};

// Dredge.cs: Exhaust. Choose up to 3 cards from the discard pile (limited by room in the 10-card
// hand) and put them into your hand; upgrading adds Retain.
struct Dredge : IroncladT<Dredge> {
  CARD_HEADER(Dredge, "DREDGE", 1, Skill, Uncommon, Self)
    keywords = kwExhaust;
    addVar("Cards", 3);
  }
  Task<> onPlay(CardPlay&) override {
    int num = std::min(val("Cards").toInt(), 10 - (int)combat->hand.size());  // CardPile.MaxCardsInHand
    if (num <= 0) co_return;
    int n = std::min(num, (int)combat->discard.size());
    auto picked = co_await cmd::selectCards(*combat, "DREDGE", combat->discard, n, n);
    for (Card* c : picked) co_await cmd::moveCard(*combat, c, Pile::Hand);
  }
  void onUpgrade() override { addKeyword(kwRetain); }
};

// EnfeeblingTouch.cs: Ethereal, the target loses 8 (+3 upgraded) Strength this turn.
struct EnfeeblingTouch : IroncladT<EnfeeblingTouch> {
  CARD_HEADER(EnfeeblingTouch, "ENFEEBLING_TOUCH", 1, Skill, Uncommon, AnyEnemy)
    keywords = kwEthereal;
    addVar("StrengthLoss", 8);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await applyPower<EnfeeblingTouchPower>(p.target, val("StrengthLoss"), me(), this);
  }
  void onUpgrade() override { upgradeVar("StrengthLoss", 3); }
};

// Fetch.cs: 0 cost, OstyAttack. If Osty is alive, Osty hits for 3 (+3 upgraded); draw 1 card the
// first time this card is played this turn.
// PORT NOTE: "played this turn" is a per-card turn stamp (the C# reads CardPlaysFinished).
struct Fetch : IroncladT<Fetch> {
  CARD_HEADER(Fetch, "FETCH", 0, Attack, Uncommon, AnyEnemy)
    tags = tagOstyAttack;
    addVar("OstyDamage", 3);
    addVar("Cards", 1);
  }
  int playedTurn = -1;
  Task<> onPlay(CardPlay& p) override {
    bool already = playedTurn == combat->turnNumber;
    playedTurn = combat->turnNumber;
    if (ostyMissing(*combat)) co_return;
    cmd::Attack a;
    a.damagePerHit = val("OstyDamage");
    a.attacker = combat->osty;
    a.source = this;
    a.single = p.target;
    co_await a.execute(*combat);
    if (!already) co_await drawCards(val("Cards"));
  }
  void onUpgrade() override { upgradeVar("OstyDamage", 3); }
};

// Friendship.cs: Power, lose 2 (-1 upgraded) Strength and gain 1 max energy (FriendshipPower).
struct Friendship : IroncladT<Friendship> {
  CARD_HEADER(Friendship, "FRIENDSHIP", 1, Power, Uncommon, Self)
    addVar("StrengthPower", 2);
    addVar("Energy", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<StrengthPower>(me(), -val("StrengthPower"), me(), this);
    co_await applyPower<FriendshipPower>(me(), val("Energy"), me(), this);
  }
  void onUpgrade() override { upgradeVar("StrengthPower", -1); }
};

}  // namespace

void registerNecrobinderUncommonCards1() {
  registerPowerType<BorrowedTimePower>();
  registerPowerType<CalcifyPower>();
  registerPowerType<CountdownPower>();
  registerPowerType<DanseMacabrePower>();
  registerPowerType<DebilitatePower>();
  registerPowerType<EnfeeblingTouchPower>();
  registerPowerType<FriendshipPower>();
  registerCardType<BoneShards>();
  registerCardType<BorrowedTime>();
  registerCardType<Bury>();
  registerCardType<Calcify>();
  registerCardType<CaptureSpirit>();
  registerCardType<Cleanse>();
  registerCardType<Countdown>();
  registerCardType<DanseMacabre>();
  registerCardType<DeathMarch>();
  registerCardType<Deathbringer>();
  registerCardType<DeathsDoor>();
  registerCardType<Debilitate>();
  registerCardType<Delay>();
  registerCardType<Dirge>();
  registerCardType<Dredge>();
  registerCardType<EnfeeblingTouch>();
  registerCardType<Fetch>();
  registerCardType<Friendship>();
}

}  // namespace sts
