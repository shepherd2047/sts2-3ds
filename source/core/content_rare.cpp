// Translated rare and ancient Ironclad cards (MegaCrit.Sts2.Core.Models.Cards).
// Values are the non-ascension ones; VFX/SFX/animation code is dropped per
// docs/PORTING.md. CardRarity.Ancient cards use Rarity::Rare (this engine has
// no separate Ancient rarity).
#include "cards.h"

namespace sts {

struct Break : IroncladT<Break> {
  CARD_HEADER(Break, "BREAK", 1, Attack, Ancient, AnyEnemy)
    addVar("Damage", 20);
    addVar("VulnerablePower", 5);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<VulnerablePower>(p.target, val("VulnerablePower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 10); upgradeVar("VulnerablePower", 2); }
};

struct Corruption : IroncladT<Corruption> {
  CARD_HEADER(Corruption, "CORRUPTION", 3, Power, Ancient, Self)
    addVar("Power", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<CorruptionPower>(me(), val("Power"), me(), this); }
  void onUpgrade() override { cost -= 1; }
};

struct Aggression : IroncladT<Aggression> {
  CARD_HEADER(Aggression, "AGGRESSION", 1, Power, Rare, Self)
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<AggressionPower>(me(), 1, me(), this); }
  void onUpgrade() override { keywords |= kwInnate; }
};

struct Barricade : IroncladT<Barricade> {
  CARD_HEADER(Barricade, "BARRICADE", 3, Power, Rare, Self)
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<BarricadePower>(me(), 1, me(), this); }
  void onUpgrade() override { cost -= 1; }
};

struct Brand : IroncladT<Brand> {
  CARD_HEADER(Brand, "BRAND", 0, Skill, Rare, Self)
    addVar("HpLoss", 1);
    addVar("StrengthPower", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await loseHp(val("HpLoss"));
    if (!combat->hand.empty()) {
      auto picked = co_await cmd::selectCards(*combat, "BRAND", combat->hand, 1, 1);
      if (!picked.empty()) co_await cmd::exhaustCard(*combat, picked[0]);
    }
    co_await applyPower<StrengthPower>(me(), val("StrengthPower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("StrengthPower", 1); }
};

struct Cascade : IroncladT<Cascade> {
  CARD_HEADER(Cascade, "CASCADE", -1, Skill, Rare, Self)
    costsX = true;
  }
  Task<> onPlay(CardPlay&) override {
    int n = xValue;
    if (upgraded()) ++n;
    co_await cmd::autoPlayFromDrawPile(*combat, n, false);
  }
};

struct Conflagration : IroncladT<Conflagration> {
  CARD_HEADER(Conflagration, "CONFLAGRATION", 1, Attack, Rare, AllEnemies)
    addVar("Damage", 2);
    addVar("Repeat", 4);
  }
  Task<> onPlay(CardPlay&) override { co_await attackAll(val("Damage"), val("Repeat").toInt()); }
  void onUpgrade() override { upgradeVar("Repeat", 1); }
};

struct CrimsonMantle : IroncladT<CrimsonMantle> {
  CARD_HEADER(CrimsonMantle, "CRIMSON_MANTLE", 1, Power, Rare, Self)
    addVar("CrimsonMantlePower", 7);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<CrimsonMantlePower>(me(), val("CrimsonMantlePower"), me(), this);
    if (auto* pw = me()->get<CrimsonMantlePower>()) pw->incrementSelfDamage();
  }
  void onUpgrade() override { upgradeVar("CrimsonMantlePower", 3); }
};

struct DarkEmbrace : IroncladT<DarkEmbrace> {
  CARD_HEADER(DarkEmbrace, "DARK_EMBRACE", 2, Power, Rare, Self)
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<DarkEmbracePower>(me(), 1, me(), this); }
  void onUpgrade() override { cost -= 1; }
};

struct DemonForm : IroncladT<DemonForm> {
  CARD_HEADER(DemonForm, "DEMON_FORM", 3, Power, Rare, Self)
    addVar("StrengthPower", 3);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<DemonFormPower>(me(), val("StrengthPower"), me(), this); }
  void onUpgrade() override { upgradeVar("StrengthPower", 1); }
};

