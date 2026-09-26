// Ancients (MegaCrit.Sts2.Core.Models.AncientEventModel): the first room of an act heals
// to full and offers relics. Act 1 is always Neow (Models.Events\Neow.cs); its relics
// (RelicRarity.Ancient, Models.Relics) and the cards they add live here too.
#include <algorithm>

#include "cards.h"

namespace sts {

namespace {
template <class R> void regRelic() { db::registerRelic(R::kId, [] { return std::unique_ptr<Relic>(new R()); }); }
#define EVENT_HEADER(Name, Key) \
  static constexpr const char* kId = #Name; \
  Name() { id = #Name; locKey = Key; }

// CardFactory.CreateForReward with uniform odds over one rarity: `count` distinct cards.
std::vector<std::unique_ptr<Card>> distinctCards(Run& r, Rarity rarity, int count, Rng& rng) {
  auto pool = db::ironcladCards([&](const Card& c) { return c.rarity == rarity; });
  std::vector<std::unique_ptr<Card>> out;
  for (int i = 0; i < count && !pool.empty(); ++i) {
    std::string id = rng.nextItem(pool);
    pool.erase(std::find(pool.begin(), pool.end(), id));
    out.push_back(db::card(id));
  }
  (void)r;
  return out;
}

// The last / first Basic card of the deck with a tag (Strike / Defend).
Card* basicWithTag(Run& r, int tag, bool last) {
  Card* found = nullptr;
  for (auto& c : r.deck) {
    if (c->rarity != Rarity::Basic || !(c->tags & tag)) continue;
    found = c.get();
    if (!last) break;
  }
  return found;
}
}  // namespace

// ---------------------------------------------------------------- cards

// NeowsFury.cs: 10 damage, then put up to 2 cards from the discard pile into your hand.
struct NeowsFury : IroncladT<NeowsFury> {
  CARD_HEADER(NeowsFury, "NEOWS_FURY", 1, Attack, Ancient, AnyEnemy)
    keywords = kwExhaust;
    addVar("Damage", 10);
    addVar("Cards", 2);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    int room = std::min(val("Cards").toInt(), 10 - (int)combat->hand.size());  // CardPile.MaxCardsInHand
    if (room <= 0 || combat->discard.empty()) co_return;
    auto picked = co_await cmd::selectCards(*combat, "NEOWS_FURY", combat->discard, 0, room);
    for (Card* c : picked) co_await cmd::moveCard(*combat, c, Pile::Hand);
  }
  void onUpgrade() override {
    upgradeVar("Damage", 4);
    upgradeVar("Cards", 1);
  }
};

// Greed.cs. PORT NOTE: no Eternal keyword here, so it can be removed like other curses.
struct Greed : IroncladT<Greed> {
  CARD_HEADER(Greed, "GREED", -1, Curse, Curse, None)
    keywords = kwUnplayable;
    maxUpgradeLevel = 0;
  }
};

// Injury.cs
struct Injury : IroncladT<Injury> {
  CARD_HEADER(Injury, "INJURY", -1, Curse, Curse, None)
    keywords = kwUnplayable;
    maxUpgradeLevel = 0;
  }
};

// ---------------------------------------------------------------- Neow's relics (positive)

// ArcaneScroll.cs: a random Rare card of your pool.
struct ArcaneScroll : Relic {
  RELIC_HEADER(ArcaneScroll, "ARCANE_SCROLL", Ancient)
    addVar("Cards", 1);
  }
  Task<> afterObtained() override {
    auto cards = distinctCards(*run, Rarity::Rare, val("Cards").toInt(), run->rng("Rewards"));
    for (auto& c : cards) run->addCardToDeck(std::move(c));
    co_return;
  }
};

// BoomingConch.cs: in elite fights, draw 2 more and gain 1 energy on turn 1.
struct BoomingConch : Relic {
  RELIC_HEADER(BoomingConch, "BOOMING_CONCH", Ancient)
    addVar("Cards", 2);
    addVar("Energy", 1);
  }
  Dec modifyHandDraw(Dec amount) override {
    if (!combat || !combat->isElite || combat->turnNumber > 1) return amount;
    return amount + val("Cards");
  }
  Task<> afterSideTurnStart(Side side, const std::vector<Creature*>& participants) override {
    if (side != Side::Player || !combat || !combat->isElite || combat->turnNumber > 1) co_return;
    if (std::find(participants.begin(), participants.end(), owner()) == participants.end()) co_return;
    doFlash();
    co_await cmd::gainEnergy(*combat, val("Energy").toInt());
  }
};

