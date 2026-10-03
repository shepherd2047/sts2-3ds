// Split from ui.cpp (F3).
#include "ui_common.h"

namespace ui {

// ================================================================ widgets

void App::panel(float x, float y, float w, float h, uint32_t fill, uint32_t border) {
  gfx::rect(x, y, w, h, fill);
  gfx::rect(x, y, w, 1, border);
  gfx::rect(x, y + h - 1, w, 1, border);
  gfx::rect(x, y, 1, h, border);
  gfx::rect(x + w - 1, y, 1, h, border);
}

bool App::button(float x, float y, float w, float h, const std::string& label, int id, bool enabled, bool highlight) {
  // "<" / ">" (ascension, pagers): the game's yellow triangle (settings_tiny_*_arrow), no plate.
  if (label == "<" || label == ">") {
    Sprite a = R().sprite(label == "<" ? "ui/arrow_left" : "ui/arrow_right");
    if (a) {
      const float k = std::min(w / a.w, h / a.h), aw = a.w * k, ah = a.h * k;
      if (!enabled) gfx::pushAlpha(0.3f);
      spr(a, x + (w - aw) / 2, y + (h - ah) / 2, aw, ah);
      if (!enabled) gfx::popAlpha();
      if (enabled) hits_.push_back({x, y, w, h, id});
      return enabled;
    }
  }
  // Original art: back = blue arrow plate, highlighted = red proceed plate, the rest = confirm plate.
  bool back = id == ID_BACK || label == "返回" || label == "取消" || label == "关闭";
  Sprite sp = R().sprite(back ? "ui/btn_back" : highlight ? "ui/btn_proceed" : "ui/btn_confirm");
  uint32_t tint = !enabled ? 0x808080FF : 0xFFFFFFFF;  // multiply tint only; never citro2d blend
  if (sp && sp.nl + sp.nt + sp.nr + sp.nb > 0)
    gfx::nineSlice(sp.tex, sp.x, sp.y, sp.w, sp.h, (float)sp.nl, (float)sp.nt, (float)sp.nr, (float)sp.nb, x, y, w, h,
                   tint, 0.f);
  else
    panel(x, y, w, h, !enabled ? 0x2A2A2AE0 : 0x3A2E24F0, !enabled ? 0x555555FF : 0xB89A60FF);
  float th;
  // Large font when it fits on one line, otherwise the small one, then shrink.
  TextStyle st = ts(F16, !enabled ? col::gray : highlight && !back ? col::gold : col::white, CENTER);
  float tw = R().measure(label, st, &th);
  if (tw > w - 6 || th > h - 2) {
    st.size = F12;
    tw = R().measure(label, st, &th);
    if (tw > w - 6) { st.scale = (w - 6) / tw; th *= st.scale; }
  }
  R().text(x + w / 2, y + (h - th) / 2, label, st);
  if (enabled) hits_.push_back({x, y, w, h, id});
  return enabled;
}

int App::hitAt(int tx, int ty) {
  // Later registrations are drawn on top, so search backwards.
  for (int i = (int)hits_.size() - 1; i >= 0; --i) {
    auto& h = hits_[i];
    if (tx >= h.x && tx < h.x + h.w && ty >= h.y && ty < h.y + h.h) return h.id;
  }
  return ID_NONE;
}

}  // namespace ui
