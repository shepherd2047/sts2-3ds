// Split from ui.cpp (F3).
#include <cmath>

#include "../ui_common.h"
#include "combat_internal.h"

namespace ui {

// ================================================================ merchant

// S16 (RGDSplus U20, C# NMerchantInventory): the goods, their price tags, the card removal
// service and the leave button on the bottom screen; the merchant's tent on top with the
// focused item large and described over it. The bottom follows the C# rug: the five character
// cards in a row, then the two colorless cards, relics above potions, and the removal service
// at the right. Touch: a tap on an item buys it (owner decision 2026-09-30: one tap picks) and
// focuses it, so a refused tap leaves it described on top next to the merchant's line; D-pad
// moves the focus between items and buttons, A buys the focused item, X shows the card / relic
// detail, B drops the focus. 购买 buys the focused item too.
// A purchase the merchant refuses (not enough gold, no potion slot) goes through shopChoice
// anyway so Run::enterShop sets his refusal line, as the C# plays it on a failed click.
// The FakeMerchant event (relics only) reuses this screen: without cards, its relics are laid
// out in one centred row of larger slots.
namespace {
constexpr int kGoodsId = 100;  // widget id of shop item i: kGoodsId + i
constexpr int kLeaveId = 90, kPotionsId = 91, kBuyId = 92;
constexpr float kCardS = 0.42f;                                  // bottom-screen shop card
constexpr float kShopCardW = kCardW * kCardS, kShopCardH = kCardH * kCardS;
constexpr float kPriceH = 14;                                    // price tag line under a slot
constexpr float kRow1Y = 5, kRow2Y = 96;                         // character cards / the rest
constexpr float kCellW = 40, kIcon = 32;                         // relic and potion cells
constexpr float kGridX = 128;                                    // first relic / potion column
constexpr float kRemovalX = 256, kRemovalW = 58, kRemovalIcon = 40;
constexpr float kTopCardS = 1.1f;                                // focused card on the top screen

struct Slot { float x, y, w, h; };  // hit box: the art plus its price tag

// A purchase happened while no refusal is showing: the merchant thanks the player for a while
// (purchaseSuccess lines; the C# rolls one of three, here the next one each time).
int lastStocked_ = -1;
float thanksT_ = 0;
int thanksLine_ = 0;
int welcomedFloor_ = -1;    // the shop whose merchant_welcome has played (one per visit)
std::string lastMessage_;  // the refusal shown last frame (merchant_dissapointment on a new one)
}  // namespace

namespace {

// Where every item of the shop sits on the bottom screen (by kind, so the FakeMerchant's
// relic-only shop and a shop with missing slots both lay out).
std::vector<Slot> shopLayout(const std::vector<sts::ShopItem>& shop) {
  std::vector<Slot> out(shop.size(), Slot{-1000, -1000, 0, 0});
  int nChar = 0, nColor = 0, nRelic = 0, nPotion = 0;
  bool anyCard = false;
  for (auto& s : shop) {
    if (s.kind == sts::ShopItem::CardItem) { anyCard = true; (s.colorless ? nColor : nChar)++; }
    if (s.kind == sts::ShopItem::RelicItem) nRelic++;
    if (s.kind == sts::ShopItem::PotionItem) nPotion++;
  }
  if (!anyCard) {  // relics (and potions) centred in rows of up to six larger cells
    const float cell = 50;
    int ri = 0, pi = 0;
    for (size_t i = 0; i < shop.size(); ++i) {
      auto& s = shop[i];
      int row, col, cnt;
      if (s.kind == sts::ShopItem::RelicItem) { row = ri / 6; col = ri % 6; cnt = std::min(6, nRelic - row * 6); ri++; }
      else if (s.kind == sts::ShopItem::PotionItem) { row = 2 + pi / 6; col = pi % 6; cnt = std::min(6, nPotion - (row - 2) * 6); pi++; }
      else continue;
      float x0 = (kBot - cnt * cell) / 2;
      out[i] = {x0 + col * cell, 60.f + row * 62, cell, 58};
    }
    return out;
  }
  const float gap = (kBot - 5 * kShopCardW) / 6;
  int ci = 0, cc = 0, ri = 0, pi = 0;
  for (size_t i = 0; i < shop.size(); ++i) {
    auto& s = shop[i];
    switch (s.kind) {
      case sts::ShopItem::CardItem:
        if (!s.colorless) out[i] = {gap + ci++ * (kShopCardW + gap), kRow1Y, kShopCardW, kShopCardH + kPriceH};
        else out[i] = {10 + cc++ * (kShopCardW + 8), kRow2Y, kShopCardW, kShopCardH + kPriceH};
        break;
      case sts::ShopItem::RelicItem:
        out[i] = {kGridX + (ri % 3) * kCellW, kRow2Y + (ri / 3) * 96, kCellW, kIcon + kPriceH + 2};
        ri++;
        break;
      case sts::ShopItem::PotionItem:
        out[i] = {kGridX + (pi % 3) * kCellW, kRow2Y + 48 + (pi / 3) * 96, kCellW, kIcon + kPriceH + 2};
        pi++;
        break;
      case sts::ShopItem::Removal:
        out[i] = {kRemovalX, kRow2Y + 8, kRemovalW, kRemovalIcon + 20 + kPriceH};
        break;
    }
  }
  (void)nChar; (void)nColor; (void)nRelic; (void)nPotion;
  return out;
}

}  // namespace

// A price tag: the gold coin and the price, gold when affordable and red when not (C#
// NMerchantSlot UpdateVisual: StsColors.red when the cost exceeds the player's gold).
static void priceTag(float cx, float y, int price, bool affordable, float scale = 0.9f) {
  TextStyle st = ts(F12, affordable ? col::gold : col::red, LEFT, 0, scale);
  std::string s = num(price);
  float coin = 12 * scale / 0.9f, w = coin + 2 + R().measure(s, st);
  float x = cx - w / 2;
  spr(R().sprite("ui/tb_gold"), x, y + 1, coin, coin * 17 / 18);
  R().text(x + coin + 2, y, s, st);
}

void App::drawShop(bool top) {
  Run& r = *run_;
  int n = (int)r.shop.size();
  if (sel_ >= n) sel_ = -1;
  ShopItem* it = sel_ >= 0 ? &r.shop[sel_] : nullptr;

  if (top) {
    // Purchase feedback (bookkeeping on the top pass, once per frame).
    int stocked = 0;
    for (auto& s : r.shop) stocked += s.stocked() ? 1 : 0;
    if (welcomedFloor_ != r.floor) {  // NMerchantInventory.Open: merchant_welcome
      welcomedFloor_ = r.floor;
      lastStocked_ = -1;
      lastMessage_.clear();
      sfx::play("event:/sfx/npcs/merchant/merchant_welcome");
    }
    if (lastStocked_ >= 0 && stocked < lastStocked_ && r.shopMessage.empty()) {
      thanksT_ = 2.5f;
      thanksLine_ = thanksLine_ % 3 + 1;
      sfx::play("event:/sfx/npcs/merchant/merchant_thank_yous");  // OnPurchaseCompleted
    }
    if (!r.shopMessage.empty() && r.shopMessage != lastMessage_)
      sfx::play("event:/sfx/npcs/merchant/merchant_dissapointment");  // NMerchantSlot.OnPurchaseFailed
    lastMessage_ = r.shopMessage;
    lastStocked_ = stocked;
    if (thanksT_ > 0) thanksT_ -= 1.f / 60;
    if (!r.shopMessage.empty()) thanksT_ = 0;

    gfx::image(R().texture("gfx/bg_merchant.t3t"), 0, 0, kTop, kH, 0, 0, kTop, kH);
    if (it && it->stocked()) {  // the focus side reads over the tent, fading out before the merchant
      gfx::rect(0, 0, 200, kH, 0x00000070);
      gfx::gradient(200, 0, 60, kH, 0x00000070, 0x00000000, 0x00000070, 0x00000000);
    }
    drawTopBar();

    // The merchant's line in a bubble over his head (he sits at about x 255..310).
    std::string line;
    if (!r.shopMessage.empty() && R().hasLoc("merchant_room." + r.shopMessage + ".line1"))
      line = L("merchant_room." + r.shopMessage + ".line1");
    else if (thanksT_ > 0)
      line = L("merchant_room.MERCHANT.talk.purchaseSuccess.line" + num(thanksLine_));
    if (!line.empty()) {
      TextStyle bt = ts(F12, col::white, CENTER);
      float w = std::min(200.f, R().measure(line, bt) + 20);
      float cx = std::min(kTop - w / 2 - 6, 290.f);
      widgets::panel("ui/hover_tip", cx - w / 2, 64, w, 26);
      R().text(cx, 70, line, ts(F12, col::white, CENTER, w - 12));
    }

    if (!it || !it->stocked()) {
      if (sel_ < 0) {
        // A small hint in the corner away from the merchant (the old full-width bar covered him).
        TextStyle ht = ts(F12, col::white, CENTER);
        const std::string hint = tr("点选商品即可购买", "Tap an item to buy it");
        const float hw = R().measure(hint, ht) + 16;
        widgets::panel("ui/hover_tip", 8, kH - 32, hw, 24);
        R().text(8 + hw / 2, kH - 27, hint, ht);
      }
      return;
    }
    int price = r.shopPrice(*it);
    bool afford = price <= r.gold;
    // The price band under the focused item (same place for every kind).
    auto priceBand = [&](float x, float w, bool sale) {
      widgets::panel("ui/hover_tip", x, kH - 30, w, 24);
      float cx = x + w / 2 - (sale ? 18 : 0);
      priceTag(cx, kH - 25, price, afford, 1.15f);
      if (sale) {
        spr(R().sprite("ui/sale_tag"), cx + 26, kH - 30, 22, 22);
        R().text(cx + 50, kH - 25, tr("特价", "Sale"), ts(F12, col::green, LEFT));
      }
    };
    if (it->kind == ShopItem::CardItem) {
      const float cw = kCardW * kTopCardS;
      drawCard(it->card.get(), 14, 26, kTopCardS, false, true);
      if (it->onSale) spr(R().sprite("ui/sale_tag"), 14 + cw - 22, 20, 30, 30);
      priceBand(14, cw, it->onSale);
      // Keyword-free side note: which shelf the card is from.
      R().text(14 + cw + 20, 30, it->colorless ? tr("无色牌", "Colorless card") : tr("角色牌", "Character card"), ts(F12, col::gold));
      if (!afford) R().text(14 + cw + 20, 46, tr("金币不足", "Not enough gold"), ts(F12, col::red));
      return;
    }
    // Relic / potion / removal: a panel with the big icon, name, rarity and description.
    const float px = 10, pw = 232, py = 28 + relicRowH();
    std::string title, sub, desc;
    if (it->kind == ShopItem::RelicItem) {
      const char* rarities[] = {"", tr("初始", "Starter"), tr("普通", "Common"), tr("罕见", "Uncommon"), tr("稀有", "Rare"), tr("商店", "Shop"), tr("事件", "Event"), tr("先古", "Ancient")};
      title = L("relics." + it->relic->locKey + ".title");
      int rr = (int)it->relic->rarity;
      sub = std::string(tr("遗物 · ", "Relic · ")) + (rr >= 0 && rr < 8 ? rarities[rr] : "");
      desc = describeRelic(it->relic.get());
    } else if (it->kind == ShopItem::PotionItem) {
      const char* rarities[] = {"", tr("普通", "Common"), tr("罕见", "Uncommon"), tr("稀有", "Rare"), tr("事件", "Event"), tr("衍生", "Token")};
      title = L("potions." + it->potion->locKey + ".title");
      int pr = (int)it->potion->rarity;
      sub = std::string(tr("药水 · ", "Potion · ")) + (pr >= 0 && pr < 6 ? rarities[pr] : "");
      desc = describePotion(it->potion.get());
    } else {
      title = L("merchant_room.MERCHANT.cardRemovalService.title");
      sub = tr("服务", "Service");
      desc = L("merchant_room.MERCHANT.cardRemovalService.description");
      std::string amt = num(r.ascValue(sts::kInflation, 50, 25));
      for (size_t p; (p = desc.find("{Amount}")) != std::string::npos;) desc.replace(p, 8, amt);
    }
    const float big = 60;
    TextStyle dt = ts(F12, col::white, LEFT, pw - 20);
    float dh = 0;
    R().measure(desc, dt, &dh);
    float ph = std::min(kH - 36 - py, big + 60 + dh);
    widgets::panel("ui/hover_tip", px, py, pw, ph);
    const float cx = px + pw / 2;
    gfx::circle(cx, py + 8 + big / 2, big * 0.62f, 0xFFE07030);
    if (it->kind == ShopItem::RelicItem) spr(R().sprite("relic/" + it->relic->icon), cx - big / 2, py + 8, big, big);  // no counter over the price
    else if (it->kind == ShopItem::PotionItem) drawPotionIcon(it->potion.get(), cx - big / 2, py + 8, big);
    else spr(R().sprite("ui/card_removal"), cx - big / 2, py + 8, big, big);
    TextStyle tt = ts(F16, col::gold, CENTER, pw - 16);
    tt.scale = 1.1f;
    R().text(cx, py + big + 12, title, tt);
    R().text(cx, py + big + 34, sub, ts(F12, col::gray, CENTER));
    R().text(px + 10, py + big + 52, desc, dt);
    priceBand(px, pw, false);
    if (it->kind == ShopItem::PotionItem && !r.hasOpenPotionSlot())
      R().text(px + pw + 8, kH - 25, tr("药水栏已满", "Potion belt full"), ts(F12, col::red));
    else if (!afford)
      R().text(px + pw + 8, kH - 25, tr("金币不足", "Not enough gold"), ts(F12, col::red));
    return;
  }

  // ---- bottom: the goods ----
  drawSceneBg(false, 0.6f);
  std::vector<Slot> slots = shopLayout(r.shop);
  // While the pause menu is over the shop, its goods must not take the input.
  gfx::Input in = pauseOpen_ ? gfx::Input{} : gfx::input();
  {  // focus left over from another screen: drop it, so the first D-pad press lands on the
     // item nearest the top-left (the first card)
    int fo = widgets::focused();
    bool ours = (fo >= kGoodsId && fo < kGoodsId + n) || fo == kLeaveId || fo == kPotionsId || fo == kBuyId;
    if (!ours) widgets::setFocus(-1);
  }
  widgets::beginFrame(in);
  int f = widgets::focused() - kGoodsId;
  if (widgets::usingPad() && f >= 0 && f < n) sel_ = f;
  bool canAct = r.shopChoice.waiting() && !pauseOpen_;
  int buy = -1;
  float pulse = 0.75f + 0.25f * std::sin((float)time_ * 5.f);
  for (int i = 0; i < n; ++i) {
    ShopItem& s = r.shop[i];
    const Slot& b = slots[i];
    if (b.x < -500) continue;
    bool stocked = s.stocked();
    int price = r.shopPrice(s);
    bool afford = price <= r.gold;
    if (i == sel_) {  // the focused slot: a soft glow and a pulsing gold outline
      uint32_t c = (style::kFocus & 0xFFFFFF00u) | (uint32_t)(0xFF * pulse);
      gfx::rect(b.x - 3, b.y - 3, b.w + 6, b.h + 6, style::kSelectedFill);
      gfx::rect(b.x - 3, b.y - 3, b.w + 6, 2, c);
      gfx::rect(b.x - 3, b.y + b.h + 1, b.w + 6, 2, c);
      gfx::rect(b.x - 3, b.y - 3, 2, b.h + 6, c);
      gfx::rect(b.x + b.w + 1, b.y - 3, 2, b.h + 6, c);
    }
    float priceY = b.y + b.h - kPriceH + 1;
    switch (s.kind) {
      case ShopItem::CardItem:
        if (s.card) {
          drawCard(s.card.get(), b.x, b.y, kCardS, false, true);
          if (s.onSale) spr(R().sprite("ui/sale_tag"), b.x + b.w - 14, b.y - 5, 20, 20);
        } else {
          gfx::rect(b.x, b.y, b.w, kShopCardH, 0xFFFFFF10);
        }
        break;
      case ShopItem::RelicItem:
      case ShopItem::PotionItem: {
        float ic = b.w >= 50 ? 40 : kIcon, ix = b.x + (b.w - ic) / 2, iy = b.y + 2;
        if (s.relic) spr(R().sprite("relic/" + s.relic->icon), ix, iy, ic, ic);
        else if (s.potion) drawPotionIcon(s.potion.get(), ix, iy, ic);
        else gfx::circle(ix + ic / 2, iy + ic / 2, ic * 0.36f, 0xFFFFFF18);
        break;
      }
      case ShopItem::Removal: {
        float ix = b.x + (b.w - kRemovalIcon) / 2;
        spr(R().sprite("ui/card_removal"), ix, b.y + 2, kRemovalIcon, kRemovalIcon, stocked ? 0xFFFFFFFF : 0x606060FF);
        R().text(b.x + b.w / 2, b.y + kRemovalIcon + 4, tr("移除卡牌", "Remove card"), ts(F12, stocked ? col::white : col::gray, CENTER, 0, 0.85f));
        break;
      }
    }
    if (stocked) priceTag(b.x + b.w / 2, priceY, price, afford);
    else R().text(b.x + b.w / 2, priceY, tr("售罄", "Sold out"), ts(F12, col::gray, CENTER, 0, 0.85f));
    // Sold slots stay focusable (for the D-pad path) but do nothing.
    if (widgets::hit(kGoodsId + i, b.x, b.y, b.w, b.h, canAct)) {
      sel_ = i;  // one tap buys (a refusal still plays his line, and the item stays focused)
      if (stocked) buy = i;
    }
  }

  // Action bar: leave (back, bottom-left), the potion belt (Foul Potion throw), gold, buy.
  if (widgets::button(kLeaveId, style::kMargin, style::kActionY, 70, style::kButtonH, tr("离开", "Leave"),
                      widgets::Kind::Secondary, canAct)) {
    sel_ = -1;
    r.shopChoice.fire(-1);
    widgets::endFrame();
    return;
  }
  if (widgets::button(kPotionsId, style::kMargin + 74, style::kActionY, 64, style::kButtonH, tr("药水", "Potions"),
                      widgets::Kind::Secondary, canAct)) {
    potionsOpen_ = true;
    potionAim_ = false;
    potionSel_ = -1;
  }
  {
    float gx = 166, gy = style::kActionY + (style::kButtonH - 16) / 2;
    spr(R().sprite("ui/tb_gold"), gx, gy, 18, 17);
    R().text(gx + 22, gy, num(r.gold), ts(F16, col::gold));
  }
  bool canBuy = it && it->stocked();
  if (widgets::button(kBuyId, kBot - style::kMargin - 84, style::kActionY, 84, style::kButtonH, tr("购买", "Buy"),
                      widgets::Kind::Primary, canAct && canBuy))
    buy = sel_;
  widgets::endFrame();

  if (buy >= 0 && canAct) {
    // The removal service opens the deck choice, which uses sel_ as well.
    if (r.shop[buy].kind == ShopItem::Removal) { sel_ = -1; scroll_ = 0; }
    r.shopChoice.fire(buy);
  }
}

void App::updateShop(const gfx::Input& in) {
  // Buying, leaving and the focus are handled by the widgets in drawShop; the extra keys here.
  Run& r = *run_;
  if (!r.shopChoice.waiting()) return;
  int n = (int)r.shop.size();
  if ((in.down & gfx::BTN_X) && sel_ >= 0 && sel_ < n && r.shop[sel_].stocked()) {
    ShopItem& item = r.shop[sel_];
    if (item.card) inspectCard(item.card.get());
    else if (item.relic) inspectRelic(item.relic.get());
    else if (item.potion) inspectPotion(item.potion.get());
    return;
  }
  if ((in.down & gfx::BTN_B) && sel_ >= 0) { sel_ = -1; widgets::setFocus(-1); }
}

}  // namespace ui
