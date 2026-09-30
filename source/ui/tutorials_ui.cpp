// M13 tutorials: the FTUE popup (C# NFtue scenes) and the watcher for screen-based triggers. The
// queue / seen flags / gating live in tutorials.cpp (gfx-free, unit-tested).
//
// Layout (3DS): the tip is a text panel on the top screen in the C#'s ftue_popup plate (bulb in
// its corner, gold header, cream body; NCombatRulesFtue's three combat_ftue pictures beside the
// text), the bottom screen is dimmed with the C#'s red 了解了！ ribbon (NFtueConfirmButton) to tap.
// A = 了解了！ / next page, B or ← = previous page, → = next page. The tip owns the input while open
// (the screen under it is frozen, as under the C#'s NModalContainer backstop).
#include "tutorials.h"

#include "ui_common.h"

namespace ui {

namespace {

constexpr float kBtnW = 128, kBtnH = 40, kBtnY = 128;
constexpr float kSideW = 88, kSideH = 34;
constexpr float kImgW = 150, kImgH = 117;
float openT = 0;       // seconds since the tip opened (slide-in)
int pressId = -1;      // bottom button under a touch that started on it

struct Btn { int id; float x, y, w, h; };
// 0 = 了解了！ / next / yes, 1 = previous / no.
std::vector<Btn> buttons(Ftue t, int page) {
  std::vector<Btn> b;
  if (t == Ftue::AcceptTutorials) {
    b.push_back({0, kBot / 2.f + 8, kBtnY, kSideW + 20, kSideH + 6});
    b.push_back({1, kBot / 2.f - 8 - kSideW - 20, kBtnY, kSideW + 20, kSideH + 6});
    return b;
  }
  b.push_back({0, (kBot - kBtnW) / 2, kBtnY, kBtnW, kBtnH});
  if (page > 0) b.push_back({1, 12, kBtnY + (kBtnH - kSideH) / 2, kSideW, kSideH});
  return b;
}

std::string pageCount(int page, int total) {
  std::string s = L("ftues.COMBAT_BASICS_FTUE_PAGE_COUNT");
  auto sub = [&](const std::string& k, int v) {
    size_t p = s.find(k);
    if (p != std::string::npos) s.replace(p, k.size(), std::to_string(v));
  };
  sub("{currentPage}", page + 1);
  sub("{totalPages}", total);
  return s;
}

}  // namespace

bool App::updateTips(const gfx::Input& in) {
  if (tips::current() == Ftue::None) {
    if (!tips::allowed()) return false;
    Screen scr = run_->screen;
    bool clear = !detailOpen() && !settingsOpen_ && !devOpen_ && !pauseOpen_ && !deckOpen_ && !relicsOpen_ &&
                 !potionsOpen_ && !mapView_ && !run_->deckChoice.active && !topBarFocus_ && transitionT_ <= 0 &&
                 (actTitleT_ < 0 || scr != Screen::Map);  // after the act's title card
    if (!clear) return false;
    // The watcher: the C#'s room / screen checks (CombatManager, NMapScreen, NRestSiteRoom,
    // NTreasureRoom + NRewardsScreen.RelicFtueCheck, NPotionContainer, NCardRewardSelectionScreen).
    Combat* cb = run_->combat.get();
    if (scr == Screen::Combat && cb && cb->playerPhase && cb->actions.waiting()) showTip(Ftue::CombatRules);
    if (scr == Screen::Map) showTip(Ftue::MapSelect);
    if (scr == Screen::Rest) showTip(Ftue::RestSite);
    if (scr == Screen::RelicOffer) showTip(Ftue::RelicReward);
    if (scr == Screen::Reward) {
      for (auto& it : run_->rewardItems)
        if (it.kind == Run::RewardKind::Relic) showTip(Ftue::RelicReward);
      for (auto& c : run_->rewardCards)
        if (c && c->type == CardType::Power) showTip(Ftue::PowerCard);
    }
    if (scr != Screen::Title && scr != Screen::GameOver && scr != Screen::Victory)
      for (auto& p : run_->potions)
        if (p) showTip(Ftue::Potion);
    if (!tips::openNext()) return false;
    openT = 0;
    pressId = -1;
    widgets::suspendInput(true);
    return true;  // the frame it opens swallows the input
  }
  widgets::suspendInput(true);
  // The hand keeps dealing under a combat tip (updateCombat, which animates it, is skipped).
  if (run_->screen == Screen::Combat && run_->combat) animateHand((float)gfx::dt() * (fastMode_ ? 1.75f : 1.f));
  Ftue t = tips::current();
  openT += (float)gfx::dt();
  if (openT < 0.15f) return true;  // don't let a held A / tap from the screen below dismiss it at once
  int act = -1;
  auto bs = buttons(t, tips::page());
  if (in.touchDown) {
    pressId = -1;
    for (auto& b : bs)
      if (in.tx >= b.x && in.tx < b.x + b.w && in.ty >= b.y && in.ty < b.y + b.h) pressId = b.id;
  }
  if (in.touchUp && pressId >= 0) {
    for (auto& b : bs)
      if (b.id == pressId && in.tx >= b.x && in.tx < b.x + b.w && in.ty >= b.y && in.ty < b.y + b.h) act = b.id;
    pressId = -1;
  }
  if (in.down & gfx::BTN_A) act = 0;
  if (in.down & gfx::BTN_B) act = 1;
  if (t == Ftue::CombatRules && (in.down & gfx::BTN_RIGHT)) act = 0;
  if (t == Ftue::CombatRules && (in.down & gfx::BTN_LEFT)) act = 1;
  if (act < 0) return true;
  sfx::click();
  if (t == Ftue::AcceptTutorials) {
    tips::answer(act == 0);
    startRun(false);  // the 出发 that asked the question (OnEmbarkPressed runs again)
    return true;
  }
  if (act == 1) {
    tips::back();
  } else if (tips::advance()) {
    widgets::suspendInput(false);
  }
  openT = std::max(openT, 0.15f);
  return true;
}

void App::drawTips(bool top) {
  Ftue t = tips::current();
  if (t == Ftue::None) return;
  float k = easeOut(std::min(1.f, openT / style::kSlide));
  float slide = (1 - k) * 12;
  int page = tips::page();
  const tips::Info& in = tips::info(t);
  if (!top) {
    hits_.clear();  // nothing under the tip is touchable (no click sound from the screen below)
    gfx::rect(0, 0, kBot, kH, 0x000000B8);
    gfx::pushAlpha(k);
    for (auto& b : buttons(t, page)) {
      bool pressed = pressId == b.id;
      std::string label;
      std::string art;
      if (t == Ftue::AcceptTutorials) {
        art = b.id == 0 ? "ui/btn_ok_s" : "ui/btn_cancel_s";
        label = L(b.id == 0 ? "main_menu_ui.GENERIC_POPUP.confirm" : "main_menu_ui.GENERIC_POPUP.cancel");
      } else if (b.id == 0) {
        art = "ui/ftue_btn";
        label = page + 1 < tips::pages(t) ? "下一页" : L("ftues.CONFIRM_BUTTON");
      } else {
        art = "ui/btn_confirm";
        label = "上一页";
      }
      float dy = pressed ? 1 : 0;
      widgets::panel(art, b.x, b.y + slide + dy, b.w, b.h, pressed ? 0xC0C0C0FF : 0xFFFFFFFF);
      R().text(b.x + b.w / 2, b.y + slide + dy + (b.h - R().lineHeight(F16)) / 2, label, ts(F16, col::white, CENTER));
    }
    R().text(kBot / 2, kBtnY + kBtnH + 14 + slide,
             t == Ftue::AcceptTutorials ? "A：确认    B：取消" : tips::pages(t) > 1 ? "A / →：下一页    B / ←：上一页" : "按 A 或点击按钮继续",
             ts(F12, col::gray, CENTER));
    gfx::popAlpha();
    return;
  }
  // ---- top: the ftue_popup panel ----
  gfx::rect(0, 0, kTop, kH, 0x00000070);
  const bool image = t == Ftue::CombatRules;
  const float w = 372, x = (kTop - w) / 2;
  const float padL = 16, padR = 18, headY = 10, bodyY = 40;
  float textX = x + padL + (image ? kImgW + 10 : 0);
  float textW = x + w - padR - textX;
  std::string body = L(tips::pageBody(t, page));
  TextStyle bs = ts(F12, col::white, LEFT, textW);
  float bodyH = 0;
  R().measure(body, bs, &bodyH);
  float h = std::max(bodyY + bodyH + 18, image ? bodyY + kImgH + 14 : 0.f);
  h = std::min(h, (float)kH - 24);
  float y = 20 + ((kH - 20) - h) / 2 + slide;
  gfx::pushAlpha(k);
  widgets::panel("ui/ftue_popup", x, y, w, h);
  TextStyle hs = ts(F16, col::gold, LEFT, w - 40 - padR - (image ? 50 : 0));
  hs.outline = 0x38311AFF;  // the header's dark outline (font_outline_color)
  R().text(x + 38, y + headY, L(in.title), hs);
  if (image) {
    R().text(x + w - padR, y + headY + 2, pageCount(page, tips::pages(t)), ts(F12, col::gray, RIGHT));
    Sprite pic = R().sprite("ui/ftue_combat_" + std::to_string(page));
    if (pic) spr(pic, x + padL, y + bodyY, kImgW, kImgH);
  }
  R().text(textX, y + bodyY, body, bs);
  gfx::popAlpha();
}

}  // namespace ui
