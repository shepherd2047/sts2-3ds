// Split from ui.cpp (F3).
#include "../ui_common.h"
#include "combat_internal.h"

namespace ui {

// ================================================================ merchant

// RGDSplus U20: the goods and their prices on the bottom screen (cards above; relics,
// potions and the card removal service below), the picked item described on top;
// buying is pick, then 购买.
namespace {
constexpr float kShopCardS = 0.34f;
constexpr int kShopCardSlots = 7;  // 5 character + 2 colorless cards, one row
constexpr int kShopCells = 7;  // relics, potions, removal
constexpr float kShopCellW = 44.f, kShopRow2Y = 98.f;
}

void App::drawShop(bool top) {
  Run& r = *run_;
  int n = (int)r.shop.size();
  if (sel_ >= n) sel_ = -1;
  ShopItem* it = sel_ >= 0 ? &r.shop[sel_] : nullptr;
  if (top) {
    gfx::image(R().texture("gfx/bg_merchant.t3t"), 0, 0, kTop, kH, 0, 0, kTop, kH);
    drawTopBar();
    if (!r.shopMessage.empty() && R().hasLoc("merchant_room." + r.shopMessage + ".line1")) {
      std::string line = L("merchant_room." + r.shopMessage + ".line1");
      float w = R().measure(line, ts(F12)) + 16;
      panel(kTop - w - 110, 40, w, 22, 0xF0E6D0F0, 0x6A5030FF);
      R().text(kTop - w / 2 - 110, 44, line, ts(F12, 0x3A2A18FF, CENTER));
    }
    if (!it || !it->stocked()) return;
    int price = r.shopPrice(*it);
    TextStyle pt = ts(F16, price <= r.gold ? col::gold : col::red);
    if (it->kind == ShopItem::CardItem) {
      drawCard(it->card.get(), 10, 26, 1.2f, false, true);
      gfx::rect(160, kH - 40, kTop - 160, 40, 0x000000B0);
      R().text(170, kH - 32, cardTitle(it->card.get()) + "   " + num(price) + " 金币" + (it->onSale ? "（特价）" : ""), pt);
      return;
    }
    gfx::rect(0, kH - 70, kTop, 70, 0x000000C8);
    std::string title, desc;
    if (it->kind == ShopItem::RelicItem) {
      drawRelicIcon(it->relic.get(), 10, kH - 62, 44);
      title = L("relics." + it->relic->locKey + ".title");
      desc = describeRelic(it->relic.get());
    } else if (it->kind == ShopItem::PotionItem) {
      drawPotionIcon(it->potion.get(), 10, kH - 62, 44);
      title = L("potions." + it->potion->locKey + ".title");
      desc = describePotion(it->potion.get());
    } else {
      spr(R().sprite("ui/card_removal"), 10, kH - 62, 44, 44);
      title = L("merchant_room.MERCHANT.cardRemovalService.title");
      desc = L("merchant_room.MERCHANT.cardRemovalService.description");
      for (size_t p; (p = desc.find("{Amount}")) != std::string::npos;) desc.replace(p, 8, "25");
    }
    R().text(62, kH - 66, title + "   ", ts(F16, col::gold));
    R().text(kTop - 8, kH - 66, num(price) + " 金币", ts(F16, price <= r.gold ? col::gold : col::red, RIGHT));
    R().text(62, kH - 44, desc, ts(F12, col::white, LEFT, kTop - 70, 0.9f));
    return;
  }
  drawSceneBg(false, 0.6f);
  auto priceText = [&](const ShopItem& s, float cx, float y) {
    if (!s.stocked()) { R().text(cx, y, "售罄", ts(F12, col::gray, CENTER, 0, 0.85f)); return; }
    int price = r.shopPrice(s);
    R().text(cx, y, num(price), ts(F12, price <= r.gold ? col::gold : col::red, CENTER, 0, 0.9f));
  };
  // Character cards.
  const float cw = kCardW * kShopCardS, ch = kCardH * kShopCardS, gap = (kBot - kShopCardSlots * cw) / (kShopCardSlots + 1);
  int cardIdx = 0;
  for (int i = 0; i < n; ++i) {
    ShopItem& s = r.shop[i];
    if (s.kind != ShopItem::CardItem) continue;
    float x = gap + cardIdx++ * (cw + gap), y = 4;
    if (i == sel_) gfx::rect(x - 3, y - 3, cw + 6, ch + 20, 0xFFE07060);
    if (s.card) {
      drawCard(s.card.get(), x, y, kShopCardS);
      if (s.onSale) spr(R().sprite("ui/sale_tag"), x + cw - 16, y - 2, 20, 20);
    }
    priceText(s, x + cw / 2, y + ch + 1);
    hits_.push_back({x, y, cw, ch + 16, ID_GRID0 + i});
  }
  // Relics, potions, card removal.
  const float x0 = (kBot - kShopCells * kShopCellW) / 2;
  int cell = 0;
  for (int i = 0; i < n; ++i) {
    ShopItem& s = r.shop[i];
    if (s.kind == ShopItem::CardItem) continue;
    float x = x0 + cell++ * kShopCellW, y = kShopRow2Y;
    if (i == sel_) gfx::rect(x + 1, y - 2, kShopCellW - 2, 52, 0xFFE07060);
    float ix = x + (kShopCellW - 32) / 2, iy = y + 2;
    if (s.kind == ShopItem::RelicItem && s.relic) drawRelicIcon(s.relic.get(), ix, iy, 32);
    else if (s.kind == ShopItem::PotionItem && s.potion) drawPotionIcon(s.potion.get(), ix, iy, 32);
    else if (s.kind == ShopItem::Removal) spr(R().sprite("ui/card_removal"), ix, iy, 32, 32, s.used ? 0x000000FF : 0xFFFFFFFF, s.used ? 0.6f : 0.f);
    priceText(s, x + kShopCellW / 2, y + 36);
    hits_.push_back({x, y, kShopCellW, 50, ID_GRID0 + i});
  }
  R().text(kBot / 2, 160, "金币 " + num(r.gold), ts(F16, col::gold, CENTER));
  bool canBuy = it && it->stocked() && r.shopPrice(*it) <= r.gold &&
                (it->kind != ShopItem::PotionItem || r.hasOpenPotionSlot());
  button(10, 196, 70, 36, "离开", ID_BACK);
  button(84, 196, 70, 36, "药水", ID_POTIONS);  // throw a Foul Potion at the merchant
  button(158, 196, 70, 36, "详情", ID_DETAIL,
         it && it->stocked() && (it->kind == ShopItem::CardItem || it->kind == ShopItem::RelicItem));
  button(kBot - 88, 196, 78, 36, "购买", ID_CONFIRM, canBuy, true);
}

void App::updateShop(const gfx::Input& in) {
  Run& r = *run_;
  if (!r.shopChoice.waiting()) return;
  int n = (int)r.shop.size();
  // D-pad: the seven cards are 0..6 (top row), the other seven items follow (bottom row).
  if (in.down & gfx::BTN_RIGHT) sel_ = std::min(n - 1, sel_ + 1);
  if (in.down & gfx::BTN_LEFT) sel_ = std::max(0, sel_ - 1);
  if (in.down & gfx::BTN_DOWN) sel_ = sel_ < kShopCardSlots ? std::min(n - 1, kShopCardSlots + std::max(sel_, 0)) : sel_;
  if (in.down & gfx::BTN_UP) sel_ = sel_ >= kShopCardSlots ? sel_ - kShopCardSlots : sel_;
  int id = in.touchDown ? hitAt(in.tx, in.ty) : ID_NONE;
  if (id >= ID_GRID0 && id < ID_GRID0 + n) sel_ = id - ID_GRID0;
  if (id == ID_DETAIL && sel_ >= 0 && r.shop[sel_].stocked()) {
    ShopItem& item = r.shop[sel_];
    if (item.kind == ShopItem::CardItem) detailCard_ = item.card.get();
    if (item.kind == ShopItem::RelicItem) detailRelic_ = item.relic.get();
    detailUpgrade_ = false;
    return;
  }
  if ((in.down & gfx::BTN_A) || id == ID_CONFIRM) {
    if (sel_ < 0) return;
    int pick = sel_;
    if (r.shop[pick].kind == ShopItem::Removal) { sel_ = -1; scroll_ = 0; }  // the deck choice uses sel_ too
    r.shopChoice.fire(pick);
    return;
  }
  if (id == ID_POTIONS) { potionsOpen_ = true; potionAim_ = false; potionSel_ = -1; return; }
  if (id == ID_BACK) { sel_ = -1; r.shopChoice.fire(-1); }
}

}  // namespace ui
