#include "spine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace spine {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegRad = kPi / 180.f;
constexpr float kRadDeg = 180.f / kPi;
constexpr int kBezierSize = 18;
constexpr int kLinear = 0, kStepped = 1, kBezier = 2;

inline float cosDeg(float d) { return std::cos(d * kDegRad); }
inline float sinDeg(float d) { return std::sin(d * kDegRad); }
inline float atan2Deg(float y, float x) { return std::atan2(y, x) * kRadDeg; }

// ---------------------------------------------------------------- binary reader

class Reader {
 public:
  explicit Reader(const std::string& d) : d_((const uint8_t*)d.data()), n_(d.size()) {}
  bool ok() const { return !bad_; }
  uint8_t byte() {
    if (p_ >= n_) { bad_ = true; return 0; }
    return d_[p_++];
  }
  bool boolean() { return byte() != 0; }
  int32_t int32() {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v = (v << 8) | byte();
    return (int32_t)v;
  }
  float f32() {
    uint32_t v = (uint32_t)int32();
    float f;
    memcpy(&f, &v, 4);
    return f;
  }
  int varint(bool optimizePositive = true) {
    uint32_t result = 0;
    int shift = 0;
    for (;;) {
      uint8_t b = byte();
      result |= (uint32_t)(b & 0x7F) << shift;
      if (!(b & 0x80) || shift >= 28) break;
      shift += 7;
    }
    if (!optimizePositive) result = (result >> 1) ^ (uint32_t)-(int32_t)(result & 1);
    return (int)result;
  }
  bool string(std::string& out) {
    int n = varint();
    if (n == 0) { out.clear(); return false; }
    out.assign((const char*)d_ + p_, std::min<size_t>(n - 1, n_ - p_));
    p_ += n - 1;
    return true;
  }
  std::string str() { std::string s; string(s); return s; }
  std::string ref() {
    int i = varint();
    return i == 0 || i - 1 >= (int)strings.size() ? std::string() : strings[i - 1];
  }
  void skip(size_t n) { p_ += n; }
  std::vector<std::string> strings;

 private:
  const uint8_t* d_;
  size_t n_, p_ = 0;
  bool bad_ = false;
};

void rgba8888(int32_t v, float out[4]) {
  uint32_t c = (uint32_t)v;
  out[0] = ((c >> 24) & 255) / 255.f;
  out[1] = ((c >> 16) & 255) / 255.f;
  out[2] = ((c >> 8) & 255) / 255.f;
  out[3] = (c & 255) / 255.f;
}

// ---------------------------------------------------------------- curves

void setBezier(Timeline& tl, int bezier, int frame, int value, float time1, float value1, float cx1, float cy1,
               float cx2, float cy2, float time2, float value2) {
  int i = tl.frameCount() + bezier * kBezierSize;
  if (value == 0) tl.curves[frame] = (float)(kBezier + i);
  float tmpx = (time1 - cx1 * 2 + cx2) * 0.03f, tmpy = (value1 - cy1 * 2 + cy2) * 0.03f;
  float dddx = ((cx1 - cx2) * 3 - time1 + time2) * 0.006f, dddy = ((cy1 - cy2) * 3 - value1 + value2) * 0.006f;
  float ddx = tmpx * 2 + dddx, ddy = tmpy * 2 + dddy;
  float dx = (cx1 - time1) * 0.3f + tmpx + dddx * 0.16666667f, dy = (cy1 - value1) * 0.3f + tmpy + dddy * 0.16666667f;
  float x = time1 + dx, y = value1 + dy;
  for (int n = i + kBezierSize; i < n; i += 2) {
    tl.curves[i] = x;
    tl.curves[i + 1] = y;
    dx += ddx;
    dy += ddy;
    ddx += dddx;
    ddy += dddy;
    x += dx;
    y += dy;
  }
}

void readBezier(Reader& r, Timeline& tl, int bezier, int frame, int value, float t1, float t2, float v1, float v2) {
  float cx1 = r.f32(), cy1 = r.f32(), cx2 = r.f32(), cy2 = r.f32();
  setBezier(tl, bezier, frame, value, t1, v1, cx1, cy1, cx2, cy2, t2, v2);
}

// Last frame index whose time <= t, or -1 before the first key.
int searchFrame(const Timeline& tl, float t) {
  int n = tl.frameCount();
  if (n == 0 || t < tl.frames[0]) return -1;
  for (int i = 1; i < n; ++i)
    if (tl.frames[i * tl.entries] > t) return i - 1;
  return n - 1;
}

float bezierValue(const Timeline& tl, float time, int frame, int valueOffset, int i) {
  const auto& c = tl.curves;
  const auto& f = tl.frames;
  int fi = frame * tl.entries;
  if (c[i] > time) {
    float x = f[fi], y = f[fi + valueOffset];
    return y + (time - x) / (c[i] - x) * (c[i + 1] - y);
  }
  int n = i + kBezierSize;
  for (i += 2; i < n; i += 2) {
    if (c[i] >= time) {
      float x = c[i - 2], y = c[i - 1];
      return y + (time - x) / (c[i] - x) * (c[i + 1] - y);
    }
  }
  fi += tl.entries;
  float x = c[n - 2], y = c[n - 1];
  return y + (time - x) / (f[fi] - x) * (f[fi + valueOffset] - y);
}

// Value `k` (0-based) of a curve timeline at `time`, given frame index `i`.
float curveValue(const Timeline& tl, float time, int i, int k) {
  const auto& f = tl.frames;
  int E = tl.entries, fi = i * E;
  if (i >= tl.frameCount() - 1) return f[fi + 1 + k];
  int type = (int)tl.curves[i];
  if (type == kLinear) {
    float t0 = f[fi], v0 = f[fi + 1 + k];
    return v0 + (time - t0) / (f[fi + E] - t0) * (f[fi + E + 1 + k] - v0);
  }
  if (type == kStepped) return f[fi + 1 + k];
  return bezierValue(tl, time, i, 1 + k, type - kBezier + k * kBezierSize);
}

float curvePercent(const Timeline& tl, float time, int frame) {
  const auto& c = tl.curves;
  const auto& f = tl.frames;
  int i = (int)c[frame];
  if (i == kLinear) {
    float x = f[frame];
    return (time - x) / (f[frame + tl.entries] - x);
  }
  if (i == kStepped) return 0;
  i -= kBezier;
  if (c[i] > time) {
    float x = f[frame];
    return c[i + 1] * (time - x) / (c[i] - x);
  }
  int n = i + kBezierSize;
  for (i += 2; i < n; i += 2) {
    if (c[i] >= time) {
      float x = c[i - 2], y = c[i - 1];
      return y + (time - x) / (c[i] - x) * (c[i + 1] - y);
    }
  }
  float x = c[n - 2], y = c[n - 1];
  return y + (1 - y) * (time - x) / (f[frame + tl.entries] - x);
}

// CurveTimeline1/2/N: first key, then (key, curve) pairs.
void readCurveTimeline(Reader& r, Timeline& tl, int frameCount, int bezierCount, int values) {
  tl.entries = 1 + values;
  tl.frames.assign(frameCount * tl.entries, 0.f);
  tl.curves.assign(frameCount + bezierCount * kBezierSize, 0.f);
  float time = r.f32();
  float v[8];
  for (int k = 0; k < values; ++k) v[k] = r.f32();
  for (int frame = 0, bezier = 0;; ++frame) {
    tl.frames[frame * tl.entries] = time;
    for (int k = 0; k < values; ++k) tl.frames[frame * tl.entries + 1 + k] = v[k];
    if (frame == frameCount - 1) break;
    float time2 = r.f32();
    float v2[8];
    for (int k = 0; k < values; ++k) v2[k] = r.f32();
    uint8_t curve = r.byte();
    if (curve == kStepped) tl.curves[frame] = kStepped;
    else if (curve == kBezier)
      for (int k = 0; k < values; ++k) readBezier(r, tl, bezier++, frame, k, time, time2, v[k], v2[k]);
    time = time2;
    for (int k = 0; k < values; ++k) v[k] = v2[k];
  }
}

void readVertices(Reader& r, Attachment& a, bool weighted) {
  a.vertexCount = r.varint();
  a.weighted = weighted;
  if (!weighted) {
    a.vertices.resize(a.vertexCount * 2);
    for (auto& v : a.vertices) v = r.f32();
    return;
  }
  for (int i = 0; i < a.vertexCount; ++i) {
    int n = r.varint();
    a.bones.push_back(n);
    for (int k = 0; k < n; ++k) {
      a.bones.push_back(r.varint());
      a.vertices.push_back(r.f32());
      a.vertices.push_back(r.f32());
      a.vertices.push_back(r.f32());
    }
  }
}

std::unique_ptr<Attachment> readAttachment(Reader& r, const std::string& key, bool nonessential) {
  auto a = std::make_unique<Attachment>();
  uint8_t flags = r.byte();
  a->name = (flags & 8) ? r.ref() : key;
  a->kind = (AttKind)(flags & 7);
  auto readSequence = [&] {
    int count = r.varint(), start = r.varint(), digits = r.varint(), setup = r.varint();
    (void)count;
    std::string idx = std::to_string(start + setup);
    while ((int)idx.size() < digits) idx = "0" + idx;
    return idx;
  };
  switch (a->kind) {
    case kRegion: {
      a->path = (flags & 16) ? r.ref() : a->name;
      if (flags & 32) rgba8888(r.int32(), a->color);
      if (flags & 64) a->path += readSequence();
      a->rotation = (flags & 128) ? r.f32() : 0.f;
      a->x = r.f32(); a->y = r.f32();
      a->scaleX = r.f32(); a->scaleY = r.f32();
      a->width = r.f32(); a->height = r.f32();
      break;
    }
    case kBoundingBox:
      readVertices(r, *a, flags & 16);
      if (nonessential) r.int32();
      break;
    case kMesh: {
      a->path = (flags & 16) ? r.ref() : a->name;
      if (flags & 32) rgba8888(r.int32(), a->color);
      if (flags & 64) a->path += readSequence();
      int hull = r.varint();
      readVertices(r, *a, flags & 128);
      int n = a->vertexCount * 2;
      a->uvs.resize(n);
      for (auto& u : a->uvs) u = r.f32();
      int tri = (n - hull - 2) * 3;
      a->triangles.resize(tri);
      for (auto& t : a->triangles) t = (uint16_t)r.varint();
      if (nonessential) {
        int edges = r.varint();
        for (int i = 0; i < edges; ++i) r.varint();
        r.f32(); r.f32();
      }
      break;
    }
    case kLinkedMesh: {
      a->path = (flags & 16) ? r.ref() : a->name;
      if (flags & 32) rgba8888(r.int32(), a->color);
      if (flags & 64) a->path += readSequence();
      a->skin = r.varint();
      a->parentName = r.ref();
      if (nonessential) { r.f32(); r.f32(); }
      break;
    }
    case kPath: {
      a->closed = flags & 16;
      a->constantSpeed = flags & 32;
      readVertices(r, *a, flags & 64);
      for (int i = 0; i < a->vertexCount * 2 / 6; ++i) a->lengths.push_back(r.f32());
      if (nonessential) r.int32();
      break;
    }
    case kPoint:
      r.f32(); r.f32(); r.f32();
      if (nonessential) r.int32();
      break;
    case kClipping:
      r.varint();
      readVertices(r, *a, flags & 16);
      if (nonessential) r.int32();
      break;
  }
  return a;
}

