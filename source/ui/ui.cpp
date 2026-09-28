// Screens and input. The top screen shows the scene; the bottom screen holds
// everything you touch.
#include "ui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
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
// Geometry is the game's own (NMapScreen / map_screen.tscn, 1920x1080 units, x from the
// screen centre): column c at x = c*150 - 450, row r (1-based there) at y = 790 - r*155,
// boss centred at (0, -1780), parchment stacked from y = -1620 to +1620, 1527 wide.
// Everything is scaled by kMapS, which gives the RGDSplus look: paper ~65% of the width,
// small nodes, the whole act on the two screens with a little scrolling.
constexpr float kMapS = 0.17f;
// Virtual y of native y = 0 at scroll 0: row 0 just above the HUD, and no row in the
// 15 px hinge between the screens in the opening view.
constexpr float kMapY0 = 337.f;
constexpr float kMapScrollMin = -60.f, kMapScrollMax = 300.f;  // px
constexpr float kMapBgW = 260.f, kMapBgH = 552.f;  // bg_map.t3t parchment strip (1527x3240 * kMapS)
constexpr float kMapBgX = (gfx::kTopW - kMapBgW) / 2.f;
constexpr float kNodeScale = kMapS * 0.8f;  // node icons: ~9 px, as small as on RGDSplus
constexpr float kBossSize = 352.f * kMapS;
constexpr float kMapTapSlop = 5.f;       // 12 px of 768 on RGDSplus, rounded up for a stylus

// Native map coordinates (NMapScreen): our row r is the game's row r + 1.
// NMapScreen: rows are 2325 / (rowCount - 1) apart (155 in Overgrowth's 16-row grid,
// wider in the shorter Hive and Glory maps); the boss sits after the last row.
inline float mapDistY(const std::vector<MapNode>& nodes) {
  int rowCount = nodes.empty() ? 16 : nodes.back().row + 1;  // the boss is last, one past the rooms
  return 2325.f / (float)std::max(2, rowCount - 1);
}
inline std::pair<float, float> mapNative(const MapNode& n, float distY) {
  if (n.type == RoomType::Boss) return {0.f, -1780.f};
  if (n.type == RoomType::Ancient) return {0.f, 790.f};  // the game's row 0: the start point
  return {n.col * 150.f - 450.f + n.jx, 790.f - (n.row + 1.f) * distY + n.jy};
}

// Per-act art: gfx/bg_<act>.t3t (room) and gfx/bg_map_<act>.t3t (map paper). The previous
// act's textures are freed when the act changes (3DS linear memory is small).
Res& R();
std::string actTexture(const Run& r, const char* kind) {
  static int loadedAct = -1;
  if (loadedAct != r.actIndex) {
    if (loadedAct >= 0) {
      const char* old = db::acts()[loadedAct].key;
      R().releaseTexture(std::string("gfx/bg_") + old + ".t3t");
      R().releaseTexture(std::string("gfx/bg_map_") + old + ".t3t");
    }
    loadedAct = r.actIndex;
  }
  return std::string("gfx/") + kind + r.act().key + ".t3t";
}

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
  ID_DEVMENU,
  ID_PGUP,
  ID_PGDN,
  ID_POTIONS,
  ID_CONTINUE,
  ID_USE,
  ID_DISCARD,
  ID_PILE_DRAW,
  ID_PILE_DISCARD,
  ID_PILE_EXHAUST,
  ID_DETAIL,
  ID_UPGRADE_PREVIEW,
  ID_KEYWORD,
  ID_FAST_MODE,
  ID_SCREEN_SHAKE,
  ID_ABANDON,
  ID_ABANDON_CONFIRM,
  ID_ABANDON_CANCEL,
  ID_TITLE,
  ID_POTION0 = 900,  // + belt slot
  ID_TARGET0 = 100,   // + enemy index
  ID_HAND0 = 200,     // + hand index
  ID_NODE0 = 300,     // + reachable index
  ID_REWARD0 = 400,   // + reward index
  ID_RELIC0 = 500,    // + owned relic index
  ID_DEV0 = 600,      // + developer action
  ID_DEVITEM0 = 700,  // + developer picker row/cell
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
    case RoomType::Shop: return L("map.LEGEND_MERCHANT.title");
    case RoomType::Ancient: return L("ancients.NEOW.title");
    default: return L("map.LEGEND_UNKNOWN.title");
  }
}

Sprite roomIcon(RoomType t, const std::string& bossId = "VantomBoss") {
  switch (t) {
    case RoomType::Monster: return R().sprite("map/monster");
    case RoomType::Elite: return R().sprite("map/elite");
    case RoomType::Rest: return R().sprite("map/rest");
    case RoomType::Boss: {  // Ceremonial Beast's map node is a Spine animation: use its creature sprite
      if (bossId == "CeremonialBeastBoss") return R().sprite("creature/CEREMONIAL_BEAST");
      Sprite s = R().sprite("map/boss_" + bossId);
      return s ? s : R().sprite("map/elite");  // bosses without a ported map icon yet
    }
    case RoomType::Treasure: return R().sprite("map/chest");
    case RoomType::Shop: return R().sprite("map/shop");
    case RoomType::Ancient: {  // map/ancient_<id>, lower case (bossId carries the Ancient's id here)
      std::string k = bossId;
      for (char& ch : k) ch = (char)std::tolower((unsigned char)ch);
      Sprite s = R().sprite("map/ancient_" + k);
      return s ? s : R().sprite("map/ancient_neow");
    }
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
  hasSave_ = hasSave();
  std::string saved;
  if (!getenv("STS_HIDDEN") && !getenv("STS_NO_SAVE") && gfx::readSave("settings.txt", saved)) {
    int fast = 0, shake = 1;
    if (std::sscanf(saved.c_str(), "v1 %d %d", &fast, &shake) == 2) {
      fastMode_ = fast == 1;
      screenShake_ = shake != 0;
    }
  }
  Scheduler::get().speed = fastMode_ ? 1.75 : 1.0;
  return true;
}

// Saves: the run is written at every map choice (Run::onSavePoint) and deleted when it
// ends. Automated previews (STS_HIDDEN) and STS_NO_SAVE neither read nor write it.
namespace {
constexpr const char* kSaveName = "run.sav";
constexpr const char* kSettingsName = "settings.txt";
bool savesEnabled() { return !getenv("STS_HIDDEN") && !getenv("STS_NO_SAVE"); }
}

void App::saveSettings() {
  if (savesEnabled())
    gfx::writeSave(kSettingsName, "v1 " + num(fastMode_ ? 1 : 0) + " " + num(screenShake_ ? 1 : 0));
}

bool App::hasSave() const {
  std::string data;
  return savesEnabled() && gfx::readSave(kSaveName, data) && !data.empty();
}

void App::startRun(bool resume) {
  run_ = std::make_unique<Run>();
  bool loaded = false;
  if (resume) {
    std::string data;
    loaded = gfx::readSave(kSaveName, data) && run_->load(data);
    if (!loaded) { run_ = std::make_unique<Run>(); toast_ = "存档无法读取，开始新游戏"; toastT_ = 2.f; }
  }
  if (!loaded) {
    if (savesEnabled()) gfx::deleteSave(kSaveName);
    const char* seed = getenv("STS_SEED");
    const char* character = getenv("STS_CHAR");  // debug: STS_CHAR=Silent (the character select is S04)
    run_->start(seed ? (uint64_t)atoll(seed) : (uint64_t)time(nullptr), character ? character : "Ironclad");
  }
  if (savesEnabled()) run_->onSavePoint = [](Run& r) { gfx::writeSave(kSaveName, r.save()); };
  if (getenv("STS_ALLCARDS")) {  // debug: every pool card in the deck
    run_->deck.clear();
    for (auto& id : run_->character().cardPool)
      if (auto c = db::card(id)) run_->deck.push_back(std::move(c));
  }
  Scheduler::get().spawn(run_->main());
  floats_.clear();
  visuals_.clear();
  sel_ = -1;
  mapSel_ = 0;
  mapScroll_ = 0;
  deckOpen_ = false;
  cardListMode_ = CardListMode::Deck;
  detailCard_ = nullptr;
  detailRelic_ = nullptr;
  detailUpgrade_ = false;
  detailKeyword_ = -1;
  relicsOpen_ = false;
  settingsOpen_ = false;
  abandonConfirm_ = false;
  titleCharacter_ = false;
  titleSelection_ = 0;
  R().releaseTexture("gfx/bg_menu.t3t");
  R().releaseTexture("gfx/bg_character_ironclad.t3t");
}

void App::returnTitle() {
  // Abandon can happen while Run::main is suspended on a UI signal. Destroy
  // those coroutines before the Run and its cards/creatures they reference.
  Scheduler::get().clear();
  if (savesEnabled()) gfx::deleteSave(kSaveName);
  visuals_.clear();
  R().releaseSkeletons({});
  run_ = std::make_unique<Run>();
  lastCombat_ = nullptr;
  centers_.clear();
  flights_.clear();
  poses_.clear();
  ghosts_.clear();
  mapTouch_ = {};
  drag_ = {};
  deckOpen_ = relicsOpen_ = settingsOpen_ = abandonConfirm_ = mapView_ = devOpen_ = false;
  potionsOpen_ = false;
  detailCard_ = nullptr;
  detailRelic_ = nullptr;
  titleCharacter_ = false;
  titleSelection_ = 0;
  hasSave_ = hasSave();
}

// ================================================================ creature animation

std::string App::idleAnim(const Visual& v) const { return v.puffed ? "idle_loop_puffed" : "idle_loop"; }

// The player's art id (Spine skeleton and portrait sprite): the run's character. Until that
// character's art is baked (X*.5) the Ironclad's stands in.
static std::string playerArt(Run* r) {
  std::string key = r ? r->character().key : std::string("IRONCLAD");
  return R().sprite("creature/" + key) ? key : std::string("IRONCLAD");
}

App::Visual* App::visual(Creature* c) {
  auto it = visuals_.find(c);
  if (it != visuals_.end()) return it->second.skel ? &it->second : nullptr;
  Visual& v = visuals_[c];
  v.key = c->isPlayer ? playerArt(run_.get()) : c->name;
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

static std::string enchantmentCardText(Card* c);  // defined after expandSmart

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
  std::string ench = enchantmentCardText(c);  // enchantment extra text, then the replay line
  if (!ench.empty()) d += (d.empty() ? "" : "\n") + ench;
  if (c->has(kwExhaust)) d += (d.empty() ? "" : "\n") + std::string("[gold]") + L("card_keywords.EXHAUST.title") + "[/gold]" + L("card_keywords.PERIOD");
  return d;
}

// Relic and event text: the card SmartFormat subset, fed by the model's own DynamicVars
// ({Name}, {Name:energyIcons()}, {Name:plural:a|b}, {Name:percentMore/Less()},
// {InCombat:a|b}, {IfUpgraded:show:a|b}), plus an event's string vars (a loc key looked up
// here, falling back to the literal text). Unknown names are left visible as "?".
std::string expandSmart(const std::string& src, const std::vector<DynVar>& vars, bool inCombat,
                         const std::map<std::string, std::string>* strVars = nullptr) {
  auto find = [&](const std::string& n) -> const DynVar* {
    for (auto& v : vars) if (v.name == n) return &v;
    return nullptr;
  };
  auto raw = [&](const std::string& n) -> Dec { auto* v = find(n); return v ? v->base : Dec(0); };
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
      if (name == "InCombat") out += choose(rest, inCombat);
      else if (name == "IfUpgraded") out += choose(rest.substr(rest.find(':') + 1), false);
      else if (rest.rfind("energyIcons", 0) == 0)
        out += name == "energyPrefix" ? std::string("点能量") : "[gold]" + num(raw(name).toInt()) + "点能量[/gold]";
      else if (rest.rfind("percentMore", 0) == 0) out += num(((raw(name) - Dec(1)) * Dec(100)).toInt());
      else if (rest.rfind("percentLess", 0) == 0) out += num(((Dec(1) - raw(name)) * Dec(100)).toInt());
      else if (rest.rfind("plural:", 0) == 0) out += choose(rest.substr(7), raw(name) == Dec(1));
      else if (find(name)) out += num(raw(name).toInt());
      else if (strVars && strVars->count(name)) {
        const std::string& v = strVars->at(name);
        out += R().hasLoc(v) ? L(v) : v;
      } else out += "?";
    }
    return out;
  };
  return expand(src);
}

