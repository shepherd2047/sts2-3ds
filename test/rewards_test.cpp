// E8 checks: CardCreationOptions + Run::createForReward (pool hooks, flags), the shared reward list
// (Run::offerRewards) with CardRewardAlternatives (Driftwood's REROLL, PaelsWing's SACRIFICE, skip),
// SpecialCardReward rows, transformCard running the added-to-deck hooks, CanUseOrRemovePotions.
// Build: make -f Makefile.sdl build/rewards_test ; run: ./build/rewards_test
#include <cstdio>
#include <cstdlib>

#include "../source/core/game.h"

using namespace sts;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    ++checks;                                                           \
    if (!(cond)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #cond); } \
  } while (0)

static bool pump(const std::function<bool()>& done, int maxFrames = 4000) {
  for (int i = 0; i < maxFrames && !done(); ++i) Scheduler::get().update(0.05);
  return done();
}

static Relic* give(Run& r, const char* id) {
  auto rel = db::relic(id);
  if (!rel) return nullptr;
  rel->run = &r;
  r.relics.push_back(std::move(rel));
  return r.relics.back().get();
}

static Task<> offerTask(Run* r, std::vector<Run::RewardItem>* items, bool* done) {
  co_await r->offerRewards(std::move(*items));
  *done = true;
}

static int count(const Run& r, const std::string& id) {
  int n = 0;
  for (auto& c : r.deck) n += c->id == id;
  return n;
}