// FishingRod.cs: every 3 normal fights, upgrade a random card of the deck.
struct FishingRod : Relic {
  RELIC_HEADER(FishingRod, "FISHING_ROD", Ancient)
    addVar("Combats", 3);
  }
  int combatsSeen = 0;
  bool showCounter() const override { return true; }
  int displayAmount() const override { return combatsSeen % 3; }
  Task<> afterCombatEnd() override {
    if (!combat || combat->isElite || combat->isBoss) co_return;
    ++combatsSeen;
    if (combatsSeen % val("Combats").toInt() != 0) co_return;
    doFlash();
    std::vector<Card*> up;
    for (auto& c : run->deck) if (c->upgradable()) up.push_back(c.get());
    if (!up.empty()) run->rng("Niche").nextItem(up)->upgrade();
  }
};

// GoldenPearl.cs: 150 gold.
struct GoldenPearl : Relic {
  RELIC_HEADER(GoldenPearl, "GOLDEN_PEARL", Ancient)
    addVar("Gold", 150);
  }
  Task<> afterObtained() override { co_await run->gainGold(val("Gold").toInt()); }
};

// NeowsTorment.cs: add Neow's Fury to the deck.
struct NeowsTorment : Relic {
  RELIC_HEADER(NeowsTorment, "NEOWS_TORMENT", Ancient)
  }
  Task<> afterObtained() override {
    run->addCardToDeck(db::card("NeowsFury"));
    co_return;
  }
};

// NewLeaf.cs: transform 1 card.
struct NewLeaf : Relic {
  RELIC_HEADER(NewLeaf, "NEW_LEAF", Ancient)
    addVar("Cards", 1);
  }
  Task<> afterObtained() override {
    auto picked = co_await run->selectFromDeck("card_selection.TO_TRANSFORM", nullptr, val("Cards").toInt());
    for (Card* c : picked) run->transformCard(c, run->randomTransformFor(c, run->rng("Niche")));
  }
};

// PreciseScissors.cs: remove 1 card.
struct PreciseScissors : Relic {
  RELIC_HEADER(PreciseScissors, "PRECISE_SCISSORS", Ancient)
    addVar("Cards", 1);
  }
  Task<> afterObtained() override {
    auto picked = co_await run->selectFromDeck("card_selection.TO_REMOVE", nullptr, val("Cards").toInt());
    for (Card* c : picked) run->removeCardFromDeck(c);
  }
};

// LavaRock.cs: the act 1 boss drops 2 extra relics.
struct LavaRock : Relic {
  RELIC_HEADER(LavaRock, "LAVA_ROCK", Ancient)
    addVar("Relics", 2);
  }
  bool triggered = false;
  int bonusRelicRewards(RoomType room) override {
    if (room != RoomType::Boss || run->actIndex != 0 || triggered) return 0;
    doFlash();
    triggered = true;
    usedUp = true;
    return val("Relics").toInt();
  }
};

// NeowsTalisman.cs: upgrade the last Basic Strike and Defend.
struct NeowsTalisman : Relic {
  RELIC_HEADER(NeowsTalisman, "NEOWS_TALISMAN", Ancient)
  }
  Task<> afterObtained() override {
    for (int tag : {tagStrike, tagDefend})
      if (Card* c = basicWithTag(*run, tag, true); c && c->upgradable()) c->upgrade();
    co_return;
  }
};

// NutritiousOyster.cs: +11 max HP.
struct NutritiousOyster : Relic {
  RELIC_HEADER(NutritiousOyster, "NUTRITIOUS_OYSTER", Ancient)
    addVar("MaxHp", 11);
  }
  Task<> afterObtained() override { co_await run->gainMaxHp(val("MaxHp").toInt()); }
};

// Pomander.cs: upgrade 1 card.
struct Pomander : Relic {
  RELIC_HEADER(Pomander, "POMANDER", Ancient)
    addVar("Cards", 1);
  }
  Task<> afterObtained() override {
    auto picked = co_await run->selectFromDeck("card_selection.TO_UPGRADE", [](Card* c) { return c->upgradable(); },
                                               val("Cards").toInt(), false, true);
    for (Card* c : picked) c->upgrade();
  }
};

// SmallCapsule.cs: a relic reward.
struct SmallCapsule : Relic {
  RELIC_HEADER(SmallCapsule, "SMALL_CAPSULE", Ancient)
  }
  Task<> afterObtained() override {
    co_await run->offerRelic(run->pullRelicFromFront(run->relicBag, run->rollRelicRarity(run->rng("Rewards"))), false);
  }
};

// StoneHumidifier.cs: resting also gives 5 max HP.
struct StoneHumidifier : Relic {
  RELIC_HEADER(StoneHumidifier, "STONE_HUMIDIFIER", Ancient)
    addVar("MaxHp", 5);
  }
  Task<> afterRestSiteHeal() override {
    doFlash();
    co_await run->gainMaxHp(val("MaxHp").toInt());
  }
};

