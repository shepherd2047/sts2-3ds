// Preview / debug switches that set up a combat (STS_POWERS, STS_ORBS, STS_STARS, STS_ENERGY, STS_HAND,
// STS_PILE_DRAW, STS_PILE_DISCARD) and the runtime debug commands of STS_SCRIPT "frame:C<cmd>" items,
// modelled on the original's dev console (DevConsole/ConsoleCommands). Nothing here runs unless one of
// those variables is set or a command is issued, so the sim's results never depend on it.
#pragma once
#include <string>
#include <vector>

#include "game.h"

namespace sts {
namespace dbg {

struct PowerSpec {
  std::string id;  // registered power id ("StrengthPower")
  int amount = 1;
  int idx = 0;     // into creatures(): 0 = player
};
struct CardSpec {
  std::string id;  // registered card id ("Bash")
  bool upgraded = false;
};

// Lookups accept the class id ("StrengthPower", "Bash", "FirePotion"), the class id without its suffix
// ("Strength", "Lightning") and the console's SCREAMING_SNAKE id ("STRENGTH_POWER", "BODY_SLAM").
// Empty when nothing is registered under any of them.
std::string powerId(const std::string& name);
std::string cardId(const std::string& name);
std::string potionId(const std::string& name);
std::string orbId(const std::string& name);

std::vector<std::string> splitList(const std::string& s, char sep = ',');  // trims, drops empty items
std::vector<PowerSpec> parsePowers(const std::string& s);  // "Strength:3:0,Vulnerable:2:1" (amount 1, idx 0 by default)
std::vector<CardSpec> parseCards(const std::string& s);    // "Bash+,Strike" ('+' = upgraded)
std::vector<std::string> parseOrbs(const std::string& s);  // "Lightning,Frost" -> orb ids
std::string decodeCommand(std::string item);               // STS_SCRIPT item text: '_' -> ' '

// CombatState.Creatures (the console's target index): the player, Osty (once summoned), the other
// pets, then the enemies in order; creatures that left the combat are skipped.
std::vector<Creature*> creatures(Combat& c);

// True when any combat-start switch is set (the combat only awaits applyCombatStart then).
bool anyCombatStartSwitch();
// After the BeforeCombatStart hooks (Osty is summoned by then): STS_PILE_DRAW / STS_PILE_DISCARD,
// STS_POWERS, STS_ORBS, STS_STARS.
Task<> applyCombatStart(Combat& c);
// Turn 1, before the opening hand draw: STS_HAND's cards go on top of the draw pile (a copy already in
// the draw pile is taken first, otherwise one is created) and the hand draw becomes exactly that many.
// Returns the forced count, 0 when STS_HAND is unset.
int forceOpeningHand(Combat& c);
// Turn 1, after the energy reset: STS_ENERGY=N sets the energy. No-op when unset.
void applyStartEnergy(Combat& c);

// One console command, as in the original's dev console (space separated, after decodeCommand):
//   power <id> <amount> [idx]   card <id>[+] [hand|draw|discard]   kill [enemyIdx|all]
//   damage <n> [idx]            block <n> [idx]                    stars <n>   energy <n>
//   potion <id>                 orb <kind>
// idx is into creatures() (0 = player); kill's index is into the enemies only and damage without an
// index hits every enemy, both as the C#. False (with a log line) for an unknown or malformed command.
Task<bool> runCommand(Run& r, std::string line);
// Spawns runCommand on the scheduler (the UI's per-frame entry point).
void startCommand(Run& r, const std::string& line);

}  // namespace dbg
}  // namespace sts
