// Minimal Spine 4.2 runtime: binary skeleton loading, animation playback with
// crossfades, bone/IK/transform-constraint solving and mesh output.
//
// Written from the Spine runtime's published algorithms (SkeletonBinary,
// Bone.updateWorldTransform, IkConstraint, TransformConstraint, CurveTimeline).
// Not implemented: physics constraints, clipping, events, sequences.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace spine {

enum Inherit { kNormal = 0, kOnlyTranslation, kNoRotationOrReflection, kNoScale, kNoScaleOrReflection };
enum AttKind { kRegion = 0, kBoundingBox, kMesh, kLinkedMesh, kPath, kPoint, kClipping };

struct BoneData {
  std::string name;
  int parent = -1;
  float rotation = 0, x = 0, y = 0, scaleX = 1, scaleY = 1, shearX = 0, shearY = 0, length = 0;
  int inherit = kNormal;
};

struct SlotData {
  std::string name;
  int bone = 0;
  float color[4] = {1, 1, 1, 1};
  std::string attachment;
  int blend = 0;  // 0 normal, 1 additive, 2 multiply, 3 screen
};

struct IkData {
  std::string name;
  int order = 0;
  std::vector<int> bones;
  int target = 0;
  int bendDirection = 1;
  bool compress = false, stretch = false, uniform = false;
  float mix = 1, softness = 0;
};

struct TransformData {
  std::string name;
  int order = 0;
  std::vector<int> bones;
  int target = 0;
  bool local = false, relative = false;
  float offRotation = 0, offX = 0, offY = 0, offScaleX = 0, offScaleY = 0, offShearY = 0;
  float mixRotate = 0, mixX = 0, mixY = 0, mixScaleX = 0, mixScaleY = 0, mixShearY = 0;
};

struct PathData {
  std::string name;
  int order = 0;
  std::vector<int> bones;
  int target = 0;  // slot
  int positionMode = 0, spacingMode = 0, rotateMode = 0;  // fixed/percent; length/fixed/percent/proportional; tangent/chain/chainScale
  float offRotation = 0, position = 0, spacing = 0, mixRotate = 0, mixX = 0, mixY = 0;
};

struct Attachment {
  AttKind kind = kRegion;
  std::string name, path;
  float color[4] = {1, 1, 1, 1};
  // region
  float x = 0, y = 0, scaleX = 1, scaleY = 1, rotation = 0, width = 0, height = 0;
  // vertex attachments (Spine layout): unweighted -> vertices = x,y pairs;
  // weighted -> bones = [n, b0..bn-1, n, ...], vertices = x,y,w per influence.
  bool weighted = false;
  int vertexCount = 0;
  std::vector<int> bones;
  std::vector<float> vertices;
  std::vector<float> uvs;
  std::vector<uint16_t> triangles;
  // path
  bool closed = false, constantSpeed = false;
  std::vector<float> lengths;
  // linked mesh
  int skin = 0;
  std::string parentName;
  Attachment* parent = nullptr;
  int region = -1;  // index into Skeleton data regions, resolved on load
};

struct Skin {
  std::string name;
  std::map<std::pair<int, std::string>, std::unique_ptr<Attachment>> attachments;
  Attachment* get(int slot, const std::string& name) const {
    auto it = attachments.find({slot, name});
    return it == attachments.end() ? nullptr : it->second.get();
  }
};

// Keyframes plus Spine's curve table: curves[frame] is 0 linear, 1 stepped or
// 2 + offset of an 18-float bezier sample block.
struct Timeline {
  enum Type {
    Rotate, Translate, TranslateX, TranslateY, Scale, ScaleX, ScaleY, Shear, ShearX, ShearY, Inherit,
    Attachment, RGBA, RGB, RGBA2, RGB2, Alpha, Deform, DrawOrder, Ik, Transform,
    PathPosition, PathSpacing, PathMix, Ignored
  } type = Ignored;
  int index = 0;           // bone / slot / constraint
  int entries = 2;         // floats per frame
  std::vector<float> frames;
  std::vector<float> curves;
  std::vector<std::string> names;              // attachment timeline
  std::vector<std::vector<float>> deforms;     // deform timeline
  std::vector<std::vector<int>> drawOrders;    // draw order timeline
  const spine::Attachment* attachment = nullptr;  // deform target
  int frameCount() const { return (int)(frames.size() / entries); }
};

struct Animation {
  std::string name;
  float duration = 0;
  std::vector<Timeline> timelines;
};

struct Region {
  std::string name;
  int page = 0;
  float a, b, c, d, e, f;  // original-image uv -> texture uv
  float u0, v0, u1, v1;     // part of the original image that is actually packed
};

