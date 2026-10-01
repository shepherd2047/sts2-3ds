// Act 3 (Glory) events, package 7, translated from MegaCrit.Sts2.Core.Models.Events:
// BattlewornDummy, GraveOfTheForgotten, HungryForMushrooms, Reflections, RoundTeaParty,
// Trial, TinkerTime (+ the cards, relics, powers and monsters they hand out).
#include <algorithm>
#include <ctime>
#include <map>

#include "cards.h"
#include "powers.h"

namespace sts {

namespace {
template <class E> void reg() { db::registerEvent(E::kId, [] { return std::unique_ptr<Event>(new E()); }); }
#define EVENT_HEADER(Name, Key) \
  static constexpr const char* kId = #Name; \
  Name() { id = #Name; locKey = Key; }
#define MONSTER_HEADER(Name, Key) \
  Name() { id = #Name; locKey = Key; }

using Targets = const std::vector<Creature*>&;
template <class M> std::unique_ptr<Monster> mk() { return std::make_unique<M>(); }

// Random cards of the deck that can still be upgraded (StableShuffle + Take).
std::vector<Card*> upgradableSample(Run& r, Rng& rng, int n) {
  std::vector<Card*> up;
  for (auto& c : r.deck) if (c->upgradable()) up.push_back(c.get());
  rng.shuffle(up);
  if ((int)up.size() > n) up.resize((size_t)n);
  return up;
}

// CreatureCmd.Escape: the creature leaves the room alive (no death hooks).
// PORT NOTE (n/a: visual): the UI has no escape animation; the death animation is played instead.
Task<> escapeCreature(Creature* c) {
  c->combat->push({VisualEvent::Death, c, 0});
  c->hp = 0;
  c->removed = true;
  co_return;
}

// ================================================================ curses

// Regret.cs: at turn end, lose HP equal to the hand size (counted before the flush).
struct Regret : IroncladT<Regret> {
  CARD_HEADER(Regret, "REGRET", -1, Curse, Curse, None)
    keywords = kwUnplayable;
    maxUpgradeLevel = 0;
  }
  int cardsInHand = 0;
  bool hasTurnEndInHandEffect() const override { return true; }
  Task<> beforeSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (!combat || !contains(participants, combat->player) || combat->pileOf(this) != Pile::Hand) co_return;
    cardsInHand = (int)combat->hand.size();
  }
  Task<> onTurnEndInHand() override {
    co_await cmd::damage(me(), Dec(cardsInHand), kUnblockable | kUnpowered | kMove, nullptr, this);
    cardsInHand = 0;
  }
};

// Shame.cs
struct Shame : IroncladT<Shame> {
  CARD_HEADER(Shame, "SHAME", -1, Curse, Curse, None)
    keywords = kwUnplayable;
    maxUpgradeLevel = 0;
    addVar("Frail", 1);
  }
  bool hasTurnEndInHandEffect() const override { return true; }
  Task<> onTurnEndInHand() override { co_await applyPower<FrailPower>(me(), val("Frail"), nullptr, this); }
};

// Doubt.cs
struct Doubt : IroncladT<Doubt> {
  CARD_HEADER(Doubt, "DOUBT", -1, Curse, Curse, None)
    keywords = kwUnplayable;
    maxUpgradeLevel = 0;
    addVar("WeakPower", 1);
  }
  bool hasTurnEndInHandEffect() const override { return true; }
  Task<> onTurnEndInHand() override { co_await applyPower<WeakPower>(me(), val("WeakPower"), nullptr, this); }
};

// BadLuck.cs (Eternal: it cannot be removed from the deck).
struct BadLuck : IroncladT<BadLuck> {
  CARD_HEADER(BadLuck, "BAD_LUCK", -1, Curse, Curse, None)
    keywords = kwUnplayable | kwEternal;
    maxUpgradeLevel = 0;
    addVar("HpLoss", 13);
  }
  bool hasTurnEndInHandEffect() const override { return true; }
  Task<> onTurnEndInHand() override {
    co_await cmd::damage(me(), val("HpLoss"), kUnblockable | kUnpowered | kMove, nullptr, this);
  }
};

