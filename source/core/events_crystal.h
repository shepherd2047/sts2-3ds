// CrystalSphere minigame state (package A7d, MegaCrit.Sts2.Core.Events.Custom.CrystalSphereEvent):
// shared by the event (events_crystal.cpp), its bottom-screen UI (ui/screens/crystal_ui.cpp) and
// the headless sim. The minigame lives while the CrystalSphere event runs; the UI shows it instead
// of the event page whenever crystalSphereGame(run) is not null.
#pragma once
#include "game.h"

namespace sts {

struct CrystalSphereGame {
  static constexpr int kSize = 11;  // CrystalSphereMinigame._defaultWidth / _defaultHeight
  enum class Tool { None, Small, Big };  // CrystalSphereToolType
  enum class ItemType { CardReward, Curse, Gold, Potion, Relic };  // CrystalSphereItemType
  // Minigame -> rewards screen (RewardsCmd.OfferCustom) -> Done (the proceed button).
  enum class Phase { Playing, Rewards, Done };

  // CrystalSphereItem and its subclasses.
  struct Item {
    ItemType type = ItemType::Gold;
    Rarity cardRarity = Rarity::Common;               // CardReward
    PotionRarity potionRarity = PotionRarity::Common;  // Potion
    bool bigGold = false;                             // Gold
    int x = 0, y = 0, w = 1, h = 1;                   // Position, Size (in cells)
    bool placed = false;  // PlaceItem succeeded (an item that did not fit stays at (0, 0), off the grid)
    bool revealed = false;
    int subscriptions = 0;  // times PopulateItems hooked Revealed (see the note in events_crystal.cpp)
    bool isGood() const { return type != ItemType::Curse; }
  };

  bool hidden[kSize][kSize];   // [x][y]: CrystalSphereCell.IsHidden (still under the fog)
  int itemAt[kSize][kSize];    // [x][y]: index into items, -1 if empty
  std::vector<Item> items;     // _items
  std::vector<int> revealed;   // _revealed, in reveal order (rewards are handed out in this order)
  int divinations = 0;         // DivinationCount
  Tool tool = Tool::Big;       // CrystalSphereTool (the UI sets it: SetTool)
  bool placedAllItems = false;
  Phase phase = Phase::Playing;
  std::string banter;          // the fortune teller's current line (events loc key)
  int banterSerial = 0;        // bumped on every new line (the UI restarts its fade)
  Signal<int> cellChoice;      // a cell (y * kSize + x) to divine with the current tool

  bool isFinished() const { return divinations == 0; }
  // The cells a click at (x, y) clears with `t`, in the C# order (GetAdjacentCells for Big).
  static std::vector<std::pair<int, int>> toolCells(Tool t, int x, int y);
};

// The running CrystalSphere event's minigame, or null.
CrystalSphereGame* crystalSphereGame(Run& r);

}  // namespace sts
