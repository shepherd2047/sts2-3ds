// Split from ui.cpp (F3). S18: rebuilt on the widget kit (RGDSplus U22, C# NRestSiteRoom).
#include "../ui_common.h"
#include "combat_internal.h"

namespace ui {

// ================================================================ rest

// S18 (RGDSplus U22, C# NRestSiteRoom / NRestSiteButton / *RestSiteOption): the act's campfire
// on the top screen (gfx/bg_rest_<act>.t3t, baked from scenes/rest_site/<act>_rest_site.tscn by
// build_assets.py) with a flickering flame and the character's Spine idle beside it; the options
// Run::restSite offers (Run::restOptions: heal, smith, and Lift / Dig / Cook / Kindle / Clone from
// relics) as rows on the bottom, each with its icon, name and description. A disabled option
// stays focusable and says why (C#: "You can still hover it for info"). The focused option's
// full description is on the top screen. Touch: a tap on an option chooses it (owner decision
// 2026-09-30: one tap picks; a tap on a disabled one focuses it, showing why); D-pad + A,
// 确认 chooses the focused one.
// After an option the result stays on screen until 继续 (C# ShowProceedButton): heal and
// clone hold the rules' short post-option wait (the scheduler is paused, then skipped), Lift and
// Kindle are applied on 继续; smith opens the deck upgrade grid, whose pick is shown the same
// way. The rules (Run::restSite) are unchanged.
namespace {
constexpr int kOptId = 200;  // widget id of option o: kOptId + o
constexpr int kLeaveId = 290, kConfirmId = 291, kProceedId = 292;
constexpr const char* kOptKeys[] = {"HEAL", "SMITH", "LIFT", "DIG", "COOK", "KINDLE", "CLONE", "HATCH"};
constexpr const char* kOptIcons[] = {"heal", "smith", "lift", "dig", "cook", "kindle", "clone", "hatch"};
// The strip right of the scene in bg_rest_*.t3t (build_assets.py REST_FLAME / REST_GLOW).
constexpr float kFlameSrc[4] = {400, 0, 48, 72};
constexpr float kGlowSrc[4] = {400, 80, 96, 96};
constexpr float kFireX = 200, kFireY = 172;  // the fire pit in the baked scene (all four acts)
constexpr float kCharX = 112, kCharFeet = 198;

// The result of the option just chosen (see the header comment).
struct RestFx {
  int opt = -1;                 // option whose result is on screen, -1 none
  bool hold = false;            // the rules' wait after the option is paused until 继续
  bool fireOnProceed = false;   // Lift / Kindle: fired on 继续
  bool skip = false;            // one frame of fast-forward after 继续 (the rest of that wait)
  int before = 0;               // HP / deck size before the option
  int pick = -1;                // smith: the upgraded card's index in Run::upgradeOptions
  float t = 0;                  // seconds since the result appeared
} fx;

std::string optName(int o) { return L(std::string("rest_site_ui.OPTION_") + kOptKeys[o] + ".name"); }

// Loc text without its rich-text colour tags (a reason line is drawn in red anyway).
std::string plain(std::string s) {
  for (size_t a; (a = s.find('[')) != std::string::npos;) {
    size_t b = s.find(']', a);
    if (b == std::string::npos) break;
    s.erase(a, b - a + 1);
  }
  return s;
}

void replaceAll(std::string& s, const std::string& key, const std::string& v) {
  for (size_t p; (p = s.find(key)) != std::string::npos;) s.replace(p, key.size(), v);
}

// Additive textured quad from the scene texture's strip (flame, glow).
void glowQuad(gfx::Texture* t, const float src[4], float x, float y, float w, float h, uint32_t rgba) {
  if (!t) return;
  float tw = (float)gfx::texWidth(t), th = (float)gfx::texHeight(t);
  // Half a texel in from the edges, so bilinear filtering never reads the neighbouring piece.
  float u0 = (src[0] + 0.5f) / tw, v0 = (src[1] + 0.5f) / th, u1 = (src[0] + src[2] - 0.5f) / tw, v1 = (src[1] + src[3] - 0.5f) / th;
  gfx::Vert v[4] = {{x, y, u0, v0, rgba}, {x + w, y, u1, v0, rgba}, {x + w, y + h, u1, v1, rgba}, {x, y + h, u0, v1, rgba}};
  static const uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
  gfx::triangles(t, v, 4, idx, 6, true);
}

uint32_t withAlpha(uint32_t rgb, float a) { return (rgb << 8) | (uint32_t)(std::clamp(a, 0.f, 1.f) * 255); }
}  // namespace

// The campfire scene of the current act (one background texture; the previous act's is freed).
static gfx::Texture* restScene(const Run& r) {
  static std::string loaded;
  std::string path = std::string("gfx/bg_rest_") + r.act().key + ".t3t";
  if (loaded != path) {
    if (!loaded.empty()) R().releaseTexture(loaded);
    // One background per scene: the act's room art is not drawn here (the next room that needs
    // it loads it again).
    R().releaseTexture(std::string("gfx/bg_") + r.act().key + ".t3t");
    loaded = path;
  }
  return R().texture(path);
}

// Option o's full description (C# RestSiteOption.Description with its DynVars) and, when it
// can't be chosen, the reason.
static std::string restDescription(const Run& r, int o, std::string* reason, bool valid) {
  bool used = std::find(r.restUsed.begin(), r.restUsed.end(), o) != r.restUsed.end();
  std::string base = std::string("rest_site_ui.OPTION_") + kOptKeys[o];
  if (reason) {
    reason->clear();
    if (used) *reason = tr("已使用", "Used");
    else if (!valid && R().hasLoc(base + ".descriptionDisabled")) *reason = plain(L(base + ".descriptionDisabled"));
    else if (!valid) *reason = tr("无法使用", "Unavailable");
  }
  if (!valid && !used && R().hasLoc(base + ".descriptionDisabled")) return L(base + ".descriptionDisabled");
  std::string d = L(base + ".description");
  switch (o) {
    case 0: {  // HealRestSiteOption: {Heal} previews Hook.ModifyRestSiteHealAmount
      Dec amount = Dec(r.player->maxHp) * Dec::lit(0.3);
      for (Model* m : const_cast<Run&>(r).listeners()) amount = m->modifyRestSiteHealAmount(r.player.get(), amount);
      replaceAll(d, "{Heal}", "[green]" + num(amount.toInt()) + "[/green]");
      std::string extra;  // Hook.ModifyExtraRestSiteHealText: the relics' additionalRestSiteHealText
      for (auto& rel : r.relics) {
        std::string k = "relics." + rel->locKey + ".additionalRestSiteHealText";
        if (R().hasLoc(k)) extra += "\n" + L(k);
      }
      replaceAll(d, "{ExtraText}", extra);
      break;
    }
    case 1: d = expandSmart(d, {{"Count", Dec(1), Dec(1)}}, false); break;  // eng: {Count:plural:a card|{} cards}
    case 2: {
      int lifted = 0;
      for (auto& rel : r.relics) if (rel->id == "Girya") lifted = rel->displayAmount();
      replaceAll(d, "{LiftsLeft}", num(3 - lifted));
      break;
    }
    case 4: replaceAll(d, "{Cards}", "2"); replaceAll(d, "{MaxHp}", "5"); break;
    case 5: replaceAll(d, "{RelicName}", L("relics.PUMPKIN_CANDLE.title")); replaceAll(d, "{RekindleAmount}", "5"); break;
    case 6: replaceAll(d, "{EnchantmentName}", L("enchantments.CLONE.title")); break;
    default: break;
  }
  return d;
}

void App::drawRest(bool top) {
  Run& r = *run_;
  auto& opts = r.restOptions;
  const bool result = fx.opt >= 0 && (fx.hold || fx.fireOnProceed);
  if (top) {
    if (result) fx.t += 1.f / 60;
    gfx::Texture* bg = restScene(r);
    gfx::image(bg, 0, 0, kTop, kH, 0, 0, kTop, kH);
    // The fire (NRestSiteFireVfx): a flickering glow and a flame from the scene's strip.
    float tm = (float)time_;
    float flick = 0.85f + 0.1f * std::sin(tm * 9.1f) + 0.05f * std::sin(tm * 23.7f);
    glowQuad(bg, kGlowSrc, kFireX - 70 * flick, kFireY - 44 * flick, 140 * flick, 80 * flick, withAlpha(0xFF8030, 0.45f));
    float fh = 34 * (0.92f + 0.08f * std::sin(tm * 11.3f)), fw = 24 * (0.95f + 0.05f * std::sin(tm * 7.7f));
    glowQuad(bg, kFlameSrc, kFireX - fw / 2, kFireY + 2 - fh, fw, fh, withAlpha(0xFF5A18, 0.95f));
    glowQuad(bg, kFlameSrc, kFireX - fw * 0.32f, kFireY + 2 - fh * 0.7f, fw * 0.64f, fh * 0.7f, withAlpha(0xFFB040, 0.9f));
    glowQuad(bg, kFlameSrc, kFireX - fw * 0.16f, kFireY + 1 - fh * 0.38f, fw * 0.32f, fh * 0.38f, withAlpha(0xFFF0C0, 0.8f));
    // The character's Spine idle, as in combat (no HP bar: the top bar has it).
    if (Visual* v = visual(r.player.get())) {
      v->anim->apply(*v->skel);
      v->skel->updateWorldTransform();
      float tint[4] = {1.f, 0.93f, 0.85f, 1.f};  // firelight
      static std::vector<spine::Batch> batches;
      batches.clear();
      v->skel->render(batches, kCharX, kCharFeet, 1.f, false, tint);
      for (auto& b : batches)
        gfx::triangles(R().texture(v->data->pages[b.page]), reinterpret_cast<const gfx::Vert*>(b.vertices.data()),
                       (int)b.vertices.size(), b.indices.data(), (int)b.indices.size(), b.blend == 1);
    } else {
      Sprite ic = R().sprite("creature/" + playerArt(run_.get()));
      spr(ic, kCharX - ic.ax, kCharFeet - ic.ay);
    }
    drawTopBar();

    // Title (C# Header): the prompt, or the chosen option.
    TextStyle tt = ts(F16, col::gold, CENTER);
    tt.scale = 1.25f;
    std::string title = result ? optName(fx.opt) : L("rest_site_ui.PROMPT");
    R().text(kTop / 2, 24, title, tt);
    float tw = R().measure(title, tt) + 16;
    gfx::rect(kTop / 2 - tw / 2, 47, tw, 2, style::kPanelHi);

    // The panel at the right: the focused option's description, or the result.
    const float px = 228, pw = kTop - 8 - px, py = 56;
    auto optionPanel = [&](int o, const std::string& body, uint32_t bodyColor, const std::string& foot) {
      TextStyle bt = ts(F12, bodyColor, LEFT, pw - 16);
      float bh = 0;
      R().measure(body, bt, &bh);
      float fh2 = foot.empty() ? 0 : 16;
      float ph = std::min(kH - 8 - py, 60 + bh + fh2);
      widgets::panel("ui/hover_tip", px, py, pw, ph);
      spr(R().sprite(std::string("ui/rest_") + kOptIcons[o]), px + 8, py + 6, 55, 36);
      R().text(px + 68, py + 14, optName(o), ts(F16, col::gold, LEFT, pw - 76));
      R().text(px + 8, py + 48, body, bt);
      if (!foot.empty()) R().text(px + 8, py + 50 + bh, foot, ts(F12, col::red, LEFT, pw - 16));
    };
    if (result) {
      std::string text;
      int amount = 0;
      switch (fx.opt) {
        case 0:
          amount = r.lastHeal;
          text = tr("回复了 [green]", "Healed [green]") + num(amount) + tr("[/green] 点生命值。", "[/green] HP.");
          break;
        case 2: {
          int lifted = 0;
          for (auto& rel : r.relics) if (rel->id == "Girya") lifted = rel->displayAmount();
          text = tr("在战斗开始时拥有 [gold]+", "Start each combat with [gold]+") + num(lifted + 1) + tr("[/gold] 力量。（剩余 ", "[/gold] Strength. (") + num(std::max(0, 2 - lifted)) + tr(" 次）", " left)");
          break;
        }
        case 5: text = "[gold]" + L("relics.PUMPKIN_CANDLE.title") + tr("[/gold] 充能 +5。", "[/gold] charges +5."); break;
        case 6:
          amount = (int)r.deck.size() - fx.before;
          text = tr("复制了 [blue]", "Duplicated [blue]") + num(amount) + tr("[/blue] 张牌。", "[/blue] cards.");
          break;
        default: break;
      }
      optionPanel(fx.opt, text, col::white, "");
      // A heal number rising over the character (the combat floats' style).
      if (fx.opt == 0 && amount > 0) {
        float k = std::min(1.f, fx.t / 0.8f), ease = 1 - (1 - k) * (1 - k);
        float a = fx.t < 1.6f ? 1.f : std::max(0.f, 1 - (fx.t - 1.6f) / 0.4f);
        TextStyle ht = ts(F16, col::green, CENTER, 0, 1.6f);
        ht.color = (col::green & 0xFFFFFF00u) | (uint32_t)(a * 255);
        R().text(kCharX, 110 - 24 * ease, "+" + num(amount), ht);
      }
      return;
    }
    if (sel_ >= 0 && sel_ < 7 && std::find(opts.begin(), opts.end(), sel_) != opts.end()) {
      std::string reason;
      bool valid = restValid(sel_);
      std::string desc = restDescription(r, sel_, &reason, valid);
      bool used = reason == tr("已使用", "Used");
      optionPanel(sel_, used ? restDescription(r, sel_, nullptr, true) : desc, valid ? col::white : col::gray,
                  valid ? "" : (used ? reason : ""));
    } else if (r.restChoice.waiting()) {
      widgets::panel("ui/hover_tip", px, py, pw, 44);
      R().text(px + pw / 2, py + 7, tr("选择一项行动", "Choose an action"), ts(F12, col::white, CENTER));
      R().text(px + pw / 2, py + 23, tr("点选一项即可选择", "Tap an option to choose it"), ts(F12, col::gray, CENTER));
    }
    return;
  }

  // ---- bottom: the options ----
  {
    gfx::Texture* bg = restScene(r);
    gfx::image(bg, 40, 0, kBot, kH, 0, 0, kBot, kH);
    gfx::rect(0, 0, kBot, kH, 0x0A0604B0);
  }
  gfx::Input in = pauseOpen_ ? gfx::Input{} : gfx::input();
  {  // focus left over from another screen: drop it
    int fo = widgets::focused();
    bool ours = (fo >= kOptId && fo < kOptId + 7) || fo == kLeaveId || fo == kConfirmId || fo == kProceedId;
    if (!ours) widgets::setFocus(-1);
  }
  widgets::beginFrame(in);
  const bool canAct = r.restChoice.waiting() && !pauseOpen_ && !result;
  const int n = (int)opts.size();
  // Rows: one column up to four options, two columns beyond (all seven fit above the bar).
  const bool twoCol = n > 4;
  const int rows = twoCol ? (n + 1) / 2 : n;
  const float top0 = 6, bottom0 = style::kActionY - 6, gap = 4;
  const float rh = std::min(twoCol ? 44.f : 52.f, (bottom0 - top0 - gap * (rows - 1)) / std::max(1, rows));
  const float y0 = top0 + std::max(0.f, (bottom0 - top0 - rows * rh - gap * (rows - 1)) / 2);
  const float cw = twoCol ? (kBot - 2 * style::kMargin - gap) / 2 : kBot - 2 * style::kMargin;
  int f = widgets::focused() - kOptId;
  if (widgets::usingPad() && f >= 0 && f < 7) sel_ = f;
  int choose = -1;
  float pulse = 0.75f + 0.25f * std::sin((float)time_ * 5.f);
  for (int i = 0; i < n; ++i) {
    int o = opts[i];
    float x = style::kMargin + (twoCol ? (i % 2) * (cw + gap) : 0);
    float y = y0 + (twoCol ? i / 2 : i) * (rh + gap);
    bool valid = restValid(o);
    bool chosen = result && fx.opt == o;
    bool bright = valid && !result;
    std::string reason;
    std::string desc = restDescription(r, o, &reason, valid);
    // Disabled options stay focusable for their description; choosing one does nothing.
    if (widgets::hit(kOptId + o, x, y, cw, rh, canAct)) {
      sel_ = o;  // one tap chooses (a disabled option only shows its reason on top)
      if (valid) choose = o;
    }
    // Disabled (or, after a choice, not chosen): the plate fades into the dark scene.
    widgets::panel("ui/btn_row", x, y, cw, rh, bright || chosen ? 0xFFFFFFFF : 0xFFFFFF80);
    if (o == sel_ && !result) {
      uint32_t c = (style::kFocus & 0xFFFFFF00u) | (uint32_t)(0xFF * pulse);
      gfx::rect(x, y, cw, rh, style::kSelectedFill);
      gfx::rect(x - 2, y - 2, cw + 4, 2, c);
      gfx::rect(x - 2, y + rh, cw + 4, 2, c);
      gfx::rect(x - 2, y - 2, 2, rh + 4, c);
      gfx::rect(x + cw, y - 2, 2, rh + 4, c);
    }
    float ih = rh - 8, iw = ih * 64 / 42;
    if (bright || chosen) spr(R().sprite(std::string("ui/rest_") + kOptIcons[o]), x + 4, y + 4, iw, ih);
    else spr(R().sprite(std::string("ui/rest_") + kOptIcons[o]), x + 4, y + 4, iw, ih, 0x202020FF, 0.55f);
    float tx = x + iw + 10, tw = cw - (tx - x) - 6;
    uint32_t nameColor = chosen ? col::gold : bright ? col::white : col::gray;
    R().text(tx, y + (rh - 32) / 2, optName(o), ts(F16, nameColor, LEFT, 0, rh < 44 ? 0.9f : 1.f));
    gfx::pushClip(tx, y, tw, rh);
    std::string line = !valid ? reason : desc;
    size_t nl = line.find('\n');
    if (nl != std::string::npos) line = line.substr(0, nl);
    TextStyle dt = ts(F12, !valid ? col::red : result ? col::gray : col::white, LEFT, 0, twoCol ? 0.85f : 1.f);
    R().text(tx, y + (rh - 32) / 2 + 18, line, dt);
    gfx::popClip();
    widgets::focusRing(kOptId + o, x, y, cw, rh);
  }

  // Action bar: 离开 (bottom-left, after one option with Miniature Tent), 确认 or 继续 (right).
  if (!r.restUsed.empty() && !result &&
      widgets::button(kLeaveId, style::kMargin, style::kActionY, 80, style::kButtonH, tr("离开", "Leave"), widgets::Kind::Secondary,
                      canAct)) {
    sel_ = -1;
    widgets::endFrame();
    r.restChoice.fire(-1);
    return;
  }
  if (result) {
    if (widgets::button(kProceedId, kBot - style::kMargin - 96, style::kActionY, 96, style::kButtonH,
                        L("ancients.PROCEED.title"), widgets::Kind::Primary, !pauseOpen_ && fx.t > 0.2f)) {
      if (fx.fireOnProceed && r.restChoice.waiting()) r.restChoice.fire(fx.opt);
      if (fx.hold) fx.skip = true;
      fx.opt = -1;
      fx.hold = fx.fireOnProceed = false;
      sel_ = -1;
      widgets::setFocus(-1);
    }
  } else if (widgets::button(kConfirmId, kBot - style::kMargin - 96, style::kActionY, 96, style::kButtonH, tr("确认", "Confirm"),
                             widgets::Kind::Primary, canAct && sel_ >= 0 && restValid(sel_))) {
    choose = sel_;
  }
  widgets::endFrame();

  if (choose >= 0 && canAct && restValid(choose)) {
    fx = RestFx{};
    fx.opt = choose;
    switch (choose) {
      case 0: fx.hold = true; fx.before = r.player->hp; break;
      case 6: fx.hold = true; fx.before = (int)r.deck.size(); break;
      case 2:
      case 5: fx.fireOnProceed = true; break;
      default: fx.opt = -1; break;  // smith / dig / cook open their own screens
    }
    if (choose == 1) fx.opt = 1;  // the upgrade grid shows its pick with 继续
    if (!fx.fireOnProceed) r.restChoice.fire(choose);
    sel_ = -1;
    widgets::setFocus(-1);
  }
}

bool App::restValid(int o) const {
  const Run& r = *run_;
  if (std::find(r.restOptions.begin(), r.restOptions.end(), o) == r.restOptions.end()) return false;
  if (std::find(r.restUsed.begin(), r.restUsed.end(), o) != r.restUsed.end()) return false;
  if (o == 1) {
    for (auto& c : r.deck) if (c->upgradable()) return true;
    return false;
  }
  if (o == 4) return r.deck.size() >= 2;
  return true;
}

// The result hold: while a result is on screen the rules' short wait after the option is
// paused (App::update set the speed this frame; the scheduler runs after it), and 继续 lets
// it run out at once. Autoplay never holds.
static void restHold(bool autoplay) {
  if (fx.skip) {
    sts::Scheduler::get().speed = 600.0;  // one frame: the 0.4-0.6 s wait is over
    fx.skip = false;
  } else if (fx.hold && !autoplay) {
    sts::Scheduler::get().speed = 0.0;
  } else if (fx.hold) {
    fx = RestFx{};
  }
}

void App::updateRest(const gfx::Input& in) {
  // Choosing, leaving and the focus are handled by the widgets in drawRest; the extra keys here.
  Run& r = *run_;
  restHold(autoplay_);
  // A result that is not ours any more (smith / cook cancelled, a new rest site).
  if (r.restChoice.waiting() && !fx.fireOnProceed && !fx.hold && fx.opt >= 0) fx = RestFx{};
  if (!r.restChoice.waiting()) return;
  if ((in.down & gfx::BTN_B) && sel_ >= 0 && !fx.fireOnProceed) { sel_ = -1; widgets::setFocus(-1); }
}

// Smith (C# NDeckUpgradeSelectScreen, cancelable, one card): the S13 grid select over the
// upgradable deck cards.
static App::GridSelectSpec smithSpec(const Run& r) {
  App::GridSelectSpec s;
  s.cards = &r.upgradeOptions;
  s.prompt = "gameplay_ui.CHOOSE_CARD_UPGRADE_HEADER";
  s.canCancel = true;
  s.upgrade = true;
  return s;
}

void App::drawUpgrade(bool top) {
  Run& r = *run_;
  auto& opts = r.upgradeOptions;
  const bool result = fx.hold && fx.opt == 1 && fx.pick >= 0 && fx.pick < (int)opts.size();
  gfx::Texture* bg = restScene(r);  // the campfire, dimmed (one background per scene)
  if (top) {
    gfx::image(bg, 0, 0, kTop, kH, 0, 0, kTop, kH);
    gfx::rect(0, 0, kTop, kH, 0x000000B0);
    drawTopBar();
    if (result) {  // the upgraded card (C# NCardSmithVfx), until 继续
      fx.t += 1.f / 60;
      TextStyle tt = ts(F16, col::gold, CENTER);
      tt.scale = 1.25f;
      R().text(kTop / 2, 24, tr("已升级", "Upgraded"), tt);
      float s = 1.1f + 0.1f * std::max(0.f, 1 - fx.t * 3);
      drawCard(opts[fx.pick], kTop / 2 - kCardW * s / 2, 52, s, false, true);
      return;
    }
    gridSelectDraw(smithSpec(r), true);  // S13: the focused card before -> after
    return;
  }
  gfx::image(bg, 40, 0, kBot, kH, 0, 0, kBot, kH);
  gfx::rect(0, 0, kBot, kH, 0x000000A8);
  if (result) {
    R().text(kBot / 2, 90, cardTitle(opts[fx.pick]), ts(F16, col::green, CENTER, 0, 1.25f));
    R().text(kBot / 2, 116, tr("已升级", "Upgraded"), ts(F12, col::white, CENTER));
    gfx::rect(0, 196, kBot, 44, 0x000000A0);
    button(kBot - 110, 200, 100, 34, L("ancients.PROCEED.title"), ID_CONFIRM, fx.t > 0.2f, true);
    return;
  }
  gridSelectDraw(smithSpec(r), false);  // S13 deck grid select
}

void App::updateUpgrade(const gfx::Input& in) {
  Run& r = *run_;
  restHold(autoplay_);
  if (fx.hold && fx.opt == 1) {  // the pick is on screen: 继续 (A / tap) ends the visit
    if (fx.t > 0.2f && ((in.down & gfx::BTN_A) || (in.touchDown && hitAt(in.tx, in.ty) == ID_CONFIRM))) {
      fx = RestFx{};
      fx.skip = true;
    }
    return;
  }
  if (!r.upgradeChoice.waiting()) return;
  int m = (int)r.upgradeOptions.size();
  // Picking a card: the rules upgrade it and wait a moment; that wait is held for the result.
  auto pick = [&](int i) {
    sfx::smith();
    fx = RestFx{};
    fx.opt = 1;
    fx.pick = i;
    fx.hold = true;
    r.upgradeChoice.fire(i);
  };
  std::vector<int> picked;
  int got = gridSelectUpdate(smithSpec(r), in, picked);  // S13 deck grid select
  if (got > 0 && !picked.empty() && picked[0] < m) pick(picked[0]);
  else if (got < 0) { fx = RestFx{}; r.upgradeChoice.fire(-1); }
}

}  // namespace ui