// Decay.cs
struct Decay : IroncladT<Decay> {
  CARD_HEADER(Decay, "DECAY", -1, Curse, Curse, None)
    keywords = kwUnplayable;
    maxUpgradeLevel = 0;
    addVar("Damage", 2);
  }
  bool hasTurnEndInHandEffect() const override { return true; }
  Task<> onTurnEndInHand() override { co_await cmd::damage(me(), val("Damage"), kUnpowered | kMove, nullptr, this); }
};

// ================================================================ relics

// BigMushroom.cs: +20 max HP, but 2 fewer cards on the first turn of each combat.
// PORT NOTE (n/a: visual): the creature is not scaled up (NCombatRoom Grow).
struct BigMushroom : Relic {
  RELIC_HEADER(BigMushroom, "BIG_MUSHROOM", Event)
    addVar("MaxHp", 20);
    addVar("Cards", 2);
  }
  Task<> afterObtained() override { co_await cmd::gainMaxHp(owner(), val("MaxHp").toInt()); }
  Dec modifyHandDraw(Dec amount) override {
    if (!combat || combat->turnNumber != 1) return amount;
    return amount - val("Cards");
  }
};

// FragrantMushroom.cs: lose 15 HP, upgrade 2 random cards.
struct FragrantMushroom : Relic {
  RELIC_HEADER(FragrantMushroom, "FRAGRANT_MUSHROOM", Event)
    addVar("HpLoss", 15);
    addVar("Cards", 2);
  }
  Task<> afterObtained() override {
    co_await run->loseHp(val("HpLoss").toInt());
    if (run->died) co_return;
    for (Card* c : upgradableSample(*run, run->rng("Niche"), val("Cards").toInt())) c->upgrade();
  }
};

// RoyalPoison.cs: lose 4 HP at the start of the first turn.
struct RoyalPoison : Relic {
  RELIC_HEADER(RoyalPoison, "ROYAL_POISON", Event)
    addVar("Damage", 4);
  }
  Task<> afterPlayerTurnStart() override {
    if (!combat || combat->turnNumber > 1) co_return;
    doFlash();
    co_await cmd::damage(owner(), val("Damage"), kUnblockable | kUnpowered, nullptr, nullptr);
  }
};

// ForgottenSoul.cs: whenever a card is exhausted, 1 damage to a random enemy.
struct ForgottenSoul : Relic {
  RELIC_HEADER(ForgottenSoul, "FORGOTTEN_SOUL", Event)
    addVar("Damage", 1);
  }
  Task<> afterCardExhausted(Card*, bool) override {
    if (!combat) co_return;
    auto enemies = combat->hittableEnemies();
    if (enemies.empty()) co_return;
    Creature* t = combat->run->rng("CombatTargets").nextItem(enemies);
    doFlash();
    co_await cmd::damage(t, val("Damage"), kUnpowered, owner(), nullptr);
  }
};

// ================================================================ powers (TinkerTime riders)