// CardModel.GetDescriptionForPile: an enchantment's extraCardText (purple) and, when it adds
// replays, the REPLAY line; nothing for enchantments without extra text or once disabled.
// PORT NOTE: the enchantment badge and the affliction line belong to the card renderer (F5).
static std::string enchantmentCardText(Card* c) {
  std::string out;
  Enchantment* e = c->enchantment.get();
  if (!e) return out;
  if (e->hasExtraCardText() && !e->disabled()) {
    std::vector<DynVar> vars = e->vars;
    vars.push_back({"Amount", Dec(e->amount), Dec(e->amount)});
    std::string key = "enchantments." + e->locKey + ".extraCardText";
    if (R().hasLoc(key)) out += "[purple]" + expandSmart(L(key), vars, c->combat != nullptr) + "[/purple]";
  }
  int times = c->enchantedReplayCount();
  if (times > 0 && R().hasLoc("static_hover_tips.REPLAY.extraText")) {
    std::vector<DynVar> vars{{"Times", Dec(times), Dec(times)}};
    out += (out.empty() ? "" : "\n") + expandSmart(L("static_hover_tips.REPLAY.extraText"), vars, false);
  }
  return out;
}

std::string App::describeRelic(Relic* r) {
  if (!R().hasLoc("relics." + r->locKey + ".description")) return {};
  return expandSmart(L("relics." + r->locKey + ".description"), r->vars, r->combat != nullptr);
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

static std::vector<const char*> cardKeywordKeys(const Card* c) {
  std::vector<const char*> keys;
  if (!c) return keys;
  if (c->has(kwUnplayable)) keys.push_back("UNPLAYABLE");
  if (c->has(kwEthereal)) keys.push_back("ETHEREAL");
  if (c->has(kwInnate)) keys.push_back("INNATE");
  if (c->has(kwRetain)) keys.push_back("RETAIN");
  if (c->has(kwExhaust)) keys.push_back("EXHAUST");
  return keys;
}

void App::drawDetail(bool top) {
  std::unique_ptr<Card> upgraded;
  Card* card = detailCard_;
  if (card && detailUpgrade_ && card->upgradable()) {
    upgraded = card->clone();
    upgraded->upgrade();
    card = upgraded.get();
  }
  drawSceneBg(top, 0.85f);
  if (top) {
    if (card) {
      drawCard(card, (kTop - 156) / 2.f, 10, 1.3f, false, true);
      if (upgraded) R().text(kTop - 10, 8, "升级预览", ts(F12, col::green, RIGHT));
    } else if (detailRelic_) {
      drawRelicDetail(detailRelic_, 67);
    }
    return;
  }
  panel(12, 10, kBot - 24, 178);
  std::string title = card ? cardTitle(card) : L("relics." + detailRelic_->locKey + ".title");
  R().text(kBot / 2, 22, title, ts(F16, col::gold, CENTER, kBot - 40));
  auto keywords = cardKeywordKeys(card);
  std::string description = card ? describe(card) : describeRelic(detailRelic_);
  if (detailKeyword_ >= 0 && detailKeyword_ < (int)keywords.size()) {
    std::string key = "card_keywords." + std::string(keywords[detailKeyword_]);
    description = "[gold]" + L(key + ".title") + "[/gold]" +
                  "  " + num(detailKeyword_ + 1) + "/" + num((int)keywords.size()) +
                  "\n\n" + L(key + ".description");
  }
  TextStyle desc = ts(F16, col::white, CENTER, kBot - 48);
  float h = 0;
  R().measure(description, desc, &h);
  while (h > 128 && desc.scale > 0.65f) {
    desc.scale *= 0.9f;
    R().measure(description, desc, &h);
  }
  R().text(kBot / 2, 55, description, desc);
  bool canUpgrade = detailCard_ && detailCard_->upgradable();
  if (canUpgrade && !keywords.empty()) {
    button(8, 199, 90, 34, "关闭", ID_BACK);
    button(106, 199, 104, 34, detailUpgrade_ ? "原卡" : "升级预览", ID_UPGRADE_PREVIEW, true, detailUpgrade_);
    button(218, 199, 94, 34, detailKeyword_ < 0 ? "关键词" :
           detailKeyword_ + 1 < (int)keywords.size() ? "下个词" : "卡牌", ID_KEYWORD, true, detailKeyword_ >= 0);
  } else if (canUpgrade) {
    button(12, 199, 140, 34, "关闭", ID_BACK);
    button(168, 199, 140, 34, detailUpgrade_ ? "原卡" : "升级预览", ID_UPGRADE_PREVIEW, true, detailUpgrade_);
  } else if (!keywords.empty()) {
    button(12, 199, 140, 34, "关闭", ID_BACK);
    button(168, 199, 140, 34, detailKeyword_ < 0 ? "关键词" :
           detailKeyword_ + 1 < (int)keywords.size() ? "下个词" : "卡牌", ID_KEYWORD, true, detailKeyword_ >= 0);
  } else {
    button(90, 199, 140, 34, "关闭", ID_BACK, true, true);
  }
}

void App::updateDetail(const gfx::Input& in) {
  int id = in.touchDown ? hitAt(in.tx, in.ty) : ID_NONE;
  if ((in.down & gfx::BTN_B) || id == ID_BACK) {
    detailCard_ = nullptr;
    detailRelic_ = nullptr;
    detailUpgrade_ = false;
    detailKeyword_ = -1;
    return;
  }
  if (detailCard_ && detailCard_->upgradable() && ((in.down & gfx::BTN_X) || id == ID_UPGRADE_PREVIEW)) {
    detailUpgrade_ = !detailUpgrade_;
    detailKeyword_ = -1;
  }
  if (detailCard_ && ((in.down & gfx::BTN_Y) || id == ID_KEYWORD)) {
    std::unique_ptr<Card> upgraded;
    Card* card = detailCard_;
    if (detailUpgrade_) { upgraded = card->clone(); upgraded->upgrade(); card = upgraded.get(); }
    auto keywords = cardKeywordKeys(card);
    if (!keywords.empty()) detailKeyword_ = (detailKeyword_ + 1) % ((int)keywords.size() + 1);
    if (detailKeyword_ == (int)keywords.size()) detailKeyword_ = -1;
  }
}

int App::gridHit(const std::vector<Card*>&, float, float, int, int tx, int ty) {
  int id = hitAt(tx, ty);
  return id >= ID_GRID0 ? id - ID_GRID0 : -1;
}

// ================================================================ frame

void App::update(const gfx::Input& in, double dt) {
  double visualDt = dt * (fastMode_ ? 1.75 : 1.0);
  time_ += visualDt;
  if (toastT_ > 0) toastT_ -= (float)visualDt;
  for (auto& f : floats_) f.t += (float)visualDt;
  floats_.erase(std::remove_if(floats_.begin(), floats_.end(), [](const Float& f) { return f.t > 1.2f; }), floats_.end());

  Screen scr = run_->screen;
  if (scr != lastScreen_) {
    detailCard_ = nullptr;
    detailRelic_ = nullptr;
    detailUpgrade_ = false;
    detailKeyword_ = -1;
    if (cardListMode_ != CardListMode::Deck) { deckOpen_ = false; cardListMode_ = CardListMode::Deck; }
    sel_ = -1;
    scroll_ = 0;
    mapTouch_ = {};
    mapUserScroll_ = false;
    lastScreen_ = scr;
    // The run is over: its save goes (dying or winning cannot be undone by reloading).
    if ((scr == Screen::GameOver || scr == Screen::Victory) && savesEnabled()) gfx::deleteSave(kSaveName);
    if (scr == Screen::Title) hasSave_ = hasSave();
  }
  if (run_->combat.get() != lastCombat_) {
    lastCombat_ = run_->combat.get();
    visuals_.clear();
    // Only the player stays cached across fights; each monster's Spine pages
    // are a few MB of linear memory on the 3DS.
    R().releaseSkeletons({playerArt(run_.get())});
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
  if (run_->combat) {
    auto decayShake = [&](Creature* c) {
      if (c) c->shake = std::max(0.f, c->shake - (float)visualDt * 4.f);
    };
    decayShake(run_->player.get());
    for (auto* enemy : run_->combat->enemies) decayShake(enemy);
  }
  for (auto& [c, v] : visuals_) {
    if (!v.anim) continue;
    v.anim->update((float)visualDt);
    if (v.dying && v.anim->finished()) v.fade = std::max(0.f, v.fade - (float)visualDt * 2.f);
  }

  if (autoplay_) autoplay(visualDt);
  if (settingsOpen_) { updateSettings(in); return; }
  if ((in.down & gfx::BTN_SELECT) && scr != Screen::Title) {
    devOpen_ = !devOpen_;
    devPage_ = 0;
    sel_ = -1;
    scroll_ = 0;
    return;
  }
  if (devOpen_) { updateDev(in); return; }
  if (detailCard_ || detailRelic_) { updateDetail(in); return; }
  if (run_->deckChoice.active) { updateDeckChoice(in); return; }
  // START opens the map for a look from any room (RGDSplus: map entry on the top bar).
  if ((in.down & gfx::BTN_START) && !mapView_ && scr != Screen::Title && scr != Screen::Map &&
      scr != Screen::GameOver && scr != Screen::Victory) {
    mapView_ = true;
    mapTouch_ = {};
    mapUserScroll_ = false;
    return;
  }
  if (mapView_) { updateMap(in); return; }
  if (relicsOpen_) { updateRelics(in); return; }
  if (deckOpen_) { updateDeck(in); return; }
  if (potionsOpen_) { updatePotions(in); return; }
  if (scr == Screen::Map && (in.down & gfx::BTN_START)) {
    settingsOpen_ = true;
    abandonConfirm_ = false;
    return;
  }
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
    case Screen::PotionOffer: updatePotionOffer(in); break;
    case Screen::Shop: updateShop(in); break;
    case Screen::Event: updateEvent(in); break;
    case Screen::Placeholder:
      if (run_->placeholderDone.waiting() &&
          ((in.down & gfx::BTN_A) || (in.touchDown && hitAt(in.tx, in.ty) == ID_CONFIRM)))
        run_->placeholderDone.fire(0);
      break;
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
    if (settingsOpen_) { drawSettings(top); continue; }
    if (devOpen_) { drawDev(top); continue; }
    if (detailCard_ || detailRelic_) { drawDetail(top); continue; }
    if (run_->deckChoice.active) { drawDeckChoice(top); continue; }
    if (mapView_) { drawMap(top); continue; }
    if (relicsOpen_) { drawRelics(top); continue; }
    if (deckOpen_) { drawDeck(top); continue; }
    if (potionsOpen_) { drawPotions(top); continue; }
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
      case Screen::PotionOffer: drawPotionOffer(top); break;
      case Screen::Shop: drawShop(top); break;
      case Screen::Event: drawEvent(top); break;
      case Screen::Placeholder:  // a room that is not ported yet (event / shop)
        drawSceneBg(top, 0.6f);
        if (top) {
          drawTopBar();
          R().text(kTop / 2, 90, run_->placeholderText, ts(F16, col::gold, CENTER, 0, 1.3f));
          R().text(kTop / 2, 124, "这个房间还没有移植，先跳过。", ts(F12, col::white, CENTER));
        } else {
          button(kBot / 2 - 60, 100, 120, 40, "继续", ID_CONFIRM, true, true);
        }
        break;
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
      if (r.restChoice.waiting()) r.restChoice.fire(r.restUsed.empty() ? 1 : -1); else acted = false;
      break;
    case Screen::RelicOffer:
      if (r.relicChoice.waiting()) r.relicChoice.fire(1); else acted = false;
      break;
    case Screen::Shop:
      if (r.deckChoice.active && r.deckChoice.result.waiting()) r.deckChoice.result.fire({});
      else if (r.shopChoice.waiting()) r.shopChoice.fire(-1); else acted = false;
      break;
    case Screen::PotionOffer:
      if (r.potionOfferChoice.waiting()) r.potionOfferChoice.fire(r.hasOpenPotionSlot() ? 1 : 0); else acted = false;
      break;
    case Screen::Placeholder:
      if (r.placeholderDone.waiting()) r.placeholderDone.fire(0); else acted = false;
      break;
    case Screen::Event:
      if (r.deckChoice.active && r.deckChoice.result.waiting()) r.deckChoice.result.fire({r.deckChoice.options[0]});
      else if (r.eventChoice.waiting() && r.currentEvent) {
        int pick = 0;
        auto& opts = r.currentEvent->options;
        while (pick < (int)opts.size() && opts[pick].locked()) ++pick;
        r.eventChoice.fire(pick);
      } else acted = false;
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
  if (titleCharacter_) {
    if (top) {
      gfx::image(R().texture("gfx/bg_character_ironclad.t3t"), 0, 0, kTop, kH, 0, 0, kTop, kH);
      gfx::rect(0, 0, kTop, 40, 0x000000A0);
      R().text(14, 9, "铁甲战士", ts(F16, col::gold));
      return;
    }
    gfx::image(R().texture("gfx/bg_menu.t3t"), 40, 240, kBot, kH, 0, 0, kBot, kH);
    gfx::rect(0, 0, kBot, kH, 0x000000B0);
    R().text(kBot / 2, 17, "选择角色", ts(F16, col::gold, CENTER));
    panel(26, 51, 268, 75);
    R().text(kBot / 2, 66, "铁甲战士", ts(F16, col::white, CENTER));
    R().text(kBot / 2, 96, "燃烧之血 · 初始生命 80", ts(F12, col::gray, CENTER));
    R().text(kBot / 2, 139, "其他角色尚未移植", ts(F12, col::gray, CENTER));
    button(19, 179, 130, 43, "返回", ID_BACK);
    button(171, 179, 130, 43, "开始", ID_START, true, true);
    return;
  }
  if (top) {
    gfx::image(R().texture("gfx/bg_menu.t3t"), 0, 0, kTop, kH, 0, 0, kTop, kH);
    return;
  }
  gfx::image(R().texture("gfx/bg_menu.t3t"), 40, 240, kBot, kH, 0, 0, kBot, kH);
  gfx::rect(0, 0, kBot, kH, 0x00000082);
  if (hasSave_) {
    button(58, 49, 204, 50, "继续", ID_CONTINUE, true, titleSelection_ == 0);
    button(58, 108, 204, 50, "新游戏", ID_START, true, titleSelection_ == 1);
  } else {
    button(58, 85, 204, 50, "新游戏", ID_START, true, true);
  }
  R().text(kBot / 2, 201, "↑↓选择 · A确认", ts(F12, col::white, CENTER));
}

void App::updateTitle(const gfx::Input& in) {
  int id = in.touchDown ? hitAt(in.tx, in.ty) : ID_NONE;
  if (titleCharacter_) {
    if (id == ID_BACK || (in.down & gfx::BTN_B)) { titleCharacter_ = false; return; }
    if (id == ID_START || (in.down & (gfx::BTN_A | gfx::BTN_START))) startRun(false);
    return;
  }
  if (hasSave_ && (in.down & (gfx::BTN_UP | gfx::BTN_DOWN))) titleSelection_ = 1 - titleSelection_;
  if (id == ID_CONTINUE || ((in.down & gfx::BTN_A) && hasSave_ && titleSelection_ == 0)) {
    startRun(true);
    return;
  }
  if (id == ID_START || (in.down & gfx::BTN_START) || ((in.down & gfx::BTN_A) && (!hasSave_ || titleSelection_ == 1))) {
    titleCharacter_ = true;
  }
}

// ================================================================ top bar

void App::drawTopBar() {
  gfx::rect(0, 0, kTop, 18, 0x000000A0);
  Creature* p = run_->player.get();
  R().text(4, 2, "生命 " + num(p->hp) + "/" + num(p->maxHp), ts(F12, col::red));
  R().text(84, 2, "金币 " + num(run_->gold), ts(F12, col::gold));
  R().text(148, 2, "第 " + num(run_->floor) + " 层", ts(F12, col::white));
  R().text(198, 2, "牌组 " + num((int)run_->deck.size()), ts(F12, col::white));
  // Potion belt (TopBar.PotionContainer), after the deck count.
  const int belt = (int)run_->potions.size();
  for (int i = 0; i < belt; ++i) drawPotionIcon(run_->potions[i].get(), 250 + i * 17, 1, 16);
  // Relics from the right edge, as many as fit after the belt; the rest are counted as
  // "+N" (all of them are in the relic page).
  const int n = (int)run_->relics.size();
  const int room = std::max(1, (int)((kTop - 2 - (250 + belt * 17)) / 20));
  const int fit = n > room ? room - 1 : n;
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
    gfx::image(R().texture(actTexture(*run_, "bg_map_")), 0, 0, kMapBgW, kMapBgH, bx + kMapBgX, by + kMapY0 - 1620.f * kMapS + mapScroll_, kMapBgW, kMapBgH);
  } else {
    gfx::image(R().texture(actTexture(*run_, "bg_")), top ? 0 : kBotOX, 0, w, kH, 0, 0, w, kH, 0x000000FF, 0.15f);
  }
  if (dim > 0) gfx::rect(0, 0, w, kH, (uint32_t)(std::clamp(dim, 0.f, 1.f) * 255));
}

// Node centre on the two-screen virtual canvas.
std::pair<float, float> App::mapPos(const MapNode& n) const {
  auto [nx, ny] = mapNative(n, mapDistY(run_->nodes));
  return {kTop / 2.f + nx * kMapS, kMapY0 + ny * kMapS + mapScroll_};
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
    float d = std::hypot(vx - x, vy - y), radius = n.type == RoomType::Boss ? kBossSize / 2 : 11.f;
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
    // Keep the next row ~1.5 rows above the HUD (never above the opening view).
    float nextY = kMapY0 + (790.f - (curRow + 2) * mapDistY(r.nodes)) * kMapS;
    float target = std::clamp(406.f - nextY, 0.f, kMapScrollMax);
    mapScroll_ += (target - mapScroll_) * 0.15f;
  }

  // One sheet of map paper behind both screens.
  float bx = 0, by = 0;
  toLocal(top, bx, by);
  gfx::image(R().texture(actTexture(*run_, "bg_map_")), 0, 0, kMapBgW, kMapBgH, bx + kMapBgX, by + kMapY0 - 1620.f * kMapS + mapScroll_, kMapBgW, kMapBgH);

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
      // NMapScreen.DrawPaths: a dot (map_dot) every 22 units, clear of both icons;
      // travelled dots are darker and 1.2x.
      float len = std::hypot(x1 - x0, y1 - y0);
      float step = 22.f * kMapS, clear = 40.f * kMapS;
      float ds = travelled ? 2.4f : 2.f;
      uint32_t dc = travelled ? 0x2A2118F0 : 0x5A4833C8;
      for (float d = clear; d < len - clear; d += step) {
        float t = d / len;
        gfx::rect(x0 + (x1 - x0) * t - ds / 2, y0 + (y1 - y0) * t - ds / 2, ds, ds, dc);
      }
    }
  }
  // Nodes. Only the normal next steps pulse; in the free (development) map every
  // node is still enterable, and the selected one is highlighted wherever it is.
  auto path = r.pathNodes();
  for (int i = 0; i < (int)r.nodes.size(); ++i) {
    auto& n = r.nodes[i];
    auto [x, y] = pos(n);
    if (y < -40 || y > kH + 40) continue;
    bool chosen = !reach.empty() && reach[mapSel_] == i;
    bool reachable = chosen || std::find(path.begin(), path.end(), i) != path.end();
    Sprite ic = roomIcon(n.type, n.type == RoomType::Ancient ? r.ancientId : r.bossId);
    // Icon height at its native size (ui_atlas map icons) times the map scale.
    float nativeH = n.type == RoomType::Elite ? 70 : n.type == RoomType::Rest ? 90 : n.type == RoomType::Treasure ? 59
                  : n.type == RoomType::Unknown ? 72 : n.type == RoomType::Ancient ? 150 : 68;
    float sz = n.type == RoomType::Boss ? kBossSize : nativeH * kNodeScale;
    if (reachable && n.type != RoomType::Boss) {
      float pulse = 1.f + 0.15f * std::sin((float)time_ * 6);
      sz *= chosen ? 1.4f * pulse : pulse;
      gfx::circle(x, y, sz * 0.75f, chosen ? 0xFFE07070 : 0xFFFFFF38);
    }
    uint32_t tint = n.visited ? 0x404040FF : 0xFFFFFFFF;
    float w = ic.w / ic.h * sz;
    gfx::pushTransform(gfx::Affine::rotateAround(x, y, n.angle * 3.14159265f / 180.f));  // NMapPoint.SetAngle
    if (n.type == RoomType::Boss)
      spr(ic, x - w / 2, y - sz / 2, w, sz, 0x2E241AFF, 1.f);  // boss icons are masks/sprites: ink them
    else
      spr(ic, x - w / 2, y - sz / 2, w, sz, tint, n.visited ? 0.5f : 0.f);
    gfx::popTransform();
    if (i == r.currentNode) spr(R().sprite("map/marker"), x - 7, y - sz / 2 - 14, 14, 16);
  }

  if (top) { drawTopBar(); return; }
  // Bottom HUD in the corners, clear of row 0. Looking at the map from another room
  // (START) shows only the red 返回 in the bottom-left corner, as on RGDSplus.
  if (mapView_) {
    const float bx = 4, by = 208, bw = 72, bh = 26;
    gfx::rect(bx, by, bw, bh, 0xB83A3AF0);
    gfx::rect(bx, by, bw, 2, 0xFF8A8AFF);
    gfx::rect(bx + bw - 8, by, 8, bh, 0x8A2020F0);
    R().text(bx + (bw - 8) / 2, by + (bh - R().lineHeight(F16)) / 2, "返回", ts(F16, col::white, CENTER));
    hits_.push_back({bx, by, bw, bh, ID_BACK});
  } else {
    button(4, 210, 64, 26, "牌组", ID_DECK);
    button(72, 210, 64, 26, "遗物", ID_RELICS);
    button(140, 210, 56, 26, "开发", ID_DEVMENU);
    button(200, 210, 56, 26, "药水", ID_POTIONS);
  }
  // Legend panel on the right, as on RGDSplus (the map's own legend, lower screen).
  {
    const float lx = kBot - 62, ly = 88, lw = 58;
    const RoomType types[] = {RoomType::Unknown, RoomType::Shop, RoomType::Treasure, RoomType::Rest,
                              RoomType::Monster, RoomType::Elite};
    panel(lx, ly, lw, 18 + 6 * 17, 0x2A2418D8, 0x8A7A5AFF);
    R().text(lx + lw / 2, ly + 3, "图例", ts(F12, col::gold, CENTER));
    for (int i = 0; i < 6; ++i) {
      float y = ly + 19 + i * 17;
      Sprite ic = roomIcon(types[i]);
      spr(ic, lx + 4, y, 14, 14);
      R().text(lx + 22, y + 1, roomName(types[i]), ts(F12, col::white, LEFT, 0, 0.85f));
    }
  }
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
  // Opened from another room (mapView_): look and drag only; 返回 / B / START go back.
  if (mapView_ && ((in.down & (gfx::BTN_B | gfx::BTN_START)) || (in.touchDown && hitAt(in.tx, in.ty) == ID_BACK))) {
    mapView_ = false;
    mapTouch_ = {};
    return;
  }
  bool choosing = !mapView_ && r.mapChoice.waiting();
  if (!choosing && !mapView_) { mapTouch_ = {}; return; }
  auto reach = r.reachableNodes();
  int n = (int)reach.size();
  if (n == 0) return;
  if (choosing && (in.down & (gfx::BTN_LEFT | gfx::BTN_RIGHT))) {
    mapSel_ = (mapSel_ + ((in.down & gfx::BTN_LEFT) ? n - 1 : 1)) % n;
    mapUserScroll_ = false;
  }
  if (choosing && (in.down & gfx::BTN_Y)) { openCardList(CardListMode::Deck); mapTouch_ = {}; return; }
  int pick = -1;
  if (choosing && (in.down & gfx::BTN_A)) pick = mapSel_;
  if (in.touchDown) {
    int hud = choosing ? hitAt(in.tx, in.ty) : ID_NONE;
    if (hud == ID_DECK) { openCardList(CardListMode::Deck); mapTouch_ = {}; return; }
    if (hud == ID_RELICS) { relicsOpen_ = true; sel_ = run_->relics.empty() ? -1 : 0; scroll_ = 0; mapTouch_ = {}; return; }
    if (hud == ID_DEVMENU) { devOpen_ = true; devPage_ = 0; sel_ = -1; scroll_ = 0; mapTouch_ = {}; return; }
    if (hud == ID_POTIONS) { potionsOpen_ = true; potionAim_ = false; potionSel_ = -1; mapTouch_ = {}; return; }
    mapTouch_ = {};
    mapTouch_.down = true;
    mapTouch_.startY = mapTouch_.lastY = in.ty;
    mapTouch_.node = mapNodeAt(in.tx, in.ty);
    if (mapTouch_.node >= 0) mapSel_ = mapTouch_.node;
  } else if (mapTouch_.down && in.touching) {
    // Map scrolling is vertical, so horizontal stylus drift alone should not
    // turn a node tap into a non-scrolling drag.
    if (std::fabs(in.ty - mapTouch_.startY) > kMapTapSlop) mapTouch_.dragged = true;
    if (mapTouch_.dragged) {
      mapScroll_ = std::clamp(mapScroll_ + (in.ty - mapTouch_.lastY), kMapScrollMin, kMapScrollMax);
      mapUserScroll_ = true;
    }
    mapTouch_.lastY = in.ty;
  }
  if (in.touchUp && mapTouch_.down) {
    // MapGesture.release: a tap needs no drag and the release over the pressed node.
    if (choosing && !mapTouch_.dragged && mapTouch_.node >= 0 && mapNodeAt(in.tx, in.ty) == mapTouch_.node)
      pick = mapTouch_.node;
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
  } else if (potionsOpen_ && potionAim_ && !alive.empty()) {
    if (target_ >= (int)alive.size()) target_ = 0;
    tgt = alive[target_];
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
    gfx::Texture* bg = R().texture(actTexture(*run_, "bg_"));
    gfx::image(bg, 0, 0, kTop, kH, 0, 0, kTop, kH);
    gfx::rectGradient(0, 150, kTop, 90, 0x00000000, 0x00000060);
    const float feet = 170;  // ~70% down the screen, the room's floor line (RGDSplus/native)
    auto center = [&](Creature* c, float x) {
      Sprite s = R().sprite("creature/" + (c->isPlayer ? playerArt(run_.get()) : c->name));
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
  gfx::Texture* room = R().texture(actTexture(*run_, "bg_"));
  gfx::image(room, kBotOX, 0, kBot, kH, 0, 0, kBot, kH, 0x000000FF, 0.15f);

  if (cb->choice.active) {
    gfx::rect(0, 0, kBot, kH, 0x000000A0);
    const CardChoice& ch = cb->choice;
    std::string prompt = R().hasLoc(ch.prompt) ? L(ch.prompt)
                         : R().hasLoc("cards." + ch.prompt + ".title") ? L("cards." + ch.prompt + ".title") + "：选择一张牌" : ch.prompt;
    R().text(kBot / 2, 4, prompt, ts(F16, col::gold, CENTER, kBot - 8, 0.9f));
    drawCardGrid(ch.options, sel_, 26, 196, scroll_);
    bool multi = ch.maxCount > 1;
    if (multi) {  // picked cards get a gold tick (as in the deck choice)
      for (int i : deckPicks_) {
        int row = i / 5 - scroll_;
        const float gs = 0.46f, cw = 120 * gs, gch = 169 * gs, gap = (kBot - 5 * cw) / 6;
        float x = gap + (i % 5) * (cw + gap), y = 26 + row * (gch + 8);
        if (y >= 26 && y < 196) gfx::circle(x + cw - 6, y + 6, 6, 0xFFD870FF);
      }
    }
    bool ready = multi ? (int)deckPicks_.size() >= ch.minCount : sel_ >= 0 && sel_ < (int)ch.options.size();
    if (ch.minCount == 0) button(kBot / 2 - 50, 200, 100, 34, "跳过", ID_SKIP);
    button(kBot - 110, 200, 100, 34, "确认", ID_CONFIRM, ready);
    if (!multi && sel_ >= 0 && sel_ < (int)ch.options.size()) R().text(10, 206, cardTitle(ch.options[sel_]), ts(F12, col::white));
    if (multi) R().text(10, 208, "已选 " + num((int)deckPicks_.size()), ts(F12, col::white));
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
  // Potions: a small button bottom centre, between the piles (opens the belt list).
  {
    const float pw = 58, ph = 22, px = (kBot - pw) / 2, py = 214;
    panel(px, py, pw, ph, canAct ? 0x3A2E24E8 : 0x2A2A2AC0, canAct ? 0xB89A60FF : 0x555555FF);
    int filled = 0;
    for (auto& pt : run_->potions) filled += pt != nullptr;
    R().text(px + pw / 2, py + (ph - R().lineHeight(F12)) / 2, "药水 " + num(filled), ts(F12, canAct ? col::white : col::gray, CENTER));
    if (canAct) hits_.push_back({px, py - 2, pw, ph + 4, ID_POTIONS});
  }
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
  hits_.push_back({0, 201, 34, 39, ID_PILE_DRAW});
  hits_.push_back({kBot - 34.f, 201, 34, 39, ID_PILE_DISCARD});
  if (!cb->exhaust.empty()) {
    R().text(kBot - 38, 200, "消耗 " + num((int)cb->exhaust.size()), ts(F12, col::white, RIGHT));
    hits_.push_back({kBot - 104.f, 196, 66, 24, ID_PILE_EXHAUST});
  }

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
  float visualDt = (float)gfx::dt() * (fastMode_ ? 1.75f : 1.f);
  clock_ += visualDt;
  animateHand(visualDt);
  for (auto& f : flights_) f.t += visualDt;
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
    const bool multi = cb->choice.maxCount > 1;
    auto finish = [&](std::vector<Card*> picked) {
      deckPicks_.clear();
      sel_ = -1;
      scroll_ = 0;
      cb->choice.result.fire(std::move(picked));
    };
    auto toggle = [&](int i) {
      auto it = std::find(deckPicks_.begin(), deckPicks_.end(), i);
      if (it != deckPicks_.end()) deckPicks_.erase(it);
      else if ((int)deckPicks_.size() < cb->choice.maxCount) deckPicks_.push_back(i);
    };
    auto confirmMulti = [&] {
      if ((int)deckPicks_.size() < cb->choice.minCount) return;
      std::vector<Card*> picked;
      for (int i : deckPicks_) picked.push_back(cb->choice.options[i]);
      finish(std::move(picked));
    };
    if (multi) {
      if ((in.down & gfx::BTN_A) && sel_ >= 0 && sel_ < m) toggle(sel_);
      if (in.down & gfx::BTN_X) { confirmMulti(); return; }
    }
    bool confirm = !multi && (in.down & gfx::BTN_A) && sel_ >= 0 && sel_ < m;
    if ((in.down & gfx::BTN_B) && cb->choice.minCount == 0) { finish({}); return; }
    if (in.touchDown) {
      int id = hitAt(in.tx, in.ty);
      if (id >= ID_GRID0 && id - ID_GRID0 < m) {
        if (multi) toggle(id - ID_GRID0);
        else if (sel_ == id - ID_GRID0) confirm = true;
        sel_ = id - ID_GRID0;
      }
      if (id == ID_SKIP && cb->choice.minCount == 0) { finish({}); return; }
      if (id == ID_CONFIRM) {
        if (multi) { confirmMulti(); return; }
        if (sel_ >= 0) confirm = true;
      }
    }
    if (confirm) finish({cb->choice.options[sel_]});
    return;
  }

  if (in.touchDown) {
    int id = hitAt(in.tx, in.ty);
    CardListMode mode;
    if (id == ID_PILE_DRAW) mode = CardListMode::Draw;
    else if (id == ID_PILE_DISCARD) mode = CardListMode::Discard;
    else if (id == ID_PILE_EXHAUST) mode = CardListMode::Exhaust;
    else mode = CardListMode::Deck;
    if (id == ID_PILE_DRAW || id == ID_PILE_DISCARD || id == ID_PILE_EXHAUST) {
      openCardList(mode);
      drag_ = {};
      aiming_ = false;
      return;
    }
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
  if (in.touchDown && hitAt(in.tx, in.ty) == ID_POTIONS) {
    potionsOpen_ = true;
    potionAim_ = false;
    potionSel_ = -1;
    drag_ = {};
    aiming_ = false;
    sel_ = -1;
    return;
  }
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
      if (sel_ == drag_.index) {
        detailCard_ = drag_.card;  // second tap: inspect without playing
        detailUpgrade_ = false;
        detailKeyword_ = -1;
      } else {
        sel_ = drag_.index;  // first tap: preview
      }
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
  if (in.down & gfx::BTN_Y) { openCardList(CardListMode::Deck); aiming_ = false; return; }
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
    gfx::Texture* bg = R().texture(actTexture(*run_, "bg_"));
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
    drawCard(r.rewardCards[i].get(), x, y, s, false, true, i == sel_);
    hits_.push_back({x, y, cw, 169 * s, ID_REWARD0 + i});
  }
  button(10, 196, 110, 36, L("gameplay_ui.CHOOSE_CARD_SKIP_BUTTON"), ID_SKIP);
  button(122, 196, 76, 36, "详情", ID_DETAIL, sel_ >= 0 && sel_ < n);
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
    if (id == ID_DETAIL && sel_ >= 0 && sel_ < n) { detailCard_ = r.rewardCards[sel_].get(); detailUpgrade_ = false; }
  }
}

