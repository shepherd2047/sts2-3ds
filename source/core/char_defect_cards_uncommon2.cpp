// The Defect's Uncommon cards, second half (X2.3b): Overclock .. WhiteNoise in DefectCardPool
// order. Orb types and Focus are in char_defect.h. The powers the cards need (Smokestack, Storm,
// Subroutine, Synchronize, Thunder, FreePower) are defined here.
#include <algorithm>

#include "cards.h"
#include "char_defect.h"

namespace sts {

namespace {

// SmokestackPower: every Status card the owner generates into combat deals Amount Unpowered
// damage to every hittable enemy.
// AfterCardGeneratedForCombat is modeled by afterCardEnteredCombat (cmd::addGeneratedCard and
// cmd::addStatusCards both fire it; createdByPlayer is the C#'s creator == Owner).
struct SmokestackPower : Power {
  POWER_HEADER(SmokestackPower, "SMOKESTACK_POWER")
  Task<> afterCardEnteredCombat(Card* card) override {
    if (card->type == CardType::Status && card->createdByPlayer) {
      flash = 1.f;
      co_await cmd::damage(owner->combat->hittableEnemies(), Dec(amount), kUnpowered, owner, nullptr);
    }
  }
};

// StormPower: after each Power card the owner plays, channel Lightning orbs equal to the amount
// this power had when that card started to play.
struct StormPower : Power {
  POWER_HEADER(StormPower, "STORM_POWER")
  std::vector<std::pair<Card*, int>> amountsForPlayedCards;
  Task<> beforeCardPlayed(const CardPlay& p) override {
    if (ownerOf(p.card) != owner || p.card->type != CardType::Power) co_return;
    amountsForPlayedCards.push_back({p.card, amount});
  }
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (ownerOf(p.card) != owner) co_return;
    auto it = std::find_if(amountsForPlayedCards.begin(), amountsForPlayedCards.end(),
                           [&](const std::pair<Card*, int>& e) { return e.first == p.card; });
    if (it == amountsForPlayedCards.end()) co_return;
    int lightning = it->second;
    amountsForPlayedCards.erase(it);
    if (lightning > 0) {
      flash = 1.f;
      for (int i = 0; i < lightning; ++i) co_await cmd::channelOrb(*owner->combat, std::make_unique<LightningOrb>());
    }
  }
};

// SubroutinePower: after each Power card the owner plays, gain 1 energy per stack the power had
// when that card started to play.
struct SubroutinePower : Power {
  POWER_HEADER(SubroutinePower, "SUBROUTINE_POWER")
  std::vector<std::pair<Card*, int>> amountsForPlayedCards;
  Task<> beforeCardPlayed(const CardPlay& p) override {
    if (ownerOf(p.card) != owner || p.card->type != CardType::Power) co_return;
    amountsForPlayedCards.push_back({p.card, amount});
  }
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (ownerOf(p.card) != owner) co_return;
    auto it = std::find_if(amountsForPlayedCards.begin(), amountsForPlayedCards.end(),
                           [&](const std::pair<Card*, int>& e) { return e.first == p.card; });
    if (it == amountsForPlayedCards.end()) co_return;
    int energy = it->second;
    amountsForPlayedCards.erase(it);
    if (energy > 0) {
      flash = 1.f;
      for (int i = 0; i < energy; ++i) co_await cmd::gainEnergy(*owner->combat, 1);
    }
  }
};

// SynchronizePower: a TemporaryFocusPower (Focus that is removed at the end of the turn).
struct SynchronizePower : TemporaryFocusPower {
  POWER_HEADER(SynchronizePower, "SYNCHRONIZE_POWER")
};

