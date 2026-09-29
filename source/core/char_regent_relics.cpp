// The Regent's character relic pool (X3.1, RegentRelicPool.cs, 8 relics) and 3 potions
// (Regent4Epoch.Potions), see char_regent.h/.cpp for the shared systems these build on
// (Combat::stars, cmd::gainStars, cmd::forge, the Sovereign Blade token).
// Translated from MegaCrit.Sts2.Core.Models.Relics / .Potions.
#include "cards.h"
#include "char_regent.h"
#include "colorless.h"

namespace sts {

namespace {

bool isCombatRoom(RoomType t) { return t == RoomType::Monster || t == RoomType::Elite || t == RoomType::Boss; }

// ---------------------------------------------------------------- relics

// ---- DivineRight (Starter): entering a combat room gives 3 stars. ----
struct DivineRight : Relic {
  RELIC_HEADER(DivineRight, "DIVINE_RIGHT", Starter) addVar("Stars", 3); }
  Task<> afterRoomEntered(RoomType room) override {
    if (!isCombatRoom(room) || !combat) co_return;
    co_await cmd::gainStars(*combat, val("Stars").toInt());
  }
};

// ---- FencingManual (Common): forges 10 damage at the start of the first turn. ----
struct FencingManual : Relic {
  RELIC_HEADER(FencingManual, "FENCING_MANUAL", Common) addVar("Forge", 10); }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!combat || combat->turnNumber > 1 || !contains(participants, owner())) co_return;
    co_await cmd::forge(*combat, val("Forge"), this);
  }
};

// ---- GalacticDust (Uncommon): every 10 stars spent (across turns) gives 10 block. ----
struct GalacticDust : Relic {
  RELIC_HEADER(GalacticDust, "GALACTIC_DUST", Uncommon) addVar("Stars", 10); addVar("Block", 10); }
  int starsSpent = 0;
  void persist(Archive& a) override { a.io(starsSpent); }
  bool showCounter() const override { return true; }
  int displayAmount() const override {
    int need = const_cast<GalacticDust*>(this)->val("Stars").toInt();
    return need > 0 ? starsSpent % need : 0;
  }
  // PORT NOTE: the ~1s "activating" display flip (IsActivating) is a cosmetic UI-only detail.
  Task<> afterStarsSpent(int amount) override {
    starsSpent += amount;
    int need = val("Stars").toInt();
    if (need > 0 && starsSpent >= need) {
      doFlash();
      co_await cmd::gainBlock(owner(), val("Block") * Dec(starsSpent / need), kUnpowered, nullptr);
      starsSpent %= need;
    }
  }
};

// ---- LunarPastry (Rare): gains 1 star at the end of every turn. ----
struct LunarPastry : Relic {
  RELIC_HEADER(LunarPastry, "LUNAR_PASTRY", Rare) addVar("Stars", 1); }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (!combat || !contains(participants, owner())) co_return;
    co_await cmd::gainStars(*combat, val("Stars").toInt());
  }
};

// ---- MiniRegent (Rare): the first time stars are spent each turn, gain 1 Strength. ----
struct MiniRegent : Relic {
  RELIC_HEADER(MiniRegent, "MINI_REGENT", Rare) addVar("StrengthPower", 1); }
  bool usedThisTurn = false;
  Task<> beforeSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner())) usedThisTurn = false;
    co_return;
  }
  Task<> afterCombatEnd() override { usedThisTurn = false; co_return; }
  Task<> afterStarsSpent(int) override {
    if (usedThisTurn) co_return;
    usedThisTurn = true;
    doFlash();
    co_await applyPower<StrengthPower>(owner(), val("StrengthPower"), owner(), nullptr);
  }
};

// ---- OrangeDough (Rare): the first turn, add 2 distinct cards from the pool to hand. ----
struct OrangeDough : Relic {
  RELIC_HEADER(OrangeDough, "ORANGE_DOUGH", Rare) addVar("Cards", 2); }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!combat || combat->turnNumber > 1 || !contains(participants, owner())) co_return;
    doFlash();
    for (auto& k : colorlessDistinctForCombat(*combat, val("Cards").toInt()))
      co_await cmd::addGeneratedCard(*combat, std::move(k), Pile::Hand);
  }
};

