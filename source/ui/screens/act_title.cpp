// S01 (RGDSplus U01): the act title card, NActBanner (scenes/ui/act_banner.tscn). The map screen
// adds it when an act's map first opens (NMapScreen: ActFloor 0, or 1 in act 1 after Neow) and on
// entering a new act (NMapRoom). It does not take input (mouse_filter = ignore), so the map stays
// usable underneath. Layout, from the 1920x1080 scene scaled to the 240 px top screen:
//   Banner   ColorRect across the screen, anchors y 0.398..0.583, black, alpha 0 -> 0.25
//            (0.5 s after 0.5 s)
//   ActName  ActModel.Title (acts.<ID>.title), Spectral Bold 120, #EFC851, centred at y 549;
//            fades in over 1 s after 0.25 s
//   ActNumber gameplay_ui.ACT_NUMBER {actNumber} = index + 1, Kreon 40, #87CEEB, box 54 tall whose
//            top eases 450 -> 440 (quad out, 1.25 s after 0.5 s) while it fades in (1 s after 0.5 s)
//   then a pause of 2 s (0.5 s in fast mode) and the whole card fades out (1 s, quad out).
// Both texts are pre-rendered in the game's own zhs fonts (act/name_<ID>, act/number_<n>,
// build_assets.bake_boot_and_act_titles); plain loc text in the UI font is the fallback.
// There is no per-character line: NActBanner shows only the act number and name (X6 checked the C#
// and the loc tables for act-transition quotes; none exist).
// Two screens (RGDSplus U01 "章节上屏"): the card itself is on the top screen; the bottom screen
// (the map the player acts on, 320 px) gets only a light veil that follows the band's fade, so the
// transition reads across both screens without covering the map controls.
// Port addition: after kSkipMin, A or a tap fades the card out in kSkipOut. The input is not taken
// (the banner never blocks the map in the C#: mouse_filter ignore), so scripted runs behave the same.
#include "../ui_common.h"

namespace ui {

namespace {
constexpr float kK = kH / 1080.f;  // scene units -> top-screen px
constexpr float kIn = 1.75f, kOut = 1.0f;
constexpr float kSkipMin = 0.75f, kSkipOut = 0.35f;
constexpr float kVeil = 0.2f;  // bottom-screen veil at full strength
float skipT = -1;              // actTitleT_ when A / a tap skipped the card; -1 = not skipped

float clamp01(float t) { return std::clamp(t, 0.f, 1.f); }
float easeOutQuad(float t) { return 1.f - (1.f - t) * (1.f - t); }
float holdTime(bool fast) { return fast ? 0.5f : 2.0f; }

// ModelId.Entry of an act: its class name in UPPER_SNAKE_CASE ("Overgrowth" -> "OVERGROWTH").
std::string actEntry(const char* name) {
  std::string out;
  for (const char* p = name; *p; ++p) {
    if (p != name && std::isupper((unsigned char)*p) && std::islower((unsigned char)p[-1])) out += '_';
    out += (char)std::toupper((unsigned char)*p);
  }
  return out;
}

// A pre-rendered title sprite centred at (cx, cy), else the loc text in the UI font.
void drawTitleText(const std::string& sprite, const std::string& text, float cx, float cy, FontSize f, float scale,
                   uint32_t color, float alpha) {
  if (alpha <= 0) return;
  gfx::pushAlpha(alpha);
  Sprite s = R().sprite(sprite);
  if (s) {
    spr(s, std::round(cx - s.w / 2), std::round(cy - s.h / 2));
  } else {
    TextStyle st = ts(f, color, CENTER, 0, scale);
    R().text(cx, cy - R().lineHeight(f) * scale / 2, text, st);
  }
  gfx::popAlpha();
}
}  // namespace

void App::updateActTitle(float dt) {
  const Run& r = *run_;
  if (r.screen == Screen::Title) {  // every run starts (or resumes) from the title
    actTitleAct_ = -1;
    actTitleT_ = -1;
    skipT = -1;
    return;
  }
  if (actTitleT_ >= 0) {
    actTitleT_ += dt;
    const float hold = holdTime(fastMode_);
    // Skip: only while the card is what the player sees (the map, no page over it) and before its
    // own fade-out. gfx::input() is this frame's reading, the same one App::update got.
    bool visible = r.screen == Screen::Map && !mapView_ && !pauseOpen_ && !devOpen_ && !settingsOpen_ &&
                   !deckOpen_ && !relicsOpen_ && !potionsOpen_ && !detailOpen();
    const gfx::Input in = gfx::input();
    if (visible && skipT < 0 && actTitleT_ >= kSkipMin && actTitleT_ < kIn + hold &&
        ((in.down & gfx::BTN_A) || in.touchDown))
      skipT = actTitleT_;
    bool over = skipT >= 0 ? actTitleT_ - skipT >= kSkipOut : actTitleT_ >= kIn + hold + kOut;
    if (over) actTitleT_ = skipT = -1;
  }
  if (r.screen == Screen::Map && !mapView_ && r.actIndex != actTitleAct_) {
    actTitleAct_ = r.actIndex;
    // Only before the act's first room (the start point is nodes[0]): a run resumed mid-act
    // opens its map without the card.
    if (r.currentNode <= 0) {
      actTitleT_ = 0;
      skipT = -1;
    }
  }
}

void App::drawActTitle(bool top) {
  if (actTitleT_ < 0) return;
  const float t = actTitleT_, hold = holdTime(fastMode_);
  float whole = t <= kIn + hold ? 1.f : 1.f - easeOutQuad(clamp01((t - kIn - hold) / kOut));
  if (skipT >= 0) whole = std::min(whole, 1.f - easeOutQuad(clamp01((t - skipT) / kSkipOut)));
  float bandIn = clamp01((t - 0.5f) / 0.5f) * whole;
  if (!top) {  // the veil, inside the bottom screen's own 320 px
    if (bandIn > 0) gfx::rect(0, 0, kBot, kH, (uint32_t)(kVeil * bandIn * 255.f + 0.5f));
    return;
  }
  float bandA = 0.25f * bandIn;
  float nameA = clamp01((t - 0.25f) / 1.0f) * whole;
  float numA = clamp01((t - 0.5f) / 1.0f) * whole;
  float numTop = 450.f - 10.f * easeOutQuad(clamp01((t - 0.5f) / 1.25f));

  if (bandA > 0) {
    float y0 = std::round(0.398f * kH), y1 = std::round(0.583f * kH);
    gfx::rect(0, y0, kTop, y1 - y0, (uint32_t)(bandA * 255.f + 0.5f));
  }
  std::string entry = actEntry(run_->act().name);
  float nameCY = (540.f + 9.f) * kK + 2.f;
  drawTitleText(std::string(english() ? "act/eng_name_" : "act/name_") + entry, L("acts." + entry + ".title"), kTop / 2.f, nameCY, F16, 1.7f, col::gold, nameA);
  // The number is drawn larger than the scene's 9 px, so the name sits 2 px lower to keep them apart.
  int n = run_->actIndex + 1;
  std::string label = L("gameplay_ui.ACT_NUMBER");
  size_t p = label.find("{actNumber}");
  if (p != std::string::npos) label.replace(p, 11, num(n));
  float numCY = (numTop + 27.f) * kK;
  drawTitleText(std::string(english() ? "act/eng_number_" : "act/number_") + num(n), label, kTop / 2.f, numCY, F12, 1.f, col::blue, numA);
}

}  // namespace ui
