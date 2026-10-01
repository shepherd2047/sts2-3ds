// A1b: colorless cards 2/3 (ColorlessCardPool order): HandOfGreed .. Purity, translated from
// Models.Cards\<Name>.cs and Models.Powers\<Name>.cs. The multiplayer-only cards of this range (HuddleUp,
// Intercept, Knockdown, Lift, Mimic) are skipped; they stay in the pool list (colorless_pool.cpp). Style
// and helpers follow colorless_cards_a.cpp.
#include "cards.h"
#include "colorless.h"

namespace sts {

namespace {

// ================================================================ powers

// MayhemPower.cs: at the start of each turn's play phase, auto-play Amount cards from the top of the draw pile.
struct MayhemPower : Power {
  POWER_HEADER(MayhemPower, "MAYHEM_POWER")
  Task<> afterAutoPrePlayPhaseEntered() override { co_await cmd::autoPlayFromDrawPile(*owner->combat, amount, false); }
};

// NoBlockPower.cs: the owner's card blocks are 0; ticks down after each enemy turn.
struct NoBlockPower : Power {
  POWER_HEADER(NoBlockPower, "NO_BLOCK_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  Task<> afterSideTurnEnd(Side side, const std::vector<Creature*>&) override {
    if (side == Side::Enemy) co_await cmd::decrement(this);
  }
  Dec modifyBlockMultiplicative(Creature* target, Dec, int props, Card* src) override {
    if (target != owner || !isPoweredBlock(props) || !src) return 1;
    return 0;
  }
};

// NostalgiaPower.cs: the first Amount Attacks / Skills played each turn go on top of the draw pile instead
// of the discard pile.
// Counts the Attack / Skill CardPlaysStarted entries of the turn (the card being resolved has none yet).
// PORT NOTE: the AfterModifyingCardPlayResultLocation flash is done in the modify hook.
struct NostalgiaPower : Power {
  POWER_HEADER(NostalgiaPower, "NOSTALGIA_POWER")
  int playsThisTurn() const {
    Combat* c = owner->combat;
    return c->history.countThisTurn(*c, CombatHistoryEntry::CardPlayStarted, [](const CombatHistoryEntry& e) {
      return e.card->type == CardType::Attack || e.card->type == CardType::Skill;
    });
  }
  Pile modifyCardPlayResultLocation(Card* card, bool, Pile pile) override {
    if (ownerOf(card) != owner) return pile;
    if (card->type != CardType::Attack && card->type != CardType::Skill) return pile;
    if (pile != Pile::Discard) return pile;
    if (playsThisTurn() >= amount) return pile;
    flash = 1.f;
    return Pile::Draw;
  }
};

// PanachePower.cs: after every 5th card played (not counting the Panache card itself), deal Amount
// unpowered damage to all enemies; the counter resets at the end of the turn.
// PORT NOTE: the C# power is Instanced (one per Panache played, each with its own counter and
// alreadyApplied flag); here one power stacks the damage and shares the counter, and a second Panache
// played is counted toward it.
struct PanachePower : Power {
  POWER_HEADER(PanachePower, "PANACHE_POWER")
  bool alreadyApplied = false;
  int cardsLeft = 5;
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (ownerOf(p.card) != owner) co_return;
    if (alreadyApplied) {
      --cardsLeft;
      if (cardsLeft <= 0) {
        flash = 1.f;
        co_await cmd::damage(owner->combat->hittableEnemies(), Dec(amount), kUnpowered, owner, nullptr);
        cardsLeft = 5;
      }
    }
    alreadyApplied = true;
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) cardsLeft = 5;
    co_return;
  }
};

// PrepTimePower.cs: at the start of your turn, gain Amount Vigor.
struct PrepTimePower : Power {
  POWER_HEADER(PrepTimePower, "PREP_TIME_POWER")
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await applyPower<VigorPower>(owner, amount, owner, nullptr);
  }
};

// ================================================================ cards

