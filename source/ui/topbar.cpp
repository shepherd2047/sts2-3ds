// Split from ui.cpp (F3).
#include "ui_common.h"

namespace ui {

// ================================================================ top bar

void App::drawTopBar() {
  gfx::rect(0, 0, kTop, 18, 0x000000A0);
  Creature* p = run_->player.get();
  // F6: the shown numbers tick towards the real value instead of snapping (App::update).
  int shownHp = shownHp_ >= 0 ? (int)std::lround(shownHp_) : p->hp;
  int shownGold = shownGold_ >= 0 ? (int)std::lround(shownGold_) : run_->gold;
  R().text(4, 2, "生命 " + num(shownHp) + "/" + num(p->maxHp), ts(F12, col::red));
  R().text(84, 2, "金币 " + num(shownGold), ts(F12, col::gold));
  R().text(148, 2, "第 " + num(run_->floor) + " 层", ts(F12, col::white));
  R().text(198, 2, "牌组 " + num((int)run_->deck.size()), ts(F12, col::white));
  // Potion belt (TopBar.PotionContainer), after the deck count.
  const int belt = (int)run_->potions.size();
  for (int i = 0; i < belt; ++i) drawPotionIcon(run_->potions[i].get(), 250 + i * 17, 1, 16);
  // Relics from the right edge, as many as fit after the belt; the rest are counted as
  // "+N" (all of them are in the relic page).
  const int n = (int)run_->relics.size();
  const int room = std::max(1, (int)((kTop - 2 - (250 + belt * 17)) / 20));
  const int fit = n > room ? room - 1 : n;
  float x = kTop - 20;
  for (int i = 0; i < fit; ++i, x -= 20) drawRelicIcon(run_->relics[i].get(), x, 0, 18);
  if (n > fit) R().text(x + 18, 3, "+" + num(n - fit), ts(F12, col::gold, RIGHT));
}

void App::drawRelicIcon(Relic* r, float x, float y, float size) {
  float pulse = r->flash > 0 ? 1.f + 0.25f * r->flash : 1.f;
  float s = size * pulse, off = (s - size) / 2;
  if (r->flash > 0) gfx::circle(x + size / 2, y + size / 2, s * 0.6f, 0xFFE07000 | (uint32_t)(r->flash * 0x90));
  spr(R().sprite("relic/" + r->icon), x - off, y - off, s, s, r->usedUp ? 0x000000FF : 0xFFFFFFFF, r->usedUp ? 0.55f : 0.f);
  r->flash = std::max(0.f, r->flash - 0.02f);
  if (r->showCounter()) {
    std::string c = num(r->displayAmount());
    R().text(x + size, y + size - 10, c, ts(F12, col::white, RIGHT, 0, size < 24 ? 0.8f : 1.f));
  }
}

}  // namespace ui
