// Split from ui.cpp (F3).
#include "../ui_common.h"
#include "combat_internal.h"

namespace ui {

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

  // HP bar
  float bw = std::clamp(s ? s.w * 0.9f : 50.f, 48.f, 72.f);
  float by = feetY + 4;
  if (c->displayHp < 0) c->displayHp = (float)c->hp;
  c->displayHp += ((float)c->hp - c->displayHp) * 0.15f;
  gfx::rect(x - bw / 2 - 1, by - 1, bw + 2, 9, 0x000000FF);
  gfx::rect(x - bw / 2, by, bw * std::max(0.f, c->displayHp) / c->maxHp, 7, 0xFFB0A0FF);
  gfx::rect(x - bw / 2, by, bw * c->hp / c->maxHp, 7, c->block > 0 ? 0x4A90D0FF : 0xC02828FF);
  R().text(x, by - 3, num(c->hp) + "/" + num(c->maxHp), ts(F12, col::white, CENTER));
  if (c->block > 0) {
    Sprite b = R().sprite("ui/block");
    spr(b, x - bw / 2 - 16, by - 7, 20, 20);
    R().text(x - bw / 2 - 6, by - 4, num(c->block), ts(F12, col::white, CENTER));
  }
  // Powers
  float px = x - bw / 2;
  for (auto& p : c->powers) {
    Sprite ic = R().sprite("power/" + p->locKey);
    uint32_t pt = p->flash > 0 ? 0xFFFFFFFF : 0xFFFFFFFF;
    spr(ic, px, by + 10, 14, 14, pt, 0);
    p->flash = std::max(0.f, p->flash - 0.03f);
    if (p->stackType() == StackType::Counter)
      R().text(px + 15, by + 12, num(p->amount), ts(F12, p->amount < 0 ? col::red : col::white, RIGHT, 0, 0.85f));
    px += 16;
  }
  // Intent
  if (c->monster && c->monster->nextMove && run_->combat && run_->combat->inProgress) {
    Combat& cb = *run_->combat;
    float ix = x - 12, iy = top - 30;
    std::string label;
    Sprite ic;
    for (auto& in : c->monster->nextMove->intents) {
      if (in.kind == Intent::Attack) {
        int single = std::max(0, cb.modifyDamage(cb.player, c, in.damage, kMove, nullptr).toInt());
        int total = single * in.hits;
        int tier = total < 5 ? 1 : total < 10 ? 2 : total < 20 ? 3 : total < 40 ? 4 : 5;
        ic = R().sprite("intent/attack_" + num(tier));
        label = in.hits > 1 ? num(single) + "×" + num(in.hits) : num(single);
        break;
      }
    }
    if (!ic) {
      auto& in = c->monster->nextMove->intents[0];
      switch (in.kind) {
        case Intent::Buff: ic = R().sprite("intent/buff"); break;
        case Intent::Defend: ic = R().sprite("intent/defend"); break;
        case Intent::Debuff:
        case Intent::DebuffStrong: ic = R().sprite("intent/debuff"); break;
        case Intent::Status: ic = R().sprite("intent/status"); label = num(in.count); break;
        case Intent::Stun: ic = R().sprite("intent/stun"); break;
        case Intent::Summon: ic = R().sprite("intent/summon"); break;
        case Intent::Heal: ic = R().sprite("intent/heal"); break;
        case Intent::Escape: ic = R().sprite("intent/escape"); break;
        case Intent::Sleep: ic = R().sprite("intent/sleep"); break;
        default: ic = R().sprite("intent/unknown"); break;
      }
    }
    float bob = 2 * std::sin((float)time_ * 3 + x);
    spr(ic, ix, iy + bob, 24, 24);
    if (!label.empty()) R().text(ix + 22, iy + 8 + bob, label, ts(F12, col::white));
  }
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
