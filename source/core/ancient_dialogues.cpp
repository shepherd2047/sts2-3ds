// Ancient dialogue selection (X6): AncientDialogueSet.GetValidDialogues + the pick in
// NEventRoom.SetupLayout, for Neow and the act 2/3 Ancients. The dialogue tables
// (AncientEventModel.DefineDialogues with the loc keys PopulateLines resolves) are generated into
// ancient_dialogues.inc by tools/gen_ancient_dialogues.py. TheArchitect keeps its own selection
// (content_architect.cpp).
#include <algorithm>
#include <cstdlib>
#include <map>

#include "game.h"
#include "progress.h"

namespace sts {

namespace {

struct AncientDialogueDef {
  int visitIndex = -1;  // AncientDialogue.VisitIndex, -1 = null (any visit)
  bool repeating = false;
  std::vector<std::string> lines;  // full keys in the ancients table ("OROBAS.talk.SILENT.0-1.char")
};

struct AncientDialogueSet {
  std::string ancient;  // event id ("Orobas")
  bool hasFirstVisitEver = false;
  AncientDialogueDef firstVisitEver;
  std::map<std::string, std::vector<AncientDialogueDef>> characters;  // key: Character::key
  std::vector<AncientDialogueDef> agnostic;
  std::vector<std::string> anyBlacklist;  // AnyCharacterDialogueBlacklist (character ids)
};

#include "ancient_dialogues.inc"

const std::vector<AncientDialogueSet>& sets() {
  static const std::vector<AncientDialogueSet> v = makeAncientDialogues();
  return v;
}

// AddRepeatingDialogues
void addRepeating(const std::vector<AncientDialogueDef>& from, std::vector<const AncientDialogueDef*>& to, int charVisits) {
  for (auto& d : from)
    if (d.repeating && (d.visitIndex < 0 || charVisits >= d.visitIndex)) to.push_back(&d);
}

}  // namespace

namespace db {

std::vector<std::string> ancientDialogue(const std::string& ancientId, const std::string& characterId, int charVisits,
                                         int totalVisits, Rng& rng) {
  const AncientDialogueSet* set = nullptr;
  for (auto& s : sets()) if (s.ancient == ancientId) set = &s;
  if (!set) return {};
  const Character& ch = character(characterId);
  bool allowAny = std::find(set->anyBlacklist.begin(), set->anyBlacklist.end(), ch.id) == set->anyBlacklist.end();
  // GetValidDialogues(characterId, charVisits, totalVisits, allowAnyCharacterDialogues)
  std::vector<const AncientDialogueDef*> valid;
  if (totalVisits == 0 && set->hasFirstVisitEver) {
    valid.push_back(&set->firstVisitEver);
  } else {
    auto it = set->characters.find(ch.key);
    if (it != set->characters.end())
      for (auto& d : it->second) if (d.visitIndex == charVisits) valid.push_back(&d);
    if (valid.empty() && allowAny)
      for (auto& d : set->agnostic) if (d.visitIndex == charVisits) valid.push_back(&d);
    if (valid.empty()) {
      if (it != set->characters.end()) addRepeating(it->second, valid, charVisits);
      if (allowAny) addRepeating(set->agnostic, valid, charVisits);
    }
  }
  // Rng.Chaotic.NextItem(validDialogues): nothing valid means no dialogue.
  const AncientDialogueDef* pick = rng.nextItem(valid);
  return pick ? pick->lines : std::vector<std::string>{};
}

std::vector<std::string> ancientDialogueFor(const Run& run, const std::string& ancientId) {
  Rng chaotic(run.seed, "AncientDialogue" + ancientId);
  int charVisits = progress::ancientVisits(ancientId, run.characterId);
  int totalVisits = progress::ancientTotalVisits(ancientId);
  // Debug: STS_ANCIENT_VISITS=N pretends this character has met every Ancient N times (total N + 1).
  if (const char* v = getenv("STS_ANCIENT_VISITS")) {
    charVisits = std::max(0, atoi(v));
    totalVisits = charVisits + 1;
  }
  return ancientDialogue(ancientId, run.characterId, charVisits, totalVisits, chaotic);
}

}  // namespace db

}  // namespace sts
