// Shared events (package A7b), translated from MegaCrit.Sts2.Core.Models.Events:
// TheLegendsWereTrue, WarHistorianRepy, WelcomeToWongos, plus HistoryCourse, WongosMysteryTicket and
// WongoCustomerAppreciationBadge. Meta progression (WongoPoints in the save, FreedRepy) is n/a.
#include <algorithm>

#include "cards.h"

namespace sts {

namespace {
template <class E> void reg() { db::registerEvent(E::kId, [] { return std::unique_ptr<Event>(new E()); }); }
#define EVENT_HEADER(Name, Key) \
  static constexpr const char* kId = #Name; \
  Name() { id = #Name; locKey = Key; }

// RelicFactory.PullNextRelicFromFront(owner, rarity, filter): the first relic of the rarity passing
// the filter (an empty / non-matching rarity falls through Shop -> Common -> Uncommon -> Rare),
// removed from every bag; FallbackRelic (Circlet) if nothing matches.
std::unique_ptr<Relic> pullRelicFiltered(Run& r, RelicRarity k, bool shopsOnly) {
  while (k != RelicRarity::None) {
    auto& v = r.relicBag[k];
    for (auto& id : v) {
      auto rel = db::relic(id);
      if (!rel || (shopsOnly && !rel->allowedInShops())) continue;
      std::string picked = id;
      for (auto& [kk, vv] : r.relicBag) vv.erase(std::remove(vv.begin(), vv.end(), picked), vv.end());
      for (auto& [kk, vv] : r.sharedRelicBag) vv.erase(std::remove(vv.begin(), vv.end(), picked), vv.end());
      return rel;
    }
    k = k == RelicRarity::Shop ? RelicRarity::Common
      : k == RelicRarity::Common ? RelicRarity::Uncommon
      : k == RelicRarity::Uncommon ? RelicRarity::Rare : RelicRarity::None;
  }
  return db::relic("Circlet");
}

// RelicReward.Populate: PullNextRelicFromFront(owner) with a rolled rarity.
std::unique_ptr<Relic> rewardRelic(Run& r) {
  return r.pullRelicFromFront(r.relicBag, r.rollRelicRarity(r.rng("Rewards")));
}

}  // namespace

// ================================================================ relics

// HistoryCourse.cs: each turn, auto-play a copy of the last Attack played the previous turn.
struct HistoryCourse : Relic {
  RELIC_HEADER(HistoryCourse, "HISTORY_COURSE", Event)
  }
  // CombatHistory.CardPlaysFinished (this player turn / the last one): only the last Attack matters.
  Card* thisTurn = nullptr;
  Card* lastTurn = nullptr;
  Task<> beforeCombatStart() override {
    thisTurn = lastTurn = nullptr;
    co_return;
  }
  Task<> afterCombatEnd() override {
    thisTurn = lastTurn = nullptr;
    co_return;
  }
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (p.card->type == CardType::Attack && !p.card->isDupe) thisTurn = p.card;
    co_return;
  }
  Task<> afterAutoPrePlayPhaseEntered() override {
    lastTurn = thisTurn;
    thisTurn = nullptr;
    if (!combat || combat->turnNumber == 1 || !lastTurn) co_return;
    doFlash();
    std::unique_ptr<Card> dupe = lastTurn->createClone();  // CardModel.CreateDupe: CreateClone, IsDupe, no Exhaust
    dupe->isDupe = true;
    dupe->removeKeyword(kwExhaust);
    Card* raw = combat->addCard(std::move(dupe));
    co_await cmd::autoPlay(*combat, raw, nullptr);
  }
};

// WongosMysteryTicket.cs: after 5 combats finished, the next combat rewards 3 extra relics.
struct WongosMysteryTicket : Relic {
  RELIC_HEADER(WongosMysteryTicket, "WONGOS_MYSTERY_TICKET", Event)
    addVar("Repeat", 3);
    addVar("RemainingCombats", 5);
  }
  int combatsFinished = 0;
  void persist(Archive& a) override { a.io(combatsFinished); a.io(usedUp); }  // [SavedProperty] x2
  bool showCounter() const override { return displayAmount() > 0; }
  int displayAmount() const override { return 5 - combatsFinished; }
  Task<> afterCombatEnd() override {
    ++combatsFinished;
    if (auto* v = var("RemainingCombats")) v->base = std::max(0, 5 - combatsFinished);
    co_return;
  }
  // TryModifyRewards + AfterModifyingRewards (only called for combat rooms).
  int bonusRelicRewards(RoomType) override {
    if (usedUp || 5 - combatsFinished > 0) return 0;
    doFlash();
    usedUp = true;  // GaveRelic
    return val("Repeat").toInt();
  }
};

// WongoCustomerAppreciationBadge.cs: no effect.
struct WongoCustomerAppreciationBadge : Relic {
  RELIC_HEADER(WongoCustomerAppreciationBadge, "WONGO_CUSTOMER_APPRECIATION_BADGE", Event)
  }
};

// ================================================================ events

