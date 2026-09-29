// Package A3c: relics that enchant cards (or hand enchanted / upgraded cards out as rewards).
// Translated from MegaCrit.Sts2.Core.Models.Relics: GnarledHammer, Kifuda, RoyalStamp, PunchDagger,
// WingCharm, MysticLighter (Shop); BeautifulBracelet, TriBoomerang, ElectricShrymp, NutritiousSoup,
// PaelsClaw, Glitter, SilkenTress, SilverCrucible (Ancient / event pool).
//
// Still skipped: DingyRug (needs the colorless card pool, A1a); PaelsGrowth (its CLONE rest site
// option, CloneRestSiteOption, is not ported; the Clone enchantment exists in enchantments_b.cpp).
//
// PORT NOTE (all reward relics): Relic::modifyCardReward has no CardCreationOptions; the flags
// IsCardReward / NoCardPoolModifications are implied (it is only called for card rewards), and the
// reward cards are fresh copies, so they are enchanted / upgraded in place instead of via
// CloneCard + CardCreationResult.ModifyCard. AfterModifyingCardRewardOptions runs at the end of
// the late pass.
// PORT NOTE: "CardCmd.Preview" / NCardEnchantVfx and the enchant preview screen are UI (S13).
#include "game.h"

namespace sts {

namespace {

template <class R> void reg() { db::registerRelic(R::kId, [] { return std::unique_ptr<Relic>(new R()); }); }

// Enchant `count` cards picked from the deck with enchantment `id` (FromDeckForEnchantment + Enchant).
Task<> pickAndEnchant(Run* run, const char* id, int count, int amount) {
  auto picked = co_await run->selectForEnchantment(id, count);
  for (Card* c : picked) run->enchantCard(c, id, amount);
}

// Enchant every deck card the enchantment fits and `filter` accepts.
void enchantAll(Run* run, const char* id, int amount, bool (*filter)(const Card&) = nullptr) {
  auto e = db::enchantment(id);
  if (!e) return;
  std::vector<Card*> cards;
  for (auto& c : run->deck) cards.push_back(c.get());
  for (Card* c : cards)
    if (e->canEnchant(*c) && (!filter || filter(*c))) run->enchantCard(c, id, amount);
}

// Enchant offered reward cards the enchantment fits.
void enchantRewards(std::vector<std::unique_ptr<Card>>& cards, const char* id, int amount) {
  auto probe = db::enchantment(id);
  if (!probe) return;
  for (auto& c : cards)
    if (c && probe->canEnchant(*c)) cmd::enchant(c.get(), db::enchantment(id), amount);
}

// ---------------------------------------------------------------- Shop

// GnarledHammer.cs: enchant up to 3 cards with Sharp 3.
struct GnarledHammer : Relic {
  RELIC_HEADER(GnarledHammer, "GNARLED_HAMMER", Shop) addVar("Cards", 3); addVar("SharpAmount", 3); }
  Task<> afterObtained() override { co_await pickAndEnchant(run, "Sharp", val("Cards").toInt(), val("SharpAmount").toInt()); }
};

// Kifuda.cs: enchant up to 3 cards with Adroit 3.
struct Kifuda : Relic {
  RELIC_HEADER(Kifuda, "KIFUDA", Shop) addVar("Cards", 3); }
  Task<> afterObtained() override { co_await pickAndEnchant(run, "Adroit", val("Cards").toInt(), 3); }
};

// RoyalStamp.cs: enchant a card with RoyallyApproved. The eligible cards are shuffled with the
// Niche stream first (UnstableShuffle) before the choice.
struct RoyalStamp : Relic {
  RELIC_HEADER(RoyalStamp, "ROYAL_STAMP", Shop) addVar("Cards", 1); }
  Task<> afterObtained() override {
    auto e = db::enchantment("RoyallyApproved");
    std::vector<Card*> list;
    for (auto& c : run->deck) if (e->canEnchant(*c)) list.push_back(c.get());
    run->rng("Niche").shuffle(list);  // only the RNG use matters: the picker shows the deck order
    co_await pickAndEnchant(run, "RoyallyApproved", 1, 1);
  }
};

// PunchDagger.cs: enchant a card with Momentum 5.
struct PunchDagger : Relic {
  RELIC_HEADER(PunchDagger, "PUNCH_DAGGER", Shop) addVar("Momentum", 5); }
  Task<> afterObtained() override { co_await pickAndEnchant(run, "Momentum", 1, val("Momentum").toInt()); }
};

// WingCharm.cs: one random card of every card reward gets Swift 1.
struct WingCharm : Relic {
  RELIC_HEADER(WingCharm, "WING_CHARM", Shop) addVar("SwiftAmount", 1); }
  void modifyCardReward(std::vector<std::unique_ptr<Card>>& cards, RoomType, bool late) override {
    if (!late) return;
    auto swift = db::enchantment("Swift");
    std::vector<Card*> fits;
    for (auto& c : cards) if (c && swift->canEnchant(*c)) fits.push_back(c.get());
    if (fits.empty()) return;
    Card* pick = run->rng("Niche").nextItem(fits);
    doFlash();
    cmd::enchant(pick, db::enchantment("Swift"), val("SwiftAmount").toInt());
  }
};

// MysticLighter.cs: +9 damage on enchanted cards' powered attacks.
struct MysticLighter : Relic {
  RELIC_HEADER(MysticLighter, "MYSTIC_LIGHTER", Shop) addVar("Damage", 9); }
  Dec modifyDamageAdditive(Creature*, Dec, int props, Creature*, Card* card) override {
    if (!isPoweredAttack(props) || !card || !card->enchantment) return 0;
    return val("Damage");
  }
};

