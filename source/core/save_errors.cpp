// S22: see save_errors.h.
#include "save_errors.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sts {
namespace saveerr {

namespace {
constexpr int kKinds = (int)Kind::Count;
bool queued[kKinds] = {};
int order[kKinds];  // report order, so the dialogs come up as the errors happened
int count = 0;

bool exists(const std::string& path) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  fclose(f);
  return true;
}
}  // namespace

void report(Kind k) {
  int i = (int)k;
  if (i < 0 || i >= kKinds || queued[i]) return;
  queued[i] = true;
  order[count++] = i;
}

bool pending(Kind k) { return (int)k >= 0 && (int)k < kKinds && queued[(int)k]; }

bool take(Kind& out) {
  if (count == 0) return false;
  int i = order[0];
  for (int j = 1; j < count; ++j) order[j - 1] = order[j];
  --count;
  queued[i] = false;
  out = (Kind)i;
  return true;
}

void clear() {
  for (auto& q : queued) q = false;
  count = 0;
}

bool faked(Kind k) {
  const char* env = getenv("STS_FAKE_SAVE_ERROR");
  if (!env || !*env) return false;
  static const char* const names[kKinds] = {"run", "progress", "settings", "write"};
  const char* name = names[(int)k];
  const size_t n = strlen(name);
  for (const char* p = env; *p;) {
    const char* e = strchr(p, ',');
    size_t len = e ? (size_t)(e - p) : strlen(p);
    if (len == n && strncmp(p, name, n) == 0) return true;
    if (!e) break;
    p = e + 1;
  }
  return false;
}

std::string quarantine(const std::string& path) {
  std::string to = path;
  size_t slash = to.find_last_of('/');
  size_t dot = to.find_last_of('.');
  if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) to.resize(dot);
  to += ".corrupt";
  remove(to.c_str());
  if (rename(path.c_str(), to.c_str()) != 0) return "";
  return to;
}

Load loadChecked(const std::string& path, bool (*load)(const std::string&), Kind kind) {
  if (!exists(path)) return Load::Missing;
  if (load(path)) return Load::Ok;
  quarantine(path);
  report(kind);
  return Load::Corrupt;
}

}  // namespace saveerr
}  // namespace sts