// ThunderPower: whenever one of the owner's Lightning orbs is evoked, deal Amount Unpowered
// damage to each of the still-living creatures it hit.
struct ThunderPower : Power {
  POWER_HEADER(ThunderPower, "THUNDER_POWER")
  Task<> afterOrbEvoked(Orb* orb, const std::vector<Creature*>& targets) override {
    if (orb->id != "LightningOrb") co_return;
    std::vector<Creature*> living;
    for (Creature* c : targets) if (!c->dead()) living.push_back(c);
    flash = 1.f;
    co_await cmd::damage(living, Dec(amount), kUnpowered, owner, nullptr);
  }
};

// FreePowerPower (Synthesis): the next Amount Power cards in hand cost 0.
struct FreePowerPower : Power {
  POWER_HEADER(FreePowerPower, "FREE_POWER_POWER")
  static bool inHandOrPlay(Card* c) {
    if (!c->combat) return false;
    Pile p = c->combat->pileOf(c);
    return p == Pile::Hand || p == Pile::Play;
  }
  int modifyEnergyCostLate(Card* card, int cost) override {
    if (ownerOf(card) != owner || card->type != CardType::Power || !inHandOrPlay(card)) return cost;
    return 0;
  }
  Task<> beforeCardPlayed(const CardPlay& p) override {
    if (ownerOf(p.card) != owner || p.card->type != CardType::Power || !inHandOrPlay(p.card)) co_return;
    co_await cmd::decrement(this);
  }
};

// Overclock.cs: draw, then a Burn into the discard pile.
struct Overclock : IroncladT<Overclock> {
  CARD_HEADER(Overclock, "OVERCLOCK", 0, Skill, Uncommon, Self)
    addVar("Cards", 2);
  }
  Task<> onPlay(CardPlay&) override {
    co_await drawCards(val("Cards"));
    co_await cmd::addGeneratedCard(*combat, db::card("Burn"), Pile::Discard);
  }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

// Refract.cs: two hits, then Repeat Glass orbs.
struct Refract : IroncladT<Refract> {
  CARD_HEADER(Refract, "REFRACT", 3, Attack, Uncommon, AnyEnemy)
    addVar("Repeat", 2);
    addVar("Damage", 10);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"), 2);
    for (int i = 0; i < val("Repeat").toInt(); ++i) co_await cmd::channelOrb(*combat, std::make_unique<GlassOrb>());
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

// RocketPunch.cs: attack, draw; every Status card the owner generates makes this card cost 1
// less until it is played.
struct RocketPunch : IroncladT<RocketPunch> {
  CARD_HEADER(RocketPunch, "ROCKET_PUNCH", 2, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 13);
    addVar("Cards", 1);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await drawCards(val("Cards"));
  }
  // PORT NOTE: AfterCardGeneratedForCombat is modeled by afterCardEnteredCombat (see SmokestackPower).
  Task<> afterCardEnteredCombat(Card* card) override {
    if (!card->createdByPlayer || card->type != CardType::Status) co_return;
    addUntilPlayed(-1);
  }
  void onUpgrade() override { upgradeVar("Damage", 1); upgradeVar("Cards", 1); }
};

// Scavenge.cs: exhaust a card from hand (pick exactly 1 if any), then Energy next turn.
struct Scavenge : IroncladT<Scavenge> {
  CARD_HEADER(Scavenge, "SCAVENGE", 1, Skill, Uncommon, Self)
    addVar("Energy", 2);
  }
  Task<> onPlay(CardPlay&) override {
    std::vector<Card*> options = combat->hand;
    options.erase(std::remove(options.begin(), options.end(), this), options.end());
    auto picked = co_await cmd::selectCards(*combat, "SCAVENGE", options, 1, 1);
    if (!picked.empty() && picked[0]) co_await cmd::exhaustCard(*combat, picked[0]);
    co_await applyPower<EnergyNextTurnPower>(me(), Dec(val("Energy").toInt()), me(), this);
  }
  void onUpgrade() override { upgradeVar("Energy", 1); }
};

