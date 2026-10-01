// Package A6: the remaining Ancient relics (Neow, Orobas, Pael) whose systems now exist:
// SeaGlass, PrismaticGem, PaelsGrowth (+ the CLONE rest site option, Run::restSite option 6),
// LeadPaperweight, Kaleidoscope, WhisperingEarring. Translated from MegaCrit.Sts2.Core.Models.Relics.
//
// Still locked (unregistered, so no Ancient offers them):
//  * Driftwood (CardReward.CanReroll), PaelsWing (CardRewardAlternative "SACRIFICE"): the card reward
//    screen has no reroll / alternative buttons.
//  * PaelsEye: ShouldTakeExtraTurn / AfterTakingExtraTurn (extra turn system).
//  * PaelsLegion, Byrdpip: pets.  GoldenCompass: golden path map.  FurCoat: map marks.
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
// db::characterIds() in "CharacterIndex". PORT NOTE: the relic title / description do not show the
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

// PrismaticGem.cs: +1 energy; card rewards draw from every character's pool (Run::cardReward asks
// allCharacterCardPools). PORT NOTE: the union is over all playable characters in game order (all
// characters are unlocked), and applies to every Run::cardReward call.
struct PrismaticGem : Relic {
  RELIC_HEADER(PrismaticGem, "PRISMATIC_GEM", Ancient) addVar("Energy", 1); }
  Dec modifyMaxEnergy(Dec amount) override { return amount + val("Energy"); }
  bool allCharacterCardPools() override { return true; }
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
  Task<> afterObtained() override {
    co_await run->chooseCardFor(colorlessRewardCards(*run, 2));
  }
};

// Kaleidoscope.cs: two card rewards, each offering one card (regular encounter odds) from three
// random other characters' pools. PORT NOTE: Rng.Niche StableShuffle is Rng::shuffle; no reroll.
struct Kaleidoscope : Relic {
  RELIC_HEADER(Kaleidoscope, "KALEIDOSCOPE", Ancient) addVar("Cards", 2); }
  Task<> afterObtained() override {
    for (int i = 0; i < val("Cards").toInt(); ++i) {
      std::vector<std::string> others;
      for (auto& id : db::allCharacters())  // UnlockState.CharacterCardPools order
        if (id != run->characterId && db::characterPlayable(id)) others.push_back(id);
      run->rng("Niche").shuffle(others);
      if (others.size() > 3) others.resize(3);
      std::vector<std::unique_ptr<Card>> options;
      for (auto& ch : others) {
        auto pool = db::characterCards(ch, rewardRarity);
        if (pool.empty()) continue;
        // CardRarityOdds.RollWithBaseOdds, then the next rarity the pool has.
        float rare = run->hasAscension(kScarcity) ? 0.0149f : 0.03f;
        float roll = run->rng("Rewards").nextFloat();
        Rarity want = roll < rare ? Rarity::Rare : roll < 0.37f + rare ? Rarity::Uncommon : Rarity::Common;
        std::vector<std::string> items;
        for (int guard = 0; guard < 3 && items.empty(); ++guard) {
          for (auto& id : pool) if (db::card(id)->rarity == want) items.push_back(id);
          if (items.empty()) want = want == Rarity::Common ? Rarity::Uncommon : want == Rarity::Uncommon ? Rarity::Rare : Rarity::Common;
        }
        if (items.empty()) continue;
        options.push_back(db::card(run->rng("Rewards").nextItem(items)));
      }
      co_await run->chooseCardFor(std::move(options));
    }
  }
};

// WhisperingEarring.cs: +1 energy; on turn 1 the first playable hand card is played over and over
// (up to 13). Card selections during it take the first options (VakuuCardSelector, Combat::autoSelectFirst).
// PORT NOTE: hooked at afterAutoPrePlayPhaseEntered (not the Late variant); cards are played with
// Combat::playCard(autoPlay = false) because that is what spends the energy (SpendResources), so
// they are not flagged as auto-plays.
struct WhisperingEarring : Relic {
  RELIC_HEADER(WhisperingEarring, "WHISPERING_EARRING", Ancient) addVar("Energy", 1); }
  Dec modifyMaxEnergy(Dec amount) override { return amount + val("Energy"); }
  Task<> afterAutoPrePlayPhaseEntered() override {
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
      co_await c.playCard(card, target, false, false);
    }
    c.autoSelectFirst = false;
  }
};

void registerRelicsAncient2() {
  reg<SeaGlass>();
  reg<PrismaticGem>();
  reg<PaelsGrowth>();
  reg<LeadPaperweight>();
  reg<Kaleidoscope>();
  reg<WhisperingEarring>();
}

}  // namespace sts
