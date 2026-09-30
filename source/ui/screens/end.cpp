// S23 (RGDSplus U28, C# NGameOverScreen / NScoreLine / NBadge): the death and victory screen,
// rebuilt on the widget kit.
//
// Two pages, as in the C#:
//  1. Intro: the banner (BANNER.lose* picked per run / BANNER.falseWin: beating the Architect is a
//     "victory...?") over the act's room art, with a random QUOTES line on a death or the victory
//     damage text (VICTORY_DAMAGE_LOCAL) on a win. 继续 (A) opens
//  2. the summary (OpenSummaryScreen / AnimateRunSummary): the banner moves up and the quote fades
//     to the encounter quote (NRunHistory.GetDeathQuote: who killed you / the ending); the score
//     lines slide in one by one (AnimateScoreLines: floors, gold, elites and bosses when > 0,
//     ascension multiplier when > 0; 0.3 s + 0.1 s each, then 0.5 s) while the total counts up,
//     then the badges pop in (AnimateBadges: 0.25 s wait, 0.25 s each, rising from below). The
//     port adds the run's stats (character, ascension, floor, time, seed, daily / custom marker)
//     and the achievements unlocked during this run (M5; locked in custom / daily runs, which the
//     screen says). Top screen: banner, quote, score breakdown; bottom: stats, badges (tap or
//     D-pad to read one), achievements, 主菜单 / 再来一局.
// Everything comes from the run history record written at the run's end (history::last(), M2 +
// M7), so the screen shows exactly what was stored. A or a tap during the count-up skips to the
// end; B = 主菜单, START = 再来一局 once it has finished.
//
// Skipped (owner decision / not in the port): the score bar towards the next epoch unlock
// (everything is unlocked), the discoveries list, VICTORY_UNLOCKED_ASCENSION, the daily
// leaderboard and 查看本局 (the run history screen, S25, shows stored runs).
// PORT NOTE: progress.sav keeps no ArchitectDamage; the victory text's "total damage dealt to the
// Architect" is the sum of the won runs' scores in the stored history (the last 50 runs).
#include <cstring>

#include "../../core/achievements.h"
#include "../../core/badges.h"
#include "../../core/history.h"
#include "../../core/modifiers.h"
#include "../../core/profiles.h"
#include "../ui_common.h"

