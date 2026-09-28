// Player settings checks (Y1): round trip, versioning, defaults, delete data, atomic write
// fallback. Build: make -f Makefile.sdl build/settings_test ; run: ./build/settings_test
//
// This never touches a real player's file: every settings::save/load/eraseAllData call below
// passes an explicit path under build/, and the in-memory settings::state() (and
// sts::progress::state(), touched by eraseAllData) is reset between sections.
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

#include "../source/core/game.h"
#include "../source/core/progress.h"
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

int main() {
  makeDir("build");
  makeDir("build/settings_fixtures");

  {  // defaults match the C# fields this was ported from (see settings.h/.cpp)
    Settings s;
    CHECK(s.fastMode == false);
    CHECK(s.screenShake == true);
    CHECK(s.textEffects == true);
    CHECK(s.runTimerEnabled == false);
    CHECK(s.handCardCountDisplay == false);
    CHECK(s.longPressConfirm == false);
    CHECK(s.commonTooltips == true);
    CHECK(s.bgmVolume == 0.5f);
    CHECK(s.sfxVolume == 0.5f);
    CHECK(s.ambienceVolume == 0.5f);
    CHECK(s.language == Language::ZhCN);
    CHECK(s.tutorialsEnabled == true);
    CHECK(s.tutorialsSeen.empty());
  }

  {  // round trip: every field survives save() -> load()
    settings::reset();
    Settings& s = settings::state();
    s.fastMode = true;
    s.screenShake = false;
    s.textEffects = false;
    s.runTimerEnabled = true;
    s.handCardCountDisplay = true;
    s.longPressConfirm = true;
    s.commonTooltips = false;
    s.bgmVolume = 0.2f;
    s.sfxVolume = 0.8f;
    s.ambienceVolume = 0.35f;
    s.language = Language::En;
    s.tutorialsEnabled = false;
    s.tutorialsSeen.insert("combat_intro");
    s.tutorialsSeen.insert("map_intro");

    std::string text = s.save();
    Settings back;
    CHECK(back.load(text));
    CHECK(back.fastMode == true);
    CHECK(back.screenShake == false);
    CHECK(back.textEffects == false);
    CHECK(back.runTimerEnabled == true);
    CHECK(back.handCardCountDisplay == true);
    CHECK(back.longPressConfirm == true);
    CHECK(back.commonTooltips == false);
    CHECK(back.bgmVolume == 0.2f);
    CHECK(back.sfxVolume == 0.8f);
    CHECK(back.ambienceVolume == 0.35f);
    CHECK(back.language == Language::En);
    CHECK(back.tutorialsEnabled == false);
    CHECK(back.tutorialsSeen == s.tutorialsSeen);
    CHECK(back.save() == text);  // stable round trip
  }

  {  // versioning
    Settings ignored;
    CHECK(!ignored.load(""));                                 // empty data
    CHECK(!ignored.load("NOT_A_SETTINGS_FILE 1 END"));        // wrong tag
    CHECK(!ignored.load("STS2SETTINGS 999999 END"));          // future version this build can't read
    CHECK(!ignored.load("STS2SETTINGS 0 END"));               // too-low version
    // Corrupt (garbled) data fails cleanly rather than crashing.
    CHECK(!ignored.load("STS2SETTINGS 1 TOGGLES 1"));
  }

  {  // out-of-range volumes from a hand-edited file are clamped on load, not rejected outright
    Settings s;
    s.bgmVolume = -1.f;
    s.sfxVolume = 5.f;
    s.ambienceVolume = 0.5f;
    std::string text = s.save();
    Settings back;
    CHECK(back.load(text));
    CHECK(back.bgmVolume == 0.f);
    CHECK(back.sfxVolume == 1.f);
    CHECK(back.ambienceVolume == 0.5f);
  }

  {  // tutorials: markTutorialSeen / tutorialSeen / resetTutorials
    settings::reset();
    CHECK(!settings::tutorialSeen("combat_intro"));
    settings::markTutorialSeen("combat_intro");
    CHECK(settings::tutorialSeen("combat_intro"));
    CHECK(!settings::tutorialSeen("map_intro"));
    // Disabling tutorials entirely hides an already-seen one too (ProgressState.SeenFtue).
    settings::state().tutorialsEnabled = false;
    CHECK(!settings::tutorialSeen("combat_intro"));
    settings::state().tutorialsEnabled = true;
    CHECK(settings::tutorialSeen("combat_intro"));
    settings::resetTutorials();
    CHECK(settings::state().tutorialsEnabled == true);
    CHECK(settings::state().tutorialsSeen.empty());
    CHECK(!settings::tutorialSeen("combat_intro"));
  }

  {  // atomic write: save() then load() round-trips through the filesystem
    settings::reset();
    settings::state().fastMode = true;
    settings::state().bgmVolume = 0.75f;
    std::string path = "build/settings_fixtures/settings.sav";
    remove(path.c_str());
    remove((path + ".tmp").c_str());
    CHECK(settings::save(path));
    settings::reset();
    CHECK(settings::state().fastMode == false);  // reset actually cleared it
    CHECK(settings::load(path));
    CHECK(settings::state().fastMode == true);
    CHECK(settings::state().bgmVolume == 0.75f);
  }

  {  // atomic write fallback: a write that can't complete leaves the previous file untouched
    std::string path = "build/settings_fixtures/fallback.sav";
    remove(path.c_str());
    remove((path + ".tmp").c_str());
    settings::reset();
    settings::state().sfxVolume = 0.4f;
    CHECK(settings::save(path));  // first, a good write
    std::string good;
    {
      FILE* f = fopen(path.c_str(), "rb");
      CHECK(f != nullptr);
      if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        good.resize((size_t)n);
        fread(&good[0], 1, (size_t)n, f);
        fclose(f);
      }
    }
    // Block the tmp write by putting a directory where the ".tmp" file needs to go.
    makeDir(path + ".tmp");
    settings::state().sfxVolume = 0.99f;  // a change that must NOT reach disk
    CHECK(!settings::save(path));
    std::string after;
    {
      FILE* f = fopen(path.c_str(), "rb");
      CHECK(f != nullptr);  // the original file is still there
      if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        after.resize((size_t)n);
        fread(&after[0], 1, (size_t)n, f);
        fclose(f);
      }
    }
    CHECK(after == good);  // byte-for-byte unchanged
    remove((path + ".tmp/.keep").c_str());
