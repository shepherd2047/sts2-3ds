// Port of MegaCrit.Sts2.Core.Random.MegaRandom (xoshiro256** seeded by
// splitmix64) and the Rng wrapper.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sts {

class MegaRandom {
 public:
  explicit MegaRandom(uint64_t seed = 0) { reinit(seed); }

  static uint64_t splitmix64(uint64_t& x) {
    uint64_t n = (x += 11400714819323198485ull);
    n = (n ^ (n >> 30)) * 13787848793156543929ull;
    n = (n ^ (n >> 27)) * 10723151780598845931ull;
    return n ^ (n >> 31);
  }
  void reinit(uint64_t seed) {
    s0_ = splitmix64(seed);
    s1_ = splitmix64(seed);
    s2_ = splitmix64(seed);
    s3_ = splitmix64(seed);
  }
  uint64_t nextULong() {
    uint64_t s = s0_, s2 = s1_, s3 = s2_, s4 = s3_;
    uint64_t result = rotl(s2 * 5, 7) * 9;
    uint64_t t = s2 << 17;
    s3 ^= s;
    s4 ^= s2;
    s2 ^= s3;
    s ^= s4;
    s3 ^= t;
    s4 = rotl(s4, 45);
    s0_ = s; s1_ = s2; s2_ = s3; s3_ = s4;
    return result;
  }
  double nextDouble() { return (double)(nextULong() >> 11) * 1.1102230246251565E-16; }
  int next(int maxValue) { return (int)(nextDouble() * (double)maxValue); }
  int next(int minValue, int maxValue) {
    int64_t range = (int64_t)maxValue - minValue;
    return (int)((int64_t)(nextDouble() * (double)range) + minValue);
  }

 private:
  static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
  uint64_t s0_, s1_, s2_, s3_;
};

// StringHelper.GetDeterministicHashCode: XxHash64 of the UTF-8 bytes, seed 0.
inline uint64_t deterministicHash(const std::string& s) {
  constexpr uint64_t P1 = 11400714785074694791ull, P2 = 14029467366897019727ull,
                     P3 = 1609587929392839161ull, P4 = 9650029242287828579ull,
                     P5 = 2870177450012600261ull;
  auto rotl = [](uint64_t x, int r) { return (x << r) | (x >> (64 - r)); };
  auto rd64 = [](const unsigned char* p) { uint64_t v = 0; for (int i = 7; i >= 0; --i) v = (v << 8) | p[i]; return v; };
  auto rd32 = [](const unsigned char* p) { uint32_t v = 0; for (int i = 3; i >= 0; --i) v = (v << 8) | p[i]; return (uint64_t)v; };
  auto round = [&](uint64_t acc, uint64_t in) { acc += in * P2; acc = rotl(acc, 31); return acc * P1; };
  auto merge = [&](uint64_t acc, uint64_t v) { acc ^= round(0, v); return acc * P1 + P4; };
  const unsigned char* p = (const unsigned char*)s.data();
  const unsigned char* end = p + s.size();
  uint64_t h;
  if (s.size() >= 32) {
    uint64_t v1 = P1 + P2, v2 = P2, v3 = 0, v4 = 0 - P1;
    for (; p + 32 <= end; p += 32) {
      v1 = round(v1, rd64(p)); v2 = round(v2, rd64(p + 8));
      v3 = round(v3, rd64(p + 16)); v4 = round(v4, rd64(p + 24));
    }
    h = rotl(v1, 1) + rotl(v2, 7) + rotl(v3, 12) + rotl(v4, 18);
    h = merge(h, v1); h = merge(h, v2); h = merge(h, v3); h = merge(h, v4);
  } else {
    h = P5;
  }
  h += (uint64_t)s.size();
  for (; p + 8 <= end; p += 8) { h ^= round(0, rd64(p)); h = rotl(h, 27) * P1 + P4; }
  if (p + 4 <= end) { h ^= rd32(p) * P1; h = rotl(h, 23) * P2 + P3; p += 4; }
  for (; p < end; ++p) { h ^= (*p) * P5; h = rotl(h, 11) * P1; }
  h ^= h >> 33; h *= P2; h ^= h >> 29; h *= P3; h ^= h >> 32;
  return h;
}

class Rng {
 public:
  explicit Rng(uint64_t seed = 0) : r_(seed) {}
  Rng(uint64_t seed, const std::string& name) : r_(seed + deterministicHash(name)) {}

  bool nextBool() { ++counter; return r_.next(2) == 0; }
  int nextInt(int maxExclusive) { ++counter; return r_.next(maxExclusive); }
  int nextInt(int minInclusive, int maxExclusive) { ++counter; return r_.next(minInclusive, maxExclusive); }
  float nextFloat(float max = 1.f) { ++counter; return (float)(r_.nextDouble() * (double)max); }
  float nextFloat(float min, float max) { ++counter; return (float)(r_.nextDouble() * (double)(max - min) + (double)min); }
  double nextDouble() { ++counter; return r_.nextDouble(); }

  template <class T> T nextItem(const std::vector<T>& v) {
    if (v.empty()) return T{};
    return v[nextInt((int)v.size())];
  }
  // List.StableShuffle: Fisher-Yates from the back.
  template <class T> void shuffle(std::vector<T>& v) {
    for (int i = (int)v.size() - 1; i > 0; --i) {
      int j = nextInt(i + 1);
      std::swap(v[i], v[j]);
    }
  }

  int counter = 0;

 private:
  MegaRandom r_;
};

}  // namespace sts
