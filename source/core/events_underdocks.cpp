// Underdocks events (package A11e), translated from MegaCrit.Sts2.Core.Models.Events:
// AbyssalBaths, DrowningBeacon, EndlessConveyor, PunchOff (+ PunchOffEventEncounter),
// SpiralingWhirlpool, SunkenTreasury, DoorsOfLightAndDark, TrashHeap, WaterloggedScriptorium.
// Also what they hand out: the relics FresnelLens, DarkstonePeriapt, DreamCatcher, HandDrill,
// MawBank and TheBoot, GlowwaterPotion and the Spiral / Steady enchantments. The event cards
// they give (FeedingFrenzy, Caltrops, ...) are A2's, in content_cards_misc.cpp. The Underdocks
// act itself is A11f.
// Everything sits in an anonymous namespace: A2 / A3c / X* may add the same ids later.
#include <algorithm>
#include <functional>

#include "cards.h"
#include "colorless.h"

namespace sts {

std::unique_ptr<Monster> makePunchConstruct(bool startsWithFastPunch, int startingHpReduction);  // content_underdocks_a.cpp

namespace {

template <class E> void reg() { db::registerEvent(E::kId, [] { return std::unique_ptr<Event>(new E()); }); }
#define EVENT_HEADER(Name, Key) \
  static constexpr const char* kId = #Name; \
  Name() { id = #Name; locKey = Key; }

bool inCombat(const Relic* r) { return r->combat && r->combat->inProgress; }

// RelicReward.Populate: PullNextRelicFromFront(owner) with a rolled rarity.
std::unique_ptr<Relic> rewardRelic(Run& r) {
  return r.pullRelicFromFront(r.relicBag, r.rollRelicRarity(r.rng("Rewards")));
}

// CardModel.CompareTo (model id, then upgrade level) + UnstableShuffle: List.StableShuffle(rng).
void stableShuffleCards(std::vector<Card*>& v, Rng& rng) {
  std::stable_sort(v.begin(), v.end(), [](Card* a, Card* b) {
    return a->id != b->id ? a->id < b->id : a->upgradeLevel < b->upgradeLevel;
  });
  rng.shuffle(v);
}

// ================================================================ powers

// ================================================================ event cards

// ================================================================ enchantments

// Spiral.cs: a Basic Strike / Defend is played Times extra times.
struct Spiral : EnchantmentT<Spiral> {
  ENCHANTMENT_HEADER(Spiral, "SPIRAL")
    addVar("Times", 1);
  }
  bool canEnchant(const Card& c) const override {
    return Enchantment::canEnchant(c) && c.rarity == Rarity::Basic && (c.tags & (tagStrike | tagDefend));
  }
  int enchantPlayCount(int n) override { return n + val("Times").toInt(); }
};

// Steady.cs: the card gains Retain.
struct Steady : EnchantmentT<Steady> {
  ENCHANTMENT_HEADER(Steady, "STEADY")
  }
  void onEnchant() override { card->keywords |= kwRetain; }
};

// ================================================================ potion

// GlowwaterPotion.cs (Event rarity, CombatOnly): exhaust the hand, draw 10.
struct GlowwaterPotion : Potion {
  POTION_HEADER(GlowwaterPotion, "GLOWWATER_POTION", Event, CombatOnly, Self)
    addVar("Cards", 10);
  }
  Task<> onUse(Creature*) override {
    Combat& c = *run->combat;
    std::vector<Card*> hand = c.hand;
    for (Card* k : hand) co_await cmd::exhaustCard(c, k);
    co_await cmd::drawCards(c, val("Cards"));
  }
};

}  // namespace

// ================================================================ relics

