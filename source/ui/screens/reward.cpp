// Split from ui.cpp (F3).
#include "../ui_common.h"

namespace ui {

// ================================================================ reward

void App::drawReward(bool top) {
  Run& r = *run_;
  int n = (int)r.rewardCards.size();
  if (top) {
    gfx::Texture* bg = R().texture(actTexture(*run_, "bg_"));
    gfx::image(bg, 0, 0, kTop, kH, 0, 0, kTop, kH, 0x000000FF, 0.55f);
    drawTopBar();
    if (sel_ >= 0 && sel_ < n) {
      drawCard(r.rewardCards[sel_].get(), (kTop - 132) / 2, 34, 1.1f, false, true);
    } else {
      TextStyle t = ts(F16, col::gold, CENTER);
      t.scale = 1.6f;
      R().text(kTop / 2, 70, "战斗胜利！", t);
      R().text(kTop / 2, 120, L("gameplay_ui.COMBAT_REWARD_ADD_CARD"), ts(F16, col::white, CENTER));
    }
    return;
  }
  drawSceneBg(false, 0.55f);
  R().text(kBot / 2, 4, L("gameplay_ui.CHOOSE_CARD_HEADER"), ts(F16, col::gold, CENTER));
  const float s = 0.78f, cw = 120 * s;
  float gap = (kBot - 3 * cw) / 4;
  for (int i = 0; i < n; ++i) {
    float x = gap + i * (cw + gap), y = 28;
    drawCard(r.rewardCards[i].get(), x, y, s, false, true, i == sel_);
    hits_.push_back({x, y, cw, 169 * s, ID_REWARD0 + i});
  }
  button(10, 196, 110, 36, L("gameplay_ui.CHOOSE_CARD_SKIP_BUTTON"), ID_SKIP);
  button(122, 196, 76, 36, "详情", ID_DETAIL, sel_ >= 0 && sel_ < n);
  button(kBot - 120, 196, 110, 36, "选择", ID_CONFIRM, sel_ >= 0 && sel_ < n, true);
}

void App::updateReward(const gfx::Input& in) {
  Run& r = *run_;
  if (!r.rewardChoice.waiting()) return;
  int n = (int)r.rewardCards.size();
  if (in.down & gfx::BTN_RIGHT) sel_ = std::min(n - 1, sel_ + 1);
  if (in.down & gfx::BTN_LEFT) sel_ = std::max(0, sel_ - 1);
  if ((in.down & gfx::BTN_A) && sel_ >= 0) { r.rewardChoice.fire(sel_); return; }
  if (in.down & gfx::BTN_B) { r.rewardChoice.fire(-1); return; }
  if (in.touchDown) {
    int id = hitAt(in.tx, in.ty);
    if (id >= ID_REWARD0 && id < ID_REWARD0 + n) {
      if (sel_ == id - ID_REWARD0) { r.rewardChoice.fire(sel_); return; }
      sel_ = id - ID_REWARD0;
    }
    if (id == ID_CONFIRM && sel_ >= 0) r.rewardChoice.fire(sel_);
    if (id == ID_SKIP) r.rewardChoice.fire(-1);
    if (id == ID_DETAIL && sel_ >= 0 && sel_ < n) { detailCard_ = r.rewardCards[sel_].get(); detailUpgrade_ = false; }
  }
}

}  // namespace ui
