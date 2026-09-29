// Split from ui.cpp (F3).
#include "../ui_common.h"

namespace ui {

// ================================================================ rest

void App::drawRest(bool top) {
  Run& r = *run_;
  int heal = (Dec(r.player->maxHp) * Dec::lit(0.3)).toInt();
  if (top) {
    gfx::Texture* bg = R().texture(actTexture(*run_, "bg_"));
    gfx::image(bg, 0, 0, kTop, kH, 0, 0, kTop, kH, 0x100400FF, 0.6f);
    gfx::circle(200, 200, 40, 0xFF802040);
    gfx::circle(200, 204, 22, 0xFFB04060);
    Sprite ic = R().sprite("creature/" + playerArt(run_.get()));
    spr(ic, 120 - ic.ax, 206 - ic.ay);
    TextStyle t = ts(F16, col::gold, CENTER);
    t.scale = 1.4f;
    R().text(kTop / 2, 40, L("map.LEGEND_REST.hoverTip.title"), t);
    R().text(kTop / 2, 76, L("rest_site_ui.PROMPT"), ts(F16, col::white, CENTER));
    drawTopBar();
    return;
  }
  drawSceneBg(false, 0.5f);
  // Options (RestSiteRoom): heal, smith, and Lift / Dig / Cook / Kindle / Clone from relics; 2 per row.
  auto& opts = r.restOptions;
  int n = (int)opts.size();
  // Four rows (7 options: all relic extras + PaelsGrowth's Clone) must end above the bottom buttons (y 196).
  const float bw = 138, bh = n > 6 ? 26 : n > 4 ? 36 : n > 2 ? 64 : 100, gapY = n > 6 ? 1 : n > 4 ? 4 : 8;
  for (int i = 0; i < n; ++i) {
    int o = opts[i];
    float x = i % 2 ? 166 : 16, y = 20 + (i / 2) * (bh + gapY + 16);
    bool used = std::find(r.restUsed.begin(), r.restUsed.end(), o) != r.restUsed.end();
    static const char* keys[] = {"OPTION_HEAL", "OPTION_SMITH", "OPTION_LIFT", "OPTION_DIG", "OPTION_COOK", "OPTION_KINDLE", "OPTION_CLONE"};
    button(x, y, bw, bh, L(std::string("rest_site_ui.") + keys[o] + ".name"), ID_GRID0 + o, r.restChoice.waiting() && restValid(o), sel_ == o);
    static const char* subs[] = {"", "升级一张牌", "战斗开始时 +1 力量", "挖出一件遗物", "移除 2 张牌，+5 最大生命", "南瓜灯 +5 场战斗", "复制所有克隆牌"};
    std::string sub = o == 0 ? "回复 " + num(heal) + " 点生命" : std::string(subs[o]);
    R().text(x + bw / 2, y + bh + 2, used ? std::string("已使用") : sub, ts(F12, used ? col::gray : o == 0 ? col::green : col::gold, CENTER));
  }
  if (r.restUsed.empty()) R().text(80, 206, "生命 " + num(r.player->hp) + "/" + num(r.player->maxHp), ts(F16, col::red, CENTER));
  // RGDSplus U22: pick an option, then confirm. With Miniature Tent, 离开 ends the visit.
  if (!r.restUsed.empty()) button(kBot - 240, 196, 110, 36, "离开", ID_BACK, r.restChoice.waiting());
  button(kBot - 120, 196, 110, 36, "确认", ID_CONFIRM, r.restChoice.waiting() && sel_ >= 0 && restValid(sel_), true);
}

bool App::restValid(int o) const {
  const Run& r = *run_;
  if (std::find(r.restOptions.begin(), r.restOptions.end(), o) == r.restOptions.end()) return false;
  if (std::find(r.restUsed.begin(), r.restUsed.end(), o) != r.restUsed.end()) return false;
  if (o == 1) {
    for (auto& c : r.deck) if (c->upgradable()) return true;
    return false;
  }
  if (o == 4) return r.deck.size() >= 2;
  return true;
}

void App::updateRest(const gfx::Input& in) {
  Run& r = *run_;
  if (!r.restChoice.waiting()) return;
  auto& opts = r.restOptions;
  int cur = (int)(std::find(opts.begin(), opts.end(), sel_) - opts.begin());
  if ((in.down & gfx::BTN_RIGHT) && !opts.empty()) sel_ = opts[std::min((int)opts.size() - 1, cur >= (int)opts.size() ? 0 : cur + 1)];
  if ((in.down & gfx::BTN_LEFT) && !opts.empty()) sel_ = opts[std::max(0, cur >= (int)opts.size() ? 0 : cur - 1)];
  if ((in.down & gfx::BTN_A) && restValid(sel_)) { r.restChoice.fire(sel_); return; }
  if (in.touchDown) {
    int id = hitAt(in.tx, in.ty);
    int pick = id >= ID_GRID0 && id < ID_GRID0 + 7 ? id - ID_GRID0 : -1;
    if (pick >= 0) {
      if (sel_ == pick && restValid(pick)) { r.restChoice.fire(pick); return; }  // second tap confirms
      sel_ = pick;
    }
    if (id == ID_CONFIRM && restValid(sel_)) r.restChoice.fire(sel_);
    if (id == ID_BACK && !r.restUsed.empty()) { sel_ = -1; r.restChoice.fire(-1); }
  }
}

void App::drawUpgrade(bool top) {
  Run& r = *run_;
  auto& opts = r.upgradeOptions;
  if (top) {
    drawSceneBg(true, 0.7f);
    drawTopBar();
    if (sel_ >= 0 && sel_ < (int)opts.size()) {
      Card* c = opts[sel_];
      auto up = c->clone();
      up->upgrade();
      drawCard(c, 50, 36, 1.05f, false, true);
      R().text(kTop / 2, 110, "→", ts(F16, col::gold, CENTER, 0, 2.f));
      drawCard(up.get(), 224, 36, 1.05f, false, true);
    } else {
      R().text(kTop / 2, 100, L("gameplay_ui.CHOOSE_CARD_UPGRADE_HEADER"), ts(F16, col::gold, CENTER));
    }
    return;
  }
  drawSceneBg(false, 0.65f);
  drawCardGrid(opts, sel_, 0, 196, scroll_);
  gfx::rect(0, 196, kBot, 44, 0x000000A0);
  button(10, 200, 100, 34, "返回", ID_BACK);
  button(kBot - 110, 200, 100, 34, "升级", ID_CONFIRM, sel_ >= 0 && sel_ < (int)opts.size(), true);
}

void App::updateUpgrade(const gfx::Input& in) {
  Run& r = *run_;
  if (!r.upgradeChoice.waiting()) return;
  int m = (int)r.upgradeOptions.size();
  if (in.down & gfx::BTN_RIGHT) sel_ = std::min(m - 1, sel_ + 1);
  if (in.down & gfx::BTN_LEFT) sel_ = std::max(0, sel_ - 1);
  if (in.down & gfx::BTN_DOWN) sel_ = std::min(m - 1, sel_ + 5);
  if (in.down & gfx::BTN_UP) sel_ = std::max(0, sel_ - 5);
  if (sel_ >= 0) scroll_ = std::max(0, sel_ / 5 - 1);
  if ((in.down & gfx::BTN_A) && sel_ >= 0) { r.upgradeChoice.fire(sel_); return; }
  if (in.down & gfx::BTN_B) { r.upgradeChoice.fire(-1); return; }
  if (in.touchDown) {
    int id = hitAt(in.tx, in.ty);
    if (id >= ID_GRID0 && id - ID_GRID0 < m) {
      if (sel_ == id - ID_GRID0) { r.upgradeChoice.fire(sel_); return; }
      sel_ = id - ID_GRID0;
    }
    if (id == ID_CONFIRM && sel_ >= 0) r.upgradeChoice.fire(sel_);
    if (id == ID_BACK) r.upgradeChoice.fire(-1);
  }
}

}  // namespace ui
