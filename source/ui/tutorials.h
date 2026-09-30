// M13 tutorials: the first-time tips of the C#'s Nodes.Ftue (NFtue subclasses). Each tip is
// shown once per profile of settings (seen ids in settings.sav, settings::tutorialSeen /
// markTutorialSeen, the same ids as the C#'s SaveManager.MarkFtueAsComplete), again after
// 设置 → 数据 → 重置教程 (settings::resetTutorials, ProgressState.ResetFtues).
//
// This header is the gfx-free logic (queue, seen flags, the gating below) so the unit test can
// link it without a platform backend; the popup itself (top screen text panel with the C#'s
// ftue_popup art, 了解了！ below, A / tap to dismiss) is App::updateTips / drawTips in
// tutorials_ui.cpp, which also watches the run for the screen-based triggers (combat, map,
// rest site, relic / power card / potion rewards). Screens that own a trigger the watcher cannot
// see call the one-line hooks at the bottom.
//
// Gating: tips never show in automated runs (STS_HIDDEN, STS_SCRIPT, STS_SHOTS, STS_AUTOPLAY) so
// the screenshot scripts keep working, unless STS_TIPS=1 (STS_TIPS=0 turns them off anywhere;
// STS_TIPS=2 also shows the "要看教程吗？" question on 出发, which would shift scripts).
#pragma once

namespace sts {
struct Card;
struct Combat;
}  // namespace sts

namespace ui {

// C# order of the Nodes.Ftue scenes that make sense single-player on the 3DS.
enum class Ftue {
  None = -1,
  AcceptTutorials,  // NAcceptTutorialsFtue: 出发 the first time (yes / no: tips on or off)
  CombatRules,      // NCombatRulesFtue: first combat, three pages with the C#'s pictures
  MapSelect,        // NMapSelectFtue: first time on the map
  RestSite,         // NRestSiteFtue: first rest site
  CombatReward,     // NCombatRewardFtue: 继续 with rewards still on the list
  RelicReward,      // NRelicRewardFtue: first relic reward (treasure chest or reward list)
  Potion,           // NObtainPotionFtue: first potion in the belt
  PowerCard,        // NPowerCardFtue: first card reward that offers a Power
  Shuffle,          // NShuffleFtue: first discard -> draw pile shuffle
  CannotPlayCard,   // NCannotPlayCardFtue: tried to play a card without enough energy
  CanPlayCards,     // NCanPlayCardsFtue: ended the turn while cards were still playable
  Count
};

namespace tips {

struct Info {
  const char* id;     // the C# ftue id (seen flag key)
  const char* title;  // loc keys
  const char* body;
};
const Info& info(Ftue t);
int pages(Ftue t);  // CombatRules: 3, others 1
const char* pageBody(Ftue t, int page);

// The environment gate described above (and settings tutorialsEnabled via tutorialSeen).
bool allowed();
bool askAllowed();  // the AcceptTutorials question (real play or STS_TIPS=2)
// Tests: -1 = use the environment (default), 0 = off, 1 = on (also allows the question).
void forceAllowed(int v);

Ftue current();  // the open tip, or Ftue::None
int page();     // its page (0-based)
bool queued(Ftue t);
// Opens the next queued tip (marking it seen, except CombatRules which is marked when its last
// page is dismissed, as in NCombatRulesFtue.ToggleRight). Returns true if one opened.
bool openNext();
// A / 了解了！: next page, or close. Returns true if the tip closed.
bool advance();
void back();                // previous page (CombatRules)
void answer(bool yes);      // AcceptTutorials: yes keeps tips on, no turns them all off
void clear();               // drop the queue and the open tip (new run / tests)

}  // namespace tips

// Queue a tip if it is allowed and not seen yet (and not already queued / open); safe to call
// every frame. Returns true if it is (or already was) queued or open.
bool showTip(Ftue t);

// ---- one-line hooks for triggers inside screens --------------------------------------------

// combat_ui.cpp, before a card play: NCardPlay.CannotPlayThisCardFtueCheck (energy too low) and
// AutoDisableCannotPlayCardFtueCheck (8 cards played without ever seeing it: never show it).
void tipPlayAttempt(sts::Combat& cb, sts::Card* c);
// combat_ui.cpp, on 结束 / X: NEndTurnButton.ShouldShowPlayableCardsFtue. True = the tip opens
// instead of ending the turn; three turns ended with nothing playable retire it.
bool tipBlockEndTurn(sts::Combat& cb);
// reward.cpp, on 继续: NRewardsScreen's RewardFtueCheck. True = the tip opens instead of leaving
// (rewards still listed); leaving an empty list after floor 4 retires it.
bool tipBlockProceed(int itemsLeft, int floor);
// title.cpp, on 出发: NCharacterSelectScreen.OnEmbarkPressed. True = the question opened; the
// run starts once it is answered (App::updateTips).
bool tipAskTutorials();

}  // namespace ui