#ifdef _WIN32
    _rmdir((path + ".tmp").c_str());
#else
    rmdir((path + ".tmp").c_str());
#endif
  }

  {  // delete data: removes run.sav/progress.sav/settings.sav and resets everything in memory
    std::string runPath = "build/settings_fixtures/run.sav";
    std::string progressPath = "build/settings_fixtures/progress.sav";
    std::string settingsPath = "build/settings_fixtures/settings2.sav";
    writeFile(runPath, "fake run save data");
    progress::reset();
    progress::state().character("Ironclad").wins = 5;
    CHECK(progress::save(progressPath));
    settings::reset();
    settings::state().fastMode = true;
    settings::state().bgmVolume = 0.1f;
    settings::state().tutorialsSeen.insert("combat_intro");
    CHECK(settings::save(settingsPath));

    CHECK(fileExists(runPath));
    CHECK(fileExists(progressPath));
    CHECK(fileExists(settingsPath));

    CHECK(settings::eraseAllData(runPath, progressPath, settingsPath));

    CHECK(!fileExists(runPath));
    CHECK(!fileExists(progressPath));
    // The settings file is rewritten with fresh defaults, not merely deleted.
    CHECK(fileExists(settingsPath));
    CHECK(settings::state().fastMode == false);
    CHECK(settings::state().bgmVolume == 0.5f);
    CHECK(settings::state().tutorialsSeen.empty());
    CHECK(progress::state().characters.empty());

    // Calling it again (nothing left to delete) must not fail or crash.
    CHECK(settings::eraseAllData(runPath, progressPath, settingsPath));
  }

  {  // delete data with default progress path: an empty progressPath argument falls back to
    // sts::progress::defaultPath() rather than trying to remove a literal empty path.
    const char* prev = getenv("STS_PROGRESS_PATH");
    std::string savedPrev = prev ? prev : "";
#ifdef _WIN32
    _putenv_s("STS_PROGRESS_PATH", "build/settings_fixtures/progress_default.sav");
#else
    setenv("STS_PROGRESS_PATH", "build/settings_fixtures/progress_default.sav", 1);
#endif
    writeFile(progress::defaultPath(), progress::state().save());
    CHECK(fileExists(progress::defaultPath()));
    std::string settingsPath = "build/settings_fixtures/settings3.sav";
    CHECK(settings::eraseAllData("build/settings_fixtures/run_unused.sav", "", settingsPath));
    CHECK(!fileExists(progress::defaultPath()));
#ifdef _WIN32
    if (!savedPrev.empty()) _putenv_s("STS_PROGRESS_PATH", savedPrev.c_str());
    else _putenv_s("STS_PROGRESS_PATH", "");
#else
    if (!savedPrev.empty()) setenv("STS_PROGRESS_PATH", savedPrev.c_str(), 1);
    else unsetenv("STS_PROGRESS_PATH");
#endif
  }

  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