namespace ui {

namespace {
constexpr int kMenuId = 3400, kRestartId = 3401, kContinueId = 3402;
constexpr int kBadge0 = 3410;  // + badge index
constexpr int kAch0 = 3440;    // + achievement index
// AnimateScoreLines / AnimateBadges timings (seconds).
constexpr float kLineIn = 0.3f, kLineStep = 0.4f, kLinesPause = 0.5f;
constexpr float kBadgeWait = 0.25f, kBadgeStep = 0.25f;
constexpr float kBannerMove = 0.5f;
constexpr float kBadgeSize = 32, kBadgePitch = 36;
constexpr int kBadgeCols = 8;
constexpr float kAchSize = 22;

struct ScoreLine {
  std::string icon, label, value;
};

struct State {
  bool open = false;
  // Identity of the record shown (a new run end resets the screen).
  int64_t startTime = -1;
  uint64_t seed = 0;
  int runTime = -1;
  bool win = false;
  history::RunRecord rec;
  std::vector<ScoreLine> lines;
  std::vector<achievements::Id> achs;  // unlocked during this run
  std::string banner, quote, encounterQuote;
  bool summary = false;
  double t0 = 0;  // time the summary opened
  bool skipped = false, swallowTouch = false, focusSet = false;
  int detail = -1;  // badge index, or kAch0 - kBadge0 + achievement index; -1 = first badge
};
State& S() {
  static State s;
  return s;
}

std::string fill(std::string s, const std::string& name, const std::string& value) {
  for (const std::string& tag : {"{" + name + "}", "{" + name + ":n()}"})
    for (size_t p; (p = s.find(tag)) != std::string::npos;) s.replace(p, tag.size(), value);
  return s;
}
// "1234567" -> "1,234,567" ({X:n()}).
std::string grouped(long long v) {
  std::string s = std::to_string(v < 0 ? -v : v), out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (i && (s.size() - i) % 3 == 0) out += ',';
    out += s[i];
  }
  return v < 0 ? "-" + out : out;
}
// VantomBoss -> VANTOM_BOSS (ModelId.Entry).
std::string upperSnake(const std::string& id) {
  std::string key;
  for (size_t i = 0; i < id.size(); ++i) {
    char c = id[i];
    if (i > 0 && std::isupper((unsigned char)c) && !std::isupper((unsigned char)id[i - 1])) key += '_';
    key += (char)std::toupper((unsigned char)c);
  }
  return key;
}
std::string lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}
std::string charTitle(const std::string& id) { return L("characters." + db::character(id).key + ".title"); }
std::string encounterTitle(const std::string& id) {
  std::string k = "encounters." + upperSnake(id) + ".title";
  return R().hasLoc(k) ? L(k) : id;
}
std::string eventTitle(const std::string& id) {
  std::string k = upperSnake(id);
  if (R().hasLoc("events." + k + ".title")) return L("events." + k + ".title");
  if (R().hasLoc("ancients." + k + ".title")) return L("ancients." + k + ".title");
  return id;
}
// TimeFormatting.Format: h:mm:ss, or m:ss under an hour.
std::string duration(int secs) {
  secs = std::max(0, secs);
  char b[32];
  if (secs >= 3600) snprintf(b, sizeof b, "%d:%02d:%02d", secs / 3600, secs / 60 % 60, secs % 60);
  else snprintf(b, sizeof b, "%d:%02d", secs / 60, secs % 60);
  return b;
}
std::string unescape(std::string s) {
  for (size_t p; (p = s.find("\\n")) != std::string::npos;) s.replace(p, 2, "\n");
  return s;
}
// All keys "<prefix><i>" that exist (i from 0, optionally two digits).
std::vector<std::string> keysWithPrefix(const std::string& prefix, bool twoDigits) {
  std::vector<std::string> v;
  for (int i = 0; i < 100; ++i) {
    char b[8];
    snprintf(b, sizeof b, twoDigits ? "%02d" : "%d", i);
    if (!R().hasLoc(prefix + b)) break;
    v.push_back(prefix + b);
  }
  return v;
}

// NRunHistory.GetDeathQuote (same as the run history screen's).
std::string deathQuote(const history::RunRecord& r) {
  auto pick = [&](const std::string& prefix, int n) {
    for (int i = n; i > 0; --i) {
      std::string k = prefix + "." + num((int)((r.seed + i) % n));
      if (R().hasLoc(k)) return L(k);
    }
    return std::string();
  };
  std::string q;
  if (r.win) {
    q = R().hasLoc("run_history.MAP_POINT_HISTORY.victory.1") ? L("run_history.MAP_POINT_HISTORY.victory.1")
                                                             : pick("run_history.MAP_POINT_HISTORY.falseVictory", 2);
  } else if (r.abandoned) {
    q = pick("run_history.MAP_POINT_HISTORY.abandon", 3);
  } else if (!r.killedByEncounter.empty()) {
    std::string k = "encounters." + upperSnake(r.killedByEncounter) + ".loss";
    q = R().hasLoc(k) ? L(k) : pick("run_history.MAP_POINT_HISTORY.defeat", 2);
    q = fill(fill(q, "encounter", encounterTitle(r.killedByEncounter)), "encounterName", encounterTitle(r.killedByEncounter));
  } else if (!r.killedByEvent.empty()) {
    std::string k = "events." + upperSnake(r.killedByEvent) + ".loss";
    if (!R().hasLoc(k)) k = "ancients." + upperSnake(r.killedByEvent) + ".loss";
    q = fill(R().hasLoc(k) ? L(k) : L("run_history.DEFAULT_EVENT_LOSS_MESSAGE"), "event", eventTitle(r.killedByEvent));
  } else {
    q = fill(pick("run_history.MAP_POINT_HISTORY.defeat", 2), "encounterName", "???");
  }
  q = fill(q, "character", charTitle(r.character));
  return L("game_over_screen.ENCOUNTER_QUOTE_LEFT") + q + L("game_over_screen.ENCOUNTER_QUOTE_RIGHT");
}

