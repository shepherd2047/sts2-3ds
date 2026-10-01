// A7d: the CrystalSphere minigame screen (NCrystalSphereScreen), shown in place of the event page
// while the event's minigame exists (core/events_crystal.h).
//
// Top screen (information): the minigame room, the fortune teller's line in a speech bubble,
// "还剩下N次占卜。" bottom-left (DivinationsLeft) and the "如何占卜" instructions on the right
// (RightUi/Instructions); after the last divination only the closing line stays.
// Bottom screen (controls): the 11x11 grid inside the sphere at the game's cell size ratio
// (20 px), fogged cells from the ScryMask look, items showing through cleared cells; on the
// right the 大幅占卜 / 小幅占卜 tool buttons (CrystalSphereToolType) and, when done, 继续.
// Input: D-pad moves the cursor (the tool's area is highlighted, as on hover), A divines; L picks
// the small tool, R the big one (the C# hotkeys are the draw / discard pile keys), Y toggles.
// Touch: tap a cell to aim, tap it again to divine; tap the tool buttons.
#include "../../core/events_crystal.h"
#include "../ui_common.h"

namespace ui {

namespace {
using CS = CrystalSphereGame;
constexpr float kCell = 20.f, kGridX = 6.f, kGridY = 10.f;  // tools/build_assets.py CS_CELL / CS_GRID
constexpr int kN = CS::kSize;
constexpr float kPanelX = 236.f, kPanelW = 80.f;
constexpr float kBtnW = 78.f, kBtnH = 31.f, kBigY = 58.f, kSmallY = 96.f;
constexpr int ID_CS_BIG = ID_DEVITEM0 + 1, ID_CS_SMALL = ID_DEVITEM0 + 2, ID_CS_PROCEED = ID_DEVITEM0 + 3;
constexpr int ID_CS_CELL0 = ID_GRID0;  // + y * 11 + x

const char* itemSprite(const CS::Item& it) {
  switch (it.type) {
    case CS::ItemType::Relic: return "crystal/relic";
    case CS::ItemType::Curse: return "crystal/curse";
    case CS::ItemType::Gold: return it.bigGold ? "crystal/big_gold" : "crystal/gold";
    case CS::ItemType::Potion: return it.potionRarity == PotionRarity::Rare ? "crystal/potion_rare" : "crystal/potion_common";
    case CS::ItemType::CardReward:
      return it.cardRarity == Rarity::Rare ? "crystal/card_rare" : it.cardRarity == Rarity::Uncommon ? "crystal/card_uncommon" : "crystal/card_common";
  }
  return "crystal/gold";
}

// One cell (i, j) of a sprite that spans cw x ch cells, drawn at (x, y).
void spriteCell(const Sprite& s, int cw, int ch, int i, int j, float x, float y) {
  if (!s) return;
  float sw = s.w / cw, sh = s.h / ch;
  gfx::image(s.tex, s.x + i * sw, s.y + j * sh, sw, sh, x, y, kCell, kCell);
}

void outline(float x, float y, float w, float h, uint32_t c, float t = 1.f) {
  gfx::rect(x, y, w, t, c);
  gfx::rect(x, y + h - t, w, t, c);
  gfx::rect(x, y, t, h, c);
  gfx::rect(x + w - t, y, t, h, c);
}
}  // namespace

void App::drawCrystalSphere(bool top) {
  CS& g = *crystalSphereGame(*run_);
  Event* e = run_->currentEvent.get();
  gfx::Texture* bg = R().texture("gfx/bg_crystal_sphere.t3t");
  bool playing = g.phase == CS::Phase::Playing;
  if (top) {
    if (bg) gfx::image(bg, 0, 0, kTop, kH, 0, 0, kTop, kH);
    drawTopBar();
    R().text(8, 22, L("events." + e->locKey + ".title"), ts(F16, col::gold, LEFT));
    // NCrystalSphereDialogue: the fortune teller's line, over the sphere.
    if (!g.banter.empty() && R().hasLoc("events." + g.banter)) {
      std::string line = L("events." + g.banter);
      TextStyle st = ts(F12, 0x3A2A18FF, CENTER, 150);
      float th;
      float tw = std::min(150.f, R().measure(line, st, &th));
      float bw = tw + 20, bh = th + 12, bx = 138 - bw / 2, by = 44;
      widgets::panel("ui/nine_dialogue", bx, by, bw, bh, 0xF0E6D0FF);
      R().text(138, by + 6, line, st);
    }
    if (playing) {
      // RightUi/Instructions: title + description, shrunk to fit.
      const float ix = 262, iy = 40, iw = 134, ih = 194;
      panel(ix, iy, iw, ih, 0x140E38E0, 0x8A8EF0FF);
      R().text(ix + iw / 2, iy + 6, L("events.CRYSTAL_SPHERE.minigame.instructions.title"), ts(F16, col::gold, CENTER));
      std::string desc = L("events.CRYSTAL_SPHERE.minigame.instructions.description");
      TextStyle dt = ts(F12, col::white, LEFT, iw - 12, 0.85f);
      float dh;
      R().measure(desc, dt, &dh);
      for (int k = 0; k < 8 && dh > ih - 30 && dt.scale > 0.55f; ++k) { dt.scale *= 0.92f; R().measure(desc, dt, &dh); }
      R().text(ix + 6, iy + 26, desc, dt);
      // DivinationsLeft, bottom-left.
      std::vector<DynVar> vars = {{"Count", Dec(g.divinations), Dec(g.divinations)}};
      std::string left = expandSmart(L("events.CRYSTAL_SPHERE.minigame.divinationsRemain"), vars, false);
      gfx::rect(0, kH - 30, 250, 30, 0x00000090);
      R().text(10, kH - 25, left, ts(F16, col::white, LEFT));
    } else if (R().hasLoc("events." + e->descKey)) {
      gfx::rect(0, kH - 30, kTop, 30, 0x00000090);
      R().text(kTop / 2, kH - 25, L("events." + e->descKey), ts(F16, col::white, CENTER));
    }
    return;
  }

  // ---- bottom: the sphere and its grid
  if (bg) gfx::image(bg, 0, 256, kBot, kH, 0, 0, kBot, kH);
  for (auto& it : g.items) {  // items show through the cleared cells
    if (!it.placed) continue;  // PORT NOTE (n/a: visual): an item that did not fit is not drawn (C#: at cell (0, 0))
    Sprite s = R().sprite(itemSprite(it));
    for (int i = 0; i < it.w; ++i)
      for (int j = 0; j < it.h; ++j)
        if (!g.hidden[it.x + i][it.y + j])
          spriteCell(s, it.w, it.h, i, j, kGridX + (it.x + i) * kCell, kGridY + (it.y + j) * kCell);
  }
  Sprite fog = R().sprite("crystal/fog");
  for (int x = 0; x < kN; ++x)
    for (int y = 0; y < kN; ++y) {
      float cx = kGridX + x * kCell, cy = kGridY + y * kCell;
      if (g.hidden[x][y]) {
        if (fog) spriteCell(fog, kN, kN, x, y, cx, cy);
        else gfx::rect(cx, cy, kCell, kCell, 0x5A5CE0F0);
      }
      if (playing && g.hidden[x][y]) hits_.push_back({cx, cy, kCell, kCell, ID_CS_CELL0 + y * kN + x});
    }
  for (auto& it : g.items)  // a revealed item: its outline (gold for loot, red for the curse)
    if (it.placed && it.revealed)
      outline(kGridX + it.x * kCell, kGridY + it.y * kCell, it.w * kCell, it.h * kCell,
              it.isGood() ? 0xFFD870C0 : 0xE04040D0);
  if (playing && crystalCursor_ >= 0 && crystalCursor_ < kN * kN) {
    int x = crystalCursor_ % kN, y = crystalCursor_ / kN;
    Sprite hl = R().sprite("crystal/highlight");
    for (auto [hx, hy] : CS::toolCells(g.tool, x, y))
      if (g.hidden[hx][hy]) spr(hl, kGridX + hx * kCell, kGridY + hy * kCell, kCell, kCell);
    float cx = kGridX + x * kCell, cy = kGridY + y * kCell;
    gfx::rect(cx, cy, kCell, kCell, 0xFFFFFF50);  // HoveredFg
    outline(cx - 1, cy - 1, kCell + 2, kCell + 2, 0xFFFFFFFF, 2);
  }

  // ---- right: tools / proceed
  gfx::rect(kPanelX - 2, 0, kBot - kPanelX + 2, kH, 0x0A0620B0);
  if (playing) {
    std::vector<DynVar> vars = {{"Count", Dec(g.divinations), Dec(g.divinations)}};
    std::string left = expandSmart(L("events.CRYSTAL_SPHERE.minigame.divinationsRemain"), vars, false);
    TextStyle lt = ts(F12, col::white, CENTER);
    float lw = R().measure(left, lt);
    if (lw > kPanelW - 4) lt.scale = (kPanelW - 4) / lw;
    R().text(kPanelX + kPanelW / 2, 24, left, lt);
    auto toolButton = [&](float y, CS::Tool t, const char* icon, const char* label, int id) {
      bool on = g.tool == t;
      float x = kPanelX + (kPanelW - kBtnW) / 2;
      Sprite b = R().sprite("crystal/button");
      if (b) spr(b, x, y, kBtnW, kBtnH, on ? 0xFFFFFFFF : 0x9090A0FF);
      else panel(x, y, kBtnW, kBtnH, 0x2A6A78F0, 0x9AD0E0FF);
      if (on) {
        Sprite o = R().sprite("crystal/button_outline");
        if (o) spr(o, x, y, kBtnW, kBtnH, 0xFFD870FF);
        else outline(x, y, kBtnW, kBtnH, 0xFFD870FF);
      }
      spr(R().sprite(icon), x + 3, y + (kBtnH - 22) / 2, 22, 22);
      R().text(x + 27, y + (kBtnH - R().lineHeight(F12)) / 2, L(std::string("events.CRYSTAL_SPHERE.button.") + label),
               ts(F12, on ? col::gold : col::white, LEFT, kBtnW - 28, 0.85f));
      hits_.push_back({x, y, kBtnW, kBtnH, id});
    };
    toolButton(kBigY, CS::Tool::Big, "crystal/icon_big", "DIVINATION_LABEL_BIG", ID_CS_BIG);
    toolButton(kSmallY, CS::Tool::Small, "crystal/icon_small", "DIVINATION_LABEL_SMALL", ID_CS_SMALL);
    R().text(kPanelX + kPanelW / 2, 140, tr("L 小 / R 大", "L Small / R Large"), ts(F12, col::gray, CENTER, 0, 0.85f));
    R().text(kPanelX + kPanelW / 2, 160, tr("点两次占卜", "Tap twice to divine"), ts(F12, col::gray, CENTER, 0, 0.85f));
  } else if (g.phase == CS::Phase::Done) {
    bool ready = run_->eventChoice.waiting();
    button(kPanelX + 4, 190, kPanelW - 8, 36, tr("继续", "Continue"), ID_CS_PROCEED, ready, true);
  }
}

void App::updateCrystalSphere(const gfx::Input& in) {
  Run& r = *run_;
  CS& g = *crystalSphereGame(r);
  if (g.phase == CS::Phase::Done) {  // the proceed button (ProceedFromTerminalRewardsScreen)
    if (!r.eventChoice.waiting()) return;
    if ((in.down & gfx::BTN_A) || (in.touchDown && hitAt(in.tx, in.ty) == ID_CS_PROCEED)) {
      crystalCursor_ = kN * kN / 2;
      r.eventChoice.fire(0);
    }
    return;
  }
  if (g.phase != CS::Phase::Playing || !g.cellChoice.waiting()) return;
  if (crystalCursor_ < 0 || crystalCursor_ >= kN * kN) crystalCursor_ = kN * kN / 2;
  int x = crystalCursor_ % kN, y = crystalCursor_ / kN;
  if (in.down & gfx::BTN_LEFT) x = std::max(0, x - 1);
  if (in.down & gfx::BTN_RIGHT) x = std::min(kN - 1, x + 1);
  if (in.down & gfx::BTN_UP) y = std::max(0, y - 1);
  if (in.down & gfx::BTN_DOWN) y = std::min(kN - 1, y + 1);
  crystalCursor_ = y * kN + x;
  if (in.down & gfx::BTN_L) g.tool = CS::Tool::Small;  // SetTool
  if (in.down & gfx::BTN_R) g.tool = CS::Tool::Big;
  if (in.down & gfx::BTN_Y) g.tool = g.tool == CS::Tool::Big ? CS::Tool::Small : CS::Tool::Big;
  if ((in.down & gfx::BTN_A) && g.hidden[x][y]) { g.cellChoice.fire(crystalCursor_); return; }
  if (!in.touchDown) return;
  int id = hitAt(in.tx, in.ty);
  if (id == ID_CS_BIG) g.tool = CS::Tool::Big;
  if (id == ID_CS_SMALL) g.tool = CS::Tool::Small;
  if (id >= ID_CS_CELL0 && id < ID_CS_CELL0 + kN * kN) {
    int c = id - ID_CS_CELL0;
    if (c == crystalCursor_) g.cellChoice.fire(c);  // a second tap on the aimed cell divines
    else crystalCursor_ = c;
  }
}

}  // namespace ui
