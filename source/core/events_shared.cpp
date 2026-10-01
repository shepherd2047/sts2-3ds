// Shared events (ModelDb.AllSharedEvents), translated from MegaCrit.Sts2.Core.Models.Events.
// Not ported (need systems that do not exist yet): CrystalSphere (own minigame UI), DollRoom
// (dynamic option text + BingBong deck hook), FakeMerchant (shop), PotionCourier,
// TheFutureOfPotions, TheLegendsWereTrue (potions, SpoilsMap), SelfHelpBook, StoneOfAllTime
// (enchantments, potions), WarHistorianRepy (IsAllowed is false in C#), WelcomeToWongos
// (shop relics, Wongo badge progress).
#include <algorithm>

#include "cards.h"
#include "colorless.h"

namespace sts {

namespace {
template <class E> void reg() { db::registerEvent(E::kId, [] { return std::unique_ptr<Event>(new E()); }); }
#define EVENT_HEADER(Name, Key) \
  static constexpr const char* kId = #Name; \
  Name() { id = #Name; locKey = Key; }

// RunState.CurrentActIndex.
int actIndex(Run& r) { return r.actIndex; }

// RelicModel.HasUponPickupEffect (the relics that override it to true) and SpawnsPets.
bool hasUponPickupEffect(const std::string& id) {
  static const char* const kIds[] = {
      "PaelsTooth", "Cauldron", "YummyCookie", "NeowsTalisman", "Whetstone", "GnarledHammer", "DollysMirror",
      "RoyalStamp", "Kaleidoscope", "BloodSoakedRose", "PreciseScissors", "GoldenPearl", "NeowsSacrifice",
      "PhialHolster", "SereTalon", "LoomingFruit", "Strawberry", "CallingBell", "Byrdpip", "NewLeaf",
      "FakeLeesWaffle", "PunchDagger", "PandorasBox", "SandCastle", "AlchemicalCoffer", "Astrolabe",
      "LostCoffer", "NeowsTorment", "ToyBox", "Kifuda", "FragrantMushroom", "HeftyTablet", "BigMushroom",
      "Mango", "ElectricShrymp", "PrecariousShears", "PotionBelt", "JewelryBox", "SeaGlass", "GoldenCompass",
      "NutritiousOyster", "FakeMango", "EmptyCage", "Orrery", "DistinguishedCape", "DowsingRod", "WarPaint",
      "OldCoin", "Pear", "LeesWaffle"};
  for (const char* k : kIds) if (id == k) return true;
  return false;
}
bool spawnsPets(const std::string& id) { return id == "Byrdpip" || id == "PhylacteryUnbound" || id == "BoundPhylactery"; }

// RelicModel.IsTradable (no relic melts in this port).
bool isTradable(const Relic& r) {
  if (r.usedUp || hasUponPickupEffect(r.id) || spawnsPets(r.id)) return false;
  return !(r.rarity == RelicRarity::Starter || r.rarity == RelicRarity::Event || r.rarity == RelicRarity::Ancient);
}
std::vector<Relic*> tradableRelics(Run& r) {
  std::vector<Relic*> out;
  for (auto& rel : r.relics) if (isTradable(*rel)) out.push_back(rel.get());
  return out;
}

// RelicFactory.PullNextRelicFromFront(owner): roll a rarity, take the next relic of it.
std::unique_ptr<Relic> pullRelic(Run& r) {
  return r.pullRelicFromFront(r.relicBag, r.rollRelicRarity(r.rng("Rewards")));
}

// RelicCmd.Remove. RelicModel.AfterRemoved has no overrides in the C# (max HP relics etc. keep their
// bonus there too), so there is nothing to call after taking it off.
void removeRelic(Run& r, Relic* rel) {
  r.relics.erase(std::remove_if(r.relics.begin(), r.relics.end(),
                                [&](const std::unique_ptr<Relic>& x) { return x.get() == rel; }),
                 r.relics.end());
}

// CardSelectCmd over generated cards (SelectCardsToAddToDeckFromGrid): the picked cards join the deck.
Task<> pickCardsForDeck(Run& r, std::vector<std::unique_ptr<Card>> cards, std::string prompt, int count) {
  std::vector<Card*> opts;
  for (auto& c : cards) opts.push_back(c.get());
  if (opts.empty()) co_return;
  r.deckChoice.prompt = std::move(prompt);
  r.deckChoice.options = std::move(opts);
  r.deckChoice.count = count;
  r.deckChoice.canCancel = false;
  r.deckChoice.showUpgrade = false;
  r.deckChoice.active = true;
  auto picked = co_await r.deckChoice.result.next();
  r.deckChoice.active = false;
  for (Card* p : picked)
    for (auto& c : cards)
      if (c.get() == p) { r.addCardToDeck(std::move(c)); break; }
}
}  // namespace

// ================================================================ cards / relics

// Clumsy.cs: Unplayable, Ethereal curse.
struct Clumsy : IroncladT<Clumsy> {
  CARD_HEADER(Clumsy, "CLUMSY", -1, Curse, Curse, None)
    keywords = kwEthereal | kwUnplayable;
    maxUpgradeLevel = 0;
  }
};

// ChosenCheese.cs: +1 max HP after every combat.
struct ChosenCheese : Relic {
  RELIC_HEADER(ChosenCheese, "CHOSEN_CHEESE", Event)
    addVar("MaxHp", 1);
  }
  Task<> afterCombatEnd() override {
    doFlash();
    co_await cmd::gainMaxHp(owner(), val("MaxHp").toInt());
  }
};