std::string badgeText(const history::BadgeEntry& b, bool title) {
  badges::LocKeys k = badges::locKeys(b);
  const std::string& a = title ? k.rarityTitle : k.rarityDescription;
  const std::string& p = title ? k.title : k.description;
  if (R().hasLoc(a)) return L(a);
  if (R().hasLoc(p)) return L(p);
  return b.id;
}
void drawBadge(const history::BadgeEntry& b, float x, float y, float size) {
  Sprite plate = R().sprite(std::string("badge/") + badges::rarityName(b.rarity));
  if (plate) spr(plate, x, y, size, size);
  Sprite art = R().sprite("badge/" + lower(b.id));
  if (art) spr(art, x, y, size, size);
  else R().text(x + size / 2, y + size / 2 - 7, "?", ts(F12, col::white, CENTER));
}
void drawAchIcon(achievements::Id id, float x, float y, float size) {
  Sprite ic = R().sprite(std::string("ach/") + achievements::info(id).snake);
  Sprite b = R().sprite("ach/border");
  const float pad = size * 0.1f;
  if (ic) spr(ic, x + pad, y + pad, size - 2 * pad, size - 2 * pad);
  else gfx::rect(x + pad, y + pad, size - 2 * pad, size - 2 * pad, 0x806020FF);
  if (b) spr(b, x, y, size, size);
}

float easeOutCubic(float t) {
  t = std::clamp(t, 0.f, 1.f);
  return 1 - (1 - t) * (1 - t) * (1 - t);
}
float easeOutBack(float t) {  // a cheap stand-in for Godot's spring transition
  t = std::clamp(t, 0.f, 1.f);
  const float c1 = 1.70158f, c3 = c1 + 1;
  return 1 + c3 * (t - 1) * (t - 1) * (t - 1) + c1 * (t - 1) * (t - 1);
}

// Summary timeline (seconds since the summary opened).
float linesEnd() { return (float)S().lines.size() * kLineStep; }
float badgesStart() { return linesEnd() + kLinesPause + kBadgeWait; }
float achsStart() { return badgesStart() + (float)S().rec.badges.size() * kBadgeStep; }
float doneTime() { return achsStart() + (float)S().achs.size() * kBadgeStep + (S().achs.empty() ? 0.f : 0.25f); }

// NGameOverScreen.GetAscensionMulti.
std::string ascensionMulti(int a) {
  int whole = a / 10 + 1, tenth = a % 10;
  return tenth ? num(whole) + "." + num(tenth) : num(whole);
}

