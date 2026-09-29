// End-of-run badge checks (M7): every single-player badge's condition and tiers, RequiresWin,
// abandoned runs, the record round trip (version 2, version 1 still loads), the run-time counters
// and that the score has no badge bonus.
// Build: make -f Makefile.sdl build/badges_test ; run: ./build/badges_test
#include <cstdio>
#include <cstdlib>

#include "../source/core/badges.h"
#include "../source/core/game.h"
#include "../source/core/history.h"

using namespace sts;
using badges::BadgeRarity;
using history::MapPoint;
using history::PointType;
using history::RoomKind;
using history::RunRecord;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                               \
  do {                                                                            \
    ++checks;                                                                     \
    if (!(cond)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #cond); } \
  } while (0)

static int rar(const std::string& id, const RunRecord& r) { return (int)badges::rarityOf(id, r); }
static bool has(const RunRecord& r, const std::string& id) {
  for (auto& b : badges::compute(r)) if (b.id == id) return true;
  return false;
}
static int tierOf(const RunRecord& r, const std::string& id) {
  for (auto& b : badges::compute(r)) if (b.id == id) return b.rarity;
  return 0;
}

static void addCards(RunRecord& r, const char* id, int n) { for (int i = 0; i < n; ++i) r.deck.push_back({id, 0, "", 0}); }

// A won Ironclad run with an ordinary deck (no badge but the ones a test sets up).
static RunRecord base() {
  RunRecord r;
  r.character = "Ironclad";
  r.win = true;
  r.runTime = 5000;
  r.maxHp = 80;
  r.gold = 50;
  r.hp = 40;
  for (int i = 0; i < 5; ++i) r.deck.push_back({"StrikeIronclad", 0, "", 0});
  for (int i = 0; i < 5; ++i) r.deck.push_back({"DefendIronclad", 0, "", 0});
  r.deck.push_back({"Bash", 0, "", 0});
  addCards(r, "Anger", 2);  // 21 cards: no deck badge (two Angers: not Highlander, not Honed)
  for (const char* c : {"Thunderclap", "TwinStrike", "Headbutt", "PommelStrike", "Armaments", "IronWave", "Inflame", "Uppercut"})
    addCards(r, c, 1);
  r.relics = {"BurningBlood"};
  return r;
}

static MapPoint pt(PointType t, RoomKind k, const std::string& model = "") { return {t, {{k, model}}, 0}; }

int main() {
  db::init();
  const int startHp = db::character("Ironclad").startingHp;
  CHECK(startHp > 0);
  CHECK(badges::all().size() == 19);
  for (auto& i : badges::all()) CHECK(badges::find(i.id) == &i);

  {  // the plain run gets nothing (win, 21 cards, 50 gold, 5000 s, 80 max hp)
    RunRecord r = base();
    r.maxHp = startHp;
    CHECK(badges::compute(r).empty());
  }
  {  // TINY_DECK: 20 / 10 / 5 or fewer
    RunRecord r = base();
    r.maxHp = startHp;
    r.deck.resize(20);
    CHECK(tierOf(r, "TINY_DECK") == 1);
    r.deck.resize(11);
    CHECK(tierOf(r, "TINY_DECK") == 1);
    r.deck.resize(10);
    CHECK(tierOf(r, "TINY_DECK") == 2);
    r.deck.resize(5);
    CHECK(tierOf(r, "TINY_DECK") == 3);
    r.win = false;
    CHECK(!has(r, "TINY_DECK"));  // RequiresWin
  }
  {  // BIG_DECK: 40 / 60 / 100
    RunRecord r = base();
    addCards(r, "Anger", 18);  // 39
    CHECK(!has(r, "BIG_DECK"));
    addCards(r, "Anger", 1);
    CHECK(tierOf(r, "BIG_DECK") == 1);
    addCards(r, "Anger", 19);
    CHECK(tierOf(r, "BIG_DECK") == 1);
    addCards(r, "Anger", 1);
    CHECK(tierOf(r, "BIG_DECK") == 2);
    addCards(r, "Anger", 39);
    CHECK(tierOf(r, "BIG_DECK") == 2);
    addCards(r, "Anger", 1);
    CHECK(tierOf(r, "BIG_DECK") == 3);
    r.win = false;
    CHECK(!has(r, "BIG_DECK"));
  }
  {  // CURSES: 5 or more Curse cards (Basic / Status do not count)
    RunRecord r = base();
    for (const char* c : {"Injury", "Regret", "Doubt", "Shame"}) CHECK(db::card(c) && db::card(c)->type == CardType::Curse);
    for (const char* c : {"Injury", "Regret", "Doubt", "Shame"}) addCards(r, c, 1);
    CHECK(!has(r, "CURSES"));
    addCards(r, "Injury", 1);
    CHECK(tierOf(r, "CURSES") == 1);
    r.win = false;
    CHECK(!has(r, "CURSES"));
  }
  {  // HIGHLANDER: every non-Basic card unique (Strikes, Defends, Bash are Basic)
    RunRecord r = base();
    CHECK(!has(r, "HIGHLANDER"));  // two Angers
    r.deck.erase(r.deck.begin() + 11);
    CHECK(has(r, "HIGHLANDER"));
    addCards(r, "Anger", 1);
    CHECK(!has(r, "HIGHLANDER"));
    r.deck.pop_back();
    r.win = false;
    CHECK(!has(r, "HIGHLANDER"));
  }
  {  // HONED: 5+ of one non-Basic card
    RunRecord r = base();
    CHECK(!has(r, "HONED"));  // 5 Strikes are Basic; 2 Angers
    addCards(r, "Anger", 2);
    CHECK(!has(r, "HONED"));
    addCards(r, "Anger", 1);
    CHECK(tierOf(r, "HONED") == 1);
    r.win = false;
    CHECK(!has(r, "HONED"));
  }
  {  // ILIKESHINY: 25 relics, win or lose
    RunRecord r = base();
    r.win = false;
    r.relics.assign(24, "Anchor");
    CHECK(!has(r, "ILIKESHINY"));
    r.relics.push_back("Anchor");
    CHECK(tierOf(r, "ILIKESHINY") == 1);
  }
  {  // DOUBLE_SNECKO: both eyes
    RunRecord r = base();
    r.win = false;
    r.relics = {"SneckoEye"};
    CHECK(!has(r, "DOUBLE_SNECKO"));
    r.relics = {"FakeSneckoEye"};
    CHECK(!has(r, "DOUBLE_SNECKO"));
    r.relics = {"FakeSneckoEye", "Anchor", "SneckoEye"};
    CHECK(tierOf(r, "DOUBLE_SNECKO") == 1);
  }
  {  // ELITE: 3 / 6 / 9 elites killed (the elite that killed you is not counted)
    RunRecord r = base();
    r.win = false;
    r.path.assign(1, {});
    for (int i = 0; i < 3; ++i) r.path[0].push_back(pt(PointType::Elite, RoomKind::Elite, "E"));
    CHECK(!has(r, "ELITE"));  // 3 elites, the last killed you = 2
    r.path[0].push_back(pt(PointType::Elite, RoomKind::Elite, "E"));
    CHECK(tierOf(r, "ELITE") == 1);
    r.win = true;
    for (int i = 0; i < 2; ++i) r.path[0].push_back(pt(PointType::Elite, RoomKind::Elite, "E"));  // 6 elites, all killed
    r.path[0].push_back(pt(PointType::Monster, RoomKind::Monster, "M"));
    CHECK(tierOf(r, "ELITE") == 2);
    for (int i = 0; i < 3; ++i) r.path[0].push_back(pt(PointType::Elite, RoomKind::Elite, "E"));
    r.path[0].push_back(pt(PointType::Monster, RoomKind::Monster, "M"));
    CHECK(tierOf(r, "ELITE") == 3);
    CHECK(rar("ELITE", r) == 3);
  }
  {  // FAMISHED: max hp < (start + 1) / 2
    RunRecord r = base();
    r.maxHp = (startHp + 1) / 2;
    CHECK(!has(r, "FAMISHED"));
    r.maxHp = (startHp + 1) / 2 - 1;
    CHECK(tierOf(r, "FAMISHED") == 1);
    r.win = false;
    CHECK(!has(r, "FAMISHED"));
  }
  {  // GLUTTON: max hp 15 / 30 / 50 over the starting hp
    RunRecord r = base();
    r.maxHp = startHp + 14;
    CHECK(!has(r, "GLUTTON"));
    r.maxHp = startHp + 15;
    CHECK(tierOf(r, "GLUTTON") == 1);
    r.maxHp = startHp + 30;
    CHECK(tierOf(r, "GLUTTON") == 2);
    r.maxHp = startHp + 49;
    CHECK(tierOf(r, "GLUTTON") == 2);
    r.maxHp = startHp + 50;
    CHECK(tierOf(r, "GLUTTON") == 3);
    r.win = false;
    CHECK(!has(r, "GLUTTON"));
    r = base();
    r.character = "Silent";  // the starting hp is the run's character's
    r.maxHp = db::character("Silent").startingHp + 15;
    CHECK(tierOf(r, "GLUTTON") == 1);
  }
  {  // KACHING: 1000+ gold spent in Shop rooms
    RunRecord r = base();
    r.path.assign(1, {});
    r.path[0].push_back(pt(PointType::Shop, RoomKind::Shop));
    r.path[0].push_back(pt(PointType::Shop, RoomKind::Shop));
    r.path[0].push_back(pt(PointType::Unknown, RoomKind::Event, "SomeEvent"));
    r.path[0][0].goldSpent = 600;
    r.path[0][1].goldSpent = 399;
    r.path[0][2].goldSpent = 500;  // not a shop room: ignored
    r.win = false;
    CHECK(!has(r, "KACHING"));
    r.path[0][1].goldSpent = 400;
    CHECK(tierOf(r, "KACHING") == 1);
  }
  {  // MONEY_MONEY: 200 / 400 / 600 gold
    RunRecord r = base();
    r.gold = 199;
    CHECK(!has(r, "MONEY_MONEY"));
    r.gold = 200;
    CHECK(tierOf(r, "MONEY_MONEY") == 1);
    r.gold = 400;
    CHECK(tierOf(r, "MONEY_MONEY") == 2);
    r.gold = 599;
    CHECK(tierOf(r, "MONEY_MONEY") == 2);
    r.gold = 600;
    CHECK(tierOf(r, "MONEY_MONEY") == 3);
    r.win = false;
    CHECK(!has(r, "MONEY_MONEY"));
  }
  {  // MYSTERY_MACHINE: 15 or more ? map points
    RunRecord r = base();
    r.win = false;
    r.path.assign(2, {});
    for (int i = 0; i < 14; ++i) r.path[i % 2].push_back(pt(PointType::Unknown, RoomKind::Event, "E"));
    r.path[0].push_back(pt(PointType::Monster, RoomKind::Monster, "M"));
    CHECK(!has(r, "MYSTERY_MACHINE"));
    r.path[1].push_back(pt(PointType::Unknown, RoomKind::Monster, "M"));  // a ? that rolled a fight still counts
    CHECK(tierOf(r, "MYSTERY_MACHINE") == 1);
  }
  {  // PERFECT: bosses beaten without damage (1 / 2 / 3); the killing boss room is skipped
    RunRecord r = base();
    r.path.assign(3, {});
    for (int a = 0; a < 3; ++a) {
      r.path[a].push_back(pt(PointType::Boss, RoomKind::Boss, "B"));
    }
    CHECK(tierOf(r, "PERFECT") == 3);
    r.path[1][0].damageTaken = 1;
    CHECK(tierOf(r, "PERFECT") == 2);
    r.path[0][0].damageTaken = 5;
    CHECK(tierOf(r, "PERFECT") == 1);
    r.path[2][0].damageTaken = 5;
    CHECK(!has(r, "PERFECT"));
    r.path[2][0].damageTaken = 0;
    r.win = false;  // died to the last boss: it does not count
    CHECK(tierOf(r, "PERFECT") == 0);
    r.path[1][0].damageTaken = 0;
    CHECK(tierOf(r, "PERFECT") == 1);  // only act 2's boss is clean
    r.path[1][0].tracked = false;  // resumed from run.sav: unknown, not counted
    CHECK(tierOf(r, "PERFECT") == 0);
  }
  {  // RESTFUL / RESTLESS
    RunRecord r = base();
    r.path.assign(1, {});
    CHECK(!has(r, "RESTFUL") && !has(r, "RESTLESS"));  // no rest sites at all
    r.path[0].push_back(pt(PointType::RestSite, RoomKind::RestSite));
    r.path[0].push_back(pt(PointType::RestSite, RoomKind::RestSite));
    r.path[0][0].restChoices = {"HEAL"};
    r.path[0][1].restChoices = {"HEAL", "SMITH"};  // Miniature Tent
    CHECK(tierOf(r, "RESTFUL") == 1 && !has(r, "RESTLESS"));
    r.path[0][1].restChoices = {"SMITH"};
    CHECK(!has(r, "RESTFUL") && !has(r, "RESTLESS"));
    r.path[0][0].restChoices = {"SMITH"};
    CHECK(!has(r, "RESTFUL") && tierOf(r, "RESTLESS") == 1);
    r.path[0][1].tracked = false;
    CHECK(!has(r, "RESTFUL") && !has(r, "RESTLESS"));
    r.path[0][1].tracked = true;
    r.win = false;
    CHECK(!has(r, "RESTLESS"));
  }
  {  // SPEEDY: win in 3000 / 2400 / 1800 s
    RunRecord r = base();
    r.runTime = 3001;
    CHECK(!has(r, "SPEEDY"));
    r.runTime = 3000;
    CHECK(tierOf(r, "SPEEDY") == 1);
    r.runTime = 2400;
    CHECK(tierOf(r, "SPEEDY") == 2);
    r.runTime = 1801;
    CHECK(tierOf(r, "SPEEDY") == 2);
    r.runTime = 1800;
    CHECK(tierOf(r, "SPEEDY") == 3);
    r.win = false;
    CHECK(!has(r, "SPEEDY"));
  }
  {  // TABLET: won with 1 max hp
    RunRecord r = base();
    r.maxHp = 1;
    CHECK(tierOf(r, "TABLET") == 3);
    r.maxHp = 2;
    CHECK(!has(r, "TABLET"));
    r.maxHp = 1;
    r.win = false;
    CHECK(!has(r, "TABLET"));
  }
  {  // CCCCOMBO: the flag, win or lose
    RunRecord r = base();
    r.win = false;
    CHECK(!has(r, "CCCCOMBO"));
    r.cccCombo = true;
    CHECK(tierOf(r, "CCCCOMBO") == 1);
  }
  {  // abandoned runs get no badges
    RunRecord r = base();
    r.cccCombo = true;
    r.maxHp = 1;
    CHECK(badges::compute(r).size() >= 2);
    r.abandoned = true;
    CHECK(badges::compute(r).empty());
  }
  {  // no badge bonus in the score
    RunRecord r = base();
    r.path.assign(1, {});
    r.path[0].push_back(pt(PointType::Monster, RoomKind::Monster, "M"));
    r.path[0][0].goldGained = 250;
    int s = history::score(r.path, 3, true);
    r.maxHp = 1;
    r.cccCombo = true;
    CHECK(!badges::compute(r).empty());
    CHECK(history::score(r.path, 3, true) == s);
  }
  {  // record: version 2 round trip keeps the badge data; a version 1 record still loads
    RunRecord r = base();
    r.path.assign(1, {});
    r.path[0].push_back(pt(PointType::RestSite, RoomKind::RestSite));
    r.path[0].push_back(pt(PointType::Shop, RoomKind::Shop));
    r.path[0][0].restChoices = {"HEAL", "SMITH"};
    r.path[0][1].goldSpent = 321;
    r.path[0][1].damageTaken = 7;
    r.cccCombo = true;
    r.maxHp = 1;
    r.badges = badges::compute(r);
    CHECK(!r.badges.empty());
    RunRecord g;
    CHECK(g.load(r.save()));
    CHECK(g == r);
    CHECK(g.badges == r.badges && g.cccCombo);
    CHECK(g.path[0][0].restChoices.size() == 2 && g.path[0][0].restChoices[1] == "SMITH");
    CHECK(g.path[0][1].goldSpent == 321 && g.path[0][1].damageTaken == 7 && g.path[0][1].tracked);
    // A version 1 record: same text up to the path, no badge section.
    std::string v1 = r.save();
    size_t at = v1.find(" 2 ");
    CHECK(at != std::string::npos);
    v1.replace(at, 3, " 1 ");
    size_t cut = v1.find("BADGES");
    size_t end = v1.find("DECK");
    CHECK(cut != std::string::npos && end != std::string::npos);
    v1.erase(cut, end - cut);
    RunRecord o;
    CHECK(o.load(v1));
    CHECK(o.badges.empty() && !o.cccCombo && !o.path[0][1].tracked && o.deck.size() == r.deck.size());
  }
  {  // loc keys
    auto k = badges::locKeys({"TINY_DECK", 2});
    CHECK(k.rarityTitle == "badges.TINY_DECK.silverTitle" && k.description == "badges.TINY_DECK.description");
  }
  {  // run-time counters
    Run run;
    badges::noteDamageTaken(run, 5);  // no map point yet: no-op
    run.historyPoint(PointType::Boss);
    badges::noteDamageTaken(run, 4);
    badges::noteDamageTaken(run, 0);
    badges::noteDamageTaken(run, 3);
    badges::noteGoldSpent(run, 75);
    badges::noteRestChoice(run, 0);
    badges::noteRestChoice(run, 1);
    badges::noteRestChoice(run, 99);
    badges::noteCardsPlayedThisTurn(run, 19);
    CHECK(!run.cccCombo);
    badges::noteCardsPlayedThisTurn(run, 20);
    CHECK(run.cccCombo);
    auto& p = run.mapHistory.back().back();
    CHECK(p.damageTaken == 7 && p.goldSpent == 75 && p.tracked);
    CHECK(p.restChoices.size() == 2 && p.restChoices[0] == "HEAL" && p.restChoices[1] == "SMITH");
  }

  printf("badges_test: %d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
