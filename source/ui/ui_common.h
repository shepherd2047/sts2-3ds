// Shared by every UI source file (split from ui.cpp, F3): the two-screen virtual canvas,
// button ids and small drawing / text helpers. Anything here is inline or constexpr.
#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <functional>

#include "res.h"
#include "sfx_router.h"
#include "style.h"
#include "tutorials.h"
#include "ui.h"
#include "widgets.h"

using namespace sts;

namespace ui {

// The act's room / map texture path (frees the previous act's textures on change). ui.cpp.
std::string actTexture(const sts::Run& r, const char* kind);
// S21: settings.sav's music / SFX / ambience volumes -> the audio buses. screens/settings.cpp.
void applyVolumes();
// The player character's Spine / sprite key. visuals.cpp.
std::string playerArt(sts::Run* r);
// X1.5-X4.5: the run's energy orb / card-energy-gem sprite names. The Ironclad's are the
// long-standing unsuffixed "ui/energy_orb" / "card/energy"; the other characters' were baked
// (X0/X2) as "..._<energyColor>" (Character::energyColor, e.g. "silent").
std::string energyOrbSprite(sts::Run* r);
std::string cardEnergySprite(const std::string& pool);  // a card pool's energyColor
// Card and event text: [dynvar], pluralisation and choose() formatting for loc strings. cardtext.cpp.
std::string expandSmart(const std::string& src, const std::vector<DynVar>& vars, bool inCombat,
                        const std::map<std::string, std::string>* strVars = nullptr, bool upgraded = false);

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
constexpr float kMapBgW = 260.f, kMapBgH = 552.f;  // bg_map.t3t parchment strip (1527x3240 * kMapS)
constexpr float kMapBgX = (gfx::kTopW - kMapBgW) / 2.f;
constexpr float kNodeScale = kMapS * 0.8f;  // node icons: ~9 px, as small as on RGDSplus
constexpr float kBossSize = 352.f * kMapS;
constexpr float kMapTapSlop = 5.f;       // 12 px of 768 on RGDSplus, rounded up for a stylus

// Native map coordinates (NMapScreen): our row r is the game's row r + 1.
// NMapScreen: rows are 2325 / (rowCount - 1) apart (155 in Overgrowth's 16-row grid,
// wider in the shorter Hive and Glory maps); the boss sits after the last row.
// DoubleBoss (a second Boss node after the first): rows are 0.9 as far apart, and the two
// boss nodes sit at (-200, -1980 * 0.9) and (-200, -2280 * 0.9) scaled 0.75.
inline bool mapDoubleBoss(const std::vector<MapNode>& nodes) {
  int bosses = 0;
  for (auto& n : nodes) bosses += n.type == RoomType::Boss;
  return bosses > 1;
}
inline int mapRowCount(const std::vector<MapNode>& nodes) {  // the grid's rows: one less than the (first) boss's
  for (auto& n : nodes) if (n.type == RoomType::Boss) return n.row + 1;
  return 16;
}
inline float mapDistY(const std::vector<MapNode>& nodes) {
  return 2325.f / (float)std::max(2, mapRowCount(nodes) - 1) * (mapDoubleBoss(nodes) ? 0.9f : 1.f);
}
// The boss icon's size on the map (NBossMapPoint, 0.75 with two bosses).
inline float mapBossSize(const std::vector<MapNode>& nodes) { return kBossSize * (mapDoubleBoss(nodes) ? 0.75f : 1.f); }
inline std::pair<float, float> mapNative(const MapNode& n, const std::vector<MapNode>& nodes) {
  if (n.type == RoomType::Boss) {
    // NBossMapPoint (400 x 400, top-left at (-200, -1980 * num)): centred at (0, -1780) on one boss.
    if (!mapDoubleBoss(nodes)) return {0.f, -1780.f};
    bool second = n.row >= mapRowCount(nodes);
    return {0.f, (second ? -2280.f : -1980.f) * 0.9f + 200.f};
  }
  if (n.type == RoomType::Ancient) return {0.f, 790.f};  // the game's row 0: the start point
  float distY = mapDistY(nodes);
  return {n.col * 150.f - 450.f + n.jx, 790.f - (n.row + 1.f) * distY + n.jy};
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
  ID_INSPECT,  // S10 combat inspect (信息)
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
  ID_ASC_DOWN,
  ID_ASC_UP,
  ID_SEED,
  ID_POTION0 = 900,  // + belt slot
  ID_TARGET0 = 100,   // + enemy index
  ID_HAND0 = 200,     // + hand index
  ID_NODE0 = 300,     // + reachable index
  ID_REWARD0 = 400,   // + reward index
  ID_RELIC0 = 500,    // + owned relic index
  ID_DEV0 = 600,      // + developer action
  ID_DEVITEM0 = 700,  // + developer picker row/cell
  ID_GRID0 = 1000,    // + grid index
  ID_CHAR0 = 1900,    // + character select button (S04)
};

inline Res& R() { return res(); }

inline void spr(const Sprite& s, float x, float y, float w = -1, float h = -1, uint32_t tint = 0xFFFFFFFF, float blend = 0) {
  if (!s) return;
  gfx::image(s.tex, s.x, s.y, s.w, s.h, x, y, w < 0 ? s.w : w, h < 0 ? s.h : h, tint, blend);
}

inline TextStyle ts(FontSize f = F12, uint32_t c = col::white, Align a = LEFT, float maxW = 0, float scale = 1.f) {
  TextStyle t;
  t.size = f;
  t.color = c;
  t.align = a;
  t.maxWidth = maxW;
  t.scale = scale;
  return t;
}

inline std::string L(const std::string& key) { return R().loc(key); }

inline std::string roomName(RoomType t) {
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

inline Sprite roomIcon(RoomType t, const std::string& bossId = "VantomBoss") {
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

inline std::string num(int v) { return std::to_string(v); }

}  // namespace
namespace {
constexpr float kDrawPileX = 16, kDrawPileY = 222, kDiscardX = 304, kDiscardY = 222;
constexpr float kDrawTime = 0.34f, kDrawGap = 0.12f, kLeaveTime = 0.28f, kLeaveGap = 0.045f;
inline float easeOut(float t) { t = 1 - t; return 1 - t * t * t; }
inline float easeIn(float t) { return t * t; }
inline float approach(float v, float to, float k, float dt) { return v + (to - v) * (1 - std::exp(-k * dt)); }
}  // namespace

// detail.cpp: the stacked keyword tips of a card (combat top screen); returns the y below the last tip.
float drawCardTipColumn(Card* c, float x, float y, float w, const std::function<std::string(Card*)>& desc);

}  // namespace ui