// ---- Regalite (Uncommon): the first card generated for combat each turn gives 4 block. ----
struct Regalite : Relic {
  RELIC_HEADER(Regalite, "REGALITE", Uncommon) addVar("Block", 4); }
  bool usedThisTurn = false;
  Task<> beforeSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner())) usedThisTurn = false;
    co_return;
  }
  Task<> afterCombatEnd() override { usedThisTurn = false; co_return; }
  // cmd::addGeneratedCard fires afterCardEnteredCombat only for generated cards (Forge, Discovery,
  // Shiv, ...), matching Hook.AfterCardGeneratedForCombat here (no separate hook needed).
  Task<> afterCardEnteredCombat(Card* card) override {
    if (usedThisTurn || !card->createdByPlayer) co_return;  // creator == Owner
    usedThisTurn = true;
    doFlash();
    co_await cmd::gainBlock(owner(), val("Block"), kUnpowered, nullptr);
  }
};

// ---- VitruvianMinion (Shop): the player's Minion cards deal and block double. ----
struct VitruvianMinion : Relic {
  RELIC_HEADER(VitruvianMinion, "VITRUVIAN_MINION", Shop) }
  Dec modifyDamageMultiplicative(Creature*, Dec, int, Creature*, Card* src) override {
    return (src && (src->tags & tagMinion)) ? Dec(2) : Dec(1);
  }
  Dec modifyBlockMultiplicative(Creature*, Dec, int, Card* src) override {
    return (src && (src->tags & tagMinion)) ? Dec(2) : Dec(1);
  }
};

// ---------------------------------------------------------------- potions

struct RegentPotionBase : Potion {
  Combat& c() { return *run->combat; }
  Creature* me() { return run->player.get(); }
};

// ---- StarPotion (Common): gain 3 stars. ----
struct StarPotion : RegentPotionBase {
  POTION_HEADER(StarPotion, "STAR_POTION", Common, CombatOnly, Self) addVar("Stars", 3); }
  Task<> onUse(Creature*) override { co_await cmd::gainStars(c(), val("Stars").toInt()); }
};

// ---- CosmicConcoction (Rare): add 3 distinct upgraded cards from the pool to hand. ----
struct CosmicConcoction : RegentPotionBase {
  POTION_HEADER(CosmicConcoction, "COSMIC_CONCOCTION", Rare, CombatOnly, Self) addVar("Cards", 3); }
  Task<> onUse(Creature*) override {
    for (auto& k : colorlessDistinctForCombat(c(), val("Cards").toInt())) {
      if (k->upgradable()) k->upgrade();
      co_await cmd::addGeneratedCard(c(), std::move(k), Pile::Hand);
    }
  }
};

// ---- KingsCourage (Uncommon): forges 15 damage. ----
struct KingsCourage : RegentPotionBase {
  POTION_HEADER(KingsCourage, "KINGS_COURAGE", Uncommon, CombatOnly, Self) addVar("Forge", 15); }
  Task<> onUse(Creature*) override { co_await cmd::forge(c(), val("Forge"), this); }
};

template <class R> void reg() { db::registerRelic(R::kId, [] { return std::unique_ptr<Relic>(new R()); }); }
template <class P> void regPotion() { db::registerPotion(P::kId, [] { return std::unique_ptr<Potion>(new P()); }); }

}  // namespace

void registerRegentRelics() {
  reg<DivineRight>();
  reg<FencingManual>();
  reg<GalacticDust>();
  reg<LunarPastry>();
  reg<MiniRegent>();
  reg<OrangeDough>();
  reg<Regalite>();
  reg<VitruvianMinion>();
}

void registerRegentPotions() {
  regPotion<StarPotion>();
  regPotion<CosmicConcoction>();
  regPotion<KingsCourage>();
}

}  // namespace sts