// Scrape.cs: attack, draw, then discard every drawn card that costs energy.
struct Scrape : IroncladT<Scrape> {
  CARD_HEADER(Scrape, "SCRAPE", 1, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 7);
    addVar("Cards", 4);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    std::vector<Card*> drawn = co_await cmd::drawCards(*combat, Dec(val("Cards").toInt()));
    std::vector<Card*> toDiscard;
    for (Card* c : drawn) if (combat->energyCost(c) != 0 || c->costsX) toDiscard.push_back(c);
    co_await cmd::discardCards(*combat, toDiscard);
  }
  void onUpgrade() override { upgradeVar("Damage", 3); upgradeVar("Cards", 1); }
};

// ShadowShield.cs: block, then a Dark orb.
struct ShadowShield : IroncladT<ShadowShield> {
  CARD_HEADER(ShadowShield, "SHADOW_SHIELD", 2, Skill, Uncommon, Self)
    addVar("Block", 11);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    co_await cmd::channelOrb(*combat, std::make_unique<DarkOrb>());
  }
  void onUpgrade() override { upgradeVar("Block", 4); }
};

// Skim.cs: draw.
struct Skim : IroncladT<Skim> {
  CARD_HEADER(Skim, "SKIM", 1, Skill, Uncommon, Self)
    addVar("Cards", 3);
  }
  Task<> onPlay(CardPlay&) override { co_await drawCards(val("Cards")); }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

// Smokestack.cs: apply SmokestackPower.
struct Smokestack : IroncladT<Smokestack> {
  CARD_HEADER(Smokestack, "SMOKESTACK", 1, Power, Uncommon, Self)
    addVar("SmokestackPower", 5);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<SmokestackPower>(me(), val("SmokestackPower"), me(), this); }
  void onUpgrade() override { upgradeVar("SmokestackPower", 2); }
};

// Storm.cs: apply StormPower.
struct Storm : IroncladT<Storm> {
  CARD_HEADER(Storm, "STORM", 1, Power, Uncommon, Self)
    addVar("StormPower", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<StormPower>(me(), val("StormPower"), me(), this); }
  void onUpgrade() override { upgradeVar("StormPower", 1); }
};

// Subroutine.cs: apply SubroutinePower (1).
struct Subroutine : IroncladT<Subroutine> {
  CARD_HEADER(Subroutine, "SUBROUTINE", 1, Power, Uncommon, Self) }
  Task<> onPlay(CardPlay&) override { co_await applyPower<SubroutinePower>(me(), 1, me(), this); }
  void onUpgrade() override { cost -= 1; }
};

// Sunder.cs: attack; if it killed the target, gain Energy.
struct Sunder : IroncladT<Sunder> {
  CARD_HEADER(Sunder, "SUNDER", 3, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 26);
    addVar("Energy", 3);
  }
  Task<> onPlay(CardPlay& p) override {
    cmd::Attack a;
    a.damagePerHit = val("Damage");
    a.hits = 1;
    a.attacker = me();
    a.source = this;
    a.single = p.target;
    co_await a.execute(*combat);
    bool killed = false;
    for (auto& hit : a.results) for (auto& r : hit) if (r.killed) killed = true;
    if (killed) co_await cmd::gainEnergy(*combat, val("Energy").toInt());
  }
  void onUpgrade() override { upgradeVar("Damage", 8); }
};

// Synchronize.cs: Focus (this turn only) = CalculationBase + CalculationExtra * number of
// distinct orb types queued.
struct Synchronize : IroncladT<Synchronize> {
  CARD_HEADER(Synchronize, "SYNCHRONIZE", 1, Skill, Uncommon, Self)
    addVar("CalculationBase", 0);
    addVar("CalculationExtra", 1);
    addVar("CalculatedFocus", 0);
    calcMultiplier = [](Card* c) {
      if (!c->combat) return 0;
      std::vector<std::string> ids;
      for (auto& o : c->combat->orbQueue)
        if (std::find(ids.begin(), ids.end(), o->id) == ids.end()) ids.push_back(o->id);
      return (int)ids.size();
    };
  }
  Task<> onPlay(CardPlay&) override {
    // CalculatedVar.Calculate = CalculationBase + CalculationExtra * multiplier
    co_await applyPower<SynchronizePower>(me(), calculatedBlock(), me(), this);
  }
  void onUpgrade() override { upgradeVar("CalculationExtra", 1); }
};