// ---------------------------------------------------------------- Neow's relics (with a cost)

// CursedPearl.cs: Greed + 333 gold.
struct CursedPearl : Relic {
  RELIC_HEADER(CursedPearl, "CURSED_PEARL", Ancient)
    addVar("Gold", 333);
  }
  Task<> afterObtained() override {
    run->addCardToDeck(db::card("Greed"));
    co_await run->gainGold(val("Gold").toInt());
  }
};

// HeftyTablet.cs: choose 1 of 3 Rare cards, and add an Injury.
struct HeftyTablet : Relic {
  RELIC_HEADER(HeftyTablet, "HEFTY_TABLET", Ancient)
    addVar("Cards", 3);
  }
  Task<> afterObtained() override {
    co_await run->chooseCardFor(distinctCards(*run, Rarity::Rare, val("Cards").toInt(), run->rng("Rewards")));
    run->addCardToDeck(db::card("Injury"));
  }
};

// LargeCapsule.cs: 2 relics, plus a Strike and a Defend.
struct LargeCapsule : Relic {
  RELIC_HEADER(LargeCapsule, "LARGE_CAPSULE", Ancient)
    addVar("Relics", 2);
  }
  Task<> afterObtained() override {
    for (int i = 0; i < val("Relics").toInt(); ++i)  // RelicFactory.PullNextRelicFromFront
      co_await run->obtainRelic(run->pullRelicFromFront(run->relicBag, run->rollRelicRarity(run->rng("Rewards"))));
    run->addCardToDeck(db::card("StrikeIronclad"));
    run->addCardToDeck(db::card("DefendIronclad"));
  }
};

// LeafyPoultice.cs: lose 12 max HP, transform the first Basic Strike and Defend.
struct LeafyPoultice : Relic {
  RELIC_HEADER(LeafyPoultice, "LEAFY_POULTICE", Ancient)
    addVar("MaxHp", 12);
  }
  Task<> afterObtained() override {
    co_await run->loseMaxHp(val("MaxHp").toInt());
    Card* strike = basicWithTag(*run, tagStrike, false);
    Card* defend = basicWithTag(*run, tagDefend, false);
    for (Card* c : {strike, defend})
      if (c) run->transformCard(c, run->randomTransformFor(c, run->rng("Transformations")));
  }
};

// NeowsBones.cs: take 2 of Neow's other relics, then a random curse.
struct NeowsBones : Relic {
  RELIC_HEADER(NeowsBones, "NEOWS_BONES", Ancient)
    addVar("Relics", 2);
    addVar("Curses", 1);
  }
  Task<> afterObtained() override;
};

// PrecariousShears.cs: remove 2 cards, take 16 damage.
struct PrecariousShears : Relic {
  RELIC_HEADER(PrecariousShears, "PRECARIOUS_SHEARS", Ancient)
    addVar("Cards", 2);
    addVar("Damage", 16);
  }
  Task<> afterObtained() override {
    auto picked = co_await run->selectFromDeck("card_selection.TO_REMOVE", nullptr, val("Cards").toInt());
    for (Card* c : picked) run->removeCardFromDeck(c);
    co_await run->loseHp(val("Damage").toInt());
  }
};

// ---------------------------------------------------------------- Neow

namespace {
// Neow.PositiveOptions / CurseOptions and the coin-flip extras, as in the C#. Relics that
// aren't registered (their systems are missing) count as not allowed at Neow.
// PORT NOTE: not ported: Kaleidoscope (needs every character unlocked; never allowed
// here), MassiveScroll (multiplayer only), LeadPaperweight (colorless pool), LostCoffer,
// PhialHolster and NeowsSacrifice (potions), ScrollBoxes (bundle screen), WingedBoots
// (free travel), DowsingRod (quest cards), SilkenTress and SilverCrucible (card reward
// hooks / enchantments).
const std::vector<std::string> kPositive = {
    "ArcaneScroll", "BoomingConch", "FishingRod", "GoldenPearl", "Kaleidoscope", "LeadPaperweight", "LostCoffer",
    "MassiveScroll", "NeowsTorment", "NewLeaf", "PhialHolster", "PreciseScissors", "ScrollBoxes", "WingedBoots"};
const std::vector<std::string> kCurse = {
    "CursedPearl", "DowsingRod", "HeftyTablet", "LargeCapsule", "LeafyPoultice", "NeowsBones", "NeowsSacrifice",
    "PrecariousShears", "SilkenTress", "SilverCrucible"};
const std::vector<std::string> kExtras = {"LavaRock", "NeowsTalisman", "NutritiousOyster", "Pomander",
                                          "SmallCapsule", "StoneHumidifier"};

bool allowedAtNeow(const std::string& id) { return db::relicRegistered(id); }
}  // namespace

