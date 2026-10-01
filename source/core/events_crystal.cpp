// CrystalSphere (package A7d), translated from MegaCrit.Sts2.Core.Models.Events.CrystalSphere and
// MegaCrit.Sts2.Core.Events.Custom.CrystalSphereEvent (CrystalSphereMinigame, CrystalSphereCell,
// CrystalSphereItem + CrystalSphereItems/*) plus OneOffSynchronizer.DoLocalCrystalSphereRewards.
// The minigame's state (events_crystal.h) is shown by ui/screens/crystal_ui.cpp.
//
// PORT NOTE: the fortune teller's lines (NCrystalSphereDialogue) use Rng.Chaotic in C#; here a
// private stream seeded from the run seed, so they never touch the game's RNG.
// The rewards go through Run::offerRewards (RewardsSet.Offer: Hook.ModifyRewards, i.e. Driftwood's
// reroll) and the card rewards through Run::makeCardReward (CardFactory.CreateForReward with its hooks).
// PORT NOTE: RewardsSet sorts with List.Sort (unstable); a stable sort keeps same-kind rewards in
// reveal order.
#include <algorithm>

#include "cards.h"
#include "events_crystal.h"

namespace sts {

namespace {

#define EVENT_HEADER(Name, Key) \
  static constexpr const char* kId = #Name; \
  Name() { id = #Name; locKey = Key; }

using Game = CrystalSphereGame;
using Item = CrystalSphereGame::Item;
using ItemType = CrystalSphereGame::ItemType;
constexpr int N = CrystalSphereGame::kSize;

bool inGrid(int x, int y) { return x >= 0 && x < N && y >= 0 && y < N; }

// GetHorizontalCells / GetVerticalCells.
void horizontal(int x, int y, std::vector<std::pair<int, int>>& out) {
  for (int i = -1; i <= 1; i += 2) if (x + i >= 0 && x + i < N) out.push_back({x + i, y});
}
void vertical(int x, int y, std::vector<std::pair<int, int>>& out) {
  for (int i = -1; i <= 1; i += 2) if (y + i >= 0 && y + i < N) out.push_back({x, y + i});
}

}  // namespace

std::vector<std::pair<int, int>> CrystalSphereGame::toolCells(Tool t, int x, int y) {
  std::vector<std::pair<int, int>> out;
  if (t != Tool::Big) { out.push_back({x, y}); return out; }
  // GetAdjacentCells: horizontal, vertical, diagonal, then the cell itself.
  horizontal(x, y, out);
  vertical(x, y, out);
  for (int i = -1; i <= 1; i += 2)
    for (int j = -1; j <= 1; j += 2)
      if (inGrid(x + i, y + j)) out.push_back({x + i, y + j});
  out.push_back({x, y});
  return out;
}

namespace {

// CrystalSphere.cs: pay 51-99 gold for 3 divinations, or take a Debt for 6. Acts 2-3, 100+ gold.
struct CrystalSphere : Event {
  EVENT_HEADER(CrystalSphere, "CRYSTAL_SPHERE")
  std::unique_ptr<Game> game;
  std::unique_ptr<Rng> banterRng;  // Rng.Chaotic stand-in (cosmetic)

  bool isAllowed(Run& r) override { return r.gold >= 100 && r.actIndex > 0; }
  void calculateVars() override {
    addVar("UncoverFutureCost", 50);
    addVar("UncoverFutureProphesizeCount", 3);
    addVar("PaymentPlanCount", 6);
    setStr("CurseTitle", "cards.DEBT.title");
    setVar("UncoverFutureCost", val("UncoverFutureCost") + Dec(rng().nextInt(1, 50)));
  }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "UNCOVER_FUTURE", [this] { return uncoverFuture(); }),
            option("INITIAL", "PAYMENT_PLAN", [this] { return paymentPlan(); })};
  }

  Task<> uncoverFuture() {
    run->gold = std::max(0, run->gold - val("UncoverFutureCost").toInt());  // PlayerCmd.LoseGold(Spent)
    co_await playMinigame(3);
    setFinished("FINISH");
  }
  Task<> paymentPlan() {
    run->addCardToDeck(db::card("Debt"));  // CardPileCmd.AddCurseToDeck<Debt>
    co_await playMinigame(6);
    setFinished("FINISH");
  }

  // ---------------------------------------------------------------- CrystalSphereMinigame

  void say(const char* group, int count) {  // NCrystalSphereDialogue.Play(Rng.Chaotic.NextItem(lines))
    if (!banterRng) banterRng = std::make_unique<Rng>(run->seed, "CrystalSphereBanter");
    game->banter = locKey + ".banter." + group + "." + std::to_string(1 + banterRng->nextInt(count));
    ++game->banterSerial;
  }

