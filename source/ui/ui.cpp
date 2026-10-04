// Split from ui.cpp (F3).
#include "../core/abandon_hook.h"
#include "../core/events_crystal.h"
#include "../core/modifiers.h"
#include "../core/profiles.h"
#include "../core/safe_file.h"
#include "../core/save_errors.h"
#include "../core/settings_store.h"
#include "confirm.h"
#include "music_router.h"
#include "ui_common.h"

namespace ui {

// Per-act art: gfx/bg_<act>.t3t (room) and gfx/bg_map_<act>.t3t (map paper). The previous
// act's textures are freed when the act changes (3DS linear memory is small).
std::string actTexture(const Run& r, const char* kind) {
  static std::string loadedAct;
  if (loadedAct != r.act().key) {
    if (!loadedAct.empty()) {
      R().releaseTexture("gfx/bg_" + loadedAct + ".t3t");
      R().releaseTexture("gfx/bg_map_" + loadedAct + ".t3t");
    }
    loadedAct = r.act().key;
  }
  return std::string("gfx/") + kind + r.act().key + ".t3t";
}

// ================================================================ setup

// Saves: the run is written at every map choice (Run::onSavePoint) and deleted when it
// ends. Automated previews (STS_HIDDEN) and STS_NO_SAVE neither read nor write it (nor
// settings.sav / profile.sav / progress.sav -- see App::init/saveSettings below).
// Y4: run.sav and progress.sav are per profile (profiles.h: <save dir>/profile<N>/...);
// progress.sav is written with the run save and when a run ends.
namespace {
std::string saveName() { return profiles::runSaveName(); }
// Y5: also off for the session when the startup SD check failed (App::init).
bool savesEnabled() { return !getenv("STS_HIDDEN") && !getenv("STS_NO_SAVE") && saveerr::storageAvailable(); }
// S22: a failed SD write (or STS_FAKE_SAVE_ERROR=write) queues the write-error dialog.
bool noteWrite(bool ok) {
  if (!ok || saveerr::faked(saveerr::Kind::WriteFailed)) saveerr::report(saveerr::Kind::WriteFailed);
  return ok;
}
}

bool App::init() {
  auto loading = beginBoot();  // S01: loading frames, then the splash (not in automated previews)
  // Y1: settings.sav, loaded once at startup. STS_HIDDEN/STS_NO_SAVE (automated previews, and the
  // headless sim which never links ui.cpp at all) must never touch the real player's file; the
  // in-memory settings::state() then just keeps its defaults for that run of the app.
  // S22: a garbled settings.sav is moved to settings.corrupt and reported; defaults are kept.
  if (savesEnabled() && saveerr::loadChecked(settings::defaultPath(), [](const std::string& p) { return settings::load(p); },
                                             saveerr::Kind::SettingsCorrupt) == saveerr::Load::Corrupt)
    settings::reset();
  // Y3: the saved language's text and glyph pages load first (STS_LANG=en|zh overrides, for previews).
  if (const char* l = getenv("STS_LANG"))
    settings::state().language = (l[0] == 'e' || l[0] == 'E') ? Language::En : Language::ZhCN;
  if (!R().load(loading, settings::state().language == Language::En ? 1 : 0)) return false;
  run_ = std::make_unique<Run>();
  // Trial's DOUBLE_DOWN (C# NAbandonRunConfirmPopup): the pause menu's 放弃 confirm, opened by an event.
  askAbandonRun = [this] {
    confirm::ask(L("main_menu_ui.ABANDON_RUN_CONFIRMATION.header"), L("main_menu_ui.ABANDON_RUN_CONFIRMATION.body"), [this] {
      run_->abandon();
      returnTitle();
    });
  };
  autoplay_ = getenv("STS_AUTOPLAY") != nullptr;
  // Y5: the SD card missing, locked or full at startup: one notice, then the game runs without
  // reading or writing any save (savesEnabled() is false from here on). STS_FAKE_SAVE_ERROR=sd.
  if (savesEnabled() && !safefile::probeWritable(profiles::defaultRoot())) {
    saveerr::setStorageAvailable(false);
    saveerr::report(saveerr::Kind::SdUnavailable);
  }
  if (saveerr::faked(saveerr::Kind::SdUnavailable)) saveerr::report(saveerr::Kind::SdUnavailable);
  // Y4: profile.sav (current slot, first-launch migration of an old top-level run.sav /
  // progress.sav into profile 1) and the current slot's progress.sav. Never in automated previews.
  if (savesEnabled()) profiles::init();
  hasSave_ = hasSave();
  checkRunSave();  // S22: a run.sav that no longer loads asks to be deleted
  for (auto k : {saveerr::Kind::ProgressCorrupt, saveerr::Kind::SettingsCorrupt})  // STS_FAKE_SAVE_ERROR previews
    if (saveerr::faked(k)) saveerr::report(k);
  fastMode_ = settings::state().fastMode;
  screenShake_ = settings::state().screenShake;
  Scheduler::get().speed = fastMode_ ? 1.75 : 1.0;
  applyVolumes();  // S21
  return true;
}

// fastMode_/screenShake_ stay as App fields (read every frame by ui.cpp/combat_ui.cpp/
// combat_scene.cpp) but now just mirror settings::state(); this is the one place they are
// written back and persisted, through the Y1 core module instead of the old ad hoc "settings.txt".
void App::saveSettings() {
  settings::state().fastMode = fastMode_;
  settings::state().screenShake = screenShake_;
  if (savesEnabled()) noteWrite(settings::save());
}

// S22: probe-load the current slot's run.sav; one that is there but no longer loads is reported
// (the dialog offers to delete it) and 继续 is hidden. STS_FAKE_SAVE_ERROR=run fakes it.
void App::checkRunSave() {
  // Y5: a run.sav whose last write was cut off comes back from run.sav.tmp / .bak first.
  if (savesEnabled())
    safefile::recover(profiles::runSavePath(), [](const std::string& p) {
      std::string d;
      Run probe;
      return safefile::readWhole(p, d) && !d.empty() && probe.load(d);
    });
  std::string data;
  bool bad = saveerr::faked(saveerr::Kind::RunCorrupt);
  if (!bad && readRunSave(data)) {
    Run probe;
    bad = !probe.load(data);
  }
  if (!bad) return;
  hasSave_ = false;
  saveerr::report(saveerr::Kind::RunCorrupt);
}

bool App::hasSave() const {
  std::string data;
  return savesEnabled() && gfx::readSave(saveName(), data) && !data.empty();
}

// Y4 hooks for the profile screen (S03), title only (no run in progress). Renaming needs no
// App state: call profiles::rename directly.
bool App::selectProfile(int id) {
  bool ok = profiles::select(id);
  hasSave_ = hasSave();
  checkRunSave();
  return ok;
}

bool App::deleteProfile(int id) {
  bool ok = profiles::remove(id);
  hasSave_ = hasSave();
  return ok;
}

bool App::readRunSave(std::string& data) const {  // S02: the main menu's continue-run info
  return savesEnabled() && gfx::readSave(saveName(), data) && !data.empty();
}

void App::startRun(bool resume) {
  run_ = std::make_unique<Run>();
  bool loaded = false;
  if (resume) {
    std::string data;
    loaded = gfx::readSave(saveName(), data) && run_->load(data);
    if (!loaded) {  // S22: stay on the menu and ask about the save (confirm.cpp) instead of a new run
      run_ = std::make_unique<Run>();
      hasSave_ = false;
      saveerr::report(saveerr::Kind::RunCorrupt);
      return;
    }
  }
  if (!loaded) {
    if (savesEnabled()) gfx::deleteSave(saveName());
    // S04: the character select's choice (Random: Run::start resolves it from the seed), ascension and seed
    // string (RunRngSet: Seed = StringHelper.GetDeterministicHashCode(seed)). Debug: STS_CHAR /
    // STS_SEED (a number) override them.
    const auto& ids = db::characterIds();
    std::string character = titleChar_ < (int)ids.size() ? ids[titleChar_] : std::string(Run::kRandomCharacter);
    if (const char* c = getenv("STS_CHAR")) character = c;
    uint64_t seed = titleSeed_.empty() ? (uint64_t)time(nullptr) : modifiers::seedFromString(titleSeed_);
    run_->seedText = titleSeed_;
    if (const char* s = getenv("STS_SEED")) { seed = (uint64_t)atoll(s); run_->seedText.clear(); }
    // M11: the custom run screen's modifiers; debug STS_MODIFIERS=Draft,Midas,CharacterCards:Silent.
    std::vector<std::string> mods = titleModifiers_;
    if (const char* m = getenv("STS_MODIFIERS")) mods = modifiers::parseList(m);
    run_->setModifiers(mods);
    run_->customRun = (titleCustom_ || !mods.empty()) && titleDaily_.empty();
    run_->dailyDate = titleDaily_;  // M12: a daily run (screens/daily_run.cpp)
    titleDaily_.clear();
    titleModifiers_.clear();
    titleCustom_ = false;
    run_->start(seed, character, titleAsc_);
  }
  if (savesEnabled() || saveerr::faked(saveerr::Kind::WriteFailed))
    run_->onSavePoint = [](Run& r) {
      if (!savesEnabled()) { noteWrite(true); return; }  // STS_FAKE_SAVE_ERROR=write in a preview
      bool ok = gfx::writeSave(saveName(), r.save());
      noteWrite(profiles::saveProgress() && ok);  // seen cards/relics/monsters so far
    };
  if (getenv("STS_ALLCARDS")) {  // debug: every pool card in the deck
    run_->deck.clear();
    for (auto& id : run_->character().cardPool)
      if (auto c = db::card(id)) run_->deck.push_back(std::move(c));
  }
  if (const char* list = getenv("STS_DECK")) {  // debug: exact deck, "Id,Id+,..." (+ = upgraded)
    run_->deck.clear();
    std::string s = list;
    for (size_t a = 0; a < s.size();) {
      size_t b = s.find(',', a);
      std::string id = s.substr(a, b == std::string::npos ? std::string::npos : b - a);
      bool up = !id.empty() && id.back() == '+';
      if (up) id.pop_back();
      if (auto c = db::card(id)) {
        if (up) c->upgrade();
        run_->deck.push_back(std::move(c));
      }
      a = b == std::string::npos ? s.size() : b + 1;
    }
  }
  if (const char* hp = getenv("STS_HP"))  // debug: start wounded (rest site previews)
    run_->player->hp = std::clamp(atoi(hp), 1, run_->player->maxHp);
  Scheduler::get().spawn(run_->main());
  floats_.clear();
  visuals_.clear();
  shownGold_ = shownHp_ = -1;  // F6: snap the top bar's ticking counters to the new run
  sel_ = -1;
  mapSel_ = 0;
  mapScroll_ = 0;
  deckOpen_ = false;
  cardListMode_ = CardListMode::Deck;
  closeDetail();
  relicsOpen_ = false;
  settingsOpen_ = false;
  pauseOpen_ = false;
  titleCharacter_ = false;
  titleSelection_ = 0;
  menuSub_ = 0;
  continueInfo_.clear();
  R().releaseTexture("gfx/bg_menu.t3t");
  for (auto& id : db::characterIds()) R().releaseTexture("gfx/bg_character_" + db::character(id).energyColor + ".t3t");
}

void App::returnTitle(bool keepSave) {
  // Abandon can happen while Run::main is suspended on a UI signal. Destroy
  // those coroutines before the Run and its cards/creatures they reference.
  Scheduler::get().clear();
  if (savesEnabled()) {
    if (!keepSave) gfx::deleteSave(saveName());
    noteWrite(profiles::saveProgress());  // after Run::abandon() / a finished run / save & quit
  }
  visuals_.clear();
  R().releaseSkeletons({});
  run_ = std::make_unique<Run>();
  lastCombat_ = nullptr;
  centers_.clear();
  flights_.clear();
  poses_.clear();
  ghosts_.clear();
  mapTouch_ = {};
  drag_ = {};
  deckOpen_ = relicsOpen_ = settingsOpen_ = mapView_ = devOpen_ = pauseOpen_ = topBarFocus_ = false;
  potionsOpen_ = false;
  closeDetail();
  titleCharacter_ = false;
  titleSelection_ = 0;
  menuSub_ = 0;
  continueInfo_.clear();
  hasSave_ = hasSave();
}

// ================================================================ frame

void App::update(const gfx::Input& frameIn, double dt) {
  // Top bar fallback (owner decision 2026-09-30): L and R together toggle the top bar's focus mode
  // like ZL / ZR (New 3DS only). The chord fires on the frame the second of the two goes down
  // (both held, one of them new this frame); that frame's L and R presses are consumed, here and
  // for every later gfx::input() call, so no screen also sees a single L or R (pile tabs, inspect
  // cycling, library categories, settings pages, map scroll). Single presses are unchanged.
  gfx::Input in = frameIn;
  {
    const uint32_t lr = gfx::BTN_L | gfx::BTN_R, now = in.held | in.down;
    lrChord_ = (in.down & lr) && (now & lr) == lr;
    if (lrChord_) {
      in.down &= ~lr;
      gfx::consumeButtons(lr);
    }
  }
  widgets::suspendInput(false);  // S19: updateTopBar suspends the bottom widgets while it owns the input
  if (updateBoot(in, dt)) return;
  // M2: time played (RunManager's active run time; the pause menu stops it).
  if (run_->screen != Screen::Title && run_->screen != Screen::GameOver && run_->screen != Screen::Victory && !settingsOpen_ &&
      !pauseOpen_)
    run_->runTime += dt;
  // Y2: the pause menu freezes the run's coroutines (RunManager.IsPaused); fast mode otherwise.
  Scheduler::get().speed = pauseOpen_ ? 0.0 : fastMode_ ? 1.75 : 1.0;
  routeMusic(*run_);  // U3: music / ambience follow the screen, room and act
  double visualDt = dt * (fastMode_ ? 1.75 : 1.0);
  time_ += visualDt;
  if (transitionT_ > 0) transitionT_ = std::max(0.f, transitionT_ - (float)visualDt / style::kFade);
  // Gold/HP counters (F6): ease towards the real value instead of snapping.
  {
    float goldNow = (float)run_->gold, hpNow = run_->player ? (float)std::max(0, run_->player->hp) : shownHp_;
    if (shownGold_ < 0) shownGold_ = goldNow;
    if (shownHp_ < 0) shownHp_ = hpNow;
    float k = 1.f - std::exp(-(float)visualDt / style::kTick);
    shownGold_ += (goldNow - shownGold_) * k;
    shownHp_ += (hpNow - shownHp_) * k;
    if (std::abs(shownGold_ - goldNow) < 0.5f) shownGold_ = goldNow;
    if (std::abs(shownHp_ - hpNow) < 0.5f) shownHp_ = hpNow;
  }
  if (toastT_ > 0) toastT_ -= (float)visualDt;
  updateAchievementToast((float)visualDt);  // M5
  for (auto& f : floats_) f.t += (float)visualDt;
  hurtT_ += (float)visualDt;
  floats_.erase(std::remove_if(floats_.begin(), floats_.end(), [](const Float& f) {
    return f.t > (f.color == col::red ? 2.f : f.color == col::blue && f.text[0] != '+' ? 1.5f : 1.2f);  // damage / Blocked / other
  }), floats_.end());

  Screen scr = run_->screen;
  if (scr != lastScreen_) {
    transitionT_ = 1.f;  // F6: fade through black on every screen change
    topBarFocus_ = false;  // S19: a new room takes the input back from the top bar
    closeDetail();
    if (cardListMode_ != CardListMode::Deck) { deckOpen_ = false; cardListMode_ = CardListMode::Deck; }
    sel_ = -1;
    scroll_ = 0;
    mapTouch_ = {};
    mapUserScroll_ = false;
    lastScreen_ = scr;
    // The run is over: its save goes (dying or winning cannot be undone by reloading).
    // Run::main has recorded the win/loss in progress::state() (M1); persist it (Y4).
    if ((scr == Screen::GameOver || scr == Screen::Victory) && savesEnabled()) {
      gfx::deleteSave(saveName());
      noteWrite(profiles::saveProgress());
    }
    if (scr == Screen::Title) hasSave_ = hasSave();
  }
  updateActTitle((float)visualDt);
  if (run_->combat.get() != lastCombat_) {
    lastCombat_ = run_->combat.get();
    visuals_.clear();
    // Only the player stays cached across fights; each monster's Spine pages
    // are a few MB of linear memory on the 3DS.
    R().releaseSkeletons({playerArt(run_.get())});
    centers_.clear();
    flights_.clear();
    poses_.clear();
    ghosts_.clear();
    drawQueue_ = leaveQueue_ = 0;
    drag_ = {};
    aiming_ = false;
    floats_.clear();
    target_ = 0;
    vfxReset();  // F7
  }
  consumeEvents();
  updateVfx((float)visualDt);  // F7
  sfx::frame(*run_);
  if (in.touchDown && hitAt(in.tx, in.ty) != ID_NONE) sfx::click();
  if (run_->combat) {
    auto decayShake = [&](Creature* c) {
      if (c) c->shake = std::max(0.f, c->shake - (float)visualDt * 4.f);
    };
    decayShake(run_->player.get());
    for (auto* enemy : run_->combat->enemies) decayShake(enemy);
  }
  for (auto& [c, v] : visuals_) {
    if (!v.anim) continue;
    v.anim->update((float)visualDt);
    if (v.dying && v.anim->finished()) v.fade = std::max(0.f, v.fade - (float)visualDt * 2.f);
  }

  if (autoplay_) autoplay(visualDt);
  if (updateConfirm(in)) return;  // S22: the shared confirmation / error modal, over everything
  if (updateTips(in)) return;  // M13: a first-time tip owns the input while open
  if (detailOpen()) { updateDetail(in); return; }  // S20: the popup over every screen
  if (updateCardLibrary(in)) return;  // M8: over any page (main menu compendium, pause menu)
  if (updateRelicCollection(in)) return;  // M9: relic collection / potion lab
  if (updateBestiary(in)) return;     // M10
  if (updateCredits(in)) return;      // S26: over the settings page
  if (settingsOpen_) { updateSettings(in); return; }
  if ((in.down & gfx::BTN_SELECT) && scr != Screen::Title) {
    devOpen_ = !devOpen_;
    devPage_ = 0;
    sel_ = -1;
    scroll_ = 0;
    return;
  }
  if (devOpen_) { updateDev(in); return; }
  if (run_->deckChoice.active) { updateDeckChoice(in); return; }
  // Y2: START opens the pause menu from any room of a run (its 地图 entry is the look-only map
  // that START used to open). While it is open, START resumes (updatePause) or closes the map.
  if ((in.down & gfx::BTN_START) && !pauseOpen_ && !mapView_ && scr != Screen::Title &&
      scr != Screen::GameOver && scr != Screen::Victory) {
    openPause();
    return;
  }
  if (mapView_) { updateMap(in); return; }
  if (relicsOpen_) { updateRelics(in); return; }
  if (deckOpen_) { updateDeck(in); return; }
  if (potionsOpen_) { updatePotions(in); return; }
  if (pauseOpen_) { updatePause(in); return; }
  if (updateTopBar(in)) return;  // S19: ZL / ZR (or L+R) focus mode on the top screen's status bar
  switch (scr) {
    case Screen::Title: updateTitle(in); break;
    case Screen::Map: updateMap(in); break;
    case Screen::Combat: updateCombat(in); break;
    case Screen::Reward: updateReward(in); break;
    case Screen::Rest: updateRest(in); break;
    case Screen::RestUpgrade: updateUpgrade(in); break;
    case Screen::GameOver: updateEnd(in); break;
    case Screen::Victory: updateEnd(in); break;
    case Screen::RelicOffer: updateRelicOffer(in); break;
    case Screen::PotionOffer: updatePotionOffer(in); break;
    case Screen::Shop: updateShop(in); break;
    case Screen::Event: updateEvent(in); break;
    case Screen::Placeholder:
      if (run_->placeholderDone.waiting() &&
          ((in.down & gfx::BTN_A) || (in.touchDown && hitAt(in.tx, in.ty) == ID_CONFIRM)))
        run_->placeholderDone.fire(0);
      break;
    default: break;
  }
}

void App::consumeEvents() {
  Combat* c = run_->combat.get();
  if (!c) return;
  for (auto& e : c->events) {
    sfx::combatEvent(e, *c, *run_);
    vfxEvent(e);  // F7 light combat VFX
    float dx = (float)((int)(floats_.size() * 13) % 21) - 10;
    switch (e.kind) {
      case VisualEvent::Damage:
        floats_.push_back({e.who, num(e.amount), col::red, 0, dx});
        // CreatureCmd.Damage: the player's hurt vignette plays when the hit leaves them at 25% HP or less.
        if (e.amount > 0 && e.who && e.who->isPlayer && e.who->hp * 4 <= e.who->maxHp) hurtT_ = 0;
        if (e.amount > 0 && e.who && e.who->alive()) trigger(e.who, "Hit", 0);
        break;
      case VisualEvent::Anim:
        if (e.who) trigger(e.who, e.text, e.amount);
        break;
      case VisualEvent::Death:
        if (e.who) trigger(e.who, "Dead", 0);
        break;
      case VisualEvent::Blocked:
        floats_.push_back({e.who, L("gameplay_ui.BLOCKED").find("gameplay_ui") == 0 ? tr("格挡", "Blocked") : L("gameplay_ui.BLOCKED"), col::blue, 0, dx});
        break;
      case VisualEvent::Block:  // no floating number: CreatureCmd.GainBlock plays only vfx_block (vfxEvent)
        break;
      case VisualEvent::Heal:
        if (e.amount > 0) floats_.push_back({e.who, "+" + num(e.amount), col::green, 0, dx});
        break;
      case VisualEvent::PowerUp:
      case VisualEvent::PowerDown:
        floats_.push_back({e.who, L("powers." + e.text + ".title"), e.kind == VisualEvent::PowerUp ? col::gold : col::purple, 0.1f, dx});
        break;
      case VisualEvent::CardExhaust:
        toast_ = L("card_keywords.EXHAUST.title") + tr("：", ": ") + L("cards." + e.text + ".title");
        toastT_ = 1.2f;
        break;
      case VisualEvent::Shuffle:
        showTip(Ftue::Shuffle);  // M13: NShuffleFtue
        toast_ = tr("洗牌", "Shuffle");
        toastT_ = 0.8f;
        break;
      case VisualEvent::Banner: {
        std::string key = e.text == "Slimed" ? "SLIMED" : e.text == "Wound" ? "WOUND" : e.text;
        toast_ = "+" + num(e.amount) + " " + L("cards." + key + ".title") + " → " + L("gameplay_ui.PILE_DISCARD").substr(0, 0) + tr("弃牌堆", "Discard Pile");
        toastT_ = 1.4f;
        break;
      }
      default:
        break;
    }
  }
  c->events.clear();
}

void App::draw() {
  hits_.clear();
  Screen scr = run_->screen;
  for (int pass = 0; pass < 2; ++pass) {
    bool top = pass == 0;
    gfx::screen(top ? gfx::TOP : gfx::BOTTOM, 0x0B0B12FF);
    if (const char* m = getenv("STS_MOCK")) { drawStyleMock(std::atoi(m), top); continue; }
    if (drawBoot(top)) continue;
    do {  // S22: every `continue` below ends in the shared modal, drawn over all of it
    if (detailOpen()) { drawDetail(top); continue; }  // S20: the popup over every screen
    if (drawCardLibrary(top)) continue;  // M8
    if (drawRelicCollection(top)) continue;  // M9
    if (drawBestiary(top)) continue;     // M10
    if (drawCredits(top)) continue;      // S26
    if (settingsOpen_) { drawSettings(top); continue; }
    if (devOpen_) { drawDev(top); continue; }
    if (run_->deckChoice.active) { drawDeckChoice(top); continue; }
    if (mapView_) { drawMap(top); continue; }
    if (relicsOpen_) { drawRelics(top); continue; }
    if (deckOpen_) { drawDeck(top); continue; }
    if (potionsOpen_) { drawPotions(top); continue; }
    switch (scr) {
      case Screen::Title: drawTitle(top); break;
      case Screen::Map: drawMap(top); break;
      case Screen::Combat: drawCombat(top); break;
      case Screen::Reward: drawReward(top); break;
      case Screen::Rest: drawRest(top); break;
      case Screen::RestUpgrade: drawUpgrade(top); break;
      case Screen::GameOver: drawEnd(top, false); break;
      case Screen::Victory: drawEnd(top, true); break;
      case Screen::RelicOffer: drawRelicOffer(top); break;
      case Screen::PotionOffer: drawPotionOffer(top); break;
      case Screen::Shop: drawShop(top); break;
      case Screen::Event: drawEvent(top); break;
      case Screen::Placeholder:  // a room that is not ported yet (event / shop)
        drawSceneBg(top, 0.6f);
        if (top) {
          drawTopBar();
          R().text(kTop / 2, 90, run_->placeholderText, ts(F16, col::gold, CENTER, 0, 1.3f));
          R().text(kTop / 2, 124, tr("这个房间还没有移植，先跳过。", "This room is not ported yet; skipping it."), ts(F12, col::white, CENTER));
        } else {
          button(kBot / 2 - 60, 100, 120, 40, tr("继续", "Continue"), ID_CONFIRM, true, true);
        }
        break;
      default: break;
    }
    if (scr == Screen::Map) drawActTitle(top);
    if (pauseOpen_) drawPause(top);  // Y2: over the room, under the fade and toasts
    drawTips(top);  // M13
    // F6: fade through black on a screen change (style::kFade seconds).
    if (transitionT_ > 0) gfx::rect(0, 0, top ? kTop : kBot, kH, 0x000000FF & (0xFFFFFF00 | (uint32_t)(transitionT_ * 255)));
    if (top) drawAchievementToast();  // M5: over everything on the top screen
    if (!top && toastT_ > 0) {
      float a = std::min(1.f, toastT_ * 3);
      float w = R().measure(toast_, ts(F12)) + 16;
      gfx::rect((kBot - w) / 2, 112, w, 20, 0x000000C0 & (0xFFFFFF00 | (uint32_t)(a * 0xC0)));
      R().text(kBot / 2, 115, toast_, ts(F12, col::gold, CENTER));
    }
    } while (false);
    drawConfirm(top);  // S22
  }
}

// ================================================================ autoplay

void App::autoplay(double dt) {
  autoT_ += dt;
  if (autoT_ < 0.6) return;
  Run& r = *run_;
  bool acted = true;
  switch (r.screen) {
    case Screen::Title: startRun(); break;
    case Screen::Map:
      if (r.mapChoice.waiting()) {
        auto reach = r.reachableNodes();
        int pick = reach[0];
        for (int i : reach) if (r.nodes[i].type == RoomType::Rest) pick = i;
        r.mapChoice.fire(pick);
      } else acted = false;
      break;
    case Screen::Combat: {
      Combat& c = *r.combat;
      if (c.choice.active && c.choice.result.waiting()) { c.choice.result.fire({c.choice.options[0]}); break; }
      if (!(c.playerPhase && c.actions.waiting())) { acted = false; break; }
      PlayerAction a;
      for (Card* card : c.hand) {
        if (!c.canPlay(card)) continue;
        a.kind = PlayerAction::PlayCard;
        a.card = card;
        if (card->target == TargetType::AnyEnemy) a.target = c.aliveEnemies()[0];
        break;
      }
      c.actions.fire(a);
      break;
    }
    case Screen::Reward:
      // S14: the reward list (Run::rewardItems) is claimed top to bottom; a Card row opens the
      // nested card grid (rewardCards non-empty), where the same "linger so the choice is
      // visible" policy as before picks index 1 (or 0 if there's only one option).
      if (!r.rewardCards.empty()) {
        if (r.rewardChoice.waiting()) {
          if (sel_ < 0) { sel_ = std::min(1, (int)r.rewardCards.size() - 1); autoT_ = -0.6; return; }
          r.rewardChoice.fire(sel_);
        } else acted = false;
      } else if (r.rewardListChoice.waiting()) {
        if (r.rewardItems.empty()) {
          r.rewardListChoice.fire(-1);  // nothing left: Proceed
        } else {
          // A full belt would otherwise leave a Potion row stuck at index 0 forever.
          if (r.rewardItems[0].kind == Run::RewardKind::Potion && !r.hasOpenPotionSlot()) r.discardPotion(0);
          r.rewardListChoice.fire(0);  // claim top to bottom
        }
      } else acted = false;
      break;
    case Screen::Rest:
      if (r.restChoice.waiting()) r.restChoice.fire(r.restUsed.empty() ? 1 : -1); else acted = false;
      break;
    case Screen::RelicOffer:
      if (r.relicChoice.waiting()) r.relicChoice.fire(1); else acted = false;
      break;
    case Screen::Shop:
      if (r.deckChoice.active && r.deckChoice.result.waiting()) r.deckChoice.result.fire({});
      else if (r.shopChoice.waiting()) r.shopChoice.fire(-1); else acted = false;
      break;
    case Screen::PotionOffer:
      if (r.potionOfferChoice.waiting()) r.potionOfferChoice.fire(r.hasOpenPotionSlot() ? 1 : 0); else acted = false;
      break;
    case Screen::Placeholder:
      if (r.placeholderDone.waiting()) r.placeholderDone.fire(0); else acted = false;
      break;
    case Screen::Event:
      if (r.deckChoice.active && r.deckChoice.result.waiting()) r.deckChoice.result.fire({r.deckChoice.options[0]});
      else if (CrystalSphereGame* g = crystalSphereGame(r); g && g->cellChoice.waiting()) {
        for (int c = 0; c < CrystalSphereGame::kSize * CrystalSphereGame::kSize; ++c)  // first fogged cell
          if (g->hidden[c % CrystalSphereGame::kSize][c / CrystalSphereGame::kSize]) { g->cellChoice.fire(c); break; }
      } else if (r.eventChoice.waiting() && r.currentEvent) {
        int pick = 0;
        auto& opts = r.currentEvent->options;
        while (pick < (int)opts.size() && opts[pick].locked()) ++pick;
        r.eventChoice.fire(pick);
      } else acted = false;
      break;
    case Screen::RestUpgrade:
      // Linger once so the grid is visible, then upgrade the 10th card (or the last one). The
      // grid select (S13) owns sel_ and resets it, so the linger has its own flag.
      if (r.upgradeChoice.waiting()) {
        static bool lingered = false;
        if (!lingered) { lingered = true; autoT_ = -0.6; return; }
        lingered = false;
        r.upgradeChoice.fire(std::min(9, (int)r.upgradeOptions.size() - 1));
      } else acted = false;
      break;
    default: acted = false; break;
  }
  if (acted) autoT_ = 0;
}

}  // namespace ui
