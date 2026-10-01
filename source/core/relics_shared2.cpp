// Package A5: the shared-pool relics that were skipped until the colorless pool, enchantments and
// Hook.ModifyPowerAmountGiven existed: DingyRug, Toolbox, UnsettlingLamp (SharedRelicPool).
// Translated from MegaCrit.Sts2.Core.Models.Relics.
// Also: RelicModel.IsAllowed / IsBeforeAct3TreasureChest (Run::removeDisallowedRelics in run.cpp).
//
// Pool diff (SharedRelicPool + the five character pools + Event pool vs RELIC_HEADER): every shared and
// character relic is now registered; the Ancient-ish ones (SeaGlass, PrismaticGem, PaelsGrowth,
// LeadPaperweight, Kaleidoscope, WhisperingEarring) are in relics_ancient2.cpp (A6). Still skipped
// (systems that are not built): Byrdpip, PaelsLegion (pets), DowsingRod (quest cards),
// FurCoat (map marks), GoldenCompass (golden path), MassiveScroll (multiplayer only), PaelsEye
// (extra turn), ScrollBoxes (bundle screen), ToyBox (wax relics),
// WingedBoots (free travel).
#include "colorless.h"
#include "game.h"

namespace sts {

namespace {

template <class R> void reg() { db::registerRelic(R::kId, [] { return std::unique_ptr<Relic>(new R()); }); }

// DingyRug.cs (Shop): every card reward also draws from the colorless pool (CardPools.Union(ColorlessCardPool),
// unless NoCardPoolModifications), through Hook.ModifyCardRewardCreationOptions (Run::createForReward).
struct DingyRug : Relic {
  RELIC_HEADER(DingyRug, "DINGY_RUG", Shop) }
  void modifyCardRewardCreationOptions(CardCreationOptions& o) override {
    if (o.has(ccNoCardPoolModifications) || !o.has(ccIsCardReward)) return;
    if (std::find(o.pools.begin(), o.pools.end(), CardCreationOptions::kColorless) == o.pools.end())
      o.pools.push_back(CardCreationOptions::kColorless);  // CardPools.Union(ColorlessCardPool)
  }
};

// Toolbox.cs (Shop): on turn 1, before the draw, choose 1 of 3 distinct colorless cards for the hand.
struct Toolbox : Relic {
  RELIC_HEADER(Toolbox, "TOOLBOX", Shop) addVar("Cards", 3); }
  Task<> beforeHandDraw() override {
    if (!combat || combat->turnNumber != 1) co_return;
    doFlash();
    auto cards = colorlessDistinctForCombat(*combat, val("Cards").toInt());
    co_await chooseGeneratedToHand(*combat, std::move(cards), false);
  }
};

// The C# `power is ITemporaryPower` and its InternallyAppliedPower id (Strength / Dexterity / Focus).
// PORT NOTE: no ITemporaryPower marker here; listed by id.
const char* internallyAppliedPower(const Power& p) {
  static const char* const kStrength[] = {"CoordinatePower", "DarkShacklesPower", "CrushUnderPower", "EnfeeblingTouchPower",
                                          "DyingStarPower", "FeedingFrenzyPower", "FlexPotionPower", "ManglePower",
                                          "MonarchsGazeStrengthDownPower", "PiercingWailPower", "ReptileTrinketPower",
                                          "ShacklingPotionPower", "SetupStrikePower"};
  static const char* const kDexterity[] = {"AnticipatePower", "FadePower", "HelicalDartPower", "SpeedPotionPower"};
  static const char* const kFocus[] = {"FocusedStrikePower", "HotfixPower", "HyperbeamFocusDownPower", "SynchronizePower"};
  for (const char* s : kStrength) if (p.id == s) return "StrengthPower";
  for (const char* s : kDexterity) if (p.id == s) return "DexterityPower";
  for (const char* s : kFocus) if (p.id == s) return "FocusPower";
  return nullptr;
}

// UnsettlingLamp.cs (Rare): the first debuff-applying card each combat doubles every debuff it applies.
// PORT NOTE: the C# keeps the PowerModel instances in DoubledPowers; only the internal power ids of the
// temporary ones matter (HasDoubledTemporaryPowerSource), so those are stored instead (a fresh power object
// can be destroyed after stacking). IsVisible is not checked (only AmbergrisPower is hidden, not ported);
// RelicStatus.Active is display only.
struct UnsettlingLamp : Relic {
  RELIC_HEADER(UnsettlingLamp, "UNSETTLING_LAMP", Rare) }
  Card* triggeringCard = nullptr;
  std::vector<std::string> doubledInternalIds;
  bool isFinishedTriggering = false;

  void reset() {
    triggeringCard = nullptr;
    doubledInternalIds.clear();
    isFinishedTriggering = false;
  }
  Task<> beforeCombatStart() override { reset(); return {}; }
  Task<> afterCombatEnd() override { reset(); return {}; }

  Task<> beforePowerAmountChanged(Power* power, Dec amount, Creature* target, Creature* applier, Card* src) override {
    if (triggeringCard || isFinishedTriggering || !src) co_return;
    if (applier != owner()) co_return;
    if (target->side == owner()->side) co_return;
    if (power->typeForAmount(amount) != PowerType::Debuff) co_return;
    if (target->power("ArtifactPower")) co_return;
    triggeringCard = src;
    if (const char* in = internallyAppliedPower(*power)) doubledInternalIds.push_back(in);
  }
  Dec modifyPowerAmountGivenMultiplicative(Power* power, Creature*, Dec amount, Creature*, Card* src) override {
    if (!triggeringCard || src != triggeringCard || isFinishedTriggering) return Dec(1);
    for (auto& in : doubledInternalIds)  // HasDoubledTemporaryPowerSource
      if (power->id == in) return Dec(1);
    if (power->typeForAmount(amount) != PowerType::Debuff) return Dec(1);
    return Dec(2);
  }
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (p.card != triggeringCard || isFinishedTriggering) co_return;
    doFlash();
    isFinishedTriggering = true;
  }
};

}  // namespace

void registerRelicsShared2() {
  reg<DingyRug>();
  reg<Toolbox>();
  reg<UnsettlingLamp>();
}

}  // namespace sts