namespace {

// FresnelLens.cs: card rewards (and any card added to the deck) that gain block get Nimble 2.
// PORT NOTE: the merchant's card list (ModifyMerchantCardCreationResults) has no hook; a bought
// card is enchanted when it enters the deck instead.
struct FresnelLens : Relic {
  RELIC_HEADER(FresnelLens, "FRESNEL_LENS", Event)
    addVar("NimbleAmount", 2);
  }
  void modifyCardReward(std::vector<std::unique_ptr<Card>>& cards, RoomType, bool late) override {
    if (!late) return;
    doFlash();
    for (auto& c : cards) enchantIfValid(c.get());
  }
  void afterCardAddedToDeck(Card* c) override { enchantIfValid(c); }
  void enchantIfValid(Card* c) {
    auto nimble = db::enchantment("Nimble");
    if (nimble && nimble->canEnchant(*c)) cmd::enchant(c, std::move(nimble), val("NimbleAmount").toInt());
  }
};

// DarkstonePeriapt.cs: +6 max HP whenever a Curse enters the deck.
struct DarkstonePeriapt : Relic {
  RELIC_HEADER(DarkstonePeriapt, "DARKSTONE_PERIAPT", Event)
    addVar("MaxHp", 6);
  }
  static Task<> gain(DarkstonePeriapt* r) {
    if (inCombat(r)) co_await cmd::gainMaxHp(r->owner(), r->val("MaxHp").toInt());
    else co_await r->run->gainMaxHp(r->val("MaxHp").toInt());
  }
  void afterCardAddedToDeck(Card* c) override {
    if (c->type != CardType::Curse) return;
    doFlash();
    run->spawnSide(gain(this));
  }
};

// DreamCatcher.cs: resting to heal also offers a card reward (3 cards, monster odds).
// PORT NOTE: TryModifyRestSiteHealRewards has no hook; the reward is offered from afterRestSiteHeal.
struct DreamCatcher : Relic {
  RELIC_HEADER(DreamCatcher, "DREAM_CATCHER", Event)
  }
  Task<> afterRestSiteHeal() override {
    doFlash();
    auto cards = run->cardReward(RoomType::Monster, 3);
    for (bool late : {false, true})
      for (auto& rel : run->relics) rel->modifyCardReward(cards, RoomType::Monster, late);
    co_await run->chooseCardFor(std::move(cards));
  }
};

// HandDrill.cs: breaking an enemy's block applies 2 Vulnerable (Hook.AfterBlockBroken).
struct HandDrill : Relic {
  RELIC_HEADER(HandDrill, "HAND_DRILL", Event)
    addVar("VulnerablePower", 2);
  }
  Task<> afterBlockBroken(Creature* target, Creature* dealer) override {
    if (!target || target->isPlayer) co_return;
    if (!dealer || (dealer != owner() && dealer->petOwner != owner())) co_return;
    doFlash();
    co_await applyPower<VulnerablePower>(target, val("VulnerablePower"), owner(), nullptr);
  }
};

// MawBank.cs: +12 gold on entering each room until something is bought in a shop.
struct MawBank : Relic {
  RELIC_HEADER(MawBank, "MAW_BANK", Event)
    addVar("Gold", 12);
  }
  void persist(Archive& a) override { a.io(usedUp); }  // HasItemBeenBought
  // BaseRoom == room: a fight started from an event is not the base room.
  Task<> afterRoomEntered(RoomType t) override {
    if (usedUp) co_return;
    bool fight = t == RoomType::Monster || t == RoomType::Elite || t == RoomType::Boss;
    if (fight && run && run->currentEvent) co_return;
    doFlash();
    co_await run->gainGold(val("Gold").toInt());
  }
  Task<> afterItemPurchased(int goldSpent) override {
    if (usedUp || goldSpent <= 0) co_return;
    doFlash();
    usedUp = true;
  }
};

// TheBoot.cs: the owner's powered attacks that deal 1-4 unblocked damage to an enemy deal 5
// (in the Late after-Osty phase, so after e.g. Intangible).
struct TheBoot : Relic {
  RELIC_HEADER(TheBoot, "THE_BOOT", Event)
    addVar("DamageMinimum", 5);
    addVar("DamageThreshold", 4);
  }
  Dec modifyHpLostAfterOstyLate(Creature* target, Dec amount, int props, Creature* dealer, Card*) override {
    if (!dealer || (dealer != owner() && dealer->petOwner != owner())) return amount;
    if (target == owner() || !isPoweredAttack(props)) return amount;
    Dec min = val("DamageMinimum");
    if (amount < Dec(1) || amount >= min) return amount;
    doFlash();
    return min;
  }
};

}  // namespace

