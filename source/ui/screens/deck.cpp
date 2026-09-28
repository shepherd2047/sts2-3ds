// Split from ui.cpp (F3).
#include "../ui_common.h"

namespace ui {

// ================================================================ deck view

std::vector<Card*> App::listedCards() {
  std::vector<Card*> cards;
  if (cardListMode_ == CardListMode::Deck || !run_->combat) {
    for (auto& c : run_->deck) cards.push_back(c.get());
    return cards;
  }
  Combat& cb = *run_->combat;
  const std::vector<Card*>& pile = cardListMode_ == CardListMode::Draw ? cb.draw
                                   : cardListMode_ == CardListMode::Discard ? cb.discard : cb.exhaust;
  cards.assign(pile.begin(), pile.end());
  // The draw-pile page must not reveal its actual next-card order.
  std::stable_sort(cards.begin(), cards.end(), [&](Card* a, Card* b) { return cardTitle(a) < cardTitle(b); });
  return cards;
}

void App::openCardList(CardListMode mode) {
  cardListMode_ = mode;
  deckOpen_ = true;
  sel_ = -1;
  scroll_ = 0;
}

void App::drawDeck(bool top) {
  std::vector<Card*> cards = listedCards();
  const char* title = cardListMode_ == CardListMode::Deck ? "牌组" :
                      cardListMode_ == CardListMode::Draw ? "抽牌堆" :
                      cardListMode_ == CardListMode::Discard ? "弃牌堆" : "消耗堆";
  if (top) {
    drawSceneBg(true, 0.7f);
    drawTopBar();
    if (sel_ >= 0 && sel_ < (int)cards.size()) drawCard(cards[sel_], (kTop - 132) / 2, 34, 1.1f, false, true);
    else R().text(kTop / 2, 100, std::string(title) + "（" + num((int)cards.size()) + " 张）", ts(F16, col::gold, CENTER));
    if (cardListMode_ == CardListMode::Draw && sel_ < 0)
      R().text(kTop / 2, 211, "不显示实际抽牌顺序", ts(F12, col::gray, CENTER));
    return;
  }
  drawSceneBg(false, 0.65f);
  drawCardGrid(cards, sel_, 0, 196, scroll_);
  gfx::rect(0, 196, kBot, 44, 0x000000A0);
  if (cardListMode_ == CardListMode::Deck) {
    button(10, 200, 100, 34, "返回", ID_BACK);
    button(118, 200, 84, 34, "详情", ID_DETAIL, sel_ >= 0 && sel_ < (int)cards.size());
    button(kBot - 110, 200, 100, 34, "遗物", ID_RELICS);
  } else {
    button(4, 200, 64, 34, "返回", ID_BACK);
    button(72, 200, 76, 34, "抽牌", ID_PILE_DRAW, true, cardListMode_ == CardListMode::Draw);
    button(154, 200, 76, 34, "弃牌", ID_PILE_DISCARD, true, cardListMode_ == CardListMode::Discard);
    button(236, 200, 76, 34, "消耗", ID_PILE_EXHAUST, true, cardListMode_ == CardListMode::Exhaust);
  }
}

void App::updateDeck(const gfx::Input& in) {
  std::vector<Card*> cards = listedCards();
  int m = (int)cards.size();
  if (cardListMode_ != CardListMode::Deck && (in.down & (gfx::BTN_L | gfx::BTN_R))) {
    int mode = (int)cardListMode_ - (int)CardListMode::Draw;
    mode = (mode + ((in.down & gfx::BTN_R) ? 1 : 2)) % 3;
    openCardList((CardListMode)((int)CardListMode::Draw + mode));
    return;
  }
  if (in.down & gfx::BTN_RIGHT) sel_ = std::min(m - 1, sel_ + 1);
  if (in.down & gfx::BTN_LEFT) sel_ = std::max(0, sel_ - 1);
  if (in.down & gfx::BTN_DOWN) sel_ = std::min(m - 1, sel_ + 5);
  if (in.down & gfx::BTN_UP) sel_ = std::max(0, sel_ - 5);
  if (sel_ >= 0) scroll_ = std::max(0, sel_ / 5 - 1);
  if (in.down & (gfx::BTN_B | gfx::BTN_Y)) { deckOpen_ = false; cardListMode_ = CardListMode::Deck; sel_ = -1; return; }
  if ((in.down & gfx::BTN_A) && sel_ >= 0 && sel_ < m) { detailCard_ = cards[sel_]; detailUpgrade_ = false; return; }
  if (in.touchDown) {
    int id = hitAt(in.tx, in.ty);
    if (id >= ID_GRID0 && id < ID_GRID0 + m) {
      int picked = id - ID_GRID0;
      if (sel_ == picked) { detailCard_ = cards[picked]; detailUpgrade_ = false; return; }
      sel_ = picked;
    }
    if (id == ID_DETAIL && sel_ >= 0 && sel_ < m) { detailCard_ = cards[sel_]; detailUpgrade_ = false; return; }
    if (id == ID_BACK) { deckOpen_ = false; cardListMode_ = CardListMode::Deck; sel_ = -1; }
    if (id == ID_RELICS) { deckOpen_ = false; relicsOpen_ = true; sel_ = run_->relics.empty() ? -1 : 0; scroll_ = 0; }
    if (id == ID_PILE_DRAW) openCardList(CardListMode::Draw);
    if (id == ID_PILE_DISCARD) openCardList(CardListMode::Discard);
    if (id == ID_PILE_EXHAUST) openCardList(CardListMode::Exhaust);
  }
}

}  // namespace ui