std::unique_ptr<Skin> readSkin(Reader& r, bool isDefault, bool nonessential) {
  auto skin = std::make_unique<Skin>();
  int slotCount;
  if (isDefault) {
    slotCount = r.varint();
    if (slotCount == 0) return nullptr;
    skin->name = "default";
  } else {
    skin->name = r.str();
    if (nonessential) r.int32();
    for (int k = 0; k < 5; ++k) {  // bones, ik, transform, path, physics
      int n = r.varint();
      for (int i = 0; i < n; ++i) r.varint();
    }
    slotCount = r.varint();
  }
  for (int i = 0; i < slotCount; ++i) {
    int slot = r.varint();
    int n = r.varint();
    for (int k = 0; k < n; ++k) {
      std::string key = r.ref();
      auto a = readAttachment(r, key, nonessential);
      skin->attachments[{slot, key}] = std::move(a);
    }
  }
  return skin;
}

bool readAnimation(Reader& r, SkeletonData& sd, Animation& anim, const std::vector<bool>& eventHasAudio) {
  r.varint();  // timeline count
  auto finish = [&](Timeline& tl) {
    if (!tl.frames.empty()) anim.duration = std::max(anim.duration, tl.frames[(tl.frameCount() - 1) * tl.entries]);
    anim.timelines.push_back(std::move(tl));
  };

  // Slot timelines
  for (int n = r.varint(), i = 0; i < n; ++i) {
    int slot = r.varint();
    for (int nn = r.varint(), ii = 0; ii < nn; ++ii) {
      int type = r.byte(), frames = r.varint();
      Timeline tl;
      tl.index = slot;
      if (type == 0) {
        tl.type = Timeline::Attachment;
        tl.entries = 1;
        for (int f = 0; f < frames; ++f) {
          tl.frames.push_back(r.f32());
          tl.names.push_back(r.ref());
        }
        finish(tl);
        continue;
      }
      int bezierCount = r.varint();
      int values = type == 1 ? 4 : type == 2 ? 3 : type == 3 ? 7 : type == 4 ? 6 : 1;
      tl.type = type == 1 ? Timeline::RGBA : type == 2 ? Timeline::RGB : type == 3 ? Timeline::RGBA2
               : type == 4 ? Timeline::RGB2 : Timeline::Alpha;
      tl.entries = 1 + values;
      tl.frames.assign(frames * tl.entries, 0.f);
      tl.curves.assign(frames + bezierCount * kBezierSize, 0.f);
      float time = r.f32(), v[7];
      for (int k = 0; k < values; ++k) v[k] = r.byte() / 255.f;
      for (int frame = 0, bezier = 0;; ++frame) {
        tl.frames[frame * tl.entries] = time;
        for (int k = 0; k < values; ++k) tl.frames[frame * tl.entries + 1 + k] = v[k];
        if (frame == frames - 1) break;
        float time2 = r.f32(), v2[7];
        for (int k = 0; k < values; ++k) v2[k] = r.byte() / 255.f;
        uint8_t curve = r.byte();
        if (curve == kStepped) tl.curves[frame] = kStepped;
        else if (curve == kBezier)
          for (int k = 0; k < values; ++k) readBezier(r, tl, bezier++, frame, k, time, time2, v[k], v2[k]);
        time = time2;
        for (int k = 0; k < values; ++k) v[k] = v2[k];
      }
      finish(tl);
    }
  }

  // Bone timelines
  for (int n = r.varint(), i = 0; i < n; ++i) {
    int bone = r.varint();
    for (int nn = r.varint(), ii = 0; ii < nn; ++ii) {
      int type = r.byte(), frames = r.varint();
      Timeline tl;
      tl.index = bone;
      if (type == 10) {
        tl.type = Timeline::Inherit;
        tl.entries = 2;
        for (int f = 0; f < frames; ++f) {
          tl.frames.push_back(r.f32());
          tl.frames.push_back((float)r.byte());
        }
        finish(tl);
        continue;
      }
      int bezierCount = r.varint();
      static const Timeline::Type types[] = {Timeline::Rotate, Timeline::Translate, Timeline::TranslateX,
                                             Timeline::TranslateY, Timeline::Scale, Timeline::ScaleX,
                                             Timeline::ScaleY, Timeline::Shear, Timeline::ShearX, Timeline::ShearY};
      if (type > 9) return false;
      tl.type = types[type];
      int values = (type == 1 || type == 4 || type == 7) ? 2 : 1;
      readCurveTimeline(r, tl, frames, bezierCount, values);
      finish(tl);
    }
  }

  // IK constraint timelines: time, mix, softness, bend, compress, stretch
  for (int n = r.varint(), i = 0; i < n; ++i) {
    Timeline tl;
    tl.type = Timeline::Ik;
    tl.index = r.varint();
    int frames = r.varint(), bezierCount = r.varint();
    tl.entries = 6;
    tl.frames.assign(frames * 6, 0.f);
    tl.curves.assign(frames + bezierCount * kBezierSize, 0.f);
    int flags = r.byte();
    float time = r.f32();
    float mix = (flags & 1) ? ((flags & 2) ? r.f32() : 1.f) : 0.f;
    float soft = (flags & 4) ? r.f32() : 0.f;
    for (int frame = 0, bezier = 0;; ++frame) {
      float* f = &tl.frames[frame * 6];
      f[0] = time; f[1] = mix; f[2] = soft;
      f[3] = (flags & 8) ? 1.f : -1.f;
      f[4] = (flags & 16) ? 1.f : 0.f;
      f[5] = (flags & 32) ? 1.f : 0.f;
      if (frame == frames - 1) break;
      flags = r.byte();
      float time2 = r.f32();
      float mix2 = (flags & 1) ? ((flags & 2) ? r.f32() : 1.f) : 0.f;
      float soft2 = (flags & 4) ? r.f32() : 0.f;
      if (flags & 64) tl.curves[frame] = kStepped;
      else if (flags & 128) {
        readBezier(r, tl, bezier++, frame, 0, time, time2, mix, mix2);
        readBezier(r, tl, bezier++, frame, 1, time, time2, soft, soft2);
      }
      time = time2; mix = mix2; soft = soft2;
    }
    finish(tl);
  }

  // Transform constraint timelines: 6 mixes
  for (int n = r.varint(), i = 0; i < n; ++i) {
    Timeline tl;
    tl.type = Timeline::Transform;
    tl.index = r.varint();
    int frames = r.varint(), bezierCount = r.varint();
    readCurveTimeline(r, tl, frames, bezierCount, 6);
    finish(tl);
  }

  // Path constraint timelines: position, spacing, mix (rotate, x, y)
  for (int n = r.varint(), i = 0; i < n; ++i) {
    int index = r.varint();
    for (int nn = r.varint(), ii = 0; ii < nn; ++ii) {
      int type = r.byte(), frames = r.varint(), bezierCount = r.varint();
      Timeline tl;
      tl.index = index;
      tl.type = type == 0 ? Timeline::PathPosition : type == 1 ? Timeline::PathSpacing : Timeline::PathMix;
      readCurveTimeline(r, tl, frames, bezierCount, type == 2 ? 3 : 1);
      finish(tl);
    }
  }

  // Physics constraint timelines (parsed, not applied)
  for (int n = r.varint(), i = 0; i < n; ++i) {
    r.varint();
    for (int nn = r.varint(), ii = 0; ii < nn; ++ii) {
      int type = r.byte(), frames = r.varint();
      if (type == 8) {
        for (int f = 0; f < frames; ++f) r.f32();
        continue;
      }
      int bezierCount = r.varint();
      Timeline tl;
      readCurveTimeline(r, tl, frames, bezierCount, 1);
    }
  }

  // Attachment timelines: deform / sequence
  for (int n = r.varint(), i = 0; i < n; ++i) {
    int skinIndex = r.varint();
    const Skin* skin = skinIndex < (int)sd.skins.size() ? sd.skins[skinIndex].get() : nullptr;
    for (int nn = r.varint(), ii = 0; ii < nn; ++ii) {
      int slot = r.varint();
      for (int nnn = r.varint(), iii = 0; iii < nnn; ++iii) {
        std::string attName = r.ref();
        int type = r.byte(), frames = r.varint();
        const Attachment* att = skin ? skin->get(slot, attName) : nullptr;
        if (type != 0) {  // sequence
          for (int f = 0; f < frames; ++f) { r.f32(); r.int32(); r.f32(); }
          continue;
        }
        int bezierCount = r.varint();
        Timeline tl;
        tl.type = Timeline::Deform;
        tl.index = slot;
        tl.attachment = att;
        tl.entries = 1;
        tl.frames.assign(frames, 0.f);
        tl.curves.assign(frames + bezierCount * kBezierSize, 0.f);
        bool weighted = att && att->weighted;
        int length = !att ? 0 : weighted ? (int)(att->vertices.size() / 3) * 2 : (int)att->vertices.size();
        float time = r.f32();
        for (int frame = 0, bezier = 0;; ++frame) {
          std::vector<float> deform;
          int end = r.varint();
          if (end != 0) {
            deform.assign(std::max(length, 0), 0.f);
            int start = r.varint();
            end += start;
            for (int v = start; v < end; ++v) {
              float val = r.f32();
              if (v < (int)deform.size()) deform[v] = val;
            }
            if (!weighted && att)
              for (int v = 0; v < length; ++v) deform[v] += att->vertices[v];
          }
          tl.frames[frame] = time;
          tl.deforms.push_back(std::move(deform));
          if (frame == frames - 1) break;
          float time2 = r.f32();
          uint8_t curve = r.byte();
          if (curve == kStepped) tl.curves[frame] = kStepped;
          else if (curve == kBezier) readBezier(r, tl, bezier++, frame, 0, time, time2, 0.f, 1.f);
          time = time2;
        }
        if (att) finish(tl);
      }
    }
  }

  // Draw order
  int slotCount = (int)sd.slots.size();
  if (int n = r.varint()) {
    Timeline tl;
    tl.type = Timeline::DrawOrder;
    tl.entries = 1;
    for (int i = 0; i < n; ++i) {
      float time = r.f32();
      int offsets = r.varint();
      std::vector<int> order(slotCount, -1), unchanged;
      int orig = 0;
      for (int k = 0; k < offsets; ++k) {
        int slot = r.varint();
        while (orig != slot) unchanged.push_back(orig++);
        int to = orig + r.varint();
        if (to >= 0 && to < slotCount) order[to] = orig;
        ++orig;
      }
      while (orig < slotCount) unchanged.push_back(orig++);
      for (int k = slotCount - 1; k >= 0; --k)
        if (order[k] == -1) { order[k] = unchanged.back(); unchanged.pop_back(); }
      tl.frames.push_back(time);
      tl.drawOrders.push_back(offsets ? order : std::vector<int>{});
    }
    finish(tl);
  }

  // Events (skipped)
  for (int n = r.varint(), i = 0; i < n; ++i) {
    r.f32();
    int e = r.varint();
    r.varint(false);
    r.f32();
    r.str();
    if (e < (int)eventHasAudio.size() && eventHasAudio[e]) { r.f32(); r.f32(); }
  }
  return r.ok();
}

}  // namespace

