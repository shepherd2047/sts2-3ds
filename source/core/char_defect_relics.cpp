// The Defect's 8 character relics (RelicPools/DefectRelicPool.cs) and 3 potions (the
// Defect4Epoch.cs pool: FocusPotion, EssenceOfDarkness, PotionOfCapacity), X2.1. Registered from
// char_defect.cpp's registerDefect() into relics.cpp / potions.cpp's registries.
#include "cards.h"
#include "char_defect.h"

namespace sts {

namespace {
template <class R> void reg() { db::registerRelic(R::kId, [] { return std::unique_ptr<Relic>(new R()); }); }
template <class P> void regPotion() { db::registerPotion(P::kId, [] { return std::unique_ptr<Potion>(new P()); }); }
bool combatRoom(RoomType t) { return t == RoomType::Monster || t == RoomType::Elite || t == RoomType::Boss; }

// CrackedCore.cs (starter): at the start of turn 1, channel a Lightning orb.
struct CrackedCore : Relic {
  RELIC_HEADER(CrackedCore, "CRACKED_CORE", Starter)
    addVar("Lightning", 1);
  }
  Task<> beforeSideTurnStart(Side side, const std::vector<Creature*>& participants) override {
    if (side != Side::Player || !combat || combat->turnNumber > 1 || !contains(participants, owner())) co_return;
    for (int i = 0; i < val("Lightning").toInt(); ++i) co_await cmd::channelOrb(*combat, std::make_unique<LightningOrb>());
  }
};

// DataDisk.cs: gain Focus on entering a combat room.
struct DataDisk : Relic {
  RELIC_HEADER(DataDisk, "DATA_DISK", Common)
    addVar("FocusPower", 1);
  }
  Task<> afterRoomEntered(RoomType room) override {
    if (!combatRoom(room)) co_return;
    doFlash();
    co_await applyPower<FocusPower>(owner(), val("FocusPower"), owner(), nullptr);
  }
};

// EmotionChip.cs: if you lost HP last turn, trigger every queued orb's passive (countAffectedByHooks)
// at the start of this turn.
// "Lost HP last turn" = a DamageReceived entry on the owner, not fully blocked, that
// HappenedLastPlayerTurn.
// PORT NOTE (n/a: visual): the C#'s RelicStatus glow and Cmd.Wait(0.25f) pacing between orbs are cosmetic.
struct EmotionChip : Relic {
  RELIC_HEADER(EmotionChip, "EMOTION_CHIP", Rare) }
  bool lostHpInPreviousTurn() {
    Creature* me = owner();
    return combat && combat->history.any([&](const CombatHistoryEntry& e) {
      return e.kind == CombatHistoryEntry::DamageReceived && e.actor == me && !e.fullyBlocked &&
             CombatHistory::happenedLastPlayerTurn(e, *combat);
    });
  }
  Task<> afterDamageReceived(Creature* target, const DamageResult& result, int, Creature*, Card*) override {
    if (combat && combat->inProgress && target == owner() && result.unblocked > 0) doFlash();
    return {};
  }
  Task<> afterPlayerTurnStart() override {
    if (!combat || !lostHpInPreviousTurn()) co_return;
    doFlash();
    std::vector<Orb*> orbs;
    for (auto& o : combat->orbQueue) orbs.push_back(o.get());
    for (Orb* o : orbs) co_await cmd::orbPassive(*combat, o, nullptr, true);
  }
};

// GoldPlatedCables.cs: the front (oldest queued) orb's passive triggers one extra time.
struct GoldPlatedCables : Relic {
  RELIC_HEADER(GoldPlatedCables, "GOLD_PLATED_CABLES", Uncommon) }
  int modifyOrbPassiveTriggerCount(Orb* orb, int count) override {
    if (!combat || combat->orbQueue.empty() || orb != combat->orbQueue.front().get()) return count;
    return count + 1;
  }
  Task<> afterModifyingOrbPassiveTriggerCount(Orb*) override {
    doFlash();
    return {};
  }
};

// PowerCell.cs: at the start of turn 1, add 2 random free (non-X-cost) cards from the draw pile
// to the hand.
struct PowerCell : Relic {
  RELIC_HEADER(PowerCell, "POWER_CELL", Rare)
    addVar("Cards", 2);
  }
  Task<> beforeSideTurnStart(Side side, const std::vector<Creature*>& participants) override {
    if (side != Side::Player || !combat || combat->turnNumber > 1 || !contains(participants, owner())) co_return;
    doFlash();
    std::vector<Card*> free;
    for (Card* c : combat->draw) if (!c->costsX && c->costWithLocalMods() == 0) free.push_back(c);
    // StableShuffle: sort, then Fisher-Yates (as cmd::shuffle).
    std::stable_sort(free.begin(), free.end(), [](Card* a, Card* b) { return a->id < b->id; });
    combat->rng("CombatCardSelection").shuffle(free);
    int n = std::min((int)free.size(), val("Cards").toInt());
    for (int i = 0; i < n; ++i) co_await cmd::moveCard(*combat, free[(size_t)i], Pile::Hand);
  }
};

