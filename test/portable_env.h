// setenv / unsetenv for the tests on Windows (MinGW has only _putenv_s); POSIX builds use the libc ones.
#pragma once
#include <cstdlib>
#ifdef _WIN32
inline int setenv(const char* k, const char* v, int overwrite) {
  if (!overwrite && std::getenv(k)) return 0;
  return _putenv_s(k, v);
}
inline int unsetenv(const char* k) { return _putenv_s(k, ""); }
#endif
