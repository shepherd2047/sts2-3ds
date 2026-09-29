// Preview audio backend: decodes the S2AD DSP-ADPCM files on the fly and mixes them with
// linear resampling in the SDL audio callback. Files are read whole (SFX cached, music per
// stream); the preview has the memory. STS_AUDIO_WAV=build/x.wav records the first
// STS_AUDIO_WAV_SECONDS (default 10) seconds of the mix and writes it at shutdown.
#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../audio/audio_backend.h"

namespace audio {
namespace backend {
namespace {

struct Sound {
  S2AD h;
  std::vector<uint8_t> bytes;  // the whole file
};

std::shared_ptr<Sound> loadSound(const std::string& path) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return nullptr;
  auto s = std::make_shared<Sound>();
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n > 0) {
    s->bytes.resize((size_t)n);
    if (fread(s->bytes.data(), 1, (size_t)n, f) != (size_t)n) s->bytes.clear();
  }
  fclose(f);
  if (!parseS2AD(s->bytes.data(), s->bytes.size(), s->h)) return nullptr;
  uint64_t need = s->h.dataOffset + (uint64_t)s->h.channelBytes * s->h.channels;
  if (need > s->bytes.size()) return nullptr;
  return s;
}

// One playing sound: sequential decode per channel + linear resampling to the device rate.
struct Voice {
  std::shared_ptr<Sound> snd;
  bool loop = false;
  float gain = 0;
  uint32_t pos = 0;  // next sample to decode (per channel)
  int16_t yn1[2] = {}, yn2[2] = {};
  int16_t frame[2][14];
  float s0[2] = {}, s1[2] = {};  // interpolation pair
  double frac = 0, step = 1;
  bool done = false;

  void start(std::shared_ptr<Sound> s, bool lp, float g, int devRate) {
    *this = Voice{};
    snd = std::move(s);
    loop = lp;
    gain = g;
    step = (double)snd->h.rate / devRate;
    rewind();
    for (int c = 0; c < 2; c++) s0[c] = s1[c] = 0;
    next();
    next();
    frac = 0;
  }
  void rewind() {
    pos = 0;
    for (int c = 0; c < snd->h.channels; c++) {
      yn1[c] = snd->h.ch[c].yn1;
      yn2[c] = snd->h.ch[c].yn2;
    }
  }
  // Shifts the next decoded sample into s1.
  void next() {
    const S2AD& h = snd->h;
    if (pos >= h.samples) {
      if (!loop) {
        for (int c = 0; c < 2; c++) s0[c] = s1[c], s1[c] = 0;
        done = true;
        return;
      }
      rewind();
    }
    uint32_t fi = pos / 14, k = pos % 14;
    if (k == 0) {
      // Byte offset of frame fi in channel c: block = byte / blockBytes.
      uint32_t byte = fi * 8, blk = byte / h.blockBytes;
      for (int c = 0; c < h.channels; c++) {
        uint32_t off, len;
        s2adBlock(h, blk, c, off, len);
        int count = h.samples - pos < 14 ? (int)(h.samples - pos) : 14;
        decodeFrame(snd->bytes.data() + off + (byte - blk * h.blockBytes), h.ch[c].coefs, yn1[c], yn2[c], frame[c], count);
      }
    }
    for (int c = 0; c < 2; c++) {
      s0[c] = s1[c];
      s1[c] = frame[c < h.channels ? c : 0][k] * (1.f / 32768.f);
    }
    pos++;
  }
  void mix(float* out, int frames) {
    bool stereo = snd->h.channels == 2;
    for (int i = 0; i < frames && !done; i++) {
      float t = (float)frac;
      float l = s0[0] + (s1[0] - s0[0]) * t;
      float r = stereo ? s0[1] + (s1[1] - s0[1]) * t : l;
      out[2 * i] += l * gain;
      out[2 * i + 1] += r * gain;
      frac += step;
      while (frac >= 1 && !done) {
        frac -= 1;
        next();
      }
    }
  }
};

constexpr int kVoices = 16;
SDL_AudioDeviceID dev = 0;
int devRate = 48000;
Voice voices[kVoices];
uint32_t voiceStart[kVoices];  // play order for stealing
uint32_t playCounter = 0;
Voice streams[kStreams];
bool streamOn[kStreams] = {};
std::unordered_map<std::string, std::shared_ptr<Sound>> cache;  // null = missing, not retried

std::vector<int16_t> capture;
size_t captureMax = 0;
const char* capturePath = nullptr;
uint64_t mixedFrames = 0;
float peak = 0;

void callback(void*, Uint8* stream, int len) {
  float* out = (float*)stream;
  int frames = len / (int)(2 * sizeof(float));
  memset(out, 0, len);
  for (Voice& v : voices)
    if (v.snd && !v.done) v.mix(out, frames);
  for (int i = 0; i < kStreams; i++)
    if (streamOn[i] && streams[i].gain > 0) streams[i].mix(out, frames);
  for (int i = 0; i < frames * 2; i++) {
    float s = out[i] < -1 ? -1 : out[i] > 1 ? 1 : out[i];
    out[i] = s;
    if (s > peak) peak = s;
    if (-s > peak) peak = -s;
    if (capture.size() < captureMax) capture.push_back((int16_t)(s * 32767));
  }
  mixedFrames += frames;
}

void writeWav() {
  FILE* f = fopen(capturePath, "wb");
  if (!f) return;
  uint32_t data = (uint32_t)(capture.size() * 2), rate = devRate, byteRate = devRate * 4;
  auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f); u32(36 + data); fwrite("WAVEfmt ", 1, 8, f);
  u32(16); u16(1); u16(2); u32(rate); u32(byteRate); u16(4); u16(16);
  fwrite("data", 1, 4, f); u32(data);
  fwrite(capture.data(), 2, capture.size(), f);
  fclose(f);
}

}  // namespace

