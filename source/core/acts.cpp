// The acts (MegaCrit.Sts2.Core.Models.Acts, ModelDb.Acts order: Overgrowth, Underdocks,
// Hive, Glory): encounter and event ids exactly as in each act's GenerateAllEncounters /
// AllEvents. Content files only register encounters and events; ids that aren't
// registered yet are skipped at runtime (Run::enterAct). Underdocks is the alternative
// act 1 (Index 0, IsDefault false); Run::start rolls which one a run gets.
#include <map>

#include "game.h"

namespace sts::db {

const std::vector<ActDef>& acts() {
  static const std::vector<ActDef> v = {
      {"Overgrowth", "overgrowth", 0, true, 3,
       {"FuzzyWurmCrawlerWeak", "NibbitsWeak", "ShrinkerBeetleWeak", "SlimesWeak"},
       {"CubexConstructNormal", "FlyconidNormal", "FogmogNormal", "InkletsNormal", "MawlerNormal", "NibbitsNormal",
        "OvergrowthCrawlers", "RubyRaidersNormal", "SlimesNormal", "SlitheringStranglerNormal",
        "SnappingJaxfruitNormal", "VineShamblerNormal"},
       {"BygoneEffigyElite", "ByrdonisElite", "PhrogParasiteElite"},
       {"CeremonialBeastBoss", "TheKinBoss", "VantomBoss"},
       {"AromaOfChaos", "ByrdonisNest", "DenseVegetation", "JungleMazeAdventure", "LuminousChoir", "MorphicGrove",
        "SapphireSeed", "SunkenStatue", "TabletOfTruth", "UnrestSite", "Wellspring", "WhisperingHollow",
        "WoodCarvings"},
       {"event:/music/act1_a1_v1", "event:/music/act1_a2_v2"}, "event:/sfx/ambience/act1_ambience", 15},
      {"Underdocks", "underdocks", 0, false, 3,
       {"CorpseSlugsWeak", "SeapunkWeak", "SludgeSpinnerWeak", "ToadpolesWeak"},
       {"CorpseSlugsNormal", "CultistsNormal", "FossilStalkerNormal", "GremlinMercNormal", "HauntedShipNormal",
        "LivingFogNormal", "PunchConstructNormal", "SeapunkNormal", "SewerClamNormal", "TwoTailedRatsNormal"},
       {"PhantasmalGardenersElite", "SkulkingColonyElite", "TerrorEelElite"},
       {"LagavulinMatriarchBoss", "SoulFyshBoss", "WaterfallGiantBoss"},
       {"AbyssalBaths", "DrowningBeacon", "EndlessConveyor", "PunchOff", "SpiralingWhirlpool", "SunkenStatue",
        "SunkenTreasury", "DoorsOfLightAndDark", "TrashHeap", "WaterloggedScriptorium"},
       {"event:/music/act1_b1_v1"}, "event:/sfx/ambience/act3_ambience", 15},
      {"Hive", "hive", 1, true, 2,
       {"BowlbugsWeak", "ExoskeletonsWeak", "ThievingHopperWeak", "TunnelerWeak"},
       {"BowlbugsNormal", "ChompersNormal", "ExoskeletonsNormal", "HunterKillerNormal", "LouseProgenitorNormal",
        "MytesNormal", "OvicopterNormal", "SlumberingBeetleNormal", "SpinyToadNormal", "TheObscuraNormal"},
       {"DecimillipedeElite", "EntomancerElite", "InfestedPrismsElite"},
       {"KaiserCrabBoss", "KnowledgeDemonBoss", "TheInsatiableBoss"},
       {"Amalgamator", "Bugslayer", "ColorfulPhilosophers", "ColossalFlower", "FieldOfManSizedHoles",
        "InfestedAutomaton", "LostWisp", "SpiritGrafter", "TheLanternKey", "ZenWeaver"},
       {"event:/music/act2_a1_v2", "event:/music/act2_a2_v2"}, "event:/sfx/ambience/act2_ambience", 14},
      {"Glory", "glory", 2, true, 2,
       {"DevotedSculptorWeak", "ScrollsOfBitingWeak", "TurretOperatorWeak"},
       {"AxebotsNormal", "ConstructMenagerieNormal", "FabricatorNormal", "FrogKnightNormal", "GlobeHeadNormal",
        "OwlMagistrateNormal", "ScrollsOfBitingNormal", "SlimedBerserkerNormal", "TheLostAndForgottenNormal"},
       {"KnightsElite", "MechaKnightElite", "SoulNexusElite"},
       {"AeonglassBoss", "QueenBoss", "TestSubjectBoss"},
       {"BattlewornDummy", "GraveOfTheForgotten", "HungryForMushrooms", "Reflections", "RoundTeaParty", "Trial",
        "TinkerTime"},
       {"event:/music/act3_a1_v1", "event:/music/act3_a2_v1"}, "event:/sfx/ambience/act3_ambience", 13},
  };
  return v;
}

// EncounterModel.Tags overrides (Models.Encounters); every other encounter has none.
const std::vector<std::string>& encounterTags(const std::string& id) {
  static const std::map<std::string, std::vector<std::string>> tags = {
      {"BowlbugsNormal", {"Workers"}},
      {"BowlbugsWeak", {"Workers"}},
      {"ChompersNormal", {"Chomper"}},
      {"CorpseSlugsNormal", {"Slugs"}},
      {"CorpseSlugsWeak", {"Slugs"}},
      {"ExoskeletonsNormal", {"Exoskeletons"}},
      {"ExoskeletonsWeak", {"Exoskeletons"}},
      {"FlyconidNormal", {"Mushroom", "Slimes"}},
      {"FuzzyWurmCrawlerWeak", {"Crawler"}},
      {"KnightsElite", {"Knights"}},
      {"NibbitsWeak", {"Nibbit"}},
      {"OvergrowthCrawlers", {"Shrinker", "Crawler"}},
      {"ScrollsOfBitingNormal", {"Scrolls"}},
      {"ScrollsOfBitingWeak", {"Scrolls"}},
      {"SeapunkNormal", {"Seapunk"}},
      {"SeapunkWeak", {"Seapunk"}},
      {"ShrinkerBeetleWeak", {"Shrinker"}},
      {"SlimesNormal", {"Slimes"}},
      {"SlimesWeak", {"Slimes"}},
      {"SlitheringStranglerNormal", {"Jaxfruit", "Slimes"}},
      {"SlumberingBeetleNormal", {"Workers"}},
      {"SnappingJaxfruitNormal", {"Mushroom", "Jaxfruit"}},
      {"ThievingHopperWeak", {"Thieves"}},
      {"TunnelerNormal", {"Burrower", "Chomper"}},
      {"TunnelerWeak", {"Burrower"}},
  };
  static const std::vector<std::string> none;
  auto it = tags.find(id);
  return it == tags.end() ? none : it->second;
}

const ActDef* act(const std::string& name) {
  for (auto& a : acts()) if (name == a.name) return &a;
  return nullptr;
}

// ActModel.GetRandomList. PORT NOTE (n/a: owner): every act is unlocked and discovered, so the C#'s forced first try of an undiscovered act is dropped.
// (IsUnlocked -> true; with no Timeline / DiscoveredActs, act 1 is always Overgrowth or Underdocks at random.)
std::vector<std::string> randomActList(Rng& rng) {
  std::vector<std::string> list;
  for (int i = 0;; ++i) {
    std::vector<std::string> candidates;  // ModelDb.ActsByIndex[i]
    for (auto& a : acts()) if (a.index == i) candidates.push_back(a.name);
    if (candidates.empty()) break;
    list.push_back(rng.nextItem(candidates));
  }
  return list;
}

std::vector<std::string> act1Weak() { return acts()[0].weak; }
std::vector<std::string> act1Normal() { return acts()[0].normal; }
std::vector<std::string> act1Elites() { return acts()[0].elites; }
std::vector<std::string> act1Bosses() { return acts()[0].bosses; }

}  // namespace sts::db