// BoneTea.cs: upgrade the starting hand of the next combat.
struct BoneTea : Relic {
  RELIC_HEADER(BoneTea, "BONE_TEA", Event)
    addVar("Combats", 1);
  }
  int combatsLeft = 1;
  void persist(Archive& a) override { a.io(combatsLeft); }  // [SavedProperty]
  bool showCounter() const override { return false; }
  int displayAmount() const override { return std::max(0, combatsLeft); }
  Task<> afterPlayerTurnStart() override {
    if (usedUp || !combat || combat->turnNumber > 1) co_return;
    for (Card* c : std::vector<Card*>(combat->hand)) cmd::upgradeCard(c);
    if (--combatsLeft <= 0) usedUp = true;
    if (auto* v = var("Combats")) v->base = combatsLeft;
    doFlash();
  }
};

// EmberTea.cs: +2 Strength at the start of the next 5 combats.
struct EmberTea : Relic {
  RELIC_HEADER(EmberTea, "EMBER_TEA", Event)
    addVar("Combats", 5);
    addVar("StrengthPower", 2);
  }
  int combatsLeft = 5;
  void persist(Archive& a) override { a.io(combatsLeft); }  // [SavedProperty]
  bool showCounter() const override { return true; }
  int displayAmount() const override { return std::max(0, combatsLeft); }
  Task<> afterRoomEntered(RoomType room) override {
    if (usedUp || (room != RoomType::Monster && room != RoomType::Elite && room != RoomType::Boss)) co_return;
    doFlash();
    co_await applyPower<StrengthPower>(owner(), val("StrengthPower"), owner(), nullptr);
    if (--combatsLeft <= 0) usedUp = true;
    if (auto* v = var("Combats")) v->base = combatsLeft;
  }
};