// StranglePower.cs: whenever the applier plays a card, the owner loses HP. InstancedPerApplier.
struct StranglePower : Power {
  POWER_HEADER(StranglePower, "STRANGLE_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  PowerInstanceType instanceType() const override { return PowerInstanceType::InstancedPerApplier; }
  std::map<Card*, int> amountsForPlayedCards;
  Task<> beforeCardPlayed(const CardPlay& p) override {
    if (!applier || !applier->isPlayer || ownerOf(p.card) != applier) co_return;  // Applier.Player plays it
    amountsForPlayedCards[p.card] = amount;
  }
  Task<> afterCardPlayed(const CardPlay& p) override {
    auto it = amountsForPlayedCards.find(p.card);
    if (it == amountsForPlayedCards.end()) co_return;
    int v = it->second;
    amountsForPlayedCards.erase(it);
    flash = 1.f;
    co_await cmd::damage(owner, Dec(v), kUnblockable | kUnpowered, nullptr, nullptr);
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await cmd::removePower(this);
  }
};

// CuriousPower.cs: Power cards cost less.
struct CuriousPower : Power {
  POWER_HEADER(CuriousPower, "CURIOUS_POWER")
  int modifyEnergyCost(Card* c, int cost) override {
    if (c->type != CardType::Power || cost <= 0) return cost;
    return std::max(0, cost - amount);
  }
};

// ImprovementPower.cs: at the end of combat, upgrade random cards of the deck.
struct ImprovementPower : Power {
  POWER_HEADER(ImprovementPower, "IMPROVEMENT_POWER")
  Task<> afterCombatEnd() override {
    if (!owner || !owner->combat) co_return;
    Run* run = owner->combat->run;
    std::vector<Card*> up;
    for (auto& c : run->deck) if (c->upgradable()) up.push_back(c.get());
    Rng& rng = run->rng("CombatCardSelection");
    for (int i = 0; i < amount && !up.empty(); ++i) {
      Card* c = rng.nextItem(up);
      up.erase(std::find(up.begin(), up.end(), c));
      c->upgrade();
    }
  }
};

// BattlewornDummyTimeLimitPower.cs: after 3 enemy turns the dummy leaves and the fight is lost.
bool g_dummyRanOutOfTime = false;  // BattlewornDummyEventEncounter.RanOutOfTime
struct BattlewornDummyTimeLimitPower : Power {
  POWER_HEADER(BattlewornDummyTimeLimitPower, "BATTLEWORN_DUMMY_TIME_LIMIT_POWER")
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner)) co_return;
    if (amount > 1) { co_await cmd::decrement(this); co_return; }
    g_dummyRanOutOfTime = true;
    co_await escapeCreature(owner);
  }
};

// ================================================================ MadScience (TinkerTime card)

enum Rider { kNone, kSapping, kViolence, kChoking, kEnergized, kWisdom, kChaos, kExpertise, kCurious, kImprovement };
const char* const kRiderNames[] = {"None", "Sapping", "Violence", "Choking", "Energized", "Wisdom",
                                   "Chaos", "Expertise", "Curious", "Improvement"};

