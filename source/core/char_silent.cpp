// The Silent's shared systems (X1.0, see char_silent.h) and her starter deck / starting relic /
// potions (X1.1): the Shiv token lives here because almost every Silent card generates it.
#include "cards.h"
#include "char_silent.h"

namespace sts {

namespace {

// ---------------------------------------------------------------- starter deck (X1.1)

// StrikeSilent.cs / DefendSilent.cs: identical to the Ironclad's Strike/Defend (only portrait,
// attack vfx and color differ in the C#).
struct StrikeSilent : IroncladT<StrikeSilent> {
  CARD_HEADER(StrikeSilent, "STRIKE_SILENT", 1, Attack, Basic, AnyEnemy)
    tags = tagStrike;
    addVar("Damage", 6);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

struct DefendSilent : IroncladT<DefendSilent> {
  CARD_HEADER(DefendSilent, "DEFEND_SILENT", 1, Skill, Basic, Self)
    tags = tagDefend;
    addVar("Block", 5);
  }
  Task<> onPlay(CardPlay&) override { co_await block(val("Block")); }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

// Neutralize.cs: 0 cost, 3 damage + 1 Weak.
struct Neutralize : IroncladT<Neutralize> {
  CARD_HEADER(Neutralize, "NEUTRALIZE", 0, Attack, Basic, AnyEnemy)
    addVar("Damage", 3);
    addVar("WeakPower", 1);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<WeakPower>(p.target, val("WeakPower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 1); upgradeVar("WeakPower", 1); }
};

// Survivor.cs: gain block, then discard a card from hand.
struct Survivor : IroncladT<Survivor> {
  CARD_HEADER(Survivor, "SURVIVOR", 1, Skill, Basic, Self)
    addVar("Block", 8);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    auto picked = co_await cmd::selectCards(*combat, "card_selection.TO_DISCARD", combat->hand, 1, 1);
    if (!picked.empty()) co_await cmd::discardCard(*combat, picked[0]);
  }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

// ---------------------------------------------------------------- potions (X1.1, Silent4Epoch)

// PoisonPotion.cs: apply Poison to a single enemy.
struct PoisonPotion : Potion {
  POTION_HEADER(PoisonPotion, "POISON_POTION", Common, CombatOnly, AnyEnemy) addVar("PoisonPower", 6); }
  Task<> onUse(Creature* t) override { co_await applyPower<PoisonPower>(t, val("PoisonPower"), run->player.get(), nullptr); }
};

// GhostInAJar.cs: apply Intangible 1 to the player. PORT NOTE: TargetType::Self for the C#'s
// AnyPlayer/Self (single player only), as in potions.cpp.
struct GhostInAJar : Potion {
  POTION_HEADER(GhostInAJar, "GHOST_IN_A_JAR", Rare, CombatOnly, Self) addVar("IntangiblePower", 1); }
  Task<> onUse(Creature* t) override {
    auto p = db::power("IntangiblePower");
    if (p) co_await cmd::applyPower(std::move(p), t, val("IntangiblePower"), run->player.get(), nullptr);
  }
};

// CunningPotion.cs: 3 Shivs in hand, upgraded.
struct CunningPotion : Potion {
  POTION_HEADER(CunningPotion, "CUNNING_POTION", Uncommon, CombatOnly, Self) addVar("Cards", 3); }
  Task<> onUse(Creature*) override {
    if (!combat) co_return;
    auto made = co_await createShivsInHand(*combat, val("Cards").toInt());
    for (Card* k : made) k->upgrade();
  }
};

// ---------------------------------------------------------------- Shiv token (X1.0)

// Shiv.cs: 0 cost, Exhaust, 4 damage (+2 upgraded); targets every enemy under FanOfKnivesPower.
struct Shiv : IroncladT<Shiv> {
  CARD_HEADER(Shiv, "SHIV", 0, Attack, Token, AnyEnemy)
    keywords = kwExhaust;
    tags = tagShiv;
    addVar("Damage", 4);
  }
  Task<> onPlay(CardPlay& p) override {
    if (combat->player->get<FanOfKnivesPower>()) co_await attackAll(val("Damage"));
    else co_await attack(p.target, val("Damage"));
  }
  void onUpgrade() override { upgradeVar("Damage", 2); }
};

}  // namespace

Task<std::vector<Card*>> createShivsInHand(Combat& c, int count) {
  std::vector<Card*> made;
  if (count <= 0 || c.over || c.ending) co_return made;
  for (int i = 0; i < count; ++i) {
    Card* k = co_await cmd::addGeneratedCard(c, db::card("Shiv"), Pile::Hand);
    made.push_back(k);
  }
  co_return made;
}

void registerSilentRelics();  // char_silent_relics.cpp
void registerSilentCards();   // char_silent_cards.cpp (X1.2)
void registerSilentUncommonCards1();  // char_silent_cards_uncommon1.cpp (X1.3a)

void registerSilent() {
  registerPowerType<AccelerantPower>();
  registerPowerType<PoisonPower>();
  registerPowerType<AccuracyPower>();
  registerPowerType<FanOfKnivesPower>();
  registerCardType<Shiv>();
  registerCardType<StrikeSilent>();
  registerCardType<DefendSilent>();
  registerCardType<Neutralize>();
  registerCardType<Survivor>();
  db::registerPotion("PoisonPotion", [] { return std::unique_ptr<Potion>(new PoisonPotion()); });
  db::registerPotion("GhostInAJar", [] { return std::unique_ptr<Potion>(new GhostInAJar()); });
  db::registerPotion("CunningPotion", [] { return std::unique_ptr<Potion>(new CunningPotion()); });
  registerSilentRelics();
  registerSilentCards();
  registerSilentUncommonCards1();
}

}  // namespace sts
