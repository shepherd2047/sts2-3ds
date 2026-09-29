// The Defect's Rare cards (X2.4): AdaptiveStrike .. Voltaic in DefectCardPool order. Orb types and
// Focus are in char_defect.h. The powers the cards need (Coolant, ConsumingShadow, CreativeAi,
// EchoForm, MachineLearning, SignalBoost, Spinner, TrashToTreasure, HyperbeamFocusDown) are defined
// here; BufferPower is the shared one (potions.cpp) and is applied by id. None of these cards
// creates a token card.
#include <algorithm>
#include <set>

#include "cards.h"
#include "char_defect.h"

namespace sts {

namespace {

// CoolantPower: at the start of the owner's turn, gain Unpowered block equal to the number of
// distinct orb types in the queue times Amount.
struct CoolantPower : Power {
  POWER_HEADER(CoolantPower, "COOLANT_POWER")
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner)) co_return;
    flash = 1.f;
    std::set<std::string> kinds;
    for (auto& o : owner->combat->orbQueue) kinds.insert(o->id);
    co_await cmd::gainBlock(owner, Dec((int)kinds.size() * amount), kUnpowered, nullptr);
  }
};

// ConsumingShadowPower: at the end of the owner's turn, evoke the last orb Amount times (only if
// the queue was not empty when the turn ended).
struct ConsumingShadowPower : Power {
  POWER_HEADER(ConsumingShadowPower, "CONSUMING_SHADOW_POWER")
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    Combat* c = owner->combat;
    if (!contains(participants, owner) || c->orbQueue.empty()) co_return;
    for (int i = 0; i < amount; ++i) {
      co_await cmd::evokeLastOrb(*c);
      co_await wait(0.25);
    }
  }
};

// CreativeAiPower: before the hand is drawn, add Amount random Power cards of the character's
// pool to the hand (CardFactory.GetDistinctForCombat with CombatCardGeneration, count 1 each).
struct CreativeAiPower : Power {
  POWER_HEADER(CreativeAiPower, "CREATIVE_AI_POWER")
  Task<> beforeHandDraw() override {
    Combat* c = owner->combat;
    for (int i = 0; i < amount; ++i) {
      // FilterForCombat: CanBeGeneratedInCombat, not Basic/Ancient; TakeRandom(1) = shuffle, take first.
      auto ids = db::characterCards(c->run->characterId, [](const Card& k) {
        return k.type == CardType::Power && k.rarity != Rarity::Basic && k.rarity != Rarity::Ancient;
      });
      c->rng("CombatCardGeneration").shuffle(ids);
      if (ids.empty()) continue;
      auto card = db::card(ids[0]);
      if (card) co_await cmd::addGeneratedCard(*c, std::move(card), Pile::Hand);
    }
  }
};

// EchoFormPower: the first Amount cards played each turn are played one extra time.
// The C# counts CardPlayStarted entries (first in series) of this turn recorded before the
// current card; Combat::cardsPlayedThisTurn is bumped before ModifyCardPlayCount, so the current
// card is subtracted.
struct EchoFormPower : Power {
  POWER_HEADER(EchoFormPower, "ECHO_FORM_POWER")
  int modifyCardPlayCount(Card* card, Creature*, int playCount) override {
    if (ownerOf(card) != owner) return playCount;
    int earlier = owner->combat->cardsPlayedThisTurn - 1;
    if (earlier >= amount) return playCount;
    return playCount + 1;
  }
  Task<> afterModifyingCardPlayCount(Card*) override {
    flash = 1.f;
    co_return;
  }
};

// MachineLearningPower: draw Amount extra cards at the start of each turn.
struct MachineLearningPower : Power {
  POWER_HEADER(MachineLearningPower, "MACHINE_LEARNING_POWER")
  Dec modifyHandDraw(Dec count) override { return count + Dec(amount); }
};

// SignalBoostPower: the next Amount Power cards are played one extra time.
struct SignalBoostPower : Power {
  POWER_HEADER(SignalBoostPower, "SIGNAL_BOOST_POWER")
  int modifyCardPlayCount(Card* card, Creature*, int playCount) override {
    if (ownerOf(card) != owner || card->type != CardType::Power) return playCount;
    return playCount + 1;
  }
  Task<> afterModifyingCardPlayCount(Card*) override { co_await cmd::decrement(this); }
};

// SpinnerPower: at the start of each turn (after the energy reset), channel Amount Glass orbs.
struct SpinnerPower : Power {
  POWER_HEADER(SpinnerPower, "SPINNER_POWER")
  Task<> afterEnergyReset() override {
    for (int i = 0; i < amount; ++i) co_await cmd::channelOrb(*owner->combat, std::make_unique<GlassOrb>());
  }
};

