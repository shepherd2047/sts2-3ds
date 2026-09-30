// S22: save / load failures the UI reports in a dialog (C# NGame.ShowInvalidSavePopup /
// NMainMenu's INVALID_SAVE_POPUP, CorruptFileHandler).
//
// The loaders stay "false = could not load"; this module tells a missing file (a fresh profile,
// nothing to say) from one that is there but garbled (moved aside to <name>.corrupt, as
// CorruptFileHandler.GenerateCorruptFilePath does, so the next save cannot overwrite what might
// still be recovered), and queues one report per kind for the UI (ui/confirm.cpp shows them).
// gfx-free, so the headless sim and the tests link it; nothing here touches disk except
// loadChecked() on the path it is given.
#pragma once
#include <string>

namespace sts {
namespace saveerr {

// Y5: SdUnavailable = the SD card is missing or not writable at startup (the game runs on
// without saving; storageAvailable() is then false).
enum class Kind { RunCorrupt, ProgressCorrupt, SettingsCorrupt, WriteFailed, SdUnavailable, Count };
enum class Load { Ok, Missing, Corrupt };

// Queue a report (at most one pending per kind). The UI takes them one at a time.
void report(Kind k);
bool pending(Kind k);
bool take(Kind& out);  // false when nothing is queued
void clear();

// Debug for previews: STS_FAKE_SAVE_ERROR=run|progress|settings|write|sd (comma list) makes that
// error happen: the UI reports run / progress / settings / sd at startup and write at every save.
bool faked(Kind k);

// Reads `path` through `load` (e.g. progress::load, settings::load): Ok (Y5: also when the file
// was recovered from the .tmp / .bak an interrupted write left, safe_file.h), Missing (no file:
// the caller keeps its defaults), or Corrupt (the file is there but `load` rejected it: it is
// renamed to <path without .sav>.corrupt, `kind` is reported and the caller must reset to
// defaults). Never throws, never crashes on garbage.
Load loadChecked(const std::string& path, bool (*load)(const std::string&), Kind kind);

// Y5: false after the startup SD check failed (App::init); saves are then skipped for the session.
void setStorageAvailable(bool ok);
bool storageAvailable();

// <dir>/<stem>.sav -> <dir>/<stem>.corrupt (any older .corrupt of that name is replaced).
// Returns the new path, or "" when the rename failed (the file then stays where it was).
std::string quarantine(const std::string& path);

}  // namespace saveerr
}  // namespace sts
