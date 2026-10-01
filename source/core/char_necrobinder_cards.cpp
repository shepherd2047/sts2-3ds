// The Necrobinder's Common card pool (X4.2), from Models.Cards / CardPools.NecrobinderCardPool.
// Uncommon/Rare/Ancient cards are ported separately (X4.3a/X4.3b/X4.4). Written like the
// Ironclad's own commons (content.cpp): IroncladT<> + CARD_HEADER, DynVars named like the C#'s
// DynamicVars (DynamicVars.<Name>.defaultName), tagOstyAttack for Osty-attack cards. Cards that
// deal damage through Osty build a raw cmd::Attack with `attacker = combat->osty` (as Unleash
// already does in char_necrobinder.cpp): a missing/dead Osty makes Attack::execute a no-op,
// matching the C#'s `Osty.CheckMissingWithAnim` early-out without a separate check.
#include "cards.h"
#include "char_necrobinder.h"

namespace sts {

namespace {

// Afterlife.cs: Exhaust, summon Osty for 6 (+3 upgraded) HP (or raise his max HP by that much).
// PORT NOTE: TriggerAnim (Necrobinder.GetSummonAnimIfApplicable) is UI, not ported (as Bodyguard
// already notes).
struct Afterlife : IroncladT<Afterlife> {
  CARD_HEADER(Afterlife, "AFTERLIFE", 1, Skill, Common, Self)
    keywords = kwExhaust;
    addVar("Summon", 6);
  }
  Task<> onPlay(CardPlay&) override { co_await summonOsty(*combat, val("Summon").toInt()); }
  void onUpgrade() override { upgradeVar("Summon", 3); }
};

// BlightStrike.cs: Strike, deal 8 (+2 upgraded) damage, then apply Doom to the target equal to
// the total damage dealt (blocked + unblocked, DamageResult.TotalDamage).
struct BlightStrike : IroncladT<BlightStrike> {
  CARD_HEADER(BlightStrike, "BLIGHT_STRIKE", 1, Attack, Common, AnyEnemy)
    tags = tagStrike;
    addVar("Damage", 8);
  }
  Task<> onPlay(CardPlay& p) override {
    cmd::Attack a;
    a.damagePerHit = val("Damage");
    a.attacker = me();
    a.source = this;
    a.single = p.target;
    co_await a.execute(*combat);
    int total = 0;
    for (auto& hit : a.results)
      for (auto& r : hit) total += r.blocked + r.unblocked;
    co_await applyPower<DoomPower>(p.target, total, me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 2); }
};

// Defile.cs: Ethereal, deal 13 (+4 upgraded) damage.
struct Defile : IroncladT<Defile> {
  CARD_HEADER(Defile, "DEFILE", 1, Attack, Common, AnyEnemy)
    keywords = kwEthereal;
    addVar("Damage", 13);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
  void onUpgrade() override { upgradeVar("Damage", 4); }
};