// TrashToTreasurePower: whenever the owner's own effect generates a Status card into combat,
// channel Amount random orbs (CombatOrbGeneration stream).
// AfterCardGeneratedForCombat is modeled by afterCardEnteredCombat (see SmokestackPower);
// createdByPlayer is the C#'s `creator.Creature == Owner`.
struct TrashToTreasurePower : Power {
  POWER_HEADER(TrashToTreasurePower, "TRASH_TO_TREASURE_POWER")
  Task<> afterCardEnteredCombat(Card* card) override {
    if (card->type != CardType::Status || !card->createdByPlayer) co_return;
    flash = 1.f;
    for (int i = 0; i < amount; ++i)
      co_await cmd::channelOrb(*owner->combat, db::randomOrb(owner->combat->rng("CombatOrbGeneration")));
  }
};

// HyperbeamFocusDownPower: TemporaryFocusPower with IsPositive == false (Focus lost until the
// end of the owner's turn).
struct HyperbeamFocusDownPower : TemporaryFocusPower {
  POWER_HEADER(HyperbeamFocusDownPower, "HYPERBEAM_FOCUS_DOWN_POWER")
  bool isPositive() const override { return false; }
};

// AdaptiveStrike.cs: hit, then a copy that costs 0 for the rest of the combat goes to the discard pile.
struct AdaptiveStrike : IroncladT<AdaptiveStrike> {
  CARD_HEADER(AdaptiveStrike, "ADAPTIVE_STRIKE", 2, Attack, Rare, AnyEnemy)
    tags = tagStrike;
    addVar("Damage", 18);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    auto copy = clone();
    copy->setThisCombat(0);
    co_await cmd::addGeneratedCard(*combat, std::move(copy), Pile::Discard);
  }
  void onUpgrade() override { upgradeVar("Damage", 5); }
};

// AllForOne.cs: hit, then return every 0-cost (non-X) Attack/Skill/Power from the discard pile to the hand.
struct AllForOne : IroncladT<AllForOne> {
  CARD_HEADER(AllForOne, "ALL_FOR_ONE", 2, Attack, Rare, AnyEnemy)
    addVar("Damage", 10);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    std::vector<Card*> picked;
    for (Card* k : combat->discard) {
      bool zero = combat->energyCost(k) == 0 && !k->costsX;
      if (zero && (k->type == CardType::Attack || k->type == CardType::Skill || k->type == CardType::Power)) picked.push_back(k);
    }
    for (Card* k : picked) co_await cmd::moveCard(*combat, k, Pile::Hand);
  }
  void onUpgrade() override { upgradeVar("Damage", 4); }
};

