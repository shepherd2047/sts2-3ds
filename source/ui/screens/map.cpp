// Split from ui.cpp (F3).
#include "../ui_common.h"

namespace ui {

// ================================================================ map

namespace {
// NMapScreen's MapLegend/LegendItems (NMapLegendItem): these six, in this order.
constexpr RoomType kLegendTypes[] = {RoomType::Unknown, RoomType::Shop, RoomType::Treasure,
                                     RoomType::Rest, RoomType::Monster, RoomType::Elite};
constexpr const char* kLegendKeys[] = {"LEGEND_UNKNOWN", "LEGEND_MERCHANT", "LEGEND_TREASURE",
                                       "LEGEND_REST", "LEGEND_ENEMY", "LEGEND_ELITE"};
constexpr int kLegendCount = 6;
constexpr int ID_PAUSE = 2400;   // 暂停: opens the pause menu (Y2), like START
constexpr int ID_LEGEND = 7000;  // + item index (touch targets local to the map)
// Legend panel on the right of the lower screen (as on RGDSplus), clear of the paths.
constexpr float kLegendX = 320 - 72, kLegendY = 56, kLegendW = 68, kLegendRow = 17, kLegendTop = 21;

// EncounterModel.Title: encounters.<ID_IN_UPPER_SNAKE>.title, e.g. VantomBoss -> VANTOM_BOSS.
std::string encounterTitle(const std::string& id) {
  std::string key;
  for (size_t i = 0; i < id.size(); ++i) {
    char c = id[i];
    if (i > 0 && std::isupper((unsigned char)c) && !std::isupper((unsigned char)id[i - 1])) key += '_';
    key += (char)std::toupper((unsigned char)c);
  }
  key = "encounters." + key + ".title";
  return R().hasLoc(key) ? L(key) : id;
}
std::string fillVar(std::string s, const std::string& name, const std::string& value) {
  std::string tag = "{" + name + "}";
  for (size_t p; (p = s.find(tag)) != std::string::npos;) s.replace(p, tag.size(), value);
  return s;
}

// Act map colours (ActModel.MapTraveledColor / MapUntraveledColor): path dots and the ink circle.
struct MapInk { uint32_t traveled, untraveled; };
MapInk mapInk(const Run& r) {
  std::string k = r.act().key;
  if (k == "hive") return {0x27221CFF, 0x6E7750FF};
  if (k == "glory") return {0x1D1E2FFF, 0x60717CFF};
  if (k == "underdocks") return {0x180F24FF, 0x534A62FF};
  return {0x28231DFF, 0x877256FF};  // Overgrowth
}
constexpr uint32_t kRouteInk = 0x7A1E1200;   // the route about to be taken (the legend's highlight red), + alpha
constexpr uint32_t kFocusGlow = 0xFFD87060;  // style kFocus, soft, behind the focused node

// Scroll bounds (px) that keep the parchment on the screens: at the lowest the paper's bottom edge
// sits on the bottom screen's bottom edge; at the highest the topmost node (the boss, drawn over
// the paper's top edge) has come fully onto the bottom screen, where it can be tapped.
std::pair<float, float> mapScrollRange(const std::vector<MapNode>& nodes) {
  float top = -1620.f;
  for (auto& n : nodes) {
    float half = n.type == RoomType::Boss ? mapBossSize(nodes) / kMapS / 2 : 60.f;
    top = std::min(top, mapNative(n, nodes).second - half);
  }
  float lo = kBotOY + kH - (kMapY0 + 1620.f * kMapS);
  float hi = kBotOY + 4 - (kMapY0 + top * kMapS);
  return {lo, std::max(lo, hi)};
}

bool contains(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

// The D-pad route preview's last node. route[k] must be a child of the node before it (the first
// of them a child of `from`, the focused next node); the route is cut where the map changed.
int routeEnd(const Run& r, int from, std::vector<int>& route) {
  for (size_t k = 0; k < route.size(); ++k) {
    if (route[k] < 0 || route[k] >= (int)r.nodes.size() || !contains(r.nodes[from].next, route[k])) {
      route.resize(k);
      break;
    }
    from = route[k];
  }
  return from;
}

float nativeX(const Run& r, int i) { return mapNative(r.nodes[i], r.nodes).first; }
float nativeY(const Run& r, int i) { return mapNative(r.nodes[i], r.nodes).second; }

// The focused node's room on the top screen, right of the paper (the boss preview is on the
// left): its icon and name, its floor, and the legend's hover tip (NMapLegendItem); a boss shows
// its name and static_hover_tips.BOSS, the start point the Ancient's name and epithet.
void drawNodeInfo(const Run& r, int i) {
  const MapNode& n = r.nodes[i];
  std::string title, desc;
  Sprite ic = roomIcon(n.type, n.type == RoomType::Ancient ? r.ancientId : r.bossIdAt(i));
  uint32_t ink = 0;  // 0: the icon's own colours
  float iconS = 20;
  if (n.type == RoomType::Boss) {
    std::string name = encounterTitle(r.bossIdAt(i));
    bool loc = R().hasLoc("static_hover_tips.BOSS.title");
    title = loc ? fillVar(L("static_hover_tips.BOSS.title"), "BossName", name) : name;
    desc = loc ? fillVar(L("static_hover_tips.BOSS.description"), "BossName", name) : L("map.LEGEND_BOSS.hoverTip.description");
    ink = 0xE0CFA8FF;  // the boss icons are masks: parchment-coloured, as in the boss preview
    iconS = 30;
  } else if (n.type == RoomType::Ancient) {
    std::string k = "ancients.";
    for (char c : r.ancientId) k += (char)std::toupper((unsigned char)c);
    title = R().hasLoc(k + ".title") ? L(k + ".title") : L("map.LEGEND_ANCIENT.title");
    desc = (R().hasLoc(k + ".epithet") ? "[gold]" + L(k + ".epithet") + "[/gold]\n" : std::string()) +
           L("map.LEGEND_ANCIENT.hoverTip.description");
    iconS = 30;
  } else {
    const char* key = n.type == RoomType::Shop ? "LEGEND_MERCHANT" : n.type == RoomType::Treasure ? "LEGEND_TREASURE"
                    : n.type == RoomType::Rest ? "LEGEND_REST" : n.type == RoomType::Monster ? "LEGEND_ENEMY"
                    : n.type == RoomType::Elite ? "LEGEND_ELITE" : "LEGEND_UNKNOWN";
    title = L(std::string("map.") + key + ".hoverTip.title");
    desc = L(std::string("map.") + key + ".hoverTip.description");
  }
  int curRow = r.currentNode >= 0 ? r.nodes[r.currentNode].row : -1;
  std::string floorLine = tr("第 ", "Floor ") + num(std::max(0, r.floor + n.row - curRow)) + tr(" 层", "");

  const float w = 110, x = kTop - w - 4, y = 24, pad = 7;
  TextStyle dt = ts(F12, col::white, LEFT, w - 2 * pad);
  float dh = 0;
  R().measure(desc, dt, &dh);
  const float th = R().lineHeight(F16), lh = R().lineHeight(F12), head = std::max(iconS, th);
  widgets::panel("ui/hover_tip", x, y, w, pad + head + 2 + lh + 4 + dh + pad);
  if (ic) {
    float iw = std::min(ic.w / ic.h * iconS, iconS * 1.4f), ih = iw * ic.h / ic.w;
    spr(ic, x + pad + (iconS - iw) / 2, y + pad + (head - ih) / 2, iw, ih, ink ? ink : 0xFFFFFFFF, ink ? 1.f : 0.f);
  }
  float tx = x + pad + iconS + 4, avail = x + w - pad - tx;
  float sc = std::clamp(avail / std::max(1.f, R().measure(title, ts(F16))), 0.75f, 1.f);
  R().text(tx, y + pad + (head - th * sc) / 2, title, ts(F16, col::gold, LEFT, avail, sc));
  R().text(x + pad, y + pad + head + 2, floorLine, ts(F12, col::gray));
  R().text(x + pad, y + pad + head + 2 + lh + 4, desc, dt);
}
}  // namespace


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
  auto [nx, ny] = mapNative(n, run_->nodes);
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
    float d = std::hypot(vx - x, vy - y), radius = n.type == RoomType::Boss ? mapBossSize(r.nodes) / 2 : 11.f;
    if (d < radius && d < bestD) { best = i; bestD = d; }
  }
  return best;
}

