// Split from ui.cpp (F3).
#include "../ui_common.h"

namespace ui {

// ================================================================ settings and end

void App::drawSettings(bool top) {
  drawSceneBg(top, 0.8f);
  if (top) {
    panel(36, 26, kTop - 72, 185);
    R().text(kTop / 2, 42, abandonConfirm_ ? "放弃本局？" : "设置", ts(F16, col::gold, CENTER, kTop - 96, 1.35f));
    if (abandonConfirm_) {
      R().text(kTop / 2, 105, "当前进度将被清除。", ts(F16, col::white, CENTER, kTop - 100));
      R().text(kTop / 2, 141, "确认后返回主菜单。", ts(F12, col::gray, CENTER));
    } else {
      R().text(kTop / 2, 91, "快速模式会加快战斗与界面动画。", ts(F12, col::white, CENTER, kTop - 100));
      R().text(kTop / 2, 122, "屏幕震动控制受击位移。", ts(F12, col::white, CENTER, kTop - 100));
      R().text(kTop / 2, 156, "音频尚未接入。", ts(F12, col::gray, CENTER));
    }
    return;
  }
  if (abandonConfirm_) {
    button(12, 92, 140, 42, "取消", ID_ABANDON_CANCEL);
    button(168, 92, 140, 42, "确认放弃", ID_ABANDON_CONFIRM, true, true);
    return;
  }
  button(18, 14, 284, 36, std::string("快速模式：") + (fastMode_ ? "开" : "关"), ID_FAST_MODE, true, fastMode_);
  button(18, 58, 284, 36, std::string("屏幕震动：") + (screenShake_ ? "开" : "关"), ID_SCREEN_SHAKE, true, screenShake_);
  button(18, 102, 284, 36, "音量：音频尚未接入", ID_NONE, false);
  button(18, 150, 284, 34, "放弃本局", ID_ABANDON);
  button(90, 202, 140, 32, "返回", ID_BACK);
}

void App::updateSettings(const gfx::Input& in) {
  int id = in.touchDown ? hitAt(in.tx, in.ty) : ID_NONE;
  if (abandonConfirm_) {
    if ((in.down & gfx::BTN_B) || id == ID_ABANDON_CANCEL) { abandonConfirm_ = false; return; }
    if ((in.down & gfx::BTN_A) || id == ID_ABANDON_CONFIRM) { returnTitle(); return; }
    return;
  }
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
  if (id == ID_ABANDON) abandonConfirm_ = true;
}

}  // namespace ui
