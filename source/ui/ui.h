#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../core/game.h"
#include "../gfx/gfx.h"
#include "../spine/spine.h"

namespace ui {

class App {
 public:
  bool init();
  void update(const gfx::Input& in, double dt);
  void draw();

 private:
  struct Float {
    sts::Creature* who;
    std::string text;
    uint32_t color;
    float t;
    float dx;
  };
  // Animated Spine body for one creature on screen.
  struct Visual {
    std::string key;
    const spine::SkeletonData* data = nullptr;
    std::unique_ptr<spine::Skeleton> skel;
    std::unique_ptr<spine::AnimationState> anim;
    bool puffed = false;  // Fuzzy Wurm Crawler after Inhale
    bool dying = false;
    float fade = 1.f;
  };
  struct Hit {  // touch target registered while drawing the bottom screen
    float x, y, w, h;
    int id;
  };

  // Bottom-screen combat interaction, modelled on the RGDSplus dual-screen port:
  // fanned hand (HandPosHelper), drag a card upward to lock the nearest enemy,
  // sideways to switch, release in the play zone, cross-screen targeting arrow.
  struct HandSlot { float x, y, angle, s; };
  struct Drag {
    bool down = false, moved = false, armed = false;
    int index = -1;
    sts::Card* card = nullptr;
    float grabDX = 0, grabDY = 0, originY = 0, x = 0, y = 0, startTx = 0, startTy = 0, lastTx = 0, accum = 0;
    sts::Creature* target = nullptr;
  };
  struct Flight {
    sts::Card* card;
    float t, x0, y0, x1, y1, s0;
  };
  HandSlot handSlot(int n, int i) const;
  int hitHandCard(float tx, float ty);
  void drawArrow(bool top, float fx, float fy, float tx, float ty, bool locked, bool ally);
  void drawFlights(bool top);
  void startFlight(sts::Card* c, float x, float y, float s, sts::Creature* target);
  // Displayed hand-card transforms, eased towards their fan slots every frame.
  struct Pose { float x = 0, y = 0, angle = 0, s = 0; float delay = 0, drawT = 1; float x0 = 0, y0 = 0, a0 = 0, s0 = 0; };
  // A card that left the hand without being played (discard/exhaust/shuffle back).
  struct Ghost { sts::Card* card; Pose from; float t, delay, tx, ty, ts; bool exhaust; };
  void animateHand(float dt);
  void drawGhosts();

  void startRun();
  Visual* visual(sts::Creature* c);
  void trigger(sts::Creature* c, const std::string& what, int amount);
  std::string idleAnim(const Visual& v) const;
  void autoplay(double dt);  // STS_AUTOPLAY: drive the UI with a trivial policy (testing)
  void consumeEvents();

  // screens
  void drawTitle(bool top);
  void drawMap(bool top);
  void drawCombat(bool top);
  void drawReward(bool top);
  void drawRest(bool top);
  void drawUpgrade(bool top);
  void drawDeck(bool top);
  void drawEnd(bool top, bool won);
  void drawRelicOffer(bool top);  // elite relic reward / treasure chest
  void drawRelics(bool top);      // owned relics: grid below, the picked one above
  // Events (RGDSplus U21): art and text above, the options stacked in the middle below.
  void drawEvent(bool top);
  void drawAncient(bool top);
  void updateEvent(const gfx::Input& in);
  // Choosing cards from the deck for an event or relic (CardSelectCmd.FromDeck*).
  void drawDeckChoice(bool top);
  void updateDeckChoice(const gfx::Input& in);
  std::vector<int> deckPicks_;
  // Developer menu (SELECT, or 开发 on the map): cheats and pickers for testing.
  void drawDev(bool top);
  void updateDev(const gfx::Input& in);
  void devApply(int page, int index);