// ================================================================ events

namespace {

// AbyssalBaths.cs: gain 2 max HP for a growing amount of damage, as often as you like.
struct AbyssalBaths : Event {
  EVENT_HEADER(AbyssalBaths, "ABYSSAL_BATHS")
  int lingerCount = 0;
  void calculateVars() override {
    addVar("MaxHp", 2);
    addVar("Damage", 3);  // Unblockable | Unpowered
    addVar("Heal", 10);
  }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "IMMERSE", [this] { return immerse(); }),
            option("INITIAL", "ABSTAIN", [this] { return abstain(); })};
  }
  std::vector<EventOption> allOptions() {
    return {option("ALL", "LINGER", [this] { return linger(); }),
            option("ALL", "EXIT_BATHS", [this] { return exitBaths(); })};
  }
  Task<> immerse() {
    co_await onImmerse();
    if (run->died) co_return;
    setPage("IMMERSE", allOptions());
  }
  Task<> abstain() {
    Creature* p = run->player.get();
    p->hp = std::min(p->maxHp, p->hp + val("Heal").toInt());  // CreatureCmd.Heal
    co_await wait(0.2);
    setFinished("ABSTAIN");
  }
  Task<> linger() {
    if (++lingerCount > 9) lingerCount = 9;
    co_await onImmerse();
    if (run->died) co_return;
    int damage = val("Damage").toInt() - val("MaxHp").toInt();
    if (run->player->hp <= damage)  // WillKillPlayer
      setPage("DEATH_WARNING", allOptions());
    else
      setPage("LINGER" + std::to_string(lingerCount), allOptions());
  }
  Task<> exitBaths() {
    setFinished("EXIT_BATHS");
    co_return;
  }
  Task<> onImmerse() {
    co_await run->gainMaxHp(val("MaxHp").toInt());
    co_await run->loseHp(val("Damage").toInt());
    setVar("Damage", val("Damage") + Dec(1));
  }
};

// DrowningBeacon.cs: bottle the water (Glowwater Potion) or climb the lighthouse (13 max HP for
// Fresnel Lens).
struct DrowningBeacon : Event {
  EVENT_HEADER(DrowningBeacon, "DROWNING_BEACON")
  void calculateVars() override {
    addVar("HpLoss", 13);
    setStr("Potion", "potions.GLOWWATER_POTION.title");
    setStr("Relic", "relics.FRESNEL_LENS.title");
  }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "BOTTLE", [this] { return bottle(); }),
            option("INITIAL", "CLIMB", [this] { return climb(); })};
  }
  Task<> bottle() {
    auto p = db::potion("GlowwaterPotion");
    p->run = run;
    co_await run->offerPotion(std::move(p));  // RewardsCmd.OfferCustom(PotionReward)
    setFinished("BOTTLE");
  }
  Task<> climb() {
    co_await run->loseMaxHp(val("HpLoss").toInt());
    co_await run->obtainRelic(db::relic("FresnelLens"));
    setFinished("CLIMB");
  }
};

// EndlessConveyor.cs (needs 120 gold): buy rolling dishes off the belt for 40 gold each.
struct EndlessConveyor : Event {
  EVENT_HEADER(EndlessConveyor, "ENDLESS_CONVEYOR")
  struct Dish {
    std::string id;
    float weight = 0;
    std::function<Task<>()> action;
  };
  std::string lastDishId;
  int numOfGrabs = 0;
  Dish currentDish;

