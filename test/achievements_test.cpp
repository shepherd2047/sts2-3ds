// M5 checks: achievements driven through real fights (Play20CardsSingleTurn, the CharacterSkill
// ones, DefeatOneBoss, Defeat<Act>Enemies), the run-end ones (win per character, NoRelicWin,
// AllCardsUpgraded, FloorTenThousand through Run::abandon), the custom / daily lock, the toast
// queue and progress.sav v3 (save / load of the unlocked set, v2 files still load).
// Never touches a real save (no profiles::init, no progress::save).
// Build: make -f Makefile.sdl build/achievements_test ; run: ./build/achievements_test
#include <cstdio>
#include <cstdlib>

#include "../source/core/achievements.h"
#include "../source/core/char_necrobinder.h"
#include "../source/core/char_regent.h"
#include "../source/core/char_silent.h"
#include "../source/core/game.h"
#include "../source/core/powers.h"
#include "../source/core/progress.h"

using namespace sts;
using achievements::Id;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                \
  do {                                                                             \
    ++checks;                                                                      \
    if (!(cond)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #cond); } \
  } while (0)

static Task<> wrap(Task<> t, bool* done) { co_await t; *done = true; }
static bool pump(const std::function<bool()>& done, int maxFrames = 6000) {
  for (int i = 0; i < maxFrames && !done(); ++i) Scheduler::get().update(0.05);
  return done();
}
static void runTask(Task<> t) {
  bool done = false;
  Scheduler::get().spawn(wrap(std::move(t), &done));
  pump([&] { return done; });
}

static Task<> fightTask(Run* r, std::string enc, bool* won) { *won = co_await r->fight(enc); }

// A fight's first player turn, enemies at 5000 HP.
struct Fight {
  std::unique_ptr<Run> r = std::make_unique<Run>();
  Combat* c = nullptr;
  bool won = false;
  Fight(const std::string& charId = "Ironclad", const std::string& enc = "NibbitsNormal", bool custom = false,
        const std::string& daily = "") {
    r->start(3, charId);
    r->customRun = custom;
    r->dailyDate = daily;
    Scheduler::get().spawn(fightTask(r.get(), enc, &won));
    pump([&] { return r->combat && r->combat->playerPhase && r->combat->actions.waiting(); });
    c = r->combat.get();
    for (Creature* e : c->enemies) e->hp = e->maxHp = 5000;
  }
  ~Fight() { Scheduler::get().clear(); }
  Creature* enemy(int i = 0) { return c->enemies[i]; }
  Card* find(const std::string& id) {
    for (Card* k : c->allCards()) if (k->id == id) return k;
    return nullptr;
  }
  Card* add(const std::string& id) {
    Card* k = c->addCard(db::card(id));
    c->hand.push_back(k);
    return k;
  }
  void toHand(Card* k) { c->removeFromPiles(k); c->hand.push_back(k); }
  void play(Card* k, Creature* t) {
    PlayerAction a;
    a.kind = PlayerAction::PlayCard;
    a.card = k;
    a.target = t;
    c->energy = 10;
    c->actions.fire(a);
    pump([&] { return c->over || (c->playerPhase && c->actions.waiting()); });
  }
  template <class P> void apply(Creature* t, int amount) {
    runTask([](Creature* t, int amount, Creature* by) -> Task<> { co_await applyPower<P>(t, amount, by, nullptr); }(t, amount, c->player));
  }
  void killAll() {
    runTask([](Combat* c) -> Task<> { co_await cmd::kill(c->aliveEnemies()); }(c));
    PlayerAction a;  // the win is noticed when the turn ends
    a.kind = PlayerAction::EndTurn;
    if (c->actions.waiting()) c->actions.fire(a);
    pump([&] { return won; }, 1000);
  }
};

static void freshProfile() {
  progress::reset();
  achievements::clearToasts();
}

int main() {
  db::init();
  CHECK(achievements::totalCount() == 22);
  CHECK(achievements::find("IroncladWin") && achievements::find("play20_cards_single_turn"));
  CHECK(achievements::locKey(Id::Play20CardsSingleTurn) == "achievements.PLAY20_CARDS_SINGLE_TURN");
  CHECK((int)Id::Play20CardsSingleTurn == 31 && (int)Id::DefeatUnderdocksEnemies == 14);

  // ---- Play20CardsSingleTurn: the 20th card played in one turn ----
  freshProfile();
  {
    Fight f;
    Card* strike = f.find("StrikeIronclad");
    CHECK(strike);
    for (int i = 0; i < 19; ++i) { f.toHand(strike); f.play(strike, f.enemy()); }
    CHECK(!achievements::isUnlocked(Id::Play20CardsSingleTurn));
    f.toHand(strike);
    f.play(strike, f.enemy());
    CHECK(achievements::isUnlocked(Id::Play20CardsSingleTurn));
    CHECK(achievements::unlockTime(Id::Play20CardsSingleTurn) > 0);
    Id t;
    CHECK(achievements::popToast(t) && t == Id::Play20CardsSingleTurn);
    CHECK(!achievements::popToast(t));
  }

  // ---- Ironclad: 20 exhausted in a fight, 999 unblocked damage in one hit ----
  freshProfile();
  {
    Fight f;
    auto exhaust = [&](int n) {
      for (int i = 0; i < n; ++i)
        runTask([](Combat* c) -> Task<> { co_await cmd::exhaustCard(*c, c->addCard(db::card("StrikeIronclad"))); }(f.c));
    };
    exhaust(19);
    CHECK(!achievements::isUnlocked(Id::CharacterSkillIronclad1));
    exhaust(1);
    CHECK(achievements::isUnlocked(Id::CharacterSkillIronclad1));
    auto hit = [&](int dmg) {
      runTask([](Combat* c, Creature* e, int dmg) -> Task<> { co_await cmd::damage(e, dmg, 0, c->player, nullptr); }(f.c, f.enemy(), dmg));
    };
    hit(998);
    CHECK(!achievements::isUnlocked(Id::CharacterSkillIronclad2));
    hit(999);
    CHECK(achievements::isUnlocked(Id::CharacterSkillIronclad2));
  }
  {  // the exhaust count is per fight
    progress::reset();
    Fight f;
    for (int i = 0; i < 19; ++i)
      runTask([](Combat* c) -> Task<> { co_await cmd::exhaustCard(*c, c->addCard(db::card("StrikeIronclad"))); }(f.c));
    CHECK(!achievements::isUnlocked(Id::CharacterSkillIronclad1));
  }

  // ---- Silent: 99 Poison on one enemy, 5 Sly auto-plays from one card ----
  freshProfile();
  {
    Fight f("Silent");
    f.apply<PoisonPower>(f.enemy(), 98);
    CHECK(!achievements::isUnlocked(Id::CharacterSkillSilent2));
    f.apply<PoisonPower>(f.enemy(), 1);
    CHECK(achievements::isUnlocked(Id::CharacterSkillSilent2));

    auto gamble = [&](int sly) {
      f.c->hand.clear();
      Card* g = f.add("CalculatedGamble");
      for (int i = 0; i < sly; ++i) f.add("DefendSilent")->singleTurnSly = true;
      f.play(g, nullptr);
    };
    gamble(4);
    CHECK(!achievements::isUnlocked(Id::CharacterSkillSilent1));
    gamble(5);
    CHECK(achievements::isUnlocked(Id::CharacterSkillSilent1));
  }
  {  // a Sly discard outside a card play counts nothing
    progress::reset();
    Fight f("Silent");
    std::vector<Card*> cards;
    for (int i = 0; i < 6; ++i) { cards.push_back(f.add("DefendSilent")); cards.back()->singleTurnSly = true; }
    runTask([](Combat* c, std::vector<Card*> v) -> Task<> { co_await cmd::discardCards(*c, v); }(f.c, cards));
    CHECK(!achievements::isUnlocked(Id::CharacterSkillSilent1));
  }

  // ---- Necrobinder: 999 Doom, 50 Strength on Osty ----
  freshProfile();
  {
    Fight f("Necrobinder");
    f.apply<DoomPower>(f.enemy(), 999);
    CHECK(achievements::isUnlocked(Id::CharacterSkillNecrobinder1));
    Creature* osty = nullptr;
    runTask([](Combat* c, Creature** out) -> Task<> { *out = co_await summonOsty(*c, 10); }(f.c, &osty));
    CHECK(osty && osty == f.c->osty);
    f.apply<StrengthPower>(f.c->player, 60);  // on the player: not Osty
    CHECK(!achievements::isUnlocked(Id::CharacterSkillNecrobinder2));
    f.apply<StrengthPower>(osty, 49);
    CHECK(!achievements::isUnlocked(Id::CharacterSkillNecrobinder2));
    f.apply<StrengthPower>(osty, 1);
    CHECK(achievements::isUnlocked(Id::CharacterSkillNecrobinder2));
  }

  // ---- Regent: 20 Stars, a Sovereign Blade of 999 ----
  freshProfile();
  {
    Fight f("Regent");
    f.c->stars = 0;
    runTask([](Combat* c) -> Task<> { co_await cmd::gainStars(*c, 19); }(f.c));
    CHECK(!achievements::isUnlocked(Id::CharacterSkillRegent2));
    runTask([](Combat* c) -> Task<> { co_await cmd::gainStars(*c, 1); }(f.c));
    CHECK(achievements::isUnlocked(Id::CharacterSkillRegent2));
    runTask([](Combat* c) -> Task<> { co_await cmd::forge(*c, 500, nullptr); }(f.c));
    CHECK(!achievements::isUnlocked(Id::CharacterSkillRegent1));
    runTask([](Combat* c) -> Task<> { co_await cmd::forge(*c, 500, nullptr); }(f.c));
    CHECK(achievements::isUnlocked(Id::CharacterSkillRegent1));
  }

  // ---- custom and daily runs lock achievements (AreAchievementsAndEpochsLocked) ----
  freshProfile();
  {
    Fight f("Silent", "NibbitsNormal", true);
    f.apply<PoisonPower>(f.enemy(), 120);
    CHECK(!achievements::isUnlocked(Id::CharacterSkillSilent2));
  }
  {
    Fight f("Silent", "NibbitsNormal", false, "2026-09-29");
    f.apply<PoisonPower>(f.enemy(), 120);
    CHECK(!achievements::isUnlocked(Id::CharacterSkillSilent2));
    Id t;
    CHECK(!achievements::popToast(t));
  }

  // ---- after a won fight: DefeatOneBoss, beaten monsters, Defeat<Act>Enemies ----
  freshProfile();
  {
    Fight f("Ironclad", "NibbitsNormal");
    f.killAll();
    CHECK(f.won);
    CHECK(progress::state().defeatedMonsters.count("Nibbit") > 0);
    CHECK(!achievements::isUnlocked(Id::DefeatOneBoss));
  }
  {
    std::string act, boss;
    {
      Run probe;
      probe.start(3, "Ironclad");
      act = probe.act().name;
      boss = probe.act().bosses.empty() ? "" : probe.act().bosses[0];
    }
    const auto& need = achievements::actMonsters(act);
    CHECK(!need.empty());
    CHECK(!boss.empty());
    // Everything of the act beaten except the boss's monsters: beating the boss completes it.
    Fight f("Ironclad", boss);
    std::set<std::string> bossMonsters;
    for (auto& e : f.c->ownedEnemies) bossMonsters.insert(e->monster->id);
    for (auto& m : need) if (!bossMonsters.count(m)) progress::state().defeatedMonsters.insert(m);
    CHECK(!achievements::isUnlocked(Id::DefeatOneBoss));
    f.killAll();
    CHECK(f.won);
    CHECK(achievements::isUnlocked(Id::DefeatOneBoss));
    Id actId = act == "Underdocks" ? Id::DefeatUnderdocksEnemies : Id::DefeatOvergrowthEnemies;
    CHECK(achievements::isUnlocked(actId));
    CHECK(!achievements::isUnlocked(Id::DefeatHiveEnemies));
  }

  // ---- run end: <Character>Win, NoRelicWin, AllCardsUpgraded; FloorTenThousand via abandon ----
  freshProfile();
  {
    Run r;
    r.start(5, "Silent");
    achievements::afterRunEnded(r, false);
    CHECK(!achievements::isUnlocked(Id::SilentWin));
    achievements::afterRunEnded(r, true);
    CHECK(achievements::isUnlocked(Id::SilentWin));
    CHECK(!achievements::isUnlocked(Id::IroncladWin));
    bool starterOnly = true;
    for (auto& rel : r.relics) starterOnly = starterOnly && rel->rarity == RelicRarity::Starter;
    CHECK(achievements::isUnlocked(Id::NoRelicWin) == starterOnly);
    CHECK(!achievements::isUnlocked(Id::AllCardsUpgraded));
    for (auto& k : r.deck) k->upgrade();
    achievements::afterRunEnded(r, true);
    CHECK(achievements::isUnlocked(Id::AllCardsUpgraded));
  }
  freshProfile();
  {
    Run r;
    r.start(5, "Defect");
    if (auto rel = db::relic("Anchor")) r.relics.push_back(std::move(rel));
    achievements::afterRunEnded(r, true);
    CHECK(achievements::isUnlocked(Id::DefectWin));
    CHECK(!achievements::isUnlocked(Id::NoRelicWin));  // a non-starter relic was obtained
  }
  freshProfile();
  {
    progress::state().counters["floorsClimbed"] = 9990;
    Run r;
    r.start(7, "Ironclad");
    r.mapHistory.assign(1, std::vector<history::MapPoint>(10));
    r.abandon();  // recordRunEnd -> achievements::afterRunEnded
    CHECK(progress::state().counters["floorsClimbed"] == 10000);
    CHECK(achievements::isUnlocked(Id::FloorTenThousand));
    CHECK(!achievements::isUnlocked(Id::IroncladWin));  // abandoned: not a win
  }
  freshProfile();
  {  // a custom run still counts floors but cannot unlock
    progress::state().counters["floorsClimbed"] = 9999;
    Run r;
    r.start(7, "Ironclad");
    r.customRun = true;
    r.mapHistory.assign(1, std::vector<history::MapPoint>(3));
    achievements::afterRunEnded(r, true);
    CHECK(progress::state().counters["floorsClimbed"] == 10002);
    CHECK(!achievements::isUnlocked(Id::FloorTenThousand));
    CHECK(!achievements::isUnlocked(Id::IroncladWin));
  }

  // ---- outside a run, revoke, save / load (progress.sav v3), old files ----
  freshProfile();
  {
    CHECK(achievements::unlock(Id::DefeatGloryEnemies, nullptr));
    CHECK(!achievements::unlock(Id::DefeatGloryEnemies, nullptr));  // already unlocked: no second toast
    CHECK(achievements::unlock(Id::RegentWin, nullptr));
    progress::state().achievements["some_future_achievement"] = 123;  // unknown names round-trip
    progress::state().defeatedMonsters.insert("Nibbit");
    CHECK(achievements::unlockedCount() == 2);
    std::string saved = progress::state().save();
    CHECK(saved.find("STS2PROGRESS 3 ") == 0);
    Progress back;
    CHECK(back.load(saved));
    CHECK(back.achievements == progress::state().achievements);
    CHECK(back.defeatedMonsters == progress::state().defeatedMonsters);
    CHECK(back.save() == saved);
    int64_t t = achievements::unlockTime(Id::RegentWin);
    progress::reset();
    CHECK(!achievements::isUnlocked(Id::RegentWin));
    CHECK(progress::state().load(saved));
    CHECK(achievements::isUnlocked(Id::RegentWin) && achievements::unlockTime(Id::RegentWin) == t);
    CHECK(achievements::isUnlocked(Id::DefeatGloryEnemies));
    CHECK(progress::state().achievements.count("some_future_achievement"));
    achievements::revoke(Id::RegentWin);
    CHECK(!achievements::isUnlocked(Id::RegentWin) && achievements::unlockedCount() == 1);
    // Version 2 and 1 files load with nothing unlocked.
    Progress v2;
    CHECK(v2.load("STS2PROGRESS 2 CHARACTERS 0 CARDS 0 RELICS 0 POTIONS 0 MONSTERS 0 COUNTERS 0 0 DAILY 0 0 END"));
    CHECK(v2.achievements.empty() && v2.defeatedMonsters.empty());
    Progress v1;
    CHECK(v1.load("STS2PROGRESS 1 CHARACTERS 0 CARDS 0 RELICS 0 POTIONS 0 MONSTERS 0 COUNTERS 0 0 END"));
    CHECK(v1.achievements.empty());
    Id tt;
    int n = 0;
    while (achievements::popToast(tt)) ++n;
    CHECK(n == 2);
  }
  progress::reset();

  printf("achievements_test: %d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
