// The three acts (MegaCrit.Sts2.Core.Models.Acts: Overgrowth, Hive, Glory): encounter
// and event ids exactly as in each act's GenerateAllEncounters / AllEvents. Content
// files only register encounters and events; ids that aren't registered yet are
// skipped at runtime (Run::enterAct).
#include "game.h"

namespace sts::db {

const std::vector<ActDef>& acts() {
  static const std::vector<ActDef> v = {
      {"Overgrowth", "overgrowth", 3,
       {"FuzzyWurmCrawlerWeak", "NibbitsWeak", "ShrinkerBeetleWeak", "SlimesWeak"},
       {"CubexConstructNormal", "FlyconidNormal", "FogmogNormal", "InkletsNormal", "MawlerNormal", "NibbitsNormal",
        "OvergrowthCrawlers", "RubyRaidersNormal", "SlimesNormal", "SlitheringStranglerNormal",
        "SnappingJaxfruitNormal", "VineShamblerNormal"},
       {"BygoneEffigyElite", "ByrdonisElite", "PhrogParasiteElite"},
       {"CeremonialBeastBoss", "TheKinBoss", "VantomBoss"},
       {"AromaOfChaos", "ByrdonisNest", "DenseVegetation", "JungleMazeAdventure", "LuminousChoir", "MorphicGrove",
        "SapphireSeed", "SunkenStatue", "TabletOfTruth", "UnrestSite", "Wellspring", "WhisperingHollow",
        "WoodCarvings"}},
      {"Hive", "hive", 2,
       {"BowlbugsWeak", "ExoskeletonsWeak", "ThievingHopperWeak", "TunnelerWeak"},
       {"BowlbugsNormal", "ChompersNormal", "ExoskeletonsNormal", "HunterKillerNormal", "LouseProgenitorNormal",
        "MytesNormal", "OvicopterNormal", "SlumberingBeetleNormal", "SpinyToadNormal", "TheObscuraNormal"},
       {"DecimillipedeElite", "EntomancerElite", "InfestedPrismsElite"},
       {"KaiserCrabBoss", "KnowledgeDemonBoss", "TheInsatiableBoss"},
       {"Amalgamator", "Bugslayer", "ColorfulPhilosophers", "ColossalFlower", "FieldOfManSizedHoles",
        "InfestedAutomaton", "LostWisp", "SpiritGrafter", "TheLanternKey", "ZenWeaver"}},
      {"Glory", "glory", 2,
       {"DevotedSculptorWeak", "ScrollsOfBitingWeak", "TurretOperatorWeak"},
       {"AxebotsNormal", "ConstructMenagerieNormal", "FabricatorNormal", "FrogKnightNormal", "GlobeHeadNormal",
        "OwlMagistrateNormal", "ScrollsOfBitingNormal", "SlimedBerserkerNormal", "TheLostAndForgottenNormal"},
       {"KnightsElite", "MechaKnightElite", "SoulNexusElite"},
       {"AeonglassBoss", "QueenBoss", "TestSubjectBoss"},
       {"BattlewornDummy", "GraveOfTheForgotten", "HungryForMushrooms", "Reflections", "RoundTeaParty", "Trial",
        "TinkerTime"}},
  };
  return v;
}

std::vector<std::string> act1Weak() { return acts()[0].weak; }
std::vector<std::string> act1Normal() { return acts()[0].normal; }
std::vector<std::string> act1Elites() { return acts()[0].elites; }
std::vector<std::string> act1Bosses() { return acts()[0].bosses; }

}  // namespace sts::db
