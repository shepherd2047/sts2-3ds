// Package A6: the remaining Ancient relics (Neow, Orobas, Pael) whose systems now exist:
// SeaGlass, PrismaticGem, PaelsGrowth (+ the CLONE rest site option, Run::restSite option 6),
// LeadPaperweight, Kaleidoscope, WhisperingEarring; PaelsEye (E7, extra turns); E8: Driftwood,
// PaelsWing (card reward alternatives). Translated from MegaCrit.Sts2.Core.Models.Relics.
//
// Still locked (unregistered, so no Ancient offers them):
//  * GoldenCompass: golden path map.  FurCoat: map marks.  (PaelsLegion, Byrdpip: pets.cpp.)
//    ToyBox: wax relics.  (WingedBoots, DowsingRod, ScrollBoxes: quests.cpp, E2.)
//    MassiveScroll: multiplayer only.
#include "cards.h"
#include "colorless.h"
#include "game.h"

namespace sts {

namespace {

template <class R> void reg() { db::registerRelic(R::kId, [] { return std::unique_ptr<Relic>(new R()); }); }

// CardSelectCmd over generated cards (FromSimpleGridForRewards): the picked ones join the deck.
Task<> pickCardsForDeck(Run& r, std::vector<std::unique_ptr<Card>> cards, std::string prompt, int minCount, int maxCount) {
  std::vector<Card*> opts;
  for (auto& c : cards) opts.push_back(c.get());
  if (opts.empty()) co_return;
  r.deckChoice.prompt = std::move(prompt);
  r.deckChoice.options = std::move(opts);
  r.deckChoice.count = maxCount;
  r.deckChoice.minCount = minCount;
  r.deckChoice.canCancel = false;
  r.deckChoice.showUpgrade = false;
  r.deckChoice.active = true;
  auto picked = co_await r.deckChoice.result.next();
  r.deckChoice.active = false;
  for (Card* p : picked)
    for (auto& c : cards)
      if (c.get() == p) { r.addCardToDeck(std::move(c)); break; }
}

bool rewardRarity(const Card& c) { return c.rarity == Rarity::Common || c.rarity == Rarity::Uncommon || c.rarity == Rarity::Rare; }

}  // namespace

// SeaGlass.cs: pick any of 15 cards (5 Common, 5 Uncommon, 5 Rare, uniform odds, no rarity or pool
// modification) from another character's pool. Orobas stores the chosen character as an index into
// db::characterIds() in "CharacterIndex". PORT NOTE (n/a: visual): the relic title / description do not show the
// character (StringVar "Character").
struct SeaGlass : Relic {
  RELIC_HEADER(SeaGlass, "SEA_GLASS", Ancient) addVar("Cards", 15); addVar("CharacterIndex", -1); }
  Task<> afterObtained() override {
    const auto& ids = db::characterIds();
    int idx = val("CharacterIndex").toInt();
    std::string character = idx >= 0 && idx < (int)ids.size() ? ids[idx] : std::string("Ironclad");
    int cardCount = val("Cards").toInt() / 3;
    std::vector<std::unique_ptr<Card>> all;
    for (Rarity rar : {Rarity::Common, Rarity::Uncommon, Rarity::Rare}) {
      auto pool = db::characterCards(character, [&](const Card& c) { return c.rarity == rar; });
      for (int i = 0; i < cardCount && !pool.empty(); ++i) {
        std::string id = run->rng("Rewards").nextItem(pool);
        pool.erase(std::find(pool.begin(), pool.end(), id));
        all.push_back(db::card(id));
      }
    }
    int n = (int)all.size();
    co_await pickCardsForDeck(*run, std::move(all), "relics.SEA_GLASS.selectionScreenPrompt", 0, n);
  }
};

// PrismaticGem.cs: +1 energy; card rewards draw from every character's pool
// (ModifyCardRewardCreationOptions: UnlockState.CharacterCardPools.Union(CardPools), unless the pools
// are all colorless). PORT NOTE (n/a: owner): CharacterCardPools is every character in game order (all unlocked).
struct PrismaticGem : Relic {
  RELIC_HEADER(PrismaticGem, "PRISMATIC_GEM", Ancient) addVar("Energy", 1); }
  Dec modifyMaxEnergy(Dec amount) override { return amount + val("Energy"); }
  void modifyCardRewardCreationOptions(CardCreationOptions& o) override {
    if (o.has(ccNoCardPoolModifications) || !o.has(ccIsCardReward)) return;
    bool allColorless = true;
    for (auto& p : o.pools) if (p != CardCreationOptions::kColorless) allColorless = false;
    if (allColorless) return;
    std::vector<std::string> pools = db::allCharacters();
    for (auto& p : o.pools)
      if (std::find(pools.begin(), pools.end(), p) == pools.end()) pools.push_back(p);
    o.pools = std::move(pools);
  }
};

