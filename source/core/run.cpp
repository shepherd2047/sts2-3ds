// Run flow: map, room dispatch, rewards and rest sites.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

#include "game.h"

namespace sts {

namespace {
// Registered ids of a list, in order.
std::vector<std::string> registered(const std::vector<std::string>& ids) {
  std::vector<std::string> out;
  for (auto& id : ids) if (db::encounter(id)) out.push_back(id);
  return out;
}
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

// ---------------------------------------------------------------- events

Creature* Event::owner() { return run->player.get(); }

// ActModel.PullNextEvent + RoomSet.EnsureNextEventIsValid: the next allowed event not
// seen this run; when all are used up, repeats are allowed.
std::unique_ptr<Event> Run::pullNextEvent() {
  for (int pass = 0; pass < 2; ++pass) {
    for (size_t i = 0; i < eventQueue.size(); ++i) {
      const std::string& id = eventQueue[i];
      bool seen = std::find(visitedEvents.begin(), visitedEvents.end(), id) != visitedEvents.end();
      if (pass == 0 && seen) continue;
      auto e = db::event(id);
      if (!e || !e->isAllowed(*this)) continue;
      eventQueue.erase(eventQueue.begin() + (long)i);
      return e;
    }
  }
  return nullptr;
}

Task<> Run::runEvent(std::unique_ptr<Event> e) {
  e->run = this;
  e->rngPtr = std::make_unique<Rng>(seed, e->id);  // EventModel.Rng: seed + hash(id)
  e->calculateVars();
  e->descKey = e->page("INITIAL") + ".description";
  e->options = e->initialOptions();
  visitedEvents.push_back(e->id);
  currentEvent = std::move(e);
  Event* ev = currentEvent.get();
  for (Model* m : listeners()) co_await m->afterRoomEntered(RoomType::Unknown);
  for (;;) {
    screen = Screen::Event;
    int pick = co_await eventChoice.next();
    if (ev->finished || died) break;
    if (pick < 0 || pick >= (int)ev->options.size() || ev->options[pick].locked()) continue;
    auto action = ev->options[pick].action;  // the action may replace the options
    co_await action();
    if (died) break;
  }
  currentEvent.reset();
}

Task<std::vector<Card*>> Run::selectFromDeck(std::string prompt, std::function<bool(Card*)> filter, int count,
                                             bool canCancel, bool showUpgrade) {
  std::vector<Card*> opts;
  for (auto& c : deck) if (!filter || filter(c.get())) opts.push_back(c.get());
  if (opts.empty()) co_return std::vector<Card*>{};
  deckChoice.prompt = std::move(prompt);
  deckChoice.options = std::move(opts);
  deckChoice.count = count;
  deckChoice.canCancel = canCancel;
  deckChoice.showUpgrade = showUpgrade;
  deckChoice.active = true;
  auto picked = co_await deckChoice.result.next();
  deckChoice.active = false;
  co_return picked;
}

Card* Run::addCardToDeck(std::unique_ptr<Card> c) {
  if (!c) return nullptr;
  deck.push_back(std::move(c));
  return deck.back().get();
}

void Run::removeCardFromDeck(Card* c) {
  deck.erase(std::remove_if(deck.begin(), deck.end(), [&](const std::unique_ptr<Card>& d) { return d.get() == c; }), deck.end());
}

Card* Run::transformCard(Card* c, std::unique_ptr<Card> into) {
  if (!into) return c;
  for (auto& d : deck)
    if (d.get() == c) { d = std::move(into); return d.get(); }
  return addCardToDeck(std::move(into));
}

// CardFactory transform: a random card of the character's pool (Common/Uncommon/Rare),
// never the card itself. PORT NOTE: C# also weights by rarity odds; this picks uniformly.
std::unique_ptr<Card> Run::randomTransformFor(Card* c, Rng& rr) {
  auto pool = db::ironcladCards([&](const Card& x) {
    return x.id != c->id && (x.rarity == Rarity::Common || x.rarity == Rarity::Uncommon || x.rarity == Rarity::Rare);
  });
  if (pool.empty()) return nullptr;
  return db::card(rr.nextItem(pool));
}

Task<> Run::loseHp(int amount) {
  if (devGod || amount <= 0) co_return;
  player->hp = std::max(0, player->hp - amount);
  if (player->hp == 0) died = true;
  co_await wait(0.3);
}

Task<> Run::gainMaxHp(int amount) {
  player->maxHp += amount;
  player->hp += amount;
  co_await wait(0.2);
}

Task<> Run::loseMaxHp(int amount) {
  player->maxHp = std::max(1, player->maxHp - amount);
  player->hp = std::min(player->hp, player->maxHp);
  co_await wait(0.2);
}

// EventModel.EnterCombatWithoutExitingEvent: fight, take the room's rewards, return.
Task<bool> Run::eventFight(const std::string& encounterId) {
  const Encounter* enc = db::encounter(encounterId);
  if (!enc) co_return true;
  bool won = co_await fight(encounterId);
  if (!won) { died = true; co_return false; }
  Rng& rr = rng("Rewards");
  co_await gainGold(enc->room == RoomType::Elite ? rr.nextInt(35, 46) : rr.nextInt(10, 21));
  combat.reset();
  for (auto& rel : relics) rel->combat = nullptr;
  if (enc->room == RoomType::Elite) co_await offerRelic(pullRelicFromFront(relicBag, rollRelicRarity(rr)), false);
  rewardCards = cardReward(enc->room, 3);
  screen = Screen::Reward;
  int pick = co_await rewardChoice.next();
  if (pick >= 0 && pick < (int)rewardCards.size()) deck.push_back(std::move(rewardCards[pick]));
  rewardCards.clear();
  co_return true;
}

// UnknownMapPointOdds.Roll (single player, no blacklist): Monster 10%, Treasure 2%,
// Shop 3%, else Event; the rolled type resets to its base odds, the others grow by theirs.
RoomType Run::rollUnknownRoom() {
  float roll = rng("UnknownMapPoint").nextFloat();
  RoomType result = RoomType::Unknown;  // event
  float sum = 0;
  const std::pair<RoomType, float*> odds[] = {{RoomType::Monster, &unknownMonsterOdds},
                                              {RoomType::Treasure, &unknownTreasureOdds},
                                              {RoomType::Shop, &unknownShopOdds}};
  for (auto& [t, p] : odds) {
    sum += *p;
    if (roll <= sum) { result = t; break; }
  }
  const float base[] = {0.1f, 0.02f, 0.03f};
  for (int i = 0; i < 3; ++i) *odds[i].second = odds[i].first == result ? base[i] : *odds[i].second + base[i];
  return result;
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
  visitedEvents.clear();
  died = false;
  freeMap = getenv("STS_PATH_ONLY") == nullptr;
  // Debug: STS_ACT=2|3 starts the run in that act.
  const char* startAct = getenv("STS_ACT");
  enterAct(startAct ? std::atoi(startAct) - 1 : 0);
}

const db::ActDef& Run::act() const { return db::acts()[actIndex]; }

// RunManager.EnterAct + ActModel.GenerateRooms for one act. PORT NOTE: C# generates every
// act's rooms up front from Rng.UpFront; here each act draws from the Encounters/Events
// streams when it is entered. Encounter lists fall back when an act's pool isn't ported
// yet (elites -> normal fights, boss -> elites -> normal fights) so a run can always go on.
void Run::enterAct(int index) {
  actIndex = std::clamp(index, 0, kActs - 1);
  const db::ActDef& a = act();
  eventQueue.clear();
  for (auto& id : a.events) if (db::event(id)) eventQueue.push_back(id);
  for (auto& id : db::sharedEvents()) if (db::event(id)) eventQueue.push_back(id);
  rng("Events").shuffle(eventQueue);

  weakQueue = registered(a.weak);
  normalQueue = registered(a.normal);
  eliteQueue = registered(a.elites);
  std::vector<std::string> bosses = registered(a.bosses);
  if (weakQueue.empty()) weakQueue = normalQueue;
  if (normalQueue.empty()) normalQueue = weakQueue;
  if (normalQueue.empty()) weakQueue = normalQueue = registered(db::acts()[0].normal);
  if (eliteQueue.empty()) eliteQueue = normalQueue;
  if (bosses.empty()) bosses = registered(a.elites);
  if (bosses.empty()) bosses = normalQueue;
  Rng& er = rng("Encounters");
  er.shuffle(weakQueue);
  er.shuffle(normalQueue);
  er.shuffle(eliteQueue);
  bossId = er.nextItem(bosses);
  fightsThisAct = 0;
  // The act's Ancient (ActModel.GenerateRooms: act 1 is always Neow). PORT NOTE: the
  // Ancients of Hive and Glory are not ported yet (package 11b). Debug starts
  // (STS_ENCOUNTER / STS_ROOM / STS_EVENT / STS_NO_NEOW) skip it so scripts reach the map.
  ancientId.clear();
  bool debugStart = getenv("STS_ENCOUNTER") || getenv("STS_ROOM") || getenv("STS_EVENT") || getenv("STS_NO_NEOW");
  if (actIndex == 0 && !debugStart && db::event("Neow")) ancientId = "Neow";
  // SetActInternal: UnknownMapPointOdds.ResetToBase.
  unknownMonsterOdds = 0.1f;
  unknownTreasureOdds = 0.02f;
  unknownShopOdds = 0.03f;
  generateMap();
  currentNode = 0;  // the starting point (the Ancient's node)
  nodes[0].visited = true;
  ancientPending = !ancientId.empty();
}

// EnterMapCoord(StartingMapPoint): the Ancient event. AncientEventModel.BeforeEventStarted
// heals to full first (Neow from 0 HP, which only matters for the animation).
Task<> Run::enterAncient() {
  ancientPending = false;
  auto e = db::event(ancientId);
  if (!e) co_return;
  player->hp = player->maxHp;
  co_await runEvent(std::move(e));
}

Task<> Run::chooseCardFor(std::vector<std::unique_ptr<Card>> options) {
  if (options.empty()) co_return;
  rewardCards = std::move(options);
  screen = Screen::Reward;
  int pick = co_await rewardChoice.next();
  if (pick >= 0 && pick < (int)rewardCards.size()) deck.push_back(std::move(rewardCards[pick]));
  rewardCards.clear();
}

void Run::generateMap() {
  // StandardActMap (mapgen.cpp): the game's own generator, paths, pruning and types.
  // StandardActMap.CreateFor: Rng(seed, "act_<n>_map").
  std::string stream = "act_" + std::to_string(actIndex + 1) + "_map";
  nodes = generateStandardActMap(rng(stream.c_str()), actIndex);
  // NMapScreen layout: each point jittered by up to ±21 / ±25 units (map_jitter_<act>
  // stream) and tilted by NextGaussianFloat(0, 8) degrees (Rng.Chaotic in C#: cosmetic only).
  std::string jitter = "map_jitter_" + std::to_string(actIndex);
  Rng& j = rng(jitter.c_str());
  for (auto& n : nodes) {
    n.x = (float)n.col;
    n.y = (float)n.row;
    if (n.type == RoomType::Boss || n.type == RoomType::Ancient) continue;
    n.jx = j.nextFloat(42.f) - 21.f;
    n.jy = j.nextFloat(50.f) - 25.f;
    float u1 = std::max(1e-6f, j.nextFloat()), u2 = j.nextFloat();
    n.angle = 8.f * std::sqrt(-2.f * std::log(u1)) * std::cos(6.2831853f * u2);
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
    if (nodes[i].type != RoomType::Ancient && std::find(out.begin(), out.end(), i) == out.end()) out.push_back(i);
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
  for (;;) {
    if (ancientPending) {
      co_await enterAncient();
      if (died) { screen = Screen::GameOver; co_return; }
    }
    screen = Screen::Map;
    int choice = co_await mapChoice.next();
    if (devSkipAct) {  // developer menu: 跳到下一幕
      devSkipAct = false;
      if (actIndex + 1 < kActs) enterAct(actIndex + 1);
      continue;
    }
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
    // Debug: STS_ROOM=Event makes the first room an event; STS_EVENT=<id> picks which.
    bool forcedEvent = floor == 1 && getenv("STS_ROOM") && std::string(getenv("STS_ROOM")) == "Event";
    // "?" rooms resolve when entered (UnknownMapPointOdds); Unknown afterwards means an event.
    if (forcedEvent) type = RoomType::Unknown;
    else if (type == RoomType::Unknown) type = rollUnknownRoom();
    if (type == RoomType::Unknown) {
      std::unique_ptr<Event> e;
      if (const char* id = getenv("STS_EVENT"); forcedEvent && id) e = db::event(id);
      if (!e) e = pullNextEvent();
      if (e) {
        co_await runEvent(std::move(e));
        if (died) { screen = Screen::GameOver; co_return; }
        continue;
      }
    }
    if (type == RoomType::Unknown || type == RoomType::Shop) {
      // PORT NOTE: events and the merchant are not ported yet; say so and move on.
      for (Model* m : listeners()) co_await m->afterRoomEntered(type);
      placeholderText = type == RoomType::Shop ? "商店（尚未实现）" : "事件（尚未实现）";
      screen = Screen::Placeholder;
      co_await placeholderDone.next();
      continue;
    }

    if (type == RoomType::Monster || type == RoomType::Elite || type == RoomType::Boss) {
      std::string id;
      if (type == RoomType::Boss) id = bossId;
      else if (type == RoomType::Elite) {
        // ActModel.GenerateRooms: elites come from a grab bag, refilled when empty.
        id = eliteQueue.front();
        std::rotate(eliteQueue.begin(), eliteQueue.begin() + 1, eliteQueue.end());
      } else if (fightsThisAct < act().weakCount) {
        id = weakQueue[fightsThisAct % weakQueue.size()];
      } else {
        id = normalQueue[(fightsThisAct - act().weakCount) % normalQueue.size()];
      }
      if (type == RoomType::Monster) ++fightsThisAct;
      // Debug: STS_ENCOUNTER=<EncounterId> makes the first fight that encounter.
      if (const char* forced = getenv("STS_ENCOUNTER"); forced && floor == 1 && db::encounter(forced)) id = forced;
      if (!devNextEncounter.empty() && db::encounter(devNextEncounter)) { id = devNextEncounter; devNextEncounter.clear(); }

      bool won = co_await fight(id);
      if (!won) { screen = Screen::GameOver; co_return; }
      // RewardsSet.WithRewardsFromRoom: the last act's boss gives nothing; the run is won.
      // PORT NOTE: C# then enters TheArchitect event (the ending); not ported yet.
      if (type == RoomType::Boss && actIndex + 1 >= kActs) { screen = Screen::Victory; co_return; }

      // Rewards (RewardsSet): gold, then for elites a relic, then a pick of three cards.
      // EncounterModel gold: monster 10-20, elite 35-45, boss 100.
      Rng& rr = rng("Rewards");
      co_await gainGold(type == RoomType::Boss ? 100 : type == RoomType::Elite ? rr.nextInt(35, 46) : rr.nextInt(10, 21));
      combat.reset();
      for (auto& rel : relics) rel->combat = nullptr;
      if (type == RoomType::Elite) co_await offerRelic(pullRelicFromFront(relicBag, rollRelicRarity(rr)), false);
      rewardCards = cardReward(type, 3);
      screen = Screen::Reward;
      int pick = co_await rewardChoice.next();
      if (pick >= 0 && pick < (int)rewardCards.size()) deck.push_back(std::move(rewardCards[pick]));
      rewardCards.clear();
      if (type == RoomType::Boss) {
        // Hook.TryModifyRewards (Lava Rock): extra relic rewards after the boss.
        int extra = 0;
        for (auto& rel : relics) extra += rel->bonusRelicRewards(RoomType::Boss);
        for (int i = 0; i < extra; ++i) co_await offerRelic(pullRelicFromFront(relicBag, rollRelicRarity(rr)), false);
        // RunManager.EnterNextAct. PORT NOTE: the next act starts with its Ancient, which
        // heals to full (AncientEventModel.BeforeEventStarted); until Ancients are ported
        // (package 11) the heal happens here.
        enterAct(actIndex + 1);
        if (ancientId.empty()) player->hp = player->maxHp;  // stands in for the Ancient's heal
      }
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
