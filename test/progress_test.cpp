// Profile progress checks (M1): round trip, versioning, atomic write fallback, stats updated
// after a simulated win/loss, and the "seen" hooks (starter deck/relics, card reward, potion
// procured, relic obtained, monster fought).
// Build: make -f Makefile.sdl build/progress_test ; run: ./build/progress_test
//
// This never touches a real profile file: every progress::save/load call below passes an
// explicit path under build/, and the in-memory progress::state() is reset between sections.
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "../source/core/game.h"
#include "../source/core/progress.h"

using namespace sts;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    ++checks;                                                           \
    if (!(cond)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #cond); } \
  } while (0)

static bool pump(const std::function<bool()>& done, int maxFrames = 4000) {
  for (int i = 0; i < maxFrames && !done(); ++i) Scheduler::get().update(0.05);
  return done();
}

static void makeDir(const std::string& d) {
#ifdef _WIN32
  _mkdir(d.c_str());
#else
  mkdir(d.c_str(), 0777);
#endif
}

static void removeDir(const std::string& d) {
#ifdef _WIN32
  _rmdir(d.c_str());
#else
  rmdir(d.c_str());
#endif
}

int main() {
  db::init();
  makeDir("build");
  makeDir("build/progress_fixtures");

  {  // round trip: every field survives save() -> load()
    progress::reset();
    Progress& p = progress::state();
    p.character("Ironclad").wins = 3;
    p.character("Ironclad").losses = 1;
    p.character("Ironclad").currentStreak = 2;
    p.character("Ironclad").bestStreak = 5;
    p.character("Ironclad").maxAscension = 7;
    p.character("Silent").maxAscension = 1;
    p.seenCards.insert("StrikeIronclad");
    p.seenCards.insert("Neutralize");
    p.seenRelics.insert("BurningBlood");
    p.seenPotions.insert("FirePotion");
    p.seenMonsters.insert("Nibbit");
    p.counters["runsWon"] = 3;
    p.counters["runsAbandoned"] = 1;

    std::string text = p.save();
    Progress back;
    CHECK(back.load(text));
    CHECK(back.characters.at("Ironclad").wins == 3);
    CHECK(back.characters.at("Ironclad").losses == 1);
    CHECK(back.characters.at("Ironclad").currentStreak == 2);
    CHECK(back.characters.at("Ironclad").bestStreak == 5);
    CHECK(back.characters.at("Ironclad").maxAscension == 7);
    CHECK(back.characters.at("Silent").maxAscension == 1);
    CHECK(back.seenCards == p.seenCards);
    CHECK(back.seenRelics == p.seenRelics);
    CHECK(back.seenPotions == p.seenPotions);
    CHECK(back.seenMonsters == p.seenMonsters);
    CHECK(back.counters.at("runsWon") == 3);
    CHECK(back.counters.at("runsAbandoned") == 1);
    CHECK(back.save() == text);  // stable round trip
  }

  {  // versioning
    Progress ignored;
    CHECK(!ignored.load(""));                                  // empty data
    CHECK(!ignored.load("NOT_A_PROGRESS_FILE 1 END"));         // wrong tag
    CHECK(!ignored.load("STS2PROGRESS 999999 END"));           // future version this build can't read
    // A too-low version (below the oldest this build understands) is also rejected.
    CHECK(!ignored.load("STS2PROGRESS 0 END"));
    // Corrupt (garbled) data fails cleanly rather than crashing: claims 5 characters follow
    // but the stream ends right there.
    CHECK(!ignored.load("STS2PROGRESS 1 CHARACTERS 5"));
  }

  {  // M-stats (version 4): totals, enemy stats, events, per-character times round-trip
    progress::reset();
    progress::recordRunTotals("Ironclad", 600, true, 1234, true);
    progress::recordRunTotals("Ironclad", 400, true, 100, true);   // faster standard win
    progress::recordRunTotals("Ironclad", 100, true, 50, false);   // custom/daily win: no fastest
    progress::recordRunTotals("Silent", 90, false, 999, true);     // loss: no damage, no fastest
    Progress& p = progress::state();
    CHECK(p.totalPlaytime == 1190);
    CHECK(p.architectDamage == 1384);
    CHECK(p.characters.at("Ironclad").playtime == 1100);
    CHECK(p.characters.at("Ironclad").fastestWin == 400);
    CHECK(p.characters.at("Silent").fastestWin == -1);
    CHECK(p.fastestVictory() == 400);
    p.characters["Ironclad"].bestStreak = 4;
    p.characters["Silent"].bestStreak = 6;
    CHECK(p.bestWinStreak() == 6);
    progress::markEventSeen("Wellspring");
    progress::markEventSeen("");
    p.enemyStats["Nibbit"] = EnemyProgress{3, 1};
    p.enemyStats["Fogmog"] = EnemyProgress{2, 0};
    CHECK(p.totalKills() == 5);
    Progress back;
    CHECK(back.load(p.save()));
    CHECK(back.totalPlaytime == 1190);
    CHECK(back.architectDamage == 1384);
    CHECK(back.characters.at("Ironclad").playtime == 1100);
    CHECK(back.characters.at("Ironclad").fastestWin == 400);
    CHECK(back.characters.at("Silent").fastestWin == -1);
    CHECK(back.discoveredEvents.size() == 1 && back.discoveredEvents.count("Wellspring") == 1);
    CHECK(back.enemyStats.at("Nibbit").wins == 3 && back.enemyStats.at("Nibbit").losses == 1);
    CHECK(back.totalKills() == 5);
    progress::reset();
  }

  {  // a version 3 file (before M-stats) still loads, with the new fields empty
    Progress old;
    CHECK(old.load("STS2PROGRESS 3 CHARACTERS 1 Ironclad 2 1 1 3 4 CARDS 0 RELICS 0 POTIONS 0 MONSTERS 0 "
                   "COUNTERS 0 0 DAILY 0 0 ACHIEVEMENTS 0 0 DEFEATED 0 END"));
    CHECK(old.characters.at("Ironclad").wins == 2);
    CHECK(old.characters.at("Ironclad").maxAscension == 4);
    CHECK(old.characters.at("Ironclad").fastestWin == -1);
    CHECK(old.characters.at("Ironclad").playtime == 0);
    CHECK(old.totalPlaytime == 0 && old.architectDamage == 0);
    CHECK(old.enemyStats.empty() && old.discoveredEvents.empty());
    CHECK(old.fastestVictory() == -1);
  }

  {  // negative fields from a hand-edited file are clamped on load, not rejected outright
    Progress p;
    p.character("Ironclad").wins = -5;
    p.character("Ironclad").maxAscension = 99;
    p.counters["runsWon"] = -2;
    std::string text = p.save();
    Progress back;
    CHECK(back.load(text));
    CHECK(back.characters.at("Ironclad").wins == 0);
    CHECK(back.characters.at("Ironclad").maxAscension == 10);
    CHECK(back.counters.at("runsWon") == 0);
  }

  {  // atomic write: save() then load() round-trips through the filesystem
    progress::reset();
    progress::state().character("Ironclad").wins = 11;
    progress::state().seenCards.insert("Bash");
    std::string path = "build/progress_fixtures/progress.sav";
    remove(path.c_str());
    remove((path + ".tmp").c_str());
    CHECK(progress::save(path));
    progress::reset();
    CHECK(progress::state().characters.empty());
    CHECK(progress::load(path));
    CHECK(progress::state().characters.at("Ironclad").wins == 11);
    CHECK(progress::state().seenCards.count("Bash") == 1);
  }

  {  // atomic write fallback: a write that can't complete leaves the previous file untouched
    std::string path = "build/progress_fixtures/fallback.sav";
    remove(path.c_str());
    remove((path + ".tmp").c_str());
    progress::reset();
    progress::state().character("Ironclad").wins = 1;
    CHECK(progress::save(path));  // first, a good write
    std::string good;
    {
      FILE* f = fopen(path.c_str(), "rb");
      CHECK(f != nullptr);
      if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        good.resize((size_t)n);
        fread(&good[0], 1, (size_t)n, f);
        fclose(f);
      }
    }
    // Block the tmp write by putting a directory where the ".tmp" file needs to go: fopen(...,
    // "wb") on a directory fails, so save() must bail out before it ever removes/replaces `path`.
    makeDir(path + ".tmp");
    progress::state().character("Ironclad").wins = 999;  // a change that must NOT reach disk
    CHECK(!progress::save(path));
    std::string after;
    {
      FILE* f = fopen(path.c_str(), "rb");
      CHECK(f != nullptr);  // the original file is still there
      if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        after.resize((size_t)n);
        fread(&after[0], 1, (size_t)n, f);
        fclose(f);
      }
    }
    CHECK(after == good);  // byte-for-byte unchanged
    removeDir(path + ".tmp");
  }

  {  // stats updated after a simulated win/loss (ProgressSaveManager.UpdateWithRunData)
    progress::reset();
    progress::onRunEnded("Ironclad", 0, progress::RunOutcome::Win);
    {
      auto& cp = progress::state().character("Ironclad");
      CHECK(cp.wins == 1 && cp.losses == 0);
      CHECK(cp.currentStreak == 1 && cp.bestStreak == 1);
      CHECK(cp.maxAscension == 1);  // won at ascension 0 == maxAscension(0): unlocks 1
    }
    progress::onRunEnded("Ironclad", 0, progress::RunOutcome::Win);  // not at the frontier anymore
    {
      auto& cp = progress::state().character("Ironclad");
      CHECK(cp.wins == 2 && cp.currentStreak == 2 && cp.bestStreak == 2);
      CHECK(cp.maxAscension == 1);  // ascension 0 != maxAscension 1: no further unlock
    }
    progress::onRunEnded("Ironclad", 1, progress::RunOutcome::Win);  // now at the new frontier
    {
      auto& cp = progress::state().character("Ironclad");
      CHECK(cp.wins == 3 && cp.currentStreak == 3 && cp.bestStreak == 3);
      CHECK(cp.maxAscension == 2);
    }
    progress::onRunEnded("Ironclad", 1, progress::RunOutcome::Loss);
    {
      auto& cp = progress::state().character("Ironclad");
      CHECK(cp.wins == 3 && cp.losses == 1);
      CHECK(cp.currentStreak == 0 && cp.bestStreak == 3);  // streak resets, best is kept
      CHECK(cp.maxAscension == 2);                          // a loss never changes maxAscension
    }
    progress::onRunEnded("Ironclad", 0, progress::RunOutcome::Abandon);
    {
      auto& cp = progress::state().character("Ironclad");
      CHECK(cp.losses == 2 && cp.currentStreak == 0);  // C#'s UpdateWithRunData: abandon counts as a loss
    }
    CHECK(progress::state().counters.at("runsWon") == 3);
    CHECK(progress::state().counters.at("runsLost") == 2);
    CHECK(progress::state().counters.at("runsAbandoned") == 1);
    // A different character's record is independent.
    CHECK(progress::state().characters.find("Silent") == progress::state().characters.end());
  }

  {  // "seen" hooks: starter deck / starting relics (Run::start). Only cards/relics that are
    // actually ported end up in the run (see the PORT NOTE in Run::start), so check what was
    // actually built rather than the character table's full (aspirational) list.
    progress::reset();
    Run r;
    r.start(42, "Ironclad");
    CHECK(!r.deck.empty());
    for (auto& c : r.deck) CHECK(progress::state().seenCards.count(c->id) == 1);
    CHECK(!r.relics.empty());
    for (auto& rel : r.relics) CHECK(progress::state().seenRelics.count(rel->id) == 1);
  }

  {  // "seen" hooks: a card reward marks every offered card, even the ones not picked
    progress::reset();
    Run r;
    r.start(7, "Ironclad");
    auto offered = r.cardReward(RoomType::Monster, 3);
    CHECK(!offered.empty());
    for (auto& c : offered) CHECK(progress::state().seenCards.count(c->id) == 1);
  }

  {  // "seen" hooks: a procured potion
    progress::reset();
    Run r;
    r.start(3, "Ironclad");
    CHECK(!db::potionPool().empty());
    std::string potId = db::potionPool().front();
    auto p = db::potion(potId);
    CHECK(p != nullptr);
    CHECK(r.procurePotion(std::move(p)));
    CHECK(progress::state().seenPotions.count(potId) == 1);
  }

  {  // "seen" hooks: an obtained relic (Task<>, needs the scheduler)
    progress::reset();
    Run r;
    r.start(5, "Ironclad");
    auto rel = db::relic("Anchor");
    CHECK(rel != nullptr);
    Scheduler::get().spawn([](Run* run, std::unique_ptr<Relic> rl) -> Task<> {
      co_await run->obtainRelic(std::move(rl));
    }(&r, std::move(rel)));
    CHECK(pump([&] { return progress::state().seenRelics.count("Anchor") == 1; }));
    Scheduler::get().clear();
  }

  {  // "seen" hooks: monster fought (Run::fight, before combat resolves)
    progress::reset();
    auto r = std::make_unique<Run>();
    r->start(1, "Ironclad");
    Scheduler::get().spawn([](Run* run) -> Task<> { co_await run->fight("NibbitsNormal"); }(r.get()));
    CHECK(pump([&] { return r->combat && r->combat->playerPhase && r->combat->actions.waiting(); }));
    CHECK(progress::state().seenMonsters.count("Nibbit") == 1);
    Scheduler::get().clear();
  }

  {  // Run::abandon() records exactly once even if called twice
    progress::reset();
    Run r;
    r.start(9, "Defect");
    r.abandon();
    r.abandon();
    CHECK(progress::state().character("Defect").losses == 1);
  }

  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
