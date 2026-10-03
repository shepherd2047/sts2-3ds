// Y5 checks: atomic save writes and recovery after an interrupted write (core/safe_file.h), both
// with a rename that replaces (POSIX) and one that refuses an existing target (3DS SD / Windows,
// forced with safefile::testForceNoReplace), and through saveerr::loadChecked / profiles / history.
// Build: make -f Makefile.sdl build/safe_file_test ; run: ./build/safe_file_test
//
// Never touches a real save: every file lives under build/safe_file_fixtures/.
#include <cstdio>
#include "portable_env.h"
#include <cstdlib>
#include <string>
#ifdef _WIN32
#include <direct.h>
#endif

#include "../source/core/game.h"
#include "../source/core/history.h"
#include "../source/core/profiles.h"
#include "../source/core/progress.h"
#include "../source/core/safe_file.h"
#include "../source/core/save_errors.h"
#include "../source/core/settings_store.h"

using namespace sts;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                               \
  do {                                                                            \
    ++checks;                                                                     \
    if (!(cond)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #cond); } \
  } while (0)

static void setEnv(const char* k, const char* v) {
#ifdef _WIN32
  _putenv_s(k, v ? v : "");
#else
  if (v) setenv(k, v, 1); else unsetenv(k);
#endif
}

static const std::string kRoot = "build/safe_file_fixtures/";

static void put(const std::string& path, const std::string& data) {
  safefile::makeParentDirs(path);
  FILE* f = fopen(path.c_str(), "wb");
  if (f) { fwrite(data.data(), 1, data.size(), f); fclose(f); }
}
static std::string get(const std::string& path) {
  std::string s;
  return safefile::readWhole(path, s) ? s : std::string("<missing>");
}
static bool has(const std::string& p) { return safefile::exists(p); }

// A toy format: "GOOD <payload> END"; anything else (e.g. a truncated write) is corrupt.
static std::string loaded;
static bool validToy(const std::string& path) {
  std::string d;
  if (!safefile::readWhole(path, d) || d.size() < 9 || d.compare(0, 5, "GOOD ") != 0 || d.compare(d.size() - 4, 4, " END") != 0)
    return false;
  loaded = d.substr(5, d.size() - 9);
  return true;
}
static std::string toy(const std::string& payload) { return "GOOD " + payload + " END"; }

static void wipe(const std::string& p) {
  safefile::removeAll(p);
  std::string stem = p.substr(0, p.find_last_of('.'));
  remove((stem + ".corrupt").c_str());
}

static void writerChecks(bool noReplace) {
  safefile::testForceNoReplace(noReplace);
  const std::string f = kRoot + (noReplace ? "noreplace/" : "replace/") + "run.sav";
  wipe(f);
  // First write (no old file), then replace: the main file always holds a complete copy and no
  // .tmp / .bak is left behind.
  CHECK(safefile::writeAtomic(f, toy("one")));
  CHECK(get(f) == toy("one"));
  CHECK(!has(f + ".tmp") && !has(f + ".bak"));
  CHECK(safefile::writeAtomic(f, toy("two")));
  CHECK(get(f) == toy("two"));
  CHECK(!has(f + ".tmp") && !has(f + ".bak"));
  // A stale (truncated) .tmp from an earlier cut-off write does not get in the way.
  put(f + ".tmp", "GOOD thr");
  CHECK(safefile::writeAtomic(f, toy("three")));
  CHECK(get(f) == toy("three"));
  CHECK(!has(f + ".tmp"));
  // A write that cannot even create its .tmp (a directory is in the way) fails and leaves the
  // old file exactly as it was.
  const std::string blocked = f + ".tmp";
  safefile::makeParentDirs(blocked + "/x");
  CHECK(!safefile::writeAtomic(f, toy("four")));
  CHECK(get(f) == toy("three"));
#ifdef _WIN32
  _rmdir(blocked.c_str());
#else
  remove(blocked.c_str());
#endif
  // Large payloads survive intact (no partial main file).
  std::string big(200000, 'x');
  CHECK(safefile::writeAtomic(f, toy(big)));
  CHECK(get(f) == toy(big));
  safefile::testForceNoReplace(false);
}

static void recoveryChecks() {
  const std::string f = kRoot + "recover/progress.sav";
  // 1. Stale .tmp next to a good file (power lost while writing the .tmp): keep the good file,
  //    drop the leftover.
  wipe(f);
  put(f, toy("good"));
  put(f + ".tmp", "GOOD half");
  CHECK(safefile::recover(f, validToy) == safefile::Recover::MainOk);
  CHECK(loaded == "good" && get(f) == toy("good") && !has(f + ".tmp"));
  // 2. A complete .tmp next to a good (older) file (lost before the swap): still the good main.
  wipe(f);
  put(f, toy("old"));
  put(f + ".tmp", toy("new"));
  CHECK(safefile::recover(f, validToy) == safefile::Recover::MainOk);
  CHECK(get(f) == toy("old") && !has(f + ".tmp"));
  // 3. A .tmp without the main file (lost between the renames): the .tmp becomes the file.
  wipe(f);
  put(f + ".tmp", toy("new"));
  CHECK(safefile::recover(f, validToy) == safefile::Recover::Recovered);
  CHECK(loaded == "new" && get(f) == toy("new") && !has(f + ".tmp"));
  // 4. .tmp (new) and .bak (old), no main (3DS path, lost between the renames): the newer .tmp.
  wipe(f);
  put(f + ".tmp", toy("new"));
  put(f + ".bak", toy("old"));
  CHECK(safefile::recover(f, validToy) == safefile::Recover::Recovered);
  CHECK(get(f) == toy("new") && !has(f + ".tmp") && !has(f + ".bak"));
  // 5. Truncated .tmp and a good .bak, no main: the .bak.
  wipe(f);
  put(f + ".tmp", "GOOD ne");
  put(f + ".bak", toy("old"));
  CHECK(safefile::recover(f, validToy) == safefile::Recover::Recovered);
  CHECK(loaded == "old" && get(f) == toy("old") && !has(f + ".tmp") && !has(f + ".bak"));
  // 6. Main and .bak (lost before the .bak was removed): the main, .bak dropped.
  wipe(f);
  put(f, toy("new"));
  put(f + ".bak", toy("old"));
  CHECK(safefile::recover(f, validToy) == safefile::Recover::MainOk);
  CHECK(get(f) == toy("new") && !has(f + ".bak"));
  // 7. Garbled main but a complete .tmp: recovered, the garbled file kept as .corrupt.
  wipe(f);
  put(f, "garbage");
  put(f + ".tmp", toy("new"));
  CHECK(safefile::recover(f, validToy) == safefile::Recover::Recovered);
  CHECK(get(f) == toy("new") && get(kRoot + "recover/progress.corrupt") == "garbage");
  // 8. Nothing usable: main untouched (the S22 flow reports it), leftovers removed.
  wipe(f);
  put(f, "garbage");
  put(f + ".tmp", "GOOD x");
  CHECK(safefile::recover(f, validToy) == safefile::Recover::None);
  CHECK(get(f) == "garbage" && !has(f + ".tmp"));
  // 9. No file at all.
  wipe(f);
  CHECK(safefile::recover(f, validToy) == safefile::Recover::None);
  CHECK(!has(f));
  // removeAll takes the leftovers too, so a deleted save cannot come back.
  put(f, toy("a"));
  put(f + ".tmp", toy("b"));
  put(f + ".bak", toy("c"));
  safefile::removeAll(f);
  CHECK(!has(f) && !has(f + ".tmp") && !has(f + ".bak"));
  CHECK(safefile::recover(f, validToy) == safefile::Recover::None);
  wipe(f);
}

static void storeChecks() {
  // Settings through saveerr::loadChecked: a .tmp without the main file is a normal load, no
  // dialog queued.
  saveerr::clear();
  const std::string sp = kRoot + "store/settings.sav";
  wipe(sp);
  settings::reset();
  settings::state().fastMode = true;
  safefile::makeParentDirs(sp);
  CHECK(settings::save(sp));
  std::string good = get(sp);
  remove(sp.c_str());
  put(sp + ".tmp", good);
  settings::reset();
  CHECK(!settings::state().fastMode);
  CHECK(saveerr::loadChecked(sp, [](const std::string& p) { return settings::load(p); }, saveerr::Kind::SettingsCorrupt) ==
        saveerr::Load::Ok);
  CHECK(settings::state().fastMode);
  CHECK(has(sp) && !has(sp + ".tmp"));
  CHECK(!saveerr::pending(saveerr::Kind::SettingsCorrupt));
  // A truncated .tmp and no main: Missing (fresh defaults), nothing reported.
  wipe(sp);
  put(sp + ".tmp", good.substr(0, good.size() / 2));
  CHECK(saveerr::loadChecked(sp, [](const std::string& p) { return settings::load(p); }, saveerr::Kind::SettingsCorrupt) ==
        saveerr::Load::Missing);
  CHECK(!saveerr::pending(saveerr::Kind::SettingsCorrupt));
  wipe(sp);
  settings::reset();

  // Profiles: profile.sav recovered from .bak; progress.sav from .tmp.
  const std::string root = kRoot + "store/";
  profiles::reset();
  wipe(root + "profile.sav");
  for (int id = 1; id <= profiles::kCount; ++id) wipe(root + "profile" + std::to_string(id) + "/progress.sav");
  profiles::init(root);
  CHECK(profiles::rename(2, "Recovered"));
  CHECK(profiles::select(2));
  progress::incrementCounter("y5_test", 7);
  CHECK(profiles::saveProgress());
  profiles::reset();
  progress::reset();
  CHECK(!rename((root + "profile.sav").c_str(), (root + "profile.sav.bak").c_str()));
  CHECK(!rename((root + "profile2/progress.sav").c_str(), (root + "profile2/progress.sav.tmp").c_str()));
  profiles::init(root);
  CHECK(profiles::current() == 2);
  CHECK(profiles::info(2).name == "Recovered");
  CHECK(progress::state().counters["y5_test"] == 7);
  CHECK(!has(root + "profile.sav.bak") && !has(root + "profile2/progress.sav.tmp"));
  CHECK(!saveerr::pending(saveerr::Kind::ProgressCorrupt));

  // History: a record whose rename never happened (.tmp only) shows up again.
  history::removeAll(2);
  Run run;
  run.start(42, "Ironclad", 0);
  history::RunRecord rec = history::fromRun(run, false, false);
  CHECK(history::append(2, rec));
  CHECK(history::load(2).size() == 1);
  std::string slot = history::slotPath(2, (int)(rec.seq % history::kMax));
  CHECK(!rename(slot.c_str(), (slot + ".tmp").c_str()));
  CHECK(history::load(2).size() == 1);
  CHECK(has(slot) && !has(slot + ".tmp"));
  history::removeAll(2);
  CHECK(history::load(2).empty());

  for (int id = 1; id <= profiles::kCount; ++id) safefile::removeAll(root + "profile" + std::to_string(id) + "/progress.sav");
  safefile::removeAll(root + "profile.sav");
  profiles::reset();
  progress::reset();
}

int main() {
  setEnv("STS_PROGRESS_PATH", nullptr);
  setEnv("STS_FAKE_SAVE_ERROR", nullptr);
  safefile::makeParentDirs(kRoot + "x");
  writerChecks(false);
  writerChecks(true);
  recoveryChecks();
  storeChecks();
  CHECK(safefile::probeWritable(kRoot + "probe"));
  CHECK(!has(kRoot + "probe/sdcheck.tmp"));
  printf("safe_file_test: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
