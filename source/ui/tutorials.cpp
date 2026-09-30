// M13 tutorials: queue, seen flags and gating (see tutorials.h). No gfx here: the popup and the
// screen watcher are App::updateTips / drawTips in tutorials_ui.cpp.
#include "tutorials.h"

#include <cstdlib>
#include <deque>
#include <string>

#include "../core/game.h"
#include "../core/save_errors.h"
#include "../core/settings_store.h"

namespace ui {

namespace settings = sts::settings;

namespace tips {

namespace {

const Info kInfo[(int)Ftue::Count] = {
    {"accept_tutorials_ftue", "main_menu_ui.ENABLE_TUTORIALS.title", "main_menu_ui.ENABLE_TUTORIALS.description"},
    {"combat_rules_ftue", "ftues.COMBAT_BASICS_FTUE_HEADER", "ftues.TUTORIAL_FTUE_BODY_1"},
    {"map_select_ftue", "ftues.MAP_SELECT_TITLE", "ftues.MAP_SELECT_DESCRIPTION"},
    {"rest_site_ftue", "ftues.REST_SITE_FTUE_TITLE", "ftues.REST_SITE_FTUE_DESCRIPTION"},
    {"combat_reward_ftue", "ftues.REWARDS_FTUE_TITLE", "ftues.REWARDS_FTUE_DESCRIPTION"},
    {"obtain_relic_ftue", "ftues.RELIC_FTUE_TITLE", "ftues.RELIC_FTUE_DESCRIPTION"},
    {"obtain_potion_ftue", "ftues.POTION_FTUE_TITLE", "ftues.POTION_FTUE_DESCRIPTION"},
    {"power_card_ftue", "ftues.POWER_FTUE_TITLE", "ftues.POWER_FTUE_DESCRIPTION"},
    {"shuffle_ftue", "ftues.SHUFFLE_FTUE_TITLE", "ftues.SHUFFLE_FTUE_DESCRIPTION"},
    {"cannot_play_card_ftue", "ftues.CANNOT_PLAY_CARD_FTUE_TITLE", "ftues.CANNOT_PLAY_CARD_FTUE_DESCRIPTION"},
    {"can_play_cards_ftue", "ftues.CAN_PLAY_CARDS_FTUE_TITLE", "ftues.CAN_PLAY_CARDS_FTUE_DESCRIPTION"},
};
const Info kNone = {"", "", ""};

std::deque<Ftue> queue_;
Ftue current_ = Ftue::None;
int page_ = 0;
int forced_ = -1;
int cardsPlayed_ = 0;       // NCardPlay._totalCardsPlayedForFtue
int idleEndTurns_ = 0;      // NEndTurnButton._endTurnWithNoPlayableCardsCount

// settings.sav is written when a flag changes, except in automated previews / STS_NO_SAVE
// (the same rule as App::saveSettings; tests set STS_NO_SAVE).
void persist() {
  if (!getenv("STS_HIDDEN") && !getenv("STS_NO_SAVE") && sts::saveerr::storageAvailable()) settings::save();  // Y5: SD check
}

void markSeen(Ftue t) {
  if (t == Ftue::None || settings::state().tutorialsSeen.count(info(t).id)) return;
  settings::markTutorialSeen(info(t).id);
  persist();
}

}  // namespace

const Info& info(Ftue t) { return (t > Ftue::None && t < Ftue::Count) ? kInfo[(int)t] : kNone; }

int pages(Ftue t) { return t == Ftue::CombatRules ? 3 : 1; }

const char* pageBody(Ftue t, int page) {
  if (t == Ftue::CombatRules) {
    static const char* kBodies[3] = {"ftues.TUTORIAL_FTUE_BODY_1", "ftues.TUTORIAL_FTUE_BODY_2", "ftues.TUTORIAL_FTUE_BODY_3"};
    return kBodies[page < 0 ? 0 : page > 2 ? 2 : page];
  }
  return info(t).body;
}

bool allowed() {
  if (forced_ >= 0) return forced_ != 0;
  static const int env = [] {
    if (const char* t = getenv("STS_TIPS")) return atoi(t);
    if (getenv("STS_HIDDEN") || getenv("STS_SCRIPT") || getenv("STS_SHOTS") || getenv("STS_AUTOPLAY")) return 0;
    return 1;
  }();
  return env != 0;
}

bool askAllowed() {
  if (forced_ >= 0) return forced_ != 0;
  if (!allowed()) return false;
  const char* t = getenv("STS_TIPS");
  return !t || atoi(t) >= 2;
}

void forceAllowed(int v) { forced_ = v; }

Ftue current() { return current_; }
int page() { return page_; }

bool queued(Ftue t) {
  for (Ftue q : queue_)
    if (q == t) return true;
  return false;
}

bool openNext() {
  if (current_ != Ftue::None) return false;
  while (!queue_.empty()) {
    Ftue t = queue_.front();
    queue_.pop_front();
    if (settings::tutorialSeen(info(t).id)) continue;  // seen meanwhile (or tips turned off)
    current_ = t;
    page_ = 0;
    if (t != Ftue::CombatRules && t != Ftue::AcceptTutorials) markSeen(t);
    return true;
  }
  return false;
}

bool advance() {
  if (current_ == Ftue::None) return false;
  if (page_ + 1 < pages(current_)) {
    ++page_;
    return false;
  }
  if (current_ == Ftue::AcceptTutorials) {
    answer(true);
    return true;
  }
  markSeen(current_);  // CombatRules: only once every page was read
  current_ = Ftue::None;
  page_ = 0;
  return true;
}

void back() {
  if (page_ > 0) --page_;
}

void answer(bool yes) {
  // NAcceptTutorialsFtue.YesTutorials / NoTutorials: both mark the question seen; no disables
  // every tip (SetFtuesEnabled(false)) until 重置教程.
  settings::markTutorialSeen(info(Ftue::AcceptTutorials).id);
  if (!yes) settings::state().tutorialsEnabled = false;
  persist();
  if (!yes) queue_.clear();
  current_ = Ftue::None;
  page_ = 0;
}

void clear() {
  queue_.clear();
  current_ = Ftue::None;
  page_ = 0;
  cardsPlayed_ = idleEndTurns_ = 0;
}

}  // namespace tips

bool showTip(Ftue t) {
  if (t <= Ftue::None || t >= Ftue::Count) return false;
  if (tips::current() == t || tips::queued(t)) return true;
  if (t == Ftue::AcceptTutorials ? !tips::askAllowed() : !tips::allowed()) return false;
  if (settings::tutorialSeen(tips::info(t).id)) return false;
  tips::queue_.push_back(t);
  return true;
}

void tipPlayAttempt(sts::Combat& cb, sts::Card* c) {
  if (!c || !cb.playerPhase) return;
  std::string why;
  if (cb.canPlay(c, &why)) {
    if (++tips::cardsPlayed_ == 8 && !settings::tutorialSeen(tips::info(Ftue::CannotPlayCard).id))
      tips::markSeen(Ftue::CannotPlayCard);  // played 8 cards without needing the tip
  } else if (why == "ENERGY") {
    showTip(Ftue::CannotPlayCard);
  }
}

bool tipBlockEndTurn(sts::Combat& cb) {
  if (!tips::allowed() || settings::tutorialSeen(tips::info(Ftue::CanPlayCards).id)) return false;
  if (tips::current() == Ftue::CanPlayCards || tips::queued(Ftue::CanPlayCards)) return true;
  bool playable = false;
  for (sts::Card* c : cb.hand)
    if (c && cb.canPlay(c)) { playable = true; break; }
  if (playable) return showTip(Ftue::CanPlayCards);
  if (++tips::idleEndTurns_ == 3) tips::markSeen(Ftue::CanPlayCards);
  return false;
}

bool tipBlockProceed(int itemsLeft, int floor) {
  if (itemsLeft > 0) return showTip(Ftue::CombatReward);
  if (floor > 4 && tips::allowed()) tips::markSeen(Ftue::CombatReward);
  return false;
}

bool tipAskTutorials() { return showTip(Ftue::AcceptTutorials); }

}  // namespace ui
