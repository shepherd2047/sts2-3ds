// The Defect's shared systems (X2.0), see char_defect.h.
#include "cards.h"
#include "char_defect.h"

namespace sts {

namespace {
template <class T> void regOrbType() { db::registerOrb(T::kId, [] { return std::unique_ptr<Orb>(new T()); }); }
}  // namespace

void registerDefect() {
  registerPowerType<FocusPower>();
  regOrbType<LightningOrb>();
  regOrbType<FrostOrb>();
  regOrbType<DarkOrb>();
  regOrbType<PlasmaOrb>();
  regOrbType<GlassOrb>();
}

}  // namespace sts