// Buffer.cs: BufferPower (shared, potions.cpp) applied by id.
struct Buffer : IroncladT<Buffer> {
  CARD_HEADER(Buffer, "BUFFER", 2, Power, Rare, Self)
    addVar("BufferPower", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await cmd::applyPower(db::power("BufferPower"), me(), val("BufferPower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("BufferPower", 1); }
};

// ConsumingShadow.cs: channel Repeat Dark orbs, then the power.
struct ConsumingShadow : IroncladT<ConsumingShadow> {
  CARD_HEADER(ConsumingShadow, "CONSUMING_SHADOW", 2, Power, Rare, Self)
    addVar("Repeat", 2);
    addVar("ConsumingShadowPower", 1);
  }
  Task<> onPlay(CardPlay&) override {
    for (int i = 0; i < val("Repeat").toInt(); ++i) co_await cmd::channelOrb(*combat, std::make_unique<DarkOrb>());
    co_await applyPower<ConsumingShadowPower>(me(), val("ConsumingShadowPower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Repeat", 1); }
};

// Coolant.cs
struct Coolant : IroncladT<Coolant> {
  CARD_HEADER(Coolant, "COOLANT", 1, Power, Rare, Self)
    addVar("CoolantPower", 2);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<CoolantPower>(me(), val("CoolantPower"), me(), this); }
  void onUpgrade() override { upgradeVar("CoolantPower", 1); }
};

// CreativeAi.cs
struct CreativeAi : IroncladT<CreativeAi> {
  CARD_HEADER(CreativeAi, "CREATIVE_AI", 3, Power, Rare, Self)
    addVar("CreativeAi", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<CreativeAiPower>(me(), val("CreativeAi"), me(), this); }
  void onUpgrade() override { cost -= 1; }
};

// Defragment.cs
struct Defragment : IroncladT<Defragment> {
  CARD_HEADER(Defragment, "DEFRAGMENT", 1, Power, Rare, Self)
    addVar("FocusPower", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<FocusPower>(me(), val("FocusPower"), me(), this); }
  void onUpgrade() override { upgradeVar("FocusPower", 1); }
};

// EchoForm.cs: Ethereal until upgraded.
struct EchoForm : IroncladT<EchoForm> {
  CARD_HEADER(EchoForm, "ECHO_FORM", 3, Power, Rare, Self)
    keywords = kwEthereal;
    addVar("EchoForm", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<EchoFormPower>(me(), val("EchoForm"), me(), this); }
  void onUpgrade() override { removeKeyword(kwEthereal); }
};

// FlakCannon.cs: hits = Status cards not yet exhausted (CalculationBase 0 + 1 each); the hit count
// is fixed first, then those Statuses are exhausted, then random-target hits.
struct FlakCannon : IroncladT<FlakCannon> {
  CARD_HEADER(FlakCannon, "FLAK_CANNON", 2, Attack, Rare, RandomEnemy)
    addVar("Damage", 8);
    addVar("CalculationBase", 0);
    addVar("CalculationExtra", 1);
    addVar("CalculatedHits", 0);
    calcMultiplier = [](Card* c) { return c->combat ? (int)statuses(c->combat).size() : 0; };
  }
  static std::vector<Card*> statuses(Combat* combat) {
    std::vector<Card*> out;
    for (Card* k : combat->allCards())
      if (k->type == CardType::Status && combat->pileOf(k) != Pile::Exhaust) out.push_back(k);
    return out;
  }
  Task<> onPlay(CardPlay&) override {
    std::vector<Card*> list = statuses(combat);
    int hits = calculatedBlock().toInt();
    for (Card* k : list) co_await cmd::exhaustCard(*combat, k);
    if (hits > 0) co_await attackRandom(val("Damage"), hits);
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

// GeneticAlgorithm.cs: Block starts at 1 and grows by Increase each play, on this copy and on its
// deck version. PORT NOTE: the C# keeps CurrentBlock / IncreasedBlock as [SavedProperty]s; here the
// running Block DynVar is the state (saved with the card's vars; IncreasedBlock = Block - 1), and
// the AfterDowngraded refresh is not needed (no downgrade in this engine).
struct GeneticAlgorithm : IroncladT<GeneticAlgorithm> {
  CARD_HEADER(GeneticAlgorithm, "GENETIC_ALGORITHM", 1, Skill, Rare, Self)
    keywords = kwExhaust;
    addVar("Block", 1);
    addVar("Increase", 3);
  }
  static void buffFromPlay(Card* c, int extra) {
    if (auto* v = c->var("Block")) v->base = v->base + Dec(extra);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    int inc = val("Increase").toInt();
    buffFromPlay(this, inc);
    if (deckVersion.p && deckVersion.p->id == "GeneticAlgorithm") buffFromPlay(deckVersion.p, inc);
  }
  void onUpgrade() override { upgradeVar("Increase", 1); }
};

// HelixDrill.cs: hits = energy spent this turn (CalculationBase 0 + 1 each); a card being played
// does not count its own cost.
struct HelixDrill : IroncladT<HelixDrill> {
  CARD_HEADER(HelixDrill, "HELIX_DRILL", 0, Attack, Rare, AnyEnemy)
    addVar("Damage", 3);
    addVar("CalculationBase", 0);
    addVar("CalculationExtra", 1);
    addVar("CalculatedHits", 0);
    calcMultiplier = [](Card* c) {
      Combat* cb = c->combat;
      if (!cb || cb->pileOf(c) == Pile::None) return 0;
      int n = cb->energySpentThisTurn;
      if (cb->pileOf(c) == Pile::Play) n -= cb->energyCost(c);
      return n;
    };
  }
  Task<> onPlay(CardPlay& p) override {
    int hits = calculatedBlock().toInt();
    if (hits > 0) co_await attack(p.target, val("Damage"), hits);
  }
  void onUpgrade() override { upgradeVar("Damage", 2); }
};

// Hyperbeam.cs: hit all, then lose Focus until end of turn.
struct Hyperbeam : IroncladT<Hyperbeam> {
  CARD_HEADER(Hyperbeam, "HYPERBEAM", 2, Attack, Rare, AllEnemies)
    addVar("Damage", 24);
    addVar("FocusPower", 3);
  }
  Task<> onPlay(CardPlay&) override {
    co_await attackAll(val("Damage"));
    co_await applyPower<HyperbeamFocusDownPower>(me(), val("FocusPower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 6); }
};

// IceLance.cs: hit, then channel Repeat Frost orbs.
struct IceLance : IroncladT<IceLance> {
  CARD_HEADER(IceLance, "ICE_LANCE", 3, Attack, Rare, AnyEnemy)
    addVar("Damage", 19);
    addVar("Repeat", 3);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    for (int i = 0; i < val("Repeat").toInt(); ++i) co_await cmd::channelOrb(*combat, std::make_unique<FrostOrb>());
  }
  void onUpgrade() override { upgradeVar("Damage", 5); }
};

// MachineLearning.cs: Innate when upgraded.
struct MachineLearning : IroncladT<MachineLearning> {
  CARD_HEADER(MachineLearning, "MACHINE_LEARNING", 1, Power, Rare, Self)
    addVar("Cards", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<MachineLearningPower>(me(), val("Cards"), me(), this); }
  void onUpgrade() override { addKeyword(kwInnate); }
};

// MeteorStrike.cs: hit, then channel 3 Plasma orbs.
struct MeteorStrike : IroncladT<MeteorStrike> {
  CARD_HEADER(MeteorStrike, "METEOR_STRIKE", 5, Attack, Rare, AnyEnemy)
    tags = tagStrike;
    addVar("Damage", 24);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    for (int i = 0; i < 3; ++i) co_await cmd::channelOrb(*combat, std::make_unique<PlasmaOrb>());
  }
  void onUpgrade() override { upgradeVar("Damage", 6); }
};

// Modded.cs: orb slots, draw, then this card costs 1 more for the rest of the combat.
struct Modded : IroncladT<Modded> {
  CARD_HEADER(Modded, "MODDED", 0, Skill, Rare, Self)
    addVar("Repeat", 1);
    addVar("Cards", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await cmd::addOrbSlots(*combat, val("Repeat").toInt());
    co_await drawCards(val("Cards"));
    addThisCombat(1);
  }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

// MultiCast.cs: X (+1 upgraded) evokes of the front orb; only the last one dequeues it.
struct MultiCast : IroncladT<MultiCast> {
  CARD_HEADER(MultiCast, "MULTI_CAST", 0, Skill, Rare, Self)
    costsX = true;
  }
  Task<> onPlay(CardPlay&) override {
    int evokeCount = xValue;
    if (upgraded()) ++evokeCount;
    for (int i = 0; i < evokeCount; ++i) {
      co_await cmd::evokeNextOrb(*combat, i == evokeCount - 1);
      co_await wait(0.25);
    }
  }
};

// Rainbow.cs: channel Lightning, Frost, Dark; Exhaust until upgraded.
struct Rainbow : IroncladT<Rainbow> {
  CARD_HEADER(Rainbow, "RAINBOW", 2, Skill, Rare, Self)
    keywords = kwExhaust;
  }
  Task<> onPlay(CardPlay&) override {
    co_await cmd::channelOrb(*combat, std::make_unique<LightningOrb>());
    co_await cmd::channelOrb(*combat, std::make_unique<FrostOrb>());
    co_await cmd::channelOrb(*combat, std::make_unique<DarkOrb>());
  }
  void onUpgrade() override { removeKeyword(kwExhaust); }
};

