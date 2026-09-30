// S20 (RGDSplus U25; C# NInspectCardScreen / NInspectRelicScreen + NHoverTipSet): the detail
// popup every screen opens for a card, relic or potion. Top: the item large, its hover tips
// (keywords, powers it references, enchantment, Block/Energy/... statics; C# HoverTips) in a
// column beside it, paged when they do not fit. Bottom: name + text, prev / next when opened
// from a list (deck, library, collection, hand, owned relics), the upgrade-preview checkbox for
// cards (card_selection.VIEW_UPGRADES), 关闭. Both screens dimmed.
//
// Keys: left / right or L R previous / next, X (or A) upgrade preview, Y / ▲ ▼ next page of tips, B close.
// Touch: the buttons. Open with inspectCard / inspectRelic / inspectPotion (ui.h); assigning
// detailCard_ / detailRelic_ directly still works (no list).
#include "../ui_common.h"

#include <unordered_map>

namespace ui {

namespace {

// ---------------------------------------------------------------- hover tips (C# HoverTips)

struct TipRow { const char* id; const char* tips; };
const TipRow kTipRows[] = {
#include "../hover_tips.inc"
};

const char* extraTips(const std::string& key) {
  static std::unordered_map<std::string, const char*> map;
  if (map.empty())
    for (auto& r : kTipRows) map[r.id] = r.tips;
  auto it = map.find(key);
  return it == map.end() ? "" : it->second;
}

std::string slug(const std::string& name) {  // StringHelper.Slugify: VulnerablePower -> VULNERABLE_POWER
  std::string out;
  for (size_t i = 0; i < name.size(); ++i) {
    char c = name[i];
    bool up = std::isupper((unsigned char)c);
    if (i && up) {
      char p = name[i - 1];
      bool nextLower = i + 1 < name.size() && std::islower((unsigned char)name[i + 1]);
      if (std::islower((unsigned char)p) || std::isdigit((unsigned char)p) || (std::isupper((unsigned char)p) && nextLower))
        out += '_';
    }
    out += (char)std::toupper((unsigned char)c);
  }
  return out;
}

struct Tip { std::string title, desc; };

void addTip(std::vector<Tip>& out, const std::string& title, const std::string& desc) {
  if (title.empty()) return;
  for (auto& t : out) if (t.title == title && t.desc == desc) return;  // HoverTips.Distinct()
  out.push_back({title, desc});
}

std::vector<DynVar> withAmount(std::vector<DynVar> vars, int amount) {
  vars.push_back({"Amount", Dec(amount), Dec(amount)});
  return vars;
}

}  // namespace

// ---------------------------------------------------------------- popup state

namespace {

struct Item { Card* card = nullptr; Relic* relic = nullptr; Potion* potion = nullptr; };

struct Popup {
  bool active = false;       // a session is running (reset by closeDetail)
  bool armed = false;        // the touch that opened it has been released
  bool viewAll = false;      // NInspectCardScreen._viewAllUpgraded
  float openT = 0;           // slide-in, 0..1
  int savedFocus = -1;       // the caller's widget focus, restored on close
  std::vector<Item> list;
  int index = -1;
  int page = 0;              // tip page
  // Cache of what is shown (rebuilt when the item or the upgrade tick changes).
  const void* key = nullptr;
  bool keyUpgrade = false;
  std::unique_ptr<Card> shown;  // the upgraded / downgraded copy, or null = the card itself
  std::vector<Tip> tips;
  std::vector<std::vector<int>> pages;
  float pageW = 0;
};
Popup& P() {
  static Popup p;
  return p;
}

constexpr int kIdClose = 7700, kIdPrev = 7701, kIdNext = 7702, kIdUpgrade = 7703, kIdPage = 7704;
constexpr float kTipGap = 4, kTipPad = 6, kTipTop = 8, kTipBottom = 232;

std::string relicRarityName(RelicRarity r) {
  static const char* names[] = {"", "初始", "普通", "罕见", "稀有", "商店", "事件", "先古"};
  return names[(int)r];
}
std::string potionRarityName(PotionRarity r) {
  static const char* names[] = {"", "普通", "罕见", "稀有", "事件", "衍生"};
  return names[(int)r];
}
std::string cardTypeName(CardType t) {
  switch (t) {
    case CardType::Attack: return L("gameplay_ui.CARD_TYPE.ATTACK");
    case CardType::Skill: return L("gameplay_ui.CARD_TYPE.SKILL");
    case CardType::Power: return L("gameplay_ui.CARD_TYPE.POWER");
    case CardType::Status: return "状态";
    case CardType::Curse: return "诅咒";
  }
  return "";
}
std::string cardRarityName(Rarity r) {
  static const char* names[] = {"基础", "普通", "罕见", "稀有", "先古", "衍生", "状态", "诅咒"};
  return names[(int)r];
}

float tipHeight(const Tip& t, float w) {
  float dh = 0;
  if (!t.desc.empty()) R().measure(t.desc, ts(F12, col::white, LEFT, w - 2 * kTipPad), &dh);
  return kTipPad * 2 + R().lineHeight(F16) + (t.desc.empty() ? 0 : dh + 2);
}

float drawTip(const Tip& t, float x, float y, float w) {
  float h = tipHeight(t, w);
  widgets::panel("ui/hover_tip", x, y, w, h);
  R().text(x + kTipPad, y + kTipPad - 1, t.title, ts(F16, col::gold, LEFT, w - 2 * kTipPad));
  if (!t.desc.empty())
    R().text(x + kTipPad, y + kTipPad + R().lineHeight(F16) + 1, t.desc, ts(F12, col::white, LEFT, w - 2 * kTipPad));
  return h;
}

}  // namespace

// The tips one C# ExtraHoverTips list names ("P:VulnerablePower;K:EXHAUST;..."), with `vars` for
// the dynamic statics ({Times}, {Summon}) and power amounts.
static void tableTips(std::vector<Tip>& out, const std::string& key, const std::vector<DynVar>& vars,
                      const std::function<std::string(Card*)>& describe) {
  std::string s = extraTips(key);
  size_t a = 0;
  while (a < s.size()) {
    size_t b = s.find(';', a);
    if (b == std::string::npos) b = s.size();
    std::string t = s.substr(a, b - a);
    a = b + 1;
    if (t.size() < 3 || t[1] != ':') continue;
    char kind = t[0];
    std::string name = t.substr(2);
    std::string tk, dk;
    std::vector<DynVar> v = vars;
    switch (kind) {
      case 'P': {  // PowerModel.GetDumbHoverTip: title + description with {Amount}
        const DynVar* amt = nullptr;
        for (auto& d : vars) if (d.name == name) amt = &d;
        v.push_back({"Amount", amt ? amt->base : Dec(0), amt ? amt->base : Dec(0)});
        tk = "powers." + slug(name) + ".title";
        dk = "powers." + slug(name) + ".description";
        break;
      }
      case 'S': tk = "static_hover_tips." + name + ".title"; dk = "static_hover_tips." + name + ".description"; break;
      case 'K': tk = "card_keywords." + name + ".title"; dk = "card_keywords." + name + ".description"; break;
      case 'E': tk = "enchantments." + slug(name) + ".title"; dk = "enchantments." + slug(name) + ".description"; break;
      case 'O': tk = "potions." + slug(name) + ".title"; dk = "potions." + slug(name) + ".description"; break;
      case 'R': tk = "orbs." + slug(name) + ".title"; dk = "orbs." + slug(name) + ".description"; break;
      case 'C': {  // CardHoverTip: the card's name and text
        auto c = db::card(name);
        if (!c || !R().hasLoc("cards." + c->locKey + ".title")) break;
        addTip(out, L("cards." + c->locKey + ".title"), describe(c.get()));
        break;
      }
      default: break;
    }
    if (tk.empty() || !R().hasLoc(tk)) continue;
    addTip(out, expandSmart(L(tk), v, false), R().hasLoc(dk) ? expandSmart(L(dk), v, false) : std::string());
  }
}

// ---------------------------------------------------------------- opening / closing

bool App::detailOpen() const { return detailCard_ || detailRelic_ || detailPotion_; }

void App::closeDetail() {
  Popup& p = P();
  if (p.active) widgets::setFocus(p.savedFocus);
  p = Popup{};
  detailCard_ = nullptr;
  detailRelic_ = nullptr;
  detailPotion_ = nullptr;
  detailUpgrade_ = false;
  detailKeyword_ = -1;
}

static void setItem(const Item& it, sts::Card*& card, sts::Relic*& relic, sts::Potion*& potion, bool& upgrade) {
  card = it.card;
  relic = it.relic;
  potion = it.potion;
  // SetCard: ticked when the card is already upgraded or the list asks for upgrades.
  upgrade = it.card && it.card->maxUpgradeLevel > 0 && (it.card->upgraded() || P().viewAll);
  P().page = 0;
}

void App::inspectCard(const std::vector<Card*>& list, int index, bool upgrade) {
  if (index < 0 || index >= (int)list.size() || !list[index]) return;
  closeDetail();
  Popup& p = P();
  p.viewAll = upgrade;
  for (Card* c : list) p.list.push_back({c, nullptr, nullptr});
  p.index = index;
  setItem(p.list[index], detailCard_, detailRelic_, detailPotion_, detailUpgrade_);
}
void App::inspectCard(Card* c, bool upgrade) { inspectCard(std::vector<Card*>{c}, 0, upgrade); }

void App::inspectRelic(const std::vector<Relic*>& list, int index) {
  if (index < 0 || index >= (int)list.size() || !list[index]) return;
  closeDetail();
  Popup& p = P();
  for (Relic* r : list) p.list.push_back({nullptr, r, nullptr});
  p.index = index;
  setItem(p.list[index], detailCard_, detailRelic_, detailPotion_, detailUpgrade_);
}
void App::inspectRelic(Relic* r) { inspectRelic(std::vector<Relic*>{r}, 0); }
void App::inspectRelics(const std::vector<std::unique_ptr<Relic>>& list, int index) {
  std::vector<Relic*> v;
  for (auto& r : list) v.push_back(r.get());
  inspectRelic(v, index);
}

void App::inspectPotion(const std::vector<Potion*>& list, int index) {
  if (index < 0 || index >= (int)list.size() || !list[index]) return;
  closeDetail();
  Popup& p = P();
  for (Potion* q : list) p.list.push_back({nullptr, nullptr, q});
  p.index = index;
  setItem(p.list[index], detailCard_, detailRelic_, detailPotion_, detailUpgrade_);
}
void App::inspectPotion(Potion* q) { inspectPotion(std::vector<Potion*>{q}, 0); }

// The list only counts while its current entry is what is shown (a caller may have assigned
// detailCard_ directly since).
static bool listValid(Card* c, Relic* r, Potion* q) {
  Popup& p = P();
  if (p.list.size() < 2 || p.index < 0 || p.index >= (int)p.list.size()) return false;
  const Item& it = p.list[p.index];
  return it.card == c && it.relic == r && it.potion == q;
}

// ---------------------------------------------------------------- the shown item and its tips

void App::drawRelicDetail(Relic* r, float cy) {
  const float big = 56;
  gfx::circle(kTop / 2.f, cy, 38, 0xFFE07030);
  drawRelicIcon(r, kTop / 2.f - big / 2, cy - big / 2, big);
  TextStyle nt = ts(F16, col::gold, CENTER);
  nt.scale = 1.2f;
  R().text(kTop / 2.f, cy + 36, L("relics." + r->locKey + ".title"), nt);
  R().text(kTop / 2.f, cy + 60, relicRarityName(r->rarity), ts(F12, col::gray, CENTER));
  R().text(kTop / 2.f, cy + 78, describeRelic(r), ts(F12, col::white, CENTER, kTop - 60));
}

void App::refreshDetail() {
  Popup& p = P();
  const void* key = detailCard_ ? (const void*)detailCard_ : detailRelic_ ? (const void*)detailRelic_ : (const void*)detailPotion_;
  bool up = detailCard_ && detailUpgrade_;
  if (p.key == key && p.keyUpgrade == up && !p.pages.empty()) return;
  p.key = key;
  p.keyUpgrade = up;
  p.shown.reset();
  p.tips.clear();
  p.pages.clear();
  auto desc = [this](Card* c) { return describe(c); };
  if (Card* c = detailCard_) {
    if (up && !c->upgraded() && c->upgradable()) {
      p.shown = c->clone();
      p.shown->upgrade();
    } else if (!up && c->upgraded()) {
      if ((p.shown = db::card(c->id))) {
        p.shown->enchantment = c->enchantment;
        p.shown->adoptEnchantment();
        if (p.shown->enchantment) p.shown->enchantment->modifyCard();
      }
    }
    Card* s = p.shown ? p.shown.get() : c;
    // CardModel.HoverTips: ExtraHoverTips, enchantment, replays, Block, keywords (Ethereal adds Exhaust).
    tableTips(p.tips, "card:" + s->id, s->vars, desc);
    if (Enchantment* e = s->enchantment.get()) {
      std::string k = "enchantments." + e->locKey;
      if (R().hasLoc(k + ".title")) {
        auto v = withAmount(e->vars, e->amount);
        addTip(p.tips, L(k + ".title"), R().hasLoc(k + ".description") ? expandSmart(L(k + ".description"), v, false) : "");
      }
      tableTips(p.tips, "ench:" + e->id, withAmount(e->vars, e->amount), desc);
    }
    if (int times = s->enchantedReplayCount(); times > 0 && R().hasLoc("static_hover_tips.REPLAY_DYNAMIC.title")) {
      std::vector<DynVar> v{{"Times", Dec(times), Dec(times)}};
      addTip(p.tips, expandSmart(L("static_hover_tips.REPLAY_DYNAMIC.title"), v, false),
             expandSmart(L("static_hover_tips.REPLAY_DYNAMIC.description"), v, false));
    }
    if (s->gainsBlock() && R().hasLoc("static_hover_tips.BLOCK.title"))
      addTip(p.tips, L("static_hover_tips.BLOCK.title"), L("static_hover_tips.BLOCK.description"));
    static const std::pair<int, const char*> kws[] = {{kwExhaust, "EXHAUST"}, {kwEthereal, "ETHEREAL"}, {kwInnate, "INNATE"},
                                                      {kwUnplayable, "UNPLAYABLE"}, {kwRetain, "RETAIN"}, {kwSly, "SLY"},
                                                      {kwEternal, "ETERNAL"}};
    auto kw = [&](const char* k) {
      std::string base = std::string("card_keywords.") + k;
      if (R().hasLoc(base + ".title")) addTip(p.tips, L(base + ".title"), L(base + ".description"));
    };
    for (auto& [bit, k] : kws) {
      if (!s->has(bit)) continue;
      kw(k);
      if (bit == kwEthereal) kw("EXHAUST");
    }
  } else if (Relic* r = detailRelic_) {
    tableTips(p.tips, "relic:" + r->id, r->vars, desc);
  } else if (Potion* q = detailPotion_) {
    tableTips(p.tips, "potion:" + q->id, q->vars, desc);
  }
  // Pages of tips that fit the column's height.
  p.pageW = detailCard_ ? 196.f : 172.f;
  float y = kTipTop;
  p.pages.push_back({});
  for (int i = 0; i < (int)p.tips.size(); ++i) {
    float h = tipHeight(p.tips[i], p.pageW);
    if (!p.pages.back().empty() && y + h > kTipBottom) { p.pages.push_back({}); y = kTipTop; }
    p.pages.back().push_back(i);
    y += h + kTipGap;
  }
  if (p.page >= (int)p.pages.size()) p.page = 0;
}

// ---------------------------------------------------------------- drawing

void App::drawDetail(bool top) {
  Popup& p = P();
  if (!p.active) {  // a new session (also when a caller assigned detailCard_ directly)
    Popup fresh;
    fresh.list = std::move(p.list);
    fresh.index = p.index;
    fresh.viewAll = p.viewAll;
    fresh.page = p.page;
    fresh.savedFocus = widgets::focused();
    fresh.active = true;
    p = std::move(fresh);
  }
  refreshDetail();
  Card* card = detailCard_ ? (p.shown ? p.shown.get() : detailCard_) : nullptr;
  const float slide = 1.f - std::pow(1.f - std::min(1.f, p.openT), 3.f);  // ease-out cubic
  const float dy = (1.f - slide) * 12.f;

  if (run_->screen == Screen::Title) drawMenuBg(top, 0.85f);
  else drawSceneBg(top, 0.85f);

  if (top) {
    p.openT = std::min(1.f, p.openT + (float)gfx::dt() / style::kSlide);
    gfx::pushAlpha(slide);
    const bool tips = !p.tips.empty();
    float tipX = 0;
    if (card) {
      const float cw = 120 * 1.3f;
      float cx = tips ? 20.f : (kTop - cw) / 2.f;
      drawCard(card, cx, 10 + dy, 1.3f, false, true);
      tipX = cx + cw + 14;
    } else {
      // Relic / potion: big icon, name, rarity, text, flavour in the left block (C# NInspectRelicScreen).
      const float bw = tips ? 208.f : kTop - 40.f, bx = tips ? 8.f : 20.f, mid = bx + bw / 2;
      const float big = 64, iy = 14 + dy;
      gfx::circle(mid, iy + big / 2, big * 0.62f, 0xFFE07030);
      if (detailRelic_) drawRelicIcon(detailRelic_, mid - big / 2, iy, big);
      else drawPotionIcon(detailPotion_, mid - big / 2, iy, big);
      std::string base = detailRelic_ ? "relics." + detailRelic_->locKey : "potions." + detailPotion_->locKey;
      TextStyle nt = ts(F16, col::gold, CENTER, bw);
      nt.scale = 1.25f;
      float y = iy + big + 10;
      y += R().text(mid, y, L(base + ".title"), nt) + 2;
      std::string rar = detailRelic_ ? relicRarityName(detailRelic_->rarity) : potionRarityName(detailPotion_->rarity);
      y += R().text(mid, y, rar, ts(F12, col::gold, CENTER)) + 8;
      std::string text = detailRelic_ ? describeRelic(detailRelic_) : describePotion(detailPotion_);
      y += R().text(mid, y, text, ts(F12, col::white, CENTER, bw)) + 8;
      if (R().hasLoc(base + ".flavor") && y < kH - 30)
        R().text(mid, y, L(base + ".flavor"), ts(F12, 0xB8A888FF, CENTER, bw));
      tipX = bx + bw + 6;
    }
    if (tips) {
      const auto& page = p.pages[p.page];
      float y = kTipTop + dy;
      for (int i : page) y += drawTip(p.tips[i], tipX, y, p.pageW) + kTipGap;
    }
    gfx::popAlpha();
    return;
  }

  // Bottom: name, type line and text in a panel; prev / next; the action bar.
  gfx::Input in = gfx::input();
  if (!in.touching && !in.touchDown) p.armed = true;
  gfx::Input win;  // the widgets only take touches: keys are handled in updateDetail
  if (p.armed) { win.touching = in.touching; win.touchDown = in.touchDown; win.touchUp = in.touchUp; win.tx = in.tx; win.ty = in.ty; }
  widgets::beginFrame(win);
  gfx::pushAlpha(slide);
  const float px = style::kMargin, pw = kBot - 2 * style::kMargin, py = style::kMargin + dy, ph = 140;
  widgets::panel("ui/hover_tip", px, py, pw, ph);
  std::string name, meta, text;
  if (card) {
    name = cardTitle(card);
    meta = cardTypeName(card->type) + " · " + cardRarityName(card->rarity);
    text = describe(card);
  } else if (detailRelic_) {
    name = L("relics." + detailRelic_->locKey + ".title");
    meta = relicRarityName(detailRelic_->rarity);
    text = describeRelic(detailRelic_);
  } else if (detailPotion_) {
    name = L("potions." + detailPotion_->locKey + ".title");
    meta = potionRarityName(detailPotion_->rarity);
    text = describePotion(detailPotion_);
  }
  TextStyle nt = ts(F16, col::gold, CENTER, pw - 16);
  nt.scale = 1.25f;
  float y = py + 6;
  y += R().text(kBot / 2.f, y, name, nt);
  gfx::rect(kBot / 2.f - 50, y + 1, 100, 2, style::kPanelHi);
  y += 6;
  y += R().text(kBot / 2.f, y, meta, ts(F12, col::gold, CENTER)) + 4;
  TextStyle dt = ts(F16, col::white, CENTER, pw - 20);
  float h = 0, room = py + ph - 6 - y;
  R().measure(text, dt, &h);
  while (h > room && dt.scale > 0.75f) {
    dt.scale *= 0.9f;
    R().measure(text, dt, &h);
  }
  if (h > room) { dt = ts(F12, col::white, CENTER, pw - 20, 0.9f); }
  R().text(kBot / 2.f, y, text, dt);

  // Prev / next (NInspectCardScreen's arrows: shown while there is a neighbour).
  if (listValid(detailCard_, detailRelic_, detailPotion_)) {
    const float ry = py + ph + 6, bw = 96;
    const int n = (int)p.list.size();
    if (widgets::button(kIdPrev, px, ry, bw, style::kButtonH, "L 上一个", widgets::Kind::Secondary, p.index > 0))
      detailStep(-1);
    R().text(kBot / 2.f, ry + 9, num(p.index + 1) + " / " + num(n), ts(F16, col::white, CENTER));
    if (widgets::button(kIdNext, kBot - px - bw, ry, bw, style::kButtonH, "下一个 R", widgets::Kind::Secondary, p.index + 1 < n))
      detailStep(1);
  }
  // Action bar: 关闭 bottom-left, more tips in the middle, the upgrade checkbox at the right.
  if (widgets::button(kIdClose, px, style::kActionY, 100, style::kButtonH, "B 关闭")) { gfx::popAlpha(); widgets::endFrame(); closeDetail(); return; }
  if (p.pages.size() > 1 &&
      widgets::button(kIdPage, 116, style::kActionY, 88, style::kButtonH,
                     "Y 说明 " + num(p.page + 1) + "/" + num((int)p.pages.size())))
    p.page = (p.page + 1) % (int)p.pages.size();
  if (detailCard_ && detailCard_->maxUpgradeLevel > 0) {
    bool v = widgets::toggle(kIdUpgrade, 212, style::kActionY + 1, detailUpgrade_, "升级");
    if (v != detailUpgrade_) detailToggleUpgrade();
  }
  gfx::popAlpha();
  widgets::endFrame();
}

void App::detailStep(int delta) {
  Popup& p = P();
  if (!listValid(detailCard_, detailRelic_, detailPotion_)) return;
  int i = std::clamp(p.index + delta, 0, (int)p.list.size() - 1);
  if (i == p.index) return;
  p.index = i;
  setItem(p.list[i], detailCard_, detailRelic_, detailPotion_, detailUpgrade_);
  sfx::click();
}

void App::detailToggleUpgrade() {
  if (!detailCard_ || detailCard_->maxUpgradeLevel <= 0) return;
  P().viewAll = false;  // ToggleShowUpgrade
  detailUpgrade_ = !detailUpgrade_;
  P().page = 0;
}

void App::updateDetail(const gfx::Input& in) {
  const uint32_t d = in.down;
  if (d & gfx::BTN_B) { sfx::play("event:/sfx/ui/clicks/ui_back"); closeDetail(); return; }
  if (d & (gfx::BTN_LEFT | gfx::BTN_L)) detailStep(-1);
  if (d & (gfx::BTN_RIGHT | gfx::BTN_R)) detailStep(1);
  if (d & (gfx::BTN_X | gfx::BTN_A)) {
    if (detailCard_ && detailCard_->maxUpgradeLevel > 0) detailToggleUpgrade();
    else if (d & gfx::BTN_A) { sfx::play("event:/sfx/ui/clicks/ui_back"); closeDetail(); return; }
  }
  Popup& p = P();
  if ((d & (gfx::BTN_Y | gfx::BTN_DOWN)) && p.pages.size() > 1) p.page = (p.page + 1) % (int)p.pages.size();
  if ((d & gfx::BTN_UP) && p.pages.size() > 1) p.page = (p.page + (int)p.pages.size() - 1) % (int)p.pages.size();
}

}  // namespace ui