struct Dominate : IroncladT<Dominate> {
  CARD_HEADER(Dominate, "DOMINATE", 1, Skill, Rare, AnyEnemy)
    keywords = kwExhaust;
    addVar("VulnerablePower", 1);
    addVar("StrengthPerVulnerable", 1);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await applyPower<VulnerablePower>(p.target, val("VulnerablePower"), me(), this);
    int n = p.target->powerAmount<VulnerablePower>();
    co_await applyPower<StrengthPower>(me(), Dec(n) * val("StrengthPerVulnerable"), me(), this);
  }
  void onUpgrade() override { upgradeVar("VulnerablePower", 1); }
};

struct Feed : IroncladT<Feed> {
  bool canBeGeneratedInCombat() const override { return false; }
  CARD_HEADER(Feed, "FEED", 1, Attack, Rare, AnyEnemy)
    keywords = kwExhaust;
    addVar("Damage", 10);
    addVar("MaxHp", 3);
  }
  Task<> onPlay(CardPlay& p) override {
    bool fatal = p.target && p.target->deathIsFatal();  // checked before the hit, as in C#
    cmd::Attack a;
    a.damagePerHit = val("Damage");
    a.hits = 1;
    a.attacker = me();
    a.source = this;
    a.single = p.target;
    co_await a.execute(*combat);
    bool killed = false;
    for (auto& hitResults : a.results)
      for (auto& r : hitResults)
        if (r.killed) killed = true;
    if (killed && fatal) co_await cmd::gainMaxHp(me(), val("MaxHp").toInt());
  }
  void onUpgrade() override { upgradeVar("Damage", 2); upgradeVar("MaxHp", 1); }
};

struct FiendFire : IroncladT<FiendFire> {
  CARD_HEADER(FiendFire, "FIEND_FIRE", 2, Attack, Rare, AnyEnemy)
    keywords = kwExhaust;
    addVar("Damage", 7);
  }
  Task<> onPlay(CardPlay& p) override {
    std::vector<Card*> handCopy = combat->hand;
    int cardCount = (int)handCopy.size();
    for (Card* c : handCopy) co_await cmd::exhaustCard(*combat, c);
    co_await attack(p.target, val("Damage"), cardCount);
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

struct Hellraiser : IroncladT<Hellraiser> {
  CARD_HEADER(Hellraiser, "HELLRAISER", 2, Power, Rare, Self)
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<HellraiserPower>(me(), 1, me(), this); }
  void onUpgrade() override { cost -= 1; }
};

struct Impervious : IroncladT<Impervious> {
  CARD_HEADER(Impervious, "IMPERVIOUS", 2, Skill, Rare, Self)
    keywords = kwExhaust;
    addVar("Block", 30);
  }
  Task<> onPlay(CardPlay&) override { co_await block(val("Block")); }
  void onUpgrade() override { upgradeVar("Block", 10); }
};

struct Juggernaut : IroncladT<Juggernaut> {
  CARD_HEADER(Juggernaut, "JUGGERNAUT", 2, Power, Rare, Self)
    addVar("JuggernautPower", 6);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<JuggernautPower>(me(), val("JuggernautPower"), me(), this); }
  void onUpgrade() override { upgradeVar("JuggernautPower", 2); }
};

struct Mangle : IroncladT<Mangle> {
  CARD_HEADER(Mangle, "MANGLE", 3, Attack, Rare, AnyEnemy)
    addVar("Damage", 20);
    addVar("StrengthLoss", 10);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<ManglePower>(p.target, val("StrengthLoss"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 6); upgradeVar("StrengthLoss", 5); }
};