// ================================================================ rest

void App::drawRest(bool top) {
  Run& r = *run_;
  int heal = (Dec(r.player->maxHp) * Dec::lit(0.3)).toInt();
  if (top) {
    gfx::Texture* bg = R().texture(actTexture(*run_, "bg_"));
    gfx::image(bg, 0, 0, kTop, kH, 0, 0, kTop, kH, 0x100400FF, 0.6f);
    gfx::circle(200, 200, 40, 0xFF802040);
    gfx::circle(200, 204, 22, 0xFFB04060);
    Sprite ic = R().sprite("creature/" + playerArt(run_.get()));
    spr(ic, 120 - ic.ax, 206 - ic.ay);
    TextStyle t = ts(F16, col::gold, CENTER);
    t.scale = 1.4f;
    R().text(kTop / 2, 40, L("map.LEGEND_REST.hoverTip.title"), t);
    R().text(kTop / 2, 76, L("rest_site_ui.PROMPT"), ts(F16, col::white, CENTER));
    drawTopBar();
    return;
  }
  drawSceneBg(false, 0.5f);
  // Options (RestSiteRoom): heal, smith, and Lift / Dig with Girya / Shovel; up to 2 per row.
  auto& opts = r.restOptions;
  int n = (int)opts.size();
  const float bw = 138, bh = n > 4 ? 36 : n > 2 ? 64 : 100, gapY = n > 4 ? 4 : 8;
  for (int i = 0; i < n; ++i) {
    int o = opts[i];
    float x = i % 2 ? 166 : 16, y = 20 + (i / 2) * (bh + gapY + 16);
    bool used = std::find(r.restUsed.begin(), r.restUsed.end(), o) != r.restUsed.end();
    static const char* keys[] = {"OPTION_HEAL", "OPTION_SMITH", "OPTION_LIFT", "OPTION_DIG", "OPTION_COOK", "OPTION_KINDLE"};
    button(x, y, bw, bh, L(std::string("rest_site_ui.") + keys[o] + ".name"), ID_GRID0 + o, r.restChoice.waiting() && restValid(o), sel_ == o);
    static const char* subs[] = {"", "升级一张牌", "战斗开始时 +1 力量", "挖出一件遗物", "移除 2 张牌，+5 最大生命", "南瓜灯 +5 场战斗"};
    std::string sub = o == 0 ? "回复 " + num(heal) + " 点生命" : std::string(subs[o]);
    R().text(x + bw / 2, y + bh + 2, used ? std::string("已使用") : sub, ts(F12, used ? col::gray : o == 0 ? col::green : col::gold, CENTER));
  }
  if (r.restUsed.empty()) R().text(80, 206, "生命 " + num(r.player->hp) + "/" + num(r.player->maxHp), ts(F16, col::red, CENTER));
  // RGDSplus U22: pick an option, then confirm. With Miniature Tent, 离开 ends the visit.
  if (!r.restUsed.empty()) button(kBot - 240, 196, 110, 36, "离开", ID_BACK, r.restChoice.waiting());
  button(kBot - 120, 196, 110, 36, "确认", ID_CONFIRM, r.restChoice.waiting() && sel_ >= 0 && restValid(sel_), true);
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

void App::updateRest(const gfx::Input& in) {
  Run& r = *run_;
  if (!r.restChoice.waiting()) return;
  auto& opts = r.restOptions;
  int cur = (int)(std::find(opts.begin(), opts.end(), sel_) - opts.begin());
  if ((in.down & gfx::BTN_RIGHT) && !opts.empty()) sel_ = opts[std::min((int)opts.size() - 1, cur >= (int)opts.size() ? 0 : cur + 1)];
  if ((in.down & gfx::BTN_LEFT) && !opts.empty()) sel_ = opts[std::max(0, cur >= (int)opts.size() ? 0 : cur - 1)];
  if ((in.down & gfx::BTN_A) && restValid(sel_)) { r.restChoice.fire(sel_); return; }
  if (in.touchDown) {
    int id = hitAt(in.tx, in.ty);
    int pick = id >= ID_GRID0 && id < ID_GRID0 + 6 ? id - ID_GRID0 : -1;
    if (pick >= 0) {
      if (sel_ == pick && restValid(pick)) { r.restChoice.fire(pick); return; }  // second tap confirms
      sel_ = pick;
    }
    if (id == ID_CONFIRM && restValid(sel_)) r.restChoice.fire(sel_);
    if (id == ID_BACK && !r.restUsed.empty()) { sel_ = -1; r.restChoice.fire(-1); }
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

std::vector<Card*> App::listedCards() {
  std::vector<Card*> cards;
  if (cardListMode_ == CardListMode::Deck || !run_->combat) {
    for (auto& c : run_->deck) cards.push_back(c.get());
    return cards;
  }
  Combat& cb = *run_->combat;
  const std::vector<Card*>& pile = cardListMode_ == CardListMode::Draw ? cb.draw
                                   : cardListMode_ == CardListMode::Discard ? cb.discard : cb.exhaust;
  cards.assign(pile.begin(), pile.end());
  // The draw-pile page must not reveal its actual next-card order.
  std::stable_sort(cards.begin(), cards.end(), [&](Card* a, Card* b) { return cardTitle(a) < cardTitle(b); });
  return cards;
}

void App::openCardList(CardListMode mode) {
  cardListMode_ = mode;
  deckOpen_ = true;
  sel_ = -1;
  scroll_ = 0;
}

void App::drawDeck(bool top) {
  std::vector<Card*> cards = listedCards();
  const char* title = cardListMode_ == CardListMode::Deck ? "牌组" :
                      cardListMode_ == CardListMode::Draw ? "抽牌堆" :
                      cardListMode_ == CardListMode::Discard ? "弃牌堆" : "消耗堆";
  if (top) {
    drawSceneBg(true, 0.7f);
    drawTopBar();
    if (sel_ >= 0 && sel_ < (int)cards.size()) drawCard(cards[sel_], (kTop - 132) / 2, 34, 1.1f, false, true);
    else R().text(kTop / 2, 100, std::string(title) + "（" + num((int)cards.size()) + " 张）", ts(F16, col::gold, CENTER));
    if (cardListMode_ == CardListMode::Draw && sel_ < 0)
      R().text(kTop / 2, 211, "不显示实际抽牌顺序", ts(F12, col::gray, CENTER));
    return;
  }
  drawSceneBg(false, 0.65f);
  drawCardGrid(cards, sel_, 0, 196, scroll_);
  gfx::rect(0, 196, kBot, 44, 0x000000A0);
  if (cardListMode_ == CardListMode::Deck) {
    button(10, 200, 100, 34, "返回", ID_BACK);
    button(118, 200, 84, 34, "详情", ID_DETAIL, sel_ >= 0 && sel_ < (int)cards.size());
    button(kBot - 110, 200, 100, 34, "遗物", ID_RELICS);
  } else {
    button(4, 200, 64, 34, "返回", ID_BACK);
    button(72, 200, 76, 34, "抽牌", ID_PILE_DRAW, true, cardListMode_ == CardListMode::Draw);
    button(154, 200, 76, 34, "弃牌", ID_PILE_DISCARD, true, cardListMode_ == CardListMode::Discard);
    button(236, 200, 76, 34, "消耗", ID_PILE_EXHAUST, true, cardListMode_ == CardListMode::Exhaust);
  }
}

void App::updateDeck(const gfx::Input& in) {
  std::vector<Card*> cards = listedCards();
  int m = (int)cards.size();
  if (cardListMode_ != CardListMode::Deck && (in.down & (gfx::BTN_L | gfx::BTN_R))) {
    int mode = (int)cardListMode_ - (int)CardListMode::Draw;
    mode = (mode + ((in.down & gfx::BTN_R) ? 1 : 2)) % 3;
    openCardList((CardListMode)((int)CardListMode::Draw + mode));
    return;
  }
  if (in.down & gfx::BTN_RIGHT) sel_ = std::min(m - 1, sel_ + 1);
  if (in.down & gfx::BTN_LEFT) sel_ = std::max(0, sel_ - 1);
  if (in.down & gfx::BTN_DOWN) sel_ = std::min(m - 1, sel_ + 5);
  if (in.down & gfx::BTN_UP) sel_ = std::max(0, sel_ - 5);
  if (sel_ >= 0) scroll_ = std::max(0, sel_ / 5 - 1);
  if (in.down & (gfx::BTN_B | gfx::BTN_Y)) { deckOpen_ = false; cardListMode_ = CardListMode::Deck; sel_ = -1; return; }
  if ((in.down & gfx::BTN_A) && sel_ >= 0 && sel_ < m) { detailCard_ = cards[sel_]; detailUpgrade_ = false; return; }
  if (in.touchDown) {
    int id = hitAt(in.tx, in.ty);
    if (id >= ID_GRID0 && id < ID_GRID0 + m) {
      int picked = id - ID_GRID0;
      if (sel_ == picked) { detailCard_ = cards[picked]; detailUpgrade_ = false; return; }
      sel_ = picked;
    }
    if (id == ID_DETAIL && sel_ >= 0 && sel_ < m) { detailCard_ = cards[sel_]; detailUpgrade_ = false; return; }
    if (id == ID_BACK) { deckOpen_ = false; cardListMode_ = CardListMode::Deck; sel_ = -1; }
    if (id == ID_RELICS) { deckOpen_ = false; relicsOpen_ = true; sel_ = run_->relics.empty() ? -1 : 0; scroll_ = 0; }
    if (id == ID_PILE_DRAW) openCardList(CardListMode::Draw);
    if (id == ID_PILE_DISCARD) openCardList(CardListMode::Discard);
    if (id == ID_PILE_EXHAUST) openCardList(CardListMode::Exhaust);
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

// ================================================================ potions

std::string App::describePotion(Potion* p) {
  if (!R().hasLoc("potions." + p->locKey + ".description")) return {};
  return expandSmart(L("potions." + p->locKey + ".description"), p->vars, run_->combat != nullptr);
}

void App::drawPotionIcon(Potion* p, float x, float y, float size) {
  if (!p) {  // empty slot: a faint outline
    gfx::circle(x + size / 2, y + size / 2, size * 0.36f, 0xFFFFFF28);
    return;
  }
  Sprite s = R().sprite("potion/" + p->locKey);
  if (!s) { gfx::circle(x + size / 2, y + size / 2, size * 0.4f, 0xC04040FF); return; }
  float k = std::min(size / s.w, size / s.h);
  spr(s, x + (size - s.w * k) / 2, y + (size - s.h * k) / 2, s.w * k, s.h * k);
}

void App::drawPotions(bool top) {
  Run& r = *run_;
  bool fight = r.screen == Screen::Combat && r.combat;
  int n = (int)r.potions.size();
  if (potionSel_ >= n) potionSel_ = -1;
  Potion* p = potionSel_ >= 0 ? r.potions[potionSel_].get() : nullptr;
  if (top) {
    if (fight) drawCombat(true);
    else { drawSceneBg(true, 0.65f); drawTopBar(); }
    if (!p) {
      if (!fight) R().text(kTop / 2, 100, "药水", ts(F16, col::gold, CENTER, 0, 1.3f));
      return;
    }
    // The picked potion along the bottom of the top screen (below the creatures' feet).
    gfx::rect(0, kH - 54, kTop, 54, 0x000000C8);
    drawPotionIcon(p, 8, kH - 48, 40);
    R().text(56, kH - 52, L("potions." + p->locKey + ".title"), ts(F16, col::gold));
    R().text(56, kH - 32, describePotion(p), ts(F12, col::white, LEFT, kTop - 64, 0.9f));
    if (potionAim_) R().text(kTop / 2, 24, "选择目标", ts(F16, col::gold, CENTER));
    return;
  }
  drawSceneBg(false, 0.75f);
  if (potionAim_ && fight) {
    auto alive = r.combat->aliveEnemies();
    if (target_ >= (int)alive.size()) target_ = 0;
    R().text(kBot / 2, 40, "选择目标", ts(F16, col::gold, CENTER));
    if (!alive.empty()) {
      Creature* t = alive[target_];
      std::string name = R().hasLoc("monsters." + t->name + ".name") ? L("monsters." + t->name + ".name") : t->name;
      R().text(kBot / 2, 90, name + "  " + num(t->hp) + "/" + num(t->maxHp), ts(F16, col::white, CENTER));
    }
    button(20, 80, 50, 40, "<", ID_PGUP, alive.size() > 1);
    button(kBot - 70, 80, 50, 40, ">", ID_PGDN, alive.size() > 1);
    button(10, 196, 110, 36, "取消", ID_BACK);
    button(kBot - 120, 196, 110, 36, "使用", ID_CONFIRM, !alive.empty(), true);
    return;
  }
  R().text(kBot / 2, 4, "药水", ts(F16, col::gold, CENTER));
  const float rx = 10, rw = kBot - 20, gap = n > 3 ? 4 : 6, y0 = 26;
  const float rh = std::min(46.f, (190.f - y0 - gap * (n - 1)) / n);
  for (int i = 0; i < n; ++i) {
    float y = y0 + i * (rh + gap);
    Potion* q = r.potions[i].get();
    bool hl = i == potionSel_;
    panel(rx, y, rw, rh, hl ? 0x5A3A20F0 : 0x2A2218E8, hl ? 0xFFD870FF : 0x8A7A5AFF);
    drawPotionIcon(q, rx + 5, y + (rh - std::min(36.f, rh - 4)) / 2, std::min(36.f, rh - 4));
    if (q) {
      R().text(rx + 48, y + 3, L("potions." + q->locKey + ".title"), ts(F12, col::gold));
      TextStyle st = ts(F12, col::white, LEFT, rw - 54, 0.8f);
      std::string d = describePotion(q);
      float dh;
      R().measure(d, st, &dh);
      float room = rh - 20;
      if (room < 10) { st.maxWidth = 0; d = d.substr(0, 0); }  // too small: title only
      else if (dh > room) st.scale *= room / dh;
      if (!d.empty()) R().text(rx + 48, y + 19, d, st);
    } else {
      R().text(rx + 48, y + (rh - R().lineHeight(F12)) / 2, "空", ts(F12, col::gray));
    }
    hits_.push_back({rx, y, rw, rh, ID_POTION0 + i});
  }
  bool canUse = potionSel_ >= 0 && r.canUsePotion(potionSel_);
  button(10, 196, 96, 36, "返回", ID_BACK);
  button(112, 196, 96, 36, "丢弃", ID_DISCARD, p != nullptr);
  button(214, 196, 96, 36, "使用", ID_USE, canUse, true);
}

void App::updatePotions(const gfx::Input& in) {
  Run& r = *run_;
  bool fight = r.screen == Screen::Combat && r.combat;
  int n = (int)r.potions.size();
  auto close = [&] { potionsOpen_ = false; potionAim_ = false; potionSel_ = -1; };
  // In combat the list only makes sense while the player may act.
  if (fight && !(r.combat->playerPhase && r.combat->actions.waiting())) { close(); return; }
  auto fire = [&](Creature* target) {
    int slot = potionSel_;
    close();
    if (fight) {
      PlayerAction a;
      a.kind = PlayerAction::UsePotion;
      a.potionSlot = slot;
      a.target = target;
      r.combat->actions.fire(a);
    } else {
      Scheduler::get().spawn(r.usePotion(slot, nullptr));
    }
  };
  auto use = [&] {
    if (potionSel_ < 0 || !r.canUsePotion(potionSel_)) return;
    if (fight && r.potions[potionSel_]->target == TargetType::AnyEnemy) {
      potionAim_ = true;
      target_ = 0;
      return;
    }
    fire(nullptr);
  };
  int id = in.touchDown ? hitAt(in.tx, in.ty) : ID_NONE;
  if (potionAim_) {
    auto alive = fight ? r.combat->aliveEnemies() : std::vector<Creature*>{};
    int m = (int)alive.size();
    if (m == 0 || (in.down & gfx::BTN_B) || id == ID_BACK) { potionAim_ = false; return; }
    if ((in.down & gfx::BTN_LEFT) || id == ID_PGUP) target_ = (target_ + m - 1) % m;
    if ((in.down & gfx::BTN_RIGHT) || id == ID_PGDN) target_ = (target_ + 1) % m;
    if ((in.down & gfx::BTN_A) || id == ID_CONFIRM) fire(alive[std::min(target_, m - 1)]);
    return;
  }
  if ((in.down & gfx::BTN_B) || id == ID_BACK) { close(); return; }
  if (in.down & gfx::BTN_DOWN) potionSel_ = std::min(n - 1, potionSel_ + 1);
  if (in.down & gfx::BTN_UP) potionSel_ = std::max(0, potionSel_ - 1);
  if (in.down & gfx::BTN_A) use();
  if ((in.down & gfx::BTN_X) && potionSel_ >= 0) r.discardPotion(potionSel_);
  if (id >= ID_POTION0 && id < ID_POTION0 + n) potionSel_ = id - ID_POTION0;
  if (id == ID_USE) use();
  if (id == ID_DISCARD && potionSel_ >= 0) r.discardPotion(potionSel_);
}

// PotionReward: the potion on top; take or skip below. With a full belt the belt is
// listed so one can be discarded first (the game refuses the reward while it is full).
void App::drawPotionOffer(bool top) {
  Run& r = *run_;
  Potion* p = r.potionOffer.get();
  if (top) {
    drawSceneBg(true, 0.6f);
    drawTopBar();
    R().text(kTop / 2, 24, "药水", ts(F16, col::gold, CENTER));
    if (p) {
      gfx::circle(kTop / 2.f, 84, 38, 0xFFE07030);
      drawPotionIcon(p, kTop / 2.f - 28, 56, 56);
      TextStyle nt = ts(F16, col::gold, CENTER);
      nt.scale = 1.2f;
      R().text(kTop / 2.f, 124, L("potions." + p->locKey + ".title"), nt);
      R().text(kTop / 2.f, 152, describePotion(p), ts(F12, col::white, CENTER, kTop - 60));
    }
    return;
  }
  drawSceneBg(false, 0.55f);
  bool room = r.hasOpenPotionSlot();
  if (p) {
    float s = 40, x = kBot / 2 - s / 2, y = room ? 60 : 8;
    drawPotionIcon(p, x, y, s);
    R().text(kBot / 2, y + s + 4, L("potions." + p->locKey + ".title"), ts(F16, col::white, CENTER));
  }
  if (!room) {
    R().text(kBot / 2, 74, "药水栏已满：点一瓶丢弃，或跳过", ts(F12, col::gold, CENTER));
    for (int i = 0; i < (int)r.potions.size(); ++i) {
      const int belt = (int)r.potions.size();
      const float pw = std::min(72.f, (kBot - 20.f) / belt - 6), step = (kBot - 20.f) / belt;
      float x = 10 + i * step + (step - pw) / 2, y = 96;
      panel(x, y, pw, 80, 0x2A2218E8, 0x8A7A5AFF);
      drawPotionIcon(r.potions[i].get(), x + pw / 2 - 20, y + 6, 40);
      if (r.potions[i]) R().text(x + pw / 2, y + 50, L("potions." + r.potions[i]->locKey + ".title"), ts(F12, col::white, CENTER, pw - 2, 0.7f));
      R().text(x + pw / 2, y + 64, "丢弃", ts(F12, col::red, CENTER, 0, 0.8f));
      hits_.push_back({x, y, pw, 80, ID_POTION0 + i});
    }
  }
  button(10, 196, 110, 36, "跳过", ID_SKIP);
  button(kBot - 120, 196, 110, 36, "拿取", ID_TAKE, p != nullptr && room, true);
}

void App::updatePotionOffer(const gfx::Input& in) {
  Run& r = *run_;
  if (!r.potionOfferChoice.waiting()) return;
  int id = in.touchDown ? hitAt(in.tx, in.ty) : ID_NONE;
  if (id >= ID_POTION0 && id < ID_POTION0 + (int)r.potions.size()) { r.discardPotion(id - ID_POTION0); return; }
  if (((in.down & gfx::BTN_A) || id == ID_TAKE) && r.hasOpenPotionSlot()) { r.potionOfferChoice.fire(1); return; }
  if ((in.down & gfx::BTN_B) || id == ID_SKIP) r.potionOfferChoice.fire(0);
}

// ================================================================ merchant

// RGDSplus U20: the goods and their prices on the bottom screen (cards above; relics,
// potions and the card removal service below), the picked item described on top;
// buying is pick, then 购买.
namespace {
constexpr float kShopCardS = 0.4f;
constexpr int kShopCells = 7;  // relics, potions, removal
constexpr float kShopCellW = 44.f, kShopRow2Y = 98.f;
}

void App::drawShop(bool top) {
  Run& r = *run_;
  int n = (int)r.shop.size();
  if (sel_ >= n) sel_ = -1;
  ShopItem* it = sel_ >= 0 ? &r.shop[sel_] : nullptr;
  if (top) {
    gfx::image(R().texture("gfx/bg_merchant.t3t"), 0, 0, kTop, kH, 0, 0, kTop, kH);
    drawTopBar();
    if (!r.shopMessage.empty() && R().hasLoc("merchant_room." + r.shopMessage + ".line1")) {
      std::string line = L("merchant_room." + r.shopMessage + ".line1");
      float w = R().measure(line, ts(F12)) + 16;
      panel(kTop - w - 110, 40, w, 22, 0xF0E6D0F0, 0x6A5030FF);
      R().text(kTop - w / 2 - 110, 44, line, ts(F12, 0x3A2A18FF, CENTER));
    }
    if (!it || !it->stocked()) return;
    int price = r.shopPrice(*it);
    TextStyle pt = ts(F16, price <= r.gold ? col::gold : col::red);
    if (it->kind == ShopItem::CardItem) {
      drawCard(it->card.get(), 10, 26, 1.2f, false, true);
      gfx::rect(160, kH - 40, kTop - 160, 40, 0x000000B0);
      R().text(170, kH - 32, cardTitle(it->card.get()) + "   " + num(price) + " 金币" + (it->onSale ? "（特价）" : ""), pt);
      return;
    }
    gfx::rect(0, kH - 70, kTop, 70, 0x000000C8);
    std::string title, desc;
    if (it->kind == ShopItem::RelicItem) {
      drawRelicIcon(it->relic.get(), 10, kH - 62, 44);
      title = L("relics." + it->relic->locKey + ".title");
      desc = describeRelic(it->relic.get());
    } else if (it->kind == ShopItem::PotionItem) {
      drawPotionIcon(it->potion.get(), 10, kH - 62, 44);
      title = L("potions." + it->potion->locKey + ".title");
      desc = describePotion(it->potion.get());
    } else {
      spr(R().sprite("ui/card_removal"), 10, kH - 62, 44, 44);
      title = L("merchant_room.MERCHANT.cardRemovalService.title");
      desc = L("merchant_room.MERCHANT.cardRemovalService.description");
      for (size_t p; (p = desc.find("{Amount}")) != std::string::npos;) desc.replace(p, 8, "25");
    }
    R().text(62, kH - 66, title + "   ", ts(F16, col::gold));
    R().text(kTop - 8, kH - 66, num(price) + " 金币", ts(F16, price <= r.gold ? col::gold : col::red, RIGHT));
    R().text(62, kH - 44, desc, ts(F12, col::white, LEFT, kTop - 70, 0.9f));
    return;
  }
  drawSceneBg(false, 0.6f);
  auto priceText = [&](const ShopItem& s, float cx, float y) {
    if (!s.stocked()) { R().text(cx, y, "售罄", ts(F12, col::gray, CENTER, 0, 0.85f)); return; }
    int price = r.shopPrice(s);
    R().text(cx, y, num(price), ts(F12, price <= r.gold ? col::gold : col::red, CENTER, 0, 0.9f));
  };
  // Character cards.
  const float cw = kCardW * kShopCardS, ch = kCardH * kShopCardS, gap = (kBot - 5 * cw) / 6;
  int cardIdx = 0;
  for (int i = 0; i < n; ++i) {
    ShopItem& s = r.shop[i];
    if (s.kind != ShopItem::CardItem) continue;
    float x = gap + cardIdx++ * (cw + gap), y = 4;
    if (i == sel_) gfx::rect(x - 3, y - 3, cw + 6, ch + 20, 0xFFE07060);
    if (s.card) {
      drawCard(s.card.get(), x, y, kShopCardS);
      if (s.onSale) spr(R().sprite("ui/sale_tag"), x + cw - 16, y - 2, 20, 20);
    }
    priceText(s, x + cw / 2, y + ch + 1);
    hits_.push_back({x, y, cw, ch + 16, ID_GRID0 + i});
  }
  // Relics, potions, card removal.
  const float x0 = (kBot - kShopCells * kShopCellW) / 2;
  int cell = 0;
  for (int i = 0; i < n; ++i) {
    ShopItem& s = r.shop[i];
    if (s.kind == ShopItem::CardItem) continue;
    float x = x0 + cell++ * kShopCellW, y = kShopRow2Y;
    if (i == sel_) gfx::rect(x + 1, y - 2, kShopCellW - 2, 52, 0xFFE07060);
    float ix = x + (kShopCellW - 32) / 2, iy = y + 2;
    if (s.kind == ShopItem::RelicItem && s.relic) drawRelicIcon(s.relic.get(), ix, iy, 32);
    else if (s.kind == ShopItem::PotionItem && s.potion) drawPotionIcon(s.potion.get(), ix, iy, 32);
    else if (s.kind == ShopItem::Removal) spr(R().sprite("ui/card_removal"), ix, iy, 32, 32, s.used ? 0x000000FF : 0xFFFFFFFF, s.used ? 0.6f : 0.f);
    priceText(s, x + kShopCellW / 2, y + 36);
    hits_.push_back({x, y, kShopCellW, 50, ID_GRID0 + i});
  }
  R().text(kBot / 2, 160, "金币 " + num(r.gold), ts(F16, col::gold, CENTER));
  bool canBuy = it && it->stocked() && r.shopPrice(*it) <= r.gold &&
                (it->kind != ShopItem::PotionItem || r.hasOpenPotionSlot());
  button(10, 196, 100, 36, "离开", ID_BACK);
  button(116, 196, 88, 36, "详情", ID_DETAIL,
         it && it->stocked() && (it->kind == ShopItem::CardItem || it->kind == ShopItem::RelicItem));
  button(kBot - 110, 196, 100, 36, "购买", ID_CONFIRM, canBuy, true);
}

void App::updateShop(const gfx::Input& in) {
  Run& r = *run_;
  if (!r.shopChoice.waiting()) return;
  int n = (int)r.shop.size();
  // D-pad: cards are 0..4 (top row), the other items follow (bottom row).
  if (in.down & gfx::BTN_RIGHT) sel_ = std::min(n - 1, sel_ + 1);
  if (in.down & gfx::BTN_LEFT) sel_ = std::max(0, sel_ - 1);
  if (in.down & gfx::BTN_DOWN) sel_ = sel_ < 5 ? std::min(n - 1, 5 + std::max(sel_, 0)) : sel_;
  if (in.down & gfx::BTN_UP) sel_ = sel_ >= 5 ? std::min(4, sel_ - 5) : sel_;
  int id = in.touchDown ? hitAt(in.tx, in.ty) : ID_NONE;
  if (id >= ID_GRID0 && id < ID_GRID0 + n) sel_ = id - ID_GRID0;
  if (id == ID_DETAIL && sel_ >= 0 && r.shop[sel_].stocked()) {
    ShopItem& item = r.shop[sel_];
    if (item.kind == ShopItem::CardItem) detailCard_ = item.card.get();
    if (item.kind == ShopItem::RelicItem) detailRelic_ = item.relic.get();
    detailUpgrade_ = false;
    return;
  }
  if ((in.down & gfx::BTN_A) || id == ID_CONFIRM) {
    if (sel_ < 0) return;
    int pick = sel_;
    if (r.shop[pick].kind == ShopItem::Removal) { sel_ = -1; scroll_ = 0; }  // the deck choice uses sel_ too
    r.shopChoice.fire(pick);
    return;
  }
  if (id == ID_BACK) { sel_ = -1; r.shopChoice.fire(-1); }
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
  button(118, 200, 84, 34, "详情", ID_DETAIL, sel_ >= 0 && sel_ < n);
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
  if ((in.down & gfx::BTN_A) && sel_ >= 0 && sel_ < n) { detailRelic_ = run_->relics[sel_].get(); return; }
  if (in.touchDown) {
    int id = hitAt(in.tx, in.ty);
    if (id >= ID_RELIC0 && id < ID_RELIC0 + n) {
      int picked = id - ID_RELIC0;
      if (sel_ == picked) { detailRelic_ = run_->relics[picked].get(); return; }
      sel_ = picked;
    }
    if (id == ID_DETAIL && sel_ >= 0 && sel_ < n) { detailRelic_ = run_->relics[sel_].get(); return; }
    if (id == ID_BACK) close();
    if (id == ID_DECK) { close(); openCardList(CardListMode::Deck); }
  }
}

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
    std::string title = e->finished ? "继续" : expandSmart(L("events." + e->options[i].key + ".title"), e->vars, false, &e->strVars);
    std::string desc = e->finished ? "" : expandSmart(L("events." + e->options[i].key + ".description"), e->vars, false, &e->strVars);
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

// ================================================================ developer menu

namespace {
const char* kDevActions[] = {"无敌", "回满血", "金币 +100", "最大生命 +10", "获得遗物…",
                             "加入卡牌…", "升级全部卡牌", "指定下一场战斗…", "秒杀敌人", "自由地图",
                             "跳到下一幕"};
constexpr int kDevActionCount = 11;
constexpr int kDevRows = 9;  // encounter list rows per page
}  // namespace

void App::drawDev(bool top) {
  Run& r = *run_;
  if (devRelics_.empty()) {
    std::vector<std::string> ids = db::sharedRelicPool();
    for (auto& id : run_->character().relicPool) ids.push_back(id);
    for (auto& id : ids) if (auto rel = db::relic(id)) { rel->run = run_.get(); devRelics_.push_back(std::move(rel)); }
    for (auto& id : run_->character().cardPool) if (auto c = db::card(id)) devCards_.push_back(std::move(c));
    for (auto& a : db::acts())
      for (auto* list : {&a.weak, &a.normal, &a.elites, &a.bosses})
        for (auto& id : *list) if (db::encounter(id)) devEncounters_.push_back(id);
  }
  for (auto& rel : devRelics_) rel->run = run_.get();
  if (top) {
    drawSceneBg(true, 0.75f);
    drawTopBar();
    R().text(kTop / 2, 22, "开发者模式", ts(F16, col::gold, CENTER));
    if (devPage_ == 1 && sel_ >= 0 && sel_ < (int)devRelics_.size()) drawRelicDetail(devRelics_[sel_].get(), 90);
    else if (devPage_ == 2 && sel_ >= 0 && sel_ < (int)devCards_.size()) drawCard(devCards_[sel_].get(), (kTop - 132) / 2, 44, 1.1f, false, true);
    else if (devPage_ == 3 && sel_ >= 0 && sel_ < (int)devEncounters_.size()) {
      R().text(kTop / 2, 100, devEncounters_[sel_], ts(F16, col::white, CENTER));
      R().text(kTop / 2, 124, "再点一次：下一场战斗就是它", ts(F12, col::gray, CENTER));
    } else {
      auto line = [&](float y, const std::string& k, const std::string& v, uint32_t c) {
        R().text(120, y, k, ts(F12, col::gray, RIGHT));
        R().text(130, y, v, ts(F12, c));
      };
      line(60, "无敌", r.devGod ? "开" : "关", r.devGod ? col::green : col::white);
      line(80, "自由地图", r.freeMap ? "开（任意房间可进）" : "关", r.freeMap ? col::green : col::white);
      line(100, "下一场战斗", r.devNextEncounter.empty() ? "随机" : r.devNextEncounter, col::white);
      line(140, "当前", "第 " + num(r.actIndex + 1) + " 幕 · " + r.act().name, col::white);
      line(120, "牌组 / 遗物", num((int)r.deck.size()) + " 张 / " + num((int)r.relics.size()) + " 个", col::white);
      R().text(kTop / 2, 160, devPage_ == 0 ? "SELECT 或 B 关闭" : "点一下选中，再点一次确认", ts(F12, col::gray, CENTER));
    }
    return;
  }
  drawSceneBg(false, 0.75f);
  if (devPage_ == 0) {
    for (int i = 0; i < kDevActionCount; ++i) {
      float x = i % 2 ? 164 : 6, y = 6 + (i / 2) * 38;
      if (i == 10) { x = 164; y = 200; }  // beside 关闭
      std::string label = kDevActions[i];
      bool on = (i == 0 && r.devGod) || (i == 9 && r.freeMap);
      if (i == 0 || i == 9) label += on ? "：开" : "：关";
      bool enabled = i == 8 ? (r.combat && r.combat->playerPhase && r.combat->actions.waiting())
                   : i == 10 ? (r.screen == Screen::Map && r.actIndex + 1 < Run::kActs) : true;
      button(x, y, 150, 32, label, ID_DEV0 + i, enabled, on);
    }
    button(10, 200, 100, 34, "关闭", ID_BACK);
  } else if (devPage_ == 1) {
    const int cols = 6;
    const float cell = 48, x0 = (kBot - cols * cell) / 2;
    for (int i = 0; i < (int)devRelics_.size(); ++i) {
      float x = x0 + (i % cols) * cell, y = 6 + (i / cols - scroll_) * cell;
      if (y < 0 || y > 150) continue;
      if (i == sel_) gfx::rect(x + 2, y + 2, cell - 4, cell - 4, 0xFFE07060);
      bool owned = r.hasRelic(devRelics_[i]->id);
      spr(R().sprite("relic/" + devRelics_[i]->icon), x + 6, y + 6, cell - 12, cell - 12, owned ? 0x000000FF : 0xFFFFFFFF, owned ? 0.6f : 0.f);
      hits_.push_back({x, y, cell, cell, ID_DEVITEM0 + i});
    }
  } else if (devPage_ == 2) {
    std::vector<Card*> cards;
    for (auto& c : devCards_) cards.push_back(c.get());
    drawCardGrid(cards, sel_, 0, 196, scroll_);
  } else {
    for (int k = 0; k < kDevRows; ++k) {
      int i = scroll_ * kDevRows + k;
      if (i >= (int)devEncounters_.size()) break;
      float y = 4 + k * 21;
      bool hl = i == sel_;
      gfx::rect(6, y, kBot - 12, 19, hl ? 0x8A5A20F0 : 0x00000080);
      R().text(12, y + 2, devEncounters_[i], ts(F12, hl ? col::gold : col::white));
      hits_.push_back({6, y, kBot - 12.f, 19, ID_DEVITEM0 + i});
    }
  }
  if (devPage_ != 0) {
    gfx::rect(0, 196, kBot, 44, 0x000000A0);
    button(10, 200, 90, 34, "返回", ID_BACK);
    button(kBot - 150, 200, 66, 34, "上页", ID_PGUP);
    button(kBot - 78, 200, 66, 34, "下页", ID_PGDN);
  }
  if (toastT_ > 0) {
    float w = R().measure(toast_, ts(F12)) + 16;
    gfx::rect((kBot - w) / 2, 172, w, 20, 0x000000C0);
    R().text(kBot / 2, 175, toast_, ts(F12, col::gold, CENTER));
  }
}

void App::devApply(int page, int i) {
  Run& r = *run_;
  auto say = [&](const std::string& s) { toast_ = s; toastT_ = 1.2f; };
  if (page == 1 && i < (int)devRelics_.size()) {
    const std::string& id = devRelics_[i]->id;
    if (r.hasRelic(id)) { say("已经有了"); return; }
    Scheduler::get().spawn(r.obtainRelic(db::relic(id)));
    say("获得遗物：" + L("relics." + devRelics_[i]->locKey + ".title"));
  } else if (page == 2 && i < (int)devCards_.size()) {
    r.deck.push_back(db::card(devCards_[i]->id));
    say("加入牌组：" + cardTitle(devCards_[i].get()));
  } else if (page == 3 && i < (int)devEncounters_.size()) {
    r.devNextEncounter = devEncounters_[i];
    say("下一场战斗：" + devEncounters_[i]);
    devPage_ = 0; sel_ = -1; scroll_ = 0;
  }
}

void App::updateDev(const gfx::Input& in) {
  Run& r = *run_;
  auto close = [&] { devOpen_ = false; devPage_ = 0; sel_ = -1; scroll_ = 0; };
  auto toPage = [&](int p) { devPage_ = p; sel_ = -1; scroll_ = 0; };
  if (in.down & gfx::BTN_B) { if (devPage_ == 0) close(); else toPage(0); return; }
  int count = devPage_ == 1 ? (int)devRelics_.size() : devPage_ == 2 ? (int)devCards_.size() : (int)devEncounters_.size();
  int perRow = devPage_ == 1 ? 6 : devPage_ == 2 ? 5 : 1;
  int pageRows = devPage_ == 1 ? 3 : devPage_ == 2 ? 2 : 1;
  int maxScroll = devPage_ == 3 ? (count - 1) / kDevRows : std::max(0, (count + perRow - 1) / perRow - pageRows);
  if (devPage_ != 0) {
    if (in.down & gfx::BTN_RIGHT) sel_ = std::min(count - 1, sel_ + 1);
    if (in.down & gfx::BTN_LEFT) sel_ = std::max(0, sel_ - 1);
    if (in.down & gfx::BTN_DOWN) sel_ = std::min(count - 1, sel_ + perRow);
    if (in.down & gfx::BTN_UP) sel_ = std::max(0, sel_ - perRow);
    if ((in.down & (gfx::BTN_LEFT | gfx::BTN_RIGHT | gfx::BTN_UP | gfx::BTN_DOWN)) && sel_ >= 0)
      scroll_ = devPage_ == 3 ? sel_ / kDevRows : std::clamp(sel_ / perRow - 1, 0, maxScroll);
    if ((in.down & gfx::BTN_A) && sel_ >= 0) { devApply(devPage_, sel_); return; }
  }
  if (!in.touchDown) return;
  int id = hitAt(in.tx, in.ty);
  if (id == ID_BACK) { if (devPage_ == 0) close(); else toPage(0); return; }
  if (id == ID_PGUP) { scroll_ = std::max(0, scroll_ - (devPage_ == 3 ? 1 : pageRows)); return; }
  if (id == ID_PGDN) { scroll_ = std::min(maxScroll, scroll_ + (devPage_ == 3 ? 1 : pageRows)); return; }
  if (devPage_ == 0 && id >= ID_DEV0 && id < ID_DEV0 + kDevActionCount) {
    Creature* p = r.player.get();
    switch (id - ID_DEV0) {
      case 0: r.devGod = !r.devGod; break;
      case 1: p->hp = p->maxHp; break;
      case 2: r.gold += 100; break;
      case 3: p->maxHp += 10; p->hp += 10; break;
      case 4: toPage(1); break;
      case 5: toPage(2); break;
      case 6: for (auto& c : r.deck) c->upgrade(); toast_ = "牌组已全部升级"; toastT_ = 1.2f; break;
      case 7: toPage(3); break;
      case 8:
        if (r.combat && r.combat->playerPhase && r.combat->actions.waiting()) {
          PlayerAction a;
          a.kind = PlayerAction::DevKillAll;
          r.combat->actions.fire(a);
          close();
        }
        break;
      case 9: r.freeMap = !r.freeMap; break;
      case 10:
        if (r.screen == Screen::Map && r.actIndex + 1 < Run::kActs) {
          r.devSkipAct = true;  // Run::main enters the next act at the map choice
          r.mapChoice.fire(-1);
          mapScroll_ = 0;
          close();
        }
        break;
    }
    return;
  }
  int card = devPage_ == 2 && id >= ID_GRID0 ? id - ID_GRID0 : -1;
  int item = devPage_ != 2 && id >= ID_DEVITEM0 && id < ID_GRID0 ? id - ID_DEVITEM0 : card;
  if (item >= 0 && item < count) {
    if (sel_ == item) devApply(devPage_, item);
    else sel_ = item;
  }
}

// ================================================================ settings and end

void App::drawSettings(bool top) {
  drawSceneBg(top, 0.8f);
  if (top) {
    panel(36, 26, kTop - 72, 185);
    R().text(kTop / 2, 42, abandonConfirm_ ? "放弃本局？" : "设置", ts(F16, col::gold, CENTER, kTop - 96, 1.35f));
    if (abandonConfirm_) {
      R().text(kTop / 2, 105, "当前进度将被清除。", ts(F16, col::white, CENTER, kTop - 100));
      R().text(kTop / 2, 141, "确认后返回主菜单。", ts(F12, col::gray, CENTER));
    } else {
      R().text(kTop / 2, 91, "快速模式会加快战斗与界面动画。", ts(F12, col::white, CENTER, kTop - 100));
      R().text(kTop / 2, 122, "屏幕震动控制受击位移。", ts(F12, col::white, CENTER, kTop - 100));
      R().text(kTop / 2, 156, "音频尚未接入。", ts(F12, col::gray, CENTER));
    }
    return;
  }
  if (abandonConfirm_) {
    button(12, 92, 140, 42, "取消", ID_ABANDON_CANCEL);
    button(168, 92, 140, 42, "确认放弃", ID_ABANDON_CONFIRM, true, true);
    return;
  }
  button(18, 14, 284, 36, std::string("快速模式：") + (fastMode_ ? "开" : "关"), ID_FAST_MODE, true, fastMode_);
  button(18, 58, 284, 36, std::string("屏幕震动：") + (screenShake_ ? "开" : "关"), ID_SCREEN_SHAKE, true, screenShake_);
  button(18, 102, 284, 36, "音量：音频尚未接入", ID_NONE, false);
  button(18, 150, 284, 34, "放弃本局", ID_ABANDON);
  button(90, 202, 140, 32, "返回", ID_BACK);
}

void App::updateSettings(const gfx::Input& in) {
  int id = in.touchDown ? hitAt(in.tx, in.ty) : ID_NONE;
  if (abandonConfirm_) {
    if ((in.down & gfx::BTN_B) || id == ID_ABANDON_CANCEL) { abandonConfirm_ = false; return; }
    if ((in.down & gfx::BTN_A) || id == ID_ABANDON_CONFIRM) { returnTitle(); return; }
    return;
  }
  if ((in.down & (gfx::BTN_B | gfx::BTN_START)) || id == ID_BACK) { settingsOpen_ = false; return; }
  if ((in.down & gfx::BTN_X) || id == ID_FAST_MODE) {
    fastMode_ = !fastMode_;
    Scheduler::get().speed = fastMode_ ? 1.75 : 1.0;
    saveSettings();
  }
  if ((in.down & gfx::BTN_Y) || id == ID_SCREEN_SHAKE) {
    screenShake_ = !screenShake_;
    saveSettings();
  }
  if (id == ID_ABANDON) abandonConfirm_ = true;
}

void App::drawEnd(bool top, bool won) {
  if (top) {
    gfx::Texture* bg = R().texture(actTexture(*run_, "bg_"));
    gfx::image(bg, 0, 0, kTop, kH, 0, 0, kTop, kH, won ? 0x302000FF : 0x200000FF, 0.6f);
    TextStyle t = ts(F16, won ? col::gold : col::red, CENTER);
    t.scale = 2.f;
    R().text(kTop / 2, 70, won ? L("game_over_screen.BANNER.trueWin") : L("game_over_screen.BANNER.lose0"), t);
    std::string q = won ? "第三幕首领已被击败。" : L("game_over_screen.QUOTES.0" + num((int)(run_->seed % 10)));
    R().text(kTop / 2, 130, q, ts(F16, col::white, CENTER));
    R().text(kTop / 2, 169, "到达第 " + num(run_->floor) + " 层", ts(F12, col::gold, CENTER));
    return;
  }
  gfx::rectGradient(0, 0, kBot, kH, 0x201810FF, 0x0B0B12FF);
  panel(12, 10, kBot - 24, 165);
  R().text(kBot / 2, 20, "本局记录", ts(F16, col::gold, CENTER));
  auto row = [&](float y, const std::string& label, const std::string& value) {
    R().text(38, y, label, ts(F12, col::gray));
    R().text(kBot - 38, y, value, ts(F12, col::white, RIGHT));
  };
  row(53, "进度", "第 " + num(run_->actIndex + 1) + " 幕 · 第 " + num(run_->floor) + " 层");
  row(78, "生命", num(run_->player->hp) + "/" + num(run_->player->maxHp));
  row(103, "金币", num(run_->gold));
  row(128, "牌组", num((int)run_->deck.size()) + " 张");
  row(153, "遗物", num((int)run_->relics.size()) + " 个");
  button(12, 191, 140, 39, "主菜单", ID_TITLE);
  button(168, 191, 140, 39, "再来一局", ID_RESTART, true, true);
}

void App::updateEnd(const gfx::Input& in) {
  int id = in.touchDown ? hitAt(in.tx, in.ty) : ID_NONE;
  if ((in.down & gfx::BTN_B) || id == ID_TITLE) { returnTitle(); return; }
  if ((in.down & (gfx::BTN_A | gfx::BTN_START)) || id == ID_RESTART) startRun();
}

}  // namespace ui
