// Split from ui.cpp (F3).
#include "../ui_common.h"

namespace ui {

void App::drawRelicDetail(Relic* r, float cy) {
  const float big = 56;
  gfx::circle(kTop / 2.f, cy, 38, 0xFFE07030);
  drawRelicIcon(r, kTop / 2.f - big / 2, cy - big / 2, big);
  static const char* rarities[] = {"", "初始", "普通", "罕见", "稀有", "商店", "事件", "先古"};
  TextStyle nt = ts(F16, col::gold, CENTER);
  nt.scale = 1.2f;
  R().text(kTop / 2.f, cy + 36, L("relics." + r->locKey + ".title"), nt);
  R().text(kTop / 2.f, cy + 60, rarities[(int)r->rarity], ts(F12, col::gray, CENTER));
  R().text(kTop / 2.f, cy + 78, describeRelic(r), ts(F12, col::white, CENTER, kTop - 60));
}

static std::vector<const char*> cardKeywordKeys(const Card* c) {
  std::vector<const char*> keys;
  if (!c) return keys;
  if (c->has(kwUnplayable)) keys.push_back("UNPLAYABLE");
  if (c->has(kwEthereal)) keys.push_back("ETHEREAL");
  if (c->has(kwInnate)) keys.push_back("INNATE");
  if (c->has(kwRetain)) keys.push_back("RETAIN");
  if (c->has(kwExhaust)) keys.push_back("EXHAUST");
  return keys;
}

void App::drawDetail(bool top) {
  std::unique_ptr<Card> upgraded;
  Card* card = detailCard_;
  if (card && detailUpgrade_ && card->upgradable()) {
    upgraded = card->clone();
    upgraded->upgrade();
    card = upgraded.get();
  }
  drawSceneBg(top, 0.85f);
  if (top) {
    if (card) {
      drawCard(card, (kTop - 156) / 2.f, 10, 1.3f, false, true);
      if (upgraded) R().text(kTop - 10, 8, "升级预览", ts(F12, col::green, RIGHT));
    } else if (detailRelic_) {
      drawRelicDetail(detailRelic_, 67);
    }
    return;
  }
  panel(12, 10, kBot - 24, 178);
  std::string title = card ? cardTitle(card) : L("relics." + detailRelic_->locKey + ".title");
  R().text(kBot / 2, 22, title, ts(F16, col::gold, CENTER, kBot - 40));
  auto keywords = cardKeywordKeys(card);
  std::string description = card ? describe(card) : describeRelic(detailRelic_);
  if (detailKeyword_ >= 0 && detailKeyword_ < (int)keywords.size()) {
    std::string key = "card_keywords." + std::string(keywords[detailKeyword_]);
    description = "[gold]" + L(key + ".title") + "[/gold]" +
                  "  " + num(detailKeyword_ + 1) + "/" + num((int)keywords.size()) +
                  "\n\n" + L(key + ".description");
  }
  TextStyle desc = ts(F16, col::white, CENTER, kBot - 48);
  float h = 0;
  R().measure(description, desc, &h);
  while (h > 128 && desc.scale > 0.65f) {
    desc.scale *= 0.9f;
    R().measure(description, desc, &h);
  }
  R().text(kBot / 2, 55, description, desc);
  bool canUpgrade = detailCard_ && detailCard_->upgradable();
  if (canUpgrade && !keywords.empty()) {
    button(8, 199, 90, 34, "关闭", ID_BACK);
    button(106, 199, 104, 34, detailUpgrade_ ? "原卡" : "升级预览", ID_UPGRADE_PREVIEW, true, detailUpgrade_);
    button(218, 199, 94, 34, detailKeyword_ < 0 ? "关键词" :
           detailKeyword_ + 1 < (int)keywords.size() ? "下个词" : "卡牌", ID_KEYWORD, true, detailKeyword_ >= 0);
  } else if (canUpgrade) {
    button(12, 199, 140, 34, "关闭", ID_BACK);
    button(168, 199, 140, 34, detailUpgrade_ ? "原卡" : "升级预览", ID_UPGRADE_PREVIEW, true, detailUpgrade_);
  } else if (!keywords.empty()) {
    button(12, 199, 140, 34, "关闭", ID_BACK);
    button(168, 199, 140, 34, detailKeyword_ < 0 ? "关键词" :
           detailKeyword_ + 1 < (int)keywords.size() ? "下个词" : "卡牌", ID_KEYWORD, true, detailKeyword_ >= 0);
  } else {
    button(90, 199, 140, 34, "关闭", ID_BACK, true, true);
  }
}

void App::updateDetail(const gfx::Input& in) {
  int id = in.touchDown ? hitAt(in.tx, in.ty) : ID_NONE;
  if ((in.down & gfx::BTN_B) || id == ID_BACK) {
    detailCard_ = nullptr;
    detailRelic_ = nullptr;
    detailUpgrade_ = false;
    detailKeyword_ = -1;
    return;
  }
  if (detailCard_ && detailCard_->upgradable() && ((in.down & gfx::BTN_X) || id == ID_UPGRADE_PREVIEW)) {
    detailUpgrade_ = !detailUpgrade_;
    detailKeyword_ = -1;
  }
  if (detailCard_ && ((in.down & gfx::BTN_Y) || id == ID_KEYWORD)) {
    std::unique_ptr<Card> upgraded;
    Card* card = detailCard_;
    if (detailUpgrade_) { upgraded = card->clone(); upgraded->upgrade(); card = upgraded.get(); }
    auto keywords = cardKeywordKeys(card);
    if (!keywords.empty()) detailKeyword_ = (detailKeyword_ + 1) % ((int)keywords.size() + 1);
    if (detailKeyword_ == (int)keywords.size()) detailKeyword_ = -1;
  }
}

}  // namespace ui
