// Split from ui.cpp (F3).
#include "../ui_common.h"

namespace ui {

// ================================================================ events

namespace {
constexpr float kOptW = 300, kOptH = 40, kOptGap = 6;  // event option buttons (1.22x native on RGDSplus)
}

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
      bool player = k.size() > 5 && k.compare(k.size() - 5, 5, ".char") == 0;  // the Ironclad answers
      R().text(kTop / 2, kH - 52, L("ancients." + k), ts(F16, player ? col::gold : 0xB8E8FFFF, CENTER, kTop - 24));
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
    } else {  // a locked option (e.g. Orobas without a starter relic): its text, greyed out
      std::string k = e->options[i].key;
      std::string table = R().hasLoc("ancients." + k + ".title") ? "ancients." : "events.";
      R().text(x + 8, y + 3, L(table + k + ".title"), ts(F12, col::gray));
      if (R().hasLoc(table + k + ".description"))
        R().text(x + 8, y + 19, L(table + k + ".description"), ts(F12, col::gray, LEFT, w - 16, 0.85f));
    }
    if (!e->options[i].locked()) hits_.push_back({x, y, w, h, ID_DEVITEM0 + i});
  }
}

void App::drawEvent(bool top) {
  Event* e = run_->currentEvent.get();
  if (e && e->ancient) { drawAncient(top); return; }
  drawSceneBg(top, 0.6f);
  if (!e) return;
  if (top) {
    drawTopBar();
    R().text(kTop / 2, 22, L("events." + e->locKey + ".title"), ts(F16, col::gold, CENTER));
    float y = 42;
    Sprite art = R().sprite("event/" + e->locKey);
    if (art) {
      spr(art, (kTop - art.w) / 2, y, art.w, art.h);
      y += art.h + 4;
    }
    // The page text, shrunk until it fits under the art.
    TextStyle dt = ts(F12, col::white, CENTER, kTop - 24);
    std::string text = expandSmart(L("events." + e->descKey), e->vars, false, &e->strVars);
    float th;
    R().measure(text, dt, &th);
    for (int k = 0; k < 6 && th > kH - y - 4 && dt.scale > 0.6f; ++k) { dt.scale *= 0.9f; R().measure(text, dt, &th); }
    R().text(kTop / 2, y, text, dt);
    return;
  }
  // Options stacked around the middle of the bottom screen (RGDSplus eventOption layout).
  int n = e->finished ? 1 : (int)e->options.size();
  float total = n * kOptH + (n - 1) * kOptGap, y0 = (kH - total) / 2, x = (kBot - kOptW) / 2;
  for (int i = 0; i < n; ++i) {
    float y = y0 + i * (kOptH + kOptGap);
    bool locked = !e->finished && e->options[i].locked();
    bool hl = i == sel_;
    panel(x, y, kOptW, kOptH, locked ? 0x2A2A2AE0 : hl ? 0x8A5A20F0 : 0x3A2E24F0, locked ? 0x555555FF : hl ? 0xFFD870FF : 0xB89A60FF);
    std::map<std::string, std::string> sv = e->strVars;  // the event's placeholders + this option's own
    std::map<std::string, std::string> svDesc;
    if (!e->finished) {
      for (auto& kv : e->options[i].strVars) if (kv.first.rfind("desc.", 0) != 0) sv[kv.first] = kv.second;
      svDesc = sv;
      for (auto& kv : e->options[i].strVars) if (kv.first.rfind("desc.", 0) == 0) svDesc[kv.first.substr(5)] = kv.second;
    }
    std::string title = e->finished ? "继续" : e->options[i].title.empty()
        ? expandSmart(L("events." + e->options[i].key + ".title"), e->vars, false, &sv)
        : expandSmart(R().hasLoc(e->options[i].title) ? L(e->options[i].title) : e->options[i].title, e->vars, false, &sv);
    std::string desc = e->finished ? "" : expandSmart(L("events." + e->options[i].key + ".description"), e->vars, false, &svDesc);
    if (!e->finished && !R().hasLoc("events." + e->options[i].key + ".description")) desc.clear();
    if (desc.empty()) {
      R().text(x + kOptW / 2, y + (kOptH - R().lineHeight(F16)) / 2, title, ts(F16, locked ? col::gray : col::white, CENTER));
    } else {
      R().text(x + 8, y + 3, title, ts(F12, locked ? col::gray : col::gold));
      TextStyle st = ts(F12, locked ? col::gray : col::white, LEFT, kOptW - 16, 0.85f);
      float dh;
      R().measure(desc, st, &dh);
      if (dh > kOptH - 18) st.scale *= (kOptH - 18) / dh;
      R().text(x + 8, y + 19, desc, st);
    }
    if (!locked) hits_.push_back({x, y, kOptW, kOptH, ID_DEVITEM0 + i});
  }
}

void App::updateEvent(const gfx::Input& in) {
  Run& r = *run_;
  Event* e = r.currentEvent.get();
  if (!e || !r.eventChoice.waiting()) return;
  if (e->ancient && ancientTalking(e)) {  // dialogue: A / tap advances a line
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
