// Split from ui.cpp (F3).
#include <cstring>

#include "../../core/progress.h"
#include "../ui_common.h"

namespace ui {

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

// ================================================================ main menu (S02, U02)
// NMainMenu: the game's text buttons (cream labels, gold + 1.05x when focused, reticles either
// side, a quarter alpha when disabled) over the two-screen main menu background, the logo on
// the top screen. Buttons (NMainMenu.RefreshButtons): with a run save 继续游戏 + 放弃当前游戏
// replace 单人模式; then 百科大全 / 统计 / 设置 / 退出. RGDSplus U02 puts the buttons on the
// bottom screen at 1.65x: the game's 50 px rows at 1080p are 11 px on a 240 px screen, x1.65
// ~18 px of text in 32 px rows (the touch minimum). Submenus (NSingleplayerSubmenu,
// NCompendiumSubmenu) are NSubmenuButton cards on the bottom, the focused one's description on
// top. Not ported yet (daily, custom, compendium screens M8-M10, stats M6, history M2): shown,
// dimmed, a "未完成" toast when picked.

namespace {
constexpr int kMContinue = 2001, kMAbandon = 2002, kMSingle = 2003, kMCompendium = 2004, kMStats = 2005,
              kMSettings = 2006, kMQuit = 2007, kMProfile = 2008;
constexpr int kSStandard = 2101, kSDaily = 2102, kSCustom = 2103, kSCards = 2111,  // + 0..3: the four cards
              kSStats = 2115, kSHistory = 2116, kSBack = 2199;
constexpr int kDCancel = 2201, kDConfirm = 2202;
constexpr float kRowH = 32, kLabelScale = 1.2f;
constexpr uint32_t kCream = col::white;  // StsColors.cream

struct MenuItem { int id; const char* key; };

std::vector<MenuItem> menuItems(bool save) {
  std::vector<MenuItem> v;
  if (save) {
    v.push_back({kMContinue, "CONTINUE"});
    v.push_back({kMAbandon, "ABANDON_RUN"});
  } else {
    v.push_back({kMSingle, "SINGLE_PLAYER"});
  }
  v.push_back({kMCompendium, "COMPENDIUM"});
  v.push_back({kMStats, "STATISTICS"});
  v.push_back({kMSettings, "SETTINGS"});
  v.push_back({kMQuit, "QUIT"});
  return v;
}
float menuY0(int n) { return std::round((kH - n * kRowH) / 2.f); }

// NSubmenuButton cards: big art cards (icon + title) and the compendium's short buttons.
struct SubItem { int id; float x, y, w, h; const char* sprite; const char* key; bool ported; bool shortButton; };

std::vector<SubItem> subItems(int sub) {
  std::vector<SubItem> v;
  if (sub == 1) {  // NSingleplayerSubmenu: standard / daily / custom
    const float w = 96, h = 150, gap = 8, x0 = (kBot - (3 * w + 2 * gap)) / 2, y = 18;
    v.push_back({kSStandard, x0, y, w, h, "ui/sub_standard", "STANDARD", true, false});
    v.push_back({kSDaily, x0 + (w + gap), y, w, h, "ui/sub_daily", "DAILY", false, false});
    v.push_back({kSCustom, x0 + 2 * (w + gap), y, w, h, "ui/sub_custom", "CUSTOM", false, false});
  } else {  // NCompendiumSubmenu: four art cards, then stats and run history
    const float w = 72, h = 116, gap = 6, x0 = (kBot - (4 * w + 3 * gap)) / 2, y = 10;
    const char* spr4[] = {"ui/sub_card_library", "ui/sub_relic_collection", "ui/sub_potion_lab", "ui/sub_bestiary"};
    const char* key4[] = {"COMPENDIUM_CARD_LIBRARY", "COMPENDIUM_RELIC_COLLECTION", "COMPENDIUM_POTION_LAB",
                          "COMPENDIUM_BESTIARY"};
    for (int i = 0; i < 4; ++i) v.push_back({kSCards + i, x0 + i * (w + gap), y, w, h, spr4[i], key4[i], i == 0, false});
    const float sw = (kBot - 2 * x0 - gap) / 2, sy = y + h + 10;
    v.push_back({kSStats, x0, sy, sw, 44, "ui/sub_stats", "STATISTICS", true, true});
    v.push_back({kSHistory, x0 + sw + gap, sy, sw, 44, "ui/sub_history", "RUN_HISTORY", true, true});
  }
  v.push_back({kSBack, 8, 198, 96, 34, "ui/btn_back", nullptr, true, true});
  return v;
}

// D-pad: the nearest item in the pressed direction (centre distance, off-axis weighted).
int navigate(const std::vector<SubItem>& items, int cur, uint32_t down) {
  float dx = (down & gfx::BTN_RIGHT) ? 1.f : (down & gfx::BTN_LEFT) ? -1.f : 0.f;
  float dy = (down & gfx::BTN_DOWN) ? 1.f : (down & gfx::BTN_UP) ? -1.f : 0.f;
  if ((dx == 0 && dy == 0) || cur < 0 || cur >= (int)items.size()) return cur;
  const SubItem& c = items[cur];
  float cx = c.x + c.w / 2, cy = c.y + c.h / 2;
  int best = cur;
  float bestScore = 1e9f;
  for (int i = 0; i < (int)items.size(); ++i) {
    if (i == cur) continue;
    float ox = items[i].x + items[i].w / 2 - cx, oy = items[i].y + items[i].h / 2 - cy;
    float along = ox * dx + oy * dy, across = std::abs(ox * dy) + std::abs(oy * dx);
    if (along <= 1) continue;
    float score = along + across * 2;
    if (score < bestScore) { bestScore = score; best = i; }
  }
  return best;
}

std::string stripBold(std::string s) {
  for (const char* tag : {"[b]", "[/b]"})
    for (size_t p; (p = s.find(tag)) != std::string::npos;) s.erase(p, strlen(tag));
  return s;
}

std::string mm(const std::string& key) { return L("main_menu_ui." + key); }

// Gold outline around a focused card / button.
void outline(float x, float y, float w, float h, uint32_t c = col::gold, float t = 2) {
  gfx::rect(x - t, y - t, w + 2 * t, t, c);
  gfx::rect(x - t, y + h, w + 2 * t, t, c);
  gfx::rect(x - t, y, t, h, c);
  gfx::rect(x + w, y, t, h, c);
}

// NMainMenu.SingleplayerButtonPressed: a profile without any finished run goes straight to
// character select; afterwards the singleplayer submenu opens.
int finishedRuns() {
  int n = 0;
  for (auto& [id, c] : progress::state().characters) n += c.wins + c.losses;
  return n;
}
}  // namespace

// The tall main menu background (gfx/bg_menu.t3t, 400x480: top screen = rows 0..239, bottom =
// rows 240..479 at x 40..360) plus a scrim of `dim` (0..1). Also used behind the settings page.
void App::drawMenuBg(bool top, float dim) {
  if (top) gfx::image(R().texture("gfx/bg_menu.t3t"), 0, 0, kTop, kH, 0, 0, kTop, kH);
  else gfx::image(R().texture("gfx/bg_menu.t3t"), kBotOX, kH, kBot, kH, 0, 0, kBot, kH);
  if (dim > 0) gfx::rect(0, 0, top ? kTop : kBot, kH, (uint32_t)(std::clamp(dim, 0.f, 1.f) * 255));
}

// NContinueRunInfo: character, act and floor, HP, gold and ascension of the saved run, read
// once per visit to the menu (a throwaway Run loads the save).
void App::refreshContinueInfo() {
  continueInfo_ = " ";
  continueIcon_.clear();
  std::string data;
  if (!readRunSave(data)) return;
  Run peek;
  if (!peek.load(data) || !peek.player) return;
  const Character& ch = peek.character();
  continueIcon_ = "ui/char_" + ch.energyColor;
  std::string act = peek.act().name;
  for (char& c : act) c = (char)std::toupper((unsigned char)c);
  std::string s = L("characters." + ch.key + ".title");
  if (peek.ascension > 0) s += "   " + stripBold(mm("CONTINUE_RUN_INFO.ascension")) + " " + num(peek.ascension);
  s += "\n" + L("acts." + act + ".title") + " [blue]- " + mm("CONTINUE_RUN_INFO.floor") + " " + num(peek.floor) + "[/blue]";
  s += "\n[icon:hp] [red]" + num(peek.player->hp) + "/" + num(peek.player->maxHp) + "[/red]   [icon:gold] [gold]" +
       num(peek.gold) + "[/gold]";
  continueInfo_ = s;
}

void App::drawTitle(bool top) {
  if (titleCharacter_) { drawCharacterSelect(top); return; }
  if (drawProfiles(top)) return;  // S03
  if (drawStats(top)) return;     // M6
  const bool sub = menuSub_ != 0;
  if (top) {
    drawMenuBg(true, sub ? 0.35f : 0.f);
    Sprite logo = R().sprite("ui/menu_logo");
    if (logo) spr(logo, std::round((kTop - logo.w) / 2), 14, -1, -1, 0xFFFFFFFF);
    if (sub) {
      // The focused card's title and description (NSubmenuButton hover).
      auto items = subItems(menuSub_);
      const SubItem& it = items[std::clamp(subSel_, 0, (int)items.size() - 1)];
      if (!it.key) return;
      std::string desc = mm(std::string(it.key) + ".description");
      for (size_t p; (p = desc.find("\n\n")) != std::string::npos;) desc.erase(p, 1);  // DAILY's blank line
      const float px = 60, pw = kTop - 120;
      TextStyle dt = ts(F12, col::white, LEFT, pw - 24);
      float dh;
      R().measure(desc, dt, &dh);
      const float ph = 36 + dh, py = 234 - ph;  // grows upwards from the bottom edge
      widgets::panel("ui/hover_tip", px, py, pw, ph);
      std::string title = std::string(it.key) == "STATISTICS" ? mm("STATISTICS.title") : mm(std::string(it.key) + ".title");
      R().text(px + 12, py + 7, title, ts(F16, col::gold));
      if (!it.ported) R().text(px + pw - 12, py + 9, "未完成", ts(F12, col::gray, RIGHT));
      R().text(px + 12, py + 28, desc, dt);
      return;
    }
    if (hasSave_) {
      if (continueInfo_.empty()) refreshContinueInfo();
      if (continueInfo_.size() > 1) {
        const float px = 84, py = 166, pw = kTop - 168, ph = 64;
        widgets::panel("ui/hover_tip", px, py, pw, ph);
        Sprite icon = R().sprite(continueIcon_);
        if (icon) spr(icon, px + 10, py + 8, 24, 24);
        R().text(px + 40, py + 6, continueInfo_, ts(F12, col::white, LEFT, pw - 48));
      }
    }
    return;
  }

  // Bottom: the tower continues from the top screen; a soft dark band behind the button column.
  drawMenuBg(false, sub || menuModal_ ? 0.45f : 0.2f);
  if (!sub) {
    auto items = menuItems(hasSave_);
    const int n = (int)items.size();
    const float y0 = menuY0(n);
    titleSelection_ = std::clamp(titleSelection_, 0, n - 1);
    for (int i = 0; i < n; ++i) {
      const float y = y0 + i * kRowH;
      const bool focus = i == titleSelection_ && !menuModal_;
      TextStyle st = ts(F16, focus ? col::gold : kCream, CENTER, 0, kLabelScale * (focus ? 1.05f : 1.f));
      st.outline = 0x000000FF;  // the labels sit straight on the tower art
      std::string label = mm(items[i].key);
      float th, tw = R().measure(label, st, &th);
      R().text(kBot / 2, y + (kRowH - th) / 2, label, st);
      hits_.push_back({kBot / 2 - 100, y, 200, kRowH, items[i].id});
      if (focus) {
        // ButtonReticleLeft/Right: 28 units outside the label, eased towards the focused row.
        float target = y + (kRowH - 20) / 2;
        reticleY_ = reticleY_ < 0 ? target : reticleY_ + (target - reticleY_) * 0.35f;
        spr(R().sprite("ui/menu_reticle"), kBot / 2 - tw / 2 - 26, reticleY_, 20, 20, col::gold, 1.f);
        spr(R().sprite("ui/menu_reticle_r"), kBot / 2 + tw / 2 + 6, reticleY_, 20, 20, col::gold, 1.f);
      }
    }
    drawProfileChip(kMProfile);  // S03: NOpenProfileScreenButton
  } else {
    auto items = subItems(menuSub_);
    subSel_ = std::clamp(subSel_, 0, (int)items.size() - 1);
    for (int i = 0; i < (int)items.size(); ++i) {
      const SubItem& it = items[i];
      const bool focus = i == subSel_ && !menuModal_;
      float y = it.y - (focus && !it.shortButton ? 3 : 0);
      if (it.id == kSBack) {
        widgets::panel("ui/btn_back", it.x, y, it.w, it.h);
        R().text(it.x + it.w / 2, y + (it.h - R().lineHeight(F16)) / 2, "返回", ts(F16, col::white, CENTER));
      } else if (it.shortButton) {  // NCompendiumBottomButton: green plate, icon + label
        spr(R().sprite("ui/btn_compendium"), it.x, y, it.w, it.h, 0xFFFFFFFF);
        Sprite icon = R().sprite(it.sprite);
        if (icon) spr(icon, it.x + 8, y + (it.h - icon.h) / 2, -1, -1, it.ported ? 0xFFFFFFFF : 0xFFFFFF90);
        std::string title = std::string(it.key) == "STATISTICS" ? mm("STATISTICS.title") : mm(std::string(it.key) + ".title");
        R().text(it.x + 58, y + (it.h - R().lineHeight(F16)) / 2, title, ts(F16, it.ported ? kCream : 0xFFF6E2A0));
      } else {  // NSubmenuButton: parchment panel, the art, the title under it
        widgets::panel("ui/panel_submenu", it.x, y, it.w, it.h);
        const float iw = it.w - 12, ih = iw * 64 / 84;
        Sprite icon = R().sprite(it.sprite);
        if (icon) spr(icon, it.x + 6, y + 8, iw, ih, it.ported ? 0xFFFFFFFF : 0x202020FF, it.ported ? 0.f : 0.45f);
        if (!it.ported) {
          Sprite lock = R().sprite("ui/sub_lock");
          if (lock) spr(lock, it.x + (it.w - lock.w) / 2, y + 8 + (ih - lock.h) / 2);
        }
        TextStyle tt = ts(it.w > 80 ? F16 : F12, col::dark, CENTER, it.w - 8);
        tt.shadow = false;
        R().text(it.x + it.w / 2, y + 14 + ih, mm(std::string(it.key) + ".title"), tt);
        if (!it.ported) {
          TextStyle nt = ts(F12, 0x8A3A2CFF, CENTER);
          nt.shadow = false;
          R().text(it.x + it.w / 2, y + 18 + ih + R().lineHeight(tt.size), "未完成", nt);
        }
      }
      if (focus) outline(it.x, y, it.w, it.h);
      hits_.push_back({it.x, y, it.w, it.h, it.id});
    }
  }

  if (menuModal_) {  // NGenericPopup / NAbandonRunConfirmPopup
    const bool quit = menuModal_ == 2;
    gfx::rect(0, 0, kBot, kH, 0x000000C0);
    const float w = 260, h = 140, x = (kBot - w) / 2, y = 46;
    widgets::panel("ui/panel_popup", x, y, w, h);
    R().text(x + w / 2, y + 16, mm(quit ? "QUIT_CONFIRM_POPUP.header" : "ABANDON_RUN_CONFIRMATION.header"),
             ts(F16, col::gold, CENTER, 0, 1.1f));
    R().text(x + w / 2, y + 48, mm(quit ? "QUIT_CONFIRM_POPUP.body" : "ABANDON_RUN_CONFIRMATION.body"),
             ts(F12, col::white, CENTER, w - 32));
    const float bw = 104, bh = 34, by = y + h - bh - 14;
    struct { int id; const char* sprite; const char* key; float x; } btn[2] = {
        {kDCancel, "ui/btn_cancel_s", "GENERIC_POPUP.cancel", x + 18},
        {kDConfirm, "ui/btn_ok_s", "GENERIC_POPUP.confirm", x + w - 18 - bw}};
    for (int i = 0; i < 2; ++i) {
      widgets::panel(btn[i].sprite, btn[i].x, by, bw, bh);
      R().text(btn[i].x + bw / 2, by + (bh - R().lineHeight(F16)) / 2, mm(btn[i].key), ts(F16, col::white, CENTER));
      if (i == menuModalSel_) outline(btn[i].x, by, bw, bh);
      hits_.push_back({btn[i].x, by, bw, bh, btn[i].id});
    }
  }
}

void App::activateMenu(int id) {
  auto notDone = [&](const std::string& what) { toast_ = what + " · 未完成"; toastT_ = 1.2f; };
  switch (id) {
    case kMContinue: startRun(true); break;
    case kMAbandon: menuModal_ = 1; menuModalSel_ = 0; break;
    case kMSingle:
      if (finishedRuns() > 0) { menuSub_ = 1; subSel_ = 0; }
      else { titleCharacter_ = true; titleSeed_ = randomSeed(); }
      break;
    case kMCompendium: menuSub_ = 2; subSel_ = 0; break;
    case kMStats: openStats(1); break;  // M6
    case kMSettings: settingsOpen_ = true; abandonConfirm_ = false; break;
    case kMQuit: menuModal_ = 2; menuModalSel_ = 0; break;
    case kMProfile: openProfiles(); break;
    case kSStandard: titleCharacter_ = true; titleSeed_ = randomSeed(); break;
    case kSCards: openCardLibrary(); break;  // M8
    case kSBack: menuSub_ = 0; break;
    case kSStats: openStats(1); break;    // M6
    case kSHistory: openStats(2); break;  // M6
    default:
      for (auto& it : subItems(menuSub_))
        if (it.id == id && it.key)
          notDone(std::string(it.key) == "STATISTICS" ? mm("STATISTICS.title") : mm(std::string(it.key) + ".title"));
      break;
  }
}

void App::updateTitle(const gfx::Input& in) {
  if (titleCharacter_) { updateCharacterSelect(in); return; }
  if (updateProfiles(in)) return;  // S03
  if (updateStats(in)) return;     // M6
  int id = in.touchDown ? hitAt(in.tx, in.ty) : ID_NONE;
  if (menuModal_) {
    if (in.down & (gfx::BTN_LEFT | gfx::BTN_RIGHT)) menuModalSel_ = 1 - menuModalSel_;
    if (in.down & gfx::BTN_A) id = menuModalSel_ ? kDConfirm : kDCancel;
    if ((in.down & gfx::BTN_B) || id == kDCancel) { menuModal_ = 0; return; }
    if (id == kDConfirm) {
      if (menuModal_ == 2) quit_ = true;   // NGame.Quit: back to the Homebrew menu / closes the preview
      else returnTitle();                  // AbandonRun: the save goes, the menu shows 单人模式 again
      menuModal_ = 0;
      titleSelection_ = 0;
    }
    return;
  }
  if (menuSub_) {
    auto items = subItems(menuSub_);
    subSel_ = navigate(items, std::clamp(subSel_, 0, (int)items.size() - 1), in.down);
    if (id == ID_NONE && (in.down & gfx::BTN_A)) id = items[subSel_].id;
    if (in.down & gfx::BTN_B) id = kSBack;
    for (int i = 0; i < (int)items.size(); ++i)
      if (items[i].id == id) subSel_ = i;
    if (id != ID_NONE) activateMenu(id);
    return;
  }
  auto items = menuItems(hasSave_);
  const int n = (int)items.size();
  if (in.down & gfx::BTN_UP) titleSelection_ = (titleSelection_ + n - 1) % n;
  if (in.down & gfx::BTN_DOWN) titleSelection_ = (titleSelection_ + 1) % n;
  titleSelection_ = std::clamp(titleSelection_, 0, n - 1);
  if (id == ID_NONE && (in.down & gfx::BTN_A)) id = items[titleSelection_].id;
  // START: a new run straight away (or continue the saved one), as before the menu existed.
  if (id == ID_NONE && (in.down & gfx::BTN_START)) id = hasSave_ ? kMContinue : kMSingle;
  // B: the profile button's hotkey (NOpenProfileScreenButton.Hotkeys = pauseAndBack).
  if (id == ID_NONE && (in.down & gfx::BTN_B)) id = kMProfile;
  for (int i = 0; i < n; ++i)
    if (items[i].id == id) titleSelection_ = i;
  if (id != ID_NONE) activateMenu(id);
}

}  // namespace ui