// Defy.cs: Ethereal, gain 6 (+3 upgraded) Block, apply 1 Weak.
struct Defy : IroncladT<Defy> {
  CARD_HEADER(Defy, "DEFY", 1, Skill, Common, AnyEnemy)
    keywords = kwEthereal;
    addVar("Block", 6);
    addVar("WeakPower", 1);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await block(val("Block"));
    co_await applyPower<WeakPower>(p.target, val("WeakPower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

// DrainPower.cs: deal 10 (+2 upgraded) damage, then upgrade 2 (+1 upgraded) random upgradable
// cards in the discard pile (TakeRandom(Cards, CombatCardSelection) -- shuffle + take-first-N, the
// idiom already used for other TakeRandom ports in this codebase, e.g. ancients_later.cpp).
// PORT NOTE: CardCmd.Preview (highlighting the upgraded cards) is UI, not ported.
struct DrainPower : IroncladT<DrainPower> {
  CARD_HEADER(DrainPower, "DRAIN_POWER", 1, Attack, Common, AnyEnemy)
    addVar("Damage", 10);
    addVar("Cards", 2);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    std::vector<Card*> opts;
    for (Card* c : combat->discard)
      if (c->upgradable()) opts.push_back(c);
    combat->rng("CombatCardSelection").shuffle(opts);
    int n = std::min((int)opts.size(), val("Cards").toInt());
    for (int i = 0; i < n; ++i) cmd::upgradeCard(opts[i]);
  }
  void onUpgrade() override {
    upgradeVar("Damage", 2);
    upgradeVar("Cards", 1);
  }
};

// Fear.cs: Ethereal, deal 7 (+1 upgraded) damage, apply 1 (+1 upgraded) Vulnerable.
struct Fear : IroncladT<Fear> {
  CARD_HEADER(Fear, "FEAR", 1, Attack, Common, AnyEnemy)
    keywords = kwEthereal;
    addVar("Damage", 7);
    addVar("VulnerablePower", 1);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<VulnerablePower>(p.target, val("VulnerablePower"), me(), this);
  }
  void onUpgrade() override {
    upgradeVar("Damage", 1);
    upgradeVar("VulnerablePower", 1);
  }
};

// Flatten.cs: OstyAttack, deal 12 (+4 upgraded) Osty damage; costs 0 for the rest of the turn once
// Osty has landed an attack this turn (AfterCardEnteredCombat covers the card being drawn/added
// after that already happened; AfterAttack covers it happening while the card is already in hand).
struct Flatten : IroncladT<Flatten> {
  CARD_HEADER(Flatten, "FLATTEN", 2, Attack, Common, AnyEnemy)
    tags = tagOstyAttack;
    addVar("OstyDamage", 12);
  }
  // A CreatureAttacked entry by Osty this turn.
  bool hasOstyAttackedThisTurn() const {
    return combat && combat->osty && combat->history.countThisTurn(*combat, CombatHistoryEntry::CreatureAttacked, [&](const CombatHistoryEntry& e) {
      return e.actor == combat->osty;
    }) > 0;
  }
  Task<> onPlay(CardPlay& p) override {
    cmd::Attack a;
    a.damagePerHit = val("OstyDamage");
    a.attacker = combat->osty;
    a.source = this;
    a.single = p.target;
    co_await a.execute(*combat);
  }
  void onUpgrade() override { upgradeVar("OstyDamage", 4); }
  Task<> afterCardEnteredCombat(Card* card) override {
    if (card == this && hasOstyAttackedThisTurn()) setThisTurn(0);
    return {};
  }
  Task<> afterAttack(const cmd::Attack& a) override {
    if (combat && a.attacker && a.attacker == combat->osty) setThisTurn(0);
    return {};
  }
};

// GraveWarden.cs: gain 8 (+3 upgraded) Block, add 1 Soul to a random spot in the draw pile.
struct GraveWarden : IroncladT<GraveWarden> {
  CARD_HEADER(GraveWarden, "GRAVE_WARDEN", 1, Skill, Common, Self)
    addVar("Block", 8);
    addVar("Cards", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    int n = val("Cards").toInt();
    for (int i = 0; i < n; ++i) co_await addSoulToDrawPileRandom(*combat);
  }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

// Graveblast.cs: Exhaust, deal 4 (+2 upgraded) damage, then look at the discard pile and add a
// card to hand (Exhaust removed on upgrade).
struct Graveblast : IroncladT<Graveblast> {
  CARD_HEADER(Graveblast, "GRAVEBLAST", 1, Attack, Common, AnyEnemy)
    keywords = kwExhaust;
    addVar("Damage", 4);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    auto picked = co_await cmd::selectCards(*combat, "GRAVEBLAST", combat->discard, 0, 1);
    if (!picked.empty()) co_await cmd::moveCard(*combat, picked[0], Pile::Hand);
  }
  void onUpgrade() override {
    upgradeVar("Damage", 2);
    removeKeyword(kwExhaust);
  }
};