// MadScience.cs: an attack, skill or power chosen in the event, with a rider effect.
// The description's SmartFormat flags are exposed as 0/1 vars: "CardType" (0 Attack, 1 Skill,
// 2 Power), "HasRider" and one per rider name.
struct MadScience : IroncladT<MadScience> {
  CARD_HEADER(MadScience, "MAD_SCIENCE", 1, Attack, Event, AnyEnemy)
    addVar("Damage", 12);
    addVar("Block", 8);
    addVar("SappingWeak", 2);
    addVar("SappingVulnerable", 2);
    addVar("ViolenceHits", 3);
    addVar("ChokingDamage", 6);
    addVar("EnergizedEnergy", 2);
    addVar("WisdomCards", 3);
    addVar("ExpertiseStrength", 2);
    addVar("ExpertiseDexterity", 2);
    addVar("CuriousReduction", 1);
    addVar("energyPrefix", 1);
    configure(CardType::Attack, kNone);
  }
  int rider = kNone;
  bool gainsBlock() const override { return type == CardType::Skill; }  // GainsBlock => TinkerTimeType == Skill
  void configure(CardType t, int r) {
    type = t;
    target = t == CardType::Attack ? TargetType::AnyEnemy : TargetType::Self;
    rider = r;
    setVar("CardType", t == CardType::Attack ? 0 : t == CardType::Skill ? 1 : 2);
    setVar("HasRider", r != kNone ? 1 : 0);
    for (int i = 1; i <= kImprovement; ++i) setVar(kRiderNames[i], i == r ? 1 : 0);
  }
  void setVar(const char* n, int v) {
    if (auto* d = var(n)) d->base = Dec(v); else addVar(n, Dec(v));
  }
  void onUpgrade() override { keywords |= kwInnate; }
  // The chosen type and rider live in the vars (saved); the fields that drive onPlay are rebuilt.
  void afterLoad() override {
    int t = val("CardType").toInt();
    int r = kNone;
    for (int i = 1; i <= kImprovement; ++i) if (val(kRiderNames[i]).toInt()) r = i;
    configure(t == 0 ? CardType::Attack : t == 1 ? CardType::Skill : CardType::Power, r);
  }

  Task<> onPlay(CardPlay& p) override {
    switch (type) {
      case CardType::Attack:
        co_await attack(p.target, val("Damage"), rider == kViolence ? val("ViolenceHits").toInt() : 1);
        break;
      case CardType::Skill:
        co_await block(val("Block"));
        break;
      default:
        if (rider == kExpertise) {
          co_await applyPower<StrengthPower>(me(), val("ExpertiseStrength"), me(), this);
          co_await applyPower<DexterityPower>(me(), val("ExpertiseDexterity"), me(), this);
        } else if (rider == kCurious) {
          co_await applyPower<CuriousPower>(me(), val("CuriousReduction"), me(), this);
        } else if (rider == kImprovement) {
          co_await applyPower<ImprovementPower>(me(), 1, me(), this);
        }
        break;
    }
    switch (rider) {
      case kSapping:
        co_await applyPower<WeakPower>(p.target, val("SappingWeak"), me(), this);
        co_await applyPower<VulnerablePower>(p.target, val("SappingVulnerable"), me(), this);
        break;
      case kChoking:
        co_await applyPower<StranglePower>(p.target, val("ChokingDamage"), me(), this);
        break;
      case kEnergized:
        co_await cmd::gainEnergy(*combat, val("EnergizedEnergy").toInt());
        break;
      case kWisdom:
        co_await drawCards(val("WisdomCards"));
        break;
      case kChaos: {
        // GetUnlockedCards + FilterForCombat: the raw cardPool also lists unported / multiplayer-only
        // ids, for which db::card returns null.
        auto pool = db::characterCards(combat->run->characterId, [](const Card& c) {
          return c.canBeGeneratedInCombat() && c.rarity != Rarity::Basic && c.rarity != Rarity::Ancient;
        });
        if (pool.empty()) break;
        auto card = db::card(combat->run->rng("CombatCardGeneration").nextItem(pool));
        card->setThisTurn(0);
        co_await cmd::addGeneratedCard(*combat, std::move(card), Pile::Hand);
        break;
      }
      default: break;
    }
  }
};

// ================================================================ events

// BattlewornDummy.cs: three difficulty settings; a dummy with a 3-turn time limit.
struct BattlewornDummy : Event {
  EVENT_HEADER(BattlewornDummy, "BATTLEWORN_DUMMY")
  void calculateVars() override {
    addVar("Setting1Hp", 75);
    addVar("Setting2Hp", 150);
    addVar("Setting3Hp", 300);
  }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "SETTING_1", [this] { return setting(1); }),
            option("INITIAL", "SETTING_2", [this] { return setting(2); }),
            option("INITIAL", "SETTING_3", [this] { return setting(3); })};
  }
  // EnterCombatWithoutExitingEvent (no rewards from the room) + Resume.
  Task<> setting(int level) {
    g_dummyRanOutOfTime = false;
    bool won = co_await run->fight("BattlewornDummyEventV" + std::to_string(level) + "Encounter");
    if (!won) { run->died = true; co_return; }
    run->combat.reset();
    run->player->combat = nullptr;
    for (auto& rel : run->relics) rel->combat = nullptr;
    if (g_dummyRanOutOfTime) { setFinished("DEFEAT"); co_return; }
    setFinished("VICTORY");
    if (level == 1) {
      // Character potion pool + SharedPotionPool, one NextItem from the Rewards stream.
      std::vector<std::string> items;
      for (auto& id : db::potionPool(run->characterId)) if (db::potion(id)) items.push_back(id);
      std::string pick = run->rng("Rewards").nextItem(items);
      if (!pick.empty()) {
        auto p = db::potion(pick);
        p->run = run;
        co_await run->offerPotion(std::move(p));
      }
    } else if (level == 2) {
      for (Card* c : upgradableSample(*run, rng(), 2)) c->upgrade();
    } else {
      co_await run->offerRelic(run->pullRelicFromFront(run->relicBag, run->rollRelicRarity(run->rng("Rewards"))), false);
    }
  }
};