// PORT NOTE: C# is MultiplayerConstraint.MultiplayerOnly; this build is
// single-player only, so that restriction has nothing to gate against and is
// dropped. AfterCardEnteredCombat reduces cost by the CardExhausted entries so far
// (IsClone, a clone already carrying the reduction, is approximated by "has a
// this-combat cost modifier": the engine has no CloneOf link).
struct Midnight : IroncladT<Midnight> {
  CARD_HEADER(Midnight, "MIDNIGHT", 12, Attack, Rare, AnyEnemy)
    addVar("Damage", 60);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
  Task<> afterCardEnteredCombat(Card* card) override {
    if (card != this || !combat || !costMods.empty()) return {};
    addThisCombat(-combat->history.count(CombatHistoryEntry::CardExhausted));
    return {};
  }
  Task<> afterCardExhausted(Card*, bool) override {
    addThisCombat(-1);
    return {};
  }
  void onUpgrade() override { upgradeVar("Damage", 12); }
};

struct NotYet : IroncladT<NotYet> {
  bool canBeGeneratedInCombat() const override { return false; }
  CARD_HEADER(NotYet, "NOT_YET", 2, Skill, Rare, Self)
    keywords = kwExhaust;
    addVar("Heal", 10);
  }
  Task<> onPlay(CardPlay&) override { co_await cmd::heal(me(), val("Heal")); }
  void onUpgrade() override { upgradeVar("Heal", 3); }
};

struct Offering : IroncladT<Offering> {
  CARD_HEADER(Offering, "OFFERING", 0, Skill, Rare, Self)
    keywords = kwExhaust;
    addVar("HpLoss", 6);
    addVar("Energy", 2);
    addVar("Cards", 3);
  }
  Task<> onPlay(CardPlay&) override {
    co_await loseHp(val("HpLoss"));
    co_await cmd::gainEnergy(*combat, val("Energy").toInt());
    co_await drawCards(val("Cards"));
  }
  void onUpgrade() override { upgradeVar("Cards", 2); }
};

struct OneTwoPunch : IroncladT<OneTwoPunch> {
  CARD_HEADER(OneTwoPunch, "ONE_TWO_PUNCH", 1, Skill, Rare, Self)
    addVar("Attacks", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<OneTwoPunchPower>(me(), val("Attacks"), me(), this); }
  void onUpgrade() override { upgradeVar("Attacks", 1); }
};

struct PactsEnd : IroncladT<PactsEnd> {
  CARD_HEADER(PactsEnd, "PACTS_END", 0, Attack, Rare, AllEnemies)
    addVar("Damage", 18);
    addVar("Cards", 3);
  }
  Task<> onPlay(CardPlay&) override {
    if ((int)combat->exhaust.size() >= val("Cards").toInt()) co_await attackAll(val("Damage"));
  }
  void onUpgrade() override { upgradeVar("Damage", 6); }
};

// PrimalForce.cs: every transformable Attack in hand becomes a Giant Rock (upgraded if this is).
struct PrimalForce : IroncladT<PrimalForce> {
  CARD_HEADER(PrimalForce, "PRIMAL_FORCE", 0, Skill, Rare, Self)
  }
  Task<> onPlay(CardPlay&) override {
    std::vector<Card*> handCopy = combat->hand;
    for (Card* c : handCopy) {
      if (!c || !c->isTransformable() || c->type != CardType::Attack) continue;
      auto rock = db::card("GiantRock");
      if (!rock) continue;
      if (upgraded()) rock->upgrade();
      co_await cmd::transform(*combat, c, std::move(rock));
    }
  }
};

struct Pyre : IroncladT<Pyre> {
  CARD_HEADER(Pyre, "PYRE", 2, Power, Rare, Self)
    addVar("Energy", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<PyrePower>(me(), val("Energy"), me(), this); }
  void onUpgrade() override { upgradeVar("Energy", 1); }
};

struct Stoke : IroncladT<Stoke> {
  CARD_HEADER(Stoke, "STOKE", 1, Skill, Rare, Self)
  }
  Task<> onPlay(CardPlay&) override {
    std::vector<Card*> handCopy = combat->hand;
    int n = (int)handCopy.size();
    for (Card* c : handCopy) co_await cmd::exhaustCard(*combat, c);
    // CardFactory.GetForCombat(Owner.Character.CardPool, exhaustCount, CombatCardGeneration)
    for (auto& card : randomForCombat(*combat, db::characterPool(combat->run->characterId), n)) {
      if (upgraded() && card->upgradable()) card->upgrade();
      co_await cmd::addGeneratedCard(*combat, std::move(card), Pile::Hand);
    }
  }
};

