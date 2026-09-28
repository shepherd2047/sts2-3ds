#include "widgets.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace ui::widgets {

using namespace style;

namespace {

struct Box { int id; float x, y, w, h; };

gfx::Input curInput;
std::vector<Box> curHits, prevHits;  // this frame's / last frame's registered boxes
int focusId = -1;
bool padMode = false;
float clock_ = 0;

// Per-id transient state (press animation, toggle knobs, etc.), keyed by id so it survives
// across frames without the caller owning storage.
struct State { float press = 0, hoverT = 0; };
std::map<int, State> states;

Res& R() { return res(); }

TextStyle ts(FontSize f = F12, uint32_t c = col::white, Align a = LEFT, float maxW = 0, float scale = 1.f) {
  TextStyle t;
  t.size = f; t.color = c; t.align = a; t.maxWidth = maxW; t.scale = scale;
  return t;
}

void spr(const Sprite& s, float x, float y, float w = -1, float h = -1, uint32_t tint = 0xFFFFFFFF) {
  if (s) gfx::image(s.tex, s.x, s.y, s.w, s.h, x, y, w < 0 ? s.w : w, h < 0 ? s.h : h, tint);
}

void nine(const Sprite& s, float x, float y, float w, float h, uint32_t tint = 0xFFFFFFFF) {
  if (s) gfx::nineSlice(s.tex, s.x, s.y, s.w, s.h, (float)s.nl, (float)s.nt, (float)s.nr, (float)s.nb, x, y, w, h, tint);
}

// Nearest previous-frame box in `dir` (0 right, 1 left, 2 up, 3 down) from the focused box.
int navigate(uint32_t downBtn) {
  if (prevHits.empty()) return focusId;
  const Box* cur = nullptr;
  for (auto& b : prevHits) if (b.id == focusId) { cur = &b; break; }
  float cx = cur ? cur->x + cur->w / 2 : 0, cy = cur ? cur->y + cur->h / 2 : 0;
  float dx = 0, dy = 0;
  if (downBtn & gfx::BTN_RIGHT) dx = 1;
  else if (downBtn & gfx::BTN_LEFT) dx = -1;
  else if (downBtn & gfx::BTN_DOWN) dy = 1;
  else if (downBtn & gfx::BTN_UP) dy = -1;
  else return focusId;
  const Box* best = nullptr;
  float bestScore = 1e18f;
  for (auto& b : prevHits) {
    if (cur && b.id == cur->id) continue;
    float bx = b.x + b.w / 2, by = b.y + b.h / 2;
    float vx = bx - cx, vy = by - cy;
    float along = vx * dx + vy * dy;       // distance in the requested direction
    float across = std::abs(vx * dy - vy * dx);  // perpendicular drift, penalised
    if (!cur) { along = -(std::abs(vx) + std::abs(vy)); across = 0; }  // no focus yet: nearest to origin-ish
    else if (along <= 0.5f) continue;
    float score = along + across * 2.5f;
    if (score < bestScore) { bestScore = score; best = &b; }
  }
  return best ? best->id : focusId;
}

}  // namespace

void beginFrame(const gfx::Input& in) {
  curInput = in;
  curHits.clear();
  clock_ += 1.f / 60;
  if (in.down & (gfx::BTN_LEFT | gfx::BTN_RIGHT | gfx::BTN_UP | gfx::BTN_DOWN)) {
    padMode = true;
    focusId = navigate(in.down);
  }
  if (in.touchDown) padMode = false;
}

void endFrame() {
  prevHits = curHits;
  for (auto it = states.begin(); it != states.end();) {
    it->second.press = std::max(0.f, it->second.press - 1.f / 60 / kPress);
    ++it;
  }
}

int focused() { return focusId; }
void setFocus(int id) { focusId = id; }
bool usingPad() { return padMode; }

bool hit(int id, float x, float y, float w, float h, bool enabled) {
  if (!enabled) return false;
  curHits.push_back({id, x, y, w, h});
  bool activated = false;
  if (curInput.touchDown && curInput.tx >= x && curInput.tx < x + w && curInput.ty >= y && curInput.ty < y + h) {
    setFocus(id);
    padMode = false;
    activated = true;
    states[id].press = 1.f;
  }
  if (padMode && id == focusId && (curInput.down & gfx::BTN_A)) {
    activated = true;
    states[id].press = 1.f;
  }
  return activated;
}

void panel(const std::string& spriteName, float x, float y, float w, float h, uint32_t tint) {
  Sprite s = R().sprite(spriteName);
  if (s && s.nl + s.nt + s.nr + s.nb > 0) { nine(s, x, y, w, h, tint); return; }
  if (s) { spr(s, x, y, w, h, tint); return; }
  gfx::rect(x, y, w, h, kPanel);
  gfx::rect(x, y, w, 1, kPanelEdge);
  gfx::rect(x, y + h - 1, w, 1, kPanelEdge);
  gfx::rect(x, y, 1, h, kPanelEdge);
  gfx::rect(x + w - 1, y, 1, h, kPanelEdge);
}