void build(const history::RunRecord& rec) {
  State& s = S();
  s = State{};
  s.open = true;
  s.startTime = rec.startTime;
  s.seed = rec.seed;
  s.runTime = rec.runTime;
  s.win = rec.win;
  s.rec = rec;
  // AnimateScoreLines.
  auto line = [&](const char* icon, const char* key, const char* var, int amount, const std::string& value) {
    s.lines.push_back({std::string("ui/score_") + icon, fill(L(std::string("game_over_screen.SCORE_LINE.") + key), var, num(amount)), value});
  };
  int gold = 0;
  for (auto& act : rec.path)
    for (auto& p : act) gold += p.goldGained;
  line("floor", "floorsClimbed", "FloorCount", rec.floorReached, "+" + num(history::floorScore(rec.path)));
  line("gold", "goldGained", "GoldAmount", gold, "+" + num(history::goldScore(rec.path)));
  int elites = history::elitesKilled(rec.path);
  if (elites > 0) line("elite", "elitesKilled", "EliteCount", elites, "+" + num(elites * 50));
  int bosses = history::bossesSlain(rec.path, rec.win);
  if (bosses > 0) line("boss", "bossesSlain", "BossCount", bosses, "+" + num(bosses * 100));
  if (rec.ascension > 0) line("ascension", "ascension", "AscensionLevel", rec.ascension, "x" + ascensionMulti(rec.ascension));
  // Achievements unlocked since this run started (M5 stamps each unlock with the time).
  for (auto& i : achievements::all())
    if (achievements::isUnlocked(i.id) && rec.startTime > 0 && achievements::unlockTime(i.id) >= rec.startTime) s.achs.push_back(i.id);
  // InitializeBannerAndQuote (Rng.Chaotic in the C#; here picked from the run so it is stable).
  uint64_t pickSeed = rec.seed * 31 + (uint64_t)rec.floorReached;
  if (rec.win) {
    s.banner = L("game_over_screen.BANNER.falseWin");
    long long personal = rec.score;
    if (profiles::diskEnabled()) {
      personal = 0;
      bool stored = false;
      for (auto& r : history::load(profiles::current())) {
        if (r.win) personal += r.score;
        stored = stored || (r.startTime == rec.startTime && r.seed == rec.seed && r.win);
      }
      if (!stored) personal += rec.score;
    }
    s.quote = unescape(L("game_over_screen.VICTORY_DAMAGE_LOCAL"));
    s.quote = fill(fill(s.quote, "PlayerDamage", grouped(rec.score)), "PersonalDamage", grouped(personal));
  } else {
    auto banners = keysWithPrefix("game_over_screen.BANNER.lose", false);
    s.banner = banners.empty() ? "" : L(banners[pickSeed % banners.size()]);
    auto quotes = keysWithPrefix("game_over_screen.QUOTES.", true);
    s.quote = quotes.empty() ? "" : L(quotes[(pickSeed / 7) % quotes.size()]);
  }
  s.encounterQuote = deathQuote(rec);
}
}  // namespace

