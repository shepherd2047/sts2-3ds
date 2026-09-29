// End-of-run badges (package M7), for the death / victory screen (S23) and the run history.
//
// Ported from Models.Badges/*.cs (Badge, BadgePool, the 19 single-player badges), Runs/
// ScoreUtility.GetBadges and Runs/RunHistoryUtilities.GetBadgesForPlayer (no badges when the run
// was abandoned). The badges are computed once, when the run ends (history::fromRun), and stored in
// the run history record (RunRecord::badges); history::last() is what S23 reads.
//
// The score gets no badge bonus: ScoreUtility.CalculateScore only sums floors, gold, elites and
// bosses (badge rarity is only a leaderboard tiebreaker in the C#), so history::score is unchanged.
//
// Not ported (multiplayer only, skipped by GetBadges in a single-player run): DAMAGE_LEADER,
// DEBUFFER, HEALER, TEAM_PLAYER. WHOMPER exists in the C# but is not in BadgePool ("currently not
// in use", IsObtained is false). That leaves the 19 badges below, all with their C# conditions.
//
// Inputs the run did not track before are counted as it goes, in the existing history style (see
// MapPoint in history.h): goldSpent at the merchant, damageTaken, rest site choices, and the
// CCCCOMBO flag (Run::cccCombo). PORT NOTE: run.sav is unchanged, so after a Continue the points
// entered before the save have tracked == false and CCCCOMBO restarts at false; badges that need
// that data (PERFECT, KACHING, RESTFUL, RESTLESS, CCCCOMBO) then only count what was tracked.
//
// Never touches an Rng.
#pragma once
#include <string>
#include <vector>

#include "history.h"

namespace sts {

struct Run;

namespace badges {

// MegaCrit.Sts2.Core.Models.Badges.BadgeRarity (same values).
enum class BadgeRarity { None, Bronze, Silver, Gold };

struct Info {
  const char* id;    // Badge.Id
  bool requiresWin;  // Badge.RequiresWin
  const char* what;  // one line: the condition, tiers separated by '/'
};

// The single-player badges in BadgePool order (19).
const std::vector<Info>& all();
const Info* find(const std::string& id);

// One badge's rarity for the run of `rec` (None = not obtained), ignoring RequiresWin.
BadgeRarity rarityOf(const std::string& id, const history::RunRecord& rec);
// ScoreUtility.GetBadges: the badges obtained (win required where the badge needs it; none if
// the run was abandoned, RunHistoryUtilities.GetBadgesForPlayer). Reads rec.win / abandoned.
std::vector<history::BadgeEntry> compute(const history::RunRecord& rec);

// NBadge's loc keys (table "badges", baked by tools/build_assets.py). Tiered badges may have a
// per-rarity string (<ID>.<bronze|silver|gold>Title / ...Description): the screen uses the rarity
// keys when the loc has them, else the plain ones.
struct LocKeys {
  std::string rarityTitle, rarityDescription;  // badges.<ID>.<rarity>Title / Description
  std::string title, description;              // badges.<ID>.title / description
};
LocKeys locKeys(const history::BadgeEntry& b);
const char* rarityName(int rarity);  // "bronze" / "silver" / "gold" ("ERROR" for none)

// The score screen's API: the record of the run that just ended (history::last()) and its badges.
// Null / empty when no run ended this session. history::load() gives the same for stored runs.
const history::RunRecord* lastRecord();
inline const std::vector<history::BadgeEntry>& lastBadges() {
  static const std::vector<history::BadgeEntry> none;
  const history::RunRecord* r = lastRecord();
  return r ? r->badges : none;
}

// ---- run-time counters (called from the engine; no-ops without a current map point) -------------
void noteDamageTaken(Run& run, int unblocked);            // CreatureCmd.Damage: player hp lost
void noteGoldSpent(Run& run, int gold);                   // PlayerCmd.LoseGold(Spent) at the merchant
void noteRestChoice(Run& run, int restOption);            // RestSiteSynchronizer: option id into the point
void noteCardsPlayedThisTurn(Run& run, int cardsPlayed);  // CccComboModel.AfterCardPlayed

}  // namespace badges
}  // namespace sts
