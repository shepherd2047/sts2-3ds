// M11 + S05 (U05): the custom run screen (NCustomRunScreen + NCustomRunModifiersList).
// Top screen: the focused modifier's title and description (NRunModifierTickbox hover), the run
// summary (character, ascension, seed, ticked modifiers) and the disclaimer. Bottom screen: the
// character buttons (+ Random), the ascension arrows, the seed field, the modifier list with
// tickboxes (ModelDb.GoodModifiers then BadModifiers, CharacterCards once per character; ticking
// one of SealedDeck / Draft / Insanity unticks the others), then back / randomize / embark.
// Keys: UP/DOWN focus a modifier, A tick it, LEFT/RIGHT character, L/R ascension, X seed entry
// (the 3DS software keyboard, gfx::textInput; an empty seed = a random one at embark), Y
// randomize (NCustomRunRandomizeButton: a random character and ModifierModel.Pick2Good1Bad),
// START embark, B back. STS_OPEN_CUSTOM=1 opens it from the title (automated previews).
// PORT NOTE: the seed keeps only SeedHelper characters (after CanonicalizeSeed), since saves are
// whitespace-separated tokens; the C#'s LineEdit takes any text.
#include <cstring>

#include "../../core/modifiers.h"
#include "../ui_common.h"

namespace ui {

namespace {
constexpr int kAscMax = 10;
constexpr int kCBack = 2801, kCStart = 2802, kCRandom = 2803, kCSeed = 2804, kCAscDown = 2805, kCAscUp = 2806,
              kCChar0 = 2810,  // + character button
              kCRow0 = 2850;   // + modifier row
constexpr float kListX = 6, kListY = 76, kListW = kBot - 12, kRowH = 21, kListH = 6 * kRowH + 2;
constexpr int kVisible = 6;

struct State {
  bool open = false;
  int character = 0;  // db::characterIds() index, or its size for Random
  int asc = 0;
  std::string seed;   // "" = random at embark
  std::vector<bool> ticked;
  int focus = 0;      // modifier row
  int scroll = 0;     // first visible row
  uint64_t rng = 0;   // Rng.Chaotic stand-in (randomize, random seeds)
};
State& S() {
  static State s;
  return s;
}

const std::vector<std::string>& keys() { return modifiers::allKeys(); }

void tick(State& st, int i, bool on) {
  const auto& k = keys();
  st.ticked[i] = on;
  if (!on) return;
  for (int j = 0; j < (int)k.size(); ++j)  // UntickMutuallyExclusiveModifiersForTickbox
    if (j != i && modifiers::mutuallyExclusive(k[i].substr(0, k[i].find(':')), k[j].substr(0, k[j].find(':'))))
      st.ticked[j] = false;
}

uint64_t nextRand(State& st) {
  if (!st.rng) st.rng = (uint64_t)time(nullptr) * 2654435761u + 7;
  st.rng ^= st.rng << 13;
  st.rng ^= st.rng >> 7;
  st.rng ^= st.rng << 17;
  return st.rng;
}

// NCustomRunScreen randomize: a random character, then ModifierModel.Pick2Good1Bad (two good ones,
// SealedDeck / Draft / Insanity at most once, CharacterCards for a character other than the picked
// one, and one bad one).
void randomize(State& st) {
  const auto& ids = db::characterIds();
  st.character = (int)(nextRand(st) % ids.size());
  const std::string me = ids[st.character];
  std::fill(st.ticked.begin(), st.ticked.end(), false);
  std::vector<std::string> good = {"Draft", "SealedDeck", "Hoarder", "Specialized", "Insanity",
                                   "AllStar", "Flight", "Vintage", "CharacterCards"};
  std::vector<std::string> others;
  for (auto& ch : db::allCharacters()) if (ch != me) others.push_back(ch);
  auto set = [&](const std::string& key) {
    for (int i = 0; i < (int)keys().size(); ++i) if (keys()[i] == key) tick(st, i, true);
  };
  for (int n = 0; n < 2 && !good.empty(); ++n) {
    std::string g = good[nextRand(st) % good.size()];
    good.erase(std::find(good.begin(), good.end(), g));
    if (g == "CharacterCards") g += ":" + others[nextRand(st) % others.size()];
    set(g);
    if (g == "SealedDeck" || g == "Draft" || g == "Insanity")  // MutuallyExclusiveModifiers
      for (const char* x : {"SealedDeck", "Draft", "Insanity"}) good.erase(std::remove(good.begin(), good.end(), x), good.end());
  }
  const char* bad[] = {"DeadlyEvents", "CursedRun", "BigGameHunter", "Midas", "Murderous", "NightTerrors", "Terminal"};
  set(bad[nextRand(st) % 7]);
}

std::string cleanSeed(const std::string& in) {
  std::string c = modifiers::canonicalizeSeed(in), out;
  for (char ch : c)
    if (std::strchr(modifiers::kSeedChars, ch) && ch) out += ch;
  return out;
}

std::string modTitle(const std::string& key) { return L(modifiers::titleKey(key)); }
}  // namespace

void App::openCustomRun() {
  State& st = S();
  st.open = true;
  st.ticked.assign(keys().size(), false);
  st.character = 0;
  st.asc = 0;
  st.seed.clear();
  st.focus = st.scroll = 0;
}

bool App::drawCustomRun(bool top) {
  State& st = S();
  if (!st.open) return false;
  const auto& ids = db::characterIds();
  const auto& k = keys();
  const bool random = st.character >= (int)ids.size();
  const Character* ch = random ? nullptr : &db::character(ids[st.character]);
  if (top) {
    gfx::Texture* bg = ch ? R().texture("gfx/bg_character_" + ch->energyColor + ".t3t") : nullptr;
    if (bg) gfx::image(bg, 0, 0, kTop, kH, 0, 0, kTop, kH);
    else drawMenuBg(true, 0.f);
    gfx::rect(0, 0, kTop, kH, 0x000000A8);
    R().text(10, 6, L("main_menu_ui.CUSTOM_RUN_SCREEN.CUSTOM_MODE_TITLE"), ts(F16, col::gold));
    // Summary: character, ascension, seed.
    std::string who = L("characters." + (ch ? ch->key : std::string("RANDOM_CHARACTER")) + ".title");
    std::string sum = who + "   " + L("main_menu_ui.CUSTOM_RUN_SCREEN.SEED_LABEL") +
                      (st.seed.empty() ? L("main_menu_ui.CUSTOM_RUN_SCREEN.SEED_RANDOM_PLACEHOLDER") : st.seed);
    if (st.asc > 0) {
      char lk[32];
      snprintf(lk, sizeof lk, "ascension.LEVEL_%02d", st.asc);
      sum += "   " + L(std::string(lk) + ".title");
    }
    R().text(kTop - 10, 9, sum, ts(F12, col::white, RIGHT));
    // The focused modifier (NRunModifierTickbox hover tip): title in green (good) / red (bad).
    const std::string& fk = k[std::clamp(st.focus, 0, (int)k.size() - 1)];
    const float px = 10, py = 30, pw = kTop - 20;
    TextStyle dt = ts(F12, col::white, LEFT, pw - 24);
    float dh;
    R().measure(L(modifiers::descriptionKey(fk)), dt, &dh);
    widgets::panel("ui/hover_tip", px, py, pw, 34 + dh);
    R().text(px + 12, py + 7, modTitle(fk), ts(F16, modifiers::isGood(fk) ? 0x7FE07FFF : 0xFF7A6AFF));
    R().text(px + 12, py + 28, L(modifiers::descriptionKey(fk)), dt);
    // The ticked modifiers (MODIFIERS_TITLE).
    float y = py + 44 + dh;
    std::string list;
    for (size_t i = 0; i < k.size(); ++i)
      if (st.ticked[i]) list += (list.empty() ? "" : tr("、", ", ")) + modTitle(k[i]);
    R().text(12, y, L("main_menu_ui.CUSTOM_RUN_SCREEN.MODIFIERS_TITLE") + tr("：", ": ") + (list.empty() ? "-" : list),
             ts(F12, col::gold, LEFT, kTop - 24));
    R().text(12, kH - 34, L("main_menu_ui.CUSTOM_RUN_SCREEN.disclaimer"), ts(F12, col::gray, LEFT, kTop - 24, 0.9f));
    return true;
  }

  drawMenuBg(false, 0.6f);
  // Character buttons (+ Random), the selected one outlined and raised.
  const int n = (int)ids.size() + 1;
  const float bh = 40, bw = bh * 132 / 195, gap = 5, by = 6;
  for (int i = 0; i < n; ++i) {
    float x = 8 + i * (bw + gap), yy = by - (i == st.character ? 3 : 0);
    std::string art = i < (int)ids.size() ? "ui/" + db::character(ids[i]).energyColor + "_select" : "ui/random_select";
    if (i == st.character) {
      float oh = bh * 207 / 195, ow = oh * 144 / 207;
      spr(R().sprite("ui/char_select_outline"), x - (ow - bw) / 2, yy - (oh - bh) / 2, ow, oh);
    }
    spr(R().sprite(art), x, yy, bw, bh, i == st.character ? 0xFFFFFFFF : 0x000000FF, i == st.character ? 0.f : 0.35f);
    hits_.push_back({x, yy, bw, bh, kCChar0 + i});
  }
  // Ascension: < N >.
  const float ax = 8 + n * (bw + gap) + 4;
  button(ax, 10, 24, 30, "<", kCAscDown, st.asc > 0);
  spr(R().sprite("ui/tb_ascension"), ax + 28, 6, 26, 38);
  R().text(ax + 41, 16, num(st.asc), ts(F16, col::white, CENTER));
  button(ax + 58, 10, 24, 30, ">", kCAscUp, st.asc < kAscMax);
  // Seed field.
  panel(kListX, 50, kListW, 22);
  R().text(kListX + 6, 53, L("main_menu_ui.CUSTOM_RUN_SCREEN.SEED_LABEL") +
                               (st.seed.empty() ? "[gray]" + L("main_menu_ui.CUSTOM_RUN_SCREEN.SEED_RANDOM_PLACEHOLDER") + "[/gray]" : st.seed),
           ts(F12, col::white));
  R().text(kListX + kListW - 6, 53, tr("X 输入", "X Enter"), ts(F12, col::gray, RIGHT));
  hits_.push_back({kListX, 50, kListW, 22, kCSeed});
  // Modifier list.
  panel(kListX, kListY, kListW, kListH);
  st.scroll = std::clamp(st.scroll, 0, std::max(0, (int)k.size() - kVisible));
  for (int r = 0; r < kVisible && st.scroll + r < (int)k.size(); ++r) {
    int i = st.scroll + r;
    float y = kListY + 1 + r * kRowH;
    if (i == st.focus) gfx::rect(kListX + 1, y, kListW - 2, kRowH, 0x8A5A2080);
    spr(R().sprite(st.ticked[i] ? "ui/checkbox_on" : "ui/checkbox_off"), kListX + 4, y + 1, kRowH - 2, kRowH - 2);
    uint32_t c = modifiers::isGood(k[i]) ? 0x9FE89FFF : 0xFF9A8AFF;
    R().text(kListX + kRowH + 6, y + (kRowH - R().lineHeight(F12)) / 2, modTitle(k[i]), ts(F12, c));
    hits_.push_back({kListX, y, kListW, kRowH, kCRow0 + i});
  }
  // Scroll thumb.
  if ((int)k.size() > kVisible) {
    float th = kListH * kVisible / (float)k.size();
    float ty = kListY + (kListH - th) * st.scroll / (float)((int)k.size() - kVisible);
    gfx::rect(kListX + kListW - 4, ty, 3, th, 0xB89A60FF);
  }
  button(6, 206, 96, 30, tr("返回", "Back"), kCBack);
  button(112, 206, 96, 30, L("main_menu_ui.CUSTOM_RUN_SCREEN.RANDOMIZE"), kCRandom);
  button(218, 206, 96, 30, tr("开始", "Start"), kCStart, true, true);
  return true;
}

bool App::updateCustomRun(const gfx::Input& in) {
  State& st = S();
  static bool autoOpened = false;  // STS_OPEN_CUSTOM=1: straight to this screen (automated previews)
  if (!autoOpened && getenv("STS_OPEN_CUSTOM")) { autoOpened = true; openCustomRun(); }
  if (!st.open) return false;
  const auto& ids = db::characterIds();
  const auto& k = keys();
  const int nChars = (int)ids.size() + 1, nMods = (int)k.size();
  int id = in.touchDown ? hitAt(in.tx, in.ty) : ID_NONE;
  if (id == kCBack || (in.down & gfx::BTN_B)) { st.open = false; return true; }
  if (id >= kCChar0 && id < kCChar0 + nChars) st.character = id - kCChar0;
  if (in.down & gfx::BTN_LEFT) st.character = (st.character + nChars - 1) % nChars;
  if (in.down & gfx::BTN_RIGHT) st.character = (st.character + 1) % nChars;
  if ((id == kCAscDown || (in.down & gfx::BTN_L)) && st.asc > 0) --st.asc;
  if ((id == kCAscUp || (in.down & gfx::BTN_R)) && st.asc < kAscMax) ++st.asc;
  if (in.down & gfx::BTN_UP) st.focus = (st.focus + nMods - 1) % nMods;
  if (in.down & gfx::BTN_DOWN) st.focus = (st.focus + 1) % nMods;
  if (id >= kCRow0 && id < kCRow0 + nMods) {
    st.focus = id - kCRow0;
    tick(st, st.focus, !st.ticked[st.focus]);
  }
  if (in.down & gfx::BTN_A) tick(st, st.focus, !st.ticked[st.focus]);
  if (st.focus < st.scroll) st.scroll = st.focus;
  if (st.focus >= st.scroll + kVisible) st.scroll = st.focus - kVisible + 1;
  if (id == kCRandom || (in.down & gfx::BTN_Y)) randomize(st);
  if (id == kCSeed || (in.down & gfx::BTN_X)) {
    std::string text;
    if (gfx::textInput(L("main_menu_ui.CUSTOM_RUN_SCREEN.SEED_LABEL").c_str(), st.seed, text, 24)) st.seed = cleanSeed(text);
  }
  if (id == kCStart || (in.down & gfx::BTN_START)) {
    // StartRunLobby.BeginRunLocally: the canonical seed, or SeedHelper.GetRandomSeed when empty.
    titleChar_ = st.character;
    titleAsc_ = st.asc;
    titleSeed_ = st.seed.empty() ? [&] { uint64_t x = nextRand(st); return modifiers::randomSeed(x); }() : st.seed;
    titleModifiers_.clear();
    for (int i = 0; i < nMods; ++i) if (st.ticked[i]) titleModifiers_.push_back(k[i]);
    titleCustom_ = true;
    st.open = false;
    startRun(false);
  }
  return true;
}

}  // namespace ui