// NMapScreen / NNormalMapPoint states: Traveled points keep their colours and get the ink circle
// (NMapCircleVfx), Travelable ones pulse (scale 1.2 + 0.25 sin(4t)), Untravelable ones are half
// transparent; travelled paths are darker, larger dots. The focused point is drawn at the hover
// scale 1.45 with a soft glow, and the route about to be taken (current point -> focused next
// point -> the D-pad preview above it) is inked red.
void App::drawMap(bool top) {
  Run& r = *run_;
  auto reach = r.reachableNodes();
  if (mapSel_ >= (int)reach.size()) mapSel_ = 0;
  auto path = r.pathNodes();  // the game's travelable points (the free map adds the rest to reach)
  const bool choosing = !mapView_ && r.mapChoice.waiting();
  const int sel = reach.empty() ? -1 : reach[mapSel_];
  const int cursor = sel >= 0 ? routeEnd(r, sel, mapRoute_) : -1;
  if (mapInspect_ >= (int)r.nodes.size()) mapInspect_ = -1;
  const int focus = mapInspect_ >= 0 ? mapInspect_ : choosing ? cursor : -1;
  if (top) {  // once per frame: scrolling, within the parchment
    auto [lo, hi] = mapScrollRange(r.nodes);
    if (!mapUserScroll_) {
      // The next steps low on the bottom screen (y 406, ~1.5 rows above the HUD), never below
      // the opening view; a route walked up with the D-pad keeps its end below the top bar.
      float nextY = 1e9f;
      for (int i : path) nextY = std::min(nextY, kMapY0 + nativeY(r, i) * kMapS);
      float target = nextY < 1e8f ? std::max(0.f, 406.f - nextY) : 0.f;
      if (focus >= 0) {
        float half = r.nodes[focus].type == RoomType::Boss ? mapBossSize(r.nodes) / 2 : 8.f;
        float fy = kMapY0 + nativeY(r, focus) * kMapS - half + target;
        if (fy < 26) target += 26 - fy;
      }
      target = std::clamp(target, lo, hi);
      mapScroll_ += (target - mapScroll_) * 0.15f;
    }
    mapScroll_ = std::clamp(mapScroll_, lo, hi);
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
  const MapInk ink = mapInk(r);
  const float wave = std::sin((float)time_ * 4);
  // The route about to be taken, as (from, to) edges.
  std::vector<std::pair<int, int>> route;
  if (choosing && sel >= 0) {
    if (r.currentNode >= 0 && contains(r.nodes[r.currentNode].next, sel)) route.push_back({r.currentNode, sel});
    int prev = sel;
    for (int k : mapRoute_) { route.push_back({prev, k}); prev = k; }
  }
  // Edges
  for (int a = 0; a < (int)r.nodes.size(); ++a) {
    auto& n = r.nodes[a];
    auto [x0, y0] = pos(n);
    for (int b : n.next) {
      auto [x1, y1] = pos(r.nodes[b]);
      if (std::max(y0, y1) < -20 || std::min(y0, y1) > kH + 20) continue;
      bool travelled = n.visited && r.nodes[b].visited;
      bool lit = !travelled && std::find(route.begin(), route.end(), std::pair{a, b}) != route.end();
      // NMapScreen.DrawPaths: a dot (map_dot) every 22 units, clear of both icons;
      // travelled dots are darker and 1.2x.
      float len = std::hypot(x1 - x0, y1 - y0);
      float step = 22.f * kMapS, clear = 40.f * kMapS;
      float ds = travelled || lit ? 2.4f : 2.f;
      uint32_t dc = travelled ? ink.traveled : lit ? kRouteInk | (uint32_t)(200 + 55 * wave) : (ink.untraveled & ~0xFFu) | 0xD8;
      for (float d = clear; d < len - clear; d += step) {
        float t = d / len;
        gfx::rect(x0 + (x1 - x0) * t - ds / 2, y0 + (y1 - y0) * t - ds / 2, ds, ds, dc);
      }
    }
  }
  // Nodes
  const Sprite circle = R().sprite("map/circle");
  for (int i = 0; i < (int)r.nodes.size(); ++i) {
    auto& n = r.nodes[i];
    auto [x, y] = pos(n);
    if (y < -40 || y > kH + 40) continue;
    const bool travelable = choosing && contains(path, i);
    const bool focused = i == focus;
    const bool onRoute = choosing && !focused && (i == sel || contains(mapRoute_, i));
    Sprite ic = roomIcon(n.type, n.type == RoomType::Ancient ? r.ancientId : r.bossIdAt(i));
    // Icon height at its native size (ui_atlas map icons) times the map scale.
    float nativeH = n.type == RoomType::Elite ? 70 : n.type == RoomType::Rest ? 90 : n.type == RoomType::Treasure ? 59
                  : n.type == RoomType::Unknown ? 72 : n.type == RoomType::Ancient ? 150 : 68;
    float sz = n.type == RoomType::Boss ? mapBossSize(r.nodes) : nativeH * kNodeScale;
    // NMapLegendItem focus -> NMapScreen.HighlightPointType: that type's icons at 1.45x, in white.
    bool legendLit = mapLegend_ >= 0 && n.type == kLegendTypes[mapLegend_];
    if (legendLit) sz *= 1.45f;
    if (n.type != RoomType::Boss) {
      if (focused) sz *= 1.45f;                       // NNormalMapPoint.HoverScale
      else if (onRoute) sz *= 1.3f;
      else if (travelable) sz *= 1.2f + 0.25f * wave;  // NNormalMapPoint._Process
      if (focused || onRoute) gfx::circle(x, y, sz * 0.75f, focused ? kFocusGlow : kFocusGlow - 0x30);
    } else {
      if (travelable) sz *= 1.f + 0.06f * wave;  // a travelable boss (NBossMapPoint): a slow pulse
      if (focused) gfx::circle(x, y, sz * 0.5f, kFocusGlow - 0x28);
    }
    // MapPointState colours: Traveled / Travelable white, Untravelable halfTransparentWhite.
    uint32_t tint = n.visited || travelable || focused || onRoute || legendLit ? 0xFFFFFFFF : 0xFFFFFF80;
    float w = ic.w / ic.h * sz;
    gfx::pushTransform(gfx::Affine::rotateAround(x, y, n.angle * 3.14159265f / 180.f));  // NMapPoint.SetAngle
    if (n.type == RoomType::Boss) {
      // NBossMapPoint: the placeholder image over its outline (MapBgColor), so the boss reads on
      // the parchment and above it. Boss icons are masks/sprites: ink them.
      const float o = 1.5f;
      for (auto [dx, dy] : {std::pair{-o, 0.f}, {o, 0.f}, {0.f, -o}, {0.f, o}})
        spr(ic, x - w / 2 + dx, y - sz / 2 + dy, w, sz, 0xD8C8A0FF, 1.f);
      spr(ic, x - w / 2, y - sz / 2, w, sz, n.visited ? 0x6A5A48FF : 0x2E241AFF, 1.f);
    } else
      spr(ic, x - w / 2, y - sz / 2, w, sz, tint, 0.f);
    gfx::popTransform();
    // NMapCircleVfx around a travelled point (200 units at 0.85-0.9x; a bit smaller here, where
    // the icons are small), turned by a per-row angle.
    if (n.visited && n.type != RoomType::Boss && n.type != RoomType::Ancient && circle) {
      float cs = 140.f * kNodeScale, rot = (float)((n.row * 131 + n.col * 57) % 360) * 3.14159265f / 180.f;
      gfx::pushTransform(gfx::Affine::rotateAround(x, y, rot));
      spr(circle, x - cs / 2, y - cs / 2, cs, cs, (ink.traveled & ~0xFFu) | 0xF2, 1.f);
      gfx::popTransform();
    }
    if (!n.quests.empty()) {  // MapPoint.Quests (E2: SpoilsMap's treasure): NMapPoint's quest mark
      Sprite q = R().sprite("map/quest");
      if (q) spr(q, x + sz / 2 - 6, y - sz / 2 - 6, 12, 12);
      else {
        gfx::circle(x + sz / 2 - 1, y - sz / 2 + 1, 5.5f, 0xE0B040FF);
        R().text(x + sz / 2 - 1, y - sz / 2 - 6, "!", ts(F12, 0x2E241AFF, CENTER));
      }
    }
    if (i == r.currentNode) spr(R().sprite("map/marker"), x - 7, y - sz / 2 - 14, 14, 16);
  }

  if (top) {
    drawTopBar();
    drawBossPreview();
    if (focus >= 0) drawNodeInfo(r, focus);
    return;
  }
  // Bottom HUD in the corners, clear of row 0. Looking at the map from another room
  // (the pause menu's 地图) shows only the red 返回 in the bottom-left corner, as on RGDSplus.
  if (mapView_) {
    button(4, 208, 72, 26, tr("返回", "Back"), ID_BACK);
  } else {
    button(4, 210, 64, 26, tr("牌组", "Deck"), ID_DECK);
    button(72, 210, 64, 26, tr("遗物", "Relics"), ID_RELICS);
    button(140, 210, 56, 26, tr("药水", "Potions"), ID_POTIONS);
    button(kBot - 52, 4, 48, 24, tr("暂停", "Pause"), ID_PAUSE);  // top-right, clear of the nodes and the legend
  }
  // Legend (NMapScreen MapLegend: LEGEND_HEADER over the six NMapLegendItems) on the right, as
  // on RGDSplus. Tapping an item is its focus: that point type lights up on the map and its hover
  // tip shows; tapping it again (or anything else) clears it.
  {
    const float h = kLegendTop + kLegendCount * kLegendRow + 10;
    widgets::panel("ui/panel_legend", kLegendX, kLegendY, kLegendW, h);
    R().text(kLegendX + kLegendW / 2, kLegendY + 4, L("map.LEGEND_HEADER"), ts(F12, col::dark, CENTER));
    for (int i = 0; i < kLegendCount; ++i) {
      float y = kLegendY + kLegendTop + i * kLegendRow;
      Sprite ic = roomIcon(kLegendTypes[i]);
      float s = mapLegend_ == i ? 17.f : 14.f;  // NMapLegendItem: the icon at 1.25x while focused
      if (ic) spr(ic, kLegendX + 17 - ic.w / ic.h * s / 2, y + 8 - s / 2, ic.w / ic.h * s, s);  // coloured, as in the original
      TextStyle lt = ts(F12, mapLegend_ == i ? 0x7A1E12FF : col::dark, LEFT, 0, 0.85f);
      const std::string name = L(std::string("map.") + kLegendKeys[i] + ".title");
      float nw = R().measure(name, lt);  // Y3: English names are wider; shrink to the panel
      if (nw > kLegendW - 31) lt.scale *= (kLegendW - 31) / nw;
      R().text(kLegendX + 28, y + 2 + (0.85f - lt.scale) * R().lineHeight(F12) / 2, name, lt);
      hits_.push_back({kLegendX, y, kLegendW, kLegendRow, ID_LEGEND + i});
    }
    if (mapLegend_ >= 0) {
      std::string k = std::string("map.") + kLegendKeys[mapLegend_] + ".hoverTip.";
      widgets::keywordTip(L(k + "title"), L(k + "description"), kLegendX - 100,  // left of the legend
                          kLegendY + kLegendTop + mapLegend_ * kLegendRow + 20, false);
    }
  }
}

// The boss preview (NTopBarBossIcon) in the top screen's corner, under the top bar: the act's
// boss icon and name (static_hover_tips BOSS); with DoubleBoss both icons, the second offset
// behind the first (second_boss_icon.tscn at (30, 22)) and dimmed until the first is beaten, and
// DOUBLE_BOSS's names. Standing on the first boss (ShouldOnlyShowSecondBossIcon) only the
// second is shown.
void App::drawBossPreview() {
  Run& r = *run_;
  int b = r.bossNode(), b2 = r.secondBossNode();
  if (b < 0) return;
  bool onlySecond = b2 >= 0 && r.currentNode == b;
  const std::string& first = onlySecond ? r.secondBossId : r.bossId;
  const float x = 6, y = 22, s = 40;
  auto icon = [&](const std::string& id, float ix, float iy, float size, uint32_t ink) {
    Sprite ic = roomIcon(RoomType::Boss, id);
    if (!ic) return;
    float w = ic.w / ic.h * size;
    spr(ic, ix + (size - w) / 2, iy, w, size, ink, 1.f);  // the icon is a mask: paint it
  };
  bool two = b2 >= 0 && !onlySecond;
  // Parchment-coloured on the dark scene; the second one dimmed (MapUntraveledColor) until its turn.
  if (two) icon(r.secondBossId, x + s * 0.45f, y + s * 0.35f, s * 0.75f, 0x8A7A62FF);
  icon(first, x, y, s, 0xE0CFA8FF);
  std::string name = two ? fillVar(fillVar(L("static_hover_tips.DOUBLE_BOSS.title"), "BossName1", encounterTitle(r.bossId)),
                                   "BossName2", encounterTitle(r.secondBossId))
                         : fillVar(L("static_hover_tips.BOSS.title"), "BossName", encounterTitle(first));
  if (!R().hasLoc("static_hover_tips.BOSS.title"))  // romfs built before C11
    name = two ? encounterTitle(r.bossId) + " & " + encounterTitle(r.secondBossId) : encounterTitle(first);
  R().text(x, y + s + (two ? 12 : 4), name, ts(F12, col::gold, LEFT, 64));
}

// RGDSplus R4_MAP_TOUCH: drag anywhere to scroll; a press that never moved more than
// the slop and started on a reachable node enters it on release; a tap on any other node shows
// its room on the top screen. D-pad: ←→ move along the next row (or, up the preview, between the
// children of the node below), ↑ follows a path upward (the straightest child), ↓ steps back,
// B clears the preview, A enters the focused next node. L / R (and ↑↓ when only looking) scroll.
void App::updateMap(const gfx::Input& in) {
  Run& r = *run_;
  // Opened from another room (mapView_): look and drag only; 返回 / B / START go back.
  if (mapView_ && ((in.down & (gfx::BTN_B | gfx::BTN_START)) || (in.touchDown && hitAt(in.tx, in.ty) == ID_BACK))) {
    mapView_ = false;
    mapTouch_ = {};
    mapInspect_ = -1;
    return;
  }
  bool choosing = !mapView_ && r.mapChoice.waiting();
  if (!choosing && !mapView_) { mapTouch_ = {}; mapRoute_.clear(); mapInspect_ = -1; return; }
  auto reach = r.reachableNodes();
  int n = (int)reach.size();
  if (mapSel_ >= n) mapSel_ = 0;
  float scroll = 0;  // + shows the rows above (toward the boss)
  if (in.held & gfx::BTN_R) scroll += 4;
  if (in.held & gfx::BTN_L) scroll -= 4;
  if (mapView_ && (in.held & gfx::BTN_UP)) scroll += 4;
  if (mapView_ && (in.held & gfx::BTN_DOWN)) scroll -= 4;
  if (scroll != 0) {
    auto [lo, hi] = mapScrollRange(r.nodes);
    mapScroll_ = std::clamp(mapScroll_ + scroll, lo, hi);
    mapUserScroll_ = true;
  }
  if (n == 0) return;
  const int sel = reach[mapSel_];
  if (choosing && (in.down & (gfx::BTN_LEFT | gfx::BTN_RIGHT | gfx::BTN_UP | gfx::BTN_DOWN))) {
    const int cur = routeEnd(r, sel, mapRoute_);
    mapInspect_ = -1;
    mapUserScroll_ = false;
    if (in.down & (gfx::BTN_LEFT | gfx::BTN_RIGHT)) {
      std::vector<int> row;
      if (!mapRoute_.empty()) {
        row = r.nodes[mapRoute_.size() >= 2 ? mapRoute_[mapRoute_.size() - 2] : sel].next;
      } else if (auto path = r.pathNodes(); contains(path, sel)) {
        row = path;
      } else {  // the free map: a node off the paths, among the reachable ones of its row
        for (int i : reach) if (r.nodes[i].row == r.nodes[sel].row) row.push_back(i);
      }
      std::sort(row.begin(), row.end(), [&](int a, int b) { return nativeX(r, a) < nativeX(r, b); });
      int k = (int)(std::find(row.begin(), row.end(), cur) - row.begin()) + ((in.down & gfx::BTN_LEFT) ? -1 : 1);
      if (k >= 0 && k < (int)row.size()) {
        if (!mapRoute_.empty()) mapRoute_.back() = row[k];
        else mapSel_ = (int)(std::find(reach.begin(), reach.end(), row[k]) - reach.begin()) % n;
      }
    }
    if ((in.down & gfx::BTN_UP) && !r.nodes[cur].next.empty()) {
      int best = r.nodes[cur].next[0];
      for (int c : r.nodes[cur].next)
        if (std::fabs(nativeX(r, c) - nativeX(r, cur)) < std::fabs(nativeX(r, best) - nativeX(r, cur))) best = c;
      mapRoute_.push_back(best);
    }
    if ((in.down & gfx::BTN_DOWN) && !mapRoute_.empty()) mapRoute_.pop_back();
  }
  if (choosing && (in.down & gfx::BTN_B)) { mapRoute_.clear(); mapInspect_ = -1; mapUserScroll_ = false; }
  if (choosing && (in.down & gfx::BTN_Y)) { openCardList(CardListMode::Deck); mapTouch_ = {}; return; }
  int pick = -1;
  if (choosing && (in.down & gfx::BTN_A)) pick = mapSel_;
  if (in.touchDown) {
    int hud = hitAt(in.tx, in.ty);
    // Legend items (both while choosing and when only looking): toggle the highlight.
    if (hud >= ID_LEGEND && hud < ID_LEGEND + kLegendCount) {
      mapLegend_ = mapLegend_ == hud - ID_LEGEND ? -1 : hud - ID_LEGEND;
      mapTouch_ = {};
      return;
    }
    mapLegend_ = -1;
    if (!choosing) hud = ID_NONE;
    if (hud == ID_DECK) { openCardList(CardListMode::Deck); mapTouch_ = {}; return; }
    if (hud == ID_RELICS) { relicsOpen_ = true; sel_ = run_->relics.empty() ? -1 : 0; scroll_ = 0; mapTouch_ = {}; return; }
    if (hud == ID_PAUSE) { openPause(); return; }
    if (hud == ID_POTIONS) { potionsOpen_ = true; potionAim_ = false; potionSel_ = -1; mapTouch_ = {}; return; }
    mapTouch_ = {};
    mapTouch_.down = true;
    mapTouch_.startY = mapTouch_.lastY = in.ty;
    mapTouch_.node = choosing ? mapNodeAt(in.tx, in.ty) : -1;
    if (mapTouch_.node >= 0) { mapSel_ = mapTouch_.node; mapRoute_.clear(); mapInspect_ = -1; }
  } else if (mapTouch_.down && in.touching) {
    // Map scrolling is vertical, so horizontal stylus drift alone should not
    // turn a node tap into a non-scrolling drag.
    if (std::fabs(in.ty - mapTouch_.startY) > kMapTapSlop) mapTouch_.dragged = true;
    if (mapTouch_.dragged) {
      auto [lo, hi] = mapScrollRange(r.nodes);
      mapScroll_ = std::clamp(mapScroll_ + (in.ty - mapTouch_.lastY), lo, hi);
      mapUserScroll_ = true;
    }
    mapTouch_.lastY = in.ty;
  }
  if (in.touchUp && mapTouch_.down) {
    // MapGesture.release: a tap needs no drag and the release over the pressed node.
    if (choosing && !mapTouch_.dragged && mapTouch_.node >= 0 && mapNodeAt(in.tx, in.ty) == mapTouch_.node)
      pick = mapTouch_.node;
    else if (!mapTouch_.dragged && mapTouch_.node < 0) {
      // Any other node: its room info on the top screen (a tap on the paper clears it).
      float vx = in.tx + kBotOX, vy = in.ty + kBotOY;
      mapInspect_ = -1;
      for (int i = 0; i < (int)r.nodes.size(); ++i) {
        auto [x, y] = mapPos(r.nodes[i]);
        float radius = r.nodes[i].type == RoomType::Boss ? mapBossSize(r.nodes) / 2 : 11.f;
        if (std::hypot(vx - x, vy - y) < radius) { mapInspect_ = i; break; }
      }
    }
    mapTouch_ = {};
  }
  if (pick >= 0) {
    sfx::mapSelect();
    r.mapChoice.fire(reach[pick]);
    mapSel_ = 0;
    mapUserScroll_ = false;
    mapRoute_.clear();
    mapInspect_ = -1;
  }
}

}  // namespace ui
