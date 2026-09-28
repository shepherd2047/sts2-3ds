// Split from ui.cpp (F3).
#include "../ui_common.h"

namespace ui {

// ================================================================ reward

// S14: the reward-list strip shared by this screen and the potion/relic offer screens
// (RGDSplus U17) -- a disabled ui/btn_row per resolved run_->rewardItems entry (gold, and
// any potion/relic already taken or skipped). Card entries just mark "a card choice is
// pending" for run.cpp's own bookkeeping and are never shown as a row here (the card grid /
// large-card preview already represents that step). Ids are throwaway: every row is drawn
// disabled, so widgets::hit() never registers them.
void App::drawRewardList(bool top, float y0, bool excludeLast) {
  Run& r = *run_;
  int n = (int)r.rewardItems.size();
  int end = excludeLast ? n - 1 : n;
  float x = top ? (kTop - 304) / 2.f : (kBot - 304) / 2.f, w = 304, y = y0;
  // Rows shared between screens with different room (drawReward has less of it, sharing the
  // top screen with the card list header/footer): stop with a "+N" row rather than overflow.
  const int maxRows = 3;
  int shown = 0, extra = 0;
  for (int i = 0; i < end; ++i) {
    auto& it = r.rewardItems[i];
    std::string icon, label, value;
    switch (it.kind) {
      case Run::RewardKind::Gold:
        icon = "ui/reward_money";
        label = num(it.gold) + " 金币";
        value = "已领取";
        break;
      case Run::RewardKind::Potion:
        icon = "potion/" + it.label;
        label = it.label.empty() ? "" : L("potions." + it.label + ".title");
        value = "已处理";  // taken or skipped -- this log doesn't track which
        break;
      case Run::RewardKind::Relic:
        icon = it.label.empty() ? "" : "relic/" + it.label;
        label = it.label.empty() ? "?" : L("relics." + it.label + ".title");
        value = "已处理";
        break;
      default:
        continue;  // Card: no row, the card grid / preview is the current step
    }
    if (label.empty()) continue;
    if (shown >= maxRows) { ++extra; continue; }
    widgets::row(900 + i, x, y, w, icon, label, value, /*enabled=*/false);
    y += style::kRowH + style::kGap;
    ++shown;
  }
  if (extra > 0) widgets::row(999, x, y, w, "", "+" + num(extra) + " 更多", "", /*enabled=*/false);
}

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
      R().text(kTop / 2, 26, "战斗胜利！", t);
      gfx::rect(kTop / 2.f - 70, 50, 140, 2, style::kPanelHi);
      // S14: everything this reward sequence has already granted (gold, potion, relic).
      drawRewardList(true, 62, /*excludeLast=*/false);
      R().text(kTop / 2, 200, L("gameplay_ui.COMBAT_REWARD_ADD_CARD"), ts(F16, col::white, CENTER));
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
  // S14: skip / detail / select as nine-slice widget buttons (F3), not flat fills. Guarded by
  // waiting(): updateReward() may already have resolved the choice this same frame (BTN_A/B),
  // and a widget button that happens to hold pad focus must not fire a second time.
  widgets::beginFrame(gfx::input());
  if (widgets::button(1, 10, style::kActionY, 110, style::kButtonH, L("gameplay_ui.CHOOSE_CARD_SKIP_BUTTON")) &&
      r.rewardChoice.waiting())
    r.rewardChoice.fire(-1);
  if (widgets::button(2, 122, style::kActionY, 76, style::kButtonH, "详情", widgets::Kind::Secondary,
                      sel_ >= 0 && sel_ < n))
    { detailCard_ = r.rewardCards[sel_].get(); detailUpgrade_ = false; }
  if (widgets::button(3, kBot - 120, style::kActionY, 110, style::kButtonH, "选择", widgets::Kind::Primary,
                      sel_ >= 0 && sel_ < n) &&
      r.rewardChoice.waiting())
    r.rewardChoice.fire(sel_);
  widgets::endFrame();
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
  }
}

}  // namespace ui
