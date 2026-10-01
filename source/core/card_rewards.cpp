// Package E8: card-reward creation and the reward screen's engine side.
//  * CardCreationOptions (ForRoom / ForNonCombatWith*Odds) and CardFactory.CreateForReward
//    (Run::createForReward): Hook.ModifyCardRewardCreationOptions per card (DingyRug, PrismaticGem,
//    the CharacterCards / BigGameHunter modifiers), CardRarityOdds Roll / RollWithBaseOdds,
//    GetNextAllowedRarity, RollForUpgrade, then Hook.TryModifyCardRewardOptions(Late).
//  * CardReward (Run::makeCardReward) with CanReroll (Driftwood) and CardRewardAlternative
//    (Skip, REROLL, PaelsWing's SACRIFICE), SpecialCardReward, and RewardsSet.Offer / RewardsCmd.OfferCustom
//    (Run::offerRewards): the claimable list shared by combat rooms, events and relics.
// Translated from MegaCrit.Sts2.Core.Runs / .Factories / .Rewards / .Entities.CardRewardAlternatives.
#include <algorithm>

#include "game.h"
#include "progress.h"

namespace sts {

CardCreationOptions CardCreationOptions::forRoom(const std::string& characterId, RoomType room) {
  CardCreationOptions o;
  o.pools = {characterId};
  o.room = room;
  switch (room) {
    case RoomType::Monster: case RoomType::Elite: case RoomType::Boss: o.source = CardSource::Encounter; break;
    case RoomType::Shop: o.source = CardSource::Shop; break;
    default: o.source = CardSource::Other; break;
  }
  o.odds = room == RoomType::Elite ? RarityOdds::EliteEncounter
         : room == RoomType::Boss  ? RarityOdds::BossEncounter
         : room == RoomType::Shop  ? RarityOdds::Shop
                                   : RarityOdds::RegularEncounter;
  return o;
}

CardCreationOptions CardCreationOptions::forNonCombat(std::vector<std::string> pools, bool uniform,
                                                      std::function<bool(const Card&)> filter) {
  CardCreationOptions o;
  o.pools = std::move(pools);
  o.filter = std::move(filter);
  o.source = CardSource::Other;
  o.odds = uniform ? RarityOdds::Uniform : RarityOdds::RegularEncounter;
  o.flags = ccNoUpgradeRoll;
  return o;
}

namespace {

// CardRarityOdds.GetBaseOdds (rare, uncommon).
void baseOdds(const Run& r, RarityOdds t, float& rare, float& uncommon) {
  const bool scarce = r.hasAscension(kScarcity);
  switch (t) {
    case RarityOdds::EliteEncounter: rare = scarce ? 0.05f : 0.1f; uncommon = 0.4f; break;
    case RarityOdds::BossEncounter: rare = 1.f; uncommon = 0.f; break;
    case RarityOdds::Shop: rare = scarce ? 0.045f : 0.09f; uncommon = 0.37f; break;
    case RarityOdds::Uniform: rare = 0.33f; uncommon = 0.33f; break;
    default: rare = scarce ? 0.0149f : 0.03f; uncommon = 0.37f; break;
  }
}

// CardFactory.RollForRarity: CardRarityOdds.Roll (changes the future odds) for encounter rewards or
// ForceRarityOddsChange, RollWithBaseOdds otherwise.
Rarity rollForRarity(Run& r, RarityOdds t, CardSource source, bool force) {
  const bool encounterOdds = t == RarityOdds::RegularEncounter || t == RarityOdds::EliteEncounter || t == RarityOdds::BossEncounter;
  if (force || (source == CardSource::Encounter && encounterOdds)) {
    if (encounterOdds)  // Run::rollRarity is CardRarityOdds.Roll for these three
      return r.rollRarity(t == RarityOdds::EliteEncounter ? RoomType::Elite : t == RarityOdds::BossEncounter ? RoomType::Boss : RoomType::Monster);
    float rare, uncommon;
    baseOdds(r, t, rare, uncommon);
    float roll = r.rng("Rewards").nextFloat();
    float rareOdds = rare + r.rarityOffset;
    Rarity result = roll < rareOdds ? Rarity::Rare : roll < uncommon + rareOdds ? Rarity::Uncommon : Rarity::Common;
    if (result == Rarity::Rare) r.rarityOffset = -0.05f;
    else r.rarityOffset = std::min(r.rarityOffset + (r.hasAscension(kScarcity) ? 0.005f : 0.01f), 0.4f);
    return result;
  }
  float rare, uncommon;
  baseOdds(r, t, rare, uncommon);
  float roll = r.rng("Rewards").nextFloat();
  return roll < rare ? Rarity::Rare : roll < uncommon + rare ? Rarity::Uncommon : Rarity::Common;
}

// CardRarity.GetNextHighestRarityWithWrapping over Common / Uncommon / Rare.
Rarity nextWrapping(Rarity r) {
  return r == Rarity::Common ? Rarity::Uncommon : r == Rarity::Uncommon ? Rarity::Rare : Rarity::Common;
}

// CardCreationOptions.GetPossibleCards: the pools' cards in pool order (a Union), filtered.
std::vector<std::string> possibleCards(const CardCreationOptions& o) {
  std::vector<std::string> out;
  auto f = [&](const Card& c) { return !o.filter || o.filter(c); };
  for (auto& pool : o.pools) {
    std::vector<std::string> ids = pool == CardCreationOptions::kColorless ? db::colorlessCards(f) : db::characterCards(pool, f);
    for (auto& id : ids)
      if (std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
  }
  return out;
}

void unionPools(CardCreationOptions& o, const std::vector<std::string>& extra) {
  for (auto& p : extra)
    if (std::find(o.pools.begin(), o.pools.end(), p) == o.pools.end()) o.pools.push_back(p);
}

}  // namespace

std::vector<std::unique_ptr<Card>> Run::createForReward(const CardCreationOptions& base, int count) {
  std::vector<std::unique_ptr<Card>> out;
  std::vector<std::string> blacklist;
  for (int i = 0; i < count; ++i) {
    // Hook.ModifyCardRewardCreationOptions (CardFactory.CreateForReward(player, blacklist, options)).
    CardCreationOptions o = base;
    const bool poolMods = !o.has(ccNoCardPoolModifications);
    if (poolMods && o.has(ccIsCardReward)) unionPools(o, modifierCardPools());  // CharacterCards
    if (hasModifier("BigGameHunter") && o.source == CardSource::Encounter && o.odds == RarityOdds::EliteEncounter &&
        poolMods && !o.has(ccNoRarityModification)) {
      o.odds = RarityOdds::Uniform;
      o.filter = [](const Card& c) { return c.rarity == Rarity::Rare; };
      if (possibleCards(o).empty()) o.pools = {characterId};
    }
    for (auto& rel : relics) rel->modifyCardRewardCreationOptions(o);

    std::vector<std::string> possible;
    for (auto& id : possibleCards(o))
      if (std::find(blacklist.begin(), blacklist.end(), id) == blacklist.end()) possible.push_back(id);
    auto rarityOf = [](const std::string& id) { return db::card(id)->rarity; };
    std::vector<std::string> items;
    if (o.odds == RarityOdds::Uniform) {
      for (auto& id : possible) {
        Rarity q = rarityOf(id);
        if (q != Rarity::Basic && q != Rarity::Ancient) items.push_back(id);
      }
    } else {
      auto allowed = [&](Rarity q) {
        for (auto& id : possible) if (rarityOf(id) == q) return true;
        return false;
      };
      Rarity want = rollForRarity(*this, o.odds, o.source, o.has(ccForceRarityOddsChange));
      for (int k = 0; k < 3 && !allowed(want); ++k) want = nextWrapping(want);  // GetNextAllowedRarity
      for (auto& id : possible) if (rarityOf(id) == want) items.push_back(id);
    }
    if (items.empty()) break;  // the C# throws (no valid card)
    Rng& rr = o.rng ? *o.rng : rng("Rewards");
    std::string id = rr.nextItem(items);
    blacklist.push_back(id);
    progress::markCardSeen(id);
    out.push_back(db::card(id));
    if (!base.has(ccNoUpgradeRoll)) {  // RollForUpgrade(player, card, 0, RngOverride ?? Rewards)
      if (!o.rng) {
        rollCardUpgrade(*out.back(), 0);
      } else {
        Card& c = *out.back();
        double roll = rr.nextFloat();
        if (c.upgradable()) {
          double odds = c.rarity != Rarity::Rare ? actIndex * (hasAscension(kScarcity) ? 0.125 : 0.25) : 0;
          if (roll <= odds) c.upgrade();
        }
      }
    }
  }
  if (!base.has(ccNoModifyHooks)) runCardRewardHooks(out, base);
  return out;
}

// Hook.TryModifyCardRewardOptions, then ...Late (AfterModifyingCardRewardOptions is inline in the relics).
void Run::runCardRewardHooks(std::vector<std::unique_ptr<Card>>& cards, const CardCreationOptions& o) {
  for (bool late : {false, true})
    for (auto& rel : relics) rel->modifyCardReward(cards, o, late);
}

// The C# CardReward constructor adds IsCardReward; Populate creates the cards, then AfterGenerated.
Run::RewardItem Run::makeCardReward(CardCreationOptions o, int count,
                                    std::function<void(std::vector<std::unique_ptr<Card>>&)> afterGenerated) {
  RewardItem item;
  item.kind = RewardKind::Card;
  o.with(ccIsCardReward);
  item.cards = createForReward(o, count);
  item.cardOptions = std::move(o);
  item.cardCount = count;
  item.afterGenerated = std::move(afterGenerated);
  if (item.afterGenerated) item.afterGenerated(item.cards);
  return item;
}

namespace {

int rewardsSetIndex(Run::RewardKind k) {  // Reward.RewardsSetIndex
  switch (k) {
    case Run::RewardKind::Gold: return 1;
    case Run::RewardKind::Potion: return 2;
    case Run::RewardKind::Relic: return 3;
    case Run::RewardKind::SpecialCard: return 4;
    case Run::RewardKind::Card: return 5;
  }
  return 5;
}

// CardReward.OnSelect: the card screen with its CardRewardAlternatives until a card is taken, an
// alternative ends the selection, or the reward is skipped. True = the reward is complete.
Task<bool> claimCardReward(Run& r, Run::RewardItem& item) {
  r.rewardCards = std::move(item.cards);
  bool complete = false;
  for (;;) {
    // CardRewardAlternative.Generate: Skip (the -1 answer), REROLL, then the hook-added ones.
    std::vector<Relic*> owners;
    r.rewardAlternatives.clear();
    if (item.canReroll && item.cardCount > 0) { r.rewardAlternatives.push_back("REROLL"); owners.push_back(nullptr); }
    for (auto& rel : r.relics)
      if (const char* alt = rel->cardRewardAlternative()) { r.rewardAlternatives.push_back(alt); owners.push_back(rel.get()); }
    const int n = (int)r.rewardCards.size(), alts = (int)r.rewardAlternatives.size();
    int pick = co_await r.rewardChoice.next();
    if (pick >= 0 && pick < n) {
      r.addCardToDeck(std::move(r.rewardCards[(size_t)pick]));
      r.rewardCards.erase(r.rewardCards.begin() + pick);
      complete = true;  // Hook.ShouldAllowSelectingMoreCardRewards: no model allows more
      break;
    }
    if (pick >= n && pick < n + alts) {
      Relic* owner = owners[(size_t)(pick - n)];
      if (!owner) {  // CardReward.Reroll: CanReroll off, populate again (DoNothing: the screen stays)
        item.canReroll = false;
        // Empty reroll pools (Kaleidoscope's fixed rewards) make the C# throw; the old cards stay.
        auto fresh = r.createForReward(item.cardOptions, item.cardCount);
        if (!fresh.empty()) r.rewardCards = std::move(fresh);
        if (item.afterGenerated) item.afterGenerated(r.rewardCards);
        continue;
      }
      complete = true;  // EndSelectionAndCompleteReward (SACRIFICE)
      co_await owner->onCardRewardAlternative();
      break;
    }
    if (item.canSkip) break;  // Skip: EndSelectionAndDoNotCompleteReward, the row stays
  }
  if (!complete) item.cards = std::move(r.rewardCards);
  r.rewardCards.clear();
  r.rewardAlternatives.clear();
  co_return complete;
}

}  // namespace

Task<> Run::offerRewards(std::vector<RewardItem> items, bool terminal) {
  if (player->hp <= 0 || died) co_return;  // RewardsSet.Offer: nothing for a dead player
  // Hook.ModifyRewards, TryModifyRewardsLate: Driftwood makes every card reward rerollable.
  bool reroll = false;
  for (auto& rel : relics) if (rel->makesCardRewardsRerollable()) reroll = true;
  if (reroll)
    for (auto& item : items) if (item.kind == RewardKind::Card) item.canReroll = true;
  std::stable_sort(items.begin(), items.end(), [](const RewardItem& a, const RewardItem& b) {
    return rewardsSetIndex(a.kind) < rewardsSetIndex(b.kind);
  });
  if (items.empty() && !terminal) co_return;  // an empty custom set shows nothing
  // A reward's own effect may offer another set (a relic whose AfterObtained offers rewards): the
  // outer list is put aside and comes back afterwards.
  std::vector<RewardItem> outer = std::move(rewardItems);
  const Screen outerScreen = screen;
  rewardItems = std::move(items);
  screen = Screen::Reward;
  // Claim rows in any order; Proceed (-1 or an out-of-range index) forfeits the rest. A custom
  // set (NRewardsScreen without a proceed button) also closes once everything is taken.
  while (terminal || !rewardItems.empty()) {
    int pick = co_await rewardListChoice.next();
    if (pick < 0 || pick >= (int)rewardItems.size()) break;
    RewardItem& item = rewardItems[(size_t)pick];
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
      case RewardKind::SpecialCard:  // SpecialCardReward.OnSelect
        addCardToDeck(std::move(item.card));
        claimed = true;
        break;
      case RewardKind::Card:
        claimed = co_await claimCardReward(*this, item);
        break;
    }
    if (claimed) rewardItems.erase(rewardItems.begin() + pick);
  }
  rewardItems.clear();
  rewardCards.clear();
  rewardAlternatives.clear();
  rewardItems = std::move(outer);
  if (!rewardItems.empty()) screen = outerScreen;
}

}  // namespace sts