// TeaOfDiscourtesy.cs: 2 Dazed shuffled into the draw pile of the next combat.
struct TeaOfDiscourtesy : Relic {
  RELIC_HEADER(TeaOfDiscourtesy, "TEA_OF_DISCOURTESY", Event)
    addVar("Heal", 1);
    addVar("Combats", 1);
    addVar("DazedCount", 2);
  }
  int combatsLeft = 1;
  void persist(Archive& a) override { a.io(combatsLeft); }  // [SavedProperty]
  bool showCounter() const override { return false; }
  Task<> beforeCombatStart() override {
    if (usedUp || !combat) co_return;
    for (int i = 0; i < val("DazedCount").toInt(); ++i) {
      Card* c = co_await cmd::addGeneratedCard(*combat, db::card("Dazed"), Pile::Draw);
      // CardPilePosition.Random: swap it to a random slot of the draw pile.
      auto& d = combat->draw;
      auto it = std::find(d.begin(), d.end(), c);
      if (it != d.end() && d.size() > 1) std::iter_swap(it, d.begin() + combat->rng("CombatCardGeneration").nextInt((int)d.size()));
    }
    if (--combatsLeft <= 0) usedUp = true;
    if (auto* v = var("Combats")) v->base = combatsLeft;
    doFlash();
  }
};

// ================================================================ events

// BrainLeech.cs (acts 1-2): pick 1 of 5 cards, or take 5 damage for a colorless card reward.
struct BrainLeech : Event {
  EVENT_HEADER(BrainLeech, "BRAIN_LEECH")
  bool isAllowed(Run& r) override { return actIndex(r) < 2; }
  void calculateVars() override {
    addVar("RipHpLoss", 5);
    addVar("RewardCount", 1);
    addVar("CardChoiceCount", 1);
    addVar("FromCardChoiceCount", 5);
  }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "SHARE_KNOWLEDGE", [this] { return shareKnowledge(); }),
            option("INITIAL", "RIP", [this] { return rip(); })};
  }
  Task<> rip() {
    co_await run->loseHp(val("RipHpLoss").toInt());
    if (run->died) co_return;
    // RewardCount times: OfferCustom(CardReward(ForNonCombatWithDefaultOdds(ColorlessCardPool) |
    // NoRarityModification | NoCardPoolModifications, 3)).
    for (int i = 0; i < val("RewardCount").toInt(); ++i) {
      auto o = CardCreationOptions::forNonCombat({CardCreationOptions::kColorless}, false);
      std::vector<Run::RewardItem> rows;
      rows.push_back(run->makeCardReward(o.with(ccNoRarityModification | ccNoCardPoolModifications), 3));
      co_await run->offerRewards(std::move(rows));
    }
    setFinished("RIP");
  }
  Task<> shareKnowledge() {
    // CardFactory.CreateForReward(FromCardChoiceCount, ForNonCombatWithDefaultOdds(character pool)).
    auto cards = run->createForReward(CardCreationOptions::forNonCombat({run->characterId}, false),
                                      val("FromCardChoiceCount").toInt());
    co_await pickCardsForDeck(*run, std::move(cards), "events." + page("SHARE_KNOWLEDGE") + ".selectionScreenPrompt", 1);
    setFinished("SHARE_KNOWLEDGE");
  }
};

// RoomFullOfCheese.cs (acts 1-2): pick 2 of 8 Commons, or 14 damage for Chosen Cheese.
struct RoomFullOfCheese : Event {
  EVENT_HEADER(RoomFullOfCheese, "ROOM_FULL_OF_CHEESE")
  bool isAllowed(Run& r) override { return actIndex(r) < 2; }
  void calculateVars() override { addVar("Damage", 14); }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "GORGE", [this] { return gorge(); }),
            option("INITIAL", "SEARCH", [this] { return search(); })};
  }
  Task<> gorge() {
    // CardCreationOptions.ForNonCombatWithUniformOdds(Common, NoRarityModification), 8 distinct cards.
    auto pool = db::characterCards(run->characterId, [](const Card& c) { return c.rarity == Rarity::Common; });
    std::vector<std::unique_ptr<Card>> cards;
    for (int i = 0; i < 8 && !pool.empty(); ++i) {
      size_t k = (size_t)rng().nextInt((int)pool.size());
      cards.push_back(db::card(pool[k]));
      pool.erase(pool.begin() + (long)k);
    }
    co_await pickCardsForDeck(*run, std::move(cards), "events." + page("GORGE") + ".selectionScreenPrompt", 2);
    setFinished("GORGE");
  }
  Task<> search() {
    co_await run->loseHp(val("Damage").toInt());
    if (run->died) co_return;
    co_await run->obtainRelic(db::relic("ChosenCheese"));
    setFinished("SEARCH");
  }
};

