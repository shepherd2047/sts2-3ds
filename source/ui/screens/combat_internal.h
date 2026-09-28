// Split from ui.cpp (F3).
#include "../ui_common.h"

namespace ui {

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

}  // namespace ui