  bool isAllowed(Run& r) override { return r.gold >= 120; }
  void calculateVars() override {
    addVar("Gold", 40);
    addVar("GoldenFyshGold", 75);
    addVar("ClamRollHeal", 10);
    addVar("CaviarMaxHp", 4);
    setStr("CurrentDishTitle", "");
    setStr("LastDishTitle", "");
    rollDish();
    setStr("CurrentDishTitle", dishTitle(currentDish.id));
  }
  static std::string dishTitle(const std::string& id) { return "events.ENDLESS_CONVEYOR.DISHES." + id + ".title"; }
  std::vector<EventOption> initialOptions() override {
    return {grabOption(), option("INITIAL", "OBSERVE_CHEF", [this] { return observeChef(); })};
  }
  EventOption grabOption() {
    if (run->gold >= val("Gold").toInt())
      return EventOption{page("ALL") + ".options." + currentDish.id, [this] { return grabSomethingOffTheBelt(); }};
    return EventOption{page("ALL") + ".options.LOCKED", nullptr};
  }
  Task<> grabSomethingOffTheBelt() {
    if (currentDish.id != "GOLDEN_FYSH") run->gold -= val("Gold").toInt();  // LoseGold(Spent)
    co_await currentDish.action();
    if (run->died) co_return;
    rollDish();
    setStr("LastDishTitle", strVars["CurrentDishTitle"]);
    setStr("CurrentDishTitle", dishTitle(currentDish.id));
    setPage("GRAB_SOMETHING_OFF_THE_BELT",
            {grabOption(), option("GRAB_SOMETHING_OFF_THE_BELT", "LEAVE", [this] { return leave(); })});
  }

  Task<> clamRoll() {
    Creature* p = run->player.get();
    p->hp = std::min(p->maxHp, p->hp + val("ClamRollHeal").toInt());
    co_await wait(0.2);
  }
  Task<> caviar() { co_await run->gainMaxHp(val("CaviarMaxHp").toInt()); }
  Task<> suspiciousCondiment() {
    // Character potion pool + SharedPotionPool, one NextItem from the Rewards stream.
    std::vector<std::string> items;
    for (auto& id : db::potionPool(run->characterId)) if (db::potion(id)) items.push_back(id);
    std::string pick = run->rng("Rewards").nextItem(items);
    if (pick.empty()) co_return;
    auto p = db::potion(pick);
    p->run = run;
    co_await run->offerPotion(std::move(p));
  }
  Task<> jellyLiver() {
    auto picked = co_await run->selectFromDeck("card_selection.TO_TRANSFORM", [](Card*) { return true; }, 1);
    for (Card* c : picked) run->transformCard(c, run->randomTransformFor(c, rng()));  // TransformToRandom
  }
  Task<> seapunkSalad() {
    run->addCardToDeck(db::card("FeedingFrenzy"));
    co_return;
  }
  Task<> friedEel() {
    // CardFactory.CreateForReward(1, ForNonCombatWithDefaultOdds(ColorlessCardPool)) into the deck.
    auto cards = colorlessRewardCards(*run, 1);
    if (!cards.empty()) run->addCardToDeck(std::move(cards[0]));
    co_return;
  }
  Task<> goldenFysh() { co_await run->gainGold(val("GoldenFyshGold").toInt()); }
  Task<> spicySnappy() {
    upgradeRandomDeckCard();
    co_return;
  }
  void upgradeRandomDeckCard() {
    std::vector<Card*> up;
    for (auto& c : run->deck) if (c->upgradable()) up.push_back(c.get());
    if (up.empty()) return;
    rng().nextItem(up)->upgrade();
  }

