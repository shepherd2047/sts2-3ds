// S22 (U27): the shared confirmation / error modal (confirm.h) and the save error dialogs.
#include "confirm.h"

#include <cstring>
#include <deque>

#include "../core/profiles.h"
#include "../core/save_errors.h"
#include "ui_common.h"

namespace ui {

namespace confirm {
namespace {

struct Dialog {
  Spec spec;
  bool fresh = true;  // not shown yet: its first frame swallows the input
};

std::deque<Dialog> queue;
float openT = 0;   // seconds since the front dialog opened (slide-in)
int sel = 0;       // focused button: 0 cancel, 1 ok
int pressId = -1;  // button under a touch that started on it
bool taken = false;

constexpr float kW = 272, kBtnW = 104, kBtnH = 34, kPad = 14;

struct Layout {
  float x, y, w, h, bodyY, btnY;
  std::string body;
};

std::string cleanBody(std::string s) {  // [sine] / [b] have no effect on the 3DS
  for (const char* tag : {"[sine]", "[/sine]", "[b]", "[/b]"})
    for (size_t p; (p = s.find(tag)) != std::string::npos;) s.erase(p, strlen(tag));
  return s;
}

Layout layout(const Spec& s) {
  Layout l;
  l.w = kW;
  l.x = (kBot - l.w) / 2;
  l.body = cleanBody(s.body);
  const float titleH = R().lineHeight(F16) * 1.1f;
  TextStyle bs = ts(F12, col::white, CENTER, l.w - 32);
  float bodyH = 0;
  R().measure(l.body, bs, &bodyH);
  auto height = [&] { return kPad + titleH + 10 + bodyH + 14 + kBtnH + kPad; };
  if (height() > kH - 12) {  // too tall: blank lines between paragraphs go first
    for (size_t p; (p = l.body.find("\n\n")) != std::string::npos;) l.body.erase(p, 1);
    R().measure(l.body, bs, &bodyH);
  }
  l.h = std::min(height(), (float)kH - 8);
  l.y = std::max(4.f, (kH - l.h) / 2 - 6);
  l.bodyY = l.y + kPad + titleH + 10;
  l.btnY = l.y + l.h - kPad - kBtnH;
  return l;
}

bool single(const Spec& s) { return s.cancel.empty(); }

// Button rects: index 0 = cancel (left), 1 = ok (right, or centred on a notice).
bool buttonRect(const Layout& l, const Spec& s, int i, float& bx) {
  if (single(s)) {
    if (i == 0) return false;
    bx = l.x + (l.w - kBtnW) / 2;
    return true;
  }
  bx = i == 0 ? l.x + 18 : l.x + l.w - 18 - kBtnW;
  return true;
}

void outline(float x, float y, float w, float h, uint32_t c = col::gold, float t = 2) {
  gfx::rect(x - t, y - t, w + 2 * t, t, c);
  gfx::rect(x - t, y + h, w + 2 * t, t, c);
  gfx::rect(x - t, y, t, h, c);
  gfx::rect(x + w, y, t, h, c);
}

}  // namespace

void open(Spec s) { queue.push_back({std::move(s)}); }

void ask(const std::string& title, const std::string& body, std::function<void()> onOk, const std::string& ok,
         const std::string& cancel) {
  Spec s;
  s.title = title;
  s.body = body;
  s.ok = ok;
  s.cancel = cancel.empty() ? std::string(tr("取消", "Cancel")) : cancel;
  s.onOk = std::move(onOk);
  open(std::move(s));
}

void notice(const std::string& title, const std::string& body, const std::string& ok, std::function<void()> onClose) {
  Spec s;
  s.title = title;
  s.body = body;
  s.ok = ok;
  s.cancel.clear();
  s.onOk = onClose;
  s.onCancel = std::move(onClose);
  open(std::move(s));
}

bool isOpen() { return !queue.empty(); }
bool inputTaken() { return taken; }
void clear() { queue.clear(); }

}  // namespace confirm

// ---------------------------------------------------------------- save errors

namespace {
bool savesOn() { return !getenv("STS_HIDDEN") && !getenv("STS_NO_SAVE"); }

// The C#'s INVALID_SAVE_POPUP text when baked, else `fallback`. Only its first paragraph: the
// second asks for a bug report to MegaCrit, which does not apply to this port.
std::string invalidSaveText(const char* key, const char* fallback) {
  std::string k = std::string("main_menu_ui.INVALID_SAVE_POPUP.") + key;
  std::string s = L(k);
  if (s == k) return fallback;
  size_t p = s.find("\n\n");
  return p == std::string::npos ? s : s.substr(0, p);
}
std::string invalidSaveTitle() {
  std::string s = L("main_menu_ui.INVALID_SAVE_POPUP.title");
  return s.rfind("main_menu_ui.", 0) == 0 ? std::string(tr("存档损坏！", "Corrupt save!")) : s;
}
std::string dismissLabel() {
  std::string s = L("main_menu_ui.INVALID_SAVE_POPUP.dismiss");
  return s.rfind("main_menu_ui.", 0) == 0 ? std::string(tr("了解了", "OK")) : s;
}
}  // namespace

void App::openSaveError(int kind) {
  using saveerr::Kind;
  switch ((Kind)kind) {
    case Kind::RunCorrupt: {
      // NMainMenu: INVALID_SAVE_POPUP.description_run. The port offers to delete the save (the
      // safe choice, keeping it, is focused): progress and unlocks are in progress.sav, untouched.
      confirm::Spec s;
      s.title = invalidSaveTitle();
      s.body = invalidSaveText("description_run", tr("无法加载这局游戏。", "This run could not be loaded.")) +
               tr("\n\n要删除这局游戏的存档吗？\n进度与解锁不受影响。", "\n\nDelete this run's save?\nProgress and unlocks are not affected.");
      s.ok = tr("删除存档", "Delete save");
      s.cancel = tr("保留", "Keep");
      s.onOk = [this] {
        if (savesOn()) gfx::deleteSave(profiles::runSaveName());
        hasSave_ = hasSave();
        continueInfo_.clear();
        titleSelection_ = 0;
        toast_ = tr("存档已删除", "Save deleted");
        toastT_ = 1.6f;
      };
      s.onCancel = [this] {  // kept on the SD card, but 继续 is hidden until the menu is rebuilt
        hasSave_ = false;
        continueInfo_.clear();
        titleSelection_ = 0;
      };
      confirm::open(std::move(s));
      break;
    }
    case Kind::ProgressCorrupt:
      confirm::notice(invalidSaveTitle(),
                      invalidSaveText("description_progress", tr("加载存档文件时出现问题。", "There was a problem loading the save file.")) +
                          tr("\n\n已改用新的进度。损坏的文件另存为 progress.corrupt。", "\n\nStarted with fresh progress. The damaged file was kept as progress.corrupt."),
                      dismissLabel());
      break;
    case Kind::SettingsCorrupt:
      confirm::notice(invalidSaveTitle(),
                      invalidSaveText("description_settings", tr("加载设置文件时出现问题。", "There was a problem loading the settings file.")) +
                          tr("\n\n已恢复默认设置。损坏的文件另存为 settings.corrupt。", "\n\nSettings were reset to defaults. The damaged file was kept as settings.corrupt."),
                      dismissLabel());
      break;
    case Kind::WriteFailed:  // no C# text (Steam / the OS report it there)
      confirm::notice(tr("存档失败", "Save failed"),
                      tr("无法写入SD卡，这次的进度没有保存。\n\n请检查SD卡是否插好、没有锁定，并且还有剩余空间。", "Could not write to the SD card; this progress was not saved.\n\nCheck that the SD card is inserted, not locked, and has free space."),
                      L("main_menu_ui.GENERIC_POPUP.ok").rfind("main_menu_ui.", 0) == 0 ? tr("了解了", "OK")
                                                                                       : L("main_menu_ui.GENERIC_POPUP.ok"));
      break;
    default: break;
  }
}

// ---------------------------------------------------------------- frame

bool App::updateConfirm(const gfx::Input& in) {
  using namespace confirm;
  taken = false;
  if (queue.empty()) {
    saveerr::Kind k;
    if (saveerr::take(k)) openSaveError((int)k);
  }
  if (queue.empty()) return false;
  taken = true;
  widgets::suspendInput(true);
  Dialog& d = queue.front();
  if (d.fresh) {  // its first frame: reset the focus to the safe choice, swallow the input
    d.fresh = false;
    openT = 0;
    pressId = -1;
    sel = single(d.spec) ? 1 : 0;
    return true;
  }
  openT += (float)gfx::dt();
  const Layout l = layout(d.spec);
  int act = -1;
  auto inside = [&](int i) {
    float bx;
    return buttonRect(l, d.spec, i, bx) && in.tx >= bx && in.tx < bx + kBtnW && in.ty >= l.btnY && in.ty < l.btnY + kBtnH;
  };
  if (in.touchDown) {
    pressId = -1;
    for (int i = 0; i < 2; ++i)
      if (inside(i)) pressId = i;
    if (pressId >= 0) sel = pressId;
  }
  if (in.touchUp && pressId >= 0) {
    if (inside(pressId)) act = pressId;
    pressId = -1;
  }
  if (!single(d.spec) && (in.down & (gfx::BTN_LEFT | gfx::BTN_RIGHT | gfx::BTN_UP | gfx::BTN_DOWN))) sel = 1 - sel;
  if (in.down & gfx::BTN_A) act = sel;
  if (in.down & gfx::BTN_B) act = single(d.spec) ? 1 : 0;
  if (act < 0) return true;
  sfx::click();
  Spec s = std::move(d.spec);
  queue.pop_front();  // before the callback, which may open the next dialog
  if (act == 1 && s.onOk) s.onOk();
  if (act == 0 && s.onCancel) s.onCancel();
  return true;
}

void App::drawConfirm(bool top) {
  using namespace confirm;
  if (queue.empty()) return;
  const Spec& s = queue.front().spec;
  const float k = queue.front().fresh ? 0.f : easeOut(std::min(1.f, openT / style::kSlide));
  if (top) {
    gfx::rect(0, 0, kTop, kH, 0x00000090);
    return;
  }
  hits_.clear();  // nothing under the modal is touchable
  gfx::rect(0, 0, kBot, kH, 0x000000C0);
  const Layout l = layout(s);
  const float dy = (1 - k) * 12;
  gfx::pushAlpha(std::max(k, 0.01f));
  widgets::panel("ui/panel_popup", l.x, l.y + dy, l.w, l.h);
  R().text(l.x + l.w / 2, l.y + kPad + dy, s.title, ts(F16, col::gold, CENTER, l.w - 24, 1.1f));
  R().text(l.x + l.w / 2, l.bodyY + dy, l.body, ts(F12, col::white, CENTER, l.w - 32));
  for (int i = 0; i < 2; ++i) {
    float bx;
    if (!buttonRect(l, s, i, bx)) continue;
    const bool pressed = pressId == i;
    const float by = l.btnY + dy + (pressed ? 1 : 0);
    widgets::panel(i == 0 ? "ui/btn_cancel_s" : "ui/btn_ok_s", bx, by, kBtnW, kBtnH, pressed ? 0xC0C0C0FF : 0xFFFFFFFF);
    const std::string& label = i == 0 ? s.cancel : s.ok;
    TextStyle st = ts(F16, col::white, CENTER);
    if (R().measure(label, st) > kBtnW - 10) st = ts(F12, col::white, CENTER);
    R().text(bx + kBtnW / 2, by + (kBtnH - R().lineHeight(st.size)) / 2, label, st);
    if (i == sel) outline(bx, by, kBtnW, kBtnH);
  }
  if (l.y + l.h + 18 < kH)
    R().text(kBot / 2, kH - 16, single(s) ? tr("A / B：关闭", "A / B: Close") : tr("A：选择    B：取消", "A: Select    B: Cancel"), ts(F12, col::gray, CENTER));
  gfx::popAlpha();
}

}  // namespace ui
