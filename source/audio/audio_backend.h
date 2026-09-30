// Internal: what audio.cpp needs from a platform backend, plus the S2AD header and the
// DSP-ADPCM decoder shared by the backends (same math as tools/audio_dsp.py decode()).
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace audio {

// ---- S2AD file header (little-endian; see tools/audio_dsp.py) -------------------------------
struct S2ADChannel {
  int16_t coefs[16];
  uint16_t ps;
  int16_t yn1, yn2;
};
struct S2AD {
  int channels = 0;
  uint32_t rate = 0, samples = 0, dataOffset = 0, channelBytes = 0, blockBytes = 0;
  S2ADChannel ch[2];
};
constexpr int kS2ADMaxHeader = 0x30 + 0x30 * 2 + 32;

inline uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
inline uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

// Parses the header from the first `n` bytes of a file; false if it isn't a usable S2AD file.
inline bool parseS2AD(const uint8_t* b, size_t n, S2AD& h) {
  if (n < 0x30 || b[0] != 'S' || b[1] != '2' || b[2] != 'A' || b[3] != 'D' || b[4] != 1) return false;
  h.channels = b[5];
  if (h.channels < 1 || h.channels > 2 || n < 0x30 + 0x30u * h.channels) return false;
  h.rate = rd32(b + 0x08);
  h.samples = rd32(b + 0x0C);
  h.dataOffset = rd32(b + 0x18);
  h.channelBytes = rd32(b + 0x1C);
  h.blockBytes = rd32(b + 0x20);
  if (!h.rate || !h.samples || h.channelBytes < (h.samples + 13) / 14 * 8 || !h.blockBytes || h.blockBytes % 8)
    return false;
  for (int c = 0; c < h.channels; c++) {
    const uint8_t* p = b + 0x30 + 0x30 * c;
    for (int i = 0; i < 16; i++) h.ch[c].coefs[i] = (int16_t)rd16(p + 2 * i);
    h.ch[c].ps = rd16(p + 0x20);
    h.ch[c].yn1 = (int16_t)rd16(p + 0x22);
    h.ch[c].yn2 = (int16_t)rd16(p + 0x24);
  }
  return true;
}

// File offset and length of channel `c`'s bytes in interleave block `blk` (a mono file is one block).
inline void s2adBlock(const S2AD& h, uint32_t blk, int c, uint32_t& off, uint32_t& len) {
  uint32_t start = blk * h.blockBytes;
  len = h.channelBytes - start < h.blockBytes ? h.channelBytes - start : h.blockBytes;
  off = h.dataOffset + blk * h.blockBytes * h.channels + c * len;
}

// Decodes one 8-byte frame into `count` (<= 14) samples, updating the history.
inline void decodeFrame(const uint8_t* f, const int16_t* coefs, int16_t& yn1, int16_t& yn2, int16_t* out, int count) {
  int scale = 1 << (f[0] & 0xF);
  int pred = (f[0] >> 4) & 7;
  int64_t c1 = coefs[2 * pred], c2 = coefs[2 * pred + 1];
  for (int s = 0; s < count; s++) {
    int nib = (s & 1) ? (f[1 + s / 2] & 0xF) : (f[1 + s / 2] >> 4);
    if (nib >= 8) nib -= 16;
    int64_t v = ((int64_t)(nib * scale) * 2048 + 1024 + c1 * yn1 + c2 * yn2) >> 11;
    v = v < -32768 ? -32768 : v > 32767 ? 32767 : v;
    out[s] = (int16_t)v;
    yn2 = yn1;
    yn1 = (int16_t)v;
  }
}

namespace backend {
constexpr int kStreams = 4;  // music A/B and ambience A/B (crossfades)
bool init();
void shutdown();
void update();  // main thread, once per frame
// Plays a whole file once (cached by the backend); false if it can't be loaded.
bool playSound(const std::string& path, float gain);
// Opens `path` on stream slot `slot` and loops it; starts silent (gain 0).
bool streamOpen(int slot, const std::string& path);
void streamClose(int slot);
void streamGain(int slot, float gain);
void setPaused(bool paused);  // Y5: see audio::setPaused
}  // namespace backend

}  // namespace audio