// Metronome.cs: every 7th orb channeled this combat, hit every hittable enemy for 30 (Unpowered).
struct Metronome : Relic {
  RELIC_HEADER(Metronome, "METRONOME", Rare)
    addVar("Damage", 30);
    addVar("OrbCount", 7);
  }
  int orbsChanneled = 0;
  bool showCounter() const override { return combat && combat->inProgress; }
  int displayAmount() const override {
    for (auto& v : vars) if (v.name == "OrbCount") return std::min(orbsChanneled, v.base.toInt());
    return orbsChanneled;
  }
  Task<> afterRoomEntered(RoomType room) override {
    if (combatRoom(room)) orbsChanneled = 0;
    return {};
  }
  Task<> afterOrbChanneled(Orb*) override {
    ++orbsChanneled;
    if (orbsChanneled == val("OrbCount").toInt()) {
      doFlash();
      orbsChanneled = 0;
      if (combat) co_await cmd::damage(combat->hittableEnemies(), val("Damage"), kUnpowered, owner(), nullptr);
    }
  }
  Task<> afterCombatEnd() override { orbsChanneled = 0; return {}; }
};

// RunicCapacitor.cs (Shop relic): at the start of turn 1, add 3 orb slots.
struct RunicCapacitor : Relic {
  RELIC_HEADER(RunicCapacitor, "RUNIC_CAPACITOR", Shop)
    addVar("Repeat", 3);
  }
  Task<> afterSideTurnStart(Side side, const std::vector<Creature*>& participants) override {
    if (side != Side::Player || !combat || combat->turnNumber > 1 || !contains(participants, owner())) co_return;
    doFlash();
    co_await cmd::addOrbSlots(*combat, val("Repeat").toInt());
  }
};

// SymbioticVirus.cs: at the start of turn 1, channel a Dark orb.
struct SymbioticVirus : Relic {
  RELIC_HEADER(SymbioticVirus, "SYMBIOTIC_VIRUS", Uncommon)
    addVar("Dark", 1);
  }
  Task<> afterSideTurnStart(Side side, const std::vector<Creature*>& participants) override {
    if (side != Side::Player || !combat || combat->turnNumber > 1 || !contains(participants, owner())) co_return;
    for (int i = 0; i < val("Dark").toInt(); ++i) co_await cmd::channelOrb(*combat, std::make_unique<DarkOrb>());
  }
};

// ================================================================ potions

struct DefectPotionBase : Potion {
  Combat& c() { return *run->combat; }
  Creature* me() { return run->player.get(); }
};

// FocusPotion.cs: gain Focus.
struct FocusPotion : DefectPotionBase {
  POTION_HEADER(FocusPotion, "FOCUS_POTION", Common, CombatOnly, Self) addVar("FocusPower", 2); }
  Task<> onUse(Creature* t) override { co_await applyPower<FocusPower>(t, val("FocusPower"), me(), nullptr); }
};

// EssenceOfDarkness.cs: channel a Dark orb for every orb slot.
struct EssenceOfDarkness : DefectPotionBase {
  POTION_HEADER(EssenceOfDarkness, "ESSENCE_OF_DARKNESS", Rare, CombatOnly, Self) }
  Task<> onUse(Creature*) override {
    int count = c().orbCapacity;
    for (int i = 0; i < count; ++i) co_await cmd::channelOrb(c(), std::make_unique<DarkOrb>());
  }
};

// PotionOfCapacity.cs: add 2 orb slots.
struct PotionOfCapacity : DefectPotionBase {
  POTION_HEADER(PotionOfCapacity, "POTION_OF_CAPACITY", Uncommon, CombatOnly, Self) addVar("Repeat", 2); }
  Task<> onUse(Creature*) override { co_await cmd::addOrbSlots(c(), val("Repeat").toInt()); }
};

}  // namespace

void registerDefectRelics() {
  reg<CrackedCore>();
  reg<DataDisk>();
  reg<EmotionChip>();
  reg<GoldPlatedCables>();
  reg<PowerCell>();
  reg<Metronome>();
  reg<RunicCapacitor>();
  reg<SymbioticVirus>();
}

void registerDefectPotions() {
  regPotion<FocusPotion>();
  regPotion<EssenceOfDarkness>();
  regPotion<PotionOfCapacity>();
}

}  // namespace sts
