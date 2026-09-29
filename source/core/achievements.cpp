// Achievements (package M5). See achievements.h for where each check runs and what is inferred.
#include "achievements.h"

#include <algorithm>
#include <ctime>
#include <deque>
#include <map>
#include <set>

#include "char_necrobinder.h"
#include "char_silent.h"
#include "game.h"
#include "powers.h"
#include "progress.h"

namespace sts {
namespace achievements {

namespace {

const std::vector<Info> kAll = {
    {Id::IroncladWin, "IroncladWin", "ironclad_win"},
    {Id::SilentWin, "SilentWin", "silent_win"},
    {Id::RegentWin, "RegentWin", "regent_win"},
    {Id::NecrobinderWin, "NecrobinderWin", "necrobinder_win"},
    {Id::DefectWin, "DefectWin", "defect_win"},
    {Id::DefeatUnderdocksEnemies, "DefeatUnderdocksEnemies", "defeat_underdocks_enemies"},
    {Id::DefeatOvergrowthEnemies, "DefeatOvergrowthEnemies", "defeat_overgrowth_enemies"},
    {Id::DefeatHiveEnemies, "DefeatHiveEnemies", "defeat_hive_enemies"},
    {Id::DefeatGloryEnemies, "DefeatGloryEnemies", "defeat_glory_enemies"},
    {Id::DefeatOneBoss, "DefeatOneBoss", "defeat_one_boss"},
    {Id::FloorTenThousand, "FloorTenThousand", "floor_ten_thousand"},
    {Id::CharacterSkillSilent1, "CharacterSkillSilent1", "character_skill_silent1"},
    {Id::CharacterSkillSilent2, "CharacterSkillSilent2", "character_skill_silent2"},
    {Id::CharacterSkillIronclad1, "CharacterSkillIronclad1", "character_skill_ironclad1"},
    {Id::CharacterSkillIronclad2, "CharacterSkillIronclad2", "character_skill_ironclad2"},
    {Id::CharacterSkillNecrobinder1, "CharacterSkillNecrobinder1", "character_skill_necrobinder1"},
    {Id::CharacterSkillNecrobinder2, "CharacterSkillNecrobinder2", "character_skill_necrobinder2"},
    {Id::CharacterSkillRegent1, "CharacterSkillRegent1", "character_skill_regent1"},
    {Id::CharacterSkillRegent2, "CharacterSkillRegent2", "character_skill_regent2"},
    {Id::NoRelicWin, "NoRelicWin", "no_relic_win"},
    {Id::AllCardsUpgraded, "AllCardsUpgraded", "all_cards_upgraded"},
    {Id::Play20CardsSingleTurn, "Play20CardsSingleTurn", "play20_cards_single_turn"},
};

std::deque<Id>& toasts() {
  static std::deque<Id> q;
  return q;
}

// The AchievementModels of the C# as one listener; its counters live per combat.
struct Watcher : Model {
  Combat* c = nullptr;
  int cardsPlayedThisTurn = 0;      // Play20CardsSingleTurnAchievement
  int cardsExhaustedThisCombat = 0;  // SkillIronclad1Achievement
  Card* firstCardOnStack = nullptr;  // SkillSilent1Achievement
  int slyCardsPlayed = 0;

  const Run* run() const { return c ? c->run : nullptr; }
  bool mine(Card* k) const { return k && c && k->combat == c; }  // LocalContext.IsMine (single player)
  bool me(Creature* cr) const { return cr && c && cr == c->player; }  // LocalContext.IsMe

