// Player settings (package Y1). See settings.h for the data model and what it was ported from.
// The token-stream format follows save.cpp/progress.cpp's Archive pattern exactly (one io
// function shared by save() and load()); the file I/O below mirrors progress.cpp's tmp-then-
// rename atomic write without depending on gfx.h, so this stays a plain core module the headless
// sim and tests can link without a platform backend.
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
#include "settings_store.h"

namespace sts {

namespace {

// Same token-stream Archive as save.cpp/progress.cpp. Reading and writing share this function.
void ioSettings(Archive& a, Settings& s) {
  a.tag("STS2SETTINGS");
  int version = Settings::kVersion;
  a.io(version);
  if (version < 1 || version > Settings::kVersion) { a.ok = false; return; }

  a.tag("TOGGLES");
  a.io(s.fastMode);
  a.io(s.screenShake);
  a.io(s.textEffects);
  a.io(s.runTimerEnabled);
  a.io(s.handCardCountDisplay);
  a.io(s.longPressConfirm);
  a.io(s.commonTooltips);

  a.tag("AUDIO");
  a.io(s.bgmVolume);
  a.io(s.sfxVolume);
  a.io(s.ambienceVolume);

  a.tag("LANGUAGE");
  int lang = (int)s.language;
  a.io(lang);
  if (a.reading) s.language = (lang == (int)Language::En) ? Language::En : Language::ZhCN;

  a.tag("TUTORIALS");
  a.io(s.tutorialsEnabled);
  std::vector<std::string> seen;
  if (!a.reading) seen.assign(s.tutorialsSeen.begin(), s.tutorialsSeen.end());
  a.io(seen);
  if (a.reading) s.tutorialsSeen = std::set<std::string>(seen.begin(), seen.end());

  a.tag("END");
}

}  // namespace

std::string Settings::save() const {
  Archive a;
  ioSettings(a, const_cast<Settings&>(*this));
  return a.out;
}

bool Settings::load(const std::string& data) {
  if (data.empty()) return false;
  Archive a;
  a.reading = true;
  std::istringstream in(data);
  for (std::string t; in >> t;) a.toks.push_back(t);
  Settings fresh;
  ioSettings(a, fresh);
  if (!a.ok) return false;
  // Volumes from a hand-edited file are clamped instead of rejecting the whole save (matches
  // progress.cpp's negative-counter clamp).
  auto clamp01 = [](float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); };
  fresh.bgmVolume = clamp01(fresh.bgmVolume);
  fresh.sfxVolume = clamp01(fresh.sfxVolume);
  fresh.ambienceVolume = clamp01(fresh.ambienceVolume);
  *this = std::move(fresh);
  return true;
}

namespace settings {

Settings& state() {
  static Settings s;
  return s;
}

void reset() { state() = Settings{}; }

bool tutorialSeen(const std::string& id) {
  if (!state().tutorialsEnabled) return true;  // ProgressSaveManager.SeenFtue: all off = all seen
  return state().tutorialsSeen.count(id) != 0;
}

void markTutorialSeen(const std::string& id) {
  if (!id.empty()) state().tutorialsSeen.insert(id);
}

void resetTutorials() {
  state().tutorialsEnabled = true;
  state().tutorialsSeen.clear();
}

std::string defaultPath() {
  if (const char* p = getenv("STS_SETTINGS_PATH")) return p;
#ifdef __3DS__
  return "sdmc:/3ds/sts2-3ds/settings.sav";
#else
  return "saves/settings.sav";
#endif
}

std::string defaultRunSavePath() {
  if (const char* p = getenv("STS_RUN_SAVE_PATH")) return p;
  return profiles::runSavePath();  // Y4: the current profile's run save
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

bool eraseAllData(const std::string& runSavePath, const std::string& progressPath,
                   const std::string& settingsPath) {
  std::string progPath = progressPath.empty() ? progress::defaultPath() : progressPath;
  remove(runSavePath.c_str());
  remove((runSavePath + ".tmp").c_str());
  remove(progPath.c_str());
  remove((progPath + ".tmp").c_str());
  progress::reset();
  reset();  // settings back to fresh defaults
  // "Settings reset" persists the fresh defaults (rather than leaving a stale file on disk that
  // a later load() would still read back).
  return save(settingsPath);
}

}  // namespace settings

}  // namespace sts
