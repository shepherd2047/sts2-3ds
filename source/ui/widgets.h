// The F3 widget kit: art-backed controls (F1 sprites, F2 9-slice/clip/alpha) with one input
// model for touch and for D-pad/A/B/L/R. Immediate-mode: call a widget every frame with a
// stable `id`; it draws itself, registers its hit box, and returns whether it was activated
// this frame. Screens in track S adopt this kit as they are rebuilt (docs/UI_STYLE.md); older
// screens keep using App's own panel()/button() (source/ui/legacy_widgets.cpp) until then.
//
// Frame contract, called once per screen (both passes: top then bottom -- widgets only draw
// and register hits on the bottom pass, since only it is touched):
//   widgets::beginFrame(in);              // before drawing the bottom screen
//   ... draw widgets ...
//   widgets::endFrame();                  // after the bottom screen is drawn
// `in` is the frame's gfx::Input; D-pad focus movement is computed from the PREVIOUS frame's
// registered hit boxes (like an immediate-mode nav pass), so it is one frame behind on a
// layout change, which is never visible at 60 fps.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "res.h"
#include "style.h"
#include "../gfx/gfx.h"

namespace ui::widgets {

// ---- frame / focus / touch -----------------------------------------------------------------

void beginFrame(const gfx::Input& in);
void endFrame();
// While true, beginFrame ignores its input (no touch, no D-pad): another layer owns the input this
// frame -- the top bar's focus mode (S19), over a screen whose widgets read the input in draw.
void suspendInput(bool on);

// The currently focused control id (-1 if none; set by touch too, so the ring follows).
int focused();
void setFocus(int id);
// True while the player is using the D-pad (shows the focus ring); touch hides it.
bool usingPad();

// ---- primitives (used by the higher-level widgets, and directly for custom layouts) --------

// Registers a touch/focus target and returns true the frame it is activated (touch release
// while still inside, or A while focused). `enabled=false` never registers or focuses.
bool hit(int id, float x, float y, float w, float h, bool enabled = true);
// A 9-slice panel from an atlas sprite name (falls back to a flat style::kPanel box).
void panel(const std::string& sprite, float x, float y, float w, float h, uint32_t tint = 0xFFFFFFFF);
// The dotted focus ring, drawn around a control that is focused and usingPad().
void focusRing(int id, float x, float y, float w, float h);

// ---- controls --------------------------------------------------------------------------

enum class Kind { Primary, Secondary, Danger, Row, Event, Ancient };

// A labelled button; art per `kind` (docs/UI_STYLE.md button families 1-3, 5, 6).
bool button(int id, float x, float y, float w, float h, const std::string& label, Kind kind = Kind::Secondary,
            bool enabled = true, const char* rightLabel = nullptr);
// A square art-only button (piles, potion belt, deck/map/settings in the top bar).
bool iconButton(int id, float x, float y, float size, const std::string& iconSprite, bool enabled = true,
                bool notify = false);
// A row list item (icon left, label, optional right value) -- reward lists, shop, settings.
bool row(int id, float x, float y, float w, const std::string& iconSprite, const std::string& label,
        const std::string& rightValue = "", bool enabled = true, uint32_t rightColor = col::gold);
// An event / Ancient option button with a locked reason line under it when disabled.
bool optionButton(int id, float x, float y, float w, float h, const std::string& label, bool enabled = true,
                  const std::string& lockedReason = "", bool ancient = false);

// A left/right tab strip; returns the newly selected index, or `sel` unchanged.
int tabs(int baseId, float x, float y, float w, float h, const std::vector<std::string>& labels, int sel);
// A checkbox; returns the new value (toggles on activation).
bool toggle(int id, float x, float y, bool value, const std::string& label = "");
// A horizontal slider, 0..1; D-pad left/right on focus steps by `step` (default 0.1).
float slider(int id, float x, float y, float w, float value, float step = 0.1f);
// Page arrows either side of "n / total"; returns the (clamped) new page.
int paginator(int id, float x, float y, float w, int page, int total);

// A vertical scroll list: call begin(), draw rows at y - scroll (clip is already pushed),
// then end(). Drag scrolls with inertia; a thumb is drawn at the right edge when content
// overflows. `contentH` is the full (unclipped) content height.
struct ScrollList {
  int id;
  float x, y, w, h;
  float scroll = 0, vel = 0;
  bool dragging = false, everSet = false;
  void begin(int id_, float x_, float y_, float w_, float h_, float contentH);
  void end();
};

// A confirm modal (danger action). Draws over the whole bottom screen; returns 1 = confirmed,
// -1 = cancelled, 0 = still open. Call every frame while `open` is true.
int modal(int id, const std::string& title, const std::string& message, bool danger = true);

// A toast queued for `style::kToast` seconds, drawn centred near the top of the bottom screen.
void toast(const std::string& text);
void drawToasts(float dt);

// A ribbon banner (reward screen headers, act transitions).
void banner(float cx, float y, const std::string& text, float scale = 1.f);

// A keyword glossary popover (F4): `ui/hover_tip` sized to fit title + description, anchored
// above/right of (anchorX, anchorY) and flipped to stay on screen (screen is kTopW wide on the
// top pass, kBottomW on the bottom). For HoverTipFactory-style glossary entries (card_keywords,
// static_hover_tips, powers, ...) -- callers pass the already-localised title/description.
void keywordTip(const std::string& title, const std::string& description, float anchorX, float anchorY, bool top);

}  // namespace ui::widgets
