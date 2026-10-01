// Shared events 1/3 (A7a), translated from MegaCrit.Sts2.Core.Models.Events: DollRoom,
// PotionCourier, SelfHelpBook, StoneOfAllTime, TheFutureOfPotions, plus what they hand out:
// the relics DaughterOfTheWind / MrStruggles / BingBong, the FoulPotion and the Nimble enchantment.
#include <algorithm>

#include "cards.h"

namespace sts {

namespace {
template <class E> void reg() { db::registerEvent(E::kId, [] { return std::unique_ptr<Event>(new E()); }); }
#define EVENT_HEADER(Name, Key) \
  static constexpr const char* kId = #Name; \
  Name() { id = #Name; locKey = Key; }

int actIndex(Run& r) { return r.actIndex; }

std::string upperSnake(PotionRarity r) {
  switch (r) {
    case PotionRarity::Common: return "COMMON";
    case PotionRarity::Uncommon: return "UNCOMMON";
    case PotionRarity::Rare: return "RARE";
    case PotionRarity::Event: return "EVENT";
    default: return "TOKEN";
  }
}
}  // namespace

// ================================================================ enchantment / potion / relics

// Nimble.cs: +Amount block on a card that gains block.
struct Nimble : EnchantmentT<Nimble> {
  ENCHANTMENT_HEADER(Nimble, "NIMBLE")
  }
  bool showAmount() const override { return true; }
  bool canEnchant(const Card& c) const override { return Enchantment::canEnchant(c) && c.gainsBlock(); }
  Dec enchantBlockAdditive(Dec) override { return Dec(amount); }
};

// FoulPotion.cs (Event rarity, AnyTime): in combat 12 damage to every non-pet creature,
// the player included.
struct FoulPotion : Potion {
  POTION_HEADER(FoulPotion, "FOUL_POTION", Event, AnyTime, AllEnemies)
    addVar("Damage", 12);
    addVar("Gold", 100);
  }
  Task<> onUse(Creature*) override {
    Combat* c = run->combat.get();
    if (c && c->inProgress && !c->over) {
      std::vector<Creature*> all{run->player.get()};
      for (Creature* e : c->enemies) if (!e->petOwner && e->alive()) all.push_back(e);
      co_await cmd::damage(all, val("Damage"), kUnpowered, owner(), nullptr);
      co_return;
    }
    // Out of combat (usable only at a merchant, see passesCustomUsabilityCheck): the merchant pays
    // 100 gold; the FakeMerchant starts its fight (FakeMerchant.FoulPotionThrown, via shopChoice).
    // PORT NOTE: ShowPotionVfx / NMerchantRoom.FoulPotionThrown (splat, merchant reaction) dropped.
    if (run->currentEvent && run->currentEvent->id == "FakeMerchant") {
      run->shopChoice.fire(kFoulPotionThrow);
      co_return;
    }
    co_await run->gainGold(val("Gold").toInt());
  }
  // FoulPotion.PassesCustomUsabilityCheck / GetFoulPotionMerchantTarget: only while the merchant's
  // shop screen (or the FakeMerchant's) is up and waiting, i.e. its inventory is not being used.
  bool passesCustomUsabilityCheck() const override {
    return run && run->screen == Screen::Shop && run->shopChoice.waiting() && !run->deckChoice.active;
  }
};

// DaughterOfTheWind.cs: 1 Block for every Attack played.
struct DaughterOfTheWind : Relic {
  RELIC_HEADER(DaughterOfTheWind, "DAUGHTER_OF_THE_WIND", Event)
    addVar("Block", 1);
  }
  Task<> afterCardPlayed(const CardPlay& cp) override {
    if (cp.card->type != CardType::Attack || ownerOf(cp.card) != owner()) co_return;
    doFlash();
    co_await cmd::gainBlock(owner(), val("Block"), kUnpowered, nullptr, true);
  }
};

