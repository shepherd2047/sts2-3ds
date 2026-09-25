// Fixed-point stand-in for C# decimal. The game multiplies by factors such as
// 1.5, 0.75 and 0.7 and then truncates to int; doing that in binary floating
// point gives 20.999... for 30 * 0.7, so amounts are kept exact at 1e-6.
#pragma once
#include <cstdint>

namespace sts {

struct Dec {
  static constexpr int64_t kScale = 1000000;
  int64_t raw = 0;

  constexpr Dec() = default;
  constexpr Dec(int v) : raw((int64_t)v * kScale) {}
  static constexpr Dec fromRaw(int64_t r) { Dec d; d.raw = r; return d; }
  // Exact for literals with at most six decimal places.
  static constexpr Dec lit(double v) { return fromRaw((int64_t)(v * kScale + (v >= 0 ? 0.5 : -0.5))); }

  // (int) cast in C# truncates toward zero.
  constexpr int toInt() const { return (int)(raw / kScale); }
  int ceilInt() const { int64_t q = raw / kScale; return (int)(raw > 0 && raw % kScale ? q + 1 : q); }

  constexpr Dec operator+(Dec o) const { return fromRaw(raw + o.raw); }
  constexpr Dec operator-(Dec o) const { return fromRaw(raw - o.raw); }
  constexpr Dec operator-() const { return fromRaw(-raw); }
  // No __int128 on 32-bit ARM: split into high/low parts. Exact for the
  // magnitudes the game uses (amounts < 1e9, multipliers < 1e3).
  Dec operator*(Dec o) const {
    bool neg = (raw < 0) != (o.raw < 0);
    uint64_t a = raw < 0 ? -raw : raw, b = o.raw < 0 ? -o.raw : o.raw;
    uint64_t ah = a / kScale, al = a % kScale, bh = b / kScale, bl = b % kScale;
    uint64_t r = ah * bh * kScale + ah * bl + al * bh + al * bl / kScale;
    return fromRaw(neg ? -(int64_t)r : (int64_t)r);
  }
  Dec operator/(Dec o) const {
    bool neg = (raw < 0) != (o.raw < 0);
    uint64_t a = raw < 0 ? -raw : raw, b = o.raw < 0 ? -o.raw : o.raw;
    uint64_t q = a / b, rem = a % b;
    // rem < b; b is a small divisor in practice (e.g. 100), so rem * kScale fits.
    uint64_t r = q * kScale + rem * kScale / b;
    return fromRaw(neg ? -(int64_t)r : (int64_t)r);
  }
  Dec& operator+=(Dec o) { raw += o.raw; return *this; }
  Dec& operator-=(Dec o) { raw -= o.raw; return *this; }
  Dec& operator*=(Dec o) { *this = *this * o; return *this; }

  constexpr bool operator==(Dec o) const { return raw == o.raw; }
  constexpr bool operator!=(Dec o) const { return raw != o.raw; }
  constexpr bool operator<(Dec o) const { return raw < o.raw; }
  constexpr bool operator>(Dec o) const { return raw > o.raw; }
  constexpr bool operator<=(Dec o) const { return raw <= o.raw; }
  constexpr bool operator>=(Dec o) const { return raw >= o.raw; }
};

inline Dec dmax(Dec a, Dec b) { return a < b ? b : a; }
inline Dec dmin(Dec a, Dec b) { return a < b ? a : b; }

}  // namespace sts