// BattleFriendV1..V3.cs: a dummy that does nothing and leaves after 3 enemy turns.
template <int HP>
struct BattleFriend : Monster {
  int minHp() const override { return HP; }
  int maxHp() const override { return HP; }
  void buildMoves() override {
    auto* nothing = machine.add<MoveState>("NOTHING_MOVE");
    nothing->perform = [](Targets) -> Task<> { co_return; };
    nothing->followUp = nothing;
    machine.start(nothing);
  }
  Task<> afterAddedToRoom() override { co_await applyToSelf<BattlewornDummyTimeLimitPower>(3); }
};
struct BattleFriendV1 : BattleFriend<75> { MONSTER_HEADER(BattleFriendV1, "BATTLE_FRIEND_V1") };
struct BattleFriendV2 : BattleFriend<150> { MONSTER_HEADER(BattleFriendV2, "BATTLE_FRIEND_V2") };
struct BattleFriendV3 : BattleFriend<300> { MONSTER_HEADER(BattleFriendV3, "BATTLE_FRIEND_V3") };

// GraveOfTheForgotten.cs: take the Forgotten Soul relic, or curse yourself to make a card
// lose Exhaust (the SoulsPower enchantment).
struct GraveOfTheForgotten : Event {
  EVENT_HEADER(GraveOfTheForgotten, "GRAVE_OF_THE_FORGOTTEN")
  void calculateVars() override {
    setStr("Relic", "relics.FORGOTTEN_SOUL.title");
    setStr("Enchantment", "enchantments.SOULS_POWER.title");
    setStr("Curse", "cards.DECAY.title");
  }
  static bool hasEnchantable(Run& r) { return r.canEnchantAny("SoulsPower"); }
  bool isAllowed(Run& r) override { return hasEnchantable(r); }
  std::vector<EventOption> initialOptions() override {
    EventOption confront = hasEnchantable(*run)
        ? option("INITIAL", "CONFRONT", [this] { return confrontSoul(); })
        : EventOption{page("INITIAL") + ".options.CONFRONT_LOCKED", nullptr};
    return {confront, option("INITIAL", "ACCEPT", [this] { return accept(); })};
  }
  Task<> confrontSoul() {
    run->addCardToDeck(db::card("Decay"));
    auto picked = co_await run->selectForEnchantment("SoulsPower", 1);
    if (!picked.empty()) run->enchantCard(picked[0], "SoulsPower", 1);
    setFinished("CONFRONT");
  }
  Task<> accept() {
    co_await run->obtainRelic(db::relic("ForgottenSoul"));
    setFinished("ACCEPT");
  }
};

// HungryForMushrooms.cs
struct HungryForMushrooms : Event {
  EVENT_HEADER(HungryForMushrooms, "HUNGRY_FOR_MUSHROOMS")
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "BIG_MUSHROOM", [this] { return take("BigMushroom", "BIG_MUSHROOM"); }),
            option("INITIAL", "FRAGRANT_MUSHROOM", [this] { return take("FragrantMushroom", "FRAGRANT_MUSHROOM"); })};
  }
  Task<> take(const char* relicId, const char* pageName) {
    co_await run->obtainRelic(db::relic(relicId));
    if (!run->died) setFinished(pageName);
  }
};

