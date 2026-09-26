// Act 1 (Overgrowth) events, translated from MegaCrit.Sts2.Core.Models.Events.
#include "cards.h"

namespace sts {

namespace {
template <class E> void reg() { db::registerEvent(E::kId, [] { return std::unique_ptr<Event>(new E()); }); }
#define EVENT_HEADER(Name, Key) \
  static constexpr const char* kId = #Name; \
  Name() { id = #Name; locKey = Key; }
}  // namespace

// AromaOfChaos.cs: transform a card into a random one, or upgrade one.
struct AromaOfChaos : Event {
  EVENT_HEADER(AromaOfChaos, "AROMA_OF_CHAOS")
  void calculateVars() override { setStr("AromaPrinciple", "characters.IRONCLAD.aromaPrinciple"); }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "LET_GO", [this] { return letGo(); }),
            option("INITIAL", "MAINTAIN_CONTROL", [this] { return maintainControl(); })};
  }
  Task<> letGo() {
    auto picked = co_await run->selectFromDeck("card_selection.TO_TRANSFORM", [](Card*) { return true; }, 1);
    if (!picked.empty()) run->transformCard(picked[0], run->randomTransformFor(picked[0], rng()));
    setFinished("LET_GO");
  }
  Task<> maintainControl() {
    auto picked = co_await run->selectFromDeck("card_selection.TO_UPGRADE", [](Card* c) { return c->upgradable(); }, 1, false, true);
    if (!picked.empty()) picked[0]->upgrade();
    setFinished("MAINTAIN_CONTROL");
  }
};

void registerAct1Events() {
  reg<AromaOfChaos>();
}

}  // namespace sts