// ---------------------------------------------------------------- data

const Animation* SkeletonData::animation(const std::string& name) const {
  for (auto& a : animations) if (a.name == name) return &a;
  return nullptr;
}

int SkeletonData::region(const std::string& name) const {
  for (int i = 0; i < (int)regions.size(); ++i) if (regions[i].name == name) return i;
  return -1;
}

float SkeletonData::mix(const std::string& from, const std::string& to) const {
  auto it = mixes.find({from, to});
  return it == mixes.end() ? defaultMix : it->second;
}

std::unique_ptr<SkeletonData> loadSkeleton(const std::string& bytes, const std::string& atlasText, std::string* error) {
  auto sd = std::make_unique<SkeletonData>();
  Reader r(bytes);
  r.skip(8);  // hash
  std::string version = r.str();
  if (version.rfind("4.2", 0) != 0) {
    if (error) *error = "unsupported spine version " + version;
    return nullptr;
  }
  for (int i = 0; i < 5; ++i) r.f32();  // x, y, width, height, reference scale
  bool nonessential = r.boolean();
  if (nonessential) { r.f32(); r.str(); r.str(); }
  for (int n = r.varint(), i = 0; i < n; ++i) r.strings.push_back(r.str());

  for (int n = r.varint(), i = 0; i < n; ++i) {
    BoneData b;
    b.name = r.str();
    b.parent = i == 0 ? -1 : r.varint();
    b.rotation = r.f32(); b.x = r.f32(); b.y = r.f32();
    b.scaleX = r.f32(); b.scaleY = r.f32();
    b.shearX = r.f32(); b.shearY = r.f32();
    b.length = r.f32();
    b.inherit = r.byte();
    r.boolean();  // skin required
    if (nonessential) { r.int32(); r.str(); r.boolean(); }
    sd->bones.push_back(b);
  }

  for (int n = r.varint(), i = 0; i < n; ++i) {
    SlotData s;
    s.name = r.str();
    s.bone = r.varint();
    rgba8888(r.int32(), s.color);
    r.int32();  // dark colour
    s.attachment = r.ref();
    s.blend = r.varint();
    if (nonessential) r.boolean();
    sd->slots.push_back(s);
  }

  for (int n = r.varint(), i = 0; i < n; ++i) {
    IkData ik;
    ik.name = r.str();
    ik.order = r.varint();
    for (int nb = r.varint(), k = 0; k < nb; ++k) ik.bones.push_back(r.varint());
    ik.target = r.varint();
    int flags = r.byte();
    ik.bendDirection = (flags & 2) ? 1 : -1;
    ik.compress = flags & 4;
    ik.stretch = flags & 8;
    ik.uniform = flags & 16;
    if (flags & 32) ik.mix = (flags & 64) ? r.f32() : 1.f;
    else ik.mix = 0.f;
    if (flags & 128) ik.softness = r.f32();
    sd->iks.push_back(ik);
  }

  for (int n = r.varint(), i = 0; i < n; ++i) {
    TransformData t;
    t.name = r.str();
    t.order = r.varint();
    for (int nb = r.varint(), k = 0; k < nb; ++k) t.bones.push_back(r.varint());
    t.target = r.varint();
    int flags = r.byte();
    t.local = flags & 2;
    t.relative = flags & 4;
    if (flags & 8) t.offRotation = r.f32();
    if (flags & 16) t.offX = r.f32();
    if (flags & 32) t.offY = r.f32();
    if (flags & 64) t.offScaleX = r.f32();
    if (flags & 128) t.offScaleY = r.f32();
    flags = r.byte();
    if (flags & 1) t.offShearY = r.f32();
    if (flags & 2) t.mixRotate = r.f32();
    if (flags & 4) t.mixX = r.f32();
    if (flags & 8) t.mixY = r.f32();
    if (flags & 16) t.mixScaleX = r.f32();
    if (flags & 32) t.mixScaleY = r.f32();
    if (flags & 64) t.mixShearY = r.f32();
    sd->transforms.push_back(t);
  }

  for (int n = r.varint(), i = 0; i < n; ++i) {
    PathData p;
    p.name = r.str();
    p.order = r.varint();
    r.boolean();  // skin required
    for (int nb = r.varint(), k = 0; k < nb; ++k) p.bones.push_back(r.varint());
    p.target = r.varint();
    int flags = r.byte();
    p.positionMode = flags & 1;
    p.spacingMode = (flags >> 1) & 3;
    p.rotateMode = (flags >> 3) & 3;
    if (flags & 128) p.offRotation = r.f32();
    p.position = r.f32();
    p.spacing = r.f32();
    p.mixRotate = r.f32();
    p.mixX = r.f32();
    p.mixY = r.f32();
    sd->paths.push_back(p);
  }

  for (int n = r.varint(), i = 0; i < n; ++i) {  // physics constraints (skipped)
    r.str(); r.varint(); r.varint();
    int flags = r.byte();
    for (int bit : {2, 4, 8, 16, 32, 64}) if (flags & bit) r.f32();
    r.byte();
    r.f32(); r.f32(); r.f32();
    if (flags & 128) r.f32();
    r.f32(); r.f32();
    flags = r.byte();
    if (flags & 128) r.f32();
  }

  if (auto def = readSkin(r, true, nonessential)) sd->skins.push_back(std::move(def));
  for (int n = r.varint(), i = 0; i < n; ++i) sd->skins.push_back(readSkin(r, false, nonessential));

  // Linked meshes take their geometry from the parent mesh.
  for (auto& skin : sd->skins) {
    for (auto& [key, att] : skin->attachments) {
      if (att->kind != kLinkedMesh) continue;
      Skin* src = att->skin < (int)sd->skins.size() ? sd->skins[att->skin].get() : skin.get();
      att->parent = src->get(key.first, att->parentName);
      if (!att->parent) att->parent = skin->get(key.first, att->parentName);
    }
  }

  std::vector<bool> eventAudio;
  for (int n = r.varint(), i = 0; i < n; ++i) {
    r.str();
    r.varint(false);
    r.f32();
    r.str();
    std::string audio;
    bool has = r.string(audio);
    eventAudio.push_back(has);
    if (has) { r.f32(); r.f32(); }
  }

  for (int n = r.varint(), i = 0; i < n; ++i) {
    Animation anim;
    anim.name = r.str();
    if (!readAnimation(r, *sd, anim, eventAudio)) {
      if (error) *error = "bad animation " + anim.name;
      return nullptr;
    }
    sd->animations.push_back(std::move(anim));
  }
  if (!r.ok()) {
    if (error) *error = "truncated skeleton";
    return nullptr;
  }

  // Atlas mapping from build_assets.py.
  std::istringstream in(atlasText);
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream ls(line);
    std::string tag;
    ls >> tag;
    if (tag == "scale") ls >> sd->scale;
    else if (tag == "page") {
      int idx;
      std::string path;
      ls >> idx >> path;
      if ((int)sd->pages.size() <= idx) sd->pages.resize(idx + 1);
      sd->pages[idx] = path;
    } else if (tag == "region") {
      Region reg;
      ls >> reg.page >> reg.a >> reg.b >> reg.c >> reg.d >> reg.e >> reg.f >> reg.u0 >> reg.v0 >> reg.u1 >> reg.v1;
      std::getline(ls, reg.name);
      if (!reg.name.empty() && reg.name[0] == ' ') reg.name.erase(0, 1);
      sd->regions.push_back(reg);
    } else if (tag == "mix") {
      std::string from, to;
      float d;
      ls >> from >> to >> d;
      sd->mixes[{from, to}] = d;
    } else if (tag == "defaultmix") {
      ls >> sd->defaultMix;
    }
  }
  for (auto& skin : sd->skins)
    for (auto& [key, att] : skin->attachments) att->region = sd->region(att->path.empty() ? att->name : att->path);
  return sd;
}

// ---------------------------------------------------------------- skeleton

Skeleton::Skeleton(const SkeletonData* d) : data(d) {
  bones.resize(d->bones.size());
  for (size_t i = 0; i < bones.size(); ++i) {
    bones[i].data = &d->bones[i];
    bones[i].parent = d->bones[i].parent;
  }
  slots.resize(d->slots.size());
  for (size_t i = 0; i < slots.size(); ++i) slots[i].data = &d->slots[i];
  setToSetupPose();
}

static const Attachment* findAttachment(const SkeletonData* d, int slot, const std::string& name) {
  if (name.empty()) return nullptr;
  for (auto& s : d->skins)
    if (auto* a = s->get(slot, name)) return a;
  return nullptr;
}

void Skeleton::setToSetupPose() {
  for (auto& b : bones) {
    const BoneData& d = *b.data;
    b.x = d.x; b.y = d.y; b.rotation = d.rotation;
    b.scaleX = d.scaleX; b.scaleY = d.scaleY;
    b.shearX = d.shearX; b.shearY = d.shearY;
    b.inherit = d.inherit;
  }
  for (size_t i = 0; i < slots.size(); ++i) {
    auto& s = slots[i];
    memcpy(s.color, s.data->color, sizeof s.color);
    s.attachment = findAttachment(data, (int)i, s.data->attachment);
    s.deform.clear();
  }
  drawOrder.resize(slots.size());
  for (size_t i = 0; i < drawOrder.size(); ++i) drawOrder[i] = (int)i;
  iks = data->iks;
  transforms = data->transforms;
  paths = data->paths;
}

