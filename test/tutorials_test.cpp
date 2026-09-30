// M13 tutorials checks: a tip shows once, not again once seen, again after 重置教程; the
// combat-basics tip is only marked seen after its last page; "no tutorials" turns every tip off;
// the seen flags survive a settings.sav round trip. Build: make -f Makefile.sdl build/tutorials_test
//
// Never touches a real save: STS_NO_SAVE keeps tutorials.cpp from writing settings.sav, and the
// explicit file round trip below uses a path under build/.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

#include "../source/core/settings_store.h"
#include "../source/ui/tutorials.h"

using namespace sts;
using ui::Ftue;
using ui::showTip;
namespace tips = ui::tips;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                               \
  do {                                                                            \
    ++checks;                                                                     \
    if (!(cond)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #cond); } \
  } while (0)

static void setEnv(const char* k, const char* v) {
#ifdef _WIN32
  _putenv_s(k, v);
#else
  setenv(k, v, 1);
#endif
}

// Opens and dismisses whatever is queued; returns how many tips opened.
static int drain() {
  int n = 0;
  while (tips::openNext()) {
    ++n;
    while (!tips::advance()) {}
  }
  return n;
}

int main() {
  setEnv("STS_NO_SAVE", "1");
  setEnv("STS_SETTINGS_PATH", "build/tutorials_fixtures/settings_default.sav");
  setEnv("STS_HIDDEN", "1");

  {  // automated previews: no tips unless STS_TIPS (the env gate, read once)
    settings::reset();
    CHECK(!tips::allowed());
    CHECK(!showTip(Ftue::MapSelect));
    CHECK(!tips::openNext());
  }

  tips::forceAllowed(1);

  {  // shows once, not again after seen, again after reset
    settings::reset();
    tips::clear();
    CHECK(showTip(Ftue::MapSelect));
    CHECK(showTip(Ftue::MapSelect));  // already queued: no duplicate
    CHECK(tips::openNext());
    CHECK(tips::current() == Ftue::MapSelect);
    CHECK(settings::tutorialSeen("map_select_ftue"));  // marked when shown, as the C#
    CHECK(showTip(Ftue::RestSite));
    CHECK(tips::advance());  // one page: 了解了！ closes it
    CHECK(tips::current() == Ftue::None);
    CHECK(tips::openNext());  // the rest site tip queued above
    CHECK(tips::advance());
    CHECK(!showTip(Ftue::MapSelect));
    CHECK(!tips::openNext());
    settings::resetTutorials();
    CHECK(showTip(Ftue::MapSelect));
    CHECK(drain() == 1);
  }

  {  // combat basics: three pages, back works, seen only after the last one
    settings::reset();
    tips::clear();
    CHECK(tips::pages(Ftue::CombatRules) == 3);
    CHECK(showTip(Ftue::CombatRules));
    CHECK(tips::openNext());
    CHECK(!settings::tutorialSeen("combat_rules_ftue"));
    CHECK(!tips::advance() && tips::page() == 1);
    tips::back();
    CHECK(tips::page() == 0);
    CHECK(!tips::advance() && !tips::advance() && tips::page() == 2);
    CHECK(showTip(Ftue::CombatRules));  // open: the watcher calling every frame is a no-op
    CHECK(!tips::openNext());
    CHECK(tips::advance());
    CHECK(settings::tutorialSeen("combat_rules_ftue"));
    CHECK(!showTip(Ftue::CombatRules));
  }

  {  // the question on 出发: no = every tip off until 重置教程
    settings::reset();
    tips::clear();
    CHECK(ui::tipAskTutorials());
    CHECK(tips::openNext() && tips::current() == Ftue::AcceptTutorials);
    tips::answer(false);
    CHECK(tips::current() == Ftue::None);
    CHECK(!settings::state().tutorialsEnabled);
    CHECK(!ui::tipAskTutorials());
    CHECK(!showTip(Ftue::MapSelect));
    CHECK(!showTip(Ftue::Shuffle));
    settings::resetTutorials();
    CHECK(ui::tipAskTutorials());
    CHECK(tips::openNext());
    tips::answer(true);
    CHECK(settings::state().tutorialsEnabled);
    CHECK(!ui::tipAskTutorials());
    CHECK(showTip(Ftue::Shuffle));
    CHECK(drain() == 1);
  }

  {  // 继续 with rewards left: the tip opens instead; leaving an empty list after floor 4 retires it
    settings::reset();
    tips::clear();
    CHECK(ui::tipBlockProceed(2, 1));
    CHECK(drain() == 1);
    CHECK(!ui::tipBlockProceed(2, 1));  // seen: proceeds
    settings::reset();
    CHECK(!ui::tipBlockProceed(0, 3));
    CHECK(!settings::tutorialSeen("combat_reward_ftue"));
    CHECK(!ui::tipBlockProceed(0, 5));
    CHECK(settings::tutorialSeen("combat_reward_ftue"));
  }

  {  // tips forced off (STS_TIPS=0)
    settings::reset();
    tips::clear();
    tips::forceAllowed(0);
    CHECK(!showTip(Ftue::Potion));
    CHECK(!ui::tipBlockProceed(3, 1));
    tips::forceAllowed(1);
  }

  {  // the seen flags survive settings.sav (string and file round trip)
    settings::reset();
    tips::clear();
    showTip(Ftue::MapSelect);
    showTip(Ftue::Potion);
    drain();
    Settings back;
    CHECK(back.load(settings::state().save()));
    CHECK(back.tutorialsSeen.count("map_select_ftue") && back.tutorialsSeen.count("obtain_potion_ftue"));
    CHECK(back.tutorialsEnabled);
#ifdef _WIN32
    _mkdir("build");
    _mkdir("build/tutorials_fixtures");
#else
    mkdir("build", 0777);
    mkdir("build/tutorials_fixtures", 0777);
#endif
    const std::string path = "build/tutorials_fixtures/settings.sav";
    CHECK(settings::save(path));
    settings::reset();
    CHECK(!settings::tutorialSeen("map_select_ftue"));
    CHECK(settings::load(path));
    CHECK(settings::tutorialSeen("map_select_ftue"));
    CHECK(!showTip(Ftue::MapSelect));
    CHECK(showTip(Ftue::RestSite));
    settings::resetTutorials();
    CHECK(settings::save(path));
    settings::reset();
    settings::markTutorialSeen("map_select_ftue");
    CHECK(settings::load(path));
    CHECK(!settings::tutorialSeen("map_select_ftue"));
    remove(path.c_str());
  }

  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
