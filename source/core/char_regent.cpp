// The Regent's shared systems (X3.0), see char_regent.h. The Sovereign Blade token card lives
// here because ForgeCmd.Forge (used by many Regent cards) creates and grows it.
#include "cards.h"
#include "char_regent.h"

namespace sts {

namespace {

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
  registerCardType<SovereignBlade>();
}

}  // namespace sts
