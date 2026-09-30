// Split from ui.cpp (F3).
#include "../ui_common.h"
#include "combat_internal.h"
#include <cctype>

namespace ui {

namespace {
// S12: the hand-select prompt (C# NPlayerHand._selectionHeader = prefs.Prompt with Amount /
// MinCount / MaxCount). Rules pass a loc key, a card/relic id or plain text.
std::string handSelectPrompt(const CardChoice& ch, int minN, int maxN) {
  const std::string& p = ch.prompt;
  std::string s;
  if (R().hasLoc(p)) s = L(p);
  else if (R().hasLoc("cards." + p + ".selectionScreenPrompt")) s = L("cards." + p + ".selectionScreenPrompt");
  else if (R().hasLoc("relics." + p + ".selectionScreenPrompt")) s = L("relics." + p + ".selectionScreenPrompt");
  else if (R().hasLoc("cards." + p + ".title")) s = "[gold]" + L("cards." + p + ".title") + "[/gold]：选择[blue]" + num(maxN) + "[/blue]张牌";
  else s = p;
  auto sub = [&](const std::string& key, int v) {
    for (size_t at; (at = s.find(key)) != std::string::npos;) s.replace(at, key.size(), num(v));
  };
  sub("{Amount}", maxN);
  sub("{MinCount}", minN);
  sub("{MaxCount}", maxN);
  return s;
}

// A new hand choice resets the picks and focuses the first card that can be picked.
HandSelect& syncHandSelect(const Combat& cb, int& sel) {
  HandSelect& hs = handSel();
  if (hs.options != cb.choice.options) {
    hs = HandSelect{};
    hs.options = cb.choice.options;
    sel = -1;
    for (int i = 0; i < (int)cb.hand.size() && sel < 0; ++i)
      if (hs.has(cb.hand[i])) sel = i;
    widgets::setFocus(-1);
  }
  return hs;
}

constexpr int kHsClearId = 0x5120, kHsConfirmId = 0x5121;  // widget ids (bottom action bar)
}  // namespace

void App::drawCombat(bool top) {
  Combat* cb = run_->combat.get();
  if (!cb) return;
  auto enemies = visibleEnemies();
  auto alive = cb->aliveEnemies();
  int n = (int)cb->hand.size();
  if (sel_ >= n) sel_ = -1;
  if (target_ >= (int)alive.size()) target_ = 0;
  const bool selecting = isHandSelect(*cb);  // S12 hand select (U14)
  if (selecting) syncHandSelect(*cb, sel_);
  Card* selCard = sel_ >= 0 && !selecting ? cb->hand[sel_] : nullptr;
  bool canAct = cb->playerPhase && cb->actions.waiting();

  // Which card aims where this frame.
  Creature* tgt = nullptr;
  bool arrow = false, arrowAlly = false;
  float afx = 0, afy = 0;
  if (drag_.down && drag_.moved && drag_.armed && drag_.card) {
    afx = drag_.x + kBotOX;
    afy = drag_.y - kCardH * kDragS / 2 + kBotOY;
    if (drag_.card->target == TargetType::AnyEnemy && drag_.target) { tgt = drag_.target; arrow = true; }
    else if (drag_.card->target == TargetType::Self) { tgt = cb->player; arrow = arrowAlly = true; }
  } else if (potionsOpen_ && potionAim_ && !alive.empty()) {
    if (target_ >= (int)alive.size()) target_ = 0;
    tgt = alive[target_];
  } else if (aiming_ && selCard && !alive.empty()) {
    float cx = std::clamp(handSlot(n, sel_).x, 70.f, kBot - 70.f);
    afx = cx + kBotOX;
    afy = kPreviewY + kBotOY;
    tgt = alive[target_];
    arrow = true;
  }
  float atx = 0, aty = 0;
  if (arrow && centers_.count(tgt)) { atx = centers_[tgt].first; aty = centers_[tgt].second; }
  else arrow = false;

  if (top) {
    gfx::Texture* bg = R().texture(actTexture(*run_, "bg_"));
    gfx::image(bg, 0, 0, kTop, kH, 0, 0, kTop, kH);
    gfx::rectGradient(0, 150, kTop, 90, 0x00000000, 0x00000060);
    const float feet = 170;  // ~70% down the screen, the room's floor line (RGDSplus/native)
    auto center = [&](Creature* c, float x) {
      Sprite s = R().sprite("creature/" + (c->isPlayer ? playerArt(run_.get()) : c->name));
      centers_[c] = {x, s ? feet - s.ay + s.h / 2.f : feet - 30};
    };
    center(cb->player, 95);
    drawCreature(cb->player, 95, feet, tgt == cb->player && !arrowAlly);
    // X4.5: the Necrobinder's Osty (Combat::osty) stands beside the player. drawCreature already
    // draws its idle animation, HP bar and powers (it treats any non-player Creature the same
    // way), and fades it out on death (c->dead() -> "if (dying) return;" after the die anim), so
    // no separate dead-state drawing is needed here.
    if (cb->osty && !cb->osty->removed) drawCreature(cb->osty, 150, feet, false);
    // X2.5: the Defect's orb slots (Combat::orbQueue / orbCapacity), laid out like NOrbManager's
    // arc around the player but simplified to a row above the top bar for the 400px screen.
    // Filled slots show the queued orb (id "<Name>Orb" -> sprite orb/<name>), empty ones
    // orb/empty; orbCapacity can exceed the queue (Defect starts with 3, relics/Focus add more).
    if (run_->character().orbSlots > 0 || cb->orbCapacity > 0) {
      const float os = 20, ogap = 4, oy2 = 24;
      float ox2 = 4;
      for (int i = 0; i < cb->orbCapacity; ++i) {
        std::string name = "orb/empty";
        if (i < (int)cb->orbQueue.size()) {
          std::string id = cb->orbQueue[i]->id;  // "LightningOrb" -> "orb/lightning"
          std::string lower;
          for (char ch : id) lower += (char)std::tolower((unsigned char)ch);
          size_t suf = lower.rfind("orb");
          if (suf != std::string::npos && suf + 3 == lower.size()) lower.resize(suf);
          name = "orb/" + lower;
          if (!R().sprite(name)) name = "orb/empty";
        }
        spr(R().sprite(name), ox2, oy2, os, os);
        ox2 += os + ogap;
      }
    }
    for (int i = 0; i < (int)enemies.size(); ++i) {
      float x = enemyX(i, (int)enemies.size());
      center(enemies[i], x);
      bool aoe = drag_.down && drag_.armed && drag_.card && drag_.card->target == TargetType::AllEnemies;
      drawCreature(enemies[i], x, feet, enemies[i] == tgt || (aoe && enemies[i]->alive()));
    }
    for (auto& f : floats_) {
      if (f.t < 0) continue;
      float x = 95, y = feet - 60;
      if (f.who && !f.who->isPlayer)
        for (int i = 0; i < (int)enemies.size(); ++i)
          if (enemies[i] == f.who) x = enemyX(i, (int)enemies.size());
      float a = std::clamp(1.2f - f.t, 0.f, 1.f);
      TextStyle st = ts(F16, (f.color & 0xFFFFFF00) | (uint32_t)(a * 255), CENTER);
      st.scale = 1.3f;
      R().text(x + f.dx, y - f.t * 40, f.text, st);
    }
    if (selecting) {
      // S12 top screen: the battlefield dimmed under the prompt, the k / N counter at the left,
      // the focused hand card large in the middle, the picked cards listed at the right.
      const HandSelect& hs = handSel();
      const int maxN = handSelectMax(*cb), minN = handSelectMin(*cb), k = (int)hs.picks.size();
      const bool valid = k >= minN && k <= maxN;
      gfx::rect(0, 18, kTop, kH - 18, 0x000000B0);
      TextStyle pt = ts(F16, col::white, CENTER, kTop - 24);
      std::string prompt = handSelectPrompt(cb->choice, minN, maxN);
      float ph = 0;
      R().measure(prompt, pt, &ph);
      R().text(kTop / 2, 22, prompt, pt);
      const float lineY = 22 + std::max(ph, R().lineHeight(F16)) + 3;
      gfx::rect(kTop / 2 - 100, lineY, 200, 2, style::kPanelHi);

      const float cs = 1.f, cw = kCardW * cs, ch = kCardH * cs;
      const float cx = (kTop - cw) / 2, cy = std::min(kH - ch - 6, std::max(62.f, lineY + 8));
      const float sideY = cy + 8, sideW = 118;
      // Counter.
      {
        const float x = 10, h = 104;
        widgets::panel("ui/hover_tip", x, sideY, sideW, h);
        R().text(x + sideW / 2, sideY + 8, "已选", ts(F12, col::gold, CENTER));
        TextStyle kt = ts(F16, valid ? col::gold : col::white, CENTER, 0, 1.6f);
        R().text(x + sideW / 2, sideY + 26, num(k) + " / " + num(maxN), kt);
        std::string range = minN == maxN ? "需选 " + num(maxN) + " 张"
                            : minN == 0  ? "最多 " + num(maxN) + " 张，可不选"
                                         : "选 " + num(minN) + "–" + num(maxN) + " 张";
        R().text(x + sideW / 2, sideY + 60, range, ts(F12, col::gray, CENTER, sideW - 12));
        if (k < minN) R().text(x + sideW / 2, sideY + 80, "还需选 " + num(minN - k) + " 张", ts(F12, col::red, CENTER));
      }
      // Picked cards, in pick order.
      {
        const float x = kTop - 10 - sideW;
        const int rows = std::max(1, std::min(k, 8));
        widgets::panel("ui/hover_tip", x, sideY, sideW, 30 + rows * 15.f);
        R().text(x + sideW / 2, sideY + 8, "已选的牌", ts(F12, col::gold, CENTER));
        if (k == 0) R().text(x + sideW / 2, sideY + 26, "（无）", ts(F12, col::gray, CENTER));
        for (int i = 0; i < k && i < 8; ++i) {
          std::string name = i == 7 && k > 8 ? "…" : num(i + 1) + ". " + cardTitle(hs.picks[i]);
          gfx::pushClip(x + 6, sideY + 24 + i * 15.f, sideW - 12, 15);
          R().text(x + 8, sideY + 25 + i * 15.f, name, ts(F12, col::white));
          gfx::popClip();
        }
      }
      // The focused card, readable.
      Card* fc = sel_ >= 0 && sel_ < n ? cb->hand[sel_] : nullptr;
      if (fc) {
        bool ok = hs.has(fc), pk = hs.picked(fc);
        drawCard(fc, cx, cy, cs, !ok, true, pk);
        std::string tag = pk ? "已选中" : ok ? "" : "不可选";
        if (!tag.empty()) {
          TextStyle tt = ts(F12, pk ? col::gold : col::gray, CENTER);
          float tw = R().measure(tag, tt) + 14;
          gfx::rect(kTop / 2 - tw / 2, cy + ch - 20, tw, 16, 0x000000D0);
          R().text(kTop / 2, cy + ch - 19, tag, tt);
        }
      } else {
        R().text(kTop / 2, cy + ch / 2 - 8, "←→ 或触摸下屏选择手牌", ts(F12, col::gray, CENTER));
      }
    }
    drawTopBar();
    if (cb->bannerTime > 0) {
      cb->bannerTime -= 1.f / 60;
      float a = std::min(1.f, cb->bannerTime * 2);
      gfx::rect(0, 96, kTop, 40, (uint32_t)(a * 0xA0));
      TextStyle st = ts(F16, col::gold, CENTER);
      st.scale = 1.6f;
      R().text(kTop / 2, 104, cb->banner, st);
    }
    if (arrow) drawArrow(true, afx, afy, atx, aty, true, arrowAlly);
    drawFlights(true);
    if (cb->choice.active && combatChooseOne()) {  // S13: the focused offer over the fight
      gfx::rect(0, 0, kTop, kH, 0x000000B0);
      chooseOneDraw(combatChooseOneSpec(), true);
    }
    return;
  }

  // ---- bottom screen (RGDSplus layout): the same room behind, no status strip, the hand
  // centred, energy and end turn at the sides below it, piles in the bottom corners.
  gfx::Texture* room = R().texture(actTexture(*run_, "bg_"));
  gfx::image(room, kBotOX, 0, kBot, kH, 0, 0, kBot, kH, 0x000000FF, 0.15f);

  if (cb->choice.active && combatChooseOne()) {  // S13: generated cards -> the choose-one screen
    gfx::rect(0, 0, kBot, kH, style::kScrim);
    chooseOneDraw(combatChooseOneSpec(), false);
    return;
  }
  if (selecting) {
    // S12 bottom screen: the fanned hand stays (C# moves the hand in front of a backstop);
    // pickable cards bright, others dimmed, picked ones raised with their pick number;
    // 重选 left, the counter centre, 确认 right (enabled only for a legal count).
    HandSelect& hs = handSel();
    const int maxN = handSelectMax(*cb), minN = handSelectMin(*cb), k = (int)hs.picks.size();
    const bool valid = k >= minN && k <= maxN && cb->choice.result.waiting();
    gfx::rect(0, 0, kBot, kH, 0x00000070);
    gfx::Input in = pauseOpen_ ? gfx::Input{} : gfx::input();
    {
      int fo = widgets::focused();
      if (fo != kHsClearId && fo != kHsConfirmId) widgets::setFocus(-1);
    }
    const int prevFocus = widgets::focused();
    widgets::beginFrame(in);
    // The D-pad walks the hand (updateCombat) while no button is focused; down goes to the
    // bar, up comes back.
    const uint32_t dpad = gfx::BTN_LEFT | gfx::BTN_RIGHT | gfx::BTN_UP | gfx::BTN_DOWN;
    if (in.down & dpad) {
      if (prevFocus < 0) widgets::setFocus((in.down & gfx::BTN_DOWN) ? (valid ? kHsConfirmId : k > 0 ? kHsClearId : -1) : -1);
      else if (in.down & gfx::BTN_UP) widgets::setFocus(-1);
    }
    R().text(kBot / 2, 4, handSelectPrompt(cb->choice, minN, maxN), ts(F12, col::white, CENTER, kBot - 16));

    auto flying = [&](Card* c) {
      for (auto& f : flights_) if (f.card == c) return true;
      return false;
    };
    drawGhosts();
    const bool padOnHand = widgets::usingPad() && widgets::focused() < 0;
    // Left to right (right cards overlap left ones), arriving cards next, the focused card last.
    for (int pass = 0; pass < 3; ++pass)
      for (int i = 0; i < n; ++i) {
        Card* c = cb->hand[i];
        if (flying(c)) continue;
        auto it = poses_.find(c);
        if (it == poses_.end() || it->second.delay > 0) continue;
        const Pose& p = it->second;
        int want = i == sel_ ? 2 : p.drawT < 1 ? 1 : 0;
        if (want != pass) continue;
        bool ok = hs.has(c), pk = hs.picked(c);
        float w = kCardW * p.s, h = kCardH * p.s, x = p.x - w / 2, y = p.y - h / 2;
        gfx::pushTransform(gfx::Affine::rotateAround(p.x, p.y, p.angle));
        drawCard(c, x, y, p.s, !ok, true, pk);
        if (pk) {
          int order = (int)(std::find(hs.picks.begin(), hs.picks.end(), c) - hs.picks.begin()) + 1;
          const float bx = x + w - 5, by = y + 5;  // top-right corner, clear of cost and title
          gfx::circle(bx, by, 8, style::kFocus);
          R().text(bx, by - R().lineHeight(F12) / 2, num(order), ts(F12, 0x2A1A08FF, CENTER));
        }
        if (i == sel_ && padOnHand) {
          const float t = 2;
          gfx::rect(x - t, y - t, w + 2 * t, t, style::kFocus);
          gfx::rect(x - t, y + h, w + 2 * t, t, style::kFocus);
          gfx::rect(x - t, y - t, t, h + 2 * t, style::kFocus);
          gfx::rect(x + w, y - t, t, h + 2 * t, style::kFocus);
        }
        gfx::popTransform();
      }

    if (widgets::button(kHsClearId, style::kMargin, style::kActionY, 72, style::kButtonH, "重选",
                        widgets::Kind::Secondary, k > 0 && cb->choice.result.waiting())) {
      hs.picks.clear();
      widgets::setFocus(-1);
    }
    if (widgets::button(kHsConfirmId, kBot - style::kMargin - 96, style::kActionY, 96, style::kButtonH, "确认",
                        widgets::Kind::Primary, valid))
      hs.confirm = true;  // fired by updateCombat next frame
    R().text(kBot / 2, style::kActionY + 2, num(k) + " / " + num(maxN), ts(F16, valid ? col::gold : col::white, CENTER));
    std::string why = k < minN ? "还需选 " + num(minN - k) + " 张" : minN < maxN ? "最多 " + num(maxN) + " 张" : "";
    if (!why.empty()) R().text(kBot / 2, style::kActionY + 20, why, ts(F12, k < minN ? col::red : col::gray, CENTER));
    widgets::endFrame();
    return;
  }

  if (cb->choice.active) {
    gfx::rect(0, 0, kBot, kH, 0x000000A0);
    const CardChoice& ch = cb->choice;
    std::string prompt = R().hasLoc(ch.prompt) ? L(ch.prompt)
                         : R().hasLoc("cards." + ch.prompt + ".title") ? L("cards." + ch.prompt + ".title") + "：选择一张牌" : ch.prompt;
    R().text(kBot / 2, 4, prompt, ts(F16, col::gold, CENTER, kBot - 8, 0.9f));
    drawCardGrid(ch.options, sel_, 26, 196, scroll_);
    bool multi = ch.maxCount > 1;
    if (multi) {  // picked cards get a gold tick (as in the deck choice)
      for (int i : deckPicks_) {
        int row = i / 5 - scroll_;
        const float gs = 0.46f, cw = 120 * gs, gch = 169 * gs, gap = (kBot - 5 * cw) / 6;
        float x = gap + (i % 5) * (cw + gap), y = 26 + row * (gch + 8);
        if (y >= 26 && y < 196) gfx::circle(x + cw - 6, y + 6, 6, 0xFFD870FF);
      }
    }
    bool ready = multi ? (int)deckPicks_.size() >= ch.minCount : sel_ >= 0 && sel_ < (int)ch.options.size();
    if (ch.minCount == 0) button(kBot / 2 - 50, 200, 100, 34, "跳过", ID_SKIP);
    button(kBot - 110, 200, 100, 34, "确认", ID_CONFIRM, ready);
    if (!multi && sel_ >= 0 && sel_ < (int)ch.options.size()) R().text(10, 206, cardTitle(ch.options[sel_]), ts(F12, col::white));
    if (multi) R().text(10, 208, "已选 " + num((int)deckPicks_.size()), ts(F12, col::white));
    return;
  }

  // Energy orb left and end turn right, level with each other a little below the hand
  // (~77% down, ~10% / ~87% across on RGDSplus). Colour is the run's character
  // (Character::energyColor; X1.5-X4.5).
  Sprite orb = R().sprite(energyOrbSprite(run_.get()));
  const float ox = 32, oy = 185, od = 34;
  spr(orb, ox - od / 2, oy - od / 2, od, od);
  TextStyle et = ts(F12, cb->energy > 0 ? col::white : col::gray, CENTER);
  R().text(ox, oy - R().lineHeight(F12) / 2, num(cb->energy) + "/" + num(cb->maxEnergyNow()), et);
  // X3.5: the Regent's star counter (Combat::stars, Character::alwaysShowStars) next to energy.
  if (run_->character().alwaysShowStars || cb->stars > 0) {
    const float sx = ox + od / 2 + 22, sy = oy;
    spr(R().sprite("ui/star"), sx - 9, sy - 9, 18, 18);
    R().text(sx + 12, sy - R().lineHeight(F12) / 2, num(cb->stars), ts(F12, col::white, LEFT));
  }
  Sprite endTurn = R().sprite("ui/end_turn");
  const float ew = 64, eh = 32, ex = 278 - ew / 2, ey = oy - eh / 2;  // sprite is 2:1
  spr(endTurn, ex, ey, ew, eh, canAct ? 0xFFFFFFFF : 0x000000FF, canAct ? 0 : 0.5f);
  {
    // The visible plate is ~78% x 62% of the sprite, centred; the label fills 80% of it.
    const float pw = ew * 0.78f, ph = eh * 0.62f;
    TextStyle lt = ts(F16, canAct ? col::white : col::gray, CENTER);
    // The text box takes 80% of the plate; CJK ink sits a little below the box middle (measured).
    const float inkMid = 0.53f;
    float lw = R().measure("结束", lt), lh = R().lineHeight(F16);
    lt.scale = std::min(0.8f * pw / lw, 0.8f * ph / lh);
    R().text(ex + ew / 2, ey + eh / 2 - lh * lt.scale * inkMid, "结束", lt);
  }
  if (canAct) hits_.push_back({ex, ey - 4, ew, eh + 8, ID_END_TURN});
  // Potions: a small button bottom centre, between the piles (opens the belt list).
  {
    const float pw = 58, ph = 22, px = (kBot - pw) / 2, py = 214;
    panel(px, py, pw, ph, canAct ? 0x3A2E24E8 : 0x2A2A2AC0, canAct ? 0xB89A60FF : 0x555555FF);
    int filled = 0;
    for (auto& pt : run_->potions) filled += pt != nullptr;
    R().text(px + pw / 2, py + (ph - R().lineHeight(F12)) / 2, "药水 " + num(filled), ts(F12, canAct ? col::white : col::gray, CENTER));
    if (canAct) hits_.push_back({px, py - 2, pw, ph + 4, ID_POTIONS});
  }
  // Piles: small icons in the corners with a red count badge.
  auto pile = [&](const char* sprite, float cx, float cy, int count) {
    spr(R().sprite(sprite), cx - 12, cy - 12, 24, 24);
    gfx::circle(cx + 10, cy + 8, 7, 0xC02828FF);
    R().text(cx + 10, cy + 8 - R().lineHeight(F12) * 0.4f, num(count), ts(F12, col::white, CENTER, 0, 0.8f));
  };
  int waiting = 0;  // drawn cards still sitting on the pile, waiting for their turn to fly
  for (auto& [c, p] : poses_) waiting += p.delay > 0;
  pile("ui/draw_pile", kDrawPileX, kDrawPileY, (int)cb->draw.size() + waiting);
  pile("ui/discard_pile", kDiscardX, kDiscardY, (int)cb->discard.size());
  hits_.push_back({0, 201, 34, 39, ID_PILE_DRAW});
  hits_.push_back({kBot - 34.f, 201, 34, 39, ID_PILE_DISCARD});
  if (!cb->exhaust.empty()) {
    R().text(kBot - 38, 200, "消耗 " + num((int)cb->exhaust.size()), ts(F12, col::white, RIGHT));
    hits_.push_back({kBot - 104.f, 196, 66, 24, ID_PILE_EXHAUST});
  }

  auto flying = [&](Card* c) {
    for (auto& f : flights_) if (f.card == c) return true;
    return false;
  };
  drawGhosts();
  // Fan, left to right so right cards overlap left ones (native order); cards still
  // arriving from the draw pile go on top.
  for (int pass = 0; pass < 2; ++pass)
    for (int i = 0; i < n; ++i) {
      Card* c = cb->hand[i];
      if (flying(c) || (drag_.down && drag_.moved && drag_.card == c) || (i == sel_ && !drag_.down)) continue;
      auto it = poses_.find(c);
      if (it == poses_.end() || it->second.delay > 0) continue;
      const Pose& p = it->second;
      if ((p.drawT < 1) != (pass == 1)) continue;
      gfx::pushTransform(gfx::Affine::rotateAround(p.x, p.y, p.angle));
      drawCard(c, p.x - kCardW * p.s / 2, p.y - kCardH * p.s / 2, p.s, !cb->canPlay(c) && canAct, true, false);
      gfx::popTransform();
    }
  // Tapped card: raised and enlarged so its text is readable (native hover).
  if (selCard && !flying(selCard) && !(drag_.down && drag_.moved && drag_.card == selCard)) {
    float cx = std::clamp(handSlot(n, sel_).x, 70.f, kBot - 70.f);
    drawCard(selCard, cx - kCardW * kPreviewS / 2, kPreviewY, kPreviewS, !cb->canPlay(selCard) && canAct, true, aiming_);
    std::string why;
    if (canAct && !cb->canPlay(selCard, &why)) {
      std::string msg = why == "ENERGY" ? L("combat_messages.NOT_ENOUGH_ENERGY") : L("combat_messages.UNPLAYABLE");
      for (auto& ch : msg) if (ch == '\n') ch = ' ';
      R().text(cx, kPreviewY + kCardH * kPreviewS + 2, msg, ts(F12, col::red, CENTER));
    }
  }
  // Play line and cancel zone while dragging; the hint sits between the energy orb and
  // end turn, below the hand, where neither the finger nor the card covers it.
  if (drag_.down && drag_.moved && drag_.card && !flying(drag_.card)) {
    float pulse = 0.5f + 0.5f * std::sin(clock_ * 6.f);
    std::string hint;
    uint32_t hc;
    if (drag_.armed) {
      gfx::rect(0, kPlayLine, kBot, kH - kPlayLine, 0x00000060);
      gfx::rect(0, kPlayLine, kBot, 1, 0xFFFFFF60);
      hint = "松手打出 · 拖回手牌取消";
      hc = 0x90FF90FF;
    } else {
      gfx::rect(0, 0, kBot, kPlayLine, 0x60C0FF00 | (uint32_t)(0x10 + pulse * 0x18));
      for (float x = 4; x < kBot; x += 12) gfx::rect(x, kPlayLine, 6, 2, 0x60C0FFC0);
      hint = "↑ 拖过虚线出牌 · 松手取消";
      hc = 0x90D0FFFF;
    }
    TextStyle st = ts(F12, hc, CENTER);
    float w = R().measure(hint, st);
    gfx::rect(kBot / 2 - w / 2 - 6, 200, w + 12, 16, 0x000000B0);
    R().text(kBot / 2, 201, hint, st);
  }
  // Card being dragged, following the finger; glows when it is in the play zone.
  if (drag_.down && drag_.moved && drag_.card && !flying(drag_.card)) {
    float s = kDragS;
    if (drag_.armed) gfx::rect(drag_.x - kCardW * s / 2 - 3, drag_.y - kCardH * s / 2 - 3, kCardW * s + 6, kCardH * s + 6, 0x60D0FF90);
    drawCard(drag_.card, drag_.x - kCardW * s / 2, drag_.y - kCardH * s / 2, s, false, true, false);
  }
  if (arrow) drawArrow(false, afx, afy, atx, aty, true, arrowAlly);
  drawFlights(false);
}

void App::updateCombat(const gfx::Input& in) {
  Combat* cb = run_->combat.get();
  if (!cb) return;
  float visualDt = (float)gfx::dt() * (fastMode_ ? 1.75f : 1.f);
  clock_ += visualDt;
  animateHand(visualDt);
  for (auto& f : flights_) f.t += visualDt;
  flights_.erase(std::remove_if(flights_.begin(), flights_.end(), [](const Flight& f) { return f.t > 0.42f; }),
                 flights_.end());
  auto alive = cb->aliveEnemies();
  int n = (int)cb->hand.size();

  if (!isHandSelect(*cb) && !handSel().options.empty()) handSel() = HandSelect{};
  if (isHandSelect(*cb)) {
    // S12 hand select: tap / A toggles a card (full: the last pick is swapped out, as C#
    // SelectCardInSimpleMode), left/right or L/R walk the pickable cards, X or 确认 confirms a
    // legal count, B takes back the last pick (or answers an "up to N" choice with nothing).
    // A forced choice has no way out, as in C#.
    drag_ = {};
    aiming_ = false;
    HandSelect& hs = syncHandSelect(*cb, sel_);
    if (!cb->choice.result.waiting()) return;
    const int maxN = handSelectMax(*cb), minN = handSelectMin(*cb);
    auto ok = [&](int i) { return i >= 0 && i < n && hs.has(cb->hand[i]); };
    auto finish = [&] {
      std::vector<Card*> picked = hs.picks;
      hs = HandSelect{};
      sel_ = -1;
      widgets::setFocus(-1);
      cb->choice.result.fire(std::move(picked));
    };
    auto toggle = [&](int i) {
      Card* c = cb->hand[i];
      auto it = std::find(hs.picks.begin(), hs.picks.end(), c);
      if (it != hs.picks.end()) hs.picks.erase(it);
      else {
        if ((int)hs.picks.size() >= maxN) hs.picks.pop_back();
        hs.picks.push_back(c);
      }
      sfx::click();
    };
    auto step = [&](int d) {
      int i = sel_ < 0 ? (d > 0 ? -1 : n) : sel_;
      for (int t = 0; t < n; ++t) {
        i = ((i + d) % n + n) % n;
        if (ok(i)) { sel_ = i; return; }
      }
    };
    const int k = (int)hs.picks.size();
    const bool valid = k >= minN && k <= maxN;
    if (hs.confirm) {
      hs.confirm = false;
      if (valid) { finish(); return; }
    }
    const bool onHand = widgets::focused() < 0;
    if (onHand) {
      if (in.down & (gfx::BTN_RIGHT | gfx::BTN_R)) step(1);
      if (in.down & (gfx::BTN_LEFT | gfx::BTN_L)) step(-1);
      if ((in.down & gfx::BTN_A) && ok(sel_)) toggle(sel_);
    }
    if ((in.down & gfx::BTN_X) && valid) { finish(); return; }
    if (in.down & gfx::BTN_B) {
      if (!onHand) widgets::setFocus(-1);
      else if (!hs.picks.empty()) hs.picks.pop_back();
      else if (minN == 0) { finish(); return; }
    }
    if (in.touchDown) {
      int i = hitHandCard(in.tx, in.ty);
      if (ok(i)) { sel_ = i; toggle(i); widgets::setFocus(-1); }
      else if (i >= 0) sel_ = i;  // not pickable: shown dimmed on the top screen
    }
    return;
  }

  if (cb->choice.active) {
    drag_ = {};
    aiming_ = false;
    int m = (int)cb->choice.options.size();
    if (!cb->choice.result.waiting()) return;
    if (combatChooseOne()) {  // S13 choose-one (a potion's / card's generated offers)
      int pick = chooseOneUpdate(combatChooseOneSpec(), in);
      if (pick == -1) cb->choice.result.fire({});
      else if (pick >= 0 && pick < m) cb->choice.result.fire({cb->choice.options[pick]});
      return;
    }
    if (in.down & gfx::BTN_RIGHT) sel_ = std::min(m - 1, sel_ + 1);
    if (in.down & gfx::BTN_LEFT) sel_ = std::max(0, sel_ - 1);
    if (in.down & gfx::BTN_DOWN) sel_ = std::min(m - 1, sel_ + 5);
    if (in.down & gfx::BTN_UP) sel_ = std::max(0, sel_ - 5);
    if (sel_ >= 0) scroll_ = std::max(0, sel_ / 5 - 1);
    const bool multi = cb->choice.maxCount > 1;
    auto finish = [&](std::vector<Card*> picked) {
      deckPicks_.clear();
      sel_ = -1;
      scroll_ = 0;
      cb->choice.result.fire(std::move(picked));
    };
    auto toggle = [&](int i) {
      auto it = std::find(deckPicks_.begin(), deckPicks_.end(), i);
      if (it != deckPicks_.end()) deckPicks_.erase(it);
      else if ((int)deckPicks_.size() < cb->choice.maxCount) deckPicks_.push_back(i);
    };
    auto confirmMulti = [&] {
      if ((int)deckPicks_.size() < cb->choice.minCount) return;
      std::vector<Card*> picked;
      for (int i : deckPicks_) picked.push_back(cb->choice.options[i]);
      finish(std::move(picked));
    };
    if (multi) {
      if ((in.down & gfx::BTN_A) && sel_ >= 0 && sel_ < m) toggle(sel_);
      if (in.down & gfx::BTN_X) { confirmMulti(); return; }
    }
    bool confirm = !multi && (in.down & gfx::BTN_A) && sel_ >= 0 && sel_ < m;
    if ((in.down & gfx::BTN_B) && cb->choice.minCount == 0) { finish({}); return; }
    if (in.touchDown) {
      int id = hitAt(in.tx, in.ty);
      if (id >= ID_GRID0 && id - ID_GRID0 < m) {
        if (multi) toggle(id - ID_GRID0);
        else if (sel_ == id - ID_GRID0) confirm = true;
        sel_ = id - ID_GRID0;
      }
      if (id == ID_SKIP && cb->choice.minCount == 0) { finish({}); return; }
      if (id == ID_CONFIRM) {
        if (multi) { confirmMulti(); return; }
        if (sel_ >= 0) confirm = true;
      }
    }
    if (confirm) finish({cb->choice.options[sel_]});
    return;
  }

  if (in.touchDown) {
    int id = hitAt(in.tx, in.ty);
    CardListMode mode;
    if (id == ID_PILE_DRAW) mode = CardListMode::Draw;
    else if (id == ID_PILE_DISCARD) mode = CardListMode::Discard;
    else if (id == ID_PILE_EXHAUST) mode = CardListMode::Exhaust;
    else mode = CardListMode::Deck;
    if (id == ID_PILE_DRAW || id == ID_PILE_DISCARD || id == ID_PILE_EXHAUST) {
      openCardList(mode);
      drag_ = {};
      aiming_ = false;
      return;
    }
  }
  bool canAct = cb->playerPhase && cb->actions.waiting();
  if (sel_ >= n) sel_ = -1;
  if (!canAct) {
    drag_ = {};
    aiming_ = false;
    return;
  }
  if (target_ >= (int)alive.size()) target_ = 0;

  auto play = [&](Card* c, Creature* t, float x, float y, float s) {
    tipPlayAttempt(*cb, c);  // M13
    if (!cb->canPlay(c)) return false;
    if (c->target == TargetType::AnyEnemy && (!t || t->dead())) return false;
    PlayerAction a;
    a.kind = PlayerAction::PlayCard;
    a.card = c;
    a.target = c->target == TargetType::AnyEnemy ? t : nullptr;
    cb->actions.fire(a);
    startFlight(c, x, y, s, a.target);
    sel_ = -1;
    aiming_ = false;
    return true;
  };

  // ---- touch: drag to play (RGDSplus R4 drag-lock rules)
  if (in.touchDown && hitAt(in.tx, in.ty) == ID_POTIONS) {
    potionsOpen_ = true;
    potionAim_ = false;
    potionSel_ = -1;
    drag_ = {};
    aiming_ = false;
    sel_ = -1;
    return;
  }
  if (in.touchDown) {
    int i = hitHandCard(in.tx, in.ty);
    if (i >= 0) {
      drag_ = {};
      drag_.down = true;
      drag_.index = i;
      drag_.card = cb->hand[i];
      float cx, cy;
      if (i == sel_) {
        cx = std::clamp(handSlot(n, i).x, 70.f, kBot - 70.f);
        cy = kPreviewY + kCardH * kPreviewS / 2;
      } else {
        HandSlot h = handSlot(n, i);
        cx = h.x;
        cy = h.y;
      }
      drag_.grabDX = 0;  // the dragged card centres on the finger
      drag_.grabDY = 0;
      drag_.originY = in.ty;
      drag_.x = cx;
      drag_.y = cy;
      drag_.startTx = drag_.lastTx = in.tx;
      drag_.startTy = in.ty;
      aiming_ = false;
    } else {
      int id = hitAt(in.tx, in.ty);
      if (id == ID_END_TURN) {
        if (tipBlockEndTurn(*cb)) return;  // M13: NCanPlayCardsFtue
        cb->actions.fire({PlayerAction::EndTurn});
        sel_ = -1;
        aiming_ = false;
        return;
      }
      sel_ = -1;
      aiming_ = false;
    }
  }
  if (drag_.down && in.touching) {
    if (!drag_.moved && std::hypot(in.tx - drag_.startTx, in.ty - drag_.startTy) > kTapSlop) {
      drag_.moved = true;
      if (sel_ != drag_.index) sel_ = -1;
    }
    if (drag_.moved) {
      drag_.x = (float)in.tx;
      drag_.y = (float)in.ty;
      float lift = drag_.originY - in.ty;
      bool needsTarget = drag_.card->target == TargetType::AnyEnemy;
      if (!drag_.armed && lift >= kArm && in.ty < kPlayLine) {
        drag_.armed = true;
        drag_.accum = 0;
        drag_.lastTx = in.tx;
        if (needsTarget) {
          // Lock the enemy nearest to the card in the two-screen space.
          float vx = drag_.x + kBotOX, vy = drag_.y + kBotOY, best = 1e9f;
          drag_.target = nullptr;
          for (auto* e : alive) {
            if (!centers_.count(e)) continue;
            float d = std::hypot(centers_[e].first - vx, centers_[e].second - vy);
            if (d < best) { best = d; drag_.target = e; }
          }
        }
      } else if (drag_.armed && (lift <= 0 || in.ty > kPlayLine + 6)) {
        drag_.armed = false;  // dragged back: unlock, gesture continues
        drag_.target = nullptr;
      }
      if (drag_.armed && needsTarget && drag_.target) {
        drag_.accum += in.tx - drag_.lastTx;
        drag_.lastTx = in.tx;
        std::vector<Creature*> order = alive;
        std::sort(order.begin(), order.end(), [&](Creature* a, Creature* b) { return centers_[a].first < centers_[b].first; });
        int idx = (int)(std::find(order.begin(), order.end(), drag_.target) - order.begin());
        while (drag_.accum >= kSwitch && idx + 1 < (int)order.size()) { ++idx; drag_.accum -= kSwitch; }
        while (drag_.accum <= -kSwitch && idx > 0) { --idx; drag_.accum += kSwitch; }
        if (idx < (int)order.size()) drag_.target = order[idx];
        drag_.accum = std::clamp(drag_.accum, -kSwitch, kSwitch);
      }
    }
  }
  if (drag_.down && in.touchUp) {
    if (!drag_.moved) {
      if (sel_ == drag_.index) {
        inspectCard(cb->hand, drag_.index);  // second tap: inspect without playing (S20 popup)
      } else {
        sel_ = drag_.index;  // first tap: preview
      }
    } else {
      bool edge = in.tx <= 2 || in.ty <= 2 || in.tx >= kBot - 3 || in.ty >= kH - 3;
      if (drag_.armed && !edge) play(drag_.card, drag_.target, drag_.x, drag_.y, kDragS);
    }
    drag_ = {};
  }
  if (drag_.down && (in.down & gfx::BTN_B)) drag_ = {};

  // ---- buttons: L/R or ←→ choose a card, A to aim/play, ←→ choose an enemy, A confirm, B back.
  if (drag_.down) return;
  Card* selCard = sel_ >= 0 ? cb->hand[sel_] : nullptr;
  if (in.down & gfx::BTN_Y) { openCardList(CardListMode::Deck); aiming_ = false; return; }
  if ((in.down & gfx::BTN_X) && tipBlockEndTurn(*cb)) return;  // M13: NCanPlayCardsFtue
  if (in.down & gfx::BTN_X) { cb->actions.fire({PlayerAction::EndTurn}); sel_ = -1; aiming_ = false; return; }
  if (in.down & gfx::BTN_B) {
    if (aiming_) aiming_ = false;
    else sel_ = -1;
  }
  if (in.down & gfx::BTN_R) { sel_ = n ? (sel_ + 1) % n : -1; aiming_ = false; }
  if (in.down & gfx::BTN_L) { sel_ = n ? (sel_ <= 0 ? n - 1 : sel_ - 1) : -1; aiming_ = false; }
  if (in.down & (gfx::BTN_LEFT | gfx::BTN_RIGHT)) {
    int d = (in.down & gfx::BTN_RIGHT) ? 1 : -1;
    if (aiming_ && !alive.empty()) target_ = std::clamp(target_ + d, 0, (int)alive.size() - 1);
    else if (n) sel_ = sel_ < 0 ? 0 : (sel_ + d + n) % n;
  }
  if (in.down & gfx::BTN_A) {
    if (!selCard) {
      sel_ = n ? 0 : -1;
    } else if (selCard->target == TargetType::AnyEnemy && !aiming_) {
      if (cb->canPlay(selCard) && !alive.empty()) aiming_ = true;
    } else {
      float cx = std::clamp(handSlot(n, sel_).x, 70.f, kBot - 70.f);
      play(selCard, alive.empty() ? nullptr : alive[target_], cx, kPreviewY + kCardH * kPreviewS / 2, kPreviewS);
    }
  }
}

}  // namespace ui
