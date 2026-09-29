// 3DS audio backend on ndsp: the DSP plays DSP-ADPCM natively, so nothing is decoded on the CPU.
//  - SFX: whole files in linear memory (cached, LRU-evicted above kCacheMax), played on a pool
//    of kSfxVoices voices (the oldest is stolen); each voice owns two ndsp channels (stereo).
//  - Music / ambience: kStreams streams on channels 16..23. A background thread reads the SD
//    file in chunks (16 KB per channel, ~1.3 s at 22 kHz) into a ring of kBufs wave buffers and
//    requeues them as the DSP finishes; at the end it seeks back to the start (whole-file loop)
//    and resets the ADPCM history. The main thread never reads more than a file header.
// ndspInit needs sdmc:/3ds/dspfirm.cdc on real hardware; without it audio stays off.
#include <3ds.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

#include "../audio/audio_backend.h"

namespace audio {
namespace backend {
namespace {

constexpr int kSfxVoices = 8;         // channels 0..15
constexpr int kStreamChannel = 16;    // channels 16..23
constexpr int kBufs = 4;              // wave buffers per stream channel
constexpr uint32_t kMonoChunk = 16384;
constexpr uint32_t kMaxChunk = 65536;
constexpr size_t kCacheMax = 2u << 20;  // linear memory for cached SFX

bool on = false;

void setupChannel(int ch, const S2AD& h, int c, float gain) {
  ndspChnReset(ch);
  ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
  ndspChnSetRate(ch, (float)h.rate);
  ndspChnSetFormat(ch, NDSP_FORMAT_MONO_ADPCM);
  ndspChnSetAdpcmCoefs(ch, (u16*)h.ch[c].coefs);
  float mix[12] = {};
  mix[0] = (h.channels == 1 || c == 0) ? gain : 0;
  mix[1] = (h.channels == 1 || c == 1) ? gain : 0;
  ndspChnSetMix(ch, mix);
}

void setGain(int ch, int channels, int c, float gain) {
  float mix[12] = {};
  mix[0] = (channels == 1 || c == 0) ? gain : 0;
  mix[1] = (channels == 1 || c == 1) ? gain : 0;
  ndspChnSetMix(ch, mix);
}

bool readHeader(FILE* f, S2AD& h) {
  uint8_t hdr[kS2ADMaxHeader];
  size_t n = fread(hdr, 1, sizeof hdr, f);
  return parseS2AD(hdr, n, h) && h.dataOffset >= 0x30u + 0x30u * h.channels;
}

// ---- SFX ------------------------------------------------------------------------------------
struct Sound {
  S2AD h;
  u8* data[2] = {};
  size_t bytes = 0;
  uint32_t lastUse = 0;
};

struct SfxVoice {
  Sound* snd = nullptr;
  ndspWaveBuf wb[2];
  ndspAdpcmData ad[2];
  uint32_t order = 0;
  bool playing() const { return snd && wb[0].status != NDSP_WBUF_DONE && wb[0].status != NDSP_WBUF_FREE; }
};

std::unordered_map<std::string, Sound*> cache;  // nullptr = missing file, never retried
size_t cacheBytes = 0;
uint32_t useClock = 0;
SfxVoice voices[kSfxVoices];

void freeSound(Sound* s) {
  for (SfxVoice& v : voices)
    if (v.snd == s) {
      for (int c = 0; c < s->h.channels; c++) ndspChnWaveBufClear((int)(&v - voices) * 2 + c);
      v.snd = nullptr;
    }
  cacheBytes -= s->bytes;
  for (u8* d : s->data)
    if (d) linearFree(d);
  delete s;
}

void evict(size_t need) {
  while (cacheBytes + need > kCacheMax) {
    auto victim = cache.end();
    for (auto it = cache.begin(); it != cache.end(); ++it) {
      if (!it->second) continue;
      bool busy = false;
      for (const SfxVoice& v : voices) busy |= v.snd == it->second && v.playing();
      if (!busy && (victim == cache.end() || it->second->lastUse < victim->second->lastUse)) victim = it;
    }
    if (victim == cache.end()) return;
    freeSound(victim->second);
    cache.erase(victim);
  }
}

Sound* loadSound(const std::string& path) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return nullptr;
  Sound* s = new Sound;
  bool ok = readHeader(f, s->h) && s->h.channelBytes <= (4u << 20);
  if (ok) {
    evict(s->h.channelBytes * s->h.channels);
    for (int c = 0; c < s->h.channels && ok; c++) ok = (s->data[c] = (u8*)linearAlloc(s->h.channelBytes)) != nullptr;
    // Deinterleave the blocks into one contiguous buffer per channel.
    for (uint32_t blk = 0; ok && blk * s->h.blockBytes < s->h.channelBytes; blk++)
      for (int c = 0; c < s->h.channels && ok; c++) {
        uint32_t off, len;
        s2adBlock(s->h, blk, c, off, len);
        ok = fseek(f, off, SEEK_SET) == 0 && fread(s->data[c] + blk * s->h.blockBytes, 1, len, f) == len;
      }
  }
  fclose(f);
  if (!ok) {
    for (u8* d : s->data)
      if (d) linearFree(d);
    delete s;
    return nullptr;
  }
  s->bytes = (size_t)s->h.channelBytes * s->h.channels;
  for (int c = 0; c < s->h.channels; c++) DSP_FlushDataCache(s->data[c], s->h.channelBytes);
  cacheBytes += s->bytes;
  return s;
}

// ---- Streams ----------------------------------------------------------------------------------
struct Stream {
  bool open = false;
  FILE* f = nullptr;
  S2AD h;
  uint32_t chunk = 0, nextChunk = 0, chunks = 0;
  int nextBuf = 0;
  u8* buf[kBufs][2] = {};
  ndspWaveBuf wb[kBufs][2];
  ndspAdpcmData ad[2];
};

Stream streams[kStreams];
LightLock lock;
Thread thread = nullptr;
volatile bool threadRun = false;

int streamCh(int slot, int c) { return kStreamChannel + slot * 2 + c; }

// Reads chunk `k` of every channel into ring buffer `b`. Mono chunks are kMonoChunk bytes of the
// single stream; stereo chunks are the file's interleave blocks (both channels are adjacent).
bool readChunk(Stream& s, uint32_t k, int b, uint32_t& samples) {
  const S2AD& h = s.h;
  uint32_t off, len;
  if (h.channels == 1) {
    off = h.dataOffset + k * s.chunk;
    len = h.channelBytes - k * s.chunk < s.chunk ? h.channelBytes - k * s.chunk : s.chunk;
  } else {
    s2adBlock(h, k, 0, off, len);
  }
  if (fseek(s.f, off, SEEK_SET) != 0) return false;
  for (int c = 0; c < h.channels; c++)
    if (fread(s.buf[b][c], 1, len, s.f) != len) return false;
  uint32_t first = k * (s.chunk / 8) * 14;
  samples = len / 8 * 14;
  if (first + samples > h.samples) samples = h.samples - first;
  for (int c = 0; c < h.channels; c++) DSP_FlushDataCache(s.buf[b][c], len);
  return true;
}

// Queues every finished buffer with the next chunk, in ring order. Called with the lock held.
void pump(int slot) {
  Stream& s = streams[slot];
  if (!s.open || !s.f) return;
  for (int n = 0; n < kBufs; n++) {
    int b = s.nextBuf;
    u8 st = s.wb[b][0].status;
    if (st != NDSP_WBUF_FREE && st != NDSP_WBUF_DONE) return;
    uint32_t k = s.nextChunk, samples = 0;
    if (!readChunk(s, k, b, samples) || samples == 0) {
      fclose(s.f);  // unreadable: the stream falls silent until closed
      s.f = nullptr;
      return;
    }
    for (int c = 0; c < s.h.channels; c++) {
      ndspWaveBuf& w = s.wb[b][c];
      memset(&w, 0, sizeof w);
      w.data_adpcm = s.buf[b][c];
      w.nsamples = samples;
      // Chunk 0 (start and every loop) resets the decoder history; others continue it.
      w.adpcm_data = k == 0 ? &s.ad[c] : nullptr;
      ndspChnWaveBufAdd(streamCh(slot, c), &w);
    }
    s.nextChunk = k + 1 >= s.chunks ? 0 : k + 1;
    s.nextBuf = (b + 1) % kBufs;
  }
}

void closeLocked(int slot) {
  Stream& s = streams[slot];
  if (!s.open) return;
  for (int c = 0; c < s.h.channels; c++) {
    ndspChnWaveBufClear(streamCh(slot, c));
    ndspChnReset(streamCh(slot, c));
  }
  if (s.f) fclose(s.f);
  for (auto& bb : s.buf)
    for (u8* d : bb)
      if (d) linearFree(d);
  s = Stream{};
}

void threadMain(void*) {
  while (threadRun) {
    LightLock_Lock(&lock);
    for (int i = 0; i < kStreams; i++) pump(i);
    LightLock_Unlock(&lock);
    svcSleepThread(20 * 1000 * 1000ULL);  // 20 ms; each stream holds >= 2.5 s of audio
  }
}

}  // namespace

bool init() {
  if (R_FAILED(ndspInit())) return false;
  on = true;
  ndspSetOutputMode(NDSP_OUTPUT_STEREO);
  LightLock_Init(&lock);
  s32 prio = 0x30;
  svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
  threadRun = true;
  thread = threadCreate(threadMain, nullptr, 16 * 1024, prio > 0x18 ? prio - 1 : 0x18, -2, false);
  if (!thread) {
    threadRun = false;
    ndspExit();
    on = false;
    return false;
  }
  return true;
}

void shutdown() {
  if (!on) return;
  threadRun = false;
  threadJoin(thread, U64_MAX);
  threadFree(thread);
  thread = nullptr;
  for (int i = 0; i < kStreams; i++) closeLocked(i);
  for (int ch = 0; ch < kSfxVoices * 2; ch++) ndspChnWaveBufClear(ch);
  for (SfxVoice& v : voices) v.snd = nullptr;
  for (auto& kv : cache)
    if (kv.second) freeSound(kv.second);
  cache.clear();
  ndspExit();
  on = false;
}

void update() {}

bool playSound(const std::string& path, float gain) {
  auto it = cache.find(path);
  if (it == cache.end()) it = cache.emplace(path, loadSound(path)).first;
  Sound* s = it->second;
  if (!s) return false;
  s->lastUse = ++useClock;
  int best = 0;
  for (int i = 0; i < kSfxVoices; i++) {
    if (!voices[i].playing()) { best = i; break; }
    if (voices[i].order < voices[best].order) best = i;  // steal the oldest
  }
  SfxVoice& v = voices[best];
  for (int c = 0; c < 2; c++) ndspChnWaveBufClear(best * 2 + c);
  v.snd = s;
  v.order = useClock;
  for (int c = 0; c < s->h.channels; c++) {
    int ch = best * 2 + c;
    setupChannel(ch, s->h, c, gain);
    v.ad[c] = ndspAdpcmData{s->h.ch[c].ps, s->h.ch[c].yn1, s->h.ch[c].yn2};
    ndspWaveBuf& w = v.wb[c];
    memset(&w, 0, sizeof w);
    w.data_adpcm = s->data[c];
    w.nsamples = s->h.samples;
    w.adpcm_data = &v.ad[c];
    ndspChnWaveBufAdd(ch, &w);
  }
  return true;
}

bool streamOpen(int slot, const std::string& path) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  S2AD h;
  uint32_t chunk = 0;
  bool ok = readHeader(f, h);
  if (ok) {
    chunk = h.channels == 1 ? kMonoChunk : h.blockBytes;
    ok = chunk <= kMaxChunk;
  }
  if (!ok) {
    fclose(f);
    return false;
  }
  LightLock_Lock(&lock);
  closeLocked(slot);
  Stream& s = streams[slot];
  for (int b = 0; b < kBufs && ok; b++)
    for (int c = 0; c < h.channels && ok; c++) ok = (s.buf[b][c] = (u8*)linearAlloc(chunk)) != nullptr;
  if (!ok) {
    s.open = true;
    s.h = h;
    closeLocked(slot);  // frees what was allocated
    LightLock_Unlock(&lock);
    fclose(f);
    return false;
  }
  s.open = true;
  s.f = f;
  s.h = h;
  s.chunk = chunk;
  s.chunks = (h.channelBytes + chunk - 1) / chunk;
  for (int c = 0; c < h.channels; c++) {
    s.ad[c] = ndspAdpcmData{h.ch[c].ps, h.ch[c].yn1, h.ch[c].yn2};
    setupChannel(streamCh(slot, c), h, c, 0.f);
    for (int b = 0; b < kBufs; b++) s.wb[b][c].status = NDSP_WBUF_FREE;
  }
  LightLock_Unlock(&lock);  // the thread fills the buffers within ~20 ms
  return true;
}

void streamClose(int slot) {
  LightLock_Lock(&lock);
  closeLocked(slot);
  LightLock_Unlock(&lock);
}

void streamGain(int slot, float gain) {
  // Only the main thread opens / closes streams, so the channel count is stable here.
  const Stream& s = streams[slot];
  if (!s.open) return;
  for (int c = 0; c < s.h.channels; c++) setGain(streamCh(slot, c), s.h.channels, c, gain);
}

}  // namespace backend
}  // namespace audio