// PORT NOTE: DamageDecrease (0.5) is only consumed by the multiplayer
// teammate-sharing side of TankPower (see powers_ironclad.h), which this
// single-player build doesn't model; the self damage-taken increase (x1.5) is
// implemented.
struct Tank : IroncladT<Tank> {
  CARD_HEADER(Tank, "TANK", 1, Power, Rare, Self)
    addVar("DamageIncrease", Dec::lit(1.5));
    addVar("DamageDecrease", Dec::lit(0.5));
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<TankPower>(me(), 1, me(), this); }
  void onUpgrade() override { cost -= 1; }
};

// CalculatedHits = 1 + the owner's DamageReceived entries with unblocked damage this combat.
struct TearAsunder : IroncladT<TearAsunder> {
  CARD_HEADER(TearAsunder, "TEAR_ASUNDER", 2, Attack, Rare, AnyEnemy)
    addVar("Damage", 5);
    addVar("Repeat", 1);
    addVar("CalculationBase", 0);
    addVar("CalculationExtra", 1);
    addVar("CalculatedHits", 1);
    calcMultiplier = [](Card* c) {
      if (!c->combat) return 1;
      Creature* me = c->combat->player;
      return 1 + c->combat->history.count([me](const CombatHistoryEntry& e) {
        return e.kind == CombatHistoryEntry::DamageReceived && e.actor == me && e.unblocked > 0;
      });
    };
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage"), calculatedBlock().toInt()); }
  void onUpgrade() override { upgradeVar("Damage", 2); }
};

// PORT NOTE: AfterDowngraded (restoring the permanently-added ExtraDamage
// when a relic/effect downgrades a card) isn't modeled — this engine has no
// downgrade mechanic — so ExtraDamage isn't tracked separately.
struct Thrash : IroncladT<Thrash> {
  CARD_HEADER(Thrash, "THRASH", 1, Attack, Rare, AnyEnemy)
    addVar("Damage", 4);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"), 2);
    std::vector<Card*> attacks;
    for (Card* c : combat->hand) if (c->type == CardType::Attack) attacks.push_back(c);
    Card* card = combat->rng("CombatCardSelection").nextItem(attacks);
    if (card) {
      Dec damage = 0;
      if (card->var("CalculatedDamage")) damage = card->calculatedDamage();
      else if (card->var("Damage")) damage = card->val("Damage");
      else if (card->var("OstyDamage")) damage = card->val("OstyDamage");
      damage = combat->modifyDamage(nullptr, me(), damage, kMove, card);
      upgradeVar("Damage", damage);
      co_await cmd::exhaustCard(*combat, card);
    }
  }
  void onUpgrade() override { upgradeVar("Damage", 2); }
};

struct Unmovable : IroncladT<Unmovable> {
  CARD_HEADER(Unmovable, "UNMOVABLE", 2, Power, Rare, Self)
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<UnmovablePower>(me(), 1, me(), this); }
  void onUpgrade() override { cost -= 1; }
};

void registerIroncladRare() {
  registerCardType<Break>();
  registerCardType<Corruption>();
  registerCardType<Aggression>();
  registerCardType<Barricade>();
  registerCardType<Brand>();
  registerCardType<Cascade>();
  registerCardType<Conflagration>();
  registerCardType<CrimsonMantle>();
  registerCardType<DarkEmbrace>();
  registerCardType<DemonForm>();
  registerCardType<Dominate>();
  registerCardType<Feed>();
  registerCardType<FiendFire>();
  registerCardType<Hellraiser>();
  registerCardType<Impervious>();
  registerCardType<Juggernaut>();
  registerCardType<Mangle>();
  registerCardType<Midnight>();
  registerCardType<NotYet>();
  registerCardType<Offering>();
  registerCardType<OneTwoPunch>();
  registerCardType<PactsEnd>();
  registerCardType<PrimalForce>();
  registerCardType<Pyre>();
  registerCardType<Stoke>();
  registerCardType<Tank>();
  registerCardType<TearAsunder>();
  registerCardType<Thrash>();
  registerCardType<Unmovable>();
}

}  // namespace sts