// Reboot.cs: the hand goes to the bottom of the draw pile, shuffle, draw.
struct Reboot : IroncladT<Reboot> {
  CARD_HEADER(Reboot, "REBOOT", 0, Skill, Rare, Self)
    keywords = kwExhaust;
    addVar("Cards", 4);
  }
  Task<> onPlay(CardPlay&) override {
    std::vector<Card*> handCopy = combat->hand;
    for (Card* k : handCopy) co_await cmd::moveCard(*combat, k, Pile::Draw, false);
    co_await cmd::shuffle(*combat);
    co_await drawCards(val("Cards"));
  }
  void onUpgrade() override { upgradeVar("Cards", 2); }
};

// Shatter.cs: hit all, then evoke every orb (front orb twice each, dequeuing on the second).
struct Shatter : IroncladT<Shatter> {
  CARD_HEADER(Shatter, "SHATTER", 1, Attack, Rare, AllEnemies)
    keywords = kwExhaust;
    addVar("Damage", 7);
  }
  Task<> onPlay(CardPlay&) override {
    co_await attackAll(val("Damage"));
    int orbCount = (int)combat->orbQueue.size();
    for (int i = 0; i < orbCount; ++i) {
      co_await cmd::evokeNextOrb(*combat, false);
      co_await cmd::evokeNextOrb(*combat);
    }
  }
  void onUpgrade() override { upgradeVar("Damage", 4); }
};

