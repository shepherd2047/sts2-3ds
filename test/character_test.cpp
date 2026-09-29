// Character plumbing checks (X0) and cross-character content (X6): the Character table, Run::start(seed, character), saves.
// Build: make -f Makefile.sdl build/character_test ; run: ./build/character_test
#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>

#include "../source/core/game.h"

using namespace sts;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    ++checks;                                                           \
    if (!(cond)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #cond); } \
  } while (0)

int main() {
  db::init();
  {  // the table (Models.Characters)
    CHECK(db::characterIds().size() == 5);
    const Character& i = db::character("Ironclad");
    CHECK(i.key == "IRONCLAD" && i.startingHp == 80 && i.startingGold == 99 && i.maxEnergy == 3);
    CHECK(i.starterDeck.size() == 10 && i.startingRelics == std::vector<std::string>{"BurningBlood"});
    CHECK(i.cardPool.size() == 90 && i.cardPool.front() == "Aggression" && i.cardPool.back() == "Whirlwind");
    CHECK(i.multiplayerOnly.size() == 5);
    CHECK(db::character("Silent").startingHp == 70 && db::character("Silent").starterDeck.size() == 12);
    CHECK(db::character("Defect").startingHp == 75 && db::character("Defect").orbSlots == 3);
    CHECK(db::character("Regent").startingHp == 75 && db::character("Regent").alwaysShowStars);
    CHECK(db::character("Necrobinder").startingHp == 66 && db::character("Necrobinder").key == "NECROBINDER");
    CHECK(db::character("Nobody").id == "Ironclad");  // unknown ids fall back
    for (auto& id : db::characterIds()) {
      const Character& c = db::character(id);
      CHECK(!c.cardPool.empty() && c.relicPool.size() == 8 && c.potions.size() == 3 && c.startingRelics.size() == 1);
    }
    CHECK(db::characterPlayable("Ironclad"));
    CHECK(!db::characterPlayable("Nobody"));
  }
  {  // pools
    auto attacks = db::characterCards("Ironclad", [](const Card& c) { return c.type == CardType::Attack; });
    CHECK(!attacks.empty());
    auto all = db::characterCards("Ironclad", [](const Card&) { return true; });
    for (auto& id : {"Blaze", "DemonicShield", "Midnight", "Outrage", "Tank"})
      CHECK(std::find(all.begin(), all.end(), id) == all.end());  // multiplayer only
    CHECK(db::potionPool("Ironclad").front() == "BloodPotion" && db::potionPool("Silent").front() == "PoisonPotion");
    CHECK(db::potionPool("Silent").size() == db::potionPool("Ironclad").size());
  }
  {  // Run::start reads the character
    Run r;
    r.start(5);
    CHECK(r.characterId == "Ironclad" && r.player->name == "IRONCLAD" && r.player->maxHp == 80 && r.deck.size() == 10);
    CHECK(r.relics.size() == 1 && r.relics[0]->id == "BurningBlood" && r.relics[0]->run == &r);
    Run s;
    s.start(5, "Silent");
    CHECK(s.characterId == "Silent" && s.player->name == "SILENT" && s.player->hp == 70 && s.player->maxHp == 70);
    CHECK(s.character().key == "SILENT");
    // Silent's cards are not ported yet: the run starts with what exists and does not crash.
    CHECK(s.deck.size() <= 12);
    Run u;
    u.start(5, "Nobody");
    CHECK(u.characterId == "Ironclad");
  }
  {  // saves carry the character; version 2 saves load as the Ironclad
    Run s;
    s.start(7, "Regent");
    std::string text = s.save();
    Run back;
    CHECK(back.load(text));
    CHECK(back.characterId == "Regent" && back.player->maxHp == 75 && back.save() == text);
    Run i;
    i.start(7);
    std::string v3 = i.save();
    std::istringstream in(v3);
    std::vector<std::string> toks;
    for (std::string t; in >> t;) toks.push_back(t);
    CHECK(toks.size() > 5 && toks[0] == "STS2SAVE" && toks[1] == "7" && toks[3] == "Ironclad" && toks[4] == "0");
    // A save from a newer build is refused; an ascension run keeps its level.
    std::string future = v3;
    future.replace(future.find("STS2SAVE 7"), 10, "STS2SAVE 9");
    Run refused;
    CHECK(!refused.load(future));
    Run asc;
    asc.start(7, "Ironclad", 10);
    Run ascBack;
    CHECK(ascBack.load(asc.save()));
    CHECK(ascBack.ascension == 10 && ascBack.hasAscension(kDoubleBoss) && ascBack.deck.size() == 11);  // + AscendersBane
    CHECK(ascBack.potions.size() == 2);                                                             // TightBelt
  }
  {  // X6: the character select's Random (StartRunLobby.BeginRunLocally)
    CHECK(db::allCharacters() == (std::vector<std::string>{"Ironclad", "Silent", "Regent", "Necrobinder", "Defect"}));
    std::set<std::string> seen;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
      Rng rng(seed, "act_selection");
      auto acts = db::randomActList(rng);
      std::string want = rng.nextItem(db::allCharacters());
      Run r;
      r.start(seed, Run::kRandomCharacter);
      CHECK(r.characterId == want && r.actIds == acts);
      seen.insert(r.characterId);
    }
    CHECK(seen.size() == 5);
  }
  auto startEvent = [](Run& r, const char* id) {
    auto e = db::event(id);
    e->run = &r;
    e->rngPtr = std::make_unique<Rng>(r.seed, e->id);
    e->calculateVars();
    e->options = e->initialOptions();
    return e;
  };
  {  // X6: ColorfulPhilosophers offers three of the other four colours, in CardPoolColorOrder
    const std::vector<std::string> order = {"NECROBINDER", "IRONCLAD", "REGENT", "SILENT", "DEFECT"};
    for (auto& ch : db::characterIds())
      for (uint64_t seed = 1; seed <= 5; ++seed) {
        Run r;
        r.start(seed, ch);
        auto e = startEvent(r, "ColorfulPhilosophers");
        CHECK(e->options.size() == 3);
        int last = -1;
        for (auto& o : e->options) {
          std::string key = o.key.substr(o.key.rfind('.') + 1);
          int at = (int)(std::find(order.begin(), order.end(), key) - order.begin());
          CHECK(o.key.rfind("COLORFUL_PHILOSOPHERS.pages.INITIAL.options.", 0) == 0 && at < 5 && at > last);
          CHECK(key != r.character().key && !o.locked());
          last = at;
        }
      }
  }
  {  // X6: Orobas's third pool upgrades every character's starter (TouchOfOrobas / ArchaicTooth)
    for (auto& ch : db::characterIds()) {
      Run r;
      r.start(3, ch);
      auto e = startEvent(r, "Orobas");
      CHECK(e->options.size() == 3);
      CHECK(!e->options.empty() && e->options.back().relic &&
            (e->options.back().relic->id == "TouchOfOrobas" || e->options.back().relic->id == "ArchaicTooth"));
      // The dialogue is this character's (or the first-visit-ever one on a fresh profile).
      CHECK(!e->dialogue.empty());
    }
  }
  {  // X6: AncientDialogueSet.GetValidDialogues
    Rng rng(1);
    auto first = db::ancientDialogue("Orobas", "Silent", 0, 0, rng);
    CHECK(first == std::vector<std::string>{"OROBAS.talk.firstVisitEver.0-0.ancient"});
    auto silent0 = db::ancientDialogue("Orobas", "Silent", 0, 3, rng);
    CHECK(silent0 == (std::vector<std::string>{"OROBAS.talk.SILENT.0-0.ancient", "OROBAS.talk.SILENT.0-1.char"}));
    CHECK(db::ancientDialogue("Neow", "Regent", 4, 9, rng).size() == 3);  // VisitIndex 4
    std::set<std::string> pool;
    for (int i = 0; i < 60; ++i) pool.insert(db::ancientDialogue("Orobas", "Silent", 2, 5, rng).front());
    CHECK(pool == (std::set<std::string>{"OROBAS.talk.SILENT.1-0r.ancient", "OROBAS.talk.ANY.0-0r.ancient",
                                         "OROBAS.talk.ANY.1-0r.ancient"}));
    pool.clear();  // Tezcatara's agnostic lines are blacklisted for the Defect
    for (int i = 0; i < 30; ++i) pool.insert(db::ancientDialogue("Tezcatara", "Defect", 3, 5, rng).front());
    CHECK(pool == std::set<std::string>{"TEZCATARA.talk.DEFECT.1-0r.ancient"});
    CHECK(db::ancientDialogue("NoSuchAncient", "Silent", 0, 0, rng).empty());
  }
  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