// Synthesis.cs: attack, then the next Power card played costs 0.
struct Synthesis : IroncladT<Synthesis> {
  CARD_HEADER(Synthesis, "SYNTHESIS", 2, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 14);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<FreePowerPower>(me(), 1, me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 6); }
};

// Tempest.cs: X-cost; channel X Lightning orbs (+1 when upgraded).
struct Tempest : IroncladT<Tempest> {
  CARD_HEADER(Tempest, "TEMPEST", 0, Skill, Uncommon, Self)
    costsX = true;
  }
  Task<> onPlay(CardPlay&) override {
    int numOfOrbs = xValue;
    if (upgraded()) ++numOfOrbs;
    for (int i = 0; i < numOfOrbs; ++i) co_await cmd::channelOrb(*combat, std::make_unique<LightningOrb>());
  }
};

// TeslaCoil.cs: attack, then trigger every queued Lightning orb's passive at the target (twice
// when upgraded).
struct TeslaCoil : IroncladT<TeslaCoil> {
  CARD_HEADER(TeslaCoil, "TESLA_COIL", 0, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 3);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    std::vector<Orb*> lightning;
    for (auto& o : combat->orbQueue) if (o->id == "LightningOrb") lightning.push_back(o.get());
    for (Orb* o : lightning) {
      co_await cmd::orbPassive(*combat, o, p.target);
      if (upgraded()) co_await cmd::orbPassive(*combat, o, p.target);
    }
  }
  void onUpgrade() override { upgradeVar("Damage", 1); }
};

// Thunder.cs: apply ThunderPower.
struct Thunder : IroncladT<Thunder> {
  CARD_HEADER(Thunder, "THUNDER", 1, Power, Uncommon, Self)
    addVar("ThunderPower", 8);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<ThunderPower>(me(), val("ThunderPower"), me(), this); }
  void onUpgrade() override { upgradeVar("ThunderPower", 3); }
};

// WhiteNoise.cs: Exhaust; add a random Defect Power card to the hand, free this turn.
// CardFactory.GetDistinctForCombat(pool.Where(Power), 1, CombatCardGeneration) via card_factory.h.
struct WhiteNoise : IroncladT<WhiteNoise> {
  CARD_HEADER(WhiteNoise, "WHITE_NOISE", 1, Skill, Uncommon, Self)
    keywords = kwExhaust;
  }
  Task<> onPlay(CardPlay&) override {
    auto card = oneDistinctForCombat(*combat, [](const Card& c) { return c.type == CardType::Power; });
    if (!card) co_return;
    card->setThisTurnOrUntilPlayed(0);  // SetToFreeThisTurn
    co_await cmd::addGeneratedCard(*combat, std::move(card), Pile::Hand);
  }
  void onUpgrade() override { cost -= 1; }
};

}  // namespace

void registerDefectUncommonCards2() {
  registerPowerType<SmokestackPower>();
  registerPowerType<StormPower>();
  registerPowerType<SubroutinePower>();
  registerPowerType<SynchronizePower>();
  registerPowerType<ThunderPower>();
  registerPowerType<FreePowerPower>();
  registerCardType<Overclock>();
  registerCardType<Refract>();
  registerCardType<RocketPunch>();
  registerCardType<Scavenge>();
  registerCardType<Scrape>();
  registerCardType<ShadowShield>();
  registerCardType<Skim>();
  registerCardType<Smokestack>();
  registerCardType<Storm>();
  registerCardType<Subroutine>();
  registerCardType<Sunder>();
  registerCardType<Synchronize>();
  registerCardType<Synthesis>();
  registerCardType<Tempest>();
  registerCardType<TeslaCoil>();
  registerCardType<Thunder>();
  registerCardType<WhiteNoise>();
}

}  // namespace sts