// TheLegendsWereTrue.cs (act 1, HP >= 10): a map to a treasure, or 8 damage and a random potion.
struct TheLegendsWereTrue : Event {
  EVENT_HEADER(TheLegendsWereTrue, "THE_LEGENDS_WERE_TRUE")
  bool isAllowed(Run& r) override { return r.actIndex == 0 && !r.deck.empty() && r.player->hp >= 10; }
  void calculateVars() override { addVar("Damage", 8); }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "NAB_THE_MAP", [this] { return nabTheMap(); }),
            option("INITIAL", "SLOWLY_FIND_AN_EXIT", [this] { return slowlyFindAnExit(); })};
  }
  // NAB_THE_MAP: the SpoilsMap quest card (E2: act 2 becomes a SpoilsActMap, quests.cpp).
  Task<> nabTheMap() {
    run->addCardToDeck(db::card("SpoilsMap"));
    co_await wait(0.5);  // Cmd.CustomScaledWait(0.5, 1.2)
    setFinished("NAB_THE_MAP");
  }
  Task<> slowlyFindAnExit() {
    co_await run->loseHp(val("Damage").toInt());  // Unblockable | Unpowered
    if (run->died) co_return;
    // Character potion pool + SharedPotionPool, one NextItem from the Rewards stream.
    std::vector<std::string> items;
    for (auto& id : db::potionPool(run->characterId)) if (db::potion(id)) items.push_back(id);
    std::string pick = run->rng("Rewards").nextItem(items);
    if (!pick.empty()) {
      auto p = db::potion(pick);
      p->run = run;
      co_await run->offerPotion(std::move(p));  // RewardsCmd.OfferCustom(PotionReward)
    }
    setFinished("SLOWLY_FIND_AN_EXIT");
  }
};

// WarHistorianRepy.cs (IsAllowed is false in C#; reached only through LanternKey.ModifyNextEvent in
// act 3): trade the Lantern Key for HistoryCourse (cage) or 2 potions + 2 relics (chest); a second
// pick is offered in single player when a Lantern Key is still in the deck.
struct WarHistorianRepy : Event {
  EVENT_HEADER(WarHistorianRepy, "WAR_HISTORIAN_REPY")
  bool isAllowed(Run&) override { return false; }
  bool shouldGetSecondReward() const {
    for (auto& c : run->deck) if (c->id == "LanternKey") return true;  // Players.Count <= 1
    return false;
  }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "UNLOCK_CAGE", [this] { return initialUnlockCage(); }),
            option("INITIAL", "UNLOCK_CHEST", [this] { return initialUnlockChest(); })};
  }
  Task<> initialUnlockCage() {
    removeFirstLanternKey();  // RemoveLanternKeysForInitialChoice (single player)
    co_await unlockCage();
    if (shouldGetSecondReward())
      setPage("UNLOCK_CAGE", {option("INITIAL", "UNLOCK_CHEST", [this] { return secondUnlockChest(); })});
    else
      setFinished("UNLOCK_CAGE");
  }
  Task<> initialUnlockChest() {
    removeFirstLanternKey();
    co_await unlockChest();
    if (shouldGetSecondReward())
      setPage("UNLOCK_CHEST", {option("INITIAL", "UNLOCK_CAGE", [this] { return secondUnlockCage(); })});
    else
      setFinished("UNLOCK_CHEST");
  }
  Task<> secondUnlockCage() {
    setFinished("EXTRA_UNLOCK_CAGE");
    removeAllLanternKeys();
    co_await unlockCage();
  }
  Task<> secondUnlockChest() {
    setFinished("EXTRA_UNLOCK_CHEST");
    removeAllLanternKeys();
    co_await unlockChest();
  }
  // RewardsCmd.OfferCustom: two potion and two relic rewards, populated in that order.
  Task<> unlockChest() {
    std::vector<Run::RewardItem> rows;
    for (int i = 0; i < 2; ++i) {
      Run::RewardItem p;
      p.kind = Run::RewardKind::Potion;
      p.potion = run->randomPotion(run->rng("Rewards"), false);
      rows.push_back(std::move(p));
    }
    for (int i = 0; i < 2; ++i) {
      Run::RewardItem r;
      r.kind = Run::RewardKind::Relic;
      r.relic = rewardRelic(*run);
      if (r.relic) rows.push_back(std::move(r));
    }
    co_await run->offerRewards(std::move(rows));
  }
  Task<> unlockCage() {
    // FreedRepy (RunState.ExtraFields, meta) is n/a.
    co_await run->obtainRelic(db::relic("HistoryCourse"));
  }
  void removeFirstLanternKey() {
    for (auto& c : run->deck)
      if (c->id == "LanternKey") { cmd::completeQuest(*run, c.get()); run->removeCardFromDeck(c.get()); return; }
  }
  void removeAllLanternKeys() {
    std::vector<Card*> keys;
    for (auto& c : run->deck) if (c->id == "LanternKey") keys.push_back(c.get());
    for (Card* c : keys) { cmd::completeQuest(*run, c); run->removeCardFromDeck(c); }
  }
};

