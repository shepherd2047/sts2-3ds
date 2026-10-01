// Profile progress (package M1): per-character wins/losses/streaks/max ascension, the
// compendium "seen" sets (cards, relics, potions, monsters) and a free-form counter bucket
// for stats/achievements (M5). One file, `progress.sav`, shared by every run of a profile
// (unlike run.sav, which holds a single in-progress run and is deleted when it ends).
//
// Ported from the C#'s MegaCrit.Sts2.Core.Saves.ProgressState / SerializableProgress
// (Saves/ProgressState.cs, Saves/SerializableProgress.cs, Saves/CharacterStats.cs) and the
// parts of Saves.Managers/ProgressSaveManager.cs that update them after a run or a pickup.
// Trimmed to what a single-player, all-unlocked build needs:
//
// PORT NOTE: dropped vs. the C#'s SerializableProgress:
//  - UniqueId, EnableFtues/FtueCompleted (no FTUE package yet, tracked separately if it lands),
//  - Epochs / TotalUnlocks / PendingCharacterUnlock / WongoPoints / CurrentScore
//    (the score-bar meta-unlock system; n/a per the owner's decision that everything is unlocked
//    from the start),
//  - PreferredMultiplayerAscension / MaxMultiplayerAscension / TestSubjectKills (multiplayer; n/a),
//  - per-character PreferredAscension / Badges, and DiscoveredActs / EncounterStats / CardStats
//    (fine-grained history nobody reads yet; `counters` covers simple tallies in the meantime).
//  - EnemyStats keeps wins/losses per enemy, not per enemy and character (FightStats.Character).
//  Kept since version 4 (M-stats): TotalPlaytime, ArchitectDamage, per-character Playtime and
//  FastestWinTime, DiscoveredEvents, EnemyStats (monster kill counts). The overall win streak is
//  derived like ProgressState.BestWinStreak (the best character streak), not stored.
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>

namespace sts {

struct Combat;

// Per-character record (CharacterStats in the C#).
struct CharacterProgress {
  int wins = 0;
  int losses = 0;
  int currentStreak = 0;
  int bestStreak = 0;
  int maxAscension = 0;  // highest ascension level ever won at (0-10)
  // M-stats: CharacterStats.Playtime (seconds, every run) and FastestWinTime (-1 = none).
  int64_t playtime = 0;
  int64_t fastestWin = -1;
};

// EnemyStats (TotalWins / TotalLosses): fights won / lost against one monster id.
struct EnemyProgress {
  int wins = 0;
  int losses = 0;
};

struct Progress {
  // 2 (M12): the local daily run best scores (older files: none).
  // 3 (M5): unlocked achievements + unlock times, monsters defeated (older files: none).
  // 4 (M-stats): playtime, architect damage, enemy kill counts, discovered events, per-character
  //   playtime / fastest win (older files: zeros / empty / -1).
  static constexpr int kVersion = 4;

  std::map<std::string, CharacterProgress> characters;  // key: Character::id ("Ironclad", "Silent", ...)
  std::set<std::string> seenCards, seenRelics, seenPotions, seenMonsters;
  // Free-form named counters for M5's achievements and any stats screen. Never removed on load
  // (an unknown counter from a newer build round-trips untouched); Progress::load clamps
  // negative values to 0.
  std::map<std::string, int64_t> counters;

  // M12 (daily.h): the best ScoreUtility.CalculateScore per daily date ("YYYY-MM-DD"); the C#'s
  // leaderboards replaced by a local record.
  std::map<std::string, int> dailyBest;

  // M5 (achievements.h): ProgressState.UnlockedAchievements, keyed by the C#'s snake_case name
  // ("ironclad_win") -> unlock time (unix seconds). Unknown names round-trip untouched (the C#
  // keeps them in _unknownUnlockedAchievements).
  std::map<std::string, int64_t> achievements;
  // M5: every monster id ever beaten in a won fight (the C#'s EnemyStats with a win), for the
  // DefeatAll<Act>Enemies achievements.
  std::set<std::string> defeatedMonsters;

  // M-stats (ProgressState): TotalPlaytime (seconds), ArchitectDamage (the summed scores of won
  // runs, shown in the victory text), EnemyStats keyed by monster id, DiscoveredEvents.
  int64_t totalPlaytime = 0;
  int64_t architectDamage = 0;
  std::map<std::string, EnemyProgress> enemyStats;
  std::set<std::string> discoveredEvents;

