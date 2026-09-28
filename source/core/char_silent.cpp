// The Silent's shared systems (X1.0), see char_silent.h. The Shiv token card lives here because
// almost every Silent card generates it.
#include "cards.h"
#include "char_silent.h"

namespace sts {

namespace {

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

void registerSilent() {
  registerPowerType<AccelerantPower>();
  registerPowerType<PoisonPower>();
  registerPowerType<AccuracyPower>();
  registerPowerType<FanOfKnivesPower>();
  registerCardType<Shiv>();
}

}  // namespace sts
