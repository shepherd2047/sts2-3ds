// M12: the offline daily run (MegaCrit.Sts2.Core.Daily + NDailyRunScreen.SetupLobbyParams).
//
// The day's character, ascension, seed and modifiers all come from the date, as in the C#:
//   Rng rng  = new Rng(GetDeterministicHashCode(CanonicalizeSeed("dd_MM_yyyy")));
//   rng2/rng3/rng4 = new Rng(rng.NextUnsignedLong()) (in that order);
//   character  = rng2.NextItem(ModelDb.AllCharacters)      (one per lobby player; single player here)
//   ascension  = rng3.NextInt(0, 11)
//   modifiers  = ModifierModel.Pick2Good1Bad(rng4, excluded = the lobby's characters, i.e. the daily one)
//   seed text  = CanonicalizeSeed("dd_MM_yyyy_1p")          (the run's string seed, RunRngSet)
// A daily run is GameMode.Daily: run.sav / the run history keep its date (Run::dailyDate), and like
// a custom run it never raises the ascension level (ProgressSaveManager.UpdateWithRunData).
//
// PORT NOTE: no time server and no leaderboard. The date is the console's / PC's local date (the C#
// asks Mega Crit's time server and falls back to DateTimeOffset.UtcNow, i.e. the UTC date), and the
// only score kept is the profile's local best per date (Progress::dailyBest, progress.sav v2). The
// score is ScoreUtility.CalculateScore (the game over screen's score), not the leaderboard's encoded
// CalculateDailyScore (victory / floor / badges / time), which only exists to rank uploads.
// Debug: STS_DAILY_DATE=YYYY-MM-DD replaces today's date (tests, previews).
#pragma once
#include <string>
#include <vector>

namespace sts {
struct Progress;

namespace daily {

struct Date {
  int year = 2000, month = 1, day = 1;
  bool operator==(const Date&) const = default;
};

bool parseDate(const std::string& text, Date& out);  // "YYYY-MM-DD" (validated)
std::string key(const Date& d);                      // "YYYY-MM-DD" (progress / run.sav / history)
Date today();  // $STS_DAILY_DATE if valid, else the local date (std::time + std::localtime)

struct Params {
  Date date;
  std::string character;               // Character::id
  int ascension = 0;                   // 0..10
  std::string seedText;                // "DD_MM_YYYY_1P"
  std::vector<std::string> modifiers;  // modifiers.h keys: two good, then one bad
};
Params forDate(const Date& d);

// Local best score per date (Progress::dailyBest). -1 when that date was never finished.
int best(const Progress& p, const std::string& dateKey);
int bestOverall(const Progress& p);  // -1 when no daily run was ever finished
// Records a finished daily run's score in progress::state(); true if it is a new best for the date.
bool recordScore(const std::string& dateKey, int score);

}  // namespace daily
}  // namespace sts