void focusRing(int id, float x, float y, float w, float h) {
  if (!padMode || id != focusId) return;
  float pulse = 0.75f + 0.25f * std::sin(clock_ * 6.2831853f / kFocusPulse);
  uint32_t c = (kFocus & 0xFFFFFF00u) | (uint32_t)(0xFF * pulse);
  const float t = 2;
  gfx::rect(x - t, y - t, w + 2 * t, t, c);
  gfx::rect(x - t, y + h, w + 2 * t, t, c);
  gfx::rect(x - t, y - t, t, h + 2 * t, c);
  gfx::rect(x + w, y - t, t, h + 2 * t, c);
}

// ---------------------------------------------------------------- button

bool button(int id, float x, float y, float w, float h, const std::string& label, Kind kind, bool enabled,
            const char* rightLabel) {
  bool activated = hit(id, x, y, w, h, enabled);
  float press = states.count(id) ? states[id].press : 0;
  float oy = press > 0.3f ? 1.f : 0.f;
  const char* art = kind == Kind::Primary ? "ui/btn_proceed" : kind == Kind::Danger ? "ui/btn_cancel_s"
                    : kind == Kind::Row ? "ui/btn_row" : kind == Kind::Event ? "ui/btn_event"
                    : kind == Kind::Ancient ? "ui/btn_ancient" : "ui/btn_confirm";
  Sprite s = R().sprite(art);
  uint32_t tint = !enabled ? 0x808080FF : padMode && id == focusId ? 0xFFFFFFFF : 0xF0F0F0FF;
  float blend = !enabled ? 0.55f : 0.f;
  if (s && s.nl + s.nt + s.nr + s.nb > 0) {
    gfx::nineSlice(s.tex, s.x, s.y, s.w, s.h, (float)s.nl, (float)s.nt, (float)s.nr, (float)s.nb, x, y + oy, w, h,
                  tint, blend);
  } else {
    uint32_t fill = !enabled ? kPlateOff : kind == Kind::Primary ? kPrimary : kind == Kind::Danger ? kDanger : kPlate;
    gfx::rect(x, y + oy, w, h, fill);
    gfx::rect(x, y + oy, w, 1, enabled ? kEdge : kEdgeOff);
    gfx::rect(x, y + oy + h - 1, w, 1, enabled ? kEdge : kEdgeOff);
    gfx::rect(x, y + oy, 1, h, enabled ? kEdge : kEdgeOff);
    gfx::rect(x + w - 1, y + oy, 1, h, enabled ? kEdge : kEdgeOff);
  }
  TextStyle st = ts(F16, enabled ? col::white : col::gray, rightLabel ? LEFT : CENTER);
  float th, tw = R().measure(label, st, &th);
  if (tw > w - 10) { st.scale = (w - 10) / tw; th *= st.scale; }
  float tx = rightLabel ? x + 8 : x + w / 2;
  R().text(tx, y + oy + (h - th) / 2, label, st);
  if (rightLabel) R().text(x + w - 8, y + oy + (h - th) / 2, rightLabel, ts(F12, col::gold, RIGHT));
  focusRing(id, x, y, w, h);
  return activated;
}

bool iconButton(int id, float x, float y, float size, const std::string& iconSprite, bool enabled, bool notify) {
  bool activated = hit(id, x, y, size, size, enabled);
  Sprite bg = R().sprite("ui/btn_row");
  uint32_t tint = enabled ? 0xFFFFFFFF : 0x808080FF;
  if (bg && bg.nl + bg.nt + bg.nr + bg.nb > 0)
    gfx::nineSlice(bg.tex, bg.x, bg.y, bg.w, bg.h, (float)bg.nl, (float)bg.nt, (float)bg.nr, (float)bg.nb, x, y, size,
                  size, tint, enabled ? 0.f : 0.55f);
  Sprite ic = R().sprite(iconSprite);
  float pad = size * 0.16f;
  spr(ic, x + pad, y + pad, size - 2 * pad, size - 2 * pad, tint);
  if (notify) { Sprite d = R().sprite("ui/notify_dot"); spr(d, x + size - 10, y - 2, 12, 12); }
  focusRing(id, x, y, size, size);
  return activated;
}

