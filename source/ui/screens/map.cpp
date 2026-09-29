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
constexpr float kLegendX = 320 - 64, kLegendY = 60, kLegendW = 60, kLegendRow = 17, kLegendTop = 21;

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
    Sprite ic = roomIcon(n.type, n.type == RoomType::Ancient ? r.ancientId : r.bossIdAt(i));
    // Icon height at its native size (ui_atlas map icons) times the map scale.
    float nativeH = n.type == RoomType::Elite ? 70 : n.type == RoomType::Rest ? 90 : n.type == RoomType::Treasure ? 59
                  : n.type == RoomType::Unknown ? 72 : n.type == RoomType::Ancient ? 150 : 68;
    float sz = n.type == RoomType::Boss ? mapBossSize(r.nodes) : nativeH * kNodeScale;
    // NMapLegendItem focus -> NMapScreen.HighlightPointType: that type's icons at 1.45x, in white.
    bool legendLit = mapLegend_ >= 0 && n.type == kLegendTypes[mapLegend_];
    if (legendLit) sz *= 1.45f;
    if (reachable && n.type != RoomType::Boss) {
      float pulse = 1.f + 0.15f * std::sin((float)time_ * 6);
      sz *= chosen ? 1.4f * pulse : pulse;
      gfx::circle(x, y, sz * 0.75f, chosen ? 0xFFE07070 : 0xFFFFFF38);
    } else if (reachable && !mapView_) {  // a travelable boss (NBossMapPoint): a slow pulse
      sz *= 1.f + 0.06f * std::sin((float)time_ * 4);
    }
    uint32_t tint = n.visited && !legendLit ? 0x404040FF : 0xFFFFFFFF;
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
      spr(ic, x - w / 2, y - sz / 2, w, sz, tint, n.visited ? 0.5f : 0.f);
    gfx::popTransform();
    if (i == r.currentNode) spr(R().sprite("map/marker"), x - 7, y - sz / 2 - 14, 14, 16);
  }

  if (top) {
    drawTopBar();
    drawBossPreview();
    return;
  }
  // Bottom HUD in the corners, clear of row 0. Looking at the map from another room
  // (the pause menu's 地图) shows only the red 返回 in the bottom-left corner, as on RGDSplus.
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
    button(kBot - 52, 4, 48, 24, "暂停", ID_PAUSE);  // top-right, clear of the nodes and the legend
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
      if (ic) spr(ic, kLegendX + 11 - ic.w / ic.h * s / 2, y + 8 - s / 2, ic.w / ic.h * s, s, 0x2E241AFF, 1.f);
      R().text(kLegendX + 21, y + 2, L(std::string("map.") + kLegendKeys[i] + ".title"),
               ts(F12, mapLegend_ == i ? 0x7A1E12FF : col::dark, LEFT, 0, 0.85f));
      hits_.push_back({kLegendX, y, kLegendW, kLegendRow, ID_LEGEND + i});
    }
    if (mapLegend_ >= 0) {
      std::string k = std::string("map.") + kLegendKeys[mapLegend_] + ".hoverTip.";
      widgets::keywordTip(L(k + "title"), L(k + "description"), kLegendX - 100,  // left of the legend
                          kLegendY + kLegendTop + mapLegend_ * kLegendRow + 20, false);
    }
  }
  if (!reach.empty()) {
    std::string label = roomName(r.nodes[reach[mapSel_]].type);
    float w = R().measure(label, ts(F12)) + 12;
    gfx::rect(kBot - w - 4, 214, w, 20, 0x000000A0);
    R().text(kBot - 10, 217, label, ts(F12, col::gold, RIGHT));
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
    if (hud == ID_DEVMENU) { devOpen_ = true; devPage_ = 0; sel_ = -1; scroll_ = 0; mapTouch_ = {}; return; }
    if (hud == ID_PAUSE) { openPause(); return; }
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
    sfx::mapSelect();
    r.mapChoice.fire(reach[pick]);
    mapSel_ = 0;
    mapUserScroll_ = false;
  }
}

}  // namespace ui