// WelcomeToWongos.cs (act 2, needs 100 gold): buy the bargain bin (Common relic), the featured
// Rare relic or a Wongo's Mystery Ticket; leaving downgrades a random upgraded card.
struct WelcomeToWongos : Event {
  EVENT_HEADER(WelcomeToWongos, "WELCOME_TO_WONGOS")
  static constexpr int kPointsForBadge = 2000;
  std::unique_ptr<Relic> featured;
  bool isAllowed(Run& r) override { return r.actIndex == 1 && r.gold >= 100; }
  void calculateVars() override {
    addVar("BargainBinCost", 100);
    addVar("MysteryBoxCost", 300);
    addVar("FeaturedItemCost", 200);
    addVar("MysteryBoxRelicCount", 3);
    addVar("MysteryBoxCombatCount", 5);
    addVar("WongoPointAmount", 0);
    addVar("RemainingWongoPointAmount", 0);
    addVar("TotalWongoBadgeAmount", 0);
    setStr("RandomRelic", "");
  }
  std::vector<EventOption> initialOptions() override {
    featured = pullRelicFiltered(*run, RelicRarity::Rare, true);
    featured->run = run;
    setStr("RandomRelic", "relics." + featured->locKey + ".title");
    std::vector<EventOption> o;
    int gold = run->gold;
    o.push_back(gold >= val("BargainBinCost").toInt()
                    ? option("INITIAL", "BARGAIN_BIN", [this] { return buyBargainBin(); })
                    : EventOption{page("INITIAL") + ".options.BARGAIN_BIN_LOCKED", nullptr});
    o.push_back(gold >= val("FeaturedItemCost").toInt()
                    ? option("INITIAL", "FEATURED_ITEM", [this] { return buyFeaturedItem(); })
                    : EventOption{page("INITIAL") + ".options.FEATURED_ITEM_LOCKED", nullptr});
    o.push_back(gold >= val("MysteryBoxCost").toInt()
                    ? option("INITIAL", "MYSTERY_BOX", [this] { return buyMysteryBox(); })
                    : EventOption{page("INITIAL") + ".options.MYSTERY_BOX_LOCKED", nullptr});
    o.push_back(option("INITIAL", "LEAVE", [this] { return leave(); }));
    return o;
  }
  // Returns the finishing page. PORT NOTE (n/a: equivalent): SaveManager.Progress.WongoPoints is only
  // read, never incremented, in the C# (ExtraFields.WongoPoints is written and never read back), so it is 0
  // there too: the Wongo badge (2000 points) never triggers.
  Task<std::string> checkObtainWongoBadge(int pointsEarned) {
    int wongoPoints = 0;
    int num = wongoPoints % kPointsForBadge;
    int num2 = num + pointsEarned;
    int num3 = wongoPoints + pointsEarned;
    setVar("WongoPointAmount", num2);
    setVar("RemainingWongoPointAmount", kPointsForBadge - num2);
    setVar("TotalWongoBadgeAmount", num3 / kPointsForBadge);
    if (num2 >= kPointsForBadge) {
      co_await run->obtainRelic(db::relic("WongoCustomerAppreciationBadge"));
      co_return "AFTER_BUY_RECEIVE_BADGE";
    }
    if (val("TotalWongoBadgeAmount").toInt() > 0) co_return "AFTER_BUY_BADGE_COUNTER";
    co_return "AFTER_BUY";
  }
  Task<> buyBargainBin() {
    run->gold -= val("BargainBinCost").toInt();  // PlayerCmd.LoseGold(Spent)
    co_await run->obtainRelic(pullRelicFiltered(*run, RelicRarity::Common, true));
    setFinished(co_await checkObtainWongoBadge(32));
  }
  Task<> buyMysteryBox() {
    run->gold -= val("MysteryBoxCost").toInt();
    co_await run->obtainRelic(db::relic("WongosMysteryTicket"));
    setFinished(co_await checkObtainWongoBadge(8));
  }
  Task<> buyFeaturedItem() {
    run->gold -= val("FeaturedItemCost").toInt();
    co_await run->obtainRelic(std::move(featured));
    setFinished(co_await checkObtainWongoBadge(16));
  }
  Task<> leave() {
    std::vector<Card*> upgraded;
    for (auto& c : run->deck) if (c->upgraded()) upgraded.push_back(c.get());
    Card* c = rng().nextItem(upgraded);
    if (c) cmd::downgradeCard(c);
    setFinished("LEAVE");
    co_return;
  }
};

void registerShared3Events() {
  db::registerRelic(HistoryCourse::kId, [] { return std::unique_ptr<Relic>(new HistoryCourse()); });
  db::registerRelic(WongosMysteryTicket::kId, [] { return std::unique_ptr<Relic>(new WongosMysteryTicket()); });
  db::registerRelic(WongoCustomerAppreciationBadge::kId,
                    [] { return std::unique_ptr<Relic>(new WongoCustomerAppreciationBadge()); });
  reg<TheLegendsWereTrue>();
  reg<WarHistorianRepy>();
  reg<WelcomeToWongos>();
}

}  // namespace sts
