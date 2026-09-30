// S21: the settings screen (C# NSettingsScreen / NSettingsTabManager; RGDSplus U26: the scene stays
// dimmed on the top screen with the focused setting's description, every control is on the bottom).
// Opened from the main menu (S02) and from the pause menu (Y2); B / START / 返回 go back to it.
//
// Every control is bound to the Y1 store (core/settings_store.h, settings.sav): it writes
// settings::state() and saves through App::saveSettings(), which also mirrors fastMode_ /
// screenShake_ (read every frame by the combat code).
//
// The C# has four tabs: General, Graphics, Sound, Input. On the 3DS:
//  - 游戏设置 (General) is split in two so each page fits four 36 px rows without scrolling:
//    游戏设置 (fast mode, long-press confirmation, common tooltips, language) and 显示 (screen
//    shake, text effects, run timer, hand card count); its reset-tutorials button moves to a
//    数据 page together with Y1's delete-data action.
//  - 音频设置 (Sound): music, SFX and ambience volume, applied to the audio buses at once.
//  - Left out (no meaning on a 3DS, or no backing in the store): the whole Graphics tab (fullscreen,
//    display selection, windowed resolution, aspect ratio, window resizing, vsync, FPS cap, MSAA),
//    the Input tab (key rebinding, keyboard-only mode: the 3DS buttons are fixed), master volume and
//    mute-in-background (no store field / no background on a 3DS), limit FPS in background, skip intro
//    logo, upload gameplay data, send feedback, modding, credits, phobia mode, multiplayer drawings.
#include "../../audio/audio.h"
#include "../../core/progress.h"
#include "../../core/settings_store.h"
#include "../confirm.h"
#include "../ui_common.h"

