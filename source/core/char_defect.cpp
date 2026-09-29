// The Defect's shared systems (X2.0), see char_defect.h. The starter deck (X2.1) lives here too,
// next to the orb types its two orb cards use.
#include "cards.h"
#include "char_defect.h"

namespace sts {

void registerDefectRelics();      // char_defect_relics.cpp
void registerDefectPotions();     // char_defect_relics.cpp
void registerDefectCommonCards(); // char_defect_cards.cpp
void registerDefectUncommonCards1(); // char_defect_cards_uncommon1.cpp

namespace {
template <class T> void regOrbType() { db::registerOrb(T::kId, [] { return std::unique_ptr<Orb>(new T()); }); }

// StrikeDefect.cs / DefendDefect.cs: identical numbers to the Ironclad's Strike/Defend, only the
// portrait/vfx/color differ (cosmetic, UI-only).
struct StrikeDefect : IroncladT<StrikeDefect> {
  CARD_HEADER(StrikeDefect, "STRIKE_DEFECT", 1, Attack, Basic, AnyEnemy)
    tags = tagStrike;
    addVar("Damage", 6);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

struct DefendDefect : IroncladT<DefendDefect> {
  CARD_HEADER(DefendDefect, "DEFEND_DEFECT", 1, Skill, Basic, Self)
    tags = tagDefend;
    addVar("Block", 5);
  }
  Task<> onPlay(CardPlay&) override { co_await block(val("Block")); }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

// Zap.cs: channel a Lightning orb. PORT NOTE: drops the C#'s TriggerAnim("Cast", ...) call, a
// pure animation cue with no gameplay effect (no anim system in this port).
struct Zap : IroncladT<Zap> {
  CARD_HEADER(Zap, "ZAP", 1, Skill, Basic, Self) }
  Task<> onPlay(CardPlay&) override { co_await cmd::channelOrb(*combat, std::make_unique<LightningOrb>()); }
  void onUpgrade() override { cost -= 1; }
};

// Dualcast.cs: evoke the front orb twice without dequeuing it. PORT NOTE: drops the C#'s
// TriggerAnim + CustomScaledWait (animation-only); the `Orbs.Count > 0` guard is kept for
// fidelity even though evokeNextOrb is already a no-op on an empty queue.
struct Dualcast : IroncladT<Dualcast> {
  CARD_HEADER(Dualcast, "DUALCAST", 1, Skill, Basic, Self) }
  Task<> onPlay(CardPlay&) override {
    if (!combat->orbQueue.empty()) {
      co_await cmd::evokeNextOrb(*combat, false);
      co_await cmd::evokeNextOrb(*combat);
    }
  }
  void onUpgrade() override { cost -= 1; }
};

}  // namespace

void registerDefect() {
  registerPowerType<FocusPower>();
  regOrbType<LightningOrb>();
  regOrbType<FrostOrb>();
  regOrbType<DarkOrb>();
  regOrbType<PlasmaOrb>();
  regOrbType<GlassOrb>();
  registerCardType<StrikeDefect>();
  registerCardType<DefendDefect>();
  registerCardType<Zap>();
  registerCardType<Dualcast>();
  registerDefectCommonCards();
  registerDefectUncommonCards1();
  registerDefectRelics();
  registerDefectPotions();
}

}  // namespace sts