bool row(int id, float x, float y, float w, const std::string& iconSprite, const std::string& label,
        const std::string& rightValue, bool enabled, uint32_t rightColor) {
  const float h = kRowH;
  bool activated = hit(id, x, y, w, h, enabled);
  bool focus = padMode && id == focusId;
  panel(focus ? "ui/btn_row" : "ui/btn_row", x, y, w, h, enabled ? (focus ? 0xFFFFFFFF : 0xE8E8E8FF) : 0x808080FF);
  if (focus) gfx::rect(x, y, w, h, kSelectedFill);
  Sprite ic = R().sprite(iconSprite);
  if (ic) spr(ic, x + 4, y + 2, h - 4, h - 4, enabled ? 0xFFFFFFFF : 0x808080FF);
  float labelX = ic ? x + h : x + 10;
  R().text(labelX, y + (h - R().lineHeight(F16)) / 2, label, ts(F16, enabled ? col::white : col::gray));
  if (!rightValue.empty()) R().text(x + w - 8, y + (h - R().lineHeight(F12)) / 2, rightValue,
                                    ts(F12, enabled ? rightColor : col::gray, RIGHT));
  focusRing(id, x, y, w, h);
  return activated;
}

bool optionButton(int id, float x, float y, float w, float h, const std::string& label, bool enabled,
                  const std::string& lockedReason, bool ancient) {
  bool activated = hit(id, x, y, w, h, enabled);
  panel(ancient ? "ui/btn_ancient" : "ui/btn_event", x, y, w, h, enabled ? 0xFFFFFFFF : 0x808080FF);
  TextStyle st = ts(F16, enabled ? col::white : col::gray, CENTER, w - 16);
  float th;
  R().measure(label, st, &th);
  R().text(x + w / 2, y + (h - th) / 2 - (enabled || lockedReason.empty() ? 0 : 4), label, st);
  if (!enabled && !lockedReason.empty()) R().text(x + w / 2, y + h - 2, lockedReason, ts(F12, col::red, CENTER));
  focusRing(id, x, y, w, h);
  return activated;
}

int tabs(int baseId, float x, float y, float w, float h, const std::vector<std::string>& labels, int sel) {
  int n = (int)labels.size();
  if (n == 0) return sel;
  float tw = w / n;
  for (int i = 0; i < n; ++i) {
    float tx = x + i * tw;
    bool selected = i == sel;
    if (hit(baseId + i, tx, y, tw, h, true)) sel = i;
    Sprite art = R().sprite(selected ? "ui/tab_selected" : "ui/tab_stroke");
    if (art) nine(art, tx, y, tw - 2, h);
    else { gfx::rect(tx, y, tw - 2, h, selected ? kPlateHover : kPlate); gfx::rect(tx, y + h - 2, tw - 2, 2, kPanelHi); }
    R().text(tx + (tw - 2) / 2, y + (h - R().lineHeight(F12)) / 2, labels[i], ts(F12, selected ? col::gold : col::white, CENTER));
    focusRing(baseId + i, tx, y, tw - 2, h);
  }
  return sel;
}

bool toggle(int id, float x, float y, bool value, const std::string& label) {
  bool activated = hit(id, x, y, kIconBtn, kIconBtn, true);
  if (activated) value = !value;
  Sprite s = R().sprite(value ? "ui/checkbox_on" : "ui/checkbox_off");
  spr(s, x, y, kIconBtn, kIconBtn);
  if (!label.empty()) R().text(x + kIconBtn + 6, y + (kIconBtn - R().lineHeight(F12)) / 2, label, ts(F12, col::white));
  focusRing(id, x, y, kIconBtn, kIconBtn);
  return value;
}

float slider(int id, float x, float y, float w, float value, float step) {
  const float h = kIconBtn;
  hit(id, x, y, w, h, true);
  bool focus = padMode && id == focusId;
  if (focus && (curInput.down & gfx::BTN_RIGHT)) value = std::min(1.f, value + step);
  if (focus && (curInput.down & gfx::BTN_LEFT)) value = std::max(0.f, value - step);
  if (curInput.touching && curInput.tx >= x && curInput.tx < x + w && curInput.ty >= y && curInput.ty < y + h)
    value = std::clamp((curInput.tx - x) / w, 0.f, 1.f);
  gfx::rect(x, y + h / 2 - 3, w, 6, kPlateOff);
  gfx::rect(x, y + h / 2 - 3, w * value, 6, kPrimary);
  float knobX = x + w * value - 8;
  gfx::rect(knobX, y + h / 2 - 8, 16, 16, kFocus);
  focusRing(id, x, y, w, h);
  return value;
}