void Skeleton::updateBoneWith(Bone& b, float x, float y, float rotation, float scaleX, float scaleY, float shearX,
                              float shearY) {
  b.ax = x; b.ay = y; b.arotation = rotation;
  b.ascaleX = scaleX; b.ascaleY = scaleY;
  b.ashearX = shearX; b.ashearY = shearY;
  if (b.parent < 0) {
    float rx = (rotation + shearX) * kDegRad, ry = (rotation + 90 + shearY) * kDegRad;
    b.a = std::cos(rx) * scaleX;
    b.b = std::cos(ry) * scaleY;
    b.c = std::sin(rx) * scaleX;
    b.d = std::sin(ry) * scaleY;
    b.worldX = x;
    b.worldY = y;
    return;
  }
  const Bone& p = bones[b.parent];
  float pa = p.a, pb = p.b, pc = p.c, pd = p.d;
  b.worldX = pa * x + pb * y + p.worldX;
  b.worldY = pc * x + pd * y + p.worldY;
  switch (b.inherit) {
    case kNormal: {
      float rx = (rotation + shearX) * kDegRad, ry = (rotation + 90 + shearY) * kDegRad;
      float la = std::cos(rx) * scaleX, lb = std::cos(ry) * scaleY;
      float lc = std::sin(rx) * scaleX, ld = std::sin(ry) * scaleY;
      b.a = pa * la + pb * lc;
      b.b = pa * lb + pb * ld;
      b.c = pc * la + pd * lc;
      b.d = pc * lb + pd * ld;
      return;
    }
    case kOnlyTranslation: {
      float rx = (rotation + shearX) * kDegRad, ry = (rotation + 90 + shearY) * kDegRad;
      b.a = std::cos(rx) * scaleX;
      b.b = std::cos(ry) * scaleY;
      b.c = std::sin(rx) * scaleX;
      b.d = std::sin(ry) * scaleY;
      return;
    }
    case kNoRotationOrReflection: {
      float s = pa * pa + pc * pc, prx;
      if (s > 0.0001f) {
        s = std::fabs(pa * pd - pb * pc) / s;
        pb = pc * s;
        pd = pa * s;
        prx = atan2Deg(pc, pa);
      } else {
        pa = 0;
        pc = 0;
        prx = 90 - atan2Deg(pd, pb);
      }
      float rx = (rotation + shearX - prx) * kDegRad, ry = (rotation + shearY - prx + 90) * kDegRad;
      float la = std::cos(rx) * scaleX, lb = std::cos(ry) * scaleY;
      float lc = std::sin(rx) * scaleX, ld = std::sin(ry) * scaleY;
      b.a = pa * la - pb * lc;
      b.b = pa * lb - pb * ld;
      b.c = pc * la + pd * lc;
      b.d = pc * lb + pd * ld;
      return;
    }
    default: {  // no scale / no scale or reflection
      float cs = cosDeg(rotation), sn = sinDeg(rotation);
      float za = pa * cs + pb * sn, zc = pc * cs + pd * sn;
      float s = std::sqrt(za * za + zc * zc);
      if (s > 0.00001f) s = 1 / s;
      za *= s;
      zc *= s;
      s = std::sqrt(za * za + zc * zc);
      if (b.inherit == kNoScale && (pa * pd - pb * pc < 0)) s = -s;
      float r = kPi / 2 + std::atan2(zc, za);
      float zb = std::cos(r) * s, zd = std::sin(r) * s;
      float rx = shearX * kDegRad, ry = (90 + shearY) * kDegRad;
      float la = std::cos(rx) * scaleX, lb = std::cos(ry) * scaleY;
      float lc = std::sin(rx) * scaleX, ld = std::sin(ry) * scaleY;
      b.a = za * la + zb * lc;
      b.b = za * lb + zb * ld;
      b.c = zc * la + zd * lc;
      b.d = zc * lb + zd * ld;
      return;
    }
  }
}

void Skeleton::updateBone(Bone& b) { updateBoneWith(b, b.x, b.y, b.rotation, b.scaleX, b.scaleY, b.shearX, b.shearY); }

void Skeleton::updateAppliedTransform(Bone& b) {
  if (b.parent < 0) {
    b.ax = b.worldX;
    b.ay = b.worldY;
    b.arotation = atan2Deg(b.c, b.a);
    b.ascaleX = std::sqrt(b.a * b.a + b.c * b.c);
    b.ascaleY = std::sqrt(b.b * b.b + b.d * b.d);
    b.ashearX = 0;
    b.ashearY = atan2Deg(b.a * b.b + b.c * b.d, b.a * b.d - b.b * b.c);
    return;
  }
  const Bone& p = bones[b.parent];
  float pa = p.a, pb = p.b, pc = p.c, pd = p.d;
  float pid = 1 / (pa * pd - pb * pc);
  float ia = pd * pid, ib = pb * pid, ic = pc * pid, id = pa * pid;
  float dx = b.worldX - p.worldX, dy = b.worldY - p.worldY;
  b.ax = dx * ia - dy * ib;
  b.ay = dy * id - dx * ic;
  float ra, rb, rc, rd;
  if (b.inherit == kOnlyTranslation) {
    ra = b.a; rb = b.b; rc = b.c; rd = b.d;
  } else {
    if (b.inherit == kNoRotationOrReflection) {
      float s = std::fabs(pa * pd - pb * pc) / (pa * pa + pc * pc);
      pb = -pc * s;
      pd = pa * s;
      pid = 1 / (pa * pd - pb * pc);
      ia = pd * pid;
      ib = pb * pid;
    } else if (b.inherit == kNoScale || b.inherit == kNoScaleOrReflection) {
      float cs = cosDeg(b.rotation), sn = sinDeg(b.rotation);
      pa = pa * cs + pb * sn;
      pc = pc * cs + pd * sn;
      float s = std::sqrt(pa * pa + pc * pc);
      if (s > 0.00001f) s = 1 / s;
      pa *= s;
      pc *= s;
      s = std::sqrt(pa * pa + pc * pc);
      if (b.inherit == kNoScale && pid < 0) s = -s;
      float r = kPi / 2 + std::atan2(pc, pa);
      pb = std::cos(r) * s;
      pd = std::sin(r) * s;
      pid = 1 / (pa * pd - pb * pc);
      ia = pd * pid;
      ib = pb * pid;
      ic = pc * pid;
      id = pa * pid;
    }
    ra = ia * b.a - ib * b.c;
    rb = ia * b.b - ib * b.d;
    rc = id * b.c - ic * b.a;
    rd = id * b.d - ic * b.b;
  }
  b.ashearX = 0;
  b.ascaleX = std::sqrt(ra * ra + rc * rc);
  if (b.ascaleX > 0.0001f) {
    float det = ra * rd - rb * rc;
    b.ascaleY = det / b.ascaleX;
    b.ashearY = -atan2Deg(ra * rb + rc * rd, det);
    b.arotation = atan2Deg(rc, ra);
  } else {
    b.ascaleX = 0;
    b.ascaleY = std::sqrt(rb * rb + rd * rd);
    b.ashearY = 0;
    b.arotation = 90 - atan2Deg(rd, rb);
  }
}

void Skeleton::updateDescendants(const std::vector<int>& roots) {
  std::vector<char> dirty(bones.size(), 0);
  for (int r : roots) dirty[r] = 2;  // 2: changed by the constraint itself, don't recompute
  for (size_t i = 0; i < bones.size(); ++i) {
    if (dirty[i]) continue;
    int p = bones[i].parent;
    if (p >= 0 && dirty[p]) {
      dirty[i] = 1;
      Bone& b = bones[i];
      updateBoneWith(b, b.ax, b.ay, b.arotation, b.ascaleX, b.ascaleY, b.ashearX, b.ashearY);
    }
  }
}

void Skeleton::applyIk1(Bone& bone, float targetX, float targetY, bool compress, bool stretch, bool uniform, float alpha) {
  if (bone.parent < 0) return;
  const Bone& p = bones[bone.parent];
  float pa = p.a, pb = p.b, pc = p.c, pd = p.d;
  float rotationIK = -bone.ashearX - bone.arotation, tx, ty;
  switch (bone.inherit) {
    case kOnlyTranslation:
      tx = targetX - bone.worldX;
      ty = targetY - bone.worldY;
      break;
    case kNoRotationOrReflection: {
      float s = std::fabs(pa * pd - pb * pc) / std::max(0.0001f, pa * pa + pc * pc);
      float sa = pa, sc = pc;
      pb = -sc * s;
      pd = sa * s;
      rotationIK += atan2Deg(sc, sa);
    }
      [[fallthrough]];
    default: {
      float x = targetX - p.worldX, y = targetY - p.worldY;
      float d = pa * pd - pb * pc;
      if (std::fabs(d) <= 0.0001f) {
        tx = 0;
        ty = 0;
      } else {
        tx = (x * pd - y * pb) / d - bone.ax;
        ty = (y * pa - x * pc) / d - bone.ay;
      }
    }
  }
  rotationIK += atan2Deg(ty, tx);
  if (bone.ascaleX < 0) rotationIK += 180;
  if (rotationIK > 180) rotationIK -= 360;
  else if (rotationIK < -180) rotationIK += 360;
  float sx = bone.ascaleX, sy = bone.ascaleY;
  if (compress || stretch) {
    if (bone.inherit == kNoScale || bone.inherit == kNoScaleOrReflection) {
      tx = targetX - bone.worldX;
      ty = targetY - bone.worldY;
    }
    float b = bone.data->length * sx;
    if (b > 0.0001f) {
      float dd = tx * tx + ty * ty;
      if ((compress && dd < b * b) || (stretch && dd > b * b)) {
        float s = (std::sqrt(dd) / b - 1) * alpha + 1;
        sx *= s;
        if (uniform) sy *= s;
      }
    }
  }
  updateBoneWith(bone, bone.ax, bone.ay, bone.arotation + rotationIK * alpha, sx, sy, bone.ashearX, bone.ashearY);
}