// Symbiote.cs (acts 2-3): enchant a card with Corrupted, or transform a card.
struct Symbiote : Event {
  EVENT_HEADER(Symbiote, "SYMBIOTE")
  bool isAllowed(Run& r) override { return actIndex(r) > 0; }
  void calculateVars() override {
    setStr("Enchantment", "enchantments.CORRUPTED.title");
    addVar("Cards", 1);
  }
  std::vector<EventOption> initialOptions() override {
    return {run->canEnchantAny("Corrupted") ? option("INITIAL", "APPROACH", [this] { return approach(); })
                                            : EventOption{page("INITIAL") + ".options.APPROACH_LOCKED", nullptr},
            option("INITIAL", "KILL_WITH_FIRE", [this] { return killWithFire(); })};
  }
  Task<> approach() {
    auto picked = co_await run->selectForEnchantment("Corrupted", 1);
    if (!picked.empty()) run->enchantCard(picked[0], "Corrupted", 1);
    setFinished("APPROACH");
  }
  Task<> killWithFire() {
    auto picked = co_await run->selectFromDeck("card_selection.TO_TRANSFORM", [](Card*) { return true; }, val("Cards").toInt());
    for (Card* c : picked) run->transformCard(c, run->randomTransformFor(c, rng()));
    setFinished("KILL_WITH_FIRE");
  }
};

// TeaMaster.cs (acts 1-2, needs 150 gold): buy one of three teas.
struct TeaMaster : Event {
  EVENT_HEADER(TeaMaster, "TEA_MASTER")
  bool isAllowed(Run& r) override { return actIndex(r) < 2 && r.gold >= 150; }
  void calculateVars() override {
    addVar("BoneTeaCost", 50);
    addVar("EmberTeaCost", 150);
    // StringVar(ModelDb.Relic<X>().DynamicDescription.GetFormattedText()): "@relic:<Id>" makes the
    // UI's expandSmart (ui/cardtext.cpp) expand that relic's description with the relic's own vars.
    setStr("BoneTeaDescription", "@relic:BoneTea");
    setStr("EmberTeaDescription", "@relic:EmberTea");
    setStr("TeaOfDiscourtesyDescription", "@relic:TeaOfDiscourtesy");
  }
  std::vector<EventOption> initialOptions() override {
    std::vector<EventOption> o;
    o.push_back(run->gold >= 50 ? option("INITIAL", "BONE_TEA", [this] { return buy("BoneTea", 50); })
                                : EventOption{page("INITIAL") + ".options.BONE_TEA_LOCKED", nullptr});
    o.push_back(run->gold >= 150 ? option("INITIAL", "EMBER_TEA", [this] { return buy("EmberTea", 150); })
                                 : EventOption{page("INITIAL") + ".options.EMBER_TEA_LOCKED", nullptr});
    o.push_back(option("INITIAL", "TEA_OF_DISCOURTESY", [this]() -> Task<> {
      co_await run->obtainRelic(db::relic("TeaOfDiscourtesy"));
      setFinished("TEA_OF_DISCOURTESY");
    }));
    return o;
  }
  Task<> buy(const char* relic, int cost) {
    run->gold -= cost;
    co_await run->obtainRelic(db::relic(relic));
    setFinished("DONE");
  }
};

// ThisOrThat.cs: 6 HP for gold, or a relic and the Clumsy curse.
struct ThisOrThat : Event {
  EVENT_HEADER(ThisOrThat, "THIS_OR_THAT")
  void calculateVars() override {
    addVar("HpLoss", 6);
    addVar("Gold", rng().nextInt(41, 69));
    setStr("Curse", "cards.CLUMSY.title");
  }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "PLAIN", [this] { return plain(); }),
            option("INITIAL", "ORNATE", [this] { return ornate(); })};
  }
  Task<> plain() {
    co_await run->loseHp(val("HpLoss").toInt());
    if (run->died) co_return;
    co_await run->gainGold(val("Gold").toInt());
    setFinished("PLAIN");
  }
  Task<> ornate() {
    co_await run->obtainRelic(pullRelic(*run));
    run->addCardToDeck(db::card("Clumsy"));
    setFinished("ORNATE");
  }
};

