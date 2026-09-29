// Split from ui.cpp (F3).
#include "../ui_common.h"

namespace ui {

// ================================================================ settings and end

void App::drawSettings(bool top) {
  // S02: opened from the main menu, the page sits on the menu background. During a run it opens from
  // the pause menu (Y2), which owns 放弃 (give up), as in the C# NPauseMenu / NSettingsScreen.
  const bool fromMenu = run_->screen == Screen::Title;
  if (fromMenu) drawMenuBg(top, 0.8f);
  else drawSceneBg(top, 0.8f);
  if (top) {
    panel(36, 26, kTop - 72, 185);
    R().text(kTop / 2, 42, "设置", ts(F16, col::gold, CENTER, kTop - 96, 1.35f));
    R().text(kTop / 2, 91, "快速模式会加快战斗与界面动画。", ts(F12, col::white, CENTER, kTop - 100));
    R().text(kTop / 2, 122, "屏幕震动控制受击位移。", ts(F12, col::white, CENTER, kTop - 100));
    R().text(kTop / 2, 156, "音频尚未接入。", ts(F12, col::gray, CENTER));
    return;
  }
  button(18, 14, 284, 36, std::string("快速模式：") + (fastMode_ ? "开" : "关"), ID_FAST_MODE, true, fastMode_);
  button(18, 58, 284, 36, std::string("屏幕震动：") + (screenShake_ ? "开" : "关"), ID_SCREEN_SHAKE, true, screenShake_);
  button(18, 102, 284, 36, "音量：音频尚未接入", ID_NONE, false);
  button(90, 202, 140, 32, "返回", ID_BACK);
}

void App::updateSettings(const gfx::Input& in) {
  int id = in.touchDown ? hitAt(in.tx, in.ty) : ID_NONE;
  if ((in.down & (gfx::BTN_B | gfx::BTN_START)) || id == ID_BACK) { settingsOpen_ = false; return; }
  if ((in.down & gfx::BTN_X) || id == ID_FAST_MODE) {
    fastMode_ = !fastMode_;
    Scheduler::get().speed = fastMode_ ? 1.75 : 1.0;
    saveSettings();
  }
  if ((in.down & gfx::BTN_Y) || id == ID_SCREEN_SHAKE) {
    screenShake_ = !screenShake_;
    saveSettings();
  }
}

}  // namespace ui
