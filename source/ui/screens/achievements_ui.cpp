// M5: the achievement list page and the unlock toast.
//
// List page: NStatsScreen's achievements tab (NAchievementsGrid / NAchievementHolder). The C#
// puts it in the stats screen (the main menu's 统计), so here it is a page of the stats screen
// (screens/stats_screen.cpp: 成就 button / Y on the stats page). Unlocked achievements first, then
// the locked ones, both in enum order (NAchievementsGrid._Ready). Bottom screen: the icon grid
// (locked = darkened icon + the lock), top screen: the focused one's icon, title (LOCKED.title in
// red when locked), description and unlock date (UNLOCK_DATE.text), plus the unlocked count.
// Keys: D-pad focus, B back; tap an icon to focus it.
//
// Toast (the platform's unlock popup; the C# leaves it to Steam): top screen, icon + name, 2.5 s,
// fresh unlocks queued (achievements::popToast). Each popped toast also writes progress.sav
// (the C#'s Unlock saves the progress file at once).
//
// Debug: STS_ACHIEVE_TOAST=<id>[,<id>] queues toasts, STS_ACHIEVEMENTS=<id>,.. unlocks in memory
// (use with STS_HIDDEN so no save is written), STS_OPEN_ACHIEVEMENTS=1 opens the list page.
#include <cstdlib>
#include <cstring>

#include "../../core/achievements.h"
#include "../../core/profiles.h"
#include "../ui_common.h"