void Skeleton::applyIk2(Bone& parent, Bone& child, float targetX, float targetY, int bendDir, bool stretch, bool uniform,
                        float softness, float alpha) {
  if (parent.inherit != kNormal || child.inherit != kNormal || parent.parent < 0) return;
  float px = parent.ax, py = parent.ay, psx = parent.ascaleX, psy = parent.ascaleY, sx = psx, sy = psy;
  float csx = child.ascaleX;
  int os1, os2, s2;
  if (psx < 0) { psx = -psx; os1 = 180; s2 = -1; } else { os1 = 0; s2 = 1; }
  if (psy < 0) { psy = -psy; s2 = -s2; }
  if (csx < 0) { csx = -csx; os2 = 180; } else os2 = 0;
  float cx = child.ax, cy, cwx, cwy, a = parent.a, b = parent.b, c = parent.c, d = parent.d;
  bool u = std::fabs(psx - psy) <= 0.0001f;
  if (!u || stretch) {
    cy = 0;
    cwx = a * cx + parent.worldX;
    cwy = c * cx + parent.worldY;
  } else {
    cy = child.ay;
    cwx = a * cx + b * cy + parent.worldX;
    cwy = c * cx + d * cy + parent.worldY;
  }
  const Bone& pp = bones[parent.parent];
  a = pp.a; b = pp.b; c = pp.c; d = pp.d;
  float id = a * d - b * c, x = cwx - pp.worldX, y = cwy - pp.worldY;
  id = std::fabs(id) <= 0.0001f ? 0 : 1 / id;
  float dx = (x * d - y * b) * id - px, dy = (y * a - x * c) * id - py;
  float l1 = std::sqrt(dx * dx + dy * dy), l2 = child.data->length * csx, a1, a2;
  if (l1 < 0.0001f) {
    applyIk1(parent, targetX, targetY, false, stretch, false, alpha);
    updateBoneWith(child, cx, cy, 0, child.ascaleX, child.ascaleY, child.ashearX, child.ashearY);
    return;
  }
  x = targetX - pp.worldX;
  y = targetY - pp.worldY;
  float tx = (x * d - y * b) * id - px, ty = (y * a - x * c) * id - py;
  float dd = tx * tx + ty * ty;
  if (softness != 0) {
    softness *= psx * (csx + 1) * 0.5f;
    float td = std::sqrt(dd), sd = td - l1 - l2 * psy + softness;
    if (sd > 0) {
      float p = std::min(1.f, sd / (softness * 2)) - 1;
      p = (sd - softness * (1 - p * p)) / td;
      tx -= p * tx;
      ty -= p * ty;
      dd = tx * tx + ty * ty;
    }
  }
  if (u) {
    l2 *= psy;
    float cs = (dd - l1 * l1 - l2 * l2) / (2 * l1 * l2);
    if (cs < -1) {
      cs = -1;
      a2 = kPi * bendDir;
    } else if (cs > 1) {
      cs = 1;
      a2 = 0;
      if (stretch) {
        a = (std::sqrt(dd) / (l1 + l2) - 1) * alpha + 1;
        sx *= a;
        if (uniform) sy *= a;
      }
    } else {
      a2 = std::acos(cs) * bendDir;
    }
    a = l1 + l2 * cs;
    b = l2 * std::sin(a2);
    a1 = std::atan2(ty * a - tx * b, tx * a + ty * b);
  } else {
    a = psx * l2;
    b = psy * l2;
    float aa = a * a, bb = b * b, ta = std::atan2(ty, tx);
    c = bb * l1 * l1 + aa * dd - aa * bb;
    float c1 = -2 * bb * l1, c2 = bb - aa;
    d = c1 * c1 - 4 * c2 * c;
    bool solved = false;
    if (d >= 0) {
      float q = std::sqrt(d);
      if (c1 < 0) q = -q;
      q = -(c1 + q) * 0.5f;
      float r0 = q / c2, r1 = c / q;
      float r = std::fabs(r0) < std::fabs(r1) ? r0 : r1;
      r0 = dd - r * r;
      if (r0 >= 0) {
        y = std::sqrt(r0) * bendDir;
        a1 = ta - std::atan2(y, r);
        a2 = std::atan2(y / psy, (r - l1) / psx);
        solved = true;
      }
    }
    if (!solved) {
      float minAngle = kPi, minX = l1 - a, minDist = minX * minX, minY = 0;
      float maxAngle = 0, maxX = l1 + a, maxDist = maxX * maxX, maxY = 0;
      c = -a * l1 / (aa - bb);
      if (c >= -1 && c <= 1) {
        c = std::acos(c);
        x = a * std::cos(c) + l1;
        y = b * std::sin(c);
        d = x * x + y * y;
        if (d < minDist) { minAngle = c; minDist = d; minX = x; minY = y; }
        if (d > maxDist) { maxAngle = c; maxDist = d; maxX = x; maxY = y; }
      }
      if (dd <= (minDist + maxDist) * 0.5f) {
        a1 = ta - std::atan2(minY * bendDir, minX);
        a2 = minAngle * bendDir;
      } else {
        a1 = ta - std::atan2(maxY * bendDir, maxX);
        a2 = maxAngle * bendDir;
      }
    }
  }
  float os = std::atan2(cy, cx) * s2;
  float rotation = parent.arotation;
  a1 = (a1 - os) * kRadDeg + os1 - rotation;
  if (a1 > 180) a1 -= 360;
  else if (a1 < -180) a1 += 360;
  updateBoneWith(parent, px, py, rotation + a1 * alpha, sx, sy, 0, 0);
  rotation = child.arotation;
  a2 = ((a2 + os) * kRadDeg - child.ashearX) * s2 + os2 - rotation;
  if (a2 > 180) a2 -= 360;
  else if (a2 < -180) a2 += 360;
  updateBoneWith(child, cx, cy, rotation + a2 * alpha, child.ascaleX, child.ascaleY, child.ashearX, child.ashearY);
}

void Skeleton::applyIk(const IkData& ik) {
  if (ik.mix == 0 || ik.bones.empty()) return;
  const Bone& t = bones[ik.target];
  if (ik.bones.size() == 1)
    applyIk1(bones[ik.bones[0]], t.worldX, t.worldY, ik.compress, ik.stretch, ik.uniform, ik.mix);
  else
    applyIk2(bones[ik.bones[0]], bones[ik.bones[1]], t.worldX, t.worldY, ik.bendDirection, ik.stretch, ik.uniform,
             ik.softness, ik.mix);
}

void Skeleton::applyTransform(const TransformData& d) {
  if (d.mixRotate == 0 && d.mixX == 0 && d.mixY == 0 && d.mixScaleX == 0 && d.mixScaleY == 0 && d.mixShearY == 0) return;
  Bone& target = bones[d.target];
  if (d.local) {
    for (int bi : d.bones) {
      Bone& b = bones[bi];
      float rotation, x, y, scaleX, scaleY, shearY;
      if (d.relative) {
        rotation = b.arotation + (target.arotation + d.offRotation) * d.mixRotate;
        x = b.ax + (target.ax + d.offX) * d.mixX;
        y = b.ay + (target.ay + d.offY) * d.mixY;
        scaleX = b.ascaleX * (((target.ascaleX - 1 + d.offScaleX) * d.mixScaleX) + 1);
        scaleY = b.ascaleY * (((target.ascaleY - 1 + d.offScaleY) * d.mixScaleY) + 1);
        shearY = b.ashearY + (target.ashearY + d.offShearY) * d.mixShearY;
      } else {
        rotation = b.arotation;
        if (d.mixRotate != 0) rotation += (target.arotation - rotation + d.offRotation) * d.mixRotate;
        x = b.ax + (target.ax - b.ax + d.offX) * d.mixX;
        y = b.ay + (target.ay - b.ay + d.offY) * d.mixY;
        scaleX = b.ascaleX;
        scaleY = b.ascaleY;
        if (d.mixScaleX != 0 && scaleX != 0) scaleX += (target.ascaleX - scaleX + d.offScaleX) * d.mixScaleX;
        if (d.mixScaleY != 0 && scaleY != 0) scaleY += (target.ascaleY - scaleY + d.offScaleY) * d.mixScaleY;
        shearY = b.ashearY;
        if (d.mixShearY != 0) shearY += (target.ashearY - shearY + d.offShearY) * d.mixShearY;
      }
      updateBoneWith(b, x, y, rotation, scaleX, scaleY, b.ashearX, shearY);
    }
    return;
  }
  float ta = target.a, tb = target.b, tc = target.c, td = target.d;
  float degRadReflect = ta * td - tb * tc > 0 ? kDegRad : -kDegRad;
  float offRot = d.offRotation * degRadReflect, offShearY = d.offShearY * degRadReflect;
  bool translate = d.mixX != 0 || d.mixY != 0;
  for (int bi : d.bones) {
    Bone& b = bones[bi];
    if (d.mixRotate != 0) {
      float a = b.a, bb = b.b, c = b.c, dd = b.d;
      float r = d.relative ? std::atan2(tc, ta) + offRot : std::atan2(tc, ta) - std::atan2(c, a) + offRot;
      if (r > kPi) r -= 2 * kPi;
      else if (r < -kPi) r += 2 * kPi;
      r *= d.mixRotate;
      float cs = std::cos(r), sn = std::sin(r);
      b.a = cs * a - sn * c;
      b.b = cs * bb - sn * dd;
      b.c = sn * a + cs * c;
      b.d = sn * bb + cs * dd;
    }
    if (translate) {
      float wx = d.offX * ta + d.offY * tb + target.worldX;
      float wy = d.offX * tc + d.offY * td + target.worldY;
      if (d.relative) {
        b.worldX += wx * d.mixX;
        b.worldY += wy * d.mixY;
      } else {
        b.worldX += (wx - b.worldX) * d.mixX;
        b.worldY += (wy - b.worldY) * d.mixY;
      }
    }
    if (d.mixScaleX != 0) {
      float s;
      if (d.relative) {
        s = (std::sqrt(ta * ta + tc * tc) - 1 + d.offScaleX) * d.mixScaleX + 1;
      } else {
        s = std::sqrt(b.a * b.a + b.c * b.c);
        if (s != 0) s = (s + (std::sqrt(ta * ta + tc * tc) - s + d.offScaleX) * d.mixScaleX) / s;
      }
      b.a *= s;
      b.c *= s;
    }
    if (d.mixScaleY != 0) {
      float s;
      if (d.relative) {
        s = (std::sqrt(tb * tb + td * td) - 1 + d.offScaleY) * d.mixScaleY + 1;
      } else {
        s = std::sqrt(b.b * b.b + b.d * b.d);
        if (s != 0) s = (s + (std::sqrt(tb * tb + td * td) - s + d.offScaleY) * d.mixScaleY) / s;
      }
      b.b *= s;
      b.d *= s;
    }
    if (d.mixShearY > 0) {
      float bb = b.b, dd = b.d;
      float by = std::atan2(dd, bb);
      float r;
      if (d.relative) {
        r = std::atan2(td, tb) - std::atan2(tc, ta);
        if (r > kPi) r -= 2 * kPi;
        else if (r < -kPi) r += 2 * kPi;
        r = by + (r - kPi / 2 + offShearY) * d.mixShearY;
      } else {
        r = std::atan2(td, tb) - std::atan2(tc, ta) - (by - std::atan2(b.c, b.a));
        if (r > kPi) r -= 2 * kPi;
        else if (r < -kPi) r += 2 * kPi;
        r = by + (r + offShearY) * d.mixShearY;
      }
      float s = std::sqrt(bb * bb + dd * dd);
      b.b = std::cos(r) * s;
      b.d = std::sin(r) * s;
    }
    updateAppliedTransform(b);
  }
}

