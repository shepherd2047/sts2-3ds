// Split from ui.cpp (F3).
#include "../core/events_crystal.h"
#include "../core/settings_store.h"
#include "ui_common.h"

namespace ui {

// Per-act art: gfx/bg_<act>.t3t (room) and gfx/bg_map_<act>.t3t (map paper). The previous
// act's textures are freed when the act changes (3DS linear memory is small).
std::string actTexture(const Run& r, const char* kind) {
  static int loadedAct = -1;
  if (loadedAct != r.actIndex) {
    if (loadedAct >= 0) {
      const char* old = db::acts()[loadedAct].key;
      R().releaseTexture(std::string("gfx/bg_") + old + ".t3t");
      R().releaseTexture(std::string("gfx/bg_map_") + old + ".t3t");
    }
    loadedAct = r.actIndex;
  }
  return std::string("gfx/") + kind + r.act().key + ".t3t";
}

// ================================================================ setup

// Saves: the run is written at every map choice (Run::onSavePoint) and deleted when it
// ends. Automated previews (STS_HIDDEN) and STS_NO_SAVE neither read nor write it (nor
// settings.sav / progress.sav -- see App::init/saveSettings below).
namespace {
constexpr const char* kSaveName = "run.sav";
bool savesEnabled() { return !getenv("STS_HIDDEN") && !getenv("STS_NO_SAVE"); }
}

bool App::init() {
  if (!R().load()) return false;
  run_ = std::make_unique<Run>();
  autoplay_ = getenv("STS_AUTOPLAY") != nullptr;
  hasSave_ = hasSave();
  // Y1: settings.sav, loaded once at startup. STS_HIDDEN/STS_NO_SAVE (automated previews, and the
  // headless sim which never links ui.cpp at all) must never touch the real player's file; the
  // in-memory settings::state() then just keeps its defaults for that run of the app.
  if (savesEnabled()) settings::load();
  fastMode_ = settings::state().fastMode;
  screenShake_ = settings::state().screenShake;
  Scheduler::get().speed = fastMode_ ? 1.75 : 1.0;
  return true;
}

// fastMode_/screenShake_ stay as App fields (read every frame by ui.cpp/combat_ui.cpp/
// combat_scene.cpp) but now just mirror settings::state(); this is the one place they are
// written back and persisted, through the Y1 core module instead of the old ad hoc "settings.txt".
void App::saveSettings() {
  settings::state().fastMode = fastMode_;
  settings::state().screenShake = screenShake_;
  if (savesEnabled()) settings::save();
}

bool App::hasSave() const {
  std::string data;
  return savesEnabled() && gfx::readSave(kSaveName, data) && !data.empty();
}

void App::startRun(bool resume) {
  run_ = std::make_unique<Run>();
  bool loaded = false;
  if (resume) {
    std::string data;
    loaded = gfx::readSave(kSaveName, data) && run_->load(data);
    if (!loaded) { run_ = std::make_unique<Run>(); toast_ = "存档无法读取，开始新游戏"; toastT_ = 2.f; }
  }
  if (!loaded) {
    if (savesEnabled()) gfx::deleteSave(kSaveName);
    // S04: the character select's choice (Random picks one of the five now), ascension and seed
    // string (RunRngSet: Seed = StringHelper.GetDeterministicHashCode(seed)). Debug: STS_CHAR /
    // STS_SEED (a number) override them.
    const auto& ids = db::characterIds();
    std::string character = titleChar_ < (int)ids.size() ? ids[titleChar_] : ids[(size_t)time(nullptr) % ids.size()];
    if (const char* c = getenv("STS_CHAR")) character = c;
    uint64_t seed = titleSeed_.empty() ? (uint64_t)time(nullptr) : deterministicHash(titleSeed_);
    if (const char* s = getenv("STS_SEED")) seed = (uint64_t)atoll(s);
    run_->start(seed, character, titleAsc_);
  }
  if (savesEnabled()) run_->onSavePoint = [](Run& r) { gfx::writeSave(kSaveName, r.save()); };
  if (getenv("STS_ALLCARDS")) {  // debug: every pool card in the deck
    run_->deck.clear();
    for (auto& id : run_->character().cardPool)
      if (auto c = db::card(id)) run_->deck.push_back(std::move(c));
  }
  Scheduler::get().spawn(run_->main());
  floats_.clear();
  visuals_.clear();
  shownGold_ = shownHp_ = -1;  // F6: snap the top bar's ticking counters to the new run
  sel_ = -1;
  mapSel_ = 0;
  mapScroll_ = 0;
  deckOpen_ = false;
  cardListMode_ = CardListMode::Deck;
  detailCard_ = nullptr;
  detailRelic_ = nullptr;
  detailUpgrade_ = false;
  detailKeyword_ = -1;
  relicsOpen_ = false;
  settingsOpen_ = false;
  abandonConfirm_ = false;
  titleCharacter_ = false;
  titleSelection_ = 0;
  R().releaseTexture("gfx/bg_menu.t3t");
  for (auto& id : db::characterIds()) R().releaseTexture("gfx/bg_character_" + db::character(id).energyColor + ".t3t");
}

void App::returnTitle() {
  // Abandon can happen while Run::main is suspended on a UI signal. Destroy
  // those coroutines before the Run and its cards/creatures they reference.
  Scheduler::get().clear();
  if (savesEnabled()) gfx::deleteSave(kSaveName);
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
  deckOpen_ = relicsOpen_ = settingsOpen_ = abandonConfirm_ = mapView_ = devOpen_ = false;
  potionsOpen_ = false;
  detailCard_ = nullptr;
  detailRelic_ = nullptr;
  titleCharacter_ = false;
  titleSelection_ = 0;
  hasSave_ = hasSave();
}

// ================================================================ frame

void App::update(const gfx::Input& in, double dt) {
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
  for (auto& f : floats_) f.t += (float)visualDt;
  floats_.erase(std::remove_if(floats_.begin(), floats_.end(), [](const Float& f) { return f.t > 1.2f; }), floats_.end());

  Screen scr = run_->screen;
  if (scr != lastScreen_) {
    transitionT_ = 1.f;  // F6: fade through black on every screen change
    detailCard_ = nullptr;
    detailRelic_ = nullptr;
    detailUpgrade_ = false;
    detailKeyword_ = -1;
    if (cardListMode_ != CardListMode::Deck) { deckOpen_ = false; cardListMode_ = CardListMode::Deck; }
    sel_ = -1;
    scroll_ = 0;
    mapTouch_ = {};
    mapUserScroll_ = false;
    lastScreen_ = scr;
    // The run is over: its save goes (dying or winning cannot be undone by reloading).
    if ((scr == Screen::GameOver || scr == Screen::Victory) && savesEnabled()) gfx::deleteSave(kSaveName);
    if (scr == Screen::Title) hasSave_ = hasSave();
  }
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
  }
  consumeEvents();
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
  if (settingsOpen_) { updateSettings(in); return; }
  if ((in.down & gfx::BTN_SELECT) && scr != Screen::Title) {
    devOpen_ = !devOpen_;
    devPage_ = 0;
    sel_ = -1;
    scroll_ = 0;
    return;
  }
  if (devOpen_) { updateDev(in); return; }
  if (detailCard_ || detailRelic_) { updateDetail(in); return; }
  if (run_->deckChoice.active) { updateDeckChoice(in); return; }
  // START opens the map for a look from any room (RGDSplus: map entry on the top bar).
  if ((in.down & gfx::BTN_START) && !mapView_ && scr != Screen::Title && scr != Screen::Map &&
      scr != Screen::GameOver && scr != Screen::Victory) {
    mapView_ = true;
    mapTouch_ = {};
    mapUserScroll_ = false;
    return;
  }
  if (mapView_) { updateMap(in); return; }
  if (relicsOpen_) { updateRelics(in); return; }
  if (deckOpen_) { updateDeck(in); return; }
  if (potionsOpen_) { updatePotions(in); return; }
  if (scr == Screen::Map && (in.down & gfx::BTN_START)) {
    settingsOpen_ = true;
    abandonConfirm_ = false;
    return;
  }
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
    float dx = (float)((int)(floats_.size() * 13) % 21) - 10;
    switch (e.kind) {
      case VisualEvent::Damage:
        floats_.push_back({e.who, num(e.amount), col::red, 0, dx});
        if (e.amount > 0 && e.who && e.who->alive()) trigger(e.who, "Hit", 0);
        break;
      case VisualEvent::Anim:
        if (e.who) trigger(e.who, e.text, e.amount);
        break;
      case VisualEvent::Death:
        if (e.who) trigger(e.who, "Dead", 0);
        break;
      case VisualEvent::Blocked:
        floats_.push_back({e.who, L("gameplay_ui.BLOCKED").find("gameplay_ui") == 0 ? "格挡" : L("gameplay_ui.BLOCKED"), col::blue, 0, dx});
        break;
      case VisualEvent::Block:
        floats_.push_back({e.who, "+" + num(e.amount), col::blue, 0, dx});
        break;
      case VisualEvent::Heal:
        if (e.amount > 0) floats_.push_back({e.who, "+" + num(e.amount), col::green, 0, dx});
        break;
      case VisualEvent::PowerUp:
      case VisualEvent::PowerDown:
        floats_.push_back({e.who, L("powers." + e.text + ".title"), e.kind == VisualEvent::PowerUp ? col::gold : col::purple, 0.1f, dx});
        break;
      case VisualEvent::CardExhaust:
        toast_ = L("card_keywords.EXHAUST.title") + "：" + L("cards." + e.text + ".title");
        toastT_ = 1.2f;
        break;
      case VisualEvent::Shuffle:
        toast_ = "洗牌";
        toastT_ = 0.8f;
        break;
      case VisualEvent::Banner: {
        std::string key = e.text == "Slimed" ? "SLIMED" : e.text == "Wound" ? "WOUND" : e.text;
        toast_ = "+" + num(e.amount) + " " + L("cards." + key + ".title") + " → " + L("gameplay_ui.PILE_DISCARD").substr(0, 0) + "弃牌堆";
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
    if (settingsOpen_) { drawSettings(top); continue; }
    if (devOpen_) { drawDev(top); continue; }
    if (detailCard_ || detailRelic_) { drawDetail(top); continue; }
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
          R().text(kTop / 2, 124, "这个房间还没有移植，先跳过。", ts(F12, col::white, CENTER));
        } else {
          button(kBot / 2 - 60, 100, 120, 40, "继续", ID_CONFIRM, true, true);
        }
        break;
      default: break;
    }
    // F6: fade through black on a screen change (style::kFade seconds).
    if (transitionT_ > 0) gfx::rect(0, 0, top ? kTop : kBot, kH, 0x000000FF & (0xFFFFFF00 | (uint32_t)(transitionT_ * 255)));
    if (!top && toastT_ > 0) {
      float a = std::min(1.f, toastT_ * 3);
      float w = R().measure(toast_, ts(F12)) + 16;
      gfx::rect((kBot - w) / 2, 112, w, 20, 0x000000C0 & (0xFFFFFF00 | (uint32_t)(a * 0xC0)));
      R().text(kBot / 2, 115, toast_, ts(F12, col::gold, CENTER));
    }
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
      if (r.upgradeChoice.waiting()) {
        if (sel_ < 0) { sel_ = 9; autoT_ = -0.6; return; }
        r.upgradeChoice.fire(sel_);
      } else acted = false;
      break;
    default: acted = false; break;
  }
  if (acted) autoT_ = 0;
}

}  // namespace ui