// RanwidTheElder.cs (acts 2-3): trade a potion, 100 gold or a relic for a relic.
struct RanwidTheElder : Event {
  EVENT_HEADER(RanwidTheElder, "RANWID_THE_ELDER")
  Task<> onStart() override { run->canUseOrRemovePotions = false; co_return; }  // BeforeEventStarted
  void onEventFinished() override { run->canUseOrRemovePotions = true; }
  bool isAllowed(Run& r) override {
    bool potion = false;
    for (auto& p : r.potions) if (p) potion = true;
    return actIndex(r) > 0 && !tradableRelics(r).empty() && r.gold >= 100 && potion;
  }
  void calculateVars() override {
    addVar("Gold", 100);
    setStr("Potion", "Potion");
    setStr("Relic", "Relic");
  }
  std::vector<EventOption> initialOptions() override {
    std::vector<EventOption> o;
    std::vector<Potion*> held;
    for (auto& p : run->potions) if (p) held.push_back(p.get());
    Potion* potion = rng().nextItem(held);
    if (potion) {
      setStr("Potion", "potions." + potion->locKey + ".title");
      o.push_back(option("INITIAL", "POTION", [this, potion]() -> Task<> {
        for (size_t i = 0; i < run->potions.size(); ++i)
          if (run->potions[i].get() == potion) { run->discardPotion((int)i); break; }
        co_await run->obtainRelic(pullRelic(*run));
        setFinished("POTION");
      }));
    } else {
      o.push_back(EventOption{page("INITIAL") + ".options.POTION_LOCKED", nullptr});
    }
    o.push_back(option("INITIAL", "GOLD", [this]() -> Task<> {
      run->gold -= val("Gold").toInt();
      co_await run->obtainRelic(pullRelic(*run));
      setFinished("GOLD");
    }));
    auto valid = tradableRelics(*run);
    if (!valid.empty()) {
      Relic* rel = rng().nextItem(valid);
      setStr("Relic", "relics." + rel->locKey + ".title");
      o.push_back(option("INITIAL", "RELIC", [this, rel]() -> Task<> {
        removeRelic(*run, rel);
        for (int i = 0; i < 2; ++i) co_await run->obtainRelic(pullRelic(*run));
        setFinished("RELIC");
      }));
    } else {
      o.push_back(EventOption{page("INITIAL") + ".options.RELIC_LOCKED", nullptr});
    }
    return o;
  }
};

// RelicTrader.cs (acts 2-3, needs 5 tradable relics): swap one of 3 owned relics for a new one.
struct RelicTrader : Event {
  EVENT_HEADER(RelicTrader, "RELIC_TRADER")
  std::vector<Relic*> owned;
  std::vector<std::unique_ptr<Relic>> fresh;
  bool isAllowed(Run& r) override { return actIndex(r) > 0 && tradableRelics(r).size() >= 5; }
  void calculateVars() override {
    auto valid = tradableRelics(*run);
    rng().shuffle(valid);
    for (size_t i = 0; i < valid.size() && i < 3; ++i) owned.push_back(valid[i]);
    for (int i = 0; i < 3; ++i) fresh.push_back(pullRelic(*run));
    static const char* names[] = {"Top", "Middle", "Bottom"};
    for (size_t i = 0; i < owned.size() && i < fresh.size(); ++i) {
      setStr(std::string(names[i]) + "RelicOwned", "relics." + owned[i]->locKey + ".title");
      setStr(std::string(names[i]) + "RelicNew", "relics." + fresh[i]->locKey + ".title");
    }
  }
  std::vector<EventOption> initialOptions() override {
    static const char* keys[] = {"TOP", "MIDDLE", "BOTTOM"};
    std::vector<EventOption> o;
    for (size_t i = 0; i < owned.size() && i < fresh.size(); ++i)
      o.push_back(option("INITIAL", keys[i], [this, i] { return trade((int)i); }));
    if (o.empty()) setFinished("DONE");  // C# offers PROCEED here (only reachable when forced)
    return o;
  }
  Task<> trade(int i) {
    removeRelic(*run, owned[(size_t)i]);
    co_await run->obtainRelic(std::move(fresh[(size_t)i]));
    setFinished("DONE");
  }
};

