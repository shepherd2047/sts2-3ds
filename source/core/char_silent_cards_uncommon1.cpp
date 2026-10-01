// The Silent's Uncommon cards, first half (X1.3a), translated from MegaCrit.Sts2.Core.Models.Cards.*
// of the same name (SilentCardPool.cs, CardRarity.Uncommon). Shared systems (Poison, Accelerant, Shiv,
// Accuracy) are in char_silent.h (X1.0).
#include "cards.h"
#include "char_silent.h"

namespace sts {

namespace {

// BlurPower (Blur) is the one in ancients_later.cpp (registered there); it is applied by id below.

// ---------------------------------------------------------------- cards

// Accelerant.cs: Power, Poison ticks trigger extra times.
struct Accelerant : IroncladT<Accelerant> {
  CARD_HEADER(Accelerant, "ACCELERANT", 1, Power, Uncommon, Self)
    addVar("Accelerant", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<AccelerantPower>(me(), val("Accelerant"), me(), this); }
  void onUpgrade() override { upgradeVar("Accelerant", 1); }
};

// Accuracy.cs: Power, Shivs deal more.
struct Accuracy : IroncladT<Accuracy> {
  CARD_HEADER(Accuracy, "ACCURACY", 1, Power, Uncommon, Self)
    addVar("AccuracyPower", 4);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<AccuracyPower>(me(), val("AccuracyPower"), me(), this); }
  void onUpgrade() override { upgradeVar("AccuracyPower", 2); }
};

// Acrobatics.cs: draw 3, discard a chosen card.
struct Acrobatics : IroncladT<Acrobatics> {
  CARD_HEADER(Acrobatics, "ACROBATICS", 1, Skill, Uncommon, Self)
    addVar("Cards", 3);
  }
  Task<> onPlay(CardPlay&) override {
    co_await drawCards(val("Cards"));
    auto picked = co_await cmd::selectCards(*combat, "card_selection.TO_DISCARD", combat->hand, 1, 1);
    if (!picked.empty()) co_await cmd::discardCard(*combat, picked[0]);
  }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

// Backstab.cs: 0 cost, Exhaust, Innate attack.
struct Backstab : IroncladT<Backstab> {
  CARD_HEADER(Backstab, "BACKSTAB", 0, Attack, Uncommon, AnyEnemy)
    keywords = kwExhaust | kwInnate;
    addVar("Damage", 11);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
  void onUpgrade() override { upgradeVar("Damage", 4); }
};

// Blur.cs: gain block, keep it through the next turn start.
struct Blur : IroncladT<Blur> {
  CARD_HEADER(Blur, "BLUR", 1, Skill, Uncommon, Self)
    addVar("Block", 5);
    addVar("Blur", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    auto pw = db::power("BlurPower");
    if (pw) co_await cmd::applyPower(std::move(pw), me(), val("Blur"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

// BouncingFlask.cs: Poison on a random hittable enemy, Repeat times (each pick draws CombatTargets).
struct BouncingFlask : IroncladT<BouncingFlask> {
  CARD_HEADER(BouncingFlask, "BOUNCING_FLASK", 2, Skill, Uncommon, RandomEnemy)
    addVar("PoisonPower", 3);
    addVar("Repeat", 3);
  }
  Task<> onPlay(CardPlay&) override {
    int n = val("Repeat").toInt();
    for (int i = 0; i < n; ++i) {
      Creature* enemy = combat->rng("CombatTargets").nextItem(combat->hittableEnemies());
      if (!enemy) continue;
      co_await applyPower<PoisonPower>(enemy, val("PoisonPower"), me(), this);
    }
  }
  void onUpgrade() override { upgradeVar("Repeat", 1); }
};

// BubbleBubble.cs: more Poison, only if the target already has Poison.
struct BubbleBubble : IroncladT<BubbleBubble> {
  CARD_HEADER(BubbleBubble, "BUBBLE_BUBBLE", 1, Skill, Uncommon, AnyEnemy)
    addVar("PoisonPower", 9);
  }
  Task<> onPlay(CardPlay& p) override {
    if (p.target && p.target->get<PoisonPower>()) co_await applyPower<PoisonPower>(p.target, val("PoisonPower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("PoisonPower", 3); }
};

// CalculatedGamble.cs: Exhaust, discard the whole hand and draw that many (upgrade: Retain).
struct CalculatedGamble : IroncladT<CalculatedGamble> {
  CARD_HEADER(CalculatedGamble, "CALCULATED_GAMBLE", 0, Skill, Uncommon, Self)
    keywords = kwExhaust;
  }
  Task<> onPlay(CardPlay&) override {
    std::vector<Card*> cards = combat->hand;
    int n = (int)cards.size();
    co_await cmd::discardCards(*combat, cards, n);
  }
  void onUpgrade() override { keywords |= kwRetain; }
};

// Dash.cs: block then attack.
struct Dash : IroncladT<Dash> {
  CARD_HEADER(Dash, "DASH", 2, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 10);
    addVar("Block", 10);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await block(val("Block"));
    co_await attack(p.target, val("Damage"));
  }
  void onUpgrade() override { upgradeVar("Damage", 3); upgradeVar("Block", 3); }
};

// EchoingSlash.cs: hit every enemy; every kill repeats the hit on the survivors.
struct EchoingSlash : IroncladT<EchoingSlash> {
  CARD_HEADER(EchoingSlash, "ECHOING_SLASH", 1, Attack, Uncommon, AllEnemies)
    addVar("Damage", 10);
  }
  Task<> onPlay(CardPlay&) override {
    // One AttackContext around plain damage calls (one Before/AfterAttack for all the repeats).
    cmd::Attack ctx;
    ctx.attacker = me();
    ctx.source = this;
    ctx.allOpponents = true;
    co_await cmd::beginAttackContext(*combat, ctx);
    combat->push({VisualEvent::Anim, me(), 1000 + std::min(999, val("Damage").toInt()), "Attack"});
    int attackCount = 1;
    while (attackCount > 0) {
      --attackCount;
      auto targets = combat->hittableEnemies();
      if (targets.empty()) break;
      auto results = co_await cmd::damage(targets, val("Damage"), kMove, me(), this);
      for (auto& r : results) if (r.killed) ++attackCount;
      ctx.results.push_back(std::move(results));
    }
    co_await cmd::endAttackContext(*combat, ctx);
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

// EscapePlan.cs: draw 1; block if it is a Skill.
struct EscapePlan : IroncladT<EscapePlan> {
  CARD_HEADER(EscapePlan, "ESCAPE_PLAN", 0, Skill, Uncommon, Self)
    addVar("Block", 3);
  }
  Task<> onPlay(CardPlay&) override {
    auto drawn = co_await cmd::drawCards(*combat, Dec(1));
    if (!drawn.empty() && drawn[0]->type == CardType::Skill) co_await block(val("Block"));
  }
  void onUpgrade() override { upgradeVar("Block", 2); }
};

// Expertise.cs: draw cards; each drawn card is Retained this turn.
struct Expertise : IroncladT<Expertise> {
  CARD_HEADER(Expertise, "EXPERTISE", 1, Skill, Uncommon, Self)
    addVar("Cards", 2);
  }
  Task<> onPlay(CardPlay&) override {
    auto drawn = co_await cmd::drawCards(*combat, Dec(val("Cards").toInt()));
    for (Card* c : drawn) c->singleTurnRetain = true;  // CardCmd.ApplySingleTurnRetain
  }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

// Expose.cs: Exhaust; strip the target's Block and Artifact, then apply Vulnerable.
struct Expose : IroncladT<Expose> {
  CARD_HEADER(Expose, "EXPOSE", 0, Skill, Uncommon, AnyEnemy)
    keywords = kwExhaust;
    addVar("Power", 2);
  }
  Task<> onPlay(CardPlay& p) override {
    Creature* t = p.target;
    if (!t) co_return;
    int amount = val("Power").toInt();
    co_await cmd::loseBlock(t, Dec(t->block));
    if (Power* art = t->power("ArtifactPower")) co_await cmd::removePower(art);
    co_await applyPower<VulnerablePower>(t, Dec(amount), me(), this);
  }
  void onUpgrade() override { upgradeVar("Power", 1); }
};

// Finisher.cs: hits once per Attack played this turn (finished plays only, so not itself).
struct Finisher : IroncladT<Finisher> {
  CARD_HEADER(Finisher, "FINISHER", 1, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 6);
    addVar("CalculationBase", 0);
    addVar("CalculationExtra", 1);
    addVar("CalculatedHits", 0);
    calcMultiplier = [](Card* c) { return c->combat ? c->combat->attackPlaysFinishedThisTurn : 0; };
  }
  Task<> onPlay(CardPlay& p) override {
    int hits = calculatedBlock().toInt();  // CalculatedVar: CalculationBase + CalculationExtra * multiplier
    if (hits > 0) co_await attack(p.target, val("Damage"), hits);
  }
  void onUpgrade() override { upgradeVar("Damage", 2); }
};

// Flechettes.cs: hits once per Skill in hand.
struct Flechettes : IroncladT<Flechettes> {
  CARD_HEADER(Flechettes, "FLECHETTES", 1, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 5);
    addVar("CalculationBase", 0);
    addVar("CalculationExtra", 1);
    addVar("CalculatedHits", 0);
    calcMultiplier = [](Card* c) {
      int n = 0;
      if (c->combat) for (Card* k : c->combat->hand) if (k->type == CardType::Skill) ++n;
      return n;
    };
  }
  Task<> onPlay(CardPlay& p) override {
    int hits = calculatedBlock().toInt();
    if (hits > 0) co_await attack(p.target, val("Damage"), hits);
  }
  void onUpgrade() override { upgradeVar("Damage", 2); }
};

// Sidestep.cs: 0 cost, energy next turn.
struct Sidestep : IroncladT<Sidestep> {
  CARD_HEADER(Sidestep, "SIDESTEP", 0, Skill, Uncommon, Self)
    addVar("Energy", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<EnergyNextTurnPower>(me(), Dec(val("Energy").toInt()), me(), this); }
  void onUpgrade() override { upgradeVar("Energy", 1); }
};

// Footwork.cs: Power, Dexterity.
struct Footwork : IroncladT<Footwork> {
  CARD_HEADER(Footwork, "FOOTWORK", 1, Power, Uncommon, Self)
    addVar("DexterityPower", 2);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<DexterityPower>(me(), val("DexterityPower"), me(), this); }
  void onUpgrade() override { upgradeVar("DexterityPower", 1); }
};