// Reflections.cs: downgrade 2 upgraded cards and upgrade 4, or duplicate the deck and add Bad Luck.
struct Reflections : Event {
  EVENT_HEADER(Reflections, "REFLECTIONS")
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "TOUCH_A_MIRROR", [this] { return touchAMirror(); }),
            option("INITIAL", "SHATTER", [this] { return shatter(); })};
  }
  // CardCmd.Downgrade: back to the base card (all upgrade levels), in place.
  void downgrade(Card* c) { cmd::downgradeCard(c); }
  Task<> touchAMirror() {
    std::vector<Card*> upgraded;
    for (auto& c : run->deck) if (c->upgraded()) upgraded.push_back(c.get());
    for (int i = 0; i < 2 && !upgraded.empty(); ++i) {
      Card* c = rng().nextItem(upgraded);
      upgraded.erase(std::find(upgraded.begin(), upgraded.end(), c));
      downgrade(c);
    }
    std::vector<Card*> upgradable;
    for (auto& c : run->deck) if (c->upgradable()) upgradable.push_back(c.get());
    for (int i = 0; i < 4 && !upgradable.empty(); ++i) {
      Card* c = rng().nextItem(upgradable);
      upgradable.erase(std::find(upgradable.begin(), upgradable.end(), c));
      c->upgrade();
    }
    setFinished("TOUCH_A_MIRROR");
    co_return;
  }
  Task<> shatter() {
    size_t original = run->deck.size();
    for (size_t i = 0; i < original; ++i) run->addCardToDeck(run->deck[i]->clone());
    run->addCardToDeck(db::card("BadLuck"));
    setFinished("SHATTER");
    co_return;
  }
};

// RoundTeaParty.cs: drink the tea (Royal Poison, full heal), or pick a fight (lose 11 HP, random relic).
struct RoundTeaParty : Event {
  EVENT_HEADER(RoundTeaParty, "ROUND_TEA_PARTY")
  void calculateVars() override {
    addVar("Damage", 11);
    setStr("Relic", "relics.ROYAL_POISON.title");
  }
  bool isAllowed(Run& r) override { return r.player->hp >= 12; }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "ENJOY_TEA", [this] { return enjoyTea(); }),
            option("INITIAL", "PICK_FIGHT", [this] { return pickFight(); })};
  }
  Task<> enjoyTea() {
    co_await run->obtainRelic(db::relic("RoyalPoison"));
    Creature* p = owner();
    p->hp = p->maxHp;
    setFinished("ENJOY_TEA");
  }
  Task<> pickFight() {
    setPage("PICK_FIGHT", {option("PICK_FIGHT", "CONTINUE_FIGHT", [this] { return continueFight(); })});
    co_return;
  }
  Task<> continueFight() {
    co_await run->loseHp(val("Damage").toInt());
    if (run->died) co_return;
    co_await run->obtainRelic(run->pullRelicFromFront(run->relicBag, run->rollRelicRarity(run->rng("Rewards"))));
    setFinished("CONTINUE_FIGHT");
  }
};