  void updateTitle(const gfx::Input& in);
  void updateMap(const gfx::Input& in);
  void updateCombat(const gfx::Input& in);
  void updateReward(const gfx::Input& in);
  void updateRest(const gfx::Input& in);
  bool restValid(int option) const;
  void updateUpgrade(const gfx::Input& in);
  void updateDeck(const gfx::Input& in);
  void updateEnd(const gfx::Input& in);
  void updateRelicOffer(const gfx::Input& in);
  void updateRelics(const gfx::Input& in);
  // Potions (package 8): the belt as a list on the bottom screen (opened with 药水 in
  // combat or on the map), the picked potion described on top; enemy-targeted potions
  // then pick a target with ◀ ▶ (the reticle shows on the top screen).
  void drawPotions(bool top);
  void updatePotions(const gfx::Input& in);
  void drawPotionOffer(bool top);
  void drawShop(bool top);    // merchant (RGDSplus U20)
  void updateShop(const gfx::Input& in);
  void updatePotionOffer(const gfx::Input& in);
  void drawPotionIcon(sts::Potion* p, float x, float y, float size);
  std::string describePotion(sts::Potion* p);
  bool potionsOpen_ = false;
  bool potionAim_ = false;
  int potionSel_ = -1;

  // pieces
  // RGDSplus B02-B06: pages show the current scene's own background (room, or the map
  // when opened from it) under a dark overlay of `dim` (0..1), not a flat fill.
  void drawSceneBg(bool top, float dim);
  std::pair<float, float> mapPos(const sts::MapNode& n) const;
  int mapNodeAt(float tx, float ty);
  void drawTopBar();
  void drawStatusBar(float y);
  void drawCreature(sts::Creature* c, float x, float feetY, bool targeted);
  void drawCard(sts::Card* c, float x, float y, float s, bool dim = false, bool desc = false, bool selected = false);
  void drawCardGrid(const std::vector<sts::Card*>& cards, int sel, float y0, float y1, int scrollRow);
  int gridHit(const std::vector<sts::Card*>& cards, float y0, float y1, int scrollRow, int tx, int ty);
  bool button(float x, float y, float w, float h, const std::string& label, int id, bool enabled = true,
              bool highlight = false);
  void panel(float x, float y, float w, float h, uint32_t fill = 0x1A1A24E0, uint32_t border = 0x8A7A5AFF);
  std::string describe(sts::Card* c);
  std::string describeRelic(sts::Relic* r);
  void drawRelicIcon(sts::Relic* r, float x, float y, float size);
  // A relic on its own on the top screen: big icon, name, rarity, description.
  void drawRelicDetail(sts::Relic* r, float cy);
  std::string cardTitle(sts::Card* c);
  std::vector<sts::Creature*> visibleEnemies();
  float enemyX(int i, int n);
  int hitAt(int tx, int ty);

  std::unique_ptr<sts::Run> run_;
  sts::Screen lastScreen_ = sts::Screen::Title;
  double time_ = 0;
  std::vector<Float> floats_;
  std::string toast_;
  float toastT_ = 0;
  std::vector<Hit> hits_;
  bool deckOpen_ = false;
  bool relicsOpen_ = false;
  bool mapView_ = false;  // map opened from another room (START): look only, red 返回 below
  bool devOpen_ = false;
  int devPage_ = 0;  // 0 actions, 1 relics, 2 cards, 3 encounters
  std::vector<std::unique_ptr<sts::Relic>> devRelics_;  // every registered relic, for the picker
  std::vector<std::unique_ptr<sts::Card>> devCards_;    // every registered pool card
  std::vector<std::string> devEncounters_;

  // selection state
  int sel_ = -1;       // hand index / reward index / grid index
  int target_ = 0;     // index into alive enemies
  int mapSel_ = 0;
  int scroll_ = 0;
  float mapScroll_ = 0;        // rows; see kMapBase in ui.cpp
  bool mapUserScroll_ = false;  // dragged by hand: stop auto-following the current row
  struct MapTouch { bool down = false, dragged = false; int node = -1; float startX = 0, startY = 0, lastY = 0; } mapTouch_;
  sts::Combat* lastCombat_ = nullptr;
  std::map<sts::Creature*, Visual> visuals_;
  std::map<sts::Creature*, std::pair<float, float>> centers_;  // top-screen body centres, refreshed each frame
  Drag drag_;
  std::vector<Flight> flights_;
  std::map<sts::Card*, Pose> poses_;
  std::vector<Ghost> ghosts_;
  float drawQueue_ = 0;     // stagger for cards entering the hand
  float leaveQueue_ = 0;    // stagger for cards leaving it
  bool aiming_ = false;     // controller: choosing a target for the selected card
  float arrowRot_ = 0;
  float clock_ = 0;         // seconds, drives pulsing UI      // NTargetingArrow head rotation carried between frames
  bool autoplay_ = false;
  double autoT_ = 0;
};

}  // namespace ui