  // Generates the possible dishes and does a weighted roll (RollDish).
  void rollDish() {
    ++numOfGrabs;
    if (numOfGrabs % 5 == 0) {
      lastDishId = "SEAPUNK_SALAD";
      currentDish = {"SEAPUNK_SALAD", 0.f, [this] { return seapunkSalad(); }};
      return;
    }
    std::vector<Dish> list;
    list.push_back({"CAVIAR", 6.f, [this] { return caviar(); }});
    list.push_back({"SPICY_SNAPPY", 3.f, [this] { return spicySnappy(); }});
    list.push_back({"JELLY_LIVER", 3.f, [this] { return jellyLiver(); }});
    list.push_back({"FRIED_EEL", 3.f, [this] { return friedEel(); }});
    if (run->hasOpenPotionSlot()) list.push_back({"SUSPICIOUS_CONDIMENT", 3.f, [this] { return suspiciousCondiment(); }});
    if (run->player->hp != run->player->maxHp) list.push_back({"CLAM_ROLL", 6.f, [this] { return clamRoll(); }});
    if (numOfGrabs > 1) list.push_back({"GOLDEN_FYSH", 1.f, [this] { return goldenFysh(); }});
    list.erase(std::remove_if(list.begin(), list.end(), [&](const Dish& d) { return d.id == lastDishId; }), list.end());
    float total = 0.f;
    for (auto& d : list) total += d.weight;
    float roll = rng().nextFloat() * total;
    float acc = 0.f;
    for (auto& d : list) {
      acc += d.weight;
      if (roll < acc) {
        lastDishId = d.id;
        currentDish = d;
        break;
      }
    }
  }
  Task<> observeChef() {
    upgradeRandomDeckCard();
    setFinished("OBSERVE_CHEF");
    co_return;
  }
  Task<> leave() {
    setFinished("LEAVE");
    co_return;
  }
};

// PunchOff.cs (needs floor >= 6): grab a relic from the brawling constructs for an Injury, or fight.
// PORT NOTE: the combat-layout page (the two constructs punching each other behind the event
// text, LayoutType.Combat) is UI only and not shown; the option texts carry the event.
struct PunchOff : Event {
  EVENT_HEADER(PunchOff, "PUNCH_OFF")
  bool isAllowed(Run& r) override { return r.floor >= 6; }  // TotalFloor
  void calculateVars() override { addVar("Gold", rng().nextInt(91, 99)); }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "NAB", [this] { return nab(); }),
            option("INITIAL", "I_CAN_TAKE_THEM", [this] { return takeThem(); })};
  }
  Task<> nab() {
    run->addCardToDeck(db::card("Injury"));  // CardPileCmd.AddCurseToDeck<Injury>
    co_await run->offerRelic(rewardRelic(*run), false);  // RewardsCmd.OfferCustom(RelicReward)
    setFinished("NAB");
  }
  Task<> takeThem() {
    setPage("I_CAN_TAKE_THEM", {option("I_CAN_TAKE_THEM", "FIGHT", [this] { return fight(); })});
    co_return;
  }
  Task<> fight() {
    // EnterCombatWithoutExitingEvent<PunchOffEventEncounter>(RelicReward + PotionReward, no resume).
    run->extraRewards = {Run::RewardKind::Relic, Run::RewardKind::Potion};
    bool won = co_await run->eventFight("PunchOffEventEncounter");
    if (!won) co_return;
    finished = true;  // the event is over; the map follows
    options.clear();
  }
};

// SpiralingWhirlpool.cs (needs a deck card Spiral can go on): Spiral a Basic card or heal 33%.
struct SpiralingWhirlpool : Event {
  EVENT_HEADER(SpiralingWhirlpool, "SPIRALING_WHIRLPOOL")
  bool isAllowed(Run& r) override { return r.canEnchantAny("Spiral"); }
  void calculateVars() override { addVar("Heal", run && run->player ? Dec(run->player->maxHp) * Dec::lit(0.33) : Dec(0)); }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "OBSERVE", [this] { return observeTheSpiral(); }),
            option("INITIAL", "DRINK", [this] { return drink(); })};
  }
  Task<> observeTheSpiral() {
    auto picked = co_await run->selectForEnchantment("Spiral", 1);
    if (!picked.empty()) run->enchantCard(picked[0], "Spiral", 1);
    setFinished("OBSERVE");
  }
  Task<> drink() {
    Creature* p = run->player.get();
    p->hp = std::min(p->maxHp, p->hp + val("Heal").toInt());
    co_await wait(0.2);
    setFinished("DRINK");
  }
};

