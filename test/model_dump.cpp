// Prints every registered card, relic and potion with its canonical numbers, for
// tools/balance_check.py to compare with the C# (H5).  ./build/model_dump > build/model_dump.txt
//   CARD <id> <type> <rarity> <target> cost <c> upcost <c> x <0|1> star <n> maxup <n> gen <0|1> kw <list> upkw <list>
//   VAR <id> <name> <base> <upgraded>
//   RELIC <id> <rarity>          POTION <id> <rarity> <usage> <target>
#include <cstdio>

#include "../source/core/game.h"

using namespace sts;

static const char* kTypes[] = {"Attack", "Skill", "Power", "Status", "Curse", "Quest"};
static const char* kRarities[] = {"Basic", "Common", "Uncommon", "Rare", "Ancient", "Token", "Status", "Curse", "Quest"};
static const char* kTargets[] = {"None", "Self", "AnyEnemy", "AllEnemies", "RandomEnemy"};
static const char* kRelicRarities[] = {"None", "Starter", "Common", "Uncommon", "Rare", "Shop", "Event", "Ancient"};
static const char* kPotionRarities[] = {"None", "Common", "Uncommon", "Rare", "Event", "Token"};
static const char* kUsages[] = {"CombatOnly", "AnyTime", "Automatic"};

static std::string num(Dec d) {
  char buf[32];
  long long r = (long long)d.raw;
  if (r % Dec::kScale == 0) snprintf(buf, sizeof buf, "%lld", r / Dec::kScale);
  else snprintf(buf, sizeof buf, "%.6g", (double)r / Dec::kScale);
  return buf;
}

static std::string kws(int k) {
  static const char* names[] = {"Exhaust", "Unplayable", "Ethereal", "Innate", "Retain", "Sly", "Eternal"};
  std::string s;
  for (int i = 0; i < 7; ++i)
    if (k & (1 << i)) s += (s.empty() ? "" : ",") + std::string(names[i]);
  return s.empty() ? "-" : s;
}

static void vars(const char* kind, const std::string& id, const std::vector<DynVar>& a, const std::vector<DynVar>* up) {
  for (auto& v : a) {
    Dec u = v.base;
    if (up)
      for (auto& w : *up)
        if (w.name == v.name) u = w.base;
    printf("VAR %s %s %s %s\n", id.c_str(), v.name.c_str(), num(v.base).c_str(), num(u).c_str());
  }
}

int main() {
  db::init();
  for (auto& id : db::cardIds()) {
    auto c = db::card(id);
    if (!c) continue;
    auto u = c->clone();
    u->upgrade();
    printf("CARD %s %s %s %s cost %d upcost %d x %d star %d maxup %d gen %d kw %s upkw %s\n", id.c_str(), kTypes[(int)c->type],
           kRarities[(int)c->rarity], kTargets[(int)c->target], c->canonicalCost, u->canonicalCost == c->canonicalCost ? u->cost : u->canonicalCost,
           c->costsX ? 1 : 0, c->starCost, c->maxUpgradeLevel, c->canBeGeneratedInCombat() ? 1 : 0, kws(c->keywords).c_str(), kws(u->keywords).c_str());
    vars("CARD", id, c->vars, &u->vars);
  }
  for (auto& id : db::relicIds()) {
    auto r = db::relic(id);
    if (!r) continue;
    printf("RELIC %s %s\n", id.c_str(), kRelicRarities[(int)r->rarity]);
    vars("RELIC", id, r->vars, nullptr);
  }
  for (auto& id : db::potionIds()) {
    auto p = db::potion(id);
    if (!p) continue;
    printf("POTION %s %s %s %s\n", id.c_str(), kPotionRarities[(int)p->rarity], kUsages[(int)p->usage], kTargets[(int)p->target]);
    vars("POTION", id, p->vars, nullptr);
  }
  return 0;
}