  int64_t totalKills() const;      // SaveManager.GetTotalKills: sum of EnemyStats.TotalWins
  int64_t fastestVictory() const;  // ProgressState.FastestVictory (-1 when no character has a win)
  int bestWinStreak() const;       // ProgressState.BestWinStreak

  CharacterProgress& character(const std::string& id);  // get-or-create

  // Same Archive token-stream approach as save.cpp: writing and reading share one function
  // (ioProgress in progress.cpp) so the two can never drift apart.
  std::string save() const;
  // Leaves *this unchanged and returns false if `data` is empty, garbled or a future version
  // this build does not understand (kVersion mismatch in either direction).
  bool load(const std::string& data);
};

namespace progress {

// The process-wide instance. Engine hooks below mutate it in place; nothing here touches disk on
// its own (matching save.cpp/Run::save() -- core produces and consumes strings only). The
// profile layer (profiles.h, Y4) loads the current slot's file into it and saves it back.
Progress& state();
void reset();  // test helper: back to a fresh, empty Progress

void markCardSeen(const std::string& id);
void markRelicSeen(const std::string& id);
void markPotionSeen(const std::string& id);
void markMonsterSeen(const std::string& id);
void incrementCounter(const std::string& name, int64_t amount = 1);

// AncientStats (X6): per Ancient and character, the runs won / lost after meeting it
// (ProgressSaveManager.UpdateWithRunData: every Ancient map point of the finished run). Kept in
// `counters` as "ancient.<AncientId>.<CharacterId>.wins" / ".losses" (no save version change).
// Visits = wins + losses (AncientCharacterStats.Visits); they pick the Ancients' dialogue.
void recordAncientRun(const std::string& ancientId, const std::string& characterId, bool win);
int ancientVisits(const std::string& ancientId, const std::string& characterId);  // GetVisitsAs
int ancientTotalVisits(const std::string& ancientId);                              // TotalVisits

// ProgressSaveManager.UpdateWithRunData's totals: TotalPlaytime / Playtime += the run's time,
// ArchitectDamage += the score of a won run, FastestWinTime on a standard win (not custom/daily).
void recordRunTotals(const std::string& characterId, int64_t runSeconds, bool win, int score, bool standard);
// ProgressState.MarkEventAsSeen.
void markEventSeen(const std::string& id);
// UpdateAfterCombatWon / IncrementEnemyFightLoss: every enemy of the fight gets a win or a loss.
void recordCombatEnd(const Combat& c, bool won);

enum class RunOutcome { Win, Loss, Abandon };
// Called once when a run concludes: a won fight against the final boss, a lost fight/event/HP
// loss, or the player abandoning from the pause menu. Mirrors
// ProgressSaveManager.UpdateWithRunData + IncrementSingleplayerAscension:
//  - Win: wins++, currentStreak++, bestStreak = max(bestStreak, currentStreak); if this run's
//    ascension equalled the character's maxAscension (i.e. it was played at the frontier) and
//    maxAscension < 10, maxAscension++ (winning at ascension N unlocks N+1).
//  - Loss or Abandon: losses++, currentStreak = 0. The C# has no separate "abandon" case in this
//    function (UpdateWithRunData takes a plain `victory` bool, so Abandon and Death share the
//    `else` branch and both count as a loss); Abandon additionally bumps counters["runsAbandoned"]
//    so a later stats/achievement package can tell the two apart.
void onRunEnded(const std::string& characterId, int ascension, RunOutcome outcome);

// The path used when save()/load() below are called with no argument: $STS_PROGRESS_PATH if set
// (tests and the headless sim must set this, or call the string-based Progress::save/load
// directly, so they never touch a real profile's file), else the current profile's file,
// profiles::progressPath() (saves/profile<N>/progress.sav, 3DS sdmc:/3ds/sts2-3ds/profile<N>/...).
std::string defaultPath();

// Atomic file I/O for `state()` through core/safe_file.h (Y5: "<path>.tmp", then rename or a
// .bak swap), like gfx::writeSave, so a crash or power loss mid-write leaves the previous file
// intact instead of a truncated one. gfx-free, so the sim and tests link it.
bool save(const std::string& path = defaultPath());
bool load(const std::string& path = defaultPath());

}  // namespace progress

}  // namespace sts
