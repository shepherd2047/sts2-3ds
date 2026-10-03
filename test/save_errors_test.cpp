// S22 checks: a corrupt run.sav / progress.sav / settings.sav loads as "error + safe default"
// instead of crashing (core/save_errors.h, Run::load hardening), and STS_FAKE_SAVE_ERROR parsing.
// Build: make -f Makefile.sdl build/save_errors_test ; run: ./build/save_errors_test
//
// Never touches a real save: every file lives under build/save_errors_fixtures/.
#include <cstdio>
#include "portable_env.h"
#include <cstdlib>
#include <sstream>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

#include "../source/core/game.h"
#include "../source/core/profiles.h"
#include "../source/core/progress.h"
#include "../source/core/save_errors.h"
#include "../source/core/settings_store.h"

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

static void setEnv(const char* k, const char* v) {
#ifdef _WIN32
  _putenv_s(k, v ? v : "");
#else
  if (v) setenv(k, v, 1); else unsetenv(k);
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

static const std::string kRoot = "build/save_errors_fixtures/";

static void clean() {
  for (const char* f : {"profile.sav", "settings.sav", "settings.corrupt", "settings.sav.tmp"}) remove((kRoot + f).c_str());
  for (int id = 1; id <= profiles::kCount; ++id)
    for (const char* f : {"run.sav", "progress.sav", "progress.corrupt", "progress.sav.tmp"})
      remove((kRoot + "profile" + std::to_string(id) + "/" + f).c_str());
}

static std::vector<std::string> tokens(const std::string& s) {
  std::vector<std::string> v;
  std::istringstream in(s);
  for (std::string t; in >> t;) v.push_back(t);
  return v;
}
static std::string join(const std::vector<std::string>& v, size_t n) {
  std::string s;
  for (size_t i = 0; i < n && i < v.size(); ++i) s += v[i] + " ";
  return s;
}

int main() {
  setEnv("STS_PROGRESS_PATH", nullptr);
  setEnv("STS_FAKE_SAVE_ERROR", nullptr);
  makeDir("build");
  makeDir(kRoot);
  clean();
  saveerr::clear();

  {  // the report queue: one per kind, in report order
    saveerr::report(saveerr::Kind::WriteFailed);
    saveerr::report(saveerr::Kind::ProgressCorrupt);
    saveerr::report(saveerr::Kind::WriteFailed);
    saveerr::Kind k;
    CHECK(saveerr::take(k) && k == saveerr::Kind::WriteFailed);
    CHECK(saveerr::take(k) && k == saveerr::Kind::ProgressCorrupt);
    CHECK(!saveerr::take(k));
  }

  {  // STS_FAKE_SAVE_ERROR=run|progress|settings|write, comma list
    CHECK(!saveerr::faked(saveerr::Kind::RunCorrupt));
    setEnv("STS_FAKE_SAVE_ERROR", "progress,write");
    CHECK(saveerr::faked(saveerr::Kind::ProgressCorrupt));
    CHECK(saveerr::faked(saveerr::Kind::WriteFailed));
    CHECK(!saveerr::faked(saveerr::Kind::RunCorrupt));
    CHECK(!saveerr::faked(saveerr::Kind::SettingsCorrupt));
    setEnv("STS_FAKE_SAVE_ERROR", "run");
    CHECK(saveerr::faked(saveerr::Kind::RunCorrupt));
    setEnv("STS_FAKE_SAVE_ERROR", nullptr);
  }

  {  // a missing file is not an error
    auto r = saveerr::loadChecked(kRoot + "nothing.sav", [](const std::string& p) { return settings::load(p); },
                                  saveerr::Kind::SettingsCorrupt);
    CHECK(r == saveerr::Load::Missing);
    CHECK(!saveerr::pending(saveerr::Kind::SettingsCorrupt));
  }

  {  // corrupt settings.sav: reported, moved to settings.corrupt, defaults kept
    settings::reset();
    const std::string path = kRoot + "settings.sav";
    writeFile(path, "garbage \x01\x02 not a settings file");
    auto r = saveerr::loadChecked(path, [](const std::string& p) { return settings::load(p); },
                                  saveerr::Kind::SettingsCorrupt);
    CHECK(r == saveerr::Load::Corrupt);
    CHECK(saveerr::pending(saveerr::Kind::SettingsCorrupt));
    CHECK(!fileExists(path));
    CHECK(fileExists(kRoot + "settings.corrupt"));
    CHECK(settings::state().save() == Settings{}.save());
    // a good file loads as Ok
    CHECK(settings::save(path));
    CHECK(saveerr::loadChecked(path, [](const std::string& p) { return settings::load(p); },
                               saveerr::Kind::SettingsCorrupt) == saveerr::Load::Ok);
    saveerr::clear();
  }

  {  // corrupt progress.sav: profiles::init reports it, keeps a fresh progress, never overwrites it
    makeDir(kRoot + "profile1");
    const std::string path = kRoot + "profile1/progress.sav";
    writeFile(path, "PROGRESS 999 \xff\xfe truncated");
    progress::state().characters["Ironclad"].wins = 7;  // must not survive
    profiles::init(kRoot);
    CHECK(saveerr::pending(saveerr::Kind::ProgressCorrupt));
    CHECK(progress::state().save() == Progress{}.save());
    CHECK(!fileExists(path));
    CHECK(fileExists(kRoot + "profile1/progress.corrupt"));
    // the next save writes a fresh progress.sav; the corrupt copy stays for recovery
    CHECK(profiles::saveProgress());
    CHECK(fileExists(path) && fileExists(kRoot + "profile1/progress.corrupt"));
    saveerr::clear();
    profiles::init(kRoot);  // and it now loads without an error
    CHECK(!saveerr::pending(saveerr::Kind::ProgressCorrupt));
    profiles::reset();
  }

  {  // corrupt run.sav: Run::load rejects it (no crash, no huge allocation)
    Run good;
    good.start(12345, "Ironclad", 0);
    const std::string save = good.save();
    {
      Run probe;
      CHECK(probe.load(save));
    }
    for (const char* bad : {"", "hello", "STS2SAVE", "STS2SAVE 9", "STS2SAVE 9 1 Ironclad 0 999999999",
                            "STS2SAVE 99 1 Ironclad 0", "STS2SAVE 9 1 NotACharacter 0 x y z"}) {
      Run probe;
      CHECK(!probe.load(bad));
    }
    const auto t = tokens(save);
    int truncOk = 0, truncN = 0;
    for (size_t n = 0; n + 1 < t.size(); n += 5) {  // every truncation lacks the END tag
      Run probe;
      ++truncN;
      truncOk += !probe.load(join(t, n));
    }
    CHECK(truncOk == truncN);
    int survived = 0;
    for (size_t i = 5; i < t.size(); i += 7)  // garbage in one token: loads or is rejected, never crashes
      for (const char* g : {"999999999", "-7", "x"}) {
        auto v = t;
        v[i] = g;
        Run probe;
        survived += probe.load(join(v, v.size())) ? 0 : 1;
      }
    CHECK(survived > 0);
    // indices a garbled file could still parse: rejected by the post-load check
    {
      Run probe;
      probe.start(12345, "Ironclad", 0);
      probe.currentNode = 100000;
      Run again;
      CHECK(!again.load(probe.save()));
    }
  }

  clean();
  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
