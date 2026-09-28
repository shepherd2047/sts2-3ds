// Split from ui.cpp (F3).
#include "../ui_common.h"

namespace ui {

// ================================================================ relics

// Elite relic reward / treasure chest: the relic is shown large on top (RGDSplus
// U19/U23: focus on top, take/skip below). S14: when this offer is part of the post-combat
// reward sequence (run_->rewardItems non-empty, i.e. not a treasure chest / ancient / event
// relic), the reward-list strip shows what has already been resolved, excluding this relic
// itself (the last entry -- it's the one currently offered, not yet resolved).
void App::drawRelicOffer(bool top) {
  Run& r = *run_;
  Relic* rel = r.relicOffer.get();
  if (top) {
    drawSceneBg(true, 0.6f);
    drawTopBar();
    TextStyle t = ts(F16, col::gold, CENTER);
    R().text(kTop / 2, 24, r.relicOfferFromChest ? "宝箱" : "精英战利品", t);
    if (rel) drawRelicDetail(rel, 88);
    if (!r.relicOfferFromChest && !r.rewardItems.empty()) drawRewardList(true, 180, /*excludeLast=*/true);
    return;
  }
  drawSceneBg(false, 0.55f);
  if (r.relicOfferFromChest) {
    Sprite chest = R().sprite("map/chest");
    spr(chest, kBot / 2 - 22, 18, 44, 44);
  }
  if (rel) {
    float s = 40, x = kBot / 2 - s / 2, y = 74;
    gfx::circle(kBot / 2.f, y + s / 2, 30, 0xFFE07040);
    drawRelicIcon(rel, x, y, s);
    R().text(kBot / 2, y + s + 8, L("relics." + rel->locKey + ".title"), ts(F16, col::white, CENTER));
  }
  // S14: nine-slice widget buttons (F3) in place of the flat-fill legacy button().
  widgets::beginFrame(gfx::input());
  if (widgets::button(1, 10, style::kActionY, 110, style::kButtonH, "跳过") && r.relicChoice.waiting())
    r.relicChoice.fire(0);
  if (widgets::button(2, kBot - 120, style::kActionY, 110, style::kButtonH, "拿取", widgets::Kind::Primary,
                      rel != nullptr) &&
      r.relicChoice.waiting())
    r.relicChoice.fire(1);
  widgets::endFrame();
}

void App::updateRelicOffer(const gfx::Input& in) {
  Run& r = *run_;
  if (!r.relicChoice.waiting()) return;
  if (in.down & gfx::BTN_A) { r.relicChoice.fire(1); return; }
  if (in.down & gfx::BTN_B) { r.relicChoice.fire(0); return; }
}

// Owned relics: a grid below, the selected one described above (RGDSplus U24/U25).
void App::drawRelics(bool top) {
  auto& rels = run_->relics;
  int n = (int)rels.size();
  if (sel_ >= n) sel_ = n - 1;
  if (top) {
    drawSceneBg(true, 0.7f);
    drawTopBar();
    if (sel_ >= 0) drawRelicDetail(rels[sel_].get(), 84);
    else R().text(kTop / 2, 100, "遗物（" + num(n) + " 个）", ts(F16, col::gold, CENTER));
    return;
  }
  drawSceneBg(false, 0.65f);
  const int cols = 6;
  const float cell = 48, x0 = (kBot - cols * cell) / 2, y0 = 8;
  for (int i = 0; i < n; ++i) {
    float x = x0 + (i % cols) * cell, y = y0 + (i / cols - scroll_) * cell;
    if (y < 0 || y > 190) continue;
    if (i == sel_) gfx::rect(x + 2, y + 2, cell - 4, cell - 4, 0xFFE07060);
    drawRelicIcon(rels[i].get(), x + 6, y + 6, cell - 12);
    hits_.push_back({x, y, cell, cell, ID_RELIC0 + i});
  }
  gfx::rect(0, 196, kBot, 44, 0x000000A0);
  button(10, 200, 100, 34, "返回", ID_BACK);
  button(118, 200, 84, 34, "详情", ID_DETAIL, sel_ >= 0 && sel_ < n);
  button(kBot - 110, 200, 100, 34, "牌组", ID_DECK);
}

void App::updateRelics(const gfx::Input& in) {
  int n = (int)run_->relics.size();
  auto close = [&] { relicsOpen_ = false; sel_ = -1; scroll_ = 0; };
  if (in.down & gfx::BTN_RIGHT) sel_ = std::min(n - 1, sel_ + 1);
  if (in.down & gfx::BTN_LEFT) sel_ = std::max(0, sel_ - 1);
  if (in.down & gfx::BTN_DOWN) sel_ = std::min(n - 1, sel_ + 6);
  if (in.down & gfx::BTN_UP) sel_ = std::max(0, sel_ - 6);
  if (sel_ >= 0) scroll_ = std::max(0, sel_ / 6 - 2);
  if (in.down & gfx::BTN_B) { close(); return; }
  if ((in.down & gfx::BTN_A) && sel_ >= 0 && sel_ < n) { detailRelic_ = run_->relics[sel_].get(); return; }
  if (in.touchDown) {
    int id = hitAt(in.tx, in.ty);
    if (id >= ID_RELIC0 && id < ID_RELIC0 + n) {
      int picked = id - ID_RELIC0;
      if (sel_ == picked) { detailRelic_ = run_->relics[picked].get(); return; }
      sel_ = picked;
    }
    if (id == ID_DETAIL && sel_ >= 0 && sel_ < n) { detailRelic_ = run_->relics[sel_].get(); return; }
    if (id == ID_BACK) close();
    if (id == ID_DECK) { close(); openCardList(CardListMode::Deck); }
  }
}

}  // namespace ui
