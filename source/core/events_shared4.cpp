// Shared events 3/3 (package A7c): FakeMerchant, FakeMerchantEventEncounter / FakeMerchantMonster
// and the ten Fake* relics (MegaCrit.Sts2.Core.Models.Events / .Encounters / .Monsters / .Relics).
// The fake shop reuses the shop screen (Screen::Shop, Run::shop, Run::shopChoice) with the six
// relic entries only.
// PORT NOTE: the C# fight starts when a Foul Potion is thrown at the merchant. There is no Foul
// Potion (nor a potion throw on the shop screen) in this build: the shop loop starts the fight
// when the UI fires shopChoice with kFoulPotionThrow (and consumes a "FoulPotion" from the
// belt if one exists). STS_FAKE_FIGHT=1 turns leaving the shop into that throw (debug). The
// merchant's dialogue, the NFakeMerchant scene, the purchase-failure lines and the relic-choice
// history (OnEventFinished) are not ported.
#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "cards.h"
#include "powers.h"

namespace sts {

namespace {

template <class E> void reg() { db::registerEvent(E::kId, [] { return std::unique_ptr<Event>(new E()); }); }
template <class R> void regRelic() { db::registerRelic(R::kId, [] { return std::unique_ptr<Relic>(new R()); }); }
#define EVENT_HEADER(Name, Key) \
  static constexpr const char* kId = #Name; \
  Name() { id = #Name; locKey = Key; }
#define MONSTER_HEADER(Name, Key) \
  Name() { id = #Name; locKey = Key; }

using Targets = const std::vector<Creature*>&;

Intent attackIntent(int dmg, int hits = 1) { Intent i; i.kind = Intent::Attack; i.damage = dmg; i.hits = hits; return i; }
Intent kindIntent(Intent::Kind k) { Intent i; i.kind = k; return i; }

bool inCombat(const Relic* r) { return r->combat && r->combat->inProgress; }

constexpr int kFoulPotionThrow = -2;  // shopChoice value: the Foul Potion is thrown at the merchant

// ================================================================ relics

// FakeAnchor.cs: 4 Block at the start of combat.
struct FakeAnchor : Relic {
  RELIC_HEADER(FakeAnchor, "FAKE_ANCHOR", Event)
    addVar("Block", 4);
  }
  Task<> beforeCombatStart() override {
    doFlash();
    co_await cmd::gainBlock(owner(), val("Block"), kUnpowered, nullptr);
  }
};

// FakeBloodVial.cs: heal 1 at the start of turn 1.
struct FakeBloodVial : Relic {
  RELIC_HEADER(FakeBloodVial, "FAKE_BLOOD_VIAL", Event)
    addVar("Heal", 1);
  }
  Task<> afterPlayerTurnStart() override {  // AfterPlayerTurnStartLate
    if (!combat || combat->turnNumber > 1) co_return;
    co_await cmd::heal(owner(), val("Heal"));
  }
};

// FakeHappyFlower.cs: 1 energy every 5th turn (the counter is kept between combats).
struct FakeHappyFlower : Relic {
  RELIC_HEADER(FakeHappyFlower, "FAKE_HAPPY_FLOWER", Event)
    addVar("Energy", 1);
    addVar("Turns", 5);
  }
  int turnsSeen = 0;
  void persist(Archive& a) override { a.io(turnsSeen); }  // [SavedProperty] TurnsSeen
  bool showCounter() const override { return true; }
  int displayAmount() const override { return turnsSeen; }
  Task<> afterSideTurnStart(Side side, const std::vector<Creature*>& participants) override {
    if (side != Side::Player || !combat || !contains(participants, owner())) co_return;
    turnsSeen = (turnsSeen + 1) % val("Turns").toInt();
    if (turnsSeen == 0) {
      doFlash();
      co_await cmd::gainEnergy(*combat, val("Energy").toInt());
    }
  }
};

// FakeLeesWaffle.cs: heal 10% of max HP on pickup.
struct FakeLeesWaffle : Relic {
  RELIC_HEADER(FakeLeesWaffle, "FAKE_LEES_WAFFLE", Event)
    addVar("Heal", 10);
  }
  Task<> afterObtained() override {
    co_await cmd::heal(owner(), Dec(owner()->maxHp) * (val("Heal") / Dec(100)));
  }
};

// FakeMango.cs: +3 max HP on pickup.
struct FakeMango : Relic {
  RELIC_HEADER(FakeMango, "FAKE_MANGO", Event)
    addVar("MaxHp", 3);
  }
  Task<> afterObtained() override {
    if (inCombat(this)) co_await cmd::gainMaxHp(owner(), val("MaxHp").toInt());
    else co_await run->gainMaxHp(val("MaxHp").toInt());
  }
};

// FakeMerchantsRug.cs: a plain relic, the fight reward.
struct FakeMerchantsRug : Relic {
  RELIC_HEADER(FakeMerchantsRug, "FAKE_MERCHANTS_RUG", Event) }
};