Task<> NeowsBones::afterObtained() {
  // GetValidRelics: every Neow option relic that is allowed, except Neow's Bones.
  std::vector<std::string> valid;
  for (auto* list : {&kCurse, &kPositive, &kExtras})
    for (auto& id : *list)
      if (id != "NeowsBones" && allowedAtNeow(id)) valid.push_back(id);
  run->rng("Rewards").shuffle(valid);
  // RewardsSet.WithSkippingDisallowed: both relics are taken.
  for (int i = 0; i < val("Relics").toInt() && i < (int)valid.size(); ++i)
    co_await run->obtainRelic(db::relic(valid[i]));
  // A random curse from CurseCardPool (CanBeGeneratedByModifiers: not Greed / AscendersBane).
  std::vector<std::string> curses;
  for (const char* id : {"BadLuck", "Clumsy", "CurseOfTheBell", "Debt", "Decay", "Doubt", "Enthralled", "Folly",
                         "Guilty", "Injury", "Normality", "PoorSleep", "Regret", "Shame", "SporeMind", "Writhe"})
    if (db::card(id)) curses.push_back(id);
  for (int i = 0; i < val("Curses").toInt() && !curses.empty(); ++i) {
    std::string id = run->rng("Niche").nextItem(curses);
    curses.erase(std::find(curses.begin(), curses.end(), id));
    run->addCardToDeck(db::card(id));
  }
}

struct Neow : Event {
  EVENT_HEADER(Neow, "NEOW")
  // AncientEventModel.RelicOption: take the relic, then Done().
  EventOption relicOption(const std::string& relicId) {
    EventOption o;
    std::shared_ptr<Relic> shown(db::relic(relicId).release());
    o.relic = shown;
    o.action = [this, relicId]() -> Task<> {
      co_await run->obtainRelic(db::relic(relicId));
      finished = true;
      options.clear();
      descKey.clear();
    };
    return o;
  }
  std::vector<EventOption> initialOptions() override {
    ancient = true;
    // DefineDialogues: the Ironclad's first-visit line (no profile, so always visit 0).
    dialogue = {"NEOW.talk.IRONCLAD.0-0.ancient"};
    // GenerateInitialOptions (no run modifiers).
    std::vector<std::string> curses;
    for (auto& id : kCurse) if (allowedAtNeow(id)) curses.push_back(id);
    std::string curse = curses.empty() ? "" : rng().nextItem(curses);
    std::vector<std::string> pos = kPositive;
    auto drop = [&](const char* id) { pos.erase(std::remove(pos.begin(), pos.end(), id), pos.end()); };
    if (curse == "CursedPearl") drop("GoldenPearl");
    if (curse == "HeftyTablet") drop("ArcaneScroll");
    if (curse == "LeafyPoultice") drop("NewLeaf");
    if (curse == "PrecariousShears") drop("PreciseScissors");
    if (curse == "NeowsSacrifice") { drop("PhialHolster"); drop("LostCoffer"); }
    if (curse != "LargeCapsule") pos.push_back(rng().nextBool() ? "LavaRock" : "SmallCapsule");
    pos.push_back(rng().nextBool() ? "NutritiousOyster" : "StoneHumidifier");
    pos.push_back(rng().nextBool() ? "NeowsTalisman" : "Pomander");
    pos.erase(std::remove_if(pos.begin(), pos.end(), [](const std::string& id) { return !allowedAtNeow(id); }), pos.end());
    rng().shuffle(pos);  // UnstableShuffle(Rng).Take(2), then the curse option last
    std::vector<EventOption> out;
    for (size_t i = 0; i < pos.size() && i < 2; ++i) out.push_back(relicOption(pos[i]));
    if (!curse.empty()) out.push_back(relicOption(curse));
    return out;
  }
};

void registerAncients() {
  registerCardType<NeowsFury>();
  registerCardType<Greed>();
  registerCardType<Injury>();
  regRelic<ArcaneScroll>();
  regRelic<BoomingConch>();
  regRelic<FishingRod>();
  regRelic<GoldenPearl>();
  regRelic<NeowsTorment>();
  regRelic<NewLeaf>();
  regRelic<PreciseScissors>();
  regRelic<LavaRock>();
  regRelic<NeowsTalisman>();
  regRelic<NutritiousOyster>();
  regRelic<Pomander>();
  regRelic<SmallCapsule>();
  regRelic<StoneHumidifier>();
  regRelic<CursedPearl>();
  regRelic<HeftyTablet>();
  regRelic<LargeCapsule>();
  regRelic<LeafyPoultice>();
  regRelic<NeowsBones>();
  regRelic<PrecariousShears>();
  db::registerEvent(Neow::kId, [] { return std::unique_ptr<Event>(new Neow()); });
}

}  // namespace sts
