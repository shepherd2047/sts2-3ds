// M12 checks: the daily's character / ascension / seed / modifiers from the date (golden values
// produced by the game's own sts2.dll Rng, StringHelper.GetDeterministicHashCode and
// SeedHelper.CanonicalizeSeed driving NDailyRunScreen.SetupLobbyParams + Pick2Good1Bad), date
// parsing, the local best scores (progress.sav v2), run.sav v9 and history v4 with the date.
// Never touches a real save (no profiles::init, no progress::save).
// Build: make -f Makefile.sdl build/daily_test ; run: ./build/daily_test
#include <cstdio>
#include <cstdlib>

#include "../source/core/daily.h"
#include "../source/core/game.h"
#include "../source/core/history.h"
#include "../source/core/modifiers.h"
#include "../source/core/progress.h"

using namespace sts;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                               \
  do {                                                                            \
    ++checks;                                                                     \
    if (!(cond)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #cond); } \
  } while (0)

static std::string join(const std::vector<std::string>& v) {
  std::string s;
  for (auto& x : v) s += (s.empty() ? "" : ",") + x;
  return s;
}

int main() {
  // ---- dates ----
  daily::Date d;
  CHECK(daily::parseDate("2026-09-29", d) && d.year == 2026 && d.month == 9 && d.day == 29);
  CHECK(daily::key(d) == "2026-09-29");
  CHECK(daily::parseDate("2024-02-29", d));
  CHECK(!daily::parseDate("2025-02-29", d));
  CHECK(!daily::parseDate("2026-13-01", d));
  CHECK(!daily::parseDate("2026-09-29x", d));
  CHECK(!daily::parseDate("", d));
  setenv("STS_DAILY_DATE", "2025-01-01", 1);
  CHECK(daily::key(daily::today()) == "2025-01-01");
  unsetenv("STS_DAILY_DATE");

  // ---- the C# algorithm (golden: sts2.dll) ----
  struct Golden { const char* date; const char* character; int asc; const char* seed; const char* mods; };
  const Golden golden[] = {
      {"2026-09-29", "Regent", 4, "29_09_2026_1P", "Draft,Flight,Terminal"},
      {"2026-09-30", "Silent", 4, "30_09_2026_1P", "CharacterCards:Necrobinder,SealedDeck,BigGameHunter"},
      {"2025-01-01", "Regent", 5, "01_01_2025_1P", "Specialized,Vintage,CursedRun"},
      {"2024-02-29", "Silent", 10, "29_02_2024_1P", "CharacterCards:Necrobinder,SealedDeck,CursedRun"},
      {"2026-12-31", "Defect", 5, "31_12_2026_1P", "Hoarder,CharacterCards:Regent,CursedRun"},
  };
  for (auto& g : golden) {
    CHECK(daily::parseDate(g.date, d));
    daily::Params p = daily::forDate(d);
    if (p.character != g.character || p.ascension != g.asc || p.seedText != g.seed || join(p.modifiers) != g.mods)
      printf("  %s -> %s %d %s %s\n", g.date, p.character.c_str(), p.ascension, p.seedText.c_str(), join(p.modifiers).c_str());
    CHECK(p.character == g.character);
    CHECK(p.ascension == g.asc);
    CHECK(p.seedText == g.seed);
    CHECK(join(p.modifiers) == g.mods);
    // Same date -> same run.
    daily::Params q = daily::forDate(d);
    CHECK(q.character == p.character && q.ascension == p.ascension && q.seedText == p.seedText && q.modifiers == p.modifiers);
  }
  // Different dates differ; every day has two good modifiers, one bad, all valid keys, and
  // CharacterCards never names the daily's own character.
  int differ = 0;
  daily::Params prev;
  for (int i = 0; i < 60; ++i) {
    daily::Date day{2026, 1 + i / 28, 1 + i % 28};
    daily::Params p = daily::forDate(day);
    if (i > 0 && (p.seedText == prev.seedText)) CHECK(false);
    if (i > 0 && (p.character != prev.character || p.modifiers != prev.modifiers || p.ascension != prev.ascension)) ++differ;
    CHECK(p.modifiers.size() == 3);
    CHECK(modifiers::isGood(p.modifiers[0]) && modifiers::isGood(p.modifiers[1]) && !modifiers::isGood(p.modifiers[2]));
    CHECK(p.modifiers[0] != p.modifiers[1]);
    for (auto& k : p.modifiers) {
      CHECK(modifiers::create(k) != nullptr);
      CHECK(k != "CharacterCards:" + p.character);
    }
    CHECK(!modifiers::mutuallyExclusive(p.modifiers[0].substr(0, p.modifiers[0].find(':')),
                                        p.modifiers[1].substr(0, p.modifiers[1].find(':'))));
    CHECK(p.ascension >= 0 && p.ascension <= 10);
    prev = p;
  }
  CHECK(differ >= 55);

  // ---- local bests (progress.sav v2) ----
  progress::reset();
  CHECK(daily::best(progress::state(), "2026-09-29") == -1 && daily::bestOverall(progress::state()) == -1);
  CHECK(daily::recordScore("2026-09-29", 120));
  CHECK(!daily::recordScore("2026-09-29", 80));
  CHECK(daily::recordScore("2026-09-29", 300));
  CHECK(daily::recordScore("2026-09-30", 150));
  CHECK(daily::best(progress::state(), "2026-09-29") == 300);
  CHECK(daily::bestOverall(progress::state()) == 300);
  std::string saved = progress::state().save();
  CHECK(saved.find("STS2PROGRESS 2 ") == 0);
  Progress back;
  CHECK(back.load(saved) && back.dailyBest == progress::state().dailyBest);
  CHECK(back.save() == saved);
  // A version 1 file (no DAILY section) still loads, with no bests.
  Progress v1;
  CHECK(v1.load("STS2PROGRESS 1 CHARACTERS 0 CARDS 0 RELICS 0 POTIONS 0 MONSTERS 0 COUNTERS 0 0 END"));
  CHECK(v1.dailyBest.empty());
  progress::reset();

  // ---- run.sav v9 / history v4 keep the date ----
  {
    CHECK(daily::parseDate("2026-09-30", d));
    daily::Params p = daily::forDate(d);
    Run r;
    r.setModifiers(p.modifiers);
    r.seedText = p.seedText;
    r.dailyDate = daily::key(d);
    r.start(modifiers::seedFromString(p.seedText), p.character, p.ascension);
    CHECK(r.characterId == p.character && r.ascension == p.ascension && r.modifierKeys() == p.modifiers);
    std::string s = r.save();
    Run l;
    CHECK(l.load(s));
    CHECK(l.dailyDate == "2026-09-30" && l.seedText == p.seedText && l.modifierKeys() == p.modifiers);
    CHECK(l.save() == s);
    history::RunRecord rec = history::fromRun(r, false, false);
    CHECK(rec.dailyDate == "2026-09-30" && !rec.custom);
    history::RunRecord rec2;
    CHECK(rec2.load(rec.save()) && rec2 == rec);
    Run plain;
    plain.start(7, "Ironclad", 0);
    Run pl;
    CHECK(pl.load(plain.save()) && pl.dailyDate.empty());
  }

  printf("daily_test: %d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
