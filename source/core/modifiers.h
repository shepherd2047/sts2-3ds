// M11 run modifiers (MegaCrit.Sts2.Core.Models.Modifiers) and seeded runs (SeedHelper).
//
// A modifier is a Model on the run (RunState.Modifiers): Run::listeners() and Combat::listeners()
// list them after the relics, so the combat / rest hooks (Murderous, Terminal, NightTerrors) are
// ordinary Model overrides. The effects the port has no hook for are checked by name where the C#
// hook runs (Run::hasModifier): card rewards (BigGameHunter, CharacterCards), rewards (Midas,
// Vintage), rest options (Midas), deck additions (Hoarder), the merchant (Hoarder, CharacterCards),
// "?" rooms (DeadlyEvents), the map (BigGameHunter, Flight), acts (CursedRun) and Neow (the
// GenerateNeowOption modifiers: Draft, SealedDeck, Specialized, Insanity, AllStar).
//
// Keys: the C# class name ("Draft"); CharacterCards carries its character ("CharacterCards:Silent").
// The custom run screen lists allKeys() (NCustomRunModifiersList.GetAllModifiers order).
// Debug: STS_MODIFIERS=Draft,Midas (preview, new runs) / SIM_MODIFIERS=... (build/sim).
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "game.h"

namespace sts {

struct Modifier : Model {
  std::string id;         // C# class name
  std::string character;  // CharacterCards.CharacterModel (a Character id), else ""
  Run* run = nullptr;
  std::string key() const { return character.empty() ? id : id + ":" + character; }
  virtual bool clearsDeck() const { return false; }     // ClearsPlayerDeck
  virtual bool hasNeowOption() const { return false; }  // GenerateNeowOption != null
  virtual Task<> neowOption() { return {}; }            // the option's action
};

namespace modifiers {

// GoodModifiers then BadModifiers (ModelDb), CharacterCards once per ModelDb.AllCharacters entry.
const std::vector<std::string>& allKeys();
bool isGood(const std::string& key);
std::shared_ptr<Modifier> create(const std::string& key);  // null for an unknown key
// Loc keys of the title / description (modifiers.* or characters.*.cardsModifier*).
std::string titleKey(const std::string& key);
std::string descriptionKey(const std::string& key);
// ModelDb.MutuallyExclusiveModifiers (SealedDeck, Draft, Insanity): ticking one unticks the others.
bool mutuallyExclusive(const std::string& a, const std::string& b);
// Neow.GenerateInitialOptions with modifiers: the GenerateNeowOption ones, one page each, in order.
std::vector<EventOption> neowOptions(Event& neow);
// RunManager.InitializeNewRun -> ModifierModel.OnRunCreated for a new run (Run::start, after the
// relic bags; ClearsPlayerDeck is applied by Run::start itself before the ascension effects).
void afterRunCreated(Run& run);
// Hook.AfterActEntered (Run::enterAct): CursedRun's curse.
void afterActEntered(Run& run);
// "A,B,C" -> {"A", "B", "C"} (empty entries dropped).
std::vector<std::string> parseList(const std::string& csv);

// ---- SeedHelper / RunRngSet ----
constexpr int kSeedLength = 12;                  // SeedHelper.seedDefaultLength
extern const char kSeedChars[];                  // SeedHelper._characters (no O, no I)
std::string canonicalizeSeed(std::string seed);  // upper case, O -> 0, I -> 1, trimmed
// RunRngSet(string): XxHash64 of the text; "old..." seeds use GetDeterministicHashCodeOld.
uint64_t seedFromString(const std::string& seed);
// SeedHelper.GetRandomSeed from a caller's xorshift state (Rng.Chaotic; no bad-word filter).
std::string randomSeed(uint64_t& state);

}  // namespace modifiers
}  // namespace sts
