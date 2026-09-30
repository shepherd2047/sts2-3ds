// Audio engine, platform independent part: index.txt, event -> file resolution, crossfades,
// bus volumes. The backends (audio_backend.h) own the voices and the streaming.
#include "audio.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "audio_backend.h"

namespace audio {
namespace {

struct FileEntry {
  std::string path;  // relative to the audio directory
};
using Groups = std::vector<std::vector<std::string>>;  // ';' groups of ',' alternatives

std::string dir;
std::unordered_map<std::string, FileEntry> files;
std::unordered_map<std::string, std::string> byPath;  // "music/<name>.adpcm" -> file id
std::unordered_map<std::string, Groups> events;
bool ready = false;
float volumes[kBusCount] = {1, 1, 1, 1};
uint32_t rngState = 0x9E3779B9u;

// Stream slots: 0/1 music, 2/3 ambience (backend::kStreams). Two per bus for crossfades.
struct Slot {
  std::string id;
  bool open = false;
  float gain = 0, target = 0, rate = 0;  // rate = gain units per second
  float sent = -1;                       // last gain passed to the backend
};
Slot slots[backend::kStreams];
int current[2] = {-1, -1};  // current slot of music / ambience
const std::string kEmpty;

uint32_t rnd() {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return rngState;
}

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t p = 0;
  while (p <= s.size()) {
    size_t e = s.find(sep, p);
    if (e == std::string::npos) e = s.size();
    out.push_back(s.substr(p, e - p));
    p = e + 1;
  }
  return out;
}

bool loadIndex() {
  FILE* f = fopen((dir + "index.txt").c_str(), "rb");
  if (!f) return false;
  std::string text;
  char buf[16384];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
  fclose(f);
  size_t p = 0;
  while (p < text.size()) {
    size_t e = text.find('\n', p);
    if (e == std::string::npos) e = text.size();
    std::string line = text.substr(p, e - p);
    p = e + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.size() < 2 || line[0] == '#') continue;
    std::vector<std::string> col = split(line, '\t');
    if (col[0] == "F" && col.size() >= 4) {
      files[col[1]] = FileEntry{col[3]};
      byPath[col[3]] = col[1];
    } else if (col[0] == "E" && col.size() >= 4) {
      Groups g;
      for (const std::string& grp : split(col[3], ';')) {
        std::vector<std::string> alts;
        for (const std::string& a : split(grp, ','))
          if (!a.empty()) alts.push_back(a);
        if (!alts.empty()) g.push_back(std::move(alts));
      }
      if (!g.empty()) events[col[1]] = std::move(g);
    }
  }
  return !files.empty();
}

const FileEntry* file(const std::string& id) {
  auto it = files.find(id);
  if (it == files.end()) {  // a path under the audio directory ("music/<name>.adpcm")
    auto p = byPath.find(id);
    if (p != byPath.end()) it = files.find(p->second);
  }
  return it == files.end() ? nullptr : &it->second;
}

// Music / ambience: a file id, or an event's first file.
const FileEntry* resolveStream(const std::string& id) {
  if (const FileEntry* f = file(id)) return f;
  auto it = events.find(id);
  return it == events.end() ? nullptr : file(it->second[0][0]);
}

float busGain(int slot) { return volumes[MASTER] * volumes[slot < 2 ? MUSIC : AMBIENCE]; }

bool playStream(int kind, const std::string& id, float fade) {
  if (!ready) return false;
  int base = kind * 2;
  int cur = current[kind];
  if (cur >= 0 && slots[cur].id == id) return true;
  const FileEntry* f = resolveStream(id);
  if (!f) return false;
  int next = cur == base ? base + 1 : base;
  Slot& s = slots[next];
  if (s.open) backend::streamClose(next);
  s = Slot{};
  if (!backend::streamOpen(next, dir + f->path)) return false;
  float rate = fade > 0.01f ? 1.f / fade : 1000.f;
  if (cur >= 0) {
    slots[cur].target = 0;
    slots[cur].rate = rate;
  }
  s.id = id;
  s.open = true;
  s.target = 1;
  s.rate = rate;
  current[kind] = next;
  return true;
}

void stopStream(int kind, float fade) {
  current[kind] = -1;
  for (int i = kind * 2; i < kind * 2 + 2; i++) {
    slots[i].target = 0;
    slots[i].rate = fade > 0.01f ? 1.f / fade : 1000.f;
  }
}

}  // namespace

bool init() {
  if (const char* d = getenv("STS_AUDIO_DIR")) {
    dir = d;
    if (!dir.empty() && dir.back() != '/') dir += '/';
  } else {
#ifdef __3DS__
    dir = "sdmc:/3ds/sts2-3ds/audio/";
#else
    dir = "audio/";
#endif
  }
  ready = loadIndex() && backend::init();
  if (!ready) {
    files.clear();
    byPath.clear();
    events.clear();
    return false;
  }
#ifndef __3DS__
  printf("audio: %zu files, %zu events from %s\n", files.size(), events.size(), dir.c_str());
#endif
  return true;
}

void shutdown() {
  if (!ready) return;
  for (int i = 0; i < backend::kStreams; i++)
    if (slots[i].open) backend::streamClose(i);
  backend::shutdown();
  ready = false;
}

bool available() { return ready; }

void setPaused(bool paused) {
  if (ready) backend::setPaused(paused);
}

void update(double dt) {
  if (!ready) return;
  for (int i = 0; i < backend::kStreams; i++) {
    Slot& s = slots[i];
    if (!s.open) continue;
    float step = s.rate * (float)dt;
    if (s.gain < s.target) s.gain = s.gain + step > s.target ? s.target : s.gain + step;
    else if (s.gain > s.target) s.gain = s.gain - step < s.target ? s.target : s.gain - step;
    if (s.gain <= 0 && s.target <= 0) {
      backend::streamClose(i);
      s = Slot{};
      continue;
    }
    float g = s.gain * busGain(i);
    if (g != s.sent) {
      backend::streamGain(i, g);
      s.sent = g;
    }
  }
  backend::update();
}

bool playSfx(const std::string& eventOrId) {
  if (!ready) return false;
  float gain = volumes[MASTER] * volumes[SFX];
  if (gain <= 0) return false;
  auto it = events.find(eventOrId);
  if (it == events.end()) {
    const FileEntry* f = file(eventOrId);
    return f && backend::playSound(dir + f->path, gain);
  }
  bool any = false;
  for (const std::vector<std::string>& alts : it->second) {
    const FileEntry* f = file(alts[rnd() % alts.size()]);
    if (f && backend::playSound(dir + f->path, gain)) any = true;
  }
  return any;
}

bool playMusic(const std::string& id, float fade) { return playStream(0, id, fade); }
void stopMusic(float fade) { stopStream(0, fade); }
bool playAmbience(const std::string& id, float fade) { return playStream(1, id, fade); }
void stopAmbience(float fade) { stopStream(1, fade); }
const std::string& currentMusic() { return current[0] >= 0 ? slots[current[0]].id : kEmpty; }

void setVolume(Bus bus, float v) {
  if (bus >= 0 && bus < kBusCount) volumes[bus] = v < 0 ? 0 : v > 1 ? 1 : v;
}
float volume(Bus bus) { return bus >= 0 && bus < kBusCount ? volumes[bus] : 0.f; }

}  // namespace audio