// SunkenTreasury.cs: ~60 gold, or ~333 gold and a Greed curse.
struct SunkenTreasury : Event {
  EVENT_HEADER(SunkenTreasury, "SUNKEN_TREASURY")
  void calculateVars() override {
    addVar("SmallChestGold", 60);
    addVar("LargeChestGold", 333);
    setVar("SmallChestGold", val("SmallChestGold") + Dec(rng().nextInt(16) - 8));
    setVar("LargeChestGold", val("LargeChestGold") + Dec(rng().nextInt(61) - 30));
  }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "FIRST_CHEST", [this] { return firstChest(); }),
            option("INITIAL", "SECOND_CHEST", [this] { return secondChest(); })};
  }
  Task<> firstChest() {
    co_await run->gainGold(val("SmallChestGold").toInt());
    setFinished("FIRST_CHEST");
  }
  Task<> secondChest() {
    co_await run->gainGold(val("LargeChestGold").toInt());
    run->addCardToDeck(db::card("Greed"));  // AddCurseToDeck<Greed>
    setStr("Monologue", "characters." + run->character().key + ".goldMonologue");
    setFinished("SECOND_CHEST");
  }
};

// DoorsOfLightAndDark.cs: upgrade 2 random cards, or remove a card.
struct DoorsOfLightAndDark : Event {
  EVENT_HEADER(DoorsOfLightAndDark, "DOORS_OF_LIGHT_AND_DARK")
  void calculateVars() override { addVar("Cards", 2); }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "LIGHT", [this] { return light(); }),
            option("INITIAL", "DARK", [this] { return dark(); })};
  }
  Task<> light() {
    std::vector<Card*> up;
    for (auto& c : run->deck) if (c->upgradable()) up.push_back(c.get());
    stableShuffleCards(up, rng());
    size_t n = std::min(up.size(), (size_t)val("Cards").toInt());
    for (size_t i = 0; i < n; ++i) up[i]->upgrade();
    setFinished("LIGHT");
    co_return;
  }
  Task<> dark() {
    auto picked = co_await run->selectFromDeck("card_selection.TO_REMOVE", [](Card*) { return true; }, 1);
    for (Card* c : picked) run->removeCardFromDeck(c);
    setFinished("DARK");
  }
};

// TrashHeap.cs (needs HP > 5): 8 HP for a random junk relic, or 100 gold and a random junk card.
struct TrashHeap : Event {
  EVENT_HEADER(TrashHeap, "TRASH_HEAP")
  bool isAllowed(Run& r) override { return r.player->hp > 5; }
  void calculateVars() override {
    addVar("HpLoss", 8);
    addVar("Gold", 100);
  }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "DIVE_IN", [this] { return diveIn(); }),
            option("INITIAL", "GRAB", [this] { return grab(); })};
  }
  Task<> diveIn() {
    co_await run->loseHp(val("HpLoss").toInt());  // Unblockable | Unpowered
    if (run->died) co_return;
    static const std::vector<std::string> relics = {"DarkstonePeriapt", "DreamCatcher", "HandDrill", "MawBank", "TheBoot"};
    co_await run->obtainRelic(db::relic(rng().nextItem(relics)));
    setFinished("DIVE_IN");
  }
  Task<> grab() {
    co_await run->gainGold(val("Gold").toInt());
    static const std::vector<std::string> cards = {"Caltrops", "Clash", "Distraction", "DualWield", "Entrench",
                                                   "HelloWorld", "Outmaneuver", "Rebound", "RipAndTear", "Stack"};
    run->addCardToDeck(db::card(rng().nextItem(cards)));
    setFinished("GRAB");
  }
};