// ---------------------------------------------------------------- Ancient

// BeautifulBracelet.cs: 4 random deck cards get Swift 2 (Niche stream).
struct BeautifulBracelet : Relic {
  RELIC_HEADER(BeautifulBracelet, "BEAUTIFUL_BRACELET", Ancient) addVar("Cards", 4); addVar("Swift", 2); }
  Task<> afterObtained() override {
    auto e = db::enchantment("Swift");
    std::vector<Card*> list;
    for (auto& c : run->deck) if (e->canEnchant(*c)) list.push_back(c.get());
    run->rng("Niche").shuffle(list);  // TakeRandom
    int n = std::min((int)list.size(), val("Cards").toInt());
    for (int i = 0; i < n; ++i) run->enchantCard(list[i], "Swift", val("Swift").toInt());
    co_return;
  }
};

// TriBoomerang.cs: enchant 3 cards with Instinct.
struct TriBoomerang : Relic {
  RELIC_HEADER(TriBoomerang, "TRI_BOOMERANG", Ancient) addVar("Cards", 3); }
  Task<> afterObtained() override { co_await pickAndEnchant(run, "Instinct", val("Cards").toInt(), 1); }
};

// ElectricShrymp.cs: enchant a card with Imbued.
struct ElectricShrymp : Relic {
  RELIC_HEADER(ElectricShrymp, "ELECTRIC_SHRYMP", Ancient) }
  Task<> afterObtained() override { co_await pickAndEnchant(run, "Imbued", 1, 1); }
};

// NutritiousSoup.cs: every basic Strike gets TezcatarasEmber.
struct NutritiousSoup : Relic {
  RELIC_HEADER(NutritiousSoup, "NUTRITIOUS_SOUP", Ancient) }
  Task<> afterObtained() override {
    enchantAll(run, "TezcatarasEmber", 1, [](const Card& c) { return c.rarity == Rarity::Basic && (c.tags & tagStrike) != 0; });
    co_return;
  }
};

// PaelsClaw.cs: every card that can hold Goopy gets it.
struct PaelsClaw : Relic {
  RELIC_HEADER(PaelsClaw, "PAELS_CLAW", Ancient) addVar("Cards", 3); }
  Task<> afterObtained() override { enchantAll(run, "Goopy", 1); co_return; }
};

// Glitter.cs: every offered reward card that can hold Glam gets Glam 1.
struct Glitter : Relic {
  RELIC_HEADER(Glitter, "GLITTER", Ancient) }
  void modifyCardReward(std::vector<std::unique_ptr<Card>>& cards, RoomType, bool late) override {
    if (late) enchantRewards(cards, "Glam", 1);
  }
};

// SilkenTress.cs: lose all gold; the first card reward offers Glam cards, then it is used up.
// (IsUsed is the base usedUp, which the relic save already keeps.)
struct SilkenTress : Relic {
  RELIC_HEADER(SilkenTress, "SILKEN_TRESS", Ancient) }
  Task<> afterObtained() override { run->gold = 0; co_return; }  // PlayerCmd.LoseGold(all)
  void modifyCardReward(std::vector<std::unique_ptr<Card>>& cards, RoomType, bool late) override {
    if (!late || usedUp) return;
    enchantRewards(cards, "Glam", 1);
    usedUp = true;  // AfterModifyingCardRewardOptions
  }
};

// SilverCrucible.cs: the first 3 card rewards are upgraded; the first treasure room after that is
// empty (ShouldGenerateTreasure: TreasureRoomsEntered > 1).
struct SilverCrucible : Relic {
  RELIC_HEADER(SilverCrucible, "SILVER_CRUCIBLE", Ancient) addVar("Cards", 3); }
  int timesUsed = 0, treasureRoomsEntered = 0;
  void persist(Archive& a) override { a.io(timesUsed); a.io(treasureRoomsEntered); }
  bool showCounter() const override { return const_cast<SilverCrucible*>(this)->val("Cards").toInt() > 0 && timesUsed < const_cast<SilverCrucible*>(this)->val("Cards").toInt(); }
  int displayAmount() const override { return const_cast<SilverCrucible*>(this)->val("Cards").toInt() - timesUsed; }
  void checkUsedUp() { usedUp = timesUsed >= val("Cards").toInt() && treasureRoomsEntered > 0; }
  void modifyCardReward(std::vector<std::unique_ptr<Card>>& cards, RoomType, bool late) override {
    if (!late || timesUsed >= val("Cards").toInt()) return;
    for (auto& c : cards) if (c && c->upgradable()) c->upgrade();
    ++timesUsed;  // AfterModifyingCardRewardOptions
    checkUsedUp();
  }
  Task<> afterRoomEntered(RoomType room) override {
    if (room == RoomType::Treasure) { ++treasureRoomsEntered; checkUsedUp(); }
    co_return;
  }
  bool shouldGenerateTreasure() override { return treasureRoomsEntered > 1; }
};

}  // namespace

void registerRelicsEnchant() {
  reg<GnarledHammer>();
  reg<Kifuda>();
  reg<RoyalStamp>();
  reg<PunchDagger>();
  reg<WingCharm>();
  reg<MysticLighter>();
  reg<BeautifulBracelet>();
  reg<TriBoomerang>();
  reg<ElectricShrymp>();
  reg<NutritiousSoup>();
  reg<PaelsClaw>();
  reg<Glitter>();
  reg<SilkenTress>();
  reg<SilverCrucible>();
}

}  // namespace sts