// PaelsGrowth.cs: enchant a card with Clone (4); the CLONE rest site option (Run::restSite, id 6)
// copies every Clone card of the deck.
struct PaelsGrowth : Relic {
  RELIC_HEADER(PaelsGrowth, "PAELS_GROWTH", Ancient) }
  Task<> afterObtained() override {
    auto picked = co_await run->selectForEnchantment("Clone", 1);
    for (Card* c : picked) run->enchantCard(c, "Clone", 4);
  }
};

// LeadPaperweight.cs: choose 1 of 2 colorless cards (skippable).
struct LeadPaperweight : Relic {
  RELIC_HEADER(LeadPaperweight, "LEAD_PAPERWEIGHT", Ancient) }
  // CreateForReward(2, CardCreationOptions(ColorlessCardPool, Other, RegularEncounter)): with the
  // upgrade roll and the card reward hooks.
  Task<> afterObtained() override {
    CardCreationOptions o;
    o.pools = {CardCreationOptions::kColorless};
    co_await run->chooseCardFor(run->createForReward(o, 2));
  }
};

// Kaleidoscope.cs: two card rewards, each offering one card (regular encounter odds) from three
// random other characters' pools; offered together (RewardsCmd.OfferCustom). Each is a fixed-card
// CardReward whose Driftwood reroll options are ForNonCombatWithDefaultOdds(no pools).
struct Kaleidoscope : Relic {
  RELIC_HEADER(Kaleidoscope, "KALEIDOSCOPE", Ancient) addVar("Cards", 2); }
  Task<> afterObtained() override {
    std::vector<Run::RewardItem> rows;
    for (int i = 0; i < val("Cards").toInt(); ++i) {
      std::vector<std::string> others;
      for (auto& id : db::allCharacters())  // UnlockState.CharacterCardPools order
        if (id != run->characterId && db::characterPlayable(id)) others.push_back(id);
      run->rng("Niche").shuffle(others);  // StableShuffle(Rng.Niche)
      if (others.size() > 3) others.resize(3);
      Run::RewardItem row;
      row.kind = Run::RewardKind::Card;
      for (auto& ch : others) {
        CardCreationOptions o;
        o.pools = {ch};
        o.with(ccNoCardPoolModifications);
        for (auto& c : run->createForReward(o, 1)) row.cards.push_back(std::move(c));
      }
      // CardReward(cardsToOffer, ...): Populate runs TryModifyCardRewardOptions once on the fixed cards.
      CardCreationOptions fixedOpts;
      fixedOpts.odds = RarityOdds::Uniform;
      fixedOpts.with(ccNoCardPoolModifications | ccNoCardModelModifications | ccIsCardReward);
      run->runCardRewardHooks(row.cards, fixedOpts);
      row.cardOptions = CardCreationOptions::forNonCombat({}, false);
      row.cardOptions.with(ccIsCardReward);
      row.cardCount = (int)row.cards.size();
      rows.push_back(std::move(row));
    }
    co_await run->offerRewards(std::move(rows));
  }
};

// WhisperingEarring.cs: +1 energy; on turn 1 the first playable hand card is played over and over
// (up to 13). Card selections during it take the first options (VakuuCardSelector, Combat::autoSelectFirst).
// AfterAutoPrePlayPhaseEnteredLate: card.SpendResources() then CardCmd.AutoPlay(skipXCapture) =
// Combat::playCard(autoPlay, spendResources).
struct WhisperingEarring : Relic {
  RELIC_HEADER(WhisperingEarring, "WHISPERING_EARRING", Ancient) addVar("Energy", 1); }
  Dec modifyMaxEnergy(Dec amount) override { return amount + val("Energy"); }
  Task<> afterAutoPrePlayPhaseEnteredLate() override {
    if (!combat || combat->turnNumber > 1) co_return;
    Combat& c = *combat;
    doFlash();
    c.autoSelectFirst = true;
    int played = 0;
    for (; played < 13; ++played) {
      if (c.over || c.ending) break;
      Card* card = nullptr;
      c.playerPhase = true;  // Combat::canPlay needs the play phase
      for (Card* k : c.hand)
        if (c.canPlay(k)) { card = k; break; }
      c.playerPhase = false;
      if (!card) break;
      Creature* target = nullptr;
      if (card->target == TargetType::AnyEnemy) {
        auto h = c.hittableEnemies();
        if (h.empty()) break;
        target = h.front();
      }  // AnyAlly / AnyPlayer: single player, no target needed
      co_await c.playCard(card, target, true, false, true);
    }
    c.autoSelectFirst = false;
  }
};