bool init() {
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
  SDL_AudioSpec want{}, have{};
  want.freq = 48000;
  want.format = AUDIO_F32SYS;
  want.channels = 2;
  want.samples = 1024;
  want.callback = callback;
  dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
  if (!dev) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return false;
  }
  devRate = have.freq;
  if ((capturePath = getenv("STS_AUDIO_WAV"))) {
    const char* s = getenv("STS_AUDIO_WAV_SECONDS");
    captureMax = (size_t)((s ? atof(s) : 10.0) * devRate) * 2;
    capture.reserve(captureMax);
  }
  SDL_PauseAudioDevice(dev, 0);
  return true;
}

void shutdown() {
  if (!dev) return;
  SDL_CloseAudioDevice(dev);
  dev = 0;
  printf("audio: mixed %llu frames at %d Hz, peak %.3f\n", (unsigned long long)mixedFrames, devRate, peak);
  if (capturePath) {
    writeWav();
    printf("audio: wrote %s (%.1f s)\n", capturePath, capture.size() / 2.0 / devRate);
  }
  for (Voice& v : voices) v = Voice{};
  for (Voice& v : streams) v = Voice{};
  cache.clear();
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void update() {}

bool playSound(const std::string& path, float gain) {
  auto it = cache.find(path);
  if (it == cache.end()) it = cache.emplace(path, loadSound(path)).first;
  if (!it->second) return false;
  int best = 0;
  SDL_LockAudioDevice(dev);
  for (int i = 0; i < kVoices; i++) {
    if (!voices[i].snd || voices[i].done) { best = i; break; }
    if (voiceStart[i] < voiceStart[best]) best = i;  // steal the oldest
  }
  voices[best].start(it->second, false, gain, devRate);
  voiceStart[best] = ++playCounter;
  SDL_UnlockAudioDevice(dev);
  return true;
}

bool streamOpen(int slot, const std::string& path) {
  std::shared_ptr<Sound> s = loadSound(path);
  if (!s) return false;
  SDL_LockAudioDevice(dev);
  streams[slot].start(std::move(s), true, 0, devRate);
  streamOn[slot] = true;
  SDL_UnlockAudioDevice(dev);
  return true;
}

void streamClose(int slot) {
  std::shared_ptr<Sound> old;
  SDL_LockAudioDevice(dev);
  streamOn[slot] = false;
  old = std::move(streams[slot].snd);  // freed outside the lock
  streams[slot] = Voice{};
  SDL_UnlockAudioDevice(dev);
}

void streamGain(int slot, float gain) {
  SDL_LockAudioDevice(dev);
  streams[slot].gain = gain;
  SDL_UnlockAudioDevice(dev);
}

}  // namespace backend
}  // namespace audio