int main() {
  // ---- createForReward: distinct cards from the pools, flags gate the hooks
  {
    Run r;
    r.start(11);
    auto o = CardCreationOptions::forRoom(r.characterId, RoomType::Monster);
    CHECK(o.source == CardSource::Encounter && o.odds == RarityOdds::RegularEncounter);
    auto cards = r.createForReward(o.with(ccIsCardReward), 3);
    CHECK(cards.size() == 3);
    CHECK(cards[0]->id != cards[1]->id && cards[1]->id != cards[2]->id && cards[0]->id != cards[2]->id);
    for (auto& c : cards) CHECK(!db::isColorless(c->id));

    // DingyRug unions the colorless pool into card rewards only, and never with NoCardPoolModifications.
    Relic* rug = give(r, "DingyRug");
    CHECK(rug != nullptr);
    CardCreationOptions a = CardCreationOptions::forRoom(r.characterId, RoomType::Monster).with(ccIsCardReward);
    rug->modifyCardRewardCreationOptions(a);
    CHECK(a.pools.size() == 2 && a.pools[1] == CardCreationOptions::kColorless);
    CardCreationOptions b = CardCreationOptions::forRoom(r.characterId, RoomType::Monster);
    rug->modifyCardRewardCreationOptions(b);
    CHECK(b.pools.size() == 1);
    CardCreationOptions c = CardCreationOptions::forRoom(r.characterId, RoomType::Monster).with(ccIsCardReward | ccNoCardPoolModifications);
    rug->modifyCardRewardCreationOptions(c);
    CHECK(c.pools.size() == 1);
    // Uniform colorless with a filter only yields that pool's cards.
    auto col = r.createForReward(CardCreationOptions::forNonCombat({CardCreationOptions::kColorless}, true), 4);
    CHECK(col.size() == 4);
    for (auto& k : col) CHECK(db::isColorless(k->id) && !k->upgraded());
  }

  // ---- the eggs: TryModifyCardRewardOptionsLate unless NoHookUpgrades
  {
    Run r;
    r.start(12);
    give(r, "MoltenEgg");
    auto atk = [](const Card& c) { return c.type == CardType::Attack; };
    auto up = r.createForReward(CardCreationOptions::forNonCombat({r.characterId}, true, atk), 3);
    CHECK(up.size() == 3);
    for (auto& k : up) CHECK(k->upgraded() || !k->upgradable());
    auto o = CardCreationOptions::forNonCombat({r.characterId}, true, atk);
    auto plain = r.createForReward(o.with(ccNoHookUpgrades), 3);
    for (auto& k : plain) CHECK(!k->upgraded());
  }

  // ---- transformCard: the replacement joins the deck through the added-to-deck hooks
  {
    Run r;
    r.start(13);
    give(r, "MoltenEgg");
    Card* def = nullptr;
    for (auto& k : r.deck) if (k->tags & tagDefend) { def = k.get(); break; }
    CHECK(def != nullptr);
    size_t n = r.deck.size();
    Card* bash = r.transformCard(def, db::card("Bash"));
    CHECK(bash && bash->id == "Bash" && bash->upgraded());  // Hook.ModifyCardBeingAddedToDeck (Molten Egg)
    CHECK(r.deck.size() == n && r.deck.back().get() == bash);
    // Bing Bong (AfterCardChangedPiles): the transformed card is added a second time.
    give(r, "BingBong");
    Card* strike = nullptr;
    for (auto& k : r.deck) if (k->tags & tagStrike) { strike = k.get(); break; }
    int before = count(r, "Anger");
    r.transformCard(strike, db::card("Anger"));
    CHECK(count(r, "Anger") == before + 2 && r.deck.size() == n + 1);
  }

  // ---- offerRewards: Driftwood's reroll, skip keeps the row, picking completes it
  {
    Run r;
    r.start(14);
    give(r, "Driftwood");
    std::vector<Run::RewardItem> items;
    items.push_back(r.makeCardReward(CardCreationOptions::forRoom(r.characterId, RoomType::Monster), 3));
    Run::RewardItem gold;
    gold.kind = Run::RewardKind::Gold;
    gold.gold = 25;
    items.push_back(std::move(gold));
    bool done = false;
    Scheduler::get().spawn(offerTask(&r, &items, &done));
    CHECK(pump([&] { return r.rewardListChoice.waiting(); }));
    CHECK(r.rewardItems.size() == 2 && r.rewardItems[0].kind == Run::RewardKind::Gold);  // sorted by RewardsSetIndex
    CHECK(r.rewardItems[1].canReroll);
    int g = r.gold;
    r.rewardListChoice.fire(0);
    CHECK(pump([&] { return r.rewardListChoice.waiting(); }));
    CHECK(r.gold == g + 25 && r.rewardItems.size() == 1);
    r.rewardListChoice.fire(0);  // open the card row
    CHECK(pump([&] { return r.rewardChoice.waiting(); }));
    CHECK(r.rewardAlternatives.size() == 1 && r.rewardAlternatives[0] == "REROLL");
    std::vector<std::string> first;
    for (auto& c : r.rewardCards) first.push_back(c->id);
    r.rewardChoice.fire((int)r.rewardCards.size());  // REROLL: same screen, new cards
    CHECK(pump([&] { return r.rewardChoice.waiting(); }));
    CHECK(r.rewardCards.size() == 3 && r.rewardAlternatives.empty());  // only once
    r.rewardChoice.fire(-1);  // skip: the row stays
    CHECK(pump([&] { return r.rewardListChoice.waiting(); }));
    CHECK(r.rewardItems.size() == 1 && r.rewardItems[0].cards.size() == 3 && !r.rewardItems[0].canReroll);
    size_t deck = r.deck.size();
    r.rewardListChoice.fire(0);
    CHECK(pump([&] { return r.rewardChoice.waiting(); }));
    std::string want = r.rewardCards[1]->id;
    r.rewardChoice.fire(1);
    CHECK(pump([&] { return done; }));  // a custom set closes once everything is taken
    CHECK(r.deck.size() == deck + 1 && r.deck.back()->id == want);
    Scheduler::get().clear();
  }

  // ---- PaelsWing: SACRIFICE completes the reward; every second one obtains a relic
  {
    Run r;
    r.start(15);
    Relic* wing = give(r, "PaelsWing");
    CHECK(wing != nullptr);
    for (int round = 0; round < 2; ++round) {
      std::vector<Run::RewardItem> items;
      CardCreationOptions o;
      o.pools = {r.characterId};
      items.push_back(r.makeCardReward(o, 3));
      bool done = false;
      size_t relics = r.relics.size(), deck = r.deck.size();
      Scheduler::get().spawn(offerTask(&r, &items, &done));
      CHECK(pump([&] { return r.rewardListChoice.waiting(); }));
      r.rewardListChoice.fire(0);
      CHECK(pump([&] { return r.rewardChoice.waiting(); }));
      CHECK(r.rewardAlternatives.size() == 1 && r.rewardAlternatives[0] == "SACRIFICE");
      r.rewardChoice.fire((int)r.rewardCards.size());
      CHECK(pump([&] { return done; }));
      CHECK(r.deck.size() == deck);
      CHECK(r.relics.size() == relics + (round == 1 ? 1 : 0));
      CHECK(wing->displayAmount() == (round == 0 ? 1 : 0));
      Scheduler::get().clear();
    }
  }

  // ---- SpecialCardReward rows, and an empty custom set shows nothing
  {
    Run r;
    r.start(16);
    std::vector<Run::RewardItem> items;
    Run::RewardItem key;
    key.kind = Run::RewardKind::SpecialCard;
    key.card = db::card("Bash");
    items.push_back(std::move(key));
    bool done = false;
    int bashes = count(r, "Bash");
    Scheduler::get().spawn(offerTask(&r, &items, &done));
    CHECK(pump([&] { return r.rewardListChoice.waiting(); }));
    r.rewardListChoice.fire(0);
    CHECK(pump([&] { return done; }));
    CHECK(count(r, "Bash") == bashes + 1);
    std::vector<Run::RewardItem> none;
    done = false;
    Scheduler::get().spawn(offerTask(&r, &none, &done));
    CHECK(pump([&] { return done; }) && !r.rewardListChoice.waiting());
    Scheduler::get().clear();
  }

  // ---- a reward set offered while another is open (a relic's AfterObtained) puts it back after
  {
    Run r;
    r.start(17);
    std::vector<Run::RewardItem> items;
    Run::RewardItem coffer;
    coffer.kind = Run::RewardKind::Relic;
    coffer.relic = db::relic("LostCoffer");
    items.push_back(std::move(coffer));
    Run::RewardItem gold;
    gold.kind = Run::RewardKind::Gold;
    gold.gold = 7;
    items.push_back(std::move(gold));
    bool done = false;
    Scheduler::get().spawn(offerTask(&r, &items, &done));
    CHECK(pump([&] { return r.rewardListChoice.waiting(); }));
    CHECK(r.rewardItems.size() == 2 && r.rewardItems[1].kind == Run::RewardKind::Relic);
    r.rewardListChoice.fire(1);  // Lost Coffer: its own card + potion set
    CHECK(pump([&] { return r.rewardListChoice.waiting(); }));
    CHECK(r.rewardItems.size() == 2 && r.rewardItems[0].kind == Run::RewardKind::Potion &&
          r.rewardItems[1].kind == Run::RewardKind::Card);
    r.rewardListChoice.fire(-1);  // leave the coffer's set
    CHECK(pump([&] { return r.rewardListChoice.waiting(); }));
    CHECK(r.rewardItems.size() == 1 && r.rewardItems[0].kind == Run::RewardKind::Gold && r.screen == Screen::Reward);
    r.rewardListChoice.fire(0);
    CHECK(pump([&] { return done; }));
    Scheduler::get().clear();
  }

  // ---- Player.CanUseOrRemovePotions
  {
    Run r;
    r.start(18);
    r.potions[0] = db::potion("BlockPotion");
    r.potions[1] = db::potion("FruitJuice");
    if (!r.potions[1]) r.potions[1] = db::potion("BloodPotion");
    int slot = -1;
    for (int i = 0; i < 2; ++i) if (r.potions[i] && r.potions[i]->usage == PotionUsage::AnyTime) slot = i;
    if (slot >= 0) {
      CHECK(r.canUsePotion(slot));
      r.canUseOrRemovePotions = false;
      CHECK(!r.canUsePotion(slot));
      r.canUseOrRemovePotions = true;
    }
    // StoneOfAllTime locks the belt while it runs and unlocks it when finished.
    auto ev = db::event("StoneOfAllTime");
    CHECK(ev != nullptr);
    if (ev) {
      r.actIndex = 1;
      bool done = false;
      Scheduler::get().spawn([](Run* run, std::unique_ptr<Event> e, bool* d) -> Task<> {
        co_await run->runEvent(std::move(e));
        *d = true;
      }(&r, std::move(ev), &done));
      CHECK(pump([&] { return r.eventChoice.waiting(); }));
      CHECK(!r.canUseOrRemovePotions);
      r.eventChoice.fire(0);  // LIFT: drink a potion, the event finishes
      CHECK(pump([&] { return r.currentEvent && r.currentEvent->finished; }));
      CHECK(r.canUseOrRemovePotions);
      r.eventChoice.fire(0);
      CHECK(pump([&] { return done; }));
    }
    Scheduler::get().clear();
  }

  printf("rewards_test: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