  Task<> afterRoomEntered(RoomType) override {
    cardsExhaustedThisCombat = 0;
    firstCardOnStack = nullptr;
    slyCardsPlayed = 0;
    return {};
  }
  Task<> afterSideTurnStart(Side side, const std::vector<Creature*>&) override {
    if (side == Side::Player) cardsPlayedThisTurn = 0;
    return {};
  }
  Task<> beforeCardPlayed(const CardPlay& cp) override {
    if (mine(cp.card) && !firstCardOnStack) firstCardOnStack = cp.card;
    return {};
  }
  Task<> afterCardPlayed(const CardPlay& cp) override {
    if (!mine(cp.card)) return {};
    if (++cardsPlayedThisTurn >= 20) unlock(Id::Play20CardsSingleTurn, run());
    if (cp.card == firstCardOnStack) {
      firstCardOnStack = nullptr;
      slyCardsPlayed = 0;
    }
    return {};
  }
  Task<> afterCardExhausted(Card* k, bool) override {
    if (mine(k) && ++cardsExhaustedThisCombat >= 20) unlock(Id::CharacterSkillIronclad1, run());
    return {};
  }
  Task<> afterDamageGiven(Creature* dealer, const DamageResult& r, int, Creature*, Card*) override {
    if (me(dealer) && r.unblocked >= 999) unlock(Id::CharacterSkillIronclad2, run());
    return {};
  }
  Task<> afterPowerAmountChanged(Power* p, Dec, Creature* applier, Card*) override {
    if (!p || !me(applier)) return {};
    if (p->id == DoomPower::kId && p->amount >= 999) unlock(Id::CharacterSkillNecrobinder1, run());
    if (p->id == StrengthPower::kId && p->owner && p->owner == c->osty && p->amount >= 50)
      unlock(Id::CharacterSkillNecrobinder2, run());
    if (p->id == PoisonPower::kId && p->amount >= 99) unlock(Id::CharacterSkillSilent2, run());
    return {};
  }
  Task<> afterForge(Dec, Model*) override {
    if (!c || isUnlocked(Id::CharacterSkillRegent1)) return {};
    for (Card* k : c->allCards())
      if (k->id == "SovereignBlade")
        if (DynVar* v = k->var("Damage"); v && v->base >= Dec(999)) unlock(Id::CharacterSkillRegent1, run());
    return {};
  }
  Task<> afterStarsGained(int) override {
    if (c && c->stars >= 20) unlock(Id::CharacterSkillRegent2, run());
    return {};
  }
};

Watcher& watcher() {
  static Watcher w;
  return w;
}

// Encounters' summoned monsters that generate() never returns (as the bestiary's list).
const std::vector<std::string>& summons(const std::string& encounter) {
  static const std::map<std::string, std::vector<std::string>> kSummons = {
      {"FogmogNormal", {"EyeWithTeeth"}}, {"GremlinMercNormal", {"FatGremlin", "SneakyGremlin"}},
      {"LivingFogNormal", {"GasBomb"}}, {"TheObscuraNormal", {"Parafright"}}, {"OvicopterNormal", {"ToughEgg"}}};
  static const std::vector<std::string> none;
  auto it = kSummons.find(encounter);
  return it == kSummons.end() ? none : it->second;
}

// ActModel.AllMonsters over the registered encounters of the act (built once per act).
const std::set<std::string>& monstersOf(const db::ActDef& act) {
  static std::map<std::string, std::set<std::string>> cache;
  auto [it, fresh] = cache.try_emplace(act.name);
  if (!fresh) return it->second;
  db::init();
  std::set<std::string>& out = it->second;
  for (auto* list : {&act.weak, &act.normal, &act.elites, &act.bosses})
    for (auto& encId : *list) {
      const Encounter* enc = db::encounter(encId);
      if (!enc) continue;
      for (int seed = 1; seed <= 16; ++seed) {
        Rng rng((uint64_t)seed, "Achievements");
        for (auto& m : enc->generate(rng)) out.insert(m->id);
      }
      for (auto& s : summons(encId)) if (db::monster(s)) out.insert(s);
    }
  return out;
}

Id defeatAllFor(const std::string& act, bool& ok) {
  ok = true;
  if (act == "Overgrowth") return Id::DefeatOvergrowthEnemies;
  if (act == "Underdocks") return Id::DefeatUnderdocksEnemies;
  if (act == "Hive") return Id::DefeatHiveEnemies;
  if (act == "Glory") return Id::DefeatGloryEnemies;
  ok = false;
  return Id::DefeatOneBoss;
}

}  // namespace

const std::vector<Info>& all() { return kAll; }

const std::set<std::string>& actMonsters(const std::string& actName) {
  static const std::set<std::string> none;
  for (auto& a : db::acts()) if (actName == a.name) return monstersOf(a);
  return none;
}

const Info& info(Id id) {
  for (auto& i : kAll) if (i.id == id) return i;
  return kAll[0];  // unreachable for a valid Id
}

const Info* find(const std::string& n) {
  for (auto& i : kAll) if (n == i.name || n == i.snake) return &i;
  return nullptr;
}

std::string locKey(Id id) {
  std::string s = info(id).snake;
  for (char& ch : s) if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
  return "achievements." + s;
}

bool isUnlocked(Id id) { return progress::state().achievements.count(info(id).snake) > 0; }

int64_t unlockTime(Id id) {
  auto& m = progress::state().achievements;
  auto it = m.find(info(id).snake);
  return it == m.end() ? 0 : it->second;
}

int unlockedCount() {
  int n = 0;
  for (auto& i : kAll) n += isUnlocked(i.id);
  return n;
}
int totalCount() { return (int)kAll.size(); }

bool lockedFor(const Run& run) { return run.customRun || !run.dailyDate.empty(); }

bool unlock(Id id, const Run* run) {
  if (run && lockedFor(*run)) return false;
  if (isUnlocked(id)) return false;
  progress::state().achievements[info(id).snake] = (int64_t)time(nullptr);
  queueToast(id);
  return true;
}

void revoke(Id id) { progress::state().achievements.erase(info(id).snake); }

void queueToast(Id id) { toasts().push_back(id); }
bool popToast(Id& out) {
  if (toasts().empty()) return false;
  out = toasts().front();
  toasts().pop_front();
  return true;
}
void clearToasts() { toasts().clear(); }

Model* combatListener(Combat& c) {
  Watcher& w = watcher();
  if (w.c != &c) {  // a new fight: nothing carries over from the last one
    w.c = &c;
    w.cardsPlayedThisTurn = w.cardsExhaustedThisCombat = w.slyCardsPlayed = 0;
    w.firstCardOnStack = nullptr;
  }
  return &w;
}

void beforeSlyAutoPlay(Combat& c, Card* card) {
  Watcher& w = watcher();
  if (w.c != &c || !w.mine(card) || !w.firstCardOnStack || c.ending) return;
  if (++w.slyCardsPlayed >= 5) unlock(Id::CharacterSkillSilent1, c.run);
}

void afterCombatWon(Run& run, Combat& c) {
  // ProgressSaveManager.UpdateAfterCombatWon: EnemyStats gets a win for every monster of the fight.
  for (auto& e : c.ownedEnemies)
    if (e && e->monster) progress::state().defeatedMonsters.insert(e->monster->id);
  // CombatManager: CheckForDefeatedAllEnemiesAchievement(runState.Act), then AfterBossDefeated.
  bool ok = false;
  Id actId = defeatAllFor(run.act().name, ok);
  if (ok && !isUnlocked(actId)) {
    const auto& need = monstersOf(run.act());
    const auto& have = progress::state().defeatedMonsters;
    bool allBeaten = !need.empty() && std::all_of(need.begin(), need.end(), [&](const std::string& m) { return have.count(m) > 0; });
    if (allBeaten) unlock(actId, &run);
  }
  if (c.isBoss) unlock(Id::DefeatOneBoss, &run);
}

void afterRunEnded(Run& run, bool victory) {
  // ProgressSaveManager.UpdateWithRunData: FloorsClimbed += every map point of the run.
  int64_t floors = 0;
  for (auto& act : run.mapHistory) floors += (int64_t)act.size();
  progress::incrementCounter("floorsClimbed", floors);
  if (progress::state().counters["floorsClimbed"] >= 10000) unlock(Id::FloorTenThousand, &run);
  if (!victory) return;
  const std::string& ch = run.characterId;
  if (ch == "Ironclad") unlock(Id::IroncladWin, &run);
  else if (ch == "Silent") unlock(Id::SilentWin, &run);
  else if (ch == "Regent") unlock(Id::RegentWin, &run);
  else if (ch == "Necrobinder") unlock(Id::NecrobinderWin, &run);
  else if (ch == "Defect") unlock(Id::DefectWin, &run);
  bool onlyStarter = std::all_of(run.relics.begin(), run.relics.end(),
                                 [](const std::unique_ptr<Relic>& r) { return !r || r->rarity == RelicRarity::Starter; });
  if (onlyStarter) unlock(Id::NoRelicWin, &run);
  bool allUpgraded = !run.deck.empty() && std::all_of(run.deck.begin(), run.deck.end(), [](const std::unique_ptr<Card>& k) {
    return !k || k->upgraded() || k->maxUpgradeLevel <= 0;
  });
  if (allUpgraded) unlock(Id::AllCardsUpgraded, &run);
}

}  // namespace achievements
}  // namespace sts
