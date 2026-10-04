// Split from ui.cpp (F3).
#include "../ui_common.h"
#include "combat_internal.h"
#include "../card_fly.h"

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
  const bool selecting = isHandSelect(*cb);  // S12: no preview, picked cards stand raised
  if (!selecting && sel_ >= 0 && sel_ < n && !drag_.down) {
    float cx = std::clamp(handSlot(n, sel_).x, 70.f, kBot - 70.f), cy = kPreviewY + kCardH * kPreviewS / 2;
    if (std::fabs(tx - cx) < kCardW * kPreviewS / 2 && std::fabs(ty - cy) < kCardH * kPreviewS / 2) return sel_;
  }
  for (int i = n - 1; i >= 0; --i) {
    HandSlot h = handSlot(n, i);
    if (selecting) h.y += handSelectLift(*cb, i, sel_);
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

namespace {
// ANIM_DIFF C1: the play spot. The original moves a played card in 0.25 s (ease-out cubic) to the play container's
// centre, 100 units up, at 0.8 scale, and leaves it there until the play has resolved. Here: the centre of the top
// screen, large enough to read.
constexpr float kPlayX = kTop / 2.f, kPlayY = 112.f, kPlayS = 0.8f, kPlayMove = 0.25f;
constexpr float kMinHold = 0.3f;  // a card that resolves at once still reaches the spot
constexpr float kFlightTime = 0.42f;
float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// Where a held card is shown (virtual coordinates).
template <class F>
void holdPose(const F& f, float& x, float& y, float& s, float& a) {
  const float u = easeOut(std::min(1.f, f.t / kPlayMove));
  x = lerpf(f.x0, kPlayX, u);
  y = lerpf(f.y0, kPlayY, u);
  s = lerpf(f.s0, kPlayS, u);
  a = f.a0 * (1 - u);
}
}  // namespace

void App::startFlight(Card* c, float x, float y, float s, Creature* target) {
  Combat* cb = run_->combat.get();
  float x1 = kTop / 2.f, y1 = 110.f;  // battlefield centre for untargeted cards
  if (c->target == TargetType::Self) target = cb->player;
  if (target && centers_.count(target)) {
    x1 = centers_[target].first;
    y1 = centers_[target].second;
  }
  for (auto it = flights_.begin(); it != flights_.end();) it = it->card == c ? flights_.erase(it) : it + 1;
  Flight f{c, 0.f, x + kBotOX, y + kBotOY, x1, y1, s};
  f.target = target;
  flights_.push_back(f);
}

bool App::cardInFlight(const Card* c) const {
  for (auto& f : flights_)
    if (f.card == c) return true;
  return cardfly::flying(c);
}

void App::updateFlights(float dt) {
  Combat* cb = run_->combat.get();
  cardfly::update(dt);
  if (!cb) return;
  const float discX = kDiscardX + kBotOX, discY = kDiscardY + kBotOY;
  const float drawX = kDrawPileX + kBotOX, drawY = kDrawPileY + kBotOY;
  // Cards played without a release from the hand (auto-play from the draw pile) come up from the draw pile.
  for (Card* c : cb->play) {
    bool known = false;
    for (auto& f : flights_) known |= f.card == c;
    if (known || poses_.count(c)) continue;
    Flight f{c, 0.f, drawX, drawY, kTop / 2.f, 110.f, 0.12f};
    f.a0 = -0.9f;
    flights_.push_back(f);
  }
  for (auto it = flights_.begin(); it != flights_.end();) {
    Flight& f = *it;
    f.t += dt;
    if (f.hold) {
      const Pile p = cb->pileOf(f.card);
      if (p == Pile::Play || f.t < kMinHold) { ++it; continue; }
      float x, y, s, a;
      holdPose(f, x, y, s, a);
      if (p == Pile::Discard || p == Pile::Draw) {  // C2: NCardFlyVfx to the pile
        cardfly::launch(f.card, x, y, s, a, p == Pile::Discard ? discX : drawX, p == Pile::Discard ? discY : drawY);
        it = flights_.erase(it);
        continue;
      }
      if (p == Pile::Hand) { it = flights_.erase(it); continue; }
      // Exhaust, powers and anything else: the short flight to the target, now from the play spot.
      f.hold = false;
      f.t = 0;
      f.x0 = x;
      f.y0 = y;
      f.s0 = s;
      ++it;
      continue;
    }
    if (f.t > kFlightTime) it = flights_.erase(it);
    else ++it;
  }
  // C13: cards that left the discard pile for the draw pile (a shuffle) fly over as fire comets, staggered like
  // CardPileCmd.Shuffle (min(0.045, 0.8 / n) apart, +-55 % random).
  if (cb->discard.size() < lastDiscard_.size()) {
    int n = 0;
    for (Card* c : lastDiscard_) n += cb->pileOf(c) == Pile::Draw;
    float delay = 0;
    const float gap = n > 0 ? std::min(0.045f, 0.8f / n) : 0.f;
    for (int i = 0; i < n; ++i) {
      cardfly::launch(nullptr, discX, discY, 0.3f, 0.f, drawX, drawY, delay, true);
      const float jitter = (float)((i * 7919 + 13) % 101) / 100.f - 0.5f;  // deterministic, -0.5..0.5
      delay += gap + jitter * 1.11f * gap;
    }
  }
  lastDiscard_ = cb->discard;
}

void App::drawFlights(bool top) {
  Combat* cb = run_->combat.get();
  // Comets: the trail behind the card (show_behind_parent), then the card itself shrinking and darkening.
  cardfly::drawTrails(top);
  cardfly::Body bodies[cardfly::kMaxComets];
  const int nb = cardfly::bodies(bodies, cardfly::kMaxComets);
  for (int i = 0; i < nb; ++i) {
    const cardfly::Body& b = bodies[i];
    float x = b.x, y = b.y;
    toLocal(top, x, y);
    const float w = kCardW * b.s, h = kCardH * b.s;
    if (x < -w || x > (top ? kTop : kBot) + w || y < -h || y > kH + h) continue;
    gfx::pushAlpha(b.alpha);
    gfx::pushTransform(gfx::Affine::rotateAround(x, y, b.rot));
    if (b.waiting)
      if (Sprite gs = R().sprite("card/glow_cyan")) {  // C10: the hand flashes cyan before it is flushed
        const float gw = 302.7f * b.s * 1.08f;
        spr(gs, x - gw / 2, y - gw / 2, gw, gw, 0xFFFFFFFFu);
      }
    drawCard(b.card, x - w / 2, y - h / 2, b.s, false, b.s > 0.3f);  // the text while it is still readable
    gfx::popTransform();
    cardfly::shade(x, y, w, h, b.rot, b.dark);
    gfx::popAlpha();
  }
  const bool covered = cb && (cb->choice.active || isHandSelect(*cb));
  for (auto& f : flights_) {
    if (f.hold) {
      // C1: the card at the play spot, full size, with its playable glow, while it resolves.
      if (covered) continue;
      float x, y, s, a;
      holdPose(f, x, y, s, a);
      toLocal(top, x, y);
      const float w = kCardW * s, h = kCardH * s;
      if (x < -w || x > (top ? kTop : kBot) + w || y < -h || y > kH + h) continue;
      gfx::pushTransform(gfx::Affine::rotateAround(x, y, a));
      if (Sprite gs = R().sprite("card/glow_cyan")) {
        const float gw = 302.7f * s;
        spr(gs, x - gw / 2, y - gw / 2, gw, gw, 0xFFFFFFFAu);
      }
      drawCard(f.card, x - w / 2, y - h / 2, s, false, true, false);
      gfx::popTransform();
      continue;
    }
    // NCardFlyVfx-like exit for exhausted cards / powers (their own effects are C3 / C4): a straight run, the card
    // turning its top towards where it is heading (rotation eased at 12 / s), the body shrinking to 10% in the first
    // third and to nothing over the rest.
    float t = std::min(1.f, f.t / kFlightTime);
    float x = f.x0 + (f.x1 - f.x0) * t, y = f.y0 + (f.y1 - f.y0) * t;
    const float u = std::clamp((t - 1.f / 3) / (2.f / 3), 0.f, 1.f);
    float s = f.s0 * (t < 1.f / 3 ? 1.f - 0.9f * (3.f * t) : std::max(0.f, 0.1f - 0.25f * u));
    const float ang = std::atan2(f.y1 - f.y0, f.x1 - f.x0) + 3.14159265f / 2;
    const float rot = std::remainder(ang, 6.2831853f) * (1.f - std::exp(-12.f * f.t));
    toLocal(top, x, y);
    gfx::pushTransform(gfx::Affine::rotateAround(x, y, rot));
    drawCard(f.card, x - kCardW * s / 2, y - kCardH * s / 2, s);
    gfx::popTransform();
  }
}

// StS-style hand motion: drawn cards arc out of the draw pile one by one and
// grow into the fan; everything else eases to its slot; cards that leave the
// hand unplayed fly to their pile.
void App::animateHand(float dt) {
  Combat* cb = run_->combat.get();
  int n = (int)cb->hand.size();
  const bool selecting = isHandSelect(*cb);
  drawQueue_ = std::max(0.f, drawQueue_ - dt);
  leaveQueue_ = std::max(0.f, leaveQueue_ - dt);
  auto inPile = [](const std::vector<Card*>& v, Card* c) { return std::find(v.begin(), v.end(), c) != v.end(); };

  // Cards that left the hand.
  bool swooshed = false;
  for (auto it = poses_.begin(); it != poses_.end();) {
    Card* c = it->first;
    if (inPile(cb->hand, c)) { ++it; continue; }
    bool flying = false;
    for (auto& f : flights_) flying |= f.card == c;
    Pose from = it->second;
    it = poses_.erase(it);
    if (flying || from.delay > 0) continue;
    if (inPile(cb->discard, c)) {
      // C10 (and any discard from the hand): every card leaves at once as a fire comet (NCardFlyVfx); the random
      // speeds and durations spread them out, so the hand never blinks empty.
      const float wait = cb->phase == Combat::TurnPhase::End ? 0.15f : 0.f;  // end of turn: the cyan flash first
      cardfly::launch(c, from.x + kBotOX, from.y + kBotOY, from.s, from.angle, kDiscardX + kBotOX, kDiscardY + kBotOY,
                      wait);
      if (!swooshed) sfx::cardsDiscarded();
      swooshed = true;
      continue;
    }
    Ghost g{c, from, 0, leaveQueue_, 0, 0, 0.1f, false};
    if (inPile(cb->draw, c)) { g.tx = kDrawPileX; g.ty = kDrawPileY; }
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
    if (!selecting && i == sel_ && !drag_.down && p.delay <= 0) {
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
    p.y = approach(p.y, h.y + (selecting ? handSelectLift(*cb, i, sel_) : 0.f), 16, dt);
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
