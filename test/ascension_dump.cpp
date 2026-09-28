// Prints every ported monster's HP range and move intents at an ascension level, for
// tools/ascension_check.py to compare with the C#.  ./build/ascension_dump <level>
#include <cstdio>
#include <cstdlib>
#include <set>

#include "../source/core/game.h"

using namespace sts;

int main(int argc, char** argv) {
  int level = argc > 1 ? atoi(argv[1]) : 0;
  db::init();
  Run run;
  run.start(1, "Ironclad", level);
  std::set<std::string> done;
  for (auto& encId : db::encounterIds()) {
    const Encounter* enc = db::encounter(encId);
    for (int seed = 1; seed <= 4; ++seed) {
      Combat c;
      c.run = &run;
      c.player = run.player.get();
      Rng rng(seed, "AscensionDump");
      auto monsters = enc->generate(rng);
      for (auto& m : monsters) {
        if (done.count(m->id)) continue;
        done.insert(m->id);
        Monster* mon = m.get();
        mon->combat = &c;  // Monster::asc reads the run through the combat
        printf("MONSTER %s hp %d %d\n", mon->id.c_str(), mon->minHp(), mon->maxHp());
        Creature* cr = c.createEnemy(std::move(m));
        for (auto& [id, st] : cr->monster->machine.states) {
          if (!st->isMove()) continue;
          auto* ms = static_cast<MoveState*>(st.get());
          printf("MOVE %s %s", cr->monster->id.c_str(), id.c_str());
          for (auto& in : ms->intents)
            if (in.kind == Intent::Attack) printf(" A%dx%d", in.damage, in.hits);
            else if (in.kind == Intent::Status) printf(" S%d", in.count);
          printf("\n");
        }
      }
    }
  }
  return 0;
}