// SlipperyBridge.cs (after floor 6): lose a random card, or hold on and take growing damage.
struct SlipperyBridge : Event {
  EVENT_HEADER(SlipperyBridge, "SLIPPERY_BRIDGE")
  int holdOns = 0;
  Card* cardToLose = nullptr;
  std::vector<Card*> skipped;
  int hpLoss() const { return 3 + holdOns; }
  bool isAllowed(Run& r) override { return r.floor > 6 && !r.deck.empty(); }
  void calculateVars() override { addVar("HpLoss", 3); }
  void newRandomCard() {
    std::vector<Card*> list;
    if (!cardToLose) {
      for (auto& c : run->deck) if (c->rarity != Rarity::Basic) list.push_back(c.get());
    } else {
      skipped.push_back(cardToLose);
      for (auto& c : run->deck) if (c->id != cardToLose->id) list.push_back(c.get());
    }
    list.erase(std::remove_if(list.begin(), list.end(), [&](Card* c) {
      return std::find(skipped.begin(), skipped.end(), c) != skipped.end();
    }), list.end());
    if (list.empty()) for (auto& c : run->deck) list.push_back(c.get());
    cardToLose = rng().nextItem(list);
    setStr("RandomCard", "cards." + cardToLose->locKey + ".title");
  }
  std::vector<EventOption> initialOptions() override {
    newRandomCard();
    return {option("INITIAL", "OVERCOME", [this] { return overcome(); }),
            option("INITIAL", "HOLD_ON_0", [this] { return holdOn(); })};
  }
  static std::string suffix(int n) { return n >= 7 ? "LOOP" : std::to_string(n); }
  Task<> overcome() {
    run->removeCardFromDeck(cardToLose);
    setFinished("OVERCOME");
    co_return;
  }
  Task<> holdOn() {
    co_await run->loseHp(hpLoss());
    if (run->died) co_return;
    ++holdOns;
    setVar("HpLoss", hpLoss());
    newRandomCard();
    std::string cur = suffix(holdOns - 1), next = suffix(holdOns);
    setPage("HOLD_ON_" + cur, {option("INITIAL", "OVERCOME", [this] { return overcome(); }),
                               option("HOLD_ON_" + cur, "HOLD_ON_" + next, [this] { return holdOn(); })});
  }
};

void registerSharedEvents() {
  db::registerCard("Clumsy", [] { return std::unique_ptr<Card>(new Clumsy()); });
  db::registerRelic(ChosenCheese::kId, [] { return std::unique_ptr<Relic>(new ChosenCheese()); });
  db::registerRelic(BoneTea::kId, [] { return std::unique_ptr<Relic>(new BoneTea()); });
  db::registerRelic(EmberTea::kId, [] { return std::unique_ptr<Relic>(new EmberTea()); });
  db::registerRelic(TeaOfDiscourtesy::kId, [] { return std::unique_ptr<Relic>(new TeaOfDiscourtesy()); });
  reg<BrainLeech>();
  reg<RoomFullOfCheese>();
  reg<Symbiote>();
  reg<TeaMaster>();
  reg<ThisOrThat>();
  reg<RanwidTheElder>();
  reg<RelicTrader>();
  reg<SlipperyBridge>();
}

}  // namespace sts
