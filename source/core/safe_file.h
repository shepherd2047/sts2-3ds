// Y5: power-loss-safe save files. Every save (run.sav, progress.sav, settings.sav, profile.sav,
// history/NN.run) is written through writeAtomic() and read back through recover() (directly,
// or via saveerr::loadChecked), so pulling the battery or the SD card mid-write never leaves a
// truncated file in place of a good one.
//
// Write order (<f> = the save's path):
//   1. write <f>.tmp, fflush, fclose (and verify it reads back byte for byte);
//   2. rename <f>.tmp -> <f>. POSIX replaces atomically. The 3DS SD archive (FSUSER_RenameFile)
//      and Windows refuse to rename over an existing file, so when that rename fails:
//      rename <f> -> <f>.bak, rename <f>.tmp -> <f>, remove <f>.bak.
// Whatever step the power goes at, one complete copy survives: <f> (old or new), or <f>.tmp
// (new, complete once step 1 finished) next to <f>.bak (old). recover() puts it back.
//
// gfx-free plain <cstdio>, so the sim and the tests link it. No exceptions.
#pragma once
#include <string>
#include <vector>

namespace sts {
namespace safefile {

bool exists(const std::string& path);
bool readWhole(const std::string& path, std::string& out);
// Creates every directory along `path` up to its last '/' (errors ignored; "sdmc:" is skipped).
void makeParentDirs(const std::string& path);

// Atomic replace as described above. false = the old file (if any) is still intact; no .tmp is
// left behind on failure.
bool writeAtomic(const std::string& path, const std::string& data);

// Removes <path>, <path>.tmp and <path>.bak (a deleted save must not come back via recover()).
void removeAll(const std::string& path);

// Removes a leftover <path>.tmp / <path>.bak (called once <path> is known to be good).
void dropStale(const std::string& path);

// `valid(p)` reads and checks the file at p (typically: the real loader, so a success also
// loads it). If <path> is valid: stale .tmp/.bak are dropped, returns MainOk. Otherwise the
// complete leftover of an interrupted write is tried, newest first (<path>.tmp, then
// <path>.bak); the first valid one replaces <path> (a garbled <path> is first moved aside to
// <stem>.corrupt by saveerr::quarantine) and Recovered is returned -- valid() last ran on it.
// None: nothing valid (<path> is left as it was; unusable leftovers are removed).
enum class Recover { MainOk, Recovered, None };
Recover recover(const std::string& path, bool (*valid)(const std::string& path));

// Names (not paths) of the files in `dir` that end in ".tmp" or ".bak" (for stores with many
// slots, like history/, so recovery does not probe every slot's leftovers one by one).
std::vector<std::string> leftovers(const std::string& dir);

// SD card checks at startup (Y5): creates `dir` if needed and writes/removes a small probe
// file. false = the card is missing, locked or full.
bool probeWritable(const std::string& dir);

// The platform may block sleep mode while a save is being written (3DS: aptSetSleepAllowed).
// fn(true) before the first byte, fn(false) after the last rename. Null = none.
void setWriteGuard(void (*fn)(bool writing));

// Tests only: behave as if rename() could not replace an existing file (the 3DS / Windows path).
void testForceNoReplace(bool on);

}  // namespace safefile
}  // namespace sts
