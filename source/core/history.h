// Run history (package M2): the last 50 finished runs of each profile, for the future stats /
// run history screens (M6, S25) and the end-of-run score and badges (M7, S23).
//
// Ported from the C#'s Runs/RunHistory.cs (the record), Runs/RunHistoryUtilities.cs
// (CreateRunHistoryEntry: killed-by from the last room, RunTime = WinTime if won),
// Runs.History/MapPointHistoryEntry.cs + MapPointRoomHistoryEntry.cs (the path, recorded as the run
// goes by RunState.AppendToMapPointHistory / the extra rooms of RunManager.EnterRoom), and
// Runs/ScoreUtility.cs (CalculateScore, ported whole: it only reads the path).
//
// Files: <root>profile<N>/history/<slot>.run, slot 00..49 (a ring: run number `seq` goes to slot
// seq % 50, overwriting the oldest), one small Archive token-stream file per run, written with
// the same tmp-then-rename as progress.sav.
// PORT NOTE: the C# (Saves.Managers/RunHistorySaveManager) writes profile<N>/saves/history/
// <StartTime>.run JSON files and never prunes them; the port keeps a fixed ring of 50 so the SD
// card sees one small write per run, and needs no directory listing (3DS SD listing is slow).
// Dropped vs. the C# RunHistory: platform_type, the daily game mode (M12), build_id,
// multiplayer players (single player: the player fields are the record's own),
// and most PlayerMapPointHistoryEntry stats (only GoldGained, which the score needs).
// Badges (RunHistoryPlayer.Badges, ScoreUtility.GetBadges) are computed by badges.h at the end of
// the run (fromRun) and stored in the record (M7).
//
// Nothing here touches disk unless profiles::init() ran (profiles::diskEnabled()): automated
// previews (STS_HIDDEN / STS_NO_SAVE), the headless sim and the tests never write a real profile.
// History never reads or advances an Rng.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sts {

struct Run;
struct Archive;

namespace history {

constexpr int kMax = 50;  // runs kept per profile

// MegaCrit.Sts2.Core.Map.MapPointType (same values).
enum class PointType { Unassigned, Unknown, Shop, Treasure, RestSite, Monster, Elite, Boss, Ancient };
// MegaCrit.Sts2.Core.Rooms.RoomType (same values; Map is never recorded).
enum class RoomKind { Unassigned, Monster, Elite, Boss, Treasure, Shop, Event, RestSite };

// MapPointRoomHistoryEntry: a room entered at a map point; `model` is the encounter id for fights,
// the event id for events and Ancients, "" otherwise.
struct Room {
  RoomKind type = RoomKind::Unassigned;
  std::string model;
  bool operator==(const Room&) const = default;
};

// MapPointHistoryEntry: one map point (floor). Usually one room; an event that starts a fight
// adds the fight, the last boss adds TheArchitect.
struct MapPoint {
  PointType type = PointType::Unassigned;
  std::vector<Room> rooms;
  int goldGained = 0;  // PlayerMapPointHistoryEntry.GoldGained (PlayerCmd.GainGold)
  // Badge inputs (M7, badges.h), PlayerMapPointHistoryEntry.GoldSpent / DamageTaken / RestSiteChoices.
  // ioPath does not carry them; run.sav version 7 and history record version 2 add them after the
  // path. A path read from an older save has tracked == false: the badges that need them skip it.
  int goldSpent = 0;                       // gold paid at the merchant (LoseGold Spent)
  int damageTaken = 0;                     // unblocked damage the player took (CreatureCmd.Damage)
  std::vector<std::string> restChoices;    // rest site option ids: HEAL, SMITH, LIFT, DIG, COOK, KINDLE, CLONE
  bool tracked = true;
  // The badge inputs and `tracked` are not part of equality (ioPath alone drops them).
  bool operator==(const MapPoint& o) const { return type == o.type && rooms == o.rooms && goldGained == o.goldGained; }
};
using Path = std::vector<std::vector<MapPoint>>;  // per act (RunState.MapPointHistory)

struct DeckCard {
  std::string id;
  int upgrades = 0;
  std::string enchantment;  // "" = none
  int enchantAmount = 0;
  bool operator==(const DeckCard&) const = default;
};

// SerializableBadge: an obtained badge (badges.h). rarity = badges::BadgeRarity (1 bronze .. 3 gold).
struct BadgeEntry {
  std::string id;  // Badge.Id, e.g. "TINY_DECK"
  int rarity = 0;
  bool operator==(const BadgeEntry&) const = default;
};

// RunHistory + its (single) RunHistoryPlayer.
struct RunRecord {
  // 2 (M7): per-point badge inputs (gold spent, damage taken, rest choices), the CCCCOMBO flag and
  // the badges. Version 1 records still load (no badge data: tracked == false on every point).
  // 3 (M11): game mode (custom), the seed text and the modifiers; older records: standard, none.
  static constexpr int kVersion = 3;

