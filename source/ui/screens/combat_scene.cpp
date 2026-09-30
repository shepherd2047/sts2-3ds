// Split from ui.cpp (F3).
#include "../ui_common.h"
#include "combat_internal.h"
#include "../../core/char_necrobinder.h"
#include "../../core/char_silent.h"

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

// A 9-slice bar piece from the game's health_bar_* art, flat-tinted.
void barPiece(const char* name, float x, float y, float w, float h, uint32_t tint, float blend) {
  Sprite sp = R().sprite(name);
  if (w <= 0) return;
  if (!sp) { gfx::rect(x, y, w, h, tint); return; }
  if (w < sp.nl + sp.nr) { gfx::image(sp.tex, sp.x, sp.y, sp.w, sp.h, x, y, w, h, tint, blend); return; }
  gfx::nineSlice(sp.tex, sp.x, sp.y, sp.w, sp.h, sp.nl, sp.nt, sp.nr, sp.nb, x, y, w, h, tint, blend);
}

// NHealthBar: background, the lagging middleground, the red (blue with block) foreground with the
// poison chunk at its right end and the doom chunk from the left, the block outline and badge, and
// the HP label with the C# outline colours (green / purple when poison / doom is lethal).
void drawHpBar(Creature* c, float x, float by, float bw) {
  const int maxHp = std::max(1, c->maxHp), hp = std::clamp(c->hp, 0, maxHp);
  const float x0 = x - bw / 2;
  auto W = [&](float v) { return bw * std::clamp(v, 0.f, (float)maxHp) / maxHp; };
  barPiece("ui/hp_bg", x0 - 1, by - 1, bw + 2, kBarH + 2, 0x101010FF, 0.6f);
  if (c->displayHp > hp) barPiece("ui/hp_fill", x0, by, W(c->displayHp), kBarH, kHpMiddle, 1.f);
  int poison = 0, doom = 0;
  if (auto* p = c->get<PoisonPower>()) poison = std::max(0, p->calculateTotalDamageNextTurn());
  if (auto* d = c->get<DoomPower>()) doom = std::max(0, d->amount);
  const bool poisonLethal = poison > 0 && poison >= hp;
  const bool doomLethal = !poisonLethal && doom > 0 && doom >= hp - poison;
  if (hp > 0) {
    const uint32_t fg = c->block > 0 ? kHpBlock : kHpRed;
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
  if (poisonLethal) { tc = 0x76FF40FF; oc = 0x074700FF; }
  else if (doomLethal) { tc = 0xFB8DFFFF; oc = 0x2D1263FF; }
  else if (c->block > 0) oc = kBlockOutline;
  TextStyle ht = ts(F12, tc, CENTER, 0, 0.85f);
  ht.shadow = false;
  ht.outline = oc;
  R().text(x, by + kBarH / 2 - R().lineHeight(F12) * 0.85f / 2, num(hp) + "/" + num(c->maxHp), ht);
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
      R().text(px + icon + 2, py + icon - R().lineHeight(F12) * 0.8f + 3, num(p->amount), at);
    }
  }
}