// Invoke.cs: next turn, summon Osty for 2 (+1 upgraded) HP and gain 2 (+1 upgraded) energy
// (SummonNextTurnPower / EnergyNextTurnPower, both Counter powers that remove themselves once
// they fire).
struct Invoke : IroncladT<Invoke> {
  CARD_HEADER(Invoke, "INVOKE", 1, Skill, Common, Self)
    addVar("Summon", 2);
    addVar("Energy", 2);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<SummonNextTurnPower>(me(), val("Summon"), me(), this);
    co_await applyPower<EnergyNextTurnPower>(me(), val("Energy"), me(), this);
  }
  void onUpgrade() override {
    upgradeVar("Summon", 1);
    upgradeVar("Energy", 1);
  }
};

// NegativePulse.cs: gain 5 (+1 upgraded) Block, apply 7 (+4 upgraded) Doom to every hittable
// enemy.
struct NegativePulse : IroncladT<NegativePulse> {
  CARD_HEADER(NegativePulse, "NEGATIVE_PULSE", 1, Skill, Common, AllEnemies)
    addVar("Block", 5);
    addVar("DoomPower", 7);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    for (Creature* e : combat->hittableEnemies()) co_await applyPower<DoomPower>(e, val("DoomPower"), me(), this);
  }
  void onUpgrade() override {
    upgradeVar("Block", 1);
    upgradeVar("DoomPower", 4);
  }
};

// Poke.cs: 0 cost, OstyAttack, deal 6 (+3 upgraded) Osty damage.
struct Poke : IroncladT<Poke> {
  CARD_HEADER(Poke, "POKE", 0, Attack, Common, AnyEnemy)
    tags = tagOstyAttack;
    addVar("OstyDamage", 6);
  }
  Task<> onPlay(CardPlay& p) override {
    cmd::Attack a;
    a.damagePerHit = val("OstyDamage");
    a.attacker = combat->osty;
    a.source = this;
    a.single = p.target;
    co_await a.execute(*combat);
  }
  void onUpgrade() override { upgradeVar("OstyDamage", 3); }
};

// PullAggro.cs: summon Osty for 4 (+1 upgraded) HP, gain 7 (+2 upgraded) Block.
struct PullAggro : IroncladT<PullAggro> {
  CARD_HEADER(PullAggro, "PULL_AGGRO", 2, Skill, Common, Self)
    addVar("Summon", 4);
    addVar("Block", 7);
  }
  Task<> onPlay(CardPlay&) override {
    co_await summonOsty(*combat, val("Summon").toInt());
    co_await block(val("Block"));
  }
  void onUpgrade() override {
    upgradeVar("Summon", 1);
    upgradeVar("Block", 2);
  }
};

// Reap.cs: Retain, deal 27 (+6 upgraded) damage.
struct Reap : IroncladT<Reap> {
  CARD_HEADER(Reap, "REAP", 3, Attack, Common, AnyEnemy)
    keywords = kwRetain;
    addVar("Damage", 27);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
  void onUpgrade() override { upgradeVar("Damage", 6); }
};

// Reave.cs: deal 10 (+3 upgraded) damage, add 1 Soul to a random spot in the draw pile (the Soul
// is itself pre-upgraded if Reave is upgraded).
struct Reave : IroncladT<Reave> {
  CARD_HEADER(Reave, "REAVE", 1, Attack, Common, AnyEnemy)
    addVar("Damage", 10);
    addVar("Cards", 1);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    int n = val("Cards").toInt();
    for (int i = 0; i < n; ++i) co_await addSoulToDrawPileRandom(*combat, upgraded());
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

// Scourge.cs: apply 13 (+3 upgraded) Doom to the target, draw 1 (+1 upgraded) card.
struct Scourge : IroncladT<Scourge> {
  CARD_HEADER(Scourge, "SCOURGE", 1, Skill, Common, AnyEnemy)
    addVar("DoomPower", 13);
    addVar("Cards", 1);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await applyPower<DoomPower>(p.target, val("DoomPower"), me(), this);
    co_await drawCards(val("Cards"));
  }
  void onUpgrade() override {
    upgradeVar("DoomPower", 3);
    upgradeVar("Cards", 1);
  }
};

