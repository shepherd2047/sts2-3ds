// Split from ui.cpp (F3).
#include "../ui_common.h"

namespace ui {

// ================================================================ title

// ================================================================ character select (S04, U04)
// NCharacterSelectScreen: the five characters + Random along the bottom, the ascension panel
// (NAscensionPanel; every level open per the owner) and embark / back. The top screen is the
// character's select scene with the info panel (name, HP, gold, description, starting relic).
// The standard screen has no seed field; the run's seed is shown here and Y re-rolls it.

namespace {
constexpr int kAscMax = 10;  // AscensionManager.maxAscensionAllowed
const char kSeedChars[] = "0123456789ABCDEFGHJKLMNPQRSTUVWXYZ";  // SeedHelper._characters

std::string randomSeed() {  // SeedHelper.GetRandomSeed (no bad-word filter)
  static uint64_t x = (uint64_t)time(nullptr) * 2654435761u + 1;
  std::string s;
  for (int i = 0; i < 12; ++i) {
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    s += kSeedChars[x % (sizeof(kSeedChars) - 1)];
  }
  return s;
}

// The character's first starting relic, for the info panel (CharacterModel.StartingRelics[0]).
Relic* startingRelic(const Character& ch) {
  static std::map<std::string, std::unique_ptr<Relic>> cache;
  auto& r = cache[ch.id];
  if (!r && !ch.startingRelics.empty()) {
    db::init();  // nothing registered before the first run starts
    r = db::relic(ch.startingRelics[0]);
  }
  return r.get();
}
}  // namespace

void App::drawCharacterSelect(bool top) {
  const auto& ids = db::characterIds();
  bool random = titleChar_ >= (int)ids.size();
  const Character* ch = random ? nullptr : &db::character(ids[titleChar_]);
  std::string key = ch ? ch->key : std::string("RANDOM_CHARACTER");
  if (top) {
    std::string lower = ch ? ch->energyColor : std::string();  // energyColor is the lower-case key
    gfx::Texture* bg = ch ? R().texture("gfx/bg_character_" + lower + ".t3t") : nullptr;
    if (bg) gfx::image(bg, 0, 0, kTop, kH, 0, 0, kTop, kH);
    else gfx::image(R().texture("gfx/bg_menu.t3t"), 0, 0, kTop, kH, 0, 0, kTop, kH);
    // Info panel on the left, over a fade (the game slides it in from the left).
    const float pw = 190;
    gfx::gradient(0, 0, pw + 40, kH, 0x000000D0, 0x00000000, 0x000000D0, 0x00000000);
    float y = 10;
    TextStyle nt = ts(F16, col::gold);
    nt.scale = 1.25f;
    R().text(12, y, L("characters." + key + ".title"), nt);
    y += 28;
    std::string stats = ch ? "[icon:hp] " + num(ch->startingHp) + "/" + num(ch->startingHp) + "   [icon:gold] " +
                                 num(ch->startingGold)
                           : std::string("[icon:hp] ??/??   [icon:gold] ???");
    R().text(12, y, stats, ts(F12, col::white));
    y += 20;
    y += R().text(12, y, L("characters." + key + ".description"), ts(F12, col::white, LEFT, pw - 12)) + 10;
    if (Relic* r = ch ? startingRelic(*ch) : nullptr) {
      const float is = 30;
      drawRelicIcon(r, 10, y, is);
      R().text(12 + is + 4, y + (is - R().lineHeight(F12)) / 2, L("relics." + r->locKey + ".title"),
               ts(F12, col::gold));
      y += is + 4;
      R().text(12, y, describeRelic(r), ts(F12, 0xD8D8D8FF, LEFT, pw - 12));
    }
    return;
  }

  gfx::image(R().texture("gfx/bg_menu.t3t"), 40, 240, kBot, kH, 0, 0, kBot, kH);
  gfx::rect(0, 0, kBot, kH, 0x000000B0);
  // Character buttons (char_select_<key>.png, 132x195), the selected one outlined and raised.
  const int n = (int)ids.size() + 1;
  const float bh = 62, bw = bh * 132 / 195, gap = 8, bx0 = (kBot - (n * bw + (n - 1) * gap)) / 2, by = 12;
  for (int i = 0; i < n; ++i) {
    float x = bx0 + i * (bw + gap), yy = by - (i == titleChar_ ? 4 : 0);
    std::string art = i < (int)ids.size() ? "ui/" + db::character(ids[i]).energyColor + "_select" : "ui/random_select";
    if (i == titleChar_) {
      float oh = bh * 207 / 195, ow = oh * 144 / 207;
      spr(R().sprite("ui/char_select_outline"), x - (ow - bw) / 2, yy - (oh - bh) / 2, ow, oh);
    }
    spr(R().sprite(art), x, yy, bw, bh, i == titleChar_ ? 0xFFFFFFFF : 0x000000FF, i == titleChar_ ? 0.f : 0.35f);
    hits_.push_back({x, yy, bw, bh, ID_CHAR0 + i});
  }
  // Ascension panel: arrows around the level icon, the level's title and description beside it.
  const float ay = 84;
  panel(10, ay, kBot - 20, 72);
  button(16, ay + 18, 28, 36, "<", ID_ASC_DOWN, titleAsc_ > 0);
  spr(R().sprite("ui/tb_ascension"), 50, ay + 14, 30, 44);
  R().text(65, ay + 30, num(titleAsc_), ts(F16, col::white, CENTER));
  button(86, ay + 18, 28, 36, ">", ID_ASC_UP, titleAsc_ < kAscMax);
  char lk[32];
  snprintf(lk, sizeof lk, "ascension.LEVEL_%02d", titleAsc_);
  R().text(122, ay + 6, L(std::string(lk) + ".title"), ts(F12, col::gold));
  R().text(122, ay + 24, L(std::string(lk) + ".description"), ts(F12, col::white, LEFT, kBot - 20 - 118));
  // Seed (tap or Y: a new one).
  panel(10, ay + 80, kBot - 20, 24);
  R().text(18, ay + 84, "种子  " + titleSeed_, ts(F12, col::white));
  R().text(kBot - 18, ay + 84, "Y 换一个", ts(F12, col::gray, RIGHT));
  hits_.push_back({10, ay + 80, (float)kBot - 20, 24, ID_SEED});
  button(19, 194, 130, 40, "返回", ID_BACK);
  button(171, 194, 130, 40, "开始", ID_START, true, true);
}

void App::updateCharacterSelect(const gfx::Input& in) {
  const int n = (int)db::characterIds().size() + 1;
  int id = in.touchDown ? hitAt(in.tx, in.ty) : ID_NONE;
  if (id == ID_BACK || (in.down & gfx::BTN_B)) { titleCharacter_ = false; return; }
  if (id >= ID_CHAR0 && id < ID_CHAR0 + n) titleChar_ = id - ID_CHAR0;
  if (in.down & gfx::BTN_LEFT) titleChar_ = (titleChar_ + n - 1) % n;
  if (in.down & gfx::BTN_RIGHT) titleChar_ = (titleChar_ + 1) % n;
  if ((id == ID_ASC_DOWN || (in.down & gfx::BTN_L)) && titleAsc_ > 0) --titleAsc_;
  if ((id == ID_ASC_UP || (in.down & gfx::BTN_R)) && titleAsc_ < kAscMax) ++titleAsc_;
  if (id == ID_SEED || (in.down & gfx::BTN_Y)) titleSeed_ = randomSeed();
  if (id == ID_START || (in.down & (gfx::BTN_A | gfx::BTN_START))) startRun(false);
}

// ================================================================ title

void App::drawTitle(bool top) {
  if (titleCharacter_) { drawCharacterSelect(top); return; }
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
  if (titleCharacter_) { updateCharacterSelect(in); return; }
  int id = in.touchDown ? hitAt(in.tx, in.ty) : ID_NONE;
  if (hasSave_ && (in.down & (gfx::BTN_UP | gfx::BTN_DOWN))) titleSelection_ = 1 - titleSelection_;
  if (id == ID_CONTINUE || ((in.down & gfx::BTN_A) && hasSave_ && titleSelection_ == 0)) {
    startRun(true);
    return;
  }
  if (id == ID_START || (in.down & gfx::BTN_START) || ((in.down & gfx::BTN_A) && (!hasSave_ || titleSelection_ == 1))) {
    titleCharacter_ = true;
    titleSeed_ = randomSeed();
  }
}

}  // namespace ui
