// Profile progress (package M1). See progress.h for the data model and what was dropped vs.
// the C#. The token-stream format follows save.cpp's Archive pattern exactly (one io function
// shared by save() and load()); the file I/O below mirrors gfx::writeSave's tmp-then-rename
// atomic write (gfx_sdl.cpp / gfx_3ds.cpp) without depending on gfx.h, so this stays a plain
// core module the headless sim and tests can link without a platform backend.
//
// PORT NOTE: nothing here calls save()/load() automatically. The C#'s ProgressSaveManager reads
// progress.save from a per-profile directory at startup and rewrites it after every change
// (SaveProgress()); on this port profiles.cpp (Y4) loads the current slot's file in
// profiles::init/select, and the app writes it through profiles::saveProgress() at every run
// save point and when a run ends (ui.cpp). The engine hooks below only update progress::state().
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

#include "game.h"
#include "profiles.h"
#include "progress.h"
#include "safe_file.h"

namespace sts {

CharacterProgress& Progress::character(const std::string& id) { return characters[id]; }

int64_t Progress::totalKills() const {
  int64_t n = 0;
  for (auto& [id, e] : enemyStats) n += e.wins;
  return n;
}

int64_t Progress::fastestVictory() const {
  int64_t best = -1;
  for (auto& [id, cp] : characters)
    if (cp.fastestWin >= 0 && (best < 0 || cp.fastestWin < best)) best = cp.fastestWin;
  return best;
}

int Progress::bestWinStreak() const {
  int best = 0;
  for (auto& [id, cp] : characters) best = std::max(best, cp.bestStreak);
  return best;
}

namespace {

// Same token-stream Archive as save.cpp (game.h). Reading and writing share this function.
void ioProgress(Archive& a, Progress& p) {
  a.tag("STS2PROGRESS");
  int version = Progress::kVersion;
  a.io(version);
  if (version < 1 || version > Progress::kVersion) { a.ok = false; return; }

  a.tag("CHARACTERS");
  std::vector<std::string> ids;
  if (!a.reading) for (auto& [id, cp] : p.characters) ids.push_back(id);
  a.io(ids);
  if (a.reading) p.characters.clear();
  for (auto& id : ids) {
    CharacterProgress& cp = p.characters[id];
    a.io(cp.wins);
    a.io(cp.losses);
    a.io(cp.currentStreak);
    a.io(cp.bestStreak);
    a.io(cp.maxAscension);
  }

  auto ioSet = [&](const char* tag, std::set<std::string>& s) {
    a.tag(tag);
    std::vector<std::string> v;
    if (!a.reading) v.assign(s.begin(), s.end());
    a.io(v);
    if (a.reading) s = std::set<std::string>(v.begin(), v.end());
  };
  ioSet("CARDS", p.seenCards);
  ioSet("RELICS", p.seenRelics);
  ioSet("POTIONS", p.seenPotions);
  ioSet("MONSTERS", p.seenMonsters);

  a.tag("COUNTERS");
  std::vector<std::string> names;
  std::vector<int64_t> values;
  if (!a.reading) for (auto& [name, v] : p.counters) { names.push_back(name); values.push_back(v); }
  a.io(names);
  a.io(values);
  if (a.reading) {
    p.counters.clear();
    for (size_t i = 0; i < names.size() && i < values.size(); ++i) p.counters[names[i]] = values[i];
  }
  if (version >= 2) {  // M12: daily run bests
    a.tag("DAILY");
    std::vector<std::string> dates;
    std::vector<int> scores;
    if (!a.reading) for (auto& [d, v] : p.dailyBest) { dates.push_back(d); scores.push_back(v); }
    a.io(dates);
    a.io(scores);
    if (a.reading) {
      p.dailyBest.clear();
      for (size_t i = 0; i < dates.size() && i < scores.size(); ++i) p.dailyBest[dates[i]] = std::max(0, scores[i]);
    }
  }
  if (version >= 3) {  // M5: achievements (name -> unlock time) and defeated monsters
    a.tag("ACHIEVEMENTS");
    std::vector<std::string> ach;
    std::vector<int64_t> when;
    if (!a.reading) for (auto& [n, t] : p.achievements) { ach.push_back(n); when.push_back(t); }
    a.io(ach);
    a.io(when);
    if (a.reading) {
      p.achievements.clear();
      for (size_t i = 0; i < ach.size() && i < when.size(); ++i) p.achievements.emplace(ach[i], std::max<int64_t>(0, when[i]));
    }
    ioSet("DEFEATED", p.defeatedMonsters);
  }
  if (version >= 4) {  // M-stats: totals, enemy stats, discovered events, per-character times
    a.tag("STATS");
    a.io(p.totalPlaytime);
    a.io(p.architectDamage);
    std::vector<std::string> ids;
    std::vector<int> wins, losses;
    if (!a.reading) for (auto& [id, e] : p.enemyStats) { ids.push_back(id); wins.push_back(e.wins); losses.push_back(e.losses); }
    a.io(ids);
    a.io(wins);
    a.io(losses);
    if (a.reading) {
      p.enemyStats.clear();
      for (size_t i = 0; i < ids.size() && i < wins.size() && i < losses.size(); ++i)
        p.enemyStats[ids[i]] = EnemyProgress{std::max(0, wins[i]), std::max(0, losses[i])};
    }
    ioSet("EVENTS", p.discoveredEvents);
    std::vector<std::string> cids;
    std::vector<int64_t> play, fast;
    if (!a.reading) for (auto& [id, cp] : p.characters) { cids.push_back(id); play.push_back(cp.playtime); fast.push_back(cp.fastestWin); }
    a.io(cids);
    a.io(play);
    a.io(fast);
    if (a.reading)
      for (size_t i = 0; i < cids.size() && i < play.size() && i < fast.size(); ++i) {
        CharacterProgress& cp = p.characters[cids[i]];
        cp.playtime = std::max<int64_t>(0, play[i]);
        cp.fastestWin = fast[i] < -1 ? -1 : fast[i];
      }
    p.totalPlaytime = std::max<int64_t>(0, p.totalPlaytime);
    p.architectDamage = std::max<int64_t>(0, p.architectDamage);
  }
  a.tag("END");
}

}  // namespace

std::string Progress::save() const {
  Archive a;
  ioProgress(a, const_cast<Progress&>(*this));
  return a.out;
}

bool Progress::load(const std::string& data) {
  if (data.empty()) return false;
  Archive a;
  a.reading = true;
  std::istringstream in(data);
  for (std::string t; in >> t;) a.toks.push_back(t);
  Progress fresh;
  ioProgress(a, fresh);
  if (!a.ok) return false;
  // Negative counters can only come from a corrupt or hand-edited file; clamp instead of
  // rejecting the whole save (ProgressState.ClampNonNegative does the same for its fields).
  for (auto& [name, v] : fresh.counters) if (v < 0) v = 0;
  for (auto& [id, cp] : fresh.characters) {
    if (cp.wins < 0) cp.wins = 0;
    if (cp.losses < 0) cp.losses = 0;
    if (cp.currentStreak < 0) cp.currentStreak = 0;
    if (cp.bestStreak < 0) cp.bestStreak = 0;
    if (cp.maxAscension < 0) cp.maxAscension = 0;
    if (cp.maxAscension > 10) cp.maxAscension = 10;
  }
  *this = std::move(fresh);
  return true;
}

namespace progress {

Progress& state() {
  static Progress p;
  return p;
}

void reset() { state() = Progress{}; }

void markCardSeen(const std::string& id) { if (!id.empty()) state().seenCards.insert(id); }
void markRelicSeen(const std::string& id) { if (!id.empty()) state().seenRelics.insert(id); }
void markPotionSeen(const std::string& id) { if (!id.empty()) state().seenPotions.insert(id); }
void markMonsterSeen(const std::string& id) { if (!id.empty()) state().seenMonsters.insert(id); }

void incrementCounter(const std::string& name, int64_t amount) { state().counters[name] += amount; }

// The C# shows "N/A" for the "events encountered" total (ENTRY_EVENTS.top); the port tallies the
// event rooms of finished runs in counters["eventsEncountered"] (one call per event map point).
void markEventSeen(const std::string& id) {
  if (id.empty()) return;
  state().discoveredEvents.insert(id);
  incrementCounter("eventsEncountered");
}

void recordRunTotals(const std::string& characterId, int64_t runSeconds, bool win, int score, bool standard) {
  runSeconds = std::max<int64_t>(0, runSeconds);
  CharacterProgress& cp = state().character(characterId);
  state().totalPlaytime += runSeconds;
  cp.playtime += runSeconds;
  if (!win) return;
  state().architectDamage += std::max(0, score);
  if (standard && (cp.fastestWin < 0 || cp.fastestWin > runSeconds)) cp.fastestWin = runSeconds;
}

void recordCombatEnd(const Combat& c, bool won) {
  for (auto& e : c.ownedEnemies) {
    if (!e || !e->monster) continue;
    EnemyProgress& s = state().enemyStats[e->monster->id];
    if (won) ++s.wins; else ++s.losses;
  }
}

void recordAncientRun(const std::string& ancientId, const std::string& characterId, bool win) {
  if (ancientId.empty()) return;
  incrementCounter("ancient." + ancientId + "." + characterId + (win ? ".wins" : ".losses"));
}

int ancientVisits(const std::string& ancientId, const std::string& characterId) {
  const auto& c = state().counters;
  std::string base = "ancient." + ancientId + "." + characterId;
  int64_t n = 0;
  for (const char* k : {".wins", ".losses"})
    if (auto it = c.find(base + k); it != c.end()) n += it->second;
  return (int)n;
}

int ancientTotalVisits(const std::string& ancientId) {
  std::string prefix = "ancient." + ancientId + ".";
  int64_t n = 0;
  for (auto it = state().counters.lower_bound(prefix); it != state().counters.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it)
    n += it->second;
  return (int)n;
}

// ProgressSaveManager.UpdateWithRunData + IncrementSingleplayerAscension.
void onRunEnded(const std::string& characterId, int ascension, RunOutcome outcome) {
  CharacterProgress& cp = state().character(characterId);
  if (outcome == RunOutcome::Win) {
    if (ascension == cp.maxAscension && cp.maxAscension < 10) ++cp.maxAscension;
    ++cp.wins;
    ++cp.currentStreak;
    if (cp.currentStreak > cp.bestStreak) cp.bestStreak = cp.currentStreak;
    incrementCounter("runsWon");
  } else {
    ++cp.losses;
    cp.currentStreak = 0;
    incrementCounter("runsLost");
    if (outcome == RunOutcome::Abandon) incrementCounter("runsAbandoned");
  }
}

std::string defaultPath() {
  if (const char* p = getenv("STS_PROGRESS_PATH")) return p;
  return profiles::progressPath();  // Y4: <save root>profile<N>/progress.sav
}

using safefile::readWhole;

// Y5: safe_file.h's atomic replace (a power loss never leaves a truncated file; loading goes
// through saveerr::loadChecked, which recovers the .tmp / .bak an interrupted write left).
bool save(const std::string& path) { return safefile::writeAtomic(path, state().save()); }

bool load(const std::string& path) {
  std::string data;
  if (!readWhole(path, data)) return false;
  return state().load(data);
}

}  // namespace progress

}  // namespace sts
