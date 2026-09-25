// Run flow: map, room dispatch, rewards and rest sites.
#include <algorithm>
#include <cstdlib>
#include <set>

#include "game.h"

namespace sts {

namespace {
constexpr int kCols = 7;
constexpr int kRows = 15;  // Overgrowth.BaseNumberOfRooms
constexpr int kPaths = 6;
constexpr int kWeakFights = 3;  // Overgrowth.NumberOfWeakEncounters
// GetRowCount() - 7 in StandardActMap, whose row 0 is the start point: row 8 here.
constexpr int kTreasureRow = kRows - 7;
}  // namespace

Creature* Relic::owner() const { return run->player.get(); }

std::vector<Model*> Run::listeners() {
  std::vector<Model*> out;
  for (auto& r : relics) out.push_back(r.get());
  return out;
}

bool Run::hasRelic(const std::string& id) const {
  for (auto& r : relics) if (r->id == id) return true;
  return false;
}

// RunManager: the shared bag (shared pool) then the player's bag (shared + Ironclad
// pools), both from the UpFront stream; each rarity deque is shuffled once.
// PORT NOTE: C# shuffles the deques in dictionary insertion order; here in rarity order.
void Run::populateRelicBags() {
  relicBag.clear();
  sharedRelicBag.clear();
  Rng& r = rng("UpFront");
  auto fill = [&](std::map<RelicRarity, std::vector<std::string>>& bag, std::vector<std::string> ids) {
    for (auto& id : ids) {
      auto rel = db::relic(id);
      if (!rel) continue;  // not ported yet
      RelicRarity k = rel->rarity;
      if (k == RelicRarity::Common || k == RelicRarity::Uncommon || k == RelicRarity::Rare || k == RelicRarity::Shop)
        bag[k].push_back(id);
    }
    for (auto& [k, v] : bag) r.shuffle(v);
  };
  fill(sharedRelicBag, db::sharedRelicPool());
  std::vector<std::string> all = db::sharedRelicPool();
  for (auto& id : db::ironcladRelicPool()) all.push_back(id);
  fill(relicBag, all);
  for (auto& rel : relics) {  // owned relics never drop again
    for (auto& [k, v] : relicBag) v.erase(std::remove(v.begin(), v.end(), rel->id), v.end());
    for (auto& [k, v] : sharedRelicBag) v.erase(std::remove(v.begin(), v.end(), rel->id), v.end());
  }
}

RelicRarity Run::rollRelicRarity(Rng& rr) {
  float f = rr.nextFloat();
  return f < 0.5f ? RelicRarity::Common : f < 0.83f ? RelicRarity::Uncommon : RelicRarity::Rare;
}

// RelicGrabBag.PullFromFront: an empty rarity falls through Shop -> Common ->
// Uncommon -> Rare, then RelicFactory.FallbackRelic (Circlet).
std::unique_ptr<Relic> Run::pullRelicFromFront(std::map<RelicRarity, std::vector<std::string>>& bag, RelicRarity k) {
  while (k != RelicRarity::None) {
    auto& v = bag[k];
    if (!v.empty()) {
      std::string id = v.front();
      for (auto& [kk, vv] : relicBag) vv.erase(std::remove(vv.begin(), vv.end(), id), vv.end());
      for (auto& [kk, vv] : sharedRelicBag) vv.erase(std::remove(vv.begin(), vv.end(), id), vv.end());
      return db::relic(id);
    }
    k = k == RelicRarity::Shop ? RelicRarity::Common
      : k == RelicRarity::Common ? RelicRarity::Uncommon
      : k == RelicRarity::Uncommon ? RelicRarity::Rare : RelicRarity::None;
  }
  return db::relic("Circlet");
}

Task<> Run::obtainRelic(std::unique_ptr<Relic> rel) {
  if (!rel) co_return;
  rel->run = this;
  rel->combat = combat && combat->inProgress ? combat.get() : nullptr;
  Relic* raw = rel.get();
  for (auto& [k, v] : relicBag) v.erase(std::remove(v.begin(), v.end(), raw->id), v.end());
  for (auto& [k, v] : sharedRelicBag) v.erase(std::remove(v.begin(), v.end(), raw->id), v.end());
  relics.push_back(std::move(rel));
  raw->doFlash();
  co_await raw->afterObtained();
}

Task<> Run::offerRelic(std::unique_ptr<Relic> rel, bool fromChest) {
  if (!rel) co_return;
  rel->run = this;
  relicOffer = std::move(rel);
  relicOfferFromChest = fromChest;
  screen = Screen::RelicOffer;
  int take = co_await relicChoice.next();
  if (take == 1) co_await obtainRelic(std::move(relicOffer));
  relicOffer.reset();
}

Task<> Run::gainGold(int amount) {
  Dec a = amount;
  for (Model* m : listeners()) a = m->modifyGoldGained(a);
  int n = std::max(0, a.toInt());
  gold += n;
  for (Model* m : listeners()) co_await m->afterGoldGained(n);
}

void Run::start(uint64_t s) {
  db::init();
  seed = s;
  rngs.clear();
  player = std::make_unique<Creature>();
  player->isPlayer = true;
  player->side = Side::Player;
  player->name = "IRONCLAD";
  player->hp = player->maxHp = 80;  // Ironclad.StartingHp
  gold = 99;
  floor = 0;
  deck.clear();
  for (auto& id : db::ironcladStarterDeck()) deck.push_back(db::card(id));
  relics.clear();
  auto bb = db::relic("BurningBlood");
  bb->run = this;
  relics.push_back(std::move(bb));
  populateRelicBags();

  weakQueue = db::act1Weak();
  normalQueue = db::act1Normal();
  rng("Encounters").shuffle(weakQueue);
  rng("Encounters").shuffle(normalQueue);
  bossId = rng("Encounters").nextItem(db::act1Bosses());
  generateMap();
  currentNode = -1;
  freeMap = getenv("STS_PATH_ONLY") == nullptr;
}

void Run::generateMap() {
  nodes.clear();
  Rng& r = rng("Map");
  int grid[kRows][kCols];
  for (auto& row : grid) for (int& c : row) c = -1;
  auto nodeAt = [&](int row, int col) {
    if (grid[row][col] < 0) {
      MapNode n;
      n.row = row;
      n.col = col;
      grid[row][col] = (int)nodes.size();
      nodes.push_back(n);
    }
    return grid[row][col];
  };

  int firstStart = -1;
  for (int p = 0; p < kPaths; ++p) {
    int col = r.nextInt(kCols);
    if (p == 1) while (col == firstStart) col = r.nextInt(kCols);
    if (p == 0) firstStart = col;
    int prev = nodeAt(0, col);
    for (int row = 1; row < kRows; ++row) {
      int nc = std::clamp(col + r.nextInt(-1, 2), 0, kCols - 1);
      int cur = nodeAt(row, nc);
      auto& nx = nodes[prev].next;
      if (std::find(nx.begin(), nx.end(), cur) == nx.end()) nx.push_back(cur);
      prev = cur;
      col = nc;
    }
  }

  // Room types. Row 0 fights, the last row rests before the boss, and
  // StandardActMap.AssignPointTypes makes the 7th row from the top all treasure.
  for (auto& n : nodes) {
    if (n.row == 0) { n.type = RoomType::Monster; continue; }
    if (n.row == kRows - 1) { n.type = RoomType::Rest; continue; }
    if (n.row == kTreasureRow) { n.type = RoomType::Treasure; continue; }
    float roll = r.nextFloat();
    if (n.row >= 5 && roll < 0.14f) n.type = RoomType::Elite;
    else if (n.row >= 5 && n.row != kRows - 2 && roll < 0.30f) n.type = RoomType::Rest;
    else n.type = RoomType::Monster;
  }

  // Boss node above everything.
  MapNode boss;
  boss.row = kRows;
  boss.col = kCols / 2;
  boss.type = RoomType::Boss;
  int bossIdx = (int)nodes.size();
  nodes.push_back(boss);
  for (auto& n : nodes) if (n.row == kRows - 1) n.next.push_back(bossIdx);

  for (auto& n : nodes) {
    n.x = (float)n.col + (n.type == RoomType::Boss ? 0.f : (r.nextFloat() - 0.5f) * 0.35f);
    n.y = (float)n.row;
  }
}

std::vector<int> Run::pathNodes() const {
  std::vector<int> out;
  if (currentNode < 0) {
    for (int i = 0; i < (int)nodes.size(); ++i) if (nodes[i].row == 0) out.push_back(i);
  } else {
    out = nodes[currentNode].next;
  }
  return out;
}

std::vector<int> Run::reachableNodes() const {
  std::vector<int> out = pathNodes();
  if (!freeMap) return out;
  for (int i = 0; i < (int)nodes.size(); ++i)
    if (std::find(out.begin(), out.end(), i) == out.end()) out.push_back(i);
  return out;
}

// CardRarityOdds.Roll (non-ascension values).
Rarity Run::rollRarity(RoomType room) {
  float rare, uncommon;
  float offset = rarityOffset;
  if (room == RoomType::Boss) { rare = 1.f; uncommon = 0.f; offset = 0.f; }
  else if (room == RoomType::Elite) { rare = 0.1f; uncommon = 0.4f; }
  else { rare = 0.03f; uncommon = 0.37f; }
  float r = rng("Rewards").nextFloat();
  float rareOdds = rare + offset;
  Rarity result = r < rareOdds ? Rarity::Rare : r < uncommon + rareOdds ? Rarity::Uncommon : Rarity::Common;
  if (result == Rarity::Rare) rarityOffset = -0.05f;
  else rarityOffset = std::min(rarityOffset + 0.01f, 0.4f);
  return result;
}

// CardFactory.CreateForReward: roll a rarity per card, no duplicates.
std::vector<std::unique_ptr<Card>> Run::cardReward(RoomType room, int count) {
  std::vector<std::unique_ptr<Card>> out;
  std::vector<std::string> taken;
  for (int i = 0; i < count; ++i) {
    Rarity want = rollRarity(room);
    auto pool = db::ironcladCards([&](const Card& c) { return c.rarity == want; });
    pool.erase(std::remove_if(pool.begin(), pool.end(), [&](const std::string& id) {
      return std::find(taken.begin(), taken.end(), id) != taken.end();
    }), pool.end());
    if (pool.empty()) pool = db::ironcladCards([&](const Card& c) {
      return (c.rarity == Rarity::Common || c.rarity == Rarity::Uncommon || c.rarity == Rarity::Rare) &&
             std::find(taken.begin(), taken.end(), c.id) == taken.end();
    });
    if (pool.empty()) break;
    std::string id = rng("Rewards").nextItem(pool);
    taken.push_back(id);
    out.push_back(db::card(id));
  }
  return out;
}

Task<bool> Run::fight(const std::string& encounterId) {
  const Encounter* enc = db::encounter(encounterId);
  combat = std::make_unique<Combat>();
  Combat& c = *combat;
  c.run = this;
  c.encounterId = encounterId;
  c.isBoss = enc->room == RoomType::Boss;
  c.isElite = enc->room == RoomType::Elite;
  c.player = player.get();
  player->combat = &c;
  player->block = 0;
  player->powers.clear();
  for (auto& rel : relics) rel->combat = &c;

  // Deck -> draw pile, shuffled.
  for (auto& card : deck) c.draw.push_back(c.addCard(card->clone()));
  std::stable_sort(c.draw.begin(), c.draw.end(), [](Card* a, Card* b) { return a->id < b->id; });
  rng("Shuffle").shuffle(c.draw);

  for (auto& m : enc->generate(rng("Encounters"))) c.createEnemy(std::move(m));

  screen = Screen::Combat;
  // CombatRoom.EnterInternal: Hook.AfterRoomEntered once the fight is set up.
  for (Model* m : c.listeners()) co_await m->afterRoomEntered(enc->room);
  co_await c.runCombat();
  bool won = c.won && player->alive();
  co_await wait(won ? 0.8 : 1.2);
  player->block = 0;
  player->powers.clear();
  co_return won;
}

Task<> Run::main() {
  int fightsDone = 0;
  for (;;) {
    screen = Screen::Map;
    int choice = co_await mapChoice.next();
    auto reach = reachableNodes();
    if (std::find(reach.begin(), reach.end(), choice) == reach.end()) continue;
    currentNode = choice;
    nodes[choice].visited = true;
    ++floor;
    RoomType type = nodes[choice].type;
    // Debug: STS_ROOM=Treasure|Rest|Elite|Boss turns the first room into that type.
    if (const char* forced = getenv("STS_ROOM"); forced && floor == 1) {
      std::string f = forced;
      type = f == "Treasure" ? RoomType::Treasure : f == "Rest" ? RoomType::Rest : f == "Elite" ? RoomType::Elite
           : f == "Boss" ? RoomType::Boss : type;
    }

    if (type == RoomType::Monster || type == RoomType::Elite || type == RoomType::Boss) {
      std::string id;
      if (type == RoomType::Boss) id = bossId;
      else if (type == RoomType::Elite) id = rng("Encounters").nextItem(db::act1Elites());
      else if (fightsDone < kWeakFights) id = weakQueue[fightsDone % weakQueue.size()];
      else id = normalQueue[(fightsDone - kWeakFights) % normalQueue.size()];
      if (type == RoomType::Monster) ++fightsDone;
      // Debug: STS_ENCOUNTER=<EncounterId> makes the first fight that encounter.
      if (const char* forced = getenv("STS_ENCOUNTER"); forced && floor == 1 && db::encounter(forced)) id = forced;
      if (!devNextEncounter.empty() && db::encounter(devNextEncounter)) { id = devNextEncounter; devNextEncounter.clear(); }

      bool won = co_await fight(id);
      if (!won) { screen = Screen::GameOver; co_return; }
      if (type == RoomType::Boss) { screen = Screen::Victory; co_return; }

      // Rewards (RewardsSet): gold, then for elites a relic, then a pick of three cards.
      Rng& rr = rng("Rewards");
      co_await gainGold(type == RoomType::Elite ? rr.nextInt(25, 36) : rr.nextInt(10, 21));
      combat.reset();
      for (auto& rel : relics) rel->combat = nullptr;
      if (type == RoomType::Elite) co_await offerRelic(pullRelicFromFront(relicBag, rollRelicRarity(rr)), false);
      rewardCards = cardReward(type, 3);
      screen = Screen::Reward;
      int pick = co_await rewardChoice.next();
      if (pick >= 0 && pick < (int)rewardCards.size()) deck.push_back(std::move(rewardCards[pick]));
      rewardCards.clear();
    } else if (type == RoomType::Treasure) {
      // TreasureRoom: 42-52 gold, then one relic from the shared bag.
      for (Model* m : listeners()) co_await m->afterRoomEntered(type);
      co_await gainGold(rng("Rewards").nextInt(42, 53));
      co_await offerRelic(pullRelicFromFront(sharedRelicBag, rollRelicRarity(rng("TreasureRoomRelics"))), true);
    } else if (type == RoomType::Rest) {
      for (Model* m : listeners()) co_await m->afterRoomEntered(type);
      for (;;) {
        screen = Screen::Rest;
        int opt = co_await restChoice.next();
        if (opt == 0) {
          // HealRestSiteOption: 30% of max HP, through Hook.ModifyRestSiteHealAmount.
          Dec amount = Dec(player->maxHp) * Dec::lit(0.3);
          for (Model* m : listeners()) amount = m->modifyRestSiteHealAmount(player.get(), amount);
          int before = player->hp;
          player->hp = std::min(player->maxHp, player->hp + amount.toInt());
          lastHeal = player->hp - before;
          for (Model* m : listeners()) co_await m->afterRestSiteHeal();
          co_await wait(0.6);
          break;
        }
        upgradeOptions.clear();
        for (auto& card : deck) if (card->upgradable()) upgradeOptions.push_back(card.get());
        screen = Screen::RestUpgrade;
        int idx = co_await upgradeChoice.next();
        if (idx >= 0 && idx < (int)upgradeOptions.size()) {
          upgradeOptions[idx]->upgrade();
          co_await wait(0.4);
          break;
        }
      }
    }
  }
}

}  // namespace sts
