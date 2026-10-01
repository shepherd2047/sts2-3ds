// The merchant (package 9): MerchantRoom + MerchantInventory.CreateForNormalMerchant and its
// entries (Entities/Merchant/*.cs), CardFactory.CreateForMerchant, RelicFactory
// .PullNextRelicFromBack. The two colorless card slots (Uncommon, Rare; A9) follow the five
// character slots; the Foul Potion throw is FoulPotion::onUse (events_shared2.cpp) via the
// shop screen's potion button.
// PORT NOTE (n/a: visual): no merchant dialogue on entering; the BoughtColorless history entry is not kept (history format, owner).
#include <algorithm>
#include <cmath>

#include "badges.h"
#include "game.h"

namespace sts {

namespace {

// C# Math.Round / Mathf.RoundToInt: round half to even.
int roundEven(double x) { return (int)std::nearbyint(x); }

// MerchantCardEntry.GetCost: 50/75/150, colorless cards x1.15 rounded.
int cardBaseCost(const Card& c, bool colorless = false) {
  int n = c.rarity == Rarity::Rare ? 150 : c.rarity == Rarity::Uncommon ? 75 : 50;
  return colorless ? (int)std::nearbyint((float)n * 1.15f) : n;
}

int relicBaseCost(RelicRarity r) {  // RelicModel.MerchantCost
  switch (r) {
    case RelicRarity::Common: return 175;
    case RelicRarity::Uncommon: return 225;
    case RelicRarity::Rare: return 275;
    case RelicRarity::Shop: return 200;
    default: return 999999999;
  }
}

int potionBaseCost(PotionRarity r) { return r == PotionRarity::Rare ? 100 : r == PotionRarity::Uncommon ? 75 : 50; }

}  // namespace

// RelicGrabBag.PullFromBack: the back of the rarity's list (an empty rarity falls through
// Shop -> Common -> Uncommon -> Rare), only relics allowed in shops; also leaves the
// shared bag. Falls back to Circlet.
std::unique_ptr<Relic> Run::pullRelicFromBack(RelicRarity k) {
  removeDisallowedRelics();
  while (k != RelicRarity::None) {
    auto& v = relicBag[k];
    for (int i = (int)v.size() - 1; i >= 0; --i) {
      auto r = db::relic(v[i]);
      if (!r || !r->allowedInShops()) continue;
      std::string id = v[i];
      for (auto& [kk, vv] : relicBag) vv.erase(std::remove(vv.begin(), vv.end(), id), vv.end());
      for (auto& [kk, vv] : sharedRelicBag) vv.erase(std::remove(vv.begin(), vv.end(), id), vv.end());
      r->run = this;
      return r;
    }
    k = k == RelicRarity::Shop ? RelicRarity::Common
      : k == RelicRarity::Common ? RelicRarity::Uncommon
      : k == RelicRarity::Uncommon ? RelicRarity::Rare : RelicRarity::None;
  }
  auto r = db::relic("Circlet");
  if (r) r->run = this;
  return r;
}

// MerchantEntry.Cost: the stored cost through Hook.ModifyMerchantPrice.
int Run::shopPrice(const ShopItem& it) {
  // MerchantCardRemovalEntry: 75 + 25 per removal already bought, 100 + 50 with Inflation.
  Dec price = it.kind == ShopItem::Removal
                  ? Dec(ascValue(kInflation, 100, 75) + ascValue(kInflation, 50, 25) * shopRemovalsUsed)
                  : Dec(it.cost);
  for (Model* m : listeners()) price = m->modifyMerchantPrice(price);
  return price.toInt();
}

namespace {

// MerchantCardEntry.Populate + CalcCost (CardFactory.CreateForMerchant by type): rarity
// from the shop odds (RollWithoutChangingFutureOdds: rare 9% + the reward offset,
// uncommon 37%), the next rarity with a card of that type if none, never a card already
// on sale here; price 50/75/150 x 0.95..1.05, halved when on sale.
void fillCard(Run& r, ShopItem& it) {
  std::vector<std::string> onShelf;
  for (auto& s : r.shop) if (s.card) onShelf.push_back(s.card->id);
  auto ofRarity = [&](Rarity want) {
    if (it.colorless)  // CardFactory.CreateForMerchant(rarity): no Basic, the slot's rarity only
      return db::colorlessCards([&](const Card& c) {
        return c.rarity == want && std::find(onShelf.begin(), onShelf.end(), c.id) == onShelf.end();
      });
    auto fits = [&](const Card& c) {
      return c.type == it.cardType && c.rarity == want &&
             std::find(onShelf.begin(), onShelf.end(), c.id) == onShelf.end();
    };
    auto ids = db::characterCards(r.characterId, fits);
    for (auto& ch : r.modifierCardPools())  // CharacterCards.ModifyMerchantCardPool (M11)
      for (auto& id : db::characterCards(ch, fits))
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
    return ids;
  };
  Rarity want = it.cardRarity;
  std::vector<std::string> ids;
  if (it.colorless) {
    ids = ofRarity(want);
  } else {
    float roll = r.rng("Rewards").nextFloat();
    float rare = (r.hasAscension(kScarcity) ? 0.045f : 0.09f) + r.rarityOffset;
    want = roll < rare ? Rarity::Rare : roll < 0.37f + rare ? Rarity::Uncommon : Rarity::Common;
    for (int k = 0; k < 3 && (ids = ofRarity(want)).empty(); ++k)  // GetNextHighestRarityWithWrapping
      want = want == Rarity::Common ? Rarity::Uncommon : want == Rarity::Uncommon ? Rarity::Rare : Rarity::Common;
  }
  if (ids.empty()) { it.card.reset(); return; }
  it.card = db::card(r.rng("Shops").nextItem(ids));
  r.rollCardUpgrade(*it.card, -999999999);  // CardFactory.CreateForMerchant: the roll is made but can never succeed
  for (auto& rel : r.relics)  // ModifyMerchantCardCreationResults (the eggs)
    if (rel->upgradesNewCard(*it.card)) it.card->upgrade();
  it.cost = roundEven(cardBaseCost(*it.card, it.colorless) * r.rng("Shops").nextFloat(0.95f, 1.05f));
}

void calcCardCost(Run& r, ShopItem& it) {
  it.cost = roundEven(cardBaseCost(*it.card, it.colorless) * r.rng("Shops").nextFloat(0.95f, 1.05f));
  if (it.onSale) it.cost /= 2;
}

void fillRelic(Run& r, ShopItem& it, RelicRarity rarity) {
  it.relic = r.pullRelicFromBack(rarity);
  it.cost = it.relic ? roundEven(relicBaseCost(it.relic->rarity) * r.rng("Shops").nextFloat(0.85f, 1.15f)) : 0;
}

void fillPotion(Run& r, ShopItem& it, std::unique_ptr<Potion> p) {
  it.potion = std::move(p);
  it.cost = roundEven(potionBaseCost(it.potion->rarity) * r.rng("Shops").nextFloat(0.95f, 1.05f));
}

}  // namespace

Task<> Run::enterShop() {
  shop.clear();
  shopMessage.clear();
  // PopulateCharacterCardEntries: Attack, Attack, Skill, Skill, Power; one is on sale.
  Rng& shops = rng("Shops");
  int sale = shops.nextInt(5);
  const CardType types[] = {CardType::Attack, CardType::Attack, CardType::Skill, CardType::Skill, CardType::Power};
  for (int i = 0; i < 5; ++i) {
    ShopItem it;
    it.kind = ShopItem::CardItem;
    it.cardType = types[i];
    fillCard(*this, it);
    shop.push_back(std::move(it));
    if (i == sale && shop.back().card) {  // SetOnSale recalculates the cost, halved
      shop.back().onSale = true;
      calcCardCost(*this, shop.back());
    }
  }
  // PopulateColorlessCardEntries: an Uncommon and a Rare colorless card.
  for (Rarity rar : {Rarity::Uncommon, Rarity::Rare}) {
    ShopItem it;
    it.kind = ShopItem::CardItem;
    it.colorless = true;
    it.cardRarity = rar;
    fillCard(*this, it);
    shop.push_back(std::move(it));
  }
  // PopulateRelicEntries: two rolled rarities and one Shop relic.
  RelicRarity rarities[] = {rollRelicRarity(rng("Rewards")), rollRelicRarity(rng("Rewards")), RelicRarity::Shop};
  for (RelicRarity rr : rarities) {
    ShopItem it;
    it.kind = ShopItem::RelicItem;
    fillRelic(*this, it, rr);
    shop.push_back(std::move(it));
  }
  // PopulatePotionEntries: three distinct random potions.
  for (auto& p : randomPotions(3, shops)) {
    ShopItem it;
    it.kind = ShopItem::PotionItem;
    fillPotion(*this, it, std::move(p));
    shop.push_back(std::move(it));
  }
  ShopItem removal;
  removal.kind = ShopItem::Removal;
  removal.used = hasModifier("Hoarder");  // Hoarder.ShouldAllowMerchantCardRemoval (M11): sold out
  shop.push_back(std::move(removal));

  for (Model* m : listeners()) co_await m->afterRoomEntered(RoomType::Shop);
  // LordsParasol.PurchaseEverything: every card, relic and potion for free, then a removal.
  if (hasRelic("LordsParasol")) {
    screen = Screen::Shop;
    co_await wait(0.75);
    for (auto& it : shop) {
      if (!it.stocked() || it.kind == ShopItem::Removal) continue;
      if (it.kind == ShopItem::CardItem) addCardToDeck(std::move(it.card));
      else if (it.kind == ShopItem::RelicItem) co_await obtainRelic(std::move(it.relic));
      else if (hasOpenPotionSlot()) procurePotion(std::move(it.potion));
      co_await wait(0.25);
    }
    screen = Screen::Shop;  // a bought relic may have shown its own reward screen
    std::vector<Card*> picked;
    if (!shop.back().used)  // Hoarder: no removal service
      picked = co_await selectFromDeck("merchant_room.MERCHANT.cardRemovalService.title", nullptr, 1, false);
    screen = Screen::Shop;
    if (!picked.empty()) {
      removeCardFromDeck(picked[0]);
      ++shopRemovalsUsed;
      shop.back().used = true;
    }
  }
  for (;;) {
    screen = Screen::Shop;
    int i = co_await shopChoice.next();
    if (i < 0 || i >= (int)shop.size()) break;
    ShopItem& it = shop[i];
    if (!it.stocked()) continue;
    int price = shopPrice(it);
    if (price > gold) { shopMessage = "MERCHANT.talk.purchaseFailureGold"; continue; }
    shopMessage.clear();
    bool refill = false;
    for (Model* m : listeners()) refill = refill || m->shouldRefillMerchantEntry();
    int spent = price;
    switch (it.kind) {
      case ShopItem::CardItem:
        gold -= price;
        addCardToDeck(std::move(it.card));
        if (refill) { it.onSale = false; fillCard(*this, it); }
        break;
      case ShopItem::RelicItem:
        gold -= price;
        co_await obtainRelic(std::move(it.relic));
        if (refill) fillRelic(*this, it, rollRelicRarity(rng("Rewards")));
        break;
      case ShopItem::PotionItem:
        if (!hasOpenPotionSlot()) { shopMessage = "MERCHANT.talk.purchaseFailureSpace"; continue; }
        gold -= price;
        procurePotion(std::move(it.potion));
        if (refill) fillPotion(*this, it, randomPotion(shops, false));
        break;
      case ShopItem::Removal: {
        // MerchantCardRemovalEntry: pick a card to remove (cancelable); the next removal
        // costs 25 more for the rest of the run.
        auto picked = co_await selectFromDeck("merchant_room.MERCHANT.cardRemovalService.title", nullptr, 1, true);
        screen = Screen::Shop;
        if (picked.empty()) { spent = 0; continue; }
        gold -= price;
        removeCardFromDeck(picked[0]);
        ++shopRemovalsUsed;
        it.used = true;
        break;
      }
    }
    badges::noteGoldSpent(*this, spent);  // PlayerMapPointHistoryEntry.GoldSpent (M7, KACHING)
    for (Model* m : listeners()) co_await m->afterItemPurchased(spent);
  }
  shop.clear();
}

}  // namespace sts
