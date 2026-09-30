// S03 (U03): the profile screen and the main menu's profile button.
//
// NProfileScreen (screens/profiles/profile_screen.tscn): "choose a save profile" message, three
// NProfileButtons (reward_panel background, profile_icon_<N>, "{Id}号存档" title, info or 空存档,
// the map marker over the current one) with an NDeleteProfileButton under each used slot, and a
// back button. Picking the current slot just goes back; another slot switches profile
// (SaveManager.SwitchProfileId) and reloads the main menu. Delete asks first
// (PROFILE_SCREEN.DELETE_CONFIRM_POPUP); deleting the current slot keeps the screen open on the
// now empty profile, as NGame.ReloadMainMenu + OpenProfileScreen do.
// NOpenProfileScreenButton sits in the main menu's top-left corner (icon + "存档{Id}"), hotkey B
// (pauseAndBack).
//
// Port additions: slot names (profiles::rename) edited with the 3DS software keyboard
// (gfx::textInput), shown instead of "{Id}号存档"; the card shows wins / losses and whether a run
// is in progress (the port keeps no playtime / file dates), the top screen the focused slot's
// summary incl. its saved run (RGDSplus U03 keeps all operations on the bottom screen; its top
// screen summary is listed as still to do there).
// Keys: D-pad focus, A pick, X rename, Y delete, B back; everything is also tappable.
#include "../../core/profiles.h"
#include "../confirm.h"
#include "../ui_common.h"