int paginator(int id, float x, float y, float w, int page, int total) {
  const float h = kIconBtn, arrowW = 24;
  if (hit(id, x, y, arrowW, h, page > 0)) --page;
  Sprite la = R().sprite("ui/arrow_left");
  spr(la, x + 4, y + (h - la.h) / 2, -1, -1, page > 0 ? 0xFFFFFFFF : 0x606060FF);
  focusRing(id, x, y, arrowW, h);
  std::string mid = std::to_string(page + 1) + " / " + std::to_string(std::max(1, total));
  R().text(x + w / 2, y + (h - R().lineHeight(F12)) / 2, mid, ts(F12, col::white, CENTER));
  if (hit(id + 1, x + w - arrowW, y, arrowW, h, page + 1 < total)) ++page;
  Sprite ra = R().sprite("ui/arrow_right");
  spr(ra, x + w - arrowW + 4, y + (h - ra.h) / 2, -1, -1, page + 1 < total ? 0xFFFFFFFF : 0x606060FF);
  focusRing(id + 1, x + w - arrowW, y, arrowW, h);
  return page;
}

// ---------------------------------------------------------------- scroll list

void ScrollList::begin(int id_, float x_, float y_, float w_, float h_, float contentH) {
  id = id_; x = x_; y = y_; w = w_; h = h_;
  float maxScroll = std::max(0.f, contentH - h);
  bool inside = curInput.tx >= x && curInput.tx < x + w && curInput.ty >= y && curInput.ty < y + h;
  if (curInput.touchDown && inside) { dragging = true; vel = 0; }
  if (dragging) {
    if (curInput.touching) {
      static std::map<int, float> lastY;
      float last = everSet ? lastY[id] : curInput.ty;
      float dy = curInput.ty - last;
      scroll = std::clamp(scroll - dy, 0.f, maxScroll);
      vel = -dy;
      lastY[id] = (float)curInput.ty;
      everSet = true;
    } else {
      dragging = false;
      everSet = false;
    }
  } else {
    scroll += vel;
    vel *= 0.9f;
    scroll = std::clamp(scroll, 0.f, maxScroll);
    if (scroll <= 0.f || scroll >= maxScroll) vel = 0;
  }
  gfx::pushClip(x, y, w, h);
}

void ScrollList::end() {
  gfx::popClip();
  float maxScroll = std::max(0.f, 0.f);  // thumb only meaningful with contentH remembered by caller via draws
  (void)maxScroll;
  // Scrollbar thumb: drawn by the caller knowing contentH would duplicate state, so a minimal
  // track is drawn here only when there has been any scroll this session (best-effort visual).
  if (scroll > 0.f || vel != 0.f) {
    Sprite track = R().sprite("ui/scroll_track");
    if (track) nine(track, x + w - 6, y, 6, h);
  }
}

// ---------------------------------------------------------------- modal / toast / banner

int modal(int id, const std::string& title, const std::string& message, bool danger) {
  gfx::rect(0, 0, style::kMargin > 0 ? 320 : 320, 240, 0x000000B0);  // full bottom-screen scrim
  const float w = 260, h = 150, x = (320 - w) / 2, y = (240 - h) / 2;
  panel("ui/panel_popup", x, y, w, h);
  R().text(x + w / 2, y + 14, title, ts(F16, col::gold, CENTER, 0, 1.1f));
  R().text(x + w / 2, y + 44, message, ts(F12, col::white, CENTER, w - 24));
  int result = 0;
  if (button(id * 2, x + 16, y + h - 42, (w - 40) / 2, kButtonH, "取消", Kind::Secondary)) result = -1;
  if (button(id * 2 + 1, x + w / 2 + 8, y + h - 42, (w - 40) / 2, kButtonH, "确认", danger ? Kind::Danger : Kind::Primary))
    result = 1;
  return result;
}

namespace {
struct Toast { std::string text; float t; };
std::vector<Toast> toasts;
}  // namespace

void toast(const std::string& text) { toasts.push_back({text, kToast}); }

void drawToasts(float dt) {
  for (auto it = toasts.begin(); it != toasts.end();) {
    it->t -= dt;
    if (it->t <= 0) { it = toasts.erase(it); continue; }
    float a = std::min(1.f, it->t * 3.f);
    gfx::pushAlpha(a);
    float w = R().measure(it->text, ts(F12)) + 16;
    float y = 112 + (float)(it - toasts.begin()) * 22;
    gfx::rect((320 - w) / 2, y, w, 20, 0x000000C0);
    R().text(160, y + 3, it->text, ts(F12, col::gold, CENTER));
    gfx::popAlpha();
    ++it;
  }
}

void banner(float cx, float y, const std::string& text, float scale) {
  Sprite b = R().sprite("ui/reward_banner");
  if (b) spr(b, cx - b.w * scale / 2, y, b.w * scale, b.h * scale);
  R().text(cx, y + (b ? b.h * scale : 20) / 2 - R().lineHeight(F16) * scale / 2, text, ts(F16, col::dark, CENTER, 0, scale));
}

}  // namespace ui::widgets