// WaterloggedScriptorium.cs (needs 55 gold): +6 max HP, or pay to enchant with Steady.
struct WaterloggedScriptorium : Event {
  EVENT_HEADER(WaterloggedScriptorium, "WATERLOGGED_SCRIPTORIUM")
  bool isAllowed(Run& r) override { return r.gold >= 55; }
  void calculateVars() override {
    addVar("MaxHp", 6);
    addVar("Gold", 55);
    addVar("PricklySpongeGold", 99);
    addVar("Cards", 2);
  }
  std::vector<EventOption> initialOptions() override {
    std::vector<EventOption> o;
    o.push_back(option("INITIAL", "BLOODY_INK", [this] { return bloodyInk(); }));
    o.push_back(run->gold >= val("Gold").toInt()
                    ? option("INITIAL", "TENTACLE_QUILL", [this] { return tentacleQuill(); })
                    : EventOption{page("INITIAL") + ".options.TENTACLE_QUILL_LOCKED", nullptr});
    o.push_back(run->gold >= val("PricklySpongeGold").toInt()
                    ? option("INITIAL", "PRICKLY_SPONGE", [this] { return pricklySponge(); })
                    : EventOption{page("INITIAL") + ".options.PRICKLY_SPONGE_LOCKED", nullptr});
    return o;
  }
  Task<> pricklySponge() {
    run->gold -= val("PricklySpongeGold").toInt();  // LoseGold(Spent)
    auto picked = co_await run->selectForEnchantment("Steady", val("Cards").toInt());
    for (Card* c : picked) run->enchantCard(c, "Steady", 1);
    setFinished("PRICKLY_SPONGE");
  }
  Task<> tentacleQuill() {
    run->gold -= val("Gold").toInt();
    auto picked = co_await run->selectForEnchantment("Steady", 1);
    if (!picked.empty()) run->enchantCard(picked[0], "Steady", 1);
    setFinished("TENTACLE_QUILL");
  }
  Task<> bloodyInk() {
    co_await run->gainMaxHp(val("MaxHp").toInt());
    setFinished("BLOODY_INK");
  }
};

}  // namespace

void registerUnderdocksEvents() {
  db::registerEnchantment(Spiral::kId, [] { return std::unique_ptr<Enchantment>(new Spiral()); });
  db::registerEnchantment(Steady::kId, [] { return std::unique_ptr<Enchantment>(new Steady()); });
  db::registerPotion(GlowwaterPotion::kId, [] { return std::unique_ptr<Potion>(new GlowwaterPotion()); });
  db::registerRelic(FresnelLens::kId, [] { return std::unique_ptr<Relic>(new FresnelLens()); });
  db::registerRelic(DarkstonePeriapt::kId, [] { return std::unique_ptr<Relic>(new DarkstonePeriapt()); });
  db::registerRelic(DreamCatcher::kId, [] { return std::unique_ptr<Relic>(new DreamCatcher()); });
  db::registerRelic(HandDrill::kId, [] { return std::unique_ptr<Relic>(new HandDrill()); });
  db::registerRelic(MawBank::kId, [] { return std::unique_ptr<Relic>(new MawBank()); });
  db::registerRelic(TheBoot::kId, [] { return std::unique_ptr<Relic>(new TheBoot()); });
  // PunchOffEventEncounter.GenerateMonsters: the first construct starts with Fast Punch; both lose 2-9 HP.
  db::registerEncounter("PunchOffEventEncounter", RoomType::Monster, false, [](Rng& rng) {
    std::vector<std::unique_ptr<Monster>> v;
    int a = rng.nextInt(2, 10);
    v.push_back(makePunchConstruct(true, a));
    int b = rng.nextInt(2, 10);
    v.push_back(makePunchConstruct(false, b));
    return v;
  });
  reg<AbyssalBaths>();
  reg<DrowningBeacon>();
  reg<EndlessConveyor>();
  reg<PunchOff>();
  reg<SpiralingWhirlpool>();
  reg<SunkenTreasury>();
  reg<DoorsOfLightAndDark>();
  reg<TrashHeap>();
  reg<WaterloggedScriptorium>();
}

}  // namespace sts
