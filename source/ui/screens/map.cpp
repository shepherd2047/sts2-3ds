// Split from ui.cpp (F3).
#include "../ui_common.h"

namespace ui {

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

}  // namespace ui
