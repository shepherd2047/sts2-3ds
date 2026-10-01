// E2: quest cards (CardType.Quest), the "?"-room / act-map hooks and deck cards as run-level hook
// listeners. The hooks are on Model (game.h); Run::listeners() lists the deck cards first, as
// RunState.IterateHookListeners does, so a deck card hears every run-level hook (Guilty's
// AfterCombatEnd, Dowsing's BeforeRoomEntered, LanternKey's ModifyUnknownMapPointRoomTypes /
// ModifyNextEvent, SpoilsMap's ModifyGeneratedMap / AfterMapGenerated / BeforeCardRemoved,
// ByrdonisEgg's TryModifyRestSiteOptions). The cards themselves stay in their content files
// (content_cards_misc.cpp, events_act1.cpp, events_act2.cpp); the SpoilsActMap generator is in
// mapgen.cpp. This file has the Run helpers and the relics that hand out quests or use the map
// hooks: DowsingRod, WingedBoots, ScrollBoxes (Neow).
//
// PORT NOTE: PlayerCmd.CompleteQuest only records the quest in the run history's
// MapPointHistoryEntry.CompletedQuests, which history.h does not have; it is not recorded.
#include <algorithm>

#include "card_factory.h"
#include "game.h"

namespace sts {

bool Card::inDeck() const {
  if (!run) return false;
  for (auto& c : run->deck)
    if (c.get() == this) return true;
  return false;
}

// RunManager.GenerateMap: ModifyGeneratedMapLate runs inside Hook.ModifyGeneratedMap for a new map and
// alone for a loaded one (SavedActMap); AfterMapGenerated follows either way. The quest markers are
// rebuilt from scratch, which is how a loaded map gets them back.
void Run::runLateMapHooks() {
  for (auto& n : nodes) n.quests.clear();
  for (Model* m : listeners()) m->modifyGeneratedMapLate(actIndex);
  for (Model* m : listeners()) m->afterMapGenerated(actIndex);
}

Task<> Run::beforeRoomEntered(RoomType type) {
  for (Model* m : listeners()) co_await m->beforeRoomEntered(type);
}

// OneOffSynchronizer.TryHandleSpoilsMap: on a map point marked by a SpoilsMap, every SpoilsMap in the
// deck completes its quest.
Task<int> Run::handleSpoilsMap() {
  if (currentNode < 0 || currentNode >= (int)nodes.size()) co_return 0;
  auto& q = nodes[currentNode].quests;
  if (std::find(q.begin(), q.end(), "SpoilsMap") == q.end()) co_return 0;
  std::vector<Card*> maps;
  for (auto& c : deck)
    if (c->id == "SpoilsMap") maps.push_back(c.get());
  int gold = 0;
  for (Card* c : maps) gold += co_await c->onQuestComplete();
  co_return gold;
}

// CardSelectCmd.FromChooseABundleScreen (no skip), then CardPileCmd.Add(Deck) for each card of the pick.
Task<> Run::chooseBundleFor(std::vector<std::vector<std::unique_ptr<Card>>> bundles) {
  if (bundles.empty() || bundles[0].empty()) co_return;
  const size_t size = bundles[0].size();
  rewardCards.clear();
  for (auto& b : bundles)
    for (size_t i = 0; i < size; ++i) rewardCards.push_back(i < b.size() ? std::move(b[i]) : nullptr);
  rewardBundleSize = (int)size;
  screen = Screen::Reward;
  int pick = co_await rewardChoice.next();
  if (pick < 0 || (size_t)(pick + 1) * size > rewardCards.size()) pick = 0;  // the screen cannot be skipped
  std::vector<std::unique_ptr<Card>> chosen;
  for (size_t i = 0; i < size; ++i) chosen.push_back(std::move(rewardCards[pick * size + i]));
  rewardCards.clear();
  rewardBundleSize = 1;
  for (auto& c : chosen)
    if (c) addCardToDeck(std::move(c));
}

namespace {

template <class R> void reg() { db::registerRelic(R::kId, [] { return std::unique_ptr<Relic>(new R()); }); }

// DowsingRod.cs (Ancient, Neow): adds a Dowsing quest card to the deck.
struct DowsingRod : Relic {
  RELIC_HEADER(DowsingRod, "DOWSING_ROD", Ancient) }
  Task<> afterObtained() override {
    run->addCardToDeck(db::card("Dowsing"));  // CardPileCmd.Add(Deck) (+ the preview)
    co_return;
  }
};

// WingedBoots.cs (Ancient, Neow): free travel to any point of the next row, three times; a use is
// counted when the room entered is not a child of the previous map point. TimesUsed is the
// "Rooms" var (3 - TimesUsed), which the relic save keeps.
struct WingedBoots : Relic {
  RELIC_HEADER(WingedBoots, "WINGED_BOOTS", Ancient) addVar("Rooms", 3); }
  int timesUsed() { return 3 - val("Rooms").toInt(); }
  bool showCounter() const override { return !usedUp; }
  int displayAmount() const override { return const_cast<WingedBoots*>(this)->val("Rooms").toInt(); }
  bool shouldAllowFreeTravel() override { return !usedUp; }
  Task<> afterRoomEntered(RoomType) override {
    if (usedUp || run->currentRoomCount > 1) co_return;
    int prev = run->previousNode, cur = run->currentNode;  // VisitedMapCoords[^2] / CurrentMapPoint
    if (prev < 0 || prev >= (int)run->nodes.size() || cur < 0) co_return;
    auto& next = run->nodes[prev].next;
    if (std::find(next.begin(), next.end(), cur) != next.end()) co_return;
    var("Rooms")->base = Dec(3 - (timesUsed() + 1));  // TimesUsed++
    if (timesUsed() >= 3) usedUp = true;               // CheckIfUsedUp: RelicStatus.Disabled
  }
};

