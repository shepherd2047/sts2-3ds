// Screens and input. The top screen shows the scene; the bottom screen holds
// everything you touch.
#include "ui.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <functional>

#include "res.h"

using namespace sts;

namespace ui {

namespace {

constexpr int kTop = gfx::kTopW, kBot = gfx::kBottomW, kH = gfx::kScreenH;

// The two screens as one virtual canvas: bottom sits under the top screen,
// centred, with a hinge gap (48 px of 768 on RGDSplus -> 15 of 240 here).
constexpr float kGap = 15.f;
constexpr float kBotOX = (gfx::kTopW - gfx::kBottomW) / 2.f;
constexpr float kBotOY = gfx::kScreenH + kGap;

inline void toLocal(bool top, float& x, float& y) {
  if (!top) { x -= kBotOX; y -= kBotOY; }
}

// Map (RGDSplus U07): one sheet running through both screens, bottom = lower rows.
constexpr float kRowH = 34.f;
constexpr float kMapBase = 455.f;        // virtual y of row 0 at scroll 0
constexpr float kMapMaxScroll = 11.5f;   // rows; enough to bring the boss onto the bottom screen
constexpr float kMapBgH = 495.f;         // bg_map.t3t sheet height (both screens + hinge)
constexpr float kMapTapSlop = 5.f;       // 12 px of 768 on RGDSplus, rounded up for a stylus

// Button ids
enum : int {
  ID_NONE = -1,
  ID_START = 1,
  ID_END_TURN,
  ID_PLAY,
  ID_CONFIRM,
  ID_SKIP,
  ID_BACK,
  ID_DECK,
  ID_HEAL,
  ID_SMITH,
  ID_RESTART,
  ID_TAKE,
  ID_RELICS,
  ID_TARGET0 = 100,   // + enemy index
  ID_HAND0 = 200,     // + hand index
  ID_NODE0 = 300,     // + reachable index
  ID_REWARD0 = 400,   // + reward index
  ID_RELIC0 = 500,    // + owned relic index
  ID_GRID0 = 1000,    // + grid index
};

Res& R() { return res(); }

void spr(const Sprite& s, float x, float y, float w = -1, float h = -1, uint32_t tint = 0xFFFFFFFF, float blend = 0) {
  if (!s) return;
  gfx::image(s.tex, s.x, s.y, s.w, s.h, x, y, w < 0 ? s.w : w, h < 0 ? s.h : h, tint, blend);
}

TextStyle ts(FontSize f = F12, uint32_t c = col::white, Align a = LEFT, float maxW = 0, float scale = 1.f) {
  TextStyle t;
  t.size = f;
  t.color = c;
  t.align = a;
  t.maxWidth = maxW;
  t.scale = scale;
  return t;
}

std::string L(const std::string& key) { return R().loc(key); }

std::string roomName(RoomType t) {
  switch (t) {
    case RoomType::Monster: return L("map.LEGEND_ENEMY.title");
    case RoomType::Elite: return L("map.LEGEND_ELITE.hoverTip.title");
    case RoomType::Rest: return L("map.LEGEND_REST.title");
    case RoomType::Boss: return "Boss";
    case RoomType::Treasure: return L("map.LEGEND_TREASURE.title");
    default: return L("map.LEGEND_UNKNOWN.title");
  }
}

Sprite roomIcon(RoomType t) {
  switch (t) {
    case RoomType::Monster: return R().sprite("map/monster");
    case RoomType::Elite: return R().sprite("map/elite");
    case RoomType::Rest: return R().sprite("map/rest");
    case RoomType::Boss: return R().sprite("map/boss_vantom");
    case RoomType::Treasure: return R().sprite("map/chest");
    default: return R().sprite("map/unknown");
  }
}

std::string num(int v) { return std::to_string(v); }

}  // namespace

// ================================================================ setup

bool App::init() {
  if (!R().load()) return false;
  run_ = std::make_unique<Run>();
  autoplay_ = getenv("STS_AUTOPLAY") != nullptr;
  return true;
}

void App::startRun() {
  run_ = std::make_unique<Run>();
  const char* seed = getenv("STS_SEED");
  run_->start(seed ? (uint64_t)atoll(seed) : (uint64_t)time(nullptr));
  if (getenv("STS_ALLCARDS")) {  // debug: every pool card in the deck
    run_->deck.clear();
    for (auto& id : db::ironcladPool())
      if (auto c = db::card(id)) run_->deck.push_back(std::move(c));
  }
  Scheduler::get().spawn(run_->main());
  floats_.clear();
  visuals_.clear();
  sel_ = -1;
  mapSel_ = 0;
  mapScroll_ = 0;
  deckOpen_ = false;
  relicsOpen_ = false;
}

// ================================================================ creature animation

std::string App::idleAnim(const Visual& v) const { return v.puffed ? "idle_loop_puffed" : "idle_loop"; }

App::Visual* App::visual(Creature* c) {
  auto it = visuals_.find(c);
  if (it != visuals_.end()) return it->second.skel ? &it->second : nullptr;
  Visual& v = visuals_[c];
  v.key = c->isPlayer ? "IRONCLAD" : c->name;
  v.data = R().skeleton(v.key);
  if (!v.data) return nullptr;
  v.skel = std::make_unique<spine::Skeleton>(v.data);
  v.anim = std::make_unique<spine::AnimationState>(v.data);
  v.anim->play("idle_loop", true);
  // Desynchronise identical monsters.
  v.anim->update((float)((reinterpret_cast<uintptr_t>(c) >> 4) % 97) / 97.f);
  return &v;
}

// CreatureAnimator triggers (MonsterModel.GenerateAnimator and its overrides).
void App::trigger(Creature* c, const std::string& what, int amount) {
  Visual* v = visual(c);
  if (!v || v->dying) return;
  const std::string& k = v->key;
  auto has = [&](const std::string& a) { return v->data->animation(a) != nullptr; };
  auto play = [&](const std::string& a) {
    if (has(a)) v->anim->play(a, false, idleAnim(*v));
  };
  if (what == "Dead") {
    v->dying = true;
    std::string die = v->puffed && has("die_puffed") ? "die_puffed" : "die";
    if (has(die)) v->anim->play(die, false);
    return;
  }
  if (what == "Hit") {
    play(v->puffed && has("hurt_puffed") ? "hurt_puffed" : "hurt");
    return;
  }
  // First animation that exists, else any whose name starts with the first candidate.
  auto playAny = [&](std::initializer_list<const char*> names) {
    for (const char* n : names) if (has(n)) { play(n); return; }
    std::string prefix = *names.begin();
    for (auto& a : v->data->animations)
      if (a.name.rfind(prefix, 0) == 0) { play(a.name); return; }
  };
  if (what == "Attack" || what == "AttackHeavy") {
    int hits = amount / 1000, dmg = amount % 1000;
    v->puffed = false;
    if (k == "VANTOM") play(dmg >= 20 ? "attack_heavy" : hits >= 2 ? "attack_double" : "attack");
    else if (k == "INKLET" && hits >= 3) play("attack_triple");
    else if (k == "KIN_PRIEST") play(hits >= 3 ? "attack_laser" : "attack_grenade");
    else if (k == "KIN_FOLLOWER") play(hits >= 2 ? "attack_boomerang" : "attack_slash");
    else if (what == "AttackHeavy" && has("attack_heavy")) play("attack_heavy");
    else playAny({"attack"});
    return;
  }
  if (what == "Stun") {
    if (has("stun")) v->anim->play("stun", false, has("stun_loop") ? "stun_loop" : idleAnim(*v));
    return;
  }
  if (what == "Unstun") { playAny({"wake_up"}); return; }
  if (what == "Summon") { playAny({"summon", "cast"}); return; }
  // Cast / Debuff
  if (k == "NIBBIT") play("hiss");
  else if (k == "MAWLER") play("roar");
  else if (k == "FUZZY_WURM_CRAWLER") { v->puffed = true; play("inhale"); }
  else if (k == "VANTOM") play(what == "Debuff" ? "debuff" : "buff");
  else if (what == "Debuff") playAny({"debuff", "cast", "buff", "shrill", "rally"});
  else playAny({"cast", "buff", "rally", "shrill", "summon"});
}

// ================================================================ text helpers

std::string App::cardTitle(Card* c) {
  std::string t = L("cards." + c->locKey + ".title");
  if (c->upgradeLevel > 0) t += "+";
  return t;
}

// SmartFormat subset used by these cards: {Var:diff()}, {Var},
// {InCombat:a|b}, {IfUpgraded:show:a|b}.
std::string App::describe(Card* c) {
  std::string src = L("cards." + c->locKey + ".description");
  if (!R().hasLoc("cards." + c->locKey + ".description")) src.clear();
  Combat* cb = c->combat && c->combat->inProgress ? c->combat : nullptr;

  auto value = [&](const std::string& name) -> std::string {
    DynVar* v = c->var(name.c_str());
    int shown, base;
    int canonical = v ? v->canonical.toInt() : 0;
    if (name == "CalculatedDamage") {
      Dec d = c->calculatedDamage();
      base = d.toInt();
      canonical = base;
      shown = cb ? std::max(0, cb->modifyDamage(nullptr, cb->player, d, kMove, c).toInt()) : base;
    } else if (name == "CalculatedBlock") {
      Dec d = c->calculatedBlock();
      base = d.toInt();
      canonical = base;
      shown = cb ? std::max(0, cb->modifyBlock(cb->player, d, kMove, c).toInt()) : base;
    } else if (!v) {
      return "?";
    } else if (name == "Damage") {
      base = v->base.toInt();
      shown = cb ? std::max(0, cb->modifyDamage(nullptr, cb->player, v->base, kMove, c).toInt()) : base;
    } else if (name == "Block") {
      base = v->base.toInt();
      shown = cb ? std::max(0, cb->modifyBlock(cb->player, v->base, kMove, c).toInt()) : base;
    } else {
      base = shown = v->base.toInt();
    }
    std::string s = num(shown);
    if (shown > base || (shown == base && base > canonical)) return "[green]" + s + "[/green]";
    if (shown < base) return "[red]" + s + "[/red]";
    return s;
  };

  // Recursive expansion of {...} blocks.
  std::function<std::string(const std::string&)> expand = [&](const std::string& s) -> std::string {
    std::string out;
    for (size_t i = 0; i < s.size();) {
      if (s[i] != '{') { out += s[i++]; continue; }
      int depth = 0;
      size_t j = i;
      for (; j < s.size(); ++j) {
        if (s[j] == '{') ++depth;
        else if (s[j] == '}' && --depth == 0) break;
      }
      std::string body = s.substr(i + 1, j - i - 1);
      i = j + 1;
      size_t colon = body.find(':');
      std::string name = body.substr(0, colon);
      std::string rest = colon == std::string::npos ? "" : body.substr(colon + 1);
      auto choose = [&](const std::string& alts, bool first) {
        int d = 0;
        for (size_t k = 0; k < alts.size(); ++k) {
          if (alts[k] == '{') ++d;
          else if (alts[k] == '}') --d;
          else if (alts[k] == '|' && d == 0) return expand(first ? alts.substr(0, k) : alts.substr(k + 1));
        }
        return first ? expand(alts) : std::string();
      };
      auto raw = [&](const std::string& n) -> Dec { DynVar* v = c->var(n.c_str()); return v ? v->base : Dec(0); };
      if (name == "InCombat") out += choose(rest, cb != nullptr);
      else if (name == "IfUpgraded") out += choose(rest.substr(rest.find(':') + 1), c->upgraded());
      else if (rest.rfind("energyIcons", 0) == 0) {
        // Energy icons: "{Energy:energyIcons()}" -> "2点能量"; "{energyPrefix:energyIcons(1)}" -> "点能量".
        out += name == "energyPrefix" ? std::string("点能量") : "[gold]" + value(name) + "点能量[/gold]";
      } else if (rest.rfind("percentMore", 0) == 0) {
        out += num(((raw(name) - Dec(1)) * Dec(100)).toInt());
      } else if (rest.rfind("percentLess", 0) == 0) {
        out += num(((Dec(1) - raw(name)) * Dec(100)).toInt());
      } else if (rest.rfind("plural:", 0) == 0) {
        out += choose(rest.substr(7), raw(name) == Dec(1));
      } else {
        out += value(name);
      }
    }
    return out;
  };

  std::string d = expand(src);
  if (c->has(kwUnplayable)) d = "[gold]" + L("card_keywords.UNPLAYABLE.title") + "[/gold]" + L("card_keywords.PERIOD") + (d.empty() ? "" : "\n" + d);
  if (c->has(kwExhaust)) d += (d.empty() ? "" : "\n") + std::string("[gold]") + L("card_keywords.EXHAUST.title") + "[/gold]" + L("card_keywords.PERIOD");
  return d;
}

// Relic text: the card SmartFormat subset, fed by the relic's own DynamicVars.
std::string App::describeRelic(Relic* r) {
  std::string src = L("relics." + r->locKey + ".description");
  if (!R().hasLoc("relics." + r->locKey + ".description")) return {};
  auto raw = [&](const std::string& n) -> Dec { DynVar* v = r->var(n.c_str()); return v ? v->base : Dec(0); };
  std::function<std::string(const std::string&)> expand = [&](const std::string& s) -> std::string {
    std::string out;
    for (size_t i = 0; i < s.size();) {
      if (s[i] != '{') { out += s[i++]; continue; }
      int depth = 0;
      size_t j = i;
      for (; j < s.size(); ++j) {
        if (s[j] == '{') ++depth;
        else if (s[j] == '}' && --depth == 0) break;
      }
      std::string body = s.substr(i + 1, j - i - 1);
      i = j + 1;
      size_t colon = body.find(':');
      std::string name = body.substr(0, colon);
      std::string rest = colon == std::string::npos ? "" : body.substr(colon + 1);
      auto choose = [&](const std::string& alts, bool first) {
        int d = 0;
        for (size_t k = 0; k < alts.size(); ++k) {
          if (alts[k] == '{') ++d;
          else if (alts[k] == '}') --d;
          else if (alts[k] == '|' && d == 0) return expand(first ? alts.substr(0, k) : alts.substr(k + 1));
        }
        return first ? expand(alts) : std::string();
      };
      if (name == "InCombat") out += choose(rest, r->combat != nullptr);
      else if (name == "IfUpgraded") out += choose(rest.substr(rest.find(':') + 1), false);
      else if (rest.rfind("energyIcons", 0) == 0)
        out += name == "energyPrefix" ? std::string("点能量") : "[gold]" + num(raw(name).toInt()) + "点能量[/gold]";
      else if (rest.rfind("percentMore", 0) == 0) out += num(((raw(name) - Dec(1)) * Dec(100)).toInt());
      else if (rest.rfind("percentLess", 0) == 0) out += num(((Dec(1) - raw(name)) * Dec(100)).toInt());
      else if (rest.rfind("plural:", 0) == 0) out += choose(rest.substr(7), raw(name) == Dec(1));
      else if (r->var(name.c_str())) out += num(raw(name).toInt());
      else out += "?";
    }
    return out;
  };
  return expand(src);
}

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

// ================================================================ widgets

void App::panel(float x, float y, float w, float h, uint32_t fill, uint32_t border) {
  gfx::rect(x, y, w, h, fill);
  gfx::rect(x, y, w, 1, border);
  gfx::rect(x, y + h - 1, w, 1, border);
  gfx::rect(x, y, 1, h, border);
  gfx::rect(x + w - 1, y, 1, h, border);
}

bool App::button(float x, float y, float w, float h, const std::string& label, int id, bool enabled, bool highlight) {
  uint32_t fill = !enabled ? 0x2A2A2AE0 : highlight ? 0x8A5A20F0 : 0x3A2E24F0;
  uint32_t border = !enabled ? 0x555555FF : highlight ? 0xFFD870FF : 0xB89A60FF;
  panel(x, y, w, h, fill, border);
  gfx::rect(x + 1, y + 1, w - 2, 2, 0xFFFFFF22);
  float th;
  // Large font when it fits on one line, otherwise the small one, then shrink.
  TextStyle st = ts(F16, enabled ? col::white : col::gray, CENTER);
  float tw = R().measure(label, st, &th);
  if (tw > w - 6 || th > h - 2) {
    st.size = F12;
    tw = R().measure(label, st, &th);
    if (tw > w - 6) { st.scale = (w - 6) / tw; th *= st.scale; }
  }
  R().text(x + w / 2, y + (h - th) / 2, label, st);
  if (enabled) hits_.push_back({x, y, w, h, id});
  return enabled;
}

int App::hitAt(int tx, int ty) {
  // Later registrations are drawn on top, so search backwards.
  for (int i = (int)hits_.size() - 1; i >= 0; --i) {
    auto& h = hits_[i];
    if (tx >= h.x && tx < h.x + h.w && ty >= h.y && ty < h.y + h.h) return h.id;
  }
  return ID_NONE;
}

void App::drawCard(Card* c, float x, float y, float s, bool dim, bool desc, bool selected) {
  const char* kind = c->type == CardType::Attack ? "attack" : c->type == CardType::Power ? "power" : "skill";
  bool status = c->type == CardType::Status || c->type == CardType::Curse;
  uint32_t tint = dim ? 0x000000FF : 0xFFFFFFFF;
  float blend = dim ? 0.45f : 0.f;
  if (selected) gfx::rect(x - 3 * s - 1, y - 3 * s - 1, 126 * s + 2, 175 * s + 2, 0xFFE070C0);
  spr(R().sprite("portrait/" + c->locKey), x + 8 * s, y + 16 * s, 104 * s, 78 * s, tint, blend);
  Sprite frame = R().sprite(std::string("card/frame_") + kind);
  spr(frame, x, y, 120 * s, 169 * s, status ? 0x606060FF : tint, status ? 0.5f : blend);
  spr(R().sprite(std::string("card/border_") + kind), x - 3 * s, y + 4 * s, 126 * s, 96 * s, tint, blend);
  Sprite banner = R().sprite("card/banner");
  spr(banner, x - 3 * s, y + 3 * s, 126 * s, 28 * s, tint, blend);

  // Title, shrunk to fit the banner.
  std::string title = cardTitle(c);
  FontSize f = s >= 0.8f ? F16 : F12;
  TextStyle tt = ts(f, c->upgraded() ? col::green : col::white, CENTER);
  float tw = R().measure(title, tt);
  float maxW = 104 * s;
  if (tw > maxW) tt.scale = maxW / tw;
  // The title must never reach the art: its line box ends above the portrait's top edge
  // and any extra height grows upward, past the card's top edge if need be.
  const float maxH = 23 * s, artTop = y + 17 * s;
  float lh = R().lineHeight(f);
  if (lh * tt.scale > maxH) tt.scale = maxH / lh;
  float th = lh * tt.scale;
  R().text(x + 60 * s, artTop - th, title, tt);

  // Cost orb.
  if ((c->cost >= 0 || c->costsX) && !c->has(kwUnplayable)) {
    float os = 30 * std::max(s, 0.6f);
    spr(R().sprite("card/energy"), x - 7 * s, y - 7 * s, os, os, tint, blend);
    bool live = c->combat && c->combat->inProgress;
    int shownCost = live ? c->combat->energyCost(c) : c->cost;
    uint32_t cc = shownCost < c->canonicalCost ? col::green : shownCost > c->canonicalCost ? col::red : col::white;
    TextStyle ct = ts(F16, cc, CENTER);
    ct.scale = std::max(s, 0.6f) * 1.1f;
    R().text(x - 7 * s + os / 2, y - 7 * s + (os - R().lineHeight(F16) * ct.scale) / 2, c->costsX ? std::string("X") : num(shownCost), ct);
  }

  if (desc) {
    TextStyle dt = ts(F12, dim ? col::gray : col::white, CENTER, 104 * s);
    dt.scale = s >= 0.95f ? 1.f : std::max(0.75f, s);
    dt.maxWidth = 104 * s;
    float dh;
    std::string d = describe(c);
    R().measure(d, dt, &dh);
    float top = y + 100 * s, bottom = y + 164 * s;
    // Long texts shrink until they fit the text box.
    for (int k = 0; k < 6 && dh > bottom - top && dt.scale > 0.5f; ++k) {
      dt.scale *= 0.9f;
      R().measure(d, dt, &dh);
    }
    // A lone wrapped character (usually the full stop) reads badly on small cards: shrink to one line less.
    if (s < 0.8f) {
      float lh = R().lineHeight(F12) * dt.scale, dh1;
      TextStyle t2 = dt;
      for (int k = 0; k < 3; ++k) {
        t2.scale *= 0.92f;
        R().measure(d, t2, &dh1);
        if (dh1 < dh - lh * 0.5f) { dt = t2; dh = dh1; break; }
      }
    }
    R().text(x + 60 * s, top + std::max(0.f, (bottom - top - dh) / 2), d, dt);
  }
}

void App::drawCardGrid(const std::vector<Card*>& cards, int sel, float y0, float y1, int scrollRow) {
  const float s = 0.46f, cw = 120 * s, ch = 169 * s;
  const int perRow = 5;
  float gap = (kBot - perRow * cw) / (perRow + 1);
  for (int i = 0; i < (int)cards.size(); ++i) {
    int row = i / perRow - scrollRow, colI = i % perRow;
    float x = gap + colI * (cw + gap);
    float y = y0 + 6 + row * (ch + 8);
    if (y + ch < y0 || y > y1) continue;
    drawCard(cards[i], x, y, s, false, false, i == sel);
    hits_.push_back({x, y, cw, ch, ID_GRID0 + i});
  }
}

int App::gridHit(const std::vector<Card*>&, float, float, int, int tx, int ty) {
  int id = hitAt(tx, ty);
  return id >= ID_GRID0 ? id - ID_GRID0 : -1;
}

// ================================================================ frame

void App::update(const gfx::Input& in, double dt) {
  time_ += dt;
  if (toastT_ > 0) toastT_ -= (float)dt;
  for (auto& f : floats_) f.t += (float)dt;
  floats_.erase(std::remove_if(floats_.begin(), floats_.end(), [](const Float& f) { return f.t > 1.2f; }), floats_.end());

  Screen scr = run_->screen;
  if (scr != lastScreen_) {
    sel_ = -1;
    scroll_ = 0;
    mapTouch_ = {};
    mapUserScroll_ = false;
    lastScreen_ = scr;
  }
  if (run_->combat.get() != lastCombat_) {
    lastCombat_ = run_->combat.get();
    visuals_.clear();
    // Only the player stays cached across fights; each monster's Spine pages
    // are a few MB of linear memory on the 3DS.
    R().releaseSkeletons({"IRONCLAD"});
    centers_.clear();
    flights_.clear();
    poses_.clear();
    ghosts_.clear();
    drawQueue_ = leaveQueue_ = 0;
    drag_ = {};
    aiming_ = false;
    floats_.clear();
    target_ = 0;
  }
  consumeEvents();
  for (auto& [c, v] : visuals_) {
    if (!v.anim) continue;
    v.anim->update((float)dt);
    if (v.dying && v.anim->finished()) v.fade = std::max(0.f, v.fade - (float)dt * 2.f);
  }

  if (autoplay_) autoplay(dt);
  if (relicsOpen_) { updateRelics(in); return; }
  if (deckOpen_) { updateDeck(in); return; }
  switch (scr) {
    case Screen::Title: updateTitle(in); break;
    case Screen::Map: updateMap(in); break;
    case Screen::Combat: updateCombat(in); break;
    case Screen::Reward: updateReward(in); break;
    case Screen::Rest: updateRest(in); break;
    case Screen::RestUpgrade: updateUpgrade(in); break;
    case Screen::GameOver: updateEnd(in); break;
    case Screen::Victory: updateEnd(in); break;
    case Screen::RelicOffer: updateRelicOffer(in); break;
    default: break;
  }
}

void App::consumeEvents() {
  Combat* c = run_->combat.get();
  if (!c) return;
  for (auto& e : c->events) {
    float dx = (float)((int)(floats_.size() * 13) % 21) - 10;
    switch (e.kind) {
      case VisualEvent::Damage:
        floats_.push_back({e.who, num(e.amount), col::red, 0, dx});
        if (e.amount > 0 && e.who && e.who->alive()) trigger(e.who, "Hit", 0);
        break;
      case VisualEvent::Anim:
        if (e.who) trigger(e.who, e.text, e.amount);
        break;
      case VisualEvent::Death:
        if (e.who) trigger(e.who, "Dead", 0);
        break;
      case VisualEvent::Blocked:
        floats_.push_back({e.who, L("gameplay_ui.BLOCKED").find("gameplay_ui") == 0 ? "格挡" : L("gameplay_ui.BLOCKED"), col::blue, 0, dx});
        break;
      case VisualEvent::Block:
        floats_.push_back({e.who, "+" + num(e.amount), col::blue, 0, dx});
        break;
      case VisualEvent::Heal:
        if (e.amount > 0) floats_.push_back({e.who, "+" + num(e.amount), col::green, 0, dx});
        break;
      case VisualEvent::PowerUp:
      case VisualEvent::PowerDown:
        floats_.push_back({e.who, L("powers." + e.text + ".title"), e.kind == VisualEvent::PowerUp ? col::gold : col::purple, 0.1f, dx});
        break;
      case VisualEvent::CardExhaust:
        toast_ = L("card_keywords.EXHAUST.title") + "：" + L("cards." + e.text + ".title");
        toastT_ = 1.2f;
        break;
      case VisualEvent::Shuffle:
        toast_ = "洗牌";
        toastT_ = 0.8f;
        break;
      case VisualEvent::Banner: {
        std::string key = e.text == "Slimed" ? "SLIMED" : e.text == "Wound" ? "WOUND" : e.text;
        toast_ = "+" + num(e.amount) + " " + L("cards." + key + ".title") + " → " + L("gameplay_ui.PILE_DISCARD").substr(0, 0) + "弃牌堆";
        toastT_ = 1.4f;
        break;
      }
      default:
        break;
    }
  }
  c->events.clear();
}

void App::draw() {
  hits_.clear();
  Screen scr = run_->screen;
  for (int pass = 0; pass < 2; ++pass) {
    bool top = pass == 0;
    gfx::screen(top ? gfx::TOP : gfx::BOTTOM, 0x0B0B12FF);
    if (relicsOpen_) { drawRelics(top); continue; }
    if (deckOpen_) { drawDeck(top); continue; }
    switch (scr) {
      case Screen::Title: drawTitle(top); break;
      case Screen::Map: drawMap(top); break;
      case Screen::Combat: drawCombat(top); break;
      case Screen::Reward: drawReward(top); break;
      case Screen::Rest: drawRest(top); break;
      case Screen::RestUpgrade: drawUpgrade(top); break;
      case Screen::GameOver: drawEnd(top, false); break;
      case Screen::Victory: drawEnd(top, true); break;
      case Screen::RelicOffer: drawRelicOffer(top); break;
      default: break;
    }
    if (!top && toastT_ > 0) {
      float a = std::min(1.f, toastT_ * 3);
      float w = R().measure(toast_, ts(F12)) + 16;
      gfx::rect((kBot - w) / 2, 112, w, 20, 0x000000C0 & (0xFFFFFF00 | (uint32_t)(a * 0xC0)));
      R().text(kBot / 2, 115, toast_, ts(F12, col::gold, CENTER));
    }
  }
}

// ================================================================ autoplay

void App::autoplay(double dt) {
  autoT_ += dt;
  if (autoT_ < 0.6) return;
  Run& r = *run_;
  bool acted = true;
  switch (r.screen) {
    case Screen::Title: startRun(); break;
    case Screen::Map:
      if (r.mapChoice.waiting()) {
        auto reach = r.reachableNodes();
        int pick = reach[0];
        for (int i : reach) if (r.nodes[i].type == RoomType::Rest) pick = i;
        r.mapChoice.fire(pick);
      } else acted = false;
      break;
    case Screen::Combat: {
      Combat& c = *r.combat;
      if (c.choice.active && c.choice.result.waiting()) { c.choice.result.fire({c.choice.options[0]}); break; }
      if (!(c.playerPhase && c.actions.waiting())) { acted = false; break; }
      PlayerAction a;
      for (Card* card : c.hand) {
        if (!c.canPlay(card)) continue;
        a.kind = PlayerAction::PlayCard;
        a.card = card;
        if (card->target == TargetType::AnyEnemy) a.target = c.aliveEnemies()[0];
        break;
      }
      c.actions.fire(a);
      break;
    }
    case Screen::Reward:
      if (r.rewardChoice.waiting()) {
        if (sel_ < 0) { sel_ = 1; autoT_ = -0.6; return; }  // linger so the choice is visible
        r.rewardChoice.fire(sel_);
      } else acted = false;
      break;
    case Screen::Rest:
      if (r.restChoice.waiting()) r.restChoice.fire(1); else acted = false;
      break;
    case Screen::RelicOffer:
      if (r.relicChoice.waiting()) r.relicChoice.fire(1); else acted = false;
      break;
    case Screen::RestUpgrade:
      if (r.upgradeChoice.waiting()) {
        if (sel_ < 0) { sel_ = 9; autoT_ = -0.6; return; }
        r.upgradeChoice.fire(sel_);
      } else acted = false;
      break;
    default: acted = false; break;
  }
  if (acted) autoT_ = 0;
}

// ================================================================ title

void App::drawTitle(bool top) {
  if (top) {
    gfx::Texture* bg = R().texture("gfx/bg_overgrowth.t3t");
    gfx::image(bg, 0, 0, kTop, kH, 0, 0, kTop, kH, 0x000000FF, 0.35f);
    Sprite ic = R().sprite("creature/IRONCLAD");
    spr(ic, 40, 205 - ic.ay, -1, -1);
    TextStyle t = ts(F16, col::gold, CENTER);
    t.scale = 2.f;
    R().text(250, 60, "杀戮尖塔 2", t);
    R().text(250, 110, "Nintendo 3DS 非官方移植", ts(F16, col::white, CENTER));
    R().text(250, 132, "最小可玩版本 · 铁甲战士 · 第一幕", ts(F12, col::gray, CENTER));
    R().text(kTop - 4, kH - 16, "个人自制，不可分发", ts(F12, col::gray, RIGHT));
    return;
  }
  gfx::rectGradient(0, 0, kBot, kH, 0x201810FF, 0x0B0B12FF);
  button(80, 60, 160, 44, "开始游戏", ID_START, true, true);
  R().text(kBot / 2, 130, "触摸屏：点选卡牌、目标和按钮", ts(F12, col::gray, CENTER));
  R().text(kBot / 2, 148, "按键：←→选择  A确认  B取消  X结束回合", ts(F12, col::gray, CENTER));
  R().text(kBot / 2, 166, "L/R切换手牌  Y查看牌组", ts(F12, col::gray, CENTER));
}

void App::updateTitle(const gfx::Input& in) {
  if ((in.touchDown && hitAt(in.tx, in.ty) == ID_START) || (in.down & (gfx::BTN_A | gfx::BTN_START))) startRun();
}

// ================================================================ top bar

void App::drawTopBar() {
  gfx::rect(0, 0, kTop, 18, 0x000000A0);
  Creature* p = run_->player.get();
  R().text(4, 2, "生命 " + num(p->hp) + "/" + num(p->maxHp), ts(F12, col::red));
  R().text(96, 2, "金币 " + num(run_->gold), ts(F12, col::gold));
  R().text(170, 2, "第 " + num(run_->floor) + " 层", ts(F12, col::white));
  R().text(230, 2, "牌组 " + num((int)run_->deck.size()), ts(F12, col::white));
  // Relics from the right edge; the rest are counted as "+N" (all of them are in the
  // relic page).
  const int n = (int)run_->relics.size();
  const int fit = n > 5 ? 4 : n;
  float x = kTop - 20;
  for (int i = 0; i < fit; ++i, x -= 20) drawRelicIcon(run_->relics[i].get(), x, 0, 18);
  if (n > fit) R().text(x + 18, 3, "+" + num(n - fit), ts(F12, col::gold, RIGHT));
}

void App::drawRelicIcon(Relic* r, float x, float y, float size) {
  float pulse = r->flash > 0 ? 1.f + 0.25f * r->flash : 1.f;
  float s = size * pulse, off = (s - size) / 2;
  if (r->flash > 0) gfx::circle(x + size / 2, y + size / 2, s * 0.6f, 0xFFE07000 | (uint32_t)(r->flash * 0x90));
  spr(R().sprite("relic/" + r->icon), x - off, y - off, s, s, r->usedUp ? 0x000000FF : 0xFFFFFFFF, r->usedUp ? 0.55f : 0.f);
  r->flash = std::max(0.f, r->flash - 0.02f);
  if (r->showCounter()) {
    std::string c = num(r->displayAmount());
    R().text(x + size, y + size - 10, c, ts(F12, col::white, RIGHT, 0, size < 24 ? 0.8f : 1.f));
  }
}

// ================================================================ map

void App::drawSceneBg(bool top, float dim) {
  const int w = top ? kTop : kBot;
  if (run_->screen == Screen::Map) {
    float bx = 0, by = 0;
    toLocal(top, bx, by);
    gfx::image(R().texture("gfx/bg_map.t3t"), 0, 0, kTop, kMapBgH, bx, by, kTop, kMapBgH, 0x000000FF, 0.1f);
  } else {
    gfx::image(R().texture("gfx/bg_overgrowth.t3t"), top ? 0 : kBotOX, 0, w, kH, 0, 0, w, kH, 0x000000FF, 0.15f);
  }
  if (dim > 0) gfx::rect(0, 0, w, kH, (uint32_t)(std::clamp(dim, 0.f, 1.f) * 255));
}

// Node centre on the two-screen virtual canvas.
std::pair<float, float> App::mapPos(const MapNode& n) const {
  return {58 + n.x * 47, kMapBase - (n.y - mapScroll_) * kRowH};
}

// Reachable index under a bottom-screen point, or -1.
int App::mapNodeAt(float tx, float ty) {
  Run& r = *run_;
  auto reach = r.reachableNodes();
  float vx = tx + kBotOX, vy = ty + kBotOY;
  int best = -1;
  float bestD = 1e9f;
  for (int i = 0; i < (int)reach.size(); ++i) {
    auto& n = r.nodes[reach[i]];
    auto [x, y] = mapPos(n);
    float d = std::hypot(vx - x, vy - y), radius = n.type == RoomType::Boss ? 32.f : 17.f;
    if (d < radius && d < bestD) { best = i; bestD = d; }
  }
  return best;
}

void App::drawMap(bool top) {
  Run& r = *run_;
  auto reach = r.reachableNodes();
  if (mapSel_ >= (int)reach.size()) mapSel_ = 0;
  if (top && !mapUserScroll_) {  // once per frame: keep the next row low on the bottom screen
    int curRow = r.currentNode >= 0 ? r.nodes[r.currentNode].row : -1;
    float target = std::clamp((float)curRow - 0.75f, 0.f, kMapMaxScroll);
    mapScroll_ += (target - mapScroll_) * 0.15f;
  }

  // One sheet of map paper behind both screens.
  float bx = 0, by = 0;
  toLocal(top, bx, by);
  gfx::image(R().texture("gfx/bg_map.t3t"), 0, 0, kTop, kMapBgH, bx, by, kTop, kMapBgH, 0x000000FF, 0.1f);

  auto pos = [&](const MapNode& n) {
    auto p = mapPos(n);
    toLocal(top, p.first, p.second);
    return p;
  };
  // Edges
  for (auto& n : r.nodes) {
    auto [x0, y0] = pos(n);
    for (int ni : n.next) {
      auto [x1, y1] = pos(r.nodes[ni]);
      if (std::max(y0, y1) < -20 || std::min(y0, y1) > kH + 20) continue;
      bool travelled = n.visited && r.nodes[ni].visited;
      // dashed line
      float len = std::hypot(x1 - x0, y1 - y0);
      int dashes = (int)(len / 6);
      for (int d = 0; d < dashes; d += 2) {
        float t0 = (float)d / dashes, t1 = (float)(d + 1) / dashes;
        gfx::line(x0 + (x1 - x0) * t0, y0 + (y1 - y0) * t0, x0 + (x1 - x0) * t1, y0 + (y1 - y0) * t1, 2,
                  travelled ? 0x28231DFF : 0x87725699);
      }
    }
  }
  // Nodes
  for (int i = 0; i < (int)r.nodes.size(); ++i) {
    auto& n = r.nodes[i];
    auto [x, y] = pos(n);
    if (y < -40 || y > kH + 40) continue;
    bool reachable = std::find(reach.begin(), reach.end(), i) != reach.end();
    bool chosen = reachable && reach[mapSel_] == i;
    Sprite ic = roomIcon(n.type);
    float sz = n.type == RoomType::Boss ? 56 : 22;
    if (reachable) {
      float pulse = 1.f + 0.15f * std::sin((float)time_ * 6);
      sz *= chosen ? 1.35f * pulse : pulse;
      gfx::circle(x, y, sz * 0.7f, chosen ? 0xFFE07080 : 0xFFFFFF40);
    }
    uint32_t tint = n.visited ? 0x404040FF : 0xFFFFFFFF;
    float w = ic.w / ic.h * sz;
    spr(ic, x - w / 2, y - sz / 2, w, sz, tint, n.visited ? 0.5f : 0.f);
    if (i == r.currentNode) spr(R().sprite("map/marker"), x - 13, y - 30, 26, 26);
  }

  if (top) { drawTopBar(); return; }
  // Bottom HUD in the corners, clear of row 0.
  button(4, 210, 64, 26, "牌组", ID_DECK);
  button(72, 210, 64, 26, "遗物", ID_RELICS);
  if (!reach.empty()) {
    std::string label = roomName(r.nodes[reach[mapSel_]].type);
    float w = R().measure(label, ts(F12)) + 12;
    gfx::rect(kBot - w - 4, 214, w, 20, 0x000000A0);
    R().text(kBot - 10, 217, label, ts(F12, col::gold, RIGHT));
  }
}

// RGDSplus R4_MAP_TOUCH: drag anywhere to scroll; a press that never moved more than
// the slop and started on a reachable node enters it on release. A/←→ still work.
void App::updateMap(const gfx::Input& in) {
  Run& r = *run_;
  if (!r.mapChoice.waiting()) { mapTouch_ = {}; return; }
  auto reach = r.reachableNodes();
  int n = (int)reach.size();
  if (n == 0) return;
  if (in.down & (gfx::BTN_LEFT | gfx::BTN_RIGHT)) {
    mapSel_ = (mapSel_ + ((in.down & gfx::BTN_LEFT) ? n - 1 : 1)) % n;
    mapUserScroll_ = false;
  }
  if (in.down & gfx::BTN_Y) { deckOpen_ = true; sel_ = -1; scroll_ = 0; mapTouch_ = {}; return; }
  int pick = -1;
  if (in.down & gfx::BTN_A) pick = mapSel_;
  if (in.touchDown) {
    int hud = hitAt(in.tx, in.ty);
    if (hud == ID_DECK) { deckOpen_ = true; sel_ = -1; scroll_ = 0; mapTouch_ = {}; return; }
    if (hud == ID_RELICS) { relicsOpen_ = true; sel_ = run_->relics.empty() ? -1 : 0; scroll_ = 0; mapTouch_ = {}; return; }
    mapTouch_ = {};
    mapTouch_.down = true;
    mapTouch_.startX = in.tx;
    mapTouch_.startY = mapTouch_.lastY = in.ty;
    mapTouch_.node = mapNodeAt(in.tx, in.ty);
    if (mapTouch_.node >= 0) mapSel_ = mapTouch_.node;
  } else if (mapTouch_.down && in.touching) {
    if (std::hypot(in.tx - mapTouch_.startX, in.ty - mapTouch_.startY) > kMapTapSlop) mapTouch_.dragged = true;
    if (mapTouch_.dragged) {
      mapScroll_ = std::clamp(mapScroll_ + (in.ty - mapTouch_.lastY) / kRowH, 0.f, kMapMaxScroll);
      mapUserScroll_ = true;
    }
    mapTouch_.lastY = in.ty;
  }
  if (in.touchUp && mapTouch_.down) {
    if (!mapTouch_.dragged && mapTouch_.node >= 0) pick = mapTouch_.node;
    mapTouch_ = {};
  }
  if (pick >= 0) {
    r.mapChoice.fire(reach[pick]);
    mapSel_ = 0;
    mapUserScroll_ = false;
  }
}

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
  Sprite s = R().sprite("creature/" + (c->isPlayer ? std::string("IRONCLAD") : c->name));
  float dx = x;
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

// ---------------------------------------------------------------- dual-screen hand

namespace {
// HandPosHelper._cardPositionData / _cardAngleData (StS2).
const float kHandPos[10][10][2] = {
    {{0, -50}},
    {{-100, -50}, {100, -50}},
    {{-180, -50}, {0, -59}, {180, -50}},
    {{-240, -25}, {-80, -50}, {80, -50}, {240, -25}},
    {{-340, 10}, {-170, -30}, {0, -50}, {170, -30}, {340, 10}},
    {{-460, 13}, {-273, -25}, {-90, -50}, {90, -50}, {273, -25}, {460, 13}},
    {{-534, 18}, {-365, -14}, {-189, -39}, {0, -50}, {189, -39}, {365, -14}, {534, 18}},
    {{-565, 28}, {-400, -14}, {-231, -39}, {-80, -50}, {80, -50}, {231, -39}, {400, -14}, {565, 28}},
    {{-600, 37}, {-445, -2}, {-300, -29}, {-150, -45}, {0, -50}, {150, -45}, {300, -29}, {445, -2}, {600, 37}},
    {{-610, 38}, {-472, 5}, {-340, -21}, {-200, -41}, {-64, -50}, {64, -50}, {200, -41}, {340, -21}, {472, 5}, {610, 38}},
};
const float kHandAngle[10][10] = {
    {0}, {-2, 2}, {-3, 0, 3}, {-8, -4, 4, 8}, {-8, -4, 0, 4, 8}, {-9, -6, -3, 3, 6, 9}, {-9, -6, -3, 0, 3, 6, 9},
    {-12, -9, -6, -3, 3, 6, 9, 12}, {-12, -9, -6, -3, 0, 3, 6, 9, 12}, {-15, -12, -9, -6, -3, 3, 6, 9, 12, 15},
};

constexpr float kCardW = 120.f, kCardH = 169.f;   // drawCard size at s = 1
constexpr float kPreviewS = 0.95f;                 // tapped card, readable text
constexpr float kPreviewY = 6.f;                   // near the top edge (no status strip)
constexpr float kDragS = 0.62f;
constexpr float kArm = 8.f;      // lift to arm (24 px on RGDSplus)
constexpr float kSwitch = 20.f;  // sideways travel per target switch (64 px)
constexpr float kTapSlop = 5.f;
// Fan baseline: the hand is centred between the status bar and the bottom control row.
constexpr float kHandY = 130.f;  // centre card at ~49% of the screen, as on RGDSplus
// Top of the hand area. A card is played only when dragged above it; dragging it
// back below disarms it, so releasing over the hand always cancels.
constexpr float kPlayLine = 76.f;
}  // namespace

App::HandSlot App::handSlot(int n, int i) const {
  n = std::clamp(n, 1, 10);
  i = std::clamp(i, 0, n - 1);
  // Native units -> bottom-screen pixels. Like RGDSplus, small hands are drawn larger.
  float k = n <= 5 ? 0.25f : n <= 7 ? 0.23f : 0.215f;
  float mult = n == 8 ? 0.95f : n == 9 ? 0.9f : n == 10 ? 0.85f : 1.f;  // HandPosHelper.GetScale
  HandSlot h;
  h.x = kBot / 2.f + kHandPos[n - 1][i][0] * k;
  h.y = kHandY + kHandPos[n - 1][i][1] * k;
  h.angle = kHandAngle[n - 1][i] * 3.14159265f / 180.f;
  h.s = k * 2.f * mult;
  return h;
}

// Topmost hand card under a bottom-screen point: the preview first, then the fan right to left.
int App::hitHandCard(float tx, float ty) {
  Combat* cb = run_->combat.get();
  int n = (int)cb->hand.size();
  if (sel_ >= 0 && sel_ < n && !drag_.down) {
    float cx = std::clamp(handSlot(n, sel_).x, 70.f, kBot - 70.f), cy = kPreviewY + kCardH * kPreviewS / 2;
    if (std::fabs(tx - cx) < kCardW * kPreviewS / 2 && std::fabs(ty - cy) < kCardH * kPreviewS / 2) return sel_;
  }
  for (int i = n - 1; i >= 0; --i) {
    HandSlot h = handSlot(n, i);
    float dx = tx - h.x, dy = ty - h.y;
    float c = std::cos(-h.angle), s = std::sin(-h.angle);
    float lx = dx * c - dy * s, ly = dx * s + dy * c;
    if (std::fabs(lx) < kCardW * h.s / 2 && std::fabs(ly) < kCardH * h.s / 2) return i;
  }
  return -1;
}

// NTargetingArrow.UpdateArrowPosition / UpdateSegments, in virtual coordinates.
void App::drawArrow(bool top, float fx, float fy, float tx, float ty, bool locked, bool ally) {
  const float k = 240.f / 1080.f;  // StS2 1080p units -> top-screen pixels
  auto rot = [](float x, float y, float r, float& ox, float& oy) {
    ox = x * std::cos(r) - y * std::sin(r);
    oy = x * std::sin(r) + y * std::cos(r);
  };
  float hx, hy, ex, ey;
  rot(0, 88 * k, arrowRot_, hx, hy);
  hx += tx; hy += ty;
  rot(0, 40 * k, arrowRot_, ex, ey);
  ex += tx; ey += ty;
  float cx = fx - (hx - fx) * 0.25f;
  float cy = hy + (hy - fy) * 0.5f;  // From.Y > 540: the card is on the lower screen
  if (top) arrowRot_ = std::atan2(ty - cy, tx - cx) + 3.14159265f / 2;

  uint32_t tint = !locked ? 0xFFFFFFFF : ally ? 0x36C78AFF : 0xE61E1BFF;
  Sprite seg = R().sprite("ui/arrow_segment"), head = R().sprite("ui/arrow_head");
  float px[19], py[19];
  for (int i = 0; i < 19; ++i) {
    float t = i / 20.f, u = 1 - t;
    px[i] = u * u * fx + 2 * u * t * cx + t * t * ex;
    py[i] = u * u * fy + 2 * u * t * cy + t * t * ey;
  }
  auto drawRotated = [&](const Sprite& sp, float x, float y, float r, float sc) {
    toLocal(top, x, y);
    float w = sp.w * sc, h = sp.h * sc;
    float lim = top ? kTop : kBot;
    if (x < -w || x > lim + w || y < -h || y > kH + h) return;
    gfx::pushTransform(gfx::Affine::rotateAround(x, y, r));
    spr(sp, x - w / 2, y - h / 2, w, h, tint, 1.f);
    gfx::popTransform();
  };
  for (int i = 0; i < 19; ++i) {
    float sc = 0.28f + (0.42f - 0.28f) * (i * 2.f / 19.f);  // Mathf.Lerp, unclamped
    float r = i == 0 ? std::atan2(py[0] - py[1], px[0] - px[1]) - 3.14159265f / 2
                     : std::atan2(py[i] - py[i - 1], px[i] - px[i - 1]) + 3.14159265f / 2;
    drawRotated(seg, px[i], py[i], r, sc);
  }
  drawRotated(head, hx, hy, arrowRot_, locked ? 1.05f : 0.95f);
}

void App::startFlight(Card* c, float x, float y, float s, Creature* target) {
  Combat* cb = run_->combat.get();
  float x1 = kTop / 2.f, y1 = 110.f;  // battlefield centre for untargeted cards
  if (c->target == TargetType::Self) target = cb->player;
  if (target && centers_.count(target)) {
    x1 = centers_[target].first;
    y1 = centers_[target].second;
  }
  flights_.push_back({c, 0.f, x + kBotOX, y + kBotOY, x1, y1, s});
}

void App::drawFlights(bool top) {
  for (auto& f : flights_) {
    float t = std::min(1.f, f.t / 0.42f);
    float x = f.x0 + (f.x1 - f.x0) * t, y = f.y0 + (f.y1 - f.y0) * t;
    float s = f.s0 * (1.f - 0.94f * t);  // shrink to 6%
    toLocal(top, x, y);
    drawCard(f.card, x - kCardW * s / 2, y - kCardH * s / 2, s);
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

namespace {
constexpr float kDrawPileX = 16, kDrawPileY = 222, kDiscardX = 304, kDiscardY = 222;
constexpr float kDrawTime = 0.34f, kDrawGap = 0.12f, kLeaveTime = 0.28f, kLeaveGap = 0.045f;
float easeOut(float t) { t = 1 - t; return 1 - t * t * t; }
float easeIn(float t) { return t * t; }
float approach(float v, float to, float k, float dt) { return v + (to - v) * (1 - std::exp(-k * dt)); }
}  // namespace

// StS-style hand motion: drawn cards arc out of the draw pile one by one and
// grow into the fan; everything else eases to its slot; cards that leave the
// hand unplayed fly to their pile.
void App::animateHand(float dt) {
  Combat* cb = run_->combat.get();
  int n = (int)cb->hand.size();
  drawQueue_ = std::max(0.f, drawQueue_ - dt);
  leaveQueue_ = std::max(0.f, leaveQueue_ - dt);
  auto inPile = [](const std::vector<Card*>& v, Card* c) { return std::find(v.begin(), v.end(), c) != v.end(); };

  // Cards that left the hand.
  for (auto it = poses_.begin(); it != poses_.end();) {
    Card* c = it->first;
    if (inPile(cb->hand, c)) { ++it; continue; }
    bool flying = false;
    for (auto& f : flights_) flying |= f.card == c;
    Pose from = it->second;
    it = poses_.erase(it);
    if (flying || from.delay > 0) continue;
    Ghost g{c, from, 0, leaveQueue_, 0, 0, 0.1f, false};
    if (inPile(cb->discard, c)) { g.tx = kDiscardX; g.ty = kDiscardY; }
    else if (inPile(cb->draw, c)) { g.tx = kDrawPileX; g.ty = kDrawPileY; }
    else if (inPile(cb->exhaust, c)) { g.exhaust = true; g.tx = from.x; g.ty = from.y - 40; g.ts = from.s * 0.2f; }
    else continue;
    leaveQueue_ += kLeaveGap;
    ghosts_.push_back(g);
  }
  for (auto& g : ghosts_) {
    if (g.delay > 0) g.delay -= dt;
    else g.t += dt / kLeaveTime;
  }
  ghosts_.erase(std::remove_if(ghosts_.begin(), ghosts_.end(), [](const Ghost& g) { return g.t >= 1; }), ghosts_.end());

  // Cards in the hand.
  for (int i = 0; i < n; ++i) {
    Card* c = cb->hand[i];
    HandSlot h = handSlot(n, i);
    auto it = poses_.find(c);
    if (it == poses_.end()) {
      Pose p;
      p.x0 = p.x = kDrawPileX; p.y0 = p.y = kDrawPileY; p.a0 = p.angle = -0.9f; p.s0 = p.s = 0.12f;
      p.drawT = 0;
      p.delay = drawQueue_;
      drawQueue_ += kDrawGap;
      it = poses_.emplace(c, p).first;
    }
    Pose& p = it->second;
    // Held or previewed cards track where they are shown, so they glide back when let go.
    if (drag_.down && drag_.moved && drag_.card == c) {
      p.x = drag_.x; p.y = drag_.y; p.angle = 0; p.s = kDragS; p.delay = 0; p.drawT = 1;
      continue;
    }
    if (i == sel_ && !drag_.down && p.delay <= 0) {
      p.x = std::clamp(h.x, 70.f, kBot - 70.f); p.y = kPreviewY + kCardH * kPreviewS / 2;
      p.angle = 0; p.s = kPreviewS; p.drawT = 1;
      continue;
    }
    if (p.delay > 0) { p.delay -= dt; continue; }
    if (p.drawT < 1) {
      // Arc from the pile towards the (moving) slot.
      p.drawT = std::min(1.f, p.drawT + dt / kDrawTime);
      float t = easeOut(p.drawT);
      float cx = (p.x0 + h.x) / 2, cy = std::min(p.y0, h.y) - 45;
      float u = 1 - t;
      p.x = u * u * p.x0 + 2 * u * t * cx + t * t * h.x;
      p.y = u * u * p.y0 + 2 * u * t * cy + t * t * h.y;
      p.angle = p.a0 + (h.angle - p.a0) * t;
      p.s = p.s0 + (h.s - p.s0) * t;
      continue;
    }
    p.x = approach(p.x, h.x, 16, dt);
    p.y = approach(p.y, h.y, 16, dt);
    p.angle = approach(p.angle, h.angle, 16, dt);
    p.s = approach(p.s, h.s, 16, dt);
  }
}

void App::drawGhosts() {
  for (auto& g : ghosts_) {
    if (g.delay > 0) continue;
    float t = g.exhaust ? easeOut(g.t) : easeIn(g.t);
    float x = g.from.x + (g.tx - g.from.x) * t, y = g.from.y + (g.ty - g.from.y) * t;
    if (!g.exhaust) y -= std::sin(g.t * 3.14159265f) * 30;  // small hop towards the pile
    float s = g.from.s + (g.ts - g.from.s) * t;
    float a = g.from.angle + (g.exhaust ? 0.f : 0.8f) * t;
    gfx::pushTransform(gfx::Affine::rotateAround(x, y, a));
    drawCard(g.card, x - kCardW * s / 2, y - kCardH * s / 2, s, g.exhaust, false, false);
    gfx::popTransform();
  }
}

void App::drawCombat(bool top) {
  Combat* cb = run_->combat.get();
  if (!cb) return;
  auto enemies = visibleEnemies();
  auto alive = cb->aliveEnemies();
  int n = (int)cb->hand.size();
  if (sel_ >= n) sel_ = -1;
  if (target_ >= (int)alive.size()) target_ = 0;
  Card* selCard = sel_ >= 0 ? cb->hand[sel_] : nullptr;
  bool canAct = cb->playerPhase && cb->actions.waiting();

  // Which card aims where this frame.
  Creature* tgt = nullptr;
  bool arrow = false, arrowAlly = false;
  float afx = 0, afy = 0;
  if (drag_.down && drag_.moved && drag_.armed && drag_.card) {
    afx = drag_.x + kBotOX;
    afy = drag_.y - kCardH * kDragS / 2 + kBotOY;
    if (drag_.card->target == TargetType::AnyEnemy && drag_.target) { tgt = drag_.target; arrow = true; }
    else if (drag_.card->target == TargetType::Self) { tgt = cb->player; arrow = arrowAlly = true; }
  } else if (aiming_ && selCard && !alive.empty()) {
    float cx = std::clamp(handSlot(n, sel_).x, 70.f, kBot - 70.f);
    afx = cx + kBotOX;
    afy = kPreviewY + kBotOY;
    tgt = alive[target_];
    arrow = true;
  }
  float atx = 0, aty = 0;
  if (arrow && centers_.count(tgt)) { atx = centers_[tgt].first; aty = centers_[tgt].second; }
  else arrow = false;

  if (top) {
    gfx::Texture* bg = R().texture("gfx/bg_overgrowth.t3t");
    gfx::image(bg, 0, 0, kTop, kH, 0, 0, kTop, kH);
    gfx::rectGradient(0, 150, kTop, 90, 0x00000000, 0x00000060);
    const float feet = 170;  // ~70% down the screen, the room's floor line (RGDSplus/native)
    auto center = [&](Creature* c, float x) {
      Sprite s = R().sprite("creature/" + (c->isPlayer ? std::string("IRONCLAD") : c->name));
      centers_[c] = {x, s ? feet - s.ay + s.h / 2.f : feet - 30};
    };
    center(cb->player, 95);
    drawCreature(cb->player, 95, feet, tgt == cb->player && !arrowAlly);
    for (int i = 0; i < (int)enemies.size(); ++i) {
      float x = enemyX(i, (int)enemies.size());
      center(enemies[i], x);
      bool aoe = drag_.down && drag_.armed && drag_.card && drag_.card->target == TargetType::AllEnemies;
      drawCreature(enemies[i], x, feet, enemies[i] == tgt || (aoe && enemies[i]->alive()));
    }
    for (auto& f : floats_) {
      if (f.t < 0) continue;
      float x = 95, y = feet - 60;
      if (f.who && !f.who->isPlayer)
        for (int i = 0; i < (int)enemies.size(); ++i)
          if (enemies[i] == f.who) x = enemyX(i, (int)enemies.size());
      float a = std::clamp(1.2f - f.t, 0.f, 1.f);
      TextStyle st = ts(F16, (f.color & 0xFFFFFF00) | (uint32_t)(a * 255), CENTER);
      st.scale = 1.3f;
      R().text(x + f.dx, y - f.t * 40, f.text, st);
    }
    drawTopBar();
    if (cb->bannerTime > 0) {
      cb->bannerTime -= 1.f / 60;
      float a = std::min(1.f, cb->bannerTime * 2);
      gfx::rect(0, 96, kTop, 40, (uint32_t)(a * 0xA0));
      TextStyle st = ts(F16, col::gold, CENTER);
      st.scale = 1.6f;
      R().text(kTop / 2, 104, cb->banner, st);
    }
    if (arrow) drawArrow(true, afx, afy, atx, aty, true, arrowAlly);
    drawFlights(true);
    return;
  }

  // ---- bottom screen (RGDSplus layout): the same room behind, no status strip, the hand
  // centred, energy and end turn at the sides below it, piles in the bottom corners.
  gfx::Texture* room = R().texture("gfx/bg_overgrowth.t3t");
  gfx::image(room, kBotOX, 0, kBot, kH, 0, 0, kBot, kH, 0x000000FF, 0.15f);

  if (cb->choice.active) {
    gfx::rect(0, 0, kBot, kH, 0x000000A0);
    R().text(kBot / 2, 4, L("cards." + cb->choice.prompt + ".title") + "：选择一张牌", ts(F16, col::gold, CENTER));
    drawCardGrid(cb->choice.options, sel_, 26, 196, scroll_);
    button(kBot - 110, 200, 100, 34, "确认", ID_CONFIRM, sel_ >= 0 && sel_ < (int)cb->choice.options.size());
    if (sel_ >= 0 && sel_ < (int)cb->choice.options.size()) R().text(10, 206, cardTitle(cb->choice.options[sel_]), ts(F16, col::white));
    return;
  }

  // Energy orb left and end turn right, level with each other a little below the hand
  // (~77% down, ~10% / ~87% across on RGDSplus).
  Sprite orb = R().sprite("ui/energy_orb");
  const float ox = 32, oy = 185, od = 34;
  spr(orb, ox - od / 2, oy - od / 2, od, od);
  TextStyle et = ts(F12, cb->energy > 0 ? col::white : col::gray, CENTER);
  R().text(ox, oy - R().lineHeight(F12) / 2, num(cb->energy) + "/" + num(cb->maxEnergyNow()), et);
  Sprite endTurn = R().sprite("ui/end_turn");
  const float ew = 64, eh = 32, ex = 278 - ew / 2, ey = oy - eh / 2;  // sprite is 2:1
  spr(endTurn, ex, ey, ew, eh, canAct ? 0xFFFFFFFF : 0x000000FF, canAct ? 0 : 0.5f);
  {
    // The visible plate is ~78% x 62% of the sprite, centred; the label fills 80% of it.
    const float pw = ew * 0.78f, ph = eh * 0.62f;
    TextStyle lt = ts(F16, canAct ? col::white : col::gray, CENTER);
    // The text box takes 80% of the plate; CJK ink sits a little below the box middle (measured).
    const float inkMid = 0.53f;
    float lw = R().measure("结束", lt), lh = R().lineHeight(F16);
    lt.scale = std::min(0.8f * pw / lw, 0.8f * ph / lh);
    R().text(ex + ew / 2, ey + eh / 2 - lh * lt.scale * inkMid, "结束", lt);
  }
  if (canAct) hits_.push_back({ex, ey - 4, ew, eh + 8, ID_END_TURN});
  // Piles: small icons in the corners with a red count badge.
  auto pile = [&](const char* sprite, float cx, float cy, int count) {
    spr(R().sprite(sprite), cx - 12, cy - 12, 24, 24);
    gfx::circle(cx + 10, cy + 8, 7, 0xC02828FF);
    R().text(cx + 10, cy + 8 - R().lineHeight(F12) * 0.4f, num(count), ts(F12, col::white, CENTER, 0, 0.8f));
  };
  int waiting = 0;  // drawn cards still sitting on the pile, waiting for their turn to fly
  for (auto& [c, p] : poses_) waiting += p.delay > 0;
  pile("ui/draw_pile", kDrawPileX, kDrawPileY, (int)cb->draw.size() + waiting);
  pile("ui/discard_pile", kDiscardX, kDiscardY, (int)cb->discard.size());
  if (!cb->exhaust.empty()) R().text(kBot - 4, 200, "消耗 " + num((int)cb->exhaust.size()), ts(F12, col::gray, RIGHT));

  auto flying = [&](Card* c) {
    for (auto& f : flights_) if (f.card == c) return true;
    return false;
  };
  drawGhosts();
  // Fan, left to right so right cards overlap left ones (native order); cards still
  // arriving from the draw pile go on top.
  for (int pass = 0; pass < 2; ++pass)
    for (int i = 0; i < n; ++i) {
      Card* c = cb->hand[i];
      if (flying(c) || (drag_.down && drag_.moved && drag_.card == c) || (i == sel_ && !drag_.down)) continue;
      auto it = poses_.find(c);
      if (it == poses_.end() || it->second.delay > 0) continue;
      const Pose& p = it->second;
      if ((p.drawT < 1) != (pass == 1)) continue;
      gfx::pushTransform(gfx::Affine::rotateAround(p.x, p.y, p.angle));
      drawCard(c, p.x - kCardW * p.s / 2, p.y - kCardH * p.s / 2, p.s, !cb->canPlay(c) && canAct, true, false);
      gfx::popTransform();
    }
  // Tapped card: raised and enlarged so its text is readable (native hover).
  if (selCard && !flying(selCard) && !(drag_.down && drag_.moved && drag_.card == selCard)) {
    float cx = std::clamp(handSlot(n, sel_).x, 70.f, kBot - 70.f);
    drawCard(selCard, cx - kCardW * kPreviewS / 2, kPreviewY, kPreviewS, !cb->canPlay(selCard) && canAct, true, aiming_);
    std::string why;
    if (canAct && !cb->canPlay(selCard, &why)) {
      std::string msg = why == "ENERGY" ? L("combat_messages.NOT_ENOUGH_ENERGY") : L("combat_messages.UNPLAYABLE");
      for (auto& ch : msg) if (ch == '\n') ch = ' ';
      R().text(cx, kPreviewY + kCardH * kPreviewS + 2, msg, ts(F12, col::red, CENTER));
    }
  }
  // Play line and cancel zone while dragging; the hint sits between the energy orb and
  // end turn, below the hand, where neither the finger nor the card covers it.
  if (drag_.down && drag_.moved && drag_.card && !flying(drag_.card)) {
    float pulse = 0.5f + 0.5f * std::sin(clock_ * 6.f);
    std::string hint;
    uint32_t hc;
    if (drag_.armed) {
      gfx::rect(0, kPlayLine, kBot, kH - kPlayLine, 0x00000060);
      gfx::rect(0, kPlayLine, kBot, 1, 0xFFFFFF60);
      hint = "松手打出 · 拖回手牌取消";
      hc = 0x90FF90FF;
    } else {
      gfx::rect(0, 0, kBot, kPlayLine, 0x60C0FF00 | (uint32_t)(0x10 + pulse * 0x18));
      for (float x = 4; x < kBot; x += 12) gfx::rect(x, kPlayLine, 6, 2, 0x60C0FFC0);
      hint = "↑ 拖过虚线出牌 · 松手取消";
      hc = 0x90D0FFFF;
    }
    TextStyle st = ts(F12, hc, CENTER);
    float w = R().measure(hint, st);
    gfx::rect(kBot / 2 - w / 2 - 6, 200, w + 12, 16, 0x000000B0);
    R().text(kBot / 2, 201, hint, st);
  }
  // Card being dragged, following the finger; glows when it is in the play zone.
  if (drag_.down && drag_.moved && drag_.card && !flying(drag_.card)) {
    float s = kDragS;
    if (drag_.armed) gfx::rect(drag_.x - kCardW * s / 2 - 3, drag_.y - kCardH * s / 2 - 3, kCardW * s + 6, kCardH * s + 6, 0x60D0FF90);
    drawCard(drag_.card, drag_.x - kCardW * s / 2, drag_.y - kCardH * s / 2, s, false, true, false);
  }
  if (arrow) drawArrow(false, afx, afy, atx, aty, true, arrowAlly);
  drawFlights(false);
}

void App::updateCombat(const gfx::Input& in) {
  Combat* cb = run_->combat.get();
  if (!cb) return;
  clock_ += (float)gfx::dt();
  animateHand((float)gfx::dt());
  for (auto& f : flights_) f.t += (float)gfx::dt();
  flights_.erase(std::remove_if(flights_.begin(), flights_.end(), [](const Flight& f) { return f.t > 0.42f; }),
                 flights_.end());
  auto alive = cb->aliveEnemies();
  int n = (int)cb->hand.size();

  if (cb->choice.active) {
    drag_ = {};
    aiming_ = false;
    int m = (int)cb->choice.options.size();
    if (!cb->choice.result.waiting()) return;
    if (in.down & gfx::BTN_RIGHT) sel_ = std::min(m - 1, sel_ + 1);
    if (in.down & gfx::BTN_LEFT) sel_ = std::max(0, sel_ - 1);
    if (in.down & gfx::BTN_DOWN) sel_ = std::min(m - 1, sel_ + 5);
    if (in.down & gfx::BTN_UP) sel_ = std::max(0, sel_ - 5);
    if (sel_ >= 0) scroll_ = std::max(0, sel_ / 5 - 1);
    bool confirm = (in.down & gfx::BTN_A) && sel_ >= 0 && sel_ < m;
    if (in.touchDown) {
      int id = hitAt(in.tx, in.ty);
      if (id >= ID_GRID0 && id - ID_GRID0 < m) {
        if (sel_ == id - ID_GRID0) confirm = true;
        sel_ = id - ID_GRID0;
      }
      if (id == ID_CONFIRM && sel_ >= 0) confirm = true;
    }
    if (confirm) {
      cb->choice.result.fire({cb->choice.options[sel_]});
      sel_ = -1;
      scroll_ = 0;
    }
    return;
  }

  bool canAct = cb->playerPhase && cb->actions.waiting();
  if (sel_ >= n) sel_ = -1;
  if (!canAct) {
    drag_ = {};
    aiming_ = false;
    return;
  }
  if (target_ >= (int)alive.size()) target_ = 0;

  auto play = [&](Card* c, Creature* t, float x, float y, float s) {
    if (!cb->canPlay(c)) return false;
    if (c->target == TargetType::AnyEnemy && (!t || t->dead())) return false;
    PlayerAction a;
    a.kind = PlayerAction::PlayCard;
    a.card = c;
    a.target = c->target == TargetType::AnyEnemy ? t : nullptr;
    cb->actions.fire(a);
    startFlight(c, x, y, s, a.target);
    sel_ = -1;
    aiming_ = false;
    return true;
  };

  // ---- touch: drag to play (RGDSplus R4 drag-lock rules)
  if (in.touchDown) {
    int i = hitHandCard(in.tx, in.ty);
    if (i >= 0) {
      drag_ = {};
      drag_.down = true;
      drag_.index = i;
      drag_.card = cb->hand[i];
      float cx, cy;
      if (i == sel_) {
        cx = std::clamp(handSlot(n, i).x, 70.f, kBot - 70.f);
        cy = kPreviewY + kCardH * kPreviewS / 2;
      } else {
        HandSlot h = handSlot(n, i);
        cx = h.x;
        cy = h.y;
      }
      drag_.grabDX = 0;  // the dragged card centres on the finger
      drag_.grabDY = 0;
      drag_.originY = in.ty;
      drag_.x = cx;
      drag_.y = cy;
      drag_.startTx = drag_.lastTx = in.tx;
      drag_.startTy = in.ty;
      aiming_ = false;
    } else {
      int id = hitAt(in.tx, in.ty);
      if (id == ID_END_TURN) {
        cb->actions.fire({PlayerAction::EndTurn});
        sel_ = -1;
        aiming_ = false;
        return;
      }
      sel_ = -1;
      aiming_ = false;
    }
  }
  if (drag_.down && in.touching) {
    if (!drag_.moved && std::hypot(in.tx - drag_.startTx, in.ty - drag_.startTy) > kTapSlop) {
      drag_.moved = true;
      if (sel_ != drag_.index) sel_ = -1;
    }
    if (drag_.moved) {
      drag_.x = (float)in.tx;
      drag_.y = (float)in.ty;
      float lift = drag_.originY - in.ty;
      bool needsTarget = drag_.card->target == TargetType::AnyEnemy;
      if (!drag_.armed && lift >= kArm && in.ty < kPlayLine) {
        drag_.armed = true;
        drag_.accum = 0;
        drag_.lastTx = in.tx;
        if (needsTarget) {
          // Lock the enemy nearest to the card in the two-screen space.
          float vx = drag_.x + kBotOX, vy = drag_.y + kBotOY, best = 1e9f;
          drag_.target = nullptr;
          for (auto* e : alive) {
            if (!centers_.count(e)) continue;
            float d = std::hypot(centers_[e].first - vx, centers_[e].second - vy);
            if (d < best) { best = d; drag_.target = e; }
          }
        }
      } else if (drag_.armed && (lift <= 0 || in.ty > kPlayLine + 6)) {
        drag_.armed = false;  // dragged back: unlock, gesture continues
        drag_.target = nullptr;
      }
      if (drag_.armed && needsTarget && drag_.target) {
        drag_.accum += in.tx - drag_.lastTx;
        drag_.lastTx = in.tx;
        std::vector<Creature*> order = alive;
        std::sort(order.begin(), order.end(), [&](Creature* a, Creature* b) { return centers_[a].first < centers_[b].first; });
        int idx = (int)(std::find(order.begin(), order.end(), drag_.target) - order.begin());
        while (drag_.accum >= kSwitch && idx + 1 < (int)order.size()) { ++idx; drag_.accum -= kSwitch; }
        while (drag_.accum <= -kSwitch && idx > 0) { --idx; drag_.accum += kSwitch; }
        if (idx < (int)order.size()) drag_.target = order[idx];
        drag_.accum = std::clamp(drag_.accum, -kSwitch, kSwitch);
      }
    }
  }
  if (drag_.down && in.touchUp) {
    if (!drag_.moved) {
      sel_ = sel_ == drag_.index ? -1 : drag_.index;  // tap: preview / hide
    } else {
      bool edge = in.tx <= 2 || in.ty <= 2 || in.tx >= kBot - 3 || in.ty >= kH - 3;
      if (drag_.armed && !edge) play(drag_.card, drag_.target, drag_.x, drag_.y, kDragS);
    }
    drag_ = {};
  }
  if (drag_.down && (in.down & gfx::BTN_B)) drag_ = {};

  // ---- buttons: L/R or ←→ choose a card, A to aim/play, ←→ choose an enemy, A confirm, B back.
  if (drag_.down) return;
  Card* selCard = sel_ >= 0 ? cb->hand[sel_] : nullptr;
  if (in.down & gfx::BTN_Y) { deckOpen_ = true; sel_ = -1; aiming_ = false; return; }
  if (in.down & gfx::BTN_X) { cb->actions.fire({PlayerAction::EndTurn}); sel_ = -1; aiming_ = false; return; }
  if (in.down & gfx::BTN_B) {
    if (aiming_) aiming_ = false;
    else sel_ = -1;
  }
  if (in.down & gfx::BTN_R) { sel_ = n ? (sel_ + 1) % n : -1; aiming_ = false; }
  if (in.down & gfx::BTN_L) { sel_ = n ? (sel_ <= 0 ? n - 1 : sel_ - 1) : -1; aiming_ = false; }
  if (in.down & (gfx::BTN_LEFT | gfx::BTN_RIGHT)) {
    int d = (in.down & gfx::BTN_RIGHT) ? 1 : -1;
    if (aiming_ && !alive.empty()) target_ = std::clamp(target_ + d, 0, (int)alive.size() - 1);
    else if (n) sel_ = sel_ < 0 ? 0 : (sel_ + d + n) % n;
  }
  if (in.down & gfx::BTN_A) {
    if (!selCard) {
      sel_ = n ? 0 : -1;
    } else if (selCard->target == TargetType::AnyEnemy && !aiming_) {
      if (cb->canPlay(selCard) && !alive.empty()) aiming_ = true;
    } else {
      float cx = std::clamp(handSlot(n, sel_).x, 70.f, kBot - 70.f);
      play(selCard, alive.empty() ? nullptr : alive[target_], cx, kPreviewY + kCardH * kPreviewS / 2, kPreviewS);
    }
  }
}

// ================================================================ reward

void App::drawReward(bool top) {
  Run& r = *run_;
  int n = (int)r.rewardCards.size();
  if (top) {
    gfx::Texture* bg = R().texture("gfx/bg_overgrowth.t3t");
    gfx::image(bg, 0, 0, kTop, kH, 0, 0, kTop, kH, 0x000000FF, 0.55f);
    drawTopBar();
    if (sel_ >= 0 && sel_ < n) {
      drawCard(r.rewardCards[sel_].get(), (kTop - 132) / 2, 34, 1.1f, false, true);
    } else {
      TextStyle t = ts(F16, col::gold, CENTER);
      t.scale = 1.6f;
      R().text(kTop / 2, 70, "战斗胜利！", t);
      R().text(kTop / 2, 120, L("gameplay_ui.COMBAT_REWARD_ADD_CARD"), ts(F16, col::white, CENTER));
    }
    return;
  }
  drawSceneBg(false, 0.55f);
  R().text(kBot / 2, 4, L("gameplay_ui.CHOOSE_CARD_HEADER"), ts(F16, col::gold, CENTER));
  const float s = 0.78f, cw = 120 * s;
  float gap = (kBot - 3 * cw) / 4;
  for (int i = 0; i < n; ++i) {
    float x = gap + i * (cw + gap), y = 28;
    drawCard(r.rewardCards[i].get(), x, y, s, false, false, i == sel_);
    hits_.push_back({x, y, cw, 169 * s, ID_REWARD0 + i});
  }
  button(10, 196, 110, 36, L("gameplay_ui.CHOOSE_CARD_SKIP_BUTTON"), ID_SKIP);
  button(kBot - 120, 196, 110, 36, "选择", ID_CONFIRM, sel_ >= 0 && sel_ < n, true);
}

void App::updateReward(const gfx::Input& in) {
  Run& r = *run_;
  if (!r.rewardChoice.waiting()) return;
  int n = (int)r.rewardCards.size();
  if (in.down & gfx::BTN_RIGHT) sel_ = std::min(n - 1, sel_ + 1);
  if (in.down & gfx::BTN_LEFT) sel_ = std::max(0, sel_ - 1);
  if ((in.down & gfx::BTN_A) && sel_ >= 0) { r.rewardChoice.fire(sel_); return; }
  if (in.down & gfx::BTN_B) { r.rewardChoice.fire(-1); return; }
  if (in.touchDown) {
    int id = hitAt(in.tx, in.ty);
    if (id >= ID_REWARD0 && id < ID_REWARD0 + n) {
      if (sel_ == id - ID_REWARD0) { r.rewardChoice.fire(sel_); return; }
      sel_ = id - ID_REWARD0;
    }
    if (id == ID_CONFIRM && sel_ >= 0) r.rewardChoice.fire(sel_);
    if (id == ID_SKIP) r.rewardChoice.fire(-1);
  }
}

// ================================================================ rest

void App::drawRest(bool top) {
  Run& r = *run_;
  int heal = (Dec(r.player->maxHp) * Dec::lit(0.3)).toInt();
  if (top) {
    gfx::Texture* bg = R().texture("gfx/bg_overgrowth.t3t");
    gfx::image(bg, 0, 0, kTop, kH, 0, 0, kTop, kH, 0x100400FF, 0.6f);
    gfx::circle(200, 200, 40, 0xFF802040);
    gfx::circle(200, 204, 22, 0xFFB04060);
    Sprite ic = R().sprite("creature/IRONCLAD");
    spr(ic, 120 - ic.ax, 206 - ic.ay);
    TextStyle t = ts(F16, col::gold, CENTER);
    t.scale = 1.4f;
    R().text(kTop / 2, 40, L("map.LEGEND_REST.hoverTip.title"), t);
    R().text(kTop / 2, 76, L("rest_site_ui.PROMPT"), ts(F16, col::white, CENTER));
    drawTopBar();
    return;
  }
  drawSceneBg(false, 0.5f);
  bool canSmith = false;
  for (auto& c : r.deck) if (c->upgradable()) canSmith = true;
  button(16, 30, 138, 100, L("rest_site_ui.OPTION_HEAL.name"), ID_HEAL, r.restChoice.waiting(), sel_ == 0);
  button(166, 30, 138, 100, L("rest_site_ui.OPTION_SMITH.name"), ID_SMITH, r.restChoice.waiting() && canSmith, sel_ == 1);
  R().text(85, 140, "回复 " + num(heal) + " 点生命", ts(F12, col::green, CENTER));
  R().text(235, 140, "升级一张牌", ts(F12, col::gold, CENTER));
  R().text(80, 206, "生命 " + num(r.player->hp) + "/" + num(r.player->maxHp), ts(F16, col::red, CENTER));
  // RGDSplus U22: pick an option, then confirm.
  button(kBot - 120, 196, 110, 36, "确认", ID_CONFIRM, r.restChoice.waiting() && (sel_ == 0 || (sel_ == 1 && canSmith)), true);
}

void App::updateRest(const gfx::Input& in) {
  Run& r = *run_;
  if (!r.restChoice.waiting()) return;
  bool canSmith = false;
  for (auto& c : r.deck) if (c->upgradable()) canSmith = true;
  auto valid = [&](int s) { return s == 0 || (s == 1 && canSmith); };
  if (in.down & gfx::BTN_LEFT) sel_ = 0;
  if (in.down & gfx::BTN_RIGHT) sel_ = 1;
  if ((in.down & gfx::BTN_A) && valid(sel_)) { r.restChoice.fire(sel_); return; }
  if (in.touchDown) {
    int id = hitAt(in.tx, in.ty);
    int pick = id == ID_HEAL ? 0 : id == ID_SMITH ? 1 : -1;
    if (pick >= 0) {
      if (sel_ == pick) { r.restChoice.fire(pick); return; }  // second tap confirms
      sel_ = pick;
    }
    if (id == ID_CONFIRM && valid(sel_)) r.restChoice.fire(sel_);
  }
}

void App::drawUpgrade(bool top) {
  Run& r = *run_;
  auto& opts = r.upgradeOptions;
  if (top) {
    drawSceneBg(true, 0.7f);
    drawTopBar();
    if (sel_ >= 0 && sel_ < (int)opts.size()) {
      Card* c = opts[sel_];
      auto up = c->clone();
      up->upgrade();
      drawCard(c, 50, 36, 1.05f, false, true);
      R().text(kTop / 2, 110, "→", ts(F16, col::gold, CENTER, 0, 2.f));
      drawCard(up.get(), 224, 36, 1.05f, false, true);
    } else {
      R().text(kTop / 2, 100, L("gameplay_ui.CHOOSE_CARD_UPGRADE_HEADER"), ts(F16, col::gold, CENTER));
    }
    return;
  }
  drawSceneBg(false, 0.65f);
  drawCardGrid(opts, sel_, 0, 196, scroll_);
  gfx::rect(0, 196, kBot, 44, 0x000000A0);
  button(10, 200, 100, 34, "返回", ID_BACK);
  button(kBot - 110, 200, 100, 34, "升级", ID_CONFIRM, sel_ >= 0 && sel_ < (int)opts.size(), true);
}

void App::updateUpgrade(const gfx::Input& in) {
  Run& r = *run_;
  if (!r.upgradeChoice.waiting()) return;
  int m = (int)r.upgradeOptions.size();
  if (in.down & gfx::BTN_RIGHT) sel_ = std::min(m - 1, sel_ + 1);
  if (in.down & gfx::BTN_LEFT) sel_ = std::max(0, sel_ - 1);
  if (in.down & gfx::BTN_DOWN) sel_ = std::min(m - 1, sel_ + 5);
  if (in.down & gfx::BTN_UP) sel_ = std::max(0, sel_ - 5);
  if (sel_ >= 0) scroll_ = std::max(0, sel_ / 5 - 1);
  if ((in.down & gfx::BTN_A) && sel_ >= 0) { r.upgradeChoice.fire(sel_); return; }
  if (in.down & gfx::BTN_B) { r.upgradeChoice.fire(-1); return; }
  if (in.touchDown) {
    int id = hitAt(in.tx, in.ty);
    if (id >= ID_GRID0 && id - ID_GRID0 < m) {
      if (sel_ == id - ID_GRID0) { r.upgradeChoice.fire(sel_); return; }
      sel_ = id - ID_GRID0;
    }
    if (id == ID_CONFIRM && sel_ >= 0) r.upgradeChoice.fire(sel_);
    if (id == ID_BACK) r.upgradeChoice.fire(-1);
  }
}

// ================================================================ deck view

void App::drawDeck(bool top) {
  std::vector<Card*> cards;
  for (auto& c : run_->deck) cards.push_back(c.get());
  if (top) {
    drawSceneBg(true, 0.7f);
    drawTopBar();
    if (sel_ >= 0 && sel_ < (int)cards.size()) drawCard(cards[sel_], (kTop - 132) / 2, 34, 1.1f, false, true);
    else R().text(kTop / 2, 100, "牌组（" + num((int)cards.size()) + " 张）", ts(F16, col::gold, CENTER));
    return;
  }
  drawSceneBg(false, 0.65f);
  drawCardGrid(cards, sel_, 0, 196, scroll_);
  gfx::rect(0, 196, kBot, 44, 0x000000A0);
  button(10, 200, 100, 34, "返回", ID_BACK);
  button(kBot - 110, 200, 100, 34, "遗物", ID_RELICS);
}

void App::updateDeck(const gfx::Input& in) {
  int m = (int)run_->deck.size();
  if (in.down & gfx::BTN_RIGHT) sel_ = std::min(m - 1, sel_ + 1);
  if (in.down & gfx::BTN_LEFT) sel_ = std::max(0, sel_ - 1);
  if (in.down & gfx::BTN_DOWN) sel_ = std::min(m - 1, sel_ + 5);
  if (in.down & gfx::BTN_UP) sel_ = std::max(0, sel_ - 5);
  if (sel_ >= 0) scroll_ = std::max(0, sel_ / 5 - 1);
  if (in.down & (gfx::BTN_B | gfx::BTN_Y)) { deckOpen_ = false; sel_ = -1; return; }
  if (in.touchDown) {
    int id = hitAt(in.tx, in.ty);
    if (id >= ID_GRID0) sel_ = id - ID_GRID0;
    if (id == ID_BACK) { deckOpen_ = false; sel_ = -1; }
    if (id == ID_RELICS) { deckOpen_ = false; relicsOpen_ = true; sel_ = run_->relics.empty() ? -1 : 0; scroll_ = 0; }
  }
}

// ================================================================ relics

// Elite relic reward / treasure chest: the relic is shown large on top (RGDSplus
// U19/U23: focus on top, take/skip below).
void App::drawRelicOffer(bool top) {
  Run& r = *run_;
  Relic* rel = r.relicOffer.get();
  if (top) {
    drawSceneBg(true, 0.6f);
    drawTopBar();
    TextStyle t = ts(F16, col::gold, CENTER);
    R().text(kTop / 2, 24, r.relicOfferFromChest ? "宝箱" : "精英战利品", t);
    if (rel) drawRelicDetail(rel, 88);
    return;
  }
  drawSceneBg(false, 0.55f);
  if (r.relicOfferFromChest) {
    Sprite chest = R().sprite("map/chest");
    spr(chest, kBot / 2 - 22, 18, 44, 44);
  }
  if (rel) {
    float s = 40, x = kBot / 2 - s / 2, y = 74;
    gfx::circle(kBot / 2.f, y + s / 2, 30, 0xFFE07040);
    drawRelicIcon(rel, x, y, s);
    hits_.push_back({x - 10, y - 10, s + 20, s + 20, ID_TAKE});
    R().text(kBot / 2, y + s + 8, L("relics." + rel->locKey + ".title"), ts(F16, col::white, CENTER));
  }
  button(10, 196, 110, 36, "跳过", ID_SKIP);
  button(kBot - 120, 196, 110, 36, "拿取", ID_TAKE, rel != nullptr, true);
}

void App::updateRelicOffer(const gfx::Input& in) {
  Run& r = *run_;
  if (!r.relicChoice.waiting()) return;
  if (in.down & gfx::BTN_A) { r.relicChoice.fire(1); return; }
  if (in.down & gfx::BTN_B) { r.relicChoice.fire(0); return; }
  if (in.touchDown) {
    int id = hitAt(in.tx, in.ty);
    if (id == ID_TAKE) r.relicChoice.fire(1);
    else if (id == ID_SKIP) r.relicChoice.fire(0);
  }
}

// Owned relics: a grid below, the selected one described above (RGDSplus U24/U25).
void App::drawRelics(bool top) {
  auto& rels = run_->relics;
  int n = (int)rels.size();
  if (sel_ >= n) sel_ = n - 1;
  if (top) {
    drawSceneBg(true, 0.7f);
    drawTopBar();
    if (sel_ >= 0) drawRelicDetail(rels[sel_].get(), 84);
    else R().text(kTop / 2, 100, "遗物（" + num(n) + " 个）", ts(F16, col::gold, CENTER));
    return;
  }
  drawSceneBg(false, 0.65f);
  const int cols = 6;
  const float cell = 48, x0 = (kBot - cols * cell) / 2, y0 = 8;
  for (int i = 0; i < n; ++i) {
    float x = x0 + (i % cols) * cell, y = y0 + (i / cols - scroll_) * cell;
    if (y < 0 || y > 190) continue;
    if (i == sel_) gfx::rect(x + 2, y + 2, cell - 4, cell - 4, 0xFFE07060);
    drawRelicIcon(rels[i].get(), x + 6, y + 6, cell - 12);
    hits_.push_back({x, y, cell, cell, ID_RELIC0 + i});
  }
  gfx::rect(0, 196, kBot, 44, 0x000000A0);
  button(10, 200, 100, 34, "返回", ID_BACK);
  button(kBot - 110, 200, 100, 34, "牌组", ID_DECK);
}

void App::updateRelics(const gfx::Input& in) {
  int n = (int)run_->relics.size();
  auto close = [&] { relicsOpen_ = false; sel_ = -1; scroll_ = 0; };
  if (in.down & gfx::BTN_RIGHT) sel_ = std::min(n - 1, sel_ + 1);
  if (in.down & gfx::BTN_LEFT) sel_ = std::max(0, sel_ - 1);
  if (in.down & gfx::BTN_DOWN) sel_ = std::min(n - 1, sel_ + 6);
  if (in.down & gfx::BTN_UP) sel_ = std::max(0, sel_ - 6);
  if (sel_ >= 0) scroll_ = std::max(0, sel_ / 6 - 2);
  if (in.down & gfx::BTN_B) { close(); return; }
  if (in.touchDown) {
    int id = hitAt(in.tx, in.ty);
    if (id >= ID_RELIC0 && id < ID_RELIC0 + n) sel_ = id - ID_RELIC0;
    if (id == ID_BACK) close();
    if (id == ID_DECK) { close(); deckOpen_ = true; }
  }
}

// ================================================================ end

void App::drawEnd(bool top, bool won) {
  if (top) {
    gfx::Texture* bg = R().texture("gfx/bg_overgrowth.t3t");
    gfx::image(bg, 0, 0, kTop, kH, 0, 0, kTop, kH, won ? 0x302000FF : 0x200000FF, 0.6f);
    TextStyle t = ts(F16, won ? col::gold : col::red, CENTER);
    t.scale = 2.f;
    R().text(kTop / 2, 70, won ? L("game_over_screen.BANNER.trueWin") : L("game_over_screen.BANNER.lose0"), t);
    std::string q = won ? "你击败了" + L("monsters.VANTOM.name") + "！" : L("game_over_screen.QUOTES.0" + num((int)(run_->seed % 10)));
    R().text(kTop / 2, 130, q, ts(F16, col::white, CENTER));
    R().text(kTop / 2, 160, "到达第 " + num(run_->floor) + " 层", ts(F12, col::gray, CENTER));
    return;
  }
  gfx::rectGradient(0, 0, kBot, kH, 0x201810FF, 0x0B0B12FF);
  button(80, 90, 160, 44, "重新开始", ID_RESTART, true, true);
}

void App::updateEnd(const gfx::Input& in) {
  if ((in.touchDown && hitAt(in.tx, in.ty) == ID_RESTART) || (in.down & (gfx::BTN_A | gfx::BTN_START))) startRun();
}

}  // namespace ui
