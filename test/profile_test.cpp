// Profile checks (Y4): profile.sav round trip, per-profile paths, first-launch migration of a
// top-level run.sav / progress.sav into profile 1, switching / renaming / deleting slots, and the
// progress.sav round trip through the current profile.
// Build: make -f Makefile.sdl build/profile_test ; run: ./build/profile_test
//
// Never touches a real save: every profiles::init() below uses a root under build/.
#include <cstdio>
#include "portable_env.h"
#include <cstdlib>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

#include "../source/core/game.h"
#include "../source/core/profiles.h"
#include "../source/core/progress.h"

using namespace sts;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                               \
  do {                                                                            \
    ++checks;                                                                     \
    if (!(cond)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #cond); } \
  } while (0)

static void makeDir(const std::string& d) {
#ifdef _WIN32
  _mkdir(d.c_str());
#else
  mkdir(d.c_str(), 0777);
#endif
}

static bool fileExists(const std::string& path) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  fclose(f);
  return true;
}

static void writeFile(const std::string& path, const std::string& data) {
  FILE* f = fopen(path.c_str(), "wb");
  if (f) { fwrite(data.data(), 1, data.size(), f); fclose(f); }
}

static const std::string kRoot = "build/profile_fixtures/";

static void clean() {
  for (const char* f : {"run.sav", "progress.sav", "profile.sav", "profile.sav.tmp"}) remove((kRoot + f).c_str());
  for (int id = 1; id <= profiles::kCount; ++id)
    for (const char* f : {"run.sav", "progress.sav", "progress.sav.tmp"})
      remove((kRoot + "profile" + std::to_string(id) + "/" + f).c_str());
}

