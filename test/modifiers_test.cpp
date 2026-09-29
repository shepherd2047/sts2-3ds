// M11 checks: seeds (SeedHelper / RunRngSet), the modifier list, the Neow options of the
// deck modifiers, the run-creation effects, BigGameHunter's map, Flight, Midas, run.sav version 8
// and the run history record version 3. Never touches a real save (no profiles::init).
// Build: make -f Makefile.sdl build/modifiers_test ; run: ./build/modifiers_test
#include <cstdio>
#include <cstdlib>

#include "../source/core/game.h"
#include "../source/core/history.h"
#include "../source/core/modifiers.h"

using namespace sts;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                               \
  do {                                                                            \
    ++checks;                                                                     \
    if (!(cond)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #cond); } \
  } while (0)

// Runs Neow (and nothing else): options 0, reward card 0, deck choices take the first ones.
// Returns when the run waits at the map.
static void throughNeow(Run& r) {
  Scheduler::get().spawn(r.main());
  for (int i = 0; i < 20000; ++i) {
    Scheduler::get().update(0.05);
    if (r.deckChoice.active && r.deckChoice.result.waiting()) {
      std::vector<Card*> picked(r.deckChoice.options.begin(), r.deckChoice.options.begin() + r.deckChoice.count);
      r.deckChoice.result.fire(picked);
    } else if (r.screen == Screen::Reward && r.rewardChoice.waiting()) {
      r.rewardChoice.fire(i % 3 == 0 ? -1 : 0);  // a skip is refused (CanSkip = false)
    } else if (r.screen == Screen::Event && r.eventChoice.waiting()) {
      r.eventChoice.fire(0);
    } else if (r.screen == Screen::Map && r.mapChoice.waiting()) {
      return;
    }
  }
}

static int count(const Run& r, const std::string& id) {
  int n = 0;
  for (auto& c : r.deck) n += c->id == id;
  return n;
}

int main() {
  db::init();
  {  // SeedHelper
    CHECK(modifiers::canonicalizeSeed("  abcoi9 ") == "ABC019");
    CHECK(modifiers::seedFromString("ABC019") == deterministicHash("ABC019"));
    uint64_t x = 42;
    std::string s = modifiers::randomSeed(x), t = modifiers::randomSeed(x);
    CHECK(s.size() == 12 && s != t);
    for (char c : s) CHECK(std::string(modifiers::kSeedChars).find(c) != std::string::npos);
    CHECK(modifiers::seedFromString("old12") != modifiers::seedFromString("old13"));
    CHECK(modifiers::seedFromString("old12") <= 0xFFFFFFFFull);
  }
  {  // the list: 9 good (CharacterCards x5) + 7 bad; every key makes a modifier
    const auto& k = modifiers::allKeys();
    CHECK(k.size() == 8 + 5 + 7);
    for (auto& key : k) {
      auto m = modifiers::create(key);
      CHECK(m && m->key() == key);
    }
    CHECK(!modifiers::create("Nope") && !modifiers::create("CharacterCards") && !modifiers::create("CharacterCards:Bob"));
    CHECK(modifiers::mutuallyExclusive("Draft", "Insanity") && !modifiers::mutuallyExclusive("Draft", "Draft") &&
          !modifiers::mutuallyExclusive("Draft", "Midas"));
    CHECK(modifiers::titleKey("BigGameHunter") == "modifiers.BIG_GAME_HUNTER.title");
    CHECK(modifiers::titleKey("CharacterCards:Silent") == "characters.SILENT.cardsModifierTitle");
    CHECK(modifiers::isGood("CharacterCards:Defect") && !modifiers::isGood("Terminal"));
  }
  {  // deck modifiers at Neow
    struct Case { const char* mods; int deck; };
    const Case cases[] = {{"Draft", 10}, {"SealedDeck", 10}, {"Insanity", 30}, {"Specialized", 15}, {"AllStar", 15},
                          {"Hoarder,Specialized", 10 + 15}, {"Midas", 10}, {"Draft,AllStar", 15}};
    for (auto& c : cases) {
      Run r;
      r.setModifiers(modifiers::parseList(c.mods));
      r.start(77, "Ironclad");
      if (r.modifiersClearDeck()) CHECK(r.deck.empty());
      throughNeow(r);
      if ((int)r.deck.size() != c.deck) printf("  %s: deck %zu\n", c.mods, r.deck.size());
      CHECK((int)r.deck.size() == c.deck);
      if (r.modifiersClearDeck())
        for (auto& b : {&r.relicBag, &r.sharedRelicBag})
          for (auto& [k, v] : *b) CHECK(std::find(v.begin(), v.end(), "PandorasBox") == v.end());
      Scheduler::get().clear();
    }
    Run asc;  // ClearsPlayerDeck runs before the ascension effects: Ascender's Bane stays
    asc.setModifiers({"Insanity"});
    asc.start(5, "Silent", 10);
    CHECK(asc.deck.size() == 1 && asc.deck[0]->id == "AscendersBane");
  }
  {  // Specialized: five copies of one card
    Run r;
    r.setModifiers({"Specialized"});
    r.start(3, "Ironclad");
    throughNeow(r);
    const std::string& last = r.deck.back()->id;
    CHECK(count(r, last) == 5);
    Scheduler::get().clear();
  }
  {  // DeadlyEvents, CursedRun, Hoarder, Midas, Flight, CharacterCards
    Run r;
    r.setModifiers({"DeadlyEvents", "CursedRun", "Hoarder", "Midas", "Flight", "CharacterCards:Silent"});
    r.start(9, "Ironclad");
    CHECK(r.unknownEliteOdds == 0.1f);
    for (auto& b : {&r.relicBag, &r.sharedRelicBag})
      for (auto& [k, v] : *b) CHECK(std::find(v.begin(), v.end(), "JuzuBracelet") == v.end());
    CHECK(r.deck.size() == 13);  // the starter deck + CursedRun's curse, which Hoarder copies twice
    r.addCardToDeck(db::card("Anger"));
    CHECK(count(r, "Anger") == 3);
    CHECK(r.modifierCardPools() == std::vector<std::string>{"Silent"});
    int first = -1;
    for (int i = 0; i < (int)r.nodes.size(); ++i) if (r.nodes[i].row == 0) { first = i; break; }
    r.currentNode = first;
    int row1 = 0;
    for (auto& n : r.nodes) row1 += n.row == 1;
    CHECK((int)r.pathNodes().size() == row1);
    // Silent cards in card rewards (over many rolls).
    bool silent = false;
    for (int i = 0; i < 30 && !silent; ++i)
      for (auto& c : r.cardReward(RoomType::Monster, 3))
        for (auto& id : db::character("Silent").cardPool) silent = silent || c->id == id;
    CHECK(silent);
  }
  {  // CursedRun without Hoarder: one curse per act
    Run r;
    r.setModifiers({"CursedRun"});
    r.start(4, "Ironclad");
    CHECK(r.deck.size() == 11);
    r.enterAct(1);
    CHECK(r.deck.size() == 12);
  }
  {  // BigGameHunter: 2.5x the elites on the map, elite card rewards are all rare
    Run plain, big;
    plain.start(21, "Ironclad");
    big.setModifiers({"BigGameHunter"});
    big.start(21, "Ironclad");
    auto elites = [](const Run& r) { int n = 0; for (auto& x : r.nodes) n += x.type == RoomType::Elite; return n; };
    printf("  BigGameHunter: elites %d -> %d\n", elites(plain), elites(big));
    CHECK(elites(big) > elites(plain));
    for (int i = 0; i < 5; ++i)
      for (auto& c : big.cardReward(RoomType::Elite, 3)) CHECK(c->rarity == Rarity::Rare);
  }
  {  // run.sav version 8 keeps the modifiers, the mode, the seed text and the "?" elite odds
    Run r;
    r.setModifiers({"Vintage", "CharacterCards:Defect", "DeadlyEvents"});
    r.customRun = true;
    r.seedText = "ABC123";
    r.start(modifiers::seedFromString("ABC123"), "Regent");
    std::string a = r.save();
    Run b;
    CHECK(b.load(a));
    CHECK(b.save() == a);
    CHECK(b.modifierKeys() == r.modifierKeys() && b.customRun && b.seedText == "ABC123" && b.unknownEliteOdds == 0.1f);
    CHECK(b.hasModifier("Vintage") && b.modifiers[0]->run == &b);
  }
  {  // run history record version 3
    Run r;
    r.setModifiers({"Terminal", "Flight"});
    r.customRun = true;
    r.seedText = "SEEDSEED";
    r.start(1, "Ironclad");
    history::RunRecord rec = history::fromRun(r, false, true), back;
    CHECK(back.load(rec.save()));
    CHECK(back == rec && back.custom && back.seedText == "SEEDSEED" && back.modifiers.size() == 2);
  }

  printf("modifiers_test: %d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