// Trial.cs: judge a defendant; the pages are composed from TRIAL.trialFormat / trialResult.
struct Trial : Event {
  EVENT_HEADER(Trial, "TRIAL")
  void calculateVars() override {
    if (!var("EntrantNumber")) addVar("EntrantNumber", Dec((int)(time(nullptr) % 899) + 101));
  }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "ACCEPT", [this] { return accept(); }),
            option("INITIAL", "REJECT", [this] { return reject(); })};
  }
  Task<> accept() {
    std::string story;
    switch (rng().nextInt(3)) {
      case 0:
        story = "MERCHANT";
        options = {option("MERCHANT", "GUILTY", [this] { return merchantGuilty(); }),
                   option("MERCHANT", "INNOCENT", [this] { return merchantInnocent(); })};
        break;
      case 1:
        story = "NOBLE";
        options = {option("NOBLE", "GUILTY", [this] { return nobleGuilty(); }),
                   option("NOBLE", "INNOCENT", [this] { return nobleInnocent(); })};
        break;
      default:
        story = "NONDESCRIPT";
        options = {option("NONDESCRIPT", "GUILTY", [this] { return nondescriptGuilty(); }),
                   option("NONDESCRIPT", "INNOCENT", [this] { return nondescriptInnocent(); })};
        break;
    }
    setStr("TrialStory", page(story) + ".description");
    descKey = "TRIAL.trialFormat";
    co_return;
  }
  Task<> reject() {
    setPage("REJECT", {option("REJECT", "ACCEPT", [this] { return accept(); }),
                       option("REJECT", "DOUBLE_DOWN", [this] { return doubleDown(); })});
    co_return;
  }
  // PORT NOTE: the C# opens NAbandonRunConfirmPopup (confirm, then abandon the run to the title); the port has no
  // in-run confirm popup / abandon route reachable from an event (Run::abandon is pause-menu driven), so the player just dies.
  Task<> doubleDown() { co_await run->loseHp(owner()->hp); }

  void setTrialFinished(const std::string& resultPage) {
    setStr("TrialResult", page(resultPage) + ".description");
    descKey = "TRIAL.trialResult";
    options.clear();
    finished = true;
  }
  Task<> relicFromBag() {
    co_await run->obtainRelic(run->pullRelicFromFront(run->relicBag, run->rollRelicRarity(run->rng("Rewards"))));
  }
  Task<> merchantGuilty() {
    run->addCardToDeck(db::card("Regret"));
    for (int i = 0; i < 2; ++i) co_await relicFromBag();
    setTrialFinished("MERCHANT_GUILTY");
  }
  Task<> merchantInnocent() {
    run->addCardToDeck(db::card("Shame"));
    auto picked = co_await run->selectFromDeck("card_selection.TO_UPGRADE", [](Card* c) { return c->upgradable(); }, 2, false, true);
    for (Card* c : picked) c->upgrade();
    setTrialFinished("MERCHANT_INNOCENT");
  }
  Task<> nobleGuilty() {
    Creature* p = owner();
    p->hp = std::min(p->maxHp, p->hp + 10);
    setTrialFinished("NOBLE_GUILTY");
    co_return;
  }
  Task<> nobleInnocent() {
    run->addCardToDeck(db::card("Regret"));
    co_await run->gainGold(300);
    setTrialFinished("NOBLE_INNOCENT");
  }
  Task<> nondescriptGuilty() {
    run->addCardToDeck(db::card("Doubt"));
    // RewardsCmd.OfferCustom: two CardRewards (ForNonCombatWithDefaultOdds(character pool), 3 cards).
    std::vector<Run::RewardItem> rows;
    for (int i = 0; i < 2; ++i)
      rows.push_back(run->makeCardReward(CardCreationOptions::forNonCombat({run->characterId}, false), 3));
    co_await run->offerRewards(std::move(rows));
    setTrialFinished("NONDESCRIPT_GUILTY");
  }
  Task<> nondescriptInnocent() {
    run->addCardToDeck(db::card("Doubt"));
    auto picked = co_await run->selectFromDeck("card_selection.TO_TRANSFORM", [](Card*) { return true; }, 2);
    for (Card* c : picked) run->transformCard(c, run->randomTransformFor(c, rng()));
    setTrialFinished("NONDESCRIPT_INNOCENT");
  }
};