// SignalBoost.cs
struct SignalBoost : IroncladT<SignalBoost> {
  CARD_HEADER(SignalBoost, "SIGNAL_BOOST", 1, Skill, Rare, Self)
    keywords = kwExhaust;
    addVar("SignalBoostPower", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<SignalBoostPower>(me(), val("SignalBoostPower"), me(), this); }
  void onUpgrade() override { cost -= 1; }
};

// Spinner.cs: an upgraded Spinner channels a Glass orb first (no OnUpgrade in the C#).
struct Spinner : IroncladT<Spinner> {
  CARD_HEADER(Spinner, "SPINNER", 1, Power, Rare, Self)
    addVar("SpinnerPower", 1);
  }
  Task<> onPlay(CardPlay&) override {
    if (upgraded()) co_await cmd::channelOrb(*combat, std::make_unique<GlassOrb>());
    co_await applyPower<SpinnerPower>(me(), val("SpinnerPower"), me(), this);
  }
};

// Supercritical.cs
struct Supercritical : IroncladT<Supercritical> {
  CARD_HEADER(Supercritical, "SUPERCRITICAL", 0, Skill, Rare, Self)
    keywords = kwExhaust;
    addVar("Energy", 4);
  }
  Task<> onPlay(CardPlay&) override { co_await cmd::gainEnergy(*combat, val("Energy").toInt()); }
  void onUpgrade() override { upgradeVar("Energy", 2); }
};

// TrashToTreasure.cs
struct TrashToTreasure : IroncladT<TrashToTreasure> {
  CARD_HEADER(TrashToTreasure, "TRASH_TO_TREASURE", 1, Power, Rare, Self) }
  Task<> onPlay(CardPlay&) override { co_await applyPower<TrashToTreasurePower>(me(), Dec(1), me(), this); }
  void onUpgrade() override { cost -= 1; }
};

// Voltaic.cs: channel one Lightning orb per Lightning orb channeled so far this combat
// (CalculationBase 0 + 1 each); Exhaust until upgraded. The count is read before channeling.
struct Voltaic : IroncladT<Voltaic> {
  CARD_HEADER(Voltaic, "VOLTAIC", 3, Skill, Rare, Self)
    keywords = kwExhaust;
    addVar("CalculationBase", 0);
    addVar("CalculationExtra", 1);
    addVar("CalculatedChannels", 0);
    calcMultiplier = [](Card* c) { return c->combat ? c->combat->lightningOrbsChanneled : 0; };
  }
  Task<> onPlay(CardPlay&) override {
    int n = calculatedBlock().toInt();
    for (int i = 0; i < n; ++i) co_await cmd::channelOrb(*combat, std::make_unique<LightningOrb>());
  }
  void onUpgrade() override { removeKeyword(kwExhaust); }
};

}  // namespace

void registerDefectRareCards() {
  registerPowerType<CoolantPower>();
  registerPowerType<ConsumingShadowPower>();
  registerPowerType<CreativeAiPower>();
  registerPowerType<EchoFormPower>();
  registerPowerType<MachineLearningPower>();
  registerPowerType<SignalBoostPower>();
  registerPowerType<SpinnerPower>();
  registerPowerType<TrashToTreasurePower>();
  registerPowerType<HyperbeamFocusDownPower>();
  registerCardType<AdaptiveStrike>();
  registerCardType<AllForOne>();
  registerCardType<Buffer>();
  registerCardType<ConsumingShadow>();
  registerCardType<Coolant>();
  registerCardType<CreativeAi>();
  registerCardType<Defragment>();
  registerCardType<EchoForm>();
  registerCardType<FlakCannon>();
  registerCardType<GeneticAlgorithm>();
  registerCardType<HelixDrill>();
  registerCardType<Hyperbeam>();
  registerCardType<IceLance>();
  registerCardType<MachineLearning>();
  registerCardType<MeteorStrike>();
  registerCardType<Modded>();
  registerCardType<MultiCast>();
  registerCardType<Rainbow>();
  registerCardType<Reboot>();
  registerCardType<Shatter>();
  registerCardType<SignalBoost>();
  registerCardType<Spinner>();
  registerCardType<Supercritical>();
  registerCardType<TrashToTreasure>();
  registerCardType<Voltaic>();
}

}  // namespace sts
