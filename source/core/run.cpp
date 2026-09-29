// Run flow: map, room dispatch, rewards and rest sites.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

#include "game.h"
#include "progress.h"

namespace sts {

namespace {
// progress::onRunEnded, guarded so a run is only ever recorded once (Run::main has several
// GameOver/Victory exits, and Run::abandon() is a separate, UI-triggered path).
void recordRunEnd(Run& r, progress::RunOutcome outcome) {
  if (r.progressRecorded) return;
  r.progressRecorded = true;
  progress::onRunEnded(r.characterId, r.ascension, outcome);
}
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

// RunManager: the shared bag (shared pool) then the player's bag (shared + the character's
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
  for (auto& id : character().relicPool) all.push_back(id);
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
  progress::markRelicSeen(rel->id);
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
                                             bool canCancel, bool showUpgrade, int minCount) {
  std::vector<Card*> opts;
  // CardSelectCmd.FromDeckForRemoval / FromDeckForTransformation: Eternal cards are not offered.
  bool removal = prompt == "card_selection.TO_REMOVE", transform = prompt == "card_selection.TO_TRANSFORM";
  for (auto& c : deck)
    if ((!filter || filter(c.get())) && !((removal || transform) && !c->isRemovable())) opts.push_back(c.get());
  if (opts.empty()) co_return std::vector<Card*>{};
  deckChoice.prompt = std::move(prompt);
  deckChoice.options = std::move(opts);
  deckChoice.count = count;
  deckChoice.minCount = minCount;
  deckChoice.canCancel = canCancel;
  deckChoice.showUpgrade = showUpgrade;
  deckChoice.active = true;
  auto picked = co_await deckChoice.result.next();
  deckChoice.active = false;
  co_return picked;
}

// CardPileCmd.Add(Deck): Hook.TryModifyCardBeingAddedToDeck (the eggs upgrade it), then
// AfterCardChangedPiles (Book of Five Rings, Lucky Fysh).
Card* Run::addCardToDeck(std::unique_ptr<Card> c) {
  if (!c) return nullptr;
  for (auto& rel : relics)
    if (rel->upgradesNewCard(*c)) c->upgrade();
  progress::markCardSeen(c->id);
  deck.push_back(std::move(c));
  Card* added = deck.back().get();
  for (auto& rel : relics) rel->afterCardAddedToDeck(added);
  return added;
}

void Run::removeCardFromDeck(Card* c) {
  deck.erase(std::remove_if(deck.begin(), deck.end(), [&](const std::unique_ptr<Card>& d) { return d.get() == c; }), deck.end());
}

Card* Run::transformCard(Card* c, std::unique_ptr<Card> into) {
  if (!into || !c->isTransformable()) return c;  // CardCmd.Transform skips Eternal cards
  for (auto& d : deck)
    if (d.get() == c) { d = std::move(into); return d.get(); }
  return addCardToDeck(std::move(into));
}

// CardFactory transform: a random card of the character's pool (Common/Uncommon/Rare),
// never the card itself. PORT NOTE: C# also weights by rarity odds; this picks uniformly.
std::unique_ptr<Card> Run::randomTransformFor(Card* c, Rng& rr) {
  auto pool = db::characterCards(characterId, [&](const Card& x) {
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
  co_await combatRewards(enc->room);
  co_return true;
}

// RewardsSet for a won fight (the combat is still alive for the gold hooks; it is freed
// here): gold (+ Amethyst Aubergine), a potion roll, the elite relic, then a pick of three
// cards (through the card reward hooks) and any extra card rewards (Prayer Wheel, White Star).
//
// S14 (RGDSplus U17): rewardItems is a running, display-only log of what this sequence has
// granted so far, appended to at the exact point each reward already resolved below -- the
// RNG calls and their order are byte-for-byte the same as before this package. An earlier
// version of this tried to stage every reward up front and let the UI claim them in any order;
// that changed the RNG order whenever a relic's afterObtained hook (or a potion's
// afterPotionProcured, both fire via spawnSide) ran relative to the *next* reward's roll, so
// SIM_ALLCARDS / SIM_ALLRELICS stopped matching. Don't restage without re-checking that.
// S14 (RGDSplus U17, C# RewardsSet.WithRewardsFromRoom -> GenerateRewardsFor ->
// GenerateWithoutOffering -> Offer; NRewardsScreen): every reward of the room is generated up
// front here (a list of RewardItem, nothing granted yet), then offered as one interactive list
// the player claims in any order (the loop below) until they Proceed.
//
// Generation order matches the C#'s RNG consumption, which is NOT simply "gold, potion, relic,
// card": RewardsSet.GenerateRewardsFor pushes Gold, then (maybe) Potion, then CardReward, then
// -- for Elite only -- RelicReward, and RewardsSet.GenerateWithoutOffering calls Populate() (the
// point each reward actually rolls its RNG) on that list in push order, i.e. Gold, Potion, Card,
// Relic. An earlier version of this package kept the pre-S14 order (gold, potion, relic, card)
// as a side effect of also granting things immediately; that is wrong for Elite rooms and has
// been fixed here: the card reward now rolls before the relic reward. Rewards.Sort() then
// reorders the final *list* by RewardsSetIndex (Gold < Potion < Relic < Card) regardless of
// populate order, which is why the stable_sort below runs after generation, not during it.
// Hook-added extras (Black Star's bonus relic, Amethyst Aubergine's extra flat-gold row, Prayer
// Wheel / White Star's extra card rounds -- all via TryModifyRewards in the C#) are rolled after
// all the base rewards, in relic order, matching Hook.ModifyRewards running once after the base
// Populate() loop; none of ours consume RNG except the relic pulls and card rolls, so this only
// affects card/relic order, already covered above.
Task<> Run::combatRewards(RoomType type) {
  Rng& rr = rng("Rewards");
  // EncounterModel.Min/MaxGoldReward: 10-20 / 35-45 / 100, times 0.75 (truncated) with Poverty.
  auto poor = [&](int v) { return hasAscension(kPoverty) ? (int)(v * 0.75) : v; };
  int baseGold = type == RoomType::Boss ? poor(100)
                : type == RoomType::Elite ? rr.nextInt(poor(35), poor(45) + 1) : rr.nextInt(poor(10), poor(20) + 1);
  bool finalBoss = type == RoomType::Boss && actIndex + 1 >= kActs;
  // The fight is freed and the screen leaves it in the same step (nothing may wait in
  // between: the UI still shows Screen::Combat until then).
  combat.reset();
  player->combat = nullptr;
  for (auto& rel : relics) rel->combat = nullptr;
  screen = Screen::Reward;

  rewardItems.clear();
  { RewardItem g; g.kind = RewardKind::Gold; g.gold = baseGold; rewardItems.push_back(std::move(g)); }

  if (rollPotionReward(type)) {  // RollForPotionAndAddTo / PotionReward.Populate
    RewardItem item;
    item.kind = RewardKind::Potion;
    item.potion = randomPotion(rr, false);
    rewardItems.push_back(std::move(item));
  }

  auto makeCardItem = [&](RoomType odds) {
    RewardItem item;
    item.kind = RewardKind::Card;
    item.cards = cardReward(odds, 3);
    for (bool late : {false, true})
      for (auto& rel : relics) rel->modifyCardReward(item.cards, odds, late);
    rewardItems.push_back(std::move(item));
  };
  makeCardItem(type);  // CardReward.Populate -- before the relic reward, see comment above

  if (type == RoomType::Elite) {
    RewardItem item;
    item.kind = RewardKind::Relic;
    item.relic = pullRelicFromFront(relicBag, rollRelicRarity(rr));
    rewardItems.push_back(std::move(item));
    int bonus = 0;
    for (auto& rel : relics) bonus += rel->bonusRelicRewards(RoomType::Elite);  // Black Star (hook-added, after)
    for (int i = 0; i < bonus; ++i) {
      RewardItem b;
      b.kind = RewardKind::Relic;
      b.relic = pullRelicFromFront(relicBag, rollRelicRarity(rr));
      rewardItems.push_back(std::move(b));
    }
  }
  // Amethyst Aubergine: TryModifyRewards adds its own flat GoldReward, not a bonus folded into
  // the base one -- so it is its own row here too.
  if (!finalBoss) for (auto& rel : relics) {
    int extra = rel->extraCombatGold(type);
    if (extra > 0) { RewardItem g; g.kind = RewardKind::Gold; g.gold = extra; rewardItems.push_back(std::move(g)); }
  }
  for (auto& rel : relics)
    for (RoomType odds : rel->extraCardRewards(type)) makeCardItem(odds);  // Prayer Wheel / White Star
  for (; bonusCardRewards > 0; --bonusCardRewards) makeCardItem(type);  // CombatRoom.AddExtraReward (TheHunt)

  std::stable_sort(rewardItems.begin(), rewardItems.end(), [](const RewardItem& a, const RewardItem& b) {
    auto rank = [](RewardKind k) { return k == RewardKind::Gold ? 0 : k == RewardKind::Potion ? 1 : k == RewardKind::Relic ? 2 : 3; };
    return rank(a.kind) < rank(b.kind);
  });

  // ---- offer: claim rows in any order; Proceed (-1 or an out-of-range index) forfeits the rest.
  for (;;) {
    int pick = co_await rewardListChoice.next();
    if (pick < 0 || pick >= (int)rewardItems.size()) break;
    RewardItem& item = rewardItems[pick];
    bool claimed = false;
    switch (item.kind) {
      case RewardKind::Gold:
        co_await gainGold(item.gold);
        claimed = true;
        break;
      case RewardKind::Potion:
        // PotionReward.OnSelect: fails (row stays) while the belt is full.
        if (hasOpenPotionSlot()) { procurePotion(std::move(item.potion)); claimed = true; }
        break;
      case RewardKind::Relic:
        co_await obtainRelic(std::move(item.relic));
        claimed = true;
        break;
      case RewardKind::Card: {
        rewardCards = std::move(item.cards);
        int cardPick = co_await rewardChoice.next();
        if (cardPick >= 0 && cardPick < (int)rewardCards.size()) {
          addCardToDeck(std::move(rewardCards[cardPick]));
          claimed = true;
        } else {
          item.cards = std::move(rewardCards);  // skipped: same options, the row stays
        }
        rewardCards.clear();
        break;
      }
    }
    if (claimed) rewardItems.erase(rewardItems.begin() + pick);
  }
  rewardItems.clear();
  rewardCards.clear();
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
  bool juzu = hasRelic("JuzuBracelet");  // ModifyUnknownMapPointRoomTypes: no Monster
  for (auto& [t, p] : odds) {
    if (juzu && t == RoomType::Monster) continue;
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

void Run::start(uint64_t s, const std::string& charId, int ascensionLevel) {
  db::init();
  seed = s;
  if (const char* env = getenv("STS_ASCENSION")) ascensionLevel = std::atoi(env);  // debug: STS_ASCENSION=0-10
  ascension = std::clamp(ascensionLevel, 0, 10);
  characterId = db::character(charId).id;  // unknown ids fall back to the Ironclad
  const Character& ch = character();
  rngs.clear();
  player = std::make_unique<Creature>();
  player->isPlayer = true;
  player->side = Side::Player;
  player->name = ch.key;
  player->hp = player->maxHp = ch.startingHp;
  gold = ch.startingGold;
  floor = 0;
  deck.clear();
  // PORT NOTE: cards / relics of a character that is not ported yet are skipped, so its run
  // starts with what exists (db::characterPlayable says whether it is complete).
  for (auto& id : ch.starterDeck) if (auto c = db::card(id)) { progress::markCardSeen(c->id); deck.push_back(std::move(c)); }
  relics.clear();
  for (auto& id : ch.startingRelics) {
    auto rel = db::relic(id);
    if (!rel) continue;
    rel->run = this;
    progress::markRelicSeen(rel->id);
    relics.push_back(std::move(rel));
  }
  // AscensionManager.ApplyEffectsTo: AscendersBane goes into the starting deck (TightBelt is the potion slot above).
  if (hasAscension(kAscendersBane))
    if (auto bane = db::card("AscendersBane")) deck.push_back(std::move(bane));
  populateRelicBags();
  visitedEvents.clear();
  died = false;
  progressRecorded = false;
  potions.clear();
  potions.resize((size_t)ascValue(kTightBelt, 2, 3));  // Player: 3 potion slots, one fewer with TightBelt
  potionRewardOdds = 0.4f;
  shopRemovalsUsed = 0;
  // Debug: STS_RELICS=Girya,Shovel,... adds relics (pickup effects skipped).
  if (const char* list = getenv("STS_RELICS")) {
    std::string s = list;
    for (size_t a = 0; a <= s.size();) {
      size_t b = s.find(',', a);
      if (b == std::string::npos) b = s.size();
      if (auto rel = db::relic(s.substr(a, b - a))) { rel->run = this; relics.push_back(std::move(rel)); }
      a = b + 1;
    }
  }
  // Debug: STS_POTIONS=FirePotion,BlockPotion,... fills the belt.
  if (const char* list = getenv("STS_POTIONS")) {
    std::string s = list;
    for (size_t a = 0; a <= s.size();) {
      size_t b = s.find(',', a);
      if (b == std::string::npos) b = s.size();
      procurePotion(db::potion(s.substr(a, b - a)));
      a = b + 1;
    }
  }
  // Debug: STS_ENCHANT=Sharp:3,Glam,... enchants the first fitting deck card for each entry.
  if (const char* list = getenv("STS_ENCHANT")) {
    std::string s = list;
    for (size_t a = 0; a <= s.size();) {
      size_t b = s.find(',', a);
      if (b == std::string::npos) b = s.size();
      std::string item = s.substr(a, b - a);
      size_t colon = item.find(':');
      int amount = colon == std::string::npos ? 1 : std::atoi(item.c_str() + colon + 1);
      auto probe = db::enchantment(item.substr(0, colon));
      if (probe)
        for (auto& c : deck)
          if (!c->enchantment && probe->canEnchant(*c)) { enchantCard(c.get(), probe->id, amount); break; }
      a = b + 1;
    }
  }
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
  // Hive / Glory roll one of their three (plus the shared Darv if this act got him,
  // RunManager.GenerateRooms: Darv goes to act 2, act 3 or neither). Only registered events.
  ancientId.clear();
  bool firstRoom = floor == 0;
  bool debugStart = firstRoom && (getenv("STS_ENCOUNTER") || getenv("STS_ROOM") || getenv("STS_EVENT") || getenv("STS_NO_NEOW"));
  Rng& up = rng("UpFront");
  if (firstRoom) {
    std::vector<std::string> shared = {"Darv"};
    up.shuffle(shared);
    for (auto& s : sharedAncients) s.clear();
    for (int a = 1; a < kActs; ++a) {
      int count = up.nextInt((int)shared.size() + 1);
      sharedAncients[a].assign(shared.begin(), shared.begin() + count);
      shared.erase(shared.begin(), shared.begin() + count);
    }
  }
  static const std::vector<std::string> actAncients[kActs] = {
      {"Neow"}, {"Orobas", "Pael", "Tezcatara"}, {"Nonupeipe", "Tanx", "Vakuu"}};
  std::vector<std::string> candidates;
  for (auto& id : actAncients[actIndex]) if (db::event(id)) candidates.push_back(id);
  for (auto& id : sharedAncients[actIndex]) if (db::event(id)) candidates.push_back(id);
  if (!candidates.empty() && !debugStart) ancientId = up.nextItem(candidates);
  if (const char* forced = getenv("STS_ANCIENT"); forced && db::event(forced) && !debugStart) ancientId = forced;
  // RunManager.GenerateRooms: DoubleBoss gives the last act a second boss (another of its bosses, UpFront stream).
  secondBossId.clear();
  if (hasAscension(kDoubleBoss) && actIndex == kActs - 1) {
    std::vector<std::string> others;
    for (auto& b : bosses) if (b != bossId) others.push_back(b);
    if (!others.empty()) secondBossId = up.nextItem(others);
  }
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
  // AncientEventModel.BeforeEventStarted: heal to full (Neow starts from 0 HP); WearyTraveler heals 80%.
  if (hasAscension(kWearyTraveler)) {
    int from = ancientId == "Neow" ? 0 : player->hp;
    player->hp = std::min(player->maxHp, (Dec(from) + Dec(player->maxHp - from) * Dec::lit(0.8)).toInt());
  } else {
    player->hp = player->maxHp;
  }
  co_await runEvent(std::move(e));
}

Task<> Run::chooseCardFor(std::vector<std::unique_ptr<Card>> options) {
  if (options.empty()) co_return;
  rewardCards = std::move(options);
  screen = Screen::Reward;
  int pick = co_await rewardChoice.next();
  if (pick >= 0 && pick < (int)rewardCards.size()) addCardToDeck(std::move(rewardCards[pick]));
  rewardCards.clear();
}

void Run::generateMap() {
  // StandardActMap (mapgen.cpp): the game's own generator, paths, pruning and types.
  // StandardActMap.CreateFor: Rng(seed, "act_<n>_map").
  std::string stream = "act_" + std::to_string(actIndex + 1) + "_map";
  // MapPointTypeCounts.NumOfElites: 5, 8 (round(5 * 1.6)) with SwarmingElites.
  nodes = generateStandardActMap(rng(stream.c_str()), actIndex, hasAscension(kSwarmingElites) ? 8 : 5);
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

// CardRarityOdds.Roll; Scarcity lowers the rare odds and the growth of the offset.
Rarity Run::rollRarity(RoomType room) {
  float rare, uncommon;
  float offset = rarityOffset;
  bool scarce = hasAscension(kScarcity);
  if (room == RoomType::Boss) { rare = 1.f; uncommon = 0.f; offset = 0.f; }
  else if (room == RoomType::Elite) { rare = scarce ? 0.05f : 0.1f; uncommon = 0.4f; }
  else { rare = scarce ? 0.0149f : 0.03f; uncommon = 0.37f; }
  float r = rng("Rewards").nextFloat();
  float rareOdds = rare + offset;
  Rarity result = r < rareOdds ? Rarity::Rare : r < uncommon + rareOdds ? Rarity::Uncommon : Rarity::Common;
  if (result == Rarity::Rare) rarityOffset = -0.05f;
  else rarityOffset = std::min(rarityOffset + (scarce ? 0.005f : 0.01f), 0.4f);
  return result;
}

// PORT NOTE: the C# compares (decimal)float with a decimal; here both are doubles.
void Run::rollCardUpgrade(Card& c, double baseChance) {
  double num = rng("Rewards").nextFloat();
  if (!c.upgradable()) return;
  double odds = baseChance;
  if (c.rarity != Rarity::Rare) odds += actIndex * (hasAscension(kScarcity) ? 0.125 : 0.25);
  // Hook.ModifyCardRewardUpgradeOdds: no model overrides it.
  if (num <= odds) c.upgrade();
}

// CardFactory.CreateForReward: roll a rarity per card, no duplicates.
std::vector<std::unique_ptr<Card>> Run::cardReward(RoomType room, int count) {
  std::vector<std::unique_ptr<Card>> out;
  std::vector<std::string> taken;
  for (int i = 0; i < count; ++i) {
    Rarity want = rollRarity(room);
    auto pool = db::characterCards(characterId, [&](const Card& c) { return c.rarity == want; });
    pool.erase(std::remove_if(pool.begin(), pool.end(), [&](const std::string& id) {
      return std::find(taken.begin(), taken.end(), id) != taken.end();
    }), pool.end());
    if (pool.empty()) pool = db::characterCards(characterId, [&](const Card& c) {
      return (c.rarity == Rarity::Common || c.rarity == Rarity::Uncommon || c.rarity == Rarity::Rare) &&
             std::find(taken.begin(), taken.end(), c.id) == taken.end();
    });
    if (pool.empty()) break;
    std::string id = rng("Rewards").nextItem(pool);
    taken.push_back(id);
    progress::markCardSeen(id);  // seen once offered, whether or not it is picked
    out.push_back(db::card(id));
    rollCardUpgrade(*out.back(), 0);  // CardFactory.CreateForReward: RollForUpgrade(baseChance 0)
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
  c.maxEnergy = character().maxEnergy;  // CharacterModel.MaxEnergy
  c.orbCapacity = character().orbSlots;  // PlayerCombatState(player): OrbQueue.AddCapacity(BaseOrbSlotCount)
  player->combat = &c;
  player->block = 0;
  player->powers.clear();
  for (auto& rel : relics) rel->combat = &c;

  // Deck -> draw pile, shuffled.
  for (auto& card : deck) {
    Card* cc = c.addCard(card->clone());
    cc->deckVersion.p = card.get();  // CardModel.DeckVersion
    c.draw.push_back(cc);
  }
  std::stable_sort(c.draw.begin(), c.draw.end(), [](Card* a, Card* b) { return a->id < b->id; });
  rng("Shuffle").shuffle(c.draw);
  for (Model* m : c.listeners()) m->modifyShuffleOrder(c.draw, true);  // CardPile.RandomizeOrderInternal

  for (auto& m : enc->generate(rng("Encounters"))) { progress::markMonsterSeen(m->id); c.createEnemy(std::move(m)); }

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

namespace {
Task<> sideTask(Task<> t, int* pending) {
  co_await t;
  --*pending;
}
}  // namespace

void Run::spawnSide(Task<> t) {
  ++pendingSide;
  Scheduler::get().spawn(sideTask(std::move(t), &pendingSide));
}

Task<> Run::main() {
  for (;;) {
    if (ancientPending) {
      co_await enterAncient();
      if (died) { recordRunEnd(*this, progress::RunOutcome::Loss); screen = Screen::GameOver; co_return; }
    }
    while (pendingSide > 0) co_await wait(0.05);
    screen = Screen::Map;
    if (onSavePoint) onSavePoint(*this);
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
           : f == "Shop" ? RoomType::Shop
           : f == "Boss" ? RoomType::Boss : type;
    }
    // Debug: STS_ROOM=Event makes the first room an event; STS_EVENT=<id> picks which.
    bool forcedEvent = floor == 1 && getenv("STS_ROOM") && std::string(getenv("STS_ROOM")) == "Event";
    // "?" rooms resolve when entered (UnknownMapPointOdds); Unknown afterwards means an event.
    if (forcedEvent) type = RoomType::Unknown;
    else if (type == RoomType::Unknown) {
      type = rollUnknownRoom();
      for (auto& rel : relics) co_await rel->afterUnknownRoomEntered();  // Planisphere
    }
    if (type == RoomType::Unknown) {
      std::unique_ptr<Event> e;
      if (const char* id = getenv("STS_EVENT"); forcedEvent && id) e = db::event(id);
      if (!e) e = pullNextEvent();
      if (e) {
        co_await runEvent(std::move(e));
        if (died) { recordRunEnd(*this, progress::RunOutcome::Loss); screen = Screen::GameOver; co_return; }
        continue;
      }
    }
    if (type == RoomType::Shop) {
      co_await enterShop();
      continue;
    }
    if (type == RoomType::Unknown) {
      // PORT NOTE: no registered event is left; say so and move on.
      for (Model* m : listeners()) co_await m->afterRoomEntered(type);
      placeholderText = "事件（尚未实现）";
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
      if (!won) { recordRunEnd(*this, progress::RunOutcome::Loss); screen = Screen::GameOver; co_return; }
      // RewardsSet.WithRewardsFromRoom: the last act's boss gives nothing; the run is won.
      // PORT NOTE: C# then enters TheArchitect event (the ending); not ported yet.
      if (type == RoomType::Boss && actIndex + 1 >= kActs) {
        // DoubleBoss: the second boss follows the first (which, like every boss of the last act, gives no rewards).
        if (!secondBossId.empty()) {
          std::string second = secondBossId;
          secondBossId.clear();
          ++floor;
          if (!co_await fight(second)) { recordRunEnd(*this, progress::RunOutcome::Loss); screen = Screen::GameOver; co_return; }
        }
        recordRunEnd(*this, progress::RunOutcome::Win);
        screen = Screen::Victory;
        co_return;
      }

      // Rewards (RewardsSet): gold, then for elites a relic, then a pick of three cards.
      // EncounterModel gold: monster 10-20, elite 35-45, boss 100.
      co_await combatRewards(type);
      Rng& rr = rng("Rewards");
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
      co_await restSite();
    }
  }
}

// RestSiteRoom: Heal and Smith, plus Lift (Girya, 3 times per run) and Dig (Shovel).
// Using one option ends the visit, unless Miniature Tent keeps the others open (then
// the player leaves with -1).
Task<> Run::restSite() {
  restUsed.clear();
  Relic* girya = nullptr;
  for (auto& r : relics) if (r->id == "Girya") girya = r.get();
  for (;;) {
    restOptions = {0, 1};
    if (girya && girya->displayAmount() < 3) restOptions.push_back(2);
    if (hasRelic("Shovel")) restOptions.push_back(3);
    if (hasRelic("MeatCleaver")) restOptions.push_back(4);   // CookRestSiteOption
    if (hasRelic("PumpkinCandle")) restOptions.push_back(5); // KindleRestSiteOption
    screen = Screen::Rest;
    int opt = co_await restChoice.next();
    if (opt < 0) break;
    if (std::find(restUsed.begin(), restUsed.end(), opt) != restUsed.end()) continue;
    bool done = false;
    if (opt == 0) {
      // HealRestSiteOption: 30% of max HP, through Hook.ModifyRestSiteHealAmount.
      Dec amount = Dec(player->maxHp) * Dec::lit(0.3);
      for (Model* m : listeners()) amount = m->modifyRestSiteHealAmount(player.get(), amount);
      int before = player->hp;
      player->hp = std::min(player->maxHp, player->hp + amount.toInt());
      lastHeal = player->hp - before;
      for (Model* m : listeners()) co_await m->afterRestSiteHeal();
      co_await wait(0.6);
      // TinyMailbox.TryModifyRestSiteHealRewards: two potion rewards.
      if (hasRelic("TinyMailbox"))
        for (int i = 0; i < 2; ++i) co_await offerPotion(randomPotion(rng("Rewards"), false));
      done = true;
    } else if (opt == 1) {
      upgradeOptions.clear();
      for (auto& card : deck) if (card->upgradable()) upgradeOptions.push_back(card.get());
      screen = Screen::RestUpgrade;
      int idx = co_await upgradeChoice.next();
      if (idx >= 0 && idx < (int)upgradeOptions.size()) {
        upgradeOptions[idx]->upgrade();
        co_await wait(0.4);
        done = true;
      }
    } else if (opt == 2 && girya) {  // LiftRestSiteOption
      girya->restSiteAction(2);  // TimesLifted++
      girya->doFlash();
      done = true;
    } else if (opt == 4) {  // Cook: remove 2 cards (cancelable), +5 max HP
      if (deck.size() < 2) continue;
      auto picked = co_await selectFromDeck("card_selection.TO_REMOVE", nullptr, 2, true);
      screen = Screen::Rest;
      if (picked.empty()) continue;
      for (Card* c : picked) removeCardFromDeck(c);
      co_await gainMaxHp(5);
      done = true;
    } else if (opt == 5) {  // Kindle: Pumpkin Candle +5 combats
      for (auto& r : relics) if (r->id == "PumpkinCandle") { r->restSiteAction(5); r->doFlash(); }
      done = true;
    } else if (opt == 3) {  // DigRestSiteOption: a relic from the front of the bag
      co_await offerRelic(pullRelicFromFront(relicBag, rollRelicRarity(rng("Rewards"))), false);
      done = true;
    }
    if (!done) continue;
    restUsed.push_back(opt);
    bool tent = hasRelic("MiniatureTent");
    if (!tent) break;
  }
  restOptions.clear();
}

// Player-initiated abandon (pause menu -> confirm). See the PORT NOTE on the declaration in
// game.h: nothing calls this yet because the confirm button is in source/ui/, a separate package.
void Run::abandon() { recordRunEnd(*this, progress::RunOutcome::Abandon); }

}  // namespace sts
