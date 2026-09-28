// UI style tokens (docs/UI_STYLE.md): palette, spacing, touch sizes and timings that every
// screen and widget shares. Colours are 0xRRGGBBAA. Text colours live in res.h (col::).
#pragma once
#include <cstdint>

namespace ui::style {

// ---- palette -------------------------------------------------------------------------
constexpr uint32_t kClear = 0x0B0B12FF;       // screen clear colour
constexpr uint32_t kScrim = 0x000000A0;       // dark overlay on a scene background
constexpr uint32_t kPanel = 0x22323BEC;       // panel fill (the game's slate: hover_tip / reward panel)
constexpr uint32_t kPanelEdge = 0x4F8790FF;   // panel border (teal)
constexpr uint32_t kPanelHi = 0x8FC1C8FF;     // panel highlight line / title underline
constexpr uint32_t kPlate = 0x2E4A57F0;       // button, normal (confirm_button teal-slate)
constexpr uint32_t kPlateHover = 0x3B6272F0;  // button, focused or hovered
constexpr uint32_t kPlatePress = 0x203641F0;  // button, pressed
constexpr uint32_t kPlateOff = 0x262A2CE0;    // button, disabled
constexpr uint32_t kPrimary = 0x872420F0;     // the one main action on a screen (red ribbon: proceed, end turn)
constexpr uint32_t kOk = 0x36567DF0;          // blue confirm ribbon (popup confirm)
constexpr uint32_t kDanger = 0x821F16F0;      // destructive action (abandon, delete): same red, plus a confirm
constexpr uint32_t kEdge = 0x6FA6AEFF;        // button border
constexpr uint32_t kEdgeOff = 0x555555FF;     // disabled border
constexpr uint32_t kFocus = 0xFFD870FF;       // focus ring / selected outline
constexpr uint32_t kSelectedFill = 0xFFD87028;  // selected row tint
constexpr uint32_t kLocked = 0x8A8A8AFF;      // locked or unavailable text
constexpr uint32_t kHpBar = 0xC03A3AFF;
constexpr uint32_t kBlockBar = 0x4A86C8FF;

// ---- geometry (px, real screen units) -------------------------------------------------
constexpr float kMargin = 8;      // outer margin on both screens
constexpr float kGap = 4;         // gap between neighbouring controls
constexpr float kTouchMin = 32;   // smallest touch target, both sides
constexpr float kRowH = 36;       // list row
constexpr float kButtonH = 34;    // standard button
constexpr float kActionY = 198;   // top of the bottom action bar (buttons are kButtonH tall)
constexpr float kTopBarH = 20;    // top-screen status bar
constexpr float kIconBtn = 32;    // square icon button
constexpr float kRadius = 0;      // corners stay square: the art is plated, not rounded

// ---- timings (seconds) -------------------------------------------------------------
constexpr float kPress = 0.08f;       // button press-in
constexpr float kFade = 0.18f;        // screen fade
constexpr float kSlide = 0.22f;       // panel slide-in (ease-out)
constexpr float kToast = 1.6f;        // toast lifetime
constexpr float kTipDelay = 0.35f;    // hover / hold before a tooltip
constexpr float kTick = 0.40f;        // gold / HP counters tick towards their value
constexpr float kFocusPulse = 1.2f;   // focus ring pulse period
constexpr float kCardFlight = 0.25f;  // a card flying to a pile

}  // namespace ui::style