namespace ui {

namespace {
constexpr int kN = profiles::kCount;
constexpr int kCard0 = 2301, kRename0 = 2311, kDelete0 = 2321, kBack = 2399;  // + slot index
constexpr float kCardW = 96, kCardH = 150, kCardGap = 8, kCardY = 12;
constexpr float kCardX0 = (kBot - (kN * kCardW + (kN - 1) * kCardGap)) / 2;
constexpr float kBtnY = kCardY + kCardH + 6, kBtnH = 32;

struct State {
  bool open = false;
  int sel = 0;          // focused item (index into items())
  profiles::Info info[kN];
  std::string runInfo[kN];  // the slot's saved run (top screen summary), "" = none
};
State& P() {
  static State s;
  return s;
}

struct Item { int id; float x, y, w, h; };

std::vector<Item> items() {
  std::vector<Item> v;
  for (int i = 0; i < kN; ++i) {
    float x = kCardX0 + i * (kCardW + kCardGap);
    v.push_back({kCard0 + i, x, kCardY, kCardW, kCardH});
    v.push_back({kRename0 + i, x, kBtnY, kCardW - (P().info[i].used ? 38 : 0), kBtnH});
    if (P().info[i].used) v.push_back({kDelete0 + i, x + kCardW - 32, kBtnY, 32, kBtnH});
  }
  v.push_back({kBack, 8, 204, 96, 34});
  return v;
}

// D-pad: the nearest item in the pressed direction (as the main menu's submenus).
int navigate(const std::vector<Item>& v, int cur, uint32_t down) {
  float dx = (down & gfx::BTN_RIGHT) ? 1.f : (down & gfx::BTN_LEFT) ? -1.f : 0.f;
  float dy = (down & gfx::BTN_DOWN) ? 1.f : (down & gfx::BTN_UP) ? -1.f : 0.f;
  if ((dx == 0 && dy == 0) || cur < 0 || cur >= (int)v.size()) return cur;
  float cx = v[cur].x + v[cur].w / 2, cy = v[cur].y + v[cur].h / 2;
  int best = cur;
  float bestScore = 1e9f;
  for (int i = 0; i < (int)v.size(); ++i) {
    if (i == cur) continue;
    float ox = v[i].x + v[i].w / 2 - cx, oy = v[i].y + v[i].h / 2 - cy;
    float along = ox * dx + oy * dy, across = std::abs(ox * dy) + std::abs(oy * dx);
    if (along <= 1) continue;
    float score = along + across * 2;
    if (score < bestScore) { bestScore = score; best = i; }
  }
  return best;
}

int indexOf(const std::vector<Item>& v, int id) {
  for (int i = 0; i < (int)v.size(); ++i)
    if (v[i].id == id) return i;
  return 0;
}

// Slot index (0-based) an item belongs to, or -1 (back).
int slotOf(int id) {
  for (int base : {kCard0, kRename0, kDelete0})
    if (id >= base && id < base + kN) return id - base;
  return -1;
}

std::string mm(const std::string& key) { return L("main_menu_ui." + key); }

std::string withId(std::string s, int id) {
  for (size_t p; (p = s.find("{Id}")) != std::string::npos;) s.replace(p, 4, num(id));
  return s;
}

std::string stripTags(std::string s, std::initializer_list<const char*> tags) {
  for (const char* tag : tags)
    for (size_t p; (p = s.find(tag)) != std::string::npos;) s.erase(p, strlen(tag));
  return s;
}

std::string defaultName(int id) { return withId(mm("PROFILE_SCREEN.BUTTON.title"), id); }
std::string displayName(const profiles::Info& in) { return in.name.empty() ? defaultName(in.id) : in.name; }

// The slot's run save, NContinueRunInfo style (character, act and floor, HP and gold).
std::string peekRun(int id) {
  std::string data;
  if (!gfx::readSave(profiles::runSaveName(id), data) || data.empty()) return "";
  Run peek;
  if (!peek.load(data) || !peek.player) return "";
  const Character& ch = peek.character();
  std::string act = peek.act().name;
  for (char& c : act) c = (char)std::toupper((unsigned char)c);
  std::string s = L("characters." + ch.key + ".title");
  if (peek.ascension > 0) s += "   " + stripTags(mm("CONTINUE_RUN_INFO.ascension"), {"[b]", "[/b]"}) + " " + num(peek.ascension);
  s += "\n" + L("acts." + act + ".title") + " [blue]- " + mm("CONTINUE_RUN_INFO.floor") + " " + num(peek.floor) + "[/blue]";
  s += "\n[icon:hp] [red]" + num(peek.player->hp) + "/" + num(peek.player->maxHp) + "[/red]   [icon:gold] [gold]" +
       num(peek.gold) + "[/gold]";
  return s;
}

void refresh() {
  for (int i = 0; i < kN; ++i) {
    P().info[i] = profiles::info(i + 1);
    P().runInfo[i] = P().info[i].hasRun ? peekRun(i + 1) : "";
  }
}

void outline(float x, float y, float w, float h, uint32_t c = col::gold, float t = 2) {
  gfx::rect(x - t, y - t, w + 2 * t, t, c);
  gfx::rect(x - t, y + h, w + 2 * t, t, c);
  gfx::rect(x - t, y, t, h, c);
  gfx::rect(x + w, y, t, h, c);
}

// A card title that fits: 16 px, or 12 px for a long name.
void fitText(float cx, float y, const std::string& s, uint32_t c, float maxW) {
  TextStyle st = ts(F16, c, CENTER);
  if (R().measure(s, st) > maxW) st = ts(F12, c, CENTER, maxW);
  R().text(cx, y, s, st);
}
}  // namespace

void App::openProfiles() {
  State& s = P();
  s.open = true;
  refresh();
  s.sel = indexOf(items(), kCard0 + profiles::current() - 1);  // InitialFocusedControl
}

void App::drawProfileChip(int hitId) {
  const int cur = profiles::current();
  const profiles::Info in = profiles::info(cur);
  Sprite icon = R().sprite("ui/profile_" + num(cur));
  if (icon) spr(icon, 6, 3, 20, 20);
  TextStyle st = ts(F12, col::gold);
  st.outline = 0x000000FF;
  std::string label = in.name.empty() ? withId(mm("OPEN_PROFILE_SCREEN.title"), cur) : in.name;
  float w = R().measure(label, st);
  R().text(30, 5, label, st);
  // Kept above the first menu row (y >= 24) so the two never share a touch point.
  hits_.push_back({2, 1, 32 + w, 22, hitId});
}

bool App::drawProfiles(bool top) {
  State& s = P();
  if (!s.open) return false;
  const auto v = items();
  s.sel = std::clamp(s.sel, 0, (int)v.size() - 1);
  const int focusId = v[s.sel].id;
  const int fs = std::max(0, slotOf(focusId));
  if (top) {
    drawMenuBg(true, 0.55f);
    TextStyle mt = ts(F16, col::gold, CENTER, 0, 1.15f);
    mt.outline = 0x000000FF;
    R().text(kTop / 2, 12, mm("PROFILE_SCREEN.BUTTON.chooseProfileMessage"), mt);
    // The focused slot's summary.
    const profiles::Info& in = s.info[fs];
    const float px = 60, pw = kTop - 120, py = 44;
    const bool run = !s.runInfo[fs].empty();
    const float ph = run ? 150 : 96;
    widgets::panel("ui/hover_tip", px, py, pw, ph);
    Sprite icon = R().sprite("ui/profile_" + num(in.id));
    if (icon) spr(icon, px + 12, py + 10, 32, 32);
    R().text(px + 52, py + 9, displayName(in), ts(F16, col::gold));
    std::string sub = in.name.empty() ? std::string() : defaultName(in.id) + "   ";
    if (in.id == profiles::current()) sub += "当前存档";
    R().text(px + 52, py + 29, sub, ts(F12, col::gray));
    float y = py + 52;
    if (!in.used) {
      R().text(px + 16, y, mm("PROFILE_SCREEN.BUTTON.empty"), ts(F12, col::white));
    } else {
      R().text(px + 16, y, "[blue]胜场[/blue] " + num(in.wins) + "     [blue]败场[/blue] " + num(in.losses),
               ts(F12, col::white));
      y += 22;
      if (run) {
        R().text(px + 16, y, "[gold]进行中的游戏[/gold]", ts(F12, col::white));
        R().text(px + 16, y + 18, s.runInfo[fs], ts(F12, col::white, LEFT, pw - 32));
      } else {
        R().text(px + 16, y, "没有进行中的游戏", ts(F12, col::gray));
      }
    }
    R().text(kTop / 2, 220, "A 选择    X 改名    Y 删除    B 返回", ts(F12, 0xC8C8C8FF, CENTER));
    return true;
  }

  drawMenuBg(false, 0.5f);
  const int cur = profiles::current();
  for (int i = 0; i < kN; ++i) {
    const profiles::Info& in = s.info[i];
    const float x = kCardX0 + i * (kCardW + kCardGap);
    const bool focus = focusId == kCard0 + i && !confirm::isOpen();
    const float y = kCardY - (focus ? 3 : 0);
    widgets::panel("ui/panel_reward", x, y, kCardW, kCardH, focus || fs == i ? 0xFFFFFFFF : 0xC0C0C0FF);
    if (i + 1 == cur) {  // CurrentProfileIndicator: the map marker over the top edge
      Sprite m = R().sprite("map/marker");
      if (m) spr(m, x + (kCardW - 22) / 2, y - 8, 22, 22);
    }
    Sprite icon = R().sprite("ui/profile_" + num(i + 1));
    if (icon) spr(icon, x + (kCardW - 30) / 2, y + 16, 30, 30);
    fitText(x + kCardW / 2, y + 50, displayName(in), col::gold, kCardW - 10);
    float ty = y + 72;
    if (!in.name.empty()) {
      R().text(x + kCardW / 2, ty, defaultName(in.id), ts(F12, col::gray, CENTER));
      ty += 16;
    }
    if (!in.used) {
      R().text(x + kCardW / 2, ty + 8, mm("PROFILE_SCREEN.BUTTON.empty"), ts(F12, col::white, CENTER));
    } else {
      R().text(x + kCardW / 2, ty, "[blue]胜场[/blue] " + num(in.wins), ts(F12, col::white, CENTER));
      R().text(x + kCardW / 2, ty + 16, "[blue]败场[/blue] " + num(in.losses), ts(F12, col::white, CENTER));
      if (in.hasRun) R().text(x + kCardW / 2, ty + 34, "[gold]进行中[/gold]", ts(F12, col::white, CENTER));
    }
    if (focus) outline(x, y, kCardW, kCardH);
    hits_.push_back({x, y, kCardW, kCardH, kCard0 + i});
  }
  // Rename / delete under each card.
  for (const Item& it : v) {
    const bool focus = it.id == focusId && !confirm::isOpen();
    if (it.id >= kRename0 && it.id < kRename0 + kN) {
      widgets::panel("ui/btn_ok_s", it.x, it.y, it.w, it.h);
      R().text(it.x + it.w / 2, it.y + (it.h - R().lineHeight(F16)) / 2, "改名", ts(F16, col::white, CENTER));
    } else if (it.id >= kDelete0 && it.id < kDelete0 + kN) {
      Sprite d = R().sprite("ui/btn_delete");
      if (d) spr(d, it.x, it.y, it.w, it.h, focus ? 0xFFFFFFFF : 0xE0E0E0FF);
      else R().text(it.x + it.w / 2, it.y + 8, "删", ts(F12, col::red, CENTER));
    } else if (it.id == kBack) {
      widgets::panel("ui/btn_back", it.x, it.y, it.w, it.h);
      R().text(it.x + it.w / 2, it.y + (it.h - R().lineHeight(F16)) / 2, "返回", ts(F16, col::white, CENTER));
    } else {
      continue;  // cards: drawn above
    }
    if (focus) outline(it.x, it.y, it.w, it.h);
    hits_.push_back({it.x, it.y, it.w, it.h, it.id});
  }
  return true;
}

bool App::updateProfiles(const gfx::Input& in) {
  State& s = P();
  if (!s.open) return false;
  int id = in.touchDown ? hitAt(in.tx, in.ty) : ID_NONE;

  auto v = items();
  s.sel = navigate(v, std::clamp(s.sel, 0, (int)v.size() - 1), in.down);
  const int focusSlot = slotOf(v[s.sel].id);
  if (id == ID_NONE && (in.down & gfx::BTN_A)) id = v[s.sel].id;
  if (id != ID_NONE)  // a tap moves the focus there; the hotkeys below leave it where it is
    for (int i = 0; i < (int)v.size(); ++i)
      if (v[i].id == id) s.sel = i;
  if (id == ID_NONE && (in.down & gfx::BTN_B)) id = kBack;
  if (id == ID_NONE && (in.down & gfx::BTN_X) && focusSlot >= 0) id = kRename0 + focusSlot;
  if (id == ID_NONE && (in.down & gfx::BTN_Y) && focusSlot >= 0 && s.info[focusSlot].used) id = kDelete0 + focusSlot;
  if (id == ID_NONE) return true;

  const int slot = slotOf(id);
  if (id == kBack) {
    s.open = false;
  } else if (id >= kCard0 && id < kCard0 + kN) {
    // NProfileButton.OnRelease: the current slot just closes; another one switches profile.
    if (slot + 1 != profiles::current()) {
      selectProfile(slot + 1);
      continueInfo_.clear();  // the main menu's NContinueRunInfo now reads this slot's save
      titleSelection_ = 0;
      reticleY_ = -1;
    }
    s.open = false;
  } else if (id >= kRename0 && id < kRename0 + kN) {
    const profiles::Info& pi = s.info[slot];
    std::string name;
    if (gfx::textInput(defaultName(slot + 1).c_str(), pi.name, name, profiles::kNameMax)) {
      // Trim surrounding spaces; an empty name goes back to the default label.
      size_t a = name.find_first_not_of(" \t"), b = name.find_last_not_of(" \t");
      name = a == std::string::npos ? std::string() : name.substr(a, b - a + 1);
      if (profiles::rename(slot + 1, name)) {
        toast_ = "已改名：" + (name.empty() ? defaultName(slot + 1) : name);
        toastT_ = 1.2f;
      }
      refresh();
    }
  } else if (id >= kDelete0 && id < kDelete0 + kN) {
    // NDeleteProfileButton: NGenericPopup with PROFILE_SCREEN.DELETE_CONFIRM_POPUP (S22 shared modal).
    const int id1 = slot + 1;
    confirm::ask(withId(mm("PROFILE_SCREEN.DELETE_CONFIRM_POPUP.title"), id1),
                 withId(mm("PROFILE_SCREEN.DELETE_CONFIRM_POPUP.description"), id1),
                 [this, id1] {
                   deleteProfile(id1);  // SaveManager.DeleteProfile (+ hasSave_)
                   continueInfo_.clear();
                   refresh();
                   P().sel = indexOf(items(), kCard0 + id1 - 1);
                 },
                 mm("PROFILE_SCREEN.DELETE_CONFIRM_POPUP.delete"), mm("PROFILE_SCREEN.DELETE_CONFIRM_POPUP.cancel"));
  }
  return true;
}

}  // namespace ui
