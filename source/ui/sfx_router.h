// U4: sound effects. Maps the core's VisualEvents and a few UI moments to audio::playSfx, following
// the C# SfxCmd / NCreature / AttackCommand calls. The core never calls audio; everything goes
// through here. Missing audio is a silent no-op. STS_SFX_LOG=1 prints each sound played.
#pragma once
#include <string>

namespace sts {
struct Combat;
struct Run;
struct VisualEvent;
}  // namespace sts

namespace sfx {

void play(const std::string& eventOrFile);                 // logged audio::playSfx
void combatEvent(const sts::VisualEvent& e, sts::Combat& c, sts::Run& r);  // App::consumeEvents
void frame(sts::Run& r);                                   // once per update: gold, relics, potions, turns, energy
void click();                                              // ui_click (button / list tap)
void cardDeal();                                           // a card leaves the draw pile for the hand
void cardsDiscarded();                                     // end-of-turn discard sweep
void mapSelect();                                          // a map node was chosen
void potionUsed();
void smith();                                              // a card was upgraded at a rest site

}  // namespace sfx
