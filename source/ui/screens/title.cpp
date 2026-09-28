// Split from ui.cpp (F3).
#include "../ui_common.h"

namespace ui {

// ================================================================ title

void App::drawTitle(bool top) {
  if (titleCharacter_) {
    if (top) {
      gfx::image(R().texture("gfx/bg_character_ironclad.t3t"), 0, 0, kTop, kH, 0, 0, kTop, kH);
      gfx::rect(0, 0, kTop, 40, 0x000000A0);
      R().text(14, 9, "铁甲战士", ts(F16, col::gold));
      return;
    }
    gfx::image(R().texture("gfx/bg_menu.t3t"), 40, 240, kBot, kH, 0, 0, kBot, kH);
    gfx::rect(0, 0, kBot, kH, 0x000000B0);
    R().text(kBot / 2, 17, "选择角色", ts(F16, col::gold, CENTER));
    panel(26, 51, 268, 75);
    R().text(kBot / 2, 66, "铁甲战士", ts(F16, col::white, CENTER));
    R().text(kBot / 2, 96, "燃烧之血 · 初始生命 80", ts(F12, col::gray, CENTER));
    R().text(kBot / 2, 139, "其他角色尚未移植", ts(F12, col::gray, CENTER));
    button(19, 179, 130, 43, "返回", ID_BACK);
    button(171, 179, 130, 43, "开始", ID_START, true, true);
    return;
  }
  if (top) {
    gfx::image(R().texture("gfx/bg_menu.t3t"), 0, 0, kTop, kH, 0, 0, kTop, kH);
    return;
  }
  gfx::image(R().texture("gfx/bg_menu.t3t"), 40, 240, kBot, kH, 0, 0, kBot, kH);
  gfx::rect(0, 0, kBot, kH, 0x00000082);
  if (hasSave_) {
    button(58, 49, 204, 50, "继续", ID_CONTINUE, true, titleSelection_ == 0);
    button(58, 108, 204, 50, "新游戏", ID_START, true, titleSelection_ == 1);
  } else {
    button(58, 85, 204, 50, "新游戏", ID_START, true, true);
  }
  R().text(kBot / 2, 201, "↑↓选择 · A确认", ts(F12, col::white, CENTER));
}

void App::updateTitle(const gfx::Input& in) {
  int id = in.touchDown ? hitAt(in.tx, in.ty) : ID_NONE;
  if (titleCharacter_) {
    if (id == ID_BACK || (in.down & gfx::BTN_B)) { titleCharacter_ = false; return; }
    if (id == ID_START || (in.down & (gfx::BTN_A | gfx::BTN_START))) startRun(false);
    return;
  }
  if (hasSave_ && (in.down & (gfx::BTN_UP | gfx::BTN_DOWN))) titleSelection_ = 1 - titleSelection_;
  if (id == ID_CONTINUE || ((in.down & gfx::BTN_A) && hasSave_ && titleSelection_ == 0)) {
    startRun(true);
    return;
  }
  if (id == ID_START || (in.down & gfx::BTN_START) || ((in.down & gfx::BTN_A) && (!hasSave_ || titleSelection_ == 1))) {
    titleCharacter_ = true;
  }
}

}  // namespace ui