void App::drawEnd(bool top, bool won) {
  State& s = S();
  // The record of the run that just ended (history::last(), M2); fromRun if none was kept.
  const history::RunRecord* last = history::last();
  history::RunRecord fallback;
  if (!last) {
    fallback = history::fromRun(*run_, won, false);
    fallback.score = history::score(fallback.path, fallback.ascension, won);
    last = &fallback;
  }
  if (!s.open || s.startTime != last->startTime || s.seed != last->seed || s.runTime != last->runTime || s.win != last->win)
    build(*last);
  const history::RunRecord& rec = s.rec;
  const float t = s.summary ? (float)(time_ - s.t0) : 0.f;
  const bool done = s.summary && t >= doneTime();

  if (top) {
    gfx::Texture* bg = R().texture(actTexture(*run_, "bg_"));
    gfx::image(bg, 0, 0, kTop, kH, 0, 0, kTop, kH, s.win ? 0x302000FF : 0x200000FF, 0.6f);
    gfx::rect(0, 0, kTop, kH, s.summary ? 0x000000A0 : 0x00000060);
    // Banner: centred on the intro, moved to the top on the summary (0.5 s ease-out cubic).
    float by = 60, bscale = 1.6f;
    if (s.summary) {
      float k = easeOutCubic(t / kBannerMove);
      by = 60 + (8 - 60) * k;
      bscale = 1.6f + (1.25f - 1.6f) * k;
    }
    TextStyle bt = ts(F16, s.win ? col::gold : col::red, CENTER);
    bt.scale = bscale;
    bt.outline = 0x000000FF;
    R().text(kTop / 2, by, s.banner, bt);
    if (!s.summary) {
      R().text(kTop / 2, 112, s.quote, ts(F12, col::white, CENTER, 360));
      return;
    }
    // The encounter quote fades in under the banner.
    gfx::pushAlpha(std::clamp((t - 0.25f) / 0.5f, 0.f, 1.f));
    R().text(kTop / 2, 38, s.encounterQuote, ts(F12, s.win ? col::green : col::white, CENTER, 370));
    gfx::popAlpha();
    // Score breakdown (NScoreLine: icon, label, "+N" / "xM").
    const float px = 60, pw = kTop - 2 * px, lh = 22;
    const float ph = 12 + lh * (float)s.lines.size() + 34;
    const float py = 62 + std::max(0.f, (kH - style::kMargin - 62 - ph) / 2);
    widgets::panel("ui/hover_tip", px, py, pw, ph);
    for (size_t i = 0; i < s.lines.size(); ++i) {
      float lt = (t - (float)i * kLineStep) / kLineIn;
      if (lt <= 0) break;
      float y = py + 6 + lh * (float)i;
      float dx = (1 - easeOutBack(lt)) * 24;  // slides in from the right
      gfx::pushAlpha(std::clamp(lt, 0.f, 1.f));
      Sprite ic = R().sprite(s.lines[i].icon);
      if (ic) spr(ic, px + 10 + dx, y + 1, 20, 20);
      R().text(px + 36 + dx, y + 3, s.lines[i].label, ts(F12, col::white));
      R().text(px + pw - 12 + dx, y + 3, s.lines[i].value, ts(F12, col::gold, RIGHT));
      gfx::popAlpha();
    }
    float ty = py + ph - 30;
    gfx::rect(px + 8, ty - 3, pw - 16, 1, style::kPanelEdge);
    float k = linesEnd() > 0 ? easeOutCubic(t / linesEnd()) : 1.f;
    int shown = (int)std::lround(rec.score * k);
    R().text(px + 12, ty + 4, tr("分数", "Score"), ts(F16, col::white));
    TextStyle st = ts(F16, col::gold, RIGHT);
    st.scale = 1.25f;
    R().text(px + pw - 12, ty, grouped(shown), st);
    return;
  }

  // ---- bottom ----
  {
    gfx::Texture* bg = R().texture(actTexture(*run_, "bg_"));
    gfx::image(bg, 0, 0, kBot, kH, 40, 0, kBot, kH, s.win ? 0x302000FF : 0x200000FF, 0.6f);
    gfx::rect(0, 0, kBot, kH, 0x000000A0);
  }
  gfx::Input in = gfx::input();
  if (s.swallowTouch) {  // the tap that skipped the count-up does not also press a button
    if (!in.touching && !in.touchDown) s.swallowTouch = false;
    in.touching = in.touchDown = in.touchUp = false;
  }
  if (s.summary && !done && ((in.down & gfx::BTN_A) || in.touchDown)) {
    s.t0 = time_ - doneTime();  // skip to the end
    if (in.touchDown) s.swallowTouch = true;
    in.down &= ~gfx::BTN_A;
    in.touching = in.touchDown = in.touchUp = false;
  }
  {
    int fo = widgets::focused();
    bool ours = fo >= kMenuId && fo < kAch0 + 32;
    if (!ours) widgets::setFocus(-1);
  }
  widgets::beginFrame(in);
  // A without D-pad focus (the kit only presses a focused control in pad mode): the default action.
  const bool aKey = (in.down & gfx::BTN_A) && !widgets::usingPad();
  if (!s.summary) {
    // Intro: a short summary and 继续.
    R().text(kBot / 2, 70, charTitle(rec.character), ts(F16, col::gold, CENTER));
    R().text(kBot / 2, 96, tr("到达第 ", "Reached floor ") + num(rec.floorReached) + tr(" 层", ""), ts(F12, col::white, CENTER));
    if (widgets::button(kContinueId, kBot - style::kMargin - 110, style::kActionY, 110, style::kButtonH,
                        L("game_over_screen.BUTTON.continue"), widgets::Kind::Primary) || aKey) {
      s.summary = true;
      s.t0 = time_;
      widgets::setFocus(-1);
    }
    widgets::endFrame();
    return;
  }

  // Run stats.
  const float sx = style::kMargin, sw = kBot - 2 * style::kMargin;
  std::string mods;
  for (auto& m : rec.modifiers) {
    std::string k = modifiers::titleKey(m);
    mods += (mods.empty() ? "" : tr("、", ", ")) + (R().hasLoc(k) ? L(k) : m);
  }
  const float sh = rec.custom && !mods.empty() ? 62 : 46;
  widgets::panel("ui/hover_tip", sx, 6, sw, sh);
  std::string l1 = "[gold]" + charTitle(rec.character) + "[/gold]";
  if (rec.ascension > 0) l1 += tr("  进阶 ", "  Ascension ") + num(rec.ascension);
  l1 += tr("  第 ", "  Floor ") + num(rec.floorReached) + tr(" 层", "");
  R().text(sx + 8, 11, l1, ts(F12, col::white));
  R().text(sx + sw - 8, 11, tr("用时 ", "Time ") + duration(rec.runTime), ts(F12, col::white, RIGHT));
  std::string seed = rec.seedText.empty() ? std::to_string((unsigned long long)rec.seed) : rec.seedText;
  R().text(sx + 8, 29, tr("种子 ", "Seed ") + seed, ts(F12, col::gray));
  if (!rec.dailyDate.empty()) R().text(sx + sw - 8, 29, tr("每日挑战 ", "Daily ") + rec.dailyDate, ts(F12, col::gold, RIGHT));
  else if (rec.custom) R().text(sx + sw - 8, 29, tr("自定义模式", "Custom mode"), ts(F12, col::purple, RIGHT));
  if (rec.custom && !mods.empty()) {
    gfx::pushClip(sx + 8, 45, sw - 16, 16);
    R().text(sx + 8, 46, mods, ts(F12, col::gray));
    gfx::popClip();
  }

  // Badges (AnimateBadges: each rises in over 0.25 s).
  float y = 6 + sh + 6;
  const int nb = (int)rec.badges.size();
  R().text(sx, y, tr("徽章", "Badges"), ts(F12, col::gold));
  if (nb == 0) R().text(sx + 36, y, rec.abandoned ? tr("放弃的游戏没有徽章", "Abandoned runs earn no badges") : tr("本局没有获得徽章", "No badges this run"), ts(F12, col::gray));
  y += 16;
  if (widgets::usingPad()) {
    int f = widgets::focused();
    if (f >= kBadge0 && f < kBadge0 + nb) s.detail = f - kBadge0;
    else if (f >= kAch0 && f < kAch0 + (int)s.achs.size()) s.detail = kAch0 - kBadge0 + (f - kAch0);
  }
  for (int i = 0; i < nb; ++i) {
    int rowN = std::min(kBadgeCols, nb - i / kBadgeCols * kBadgeCols);
    float rx = (kBot - rowN * kBadgePitch + (kBadgePitch - kBadgeSize)) / 2;
    float bx = rx + (float)(i % kBadgeCols) * kBadgePitch, by = y + (float)(i / kBadgeCols) * kBadgePitch;
    float bt = (t - badgesStart() - (float)i * kBadgeStep) / kBadgeStep;
    if (bt <= 0) continue;
    bool shown = bt >= 1;
    if (widgets::hit(kBadge0 + i, bx - 2, by - 2, kBadgeSize + 4, kBadgeSize + 4, shown)) s.detail = i;
    gfx::pushAlpha(std::clamp(bt, 0.f, 1.f));
    drawBadge(rec.badges[i], bx, by + (1 - easeOutBack(bt)) * 12, kBadgeSize);
    gfx::popAlpha();
    if (shown && s.detail == i) gfx::rect(bx, by + kBadgeSize + 1, kBadgeSize, 2, style::kFocus);
    widgets::focusRing(kBadge0 + i, bx - 2, by - 2, kBadgeSize + 4, kBadgeSize + 4);
  }
  if (nb > 0) y += (float)((nb + kBadgeCols - 1) / kBadgeCols) * kBadgePitch + 2;

  // The picked badge's / achievement's name and description.
  {
    std::string name, desc;
    int d = s.detail;
    if (d < 0 && nb > 0) d = 0;
    if (d >= 0 && d < nb && t >= badgesStart() + (float)d * kBadgeStep) {
      name = badgeText(rec.badges[d], true) + "  (" + std::string(rec.badges[d].rarity == 3 ? tr("金", "Gold") : rec.badges[d].rarity == 2 ? tr("银", "Silver") : tr("铜", "Bronze")) + ")";
      desc = badgeText(rec.badges[d], false);
    } else if (d >= kAch0 - kBadge0 && d - (kAch0 - kBadge0) < (int)s.achs.size()) {
      achievements::Id id = s.achs[d - (kAch0 - kBadge0)];
      name = L(achievements::locKey(id) + ".title");
      desc = L(achievements::locKey(id) + ".description");
    }
    if (!name.empty()) {
      // The description gets what is left above the achievements row (one or two lines).
      const bool achRow = !s.achs.empty() || rec.custom || !rec.dailyDate.empty();
      float room = style::kActionY - 4 - (achRow ? kAchSize + 2 : 0) - (y + 16);
      float dh = 0;
      R().measure(desc, ts(F12, col::white, LEFT, sw), &dh);
      dh = std::min(dh, std::max(14.f, room));
      R().text(sx, y, name, ts(F12, col::gold));
      gfx::pushClip(sx, y + 15, sw, dh + 1);
      R().text(sx, y + 16, desc, ts(F12, col::white, LEFT, sw));
      gfx::popClip();
      y += 16 + dh + 4;
    }
  }

  // Achievements unlocked during this run (M5).
  const bool locked = rec.custom || !rec.dailyDate.empty();
  if (!s.achs.empty() || locked) {
    float ay = std::min(y + 4, style::kActionY - 4 - kAchSize + 4);
    R().text(sx, ay, tr("本局成就", "Achievements"), ts(F12, col::gold));
    if (s.achs.empty()) {
      R().text(sx + 60, ay, tr("自定义与每日挑战不解锁成就", "No achievements in custom or daily runs"), ts(F12, col::gray));
    } else {
      float ax = sx + 60;
      const float right = kBot - style::kMargin;
      for (int i = 0; i < (int)s.achs.size(); ++i) {
        float at = (t - achsStart() - (float)i * kBadgeStep) / kBadgeStep;
        if (at <= 0) break;
        std::string name = L(achievements::locKey(s.achs[i]) + ".title");
        float w = kAchSize + 4 + R().measure(name, ts(F12));
        bool withName = ax + w <= right;
        if (!withName && ax + kAchSize > right) break;
        bool shown = at >= 1;
        if (widgets::hit(kAch0 + i, ax - 2, ay - 6, (withName ? w : kAchSize) + 4, kAchSize + 4, shown)) s.detail = kAch0 - kBadge0 + i;
        gfx::pushAlpha(std::clamp(at, 0.f, 1.f));
        drawAchIcon(s.achs[i], ax, ay - 4 + (1 - easeOutBack(at)) * 8, kAchSize);
        if (withName) R().text(ax + kAchSize + 4, ay, name, ts(F12, col::white));
        gfx::popAlpha();
        widgets::focusRing(kAch0 + i, ax - 2, ay - 6, (withName ? w : kAchSize) + 4, kAchSize + 4);
        ax += (withName ? w : kAchSize) + 8;
      }
    }
  }

  // Action bar: 主菜单 (left), 再来一局 (right, primary); both wait for the count-up.
  bool toMenu = widgets::button(kMenuId, style::kMargin, style::kActionY, 96, style::kButtonH,
                                L("game_over_screen.BUTTON.mainMenu"), widgets::Kind::Secondary, done);
  bool again = widgets::button(kRestartId, kBot - style::kMargin - 110, style::kActionY, 110, style::kButtonH,
                               tr("再来一局", "Play Again"), widgets::Kind::Primary, done);
  if (done && !s.focusSet) {  // A restarts by default, as before
    s.focusSet = true;
    widgets::setFocus(kRestartId);
  }
  widgets::endFrame();
  if (toMenu) { s.open = false; returnTitle(); return; }
  if (again || (done && aKey)) { s.open = false; startRun(); return; }
}

void App::updateEnd(const gfx::Input& in) {
  // Buttons, taps and the skip are handled by the widgets in drawEnd; the extra keys here.
  State& s = S();
  if (!s.open) return;
  const bool done = s.summary && (float)(time_ - s.t0) >= doneTime();
  if (!s.summary) return;
  if ((in.down & gfx::BTN_B) && done) { s.open = false; returnTitle(); return; }
  if ((in.down & gfx::BTN_START) && done) { s.open = false; startRun(); }
}

}  // namespace ui
