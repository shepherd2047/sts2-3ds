// S01 (RGDSplus U01): loading and the boot splash, as NGame.LaunchMainMenu + NLogoAnimation.
// The game loads the menu essentials under a black screen, then plays the MegaCrit logo (Spine,
// logo_animation.tscn): it drops in from 800 px above (Back ease-out, 0.5 s) while fading in,
// animates once, then the background eases to _logoBgColor #074254 (cubic, 2 s) and holds 1 s
// before the main menu. A click / select / cancel skips (the logo fades out in 0.25 s, expo).
// Here: both screens share the background colour and the bottom screen shows nothing else
// (U01: "上下同底色 ... 启动时下屏不混入主菜单塔景"); while Res::load runs, the bottom screen shows
// the loading label and a progress line (loadingFrame). Skippable after kSkipMin.
// Automated previews (STS_HIDDEN / STS_FIXED_STEP / STS_SCRIPT) skip all of it so scripted
// frame numbers are unchanged; STS_BOOT=1 forces it (for screenshots of the splash itself).
#include "../ui_common.h"

namespace ui {

namespace {
constexpr uint32_t kLogoBg = 0x074254FF;  // NLogoAnimation._logoBgColor
constexpr float kPreroll = 0.4f;   // black before the logo (the game: 0.8 s fade-in + 1.0 s wait)
constexpr float kDrop = 0.5f;      // drop-in and fade-in
constexpr float kDropPx = 800.f * kH / 1080.f;
constexpr float kBgFade = 2.0f, kBgHold = 1.0f;
constexpr float kSkipFade = 0.25f;
constexpr float kSkipMin = 0.3f;   // seconds after loading before A / B / START / a tap skip
// NLogoAnimation._Ready: scale = min(0.33 * size / bounds); a little larger on the 240 px screen.
constexpr float kLogoFrac = 0.42f;

struct Boot {
  bool active = false;
  const spine::SkeletonData* data = nullptr;
  std::unique_ptr<spine::Skeleton> skel;
  std::unique_ptr<spine::AnimationState> anim;
  float scale = 1, ox = kTop / 2.f, oy = kH / 2.f;  // skeleton origin on the top screen
  float t = 0;         // seconds since loading finished
  float animEnd = -1;  // t when the logo animation completed
  float skipT = -1;    // t when the player skipped
};
Boot& boot() {
  static Boot b;
  return b;
}

bool splashEnabled() {
  if (getenv("STS_BOOT")) return true;
  return !getenv("STS_HIDDEN") && !getenv("STS_FIXED_STEP") && !getenv("STS_SCRIPT");
}

float clamp01(float t) { return std::clamp(t, 0.f, 1.f); }
float easeOutBack(float t) {  // Tween.TransitionType.Back, EaseType.Out
  const float c1 = 1.70158f, c3 = c1 + 1.f, u = t - 1.f;
  return 1.f + c3 * u * u * u + c1 * u * u;
}
float easeOutExpo(float t) { return t >= 1.f ? 1.f : 1.f - std::pow(2.f, -10.f * t); }

uint32_t mixColor(uint32_t a, uint32_t b, float t) {
  uint32_t out = 0;
  for (int sh = 8; sh < 32; sh += 8) {
    float ca = (float)((a >> sh) & 0xFF), cb = (float)((b >> sh) & 0xFF);
    out |= (uint32_t)std::lround(ca + (cb - ca) * t) << sh;
  }
  return out | 0xFF;
}

// One frame per Res::load step (romfs reads + texture uploads block the main loop, so there is no
// animation in between). Top: black, as the game loads under a black screen. Bottom: the loading
// status (NLoadingOverlay's main_menu_ui.LOADING_OVERLAY.label, once the font and strings are in:
// Res::load reads them first) over a thin, non-interactive progress line (U01: "加载时显示状态，
// 不伪造可点击进度条"). Everything stays inside the 320 px bottom screen.
void loadingFrame(float progress) {
  gfx::beginFrame();
  gfx::screen(gfx::TOP, 0x000000FF);
  gfx::screen(gfx::BOTTOM, 0x000000FF);
  const float w = 160, x = std::round((kBot - w) / 2), y = std::round(kH / 2 + 6);
  if (R().fontReady()) {
    const std::string key = "main_menu_ui.LOADING_OVERLAY.label";
    const std::string& label = R().loc(key);
    if (label != key) R().text(kBot / 2, y - 8 - R().lineHeight(F12), label, ts(F12, col::gray, CENTER));
  }
  gfx::rect(x, y, w, 2, 0xFFFFFF18);
  gfx::rect(x, y, std::round(w * clamp01(progress)), 2, style::kPanelEdge);
  gfx::endFrame();
}
}  // namespace

std::function<void(float)> App::beginBoot() {
  if (!splashEnabled()) return nullptr;
  Boot& b = boot();
  b.data = R().skeleton("LOGO_MEGACRIT");  // its own small texture page, before the atlas
  if (b.data) {
    b.skel = std::make_unique<spine::Skeleton>(b.data);
    b.anim = std::make_unique<spine::AnimationState>(b.data);
    b.skel->setToSetupPose();
    b.skel->updateWorldTransform();
    float x0, y0, x1, y1;
    if (b.skel->bounds(x0, y0, x1, y1) && x1 > x0 && y1 > y0) {
      b.scale = std::min(kTop * 0.6f / (x1 - x0), kH * kLogoFrac / (y1 - y0)) / b.data->scale;
      float s = b.data->scale * b.scale;
      b.ox = kTop / 2.f - (x0 + x1) / 2.f * s;
      b.oy = kH / 2.f + (y0 + y1) / 2.f * s;  // bounds() is y-up
    }
    b.anim->play("animation", false);
  }
  b.active = true;
  loadingFrame(0);
  return loadingFrame;
}

bool App::updateBoot(const gfx::Input& in, double dt) {
  Boot& b = boot();
  if (!b.active) return false;
  float d = std::min((float)dt, 1.f / 20.f);  // the first frame's dt includes the loading time
  b.t += d;
  float logoT = b.t - kPreroll;
  bool logoPlaying = b.skel && logoT >= 0 && b.animEnd < 0 && b.skipT < 0;
  if (logoPlaying) {
    b.anim->update(d);
    if (b.anim->finished()) b.animEnd = b.t;
  } else if (!b.skel && logoT >= 0 && b.animEnd < 0) {
    b.animEnd = b.t;  // no logo baked: just the background ease
  }
  // A short minimum first, so a key still held from the Homebrew Launcher doesn't skip it unseen.
  bool skip = b.t >= kSkipMin && ((in.down & (gfx::BTN_A | gfx::BTN_B | gfx::BTN_START)) || in.touchDown);
  if (skip && b.skipT < 0) {
    if (logoPlaying) b.skipT = b.t;
    else b.animEnd = b.t - kBgFade - kBgHold;  // before or after the logo: straight on
  }
  bool done = (b.skipT >= 0 && b.t - b.skipT >= kSkipFade) || (b.animEnd >= 0 && b.t - b.animEnd >= kBgFade + kBgHold);
  if (done) {
    b.active = false;
    b.anim.reset();
    b.skel.reset();
    if (b.data) R().releaseSkeletons({});  // only the logo is loaded at this point
    b.data = nullptr;
    transitionT_ = 1.f;  // Transition.FadeOut -> the title fades in through black (F6)
  }
  return true;
}

bool App::drawBoot(bool top) {
  const Boot& b = boot();
  if (!b.active) return false;
  float bg = b.animEnd >= 0 ? easeOut(clamp01((b.t - b.animEnd) / kBgFade)) : 0.f;
  gfx::rect(0, 0, top ? kTop : kBot, kH, mixColor(0x000000FF, kLogoBg, bg));
  float logoT = b.t - kPreroll;
  if (!top || !b.skel || logoT < 0) return true;
  float k = clamp01(logoT / kDrop);
  float alpha = k;
  if (b.skipT >= 0) alpha *= 1.f - easeOutExpo(clamp01((b.t - b.skipT) / kSkipFade));
  float dy = -(1.f - easeOutBack(k)) * kDropPx;
  b.anim->apply(*b.skel);
  b.skel->updateWorldTransform();
  const float tint[4] = {1.f, 1.f, 1.f, alpha};
  static std::vector<spine::Batch> batches;
  batches.clear();
  b.skel->render(batches, b.ox, b.oy + dy, b.scale, false, tint);
  for (auto& bt : batches)
    gfx::triangles(R().texture(b.data->pages[bt.page]), reinterpret_cast<const gfx::Vert*>(bt.vertices.data()),
                   (int)bt.vertices.size(), bt.indices.data(), (int)bt.indices.size(), bt.blend == 1);
  return true;
}

}  // namespace ui