  // The constructor: fog everywhere but the four corners (grown twice along the rows and columns,
  // the part of the square outside the sphere), then the items, up to 10 tries.
  void setUp(int divinationCount) {
    game = std::make_unique<Game>();
    for (int x = 0; x < N; ++x)
      for (int y = 0; y < N; ++y) { game->hidden[x][y] = true; game->itemAt[x][y] = -1; }
    std::vector<std::pair<int, int>> list = {{0, 0}, {N - 1, 0}, {N - 1, N - 1}, {0, N - 1}};
    for (int k = 0; k < 2; ++k) {
      std::vector<std::pair<int, int>> next = list;
      for (auto [x, y] : list) horizontal(x, y, next);
      for (auto [x, y] : list) vertical(x, y, next);
      list = std::move(next);
    }
    for (auto [x, y] : list) game->hidden[x][y] = false;  // ClearCell: no items yet
    int tries = 0;
    do {
      game->placedAllItems = populateItems();
      ++tries;
    } while (!game->placedAllItems && tries < 10);
    game->divinations = divinationCount;
    game->tool = Game::Tool::Big;
  }

  // CanPlaceHere + PlaceItem: every fitting top-left cell (x outer, y inner), one picked with the rng.
  bool placeItem(Item& it, int index) {
    std::vector<std::pair<int, int>> spots;
    for (int i = 0; i < N; ++i)
      for (int j = 0; j < N; ++j) {
        bool ok = true;
        for (int a = 0; a < it.w && ok; ++a)
          for (int b = 0; b < it.h && ok; ++b) {
            int x = i + a, y = j + b;
            ok = inGrid(x, y) && game->hidden[x][y] && game->itemAt[x][y] < 0;
          }
        if (ok) spots.push_back({i, j});
      }
    if (spots.empty()) return false;
    auto pos = rng().nextItem(spots);
    it.x = pos.first;
    it.y = pos.second;
    it.placed = true;
    for (int a = 0; a < it.w; ++a)
      for (int b = 0; b < it.h; ++b) game->itemAt[it.x + a][it.y + b] = index;
    return true;
  }

  // PopulateItems. PORT NOTE (faithful quirk): a failed try keeps the items already placed and
  // listed, and every try hooks Revealed again on the whole list, so an item from an earlier try
  // would be rewarded once per hook. Placement practically never fails on the 11x11 grid.
  bool populateItems() {
    bool flag = true;
    auto add = [&](Item it) {
      int index = (int)game->items.size();
      if (flag) flag = placeItem(it, index);  // flag = flag && item.PlaceItem(this)
      game->items.push_back(it);
    };
    auto make = [](ItemType t, int w, int h) { Item it; it.type = t; it.w = w; it.h = h; return it; };
    add(make(ItemType::Relic, 4, 4));
    for (int i = 0; i < 2; ++i) {
      Item p = make(ItemType::Potion, 1, 3);
      p.potionRarity = PotionRarity::Common;
      add(p);
    }
    Item rare = make(ItemType::Potion, 2, 2);
    rare.potionRarity = PotionRarity::Rare;
    add(rare);
    for (Rarity r : {Rarity::Common, Rarity::Uncommon, Rarity::Rare}) {
      Item c = make(ItemType::CardReward, 2, 2);
      c.cardRarity = r;
      add(c);
    }
    add(make(ItemType::Curse, 2, 2));
    for (int i = 0; i < 5; ++i) add(make(ItemType::Gold, 1, 1));
    for (int i = 0; i < 2; ++i) {
      Item g = make(ItemType::Gold, 2, 1);
      g.bigGold = true;
      add(g);
    }
    for (auto& it : game->items) ++it.subscriptions;
    return flag;
  }

  Task<> clearCell(int x, int y) {
    if (!inGrid(x, y) || !game->hidden[x][y]) co_return;
    game->hidden[x][y] = false;
    int idx = game->itemAt[x][y];
    if (idx < 0) co_return;
    Item& it = game->items[idx];
    for (int a = 0; a < it.w; ++a)
      for (int b = 0; b < it.h; ++b)
        if (game->hidden[it.x + a][it.y + b]) co_return;  // AreAllOccupiedCellsClear
    co_await revealItem(idx);
  }

  // CrystalSphereItem.RevealItem: Revealed (the minigame's list, the screen's dialogue); the curse
  // then adds a Doubt to the deck right away.
  Task<> revealItem(int idx) {
    Item& it = game->items[idx];
    it.revealed = true;
    for (int k = 0; k < it.subscriptions; ++k) game->revealed.push_back(idx);
    if (it.isGood()) say("REVEAL_GOOD", 5);
    else say("REVEAL_BAD", 3);
    if (it.type == ItemType::Curse) run->addCardToDeck(db::card("Doubt"));  // AddCurseToDeck<Doubt>
    co_return;
  }