// MrStruggles.cs: at the start of each turn, damage to all enemies equal to the turn number.
struct MrStruggles : Relic {
  RELIC_HEADER(MrStruggles, "MR_STRUGGLES", Event)
  }
  Task<> afterPlayerTurnStart() override {
    if (!combat) co_return;
    doFlash();
    co_await cmd::damage(combat->hittableEnemies(), Dec(combat->turnNumber), kUnpowered, owner(), nullptr);
  }
};

// BingBong.cs: every card added to the deck (transformed ones too) is added a second time (at the bottom).
struct BingBong : Relic {
  RELIC_HEADER(BingBong, "BING_BONG", Event)
  }
  std::vector<Card*> cardsToSkip;  // CardsToSkip: the copies this relic made, between clone and hook
  void afterCardAddedToDeck(Card* card) override {
    auto it = std::find(cardsToSkip.begin(), cardsToSkip.end(), card);
    if (it != cardsToSkip.end()) { cardsToSkip.erase(it); return; }
    doFlash();
    std::unique_ptr<Card> copy = card->clone();
    cardsToSkip.push_back(copy.get());
    run->addCardToDeck(std::move(copy));
  }
};

// ================================================================ events

// DollRoom.cs (act 2): a random doll relic for free, or pay HP to choose from 2 / 3.
struct DollRoom : Event {
  EVENT_HEADER(DollRoom, "DOLL_ROOM")
  struct Doll { const char* relic; const char* relicKey; const char* page; };
  // Sorted by relic id (StableShuffle sorts first): BingBong, DaughterOfTheWind, MrStruggles.
  static const Doll* sorted() {
    static const Doll d[3] = {{"BingBong", "BING_BONG", "FABLE"},
                              {"DaughterOfTheWind", "DAUGHTER_OF_THE_WIND", "DAUGHTER_OF_WIND"},
                              {"MrStruggles", "MR_STRUGGLES", "MR_STRUGGLES"}};
    return d;
  }
  bool isAllowed(Run& r) override { return actIndex(r) == 1; }
  void calculateVars() override {
    addVar("TakeTimeHpLoss", 5);
    addVar("ExamineHpLoss", 15);
  }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "RANDOM", [this] { return chooseRandom(); }),
            option("INITIAL", "TAKE_SOME_TIME", [this] { return takeSomeTime(); }),
            option("INITIAL", "EXAMINE", [this] { return examine(); })};
  }
  EventOption optionFrom(const Doll& d) {
    std::string title = std::string("relics.") + d.relicKey + ".title";
    EventOption o = option("TAKE", "TAKE", [this, &d]() -> Task<> { co_await choose(d); });
    o.title = title;
    o.strVars["RelicName"] = title;
    return o;
  }
  Task<> chooseRandom() {
    // ModelDb order of _dolls: DaughterOfTheWind, MrStruggles, BingBong.
    static const int order[3] = {1, 2, 0};
    co_await choose(sorted()[order[rng().nextInt(3)]]);
  }
  std::vector<const Doll*> shuffled() {
    std::vector<const Doll*> v = {&sorted()[0], &sorted()[1], &sorted()[2]};
    rng().shuffle(v);
    return v;
  }
  Task<> takeSomeTime() {
    co_await run->loseHp(val("TakeTimeHpLoss").toInt());
    if (run->died) co_return;
    auto v = shuffled();
    std::vector<EventOption> opts;
    for (int i = 0; i < 2; ++i) opts.push_back(optionFrom(*v[(size_t)i]));
    setPage("TAKE_SOME_TIME", std::move(opts));
  }
  Task<> examine() {
    co_await run->loseHp(val("ExamineHpLoss").toInt());
    if (run->died) co_return;
    std::vector<EventOption> opts;
    for (const Doll* d : shuffled()) opts.push_back(optionFrom(*d));
    setPage("EXAMINE", std::move(opts));
  }
  Task<> choose(const Doll& d) {
    co_await run->obtainRelic(db::relic(d.relic));
    setFinished(d.page);
  }
};

