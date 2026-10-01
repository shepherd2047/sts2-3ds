// Package A8: event-pool relics whose source (Neow / the Ancients / Touch of Orobas) was already
// ported but whose systems were missing, the Event potion Ambergris, and the four refined
// starter relics (Touch of Orobas upgrades). Translated from MegaCrit.Sts2.Core.Models.Relics /
// .Potions.
//
// Still skipped (need systems owned elsewhere or not yet built): LeadPaperweight is in
// relics_shared2.cpp (A5); PaelsGrowth (CloneRestSiteOption); the enchanting event relics (BeautifulBracelet,
// TriBoomerang, ElectricShrymp, PaelsClaw, NutritiousSoup, Glitter, SilkenTress, SilverCrucible)
// are in relics_enchant.cpp (A3c); Byrdpip (pets +
// ByrdonisEgg hatching); WingedBoots and DowsingRod are in quests.cpp (E2);
// Driftwood (card reward reroll); PaelsWing (sacrificing card rewards); PaelsEye (extra turn);
// PaelsLegion (pets); GoldenCompass (golden path); FurCoat (map marks); ToyBox (wax relics);
// WhisperingEarring (turn-1 autoplay); ScrollBoxes (quests.cpp, E2); SeaGlass / PrismaticGem
// (other characters' card pools as reward sources); Kaleidoscope (needs the unlock state);
// MassiveScroll (multiplayer only).
#include "cards.h"
#include "char_defect.h"
#include "char_necrobinder.h"

namespace sts {

namespace {

template <class R> void reg() { db::registerRelic(R::kId, [] { return std::unique_ptr<Relic>(new R()); }); }

// ---------------------------------------------------------------- potion

// Ambergris.cs (Event rarity): heal 50% of max HP. PORT NOTE: AmbergrisPower (the extra turn:
// ShouldTakeExtraTurn / AfterTakingExtraTurn) is not applied; this engine has no extra-turn
// system yet, so only the heal happens.
struct Ambergris : Potion {
  POTION_HEADER(Ambergris, "AMBERGRIS", Event, AnyTime, Self) addVar("HealPercent", 50); }
  Task<> onUse(Creature* t) override {
    Dec amount = Dec(t->maxHp) * val("HealPercent") / Dec(100);
    if (run->combat && run->combat->inProgress && !run->combat->over) {
      co_await cmd::heal(t, amount);
      co_return;
    }
    t->hp = std::min(t->maxHp, t->hp + amount.toInt());
    co_await wait(0.2);
  }
};

// ---------------------------------------------------------------- Neow / Ancient relics

// LostCoffer.cs: a card reward (3 cards, regular encounter odds) and a potion reward.
// PORT NOTE: RewardsCmd.OfferCustom (one combined screen) is replaced by the card and potion
// screens in the same order; both are rolled first, as Populate does.
struct LostCoffer : Relic {
  RELIC_HEADER(LostCoffer, "LOST_COFFER", Ancient) }
  Task<> afterObtained() override {
    auto cards = run->cardReward(RoomType::Monster, 3);
    auto potion = run->randomPotion(run->rng("Rewards"), false);
    co_await run->chooseCardFor(std::move(cards));
    co_await run->offerPotion(std::move(potion));
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
