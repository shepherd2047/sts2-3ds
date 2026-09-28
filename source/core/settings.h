// Player settings/preferences (package Y1): the persistent options a player sets once and that
// apply across every run -- fast mode, screen shake, audio volumes, language, the in-combat run
// timer, floating text effects, long-press confirmation on destructive buttons, the common
// keyword tooltips, the hand card-count readout, which tutorials have been shown, and the
// "delete all data" / "reset tutorials" actions the settings screen (S21, separate package) will
// call. This module owns the *state and persistence*; S21 only needs to read/write
// `settings::state()` and call `settings::save()` -- it does not touch the file format.
//
// Ported from the C#'s two separate preference saves -- Saves/SettingsSave.cs (machine-level,
// "settings.save": volumes, language) and Saves/PrefsSave.cs (profile-level, "prefs.save":
// FastMode, ScreenShakeOptionIndex, ShowRunTimer, TextEffectsEnabled, IsLongPressEnabled,
// ShowCardIndices) -- plus the FTUE/tutorial fields that live in Saves/ProgressState.cs
// (EnableFtues, FtueCompleted). This port merges all of it into one file, `settings.sav`, per the
// owner's package brief; see settings.cpp for the exact C# field/default each one was ported from.
// "Common tooltips" has no C# backing field (NCommonTooltipsTickbox.SetFromSettings() is an empty
// stub that toasts "not implemented" on toggle) -- it is a new, real toggle here since F4 already
// has working tooltips, unlike the C#'s.
//
// Same shape as progress.h/.cpp (package M1): a plain-old-data struct with a versioned Archive
// token-stream save()/load() (game.h's Archive, shared by save.cpp/progress.cpp), a process-wide
// `state()` instance mutated in place, and free functions doing atomic tmp-then-rename file I/O
// with plain <cstdio> so this stays a gfx-free core module the headless sim and tests can link
// without a platform backend. Nothing here touches disk unless a caller calls settings::save()/
// load() explicitly; the headless sim and tests never do (they always pass an explicit path under
// build/, never the default).
#pragma once
#include <cstdint>
#include <set>
#include <string>

namespace sts {

// The two languages the owner decided to ship (docs/PLAN.md "Owner decisions"); Y3 bakes the loc
// and fonts for both and switches at runtime by reading this field.
enum class Language { ZhCN, En };

struct Settings {
  static constexpr int kVersion = 1;

  // --- combat / presentation (C#'s PrefsSave.cs) ---
  // C#'s FastMode is a 3-level FastModeType enum {None, Normal, Fast, Instant}, default Normal;
  // this port only ever had two speeds (existing App::fastMode_ * Scheduler::speed = 1.75), so it
  // stays a bool here rather than growing a matching enum.
  bool fastMode = false;
  // C#'s ScreenShakeOptionIndex is an int 0-4 (NONE/SOME/NORMAL/LOTS/CAAAW, multiplier
  // 0/0.5/1/2/4, default 2 = NORMAL/on); this port's shake is a single sin-wave displacement
  // (combat_scene.cpp), so it stays the existing on/off bool (App::screenShake_) rather than a
  // multiplier.
  bool screenShake = true;
  bool textEffects = true;          // PrefsSave.TextEffectsEnabled, default true
  bool runTimerEnabled = false;     // PrefsSave.ShowRunTimer, default false
  bool handCardCountDisplay = false;  // PrefsSave.ShowCardIndices, default false
  bool longPressConfirm = false;    // PrefsSave.IsLongPressEnabled, default false
  // No C# backing field (see the file comment) -- new toggle, defaults on since F4's tooltips work.
  bool commonTooltips = true;

  // --- audio (SettingsSave.cs; U-track wires these to ndsp, stored here regardless) ---
  float bgmVolume = 0.5f;       // SettingsSave.VolumeBgm, 0..1
  float sfxVolume = 0.5f;       // SettingsSave.VolumeSfx, 0..1
  float ambienceVolume = 0.5f;  // SettingsSave.VolumeAmbience, 0..1

  // --- language (SettingsSave.Language is a free-form string, default null = device locale, from
  // a 16-language LocManager list; the owner decided this port only ships 简体中文 + English
  // (docs/PLAN.md), so it is a plain 2-value enum defaulting to Chinese) ---
  Language language = Language::ZhCN;

  // --- tutorials/FTUE seen. In the C# this lives in ProgressState (EnableFtues bool, default
  // true; FtueCompleted set<string>, default empty), not either prefs save; it is kept here
  // instead because this port's "reset tutorials" button (S21) is a settings-screen action and
  // Y1 owns settings.sav -- see ProgressState.ResetFtues() (sets EnableFtues=true, clears the set).
  bool tutorialsEnabled = true;
  std::set<std::string> tutorialsSeen;

  // Same Archive token-stream approach as save.cpp/progress.cpp: writing and reading share one
  // function (ioSettings in settings.cpp) so the two can never drift apart.
  std::string save() const;
  // Leaves *this unchanged and returns false if `data` is empty, garbled or a future version this
  // build does not understand (kVersion mismatch in either direction).
  bool load(const std::string& data);
};

namespace settings {

// The process-wide instance. Nothing here touches disk on its own; whoever changes a field is
// expected to call settings::save() afterwards (mirrors App::saveSettings()).
Settings& state();
void reset();  // test helper / part of "delete all data": back to fresh defaults

// Mirrors ProgressState.SeenFtue: false immediately if tutorialsEnabled is off, else whether
// `id` is in the seen set.
bool tutorialSeen(const std::string& id);
void markTutorialSeen(const std::string& id);  // ProgressState.MarkFtueAsComplete
// "Reset tutorials" (ProgressState.ResetFtues): tutorialsEnabled = true and the seen set cleared;
// does not touch any other setting.
void resetTutorials();

// The path used when save()/load() below are called with no argument: $STS_SETTINGS_PATH if set
// (tests and the headless sim must set this, or call the string-based Settings::save/load
// directly, so they never touch a real player's file), else "saves/settings.sav" on desktop or
// the 3DS save directory next to run.sav (sdmc:/3ds/sts2-3ds/settings.sav) on device -- the same
// split gfx_sdl.cpp/gfx_3ds.cpp's saveDir() makes for run.sav, duplicated here (via __3DS__,
// defined by devkitARM) so this module stays gfx-free and linkable into the headless sim/tests.
std::string defaultPath();

// Atomic file I/O for `state()`: write to "<path>.tmp" then rename over `path`, the same
// tmp-then-rename approach as gfx::writeSave / progress::save so a crash or power loss mid-write
// leaves the previous file intact instead of a truncated one.
bool save(const std::string& path = defaultPath());
bool load(const std::string& path = defaultPath());

// The default location of the run save and profile progress files, duplicated here (matching
// gfx_sdl.cpp's kSaveName + saveDir() and progress::defaultPath()) purely so eraseAllData() below
// can find them without linking gfx.h. Overridable so tests never touch a real save.
std::string defaultRunSavePath();

// "Delete data": removes the run save, the profile progress file and the settings file from disk,
// and resets every in-memory piece of state (settings::state(), sts::progress::state()) back to
// fresh defaults. The settings screen (S21) calls this after the player confirms; it is safe to
// call with no run/profile in progress (a missing file is not an error). Paths default to the
// three modules' own defaultPath()s but can be overridden by tests so a real save is never at risk.
bool eraseAllData(const std::string& runSavePath = defaultRunSavePath(),
                   const std::string& progressPath = "",
                   const std::string& settingsPath = defaultPath());

}  // namespace settings

}  // namespace sts