  Task<> cellClicked(int x, int y) {
    game->divinations--;
    for (auto [cx, cy] : Game::toolCells(game->tool, x, y)) co_await clearCell(cx, cy);
  }

  Task<> playMinigame(int divinationCount) {
    setUp(divinationCount);
    say("START", 2);
    run->screen = Screen::Event;
    // NCrystalSphereScreen.OnCellClicked: only fogged cells take clicks, while divinations remain.
    while (game->divinations > 0) {
      int c = co_await game->cellChoice.next();
      int x = c % N, y = c / N;
      if (c < 0 || c >= N * N || !game->hidden[x][y]) continue;
      co_await cellClicked(x, y);
    }
    co_await completeMinigame();
    game->phase = Game::Phase::Done;
    say("END", 2);  // OnMinigameFinished: the proceed button (the page's own, here) comes up
    run->screen = Screen::Event;
  }

  // CompleteMinigame -> DoLocalCrystalSphereRewards -> OfferCrystalSphereRewards: ToReward for each
  // revealed item in order (a potion is picked right there), then RewardsSet.Populate in the same
  // order (all with the event's rng), then sorted by RewardsSetIndex.
  Task<> completeMinigame() {
    game->phase = Game::Phase::Rewards;
    struct Pending { int setIndex; Run::RewardItem item; const Item* src; };
    std::vector<Pending> rewards;
    for (int idx : game->revealed) {
      const Item& it = game->items[idx];
      Pending p{0, {}, &it};
      switch (it.type) {
        case ItemType::Curse: continue;  // no reward
        case ItemType::Gold: p.setIndex = 1; p.item.kind = Run::RewardKind::Gold; break;
        case ItemType::Relic: p.setIndex = 3; p.item.kind = Run::RewardKind::Relic; break;
        case ItemType::CardReward: p.setIndex = 5; p.item.kind = Run::RewardKind::Card; break;
        case ItemType::Potion: {
          // PotionFactory.GetPotionOptions(owner) where Rarity == _rarity, rng.NextItem.
          p.setIndex = 2;
          p.item.kind = Run::RewardKind::Potion;
          std::vector<std::string> ids;
          for (auto& pid : db::potionPool(run->characterId)) {
            auto pm = db::potion(pid);
            if (pm && pm->rarity == it.potionRarity) ids.push_back(pid);
          }
          if (ids.empty()) continue;
          p.item.potion = db::potion(rng().nextItem(ids));
          p.item.potion->run = run;
          break;
        }
      }
      rewards.push_back(std::move(p));
    }
    for (auto& p : rewards) {  // Populate
      const Item& it = *p.src;
      switch (it.type) {
        case ItemType::Gold: {
          int amount = it.bigGold ? 30 : 10;
          p.item.gold = rng().nextInt(amount, amount + 1);  // GoldReward(amount).Populate: NextInt(min, max + 1)
          break;
        }
        case ItemType::Relic:  // RelicReward(owner).SetRng(rng): RollRarity(rng), PullNextRelicFromFront
          p.item.relic = run->pullRelicFromFront(run->relicBag, run->rollRelicRarity(rng()));
          p.item.relic->run = run;
          break;
        case ItemType::CardReward:
          p.item = cardReward(it.cardRarity);
          break;
        default: break;
      }
    }
    std::stable_sort(rewards.begin(), rewards.end(), [](const Pending& a, const Pending& b) { return a.setIndex < b.setIndex; });
    std::vector<Run::RewardItem> rows;
    for (auto& p : rewards) rows.push_back(std::move(p.item));
    // RewardsSet.Offer: an empty set outside combat shows nothing.
    if (!rows.empty()) co_await run->offerRewards(std::move(rows));
  }

  // CardReward(CardCreationOptions(character pool, Other, Uniform, rarity == r).WithRngOverride(rng), 3):
  // CardFactory.CreateForReward with the event rng for the picks and the upgrade rolls.
  Run::RewardItem cardReward(Rarity r) {
    CardCreationOptions o;
    o.pools = {run->characterId};
    o.odds = RarityOdds::Uniform;
    o.filter = [r](const Card& c) { return c.rarity == r; };
    o.rng = &rng();
    return run->makeCardReward(o, 3);
  }
};

}  // namespace

CrystalSphereGame* crystalSphereGame(Run& r) {
  Event* e = r.currentEvent.get();
  if (!e || e->id != CrystalSphere::kId) return nullptr;
  return static_cast<CrystalSphere*>(e)->game.get();
}

void registerCrystalSphere() {
  db::registerEvent(CrystalSphere::kId, [] { return std::unique_ptr<Event>(new CrystalSphere()); });
}

}  // namespace sts