// FakeOrichalcum.cs: 3 Block at the end of your turn if you have none.
struct FakeOrichalcum : Relic {
  RELIC_HEADER(FakeOrichalcum, "FAKE_ORICHALCUM", Event)
    addVar("Block", 3);
  }
  bool shouldTrigger = false;
  // PORT NOTE: C# uses BeforeSideTurnEndVeryEarly (before Plating); beforeSideTurnEndEarly is the
  // stand-in used by Orichalcum too.
  Task<> beforeSideTurnEndEarly(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner()) || owner()->block > 0) return {};
    shouldTrigger = true;
    return {};
  }
  Task<> beforeSideTurnEnd(Side, const std::vector<Creature*>&) override {
    if (!shouldTrigger) co_return;
    shouldTrigger = false;
    doFlash();
    co_await cmd::gainBlock(owner(), val("Block"), kUnpowered, nullptr);
  }
  Task<> beforeSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner())) shouldTrigger = false;
    return {};
  }
};

// FakeSneckoEye.cs: Confused at the start of combat (and on pickup during one). The
// ConfusedPower class lives with the real Snecko Eye (ancients_later.cpp); it is applied by id.
struct FakeSneckoEye : Relic {
  RELIC_HEADER(FakeSneckoEye, "FAKE_SNECKO_EYE", Event) }
  Task<> applyConfused() {
    auto p = db::power("ConfusedPower");
    if (p) co_await cmd::applyPower(std::move(p), owner(), 1, owner(), nullptr);
  }
  Task<> beforeCombatStart() override { co_await applyConfused(); }
  Task<> afterObtained() override {
    if (inCombat(this)) co_await applyConfused();
  }
};

// FakeStrikeDummy.cs: +1 damage on Strikes.
struct FakeStrikeDummy : Relic {
  RELIC_HEADER(FakeStrikeDummy, "FAKE_STRIKE_DUMMY", Event)
    addVar("ExtraDamage", 1);
  }
  Dec modifyDamageAdditive(Creature*, Dec, int props, Creature* dealer, Card* src) override {
    if (!isPoweredAttack(props)) return 0;
    if (!src || !(src->tags & tagStrike)) return 0;
    if (dealer != owner()) return 0;
    return val("ExtraDamage");
  }
};

// FakeVenerableTeaSet.cs: 1 extra energy in the first combat after a rest site.
struct FakeVenerableTeaSet : Relic {
  RELIC_HEADER(FakeVenerableTeaSet, "FAKE_VENERABLE_TEA_SET", Event)
    addVar("Energy", 1);
  }
  bool gainEnergyInNextCombat = false;
  void persist(Archive& a) override { a.io(gainEnergyInNextCombat); }  // [SavedProperty]
  Task<> afterRoomEntered(RoomType room) override {
    if (room == RoomType::Rest) gainEnergyInNextCombat = true;
    return {};
  }
  Task<> afterEnergyReset() override {
    if (!gainEnergyInNextCombat || !combat) co_return;
    doFlash();
    co_await cmd::gainEnergy(*combat, val("Energy").toInt());
    gainEnergyInNextCombat = false;
  }
};

// ================================================================ monster

// FakeMerchantMonster.cs (the dialogue lines and animations are dropped).
struct FakeMerchantMonster : Monster {
  MONSTER_HEADER(FakeMerchantMonster, "FAKE_MERCHANT_MONSTER")
  int minHp() const override { return asc(kToughEnemies, 175, 165); }
  int maxHp() const override { return minHp(); }
  int swipeDamage() const { return asc(kDeadlyEnemies, 15, 13); }
  int throwRelicDamage() const { return asc(kDeadlyEnemies, 10, 9); }
  void buildMoves() override {
    auto* swipe = machine.add<MoveState>("SWIPE_MOVE");
    swipe->perform = [this](Targets) { return attack(swipeDamage()); };
    swipe->intents = {attackIntent(swipeDamage())};
    auto* spew = machine.add<MoveState>("SPEW_COINS_MOVE");
    spew->perform = [this](Targets) { return attack(2, 8); };
    spew->intents = {attackIntent(2, 8)};
    auto* throwRelic = machine.add<MoveState>("THROW_RELIC_MOVE");
    throwRelic->perform = [this](Targets t) { return throwRelicMove(t); };
    throwRelic->intents = {attackIntent(throwRelicDamage()), kindIntent(Intent::Debuff)};
    auto* enrage = machine.add<MoveState>("ENRAGE_MOVE");
    enrage->perform = [this](Targets) { return applyToSelf<StrengthPower>(2); };
    enrage->intents = {kindIntent(Intent::Buff)};
    auto* rand = machine.add<RandomBranchState>("RAND_MOVE");
    rand->add(swipe, MoveRepeat::CannotRepeat);
    rand->add(spew, MoveRepeat::CannotRepeat);
    rand->add(throwRelic, MoveRepeat::CannotRepeat);
    rand->add(enrage, MoveRepeat::CannotRepeat, 3.f);
    swipe->followUp = rand;
    spew->followUp = rand;
    enrage->followUp = rand;
    auto* randAttack = machine.add<RandomBranchState>("RAND_ATTACK_MOVE");
    randAttack->add(swipe, MoveRepeat::CannotRepeat);
    randAttack->add(spew, MoveRepeat::CannotRepeat);
    randAttack->add(throwRelic, MoveRepeat::CannotRepeat);
    throwRelic->followUp = randAttack;
    machine.start(swipe);
  }
  Task<> throwRelicMove(Targets targets) {
    co_await attack(throwRelicDamage());
    co_await applyToTargets<FrailPower>(targets, 1);
  }
};

