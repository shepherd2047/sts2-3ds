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
}  // namespace

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

  weakQueue = db::act1Weak();
  normalQueue = db::act1Normal();
  rng("Encounters").shuffle(weakQueue);
  rng("Encounters").shuffle(normalQueue);
  bossId = rng("Encounters").nextItem(db::act1Bosses());
  generateMap();
  currentNode = -1;
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

  // Room types. Row 0 fights, the last row rests before the boss.
  for (auto& n : nodes) {
    if (n.row == 0) { n.type = RoomType::Monster; continue; }
    if (n.row == kRows - 1) { n.type = RoomType::Rest; continue; }
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

std::vector<int> Run::reachableNodes() const {
  std::vector<int> out;
  if (currentNode < 0) {
    for (int i = 0; i < (int)nodes.size(); ++i) if (nodes[i].row == 0) out.push_back(i);
  } else {
    out = nodes[currentNode].next;
  }
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

    if (type == RoomType::Monster || type == RoomType::Elite || type == RoomType::Boss) {
      std::string id;
      if (type == RoomType::Boss) id = bossId;
      else if (type == RoomType::Elite) id = rng("Encounters").nextItem(db::act1Elites());
      else if (fightsDone < kWeakFights) id = weakQueue[fightsDone % weakQueue.size()];
      else id = normalQueue[(fightsDone - kWeakFights) % normalQueue.size()];
      if (type == RoomType::Monster) ++fightsDone;
      // Debug: STS_ENCOUNTER=<EncounterId> makes the first fight that encounter.
      if (const char* forced = getenv("STS_ENCOUNTER"); forced && floor == 1 && db::encounter(forced)) id = forced;

      bool won = co_await fight(id);
      if (!won) { screen = Screen::GameOver; co_return; }
      if (type == RoomType::Boss) { screen = Screen::Victory; co_return; }

      // Rewards: gold and a pick of three cards.
      Rng& rr = rng("Rewards");
      gold += type == RoomType::Elite ? rr.nextInt(25, 36) : rr.nextInt(10, 21);
      rewardCards = cardReward(type, 3);
      screen = Screen::Reward;
      int pick = co_await rewardChoice.next();
      if (pick >= 0 && pick < (int)rewardCards.size()) deck.push_back(std::move(rewardCards[pick]));
      rewardCards.clear();
      combat.reset();
    } else if (type == RoomType::Rest) {
      for (;;) {
        screen = Screen::Rest;
        int opt = co_await restChoice.next();
        if (opt == 0) {
          // HealRestSiteOption: 30% of max HP.
          Dec amount = Dec(player->maxHp) * Dec::lit(0.3);
          int before = player->hp;
          player->hp = std::min(player->maxHp, player->hp + amount.toInt());
          lastHeal = player->hp - before;
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
