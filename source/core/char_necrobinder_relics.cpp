// The Necrobinder's relic pool (X4.1), the 7 non-starter relics of NecrobinderRelicPool
// (BoundPhylactery, the starting relic, lives in char_necrobinder.cpp next to summonOsty).
// Translated from MegaCrit.Sts2.Core.Models.Relics.
#include "cards.h"
#include "char_necrobinder.h"

namespace sts {

namespace {

// BigHat.cs: at the start of turn 1, add up to 2 distinct Ethereal cards from the character's
// pool to hand (CardFactory.GetDistinctForCombat, CombatCardGeneration).
struct BigHat : Relic {
  RELIC_HEADER(BigHat, "BIG_HAT", Rare)
    addVar("Cards", 2);
  }
  Task<> afterSideTurnStart(Side side, const std::vector<Creature*>& participants) override {
    if (side != Side::Player || !combat || combat->turnNumber > 1 || !contains(participants, owner())) co_return;
    auto pool = db::characterPool(run->characterId, [](const Card& c) { return c.has(kwEthereal); });
    if (pool.empty()) co_return;
    doFlash();
    for (auto& card : distinctForCombat(*combat, pool, val("Cards").toInt()))
      co_await cmd::addGeneratedCard(*combat, std::move(card), Pile::Hand);
  }
};

// BoneFlute.cs: whenever Osty lands an attack, gain 2 block.
struct BoneFlute : Relic {
  RELIC_HEADER(BoneFlute, "BONE_FLUTE", Common)
    addVar("Block", 2);
  }
  Task<> afterAttack(const cmd::Attack& a) override {
    Creature* attacker = a.attacker;
    if (!combat || !attacker || attacker != combat->osty || attacker->petOwner != owner()) co_return;
    doFlash();
    co_await cmd::gainBlock(owner(), val("Block"), kUnpowered, nullptr);
  }
};

// BookRepairKnife.cs: whenever a Doom kill takes more than just the owner, heal 3 per such
// creature whose powers all let the death count (ShouldOwnerDeathTriggerFatal).
struct BookRepairKnife : Relic {
  RELIC_HEADER(BookRepairKnife, "BOOK_REPAIR_KNIFE", Uncommon)
    addVar("Heal", 3);
  }
  Task<> afterDiedToDoom(const std::vector<Creature*>& creatures) override {
    int n = 0;
    for (Creature* cr : creatures) if (cr != owner() && cr->deathIsFatal()) ++n;
    if (n == 0) co_return;
    doFlash();
    co_await cmd::heal(owner(), val("Heal") * Dec(n));
  }
};

// Bookmark.cs: after the hand is flushed, reduce a random retained (non-X, cost > 0) card's cost
// by 1 until played.
struct Bookmark : Relic {
  RELIC_HEADER(Bookmark, "BOOKMARK", Rare)
  }
  Task<> afterFlush(const std::vector<Card*>&, const std::vector<Card*>& retained) override {
    if (!combat) co_return;
    std::vector<Card*> eligible;
    for (Card* c : retained) if (!c->costsX && c->costWithLocalMods() > 0) eligible.push_back(c);
    if (eligible.empty()) co_return;
    doFlash();
    combat->rng("CombatCardSelection").nextItem(eligible)->addUntilPlayed(-1);
  }
};

// FuneraryMask.cs: on turn 1, before the hand is drawn, add 3 Soul cards to a random spot in the
// draw pile.
struct FuneraryMask : Relic {
  RELIC_HEADER(FuneraryMask, "FUNERARY_MASK", Uncommon)
    addVar("Cards", 3);
  }
  Task<> beforeHandDraw() override {
    if (!combat || combat->turnNumber != 1) co_return;
    doFlash();
    for (int i = 0; i < val("Cards").toInt(); ++i) co_await addSoulToDrawPileRandom(*combat);
  }
};

// IvoryTile.cs: whenever a card costing 3 or more energy is played, gain 1 energy.
struct IvoryTile : Relic {
  RELIC_HEADER(IvoryTile, "IVORY_TILE", Rare)
    addVar("Energy", 1);
    addVar("EnergyThreshold", 3);
  }
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (!combat || ownerOf(p.card) != owner() || p.energySpent < val("EnergyThreshold").toInt()) co_return;
    doFlash();
    co_await cmd::gainEnergy(*combat, val("Energy").toInt());
  }
};

// UndyingSigil.cs: a powered attack from a creature doomed to die this turn (CurrentHp <= its
// own Doom amount) against the owner does half damage. The comment on the C# class says Doom
// itself checks for this relic, but the actual code we can see is only this damage reduction;
// no such Doom-trigger-timing check exists elsewhere in the decompile, so only this is ported.
struct UndyingSigil : Relic {
  RELIC_HEADER(UndyingSigil, "UNDYING_SIGIL", Shop)
    addVar("DamageDecrease", Dec::lit(0.5));
  }
  Dec modifyDamageMultiplicative(Creature* target, Dec, int props, Creature* dealer, Card*) override {
    if (!dealer || !isPoweredAttack(props) || target != owner() || dealer == owner()) return 1;
    if (dealer->hp > dealer->powerAmount<DoomPower>()) return 1;
    return val("DamageDecrease");
  }
};

template <class R> void reg() { db::registerRelic(R::kId, [] { return std::unique_ptr<Relic>(new R()); }); }

}  // namespace

void registerNecrobinderRelics() {
  reg<BigHat>();
  reg<BoneFlute>();
  reg<BookRepairKnife>();
  reg<Bookmark>();
  reg<FuneraryMask>();
  reg<IvoryTile>();
  reg<UndyingSigil>();
}

}  // namespace sts