// PotionCourier.cs (acts 2-3): 3 Foul Potions, or one random Uncommon potion.
struct PotionCourier : Event {
  EVENT_HEADER(PotionCourier, "POTION_COURIER")
  bool isAllowed(Run& r) override { return actIndex(r) > 0; }
  void calculateVars() override { addVar("FoulPotions", 3); }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "GRAB_POTIONS", [this] { return grabPotions(); }),
            option("INITIAL", "RANSACK", [this] { return ransack(); })};
  }
  static Run::RewardItem potionRow(std::unique_ptr<Potion> p) {
    Run::RewardItem it;
    it.kind = Run::RewardKind::Potion;
    it.potion = std::move(p);
    return it;
  }
  Task<> grabPotions() {
    std::vector<Run::RewardItem> rows;
    for (int i = 0; i < val("FoulPotions").toInt(); ++i) rows.push_back(potionRow(db::potion("FoulPotion")));
    co_await run->offerRewards(std::move(rows));
    setFinished("GRAB_POTIONS");
  }
  Task<> ransack() {
    // The character's potion pool + SharedPotionPool, Uncommon only.
    std::vector<std::string> items;
    for (auto& pid : db::potionPool(run->characterId)) {
      auto p = db::potion(pid);
      if (p && p->rarity == PotionRarity::Uncommon) items.push_back(pid);
    }
    std::string pick = run->rng("Rewards").nextItem(items);
    if (!pick.empty()) {
      std::vector<Run::RewardItem> rows;
      rows.push_back(potionRow(db::potion(pick)));
      co_await run->offerRewards(std::move(rows));
    }
    setFinished("RANSACK");
  }
};

// SelfHelpBook.cs: enchant an Attack with Sharp 2, a Skill with Nimble 2 or a Power with Swift 2.
struct SelfHelpBook : Event {
  EVENT_HEADER(SelfHelpBook, "SELF_HELP_BOOK")
  void calculateVars() override {
    setStr("Enchantment1", "enchantments.SHARP.title");
    setStr("Enchantment2", "enchantments.NIMBLE.title");
    setStr("Enchantment3", "enchantments.SWIFT.title");
    addVar("Enchantment1Amount", 2);
    addVar("Enchantment2Amount", 2);
    addVar("Enchantment3Amount", 2);
  }
  bool has(const char* ench, CardType t) {
    return run->canEnchantAny(ench, [t](Card* c) { return c->type == t; });
  }
  std::vector<EventOption> initialOptions() override {
    std::vector<EventOption> list;
    bool a = has("Sharp", CardType::Attack), b = has("Nimble", CardType::Skill), c = has("Swift", CardType::Power);
    if (a || b || c) {
      list.push_back(a ? option("INITIAL", "READ_THE_BACK", [this] { return selectAndEnchant("Sharp", CardType::Attack, "READ_THE_BACK"); })
                       : EventOption{page("INITIAL") + ".options.READ_THE_BACK_LOCKED", nullptr});
      list.push_back(b ? option("INITIAL", "READ_PASSAGE", [this] { return selectAndEnchant("Nimble", CardType::Skill, "READ_PASSAGE"); })
                       : EventOption{page("INITIAL") + ".options.READ_PASSAGE_LOCKED", nullptr});
      list.push_back(c ? option("INITIAL", "READ_ENTIRE_BOOK", [this] { return selectAndEnchant("Swift", CardType::Power, "READ_ENTIRE_BOOK"); })
                       : EventOption{page("INITIAL") + ".options.READ_ENTIRE_BOOK_LOCKED", nullptr});
    } else {
      list.push_back(option("INITIAL", "NO_OPTIONS", [this]() -> Task<> { setFinished("NO_OPTIONS"); co_return; }));
    }
    return list;
  }
  Task<> selectAndEnchant(const char* ench, CardType t, const char* finalPage) {
    auto picked = co_await run->selectForEnchantment(ench, 1, [t](Card* c) { return c->type == t; });
    if (!picked.empty()) run->enchantCard(picked[0], ench, 2);
    setFinished(finalPage);
  }
};

