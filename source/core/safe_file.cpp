// Y5: see safe_file.h.
#include "safe_file.h"

#include <dirent.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include "save_errors.h"

namespace sts {
namespace safefile {

namespace {
void (*guardFn)(bool) = nullptr;
bool forceNoReplace = false;

struct Guard {
  Guard() { if (guardFn) guardFn(true); }
  ~Guard() { if (guardFn) guardFn(false); }
};

void makeOneDir(const std::string& d) {
#ifdef _WIN32
  _mkdir(d.c_str());
#else
  mkdir(d.c_str(), 0777);
#endif
}

// Pushes the stdio buffer and asks the OS / the 3DS FS service (sdmc devoptab fsync ->
// FSFILE_Flush) to put the bytes on the card before the file is renamed into place.
bool flushToDisk(FILE* f) {
  if (fflush(f) != 0) return false;
#ifdef _WIN32
  _commit(_fileno(f));
#else
  fsync(fileno(f));  // best effort: some filesystems report EINVAL for fsync
#endif
  return true;
}

bool writeFile(const std::string& path, const std::string& data) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return false;
  bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
  ok = flushToDisk(f) && ok;
  ok = fclose(f) == 0 && ok;
  return ok;
}
}  // namespace

bool exists(const std::string& path) {
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
  return n >= 0 && got == out.size();
}

void makeParentDirs(const std::string& path) {
  for (size_t i = path.find('/'); i != std::string::npos; i = path.find('/', i + 1)) {
    std::string d = path.substr(0, i);
    if (d.empty() || d.back() == ':' || d == "." || d == "..") continue;
    makeOneDir(d);
  }
}

bool writeAtomic(const std::string& path, const std::string& data) {
  Guard guard;
  makeParentDirs(path);
  const std::string tmp = path + ".tmp", bak = path + ".bak";
  // 1. The new data, complete and checked, next to the old file.
  std::string back;
  if (!writeFile(tmp, data) || !readWhole(tmp, back) || back != data) {
    remove(tmp.c_str());
    return false;
  }
  // 2. Into place: one atomic rename where the platform replaces existing files...
  if (!forceNoReplace && rename(tmp.c_str(), path.c_str()) == 0) {
    remove(bak.c_str());
    return true;
  }
  // ...else keep the old copy as .bak until the new one is in place.
  remove(bak.c_str());
  const bool hadOld = exists(path);
  if (hadOld && rename(path.c_str(), bak.c_str()) != 0) {
    remove(tmp.c_str());
    return false;
  }
  if (rename(tmp.c_str(), path.c_str()) != 0) {
    if (hadOld) rename(bak.c_str(), path.c_str());
    remove(tmp.c_str());
    return false;
  }
  remove(bak.c_str());
  return true;
}

void removeAll(const std::string& path) {
  remove(path.c_str());
  dropStale(path);
}

void dropStale(const std::string& path) {
  remove((path + ".tmp").c_str());
  remove((path + ".bak").c_str());
}

Recover recover(const std::string& path, bool (*valid)(const std::string& path)) {
  const bool have = exists(path);
  if (have && valid(path)) {
    dropStale(path);
    return Recover::MainOk;
  }
  // .tmp first: it only exists complete-and-newer or truncated (valid() rejects that one).
  for (const char* ext : {".tmp", ".bak"}) {
    const std::string cand = path + ext;
    if (!exists(cand) || !valid(cand)) continue;
    if (have) saveerr::quarantine(path);
    remove(path.c_str());  // still there if the quarantine rename failed
    if (rename(cand.c_str(), path.c_str()) != 0) {
      // Could not move it into place: copy instead (valid() already loaded the data).
      std::string data;
      if (readWhole(cand, data)) writeAtomic(path, data);
    }
    dropStale(path);
    return Recover::Recovered;
  }
  dropStale(path);  // nothing usable left over
  return Recover::None;
}

std::vector<std::string> leftovers(const std::string& dir) {
  std::vector<std::string> out;
  std::string d = dir;
  if (d.size() > 1 && d.back() == '/') d.pop_back();
  DIR* h = opendir(d.c_str());
  if (!h) return out;
  while (dirent* e = readdir(h)) {
    size_t n = strlen(e->d_name);
    if (n > 4 && (strcmp(e->d_name + n - 4, ".tmp") == 0 || strcmp(e->d_name + n - 4, ".bak") == 0))
      out.push_back(e->d_name);
  }
  closedir(h);
  return out;
}

bool probeWritable(const std::string& dir) {
  std::string d = dir;
  if (!d.empty() && d.back() != '/') d += '/';
  const std::string probe = d + "sdcheck.tmp";
  makeParentDirs(probe);
  bool ok = writeFile(probe, "sts2");
  std::string back;
  ok = ok && readWhole(probe, back) && back == "sts2";
  remove(probe.c_str());
  return ok;
}

void setWriteGuard(void (*fn)(bool writing)) { guardFn = fn; }
void testForceNoReplace(bool on) { forceNoReplace = on; }

}  // namespace safefile
}  // namespace sts