// ================================================================ event

// FakeMerchant.cs: a shop of six fake relics at 50 gold; throwing a Foul Potion at the merchant
// starts a fight (300 gold, the Rug and the unsold relics as rewards).
struct FakeMerchant : Event {
  EVENT_HEADER(FakeMerchant, "FAKE_MERCHANT")
  static constexpr int relicCost = 50;
  bool startedFight = false;

  bool isAllowed(Run& r) override {
    if (r.actIndex < 1) return false;
    if (r.gold >= 100) return true;
    for (auto& p : r.potions) if (p && p->id == "FoulPotion") return true;
    return false;
  }
  std::vector<EventOption> initialOptions() override { return {}; }

  Task<> onStart() override {
    // BeforeEventStarted: shuffle the nine relics with the event's rng, take six, each priced
    // with the shop rng (MerchantRelicEntry.CalcCost).
    std::vector<std::string> pool = {"FakeAnchor", "FakeBloodVial", "FakeHappyFlower", "FakeLeesWaffle", "FakeMango",
                                     "FakeOrichalcum", "FakeSneckoEye", "FakeStrikeDummy", "FakeVenerableTeaSet"};
    rng().shuffle(pool);
    pool.resize(6);
    run->shop.clear();
    run->shopMessage.clear();
    for (auto& rid : pool) {
      ShopItem it;
      it.kind = ShopItem::RelicItem;
      it.relic = db::relic(rid);
      it.relic->run = run;
      float f = run->rng("Shops").nextFloat(0.85f, 1.15f);
      it.cost = (int)std::nearbyint((float)relicCost * f);
      run->shop.push_back(std::move(it));
    }
    for (;;) {
      run->screen = Screen::Shop;
      int i = co_await run->shopChoice.next();
      if (i == -1 && getenv("STS_FAKE_FIGHT")) i = kFoulPotionThrow;  // debug
      if (i == kFoulPotionThrow) {
        co_await foulPotionThrown();
        break;
      }
      if (i < 0 || i >= (int)run->shop.size()) break;
      ShopItem& it = run->shop[i];
      if (!it.stocked()) continue;
      int price = run->shopPrice(it);
      if (price > run->gold) continue;  // PurchaseStatus.FailureGold
      run->gold -= price;
      co_await run->obtainRelic(std::move(it.relic));
      for (Model* m : run->listeners()) co_await m->afterItemPurchased(price);
      if (run->died) break;
    }
    run->shop.clear();
    finished = true;
  }

  // FoulPotionThrown: the Rug, then every unsold relic, come with the fight.
  Task<> foulPotionThrown() {
    for (size_t s = 0; s < run->potions.size(); ++s)
      if (run->potions[s] && run->potions[s]->id == "FoulPotion") { run->discardPotion((int)s); break; }
    startedFight = true;
    run->extraRewardRelics.push_back(db::relic("FakeMerchantsRug"));
    for (auto& it : run->shop)
      if (it.relic) run->extraRewardRelics.push_back(std::move(it.relic));
    for (auto& r : run->extraRewardRelics) r->run = run;
    run->shop.clear();
    run->rewardGold = 300;  // FakeMerchantEventEncounter.Min/MaxGoldReward
    co_await run->eventFight("FakeMerchantEventEncounter");
  }
};

}  // namespace

void registerSharedEvents4() {
  regRelic<FakeAnchor>();
  regRelic<FakeBloodVial>();
  regRelic<FakeHappyFlower>();
  regRelic<FakeLeesWaffle>();
  regRelic<FakeMango>();
  regRelic<FakeMerchantsRug>();
  regRelic<FakeOrichalcum>();
  regRelic<FakeSneckoEye>();
  regRelic<FakeStrikeDummy>();
  regRelic<FakeVenerableTeaSet>();
  db::registerEncounter("FakeMerchantEventEncounter", RoomType::Monster, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(std::make_unique<FakeMerchantMonster>());
    return v;
  });
  reg<FakeMerchant>();
}

}  // namespace sts