namespace ui {

namespace {
constexpr int kCell0 = 3500;  // + position in the grid
constexpr int kBack = 3590;
constexpr int kCols = 6;
constexpr float kCell = 48, kIcon = 42, kGridX = (kBot - kCols * kCell) / 2, kGridY = 8;
constexpr float kBarY = 202, kBarH = 34;
constexpr float kToastTime = 2.5f;

struct State {
  bool open = false;
  int sel = 0;
  bool envDone = false;
  bool toastActive = false;
  achievements::Id toast = achievements::Id::IroncladWin;
  float toastT = 0;
};
State& S() {
  static State s;
  return s;
}

std::vector<achievements::Id> order() {  // unlocked first, then locked (NAchievementsGrid)
  std::vector<achievements::Id> v;
  for (int pass = 0; pass < 2; ++pass)
    for (auto& i : achievements::all())
      if (achievements::isUnlocked(i.id) == (pass == 0)) v.push_back(i.id);
  return v;
}

void drawIcon(achievements::Id id, float x, float y, float size, bool unlocked) {
  Sprite b = R().sprite("ach/border");
  Sprite ic = R().sprite(std::string("ach/") + achievements::info(id).snake);
  const float pad = size * 0.1f;
  // Locked: NAchievementHolder.SetLockVisuals desaturates and darkens icon and border (HSV shader);
  // here the colours are lerped towards grey.
  if (ic) spr(ic, x + pad, y + pad, size - 2 * pad, size - 2 * pad, unlocked ? 0xFFFFFFFF : 0x303036FF, unlocked ? 0 : 0.7f);
  else gfx::rect(x + pad, y + pad, size - 2 * pad, size - 2 * pad, unlocked ? 0x806020FF : 0x303040FF);
  if (b) spr(b, x, y, size, size, unlocked ? 0xFFFFFFFF : 0x707070FF, unlocked ? 0 : 0.75f);
  if (!unlocked) {
    Sprite l = R().sprite("ach/lock");
    if (l) spr(l, x + size * 0.3f, y + size * 0.3f, size * 0.4f, size * 0.4f);
  }
}

std::string title(achievements::Id id) { return L(achievements::locKey(id) + ".title"); }

std::string dateText(int64_t t) {
  time_t tt = (time_t)t;
  struct tm* lt = localtime(&tt);
  char buf[32] = "";
  if (lt) strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", lt);
  std::string s = L("achievements.UNLOCK_DATE.text");
  size_t p = s.find("{Date}");
  if (p == std::string::npos) return std::string("已解锁 ") + buf;
  return s.replace(p, 6, std::string(" ") + buf);
}

bool savesOn() { return !getenv("STS_HIDDEN") && !getenv("STS_NO_SAVE"); }

void forEachId(const char* list, const std::function<void(achievements::Id)>& f) {
  std::string s = list;
  size_t start = 0;
  while (start <= s.size()) {
    size_t end = s.find(',', start);
    if (end == std::string::npos) end = s.size();
    if (const auto* i = achievements::find(s.substr(start, end - start))) f(i->id);
    start = end + 1;
  }
}
}  // namespace

void App::openAchievements() {
  S().open = true;
  S().sel = 0;
}

bool App::drawAchievements(bool top) {
  State& s = S();
  if (!s.open) return false;
  drawMenuBg(top, top ? 0.6f : 0.55f);
  auto ids = order();
  s.sel = std::clamp(s.sel, 0, (int)ids.size() - 1);
  const achievements::Id cur = ids[s.sel];
  const bool un = achievements::isUnlocked(cur);
  if (top) {
    TextStyle ht = ts(F16, col::gold, CENTER, 0, 1.15f);
    ht.outline = 0x000000FF;
    R().text(kTop / 2, 7, L("stats_screen.TAB_ACHIEVEMENT.header") + "  " + num(achievements::unlockedCount()) + "/" +
                               num(achievements::totalCount()), ht);
    const float px = 12, py = 36, pw = kTop - 24, ph = 150;
    widgets::panel("ui/hover_tip", px, py, pw, ph);
    drawIcon(cur, px + 14, py + (ph - 96) / 2, 96, un);
    const float tx = px + 124, tw = pw - 136;
    R().text(tx, py + 18, un ? title(cur) : L("achievements.LOCKED.title"), ts(F16, un ? col::gold : col::red, LEFT, tw, 1.15f));
    R().text(tx, py + 50, L(achievements::locKey(cur) + ".description"), ts(F12, col::white, LEFT, tw));
    if (un) R().text(tx, py + ph - 30, dateText(achievements::unlockTime(cur)), ts(F12, col::gray, LEFT, tw));
    R().text(kTop / 2, py + ph + 14, "自定义模式与每日挑战中无法解锁成就", ts(F12, col::gray, CENTER));
    return true;
  }
  for (int i = 0; i < (int)ids.size(); ++i) {
    const float x = kGridX + (i % kCols) * kCell + (kCell - kIcon) / 2, y = kGridY + (i / kCols) * kCell + (kCell - kIcon) / 2;
    drawIcon(ids[i], x, y, kIcon, achievements::isUnlocked(ids[i]));
    if (i == s.sel) {
      const float t = 2;
      gfx::rect(x - t, y - t, kIcon + 2 * t, t, col::gold);
      gfx::rect(x - t, y + kIcon, kIcon + 2 * t, t, col::gold);
      gfx::rect(x - t, y, t, kIcon, col::gold);
      gfx::rect(x + kIcon, y, t, kIcon, col::gold);
    }
    hits_.push_back({x, y, kIcon, kIcon, kCell0 + i});
  }
  widgets::panel("ui/btn_back", 8, kBarY, 96, kBarH);
  R().text(8 + 48, kBarY + (kBarH - R().lineHeight(F16)) / 2, "返回", ts(F16, col::white, CENTER));
  hits_.push_back({8, kBarY, 96, kBarH, kBack});
  return true;
}

bool App::updateAchievements(const gfx::Input& in) {
  State& s = S();
  if (!s.open) return false;
  const int n = achievements::totalCount();
  int id = in.touchDown ? hitAt(in.tx, in.ty) : ID_NONE;
  const uint32_t d = in.down;
  if (id == kBack || (d & gfx::BTN_B)) { s.open = false; return true; }
  if (id >= kCell0 && id < kCell0 + n) s.sel = id - kCell0;
  if (d & gfx::BTN_LEFT) s.sel = std::max(0, s.sel - 1);
  if (d & gfx::BTN_RIGHT) s.sel = std::min(n - 1, s.sel + 1);
  if (d & gfx::BTN_UP) s.sel = s.sel >= kCols ? s.sel - kCols : s.sel;
  if (d & gfx::BTN_DOWN) s.sel = std::min(n - 1, s.sel + kCols);
  return true;
}

void App::updateAchievementToast(float dt) {
  State& s = S();
  if (!s.envDone) {
    s.envDone = true;
    if (const char* v = getenv("STS_ACHIEVEMENTS")) {
      forEachId(v, [](achievements::Id i) { achievements::unlock(i, nullptr); });
      achievements::clearToasts();
    }
    if (const char* v = getenv("STS_ACHIEVE_TOAST")) forEachId(v, [](achievements::Id i) { achievements::queueToast(i); });
    if (getenv("STS_OPEN_ACHIEVEMENTS")) {
      openStats(1);
      openAchievements();
    }
  }
  if (s.toastActive) {
    s.toastT += dt;
    if (s.toastT >= kToastTime) s.toastActive = false;
  }
  if (!s.toastActive && achievements::popToast(s.toast)) {
    s.toastActive = true;
    s.toastT = 0;
    if (savesOn()) profiles::saveProgress();  // AchievementsUtil.Unlock saves progress at once
  }
}

void App::drawAchievementToast() {
  State& s = S();
  if (!s.toastActive) return;
  // Slide down from above the screen, hold, slide back up.
  const float in = std::min(1.f, s.toastT / 0.25f), out = std::min(1.f, (kToastTime - s.toastT) / 0.25f);
  const float k = std::min(in, out);
  const float w = 220, h = 44, x = (kTop - w) / 2, y = -h + (h + 6) * k;
  widgets::panel("ui/hover_tip", x, y, w, h);
  drawIcon(s.toast, x + 6, y + 4, 36, true);
  R().text(x + 48, y + 6, "解锁成就", ts(F12, col::gray, LEFT, w - 54));
  R().text(x + 48, y + 21, title(s.toast), ts(F16, col::gold, LEFT, w - 54));
}

}  // namespace ui
