// Split from ui.cpp (F3).
#include "../../core/events_crystal.h"
#include <cmath>

#include "../ui_common.h"

namespace ui {

// ================================================================ events

// AncientEventModel (Neow): the Ancient's scene and dialogue on the top screen, the relic
// choices (icon, name, description) on the bottom. A dialogue line with a ".next" key waits
// for a tap before the next one; the last line stays up with the options.
namespace {
bool ancientTalking(const Event* e) { return !e->finished && e->dialogueLine + 1 < e->dialogue.size(); }
}

void App::drawAncient(bool top) {
  Event* e = run_->currentEvent.get();
  std::string who = e->locKey;
  // The engine lists every line an Ancient may say; keep the ones with text.
  e->dialogue.erase(std::remove_if(e->dialogue.begin(), e->dialogue.end(),
                                   [](const std::string& k) { return !R().hasLoc("ancients." + k); }),
                    e->dialogue.end());
  if (top) {
    std::string bg = e->id;  // gfx/bg_<ancient id, lower case>.t3t
    for (char& c : bg) c = (char)std::tolower((unsigned char)c);
    gfx::image(R().texture("gfx/bg_" + bg + ".t3t"), 0, 0, kTop, kH, 0, 0, kTop, kH);
    drawTopBar();
    R().text(12, 22, L("ancients." + who + ".title"), ts(F16, col::gold, LEFT));
    if (R().hasLoc("ancients." + who + ".epithet"))  // TheArchitect has none
      R().text(12, 42, L("ancients." + who + ".epithet"), ts(F12, col::white, LEFT, 0, 0.85f));
    // Selected relic: its description in a box over the lower scene; otherwise the dialogue.
    const Relic* rel = !e->finished && !ancientTalking(e) && sel_ >= 0 && sel_ < (int)e->options.size()
                           ? e->options[sel_].relic.get() : nullptr;
    gfx::rect(0, kH - 62, kTop, 62, 0x000000B0);
    if (rel) {
      Sprite ic = R().sprite("relic/" + rel->locKey);
      if (ic) spr(ic, 10, kH - 56, 40, 40);
      R().text(58, kH - 58, L("relics." + rel->locKey + ".title"), ts(F16, col::gold, LEFT));
      R().text(58, kH - 38, describeRelic(const_cast<Relic*>(rel)), ts(F12, col::white, LEFT, kTop - 66, 0.9f));
    } else if (!e->dialogue.empty()) {
      size_t line = std::min(e->dialogueLine, e->dialogue.size() - 1);
      const std::string& k = e->dialogue[line];
      bool player = k.size() > 5 && k.compare(k.size() - 5, 5, ".char") == 0;  // the character answers
      // The event's vars fill the line (Vakuu's {Visits}).
      R().text(kTop / 2, kH - 52, expandSmart(L("ancients." + k), e->vars, false, &e->strVars), ts(F16, player ? col::gold : 0xB8E8FFFF, CENTER, kTop - 24));
    }
    return;
  }
  drawSceneBg(false, 0.7f);
  if (e->finished || ancientTalking(e)) {
    std::string label = "继续";
    if (!e->finished && R().hasLoc("ancients." + e->dialogue[e->dialogueLine].substr(0, e->dialogue[e->dialogueLine].rfind('.')) + ".next"))
      label = L("ancients." + e->dialogue[e->dialogueLine].substr(0, e->dialogue[e->dialogueLine].rfind('.')) + ".next");
    button((kBot - 140) / 2, 180, 140, 36, label, ID_DEVITEM0, true, true);
    return;
  }
  int n = (int)e->options.size();
  const float h = 52, gap = 8, w = 300;
  float y0 = (kH - n * h - (n - 1) * gap) / 2, x = (kBot - w) / 2;
  for (int i = 0; i < n; ++i) {
    float y = y0 + i * (h + gap);
    bool hl = i == sel_;
    panel(x, y, w, h, hl ? 0x1E4A5AF0 : 0x14283AF0, hl ? 0xFFD870FF : 0x6AA8C0FF);
    Relic* rel = e->options[i].relic.get();
    if (rel) {
      Sprite ic = R().sprite("relic/" + rel->locKey);
      if (ic) spr(ic, x + 6, y + (h - 36) / 2, 36, 36);
      R().text(x + 48, y + 3, L("relics." + rel->locKey + ".title"), ts(F12, col::gold));
      TextStyle st = ts(F12, col::white, LEFT, w - 54, 0.8f);
      std::string desc = describeRelic(rel);
      float dh;
      R().measure(desc, st, &dh);
      if (dh > h - 20) st.scale *= (h - 20) / dh;
      R().text(x + 48, y + 19, desc, st);
    } else {  // a text option: greyed out when locked (Orobas without a starter relic); TheArchitect's PROCEED
      std::string k = e->options[i].key;
      std::string table = R().hasLoc("ancients." + k + ".title") ? "ancients."
                          : R().hasLoc("modifiers." + k + ".title") ? "modifiers."  // M11: Neow's modifier options
                          : "events.";
      uint32_t tc = e->options[i].locked() ? col::gray : col::white;
      R().text(x + 8, y + 3, L(table + k + ".title"), ts(F12, tc));
      if (R().hasLoc(table + k + ".description"))
        R().text(x + 8, y + 19, L(table + k + ".description"), ts(F12, tc, LEFT, w - 16, 0.85f));
    }
    if (!e->options[i].locked()) hits_.push_back({x, y, w, h, ID_DEVITEM0 + i});
  }
}

// S17 (RGDSplus U21, C# NEventLayout / NEventOptionButton): a regular event page. Top screen:
// the title, the event art at the left and the page text at the right (paged with L/R or the
// bottom paginator when it is longer than the column). Bottom screen: one large option button
// per option (title + description, as the C# label "[gold]title[/gold]\n description"); a
// locked option (C# IsLocked: no action) is dimmed with its title in red and its description,
// the lock reason, under it, and cannot be focused or picked. Focusing an option (D-pad, or
// the first tap on it) turns the top screen into the option's page: its full text and the
// cards / relics / potions / enchantments it names, the C# option HoverTips (a card large at
// the left, the rest as tips). A (or a second tap) picks it, X opens the card / relic detail,
// B drops the focus back to the story text.
namespace {
constexpr int kOptId = 2100;              // widget id of option i: kOptId + i
constexpr int kProceedId = 2090, kPageId = 2080;  // 继续 on a finished event; paginator (+1)
constexpr float kColX = 216, kColW = kTop - kColX - style::kMargin;  // top: text column
constexpr float kArtX = style::kMargin, kArtW = 200, kBodyY = 54;
constexpr float kPrevCardW = 120, kPrevCardH = 169;                 // drawCard at s = 1
constexpr float kOptMaxH = 56, kOptMinDescS = 0.85f;

// Page state of the story text (the top pass measures it, the bottom pass draws the arrows).
const Event* pageEvent_ = nullptr;
std::string pageKey_;
int evPage_ = 0, evPages_ = 1;

// What an option offers, for the preview (C# EventOption.HoverTips). The port's options carry no
// hover tips, so they are read back from the option's text: the loc placeholders it fills from
// the event's / option's string vars ("cards.X.title"), and the coloured names in it that are a
// card / relic / potion / enchantment title.
struct Offer {
  enum Kind { CardK, RelicK, PotionK, EnchantK } kind;
  std::string id;
  bool operator==(const Offer& o) const { return kind == o.kind && id == o.id; }
};
struct OfferIndex {
  std::map<std::string, Offer> byTitle, byKey;  // shown title / "cards.X.title" -> offer
};
const OfferIndex& offerIndex() {
  static OfferIndex idx;
  static bool built = false;
  if (built) return idx;
  built = true;
  auto add = [&](Offer::Kind k, const char* table, const std::string& locKey, const std::string& id) {
    std::string key = std::string(table) + "." + locKey + ".title";
    if (!R().hasLoc(key)) return;
    idx.byKey.emplace(key, Offer{k, id});
    idx.byTitle.emplace(R().loc(key), Offer{k, id});  // the first registered wins a shared title
  };
  for (auto& id : db::cardIds()) if (auto c = db::card(id)) add(Offer::CardK, "cards", c->locKey, id);
  for (auto& id : db::relicIds()) if (auto r = db::relic(id)) add(Offer::RelicK, "relics", r->locKey, id);
  for (auto& id : db::potionIds()) if (auto p = db::potion(id)) add(Offer::PotionK, "potions", p->locKey, id);
  for (auto& id : db::enchantmentIds()) if (auto en = db::enchantment(id)) add(Offer::EnchantK, "enchantments", en->locKey, id);
  return idx;
}

// Preview models, made once per id and kept (the detail view holds a pointer to them).
Card* previewCard(const std::string& id) {
  static std::map<std::string, std::unique_ptr<Card>> cache;
  auto& c = cache[id];
  if (!c) c = db::card(id);
  return c.get();
}
Relic* previewRelic(const std::string& id) {
  static std::map<std::string, std::unique_ptr<Relic>> cache;
  auto& r = cache[id];
  if (!r) r = db::relic(id);
  return r.get();
}
Potion* previewPotion(const std::string& id) {
  static std::map<std::string, std::unique_ptr<Potion>> cache;
  auto& p = cache[id];
  if (!p) p = db::potion(id);
  return p.get();
}

struct OptionText { std::string title, desc; bool locked = false; };

// The option's title and description as the C# label shows them (the event's placeholders plus
// the option's own; "desc.<Name>" only for the description).
OptionText optionText(const Event* e, int i) {
  OptionText t;
  if (e->finished) { t.title = "继续"; return t; }
  const EventOption& o = e->options[i];
  t.locked = o.locked();
  std::map<std::string, std::string> sv = e->strVars, svDesc;
  for (auto& kv : o.strVars) if (kv.first.rfind("desc.", 0) != 0) sv[kv.first] = kv.second;
  svDesc = sv;
  for (auto& kv : o.strVars) if (kv.first.rfind("desc.", 0) == 0) svDesc[kv.first.substr(5)] = kv.second;
  t.title = o.title.empty() ? expandSmart(L("events." + o.key + ".title"), e->vars, false, &sv)
                            : expandSmart(R().hasLoc(o.title) ? L(o.title) : o.title, e->vars, false, &sv);
  if (R().hasLoc("events." + o.key + ".description"))
    t.desc = expandSmart(L("events." + o.key + ".description"), e->vars, false, &svDesc);
  return t;
}

std::vector<Offer> offersFor(const Event* e, int i, const OptionText& t) {
  std::vector<Offer> out;
  if (e->finished || i < 0 || i >= (int)e->options.size()) return out;
  const EventOption& o = e->options[i];
  const OfferIndex& idx = offerIndex();
  auto push = [&](const Offer& f) {
    if (std::find(out.begin(), out.end(), f) == out.end()) out.push_back(f);
  };
  if (o.relic) push({Offer::RelicK, o.relic->id});
  // Placeholders in the raw loc text whose string var names a model.
  auto scanVars = [&](const std::string& raw, bool desc) {
    for (size_t p = raw.find('{'); p != std::string::npos; p = raw.find('{', p + 1)) {
      size_t q = raw.find_first_of("}:", p);
      if (q == std::string::npos) break;
      std::string name = raw.substr(p + 1, q - p - 1);
      const std::string* v = nullptr;
      if (desc) { auto it = o.strVars.find("desc." + name); if (it != o.strVars.end()) v = &it->second; }
      if (!v) { auto it = o.strVars.find(name); if (it != o.strVars.end()) v = &it->second; }
      if (!v) { auto it = e->strVars.find(name); if (it != e->strVars.end()) v = &it->second; }
      if (!v) continue;
      auto f = idx.byKey.find(*v);
      if (f != idx.byKey.end()) push(f->second);
    }
  };
  scanVars(o.title.empty() ? L("events." + o.key + ".title") : o.title, false);
  if (R().hasLoc("events." + o.key + ".description")) scanVars(L("events." + o.key + ".description"), true);
  // Coloured names in the shown text ("[red]孢子心灵[/red]").
  auto scanTags = [&](const std::string& s) {
    for (size_t p = s.find('['); p != std::string::npos; p = s.find('[', p + 1)) {
      size_t q = s.find(']', p);
      if (q == std::string::npos) break;
      if (s[p + 1] == '/' || s.compare(p + 1, 5, "icon:") == 0) continue;
      size_t end = s.find('[', q);
      if (end == std::string::npos) break;
      auto f = idx.byTitle.find(s.substr(q + 1, end - q - 1));
      if (f != idx.byTitle.end()) push(f->second);
    }
  };
  scanTags(t.title);
  scanTags(t.desc);
  if (out.size() > 4) out.resize(4);
  return out;
}

}  // namespace

void App::drawEvent(bool top) {
  Event* e = run_->currentEvent.get();
  if (e && e->ancient) { drawAncient(top); return; }
  if (e && crystalSphereGame(*run_)) { drawCrystalSphere(top); return; }
  drawSceneBg(top, 0.6f);
  if (!e) return;
  int n = e->finished ? 1 : (int)e->options.size();
  if (sel_ >= n || (sel_ >= 0 && !e->finished && e->options[sel_].locked())) sel_ = -1;
  bool optionMode = sel_ >= 0 && !e->finished;

  if (top) {
    drawTopBar();
    // Title (UI_STYLE: F16 x 1.25, gold, teal line under it).
    TextStyle tt = ts(F16, col::gold, LEFT, kTop - 2 * style::kMargin, 1.25f);
    R().text(style::kMargin + 4, 22, L("events." + e->locKey + ".title"), tt);
    gfx::rect(style::kMargin, 49, kTop - 2 * style::kMargin, 2, style::kPanelHi);

    // Story text (measured every frame: the page count feeds the bottom paginator).
    std::string text = expandSmart(L("events." + e->descKey), e->vars, false, &e->strVars);
    TextStyle dt = ts(F12, col::white, LEFT, kColW);
    float th = 0;
    R().measure(text, dt, &th);
    float lh = R().lineHeight(F12) * dt.lineGap * dt.scale;
    int linesPerPage = std::max(1, (int)((kH - 2 - kBodyY) / lh));
    float pageH = linesPerPage * lh;
    if (pageEvent_ != e || pageKey_ != e->descKey) { pageEvent_ = e; pageKey_ = e->descKey; evPage_ = 0; }
    evPages_ = std::max(1, (int)std::ceil(th / pageH - 0.01f));
    evPage_ = std::clamp(evPage_, 0, evPages_ - 1);

    auto drawArt = [&] {
      Sprite art = R().sprite("event/" + e->locKey);
      if (art) spr(art, kArtX + (kArtW - art.w) / 2, kBodyY, art.w, art.h);
      if (evPages_ > 1 && !optionMode) {  // where the text continues
        float py = kBodyY + (art ? art.h : 0) + 10;
        R().text(kArtX + kArtW / 2, py, "第 " + num(evPage_ + 1) + " / " + num(evPages_) + " 页",
                 ts(F12, col::gold, CENTER));
        R().text(kArtX + kArtW / 2, py + 16, "L / R 翻页", ts(F12, col::gray, CENTER, 0, 0.85f));
      }
    };

    if (!optionMode) {
      drawArt();
      gfx::pushClip(kColX, kBodyY, kColW, pageH);
      R().text(kColX, kBodyY - evPage_ * pageH, text, dt);
      gfx::popClip();
      return;
    }

    // ---- the focused option's page ----
    // A tip for a relic / potion / enchantment the option names: a hover_tip panel with its
    // icon, name and description. Returns the panel's bottom.
    auto drawTip = [&](const Offer& f, float x, float y, float w, float maxY) -> float {
      int kind = f.kind;
      const std::string& id = f.id;
    std::string title, desc;
    const float ic = 32;
    Relic* rel = nullptr;
    Potion* pot = nullptr;
    if (kind == Offer::RelicK) {
      rel = previewRelic(id);
      if (!rel) return y;
      title = L("relics." + rel->locKey + ".title");
      desc = describeRelic(rel);
    } else if (kind == Offer::PotionK) {
      pot = previewPotion(id);
      if (!pot) return y;
      title = L("potions." + pot->locKey + ".title");
      desc = describePotion(pot);
    } else if (kind == Offer::EnchantK) {
      auto en = db::enchantment(id);
      if (!en) return y;
      title = L("enchantments." + en->locKey + ".title");
      desc = expandSmart(L("enchantments." + en->locKey + ".description"), en->vars, false);
    } else {
      return y;
    }
    bool icon = rel || pot;
    float tx = x + 8 + (icon ? ic + 6 : 0), tw = w - (tx - x) - 8;
    TextStyle dt = ts(F12, col::white, LEFT, tw);
    float dh = 0;
    R().measure(desc, dt, &dh);
    float h = std::min(maxY - y, std::max(icon ? ic + 12 : 0.f, 24 + dh + 6));
    if (h < 30) return y;
    widgets::panel("ui/hover_tip", x, y, w, h);
    gfx::pushClip(x, y, w, h);
    if (rel) spr(R().sprite("relic/" + rel->icon), x + 6, y + 6, ic, ic);  // no counter: not owned yet
    if (pot) drawPotionIcon(pot, x + 6, y + 6, ic);
    R().text(tx, y + 5, title, ts(F16, col::gold, LEFT, tw));
    R().text(tx, y + 24, desc, dt);
    gfx::popClip();
    return y + h;
    };
    OptionText ot = optionText(e, sel_);
    std::vector<Offer> offers = offersFor(e, sel_, ot);
    std::vector<Card*> cards;
    std::vector<Offer> tips;
    for (auto& f : offers) {
      if (f.kind == Offer::CardK && cards.size() < 2) { if (Card* c = previewCard(f.id)) cards.push_back(c); }
      else tips.push_back(f);
    }
    // Left column: the offered card(s) large, else the event art; the other tips go under the
    // option's text.
    const float colH = kH - 6 - kBodyY;
    if (cards.size() == 1) {
      float s = std::min(1.f, colH / kPrevCardH);
      drawCard(cards[0], kArtX + (kArtW - kPrevCardW * s) / 2, kBodyY, s, false, true);
    } else if (cards.size() == 2) {
      float s = std::min(0.8f, (kArtW - 4) / 2 / kPrevCardW);
      for (int k = 0; k < 2; ++k) drawCard(cards[k], kArtX + k * (kArtW / 2 + 2), kBodyY + 8, s, false, true);
    } else {
      drawArt();
    }
    // Right column: the option's full text, then the other tips, then the controls hint.
    TextStyle ht = ts(F16, col::gold, LEFT, kColW - 6);
    TextStyle bt = ts(F12, col::white, LEFT, kColW - 6);
    float hh = 0, bh = 0;
    R().measure(ot.title, ht, &hh);
    if (!ot.desc.empty()) R().measure(ot.desc, bt, &bh);
    float ph = 10 + hh + (bh > 0 ? 4 + bh : 0);
    ph = std::min(ph, kH - 26 - kBodyY);
    widgets::panel("ui/hover_tip", kColX - 4, kBodyY - 4, kColW + 4, ph + 4);
    gfx::pushClip(kColX - 4, kBodyY - 4, kColW + 4, ph + 4);
    R().text(kColX + 2, kBodyY + 1, ot.title, ht);
    if (bh > 0) R().text(kColX + 2, kBodyY + 5 + hh, ot.desc, bt);
    gfx::popClip();
    float y = kBodyY + ph + 6;
    for (size_t k = 0; k < tips.size() && y < kH - 50; ++k)
      y = drawTip(tips[k], kColX - 4, y, kColW + 4, kH - 24) + 4;
    std::string hint = widgets::usingPad() ? "A 选择   B 返回" : "再点一次选择   B 返回";
    if (!cards.empty() || (!tips.empty() && tips[0].kind == Offer::RelicK)) hint += "   X 详情";
    R().text(kColX + kColW / 2, kH - 18, hint, ts(F12, col::gray, CENTER, 0, 0.85f));
    return;
  }

  // ---- bottom: the option buttons ----
  gfx::Input in = pauseOpen_ ? gfx::Input{} : gfx::input();
  {  // focus left over from another screen: drop it (the first D-pad press lands on option 1)
    int fo = widgets::focused();
    bool ours = (fo >= kOptId && fo < kOptId + n) || fo == kProceedId || fo == kPageId || fo == kPageId + 1;
    if (!ours) widgets::setFocus(-1);
  }
  widgets::beginFrame(in);
  int f = widgets::focused() - kOptId;
  if (widgets::usingPad() && f >= 0 && f < n && !e->finished) sel_ = f;
  bool canAct = run_->eventChoice.waiting() && !pauseOpen_;
  const float x = style::kMargin, w = kBot - 2 * style::kMargin;
  float areaTop = style::kMargin, areaBot = evPages_ > 1 ? style::kActionY - style::kGap : kH - style::kMargin;
  int pick = -1;

  if (e->finished) {  // SetEventFinished: a single proceed option in the middle
    float h = 44, y = (areaTop + areaBot - h) / 2;
    if (widgets::optionButton(kProceedId, x + 40, y, w - 80, h, "继续", canAct)) pick = 0;
  } else {
    float h = std::min(kOptMaxH, (areaBot - areaTop - (n - 1) * style::kGap) / n);
    float y0 = areaTop + (areaBot - areaTop - (n * h + (n - 1) * style::kGap)) / 2;
    float pulse = 0.75f + 0.25f * std::sin((float)time_ * 5.f);
    for (int i = 0; i < n; ++i) {
      float y = y0 + i * (h + style::kGap);
      OptionText ot = optionText(e, i);
      bool locked = ot.locked;
      bool focus = i == sel_;
      if (widgets::hit(kOptId + i, x, y, w, h, canAct && !locked)) {
        bool tapFocus = in.touchDown && sel_ != i;  // the first tap only shows the option on top
        sel_ = i;
        if (!tapFocus) pick = i;
      }
      widgets::panel("ui/btn_event", x, y, w, h, locked ? 0x6A6A6AFF : focus ? 0xFFFFFFFF : 0xD8D8D8FF);
      if (focus) {  // the focused option: a soft glow and a pulsing gold outline
        uint32_t c = (style::kFocus & 0xFFFFFF00u) | (uint32_t)(0xFF * pulse);
        gfx::rect(x, y, w, h, style::kSelectedFill);
        gfx::rect(x - 2, y - 2, w + 4, 2, c);
        gfx::rect(x - 2, y + h, w + 4, 2, c);
        gfx::rect(x - 2, y - 2, 2, h + 4, c);
        gfx::rect(x + w, y - 2, 2, h + 4, c);
      }
      gfx::pushClip(x, y, w, h);
      if (ot.desc.empty()) {
        R().text(x + w / 2, y + (h - R().lineHeight(F16)) / 2, ot.title, ts(F16, locked ? col::gray : col::white, CENTER, w - 20));
      } else {
        // C# label: the title in gold (red when locked) over the description; the description
        // shrinks to F12 x 0.85 at most and is cut beyond that (the top screen has it all).
        float titleH = R().lineHeight(F16);
        float ty = y + 3;
        TextStyle st = ts(F12, locked ? col::gray : col::white, LEFT, w - 20);
        float dh = 0;
        R().measure(ot.desc, st, &dh);
        float room = h - titleH - 6;
        if (dh > room) {
          st.scale = std::max(kOptMinDescS, room / dh);
          R().measure(ot.desc, st, &dh);
        }
        if (dh < room) ty += (room - dh) / 2;  // centre the block vertically
        R().text(x + 10, ty, ot.title, ts(F16, locked ? col::red : col::gold, LEFT, w - 20));
        R().text(x + 10, ty + titleH, ot.desc, st);
      }
      gfx::popClip();
      if (locked) spr(R().sprite("ui/stats_lock_m"), x + w - 22, y + 4, 16, 16);
    }
  }
  if (evPages_ > 1) {
    const float pw = 140;
    evPage_ = widgets::paginator(kPageId, (kBot - pw) / 2, style::kActionY, pw, evPage_, evPages_);
  }
  widgets::endFrame();
  if (pick >= 0 && canAct && (e->finished || !e->options[pick].locked())) {
    sel_ = -1;
    widgets::setFocus(-1);
    run_->eventChoice.fire(pick);
  }
}

void App::updateEvent(const gfx::Input& in) {
  Run& r = *run_;
  Event* e = r.currentEvent.get();
  if (e && crystalSphereGame(r)) { updateCrystalSphere(in); return; }
  if (!e || !r.eventChoice.waiting()) return;
  if (e->ancient) {  // the Ancient layout keeps its own input (drawAncient's hit boxes)
    if (ancientTalking(e)) {  // dialogue: A / tap advances a line
      if ((in.down & gfx::BTN_A) || (in.touchDown && hitAt(in.tx, in.ty) == ID_DEVITEM0)) e->dialogueLine++;
      return;
    }
    int n = e->finished ? 1 : (int)e->options.size();
    if (in.down & gfx::BTN_DOWN) sel_ = std::min(n - 1, sel_ + 1);
    if (in.down & gfx::BTN_UP) sel_ = std::max(0, sel_ - 1);
    int pick = -1;
    if ((in.down & gfx::BTN_A) && sel_ >= 0) pick = sel_;
    if (in.touchDown) {
      int id = hitAt(in.tx, in.ty);
      if (id >= ID_DEVITEM0 && id < ID_DEVITEM0 + n) pick = id - ID_DEVITEM0;
    }
    if (pick >= 0 && (e->finished || !e->options[pick].locked())) {
      sel_ = -1;
      r.eventChoice.fire(pick);
    }
    return;
  }
  // Regular events: picking and focus are the widgets' (drawEvent); the extra keys here.
  if (in.down & gfx::BTN_L) evPage_ = std::max(0, evPage_ - 1);
  if (in.down & gfx::BTN_R) evPage_ = std::min(evPages_ - 1, evPage_ + 1);
  int n = e->finished ? 0 : (int)e->options.size();
  if ((in.down & gfx::BTN_X) && sel_ >= 0 && sel_ < n) {  // the offered card / relic, large
    OptionText ot = optionText(e, sel_);
    for (auto& f : offersFor(e, sel_, ot)) {
      if (f.kind == Offer::CardK) { detailCard_ = previewCard(f.id); break; }
      if (f.kind == Offer::RelicK) { detailRelic_ = previewRelic(f.id); break; }
    }
    detailUpgrade_ = false;
    return;
  }
  if ((in.down & gfx::BTN_B) && sel_ >= 0) { sel_ = -1; widgets::setFocus(-1); }
}

void App::drawDeckChoice(bool top) {
  DeckChoice& d = run_->deckChoice;
  int n = (int)d.options.size();
  if (top) {
    drawSceneBg(true, 0.7f);
    drawTopBar();
    std::string prompt = R().hasLoc(d.prompt) ? L(d.prompt) : d.prompt;
    for (size_t p; (p = prompt.find("{Amount}")) != std::string::npos;) prompt.replace(p, 8, num(d.count));
    R().text(kTop / 2, 22, prompt, ts(F16, col::white, CENTER));
    if (sel_ >= 0 && sel_ < n) {
      Card* c = d.options[sel_];
      if (d.showUpgrade && c->upgradable()) {
        auto up = c->clone();
        up->upgrade();
        drawCard(c, 50, 44, 1.05f, false, true);
        R().text(kTop / 2, 120, "→", ts(F16, col::gold, CENTER, 0, 2.f));
        drawCard(up.get(), 224, 44, 1.05f, false, true);
      } else {
        drawCard(c, (kTop - 132) / 2, 44, 1.1f, false, true);
      }
    }
    return;
  }
  drawSceneBg(false, 0.65f);
  drawCardGrid(d.options, sel_, 0, 196, scroll_);
  for (int i : deckPicks_) {  // multi-picks: a gold tick on each chosen card
    int row = i / 5 - scroll_;
    const float s = 0.46f, cw = 120 * s, ch = 169 * s, gap = (kBot - 5 * cw) / 6;
    float x = gap + (i % 5) * (cw + gap), y = 6 + row * (ch + 8);
    if (y >= 0 && y < 196) gfx::circle(x + cw - 6, y + 6, 6, 0xFFD870FF);
  }
  gfx::rect(0, 196, kBot, 44, 0x000000A0);
  if (d.canCancel) button(10, 200, 100, 34, "取消", ID_BACK);
  int need = std::min(d.count, n);
  int least = d.minCount >= 0 ? std::min(d.minCount, need) : need;  // "up to N" choices
  bool multi = d.count > 1 || d.minCount == 0;
  bool ready = !multi ? (sel_ >= 0 && sel_ < n) : ((int)deckPicks_.size() >= least && (int)deckPicks_.size() <= need);
  button(kBot - 110, 200, 100, 34, "确认", ID_CONFIRM, ready, true);
}

void App::updateDeckChoice(const gfx::Input& in) {
  DeckChoice& d = run_->deckChoice;
  if (!d.result.waiting()) return;
  int n = (int)d.options.size();
  auto finish = [&](std::vector<Card*> picked) {
    deckPicks_.clear();
    sel_ = -1;
    scroll_ = 0;
    d.result.fire(std::move(picked));
  };
  auto confirm = [&] {
    int least = d.minCount >= 0 ? std::min(d.minCount, std::min(d.count, n)) : std::min(d.count, n);
    if (d.count <= 1 && d.minCount != 0) { if (sel_ >= 0 && sel_ < n) finish({d.options[sel_]}); return; }
    if ((int)deckPicks_.size() < least || (int)deckPicks_.size() > std::min(d.count, n)) return;
    std::vector<Card*> picked;
    for (int i : deckPicks_) picked.push_back(d.options[i]);
    finish(std::move(picked));
  };
  auto toggle = [&](int i) {
    auto it = std::find(deckPicks_.begin(), deckPicks_.end(), i);
    if (it != deckPicks_.end()) deckPicks_.erase(it);
    else if ((int)deckPicks_.size() < d.count) deckPicks_.push_back(i);
  };
  if (in.down & gfx::BTN_RIGHT) sel_ = std::min(n - 1, sel_ + 1);
  if (in.down & gfx::BTN_LEFT) sel_ = std::max(0, sel_ - 1);
  if (in.down & gfx::BTN_DOWN) sel_ = std::min(n - 1, sel_ + 5);
  if (in.down & gfx::BTN_UP) sel_ = std::max(0, sel_ - 5);
  if (sel_ >= 0) scroll_ = std::max(0, sel_ / 5 - 1);
  if ((in.down & gfx::BTN_B) && d.canCancel) { finish({}); return; }
  if (in.down & gfx::BTN_A) {
    if (d.count > 1 && sel_ >= 0 && (int)deckPicks_.size() < d.count) toggle(sel_);
    else confirm();
    return;
  }
  if (!in.touchDown) return;
  int id = hitAt(in.tx, in.ty);
  if (id == ID_BACK && d.canCancel) { finish({}); return; }
  if (id == ID_CONFIRM) { confirm(); return; }
  if (id >= ID_GRID0 && id - ID_GRID0 < n) {
    int i = id - ID_GRID0;
    if (d.count > 1) toggle(i);
    else if (sel_ == i) { confirm(); return; }
    sel_ = i;
  }
}

}  // namespace ui
