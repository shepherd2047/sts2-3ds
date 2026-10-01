// The true ending (package A10): TheArchitect event, TheArchitectEventEncounter and the
// Architect monster (MegaCrit.Sts2.Core.Models.Events/TheArchitect.cs, .Encounters/
// TheArchitectEventEncounter.cs, .Monsters/Architect.cs). RunManager.EnterNextAct enters this
// event after the last act's boss (Run::main); its only option, PROCEED, calls
// RunManager.WinRun (Run::winRun), which ends the run as a victory.
// PORT NOTE (n/a: visual): the C# event uses the combat layout (EventLayoutType.Combat): the encounter's
// Architect stands in a combat room with the player, the speech bubbles play over them, and the
// start/end "attacks" (ArchitectAttackers: the character's attack VFX hitting for the run's score
// split by DivideWildly, the Architect's lightning back) are pure animation. Here the event uses
// the Ancient layout (scene + dialogue, gfx/bg_thearchitect.t3t) and the attack animations, their
// score/damage numbers and the event-Rng calls they make (Shuffle, DivideWildly) are dropped: the
// run ends right after them, so nothing else reads that stream.
#include <algorithm>

#include "cards.h"
#include "powers.h"
#include "progress.h"

namespace sts {

namespace {

#define EVENT_HEADER(Name, Key) \
  static constexpr const char* kId = #Name; \
  Name() { id = #Name; locKey = Key; }
#define MONSTER_HEADER(Name, Key) \
  Name() { id = #Name; locKey = Key; }

// Architect.cs: 9999 HP and a single NOTHING move (HiddenIntent) that repeats forever.
struct Architect : Monster {
  MONSTER_HEADER(Architect, "ARCHITECT")
  int minHp() const override { return 9999; }
  int maxHp() const override { return 9999; }
  void buildMoves() override {
    auto* nothing = machine.add<MoveState>("NOTHING");
    nothing->perform = [](const std::vector<Creature*>&) -> Task<> { co_return; };
    nothing->intents = {};  // HiddenIntent: nothing is shown
    nothing->followUp = nothing;
    machine.start(nothing);
  }
};

// AncientDialogue as TheArchitect defines it (DefineDialogues): the number of lines, its
// VisitIndex, and IsRepeating, which the C# derives from the "r" suffix of the loc keys
// (AncientDialogue.PopulateLines; read from the ancients table: every Architect dialogue repeats
// except NECROBINDER 0 and REGENT 0).
struct ArchitectDialogue {
  int lines;
  int visitIndex;
  bool repeating;
};

const std::vector<ArchitectDialogue>* dialoguesFor(const std::string& charKey) {
  static const std::map<std::string, std::vector<ArchitectDialogue>> kSets = {
      {"IRONCLAD", {{2, 0, true}, {3, 1, true}, {3, 2, true}}},
      {"SILENT", {{1, 0, true}, {1, 1, true}, {1, 2, true}, {1, 3, true}}},
      {"DEFECT", {{3, 0, true}, {3, 1, true}, {3, 2, true}}},
      {"NECROBINDER", {{2, 0, false}, {2, 1, true}, {2, 2, true}, {3, 3, true}}},
      {"REGENT", {{3, 0, false}, {3, 1, true}, {3, 2, true}}},
  };
  auto it = kSets.find(charKey);
  return it == kSets.end() ? nullptr : &it->second;
}

struct TheArchitect : Event {
  EVENT_HEADER(TheArchitect, "THE_ARCHITECT")

  // LoadDialogue: AncientDialogueSet.GetValidDialogues(character, charVisits = the character's
  // total wins, allowAnyCharacterDialogues: false), then Rng.NextItem. The set has no
  // first-visit-ever dialogue and no agnostic ones, so: this character's dialogues whose
  // VisitIndex == charVisits, else the repeating ones with VisitIndex <= charVisits.
  void loadDialogue() {
    dialogue.clear();
    dialogueLine = 0;
    const std::string& key = run->character().key;
    const auto* set = dialoguesFor(key);
    if (!set) return;
    int charVisits = 0;
    auto& chars = progress::state().characters;
    if (auto it = chars.find(run->characterId); it != chars.end()) charVisits = it->second.wins;
    std::vector<int> valid;
    for (int i = 0; i < (int)set->size(); ++i)
      if ((*set)[i].visitIndex == charVisits) valid.push_back(i);
    if (valid.empty())
      for (int i = 0; i < (int)set->size(); ++i)
        if ((*set)[i].repeating && charVisits >= (*set)[i].visitIndex) valid.push_back(i);
    if (valid.empty()) { rng().nextItem(valid); return; }  // NextItem of nothing: no dialogue, no draw
    int pick = rng().nextItem(valid);
    const ArchitectDialogue& d = (*set)[pick];
    // Line keys THE_ARCHITECT.talk.<CHAR>.<dialogue>-<line>[r].<ancient|char>: the speaker is
    // whichever key the loc table has, so both are listed (the UI keeps the one with text).
    std::string base = locKey + ".talk." + key + "." + std::to_string(pick) + "-";
    for (int l = 0; l < d.lines; ++l) {
      std::string k = base + std::to_string(l) + (d.repeating ? "r" : "");
      dialogue.push_back(k + ".ancient");
      dialogue.push_back(k + ".char");
    }
  }

  // GenerateInitialOptions: the dialogue (Continue / Respond per line, UI-side), then PROCEED.
  std::vector<EventOption> initialOptions() override {
    ancient = true;
    loadDialogue();
    EventOption proceed;
    proceed.key = "PROCEED";  // ancients.PROCEED.title
    proceed.action = [this]() -> Task<> { return winRun(); };
    return {proceed};
  }

  // WinRun: (the attack animations, see the note above) RunManager.WinRun.
  Task<> winRun() {
    run->winRun();
    finished = true;
    options.clear();
    co_return;
  }

  // The option loop runs here so the run ends on PROCEED without a second "continue" page
  // (the C# game-over screen replaces the event at once).
  Task<> onStart() override {
    while (!finished && !run->died) {
      run->screen = Screen::Event;
      int pick = co_await run->eventChoice.next();
      if (pick < 0 || pick >= (int)options.size() || options[pick].locked()) continue;
      auto action = options[pick].action;
      co_await action();
    }
  }
};

}  // namespace

void registerArchitect() {
  db::registerEncounter("TheArchitectEventEncounter", RoomType::Monster, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(std::make_unique<Architect>());
    return v;
  });
  db::registerEvent(TheArchitect::kId, [] { return std::unique_ptr<Event>(new TheArchitect()); });
}

}  // namespace sts