// The intents over the creature's head, side by side (NCreature's intent row of NIntents): the C#
// bob (sin(t*pi + offset) * 10 + 8 px up at 1080p, ~a quarter here), attack damage after the
// player's modifiers as "N" or "N×H" (intents.FORMAT_DAMAGE_*), the tiered attack icon by total
// damage, status card counts.
void drawIntents(Combat& cb, Creature* c, float x, float y, float time) {
  struct Shown { Sprite ic; std::string label; };
  std::vector<Shown> shown;
  for (auto& in : c->monster->nextMove->intents) {
    Shown sh;
    switch (in.kind) {
      case Intent::Attack: {
        int single = std::max(0, cb.modifyDamage(cb.player, c, in.damage, kMove, nullptr).toInt());
        int total = single * std::max(1, in.hits);
        int tier = total < 5 ? 1 : total < 10 ? 2 : total < 20 ? 3 : total < 40 ? 4 : 5;
        sh.ic = R().sprite("intent/attack_" + num(tier));
        sh.label = in.hits > 1 ? num(single) + "×" + num(in.hits) : num(single);
        break;
      }
      case Intent::Buff: sh.ic = R().sprite("intent/buff"); break;
      case Intent::Defend: sh.ic = R().sprite("intent/defend"); break;
      case Intent::Debuff:
        sh.ic = R().sprite("intent/debuff_small");
        if (!sh.ic) sh.ic = R().sprite("intent/debuff");
        break;
      case Intent::DebuffStrong: sh.ic = R().sprite("intent/debuff"); break;
      case Intent::Status:
        sh.ic = R().sprite("intent/status");
        if (in.count > 0) sh.label = num(in.count);
        break;
      case Intent::Stun: sh.ic = R().sprite("intent/stun"); break;
      case Intent::Summon: sh.ic = R().sprite("intent/summon"); break;
      case Intent::Heal: sh.ic = R().sprite("intent/heal"); break;
      case Intent::Escape: sh.ic = R().sprite("intent/escape"); break;
      case Intent::Sleep: sh.ic = R().sprite("intent/sleep"); break;
      default: sh.ic = R().sprite("intent/unknown"); break;
    }
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

}  // namespace

// ================================================================ combat

std::vector<Creature*> App::visibleEnemies() {
  std::vector<Creature*> out;
  if (!run_->combat) return out;
  for (auto* e : run_->combat->enemies) if (!e->removed) out.push_back(e);
  return out;
}

float App::enemyX(int i, int n) {
  float left = 190, right = kTop - 8;
  return left + (right - left) * (i + 0.5f) / n;
}

void App::drawCreature(Creature* c, float x, float feetY, bool targeted) {
  Sprite s = R().sprite("creature/" + (c->isPlayer ? playerArt(run_.get()) : c->name));
  float dx = x + (screenShake_ ? std::sin((float)time_ * 55.f + (c->isPlayer ? 0.f : 1.3f)) * c->shake * 3.f : 0.f);
  bool dying = c->dead();
  float flash = c->hitFlash;
  c->hitFlash = std::max(0.f, c->hitFlash - 0.08f);
  if (Visual* v = visual(c)) {
    if (v->dying && c->alive()) {  // an illusion came back
      v->dying = false;
      v->fade = 1.f;
      v->anim->play(idleAnim(*v), true);
    }
    if (v->fade <= 0) return;
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
    v->skel->render(batches, dx, feetY, 1.f, false, tint);
    static_assert(sizeof(spine::Vertex) == sizeof(gfx::Vert), "vertex layouts must match");
    for (auto& b : batches)
      gfx::triangles(R().texture(v->data->pages[b.page]), reinterpret_cast<const gfx::Vert*>(b.vertices.data()),
                     (int)b.vertices.size(), b.indices.data(), (int)b.indices.size(), b.blend == 1);
  } else {
    uint32_t tint = flash > 0 ? 0xFF4040FF : 0xFFFFFFFF;
    float blend = flash > 0 ? flash * 0.6f : 0.f;
    if (dying) { tint = 0x000000A0; blend = 0.6f; }
    if (s) spr(s, dx - s.ax, feetY - s.ay, -1, -1, tint, blend);
    else gfx::rect(dx - 20, feetY - 50, 40, 50, 0x80808080);
  }
  if (dying) return;

  float top = feetY - (s ? s.ay : 50);
  if (targeted) {
    float h = s ? s.h : 50, w = s ? s.w : 40;
    Sprite ret = R().sprite("ui/reticle");
    float pulse = 2 * std::sin((float)time_ * 8);
    float rx = dx - s.ax - 4 - pulse, ry = top - 4 - pulse, rw = w + 8 + 2 * pulse, rh = h + 8 + 2 * pulse;
    uint32_t rc = 0xFFE070FF;
    gfx::rect(rx, ry, 10, 2, rc); gfx::rect(rx, ry, 2, 10, rc);
    gfx::rect(rx + rw - 10, ry, 10, 2, rc); gfx::rect(rx + rw - 2, ry, 2, 10, rc);
    gfx::rect(rx, ry + rh - 2, 10, 2, rc); gfx::rect(rx, ry + rh - 10, 2, 10, rc);
    gfx::rect(rx + rw - 10, ry + rh - 2, 10, 2, rc); gfx::rect(rx + rw - 2, ry + rh - 10, 2, 10, rc);
    (void)ret;
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
    R().text(ix + 24, y + 4, "安全", ts(F12, 0x80C8FFFF, LEFT));
  }
}

}  // namespace ui
