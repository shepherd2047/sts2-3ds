// Character plumbing checks (X0): the Character table, Run::start(seed, character), saves.
// Build: make -f Makefile.sdl build/character_test ; run: ./build/character_test
#include <cstdio>
#include <cstdlib>
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
    CHECK(toks.size() > 5 && toks[0] == "STS2SAVE" && toks[1] == "5" && toks[3] == "Ironclad" && toks[4] == "0");
    // A save from a newer build is refused; an ascension run keeps its level.
    std::string future = v3;
    future.replace(future.find("STS2SAVE 5"), 10, "STS2SAVE 9");
    Run refused;
    CHECK(!refused.load(future));
    Run asc;
    asc.start(7, "Ironclad", 10);
    Run ascBack;
    CHECK(ascBack.load(asc.save()));
    CHECK(ascBack.ascension == 10 && ascBack.hasAscension(kDoubleBoss) && ascBack.deck.size() == 11);  // + AscendersBane
    CHECK(ascBack.potions.size() == 2);                                                             // TightBelt
  }
  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
