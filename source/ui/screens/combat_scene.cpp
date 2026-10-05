// Split from ui.cpp (F3).
#include <cstdio>
#include <cstring>
#include "../ui_common.h"
#include "combat_internal.h"
#include "../../core/char_necrobinder.h"
#include "../../core/char_silent.h"
#include "../vfx.h"

namespace ui {

namespace {

// C# NHealthBar colours.
constexpr uint32_t kHpRed = 0xF1373EFF;         // _redForegroundColor
constexpr uint32_t kHpBlock = 0x3B6FA3FF;       // _blockHpForegroundColor
constexpr uint32_t kHpMiddle = 0xFFC9A8FF;      // the lagging "middleground" chunk
constexpr uint32_t kHpPoison = 0x5FC23AFF;      // PoisonForeground
constexpr uint32_t kHpDoom = 0x8F4FC9FF;        // DoomForeground
constexpr uint32_t kHpOutline = 0x900000FF;     // _defaultFontOutlineColor
constexpr uint32_t kBlockOutline = 0x1B3045FF;  // _blockOutlineColor
constexpr float kBarH = 8;

// A 9-slice bar piece from the game's health_bar_* art, flat-tinted. The fills and the background
// come pre-coloured from build_assets (ui/hp_fill_<RRGGBB>, ui/hp_bg_dark) and are drawn untinted:
// Azahar ignores citro2d's tint blend and showed the grey art.
void barPiece(const char* name, float x, float y, float w, float h, uint32_t tint, float blend) {
  if (w <= 0) return;
  char baked[32];
  if (std::strcmp(name, "ui/hp_fill") == 0 && blend >= 1.f)
    std::snprintf(baked, sizeof baked, "ui/hp_fill_%06X", (unsigned)(tint >> 8));
  else if (std::strcmp(name, "ui/hp_bg") == 0 && tint == 0x101010FF && blend == 0.6f)
    std::snprintf(baked, sizeof baked, "ui/hp_bg_dark");
  else
    baked[0] = 0;
  Sprite sp = baked[0] ? R().sprite(baked) : Sprite{};
  if (sp) { tint = 0xFFFFFFFF; blend = 0.f; }
  else sp = R().sprite(name);
  if (!sp) { gfx::rect(x, y, w, h, tint); return; }
  if (w < sp.nl + sp.nr) { gfx::image(sp.tex, sp.x, sp.y, sp.w, sp.h, x, y, w, h, tint, blend); return; }
  gfx::nineSlice(sp.tex, sp.x, sp.y, sp.w, sp.h, sp.nl, sp.nt, sp.nr, sp.nb, x, y, w, h, tint, blend);
}

// NHealthBar: background, the lagging middleground, the red (blue with block) foreground with the
// poison chunk at its right end and the doom chunk from the left, the block outline and badge, and
// the HP label with the C# outline colours (green / purple when poison / doom is lethal).
// HpDisplay (Hardened Shell at its cap, an about-to-blow Waterfall Giant): the bar takes the lilac
// invincible colour with no poison / doom chunks; InfiniteWithoutNumbers swaps the label for the
// infinity sign, InfiniteWithNumbers keeps the numbers with the lilac outline.
void drawHpBar(Creature* c, float x, float by, float bw) {
  const int maxHp = std::max(1, c->maxHp), hp = std::clamp(c->hp, 0, maxHp);
  const float x0 = x - bw / 2;
  auto W = [&](float v) { return bw * std::clamp(v, 0.f, (float)maxHp) / maxHp; };
  barPiece("ui/hp_bg", x0 - 1, by - 1, bw + 2, kBarH + 2, 0x101010FF, 0.6f);
  if (c->displayHp > hp) barPiece("ui/hp_fill", x0, by, W(c->displayHp), kBarH, kHpMiddle, 1.f);
  const bool infinite = c->hpInfinite();
  int poison = 0, doom = 0;
  if (!infinite) {
    if (auto* p = c->get<PoisonPower>()) poison = std::max(0, p->calculateTotalDamageNextTurn());
    if (auto* d = c->get<DoomPower>()) doom = std::max(0, d->amount);
  }
  const bool poisonLethal = poison > 0 && poison >= hp;
  const bool doomLethal = !poisonLethal && doom > 0 && doom >= hp - poison;
  if (hp > 0) {
    const uint32_t fg = infinite ? 0xC5BBEDFF : c->block > 0 ? kHpBlock : kHpRed;  // _invincibleForegroundColor
    const int solid = std::max(0, hp - poison);
    if (poisonLethal) {
      barPiece("ui/hp_fill", x0, by, W(hp), kBarH, kHpPoison, 1.f);
    } else {
      barPiece("ui/hp_fill", x0, by, W(solid), kBarH, doomLethal ? kHpDoom : fg, 1.f);
      if (poison > 0) barPiece("ui/hp_fill", x0 + W(solid), by, W(hp) - W(solid), kBarH, kHpPoison, 1.f);
      if (doom > 0 && !doomLethal) barPiece("ui/hp_fill", x0, by, W(doom), kBarH, kHpDoom, 1.f);
    }
    gfx::rect(x0 + 1, by + 1, std::max(0.f, W(hp) - 2), 1, 0xFFFFFF38);  // top highlight
  }
  if (c->block > 0) {  // C# BlockOutline
    const uint32_t bc = 0x8CC4F0FF;
    gfx::rect(x0 - 1, by - 1, bw + 2, 1, bc);
    gfx::rect(x0 - 1, by + kBarH, bw + 2, 1, bc);
    gfx::rect(x0 - 1, by - 1, 1, kBarH + 2, bc);
    gfx::rect(x0 + bw, by - 1, 1, kBarH + 2, bc);
  }
  uint32_t tc = col::white, oc = kHpOutline;
  if (c->hpDisplay == HpDisplay::InfiniteWithNumbers) oc = 0x4C4370FF;  // _invincibleOutlineColor
  else if (poisonLethal) { tc = 0x76FF40FF; oc = 0x074700FF; }
  else if (doomLethal) { tc = 0xFB8DFFFF; oc = 0x2D1263FF; }
  else if (c->block > 0) oc = kBlockOutline;
  TextStyle ht = ts(F12, tc, CENTER, 0, 0.85f);
  ht.shadow = false;
  ht.outline = oc;
  if (c->hpDisplay == HpDisplay::InfiniteWithoutNumbers) {  // _infinityTex replaces the label
    const float ih = kBarH + 4, iw = ih * 50 / 28;
    spr(R().sprite("ui/infinity_hp"), x - iw / 2, by + kBarH / 2 - ih / 2, iw, ih);
  } else {
    R().text(x, by + kBarH / 2 - R().lineHeight(F12) * 0.85f / 2, num(hp) + "/" + num(c->maxHp), ht);
  }
  if (c->block > 0) {  // C# BlockContainer: the shield at the bar's left end with the amount
    const float bs = 20, bx = x0 - bs / 2 - 3, byy = by + kBarH / 2 - bs / 2;
    spr(R().sprite("ui/block"), bx, byy, bs, bs);
    TextStyle bt = ts(F12, col::white, CENTER, 0, 0.85f);
    bt.shadow = false;
    bt.outline = kBlockOutline;
    R().text(bx + bs / 2, byy + bs / 2 - R().lineHeight(F12) * 0.85f / 2, num(c->block), bt);
  }
}

// NPowerContainer.UpdatePositions: as many columns as fit the bar, at least half the count (so
// many powers wrap into two rows that widen, centred on the creature); amounts at the icon's
// bottom-right in cream, red for a debuff amount (PowerModel.AmountLabelColor); a flash (the C#
// PowerFlash particle) bursts the icon outwards when the power triggers.
void drawPowerRow(Creature* c, float x, float y, float bw) {
  const int n = (int)c->powers.size();
  if (!n) return;
  const float pitch = 16, icon = 14;
  int cols = std::max((int)std::ceil(bw / pitch), (n + 1) / 2);
  cols = std::min(cols, 9);  // never wider than ~144 px; a third row then
  const float rowW = pitch * std::min(cols, n);
  const float left = x - rowW / 2;
  for (int i = 0; i < n; ++i) {
    Power* p = c->powers[i].get();
    const float px = left + pitch * (i % cols) + 1, py = y + pitch * (i / cols);
    Sprite ic = R().sprite("power/" + p->locKey);
    spr(ic, px, py, icon, icon);
    if (p->flash > 0) {
      const float g = 1.f + (1.f - p->flash) * 0.9f, sz = icon * g;
      spr(ic, px + icon / 2 - sz / 2, py + icon / 2 - sz / 2, sz, sz, 0xFFFFFF00 | (uint32_t)(p->flash * 190), 0.4f);
    }
    p->flash = std::max(0.f, p->flash - 0.03f);
    if (p->stackType() == StackType::Counter) {
      const bool debuff = p->typeForAmount(Dec(p->amount)) == PowerType::Debuff;
      TextStyle at = ts(F12, debuff ? 0xFF5555FF : col::white, RIGHT, 0, 0.8f);
      at.shadow = false;
      at.outline = 0x000000FF;
      R().text(px + icon + 2, py + icon - R().lineHeight(F12) * 0.8f + 3, num(p->displayAmount()), at);
    }
  }
}

// One intent as shown: the icon (the tiered attack icon by total damage), its label (attack damage
// after the player's modifiers as "N" or "N×H", intents.FORMAT_DAMAGE_*; status card counts) and
// the loc key of its hover tip (intents.<KEY>.title / .description, NIntent / AbstractIntent).
struct IntentShown {
  Sprite ic;
  std::string label;
  const char* key = "UNKNOWN";
  int damage = 0, hits = 1, count = 0;
};
IntentShown intentShown(Combat& cb, Creature* c, const Intent& in) {
  IntentShown sh;
  switch (in.kind) {
    case Intent::Attack: {
      int single = std::max(0, cb.modifyDamage(cb.player, c, in.damage, kMove, nullptr).toInt());
      int total = single * std::max(1, in.hits);
      int tier = total < 5 ? 1 : total < 10 ? 2 : total < 20 ? 3 : total < 40 ? 4 : 5;
      sh.ic = R().sprite("intent/attack_" + num(tier));
      sh.label = in.hits > 1 ? num(single) + "×" + num(in.hits) : num(single);
      sh.key = "ATTACK";
      sh.damage = single;
      sh.hits = std::max(1, in.hits);
      break;
    }
    case Intent::Buff: sh.ic = R().sprite("intent/buff"); sh.key = "BUFF"; break;
    case Intent::Defend: sh.ic = R().sprite("intent/defend"); sh.key = "DEFEND"; break;
    case Intent::Debuff:
      sh.ic = R().sprite("intent/debuff_small");
      if (!sh.ic) sh.ic = R().sprite("intent/debuff");
      sh.key = "DEBUFF";
      break;
    case Intent::DebuffStrong: sh.ic = R().sprite("intent/debuff"); sh.key = "DEBUFF_STRONG"; break;
    case Intent::Status:
      sh.ic = R().sprite("intent/status");
      if (in.count > 0) sh.label = num(in.count);
      sh.key = "STATUS";
      sh.count = in.count;
      break;
    case Intent::Stun: sh.ic = R().sprite("intent/stun"); sh.key = "STUN"; break;
    case Intent::Summon: sh.ic = R().sprite("intent/summon"); sh.key = "SUMMON"; break;
    case Intent::Heal: sh.ic = R().sprite("intent/heal"); sh.key = "HEAL"; break;
    case Intent::Escape: sh.ic = R().sprite("intent/escape"); sh.key = "ESCAPE"; break;
    case Intent::Sleep: sh.ic = R().sprite("intent/sleep"); sh.key = "SLEEP"; break;
    default: sh.ic = R().sprite("intent/unknown"); break;
  }
  return sh;
}

// The intents over the creature's head, side by side (NCreature's intent row of NIntents), with the
// C# bob (sin(t*pi + offset) * 10 + 8 px up at 1080p, ~a quarter here).
void drawIntents(Combat& cb, Creature* c, float x, float y, float time) {
  std::vector<IntentShown> shown;
  for (auto& in : c->monster->nextMove->intents) {
    IntentShown sh = intentShown(cb, c, in);
    bool dup = false;  // the same plain icon twice (Buff + Buff) shows once
    for (auto& o : shown)
      dup |= sh.label.empty() && o.label.empty() && o.ic.tex == sh.ic.tex && o.ic.x == sh.ic.x && o.ic.y == sh.ic.y;
    if (!dup) shown.push_back(sh);
  }
  if (shown.empty()) return;
  const float isz = 24, gap = 1;
  TextStyle lt = ts(F16, col::white, LEFT, 0, 0.85f);
  lt.shadow = false;
  lt.outline = 0x000000FF;
  float widths[8] = {}, total = 0;
  const int m = std::min((int)shown.size(), 8);
  for (int i = 0; i < m; ++i) {
    widths[i] = isz + (shown[i].label.empty() ? 0 : std::max(0.f, R().measure(shown[i].label, lt) - 6));
    total += widths[i] + (i ? gap : 0);
  }
  const float bob = -(std::sin(time * 3.14159f + x * 0.37f) * 2.5f + 2.f);
  float ix = x - total / 2;
  for (int i = 0; i < m; ++i) {
    spr(shown[i].ic, ix, y + bob, isz, isz);
    if (!shown[i].label.empty())
      R().text(ix + isz - 6, y + bob + isz - R().lineHeight(F16) * 0.85f + 2, shown[i].label, lt);
    ix += widths[i] + gap;
  }
}

inline float expoOut(float t) { return t >= 1 ? 1.f : 1.f - std::pow(2.f, -10.f * std::max(0.f, t)); }

// S09: NSelectionReticle. The game's combat_reticle corner bracket at the four corners of the
// creature's box: on select it fades in over 0.2 s while scaling 0.9 -> 1 (expo out, 0.5 s).
// `t` is the time since the creature became the target.
void drawReticle(float x0, float y0, float x1, float y1, float t, uint32_t tint) {
  Sprite sp = R().sprite("ui/reticle");
  const float sc = 0.9f + 0.1f * expoOut(t / 0.5f);
  const float a = std::clamp(t / 0.2f, 0.f, 1.f) * (float)(tint & 0xFF) / 255.f;
  const uint32_t col = (tint & 0xFFFFFF00) | (uint32_t)(a * 255);
  const float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, hw = (x1 - x0) / 2 * sc, hh = (y1 - y0) / 2 * sc;
  const float cs = std::clamp(std::min(hw, hh) * 0.55f, 9.f, 16.f);
  const float xs[4] = {cx - hw, cx + hw - cs, cx + hw - cs, cx - hw};
  const float ys[4] = {cy - hh, cy - hh, cy + hh - cs, cy + hh - cs};
  for (int k = 0; k < 4; ++k) {  // top-left art turned a quarter per corner (clockwise)
    if (!sp) {
      gfx::rect(xs[k], ys[k] + (k >= 2 ? cs - 2 : 0), cs, 2, col);
      gfx::rect(xs[k] + (k == 1 || k == 2 ? cs - 2 : 0), ys[k], 2, cs, col);
      continue;
    }
    gfx::pushTransform(gfx::Affine::rotateAround(xs[k] + cs / 2, ys[k] + cs / 2, k * 3.14159265f / 2));
    spr(sp, xs[k], ys[k], cs, cs, col);
    gfx::popTransform();
  }
}
std::map<Creature*, float>& reticleTimes() {
  static std::map<Creature*, float> m;
  return m;
}

}  // namespace

// ================================================================ combat

std::vector<Creature*> App::visibleEnemies() {
  std::vector<Creature*> out;
  if (!run_->combat) return out;
  for (auto* e : run_->combat->enemies) {
    // A creature removed from the fight stays on screen while its death plays out (NCreature.AnimDie
    // frees the node only after the die animation and the fade); the last one of a won fight stays.
    bool shown = !e->removed;
    if (!shown && e->dead()) {
      auto it = visuals_.find(e);
      shown = it != visuals_.end() && it->second.dying && it->second.fade > 0;
    }
    if (shown) out.push_back(e);
  }
  return out;
}

float App::enemyX(int i, int n) {
  float left = 190, right = kTop - 8;
  return left + (right - left) * (i + 0.5f) / n;
}

// The creature's body (Spine skeleton, else its still) with its feet at (x, feetY). `live` is the
// battlefield copy (screen shake, hit flash decay, illusions coming back); the inspect page draws a
// second, scaled copy with live = false. False when nothing is drawn (faded out after death).
bool App::drawCreatureBody(Creature* c, float x, float feetY, float scale, bool live) {
  Sprite s = R().sprite("creature/" + (c->isPlayer ? playerArt(run_.get()) : c->name));
  float dx = x + (live && screenShake_ ? std::sin((float)time_ * 55.f + (c->isPlayer ? 0.f : 1.3f)) * c->shake * 3.f : 0.f);
  bool dying = c->dead();
  float flash = live ? c->hitFlash : 0.f;
  if (live) c->hitFlash = std::max(0.f, c->hitFlash - 0.08f);
  if (Visual* v = visual(c)) {
    if (live && v->dying && c->alive()) {  // an illusion came back
      v->dying = false;
      v->fade = 1.f;
      v->deadT = 0;
      v->anim->play(idleAnim(*v), true);
    }
    if (v->fade <= 0) return false;
    v->anim->apply(*v->skel);
    v->skel->updateWorldTransform();
    if (v->key == "VANTOM") {
      // The mega blade is masked by a clipping attachment outside its heavy attack.
      const std::string& a = v->anim->current();
      bool show = a == "attack_heavy" || a.rfind("charge", 0) == 0;
      auto& sk = *v->skel;
      sk.hidden.assign(sk.slots.size(), 0);
      for (size_t i = 0; i < sk.slots.size(); ++i)
        if (!show && sk.slots[i].data->name.rfind("mega", 0) == 0) sk.hidden[i] = 1;
    }
    float tint[4] = {1.f, 1.f - flash * 0.6f, 1.f - flash * 0.6f, v->fade};
    static std::vector<spine::Batch> batches;
    batches.clear();
    v->skel->render(batches, dx, feetY, scale, false, tint);
    static_assert(sizeof(spine::Vertex) == sizeof(gfx::Vert), "vertex layouts must match");
    for (auto& b : batches)
      gfx::triangles(R().texture(v->data->pages[b.page]), reinterpret_cast<const gfx::Vert*>(b.vertices.data()),
                     (int)b.vertices.size(), b.indices.data(), (int)b.indices.size(), b.blend == 1);
  } else {
    uint32_t tint = flash > 0 ? 0xFF4040FF : 0xFFFFFFFF;
    float blend = flash > 0 ? flash * 0.6f : 0.f;
    if (dying) { tint = 0x000000A0; blend = 0.6f; }
    if (s) spr(s, dx - s.ax * scale, feetY - s.ay * scale, s.w * scale, s.h * scale, tint, blend);
    else gfx::rect(dx - 20 * scale, feetY - 50 * scale, 40 * scale, 50 * scale, 0x80808080);
  }
  return true;
}

void App::drawCreature(Creature* c, float x, float feetY, bool targeted, uint32_t reticleTint) {
  Sprite s = R().sprite("creature/" + (c->isPlayer ? playerArt(run_.get()) : c->name));
  if (!drawCreatureBody(c, x, feetY, 1.f, true)) { reticleTimes().erase(c); return; }
  if (c->dead()) { reticleTimes().erase(c); return; }

  float top = feetY - (s ? s.ay : 50);
  // S09: the target's reticle (NCreature.ShowSingleSelectReticle), around the body's box.
  if (targeted) {
    float& t = reticleTimes()[c];
    t += std::min(0.05f, (float)gfx::dt());
    const float w = s ? s.w : 40, h = s ? s.h : 50, left = x - (s ? s.ax : 20);
    drawReticle(left - 3, top - 3, left + w + 3, top + h + 3, t, reticleTint);
  } else {
    reticleTimes().erase(c);
  }

  // S08 HUD under / over the creature (C# NHealthBar, NPowerContainer, NIntent).
  float bw = std::clamp(s ? s.w * 0.9f : 50.f, 48.f, 72.f);
  float by = feetY + 4;
  if (c->displayHp < 0) c->displayHp = (float)c->hp;
  c->displayHp += ((float)c->hp - c->displayHp) * 0.15f;
  drawHpBar(c, x, by, bw);
  drawPowerRow(c, x, by + kBarH + 4, bw);
  if (c->monster && c->monster->nextMove && run_->combat && run_->combat->inProgress)
    drawIntents(*run_->combat, c, x, std::max(26.f, top - 30), (float)time_);  // below the top bar
}

// Compact status strip (Vault of the Void style): block badge, an HP bar that
// previews this turn's damage after block, and the enemies' combined intent.
void App::drawStatusBar(float y) {
  Combat* cb = run_->combat.get();
  Creature* pl = cb->player;
  int incoming = 0;
  std::string other;  // strongest non-attack intent icon
  for (auto* e : cb->aliveEnemies()) {
    if (!e->monster->nextMove) continue;
    for (auto& in : e->monster->nextMove->intents) {
      if (in.kind == Intent::Attack) {
        incoming += std::max(0, cb->modifyDamage(pl, e, in.damage, kMove, nullptr).toInt()) * in.hits;
      } else if (other.empty()) {
        other = in.kind == Intent::Buff ? "intent/buff" : in.kind == Intent::Debuff ? "intent/debuff"
              : in.kind == Intent::Defend ? "intent/defend" : "";
      }
    }
  }
  int block = pl->block, hp = std::max(0, pl->hp), maxHp = std::max(1, pl->maxHp);
  int absorbed = std::min(incoming, block), loss = std::min(hp, incoming - absorbed);
  bool lethal = incoming > 0 && loss >= hp;
  float pulse = 0.5f + 0.5f * std::sin(clock_ * 6.f);

  // Plate
  gfx::rect(2, y, kBot - 4, 22, 0x000000A0);
  gfx::rect(2, y + 22, kBot - 4, 1, 0xFFFFFF20);

  // Block badge
  Sprite shield = R().sprite("ui/block");
  spr(shield, 4, y + 1, 21, 21, block > 0 ? 0xFFFFFFFF : 0xFFFFFF50);
  R().text(14.5f, y + 4, num(block), ts(F12, block > 0 ? col::white : 0xFFFFFF60, CENTER));

  // HP bar: red = hp, blue strip = block on top of it, flashing chunk = what gets through.
  const float bx = 30, bw = 196, by = y + 5, bh = 13;
  auto X = [&](float v) { return bx + bw * std::clamp(v, 0.f, (float)maxHp) / maxHp; };
  gfx::rect(bx - 1, by - 1, bw + 2, bh + 2, lethal ? (0xFF000000 | (uint32_t)(0x40 + pulse * 0xBF)) : 0x000000FF);
  gfx::rect(bx, by, bw, bh, 0x3A1212FF);
  gfx::rect(bx, by, X(hp) - bx, bh, 0xB02424FF);
  gfx::rect(bx, by, X(hp) - bx, 4, 0xD84040FF);  // highlight
  if (loss > 0) {
    float x0 = X(hp - loss), x1 = X(hp);
    gfx::rect(x0, by, std::max(1.f, x1 - x0), bh, 0xFFE0A000 | (uint32_t)(0x50 + pulse * 0xA0));
    gfx::rect(x0, by, 1, bh, 0xFFFFFFFF);
  }
  if (block > 0) {
    float x0 = X(hp - block), x1 = X(hp);
    gfx::rect(x0, by - 1, std::max(2.f, x1 - x0), 4, 0x58A8F0FF);
  }
  std::string hpText = loss > 0 ? num(hp) + " → " + num(hp - loss) : num(hp) + "/" + num(maxHp);
  R().text(bx + bw / 2, by, hpText, ts(F12, col::white, CENTER));

  // Incoming: intent icon + total (blue when fully blocked, red pulse when lethal).
  float ix = bx + bw + 6;
  if (incoming > 0) {
    int tier = incoming < 5 ? 1 : incoming < 10 ? 2 : incoming < 20 ? 3 : incoming < 40 ? 4 : 5;
    spr(R().sprite("intent/attack_" + num(tier)), ix, y + 1, 21, 21);
    uint32_t c = loss == 0 ? 0x80C8FFFF : lethal ? (0xFF000000 | 0x4000FF | (uint32_t)(pulse * 0xA0) << 16) : col::white;
    TextStyle st = ts(F16, c, LEFT);
    R().text(ix + 23, y + 2, num(incoming), st);
  } else {
    if (!other.empty()) spr(R().sprite(other), ix, y + 1, 21, 21);
    R().text(ix + 24, y + 4, tr("安全", "Safe"), ts(F12, 0x80C8FFFF, LEFT));
  }
}

// ================================================================ S10 combat inspect (RGDSplus U11)

namespace {

std::string creatureName(Run& r, Creature* c) {
  if (!c) return {};
  if (c->isPlayer) {
    std::string k = "characters." + r.character().key + ".title";
    return R().hasLoc(k) ? L(k) : r.character().id;
  }
  if (c->monster && R().hasLoc("monsters." + c->monster->locKey + ".name")) return L("monsters." + c->monster->locKey + ".name");
  if (R().hasLoc("monsters." + c->name + ".name")) return L("monsters." + c->name + ".name");
  return c->name;
}

std::vector<std::string> splitTop(const std::string& s, char sep) {
  std::vector<std::string> out;
  int d = 0;
  size_t start = 0;
  for (size_t k = 0; k <= s.size(); ++k) {
    if (k < s.size() && s[k] == '{') ++d;
    else if (k < s.size() && s[k] == '}') --d;
    if (k == s.size() || (s[k] == sep && d == 0)) { out.push_back(s.substr(start, k - start)); start = k + 1; }
  }
  return out;
}

// The SmartFormat pieces power texts use that expandSmart (cardtext.cpp) leaves out: {X:abs()},
// {X:cond:<0?a|b} (also ==N?, >N?, a default alt), {Name.StringValue:cond:a|b} (a when the string
// is set; {} is the value) and {singleStarIcon}. Everything else is left for expandSmart.
std::string powerPreformat(const std::string& s, const std::vector<DynVar>& vars, const std::map<std::string, std::string>& sv) {
  auto val = [&](const std::string& n) -> Dec {
    for (auto& v : vars) if (v.name == n) return v.base;
    return Dec(0);
  };
  std::string out;
  for (size_t i = 0; i < s.size();) {
    if (s[i] != '{') { out += s[i++]; continue; }
    int depth = 0;
    size_t j = i;
    for (; j < s.size(); ++j) {
      if (s[j] == '{') ++depth;
      else if (s[j] == '}' && --depth == 0) break;
    }
    if (j >= s.size()) { out += s.substr(i); break; }
    std::string body = s.substr(i + 1, j - i - 1);
    i = j + 1;
    size_t colon = body.find(':');
    std::string name = body.substr(0, colon), rest = colon == std::string::npos ? "" : body.substr(colon + 1);
    if (name == "singleStarIcon") { out += "[icon:star]"; continue; }
    if (rest.rfind("abs()", 0) == 0) { out += num(std::abs(val(name).toInt())); continue; }
    if (rest.rfind("cond:", 0) != 0) { out += "{" + body + "}"; continue; }
    auto alts = splitTop(rest.substr(5), '|');
    std::string chosen, self;
    const std::string strSuffix = ".StringValue";
    if (name.size() > strSuffix.size() && name.compare(name.size() - strSuffix.size(), strSuffix.size(), strSuffix) == 0) {
      auto it = sv.find(name.substr(0, name.size() - strSuffix.size()));
      self = it == sv.end() ? "" : it->second;
      chosen = !self.empty() ? alts[0] : alts.size() > 1 ? alts[1] : "";
    } else {
      const int v = val(name).toInt();
      self = num(v);
      for (auto& a : alts) {
        size_t q = a.find('?');
        const char c0 = a.empty() ? 0 : a[0];
        if (q == std::string::npos || !(c0 == '<' || c0 == '>' || c0 == '=' || c0 == '!')) { chosen = a; break; }
        std::string op = a.substr(0, (a.size() > 1 && a[1] == '=') ? 2 : 1);
        int n = std::atoi(a.substr(op.size(), q - op.size()).c_str());
        bool ok = op == "<" ? v < n : op == ">" ? v > n : op == "<=" ? v <= n : op == ">=" ? v >= n : op == "!=" ? v != n : v == n;
        if (ok) { chosen = a.substr(q + 1); break; }
      }
    }
    for (size_t p; (p = chosen.find("{}")) != std::string::npos;) chosen.replace(p, 2, self);
    out += powerPreformat(chosen, vars, sv);
  }
  return out;
}

// PowerModel.HoverTip: the title and the smart description with its amount and names, falling back
// to the plain description when the text needs a value this port doesn't keep on the power.
std::string powerText(Run& r, Power* p) {
  const std::string base = "powers." + p->locKey;
  std::vector<DynVar> vars{{"Amount", Dec(p->amount), Dec(p->amount)},
                           {"OnPlayer", Dec(p->owner && p->owner->isPlayer ? 1 : 0), Dec(0)}};
  if (p->locKey == "VULNERABLE_POWER" || p->locKey == "TANK_POWER") vars.push_back({"DamageIncrease", Dec::lit(1.5), Dec::lit(1.5)});
  if (p->locKey == "TANK_POWER") vars.push_back({"DamageDecrease", Dec::lit(0.5), Dec::lit(0.5)});
  std::map<std::string, std::string> sv;
  sv["OwnerName"] = creatureName(r, p->owner);
  if (p->applier && !p->applier->isPlayer) sv["ApplierName"] = creatureName(r, p->applier);
  auto ok = [](const std::string& s) { return !s.empty() && s.find('?') == std::string::npos; };
  if (R().hasLoc(base + ".smartDescription")) {
    std::string s = expandSmart(powerPreformat(L(base + ".smartDescription"), vars, sv), vars, true, &sv);
    if (ok(s)) return s;
  }
  if (!R().hasLoc(base + ".description")) return {};
  std::string s = expandSmart(powerPreformat(L(base + ".description"), vars, sv), vars, true, &sv);
  return ok(s) ? s : L(base + ".description");
}

struct InspectTip {
  Sprite icon;
  std::string title, amount, desc;
  uint32_t amountCol = col::white;
  bool header = false;  // a section label (意图 / 能力)
};
constexpr float kInsPad = 4, kInsIcon = 16, kInsTitleS = 0.85f, kInsTop = 22, kInsBottom = 237, kInsGap = 2;

float inspectTipH(const InspectTip& t, float w) {
  if (t.header) return 14;
  float dh = 0;
  if (!t.desc.empty()) R().measure(t.desc, ts(F12, col::white, LEFT, w - 2 * kInsPad), &dh);
  return kInsPad * 2 + std::max(kInsIcon, R().lineHeight(F16) * kInsTitleS) + (t.desc.empty() ? 0 : dh + 2);
}

void drawInspectTip(const InspectTip& t, float x, float y, float w) {
  const float h = inspectTipH(t, w);
  if (t.header) {
    R().text(x + 2, y, t.title, ts(F12, col::gold));
    gfx::rect(x, y + h - 2, w, 1, 0x8FC1C880);
    return;
  }
  gfx::rect(x + 2, y + 2, w - 4, h - 4, 0x0B0B12D0);  // the fight must not show through the tip art
  widgets::panel("ui/hover_tip", x, y, w, h);
  const float lh = std::max(kInsIcon, R().lineHeight(F16) * kInsTitleS);
  float tx = x + kInsPad;
  if (t.icon) {
    spr(t.icon, tx, y + kInsPad + (lh - kInsIcon) / 2, kInsIcon, kInsIcon);
    tx += kInsIcon + 4;
  }
  TextStyle at = ts(F16, t.amountCol, RIGHT, 0, kInsTitleS);
  const float aw = t.amount.empty() ? 0 : R().measure(t.amount, at) + 6;
  const float ty = y + kInsPad + (lh - R().lineHeight(F16) * kInsTitleS) / 2;
  gfx::pushClip(tx, y, std::max(1.f, x + w - kInsPad - aw - tx), h);
  R().text(tx, ty, t.title, ts(F16, col::gold, LEFT, 0, kInsTitleS));
  gfx::popClip();
  if (!t.amount.empty()) R().text(x + w - kInsPad, ty, t.amount, at);
  if (!t.desc.empty()) R().text(x + kInsPad, y + kInsPad + lh + 2, t.desc, ts(F12, col::white, LEFT, w - 2 * kInsPad));
}

// The tips of one creature: its intents (monsters, NIntent hover tips) then its powers.
std::vector<InspectTip> inspectTips(Run& r, Combat& cb, Creature* c) {
  std::vector<InspectTip> out;
  if (c->monster && c->monster->nextMove && cb.inProgress && !c->monster->nextMove->intents.empty()) {
    out.push_back({{}, tr("意图", "Intent"), "", "", col::white, true});
    for (auto& in : c->monster->nextMove->intents) {
      IntentShown sh = intentShown(cb, c, in);
      InspectTip t;
      t.icon = sh.ic;
      std::string k = std::string("intents.") + sh.key;
      t.title = R().hasLoc(k + ".title") ? L(k + ".title") : sh.key;
      t.amount = sh.label;
      t.amountCol = in.kind == Intent::Attack ? col::red : col::white;
      std::vector<DynVar> v{{"Damage", Dec(sh.damage), Dec(sh.damage)}, {"Repeat", Dec(sh.hits), Dec(sh.hits)},
                            {"CardCount", Dec(sh.count), Dec(sh.count)}};
      if (R().hasLoc(k + ".description")) t.desc = expandSmart(L(k + ".description"), v, true);
      bool dup = false;
      for (auto& o : out) dup |= !o.header && o.title == t.title && o.amount == t.amount && o.desc == t.desc;
      if (!dup) out.push_back(t);
    }
  }
  out.push_back({{}, tr("能力", "Powers"), "", "", col::white, true});
  if (c->powers.empty()) {
    InspectTip t;
    t.title = tr("没有能力", "No powers");
    out.push_back(t);
  }
  for (auto& up : c->powers) {
    Power* p = up.get();
    InspectTip t;
    t.icon = R().sprite("power/" + p->locKey);
    std::string k = "powers." + p->locKey + ".title";
    t.title = R().hasLoc(k) ? L(k) : p->id;
    if (p->stackType() == StackType::Counter) {
      t.amount = num(p->displayAmount());
      t.amountCol = p->typeForAmount(Dec(p->amount)) == PowerType::Debuff ? 0xFF5555FF : col::white;
    }
    t.desc = powerText(r, p);
    out.push_back(t);
  }
  return out;
}

constexpr int kInsCloseId = 0x5130, kInsPrevId = 0x5131, kInsNextId = 0x5132, kInsPgUpId = 0x5133, kInsPgDnId = 0x5134,
              kInsChip0 = 0x5140;
// The touch that opened the page (信息) must be let go before the page's buttons listen.
bool& inspectTouchArmed() {
  static bool armed = true;
  return armed;
}

}  // namespace

std::vector<Creature*> App::inspectList() {
  std::vector<Creature*> out;
  Combat* cb = run_->combat.get();
  if (!cb) return out;
  out.push_back(cb->player);
  if (cb->osty && !cb->osty->removed && cb->osty->alive()) out.push_back(cb->osty);
  for (Creature* e : visibleEnemies())
    if (e->alive()) out.push_back(e);
  return out;
}

// Opens on the enemy being aimed at, else the first enemy (the player when none is left).
void App::openCombatInspect() {
  auto list = inspectList();
  if (list.empty()) return;
  Creature* want = nullptr;
  if (Combat* cb = run_->combat.get()) {
    auto alive = cb->aliveEnemies();
    if (!alive.empty()) want = alive[std::clamp(target_, 0, (int)alive.size() - 1)];
  }
  inspect_ = 0;
  for (int i = 0; i < (int)list.size(); ++i)
    if (list[i] == want) inspect_ = i;
  inspectPage_ = 0;
  drag_ = {};
  aiming_ = false;
  widgets::setFocus(-1);
  inspectTouchArmed() = !gfx::input().touching;
  sfx::click();
}

void App::updateCombatInspect(const gfx::Input& in) {
  auto list = inspectList();
  const int m = (int)list.size();
  if (m == 0 || (in.down & gfx::BTN_B)) { inspect_ = -1; return; }
  inspect_ = std::clamp(inspect_, 0, m - 1);
  auto step = [&](int d) {
    inspect_ = (inspect_ + d + m) % m;
    inspectPage_ = 0;
  };
  if (in.down & (gfx::BTN_RIGHT | gfx::BTN_R)) step(1);
  if (in.down & (gfx::BTN_LEFT | gfx::BTN_L)) step(-1);
  if (in.down & gfx::BTN_DOWN) inspectPage_ = std::min(inspectPages_ - 1, inspectPage_ + 1);
  if (in.down & gfx::BTN_UP) inspectPage_ = std::max(0, inspectPage_ - 1);
}

void App::drawCombatInspect(bool top) {
  Combat* cb = run_->combat.get();
  auto list = inspectList();
  if (!cb || list.empty()) { inspect_ = -1; return; }
  inspect_ = std::clamp(inspect_, 0, (int)list.size() - 1);
  Creature* c = list[inspect_];
  if (top) {
    // Over the dimmed fight: the creature large at the left with name and HP / block, its intents
    // and powers explained at the right (paged when they do not fit).
    gfx::rect(0, 18, kTop, kH - 18, 0x000000E8);
    const float lx = 80, lw = 148;
    gfx::rect(lx - lw / 2 + 2, kInsTop + 2, lw - 4, kInsBottom - kInsTop - 4, 0x0B0B12D0);
    widgets::panel("ui/hover_tip", lx - lw / 2, kInsTop, lw, kInsBottom - kInsTop, 0xFFFFFFB0);
    TextStyle nt = ts(F16, col::gold, CENTER, lw - 10);
    R().text(lx, kInsTop + 4, creatureName(*run_, c), nt);
    const char* kind = c->isPlayer ? tr("玩家", "Player") : c == cb->osty ? tr("召唤物", "Summon") : tr("敌人", "Enemy");
    R().text(lx, kInsTop + 22, kind, ts(F12, col::gray, CENTER));
    Sprite s = R().sprite("creature/" + (c->isPlayer ? playerArt(run_.get()) : c->name));
    const float feet = 180, boxW = lw - 16, boxH = feet - (kInsTop + 40);
    float sc = s ? std::min({1.6f, boxW / std::max(1.f, (float)s.w), boxH / std::max(1.f, (float)s.h)}) : 1.f;
    gfx::pushClip(lx - lw / 2 + 2, kInsTop + 38, lw - 4, feet - kInsTop - 36);
    drawCreatureBody(c, lx, feet, sc, false);
    gfx::popClip();
    if (c->displayHp < 0) c->displayHp = (float)c->hp;
    drawHpBar(c, lx + 6, feet + 8, 108);
    std::string line = c->block > 0 ? tr("格挡 ", "Block ") + num(c->block) : "";
    if (c->isPlayer) line += (line.empty() ? "" : "  ·  ") + std::string(tr("能量 ", "Energy ")) + num(cb->energy) + "/" + num(cb->maxEnergyNow());
    if (!line.empty()) R().text(lx, feet + 22, line, ts(F12, c->block > 0 ? col::blue : col::white, CENTER));
    if (inspectPages_ > 1)
      R().text(lx, kInsBottom - 16, num(inspectPage_ + 1) + " / " + num(inspectPages_) + "  ↑↓", ts(F12, col::gray, CENTER));

    // Tips, paged by height.
    auto tips = inspectTips(*run_, *cb, c);
    const float tx = lx + lw / 2 + 6, tw = kTop - 6 - tx;
    std::vector<std::vector<int>> pages(1);
    float y = kInsTop;
    for (int i = 0; i < (int)tips.size(); ++i) {
      float h = inspectTipH(tips[i], tw);
      if (!pages.back().empty() && y + h > kInsBottom) { pages.push_back({}); y = kInsTop; }
      pages.back().push_back(i);
      y += h + kInsGap;
    }
    // A section label never ends a page.
    for (size_t p = 0; p + 1 < pages.size(); ++p)
      if (pages[p].size() > 1 && tips[pages[p].back()].header) {
        pages[p + 1].insert(pages[p + 1].begin(), pages[p].back());
        pages[p].pop_back();
      }
    inspectPages_ = (int)pages.size();
    inspectPage_ = std::clamp(inspectPage_, 0, inspectPages_ - 1);
    y = kInsTop;
    for (int i : pages[inspectPage_]) {
      drawInspectTip(tips[i], tx, y, tw);
      y += inspectTipH(tips[i], tw) + kInsGap;
    }
    return;
  }

  // Bottom: the creatures as chips (tap one), prev / next, pages, 关闭 at the bottom left.
  gfx::rect(0, 0, kBot, kH, style::kScrim);
  R().text(kBot / 2, 6, tr("←→ / L R 切换  ·  ↑↓ 翻页  ·  B 关闭", "←→ / L R Switch  ·  ↑↓ Page  ·  B Close"), ts(F12, col::gray, CENTER, kBot - 16));
  gfx::Input wi = pauseOpen_ ? gfx::Input{} : gfx::input();
  wi.down = wi.held = wi.up = 0;  // keys are read by updateCombatInspect; only touch here
  bool& armed = inspectTouchArmed();
  if (!armed) {
    if (!wi.touching && !wi.touchUp) armed = true;
    wi.touching = wi.touchDown = wi.touchUp = false;
  }
  if (widgets::focused() >= 0) widgets::setFocus(-1);
  widgets::beginFrame(wi);
  const int n = (int)list.size();
  const float gap = 4, cw = std::min(100.f, (kBot - 2 * style::kMargin - gap * (n - 1)) / n), ch = 52;
  float x = (kBot - (cw * n + gap * (n - 1))) / 2;
  const float y = 28;
  for (int i = 0; i < n; ++i, x += cw + gap) {
    Creature* e = list[i];
    const bool cur = i == inspect_;
    if (!cur) gfx::pushAlpha(0.7f);
    widgets::panel("ui/hover_tip", x, y, cw, ch);
    if (!cur) gfx::popAlpha();
    if (cur) {  // the shown creature: the focus colour traced around the plate
      gfx::rect(x - 1, y - 1, cw + 2, 2, style::kFocus);
      gfx::rect(x - 1, y + ch - 1, cw + 2, 2, style::kFocus);
      gfx::rect(x - 1, y - 1, 2, ch + 2, style::kFocus);
      gfx::rect(x + cw - 1, y - 1, 2, ch + 2, style::kFocus);
    }
    gfx::pushClip(x + 2, y, cw - 4, ch);
    R().text(x + cw / 2, y + 4, creatureName(*run_, e), ts(F12, cur ? col::gold : col::white, CENTER));
    gfx::popClip();
    R().text(x + cw / 2, y + 20, num(std::max(0, e->hp)) + "/" + num(e->maxHp), ts(F12, 0xFF8080FF, CENTER, 0, 0.9f));
    if (e->block > 0) R().text(x + cw / 2, y + 34, tr("格挡 ", "Block ") + num(e->block), ts(F12, col::blue, CENTER, 0, 0.85f));
    else if (!e->powers.empty()) R().text(x + cw / 2, y + 34, num((int)e->powers.size()) + tr(" 能力", " powers"), ts(F12, col::gray, CENTER, 0, 0.85f));
    if (widgets::hit(kInsChip0 + i, x, y, cw, ch) && i != inspect_) {
      inspect_ = i;
      inspectPage_ = 0;
      sfx::click();
    }
  }
  if (inspectPages_ > 1) {
    if (widgets::button(kInsPgUpId, kBot / 2 - 110, 138, 72, style::kButtonH, tr("上一页", "Prev"), widgets::Kind::Secondary, inspectPage_ > 0))
      inspectPage_ = std::max(0, inspectPage_ - 1);
    R().text(kBot / 2, 146, num(inspectPage_ + 1) + " / " + num(inspectPages_), ts(F16, col::white, CENTER));
    if (widgets::button(kInsPgDnId, kBot / 2 + 38, 138, 72, style::kButtonH, tr("下一页", "Next"), widgets::Kind::Secondary,
                        inspectPage_ + 1 < inspectPages_))
      inspectPage_ = std::min(inspectPages_ - 1, inspectPage_ + 1);
  }
  if (widgets::button(kInsCloseId, style::kMargin, style::kActionY, 72, style::kButtonH, tr("关闭", "Close"))) inspect_ = -1;
  if (n > 1) {
    if (widgets::button(kInsPrevId, kBot - style::kMargin - 52 * 2 - 4, style::kActionY, 52, style::kButtonH, "◀")) {
      inspect_ = (inspect_ - 1 + n) % n;
      inspectPage_ = 0;
    }
    if (widgets::button(kInsNextId, kBot - style::kMargin - 52, style::kActionY, 52, style::kButtonH, "▶")) {
      inspect_ = (inspect_ + 1) % n;
      inspectPage_ = 0;
    }
  }
  widgets::endFrame();
}

// ================================================================ F7 light combat VFX

namespace {

constexpr uint32_t kOstyRgb = 0x70FFC800;  // Osty's soul fire (osty_fire_shape, teal-green)
constexpr int kMaxTracked = 12;

// UI-side memory the VisualEvent stream does not carry: whether the damage that just arrived
// belongs to an attack (a Hit shortly before), who had poison / a Burn in hand last frame (a tick
// can remove the last stack before the event is drained), the orbs / stars / Osty of the last
// frame (to see an evoke, a star gain, a summon), and a per-creature arrow throttle.
struct VfxState {
  float clock = 0;
  float hitWindow = 0;
  Creature* hitBy = nullptr;
  const Creature* poisoned[kMaxTracked] = {};
  int nPoisoned = 0;
  bool burnInHand = false;
  const Orb* orbs[kMaxTracked] = {};
  uint32_t orbRgb[kMaxTracked] = {};
  int nOrbs = -1;  // -1: not seen yet this fight (no burst for the starting orbs)
  int stars = -1;
  const Creature* osty = nullptr;
  int ostyMax = 0;
  struct Recent {
    const Creature* who = nullptr;
    bool buff = false;
    float t = -10;
  } recent[8];
};
VfxState& vfxState() {
  static VfxState s;
  return s;
}

// NOrbVfx colours per orb id ("LightningOrb", ...).
uint32_t orbColour(const std::string& id) {
  if (id.rfind("Lightning", 0) == 0) return 0xFFF08C00;
  if (id.rfind("Frost", 0) == 0) return 0x8CD8FF00;
  if (id.rfind("Dark", 0) == 0) return 0xB070FF00;
  if (id.rfind("Plasma", 0) == 0) return 0xFF9CE000;
  if (id.rfind("Glass", 0) == 0) return 0xA8FFF000;
  return 0xFFFFFF00;
}

}  // namespace

void App::vfxReset() {
  vfxState() = VfxState{};
  vfx::clear();
}

void App::updateVfx(float dt) {
  VfxState& s = vfxState();
  s.clock += dt;
  s.hitWindow = std::max(0.f, s.hitWindow - dt);
  vfx::update(dt);
}

void App::vfxEvent(const VisualEvent& e) {
  Combat* cb = run_->combat.get();
  if (!cb) return;
  VfxState& s = vfxState();
  float x = 0, y = 0, h = 50;
  auto at = [&](Creature* c) {
    if (!c) return false;
    auto it = centers_.find(c);
    if (it == centers_.end()) return false;
    x = it->second.first;
    y = it->second.second;
    Sprite sp = R().sprite("creature/" + (c->isPlayer ? playerArt(run_.get()) : c->name));
    h = sp ? std::clamp(sp.h, 30.f, 110.f) : 50.f;
    return true;
  };
  switch (e.kind) {
    case VisualEvent::Hit:  // AttackCommand: the damage of this swing follows within ~0.15 s
      s.hitWindow = 0.6f;
      s.hitBy = e.who;
      break;
    case VisualEvent::Damage: {
      if (e.amount <= 0 || !at(e.who)) break;
      bool poisoned = e.who->get<PoisonPower>() != nullptr;
      for (int i = 0; i < s.nPoisoned && !poisoned; ++i) poisoned = s.poisoned[i] == e.who;
      if (s.hitWindow > 0 && e.who != s.hitBy) {
        const uint32_t tint = s.hitBy && s.hitBy == cb->osty ? kOstyRgb : e.who->isPlayer ? 0xFFB09000 : 0xFFF8E800;
        vfx::hit(x, y, e.amount, tint);
      } else if (poisoned) {
        vfx::poisonTick(x, y, h);
      } else if (e.who->isPlayer && s.burnInHand) {
        vfx::burnTick(x, y, h);
      } else {
        vfx::puff(x, y, 0xFF805000);
      }
      break;
    }
    case VisualEvent::Blocked:
      if (at(e.who)) vfx::blocked(x, y);
      break;
    case VisualEvent::BlockBroken:
      if (at(e.who)) vfx::blockBroken(x, y);
      break;
    case VisualEvent::Block:
      if (e.amount > 0 && at(e.who)) vfx::blockGain(x, y);
      break;
    case VisualEvent::Heal:
      if (e.amount > 0 && at(e.who)) vfx::heal(x, y, h);
      break;
    case VisualEvent::PowerUp:
    case VisualEvent::PowerDown: {
      if (!at(e.who)) break;
      // PowerUp also reports a stack growing (poison +3), so judge the power itself when present.
      bool buff = e.kind == VisualEvent::PowerUp;
      for (auto& p : e.who->powers)
        if (p->locKey == e.text) { buff = p->type() != PowerType::Debuff; break; }
      // One volley per creature and direction every 0.3 s (a card applying three powers at once).
      int slot = 0;
      for (int i = 0; i < 8; ++i) {
        if (s.recent[i].who == e.who && s.recent[i].buff == buff) { slot = i; break; }
        if (s.recent[i].t < s.recent[slot].t) slot = i;
      }
      if (s.recent[slot].who == e.who && s.recent[slot].buff == buff && s.clock - s.recent[slot].t < 0.3f) break;
      s.recent[slot] = {e.who, buff, s.clock};
      vfx::powerArrows(x, y, h, buff);
      break;
    }
    case VisualEvent::Death:
      if (e.who && e.who == cb->osty && at(e.who)) vfx::soul(x, y, h, kOstyRgb);
      break;
    default:
      break;
  }
}

void App::drawVfx(Combat& cb) {
  VfxState& s = vfxState();
  // Poison / Burn memory for the next frame's events.
  s.nPoisoned = 0;
  auto notePoison = [&](Creature* c) {
    if (c && s.nPoisoned < kMaxTracked && c->get<PoisonPower>()) s.poisoned[s.nPoisoned++] = c;
  };
  notePoison(cb.player);
  for (Creature* e : cb.enemies) notePoison(e);
  s.burnInHand = false;
  for (Card* c : cb.hand) s.burnInHand = s.burnInHand || c->id == "Burn";

  // X2: an orb that left the queue was evoked; burst at the slot it sat in (combat_ui.cpp layout: the
  // row sits at 24 + relicRowH(), slots 20 px + 4 px gap from x 4).
  const int nOrbs = std::min((int)cb.orbQueue.size(), kMaxTracked);
  if (s.nOrbs >= 0) {
    const float orbRowY = 24 + relicRowH();
    for (int i = 0; i < s.nOrbs; ++i) {
      bool still = false;
      for (int j = 0; j < nOrbs && !still; ++j) still = cb.orbQueue[j].get() == s.orbs[i];
      if (!still) vfx::orbEvoke(4 + i * 24.f + 10, orbRowY + 10, s.orbRgb[i]);
    }
  }
  for (int i = 0; i < nOrbs; ++i) {
    if (s.nOrbs < 0 || i >= s.nOrbs || s.orbs[i] != cb.orbQueue[i].get()) s.orbRgb[i] = orbColour(cb.orbQueue[i]->id);
    s.orbs[i] = cb.orbQueue[i].get();
  }
  s.nOrbs = nOrbs;

  // X3: Regent stars gained.
  if (s.stars >= 0 && cb.stars > s.stars && centers_.count(cb.player)) {
    auto [px, py] = centers_[cb.player];
    vfx::stars(px, py - 10, cb.stars - s.stars);
  }
  s.stars = cb.stars;

  // X4: Osty summoned (appears or its max HP grows).
  Creature* osty = cb.osty && !cb.osty->removed && cb.osty->alive() ? cb.osty : nullptr;
  if (osty && (osty != s.osty || osty->maxHp > s.ostyMax) && centers_.count(osty)) {
    Sprite sp = R().sprite("creature/" + osty->name);
    auto [ox, oy] = centers_[osty];
    vfx::soul(ox, oy, sp ? std::clamp(sp.h, 30.f, 110.f) : 50.f, kOstyRgb);
  }
  s.osty = osty;
  s.ostyMax = osty ? osty->maxHp : 0;

  vfx::draw();
}

}  // namespace ui