// ---------------------------------------------------------------- path constraint

namespace {
constexpr float kEps = 0.00001f;

void addBeforePosition(float p, const float* temp, int i, float* out, int o) {
  float x1 = temp[i], y1 = temp[i + 1], dx = temp[i + 2] - x1, dy = temp[i + 3] - y1, r = std::atan2(dy, dx);
  out[o] = x1 + p * std::cos(r);
  out[o + 1] = y1 + p * std::sin(r);
  out[o + 2] = r;
}

void addAfterPosition(float p, const float* temp, int i, float* out, int o) {
  float x1 = temp[i + 2], y1 = temp[i + 3], dx = x1 - temp[i], dy = y1 - temp[i + 1], r = std::atan2(dy, dx);
  out[o] = x1 + p * std::cos(r);
  out[o + 1] = y1 + p * std::sin(r);
  out[o + 2] = r;
}

void addCurvePosition(float p, float x1, float y1, float cx1, float cy1, float cx2, float cy2, float x2, float y2,
                      float* out, int o, bool tangents) {
  if (p < kEps || std::isnan(p)) {
    out[o] = x1;
    out[o + 1] = y1;
    out[o + 2] = std::atan2(cy1 - y1, cx1 - x1);
    return;
  }
  float tt = p * p, ttt = tt * p, u = 1 - p, uu = u * u, uuu = uu * u;
  float ut = u * p, ut3 = ut * 3, uut3 = u * ut3, utt3 = ut3 * p;
  float x = x1 * uuu + cx1 * uut3 + cx2 * utt3 + x2 * ttt, y = y1 * uuu + cy1 * uut3 + cy2 * utt3 + y2 * ttt;
  out[o] = x;
  out[o + 1] = y;
  if (tangents) {
    if (p < 0.001f) out[o + 2] = std::atan2(cy1 - y1, cx1 - x1);
    else out[o + 2] = std::atan2(y - (y1 * uu + cy1 * ut * 2 + cy2 * tt), x - (x1 * uu + cx1 * ut * 2 + cx2 * tt));
  }
}
}  // namespace

void Skeleton::computePathPositions(const Slot& target, const Attachment& path, const PathData& d, float position,
                                    const std::vector<float>& spaces, int spacesCount, bool tangents,
                                    std::vector<float>& out) const {
  out.assign(spacesCount * 3 + 2, 0.f);
  std::vector<float> all;  // every world vertex of the path
  computeVertices(target, path, all);
  bool closed = path.closed;
  int verticesLength = path.vertexCount * 2, curveCount = verticesLength / 6, prevCurve = -1;
  auto copy = [&](int start, int count, float* dst) {
    for (int k = 0; k < count; ++k) dst[k] = all[(start + k) % all.size()];
  };

  if (!path.constantSpeed) {
    const auto& lengths = path.lengths;
    curveCount -= closed ? 1 : 2;
    if (curveCount < 0 || curveCount >= (int)lengths.size()) return;
    float pathLength = lengths[curveCount];
    if (d.positionMode == 1) position *= pathLength;
    float multiplier = d.spacingMode == 2 ? pathLength : d.spacingMode == 3 ? pathLength / spacesCount : 1;
    float world[8];
    for (int i = 0, o = 0, curve = 0; i < spacesCount; ++i, o += 3) {
      float space = spaces[i] * multiplier;
      position += space;
      float p = position;
      if (closed) {
        p = std::fmod(p, pathLength);
        if (p < 0) p += pathLength;
        curve = 0;
      } else if (p < 0) {
        if (prevCurve != -2) { prevCurve = -2; copy(2, 4, world); }
        addBeforePosition(p, world, 0, out.data(), o);
        continue;
      } else if (p > pathLength) {
        if (prevCurve != -3) { prevCurve = -3; copy(verticesLength - 6, 4, world); }
        addAfterPosition(p - pathLength, world, 0, out.data(), o);
        continue;
      }
      for (;; ++curve) {
        float length = lengths[curve];
        if (p > length) continue;
        if (curve == 0) p /= length;
        else {
          float prev = lengths[curve - 1];
          p = (p - prev) / (length - prev);
        }
        break;
      }
      if (curve != prevCurve) {
        prevCurve = curve;
        if (closed && curve == curveCount) {
          copy(verticesLength - 4, 4, world);
          copy(0, 4, world + 4);
        } else {
          copy(curve * 6 + 2, 8, world);
        }
      }
      addCurvePosition(p, world[0], world[1], world[2], world[3], world[4], world[5], world[6], world[7], out.data(), o,
                       tangents || (i > 0 && space < kEps));
    }
    return;
  }

  std::vector<float> world;
  if (closed) {
    verticesLength += 2;
    world.resize(verticesLength);
    copy(2, verticesLength - 4, world.data());
    copy(0, 2, world.data() + verticesLength - 4);
    world[verticesLength - 2] = world[0];
    world[verticesLength - 1] = world[1];
  } else {
    curveCount--;
    verticesLength -= 4;
    world.resize(verticesLength);
    copy(2, verticesLength, world.data());
  }
  std::vector<float> curves(std::max(curveCount, 0));
  float pathLength = 0;
  float x1 = world[0], y1 = world[1], cx1 = 0, cy1 = 0, cx2 = 0, cy2 = 0, x2 = 0, y2 = 0;
  float tmpx, tmpy, dddfx, dddfy, ddfx, ddfy, dfx, dfy;
  for (int i = 0, w = 2; i < curveCount; ++i, w += 6) {
    cx1 = world[w]; cy1 = world[w + 1]; cx2 = world[w + 2]; cy2 = world[w + 3]; x2 = world[w + 4]; y2 = world[w + 5];
    tmpx = (x1 - cx1 * 2 + cx2) * 0.1875f;
    tmpy = (y1 - cy1 * 2 + cy2) * 0.1875f;
    dddfx = ((cx1 - cx2) * 3 - x1 + x2) * 0.09375f;
    dddfy = ((cy1 - cy2) * 3 - y1 + y2) * 0.09375f;
    ddfx = tmpx * 2 + dddfx;
    ddfy = tmpy * 2 + dddfy;
    dfx = (cx1 - x1) * 0.75f + tmpx + dddfx * 0.16666667f;
    dfy = (cy1 - y1) * 0.75f + tmpy + dddfy * 0.16666667f;
    pathLength += std::sqrt(dfx * dfx + dfy * dfy);
    dfx += ddfx; dfy += ddfy; ddfx += dddfx; ddfy += dddfy;
    pathLength += std::sqrt(dfx * dfx + dfy * dfy);
    dfx += ddfx; dfy += ddfy;
    pathLength += std::sqrt(dfx * dfx + dfy * dfy);
    dfx += ddfx + dddfx; dfy += ddfy + dddfy;
    pathLength += std::sqrt(dfx * dfx + dfy * dfy);
    curves[i] = pathLength;
    x1 = x2;
    y1 = y2;
  }
  if (d.positionMode == 1) position *= pathLength;
  float multiplier = d.spacingMode == 2 ? pathLength : d.spacingMode == 3 ? pathLength / spacesCount : 1;
  float segments[10];
  float curveLength = 0;
  for (int i = 0, o = 0, curve = 0, segment = 0; i < spacesCount; ++i, o += 3) {
    float space = spaces[i] * multiplier;
    position += space;
    float p = position;
    if (closed) {
      p = std::fmod(p, pathLength);
      if (p < 0) p += pathLength;
      curve = 0;
    } else if (p < 0) {
      addBeforePosition(p, world.data(), 0, out.data(), o);
      continue;
    } else if (p > pathLength) {
      addAfterPosition(p - pathLength, world.data(), verticesLength - 4, out.data(), o);
      continue;
    }
    for (;; ++curve) {
      float length = curves[curve];
      if (p > length) continue;
      if (curve == 0) p /= length;
      else {
        float prev = curves[curve - 1];
        p = (p - prev) / (length - prev);
      }
      break;
    }
    if (curve != prevCurve) {
      prevCurve = curve;
      int ii = curve * 6;
      x1 = world[ii]; y1 = world[ii + 1]; cx1 = world[ii + 2]; cy1 = world[ii + 3];
      cx2 = world[ii + 4]; cy2 = world[ii + 5]; x2 = world[ii + 6]; y2 = world[ii + 7];
      tmpx = (x1 - cx1 * 2 + cx2) * 0.03f;
      tmpy = (y1 - cy1 * 2 + cy2) * 0.03f;
      dddfx = ((cx1 - cx2) * 3 - x1 + x2) * 0.006f;
      dddfy = ((cy1 - cy2) * 3 - y1 + y2) * 0.006f;
      ddfx = tmpx * 2 + dddfx;
      ddfy = tmpy * 2 + dddfy;
      dfx = (cx1 - x1) * 0.3f + tmpx + dddfx * 0.16666667f;
      dfy = (cy1 - y1) * 0.3f + tmpy + dddfy * 0.16666667f;
      curveLength = std::sqrt(dfx * dfx + dfy * dfy);
      segments[0] = curveLength;
      for (ii = 1; ii < 8; ++ii) {
        dfx += ddfx; dfy += ddfy; ddfx += dddfx; ddfy += dddfy;
        curveLength += std::sqrt(dfx * dfx + dfy * dfy);
        segments[ii] = curveLength;
      }
      dfx += ddfx; dfy += ddfy;
      curveLength += std::sqrt(dfx * dfx + dfy * dfy);
      segments[8] = curveLength;
      dfx += ddfx + dddfx; dfy += ddfy + dddfy;
      curveLength += std::sqrt(dfx * dfx + dfy * dfy);
      segments[9] = curveLength;
      segment = 0;
    }
    p *= curveLength;
    for (;; ++segment) {
      float length = segments[segment];
      if (p > length) continue;
      if (segment == 0) p /= length;
      else {
        float prev = segments[segment - 1];
        p = segment + (p - prev) / (length - prev);
      }
      break;
    }
    addCurvePosition(p * 0.1f, x1, y1, cx1, cy1, cx2, cy2, x2, y2, out.data(), o, tangents || (i > 0 && space < kEps));
  }
}

