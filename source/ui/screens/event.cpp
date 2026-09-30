// Split from ui.cpp (F3).
#include "../../core/events_crystal.h"
#include <cmath>

#include "../ui_common.h"

namespace ui {

// ================================================================ events

// AncientEventModel: true while the dialogue has lines left before the options.
namespace {
bool ancientTalking(const Event* e) { return !e->finished && e->dialogueLine + 1 < e->dialogue.size(); }
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

// S06 (RGDSplus U06, C# NAncientEventLayout / NAncientDialogueLine / NAncientNameBanner): the
// Ancient layout. Top screen: the Ancient's scene, its name banner, and the dialogue as speech
// bubbles in the speaker's colour (C# DialogueColor) with the speaker's icon and name -- the
// Ancient at the left, the character answering at the right. A new line fades in and pushes
// the older ones up (content tween: 1 s expo-out; the C# has no typewriter), older lines stay at
// 25 % alpha. While a line is waiting, the bottom screen shows the "next" button with the line's
// NextButtonText and the whole bottom screen (A, or a tap) advances. On the last line the option
// buttons come up (C# NEventOptionButton in the ancient style): the relic's icon, name and full
// description (the cost is never only on the top screen); a locked option is dimmed with its
// reason in red. Focusing an option (D-pad, or the first tap) shows its relic large on the top
// screen with name, rarity and description; A or a second tap picks it, X opens the relic
// detail, B drops the focus. The modifiers' Neow pages (M11) are text options in the same list.
namespace {
constexpr int kAncOptId = 2200, kAncProceedId = 2190;  // option i: kAncOptId + i
constexpr float kAncOptMaxH = 60, kAncBubbleW = 300;
constexpr double kAncSlide = 1.0, kAncFade = 0.2, kAncOptDelay = 0.3;

// Reveal clock: when the current dialogue line / the options appeared.
const Event* ancEv_ = nullptr;
size_t ancLine_ = (size_t)-1;
bool ancOpts_ = false, ancAutoFocus_ = false;  // ancAutoFocus_: the first option still to take the pad focus
double ancLineT_ = 0, ancOptsT_ = 0;

uint32_t hexColor(uint32_t rgb, uint32_t a) { return (rgb << 8) | a; }
// A white 9-slice art recoloured to `rgba` (Godot Modulate / SelfModulate on white art).
void nineTint(const std::string& sprite, float x, float y, float w, float h, uint32_t rgba) {
  Sprite s = R().sprite(sprite);
  if (!s) { gfx::rect(x, y, w, h, rgba); return; }
  gfx::nineSlice(s.tex, s.x, s.y, s.w, s.h, (float)s.nl, (float)s.nt, (float)s.nr, (float)s.nb, x, y, w, h, rgba, 1.f);
}
// AncientEventModel.ButtonColor (the option plate's Modulate), alpha raised a little: the
// bottom screen is small and the scene under it busy.
uint32_t ancientButtonColor(const std::string& id) {
  static const std::map<std::string, uint32_t> kCol = {
      {"Neow", 0x001A3380}, {"Nonupeipe", 0x001A29BF}, {"Darv", 0x0F001480}, {"Tezcatara", 0x140A00BF},
      {"Orobas", 0x0D001A59}, {"Pael", 0x081400BF}, {"Tanx", 0x0D050080}, {"Vakuu", 0x0D0F1FCC}};
  auto it = kCol.find(id);
  uint32_t c = it == kCol.end() ? 0x00000059 : it->second;
  uint32_t a = std::min<uint32_t>(0xE6u, (c & 0xFFu) + 0x50u);
  return (c & 0xFFFFFF00u) | a;
}
// AncientEventModel.DialogueColor (TheArchitect keeps the default).
uint32_t ancientDialogueColor(const std::string& id) {
  static const std::map<std::string, uint32_t> kCol = {
      {"Neow", 0x28454F}, {"Vakuu", 0x3C1931}, {"Tezcatara", 0x33251E}, {"Pael", 0x332C29},
      {"Tanx", 0x731717}, {"Nonupeipe", 0x0A494D}, {"Orobas", 0x5C5F7A}, {"Darv", 0x512E66}};
  auto it = kCol.find(id);
  return it == kCol.end() ? 0x28454F : it->second;
}
// CharacterModel.DialogueColor.
uint32_t characterDialogueColor(const std::string& id) {
  static const std::map<std::string, uint32_t> kCol = {
      {"Ironclad", 0x590700}, {"Silent", 0x284719}, {"Defect", 0x13446B}, {"Regent", 0x52371D},
      {"Necrobinder", 0x6B4658}};
  auto it = kCol.find(id);
  return it == kCol.end() ? 0x28454F : it->second;
}
std::string lowerId(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}
bool isCharLine(const std::string& k) { return k.size() > 5 && k.compare(k.size() - 5, 5, ".char") == 0; }
// The line's NextButtonText ("<line base>.next"), empty if none.
std::string nextLabel(const std::string& k) {
  std::string key = "ancients." + k.substr(0, k.rfind('.')) + ".next";
  return R().hasLoc(key) ? L(key) : "";
}
float easeOutExpo(float t) { return t >= 1 ? 1.f : 1.f - std::pow(2.f, -10.f * t); }

std::string relicRarityName(RelicRarity r) {
  switch (r) {
    case RelicRarity::Starter: return L("gameplay_ui.RELIC_RARITY.STARTER");
    case RelicRarity::Common: return L("gameplay_ui.RELIC_RARITY.COMMON");
    case RelicRarity::Uncommon: return L("gameplay_ui.RELIC_RARITY.UNCOMMON");
    case RelicRarity::Rare: return L("gameplay_ui.RELIC_RARITY.RARE");
    case RelicRarity::Shop: return L("gameplay_ui.RELIC_RARITY.SHOP");
    case RelicRarity::Event: return L("gameplay_ui.RELIC_RARITY.EVENT");
    case RelicRarity::Ancient: return L("gameplay_ui.RELIC_RARITY.ANCIENT");
    default: return L("gameplay_ui.RELIC_RARITY.NONE");
  }
}

// A text option's loc table: the Ancient's own, the modifiers' (M11 Neow pages), else events.
std::string ancientOptionTable(const std::string& k) {
  return R().hasLoc("ancients." + k + ".title")     ? "ancients."
         : R().hasLoc("modifiers." + k + ".title") ? "modifiers."
                                                   : "events.";
}
}  // namespace

void App::drawAncient(bool top) {
  Event* e = run_->currentEvent.get();
  std::string who = e->locKey;
  // The engine lists every line an Ancient may say; keep the ones with text.
  e->dialogue.erase(std::remove_if(e->dialogue.begin(), e->dialogue.end(),
                                   [](const std::string& k) { return !R().hasLoc("ancients." + k); }),
                    e->dialogue.end());
  bool talking = ancientTalking(e);
  bool options = !e->finished && !talking;
  int n = e->finished ? 1 : (int)e->options.size();
  if (!options || sel_ >= n || (sel_ >= 0 && e->options[sel_].locked())) sel_ = -1;
  // Reveal clock.
  if (ancEv_ != e) { ancEv_ = e; ancLine_ = (size_t)-1; ancOpts_ = false; }
  if (ancLine_ != e->dialogueLine) { ancLine_ = e->dialogueLine; ancLineT_ = time_; }
  if (options != ancOpts_) { ancOpts_ = options; ancOptsT_ = time_; ancAutoFocus_ = options; }
  float age = (float)(time_ - ancLineT_);

  // An option's relic and its title / description (relic or text option).
  auto optionRelic = [&](int i) -> Relic* {
    return !e->finished && i >= 0 && i < (int)e->options.size() ? e->options[i].relic.get() : nullptr;
  };
  auto optionTitle = [&](int i) -> std::string {
    if (Relic* rel = optionRelic(i)) return L("relics." + rel->locKey + ".title");
    const std::string& k = e->options[i].key;
    return L(ancientOptionTable(k) + k + ".title");
  };
  auto optionDesc = [&](int i) -> std::string {
    if (Relic* rel = optionRelic(i)) return describeRelic(rel);
    const std::string& k = e->options[i].key;
    std::string t = ancientOptionTable(k);
    return R().hasLoc(t + k + ".description") ? expandSmart(L(t + k + ".description"), e->vars, false, &e->strVars)
                                              : "";
  };
  gfx::Texture* scene = R().texture("gfx/bg_" + lowerId(e->id) + ".t3t");

  if (top) {
    gfx::image(scene, 0, 0, kTop, kH, 0, 0, kTop, kH);
    drawTopBar();
    // NAncientNameBanner: the name large, the epithet under it (TheArchitect has none).
    gfx::gradient(0, 18, kTop, 44, 0x000000A0, 0x00000000, 0x00000060, 0x00000000);
    R().text(style::kMargin + 4, 20, L("ancients." + who + ".title"), ts(F16, col::gold, LEFT, 0, 1.25f));
    if (R().hasLoc("ancients." + who + ".epithet"))
      R().text(style::kMargin + 4, 43, L("ancients." + who + ".epithet"), ts(F12, col::white, LEFT, 0, 0.85f));

    if (options && sel_ >= 0) {  // ---- the focused option, large
      // Sized to its text and standing on the hint line, so the scene stays visible above it.
      const float px = style::kMargin, pw = kTop - 2 * style::kMargin, maxH = kH - 24 - 62, ic = 72;
      Relic* rel = optionRelic(sel_);
      float tx = rel ? px + 10 + ic + 10 : px + 12, tw = px + pw - 12 - tx;
      float head = rel ? 52 : 34;
      std::string desc = optionDesc(sel_);
      TextStyle dt = ts(F12, col::white, LEFT, tw);
      float dh = 0;
      if (!desc.empty()) R().measure(desc, dt, &dh);
      if (head + dh + 16 > maxH) {
        dt.scale = std::max(0.85f, (maxH - head - 16) / dh);
        R().measure(desc, dt, &dh);
      }
      float ph = std::min(maxH, std::max(rel ? ic + 20 : 0.f, head + dh + 16)), py = kH - 24 - ph;
      widgets::panel("ui/hover_tip", px, py, pw, ph);
      gfx::pushClip(px, py, pw, ph);
      if (rel) spr(R().sprite("relic/" + rel->icon), px + 10, py + 10, ic, ic);  // no counter: not owned yet
      R().text(tx, py + 8, optionTitle(sel_), ts(F16, col::gold, LEFT, tw, 1.25f));
      if (rel) R().text(tx, py + 34, relicRarityName(rel->rarity), ts(F12, col::gold, LEFT, tw, 0.85f));
      R().text(tx, py + head, desc, dt);
      gfx::popClip();
      std::string hint = widgets::usingPad() ? "A 选择   B 返回" : "再点一次选择   B 返回";
      if (rel) hint += "   X 详情";
      R().text(kTop / 2, kH - 18, hint, ts(F12, col::gray, CENTER, 0, 0.85f));
      return;
    }

    // ---- the dialogue, newest at the bottom
    if (e->dialogue.empty()) return;
    size_t cur = std::min(e->dialogueLine, e->dialogue.size() - 1);
    const Character& ch = run_->character();
    const float clipY = 60, bottom = kH - 8, gap = 6;
    struct Bubble { std::string text; bool player; float w, h; };
    auto bubble = [&](size_t i) {
      Bubble b;
      const std::string& k = e->dialogue[i];
      b.player = isCharLine(k);
      b.text = expandSmart(L("ancients." + k), e->vars, false, &e->strVars);  // Vakuu's {Visits}
      float th = 0, tw = R().measure(b.text, ts(F16, col::white, LEFT, kAncBubbleW - 24), &th);
      b.w = std::max(120.f, std::min(kAncBubbleW, tw + 24));
      b.h = th + 16 + 14;  // the speaker's name over the text
      return b;
    };
    Bubble last = bubble(cur);
    float slide = (1.f - easeOutExpo(age / (float)kAncSlide)) * (last.h + gap);
    float y = bottom + slide;
    Sprite tail = R().sprite("ui/dialogue_tail");
    gfx::pushClip(0, clipY, kTop, kH - clipY);
    for (size_t i = cur + 1; i-- > 0 && y > clipY;) {
      Bubble b = i == cur ? last : bubble(i);
      y -= b.h;
      gfx::pushAlpha(i == cur ? std::min(1.f, age / (float)kAncFade) : 0.25f);
      uint32_t bcol = hexColor(b.player ? characterDialogueColor(ch.id) : ancientDialogueColor(e->id), 0xEE);
      const float ic = 32;
      // Ancient: icon left, bubble right of it, tail pointing left; the character mirrored.
      float bx = b.player ? kTop - style::kMargin - ic - 10 - b.w : style::kMargin + ic + 10;
      float icx = b.player ? kTop - style::kMargin - ic : style::kMargin;
      Sprite icon = b.player ? R().sprite("ui/char_" + ch.energyColor) : R().sprite("map/ancient_" + lowerId(e->id));
      if (icon) spr(icon, icx, y + b.h - ic - 2, ic, ic);
      nineTint("ui/nine_dialogue", bx, y, b.w, b.h, bcol);
      if (tail) {
        float ty = y + b.h - tail.h - 6;
        if (b.player) {  // the tail mirrored about the bubble's right edge
          gfx::Affine m;
          m.a = -1;
          m.tx = 2 * (bx + b.w);
          gfx::pushTransform(m);
          spr(tail, bx + b.w - tail.w + 2, ty, tail.w, tail.h, bcol, 1.f);
          gfx::popTransform();
        } else {
          spr(tail, bx - tail.w + 2, ty, tail.w, tail.h, bcol, 1.f);
        }
      }
      std::string name = b.player ? L("characters." + ch.key + ".title") : L("ancients." + who + ".title");
      R().text(bx + 12, y + 5, name, ts(F12, col::gold, LEFT, b.w - 24, 0.85f));
      R().text(bx + 12, y + 19, b.text, ts(F16, col::white, LEFT, kAncBubbleW - 24));
      gfx::popAlpha();
      y -= gap;
    }
    gfx::popClip();
    return;
  }

  // ---- bottom: the scene continues, dimmed (RGDSplus U06 reuses the scene background)
  gfx::image(scene, kBotOX, 0, kBot, kH, 0, 0, kBot, kH);
  gfx::rect(0, 0, kBot, kH, 0x000000B4);
  bool canAct = run_->eventChoice.waiting() && !pauseOpen_;
  // The options take input once they are up (C# grabs focus 0.8 s after the last line).
  gfx::Input in = pauseOpen_ || (options && time_ - ancOptsT_ < kAncOptDelay) ? gfx::Input{} : gfx::input();
  {  // focus left over from another screen: drop it
    int fo = widgets::focused();
    bool ours = (fo >= kAncOptId && fo < kAncOptId + n) || fo == kAncProceedId;
    if (!ours) widgets::setFocus(-1);
  }
  widgets::beginFrame(in);
  const float x = style::kMargin, w = kBot - 2 * style::kMargin;
  int pick = -1;
  const uint32_t plateCol = ancientButtonColor(e->id);
  float pulse = 0.75f + 0.25f * std::sin((float)time_ * 5.f);
  uint32_t ringCol = (style::kFocus & 0xFFFFFF00u) | (uint32_t)(0xFF * pulse);
  if (talking) {
    // NAncientDialogueHitbox + FakeNextButton: the whole screen advances (updateEvent); the
    // button shows the line's reply.
    std::string label = nextLabel(e->dialogue[e->dialogueLine]);
    if (label.empty()) label = "继续";
    const float bw = 180, bh = 44, bx = (kBot - bw) / 2, by = kH - bh - 30;
    nineTint("ui/btn_ancient", bx, by, bw, bh, plateCol);
    nineTint("ui/btn_ancient_outline", bx, by, bw, bh, ringCol);  // the only thing to press
    R().text(kBot / 2, by + (bh - R().lineHeight(F16)) / 2, label, ts(F16, col::white, CENTER, bw - 16));
    R().text(kBot / 2, kH - 22, "A / 点击屏幕 继续", ts(F12, col::gray, CENTER, 0, 0.85f));
  } else if (e->finished) {
    float h = 44, y = (kH - h) / 2, bx = x + 40, bw = w - 80;
    if (widgets::hit(kAncProceedId, bx, y, bw, h, canAct)) pick = 0;
    nineTint("ui/btn_ancient", bx, y, bw, h, plateCol);
    nineTint("ui/btn_ancient_outline", bx, y, bw, h,
             widgets::usingPad() && widgets::focused() == kAncProceedId ? ringCol : style::kPanelEdge);
    R().text(kBot / 2, y + (h - R().lineHeight(F16)) / 2, "继续", ts(F16, col::white, CENTER));
  } else {
    // C# DefaultFocusedControl: with the D-pad the first open option takes the focus.
    if (ancAutoFocus_ && widgets::usingPad() && widgets::focused() < 0)
      for (int i = 0; i < n; ++i)
        if (!e->options[i].locked()) { widgets::setFocus(kAncOptId + i); break; }
    if (widgets::focused() >= 0) ancAutoFocus_ = false;
    int f = widgets::focused() - kAncOptId;
    if (widgets::usingPad() && f >= 0 && f < n) sel_ = f;
    const float top0 = style::kMargin, bot0 = kH - style::kMargin;
    float h = std::min(kAncOptMaxH, (bot0 - top0 - (n - 1) * style::kGap) / std::max(1, n));
    float y0 = top0 + (bot0 - top0 - (n * h + (n - 1) * style::kGap)) / 2;
    float appear = std::min(1.f, (float)(time_ - ancOptsT_) / 0.25f);  // AnimateButtonsIn
    gfx::pushAlpha(appear);
    for (int i = 0; i < n; ++i) {
      float y = y0 + i * (h + style::kGap) + (1 - appear) * 12;
      bool locked = e->options[i].locked();
      bool focus = i == sel_;
      if (widgets::hit(kAncOptId + i, x, y, w, h, canAct && !locked)) {
        bool tapFocus = in.touchDown && sel_ != i;  // the first tap only shows the relic on top
        sel_ = i;
        if (!tapFocus) pick = i;
      }
      // SetVisuallyLocked: the plate desaturated and darker, the label at 70 %.
      nineTint("ui/btn_ancient", x, y, w, h, locked ? 0x262626B0 : plateCol);
      if (focus) {  // NEventOptionButton focus: a lighter plate and the outline
        nineTint("ui/btn_ancient", x, y, w, h, 0xFFD87030);
        nineTint("ui/btn_ancient_outline", x - 2, y - 2, w + 4, h + 4, ringCol);
      }
      if (locked) gfx::pushAlpha(0.7f);
      gfx::pushClip(x, y, w, h);
      Relic* rel = optionRelic(i);
      float tx = x + 10;
      if (rel) {
        float ic = std::min(40.f, h - 10);
        spr(R().sprite("relic/" + rel->icon), x + 6, y + (h - ic) / 2, ic, ic, locked ? 0x808080FF : 0xFFFFFFFF);
        tx = x + 6 + ic + 6;
      }
      float tw = x + w - 8 - tx;
      std::string desc = optionDesc(i);
      float titleH = R().lineHeight(F16);
      TextStyle st = ts(F12, locked ? col::red : col::white, LEFT, tw);  // a locked option's text is its reason
      float dh = 0;
      if (!desc.empty()) R().measure(desc, st, &dh);
      float room = h - titleH - 6;
      if (dh > room) {
        st.scale = std::max(0.85f, room / dh);
        R().measure(desc, st, &dh);
      }
      float ty = desc.empty() ? y + (h - titleH) / 2 : y + 3 + (dh < room ? (room - dh) / 2 : 0);
      bool centred = desc.empty() && !rel;  // a title-only option (TheArchitect's PROCEED)
      R().text(centred ? x + w / 2 : tx, ty, optionTitle(i),
               ts(F16, locked ? col::red : col::gold, centred ? CENTER : LEFT, tw));  // C# [red] / [gold]
      if (!desc.empty()) R().text(tx, ty + titleH, desc, st);
      gfx::popClip();
      if (locked) {
        spr(R().sprite("ui/stats_lock_m"), x + w - 22, y + 4, 16, 16);
        gfx::popAlpha();
      }
    }
    gfx::popAlpha();
  }
  widgets::endFrame();
  if (pick >= 0 && canAct && (e->finished || !e->options[pick].locked())) {
    sel_ = -1;
    widgets::setFocus(-1);
    run_->eventChoice.fire(pick);
  }
}

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
  if (e->ancient) {  // the Ancient layout (drawAncient) picks with the widgets; the extra keys here
    if (ancientTalking(e)) {  // NAncientDialogueHitbox: A or a tap anywhere advances a line
      if ((in.down & gfx::BTN_A) || in.touchDown) e->dialogueLine++;
      return;
    }
    int n = e->finished ? 0 : (int)e->options.size();
    if ((in.down & gfx::BTN_X) && sel_ >= 0 && sel_ < n && e->options[sel_].relic) {
      detailRelic_ = previewRelic(e->options[sel_].relic->id);
      detailUpgrade_ = false;
      return;
    }
    if ((in.down & gfx::BTN_B) && sel_ >= 0) { sel_ = -1; widgets::setFocus(-1); }
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

// drawDeckChoice / updateDeckChoice: screens/deck.cpp (S13).

}  // namespace ui
