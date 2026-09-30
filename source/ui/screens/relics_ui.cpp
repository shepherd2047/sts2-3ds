// Split from ui.cpp (F3).
#include <algorithm>
#include <cmath>

#include "../sfx_router.h"
#include "../ui_common.h"

namespace ui {

// ================================================================ relics

// S15 (RGDSplus U19 relic choice / U23 treasure; C# NTreasureRoom, NChooseARelicSelection).
//
// Treasure room: the act's chest (treasure/chest<N>_closed, baked from ActModel.ChestSpine*) sits
// on the bottom screen; tapping it (or A / 打开) opens it: a sprite swap to the lidless chest, a
// shine, the gold gained in this room (Run already paid it on entering; rules untouched) and the
// relic rising out of the chest, which then fades to half like NTreasureButton.AnimOut.
// Relic choice (one relic from Dig / Lava Rock, or a choose-one of several, Run::chooseRelic):
// the options in a row on the bottom, the focused one's name, rarity and description large on
// top; 跳过 / 拿取 in the action bar. Touch: a tap on a relic takes it (owner decision
// 2026-09-30: one tap picks); D-pad moves the focus (the focused relic is described on top),
// A presses, B skips, X shows the relic detail page.
namespace {
constexpr int kChestId = 300, kOpenId = 301, kSkipId = 302, kTakeId = 303, kOptId = 310;
constexpr float kChestW = 220, kChestY = 50;  // bottom-screen chest (the art is baked 220 wide)
constexpr float kSlot = 56, kSlotIcon = 44;    // relic option slot / icon on the bottom
constexpr float kOpenAnim = 0.45f;             // relic rise + chest fade after opening

const void* offerKey_ = nullptr;  // the offer this state belongs to (first option's address)
bool chestOpen_ = false;
float openT_ = 0;                 // seconds since the chest opened
int focus_ = 0;                   // focused option

float riseEase(float t) {
  t = std::clamp(t, 0.f, 1.f);
  return 1 - (1 - t) * (1 - t) * (1 - t);
}

int chestAct(const sts::Run& r) { return std::clamp(r.actIndex, 0, 2) + 1; }

uint32_t withAlpha(uint32_t rgb, float a) { return (rgb & 0xFFFFFF00u) | (uint32_t)(255 * std::clamp(a, 0.f, 1.f)); }

void openChest(const sts::Run& r) {
  chestOpen_ = true;
  openT_ = 0;
  widgets::setFocus(kOptId);
  sfx::play("event:/sfx/ui/treasure/treasure_act" + num(chestAct(r)));  // ActModel.ChestOpenSfx
}

// The option slots, centred in a row (up to five fit the 320 px bottom screen).
float slotX(int i, int n) {
  const float gap = n > 3 ? 4.f : 16.f;
  float w = n * kSlot + (n - 1) * gap;
  return (kBot - w) / 2 + i * (kSlot + gap);
}
}  // namespace

void App::drawRelicOffer(bool top) {
  Run& r = *run_;
  const int n = (int)r.relicOffers.size();
  const void* key = n > 0 ? (const void*)r.relicOffers[0].get() : nullptr;
  if (key != offerKey_) {  // a new offer: reset the chest and the focus
    offerKey_ = key;
    chestOpen_ = !r.relicOfferFromChest;
    openT_ = chestOpen_ ? kOpenAnim : 0;
    focus_ = 0;
  }
  if (focus_ >= n) focus_ = std::max(0, n - 1);
  Relic* rel = n > 0 ? r.relicOffers[focus_].get() : nullptr;
  const bool chest = r.relicOfferFromChest;
  const float a = riseEase(openT_ / kOpenAnim);

  if (top) {
    if (chestOpen_ && openT_ < kOpenAnim) openT_ += 1.f / 60;  // bookkeeping once per frame
    drawSceneBg(true, 0.55f);
    drawTopBar();
    TextStyle tt = ts(F16, col::gold, CENTER);
    tt.scale = 1.25f;
    std::string title = chest ? L("map.LEGEND_TREASURE.title") : L("gameplay_ui.CHOOSE_RELIC_HEADER");
    R().text(kTop / 2, 28, title, tt);
    gfx::rect(kTop / 2.f - 60, 54, 120, 2, style::kPanelHi);
    if (chest && !chestOpen_) {
      R().text(kTop / 2, 110, L("gameplay_ui.TREASURE_BANNER"), ts(F16, col::white, CENTER));
      R().text(kTop / 2, 136, tr("点击下方的宝箱打开", "Tap the chest below to open it"), ts(F12, col::gray, CENTER));
      return;
    }
    if (!rel) return;
    // The focused relic, large, with its name, rarity and description on a panel.
    const char* rarities[] = {"", tr("初始", "Starter"), tr("普通", "Common"), tr("罕见", "Uncommon"), tr("稀有", "Rare"), tr("商店", "Shop"), tr("事件", "Event"), tr("先古", "Ancient")};
    int rr = (int)rel->rarity;
    std::string desc = describeRelic(rel);
    const float pw = 340, px = (kTop - pw) / 2, py = 62, big = 64;
    TextStyle dt = ts(F12, col::white, CENTER, pw - 24);
    float dh = 0;
    R().measure(desc, dt, &dh);
    float ph = std::min(kH - py - 6, big + 66 + dh);
    widgets::panel("ui/hover_tip", px, py, pw, ph, withAlpha(0xFFFFFF00u, a));
    const float cx = kTop / 2.f, iy = py + 8 + (1 - a) * 12;
    gfx::circle(cx, iy + big / 2, big * 0.62f, withAlpha(0xE0703000u, 0.19f * a));
    drawRelicIcon(rel, cx - big / 2, iy, big);
    TextStyle nt = ts(F16, col::gold, CENTER, pw - 16);
    nt.scale = 1.2f;
    R().text(cx, py + big + 14, L("relics." + rel->locKey + ".title"), nt);
    R().text(cx, py + big + 38, std::string(tr("遗物 · ", "Relic · ")) + (rr >= 0 && rr < 8 ? rarities[rr] : ""),
             ts(F12, col::gold, CENTER));
    R().text(cx, py + big + 56, desc, dt);
    return;
  }

  // ---- bottom ----
  drawSceneBg(false, 0.55f);
  // While the pause menu is over the room, the chest and the options must not take the input.
  gfx::Input in = pauseOpen_ ? gfx::Input{} : gfx::input();
  {  // focus left over from another screen: start on the chest, or on the first relic
    int fo = widgets::focused();
    bool ours = fo == kChestId || fo == kOpenId || fo == kSkipId || fo == kTakeId || (fo >= kOptId && fo < kOptId + n);
    if (!ours) widgets::setFocus(chest && !chestOpen_ ? kChestId : kOptId);
  }
  widgets::beginFrame(in);
  const bool canAct = r.relicChoice.waiting() && !pauseOpen_;
  const float pulse = 0.75f + 0.25f * std::sin((float)time_ * 5.f);

  if (chest) {
    Sprite cs = R().sprite("treasure/chest" + num(chestAct(r)) + (chestOpen_ ? "_open" : "_closed"));
    const float ch = cs ? cs.h * kChestW / cs.w : 100;
    const float cx = (kBot - kChestW) / 2;
    const float cy = kChestY + (chestOpen_ ? 34 * a : 0);  // the open chest settles under the relic
    if (!chestOpen_) {
      // The closed chest breathes (brighter / dimmer) to invite the tap.
      uint8_t v = (uint8_t)(200 + 55 * pulse);
      uint32_t lit = ((uint32_t)v << 24) | ((uint32_t)v << 16) | ((uint32_t)v << 8) | 0xFFu;
      if (cs) spr(cs, cx, cy, kChestW, ch, lit);
      else spr(R().sprite("map/chest"), kBot / 2.f - 30, cy + 10, 60, 60);
      const float hx = cx + 36, hw = kChestW - 72;
      bool open = widgets::hit(kChestId, hx, cy, hw, ch, canAct);
      widgets::focusRing(kChestId, hx, cy, hw, ch);
      R().text(kBot / 2, cy + ch + 10, tr("点击宝箱打开", "Tap the chest to open it"), ts(F12, col::white, CENTER));
      open |= widgets::button(kOpenId, kBot - style::kMargin - 96, style::kActionY, 96, style::kButtonH, tr("打开", "Open"),
                              widgets::Kind::Primary, canAct);
      widgets::endFrame();
      if (open && canAct) openChest(r);
      return;
    }
    // Opened: the lidless chest fades to half (AnimOut) while a shine bursts from its mouth.
    const float mouthY = cy + ch * 0.4f;
    if (a < 1) gfx::circle(kBot / 2.f, mouthY, 30 + 60 * a, withAlpha(0xFFE89000u, 0.35f * (1 - a)));
    if (cs) spr(cs, cx, cy, kChestW, ch, withAlpha(0xFFFFFF00u, 1 - 0.45f * a));
    // The chest's gold (Run paid it on entering; this room's history entry holds the amount).
    int gold = 0;
    if (!r.mapHistory.empty() && !r.mapHistory.back().empty()) gold = r.mapHistory.back().back().goldGained;
    if (gold > 0) {
      TextStyle gt = ts(F16, col::gold, LEFT);
      gt.color = withAlpha(col::gold, a);
      std::string g = "+" + num(gold);
      float w = 22 + R().measure(g, gt);
      float gx = kBot / 2.f - w / 2, gy = style::kActionY - 24 + 6 * (1 - a);
      spr(R().sprite("ui/tb_gold"), gx, gy, 18, 17, withAlpha(0xFFFFFF00u, a));
      R().text(gx + 22, gy, g, gt);
    }
  }

  // The relic options: rising out of the chest, or centred on the screen for a choice.
  const float rowY = chest ? 16 + (1 - a) * 70 : 64;
  const bool takeReady = canAct && openT_ >= kOpenAnim * 0.6f;
  int take = -1;
  for (int i = 0; i < n; ++i) {
    const float x = slotX(i, n), y = rowY;
    if (i == focus_ && n > 1) {  // the focused option: a soft fill and a pulsing gold outline
      uint32_t c = withAlpha(style::kFocus, pulse);
      gfx::rect(x - 3, y - 3, kSlot + 6, kSlot + 6, style::kSelectedFill);
      gfx::rect(x - 3, y - 3, kSlot + 6, 2, c);
      gfx::rect(x - 3, y + kSlot + 1, kSlot + 6, 2, c);
      gfx::rect(x - 3, y - 3, 2, kSlot + 6, c);
      gfx::rect(x + kSlot + 1, y - 3, 2, kSlot + 6, c);
    }
    gfx::circle(x + kSlot / 2, y + kSlot / 2, kSlot * 0.5f, 0x00000070);
    gfx::circle(x + kSlot / 2, y + kSlot / 2, kSlot * 0.44f, withAlpha(0xE0703000u, 0.25f * a));
    const float o = (kSlot - kSlotIcon) / 2;
    drawRelicIcon(r.relicOffers[i].get(), x + o, y + o, kSlotIcon);
    widgets::focusRing(kOptId + i, x - 2, y - 2, kSlot + 4, kSlot + 4);
    if (widgets::hit(kOptId + i, x, y, kSlot, kSlot, takeReady)) {
      focus_ = i;  // one tap takes it
      take = i;
    }
  }
  if (widgets::usingPad()) {
    int f = widgets::focused() - kOptId;
    if (f >= 0 && f < n) focus_ = f;
    rel = n > 0 ? r.relicOffers[focus_].get() : nullptr;
  }
  if (rel)  // the focused relic's name under the row (its description is on top)
    R().text(kBot / 2, rowY + kSlot + 6, L("relics." + rel->locKey + ".title"), ts(F16, col::white, CENTER, kBot - 20));
  if (!chest && n > 1)
    R().text(kBot / 2, rowY + kSlot + 30, tr("点选遗物即可拿取", "Tap a relic to take it"), ts(F12, col::gray, CENTER));

  bool skip = widgets::button(kSkipId, style::kMargin, style::kActionY, 96, style::kButtonH, tr("跳过", "Skip"),
                              widgets::Kind::Secondary, canAct);
  if (widgets::button(kTakeId, kBot - style::kMargin - 96, style::kActionY, 96, style::kButtonH, tr("拿取", "Take"),
                      widgets::Kind::Primary, takeReady && rel != nullptr))
    take = focus_;
  widgets::endFrame();
  if (!canAct) return;
  if (skip) { r.relicChoice.fire(0); return; }
  if (take >= 0 && take < n) r.relicChoice.fire(take + 1);
}

void App::updateRelicOffer(const gfx::Input& in) {
  // Taking, skipping and the focus are handled by the widgets in drawRelicOffer (A presses the
  // focused control while the D-pad is in use); here A without a D-pad focus, B and X.
  Run& r = *run_;
  if (!r.relicChoice.waiting() || pauseOpen_) return;
  int n = (int)r.relicOffers.size();
  bool padA = (in.down & gfx::BTN_A) && !widgets::usingPad();
  if (!chestOpen_) {
    if (padA) openChest(r);
    return;
  }
  if (padA && openT_ >= kOpenAnim * 0.6f && focus_ >= 0 && focus_ < n) {
    r.relicChoice.fire(focus_ + 1);
    return;
  }
  if ((in.down & gfx::BTN_X) && focus_ >= 0 && focus_ < n) {
    inspectRelics(r.relicOffers, focus_);
    return;
  }
  if (in.down & gfx::BTN_B) r.relicChoice.fire(0);
}

// Owned relics: a grid below, the selected one described above (RGDSplus U24/U25).
void App::drawRelics(bool top) {
  auto& rels = run_->relics;
  int n = (int)rels.size();
  if (sel_ >= n) sel_ = n - 1;
  if (top) {
    drawSceneBg(true, 0.7f);
    drawTopBar();
    if (sel_ >= 0) drawRelicDetail(rels[sel_].get(), 84);
    else R().text(kTop / 2, 100, tr("遗物（", "Relics (") + num(n) + tr(" 个）", ")"), ts(F16, col::gold, CENTER));
    return;
  }
  drawSceneBg(false, 0.65f);
  const int cols = 6;
  const float cell = 48, x0 = (kBot - cols * cell) / 2, y0 = 8;
  for (int i = 0; i < n; ++i) {
    float x = x0 + (i % cols) * cell, y = y0 + (i / cols - scroll_) * cell;
    if (y < 0 || y > 190) continue;
    if (i == sel_) gfx::rect(x + 2, y + 2, cell - 4, cell - 4, 0xFFE07060);
    drawRelicIcon(rels[i].get(), x + 6, y + 6, cell - 12);
    hits_.push_back({x, y, cell, cell, ID_RELIC0 + i});
  }
  gfx::rect(0, 196, kBot, 44, 0x000000A0);
  button(10, 200, 100, 34, tr("返回", "Back"), ID_BACK);
  button(118, 200, 84, 34, tr("详情", "Details"), ID_DETAIL, sel_ >= 0 && sel_ < n);
  button(kBot - 110, 200, 100, 34, tr("牌组", "Deck"), ID_DECK);
}

void App::updateRelics(const gfx::Input& in) {
  int n = (int)run_->relics.size();
  auto close = [&] { relicsOpen_ = false; sel_ = -1; scroll_ = 0; };
  if (in.down & gfx::BTN_RIGHT) sel_ = std::min(n - 1, sel_ + 1);
  if (in.down & gfx::BTN_LEFT) sel_ = std::max(0, sel_ - 1);
  if (in.down & gfx::BTN_DOWN) sel_ = std::min(n - 1, sel_ + 6);
  if (in.down & gfx::BTN_UP) sel_ = std::max(0, sel_ - 6);
  if (sel_ >= 0) scroll_ = std::max(0, sel_ / 6 - 2);
  if (in.down & gfx::BTN_B) { close(); return; }
  if ((in.down & gfx::BTN_A) && sel_ >= 0 && sel_ < n) { inspectRelics(run_->relics, sel_); return; }
  if (in.touchDown) {
    int id = hitAt(in.tx, in.ty);
    if (id >= ID_RELIC0 && id < ID_RELIC0 + n) {
      int picked = id - ID_RELIC0;
      if (sel_ == picked) { inspectRelics(run_->relics, picked); return; }
      sel_ = picked;
    }
    if (id == ID_DETAIL && sel_ >= 0 && sel_ < n) { inspectRelics(run_->relics, sel_); return; }
    if (id == ID_BACK) close();
    if (id == ID_DECK) { close(); openCardList(CardListMode::Deck); }
  }
}

}  // namespace ui
