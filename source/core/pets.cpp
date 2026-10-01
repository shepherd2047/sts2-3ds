// Pets: the relics that give the player a pet creature (PlayerCmd.AddPet, cmd::addPet in combat.cpp;
// Combat::pets). Translated from MegaCrit.Sts2.Core.Models.Relics.Byrdpip / PaelsLegion and their
// MonsterModels (MegaCrit.Sts2.Core.Models.Monsters.Byrdpip / PaelsLegion: 9999 HP, no health bar,
// a single NOTHING_MOVE that loops, so they never act). Osty is the Necrobinder's own pet
// (Combat::osty, char_necrobinder.cpp).
//
// Pets are summoned in BeforeCombatStart (and in AfterObtained when picked up mid-combat) and
// belong to that combat, so nothing about them is saved between rooms.
// PORT NOTE (n/a: visual): the relics' [SavedProperty] Skin (Rng(Owner, Id).NextItem(SkinOptions))
// only picks the pet's Spine skin; the pets' Spine art is not baked into romfs yet (they would need
// skin support in tools/build_assets.py), so the combat screen draws a pet only once a
// creature/<id> sprite exists, and the skin is not kept. PaelsLegion's anim triggers
// (BlockTrigger / SleepTrigger / WakeUpTrigger) and RelicStatus.Active glow are visual too.
#include "cards.h"

namespace sts {

namespace {

template <class R> void reg() { db::registerRelic(R::kId, [] { return std::unique_ptr<Relic>(new R()); }); }

// Byrdpip.cs (Event): hatched from the ByrdonisEgg at a rest site (HatchRestSiteOption, Run::restSite
// option 7). AfterObtained turns every ByrdonisEgg into a ByrdSwoop.
struct Byrdpip : Relic {
  RELIC_HEADER(Byrdpip, "BYRDPIP", Event) }
  Task<> afterObtained() override {
    std::vector<Card*> deckEggs, combatEggs;
    for (auto& c : run->deck) if (c->id == "ByrdonisEgg") deckEggs.push_back(c.get());
    if (combat && combat->inProgress)
      for (Card* c : combat->allCards()) if (c->id == "ByrdonisEgg") combatEggs.push_back(c);
    for (Card* c : deckEggs) run->transformCard(c, db::card("ByrdSwoop"));  // CardCmd.TransformTo<ByrdSwoop>
    for (Card* c : combatEggs) co_await cmd::transform(*combat, c, db::card("ByrdSwoop"));
    if (combat && combat->inProgress) co_await cmd::addPet(*combat, "Byrdpip");
  }
  Task<> beforeCombatStart() override {
    if (combat) co_await cmd::addPet(*combat, "Byrdpip");
  }
};

// PaelsLegion.cs (Ancient, from Pael): the first card play that gains block while it is ready gains
// double block; then it sleeps for Turns (2) turn starts.
struct PaelsLegion : Relic {
  RELIC_HEADER(PaelsLegion, "PAELS_LEGION", Ancient)
    addVar("Turns", 2);
  }
  int cooldown = 0;
  bool triggeredBlockLastTurn = false;
  Card* affectedCard = nullptr;  // AffectedCardPlay (the play is identified by its card here)

  bool showCounter() const override { return displayAmount() > 0; }
  int displayAmount() const override {
    if (!combat || !combat->inProgress || cooldown <= 0) return -1;
    return cooldown;
  }
  void reset() {
    cooldown = 0;
    triggeredBlockLastTurn = false;
    affectedCard = nullptr;
  }
  Task<> afterObtained() override {
    if (combat && combat->inProgress) co_await cmd::addPet(*combat, "PaelsLegion");
  }
  Task<> beforeCombatStart() override {
    // Port-only reset: the C# state is not saved, so a combat always starts from zero there too.
    reset();
    if (combat) co_await cmd::addPet(*combat, "PaelsLegion");
  }
  bool doubles(int props, Card* card) const {
    return (props & kMove) && card && ownerOf(card) == owner() && cooldown <= 0;  // IsCardOrMonsterMove
  }
  Dec modifyBlockMultiplicative(Creature*, Dec, int props, Card* card) override { return doubles(props, card) ? 2 : 1; }
  // AfterModifyingBlockAmount (called only on the models that modified the block): afterBlockGained
  // sees the same final amount and card; `cardPlay != null` = the card is being played right now.
  Task<> afterBlockGained(Creature*, Dec amount, int props, Card* card) override {
    if (!doubles(props, card) || amount <= Dec(0) || !combat) return {};
    if (std::find(combat->play.begin(), combat->play.end(), card) == combat->play.end()) return {};
    if (affectedCard && affectedCard != card) return {};
    affectedCard = card;
    return {};
  }
  Task<> afterCardPlayed(const CardPlay& cp) override {
    if (!affectedCard || affectedCard != cp.card) return {};
    doFlash();
    affectedCard = nullptr;
    cooldown = val("Turns").toInt();
    triggeredBlockLastTurn = true;
    return {};
  }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner())) return {};
    --cooldown;
    triggeredBlockLastTurn = false;
    return {};
  }
  Task<> afterCombatEnd() override {
    reset();
    return {};
  }
};

}  // namespace

void registerPetRelics() {
  reg<Byrdpip>();
  reg<PaelsLegion>();
}

}  // namespace sts