// HandOfGreed.cs: 2 cost, Attack, AnyEnemy, Rare. Damage 20 (+5); a fatal hit gains Gold 20 (+5).
struct HandOfGreed : IroncladT<HandOfGreed> {
  CARD_HEADER(HandOfGreed, "HAND_OF_GREED", 2, Attack, Rare, AnyEnemy)
    addVar("Damage", 20);
    addVar("Gold", 20);
  }
  bool canBeGeneratedInCombat() const override { return false; }
  Task<> onPlay(CardPlay& p) override {
    bool fatal = p.target && p.target->deathIsFatal();  // checked before the hit, as in C#
    cmd::Attack a;
    a.damagePerHit = val("Damage");
    a.attacker = me();
    a.source = this;
    a.single = p.target;
    co_await a.execute(*combat);
    bool killed = false;
    for (auto& hit : a.results)
      for (auto& r : hit) if (r.killed) killed = true;
    if (fatal && killed) co_await combat->run->gainGold(val("Gold").toInt());
  }
  void onUpgrade() override {
    upgradeVar("Damage", 5);
    upgradeVar("Gold", 5);
  }
};

// HiddenGem.cs: 1 cost, Skill, Self, Rare. A random playable card of the draw pile (preferring
// Attack / Skill / Power) without a replay gains Replay (2, +1) replays.
struct HiddenGem : IroncladT<HiddenGem> {
  CARD_HEADER(HiddenGem, "HIDDEN_GEM", 1, Skill, Rare, Self)
    addVar("Replay", 2);
  }
  bool canBeGeneratedInCombat() const override { return false; }
  Task<> onPlay(CardPlay&) override {
    if (combat->draw.empty()) co_return;
    std::vector<Card*> all, preferred;
    for (Card* k : combat->draw)
      if (!k->has(kwUnplayable) && k->type != CardType::Curse && k->enchantedReplayCount() < 1) all.push_back(k);
    for (Card* k : all)
      if (k->type == CardType::Attack || k->type == CardType::Skill || k->type == CardType::Power) preferred.push_back(k);
    Card* pick = combat->rng("CombatCardSelection").nextItem(preferred.empty() ? all : preferred);
    if (pick) pick->baseReplayCount += val("Replay").toInt();
    co_return;
  }
  void onUpgrade() override { upgradeVar("Replay", 1); }
};

// Impatience.cs: 0 cost, Skill, Self, Uncommon. Draw Cards (2, +1) if the hand has no Attack.
struct Impatience : IroncladT<Impatience> {
  CARD_HEADER(Impatience, "IMPATIENCE", 0, Skill, Uncommon, Self)
    addVar("Cards", 2);
  }
  Task<> onPlay(CardPlay&) override {
    for (Card* k : combat->hand) if (k->type == CardType::Attack) co_return;
    co_await drawCards(val("Cards"));
  }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

// JackOfAllTrades.cs: 0 cost, Skill, Self, Exhaust, Uncommon. Add Cards (1, +1) distinct random colorless
// cards (other than itself) to the hand.
struct JackOfAllTrades : IroncladT<JackOfAllTrades> {
  CARD_HEADER(JackOfAllTrades, "JACK_OF_ALL_TRADES", 0, Skill, Uncommon, Self)
    keywords = kwExhaust;
    addVar("Cards", 1);
  }
  Task<> onPlay(CardPlay&) override {
    auto ids = db::colorlessCards([](const Card& k) { return k.id != "JackOfAllTrades"; });
    auto made = distinctForCombat(*combat, std::move(ids), val("Cards").toInt());
    for (auto& k : made) co_await cmd::addGeneratedCard(*combat, std::move(k), Pile::Hand);
  }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

// Jackpot.cs: 3 cost, Attack, AnyEnemy, Rare. Damage 25 (+5), then add Cards (3) random 0-cost non-X cards
// of your pool to the hand (upgraded when this is).
struct Jackpot : IroncladT<Jackpot> {
  CARD_HEADER(Jackpot, "JACKPOT", 3, Attack, Rare, AnyEnemy)
    addVar("Damage", 25);
    addVar("Cards", 3);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    Combat& c = *combat;
    auto made = randomForCombat(c, db::characterPool(c.run->characterId, [](const Card& k) { return k.canonicalCost == 0 && !k.costsX; }),
                                val("Cards").toInt());
    for (auto& k : made) {
      if (upgraded()) cmd::upgradeCard(k.get());
      co_await cmd::addGeneratedCard(c, std::move(k), Pile::Hand);
    }
  }
  void onUpgrade() override { upgradeVar("Damage", 5); }
};

