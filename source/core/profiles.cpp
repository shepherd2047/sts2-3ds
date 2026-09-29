// Profiles (package Y4). See profiles.h for the layout and what it was ported from. File I/O is
// plain <cstdio> (tmp-then-rename, like progress.cpp / settings_store.cpp) so this stays a core
// module the tests can link without a platform backend.
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

namespace sts {

namespace {

std::string hexEncode(const std::string& s) {
  static const char* kHex = "0123456789abcdef";
  std::string out;
  for (unsigned char c : s) { out += kHex[c >> 4]; out += kHex[c & 15]; }
  return out;
}

bool hexDecode(const std::string& s, std::string& out) {
  if (s.size() % 2) return false;
  auto val = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
  out.clear();
  for (size_t i = 0; i < s.size(); i += 2) {
    int hi = val(s[i]), lo = val(s[i + 1]);
    if (hi < 0 || lo < 0) return false;
    out += (char)(hi * 16 + lo);
  }
  return true;
}

void ioProfiles(Archive& a, profiles::ProfileFile& p) {
  a.tag("STS2PROFILES");
  int version = profiles::ProfileFile::kVersion;
  a.io(version);
  if (version < 1 || version > profiles::ProfileFile::kVersion) { a.ok = false; return; }
  a.io(p.current);
  int count = profiles::kCount;
  a.io(count);
  if (count != profiles::kCount) { a.ok = false; return; }
  for (auto& name : p.names) {
    std::string hex = a.reading ? "" : hexEncode(name);
    a.io(hex);
    if (a.reading && !hexDecode(hex, name)) a.ok = false;
  }
  a.tag("END");
}

// Creates every directory along `path` up to its last '/' (errors ignored: most already exist,
// and "sdmc:" itself is not a directory).
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

bool fileExists(const std::string& path) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  fclose(f);
  return true;
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
  if (!ok) { remove(tmp.c_str()); return false; }
  remove(path.c_str());
  return rename(tmp.c_str(), path.c_str()) == 0;
}

void removeWithTmp(const std::string& path) {
  remove(path.c_str());
  remove((path + ".tmp").c_str());
}

struct State {
  bool disk = false;
  std::string root = profiles::defaultRoot();
  profiles::ProfileFile file;
};
State& S() {
  static State s;
  return s;
}

bool valid(int id) { return id >= 1 && id <= profiles::kCount; }

bool writeProfileFile() { return S().disk && writeAtomic(profiles::profileFilePath(), S().file.save()); }

// Loads `id`'s progress into progress::state(), or a fresh one if it has none (or it is garbled).
void loadProgress(int id) {
  progress::reset();
  if (S().disk && !progress::load(profiles::progressPath(id))) progress::reset();
}

}  // namespace

namespace profiles {

std::string ProfileFile::save() const {
  Archive a;
  ioProfiles(a, const_cast<ProfileFile&>(*this));
  return a.out;
}

bool ProfileFile::load(const std::string& data) {
  if (data.empty()) return false;
  Archive a;
  a.reading = true;
  std::istringstream in(data);
  for (std::string t; in >> t;) a.toks.push_back(t);
  ProfileFile fresh;
  ioProfiles(a, fresh);
  if (!a.ok) return false;
  if (!valid(fresh.current)) fresh.current = 1;
  for (auto& n : fresh.names) if (n.size() > (size_t)kNameMax) n.resize(kNameMax);
  *this = fresh;
  return true;
}

std::string defaultRoot() {
#ifdef __3DS__
  return "sdmc:/3ds/sts2-3ds/";
#else
  return "saves/";
#endif
}

void init(const std::string& root) {
  State& s = S();
  s.root = root;
  if (!s.root.empty() && s.root.back() != '/') s.root += '/';
  s.disk = true;
  s.file = ProfileFile{};
  std::string data;
  bool have = readWhole(profileFilePath(), data) && s.file.load(data);
  if (!have) migrateLegacy();  // first launch with profiles: an older top-level save becomes slot 1's
  writeProfileFile();
  loadProgress(s.file.current);
}

void reset() {
  State& s = S();
  s.disk = false;
  s.root = defaultRoot();
  s.file = ProfileFile{};
}

bool diskEnabled() { return S().disk; }
int current() { return S().file.current; }
std::string rootDir() { return S().root; }
std::string dir(int id) { return S().root + "profile" + std::to_string(id) + "/"; }
std::string runSaveName(int id) { return "profile" + std::to_string(id) + "/run.sav"; }
std::string runSavePath(int id) { return S().root + runSaveName(id); }
std::string progressPath(int id) { return dir(id) + "progress.sav"; }
std::string profileFilePath() { return S().root + "profile.sav"; }

int migrateLegacy() {
  int moved = 0;
  for (const char* name : {"run.sav", "progress.sav"}) {
    std::string from = S().root + name, to = dir(1) + name;
    if (!fileExists(from) || fileExists(to)) continue;
    makeParentDirs(to);
    if (std::rename(from.c_str(), to.c_str()) == 0) ++moved;
  }
  return moved;
}

Info info(int id) {
  Info r;
  r.id = id;
  if (!valid(id)) return r;
  r.name = S().file.names[id - 1];
  Progress p;
  bool haveProgress = false;
  if (id == current()) {
    p = progress::state();
    haveProgress = S().disk ? fileExists(progressPath(id)) : false;
  } else if (S().disk) {
    std::string data;
    haveProgress = readWhole(progressPath(id), data) && p.load(data);
  }
  r.hasRun = S().disk && fileExists(runSavePath(id));
  for (auto& [c, cp] : p.characters) { r.wins += cp.wins; r.losses += cp.losses; }
  r.used = r.hasRun || haveProgress;
  return r;
}

bool select(int id) {
  if (!valid(id)) return false;
  if (id == current()) return true;
  saveProgress();
  S().file.current = id;
  loadProgress(id);
  return !S().disk || writeProfileFile();
}

bool rename(int id, const std::string& name) {
  if (!valid(id)) return false;
  std::string n = name;
  if (n.size() > (size_t)kNameMax) {
    size_t cut = kNameMax;
    while (cut > 0 && ((unsigned char)n[cut] & 0xC0) == 0x80) --cut;  // don't split a character
    n.resize(cut);
  }
  S().file.names[id - 1] = n;
  return !S().disk || writeProfileFile();
}

bool remove(int id) {
  if (!valid(id)) return false;
  if (S().disk) {
    removeWithTmp(runSavePath(id));
    removeWithTmp(progressPath(id));
  }
  S().file.names[id - 1].clear();
  if (id == current()) progress::reset();
  return !S().disk || writeProfileFile();
}

bool saveProgress() { return S().disk && progress::save(progressPath()); }

}  // namespace profiles
}  // namespace sts