// SculptingStrike.cs: Strike, deal 9 (+3 upgraded) damage, then apply Ethereal to a card in hand
// that doesn't already have it.
struct SculptingStrike : IroncladT<SculptingStrike> {
  CARD_HEADER(SculptingStrike, "SCULPTING_STRIKE", 1, Attack, Common, AnyEnemy)
    tags = tagStrike;
    addVar("Damage", 9);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    std::vector<Card*> opts;
    for (Card* c : combat->hand)
      if (!c->has(kwEthereal)) opts.push_back(c);
    auto picked = co_await cmd::selectCards(*combat, "SCULPTING_STRIKE", opts, 1, 1);
    if (!picked.empty()) picked[0]->addKeyword(kwEthereal);
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

// Snap.cs: OstyAttack, deal 7 (+3 upgraded) Osty damage, then apply Retain to a card in hand that
// doesn't already have it.
struct Snap : IroncladT<Snap> {
  CARD_HEADER(Snap, "SNAP", 1, Attack, Common, AnyEnemy)
    tags = tagOstyAttack;
    addVar("OstyDamage", 7);
  }
  Task<> onPlay(CardPlay& p) override {
    cmd::Attack a;
    a.damagePerHit = val("OstyDamage");
    a.attacker = combat->osty;
    a.source = this;
    a.single = p.target;
    co_await a.execute(*combat);
    std::vector<Card*> opts;
    for (Card* c : combat->hand)
      if (!c->has(kwRetain)) opts.push_back(c);
    auto picked = co_await cmd::selectCards(*combat, "SNAP", opts, 1, 1);
    if (!picked.empty()) picked[0]->addKeyword(kwRetain);
  }
  void onUpgrade() override { upgradeVar("OstyDamage", 3); }
};

// Sow.cs: Retain, deal 8 (+3 upgraded) damage to all enemies.
struct Sow : IroncladT<Sow> {
  CARD_HEADER(Sow, "SOW", 1, Attack, Common, AllEnemies)
    keywords = kwRetain;
    addVar("Damage", 8);
  }
  Task<> onPlay(CardPlay&) override { co_await attackAll(val("Damage")); }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

// Wisp.cs: 0 cost, Exhaust, gain 1 energy; upgrading adds Retain instead of changing the numbers.
struct Wisp : IroncladT<Wisp> {
  CARD_HEADER(Wisp, "WISP", 0, Skill, Common, Self)
    keywords = kwExhaust;
    addVar("Energy", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await cmd::gainEnergy(*combat, val("Energy").toInt()); }
  void onUpgrade() override { addKeyword(kwRetain); }
};

}  // namespace

void registerNecrobinderCards() {
  registerCardType<Afterlife>();
  registerCardType<BlightStrike>();
  registerCardType<Defile>();
  registerCardType<Defy>();
  registerCardType<DrainPower>();
  registerCardType<Fear>();
  registerCardType<Flatten>();
  registerCardType<GraveWarden>();
  registerCardType<Graveblast>();
  registerCardType<Invoke>();
  registerCardType<NegativePulse>();
  registerCardType<Poke>();
  registerCardType<PullAggro>();
  registerCardType<Reap>();
  registerCardType<Reave>();
  registerCardType<Scourge>();
  registerCardType<SculptingStrike>();
  registerCardType<Snap>();
  registerCardType<Sow>();
  registerCardType<Wisp>();
}

}  // namespace sts