namespace ui {

void applyVolumes() {
  const Settings& s = settings::state();
  audio::setVolume(audio::MUSIC, s.bgmVolume);
  audio::setVolume(audio::SFX, s.sfxVolume);
  audio::setVolume(audio::AMBIENCE, s.ambienceVolume);
}

namespace {

enum Tab { kTabGeneral, kTabDisplay, kTabSound, kTabData, kTabCount };
enum Kind { kToggle, kSlider, kCycler, kAction };
enum Item {
  kFast, kLongPress, kTooltips, kLanguage,       // 游戏设置
  kShake, kTextFx, kRunTimer, kHandCount,        // 显示
  kBgm, kSfx, kAmbience,                         // 音频设置
  kResetTutorials, kCredits, kDeleteData,        // 数据 (S26: 制作人员)
  kItemCount
};

// Widget ids. Row controls are kRowId + item.
enum : int { kTabsId = 2600, kRowId = 2620, kBackId = 2650, kResetId = 2651 };

// Y3 hook: the language cycler is shown but locked until English loc + fonts are baked.
constexpr bool kLanguageReady = false;

struct Def {
  Item item;
  Tab tab;
  Kind kind;
  const char* title;  // settings_ui key (or a literal when the C# has none)
  const char* desc;
  bool wired;         // false: stored in settings.sav, but nothing in the port reads it yet
};

const Def kDefs[kItemCount] = {
    {kFast, kTabGeneral, kToggle, "FASTMODE", "FASTMODE_DESCRIPTION", true},
    {kLongPress, kTabGeneral, kToggle, "LONG_PRESS_CONFIRMATION_HEADER", "LONG_PRESS_CONFIRMATION_DESCRIPTION", false},
    {kTooltips, kTabGeneral, kToggle, "COMMON_TOOLTIPS_HEADER", "COMMON_TOOLTIPS_DESCRIPTION", false},
    {kLanguage, kTabGeneral, kCycler, "LANGUAGE", nullptr, true},
    {kShake, kTabDisplay, kToggle, "SCREENSHAKE_HEADER", "SCREENSHAKE_DESCRIPTION", true},
    {kTextFx, kTabDisplay, kToggle, "TEXT_EFFECTS", "TEXT_EFFECTS_DESCRIPTION", false},
    {kRunTimer, kTabDisplay, kToggle, "SHOW_RUN_TIMER_HEADER", "SHOW_RUN_TIMER_DESCRIPTION", false},
    {kHandCount, kTabDisplay, kToggle, "SHOW_HAND_CARD_COUNT_HEADER", "SHOW_HAND_CARD_COUNT_DESCRIPTION", false},
    {kBgm, kTabSound, kSlider, "MUSIC_VOLUME", nullptr, true},
    {kSfx, kTabSound, kSlider, "SFX_VOLUME", nullptr, true},
    {kAmbience, kTabSound, kSlider, "AMBIENCE_VOLUME", nullptr, true},
    {kResetTutorials, kTabData, kAction, "TUTORIAL_RESET", "TUTORIAL_RESET_POPUP_DESCRIPTION", true},
    {kCredits, kTabData, kAction, nullptr, nullptr, true},
    {kDeleteData, kTabData, kAction, nullptr, nullptr, true},
};

enum Modal { kNoModal, kModalReset, kModalTutorials, kModalDelete };  // which confirmation to ask

// Screen state (one settings screen exists; it lives here rather than in App).
int tab_ = kTabGeneral;
bool live_ = false;       // false on the frame the screen opens: the opening A press must not act
bool volDirty_ = false;   // a volume changed; saved once the stylus lifts (no SD write per frame)
std::string note_;       // the C# NSettingsToast line, shown on the top screen
float noteT_ = 0;

std::string S(const char* key) { return L(std::string("settings_ui.") + key); }

std::string tabLabel(int t) {
  switch (t) {
    case kTabGeneral: return S("TAB_GENERAL");
    case kTabDisplay: return S("DISPLAY");
    case kTabSound: return S("TAB_SOUND");
    default: return S("DATA");
  }
}

std::string itemTitle(const Def& d, bool inRun) {
  if (d.item == kDeleteData) return "删除档案数据";
  if (d.item == kCredits) return "制作人员";
  if (d.item == kLanguage && inRun) return S("LANGUAGE_IN_RUN");
  return S(d.title);
}

std::string itemDesc(const Def& d) {
  switch (d.item) {
    case kLanguage: return "切换游戏语言。英文版尚未提供，目前只有简体中文。";
    case kCredits: return "查看游戏的制作人员名单，以及本移植的说明。";
    case kBgm: return "调节背景音乐的音量。";
    case kSfx: return "调节战斗与界面音效的音量。";
    case kAmbience: return "调节房间环境音的音量。";
    case kDeleteData: return "删除当前档案的进行中存档与进度（解锁、统计），并把所有设置恢复为默认。此操作无法撤销。";
    default: return S(d.desc);
  }
}

bool* toggleField(Item it) {
  Settings& s = settings::state();
  switch (it) {
    case kLongPress: return &s.longPressConfirm;
    case kTooltips: return &s.commonTooltips;
    case kTextFx: return &s.textEffects;
    case kRunTimer: return &s.runTimerEnabled;
    case kHandCount: return &s.handCardCountDisplay;
    default: return nullptr;
  }
}

float* volumeField(Item it) {
  Settings& s = settings::state();
  switch (it) {
    case kBgm: return &s.bgmVolume;
    case kSfx: return &s.sfxVolume;
    case kAmbience: return &s.ambienceVolume;
    default: return nullptr;
  }
}

// The C# toasts shown when a tickbox changes (NFastModeTickbox etc.); "" for those without one.
std::string toggleToast(Item it, bool on) {
  switch (it) {
    case kFast: return S(on ? "TOAST_FAST_MODE_ON" : "TOAST_FAST_MODE_OFF");
    case kTextFx: return S(on ? "TOAST_TEXT_EFFECTS_ON" : "TOAST_TEXT_EFFECTS_OFF");
    case kRunTimer: return S(on ? "TOAST_RUN_TIMER_ON" : "TOAST_RUN_TIMER_OFF");
    case kHandCount: return S(on ? "TOAST_HAND_CARD_COUNT_ON" : "TOAST_HAND_CARD_COUNT_OFF");
    default: return "";
  }
}

void showToast(const std::string& t) { note_ = t; noteT_ = t.empty() ? 0 : 2.4f; }

int firstItem(int t) {
  for (const Def& d : kDefs)
    if (d.tab == t) return d.item;
  return 0;
}

const Def* focusedDef() {
  int f = widgets::focused() - kRowId;
  return f >= 0 && f < kItemCount ? &kDefs[f] : nullptr;
}

float stepVolume(float v, int dir) { return std::clamp(std::round(v * 10.f + dir) / 10.f, 0.f, 1.f); }

}  // namespace

void App::drawSettings(bool top) {
  const bool fromMenu = run_->screen == Screen::Title;
  const bool inRun = !fromMenu;
  if (fromMenu) drawMenuBg(top, 0.8f);
  else drawSceneBg(top, 0.8f);

  if (top) {
    noteT_ = std::max(0.f, noteT_ - 1.f / 60);
    R().text(kTop / 2, 14, L("gameplay_ui.PAUSE_MENU.SETTINGS"), ts(F16, col::gold, CENTER, 0, 1.25f));
    gfx::rect(kTop / 2.f - 60, 40, 120, 2, style::kPanelHi);
    const float px = 24, pw = kTop - 48, py = 52, ph = 130;
    widgets::panel("ui/panel_popup", px, py, pw, ph);
    const Def* d = focusedDef();
    if (d && d->tab == tab_) {
      R().text(px + 14, py + 12, itemTitle(*d, inRun), ts(F16, col::gold, LEFT, pw - 28));
      R().text(px + 14, py + 40, itemDesc(*d), ts(F12, col::white, LEFT, pw - 28));
      if (!d->wired) R().text(px + 14, py + ph - 22, "此选项会被保存，但本移植尚未接入它的效果。", ts(F12, col::gray, LEFT, pw - 28));
      else if (d->item == kLanguage && inRun) R().text(px + 14, py + ph - 22, "仅可在主菜单调整。", ts(F12, col::red, LEFT, pw - 28));
    } else {
      R().text(px + 14, py + 12, tabLabel(tab_), ts(F16, col::gold, LEFT, pw - 28));
      R().text(px + 14, py + 40, "选择一项设置来查看说明。", ts(F12, col::gray, LEFT, pw - 28));
    }
    if (noteT_ > 0) {
      uint32_t a = (uint32_t)(0xFF * std::min(1.f, noteT_ / 0.3f));
      R().text(kTop / 2, 192, note_, ts(F12, (col::white & 0xFFFFFF00u) | a, CENTER, kTop - 40));
    }
    R().text(kTop / 2, 222, "L/R 切换分页　A 确认　B 返回", ts(F12, col::gray, CENTER));
    return;
  }

  // ---- bottom: tabs, four rows, action bar ----
  gfx::Input in = gfx::input();
  if (!live_) {  // opening frame: start on the first tab, first setting; ignore the A that opened us
    in = gfx::Input{};
    tab_ = kTabGeneral;
    widgets::setFocus(kRowId + firstItem(tab_));
  }
  if (confirm::inputTaken()) in = gfx::Input{};  // S22: this frame's input went to the modal
  Modal askFor = kNoModal;
  const Def* fd = focusedDef();
  const uint32_t lr = gfx::BTN_LEFT | gfx::BTN_RIGHT;
  const int dir = (in.down & gfx::BTN_RIGHT) ? 1 : (in.down & gfx::BTN_LEFT) ? -1 : 0;
  gfx::Input navIn = in;
  // Left/right on a row belongs to its control (slider / cycler), not to focus navigation.
  if (fd) navIn.down &= ~lr;
  widgets::beginFrame(navIn);

  const float x = style::kMargin, w = kBot - 2 * style::kMargin;
  int newTab = widgets::tabs(kTabsId, x, 6, w, 32, {tabLabel(0), tabLabel(1), tabLabel(2), tabLabel(3)}, tab_);
  if (newTab != tab_) { tab_ = newTab; widgets::setFocus(kTabsId + tab_); }

  Settings& s = settings::state();
  float y = 44;
  for (const Def& d : kDefs) {
    if (d.tab != tab_) continue;
    const int id = kRowId + (int)d.item;
    const bool focus = widgets::focused() == id && widgets::usingPad();
    const bool rowEnabled = !(d.item == kDeleteData && inRun) && !(d.item == kLanguage && (!kLanguageReady || inRun));
    widgets::panel("ui/btn_row", x, y, w, style::kRowH, rowEnabled ? (focus ? 0xFFFFFFFF : 0xE8E8E8FF) : 0x909090FF);
    if (focus) gfx::rect(x, y, w, style::kRowH, style::kSelectedFill);
    const float ly = y + (style::kRowH - R().lineHeight(F16)) / 2;
    R().text(x + 10, ly, itemTitle(d, false), ts(F16, rowEnabled ? col::white : col::gray, LEFT, 150));
    const bool rowTap = in.touchDown && in.tx >= x && in.tx < x + w && in.ty >= y && in.ty < y + style::kRowH;
    switch (d.kind) {
      case kToggle: {
        bool v = d.item == kFast ? fastMode_ : d.item == kShake ? screenShake_ : *toggleField(d.item);
        const float cx = x + w - 6 - style::kIconBtn, cy = y + (style::kRowH - style::kIconBtn) / 2;
        bool nv = widgets::toggle(id, cx, cy, v);
        // The whole row is the tick box's label: a tap anywhere on it toggles too.
        if (nv == v && rowTap && !(in.tx >= cx && in.tx < cx + style::kIconBtn)) {
          nv = !v;
          widgets::setFocus(id);
        }
        R().text(cx - 6, y + (style::kRowH - R().lineHeight(F12)) / 2, nv ? "开" : "关",
                 ts(F12, nv ? col::gold : col::gray, RIGHT));
        if (nv != v) {
          if (d.item == kFast) {
            fastMode_ = nv;
            Scheduler::get().speed = fastMode_ ? 1.75 : 1.0;
          } else if (d.item == kShake) {
            screenShake_ = nv;
          } else {
            *toggleField(d.item) = nv;
          }
          saveSettings();
          showToast(toggleToast(d.item, nv));
        }
        break;
      }
      case kSlider: {
        float* f = volumeField(d.item);
        float v = *f;
        if (fd == &d && dir) v = stepVolume(v, dir);
        const float sx = x + 150, sw = w - 150 - 52;
        v = widgets::slider(id, sx, y + (style::kRowH - style::kIconBtn) / 2, sw, v);
        R().text(x + w - 8, y + (style::kRowH - R().lineHeight(F12)) / 2, num((int)std::lround(v * 100)) + "%",
                 ts(F12, col::gold, RIGHT));
        if (v != *f) {
          *f = v;
          applyVolumes();
          volDirty_ = true;
        }
        break;
      }
      case kCycler: {  // "‹ value ›"; A / tap / left-right cycle through the options
        const float cw = 130, cx = x + w - 6 - cw;
        bool act = widgets::hit(id, cx, y + 2, cw, style::kRowH - 4, rowEnabled);
        int step = act ? 1 : (fd == &d && rowEnabled) ? dir : 0;
        if (step) {
          s.language = s.language == Language::ZhCN ? Language::En : Language::ZhCN;
          saveSettings();
        }
        uint32_t c = rowEnabled ? col::white : col::gray;
        R().text(cx + 8, ly, "‹", ts(F16, c));
        R().text(cx + cw - 8, ly, "›", ts(F16, c, RIGHT));
        R().text(cx + cw / 2, ly, s.language == Language::En ? "English" : "简体中文", ts(F16, c, CENTER));
        widgets::focusRing(id, cx, y + 2, cw, style::kRowH - 4);
        if (!rowEnabled) R().text(cx - 4, y + style::kRowH - 13, inRun ? "仅主菜单" : "英文版尚未提供",
                                  ts(F12, col::red, RIGHT, 0, 0.85f));
        break;
      }
      case kAction: {
        const float bw = 84, bx = x + w - 5 - bw;
        const bool danger = d.item == kDeleteData;
        std::string label = danger ? "删除" : d.item == kCredits ? "查看" : S("TUTORIAL_RESET_BUTTON_LABEL");
        if (widgets::button(id, bx, y + 1, bw, style::kRowH - 2, label,
                            danger ? widgets::Kind::Danger : widgets::Kind::Secondary, rowEnabled)) {
          if (d.item == kCredits) openCredits();
          else askFor = danger ? kModalDelete : kModalTutorials;
        }
        if (!rowEnabled) R().text(bx - 6, y + (style::kRowH - R().lineHeight(F12)) / 2, "仅可在主菜单操作",
                                  ts(F12, col::red, RIGHT, 0, 0.85f));
        break;
      }
    }
    y += style::kRowH + 2;
  }
  if (volDirty_ && !in.touching) { saveSettings(); volDirty_ = false; }

  // Action bar: back bottom-left; 重置为默认 (this page's settings) bottom-right, not on 数据.
  bool back = widgets::button(kBackId, style::kMargin, style::kActionY, 90, style::kButtonH, "返回");
  if (tab_ != kTabData &&
      widgets::button(kResetId, kBot - style::kMargin - 120, style::kActionY, 120, style::kButtonH, S("RESET_DEFAULT")))
    askFor = kModalReset;

  if (askFor != kNoModal) {  // S22: the shared confirmation modal (confirm.h)
    std::string title, body;
    switch (askFor) {
      case kModalReset:  // NResetGameplayButton
        title = S("RESET_CONFIRMATION.header");
        body = "要将「" + tabLabel(tab_) + "」中的设置恢复为默认吗？";
        break;
      case kModalTutorials:
        title = S("TUTORIAL_RESET_POPUP_HEADER");
        body = S("TUTORIAL_RESET_POPUP_DESCRIPTION");
        break;
      default:
        title = "删除档案数据？";
        body = itemDesc(kDefs[kDeleteData]);
        break;
    }
    const int page = tab_;
    confirm::ask(title, body, [this, askFor, page] {
      Settings& s = settings::state();
      const Settings def;
      switch (askFor) {
        case kModalReset:  // NResetGameplayButton: this page's settings back to their defaults
          if (page == kTabGeneral) {
            fastMode_ = def.fastMode;
            s.longPressConfirm = def.longPressConfirm;
            s.commonTooltips = def.commonTooltips;
          } else if (page == kTabDisplay) {
            screenShake_ = def.screenShake;
            s.textEffects = def.textEffects;
            s.runTimerEnabled = def.runTimerEnabled;
            s.handCardCountDisplay = def.handCardCountDisplay;
          } else {
            s.bgmVolume = def.bgmVolume;
            s.sfxVolume = def.sfxVolume;
            s.ambienceVolume = def.ambienceVolume;
          }
          Scheduler::get().speed = fastMode_ ? 1.75 : 1.0;
          applyVolumes();
          saveSettings();
          showToast("已恢复默认设置。");
          break;
        case kModalTutorials:
          // ProgressState.ResetFtues. M13 (tutorials) reads settings::tutorialSeen(), so this is
          // all it needs; nothing else to refresh here.
          settings::resetTutorials();
          saveSettings();
          showToast("教程已重置。");
          break;
        default:
          // Y1 "delete data". Automated previews (STS_HIDDEN / STS_NO_SAVE) never touch a file:
          // there it only resets the in-memory state.
          if (getenv("STS_HIDDEN") || getenv("STS_NO_SAVE")) {
            progress::reset();
            settings::reset();
          } else {
            settings::eraseAllData();
          }
          fastMode_ = settings::state().fastMode;
          screenShake_ = settings::state().screenShake;
          Scheduler::get().speed = fastMode_ ? 1.75 : 1.0;
          applyVolumes();
          hasSave_ = hasSave();
          showToast("档案数据已删除。");
          break;
      }
    });
  }
  widgets::endFrame();
  if (back) {
    if (volDirty_) { saveSettings(); volDirty_ = false; }
    live_ = false;
    settingsOpen_ = false;
  }
}

void App::updateSettings(const gfx::Input& in) {
  const bool wasLive = live_;
  live_ = true;
  if (!wasLive) return;
  if (in.down & (gfx::BTN_B | gfx::BTN_START)) {
    if (volDirty_) { saveSettings(); volDirty_ = false; }
    live_ = false;
    settingsOpen_ = false;
    return;
  }
  int t = tab_;
  if (in.down & gfx::BTN_L) t = (tab_ + kTabCount - 1) % kTabCount;
  if (in.down & gfx::BTN_R) t = (tab_ + 1) % kTabCount;
  if (t != tab_) {
    tab_ = t;
    widgets::setFocus(kRowId + firstItem(tab_));
  }
}

}  // namespace ui