struct SkeletonData {
  std::vector<BoneData> bones;
  std::vector<SlotData> slots;
  std::vector<IkData> iks;
  std::vector<TransformData> transforms;
  std::vector<PathData> paths;
  std::vector<std::unique_ptr<Skin>> skins;
  std::vector<Animation> animations;
  std::vector<Region> regions;
  std::vector<std::string> pages;  // texture paths
  std::map<std::pair<std::string, std::string>, float> mixes;
  float defaultMix = 0.1f;
  float shiftX = 0, shiftY = 0;        // screen-pixel offset of the skeleton origin (art not centred on its root)
  std::vector<std::string> hideSlots;  // slot-name prefixes never drawn (one skeleton shared by several creatures)
  float scale = 1;  // skeleton units -> screen pixels

  const Animation* animation(const std::string& name) const;
  int region(const std::string& name) const;
  float mix(const std::string& from, const std::string& to) const;
};

// Loads KEY.skel and KEY.txt (from tools/build_assets.py).
std::unique_ptr<SkeletonData> loadSkeleton(const std::string& skelBytes, const std::string& atlasText, std::string* error);

struct Bone {
  const BoneData* data;
  int parent;
  float x, y, rotation, scaleX, scaleY, shearX, shearY;   // local (animated)
  float ax, ay, arotation, ascaleX, ascaleY, ashearX, ashearY;  // applied
  float a, b, c, d, worldX, worldY;
  int inherit;
};

struct Slot {
  const SlotData* data;
  float color[4];
  const Attachment* attachment = nullptr;
  std::vector<float> deform;
};

struct Vertex {
  float x, y, u, v;
  uint32_t rgba;
};

// One draw batch: same page texture and blend mode.
struct Batch {
  int page;
  int blend;
  std::vector<Vertex> vertices;
  std::vector<uint16_t> indices;
};

class Skeleton {
 public:
  explicit Skeleton(const SkeletonData* data);
  void setToSetupPose();
  void updateWorldTransform();
  // Appends triangles in screen space: origin at (ox, oy), y down, scaled by data->scale * extraScale.
  void render(std::vector<Batch>& out, float ox, float oy, float extraScale, bool flipX, const float tint[4]) const;
  // Axis-aligned bounds of the current pose, in skeleton units.
  bool bounds(float& minX, float& minY, float& maxX, float& maxY) const;

  const SkeletonData* data;
  std::vector<Bone> bones;
  std::vector<Slot> slots;
  std::vector<int> drawOrder;
  std::vector<char> hidden;  // per slot; lets the game hide parts (clipping is not implemented)
  std::vector<IkData> iks;              // runtime copies (mix etc. are animatable)
  std::vector<TransformData> transforms;
  std::vector<PathData> paths;

 private:
  friend void applyAnimation(const Animation&, Skeleton&, float, bool, float);
  void updateBone(Bone& b);
  void updateBoneWith(Bone& b, float x, float y, float rot, float sx, float sy, float shx, float shy);
  void updateAppliedTransform(Bone& b);
  void updateDescendants(const std::vector<int>& roots);
  void applyIk(const IkData& ik);
  void applyIk1(Bone& bone, float tx, float ty, bool compress, bool stretch, bool uniform, float alpha);
  void applyIk2(Bone& parent, Bone& child, float tx, float ty, int bend, bool stretch, bool uniform, float softness, float alpha);
  void applyTransform(const TransformData& t);
  void applyPath(const PathData& p);
  void computePathPositions(const Slot& target, const Attachment& path, const PathData& d, float position,
                            const std::vector<float>& spaces, int spacesCount, bool tangents, std::vector<float>& out) const;
  void computeVertices(const Slot& slot, const Attachment& att, std::vector<float>& out) const;
};

// Poses `skel` with `anim` at `time` (seconds). alpha < 1 blends towards the
// animation from the current pose (used for crossfades).
void applyAnimation(const Animation& anim, Skeleton& skel, float time, bool loop, float alpha);

// Track with one current animation and an optional fading-out previous one.
class AnimationState {
 public:
  explicit AnimationState(const SkeletonData* data) : data_(data) {}
  // Plays `name`; after a non-looping animation ends, `then` starts looping.
  void play(const std::string& name, bool loop, const std::string& then = "");
  void update(float dt);
  void apply(Skeleton& skel);
  const std::string& current() const { return curName_; }
  bool finished() const { return cur_ && !loop_ && time_ >= cur_->duration; }

 private:
  const SkeletonData* data_;
  const Animation* cur_ = nullptr;
  const Animation* prev_ = nullptr;
  std::string curName_, thenName_;
  float time_ = 0, prevTime_ = 0, mixTime_ = 0, mixDuration_ = 0;
  bool loop_ = true, prevLoop_ = true;
};

}  // namespace spine
