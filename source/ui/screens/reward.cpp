// Split from ui.cpp (F3).
#include "../ui_common.h"

namespace ui {

// ================================================================ reward

// S14 (RGDSplus U17, C# NRewardsScreen): the interactive reward list. run_->rewardItems holds
// every reward of the room (real payloads, nothing granted yet, see the ordering comment on
// Run::combatRewards); tapping a row claims it (fires rewardListChoice with that row's index),
// which actually grants it and removes the row on success, or leaves it in place if the claim
// failed (a Card row that was skipped keeps its same options; a Potion row stays if the belt is
// full). "继续" (Proceed) leaves with whatever is left unclaimed, forfeited, matching the C#.
//
// A Card row reuses the existing card grid as a nested sub-screen: run_->rewardCards is empty in
// list mode and holds that row's options while it is open (Run::combatRewards moves them in and,
// if skipped, back out) -- drawReward/updateReward branch on that instead of adding new state.
void App::drawReward(bool top) {
  Run& r = *run_;
  bool cardMode = !r.rewardCards.empty();
  int n = cardMode ? (int)r.rewardCards.size() : (int)r.rewardItems.size();

  if (cardMode) {
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
    return;
  }

  // ---- list mode ----
  if (top) {
    gfx::Texture* bg = R().texture(actTexture(*run_, "bg_"));
    gfx::image(bg, 0, 0, kTop, kH, 0, 0, kTop, kH, 0x000000FF, 0.55f);
    drawTopBar();
    TextStyle t = ts(F16, col::gold, CENTER);
    t.scale = 1.6f;
    R().text(kTop / 2, 60, "战斗胜利！", t);
    gfx::rect(kTop / 2.f - 70, 84, 140, 2, style::kPanelHi);
    R().text(kTop / 2, 112, n > 0 ? "点选下方的奖励，领取后继续" : "没有更多奖励了", ts(F16, col::white, CENTER));
    return;
  }
  drawSceneBg(false, 0.55f);
  const float x = style::kMargin, w = kBot - 2 * style::kMargin;
  float y = 4;
  widgets::beginFrame(gfx::input());
  for (int i = 0; i < n; ++i) {
    auto& item = r.rewardItems[i];
    std::string icon, label, value;
    bool enabled = true;
    switch (item.kind) {
      case Run::RewardKind::Gold:
        icon = "ui/reward_money";
        label = num(item.gold) + " 金币";
        break;
      case Run::RewardKind::Potion:
        icon = "potion/" + item.potion->locKey;
        label = item.potion ? L("potions." + item.potion->locKey + ".title") : "?";
        enabled = r.hasOpenPotionSlot();
        if (!enabled) value = "药水栏已满";
        break;
      case Run::RewardKind::Relic:
        icon = item.relic ? "relic/" + item.relic->locKey : "";
        label = item.relic ? L("relics." + item.relic->locKey + ".title") : "?";
        break;
      case Run::RewardKind::Card:
        icon = "ui/reward_card";
        label = L("gameplay_ui.COMBAT_REWARD_ADD_CARD");
        break;
    }
    if (widgets::row(100 + i, x, y, w, icon, label, value, enabled) && r.rewardListChoice.waiting())
      r.rewardListChoice.fire(i);
    y += style::kRowH + style::kGap;
  }
  if (widgets::button(999, kBot - 120, style::kActionY, 110, style::kButtonH, "继续", widgets::Kind::Primary) &&
      r.rewardListChoice.waiting())
    r.rewardListChoice.fire(-1);
  widgets::endFrame();
}

void App::updateReward(const gfx::Input& in) {
  Run& r = *run_;
  bool cardMode = !r.rewardCards.empty();
  if (cardMode && !rewardCardOpen_) sel_ = -1;  // a card row just opened: nothing focused yet
  rewardCardOpen_ = cardMode;
  if (!cardMode) return;  // list mode: fully widget-driven from drawReward
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
