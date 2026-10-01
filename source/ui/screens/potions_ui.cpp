// Split from ui.cpp (F3).
#include "../ui_common.h"
#include "combat_internal.h"

namespace ui {

// ================================================================ potions

std::string App::describePotion(Potion* p) {
  if (!R().hasLoc("potions." + p->locKey + ".description")) return {};
  return expandSmart(L("potions." + p->locKey + ".description"), p->vars, run_->combat != nullptr);
}

void App::drawPotionIcon(Potion* p, float x, float y, float size) {
  if (!p) {  // empty slot: a faint outline
    gfx::circle(x + size / 2, y + size / 2, size * 0.36f, 0xFFFFFF28);
    return;
  }
  Sprite s = R().sprite("potion/" + p->locKey);
  if (!s) { gfx::circle(x + size / 2, y + size / 2, size * 0.4f, 0xC04040FF); return; }
  float k = std::min(size / s.w, size / s.h);
  spr(s, x + (size - s.w * k) / 2, y + (size - s.h * k) / 2, s.w * k, s.h * k);
}

void App::drawPotions(bool top) {
  Run& r = *run_;
  bool fight = r.screen == Screen::Combat && r.combat;
  int n = (int)r.potions.size();
  if (potionSel_ >= n) potionSel_ = -1;
  Potion* p = potionSel_ >= 0 ? r.potions[potionSel_].get() : nullptr;
  if (top) {
    if (fight) drawCombat(true);
    else { drawSceneBg(true, 0.65f); drawTopBar(); }
    if (!p) {
      if (!fight) R().text(kTop / 2, 100, tr("药水", "Potions"), ts(F16, col::gold, CENTER, 0, 1.3f));
      return;
    }
    if (potionAim_ && fight) {
      // S09: the fight stays clear (reticle + arrow from drawCombat); only the target is named.
      auto alive = r.combat->aliveEnemies();
      if (!alive.empty()) {
        Creature* t = alive[std::clamp(target_, 0, (int)alive.size() - 1)];
        std::string name = R().hasLoc("monsters." + t->name + ".name") ? L("monsters." + t->name + ".name") : t->name;
        std::string s = L("potions." + p->locKey + ".title") + " → " + name;
        TextStyle st = ts(F12, col::gold, CENTER);
        float w = R().measure(s, st);
        gfx::rect(kTop / 2 - w / 2 - 8, 21, w + 16, 16, 0x000000B0);
        R().text(kTop / 2, 22, s, st);
      }
      return;
    }
    // The picked potion along the bottom of the top screen (below the creatures' feet).
    gfx::rect(0, kH - 54, kTop, 54, 0x000000C8);
    drawPotionIcon(p, 8, kH - 48, 40);
    R().text(56, kH - 52, L("potions." + p->locKey + ".title"), ts(F16, col::gold));
    R().text(56, kH - 32, describePotion(p), ts(F12, col::white, LEFT, kTop - 64, 0.9f));
    return;
  }
  drawSceneBg(false, 0.75f);
  if (potionAim_ && fight && p) {
    // S09 potion aim (NTargetManager for a potion): the potion and its text at the top, one tile per
    // enemy (a tap throws at it -- one tap picks; ←→ on the D-pad, then A), the arrow running from the potion to
    // the target's reticle on the top screen, 取消 / 使用 below.
    auto alive = r.combat->aliveEnemies();
    const int m = (int)alive.size();
    if (target_ >= m) target_ = 0;
    drawPotionIcon(p, kPotionAimX - 20, kPotionAimY + 2, 40);
    R().text(62, 20, L("potions." + p->locKey + ".title"), ts(F16, col::gold));
    R().text(62, 40, describePotion(p), ts(F12, col::white, LEFT, kBot - 70, 0.9f));
    R().text(kBot / 2, 94, m > 1 ? tr("点选目标即可使用（或 ←→ 再按 A）", "Tap a target to use it (or ←→ then A)") : tr("目标", "Target"), ts(F12, col::gold, CENTER));
    const float gap = 6, tw = m ? std::min(110.f, (kBot - 16 - gap * (m - 1)) / m) : 0, th = 64, ty = 112;
    float tx = (kBot - (tw * m + gap * (m - 1))) / 2;
    for (int i = 0; i < m; ++i, tx += tw + gap) {
      Creature* t = alive[i];
      const bool cur = i == target_;
      panel(tx, ty, tw, th, cur ? 0x5A2A20F0 : 0x2A2218E8, cur ? 0xFF6060FF : 0x8A7A5AFF);
      if (cur) gfx::rect(tx + 2, ty + th - 4, tw - 4, 2, 0xE61E1BFF);
      std::string name = R().hasLoc("monsters." + t->name + ".name") ? L("monsters." + t->name + ".name") : t->name;
      gfx::pushClip(tx + 2, ty, tw - 4, th);
      R().text(tx + tw / 2, ty + 6, name, ts(F12, cur ? col::gold : col::white, CENTER));
      gfx::popClip();
      R().text(tx + tw / 2, ty + 24, num(std::max(0, t->hp)) + "/" + num(t->maxHp), ts(F12, 0xFF8080FF, CENTER));
      if (t->block > 0) R().text(tx + tw / 2, ty + 40, tr("格挡 ", "Block ") + num(t->block), ts(F12, col::blue, CENTER, 0, 0.9f));
      hits_.push_back({tx, ty, tw, th, ID_GRID0 + i});
    }
    button(10, 196, 110, 36, tr("取消", "Cancel"), ID_BACK);
    button(kBot - 120, 196, 110, 36, tr("使用", "Use"), ID_CONFIRM, m > 0, true);
    if (m > 0 && centers_.count(alive[target_])) {
      auto [cx, cy] = centers_[alive[target_]];
      drawArrow(false, kPotionAimX + kBotOX, kPotionAimY + kBotOY, cx, cy, true, false);
    }
    return;
  }
  R().text(kBot / 2, 4, tr("药水", "Potions"), ts(F16, col::gold, CENTER));
  const float rx = 10, rw = kBot - 20, gap = n > 3 ? 4 : 6, y0 = 26;
  const float rh = std::min(46.f, (190.f - y0 - gap * (n - 1)) / n);
  for (int i = 0; i < n; ++i) {
    float y = y0 + i * (rh + gap);
    Potion* q = r.potions[i].get();
    bool hl = i == potionSel_;
    panel(rx, y, rw, rh, hl ? 0x5A3A20F0 : 0x2A2218E8, hl ? 0xFFD870FF : 0x8A7A5AFF);
    drawPotionIcon(q, rx + 5, y + (rh - std::min(36.f, rh - 4)) / 2, std::min(36.f, rh - 4));
    if (q) {
      R().text(rx + 48, y + 3, L("potions." + q->locKey + ".title"), ts(F12, col::gold));
      TextStyle st = ts(F12, col::white, LEFT, rw - 54, 0.8f);
      std::string d = describePotion(q);
      float dh;
      R().measure(d, st, &dh);
      float room = rh - 20;
      if (room < 10) { st.maxWidth = 0; d = d.substr(0, 0); }  // too small: title only
      else if (dh > room) st.scale *= room / dh;
      if (!d.empty()) R().text(rx + 48, y + 19, d, st);
    } else {
      R().text(rx + 48, y + (rh - R().lineHeight(F12)) / 2, tr("空", "Empty"), ts(F12, col::gray));
    }
    hits_.push_back({rx, y, rw, rh, ID_POTION0 + i});
  }
  bool canUse = potionSel_ >= 0 && r.canUsePotion(potionSel_);
  button(10, 196, 96, 36, tr("返回", "Back"), ID_BACK);
  button(112, 196, 96, 36, tr("丢弃", "Discard"), ID_DISCARD, p != nullptr && r.canUseOrRemovePotions);
  button(214, 196, 96, 36, tr("使用", "Use"), ID_USE, canUse, true);
}

void App::updatePotions(const gfx::Input& in) {
  Run& r = *run_;
  bool fight = r.screen == Screen::Combat && r.combat;
  int n = (int)r.potions.size();
  auto close = [&] { potionsOpen_ = false; potionAim_ = false; potionSel_ = -1; };
  // In combat the list only makes sense while the player may act.
  if (fight && !(r.combat->playerPhase && r.combat->actions.waiting())) { close(); return; }
  auto fire = [&](Creature* target) {
    int slot = potionSel_;
    sfx::potionUsed();
    close();
    if (fight) {
      PlayerAction a;
      a.kind = PlayerAction::UsePotion;
      a.potionSlot = slot;
      a.target = target;
      r.combat->actions.fire(a);
    } else {
      Scheduler::get().spawn(r.usePotion(slot, nullptr));
    }
  };
  auto use = [&] {
    if (potionSel_ < 0 || !r.canUsePotion(potionSel_)) return;
    if (fight && r.potions[potionSel_]->target == TargetType::AnyEnemy) {
      potionAim_ = true;
      target_ = 0;
      return;
    }
    fire(nullptr);
  };
  int id = in.touchDown ? hitAt(in.tx, in.ty) : ID_NONE;
  if (potionAim_) {
    auto alive = fight ? r.combat->aliveEnemies() : std::vector<Creature*>{};
    int m = (int)alive.size();
    if (m == 0 || (in.down & gfx::BTN_B) || id == ID_BACK) { potionAim_ = false; return; }
    if (in.down & (gfx::BTN_LEFT | gfx::BTN_L)) target_ = (target_ + m - 1) % m;
    if (in.down & (gfx::BTN_RIGHT | gfx::BTN_R)) target_ = (target_ + 1) % m;
    if (id >= ID_GRID0 && id < ID_GRID0 + m) {  // a tile: throw at it (one tap picks)
      target_ = id - ID_GRID0;
      sfx::click();
      fire(alive[target_]);
      return;
    }
    if ((in.down & gfx::BTN_A) || id == ID_CONFIRM) fire(alive[std::min(target_, m - 1)]);
    return;
  }
  if ((in.down & gfx::BTN_B) || id == ID_BACK) { close(); return; }
  if (in.down & gfx::BTN_DOWN) potionSel_ = std::min(n - 1, potionSel_ + 1);
  if (in.down & gfx::BTN_UP) potionSel_ = std::max(0, potionSel_ - 1);
  if (in.down & gfx::BTN_A) use();
  if ((in.down & gfx::BTN_X) && potionSel_ >= 0 && r.canUseOrRemovePotions) r.discardPotion(potionSel_);
  if (id >= ID_POTION0 && id < ID_POTION0 + n) potionSel_ = id - ID_POTION0;
  if (id == ID_USE) use();
  if (id == ID_DISCARD && potionSel_ >= 0 && r.canUseOrRemovePotions) r.discardPotion(potionSel_);
}

// PotionReward: the potion on top; take or skip below. With a full belt the belt is
// listed so one can be discarded first (the game refuses the reward while it is full). Used by
// treasure chests, Ancients and events, not by combat rewards -- S14 moved the post-combat
// potion into the interactive reward list (reward.cpp), which claims it in place instead of
// pushing through this screen.
void App::drawPotionOffer(bool top) {
  Run& r = *run_;
  Potion* p = r.potionOffer.get();
  if (top) {
    drawSceneBg(true, 0.6f);
    drawTopBar();
    R().text(kTop / 2, 24, tr("药水", "Potions"), ts(F16, col::gold, CENTER));
    if (p) {
      gfx::circle(kTop / 2.f, 84, 38, 0xFFE07030);
      drawPotionIcon(p, kTop / 2.f - 28, 56, 56);
      TextStyle nt = ts(F16, col::gold, CENTER);
      nt.scale = 1.2f;
      R().text(kTop / 2.f, 124, L("potions." + p->locKey + ".title"), nt);
      R().text(kTop / 2.f, 152, describePotion(p), ts(F12, col::white, CENTER, kTop - 60));
    }
    return;
  }
  drawSceneBg(false, 0.55f);
  bool room = r.hasOpenPotionSlot();
  if (p) {
    float s = 40, x = kBot / 2 - s / 2, y = room ? 60 : 8;
    drawPotionIcon(p, x, y, s);
    R().text(kBot / 2, y + s + 4, L("potions." + p->locKey + ".title"), ts(F16, col::white, CENTER));
  }
  if (!room) {
    R().text(kBot / 2, 74, tr("药水栏已满：点一瓶丢弃，或跳过", "Potion belt full: tap one to discard, or skip"), ts(F12, col::gold, CENTER));
    for (int i = 0; i < (int)r.potions.size(); ++i) {
      const int belt = (int)r.potions.size();
      const float pw = std::min(72.f, (kBot - 20.f) / belt - 6), step = (kBot - 20.f) / belt;
      float x = 10 + i * step + (step - pw) / 2, y = 96;
      panel(x, y, pw, 80, 0x2A2218E8, 0x8A7A5AFF);
      drawPotionIcon(r.potions[i].get(), x + pw / 2 - 20, y + 6, 40);
      if (r.potions[i]) R().text(x + pw / 2, y + 50, L("potions." + r.potions[i]->locKey + ".title"), ts(F12, col::white, CENTER, pw - 2, 0.7f));
      R().text(x + pw / 2, y + 64, tr("丢弃", "Discard"), ts(F12, col::red, CENTER, 0, 0.8f));
      hits_.push_back({x, y, pw, 80, ID_POTION0 + i});
    }
  }
  // S14: nine-slice widget buttons (F3) in place of the flat-fill legacy button().
  widgets::beginFrame(gfx::input());
  if (widgets::button(1, 10, style::kActionY, 110, style::kButtonH, tr("跳过", "Skip")) && r.potionOfferChoice.waiting())
    r.potionOfferChoice.fire(0);
  if (widgets::button(2, kBot - 120, style::kActionY, 110, style::kButtonH, tr("拿取", "Take"), widgets::Kind::Primary,
                      p != nullptr && room) &&
      r.potionOfferChoice.waiting())
    r.potionOfferChoice.fire(1);
  widgets::endFrame();
}

void App::updatePotionOffer(const gfx::Input& in) {
  Run& r = *run_;
  if (!r.potionOfferChoice.waiting()) return;
  int id = in.touchDown ? hitAt(in.tx, in.ty) : ID_NONE;
  if (id >= ID_POTION0 && id < ID_POTION0 + (int)r.potions.size()) {
    if (r.canUseOrRemovePotions) r.discardPotion(id - ID_POTION0);  // Player.CanUseOrRemovePotions
    return;
  }
  if ((in.down & gfx::BTN_A) && r.hasOpenPotionSlot()) { r.potionOfferChoice.fire(1); return; }
  if (in.down & gfx::BTN_B) r.potionOfferChoice.fire(0);
}

}  // namespace ui
