// Run history (package M2). See history.h for the format and what it was ported from. File I/O is
// plain <cstdio> (tmp-then-rename, like progress.cpp / profiles.cpp) so this stays a core module.
#include <cstdio>
#include <ctime>
#include <sstream>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "game.h"
#include "history.h"
#include "profiles.h"

namespace sts {
namespace history {

namespace {

void ioRecord(Archive& a, RunRecord& r) {
  a.tag("STS2RUN");
  int version = RunRecord::kVersion;
  a.io(version);
  if (version < 1 || version > RunRecord::kVersion) { a.ok = false; return; }
  a.io(r.seq);
  a.io(r.seed);
  a.io(r.character);
  a.io(r.ascension);
  a.io(r.acts);
  a.io(r.startTime);
  a.io(r.runTime);
  a.io(r.win);
  a.io(r.abandoned);
  a.io(r.killedByEncounter);
  a.io(r.killedByEvent);
  a.io(r.floorReached);
  a.io(r.gold);
  a.io(r.hp);
  a.io(r.maxHp);
  a.io(r.score);
  a.tag("PATH");
  ioPath(a, r.path);
  a.tag("DECK");
  int n = (int)r.deck.size();
  a.io(n);
  if (a.reading) r.deck.assign((size_t)std::clamp(n, 0, 10000), DeckCard{});
  for (auto& c : r.deck) {
    a.io(c.id);
    a.io(c.upgrades);
    a.io(c.enchantment);
    if (!c.enchantment.empty()) a.io(c.enchantAmount);
  }
  a.tag("RELICS");
  a.io(r.relics);
  a.tag("POTIONS");
  a.io(r.potions);
  a.io(r.maxPotionSlots);
  a.tag("END");
}

std::string slotName(int slot) {
  char buf[8];
  snprintf(buf, sizeof buf, "%02d", slot);
  return buf;
}

void makeParentDirs(const std::string& path) {
  for (size_t i = path.find('/'); i != std::string::npos; i = path.find('/', i + 1)) {
    std::string d = path.substr(0, i);
    if (d.empty() || d.back() == ':') continue;
#ifdef _WIN32
    _mkdir(d.c_str());
#else
    mkdir(d.c_str(), 0777);
#endif
  }
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

bool writeAtomic(const std::string& path, const std::string& data) {
  makeParentDirs(path);
  std::string tmp = path + ".tmp";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (!f) return false;
  bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
  ok = fclose(f) == 0 && ok;
  if (!ok) { std::remove(tmp.c_str()); return false; }
  std::remove(path.c_str());
  return std::rename(tmp.c_str(), path.c_str()) == 0;
}

std::unique_ptr<RunRecord>& lastRecord() {
  static std::unique_ptr<RunRecord> r;
  return r;
}

// Every room of the path in order.
std::vector<const Room*> allRooms(const Path& path) {
  std::vector<const Room*> out;
  for (auto& act : path)
    for (auto& p : act)
      for (auto& r : p.rooms) out.push_back(&r);
  return out;
}

}  // namespace

void ioPath(Archive& a, Path& path) {
  int acts = (int)path.size();
  a.io(acts);
  if (a.reading) path.assign((size_t)std::clamp(acts, 0, 16), {});
  for (auto& act : path) {
    int n = (int)act.size();
    a.io(n);
    if (a.reading) act.assign((size_t)std::clamp(n, 0, 1000), MapPoint{});
    for (auto& p : act) {
      int type = (int)p.type;
      a.io(type);
      p.type = (PointType)type;
      a.io(p.goldGained);
      int rooms = (int)p.rooms.size();
      a.io(rooms);
      if (a.reading) p.rooms.assign((size_t)std::clamp(rooms, 0, 16), Room{});
      for (auto& r : p.rooms) {
        int kind = (int)r.type;
        a.io(kind);
        r.type = (RoomKind)kind;
        a.io(r.model);
      }
    }
  }
}

std::string RunRecord::save() const {
  Archive a;
  ioRecord(a, const_cast<RunRecord&>(*this));
  return a.out;
}

bool RunRecord::load(const std::string& data) {
  if (data.empty()) return false;
  Archive a;
  a.reading = true;
  std::istringstream in(data);
  for (std::string t; in >> t;) a.toks.push_back(t);
  RunRecord fresh;
  ioRecord(a, fresh);
  if (!a.ok) return false;
  *this = std::move(fresh);
  return true;
}

// ---- ScoreUtility --------------------------------------------------------------------------

int floorScore(const Path& path) {
  int n = 0;
  for (size_t i = 0; i < path.size(); ++i) n += (int)path[i].size() * 10 * (int)(i + 1);
  return n;
}

int goldScore(const Path& path) {
  int gold = 0;
  for (auto& act : path)
    for (auto& p : act) gold += p.goldGained;
  return gold / 100;  // / (100 * playerCount)
}

int elitesKilled(const Path& path) {
  auto rooms = allRooms(path);
  int n = 0;
  for (auto* r : rooms) n += r->type == RoomKind::Elite;
  if (!rooms.empty() && rooms.back()->type == RoomKind::Elite) --n;  // the elite that killed you
  return n;
}

int bossesSlain(const Path& path, bool won) {
  // The C# skips the last room of the last point of the last act when the run was lost.
  const Room* killer = nullptr;
  if (!won && !path.empty() && !path.back().empty() && !path.back().back().rooms.empty())
    killer = &path.back().back().rooms.back();
  int n = 0;
  for (auto* r : allRooms(path)) n += r->type == RoomKind::Boss && r != killer;
  return n;
}

int score(const Path& path, int ascension, bool won) {
  int n = floorScore(path) + goldScore(path) + elitesKilled(path) * 50 + bossesSlain(path, won) * 100;
  return (int)((double)n * (1.0 + (double)ascension * 0.1));
}

// ---- recording -----------------------------------------------------------------------------

RunRecord fromRun(const Run& run, bool win, bool abandoned) {
  RunRecord r;
  r.seed = run.seed;
  r.character = run.characterId;
  r.ascension = run.ascension;
  r.acts = run.actIds;
  r.startTime = run.startTime;
  r.runTime = (int)run.runTime;
  r.win = win;
  r.abandoned = abandoned;
  r.floorReached = run.floor;
  r.path = run.mapHistory;
  // CreateRunHistoryEntry: a lost run was killed by the last room entered.
  if (!win && !r.path.empty() && !r.path.back().empty() && !r.path.back().back().rooms.empty()) {
    const Room& last = r.path.back().back().rooms.back();
    if (last.type == RoomKind::Monster || last.type == RoomKind::Elite || last.type == RoomKind::Boss)
      r.killedByEncounter = last.model;
    else if (last.type == RoomKind::Event)
      r.killedByEvent = last.model;
  }
  for (auto& c : run.deck) {
    if (!c) continue;
    DeckCard d;
    d.id = c->id;
    d.upgrades = c->upgradeLevel;
    if (c->enchantment) { d.enchantment = c->enchantment->id; d.enchantAmount = c->enchantment->amount; }
    r.deck.push_back(std::move(d));
  }
  for (auto& rel : run.relics) if (rel) r.relics.push_back(rel->id);
  for (auto& p : run.potions) if (p) r.potions.push_back(p->id);
  r.maxPotionSlots = (int)run.potions.size();
  r.gold = run.gold;
  if (run.player) { r.hp = std::max(0, run.player->hp); r.maxHp = run.player->maxHp; }
  r.score = score(r.path, r.ascension, win);
  return r;
}

void onRunEnded(const Run& run, bool win, bool abandoned) {
  auto rec = std::make_unique<RunRecord>(fromRun(run, win, abandoned));
  if (profiles::diskEnabled()) append(profiles::current(), *rec);
  lastRecord() = std::move(rec);
}

const RunRecord* last() { return lastRecord().get(); }
void clearLast() { lastRecord().reset(); }

// ---- store ---------------------------------------------------------------------------------

std::string dir(int profileId) { return profiles::dir(profileId) + "history/"; }
std::string slotPath(int profileId, int slot) { return dir(profileId) + slotName(slot) + ".run"; }

std::vector<RunRecord> load(int profileId) {
  std::vector<RunRecord> out;
  if (!profiles::diskEnabled()) return out;
  for (int slot = 0; slot < kMax; ++slot) {
    std::string data;
    RunRecord r;
    if (readWhole(slotPath(profileId, slot), data) && r.load(data)) out.push_back(std::move(r));
  }
  std::sort(out.begin(), out.end(), [](const RunRecord& a, const RunRecord& b) { return a.seq > b.seq; });
  return out;
}

int count(int profileId) { return (int)load(profileId).size(); }

bool append(int profileId, RunRecord& rec) {
  if (!profiles::diskEnabled()) return false;
  uint64_t top = 0;
  for (auto& r : load(profileId)) top = std::max(top, r.seq);
  rec.seq = top + 1;
  return writeAtomic(slotPath(profileId, (int)(rec.seq % kMax)), rec.save());
}

void removeAll(int profileId) {
  for (int slot = 0; slot < kMax; ++slot) {
    std::string p = slotPath(profileId, slot);
    std::remove(p.c_str());
    std::remove((p + ".tmp").c_str());
  }
  std::string d = dir(profileId);
  d.pop_back();
#ifdef _WIN32
  _rmdir(d.c_str());
#else
  rmdir(d.c_str());
#endif
}

}  // namespace history
}  // namespace sts