// StoneOfAllTime.cs (act 2, needs a potion): drink a random potion for +10 max HP, or take
// 6 damage to give a card Vigorous 8. The belt can't be used or emptied meanwhile (CanUseOrRemovePotions).
struct StoneOfAllTime : Event {
  EVENT_HEADER(StoneOfAllTime, "STONE_OF_ALL_TIME")
  Task<> onStart() override { run->canUseOrRemovePotions = false; co_return; }  // BeforeEventStarted
  void onEventFinished() override { run->canUseOrRemovePotions = true; }
  Potion* drinkPotion = nullptr;
  bool isAllowed(Run& r) override {
    if (actIndex(r) != 1) return false;
    for (auto& p : r.potions) if (p) return true;
    return false;
  }
  void calculateVars() override {
    setStr("DrinkRandomPotion", "");
    addVar("DrinkMaxHpGain", 10);
    addVar("PushHpLoss", 6);
    addVar("PushVigorousAmount", 8);
  }
  std::vector<EventOption> initialOptions() override {
    std::vector<Potion*> held;
    for (auto& p : run->potions) if (p) held.push_back(p.get());
    drinkPotion = rng().nextItem(held);
    EventOption lift = EventOption{page("INITIAL") + ".options.LIFT_LOCKED", nullptr};
    if (drinkPotion) {
      setStr("DrinkRandomPotion", "potions." + drinkPotion->locKey + ".title");
      lift = option("INITIAL", "LIFT", [this] { return doLift(); });
    }
    EventOption push = run->canEnchantAny("Vigorous")
                           ? option("INITIAL", "PUSH", [this] { return doPush(); })
                           : EventOption{page("INITIAL") + ".options.PUSH_LOCKED", nullptr};
    return {std::move(lift), std::move(push)};
  }
  Task<> doLift() {
    for (size_t i = 0; i < run->potions.size(); ++i)
      if (run->potions[i].get() == drinkPotion) { run->discardPotion((int)i); break; }
    co_await run->gainMaxHp(val("DrinkMaxHpGain").toInt());
    rng().nextInt(100);
    setFinished("LIFT");
  }
  Task<> doPush() {
    co_await run->loseHp(val("PushHpLoss").toInt());
    if (run->died) co_return;
    auto picked = co_await run->selectForEnchantment("Vigorous", 1);
    for (Card* c : picked) run->enchantCard(c, "Vigorous", val("PushVigorousAmount").toInt());
    rng().nextInt(100);
    setFinished("PUSH");
  }
};