// Driftwood.cs: TryModifyRewardsLate -- every card reward can be rerolled once (CardReward.CanReroll,
// the REROLL CardRewardAlternative; Run::offerRewards).
struct Driftwood : Relic {
  RELIC_HEADER(Driftwood, "DRIFTWOOD", Ancient) }
  bool makesCardRewardsRerollable() override { return true; }
};

// PaelsWing.cs: card rewards get a SACRIFICE alternative (ends and completes the reward); every
// Sacrifices (2) sacrificed rewards obtain the next relic from the front of the grab bag.
struct PaelsWing : Relic {
  RELIC_HEADER(PaelsWing, "PAELS_WING", Ancient) addVar("Sacrifices", 2); }
  int rewardsSacrificed = 0;  // [SavedProperty] RewardsSacrificed
  void persist(Archive& a) override { a.io(rewardsSacrificed); }
  bool showCounter() const override { return true; }
  int displayAmount() const override {
    int n = const_cast<PaelsWing*>(this)->val("Sacrifices").toInt();
    return n > 0 ? rewardsSacrificed % n : 0;
  }
  const char* cardRewardAlternative() override { return "SACRIFICE"; }
  Task<> onCardRewardAlternative() override {  // OnSacrifice
    ++rewardsSacrificed;
    doFlash();
    if (rewardsSacrificed % val("Sacrifices").toInt() == 0)  // RelicFactory.PullNextRelicFromFront(Owner)
      co_await run->obtainRelic(run->pullRelicFromFront(run->relicBag, run->rollRelicRarity(run->rng("Rewards"))));
  }
};

// PaelsEye.cs: once per combat, ending a turn without having played a card (auto-plays aside)
// exhausts the hand (BeforeSideTurnEndEarly) and takes another turn (ShouldTakeExtraTurn).
// WasOwnerPartOfLastPlayerTurn is always true in single player. RelicStatus (the glow) is UI only.
struct PaelsEye : Relic {
  RELIC_HEADER(PaelsEye, "PAELS_EYE", Ancient) }
  bool usedThisCombat = false;
  bool anyCardsPlayedThisTurn() const {
    if (combat->turnNumber == 1 && run->hasRelic("WhisperingEarring")) return true;
    return combat->history.countThisTurn(*combat, CombatHistoryEntry::CardPlayFinished,
                                         [](const CombatHistoryEntry& e) { return !e.autoPlay; }) > 0;
  }
  Task<> beforeCombatStart() override {
    usedThisCombat = false;
    return {};
  }
  bool shouldTakeExtraTurn() override { return combat && !usedThisCombat && !anyCardsPlayedThisTurn(); }
  Task<> beforeSideTurnEndEarly(Side, const std::vector<Creature*>& participants) override {
    if (!combat || !contains(participants, owner()) || usedThisCombat || anyCardsPlayedThisTurn()) co_return;
    std::vector<Card*> hand = combat->hand;
    for (Card* c : hand) co_await cmd::exhaustCard(*combat, c);
  }
  Task<> afterTakingExtraTurn() override {
    doFlash();
    usedThisCombat = true;
    return {};
  }
  Task<> afterCombatEnd() override {
    usedThisCombat = false;
    return {};
  }
};

void registerRelicsAncient2() {
  reg<Driftwood>();
  reg<PaelsWing>();
  reg<PaelsEye>();
  reg<SeaGlass>();
  reg<PrismaticGem>();
  reg<PaelsGrowth>();
  reg<LeadPaperweight>();
  reg<Kaleidoscope>();
  reg<WhisperingEarring>();
}

}  // namespace sts