void Skeleton::applyPath(const PathData& d) {
  const Slot& target = slots[d.target];
  const Attachment* att = target.attachment;
  if (!att || att->kind != kPath) return;
  if (d.mixRotate == 0 && d.mixX == 0 && d.mixY == 0) return;
  bool tangents = d.rotateMode == 0, scale = d.rotateMode == 2;
  int boneCount = (int)d.bones.size(), spacesCount = tangents ? boneCount : boneCount + 1;
  std::vector<float> spaces(spacesCount, 0.f), lengths(scale ? boneCount : 0, 0.f);
  float spacing = d.spacing;
  if (d.spacingMode == 2) {  // percent
    if (scale)
      for (int i = 0; i < spacesCount - 1; ++i) {
        const Bone& b = bones[d.bones[i]];
        float sl = b.data->length, x = sl * b.a, y = sl * b.c;
        lengths[i] = std::sqrt(x * x + y * y);
      }
    for (int i = 1; i < spacesCount; ++i) spaces[i] = spacing;
  } else if (d.spacingMode == 3) {  // proportional
    float sum = 0;
    for (int i = 0, n = spacesCount - 1; i < n;) {
      const Bone& b = bones[d.bones[i]];
      float sl = b.data->length;
      if (sl < kEps) {
        if (scale) lengths[i] = 0;
        spaces[++i] = spacing;
      } else {
        float x = sl * b.a, y = sl * b.c, len = std::sqrt(x * x + y * y);
        if (scale) lengths[i] = len;
        spaces[++i] = len;
        sum += len;
      }
    }
    if (sum > 0) {
      sum = spacesCount / sum * spacing;
      for (int i = 1; i < spacesCount; ++i) spaces[i] *= sum;
    }
  } else {  // length / fixed
    bool lengthSpacing = d.spacingMode == 0;
    for (int i = 0, n = spacesCount - 1; i < n;) {
      const Bone& b = bones[d.bones[i]];
      float sl = b.data->length;
      if (sl < kEps) {
        if (scale) lengths[i] = 0;
        spaces[++i] = spacing;
      } else {
        float x = sl * b.a, y = sl * b.c, len = std::sqrt(x * x + y * y);
        if (scale) lengths[i] = len;
        spaces[++i] = (lengthSpacing ? sl + spacing : spacing) * len / sl;
      }
    }
  }
  std::vector<float> positions;
  computePathPositions(target, *att, d, d.position, spaces, spacesCount, tangents, positions);
  float boneX = positions[0], boneY = positions[1], offRot = d.offRotation;
  bool tip;
  if (offRot == 0) {
    tip = d.rotateMode == 1;
  } else {
    tip = false;
    const Bone& p = bones[target.data->bone];
    offRot *= p.a * p.d - p.b * p.c > 0 ? kDegRad : -kDegRad;
  }
  for (int i = 0, p = 3; i < boneCount; ++i, p += 3) {
    Bone& b = bones[d.bones[i]];
    b.worldX += (boneX - b.worldX) * d.mixX;
    b.worldY += (boneY - b.worldY) * d.mixY;
    float x = positions[p], y = positions[p + 1], dx = x - boneX, dy = y - boneY;
    if (scale) {
      float len = lengths[i];
      if (len >= kEps) {
        float s = (std::sqrt(dx * dx + dy * dy) / len - 1) * d.mixRotate + 1;
        b.a *= s;
        b.c *= s;
      }
    }
    boneX = x;
    boneY = y;
    if (d.mixRotate > 0) {
      float a = b.a, bb = b.b, c = b.c, dd = b.d, r;
      if (tangents) r = positions[p - 1];
      else if (spaces[i + 1] < kEps) r = positions[p + 2];
      else r = std::atan2(dy, dx);
      r -= std::atan2(c, a);
      if (tip) {
        float cs = std::cos(r), sn = std::sin(r), len = b.data->length;
        boneX += (len * (cs * a - sn * c) - dx) * d.mixRotate;
        boneY += (len * (sn * a + cs * c) - dy) * d.mixRotate;
      } else {
        r += offRot;
      }
      if (r > kPi) r -= 2 * kPi;
      else if (r < -kPi) r += 2 * kPi;
      r *= d.mixRotate;
      float cs = std::cos(r), sn = std::sin(r);
      b.a = cs * a - sn * c;
      b.b = cs * bb - sn * dd;
      b.c = sn * a + cs * c;
      b.d = sn * bb + cs * dd;
    }
    updateAppliedTransform(b);
  }
}

void Skeleton::updateWorldTransform() {
  for (auto& b : bones) updateBone(b);
  // Constraints in their authored order; bones they move pass the change on
  // to their children.
  struct C { int order; int kind; int index; };
  std::vector<C> cs;
  for (int i = 0; i < (int)iks.size(); ++i) cs.push_back({iks[i].order, 0, i});
  for (int i = 0; i < (int)transforms.size(); ++i) cs.push_back({transforms[i].order, 1, i});
  for (int i = 0; i < (int)paths.size(); ++i) cs.push_back({paths[i].order, 2, i});
  std::sort(cs.begin(), cs.end(), [](const C& a, const C& b) { return a.order < b.order; });
  for (auto& c : cs) {
    if (c.kind == 0) {
      applyIk(iks[c.index]);
      updateDescendants(iks[c.index].bones);
    } else if (c.kind == 1) {
      applyTransform(transforms[c.index]);
      updateDescendants(transforms[c.index].bones);
    } else {
      applyPath(paths[c.index]);
      updateDescendants(paths[c.index].bones);
    }
  }
}

// ---------------------------------------------------------------- vertices

void Skeleton::computeVertices(const Slot& slot, const Attachment& att, std::vector<float>& out) const {
  const Attachment& geo = att.kind == kLinkedMesh && att.parent ? *att.parent : att;
  const Bone& bone = bones[slot.data->bone];
  out.resize(geo.vertexCount * 2);
  const std::vector<float>& deform = slot.deform;
  if (!geo.weighted) {
    const std::vector<float>& v = deform.size() == geo.vertices.size() ? deform : geo.vertices;
    for (int i = 0; i < geo.vertexCount; ++i) {
      float x = v[i * 2], y = v[i * 2 + 1];
      out[i * 2] = x * bone.a + y * bone.b + bone.worldX;
      out[i * 2 + 1] = x * bone.c + y * bone.d + bone.worldY;
    }
    return;
  }
  bool hasDeform = !deform.empty();
  size_t v = 0, w = 0, f = 0;
  for (int i = 0; i < geo.vertexCount; ++i) {
    int n = geo.bones[v++];
    float wx = 0, wy = 0;
    for (int k = 0; k < n; ++k, w += 3, f += 2) {
      const Bone& b = bones[geo.bones[v++]];
      float vx = geo.vertices[w], vy = geo.vertices[w + 1], weight = geo.vertices[w + 2];
      if (hasDeform && f + 1 < deform.size()) {
        vx += deform[f];
        vy += deform[f + 1];
      }
      wx += (vx * b.a + vy * b.b + b.worldX) * weight;
      wy += (vx * b.c + vy * b.d + b.worldY) * weight;
    }
    out[i * 2] = wx;
    out[i * 2 + 1] = wy;
  }
}