// TinkerTime.cs: design a MadScience card (type, then one of two random riders).
struct TinkerTime : Event {
  EVENT_HEADER(TinkerTime, "TINKER_TIME")
  CardType chosen = CardType::Attack;
  void calculateVars() override {
    addVar("Damage", 12);
    addVar("Block", 8);
    addVar("SappingWeak", 2);
    addVar("SappingVulnerable", 2);
    addVar("ViolenceHits", 3);
    addVar("ChokingDamage", 6);
    addVar("EnergizedEnergy", 2);
    addVar("WisdomCards", 3);
    addVar("ExpertiseStrength", 2);
    addVar("ExpertiseDexterity", 2);
    addVar("CuriousReduction", 1);
    addVar("energyPrefix", 1);
  }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "CHOOSE_CARD_TYPE", [this] { return chooseCardType(); })};
  }
  Task<> chooseCardType() {
    std::vector<EventOption> all = {
        option("CHOOSE_CARD_TYPE", "ATTACK", [this] { return pickType(CardType::Attack); }),
        option("CHOOSE_CARD_TYPE", "SKILL", [this] { return pickType(CardType::Skill); }),
        option("CHOOSE_CARD_TYPE", "POWER", [this] { return pickType(CardType::Power); })};
    rng().shuffle(all);  // TakeRandom(2)
    all.resize(2);
    setPage("CHOOSE_CARD_TYPE", std::move(all));
    co_return;
  }
  Task<> pickType(CardType t) {
    chosen = t;
    std::vector<int> riders = t == CardType::Attack ? std::vector<int>{kSapping, kViolence, kChoking}
                            : t == CardType::Skill  ? std::vector<int>{kEnergized, kWisdom, kChaos}
                                                    : std::vector<int>{kExpertise, kCurious, kImprovement};
    rng().shuffle(riders);
    riders.resize(2);
    std::vector<EventOption> opts;
    for (int r : riders) {
      std::string name = kRiderNames[r];
      for (auto& ch : name) ch = (char)toupper((unsigned char)ch);
      opts.push_back(option("CHOOSE_RIDER", name, [this, r] { return riderChosen(r); }));
    }
    setPage("CHOOSE_RIDER", std::move(opts));
    co_return;
  }
  Task<> riderChosen(int rider) {
    auto card = std::make_unique<MadScience>();
    card->configure(chosen, rider);
    run->addCardToDeck(std::move(card));
    setFinished("DONE");
    co_return;
  }
};

}  // namespace

void registerAct3Events() {
  registerCardType<Regret>();
  registerCardType<Shame>();
  registerCardType<Doubt>();
  registerCardType<BadLuck>();
  registerCardType<Decay>();
  registerCardType<MadScience>();
  registerPowerType<StranglePower>();
  registerPowerType<CuriousPower>();
  registerPowerType<ImprovementPower>();
  registerPowerType<BattlewornDummyTimeLimitPower>();
  db::registerRelic(BigMushroom::kId, [] { return std::unique_ptr<Relic>(new BigMushroom()); });
  db::registerRelic(FragrantMushroom::kId, [] { return std::unique_ptr<Relic>(new FragrantMushroom()); });
  db::registerRelic(RoyalPoison::kId, [] { return std::unique_ptr<Relic>(new RoyalPoison()); });
  db::registerRelic(ForgottenSoul::kId, [] { return std::unique_ptr<Relic>(new ForgottenSoul()); });
  db::registerEncounter("BattlewornDummyEventV1Encounter", RoomType::Monster, false,
                        [](Rng&) { std::vector<std::unique_ptr<Monster>> v; v.push_back(mk<BattleFriendV1>()); return v; });
  db::registerEncounter("BattlewornDummyEventV2Encounter", RoomType::Monster, false,
                        [](Rng&) { std::vector<std::unique_ptr<Monster>> v; v.push_back(mk<BattleFriendV2>()); return v; });
  db::registerEncounter("BattlewornDummyEventV3Encounter", RoomType::Monster, false,
                        [](Rng&) { std::vector<std::unique_ptr<Monster>> v; v.push_back(mk<BattleFriendV3>()); return v; });
  reg<BattlewornDummy>();
  reg<GraveOfTheForgotten>();
  reg<HungryForMushrooms>();
  reg<Reflections>();
  reg<RoundTeaParty>();
  reg<Trial>();
  reg<TinkerTime>();
}

namespace db {
// Glory.AllEvents (Act 3 pool; the shared events are added by the act setup, see sharedEvents()).
const std::vector<std::string>& act3Events() {
  static const std::vector<std::string> ids = {"BattlewornDummy", "GraveOfTheForgotten", "HungryForMushrooms",
                                               "Reflections", "RoundTeaParty", "Trial", "TinkerTime"};
  return ids;
}
}  // namespace db

}  // namespace sts
