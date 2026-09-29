// Split from ui.cpp (F3).
#include "../ui_common.h"
#include "combat_internal.h"

namespace ui {

App::HandSlot App::handSlot(int n, int i) const {
  n = std::clamp(n, 1, 10);
  i = std::clamp(i, 0, n - 1);
  // Native units -> bottom-screen pixels. Like RGDSplus, small hands are drawn larger.
  float k = n <= 5 ? 0.25f : n <= 7 ? 0.23f : 0.215f;
  float mult = n == 8 ? 0.95f : n == 9 ? 0.9f : n == 10 ? 0.85f : 1.f;  // HandPosHelper.GetScale
  HandSlot h;
  h.x = kBot / 2.f + kHandPos[n - 1][i][0] * k;
  h.y = kHandY + kHandPos[n - 1][i][1] * k;
  h.angle = kHandAngle[n - 1][i] * 3.14159265f / 180.f;
  h.s = k * 2.f * mult;
  return h;
}

// Topmost hand card under a bottom-screen point: the preview first, then the fan right to left.
int App::hitHandCard(float tx, float ty) {
  Combat* cb = run_->combat.get();
  int n = (int)cb->hand.size();
  if (sel_ >= 0 && sel_ < n && !drag_.down) {
    float cx = std::clamp(handSlot(n, sel_).x, 70.f, kBot - 70.f), cy = kPreviewY + kCardH * kPreviewS / 2;
    if (std::fabs(tx - cx) < kCardW * kPreviewS / 2 && std::fabs(ty - cy) < kCardH * kPreviewS / 2) return sel_;
  }
  for (int i = n - 1; i >= 0; --i) {
    HandSlot h = handSlot(n, i);
    float dx = tx - h.x, dy = ty - h.y;
    float c = std::cos(-h.angle), s = std::sin(-h.angle);
    float lx = dx * c - dy * s, ly = dx * s + dy * c;
    if (std::fabs(lx) < kCardW * h.s / 2 && std::fabs(ly) < kCardH * h.s / 2) return i;
  }
  return -1;
}

// NTargetingArrow.UpdateArrowPosition / UpdateSegments, in virtual coordinates.
void App::drawArrow(bool top, float fx, float fy, float tx, float ty, bool locked, bool ally) {
  const float k = 240.f / 1080.f;  // StS2 1080p units -> top-screen pixels
  auto rot = [](float x, float y, float r, float& ox, float& oy) {
    ox = x * std::cos(r) - y * std::sin(r);
    oy = x * std::sin(r) + y * std::cos(r);
  };
  float hx, hy, ex, ey;
  rot(0, 88 * k, arrowRot_, hx, hy);
  hx += tx; hy += ty;
  rot(0, 40 * k, arrowRot_, ex, ey);
  ex += tx; ey += ty;
  float cx = fx - (hx - fx) * 0.25f;
  float cy = hy + (hy - fy) * 0.5f;  // From.Y > 540: the card is on the lower screen
  if (top) arrowRot_ = std::atan2(ty - cy, tx - cx) + 3.14159265f / 2;

  uint32_t tint = !locked ? 0xFFFFFFFF : ally ? 0x36C78AFF : 0xE61E1BFF;
  Sprite seg = R().sprite("ui/arrow_segment"), head = R().sprite("ui/arrow_head");
  float px[19], py[19];
  for (int i = 0; i < 19; ++i) {
    float t = i / 20.f, u = 1 - t;
    px[i] = u * u * fx + 2 * u * t * cx + t * t * ex;
    py[i] = u * u * fy + 2 * u * t * cy + t * t * ey;
  }
  auto drawRotated = [&](const Sprite& sp, float x, float y, float r, float sc) {
    toLocal(top, x, y);
    float w = sp.w * sc, h = sp.h * sc;
    float lim = top ? kTop : kBot;
    if (x < -w || x > lim + w || y < -h || y > kH + h) return;
    gfx::pushTransform(gfx::Affine::rotateAround(x, y, r));
    spr(sp, x - w / 2, y - h / 2, w, h, tint, 1.f);
    gfx::popTransform();
  };
  for (int i = 0; i < 19; ++i) {
    float sc = 0.28f + (0.42f - 0.28f) * (i * 2.f / 19.f);  // Mathf.Lerp, unclamped
    float r = i == 0 ? std::atan2(py[0] - py[1], px[0] - px[1]) - 3.14159265f / 2
                     : std::atan2(py[i] - py[i - 1], px[i] - px[i - 1]) + 3.14159265f / 2;
    drawRotated(seg, px[i], py[i], r, sc);
  }
  drawRotated(head, hx, hy, arrowRot_, locked ? 1.05f : 0.95f);
}

void App::startFlight(Card* c, float x, float y, float s, Creature* target) {
  Combat* cb = run_->combat.get();
  float x1 = kTop / 2.f, y1 = 110.f;  // battlefield centre for untargeted cards
  if (c->target == TargetType::Self) target = cb->player;
  if (target && centers_.count(target)) {
    x1 = centers_[target].first;
    y1 = centers_[target].second;
  }
  flights_.push_back({c, 0.f, x + kBotOX, y + kBotOY, x1, y1, s});
}

void App::drawFlights(bool top) {
  for (auto& f : flights_) {
    float t = std::min(1.f, f.t / 0.42f);
    float x = f.x0 + (f.x1 - f.x0) * t, y = f.y0 + (f.y1 - f.y0) * t;
    float s = f.s0 * (1.f - 0.94f * t);  // shrink to 6%
    toLocal(top, x, y);
    drawCard(f.card, x - kCardW * s / 2, y - kCardH * s / 2, s);
  }
}

// StS-style hand motion: drawn cards arc out of the draw pile one by one and
// grow into the fan; everything else eases to its slot; cards that leave the
// hand unplayed fly to their pile.
void App::animateHand(float dt) {
  Combat* cb = run_->combat.get();
  int n = (int)cb->hand.size();
  drawQueue_ = std::max(0.f, drawQueue_ - dt);
  leaveQueue_ = std::max(0.f, leaveQueue_ - dt);
  auto inPile = [](const std::vector<Card*>& v, Card* c) { return std::find(v.begin(), v.end(), c) != v.end(); };

  // Cards that left the hand.
  for (auto it = poses_.begin(); it != poses_.end();) {
    Card* c = it->first;
    if (inPile(cb->hand, c)) { ++it; continue; }
    bool flying = false;
    for (auto& f : flights_) flying |= f.card == c;
    Pose from = it->second;
    it = poses_.erase(it);
    if (flying || from.delay > 0) continue;
    Ghost g{c, from, 0, leaveQueue_, 0, 0, 0.1f, false};
    if (inPile(cb->discard, c)) { g.tx = kDiscardX; g.ty = kDiscardY; }
    else if (inPile(cb->draw, c)) { g.tx = kDrawPileX; g.ty = kDrawPileY; }
    else if (inPile(cb->exhaust, c)) { g.exhaust = true; g.tx = from.x; g.ty = from.y - 40; g.ts = from.s * 0.2f; }
    else continue;
    if (leaveQueue_ <= 0) sfx::cardsDiscarded();
    leaveQueue_ += kLeaveGap;
    ghosts_.push_back(g);
  }
  for (auto& g : ghosts_) {
    if (g.delay > 0) g.delay -= dt;
    else g.t += dt / kLeaveTime;
  }
  ghosts_.erase(std::remove_if(ghosts_.begin(), ghosts_.end(), [](const Ghost& g) { return g.t >= 1; }), ghosts_.end());

  // Cards in the hand.
  for (int i = 0; i < n; ++i) {
    Card* c = cb->hand[i];
    HandSlot h = handSlot(n, i);
    auto it = poses_.find(c);
    if (it == poses_.end()) {
      Pose p;
      p.x0 = p.x = kDrawPileX; p.y0 = p.y = kDrawPileY; p.a0 = p.angle = -0.9f; p.s0 = p.s = 0.12f;
      p.drawT = 0;
      p.delay = drawQueue_;
      drawQueue_ += kDrawGap;
      it = poses_.emplace(c, p).first;
    }
    Pose& p = it->second;
    // Held or previewed cards track where they are shown, so they glide back when let go.
    if (drag_.down && drag_.moved && drag_.card == c) {
      p.x = drag_.x; p.y = drag_.y; p.angle = 0; p.s = kDragS; p.delay = 0; p.drawT = 1;
      continue;
    }
    if (i == sel_ && !drag_.down && p.delay <= 0) {
      p.x = std::clamp(h.x, 70.f, kBot - 70.f); p.y = kPreviewY + kCardH * kPreviewS / 2;
      p.angle = 0; p.s = kPreviewS; p.drawT = 1;
      continue;
    }
    if (p.delay > 0) { p.delay -= dt; continue; }
    if (p.drawT < 1) {
      if (p.drawT == 0) sfx::cardDeal();
      // Arc from the pile towards the (moving) slot.
      p.drawT = std::min(1.f, p.drawT + dt / kDrawTime);
      float t = easeOut(p.drawT);
      float cx = (p.x0 + h.x) / 2, cy = std::min(p.y0, h.y) - 45;
      float u = 1 - t;
      p.x = u * u * p.x0 + 2 * u * t * cx + t * t * h.x;
      p.y = u * u * p.y0 + 2 * u * t * cy + t * t * h.y;
      p.angle = p.a0 + (h.angle - p.a0) * t;
      p.s = p.s0 + (h.s - p.s0) * t;
      continue;
    }
    p.x = approach(p.x, h.x, 16, dt);
    p.y = approach(p.y, h.y, 16, dt);
    p.angle = approach(p.angle, h.angle, 16, dt);
    p.s = approach(p.s, h.s, 16, dt);
  }
}

void App::drawGhosts() {
  for (auto& g : ghosts_) {
    if (g.delay > 0) continue;
    float t = g.exhaust ? easeOut(g.t) : easeIn(g.t);
    float x = g.from.x + (g.tx - g.from.x) * t, y = g.from.y + (g.ty - g.from.y) * t;
    if (!g.exhaust) y -= std::sin(g.t * 3.14159265f) * 30;  // small hop towards the pile
    float s = g.from.s + (g.ts - g.from.s) * t;
    float a = g.from.angle + (g.exhaust ? 0.f : 0.8f) * t;
    gfx::pushTransform(gfx::Affine::rotateAround(x, y, a));
    drawCard(g.card, x - kCardW * s / 2, y - kCardH * s / 2, s, g.exhaust, false, false);
    gfx::popTransform();
  }
}

}  // namespace ui
