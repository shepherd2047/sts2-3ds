// Relic registration: each relics_*.cpp translates a batch of the game's relics
// (MegaCrit.Sts2.Core.Models.Relics) and registers them here. Only registered
// relics enter the grab bags, so unported ones simply never drop.
#include "game.h"

namespace sts {

void registerRelicsCommon();    // relics_common.cpp
void registerRelicsUncommon();  // relics_uncommon.cpp
void registerRelicsRare();      // relics_rare.cpp (rare, shop and Ironclad pool)
void registerRelicsMore();      // relics_more.cpp (package 10)
void registerRelicsEvent();     // relics_event.cpp (A8)
// The Defect's 8 relics (char_defect_relics.cpp) register from char_defect.cpp's registerDefect()
// instead of here, alongside its cards, orbs and potions.

void registerRelics() {
  registerRelicsCommon();
  registerRelicsUncommon();
  registerRelicsRare();
  registerRelicsMore();
  registerRelicsEvent();
}

}  // namespace sts
