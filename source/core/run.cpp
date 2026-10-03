// Run flow: map, room dispatch, rewards and rest sites.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <set>

#include "achievements.h"
#include "badges.h"
#include "daily.h"
#include "game.h"
#include "history.h"
#include "modifiers.h"
#include "progress.h"

namespace sts {

namespace {
// progress::onRunEnded + the run history entry (M2), guarded so a run is only ever recorded once
// (Run::main has several GameOver/Victory exits, and Run::abandon() is a separate, UI-triggered path).
void recordRunEnd(Run& r, progress::RunOutcome outcome) {
  if (r.progressRecorded) return;
  r.progressRecorded = true;
  // ProgressSaveManager.UpdateWithRunData: a custom or daily run never raises the ascension level (M11).
  int keepAsc = r.customRun || !r.dailyDate.empty() ? progress::state().character(r.characterId).maxAscension : -1;
  progress::onRunEnded(r.characterId, r.ascension, outcome);
  if (keepAsc >= 0) progress::state().character(r.characterId).maxAscension = keepAsc;
  // UpdateWithRunData: each Ancient map point's first Event room counts a win or a loss (AncientStats).
  for (auto& act : r.mapHistory)
    for (auto& point : act) {
      if (point.type != history::PointType::Ancient) continue;
      for (auto& room : point.rooms)
        if (room.type == history::RoomKind::Event) {
          progress::recordAncientRun(room.model, r.characterId, outcome == progress::RunOutcome::Win);
          break;
        }
    }
  achievements::afterRunEnded(r, outcome == progress::RunOutcome::Win);  // M5: AchievementsHelper.AfterRunEnded
  history::onRunEnded(r, outcome == progress::RunOutcome::Win, outcome == progress::RunOutcome::Abandon);
  // M-stats: TotalPlaytime / Playtime / ArchitectDamage / FastestWinTime, and DiscoveredEvents (each point's first Event room).
  progress::recordRunTotals(r.characterId, (int64_t)r.runTime, outcome == progress::RunOutcome::Win, history::last() ? history::last()->score : 0, !r.customRun && r.dailyDate.empty());
  for (auto& act : r.mapHistory) for (auto& point : act) for (auto& room : point.rooms) if (room.type == history::RoomKind::Event) { progress::markEventSeen(room.model); break; }
  // M12: the daily's local best (the C# uploads DailyRunUtility.UploadScore here).
  if (!r.dailyDate.empty())
    if (const history::RunRecord* rec = history::last()) daily::recordScore(r.dailyDate, rec->score);
}
// Run history kinds of the port's room types.
history::PointType historyPointType(RoomType t) {
  switch (t) {
    case RoomType::Monster: return history::PointType::Monster;
    case RoomType::Elite: return history::PointType::Elite;
    case RoomType::Rest: return history::PointType::RestSite;
    case RoomType::Treasure: return history::PointType::Treasure;
    case RoomType::Unknown: return history::PointType::Unknown;
    case RoomType::Boss: return history::PointType::Boss;
    case RoomType::Shop: return history::PointType::Shop;
    case RoomType::Ancient: return history::PointType::Ancient;
    default: return history::PointType::Unassigned;
  }
}
history::RoomKind historyFightKind(RoomType t) {
  return t == RoomType::Elite ? history::RoomKind::Elite : t == RoomType::Boss ? history::RoomKind::Boss : history::RoomKind::Monster;
}
// Registered ids of a list, in order.
std::vector<std::string> registered(const std::vector<std::string>& ids) {
  std::vector<std::string> out;
  for (auto& id : ids) if (db::encounter(id)) out.push_back(id);
  return out;
}
}  // namespace

Creature* Relic::owner() const { return run->player.get(); }

void Run::historyPoint(history::PointType type) {
  if ((int)mapHistory.size() <= actIndex) mapHistory.resize((size_t)actIndex + 1);
  mapHistory[actIndex].push_back({type, {}, 0});
}

void Run::historyRoom(history::RoomKind type, const std::string& model) {
  if (mapHistory.empty() || mapHistory.back().empty()) return;  // no map point yet (debug starts)
  mapHistory.back().back().rooms.push_back({type, model});
}

std::vector<Model*> Run::listeners() {
  std::vector<Model*> out;
  for (auto& c : deck) {  // RunState.IterateHookListeners: the deck cards (and enchantments) first (E2)
    c->run = this;
    out.push_back(c.get());
    if (c->enchantment) out.push_back(c->enchantment.get());
  }
  for (auto& r : relics) out.push_back(r.get());
  for (auto& m : modifiers) out.push_back(m.get());  // RunState.IterateHookListeners: modifiers after relics
  return out;
}

bool Run::hasRelic(const std::string& id) const {
  for (auto& r : relics) if (r->id == id) return true;
  return false;
}

// RunManager: the shared bag (shared pool) then the player's bag (shared + the character's
// pools), both from the UpFront stream; each rarity deque is shuffled once, in the order the
// rarities first appear in the pool (RelicGrabBag._deques is a Dictionary: insertion order).
void Run::populateRelicBags() {
  relicBag.clear();
  sharedRelicBag.clear();
  Rng& r = rng("UpFront");
  auto fill = [&](std::map<RelicRarity, std::vector<std::string>>& bag, std::vector<std::string> ids) {
    std::vector<RelicRarity> order;
    for (auto& id : ids) {
      auto rel = db::relic(id);
      if (!rel) continue;  // not ported yet
      RelicRarity k = rel->rarity;
      if (k == RelicRarity::Common || k == RelicRarity::Uncommon || k == RelicRarity::Rare || k == RelicRarity::Shop) {
        if (!bag.count(k)) order.push_back(k);
        bag[k].push_back(id);
      }
    }
    for (RelicRarity k : order) r.shuffle(bag[k]);
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

// RelicModel.IsBeforeAct3TreasureChest: TotalFloor < 41 (38 in multiplayer, not supported here).
// These relics override IsAllowed with it (and stop dropping from the Act 3 treasure chest on).
// Kept as an id list instead of per-class overrides (same result).
// PORT NOTE (n/a: owner): LastingCandy is not dropped for an Ironclad on the profile's first run (UnlockState.NumberOfRuns == 0).
static bool relicAllowed(Relic& rel, Run& run) {
  static const char* const kLimited[] = {
      "AmethystAubergine", "BookOfFiveRings", "BowlerHat", "DragonFruit", "FrozenEgg", "Girya", "JuzuBracelet",
      "LastingCandy", "LuckyFysh", "MealTicket", "MoltenEgg", "OldCoin", "Planisphere", "Shovel", "ToxicEgg",
      "WhiteBeastStatue", "WhiteStar"};
  for (const char* id : kLimited)
    if (rel.id == id && run.floor >= 41) return false;
  return rel.isAllowed(run);
}

// RelicGrabBag.RemoveDisallowedRelicsFromDeques (both bags: the answer only depends on run state).
void Run::removeDisallowedRelics() {
  auto prune = [&](std::map<RelicRarity, std::vector<std::string>>& bag) {
    for (auto& [k, v] : bag) {
      for (size_t i = 0; i < v.size();) {
        auto rel = db::relic(v[i]);
        if (rel && !relicAllowed(*rel, *this)) v.erase(v.begin() + (long)i);
        else ++i;
      }
    }
  };
  prune(relicBag);
  prune(sharedRelicBag);
}

RelicRarity Run::rollRelicRarity(Rng& rr) {
  float f = rr.nextFloat();
  return f < 0.5f ? RelicRarity::Common : f < 0.83f ? RelicRarity::Uncommon : RelicRarity::Rare;
}

// RelicGrabBag.PullFromFront: an empty rarity falls through Shop -> Common ->
// Uncommon -> Rare, then RelicFactory.FallbackRelic (Circlet).
std::unique_ptr<Relic> Run::pullRelicFromFront(std::map<RelicRarity, std::vector<std::string>>& bag, RelicRarity k) {
  removeDisallowedRelics();
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
  history::noteRelicChoice(*this, rel->id, true);  // RelicCmd.Obtain: RelicChoices (picked)
  Relic* raw = rel.get();
  for (auto& [k, v] : relicBag) v.erase(std::remove(v.begin(), v.end(), raw->id), v.end());
  for (auto& [k, v] : sharedRelicBag) v.erase(std::remove(v.begin(), v.end(), raw->id), v.end());
  relics.push_back(std::move(rel));
  raw->doFlash();
  co_await raw->afterObtained();
}

Task<> Run::offerRelic(std::unique_ptr<Relic> rel, bool fromChest) {
  std::vector<std::unique_ptr<Relic>> one;
  if (rel) one.push_back(std::move(rel));
  co_await chooseRelic(std::move(one), fromChest);
}

// RelicSelectCmd.FromChooseARelicScreen (and the single-relic offer): take one, or skip.
Task<> Run::chooseRelic(std::vector<std::unique_ptr<Relic>> rs, bool fromChest) {
  rs.erase(std::remove(rs.begin(), rs.end(), nullptr), rs.end());
  if (rs.empty()) co_return;
  for (auto& rel : rs) rel->run = this;
  relicOffers = std::move(rs);
  relicOfferFromChest = fromChest;
  screen = Screen::RelicOffer;
  int take = co_await relicChoice.next();
  const bool taken = take >= 1 && take <= (int)relicOffers.size();
  // RelicReward.OnSkipped: an offered relic that is not taken is recorded as such (a treasure chest is no RelicReward).
  if (!fromChest && !taken)
    for (auto& rel : relicOffers) history::noteRelicChoice(*this, rel->id, false);
  if (taken) co_await obtainRelic(std::move(relicOffers[take - 1]));
  relicOffers.clear();
}

// ---------------------------------------------------------------- events

Creature* Event::owner() { return run->player.get(); }

// Hook.ModifyNextEvent (E2: LanternKey turns act 3's events into WarHistorianRepy).
static std::unique_ptr<Event> modifyNextEvent(Run& r, std::unique_ptr<Event> e) {
  std::string id = e ? e->id : "", out = id;
  for (Model* m : r.listeners()) out = m->modifyNextEvent(out);
  if (out == id) return e;
  auto replaced = db::event(out);
  return replaced ? std::move(replaced) : std::move(e);
}

// ActModel.PullNextEvent + RoomSet.EnsureNextEventIsValid: the next allowed event not
// seen this run; when all are used up, repeats are allowed.
// The pointer stays at events[eventsVisited % size]; entering the event room advances it
// (RoomSet.MarkVisited, Run::main). With nothing valid left the next event repeats as is.
std::unique_ptr<Event> Run::pullNextEvent() {
  RoomSet& rs = rooms[actIndex];
  if (rs.events.empty()) return modifyNextEvent(*this, nullptr);
  auto next = [&]() -> const std::string& { return rs.events[(size_t)rs.eventsVisited % rs.events.size()]; };
  for (size_t i = 0; i < rs.events.size(); ++i) {
    const std::string& id = next();
    bool seen = std::find(visitedEvents.begin(), visitedEvents.end(), id) != visitedEvents.end();
    auto e = db::event(id);
    if (e && e->isAllowed(*this) && !seen) return modifyNextEvent(*this, std::move(e));
    ++rs.eventsVisited;
  }
  return modifyNextEvent(*this, db::event(next()));  // "All unique events exhausted, allowing repetition"
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
  co_await ev->onStart();
  if (ev->finished || died) { currentEvent.reset(); co_return; }  // custom layouts leave without a proceed page
  for (;;) {
    screen = Screen::Event;
    int pick = co_await eventChoice.next();
    if (ev->finished || died) break;
    if (pick < 0 || pick >= (int)ev->options.size() || ev->options[pick].locked()) continue;
    auto action = ev->options[pick].action;  // the action may replace the options
    co_await action();
    if (died) break;
  }
  canUseOrRemovePotions = true;  // EnsureCleanup on leaving the room (OnEventFinished)
  currentEvent.reset();
}

Task<std::vector<Card*>> Run::selectFromDeck(std::string prompt, std::function<bool(Card*)> filter, int count,
                                             bool canCancel, bool showUpgrade, int minCount) {
  std::vector<Card*> opts;
  // CardSelectCmd.FromDeckForRemoval / FromDeckForTransformation: Eternal cards are not offered.
  bool removal = prompt == "card_selection.TO_REMOVE", transform = prompt == "card_selection.TO_TRANSFORM";
  for (auto& c : deck)
    if ((!filter || filter(c.get())) && !((removal || transform) && !c->isRemovable()) &&
        !(transform && c->type == CardType::Quest))  // FromDeckForTransformation skips Quest cards (E2)
      opts.push_back(c.get());
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
  // Hoarder.AfterCardChangedPiles: a new deck card brings two clones (which don't copy again).
  // A transformed card is not copied (transformCard sets `hoarding`): CardCmd.Transform reports the
  // deck as its old pile, and Hoarder only copies cards coming from no pile.
  if (!hoarding && hasModifier("Hoarder")) {
    hoarding = true;
    for (int i = 0; i < 2; ++i) addCardToDeck(added->clone());
    hoarding = false;
  }
  return added;
}

void Run::removeCardFromDeck(Card* c) {
  for (Model* m : listeners()) m->beforeCardRemoved(c);  // Hook.BeforeCardRemoved (E2)
  auto it = std::find_if(deck.begin(), deck.end(), [&](const std::unique_ptr<Card>& d) { return d.get() == c; });
  if (it == deck.end()) return;
  graveyard.push_back(std::move(*it));
  deck.erase(it);
}

// CardCmd.Transform (deck pile): RemoveFromCurrentPile, Hook.ModifyCardBeingAddedToDeck (the eggs),
// AddInternal at the end of the deck, Hook.AfterCardChangedPiles(oldPile: Deck) -- Lucky Fysh,
// Book of Five Rings, Bing Bong, Darkstone Periapt react, Hoarder does not.
Card* Run::transformCard(Card* c, std::unique_ptr<Card> into) {
  if (!into || !c->isTransformable()) return c;  // CardCmd.Transform skips Eternal cards
  auto it = std::find_if(deck.begin(), deck.end(), [&](const std::unique_ptr<Card>& d) { return d.get() == c; });
  if (it == deck.end()) return addCardToDeck(std::move(into));
  std::unique_ptr<Card> original = std::move(*it);  // alive until the hooks ran (Dowsing transforms itself)
  deck.erase(it);
  bool wasHoarding = hoarding;
  hoarding = true;
  Card* added = addCardToDeck(std::move(into));
  hoarding = wasHoarding;
  graveyard.push_back(std::move(original));
  return added;
}

// CardFactory.CreateRandomCardForTransform(original, isInCombat: false, rng): one uniform NextItem over
// GetDefaultTransformationOptions -- the original's own pool (the colorless pool for Quest cards and
// Event / Ancient / Token rarities), Common/Uncommon/Rare only unless the
// original is a Status or Curse, never the original's id; pool order.
std::unique_ptr<Card> Run::randomTransformFor(Card* c, Rng& rr) {
  static const char* const kCursePool[] = {"AscendersBane", "BadLuck", "Clumsy", "CurseOfTheBell", "Debt", "Decay",
                                           "Doubt", "Enthralled", "Folly", "Greed", "Guilty", "Injury", "Normality",
                                           "PoorSleep", "Regret", "Shame", "SporeMind", "Writhe"};
  static const char* const kStatusPool[] = {"Beckon", "Burn", "Dazed", "Debris", "FranticEscape", "Infection",
                                            "Wither", "Slimed", "Soot", "Toxic", "Void", "Wound"};
  bool anyRarity = c->rarity == Rarity::Status || c->rarity == Rarity::Curse;
  auto keep = [&](const Card& x) {
    return x.id != c->id &&
           (anyRarity || x.rarity == Rarity::Common || x.rarity == Rarity::Uncommon || x.rarity == Rarity::Rare);
  };
  std::vector<std::string> pool;
  auto fromList = [&](const char* const* ids, size_t n) {
    for (size_t i = 0; i < n; ++i)
      if (auto x = db::card(ids[i]); x && keep(*x)) pool.push_back(ids[i]);
  };
  bool colorlessPool = c->type == CardType::Quest || c->rarity == Rarity::Ancient || c->rarity == Rarity::Event ||
                       c->rarity == Rarity::Token || c->rarity == Rarity::Quest || db::isColorless(c->id);
  if (colorlessPool) {
    pool = db::colorlessCards(keep);
  } else if (c->rarity == Rarity::Curse) {
    fromList(kCursePool, sizeof kCursePool / sizeof *kCursePool);
  } else if (c->rarity == Rarity::Status) {
    fromList(kStatusPool, sizeof kStatusPool / sizeof *kStatusPool);
  } else {
    std::string owner = characterId;  // CardModel.Pool: the character pool that lists the card
    for (auto& ch : db::allCharacters()) {
      const auto& all = db::character(ch).cardPool;
      if (std::find(all.begin(), all.end(), c->id) != all.end()) { owner = ch; break; }
    }
    pool = db::characterCards(owner, keep);
  }
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
  historyRoom(historyFightKind(enc->room), encounterId);  // the event's fight is a room of its map point
  ++currentRoomCount;  // the fight's room goes on top of the event's (E2)
  co_await beforeRoomEntered(enc->room);
  bool won = co_await fight(encounterId);
  if (!won) { died = true; co_return false; }
  co_await combatRewards(enc->room);
  --currentRoomCount;
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
  // RewardsSet.GenerateRewardsFor: a monster room's GoldReward(Min, Max) is scaled by the combat's gold
  // proportion (CombatRoom.GoldProportion = Encounter.CalculateGoldProportion at the end of the fight,
  // rounded half to even); a proportion of 0 gives no gold row (and rolls nothing).
  const float goldShare = type == RoomType::Monster && combat ? combat->goldProportion() : 1.f;
  auto share = [&](int v) { return goldShare == 1.f ? v : (int)std::nearbyint((float)v * goldShare); };
  int baseGold = type == RoomType::Boss ? poor(100)
                : type == RoomType::Elite ? rr.nextInt(poor(35), poor(45) + 1)
                : goldShare <= 0.f ? 0
                : rewardGold >= 0 ? rr.nextInt(share(poor(rewardGold)), share(poor(rewardGold)) + 1) : rr.nextInt(share(poor(10)), share(poor(20)) + 1);
  rewardGold = -1;
  bool finalBoss = type == RoomType::Boss && actIndex + 1 >= kActs;
  // The fight is freed and the screen leaves it in the same step (nothing may wait in
  // between: the UI still shows Screen::Combat until then).
  int royaltiesGold = combat ? combat->extraRewardGold : 0;
  combat.reset();
  player->combat = nullptr;
  for (auto& rel : relics) rel->combat = nullptr;
  screen = Screen::Reward;

  rewardItems.clear();
  if (goldShare > 0.f || type != RoomType::Monster) { RewardItem g; g.kind = RewardKind::Gold; g.gold = baseGold; rewardItems.push_back(std::move(g)); }

  if (royaltiesGold > 0) { RewardItem g; g.kind = RewardKind::Gold; g.gold = royaltiesGold; rewardItems.push_back(std::move(g)); }  // RoyaltiesPower

  if (rollPotionReward(type)) {  // RollForPotionAndAddTo / PotionReward.Populate
    RewardItem item;
    item.kind = RewardKind::Potion;
    item.potion = randomPotion(rr, false);
    rewardItems.push_back(std::move(item));
  }

  // CardReward(ForRoom(room), 3): the room's own one WithFlags(IsFromCombat), hook-added ones without.
  auto makeCardItem = [&](RoomType odds, bool fromCombat = false) {
    CardCreationOptions o = CardCreationOptions::forRoom(characterId, odds);
    o.room = type;
    if (fromCombat) o.with(ccIsFromCombat);
    rewardItems.push_back(makeCardReward(o, 3));
  };
  makeCardItem(type, true);  // CardReward.Populate -- before the relic reward, see comment above
  if (type == RoomType::Elite) {  // GenerateRewardsFor's RelicReward, before the room's ExtraRewards
    RewardItem item;
    item.kind = RewardKind::Relic;
    item.relic = pullRelicFromFront(relicBag, rollRelicRarity(rr));
    rewardItems.push_back(std::move(item));
  }
  for (RewardKind k : extraRewards) {  // CombatRoom.ExtraRewards, populated after the room's own
    RewardItem item;
    item.kind = k;
    if (k == RewardKind::Relic) item.relic = pullRelicFromFront(relicBag, rollRelicRarity(rr));
    else if (k == RewardKind::Potion) item.potion = randomPotion(rr, false);
    else continue;
    rewardItems.push_back(std::move(item));
  }
  extraRewards.clear();
  for (auto& item : roomExtraRewards) rewardItems.push_back(std::move(item));  // AddExtraReward (Swipe, Heist)
  roomExtraRewards.clear();
  for (; bonusCardRewards > 0; --bonusCardRewards) makeCardItem(type);  // CombatRoom.AddExtraReward (TheHunt)

  if (type == RoomType::Elite) {
    int bonus = 0;
    for (auto& rel : relics) bonus += rel->bonusRelicRewards(RoomType::Elite);  // Black Star (hook-added, after)
    for (int i = 0; i < bonus; ++i) {
      RewardItem b;
      b.kind = RewardKind::Relic;
      b.relic = pullRelicFromFront(relicBag, rollRelicRarity(rr));
      rewardItems.push_back(std::move(b));
    }
  } else if (type != RoomType::Boss) {
    // TryModifyRewards on any other combat room (Wongo's Mystery Ticket): extra RelicRewards.
    int bonus = 0;
    for (auto& rel : relics) bonus += rel->bonusRelicRewards(type);
    for (int i = 0; i < bonus; ++i) {
      RewardItem b;
      b.kind = RewardKind::Relic;
      b.relic = pullRelicFromFront(relicBag, rollRelicRarity(rr));
      rewardItems.push_back(std::move(b));
    }
  }
  for (auto& rel : extraRewardRelics) {  // EnterCombatWithoutExitingEvent's extra RelicRewards
    RewardItem item;
    item.kind = RewardKind::Relic;
    item.relic = std::move(rel);
    rewardItems.push_back(std::move(item));
  }
  extraRewardRelics.clear();
  // Amethyst Aubergine: TryModifyRewards adds its own flat GoldReward, not a bonus folded into
  // the base one -- so it is its own row here too.
  if (!finalBoss) for (auto& rel : relics) {
    int extra = rel->extraCombatGold(type);
    if (extra > 0) { RewardItem g; g.kind = RewardKind::Gold; g.gold = extra; rewardItems.push_back(std::move(g)); }
  }
  for (auto& rel : relics)
    for (RoomType odds : rel->extraCardRewards(type)) makeCardItem(odds);  // Prayer Wheel / White Star
  // Hook.ModifyRewards, TryModifyRewardsLate (M11): Vintage turns a monster room's card rewards into
  // relic rewards (populated after the rest, in place); Midas doubles every gold reward.
  for (auto& m : modifiers) {
    if (m->id == "Vintage" && type == RoomType::Monster)
      for (auto& item : rewardItems)
        if (item.kind == RewardKind::Card) {
          item.kind = RewardKind::Relic;
          item.cards.clear();
          item.relic = pullRelicFromFront(relicBag, rollRelicRarity(rr));
        }
    if (m->id == "Midas")
      for (auto& item : rewardItems)
        if (item.kind == RewardKind::Gold) item.gold *= 2;
  }

  co_await offerRewards(std::move(rewardItems), true);  // sorted by RewardsSetIndex, Driftwood, the claim loop
}

// UnknownMapPointOdds.Roll (single player, no blacklist): Monster 10%, Treasure 2%,
// Shop 3%, else Event; the rolled type resets to its base odds, the others grow by theirs.
RoomType Run::rollUnknownRoom() {
  float roll = rng("UnknownMapPoint").nextFloat();
  RoomType result = RoomType::Unknown;  // event
  float sum = 0;
  // Monster, Elite (-1: never, unless DeadlyEvents set it), Treasure, Shop; negative odds are skipped.
  const std::pair<RoomType, float*> odds[] = {{RoomType::Monster, &unknownMonsterOdds},
                                              {RoomType::Elite, &unknownEliteOdds},
                                              {RoomType::Treasure, &unknownTreasureOdds},
                                              {RoomType::Shop, &unknownShopOdds}};
  // Hook.ModifyUnknownMapPointRoomTypes (JuzuBracelet: no Monster; LanternKey: only Event) on
  // {Monster, Elite, Treasure, Shop, Event}; Event (Unknown here) wins when allowed, else the first.
  int types = roomBit(RoomType::Monster) | roomBit(RoomType::Elite) | roomBit(RoomType::Treasure) |
              roomBit(RoomType::Shop) | roomBit(RoomType::Unknown);
  for (Model* m : listeners()) types = m->modifyUnknownMapPointRoomTypes(types);
  if (!(types & roomBit(RoomType::Unknown)))
    for (auto& [t, p] : odds) if (types & roomBit(t)) { result = t; break; }
  for (auto& [t, p] : odds) {
    if (!(types & roomBit(t)) || *p < 0) continue;
    sum += *p;
    if (roll <= sum) { result = t; break; }
  }
  const bool deadly = hasModifier("DeadlyEvents");
  const float base[] = {0.1f, deadly ? 0.1f : -1.f, 0.02f, 0.03f};
  for (int i = 0; i < 4; ++i) {
    if (odds[i].first == result) { *odds[i].second = base[i]; continue; }
    if (!(types & roomBit(odds[i].first))) continue;  // only the allowed types grow
    if (*odds[i].second < 0 && base[i] < 0) continue;  // Elite without DeadlyEvents stays at -1
    // Hook.ModifyOddsIncreaseForUnrolledRoomType: DeadlyEvents doubles the treasure increase.
    float inc = deadly && odds[i].first == RoomType::Treasure ? base[i] * 2 : base[i];
    *odds[i].second += inc;
  }
  return result;
}

Task<> Run::gainGold(int amount) {
  Dec a = amount;
  for (Model* m : listeners()) a = m->modifyGoldGained(a);
  int n = std::max(0, a.toInt());
  gold += n;
  if (!mapHistory.empty() && !mapHistory.back().empty()) mapHistory.back().back().goldGained += n;  // PlayerCmd.GainGold
  for (Model* m : listeners()) co_await m->afterGoldGained(n);
}

// StartRunLobby.BeginRunLocally: a player who picked RandomCharacter gets
// rng.NextItem(ModelDb.AllCharacters) from the same Rng(seed, "act_selection") that has just
// rolled the act list (ActModel.GetRandomList), so the roll is reproducible from the seed.
std::string Run::resolveCharacter(uint64_t seed, const std::string& charId) {
  if (charId != kRandomCharacter) return charId;
  Rng rng(seed, "act_selection");
  db::randomActList(rng);
  return rng.nextItem(db::allCharacters());
}

void Run::start(uint64_t s, const std::string& charId, int ascensionLevel) {
  db::init();
  seed = s;
  if (const char* env = getenv("STS_ASCENSION")) ascensionLevel = std::atoi(env);  // debug: STS_ASCENSION=0-10
  ascension = std::clamp(ascensionLevel, 0, 10);
  characterId = db::character(resolveCharacter(seed, charId)).id;  // unknown ids fall back to the Ironclad
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
  // Guard: an id that is not registered is skipped (every character is complete:
  // db::characterPlayable).
  for (auto& id : ch.starterDeck) if (auto c = db::card(id)) { progress::markCardSeen(c->id); deck.push_back(std::move(c)); }
  relics.clear();
  for (auto& id : ch.startingRelics) {
    auto rel = db::relic(id);
    if (!rel) continue;
    rel->run = this;
    progress::markRelicSeen(rel->id);
    relics.push_back(std::move(rel));
  }
  // ModifierModel.OnRunCreated (M11): ClearsPlayerDeck empties the starting deck, before the
  // ascension effects (RunManager.InitializeNewRun), so Ascender's Bane stays.
  for (auto& m : modifiers) m->run = this;
  if (modifiersClearDeck()) deck.clear();
  // AscensionManager.ApplyEffectsTo: AscendersBane goes into the starting deck (TightBelt is the potion slot above).
  if (hasAscension(kAscendersBane))
    if (auto bane = db::card("AscendersBane")) deck.push_back(std::move(bane));
  populateRelicBags();
  unknownEliteOdds = -1.f;
  modifiers::afterRunCreated(*this);  // DeadlyEvents (after the relic bags, as in the C#)
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
  // StartRunLobby.BeginRunLocally: the run's acts, ActModel.GetRandomList(Rng(seed,
  // "act_selection")). Debug: STS_ACT1=Underdocks|Overgrowth forces act 1 after the roll,
  // like the lobby's Act1 setting (list[0] = GetAct(Act1) ?? list[0]).
  {
    Rng actRng(seed, "act_selection");
    actIds = db::randomActList(actRng);
    if (const char* a1 = getenv("STS_ACT1"))
      if (const db::ActDef* d = db::act(a1); d && d->index == 0) actIds[0] = d->name;
  }
  // Run history (M2): RunState.MapPointHistory starts empty; StartTime is the wall clock.
  mapHistory.clear();
  cccCombo = false;
  startTime = (int64_t)time(nullptr);
  runTime = 0;
  // Debug: STS_ACT=2|3 starts the run in that act.
  const char* startAct = getenv("STS_ACT");
  generateRooms();  // RunManager.GenerateRooms, after InitializeNewRun (the relic bags)
  enterAct(startAct ? std::atoi(startAct) - 1 : 0);
}

const db::ActDef& Run::act() const {
  if (actIndex >= 0 && actIndex < (int)actIds.size())
    if (const db::ActDef* d = db::act(actIds[actIndex])) return *d;
  for (auto& a : db::acts()) if (a.index == actIndex && a.isDefault) return a;
  return db::acts()[0];
}

std::string Run::actMusic() const {
  const auto& options = act().music;
  if (options.empty()) return "";
  return options[Rng(seed, "bg_music").nextInt(0, (int)options.size())];
}

namespace {

// GrabBag<EncounterModel> (Helpers/GrabBag.cs): weighted picks (every weight 1 here), removed when grabbed.
struct EncounterGrabBag {
  std::vector<std::string> entries;
  double totalWeight = 0;
  void add(const std::string& id) { entries.push_back(id); totalWeight += 1.0; }
  int grabIndex(Rng& rng) {
    double num = rng.nextDouble() * totalWeight, acc = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
      acc += 1.0;
      if (num < acc) return (int)i;
    }
    return -1;
  }
  // GrabAndRemove(rng, predicate): rolls until the predicate holds (-1 / "" if nothing matches).
  std::string grabAndRemove(Rng& rng, const std::function<bool(const std::string&)>& pred = nullptr) {
    if (pred && std::none_of(entries.begin(), entries.end(), pred)) return "";
    int i;
    do i = grabIndex(rng);
    while (pred && i >= 0 && !pred(entries[(size_t)i]));
    if (i < 0) return "";
    std::string id = entries[(size_t)i];
    totalWeight -= 1.0;
    entries.erase(entries.begin() + i);
    return id;
  }
};

bool sharesTags(const std::string& a, const std::string& b) {  // EncounterModel.SharesTagsWith
  const auto& ta = db::encounterTags(a);
  const auto& tb = db::encounterTags(b);
  for (auto& t : ta)
    if (std::find(tb.begin(), tb.end(), t) != tb.end()) return true;
  return false;
}

// ActModel.AddWithoutRepeatingTags: never the previous encounter or one sharing a tag with it,
// unless nothing else is left.
void addWithoutRepeatingTags(std::vector<std::string>& encounters, EncounterGrabBag& bag, Rng& rng) {
  std::string last = encounters.empty() ? "" : encounters.back();
  std::string id = bag.grabAndRemove(rng, [&](const std::string& e) {
    return last.empty() || (!sharesTags(e, last) && e != last);
  });
  if (id.empty()) id = bag.grabAndRemove(rng);
  if (!id.empty()) encounters.push_back(id);
}

}  // namespace

// RunManager.GenerateRooms: the shared Ancients (Darv) split over acts 2 and 3, then
// ActModel.GenerateRooms for every act -- events shuffled, NumberOfWeakEncounters weak and
// then regular fights up to BaseNumberOfRooms, 15 elites (grab bags refilled when empty, no
// repeated tags in a row), the boss, the Ancient -- and DoubleBoss's second boss for the last
// act, all from Rng.UpFront at the run start. Only registered encounters / events; a pool that
// isn't ported falls back (elites -> normal fights, boss -> elites -> normal fights).
// PORT NOTE (n/a: owner): no Epoch-locked events and no ApplyDiscoveryOrderModifications (first-run boss / encounter order).
void Run::generateRooms() {
  Rng& up = rng("UpFront");
  std::vector<std::string> shared = {"Darv"};  // UnlockState.SharedAncients
  up.shuffle(shared);
  std::vector<std::string> sharedSubset[kActs];
  for (int a = 1; a < kActs; ++a) {
    int count = up.nextInt((int)shared.size() + 1);
    sharedSubset[a].assign(shared.begin(), shared.begin() + count);
    shared.erase(shared.begin(), shared.begin() + count);
  }
  static const std::vector<std::string> actAncients[kActs] = {
      {"Neow"}, {"Orobas", "Pael", "Tezcatara"}, {"Nonupeipe", "Tanx", "Vakuu"}};
  for (int i = 0; i < kActs; ++i) {
    const db::ActDef* found = i < (int)actIds.size() ? db::act(actIds[i]) : nullptr;
    if (!found)
      for (auto& d : db::acts()) if (d.index == i && d.isDefault) { found = &d; break; }
    const db::ActDef& a = found ? *found : db::acts()[0];
    RoomSet& rs = rooms[i];
    rs = RoomSet{};
    for (auto& id : a.events) if (db::event(id)) rs.events.push_back(id);
    for (auto& id : db::sharedEvents()) if (db::event(id)) rs.events.push_back(id);
    up.shuffle(rs.events);

    std::vector<std::string> weak = registered(a.weak), normal = registered(a.normal);
    std::vector<std::string> elites = registered(a.elites), bosses = registered(a.bosses);
    if (weak.empty()) weak = normal;
    if (normal.empty()) normal = weak;
    if (normal.empty()) weak = normal = registered(db::acts()[0].normal);
    if (elites.empty()) elites = normal;
    if (bosses.empty()) bosses = registered(a.elites);
    if (bosses.empty()) bosses = normal;
    EncounterGrabBag weakBag;
    for (int n = 0; n < a.weakCount; ++n) {
      if (weakBag.entries.empty()) for (auto& id : weak) weakBag.add(id);
      addWithoutRepeatingTags(rs.normal, weakBag, up);
    }
    EncounterGrabBag regularBag;
    for (int n = a.weakCount; n < a.baseRooms; ++n) {
      if (regularBag.entries.empty()) for (auto& id : normal) regularBag.add(id);
      addWithoutRepeatingTags(rs.normal, regularBag, up);
    }
    EncounterGrabBag eliteBag;
    for (int n = 0; n < 15; ++n) {
      if (eliteBag.entries.empty()) for (auto& id : elites) eliteBag.add(id);
      addWithoutRepeatingTags(rs.elites, eliteBag, up);
    }
    rs.boss = up.nextItem(bosses);
    std::vector<std::string> ancients;  // GetUnlockedAncients + the shared subset
    for (auto& id : actAncients[i]) if (db::event(id)) ancients.push_back(id);
    for (auto& id : sharedSubset[i]) if (db::event(id)) ancients.push_back(id);
    rs.ancient = up.nextItem(ancients);
    if (i == kActs - 1 && hasAscension(kDoubleBoss)) {
      std::vector<std::string> others;
      for (auto& b : bosses) if (b != rs.boss) others.push_back(b);
      rs.secondBoss = up.nextItem(others);
    }
  }
}

// RunManager.EnterAct: the act's RoomSet (generateRooms) and a new map.
void Run::enterAct(int index) {
  actIndex = std::clamp(index, 0, kActs - 1);
  const RoomSet& rs = rooms[actIndex];
  bossId = rs.boss;
  secondBossId = rs.secondBoss;
  // The act's Ancient (act 1: Neow). Debug starts (STS_ENCOUNTER / STS_ROOM / STS_EVENT /
  // STS_NO_NEOW) skip it so scripts reach the map; STS_ANCIENT forces one.
  bool debugStart = floor == 0 && (getenv("STS_ENCOUNTER") || getenv("STS_ROOM") || getenv("STS_EVENT") || getenv("STS_NO_NEOW"));
  ancientId = debugStart ? "" : rs.ancient;
  if (const char* forced = getenv("STS_ANCIENT"); forced && db::event(forced) && !debugStart) ancientId = forced;
  // SetActInternal: UnknownMapPointOdds.ResetToBase.
  unknownMonsterOdds = 0.1f;
  unknownTreasureOdds = 0.02f;
  unknownShopOdds = 0.03f;
  unknownEliteOdds = hasModifier("DeadlyEvents") ? 0.1f : -1.f;
  generateMap();
  currentNode = 0;  // the starting point (the Ancient's node)
  previousNode = -1;  // RunState.RemoveStaleVisitedMapCoords
  nodes[0].visited = true;
  ancientPending = !ancientId.empty();
  modifiers::afterActEntered(*this);  // Hook.AfterActEntered: CursedRun
}

// EnterMapCoord(StartingMapPoint): the Ancient event. AncientEventModel.BeforeEventStarted
// heals to full first (Neow from 0 HP, which only matters for the animation).
Task<> Run::enterAncient() {
  ancientPending = false;
  auto e = db::event(ancientId);
  if (!e) co_return;
  currentRoomCount = 1;
  ++rooms[actIndex].eventsVisited;  // an EventRoom: RoomSet.MarkVisited(Event)
  historyPoint(history::PointType::Ancient);  // an Ancient's map point resolves to an Event room
  historyRoom(history::RoomKind::Event, ancientId);
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
  // StandardActMap.CreateFor: hasSecondBoss = Act.HasSecondBoss (DoubleBoss rolled a second boss).
  nodes = generateStandardActMap(rng(stream.c_str()), actIndex, hasAscension(kSwarmingElites) ? 8 : 5,
                                 !secondBossId.empty());
  // Hook.ModifyGeneratedMap, deck cards first (E2: SpoilsMap makes act 2 a SpoilsActMap), then BigGameHunter.
  spoilsActMap = false;
  for (Model* m : listeners()) m->modifyGeneratedMap(actIndex);
  // BigGameHunter.ModifyGeneratedMap: the act is generated again from a fresh Rng(seed,
  // "act_<n>_map") with this map's unknown / rest counts, 2.5x its elites, elites free of the rules.
  if (hasModifier("BigGameHunter")) {
    MapTypeCounts counts;
    int elites = 0;
    for (auto& n : nodes) {
      counts.unknowns += n.type == RoomType::Unknown;
      counts.rests += n.type == RoomType::Rest;
      elites += n.type == RoomType::Elite;
    }
    counts.elites = (int)std::round((float)elites * 2.5f);
    counts.elitesIgnoreRules = true;
    if (spoilsActMap) {  // new SpoilsActMap(runState, override): its own fresh "spoils_map" Rng
      Rng spoils(seed, "spoils_map");
      nodes = generateSpoilsActMap(spoils, actIndex, counts.elites, &counts);
    } else {
      rngs[stream] = std::make_unique<Rng>(seed, stream);
      nodes = generateStandardActMap(*rngs[stream], actIndex, counts, !secondBossId.empty());
    }
  }
  runLateMapHooks();  // Hook.ModifyGeneratedMapLate (inside ModifyGeneratedMap) + AfterMapGenerated
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
int Run::bossNode() const {
  for (int i = 0; i < (int)nodes.size(); ++i) if (nodes[i].type == RoomType::Boss) return i;
  return -1;
}
int Run::secondBossNode() const {
  int b = bossNode();
  if (b < 0) return -1;
  for (int i = b + 1; i < (int)nodes.size(); ++i) if (nodes[i].type == RoomType::Boss) return i;
  return -1;
}
const std::string& Run::bossIdAt(int node) const {
  return node >= 0 && node == secondBossNode() && !secondBossId.empty() ? secondBossId : bossId;
}

std::vector<int> Run::pathNodes() const {
  std::vector<int> out;
  if (currentNode < 0) {
    for (int i = 0; i < (int)nodes.size(); ++i) if (nodes[i].row == 0) out.push_back(i);
  } else {
    out = nodes[currentNode].next;
    // Flight / WingedBoots (Hook.ShouldAllowFreeTravel, MapTravel): every point of the next row.
    bool freeTravel = hasModifier("Flight");
    for (Model* m : const_cast<Run*>(this)->listeners()) freeTravel = freeTravel || m->shouldAllowFreeTravel();
    if (freeTravel && nodes[currentNode].type != RoomType::Boss) {
      std::vector<int> row;
      for (int i = 0; i < (int)nodes.size(); ++i)
        if (nodes[i].row == nodes[currentNode].row + 1 && nodes[i].type != RoomType::Ancient) row.push_back(i);
      if (!row.empty()) out = row;
    }
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

// (decimal)rng.NextFloat(): .NET's float -> decimal conversion keeps 7 significant digits
// (DecCalc.VarDecFromR4, round half to even). The odds are multiples of 0.125, exact in a double,
// so comparing mant / 10^power with them gives the decimal comparison.
static double decimalFromFloat(float f) {
  if (f <= 0.f) return 0.0;
  uint32_t bits;
  std::memcpy(&bits, &f, sizeof bits);
  int exp = (int)((bits >> 23) & 0xFF) - 126;
  if (exp < -94) return 0.0;
  double dbl = f;
  int power = 6 - ((exp * 19728) >> 16);
  static const double kPow10[] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14,
                                  1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22, 1e23, 1e24, 1e25, 1e26, 1e27, 1e28};
  if (power >= 0) {
    if (power > 28) power = 28;
    dbl *= kPow10[power];
  } else if (power != -1 || dbl >= 1e7) {
    dbl /= kPow10[-power];
  } else {
    power = 0;
  }
  if (dbl < 1e6 && power < 28) { dbl *= 10; ++power; }
  double mant = std::nearbyint(dbl);  // round half to even
  if (power >= 0) return mant / kPow10[power];
  return mant * kPow10[-power];
}

void Run::rollCardUpgrade(Card& c, double baseChance) {
  double num = decimalFromFloat(rng("Rewards").nextFloat());
  if (!c.upgradable()) return;
  double odds = baseChance;
  if (c.rarity != Rarity::Rare) odds += actIndex * (hasAscension(kScarcity) ? 0.125 : 0.25);
  // Hook.ModifyCardRewardUpgradeOdds: no model overrides it.
  if (num <= odds) c.upgrade();
}

// The cards of a combat room's CardReward (ForRoom + IsCardReward, card_rewards.cpp) without the
// TryModifyCardRewardOptions hooks (Run::makeCardReward runs them).
std::vector<std::unique_ptr<Card>> Run::cardReward(RoomType room, int count) {
  CardCreationOptions o = CardCreationOptions::forRoom(characterId, room);
  o.room = room;
  return createForReward(o.with(ccIsCardReward | ccNoModifyHooks), count);
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

  // EncounterModel.GenerateMonstersWithSlots: its own Rng(seed + TotalFloor + hash(id)).
  Rng encounterRng(seed + (uint64_t)floor, enc->id);
  for (auto& m : enc->generate(encounterRng)) { progress::markMonsterSeen(m->id); c.createEnemy(std::move(m)); }

  screen = Screen::Combat;
  // CombatRoom.EnterInternal: Hook.AfterRoomEntered once the fight is set up.
  for (Model* m : c.listeners()) co_await m->afterRoomEntered(enc->room);
  co_await c.runCombat();
  bool won = c.won && player->alive();
  if (won || !player->alive()) progress::recordCombatEnd(c, won);  // M-stats: EnemyStats wins / losses
  if (won) achievements::afterCombatWon(*this, c);  // M5: CombatManager's achievement checks after a win
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
    graveyard.clear();
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
    previousNode = currentNode;  // RunState.VisitedMapCoords (E2: WingedBoots)
    currentNode = choice;
    currentRoomCount = 1;
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
    historyPoint(forcedEvent ? history::PointType::Unknown : historyPointType(type));
    // "?" rooms resolve when entered (UnknownMapPointOdds); Unknown afterwards means an event.
    if (forcedEvent) type = RoomType::Unknown;
    else if (type == RoomType::Unknown) {
      type = rollUnknownRoom();
      for (auto& rel : relics) co_await rel->afterUnknownRoomEntered();  // Planisphere
    }
    co_await beforeRoomEntered(type);  // RunManager.EnterRoomInternal (E2: Dowsing)
    if (type == RoomType::Unknown) {
      std::unique_ptr<Event> e;
      if (const char* id = getenv("STS_EVENT"); forcedEvent && id) e = db::event(id);
      if (!e) e = pullNextEvent();
      if (e) {
        ++rooms[actIndex].eventsVisited;  // RoomSet.MarkVisited(Event)
        historyRoom(history::RoomKind::Event, e->id);
        co_await runEvent(std::move(e));
        if (died) { recordRunEnd(*this, progress::RunOutcome::Loss); screen = Screen::GameOver; co_return; }
        if (runWon) { recordRunEnd(*this, progress::RunOutcome::Win); screen = Screen::Victory; co_return; }  // STS_EVENT=TheArchitect
        continue;
      }
    }
    if (type == RoomType::Shop) {
      historyRoom(history::RoomKind::Shop);
      co_await enterShop();
      continue;
    }
    if (type == RoomType::Unknown) {
      // Guard: the act has no registered event at all (never the case with the full event list).
      for (Model* m : listeners()) co_await m->afterRoomEntered(type);
      historyRoom(history::RoomKind::Event);
      placeholderText = "事件（尚未实现）";
      screen = Screen::Placeholder;
      co_await placeholderDone.next();
      continue;
    }

    if (type == RoomType::Monster || type == RoomType::Elite || type == RoomType::Boss) {
      std::string id;
      if (type == RoomType::Boss) id = bossIdAt(choice);  // NBossMapPoint: SecondBossEncounter at its node
      else {
        // ActModel.PullNextEncounter (RoomSet.NextNormal/EliteEncounter), then MarkVisited.
        RoomSet& rs = rooms[actIndex];
        auto& list = type == RoomType::Elite ? rs.elites : rs.normal;
        int& visited = type == RoomType::Elite ? rs.elitesVisited : rs.normalVisited;
        if (!list.empty()) id = list[(size_t)visited % list.size()];
        ++visited;
      }
      // Debug: STS_ENCOUNTER=<EncounterId> makes the first fight that encounter.
      if (const char* forced = getenv("STS_ENCOUNTER"); forced && floor == 1 && db::encounter(forced)) id = forced;
      if (!devNextEncounter.empty() && db::encounter(devNextEncounter)) { id = devNextEncounter; devNextEncounter.clear(); }

      historyRoom(historyFightKind(type), id);
      bool won = co_await fight(id);
      if (!won) { recordRunEnd(*this, progress::RunOutcome::Loss); screen = Screen::GameOver; co_return; }
      // RewardsSet.WithRewardsFromRoom: the last act's boss gives nothing. RunManager.EnterNextAct
      // on the last act then enters TheArchitect (content_architect.cpp), whose PROCEED is WinRun.
      if (type == RoomType::Boss && actIndex + 1 >= kActs) {
        // The fight is freed and the screen leaves it in the same step (as in combatRewards).
        combat.reset();
        player->combat = nullptr;
        for (auto& rel : relics) rel->combat = nullptr;
        // DoubleBoss: the first boss (no rewards, like every boss of the last act) leads back to
        // the map, where the second boss is the only next node (NRewardsScreen proceed ->
        // ProceedFromTerminalRewardsScreen). The run ends after the second boss
        // (CombatManager: CurrentMapCoord == SecondBossMapPoint).
        if (int second = secondBossNode(); second >= 0 && currentNode != second) continue;
        // PORT NOTE (n/a: owner): no save point here (saves are only made at map choices); quitting during the ending resumes at the last map save.
        screen = Screen::Event;
        // RunManager.EnterNextAct -> EnterRoom(TheArchitect): a room of the boss's map point.
        if (auto e = db::event("TheArchitect")) { historyRoom(history::RoomKind::Event, e->id); co_await runEvent(std::move(e)); }
        if (died) { recordRunEnd(*this, progress::RunOutcome::Loss); screen = Screen::GameOver; co_return; }
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
        // RunManager.EnterNextAct. The next act starts with its Ancient, which heals to full
        // (AncientEventModel.BeforeEventStarted); when there is none the heal happens here.
        enterAct(actIndex + 1);
        if (ancientId.empty()) player->hp = player->maxHp;  // stands in for the Ancient's heal
      }
    } else if (type == RoomType::Treasure) {
      // TreasureRoom: 42-52 gold, then one relic from the shared bag.
      historyRoom(history::RoomKind::Treasure);
      for (Model* m : listeners()) co_await m->afterRoomEntered(type);
      bool generate = true;  // Hook.ShouldGenerateTreasure: no relic, no gold
      for (auto& rel : relics) generate = generate && rel->shouldGenerateTreasure();
      if (generate) {
        co_await gainGold(rng("Rewards").nextInt(42, 53));
        co_await handleSpoilsMap();  // OneOffSynchronizer.DoTreasureRoomRewards (E2)
        // Debug: STS_TREASURE_RELICS=N offers a choose-one of N chest relics (the multiplayer
        // shared-relic layout; single player always gets one).
        int n = 1;
        if (const char* e = getenv("STS_TREASURE_RELICS")) n = std::clamp(atoi(e), 1, 5);
        std::vector<std::unique_ptr<Relic>> offer;
        for (int i = 0; i < n; ++i)
          offer.push_back(pullRelicFromFront(sharedRelicBag, rollRelicRarity(rng("TreasureRoomRelics"))));
        co_await chooseRelic(std::move(offer), true);
      }
    } else if (type == RoomType::Rest) {
      historyRoom(history::RoomKind::RestSite);
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
    if (hasModifier("Midas")) restOptions = {0};  // Midas.TryModifyRestSiteOptions: no Smith
    if (girya && girya->displayAmount() < 3) restOptions.push_back(2);
    if (hasRelic("Shovel")) restOptions.push_back(3);
    if (hasRelic("MeatCleaver")) restOptions.push_back(4);   // CookRestSiteOption
    if (hasRelic("PumpkinCandle")) restOptions.push_back(5); // KindleRestSiteOption
    if (hasRelic("PaelsGrowth")) restOptions.push_back(6);   // CloneRestSiteOption
    for (Model* m : listeners()) m->tryModifyRestSiteOptions(restOptions);  // E2: ByrdonisEgg's Hatch (7)
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
      // Hook.ModifyRestSiteHealRewards (TinyMailbox, DreamCatcher), then RewardsCmd.OfferCustom.
      restSiteHealRewards.clear();
      for (auto& rel : relics) rel->modifyRestSiteHealRewards();
      co_await offerRewards(std::move(restSiteHealRewards));
      restSiteHealRewards.clear();
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
    } else if (opt == 6) {  // CloneRestSiteOption: copy every deck card enchanted with Clone
      std::vector<Card*> cloned;
      for (auto& card : deck) if (card->enchantment && card->enchantment->id == "Clone") cloned.push_back(card.get());
      for (Card* card : cloned) addCardToDeck(card->clone());
      co_await wait(0.4);
      done = true;
    } else if (opt == 7) {  // HatchRestSiteOption (E2): obtain Byrdpip
      auto pip = db::relic("Byrdpip");
      if (!pip) continue;
      co_await obtainRelic(std::move(pip));
      done = true;
    } else if (opt == 3) {  // DigRestSiteOption: a relic from the front of the bag
      co_await offerRelic(pullRelicFromFront(relicBag, rollRelicRarity(rng("Rewards"))), false);
      done = true;
    }
    if (!done) continue;
    restUsed.push_back(opt);
    badges::noteRestChoice(*this, opt);  // RestSiteChoices (M7, RESTFUL / RESTLESS)
    bool tent = hasRelic("MiniatureTent");
    if (!tent) break;
  }
  restOptions.clear();
}

// Player-initiated abandon (pause menu -> confirm, ui/screens/pause.cpp).
void Run::abandon() { recordRunEnd(*this, progress::RunOutcome::Abandon); }

}  // namespace sts