  uint64_t seq = 0;          // 1, 2, 3... per profile (set by append); higher = newer
  uint64_t seed = 0;         // Run::seed (the numeric seed; the typed seed text is not kept)
  std::string character;     // Character::id
  int ascension = 0;
  std::vector<std::string> acts;  // RunHistory.Acts (Run::actIds)
  int64_t startTime = 0;     // unix seconds when the run was started (RunHistory.StartTime)
  int runTime = 0;           // seconds played (RunHistory.RunTime)
  bool win = false;
  bool abandoned = false;
  std::string killedByEncounter;  // "" if none (won, or not killed in a fight)
  std::string killedByEvent;      // "" if none
  int floorReached = 0;           // Run::floor
  Path path;
  std::vector<DeckCard> deck;
  std::vector<std::string> relics;
  std::vector<std::string> potions;  // filled slots only
  int maxPotionSlots = 3;
  int gold = 0, hp = 0, maxHp = 0;  // at the end (port addition, for the history screen)
  int score = 0;                    // ScoreUtility.CalculateScore(path, ascension, win)
                                    // (the C# adds no badge bonus to the score)
  bool cccCombo = false;            // ExtraFields.CccomboBadgeUnlocked: 20 cards in one turn
  std::vector<BadgeEntry> badges;   // RunHistoryPlayer.Badges (empty when abandoned); badges.h
  bool custom = false;                 // RunHistory.GameMode == Custom (M11)
  std::string seedText;                // RunHistory.Seed (the typed / rolled text; "" = numeric only)
  std::vector<std::string> modifiers;  // RunHistory.Modifiers (keys, modifiers.h)

  std::string save() const;
  bool load(const std::string& data);  // false (and *this unchanged) if empty/garbled/future
  bool operator==(const RunRecord&) const = default;
};

// The path's token stream (shared by the record file and run.sav, save.cpp).
void ioPath(Archive& a, Path& path);

// ---- ScoreUtility ------------------------------------------------------------------------
int floorScore(const Path& path);            // GetScoreForFloor: points * 10 * (act + 1)
int goldScore(const Path& path);             // GetScoreForGoldGained: total gold gained / 100
int elitesKilled(const Path& path);          // GetElitesKilledCount
int bossesSlain(const Path& path, bool won); // GetBossesSlainCount
int score(const Path& path, int ascension, bool won);  // CalculateScore (single player)

// ---- recording ---------------------------------------------------------------------------
// The record for `run` as it ends now (RunHistoryUtilities.CreateRunHistoryEntry). seq = 0.
RunRecord fromRun(const Run& run, bool win, bool abandoned);
// Called once per run from Run's end-of-run hook (run.cpp recordRunEnd: death, TheArchitect's
// winRun, abandon): builds the record, keeps it as last(), and appends it to the current
// profile's history if saves are on (profiles::diskEnabled()).
void onRunEnded(const Run& run, bool win, bool abandoned);
// The record of the run that ended last in this session (the game over / victory screen), or
// null. Set even when saves are off.
const RunRecord* last();
void clearLast();  // test helper

// ---- store (per profile) -------------------------------------------------------------------
std::string dir(int profileId);                 // <root>profile<N>/history/
std::string slotPath(int profileId, int slot);  // <root>profile<N>/history/<slot, 2 digits>.run
// Gives `rec` the next seq and writes it into its ring slot (replacing the oldest once 50 are
// stored). Returns false when saves are off or the write failed. `rec.seq` is updated.
bool append(int profileId, RunRecord& rec);
// Every stored run of the profile, newest first (at most kMax). Garbled files are skipped.
std::vector<RunRecord> load(int profileId);
int count(int profileId);  // = load(profileId).size() without keeping the records
// Deletes every history file of the profile (profiles::remove calls this).
void removeAll(int profileId);

}  // namespace history
}  // namespace sts
