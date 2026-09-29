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

namespace sts {

CharacterProgress& Progress::character(const std::string& id) { return characters[id]; }

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

namespace {
void makeParentDir(const std::string& path) {
  size_t slash = path.find_last_of("/\\");
  if (slash == std::string::npos) return;
  std::string dir = path.substr(0, slash);
  if (dir.empty()) return;
#ifdef _WIN32
  _mkdir(dir.c_str());
#else
  mkdir(dir.c_str(), 0777);
#endif
}

bool readWhole(const std::string& path, std::string& out) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  out.resize(n > 0 ? (size_t)n : 0);
  size_t got = n > 0 ? fread(&out[0], 1, (size_t)n, f) : 0;
  fclose(f);
  return got == out.size();
}
}  // namespace

bool save(const std::string& path) {
  makeParentDir(path);
  std::string data = state().save();
  std::string tmp = path + ".tmp";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (!f) return false;  // the previous file (if any) is untouched
  bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
  ok = fclose(f) == 0 && ok;
  if (!ok) { remove(tmp.c_str()); return false; }  // fallback: keep whatever was already there
  remove(path.c_str());
  return rename(tmp.c_str(), path.c_str()) == 0;
}

bool load(const std::string& path) {
  std::string data;
  if (!readWhole(path, data)) return false;
  return state().load(data);
}

}  // namespace progress

}  // namespace sts
