// Y2: the pause menu (START during a run, or 暂停 on the map HUD). NPauseMenu: resume, settings,
// compendium, give up (NAbandonRunConfirmPopup), save & quit (NGame.ReturnToMainMenu); this port
// adds 地图 (the look-only map from another room, what START used to open) and 牌组. The room
// stays drawn underneath; the scheduler is frozen and the run timer stops while it is open
// (RunManager.IsPaused). Map, deck and settings open on top of it and fall back to it on close.
#include "../../core/save_errors.h"
#include "../confirm.h"
#include "../ui_common.h"

namespace ui {

namespace {
enum : int {
  kPResume = 2401, kPMap, kPDeck, kPSettings, kPCompendium, kPGiveUp, kPSaveQuit,
};

struct PauseItem {
  int id;
  std::string label;
  bool enabled;
  const char* note;  // right-aligned grey note (disabled reason), or null
};

std::string gp(const char* key) { return L(std::string("gameplay_ui.PAUSE_MENU.") + key); }
std::string mm(const char* key) { return L(std::string("main_menu_ui.") + key); }

// The run save is written at the map choice only (Run::onSavePoint); save & quit keeps the last
// one, so a run that has not reached its first save point yet (Neow) has nothing to resume. With
// saves off (automated previews / STS_NO_SAVE) it just returns to the menu.
bool savesOff() { return getenv("STS_HIDDEN") || getenv("STS_NO_SAVE") || !sts::saveerr::storageAvailable(); }  // Y5: SD check

void outline(float x, float y, float w, float h, uint32_t c = col::gold, float t = 2) {
  gfx::rect(x - t, y - t, w + 2 * t, t, c);
  gfx::rect(x - t, y + h, w + 2 * t, t, c);
  gfx::rect(x - t, y, t, h, c);
  gfx::rect(x + w, y, t, h, c);
}
}  // namespace

static std::vector<PauseItem> pauseItems(Screen scr, bool canSaveQuit) {
  std::vector<PauseItem> v;
  v.push_back({kPResume, gp("RESUME"), true, nullptr});
  if (scr != Screen::Map) v.push_back({kPMap, tr("地图", "Map"), true, nullptr});  // on the map it is the screen itself
  v.push_back({kPDeck, tr("牌组", "Deck"), true, nullptr});
  v.push_back({kPSettings, gp("SETTINGS"), true, nullptr});
  // 百科大全 opens the card library (M8); the other compendium pages (relics, potions, bestiary) come later.
  v.push_back({kPCompendium, gp("COMPENDIUM"), true, nullptr});
  v.push_back({kPGiveUp, gp("GIVE_UP"), true, nullptr});
  v.push_back({kPSaveQuit, gp("SAVE_AND_QUIT"), canSaveQuit, canSaveQuit ? nullptr : tr("尚无存档", "No save yet")});
  return v;
}

void App::openPause() {
  pauseOpen_ = true;
  pauseSel_ = 0;
  // Close the room's own overlays so the menu is on top; combat input in flight is dropped.
  deckOpen_ = relicsOpen_ = potionsOpen_ = false;
  potionAim_ = false;
  cardListMode_ = CardListMode::Deck;
  drag_ = {};
  aiming_ = false;
  sel_ = -1;
  mapTouch_ = {};
}

void App::drawPause(bool top) {
  const auto items = pauseItems(run_->screen, hasSave() || savesOff());
  pauseSel_ = std::clamp(pauseSel_, 0, (int)items.size() - 1);
  gfx::rect(0, 0, top ? kTop : kBot, kH, 0x000000B8);
  if (top) {
    R().text(kTop / 2, 48, gp("PAUSED"), ts(F16, col::gold, CENTER, 0, 1.6f));
    const Run& r = *run_;
    std::string act = r.act().name;
    for (char& c : act) c = (char)std::toupper((unsigned char)c);
    std::string info = L("characters." + r.character().key + ".title") + " · " + L("acts." + act + ".title") + tr(" · 第 ", " · Floor ") +
                       num(r.floor) + tr(" 层", "");
    R().text(kTop / 2, 92, info, ts(F12, col::white, CENTER));
    std::string tip;
    switch (items[pauseSel_].id) {
      case kPMap: tip = tr("查看本幕地图。", "View this act's map."); break;
      case kPDeck: tip = tr("查看你的牌组。", "View your deck."); break;
      case kPCompendium: tip = tr("查看卡牌总览。", "Browse the card library."); break;
      case kPGiveUp: tip = mm("ABANDON_RUN_CONFIRMATION.body"); break;
      case kPSaveQuit:
        tip = items[pauseSel_].enabled ? tr("返回主菜单。继续游戏时从上一个存档点（地图）开始。", "Return to the main menu. Continuing resumes from the last save point (the map).")
                                       : tr("到达地图后才会存档。", "The game is saved once you reach the map.");
        break;
      default: break;
    }
    if (!tip.empty()) R().text(kTop / 2, 140, tip, ts(F12, col::gray, CENTER, kTop - 80));
    return;
  }
  hits_.clear();  // the room underneath registered its own buttons; only the menu is live
  const int n = (int)items.size();
  const float w = 200, h = 26, gap = 4, x = (kBot - w) / 2;
  float y = (kH - (n * (h + gap) - gap)) / 2;
  for (int i = 0; i < n; ++i, y += h + gap) {
    const auto& it = items[i];
    widgets::panel("ui/btn_row", x, y, w, h, it.enabled ? 0xFFFFFFFF : 0x808080FF);
    uint32_t c = !it.enabled ? col::gray : it.id == kPGiveUp ? col::red : col::white;
    R().text(x + w / 2, y + (h - R().lineHeight(F16)) / 2, it.label, ts(F16, c, CENTER));
    if (it.note) R().text(x + w - 8, y + (h - R().lineHeight(F12)) / 2, it.note, ts(F12, col::gray, RIGHT));
    if (i == pauseSel_ && !confirm::isOpen()) outline(x, y, w, h);
    hits_.push_back({x, y, w, h, it.id});
  }
}

void App::updatePause(const gfx::Input& in) {
  int id = in.touchDown ? hitAt(in.tx, in.ty) : ID_NONE;
  const auto items = pauseItems(run_->screen, hasSave() || savesOff());
  const int n = (int)items.size();
  if (in.down & gfx::BTN_UP) pauseSel_ = (pauseSel_ + n - 1) % n;  // NPauseMenu: focus wraps
  if (in.down & gfx::BTN_DOWN) pauseSel_ = (pauseSel_ + 1) % n;
  pauseSel_ = std::clamp(pauseSel_, 0, n - 1);
  if (id == ID_NONE && (in.down & gfx::BTN_A)) id = items[pauseSel_].id;
  if (id == ID_NONE && (in.down & (gfx::BTN_B | gfx::BTN_START))) id = kPResume;  // back / pause toggles
  const PauseItem* picked = nullptr;
  for (int i = 0; i < n; ++i)
    if (items[i].id == id) { pauseSel_ = i; picked = &items[i]; }
  if (!picked) return;
  if (!picked->enabled) return;
  switch (id) {
    case kPResume: pauseOpen_ = false; break;
    case kPMap: mapView_ = true; mapTouch_ = {}; mapUserScroll_ = false; break;
    case kPDeck: openCardList(CardListMode::Deck); break;
    case kPSettings: settingsOpen_ = true; break;
    case kPCompendium: openCardLibrary(); break;
    case kPGiveUp:  // NAbandonRunConfirmPopup (S22 shared modal); M1/M2: a loss (+ runsAbandoned) and a history entry
      confirm::ask(mm("ABANDON_RUN_CONFIRMATION.header"), mm("ABANDON_RUN_CONFIRMATION.body"), [this] {
        run_->abandon();
        returnTitle();  // saves progress.sav
      });
      break;
    case kPSaveQuit: returnTitle(true); break;  // run.sav stays: 继续 resumes at the last map choice
    default: break;
  }
}

}  // namespace ui