int main() {
#ifdef _WIN32
  _putenv_s("STS_PROGRESS_PATH", "");
#else
  unsetenv("STS_PROGRESS_PATH");
#endif
  makeDir("build");
  makeDir(kRoot);
  clean();

  {  // profile.sav format: round trip (UTF-8 names with spaces), garbled / future versions rejected
    profiles::ProfileFile f;
    f.current = 3;
    f.names[0] = "阿 Bob";
    f.names[2] = "x y z";
    profiles::ProfileFile g;
    CHECK(g.load(f.save()));
    CHECK(g.current == 3);
    CHECK(g.names[0] == "阿 Bob");
    CHECK(g.names[1].empty());
    CHECK(g.names[2] == "x y z");
    CHECK(!g.load(""));
    CHECK(!g.load("garbage"));
    std::string future = f.save();
    future.replace(future.find(" 1 "), 3, " 99 ");
    CHECK(!g.load(future));
    CHECK(g.current == 3);  // unchanged on failure
  }

  {  // no init: disk off, nothing written
    profiles::reset();
    CHECK(!profiles::diskEnabled());
    CHECK(profiles::current() == 1);
    CHECK(!profiles::saveProgress());
    CHECK(profiles::select(2));
    CHECK(profiles::current() == 2);
    profiles::reset();
  }

  {  // paths
    profiles::reset();
    profiles::init("build/profile_fixtures");  // trailing slash added
    CHECK(profiles::rootDir() == kRoot);
    CHECK(profiles::dir(2) == kRoot + "profile2/");
    CHECK(profiles::runSaveName(3) == "profile3/run.sav");
    CHECK(profiles::runSavePath(1) == kRoot + "profile1/run.sav");
    CHECK(profiles::progressPath(2) == kRoot + "profile2/progress.sav");
    CHECK(profiles::profileFilePath() == kRoot + "profile.sav");
    CHECK(progress::defaultPath() == profiles::progressPath());
    CHECK(fileExists(kRoot + "profile.sav"));
    profiles::reset();
    clean();
  }

  {  // migration: an old top-level run.sav / progress.sav become profile 1's on first launch
    progress::reset();
    progress::state().character("Ironclad").wins = 7;
    writeFile(kRoot + "progress.sav", progress::state().save());
    writeFile(kRoot + "run.sav", "old run");
    progress::reset();
    profiles::init(kRoot);
    CHECK(!fileExists(kRoot + "run.sav"));
    CHECK(!fileExists(kRoot + "progress.sav"));
    CHECK(fileExists(kRoot + "profile1/run.sav"));
    CHECK(fileExists(kRoot + "profile1/progress.sav"));
    CHECK(fileExists(kRoot + "profile.sav"));
    CHECK(profiles::current() == 1);
    CHECK(progress::state().character("Ironclad").wins == 7);  // loaded on init
    auto i1 = profiles::info(1);
    CHECK(i1.used && i1.hasRun && i1.wins == 7);
    CHECK(!profiles::info(2).used);

    // Only on first launch: a top-level file appearing later is left alone.
    writeFile(kRoot + "run.sav", "stray");
    profiles::reset();
    profiles::init(kRoot);
    CHECK(fileExists(kRoot + "run.sav"));
    remove((kRoot + "run.sav").c_str());
  }

  {  // switching keeps each slot's progress apart; the choice persists in profile.sav
    CHECK(profiles::select(2));
    CHECK(profiles::current() == 2);
    CHECK(progress::state().characters.empty());  // slot 2 is fresh
    CHECK(fileExists(kRoot + "profile1/progress.sav"));
    progress::onRunEnded("Silent", 0, progress::RunOutcome::Win);
    progress::onRunEnded("Silent", 1, progress::RunOutcome::Loss);
    CHECK(profiles::saveProgress());
    CHECK(fileExists(kRoot + "profile2/progress.sav"));

    profiles::reset();
    progress::reset();
    profiles::init(kRoot);
    CHECK(profiles::current() == 2);
    CHECK(progress::state().character("Silent").wins == 1);
    CHECK(progress::state().character("Silent").losses == 1);
    CHECK(progress::state().character("Silent").maxAscension == 1);
    auto i2 = profiles::info(2);
    CHECK(i2.used && !i2.hasRun && i2.wins == 1 && i2.losses == 1);

    CHECK(profiles::select(1));
    CHECK(progress::state().character("Ironclad").wins == 7);
    CHECK(progress::state().characters.count("Silent") == 0);
    CHECK(profiles::info(2).wins == 1);  // read from disk for a non-current slot
    CHECK(!profiles::select(0));
    CHECK(!profiles::select(profiles::kCount + 1));
  }

  {  // rename: persisted, trimmed to kNameMax bytes without splitting a UTF-8 character
    CHECK(profiles::rename(3, "测试 profile"));
    std::string longName;
    for (int i = 0; i < 20; ++i) longName += "长";  // 60 bytes
    CHECK(profiles::rename(1, longName));
    profiles::reset();
    profiles::init(kRoot);
    CHECK(profiles::info(3).name == "测试 profile");
    CHECK(profiles::info(1).name.size() == 48);
    CHECK(profiles::info(1).name == longName.substr(0, 48));
    CHECK(!profiles::info(3).used);  // a name alone is not "used"
    CHECK(!profiles::rename(4, "x"));
  }

  {  // delete: files and name gone; deleting the current slot resets progress
    CHECK(profiles::current() == 1);
    CHECK(profiles::remove(2));
    CHECK(!fileExists(kRoot + "profile2/progress.sav"));
    CHECK(!profiles::info(2).used);
    CHECK(progress::state().character("Ironclad").wins == 7);  // current slot untouched
    CHECK(profiles::remove(1));
    CHECK(!fileExists(kRoot + "profile1/run.sav"));
    CHECK(!fileExists(kRoot + "profile1/progress.sav"));
    CHECK(profiles::info(1).name.empty());
    CHECK(progress::state().characters.empty());
    CHECK(profiles::current() == 1);  // stays selectable as an empty slot
  }

  profiles::reset();
  clean();
  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