// MasterOfStrategy.cs: 0 cost, Skill, Self, Exhaust, Rare. Draw Cards (3, +1).
struct MasterOfStrategy : IroncladT<MasterOfStrategy> {
  CARD_HEADER(MasterOfStrategy, "MASTER_OF_STRATEGY", 0, Skill, Rare, Self)
    keywords = kwExhaust;
    addVar("Cards", 3);
  }
  Task<> onPlay(CardPlay&) override { co_await drawCards(val("Cards")); }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

// Mayhem.cs: 2 cost, Power, Self, Rare. MayhemPower (1).
struct Mayhem : IroncladT<Mayhem> {
  CARD_HEADER(Mayhem, "MAYHEM", 2, Power, Rare, Self)
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<MayhemPower>(me(), 1, me(), this); }
  void onUpgrade() override { cost -= 1; }
};

// MindBlast.cs: 1 cost, Attack, AnyEnemy, Innate, Uncommon. Damage = 1 per card in the draw pile.
struct MindBlast : IroncladT<MindBlast> {
  CARD_HEADER(MindBlast, "MIND_BLAST", 1, Attack, Uncommon, AnyEnemy)
    keywords = kwInnate;
    addVar("CalculationBase", 0);
    addVar("ExtraDamage", 1);
    addVar("CalculatedDamage", 0);
    calcMultiplier = [](Card* c) { return c->combat ? (int)c->combat->draw.size() : 0; };
  }
  Task<> onPlay(CardPlay& p) override { co_await attackCalculated(p.target); }
  void onUpgrade() override { cost -= 1; }
};

// Nostalgia.cs: 1 cost, Power, Self, Rare. NostalgiaPower (1).
struct Nostalgia : IroncladT<Nostalgia> {
  CARD_HEADER(Nostalgia, "NOSTALGIA", 1, Power, Rare, Self)
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<NostalgiaPower>(me(), 1, me(), this); }
  void onUpgrade() override { cost -= 1; }
};

// Omnislice.cs: 0 cost, Attack, AnyEnemy, Uncommon. Damage 8 (+3), then every other hittable enemy takes
// the damage dealt (total + overkill) as unpowered damage from you.
// PORT NOTE: the C# groups the hits in one AttackContext (attack-hook bookkeeping); not modelled.
struct Omnislice : IroncladT<Omnislice> {
  CARD_HEADER(Omnislice, "OMNISLICE", 0, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 8);
  }
  Task<> onPlay(CardPlay& p) override {
    auto list = co_await cmd::damage(p.target, val("Damage"), kMove, me(), this);
    if (list.empty()) co_return;
    DamageResult first = list[0];
    int dealt = first.blocked + first.unblocked + first.overkill;  // TotalDamage + OverkillDamage
    std::vector<Creature*> others;
    for (Creature* e : combat->hittableEnemies()) if (e != p.target) others.push_back(e);
    if (others.empty()) co_return;
    co_await cmd::damage(others, Dec(dealt), kUnpowered | kMove, me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

// Panache.cs: 0 cost, Power, Self, Uncommon. PanachePower PanacheDamage 10 (+4).
struct Panache : IroncladT<Panache> {
  CARD_HEADER(Panache, "PANACHE", 0, Power, Uncommon, Self)
    addVar("PanacheDamage", 10);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<PanachePower>(me(), val("PanacheDamage"), me(), this); }
  void onUpgrade() override { upgradeVar("PanacheDamage", 4); }
};

// PanicButton.cs: 0 cost, Skill, Self, Exhaust, Uncommon. Block 30 (+10), then NoBlockPower for Turns (2).
struct PanicButton : IroncladT<PanicButton> {
  CARD_HEADER(PanicButton, "PANIC_BUTTON", 0, Skill, Uncommon, Self)
    keywords = kwExhaust;
    addVar("Block", 30);
    addVar("Turns", 2);
  }
  bool gainsBlock() const override { return true; }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    co_await applyPower<NoBlockPower>(me(), val("Turns"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Block", 10); }
};

// PrepTime.cs: 1 cost, Power, Self, Uncommon. PrepTimePower 4 (+2).
struct PrepTime : IroncladT<PrepTime> {
  CARD_HEADER(PrepTime, "PREP_TIME", 1, Power, Uncommon, Self)
    addVar("PrepTimePower", 4);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<PrepTimePower>(me(), val("PrepTimePower"), me(), this); }
  void onUpgrade() override { upgradeVar("PrepTimePower", 2); }
};

// Production.cs: 0 cost, Skill, Self, Exhaust, Uncommon. Gain Energy 2 (+1).
struct Production : IroncladT<Production> {
  CARD_HEADER(Production, "PRODUCTION", 0, Skill, Uncommon, Self)
    keywords = kwExhaust;
    addVar("Energy", 2);
  }
  Task<> onPlay(CardPlay&) override { co_await cmd::gainEnergy(*combat, val("Energy").toInt()); }
  void onUpgrade() override { upgradeVar("Energy", 1); }
};