static inline uint32_t packColor(const float c[4]) {
  auto q = [](float v) { return (uint32_t)(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
  return (q(c[0]) << 24) | (q(c[1]) << 16) | (q(c[2]) << 8) | q(c[3]);
}

void Skeleton::render(std::vector<Batch>& out, float ox, float oy, float extraScale, bool flipX, const float tint[4]) const {
  float s = data->scale * extraScale;
  float sx = flipX ? -s : s;
  std::vector<float> world;
  for (int si : drawOrder) {
    if (si < (int)hidden.size() && hidden[si]) continue;
    const Slot& slot = slots[si];
    const Attachment* att = slot.attachment;
    if (!att || att->region < 0) continue;
    if (att->kind != kRegion && att->kind != kMesh && att->kind != kLinkedMesh) continue;
    float color[4];
    for (int k = 0; k < 4; ++k) color[k] = slot.color[k] * att->color[k] * tint[k];
    if (color[3] <= 0.004f) continue;
    const Region& reg = data->regions[att->region];
    int blend = slot.data->blend == 1 ? 1 : 0;
    if (out.empty() || out.back().page != reg.page || out.back().blend != blend || out.back().vertices.size() > 60000) {
      out.push_back(Batch{reg.page, blend, {}, {}});
    }
    Batch& batch = out.back();
    uint32_t rgba = packColor(color);
    auto texUV = [&](float u, float v, float& tu, float& tv) {
      tu = reg.a * u + reg.b * v + reg.c;
      tv = reg.d * u + reg.e * v + reg.f;
    };
    uint16_t base = (uint16_t)batch.vertices.size();

    if (att->kind == kRegion) {
      // Quad over the packed part of the original image.
      float w = att->width * att->scaleX, h = att->height * att->scaleY;
      float x0 = -w / 2 + reg.u0 * w, x1 = -w / 2 + reg.u1 * w;
      float y1 = h / 2 - reg.v0 * h, y0 = h / 2 - reg.v1 * h;  // v runs down, y runs up
      float cs = cosDeg(att->rotation), sn = sinDeg(att->rotation);
      const Bone& b = bones[slot.data->bone];
      float corners[4][4] = {{x0, y0, reg.u0, reg.v1}, {x0, y1, reg.u0, reg.v0}, {x1, y1, reg.u1, reg.v0}, {x1, y0, reg.u1, reg.v1}};
      for (auto& c : corners) {
        float lx = c[0] * cs - c[1] * sn + att->x, ly = c[0] * sn + c[1] * cs + att->y;
        float wx = lx * b.a + ly * b.b + b.worldX, wy = lx * b.c + ly * b.d + b.worldY;
        Vertex vt;
        vt.x = ox + wx * sx;
        vt.y = oy - wy * s;
        texUV(c[2], c[3], vt.u, vt.v);
        vt.rgba = rgba;
        batch.vertices.push_back(vt);
      }
      for (uint16_t i : {0, 1, 2, 2, 3, 0}) batch.indices.push_back(base + i);
      continue;
    }

    const Attachment& geo = att->kind == kLinkedMesh && att->parent ? *att->parent : *att;
    if (geo.vertexCount == 0) continue;
    computeVertices(slot, *att, world);
    for (int i = 0; i < geo.vertexCount; ++i) {
      Vertex vt;
      vt.x = ox + world[i * 2] * sx;
      vt.y = oy - world[i * 2 + 1] * s;
      texUV(geo.uvs[i * 2], geo.uvs[i * 2 + 1], vt.u, vt.v);
      vt.rgba = rgba;
      batch.vertices.push_back(vt);
    }
    for (uint16_t t : geo.triangles) batch.indices.push_back(base + t);
  }
}

bool Skeleton::bounds(float& minX, float& minY, float& maxX, float& maxY) const {
  minX = minY = 1e9f;
  maxX = maxY = -1e9f;
  std::vector<Batch> tmp;
  const float white[4] = {1, 1, 1, 1};
  render(tmp, 0, 0, 1.f / data->scale, false, white);
  for (auto& b : tmp)
    for (auto& v : b.vertices) {
      minX = std::min(minX, v.x); maxX = std::max(maxX, v.x);
      minY = std::min(minY, -v.y); maxY = std::max(maxY, -v.y);
    }
  return minX <= maxX;
}

// ---------------------------------------------------------------- animation

void applyAnimation(const Animation& anim, Skeleton& skel, float time, bool loop, float alpha) {
  float t = time;
  if (loop && anim.duration > 0) t = std::fmod(time, anim.duration);
  else if (t > anim.duration) t = anim.duration;
  for (const Timeline& tl : anim.timelines) {
    int i = searchFrame(tl, t);
    if (i < 0) continue;
    switch (tl.type) {
      case Timeline::Rotate: {
        Bone& b = skel.bones[tl.index];
        float v = curveValue(tl, t, i, 0);
        b.rotation += (b.data->rotation + v - b.rotation) * alpha;
        break;
      }
      case Timeline::Translate: {
        Bone& b = skel.bones[tl.index];
        b.x += (b.data->x + curveValue(tl, t, i, 0) - b.x) * alpha;
        b.y += (b.data->y + curveValue(tl, t, i, 1) - b.y) * alpha;
        break;
      }
      case Timeline::TranslateX: {
        Bone& b = skel.bones[tl.index];
        b.x += (b.data->x + curveValue(tl, t, i, 0) - b.x) * alpha;
        break;
      }
      case Timeline::TranslateY: {
        Bone& b = skel.bones[tl.index];
        b.y += (b.data->y + curveValue(tl, t, i, 0) - b.y) * alpha;
        break;
      }
      case Timeline::Scale: {
        Bone& b = skel.bones[tl.index];
        b.scaleX += (b.data->scaleX * curveValue(tl, t, i, 0) - b.scaleX) * alpha;
        b.scaleY += (b.data->scaleY * curveValue(tl, t, i, 1) - b.scaleY) * alpha;
        break;
      }
      case Timeline::ScaleX: {
        Bone& b = skel.bones[tl.index];
        b.scaleX += (b.data->scaleX * curveValue(tl, t, i, 0) - b.scaleX) * alpha;
        break;
      }
      case Timeline::ScaleY: {
        Bone& b = skel.bones[tl.index];
        b.scaleY += (b.data->scaleY * curveValue(tl, t, i, 0) - b.scaleY) * alpha;
        break;
      }
      case Timeline::Shear: {
        Bone& b = skel.bones[tl.index];
        b.shearX += (b.data->shearX + curveValue(tl, t, i, 0) - b.shearX) * alpha;
        b.shearY += (b.data->shearY + curveValue(tl, t, i, 1) - b.shearY) * alpha;
        break;
      }
      case Timeline::ShearX: {
        Bone& b = skel.bones[tl.index];
        b.shearX += (b.data->shearX + curveValue(tl, t, i, 0) - b.shearX) * alpha;
        break;
      }
      case Timeline::ShearY: {
        Bone& b = skel.bones[tl.index];
        b.shearY += (b.data->shearY + curveValue(tl, t, i, 0) - b.shearY) * alpha;
        break;
      }
      case Timeline::Inherit:
        if (alpha >= 0.5f) skel.bones[tl.index].inherit = (int)tl.frames[i * 2 + 1];
        break;
      case Timeline::Attachment: {
        Slot& s = skel.slots[tl.index];
        const Attachment* a = findAttachment(skel.data, tl.index, tl.names[i]);
        if (a != s.attachment) {
          s.attachment = a;
          s.deform.clear();
        }
        break;
      }
      case Timeline::RGBA:
      case Timeline::RGBA2: {
        Slot& s = skel.slots[tl.index];
        for (int k = 0; k < 4; ++k) s.color[k] += (curveValue(tl, t, i, k) - s.color[k]) * alpha;
        break;
      }
      case Timeline::RGB:
      case Timeline::RGB2: {
        Slot& s = skel.slots[tl.index];
        for (int k = 0; k < 3; ++k) s.color[k] += (curveValue(tl, t, i, k) - s.color[k]) * alpha;
        break;
      }
      case Timeline::Alpha: {
        Slot& s = skel.slots[tl.index];
        s.color[3] += (curveValue(tl, t, i, 0) - s.color[3]) * alpha;
        break;
      }
      case Timeline::Deform: {
        Slot& s = skel.slots[tl.index];
        const Attachment* a = s.attachment;
        if (!a) break;
        const Attachment* geo = a->kind == kLinkedMesh ? a->parent : a;
        if (tl.attachment != a && tl.attachment != geo) break;
        if (!geo) break;
        size_t len = geo->weighted ? (geo->vertices.size() / 3) * 2 : geo->vertices.size();
        auto frameVals = [&](int f) -> const float* {
          const auto& d = tl.deforms[f];
          return d.size() == len ? d.data() : nullptr;  // empty: setup pose
        };
        std::vector<float> target(len);
        const float* setup = geo->weighted ? nullptr : geo->vertices.data();
        auto at = [&](const float* v, size_t k) { return v ? v[k] : (setup ? setup[k] : 0.f); };
        const float* v0 = frameVals(i);
        if (i >= tl.frameCount() - 1) {
          for (size_t k = 0; k < len; ++k) target[k] = at(v0, k);
        } else {
          float p = curvePercent(tl, t, i);
          const float* v1 = frameVals(i + 1);
          for (size_t k = 0; k < len; ++k) target[k] = at(v0, k) + (at(v1, k) - at(v0, k)) * p;
        }
        if (s.deform.size() != len) {
          s.deform.assign(len, 0.f);
          if (setup) for (size_t k = 0; k < len; ++k) s.deform[k] = setup[k];
        }
        for (size_t k = 0; k < len; ++k) s.deform[k] += (target[k] - s.deform[k]) * alpha;
        break;
      }
      case Timeline::DrawOrder:
        if (alpha >= 0.5f) {
          const auto& order = tl.drawOrders[i];
          if (order.empty()) {
            for (size_t k = 0; k < skel.drawOrder.size(); ++k) skel.drawOrder[k] = (int)k;
          } else {
            skel.drawOrder = order;
          }
        }
        break;
      case Timeline::Ik: {
        if (tl.index >= (int)skel.iks.size()) break;
        IkData& ik = skel.iks[tl.index];
        ik.mix += (curveValue(tl, t, i, 0) - ik.mix) * alpha;
        ik.softness += (curveValue(tl, t, i, 1) - ik.softness) * alpha;
        if (alpha >= 0.5f) {
          const float* f = &tl.frames[i * 6];
          ik.bendDirection = (int)f[3];
          ik.compress = f[4] != 0;
          ik.stretch = f[5] != 0;
        }
        break;
      }
      case Timeline::PathPosition:
        if (tl.index < (int)skel.paths.size())
          skel.paths[tl.index].position += (curveValue(tl, t, i, 0) - skel.paths[tl.index].position) * alpha;
        break;
      case Timeline::PathSpacing:
        if (tl.index < (int)skel.paths.size())
          skel.paths[tl.index].spacing += (curveValue(tl, t, i, 0) - skel.paths[tl.index].spacing) * alpha;
        break;
      case Timeline::PathMix:
        if (tl.index < (int)skel.paths.size()) {
          PathData& p = skel.paths[tl.index];
          p.mixRotate += (curveValue(tl, t, i, 0) - p.mixRotate) * alpha;
          p.mixX += (curveValue(tl, t, i, 1) - p.mixX) * alpha;
          p.mixY += (curveValue(tl, t, i, 2) - p.mixY) * alpha;
        }
        break;
      case Timeline::Ignored:
        break;
      case Timeline::Transform: {
        if (tl.index >= (int)skel.transforms.size()) break;
        TransformData& td = skel.transforms[tl.index];
        float* mixes[6] = {&td.mixRotate, &td.mixX, &td.mixY, &td.mixScaleX, &td.mixScaleY, &td.mixShearY};
        for (int k = 0; k < 6; ++k) *mixes[k] += (curveValue(tl, t, i, k) - *mixes[k]) * alpha;
        break;
      }
    }
  }
}

void AnimationState::play(const std::string& name, bool loop, const std::string& then) {
  const Animation* a = data_->animation(name);
  if (!a) return;
  if (cur_) {
    prev_ = cur_;
    prevTime_ = time_;
    prevLoop_ = loop_;
    mixDuration_ = data_->mix(curName_, name);
    mixTime_ = 0;
    if (mixDuration_ <= 0) prev_ = nullptr;
  }
  cur_ = a;
  curName_ = name;
  thenName_ = then;
  time_ = 0;
  loop_ = loop;
}

void AnimationState::update(float dt) {
  time_ += dt;
  if (prev_) {
    prevTime_ += dt;
    mixTime_ += dt;
    if (mixTime_ >= mixDuration_) prev_ = nullptr;
  }
  if (cur_ && !loop_ && time_ >= cur_->duration && !thenName_.empty()) play(thenName_, true);
}

void AnimationState::apply(Skeleton& skel) {
  skel.setToSetupPose();
  if (prev_) applyAnimation(*prev_, skel, prevTime_, prevLoop_, 1.f);
  if (cur_) applyAnimation(*cur_, skel, time_, loop_, prev_ ? std::min(1.f, mixTime_ / mixDuration_) : 1.f);
}

}  // namespace spine
