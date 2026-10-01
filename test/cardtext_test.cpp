// Card-text formatter checks (E9): {Name:choose(...)} on string and numeric vars, "{}" /
// "{:diff()}" self references, nested conditionals, abs() and recursive string vars.
// Links the preview objects (minus main). Build: make -f Makefile.sdl build/cardtext_test
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "../source/core/game.h"

namespace ui {
std::string expandSmart(const std::string& src, const std::vector<sts::DynVar>& vars, bool inCombat,
                        const std::map<std::string, std::string>* strVars, bool upgraded);
}

using sts::Dec;
using sts::DynVar;

static int failures = 0, checks = 0;
static void eq(int line, const std::string& got, const std::string& want) {
  ++checks;
  if (got != want) { ++failures; printf("FAIL line %d: got '%s' want '%s'\n", line, got.c_str(), want.c_str()); }
}
#define EQ(got, want) eq(__LINE__, got, want)

int main() {
  std::vector<DynVar> vars{{"Damage", Dec(7), Dec(7)}, {"Amount", Dec(1), Dec(1)}, {"Neg", Dec(-3), Dec(-3)}};
  std::map<std::string, std::string> sv{{"TargetType", "AllEnemies"}, {"CardType", "Skill"}, {"Pretty", "x{Damage}y"}};
  auto ex = [&](const std::string& s, bool up = false) { return ui::expandSmart(s, vars, false, &sv, up); };

  // {TargetType:choose(AllEnemies):a|} (Shiv, Inky): matching option picks alt 0, otherwise the empty fallback.
  EQ(ex("{TargetType:choose(AllEnemies):ALL|}hit"), "ALLhit");
  EQ(ex("{TargetType:choose(Self):ALL|}hit"), "hit");
  // {CardType:choose(Attack|Skill|Power):a|b|c||fallback}
  EQ(ex("{CardType:choose(Attack|Skill|Power):A|B|C||?}"), "B");
  EQ(ex("{CardType:choose(Attack|Power):A|C||other}"), "other");
  // Numeric choose with "{}" and "{:diff()}" self references.
  EQ(ex("{Amount:choose(1):one|{} times}"), "one");
  EQ(ex("{Damage:choose(1):one|[blue]{}[/blue] times}"), "[blue]7[/blue] times");
  EQ(ex("{Damage:choose(1):one|{:diff()} times}"), "7 times");
  // Nested {...} inside an alternative.
  EQ(ex("{Amount:choose(1):|{Damage}x}-"), "-");
  EQ(ex("{IfUpgraded:show:+{Damage}|}", true), "+7");
  EQ(ex("{IfUpgraded:show:+{Damage}|}", false), "");
  // abs()
  EQ(ex("{Neg:abs()}"), "3");
  // Recursive string var: its text is itself expanded.
  EQ(ex("{Pretty}"), "x7y");
  printf("cardtext_test: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