// Prolong.cs: 0 cost, Skill, Self, Exhaust, Uncommon. BlockNextTurnPower equal to your current Block.
// Upgrade: no Exhaust.
struct Prolong : IroncladT<Prolong> {
  CARD_HEADER(Prolong, "PROLONG", 0, Skill, Uncommon, Self)
    keywords = kwExhaust;
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<BlockNextTurnPower>(me(), Dec(me()->block), me(), this); }
  void onUpgrade() override { keywords &= ~kwExhaust; }
};

// Prowess.cs: 1 cost, Power, Self, Uncommon. Strength 1 (+1) and Dexterity 1 (+1).
struct Prowess : IroncladT<Prowess> {
  CARD_HEADER(Prowess, "PROWESS", 1, Power, Uncommon, Self)
    addVar("StrengthPower", 1);
    addVar("DexterityPower", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<StrengthPower>(me(), val("StrengthPower"), me(), this);
    co_await applyPower<DexterityPower>(me(), val("DexterityPower"), me(), this);
  }
  void onUpgrade() override {
    upgradeVar("DexterityPower", 1);
    upgradeVar("StrengthPower", 1);
  }
};

// Purity.cs: 0 cost, Skill, Self, Uncommon. Exhaust up to Cards (3, +2) cards from the hand.
struct Purity : IroncladT<Purity> {
  CARD_HEADER(Purity, "PURITY", 0, Skill, Uncommon, Self)
    keywords = kwRetain | kwExhaust;
    addVar("Cards", 3);
  }
  Task<> onPlay(CardPlay&) override {
    auto picked = co_await cmd::selectCards(*combat, "card_selection.TO_EXHAUST", combat->hand, 0, val("Cards").toInt());
    for (Card* k : picked) co_await cmd::exhaustCard(*combat, k);
  }
  void onUpgrade() override { upgradeVar("Cards", 2); }
};

}  // namespace

void registerColorlessB() {
  registerPowerType<MayhemPower>();
  registerPowerType<NoBlockPower>();
  registerPowerType<NostalgiaPower>();
  registerPowerType<PanachePower>();
  registerPowerType<PrepTimePower>();

  registerCardType<HandOfGreed>();
  registerCardType<HiddenGem>();
  // HuddleUp: multiplayer only.
  registerCardType<Impatience>();
  // Intercept: multiplayer only.
  registerCardType<JackOfAllTrades>();
  registerCardType<Jackpot>();
  // Knockdown, Lift: multiplayer only.
  registerCardType<MasterOfStrategy>();
  registerCardType<Mayhem>();
  // Mimic: multiplayer only.
  registerCardType<MindBlast>();
  registerCardType<Nostalgia>();
  registerCardType<Omnislice>();
  registerCardType<Panache>();
  registerCardType<PanicButton>();
  registerCardType<PrepTime>();
  registerCardType<Production>();
  registerCardType<Prolong>();
  registerCardType<Prowess>();
  registerCardType<Purity>();
}

}  // namespace sts