// TheFutureOfPotions.cs (needs 2+ potions): trade one of the first 3 potions for a card reward
// of 3 upgraded cards of the matching rarity and a random type. The belt can't be used or emptied
// meanwhile (CanUseOrRemovePotions).
struct TheFutureOfPotions : Event {
  EVENT_HEADER(TheFutureOfPotions, "THE_FUTURE_OF_POTIONS")
  Task<> onStart() override { run->canUseOrRemovePotions = false; co_return; }  // BeforeEventStarted
  void onEventFinished() override { run->canUseOrRemovePotions = true; }
  std::vector<std::pair<Potion*, CardType>> types;  // PotionToCardType, belt order
  bool isAllowed(Run& r) override {
    int n = 0;
    for (auto& p : r.potions) if (p) ++n;
    return n >= 2;
  }
  static Rarity cardRarity(const Potion& p) {
    switch (p.rarity) {
      case PotionRarity::Rare: case PotionRarity::Event: return Rarity::Rare;
      case PotionRarity::Uncommon: return Rarity::Uncommon;
      default: return Rarity::Common;
    }
  }
  CardType typeOf(Potion* p) {
    for (auto& t : types) if (t.first == p) return t.second;
    return CardType::Attack;
  }
  std::vector<EventOption> initialOptions() override {
    std::vector<Potion*> held;
    for (auto& p : run->potions) if (p) held.push_back(p.get());
    for (Potion* p : held) {  // PotionToCardType: built for every potion on the first access
      std::vector<CardType> list = {CardType::Attack, CardType::Skill, CardType::Power};
      if (p->rarity == PotionRarity::Common || p->rarity == PotionRarity::Token)
        list.erase(std::remove(list.begin(), list.end(), CardType::Power), list.end());
      types.push_back({p, rng().nextItem(list)});
    }
    std::vector<EventOption> out;
    for (size_t i = 0; i < held.size() && i < 3; ++i) {
      Potion* p = held[i];
      EventOption o = option("INITIAL", "POTION", [this, p] { return trade(p); });
      o.strVars["Rarity"] = "gameplay_ui.POTION_RARITY." + upperSnake(p->rarity);
      o.strVars["Potion"] = "potions." + p->locKey + ".title";
      static const char* cardRarityKey[] = {"BASIC", "COMMON", "UNCOMMON", "RARE"};
      Rarity cr = cardRarity(*p);
      o.strVars["desc.Rarity"] = std::string("gameplay_ui.CARD_RARITY.") + cardRarityKey[cr == Rarity::Common ? 1 : cr == Rarity::Uncommon ? 2 : 3];
      const char* ty = typeOf(p) == CardType::Attack ? "ATTACK" : typeOf(p) == CardType::Skill ? "SKILL" : "POWER";
      o.strVars["Type"] = std::string("gameplay_ui.CARD_TYPE.") + ty;
      out.push_back(std::move(o));
    }
    return out;
  }
  Task<> trade(Potion* potion) {
    Rarity target = cardRarity(*potion);
    CardType type = typeOf(potion);
    for (size_t i = 0; i < run->potions.size(); ++i)
      if (run->potions[i].get() == potion) { run->discardPotion((int)i); break; }
    // CardReward(ForNonCombatWithUniformOdds(character pool, rarity && type) | NoRarityModification |
    // NoCardPoolModifications, 3); AfterGenerated (every populate, rerolls too) upgrades the cards.
    auto o = CardCreationOptions::forNonCombat({run->characterId}, true, [target, type](const Card& c) {
      return c.rarity == target && c.type == type;
    });
    o.with(ccNoRarityModification | ccNoCardPoolModifications);
    std::vector<Run::RewardItem> rows;
    rows.push_back(run->makeCardReward(o, 3, [](std::vector<std::unique_ptr<Card>>& cards) {
      for (auto& c : cards) if (c && c->upgradable()) c->upgrade();
    }));
    if (rows.back().cards.empty()) rows.clear();
    co_await run->offerRewards(std::move(rows));
    setFinished("DONE");
  }
};

void registerSharedEvents2() {
  db::registerEnchantment(Nimble::kId, [] { return std::unique_ptr<Enchantment>(new Nimble()); });
  db::registerPotion(FoulPotion::kId, [] { return std::unique_ptr<Potion>(new FoulPotion()); });
  db::registerRelic(DaughterOfTheWind::kId, [] { return std::unique_ptr<Relic>(new DaughterOfTheWind()); });
  db::registerRelic(MrStruggles::kId, [] { return std::unique_ptr<Relic>(new MrStruggles()); });
  db::registerRelic(BingBong::kId, [] { return std::unique_ptr<Relic>(new BingBong()); });
  reg<DollRoom>();
  reg<PotionCourier>();
  reg<SelfHelpBook>();
  reg<StoneOfAllTime>();
  reg<TheFutureOfPotions>();
}

}  // namespace sts