// HandTrick.cs: block, then a chosen non-Sly Skill in hand becomes Sly this turn.
struct HandTrick : IroncladT<HandTrick> {
  CARD_HEADER(HandTrick, "HAND_TRICK", 1, Skill, Uncommon, Self)
    addVar("Block", 7);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    std::vector<Card*> options;
    for (Card* c : combat->hand) if (c->type == CardType::Skill && !c->isSlyThisTurn()) options.push_back(c);
    auto picked = co_await cmd::selectCards(*combat, "HAND_TRICK", options, 1, 1);
    if (!picked.empty()) picked[0]->singleTurnSly = true;  // CardCmd.ApplySingleTurnSly
  }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

}  // namespace

void registerSilentUncommonCards1() {
  registerCardType<Accelerant>();
  registerCardType<Accuracy>();
  registerCardType<Acrobatics>();
  registerCardType<Backstab>();
  registerCardType<Blur>();
  registerCardType<BouncingFlask>();
  registerCardType<BubbleBubble>();
  registerCardType<CalculatedGamble>();
  registerCardType<Dash>();
  registerCardType<EchoingSlash>();
  registerCardType<EscapePlan>();
  registerCardType<Expertise>();
  registerCardType<Expose>();
  registerCardType<Finisher>();
  registerCardType<Flechettes>();
  registerCardType<Sidestep>();
  registerCardType<Footwork>();
  registerCardType<HandTrick>();
}

}  // namespace sts
