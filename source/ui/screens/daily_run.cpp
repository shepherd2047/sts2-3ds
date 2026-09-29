// M12: the offline daily run screen (NDailyRunScreen without the lobby and the leaderboard).
// Top screen: 每日挑战, the date and the time left today, the day's modifiers with their
// descriptions (NDailyRunScreenModifier: "[gold]{title}：[/gold]{description}") and the disclaimer.
// Bottom screen: the character (NDailyRunCharacterContainer: portrait, name, ascension), the seed,
// the local best score of the day and overall (the port's stand-in for the leaderboard), then
// back / embark. Keys: A or START embark, B back. The day comes from daily::today()
// (STS_DAILY_DATE=YYYY-MM-DD overrides it); STS_OPEN_DAILY=1 opens it from the title (previews).
#include <ctime>

#include "../../core/daily.h"
#include "../../core/modifiers.h"
#include "../../core/progress.h"
#include "../ui_common.h"

namespace ui {

namespace {
constexpr int kDBack = 2901, kDStart = 2902;

struct State {
  bool open = false;
  daily::Params p;
};
State& S() {
  static State s;
  return s;
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
  for (size_t at = 0; (at = s.find(from, at)) != std::string::npos; at += to.size()) s.replace(at, from.size(), to);
  return s;
}

// DATE_FORMAT ("yyyy MMMM d") in Chinese: 2026年9月29日.
std::string dateText(const daily::Date& d) { return num(d.year) + "年" + num(d.month) + "月" + num(d.day) + "日"; }

// TIME_LEFT: until the local midnight (the C#'s end of the UTC day); "" when the date is overridden.
std::string timeLeft() {
  if (getenv("STS_DAILY_DATE")) return "";
  std::time_t t = std::time(nullptr);
  const std::tm* tm = std::localtime(&t);
  if (!tm) return "";
  int left = 86400 - (tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec);
  char b[16];
  snprintf(b, sizeof b, "%02d:%02d:%02d", left / 3600, left / 60 % 60, left % 60);
  return replaceAll(L("main_menu_ui.DAILY_RUN_MENU.TIME_LEFT"), "{time}", b);
}
}  // namespace

void App::openDailyRun() {
  State& st = S();
  st.open = true;
  st.p = daily::forDate(daily::today());
}

bool App::drawDailyRun(bool top) {
  State& st = S();
  if (!st.open) return false;
  const Character& ch = db::character(st.p.character);
  if (top) {
    gfx::Texture* bg = R().texture("gfx/bg_character_" + ch.energyColor + ".t3t");
    if (bg) gfx::image(bg, 0, 0, kTop, kH, 0, 0, kTop, kH);
    else drawMenuBg(true, 0.f);
    gfx::rect(0, 0, kTop, kH, 0x000000B0);
    R().text(10, 6, L("main_menu_ui.DAILY_RUN_MENU.DAILY_TITLE"), ts(F16, col::gold));
    R().text(kTop - 10, 6, dateText(st.p.date), ts(F16, 0x87CEEBFF, RIGHT));  // StsColors.blue
    std::string tl = timeLeft();
    if (!tl.empty()) R().text(kTop - 10, 26, tl, ts(F12, col::white, RIGHT));
    // The day's modifiers.
    float y = 44;
    R().text(12, y, L("main_menu_ui.DAILY_RUN_MENU.MODIFIERS"), ts(F12, col::gold));
    y += 18;
    const float px = 8, pw = kTop - 16;
    for (auto& k : st.p.modifiers) {
      std::string line = replaceAll(L("main_menu_ui.DAILY_RUN_MENU.MODIFIER"), "{title}", L(modifiers::titleKey(k)));
      line = replaceAll(line, "{description}", L(modifiers::descriptionKey(k)));
      TextStyle dt = ts(F12, col::white, LEFT, pw - 20);
      float dh;
      R().measure(line, dt, &dh);
      widgets::panel("ui/hover_tip", px, y, pw, dh + 12);
      gfx::rect(px + 5, y + 6, 3, dh, modifiers::isGood(k) ? 0x7FE07FFF : 0xFF7A6AFF);
      R().text(px + 13, y + 6, line, dt);
      y += dh + 16;
    }
    R().text(12, kH - 30, L("main_menu_ui.DAILY_RUN_MENU.disclaimer"), ts(F12, col::gray, LEFT, kTop - 24, 0.9f));
    return true;
  }

  drawMenuBg(false, 0.6f);
  // NDailyRunCharacterContainer: portrait, name, ascension.
  panel(8, 8, kBot - 16, 96);
  const float bh = 80, bw = bh * 132 / 195;
  spr(R().sprite("ui/" + ch.energyColor + "_select"), 16, 16, bw, bh);
  R().text(24 + bw, 18, L("characters." + ch.key + ".title"), ts(F16, col::gold));
  if (st.p.ascension > 0) {
    std::string asc = replaceAll(L("main_menu_ui.DAILY_RUN_MENU.ASCENSION"), "{ascension}", num(st.p.ascension));
    spr(R().sprite("ui/tb_ascension"), 24 + bw, 42, 20, 29);
    R().text(48 + bw, 48, asc, ts(F12, col::white));
  }
  R().text(24 + bw, 80, L("main_menu_ui.CUSTOM_RUN_SCREEN.SEED_LABEL") + st.p.seedText, ts(F12, col::gray));
  // The local records (no leaderboard offline).
  panel(8, 112, kBot - 16, 80);
  const Progress& pr = progress::state();
  int today = daily::best(pr, daily::key(st.p.date)), overall = daily::bestOverall(pr);
  R().text(18, 120, "本地记录", ts(F16, col::gold));
  R().text(18, 144, "今日最高分", ts(F12, col::white));
  R().text(kBot - 18, 144, today < 0 ? "-" : num(today), ts(F12, today < 0 ? col::gray : col::gold, RIGHT));
  R().text(18, 164, "历史最高分", ts(F12, col::white));
  R().text(kBot - 18, 164, overall < 0 ? "-" : num(overall), ts(F12, overall < 0 ? col::gray : col::gold, RIGHT));
  button(6, 204, 96, 30, "返回", kDBack);
  button(218, 204, 96, 30, "开始", kDStart, true, true);
  return true;
}

bool App::updateDailyRun(const gfx::Input& in) {
  State& st = S();
  static bool autoOpened = false;  // STS_OPEN_DAILY=1: straight to this screen (automated previews)
  if (!autoOpened && getenv("STS_OPEN_DAILY")) { autoOpened = true; openDailyRun(); }
  if (!st.open) return false;
  int id = in.touchDown ? hitAt(in.tx, in.ty) : ID_NONE;
  if (id == kDBack || (in.down & gfx::BTN_B)) { st.open = false; return true; }
  if (id == kDStart || (in.down & (gfx::BTN_A | gfx::BTN_START))) {
    // NDailyRunScreen.StartNewSingleplayerRun: the day's character, ascension, seed and modifiers.
    const auto& ids = db::characterIds();
    titleChar_ = (int)(std::find(ids.begin(), ids.end(), st.p.character) - ids.begin());
    titleAsc_ = st.p.ascension;
    titleSeed_ = st.p.seedText;
    titleModifiers_ = st.p.modifiers;
    titleCustom_ = false;
    titleDaily_ = daily::key(st.p.date);
    st.open = false;
    startRun(false);
  }
  return true;
}

}  // namespace ui
