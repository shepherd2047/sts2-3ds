// Split from ui.cpp (F3).
#include "ui_common.h"

namespace ui {

void App::drawCard(Card* c, float x, float y, float s, bool dim, bool desc, bool selected) {
  const char* kind = c->type == CardType::Attack ? "attack" : c->type == CardType::Power ? "power" : "skill";
  bool status = c->type == CardType::Status || c->type == CardType::Curse;
  uint32_t tint = dim ? 0x000000FF : 0xFFFFFFFF;
  float blend = dim ? 0.45f : 0.f;
  if (selected) gfx::rect(x - 3 * s - 1, y - 3 * s - 1, 126 * s + 2, 175 * s + 2, 0xFFE070C0);
  spr(R().sprite("portrait/" + c->locKey), x + 8 * s, y + 16 * s, 104 * s, 78 * s, tint, blend);
  Sprite frame = R().sprite(std::string("card/frame_") + kind);
  spr(frame, x, y, 120 * s, 169 * s, status ? 0x606060FF : tint, status ? 0.5f : blend);
  spr(R().sprite(std::string("card/border_") + kind), x - 3 * s, y + 4 * s, 126 * s, 96 * s, tint, blend);
  Sprite banner = R().sprite("card/banner");
  spr(banner, x - 3 * s, y + 3 * s, 126 * s, 28 * s, tint, blend);

  // Title, shrunk to fit the banner.
  std::string title = cardTitle(c);
  FontSize f = s >= 0.8f ? F16 : F12;
  TextStyle tt = ts(f, c->upgraded() ? col::green : col::white, CENTER);
  float tw = R().measure(title, tt);
  float maxW = 104 * s;
  if (tw > maxW) tt.scale = maxW / tw;
  // The title must never reach the art: its line box ends above the portrait's top edge
  // and any extra height grows upward, past the card's top edge if need be.
  const float maxH = 23 * s, artTop = y + 17 * s;
  float lh = R().lineHeight(f);
  if (lh * tt.scale > maxH) tt.scale = maxH / lh;
  float th = lh * tt.scale;
  R().text(x + 60 * s, artTop - th, title, tt);

  // Cost orb.
  if ((c->cost >= 0 || c->costsX) && !c->has(kwUnplayable)) {
    float os = 30 * std::max(s, 0.6f);
    spr(R().sprite("card/energy"), x - 7 * s, y - 7 * s, os, os, tint, blend);
    bool live = c->combat && c->combat->inProgress;
    int shownCost = live ? c->combat->energyCost(c) : c->cost;
    uint32_t cc = shownCost < c->canonicalCost ? col::green : shownCost > c->canonicalCost ? col::red : col::white;
    TextStyle ct = ts(F16, cc, CENTER);
    ct.scale = std::max(s, 0.6f) * 1.1f;
    R().text(x - 7 * s + os / 2, y - 7 * s + (os - R().lineHeight(F16) * ct.scale) / 2, c->costsX ? std::string("X") : num(shownCost), ct);
  }

  if (desc) {
    TextStyle dt = ts(F12, dim ? col::gray : col::white, CENTER, 104 * s);
    dt.scale = s >= 0.95f ? 1.f : std::max(0.75f, s);
    dt.maxWidth = 104 * s;
    float dh;
    std::string d = describe(c);
    R().measure(d, dt, &dh);
    float top = y + 100 * s, bottom = y + 164 * s;
    // Long texts shrink until they fit the text box.
    for (int k = 0; k < 6 && dh > bottom - top && dt.scale > 0.5f; ++k) {
      dt.scale *= 0.9f;
      R().measure(d, dt, &dh);
    }
    // A lone wrapped character (usually the full stop) reads badly on small cards: shrink to one line less.
    if (s < 0.8f) {
      float lh = R().lineHeight(F12) * dt.scale, dh1;
      TextStyle t2 = dt;
      for (int k = 0; k < 3; ++k) {
        t2.scale *= 0.92f;
        R().measure(d, t2, &dh1);
        if (dh1 < dh - lh * 0.5f) { dt = t2; dh = dh1; break; }
      }
    }
    R().text(x + 60 * s, top + std::max(0.f, (bottom - top - dh) / 2), d, dt);
  }
}

void App::drawCardGrid(const std::vector<Card*>& cards, int sel, float y0, float y1, int scrollRow) {
  const float s = 0.46f, cw = 120 * s, ch = 169 * s;
  const int perRow = 5;
  float gap = (kBot - perRow * cw) / (perRow + 1);
  for (int i = 0; i < (int)cards.size(); ++i) {
    int row = i / perRow - scrollRow, colI = i % perRow;
    float x = gap + colI * (cw + gap);
    float y = y0 + 6 + row * (ch + 8);
    if (y + ch < y0 || y > y1) continue;
    drawCard(cards[i], x, y, s, false, false, i == sel);
    hits_.push_back({x, y, cw, ch, ID_GRID0 + i});
  }
}

int App::gridHit(const std::vector<Card*>&, float, float, int, int tx, int ty) {
  int id = hitAt(tx, ty);
  return id >= ID_GRID0 ? id - ID_GRID0 : -1;
}

}  // namespace ui