// ScrollBoxes.cs (Ancient, Neow): choose one of two bundles (2 Commons + 1 Uncommon each, six distinct
// cards; a Defect bundle is 3 Claws 1% of the time) from the Rewards stream.
// PORT NOTE: there are no card unlocks, so CanGenerateBundles (IsAllowedAtNeow) only checks the pool
// sizes, which every character passes.
struct ScrollBoxes : Relic {
  RELIC_HEADER(ScrollBoxes, "SCROLL_BOXES", Ancient) }

  // CardCreationOptions.ForNonCombatWithUniformOdds(Character.CardPool, rarity) after
  // Hook.ModifyCardRewardCreationOptions (PrismaticGem, the CharacterCards modifier, DingyRug), as in
  // Run::cardReward: GetPossibleCards in pool order.
  static std::vector<std::string> possible(Run& r, Rarity want) {
    auto f = [want](const Card& c) { return c.rarity == want; };
    bool allPools = false;
    for (auto& rel : r.relics) allPools = allPools || rel->allCharacterCardPools();
    std::vector<std::string> chars = {r.characterId};
    for (auto& ch : r.modifierCardPools()) chars.push_back(ch);
    if (allPools) chars = db::allCharacters();
    std::vector<std::string> out;
    auto add = [&](const std::vector<std::string>& ids) {
      for (auto& id : ids)
        if (std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
    };
    for (auto& ch : chars) add(db::characterPool(ch, f));
    for (auto& rel : r.relics)
      if (rel->addsColorlessToCardRewards()) { add(db::colorlessCards(f)); break; }
    return out;
  }
  static bool canGenerateBundles(Run& r) {
    return db::characterPool(r.characterId, [](const Card& c) { return c.rarity == Rarity::Common; }).size() >= 4 &&
           db::characterPool(r.characterId, [](const Card& c) { return c.rarity == Rarity::Uncommon; }).size() >= 2;
  }

  Task<> afterObtained() override {
    if (!canGenerateBundles(*run)) co_return;
    Rng& rewards = run->rng("Rewards");
    const bool defect = run->characterId == "Defect";
    auto commons = possible(*run, Rarity::Common), uncommons = possible(*run, Rarity::Uncommon);
    std::vector<std::string> used;
    auto unused = [&](const std::vector<std::string>& ids) {
      std::vector<std::string> out;
      for (auto& id : ids)
        if (std::find(used.begin(), used.end(), id) == used.end()) out.push_back(id);
      return out;
    };
    std::vector<std::vector<std::unique_ptr<Card>>> bundles;
    for (int b = 0; b < 2; ++b) {
      std::vector<std::unique_ptr<Card>> bundle;
      if (defect && rewards.nextInt(100) < 1) {
        for (int i = 0; i < 3; ++i) bundle.push_back(db::card("Claw"));
        bundles.push_back(std::move(bundle));
        continue;
      }
      std::vector<std::string> pool = unused(commons);
      for (int i = 0; i < 2; ++i) {
        std::string id = rewards.nextItem(pool);
        bundle.push_back(db::card(id));
        used.push_back(id);
        pool.erase(std::remove(pool.begin(), pool.end(), id), pool.end());
      }
      std::string id = rewards.nextItem(unused(uncommons));
      bundle.push_back(db::card(id));
      used.push_back(id);
      bundles.push_back(std::move(bundle));
    }
    co_await run->chooseBundleFor(std::move(bundles));
  }
};

}  // namespace

void registerQuestRelics() {
  reg<DowsingRod>();
  reg<WingedBoots>();
  reg<ScrollBoxes>();
}

}  // namespace sts
