// Package A8: event-pool relics whose source (Neow / the Ancients / Touch of Orobas) was already
// ported but whose systems were missing, the Event potion Ambergris, and the four refined
// starter relics (Touch of Orobas upgrades). Translated from MegaCrit.Sts2.Core.Models.Relics /
// .Potions.
//
// Elsewhere: LeadPaperweight in relics_shared2.cpp (A5); PaelsGrowth, SeaGlass, PrismaticGem,
// Kaleidoscope, WhisperingEarring, Driftwood, PaelsWing in relics_ancient2.cpp; the enchanting event
// relics in relics_enchant.cpp (A3c); WingedBoots, DowsingRod, ScrollBoxes in quests.cpp (E2).
// Byrdpip, PaelsLegion (pets) are in pets.cpp. Still unregistered: GoldenCompass (golden path);
// FurCoat (map marks); ToyBox (wax relics); MassiveScroll (multiplayer only).
#include "cards.h"
#include "char_defect.h"
#include "char_necrobinder.h"

namespace sts {

namespace {

template <class R> void reg() { db::registerRelic(R::kId, [] { return std::unique_ptr<Relic>(new R()); }); }

// ---------------------------------------------------------------- potion

// AmbergrisPower.cs: while Amount > 0 the owner takes another turn instead of the enemies'
// (Hook.ShouldTakeExtraTurn); each extra turn taken uses one. PORT NOTE (n/a: visual): IsVisibleInternal is
// false in the C#; here it is applied silently but still listed with the owner's powers.
struct AmbergrisPower : Power {
  POWER_HEADER(AmbergrisPower, "AMBERGRIS_POWER")
  bool shouldTakeExtraTurn() override { return amount > 0 && owner && owner->isPlayer; }
  Task<> afterTakingExtraTurn() override { co_await cmd::decrement(this); }
};

// Ambergris.cs (Event rarity): heal 50% of max HP; in combat, also take an extra turn (AmbergrisPower).
struct Ambergris : Potion {
  POTION_HEADER(Ambergris, "AMBERGRIS", Event, AnyTime, Self) addVar("HealPercent", 50); }
  Task<> onUse(Creature* t) override {
    Dec amount = Dec(t->maxHp) * val("HealPercent") / Dec(100);
    if (run->combat && run->combat->inProgress && !run->combat->over) {
      co_await cmd::heal(t, amount);
      co_await applyPower<AmbergrisPower>(t, 1, run->combat->player, nullptr, true);
      co_return;
    }
    t->hp = std::min(t->maxHp, t->hp + amount.toInt());
    co_await wait(0.2);
  }
};

// ---------------------------------------------------------------- Neow / Ancient relics

// LostCoffer.cs: RewardsCmd.OfferCustom of a card reward (character pool, Source Other, regular
// encounter odds) and a potion reward, populated in that order.
struct LostCoffer : Relic {
  RELIC_HEADER(LostCoffer, "LOST_COFFER", Ancient) }
  Task<> afterObtained() override {
    std::vector<Run::RewardItem> rows;
    CardCreationOptions o;
    o.pools = {run->characterId};
    rows.push_back(run->makeCardReward(o, 3));
    Run::RewardItem potion;
    potion.kind = Run::RewardKind::Potion;
    potion.potion = run->randomPotion(run->rng("Rewards"), false);
    rows.push_back(std::move(potion));
    co_await run->offerRewards(std::move(rows));
  }
};

// PhialHolster.cs: one more potion slot and two random potions.
struct PhialHolster : Relic {
  RELIC_HEADER(PhialHolster, "PHIAL_HOLSTER", Ancient) addVar("PotionSlots", 1); addVar("Potions", 2); }
  Task<> afterObtained() override {
    run->potions.resize(run->potions.size() + (size_t)val("PotionSlots").toInt());
    for (auto& p : run->randomPotions(val("Potions").toInt(), run->rng("CombatPotionGeneration")))
      run->procurePotion(std::move(p));
    co_return;
  }
};

// NeowsSacrifice.cs: an Ambergris and a Guilty curse.
struct NeowsSacrifice : Relic {
  RELIC_HEADER(NeowsSacrifice, "NEOWS_SACRIFICE", Ancient) }
  Task<> afterObtained() override {
    auto p = db::potion("Ambergris");
    p->run = run;
    run->procurePotion(std::move(p));
    run->addCardToDeck(db::card("Guilty"));
    co_return;
  }
};

// ---------------------------------------------------------------- refined starter relics

// InfusedCore.cs (Starter): three Lightning orbs on turn 1; Lightning orbs are worth 1 more.
struct InfusedCore : Relic {
  RELIC_HEADER(InfusedCore, "INFUSED_CORE", Starter) addVar("Lightning", 3); addVar("ExtraDamage", 1); }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!combat || !contains(participants, owner()) || combat->turnNumber > 1) co_return;
    for (int i = 0; i < val("Lightning").toInt(); ++i) co_await cmd::channelOrb(*combat, std::make_unique<LightningOrb>());
  }
  Dec modifyOrbValue(Orb* orb, Dec value) override {
    if (orb->id != "LightningOrb") return value;
    return value + val("ExtraDamage");
  }
};

// RingOfTheDrake.cs (Starter): draw 2 more cards on the first 3 turns.
struct RingOfTheDrake : Relic {
  RELIC_HEADER(RingOfTheDrake, "RING_OF_THE_DRAKE", Starter) addVar("Cards", 2); addVar("Turns", 3); }
  Dec modifyHandDraw(Dec amount) override {
    if (!combat || Dec(combat->turnNumber) > val("Turns")) return amount;
    return amount + val("Cards");
  }
};

// DivineDestiny.cs (Starter): 7 stars at the start of turn 1.
struct DivineDestiny : Relic {
  RELIC_HEADER(DivineDestiny, "DIVINE_DESTINY", Starter) addVar("Stars", 7); }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!combat || !contains(participants, owner()) || combat->turnNumber > 1) co_return;
    co_await cmd::gainStars(*combat, val("Stars").toInt());
  }
};

// PhylacteryUnbound.cs (Starter): summon Osty for 5 before combat and for 2 every turn.
struct PhylacteryUnbound : Relic {
  RELIC_HEADER(PhylacteryUnbound, "PHYLACTERY_UNBOUND", Starter) addVar("StartOfCombat", 5); addVar("StartOfTurn", 2); }
  Task<> beforeCombatStart() override { co_await summonOsty(*combat, val("StartOfCombat").toInt()); }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!combat || !contains(participants, owner())) co_return;
    co_await summonOsty(*combat, val("StartOfTurn").toInt());
  }
};

}  // namespace

void registerRelicsEvent() {
  registerPowerType<AmbergrisPower>();
  db::registerPotion(Ambergris::kId, [] { return std::unique_ptr<Potion>(new Ambergris()); });
  reg<LostCoffer>();
  reg<PhialHolster>();
  reg<NeowsSacrifice>();
  reg<InfusedCore>();
  reg<RingOfTheDrake>();
  reg<DivineDestiny>();
  reg<PhylacteryUnbound>();
}

}  // namespace sts
