// Achievements (package M5): the 22 achievements of the C#'s Achievements.Achievement enum, their
// unlock checks, the unlocked set in progress.sav (v3) and a queue of fresh unlocks for the UI's
// toast (ui/screens/achievements_ui.cpp; the list page is a page of the 统计 screen, where the
// C#'s NStatsScreen has its achievements tab).
//
// Where each check runs, as in the C#:
//  - combat achievements (Models.Achievements/*, AchievementModel: a combat hook listener) are one
//    Model in Combat::listeners() (`combatListener`): Play20CardsSingleTurn, CharacterSkill*
//    (exhaust 20 in a fight, 999 unblocked damage in one hit, 99 Poison, 999 Doom, 50 Strength on
//    Osty, 5 Sly auto-plays from one card, a Sovereign Blade of 999 after a forge, 20 Stars);
//  - CombatManager after a won fight (`afterCombatWon`): CheckForDefeatedAllEnemiesAchievement
//    (every monster of the current act beaten at least once) and AfterBossDefeated;
//  - RunManager when the run ends (`afterRunEnded`): AchievementsHelper.AfterRunEnded.
//
// PORT NOTE: the decompiled AchievementsHelper and AchievementsUtil.Unlock have empty bodies (the
// shipped build strips them), so the conditions of the helper's achievements come from their loc
// descriptions: <Character>Win = win with that character, NoRelicWin = win holding only starter
// relics, AllCardsUpgraded = win with every upgradable deck card upgraded, FloorTenThousand = 10000
// floors over all runs (ProgressState.FloorsClimbed, kept in progress counters "floorsClimbed"),
// DefeatOneBoss = any boss beaten, Defeat<Act>Enemies = every monster of the act's encounters
// (ActModel.AllMonsters; only registered encounters) beaten at least once (progress.sav v3 keeps
// the beaten monsters from now on; older profiles start empty).
//
// GameModeExtension.AreAchievementsAndEpochsLocked: custom and daily runs never unlock
// achievements (their screens say so); counters (floors, beaten monsters) still update, as the
// C#'s ProgressSaveManager does for every game mode.
#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace sts {

struct Run;
struct Combat;
struct Card;
struct Model;

namespace achievements {

// MegaCrit.Sts2.Core.Achievements.Achievement (same values).
enum class Id {
  IroncladWin = 0,
  SilentWin = 1,
  RegentWin = 2,
  NecrobinderWin = 3,
  DefectWin = 4,
  DefeatUnderdocksEnemies = 14,
  DefeatOvergrowthEnemies = 15,
  DefeatHiveEnemies = 16,
  DefeatGloryEnemies = 17,
  DefeatOneBoss = 18,
  FloorTenThousand = 20,
  CharacterSkillSilent1 = 21,
  CharacterSkillSilent2 = 22,
  CharacterSkillIronclad1 = 23,
  CharacterSkillIronclad2 = 24,
  CharacterSkillNecrobinder1 = 25,
  CharacterSkillNecrobinder2 = 26,
  CharacterSkillRegent1 = 27,
  CharacterSkillRegent2 = 28,
  NoRelicWin = 29,
  AllCardsUpgraded = 30,
  Play20CardsSingleTurn = 31,
};

struct Info {
  Id id;
  const char* name;   // the C# enum name ("IroncladWin")
  const char* snake;  // save key / image name ("ironclad_win"); the loc key is its upper case
};

const std::vector<Info>& all();  // the 22, enum order (NAchievementsGrid)
const Info& info(Id id);
const Info* find(const std::string& nameOrSnake);  // by enum name or snake name
std::string locKey(Id id);  // "achievements.IRONCLAD_WIN" (+ ".title" / ".description")

bool isUnlocked(Id id);
int64_t unlockTime(Id id);  // unix seconds, 0 when locked
int unlockedCount();
int totalCount();

// GameModeExtension.AreAchievementsAndEpochsLocked (custom / daily run).
bool lockedFor(const Run& run);
// AchievementsUtil.Unlock: marks it in progress::state() with the current time unless the run
// (null = outside a run) locks achievements or it is already unlocked. Returns true (and queues a
// toast) only for a new unlock.
bool unlock(Id id, const Run* run);
void revoke(Id id);  // AchievementsUtil.Revoke (tests / debug)

// Fresh unlocks, oldest first, for the toast.
void queueToast(Id id);
bool popToast(Id& out);
void clearToasts();

// ActModel.AllMonsters for an act name ("Overgrowth", "Underdocks", "Hive", "Glory"): every monster
// of its registered encounters (generate() over a few seeds + the summons). Empty for other names.
const std::set<std::string>& actMonsters(const std::string& actName);

// ---- engine hooks -------------------------------------------------------------------------
// The AchievementModel listener for this combat (appended to Combat::listeners()).
Model* combatListener(Combat& c);
// CardCmd.AutoPlay(AutoPlayType.SlyDiscard) -> Hook.BeforeCardAutoPlayed (SkillSilent1).
void beforeSlyAutoPlay(Combat& c, Card* card);
// CombatManager after a won fight (UpdateAfterCombatWon's enemy stats, then the checks).
void afterCombatWon(Run& run, Combat& c);
// RunManager: AchievementsHelper.AfterRunEnded (+ UpdateWithRunData's FloorsClimbed).
void afterRunEnded(Run& run, bool victory);

}  // namespace achievements
}  // namespace sts
