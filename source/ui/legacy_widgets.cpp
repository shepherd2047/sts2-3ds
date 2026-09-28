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
  uint32_t fill = !enabled ? 0x2A2A2AE0 : highlight ? 0x8A5A20F0 : 0x3A2E24F0;
  uint32_t border = !enabled ? 0x555555FF : highlight ? 0xFFD870FF : 0xB89A60FF;
  panel(x, y, w, h, fill, border);
  gfx::rect(x + 1, y + 1, w - 2, 2, 0xFFFFFF22);
  float th;
  // Large font when it fits on one line, otherwise the small one, then shrink.
  TextStyle st = ts(F16, enabled ? col::white : col::gray, CENTER);
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
