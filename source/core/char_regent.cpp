// The Regent's shared systems (X3.0), see char_regent.h. The Sovereign Blade token card lives
// here because ForgeCmd.Forge (used by many Regent cards) creates and grows it.
#include "cards.h"
#include "char_regent.h"

namespace sts {

void registerRegentRelics();   // char_regent_relics.cpp
void registerRegentPotions();  // char_regent_relics.cpp
void registerRegentCards();    // char_regent_cards.cpp: the Regent's common cards (X3.2)
void registerRegentUncommonCards1();  // char_regent_cards_uncommon1.cpp (X3.3a)
void registerRegentRareCards();  // char_regent_cards_rare.cpp (X3.4)

namespace {

// StrikeRegent.cs / DefendRegent.cs: same numbers as StrikeIronclad / DefendIronclad (only
// portrait, attack vfx and colour differ in the C#).
struct StrikeRegent : IroncladT<StrikeRegent> {
  CARD_HEADER(StrikeRegent, "STRIKE_REGENT", 1, Attack, Basic, AnyEnemy)
    tags = tagStrike;
    addVar("Damage", 6);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

struct DefendRegent : IroncladT<DefendRegent> {
  CARD_HEADER(DefendRegent, "DEFEND_REGENT", 1, Skill, Basic, Self)
    tags = tagDefend;
    addVar("Block", 5);
  }
  Task<> onPlay(CardPlay&) override { co_await block(val("Block")); }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

// FallingStar.cs: 0 cost, 2 stars, Attack. Damage 8, then Weak 1 and Vulnerable 1 on the target.
struct FallingStar : IroncladT<FallingStar> {
  CARD_HEADER(FallingStar, "FALLING_STAR", 0, Attack, Basic, AnyEnemy)
    starCost = 2;
    addVar("Damage", 8);
    addVar("WeakPower", 1);
    addVar("VulnerablePower", 1);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<WeakPower>(p.target, val("WeakPower"), me(), this);
    co_await applyPower<VulnerablePower>(p.target, val("VulnerablePower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 4); }
};

// Venerate.cs: 1 cost, Skill, Self. Gain 2 stars (the generic non-Attack "Cast" anim already
// fires from Combat::playCard).
struct Venerate : IroncladT<Venerate> {
  CARD_HEADER(Venerate, "VENERATE", 1, Skill, Basic, Self)
    addVar("Stars", 2);
  }
  Task<> onPlay(CardPlay&) override { co_await cmd::gainStars(*combat, val("Stars").toInt()); }
  void onUpgrade() override { upgradeVar("Stars", 1); }
};

// SovereignBlade.cs: 2 cost, Attack, Token, Retain. Damage 10 (+CalculatedBlock via Parry),
// Repeat 1. Targets every enemy while the player has SeekingEdgePower, else AnyEnemy.
struct SovereignBlade : IroncladT<SovereignBlade> {
  CARD_HEADER(SovereignBlade, "SOVEREIGN_BLADE", 2, Attack, Token, AnyEnemy)
    keywords = kwRetain;
    tags = tagSovereignBlade;
    addVar("Damage", 10);
    addVar("Repeat", 1);
    addVar("CalculationBase", 0);
    addVar("CalculationExtra", 1);
    addVar("CalculatedBlock", 0);
    calcMultiplier = [](Card* c) { return c->combat ? c->combat->player->powerAmount<ParryPower>() : 0; };
  }
  bool createdThroughForge = false;

  void addDamage(Dec amount) { if (auto* v = var("Damage")) v->base += amount; }

  // CardModel.GainsBlock override: true while the player has Parry (block preview only).
  bool gainsBlock() const override {
    return combat && combat->player->powerAmount<ParryPower>() > 0;
  }

  Task<> onPlay(CardPlay& p) override {
    if (combat->player->get<SeekingEdgePower>()) co_await attackAll(val("Damage"), val("Repeat").toInt());
    else co_await attack(p.target, val("Damage"), val("Repeat").toInt());
    if (combat->player->powerAmount<ParryPower>() > 0) co_await block(calculatedBlock());
  }
  void onUpgrade() override { cost -= 1; }

  // AfterCloned: a dupe is not itself forged.
  std::unique_ptr<Card> clone() const override {
    auto c = std::make_unique<SovereignBlade>(*this);
    c->adoptEnchantment();
    c->createdThroughForge = false;
    return c;
  }
  // PORT NOTE: AfterDowngraded (restoring Damage/Repeat to the values from before a temporary
  // enchant/upgrade preview) isn't modeled — this engine has no downgrade mechanic, as noted in
  // content_rare.cpp (Thrash).
};

}  // namespace

namespace cmd {

Task<std::vector<Card*>> forge(Combat& c, Dec amount, Model* source) {
  std::vector<Card*> blades;
  if (c.over || c.ending) co_return blades;
  // GetSovereignBlades(includeExhausted: false): not a dupe, and not in the exhaust pile
  // (a card that hasn't joined a pile yet, e.g. mid-creation, counts as un-exhausted too).
  for (Card* k : c.allCards())
    if (k->id == "SovereignBlade" && !k->isDupe && c.pileOf(k) != Pile::Exhaust) blades.push_back(k);
  if (blades.empty()) {
    Card* nb = co_await cmd::addGeneratedCard(c, db::card("SovereignBlade"), Pile::Hand);
    static_cast<SovereignBlade*>(nb)->createdThroughForge = true;
    blades.push_back(nb);
  }
  // IncreaseSovereignBladeDamage: every Sovereign Blade, including exhausted ones.
  for (Card* k : c.allCards())
    if (k->id == "SovereignBlade" && !k->isDupe) static_cast<SovereignBlade*>(k)->addDamage(amount);
  for (Model* m : c.listeners()) co_await m->afterForge(amount, source);
  co_return blades;
}

}  // namespace cmd

void registerRegent() {
  registerPowerType<StarNextTurnPower>();
  registerPowerType<SeekingEdgePower>();
  registerPowerType<ParryPower>();
  registerPowerType<DrawCardsNextTurnPower>();
  registerCardType<SovereignBlade>();
  registerCardType<StrikeRegent>();
  registerCardType<DefendRegent>();
  registerCardType<FallingStar>();
  registerCardType<Venerate>();
  registerRegentRelics();
  registerRegentPotions();
  registerRegentCards();
  registerRegentUncommonCards1();
  registerRegentRareCards();
}

}  // namespace sts
