// Event registry. Events themselves live in events_act1.cpp (MegaCrit.Sts2.Core.Models.Events).
#include <map>

#include "game.h"

namespace sts {

void registerAct1Events();  // events_act1.cpp
void registerSharedEvents();  // events_shared.cpp
void registerSharedEvents2();  // events_shared2.cpp
void registerSharedEvents4();  // events_shared4.cpp
void registerAct2Events();  // events_act2.cpp
void registerAct3Events();  // events_act3.cpp
void registerShared3Events();  // events_shared3.cpp (A7b)
void registerUnderdocksEvents();  // events_underdocks.cpp (A11e)
void registerArchitect();  // content_architect.cpp (A10)

namespace {
std::map<std::string, EventFactory>& eventReg() { static std::map<std::string, EventFactory> m; return m; }
}  // namespace

void registerEvents() {
  registerAct1Events();
  registerSharedEvents();
  registerSharedEvents2();
  registerSharedEvents4();
  registerAct2Events();
  registerAct3Events();
  registerShared3Events();
  registerUnderdocksEvents();
  registerArchitect();
}

namespace db {

void registerEvent(const std::string& id, EventFactory f) { eventReg()[id] = f; }

std::unique_ptr<Event> event(const std::string& id) {
  auto it = eventReg().find(id);
  return it == eventReg().end() ? nullptr : it->second();
}

// Overgrowth.AllEvents
const std::vector<std::string>& act1Events() {
  static const std::vector<std::string> ids = {
      "AromaOfChaos", "ByrdonisNest", "DenseVegetation", "JungleMazeAdventure", "LuminousChoir",
      "MorphicGrove", "SapphireSeed", "SunkenStatue", "TabletOfTruth", "UnrestSite",
      "Wellspring", "WhisperingHollow", "WoodCarvings"};
  return ids;
}

// ModelDb.AllSharedEvents (added to every act's pool by ActModel.GenerateRooms)
const std::vector<std::string>& sharedEvents() {
  static const std::vector<std::string> ids = {
      "BrainLeech", "CrystalSphere", "DollRoom", "FakeMerchant", "PotionCourier", "RanwidTheElder",
      "RelicTrader", "RoomFullOfCheese", "SelfHelpBook", "SlipperyBridge", "StoneOfAllTime",
      "Symbiote", "TeaMaster", "TheFutureOfPotions", "TheLegendsWereTrue", "ThisOrThat",
      "WarHistorianRepy", "WelcomeToWongos"};
  return ids;
}

}  // namespace db
}  // namespace sts
