// Profiles (package Y4): 3 save slots, each with its own run save and progress file.
//
// Ported from the C#'s Saves.Managers/ProfileSaveManager.cs (account-level profile.save holding
// last_profile_id, maxProfileCount = 3) and SaveManager.InitProfileId / SwitchProfileId /
// DeleteProfile, with UserDataPathProvider.GetProfileDir's "profile<N>" directory names.
// Layout (root = sdmc:/3ds/sts2-3ds/ on 3DS, saves/ on desktop -- gfx::readSave's directory):
//   <root>/profile.sav            current slot + the 3 slot names (global)
//   <root>/settings.sav           Y1 settings (global; see settings_store.h)
//   <root>/profile<N>/run.sav     the run in progress of slot N (1..3)
//   <root>/profile<N>/progress.sav  M1 progress of slot N
//   <root>/profile<N>/history/NN.run  M2 run history of slot N (history.h)
// PORT NOTE (n/a: owner): flat profile<N>/*.sav files instead of the C#'s profile<N>/saves/*.save.
// The C# nests these under profile<N>/saves/ and names them *.save; the port keeps
// its .sav names and a flat per-profile directory. Slot names are a port addition (the C#
// profiles are unnamed); S03 edits them with the 3DS software keyboard.
//
// Nothing here touches disk until init() is called: automated previews (STS_HIDDEN /
// STS_NO_SAVE), the headless sim and the tests either never call it or pass a root under build/.
#pragma once
#include <string>

namespace sts {
namespace profiles {

constexpr int kCount = 3;       // slots 1..kCount (ProfileSaveManager.maxProfileCount)
constexpr int kNameMax = 48;    // bytes of UTF-8 (16 CJK characters)

// profile.sav's contents. Names are stored hex-encoded so any UTF-8 (spaces included) survives
// the whitespace-separated Archive token stream.
struct ProfileFile {
  static constexpr int kVersion = 1;
  int current = 1;
  std::string names[kCount];  // "" = unnamed (the UI shows its default label)
  std::string save() const;
  bool load(const std::string& data);  // false (and *this unchanged) if empty/garbled/future
};

// Platform default root with a trailing slash (same directory gfx_sdl/gfx_3ds saveDir() use).
std::string defaultRoot();

// Enables disk access under `root`: reads profile.sav, migrates a pre-profiles top-level
// run.sav / progress.sav into profile 1 (first launch only, i.e. no profile.sav yet), writes
// profile.sav, and loads the current slot's progress into progress::state().
void init(const std::string& root = defaultRoot());
// Back to the in-memory-only state (current = 1, no names, disk off). For tests.
void reset();
bool diskEnabled();

int current();                     // 1..kCount
std::string rootDir();             // root with trailing slash
std::string dir(int id);           // <root>profile<id>/
std::string runSaveName(int id);   // "profile<id>/run.sav", relative to root (for gfx::readSave)
std::string runSavePath(int id);   // <root>profile<id>/run.sav
std::string progressPath(int id);  // <root>profile<id>/progress.sav
std::string profileFilePath();     // <root>profile.sav
inline std::string runSaveName() { return runSaveName(current()); }
inline std::string runSavePath() { return runSavePath(current()); }
inline std::string progressPath() { return progressPath(current()); }

// ---- API for the profile screen (S03) --------------------------------------------------
struct Info {
  int id = 1;
  std::string name;   // "" = unnamed
  bool used = false;  // has a run save or a progress file
  bool hasRun = false;
  int wins = 0, losses = 0;  // summed over characters (from the slot's progress.sav)
};
Info info(int id);
// Switch slot: saves the current slot's progress, loads `id`'s (fresh if none), persists
// profile.sav. Call only from the title (no run in progress); App::selectProfile does this and
// refreshes the title's Continue button.
bool select(int id);
// Name is trimmed to kNameMax bytes (whole UTF-8 characters); "" resets to the default label.
bool rename(int id, const std::string& name);
// Deletes the slot's run.sav, progress.sav and run history (history/) and clears its name. Deleting the current slot
// also resets progress::state(). The slot stays selectable (as an empty profile).
bool remove(int id);

// Writes progress::state() to the current slot's progress.sav (no-op without init()).
bool saveProgress();

// Moves <root>run.sav / <root>progress.sav into profile 1 if profile 1 has none yet. Called by
// init() when profile.sav is missing; exposed for tests. Returns how many files moved.
int migrateLegacy();

}  // namespace profiles
}  // namespace sts
