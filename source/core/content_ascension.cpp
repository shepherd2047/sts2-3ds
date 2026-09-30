// Content that only ascension needs (C10). The rest of the ascension rules live where the
// C# has them: Run::start (TightBelt, AscendersBane), rollRarity / cardReward (Scarcity),
// combatRewards (Poverty), shop.cpp (Inflation), mapgen (SwarmingElites), enterAncient
// (WearyTraveler) and the monsters' own values (ToughEnemies / DeadlyEnemies).
#include "cards.h"

namespace sts {

namespace {

// AscendersBane.cs: a curse that cannot be played or removed and vanishes at the end of the turn.
struct AscendersBane : IroncladT<AscendersBane> {
  bool canBeGeneratedInCombat() const override { return false; }
  CARD_HEADER(AscendersBane, "ASCENDERS_BANE", -1, Curse, Curse, None)
    keywords = kwEternal | kwUnplayable | kwEthereal;
    maxUpgradeLevel = 0;
  }
};

}  // namespace

void registerAscension() { registerCardType<AscendersBane>(); }

}  // namespace sts
