// A1c: colorless cards 3/3 (ColorlessCardPool order): Rend .. Volley, translated from
// Models.Cards\<Name>.cs and Models.Powers\<Name>.cs. The multiplayer-only cards of this range (Rally,
// TagTeam, TheBall) are skipped; they stay in the pool list (colorless_pool.cpp) so the pool order is the
// game's. Style and helpers follow colorless_cards_a.cpp.
#include "cards.h"
#include "colorless.h"

namespace sts {

namespace {

constexpr int kMaxHandCards = 10;  // CardPile.MaxCardsInHand

// CardModel.CompareTo (model id, then upgrade level) + UnstableShuffle: List.StableShuffle(rng).
void stableShuffleCards(std::vector<Card*>& v, Rng& rng) {
  std::stable_sort(v.begin(), v.end(), [](Card* a, Card* b) {
    return a->id != b->id ? a->id < b->id : a->upgradeLevel < b->upgradeLevel;
  });
  rng.shuffle(v);
}

// The C# `power is ITemporaryPower` (same list as the Necrobinder cards' isTemporaryPower).
// PORT NOTE: this engine has no ITemporaryPower marker; TemporaryStrengthPower subclasses share the
// "TEMPORARY_" loc keys, the rest are listed by id.
bool isTemporaryPowerC(Power* p) {
  if (p->locKey.rfind("TEMPORARY_", 0) == 0) return true;
  return p->id == "ManglePower" || p->id == "EnfeeblingTouchPower" || p->id == "CrushUnderPower" ||
         p->id == "PlowPower" || p->id == "DarkShacklesPower" || p->id == "MonarchsGazeStrengthDownPower";
}

// PowerInstanceType.Instanced: a fresh power next to any existing one of the same id (cmd::applyPower stacks
// by id). PORT NOTE: Hook.ModifyPowerAmountReceived is skipped (only ever relevant to debuffs); Creature::get
// finds the first instance only.
template <class P> P* addInstancedPower(Creature* owner, int amount, Creature* applier) {
  Combat* c = owner->combat;
  if (!c || c->ending) return nullptr;
  auto p = std::make_unique<P>();
  P* raw = p.get();
  raw->owner = owner;
  raw->applier = applier;
  raw->amount = amount;
  raw->flash = 1.f;
  owner->powers.push_back(std::move(p));
  c->push({VisualEvent::PowerUp, owner, amount, raw->locKey});
  return raw;
}

// ================================================================ powers

// RollingBoulderPower.cs: Buff, Counter, Instanced. At the start of your turn deal Amount Unpowered damage
// to all enemies, then Amount grows by 5 (the DamageVar).
struct RollingBoulderPower : Power {
  POWER_HEADER(RollingBoulderPower, "ROLLING_BOULDER_POWER")
  Task<> afterPlayerTurnStart() override {
    flash = 1.f;
    co_await cmd::damage(owner->combat->hittableEnemies(), Dec(amount), kUnpowered, owner, nullptr);
    amount += 5;  // SetAmount(Amount + DynamicVars.Damage)
  }
};

// StratagemPower.cs: Buff, Counter. After the draw pile is shuffled, choose Amount cards from it into the hand.
struct StratagemPower : Power {
  POWER_HEADER(StratagemPower, "STRATAGEM_POWER")
  Task<> afterShuffle() override {
    Combat& c = *owner->combat;
    flash = 1.f;
    int n = std::min(amount, (int)c.draw.size());
    if (n <= 0) co_return;
    auto picked = co_await cmd::selectCards(c, "STRATAGEM_POWER", c.draw, n, n);
    for (Card* k : picked) co_await cmd::moveCard(c, k, Pile::Hand);
  }
};

// TheBombPower.cs: Buff, Counter, Instanced. Amount = turns left; at the end of your turn it counts down, and
// at 1 it deals `damage` Unpowered damage to all enemies and goes away.
struct TheBombPower : Power {
  POWER_HEADER(TheBombPower, "THE_BOMB_POWER")
  int damage = 40;  // DynamicVars.Damage, set by TheBomb (SetDamage)
  Task<> beforeSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner)) co_return;
    if (amount > 1) {
      co_await cmd::decrement(this);
      co_return;
    }
    flash = 1.f;
    co_await cmd::damage(owner->combat->hittableEnemies(), Dec(damage), kUnpowered, owner, nullptr);
    co_await cmd::removePower(this);
  }
};

// TheGambitPower.cs: Debuff, Single. Taking unblocked powered attack damage kills the owner.
struct TheGambitPower : Power {
  POWER_HEADER(TheGambitPower, "THE_GAMBIT_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  StackType stackType() const override { return StackType::Single; }
  Task<> afterDamageReceived(Creature* target, const DamageResult& result, int props, Creature*, Card*) override {
    if (target == owner && isPoweredAttack(props) && result.unblocked > 0) {
      Creature* o = owner;
      co_await cmd::removePower(this);
      co_await cmd::kill({o});
    }
  }
};

// ================================================================ cards

// Rend.cs: 1 cost, Attack, AnyEnemy, Rare. Damage 10 (+2) + 5 (+3) per non-temporary debuff on the target.
struct Rend : IroncladT<Rend> {
  CARD_HEADER(Rend, "REND", 1, Attack, Rare, AnyEnemy)
    addVar("CalculationBase", 10);
    addVar("ExtraDamage", 5);
    addVar("CalculatedDamage", 0);
    calcMultiplier = [](Card* c) { return static_cast<Rend*>(c)->debuffsOn(static_cast<Rend*>(c)->lastTarget); };
  }
  // PORT NOTE: Card::calcMultiplier has no target; onPlay sets lastTarget (the C# lambda gets the target,
  // and counts 0 for the card-text preview with none).
  Creature* lastTarget = nullptr;
  static int debuffsOn(Creature* target) {
    int n = 0;
    if (target)
      for (auto& p : target->powers)
        if (p->typeForAmount(Dec(p->amount)) == PowerType::Debuff && !isTemporaryPowerC(p.get())) ++n;
    return n;
  }
  Task<> onPlay(CardPlay& p) override {
    lastTarget = p.target;
    co_await attackCalculated(p.target);
    lastTarget = nullptr;
  }
  void onUpgrade() override {
    upgradeVar("ExtraDamage", 3);
    upgradeVar("CalculationBase", 2);
  }
};

// Restlessness.cs: 0 cost, Skill, Self, Retain, Uncommon. If it is the only card in the hand: draw Cards
// (2, +1), gain Energy (2, +1).
struct Restlessness : IroncladT<Restlessness> {
  CARD_HEADER(Restlessness, "RESTLESSNESS", 0, Skill, Uncommon, Self)
    keywords = kwRetain;
    addVar("Cards", 2);
    addVar("Energy", 2);
  }
  bool onlyCardInHand() const {
    for (Card* k : combat->hand) if (k != this) return false;
    return true;
  }
  Task<> onPlay(CardPlay&) override {
    if (!onlyCardInHand()) co_return;
    for (int i = 0; i < val("Cards").toInt(); ++i) co_await drawCards(1);
    co_await cmd::gainEnergy(*combat, val("Energy").toInt());
  }
  void onUpgrade() override {
    upgradeVar("Cards", 1);
    upgradeVar("Energy", 1);
  }
};

// RollingBoulder.cs: 3 cost, Power, Self, Rare. RollingBoulderPower 5 (+5), an instanced power.
struct RollingBoulder : IroncladT<RollingBoulder> {
  CARD_HEADER(RollingBoulder, "ROLLING_BOULDER", 3, Power, Rare, Self)
    addVar("RollingBoulderPower", 5);
    addVar("IncrementAmount", 5);
  }
  Task<> onPlay(CardPlay&) override {
    addInstancedPower<RollingBoulderPower>(me(), val("RollingBoulderPower").toInt(), me());
    co_return;
  }
  void onUpgrade() override { upgradeVar("RollingBoulderPower", 5); }
};

// Salvo.cs: 1 cost, Attack, AnyEnemy, Uncommon. Damage 12 (+4), then RetainHandPower (1).
struct Salvo : IroncladT<Salvo> {
  CARD_HEADER(Salvo, "SALVO", 1, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 12);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<RetainHandPower>(me(), 1, me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 4); }
};

// Scrawl.cs: 1 cost, Skill, Self, Exhaust, Rare. Draw until the hand is full. Upgrade: Retain.
struct Scrawl : IroncladT<Scrawl> {
  CARD_HEADER(Scrawl, "SCRAWL", 1, Skill, Rare, Self)
    keywords = kwExhaust;
  }
  Task<> onPlay(CardPlay&) override {
    int n = kMaxHandCards - (int)combat->hand.size();
    if (n > 0) co_await drawCards(n);
  }
  void onUpgrade() override { keywords |= kwRetain; }
};

// SecretTechnique.cs: 0 cost, Skill, Self, Exhaust, Rare. Choose a Skill from the draw pile into the hand.
// Upgrade: no Exhaust.
struct SecretTechnique : IroncladT<SecretTechnique> {
  CARD_HEADER(SecretTechnique, "SECRET_TECHNIQUE", 0, Skill, Rare, Self)
    keywords = kwExhaust;
  }
  Task<> onPlay(CardPlay&) override {
    std::vector<Card*> options;
    for (Card* k : combat->draw) if (k->type == CardType::Skill) options.push_back(k);
    auto picked = co_await cmd::selectCards(*combat, "SECRET_TECHNIQUE", options, 1, 1);
    if (!picked.empty()) co_await cmd::moveCard(*combat, picked[0], Pile::Hand);
  }
  void onUpgrade() override { keywords &= ~kwExhaust; }
};

// SecretWeapon.cs: as SecretTechnique, for Attacks.
struct SecretWeapon : IroncladT<SecretWeapon> {
  CARD_HEADER(SecretWeapon, "SECRET_WEAPON", 0, Skill, Rare, Self)
    keywords = kwExhaust;
  }
  Task<> onPlay(CardPlay&) override {
    std::vector<Card*> options;
    for (Card* k : combat->draw) if (k->type == CardType::Attack) options.push_back(k);
    auto picked = co_await cmd::selectCards(*combat, "SECRET_WEAPON", options, 1, 1);
    if (!picked.empty()) co_await cmd::moveCard(*combat, picked[0], Pile::Hand);
  }
  void onUpgrade() override { keywords &= ~kwExhaust; }
};

// SeekerStrike.cs: 1 cost, Attack (Strike), AnyEnemy, Uncommon. Damage 9 (+3), then choose 1 of Cards (3)
// random cards of the draw pile into the hand.
struct SeekerStrike : IroncladT<SeekerStrike> {
  CARD_HEADER(SeekerStrike, "SEEKER_STRIKE", 1, Attack, Uncommon, AnyEnemy)
    tags = tagStrike;
    addVar("Damage", 9);
    addVar("Cards", 3);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    std::vector<Card*> options = combat->draw;
    stableShuffleCards(options, combat->rng("CombatCardSelection"));
    if ((int)options.size() > val("Cards").toInt()) options.resize((size_t)val("Cards").toInt());
    auto picked = co_await cmd::selectCards(*combat, "SEEKER_STRIKE", options, 1, 1);
    if (!picked.empty()) co_await cmd::moveCard(*combat, picked[0], Pile::Hand);
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

// Shockwave.cs: 2 cost, Skill, AllEnemies, Exhaust, Uncommon. Power 3 (+2) Weak and Vulnerable to each enemy.
struct Shockwave : IroncladT<Shockwave> {
  CARD_HEADER(Shockwave, "SHOCKWAVE", 2, Skill, Uncommon, AllEnemies)
    keywords = kwExhaust;
    addVar("Power", 3);
  }
  Task<> onPlay(CardPlay&) override {
    int amount = val("Power").toInt();
    for (Creature* e : combat->hittableEnemies()) {
      co_await applyPower<WeakPower>(e, amount, me(), this);
      co_await applyPower<VulnerablePower>(e, amount, me(), this);
    }
  }
  void onUpgrade() override { upgradeVar("Power", 2); }
};

// Splash.cs: 1 cost, Skill, Self, Rare. Choose 1 of 3 distinct Attacks from the other characters' pools
// (skippable, upgraded when this is); it is free this turn and goes to the hand.
// PORT NOTE: every playable character's pool counts as unlocked; the multiplayer-only cards are left out
// (db::characterCards).
struct Splash : IroncladT<Splash> {
  CARD_HEADER(Splash, "SPLASH", 1, Skill, Rare, Self)
  }
  Task<> onPlay(CardPlay&) override {
    Combat& c = *combat;
    std::vector<std::string> ids;
    for (auto& ch : db::allCharacters()) {  // UnlockState.CharacterCardPools order
      if (ch == c.run->characterId && db::allCharacters().size() > 1) continue;
      auto part = db::characterCards(ch, [](const Card& k) { return k.type == CardType::Attack; });
      ids.insert(ids.end(), part.begin(), part.end());
    }
    auto options = distinctForCombat(c, std::move(ids), 3);
    if (upgraded())
      for (auto& k : options) cmd::upgradeCard(k.get());
    co_await chooseGeneratedToHand(c, std::move(options), true);
  }
};

// Stratagem.cs: 1 cost, Power, Self, Uncommon. StratagemPower (1).
struct Stratagem : IroncladT<Stratagem> {
  CARD_HEADER(Stratagem, "STRATAGEM", 1, Power, Uncommon, Self)
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<StratagemPower>(me(), 1, me(), this); }
  void onUpgrade() override { cost -= 1; }
};

// TheBomb.cs: 2 cost, Skill, Self, Uncommon. TheBombPower: Turns 3, BombDamage 40 (+10); instanced.
struct TheBomb : IroncladT<TheBomb> {
  CARD_HEADER(TheBomb, "THE_BOMB", 2, Skill, Uncommon, Self)
    addVar("Turns", 3);
    addVar("BombDamage", 40);
  }
  Task<> onPlay(CardPlay&) override {
    TheBombPower* p = addInstancedPower<TheBombPower>(me(), val("Turns").toInt(), me());
    if (p) p->damage = val("BombDamage").toInt();
    co_return;
  }
  void onUpgrade() override { upgradeVar("BombDamage", 10); }
};

// TheGambit.cs: 0 cost, Skill, Self, Rare. Block 50 (+25), then TheGambitPower (1).
struct TheGambit : IroncladT<TheGambit> {
  CARD_HEADER(TheGambit, "THE_GAMBIT", 0, Skill, Rare, Self)
    addVar("Block", 50);
  }
  bool gainsBlock() const override { return true; }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    co_await applyPower<TheGambitPower>(me(), 1, me(), this);
  }
  void onUpgrade() override { upgradeVar("Block", 25); }
};

// ThinkingAhead.cs: 0 cost, Skill, Self, Exhaust, Uncommon. Draw Cards (2), then put a card from the hand on
// top of the draw pile. Upgrade: no Exhaust.
struct ThinkingAhead : IroncladT<ThinkingAhead> {
  CARD_HEADER(ThinkingAhead, "THINKING_AHEAD", 0, Skill, Uncommon, Self)
    keywords = kwExhaust;
    addVar("Cards", 2);
  }
  Task<> onPlay(CardPlay&) override {
    co_await drawCards(val("Cards"));
    std::vector<Card*> options = combat->hand;
    auto picked = co_await cmd::selectCards(*combat, "THINKING_AHEAD", options, 1, 1);
    if (!picked.empty()) co_await cmd::moveCard(*combat, picked[0], Pile::Draw, true);
  }
  void onUpgrade() override { keywords &= ~kwExhaust; }
};

// ThrummingHatchet.cs: 1 cost, Attack, AnyEnemy, Uncommon. Damage 11 (+3). If it was played last player turn
// it returns to the hand before the next hand draw (as Bolas).
struct ThrummingHatchet : IroncladT<ThrummingHatchet> {
  CARD_HEADER(ThrummingHatchet, "THRUMMING_HATCHET", 1, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 11);
  }
  const Card* playedBy = nullptr;  // guards against a clone() inheriting the original's history
  int playedRound = -100;
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    playedBy = this;
    playedRound = combat->roundNumber;
  }
  Task<> beforeHandDraw() override {
    if (playedBy == this && playedRound == combat->roundNumber - 1 && combat->pileOf(this) != Pile::Hand)
      co_await cmd::moveCard(*combat, this, Pile::Hand);
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

// UltimateDefend.cs: 1 cost, Skill (Defend), Self, Uncommon. Block 11 (+4).
struct UltimateDefend : IroncladT<UltimateDefend> {
  CARD_HEADER(UltimateDefend, "ULTIMATE_DEFEND", 1, Skill, Uncommon, Self)
    tags = tagDefend;
    addVar("Block", 11);
  }
  bool gainsBlock() const override { return true; }
  Task<> onPlay(CardPlay&) override { co_await block(val("Block")); }
  void onUpgrade() override { upgradeVar("Block", 4); }
};

// UltimateStrike.cs: 1 cost, Attack (Strike), AnyEnemy, Uncommon. Damage 14 (+6).
struct UltimateStrike : IroncladT<UltimateStrike> {
  CARD_HEADER(UltimateStrike, "ULTIMATE_STRIKE", 1, Attack, Uncommon, AnyEnemy)
    tags = tagStrike;
    addVar("Damage", 14);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
  void onUpgrade() override { upgradeVar("Damage", 6); }
};

// Volley.cs: X cost, Attack, RandomEnemy, Uncommon. X hits of Damage 10 (+4) at random enemies.
struct Volley : IroncladT<Volley> {
  CARD_HEADER(Volley, "VOLLEY", 0, Attack, Uncommon, RandomEnemy)
    costsX = true;
    addVar("Damage", 10);
  }
  Task<> onPlay(CardPlay&) override { co_await attackRandom(val("Damage"), xValue); }
  void onUpgrade() override { upgradeVar("Damage", 4); }
};

}  // namespace

void registerColorlessC() {
  registerPowerType<RollingBoulderPower>();
  registerPowerType<StratagemPower>();
  registerPowerType<TheBombPower>();
  registerPowerType<TheGambitPower>();

  registerCardType<Rend>();
  // Rally: multiplayer only.
  registerCardType<Restlessness>();
  registerCardType<RollingBoulder>();
  registerCardType<Salvo>();
  registerCardType<Scrawl>();
  registerCardType<SecretTechnique>();
  registerCardType<SecretWeapon>();
  registerCardType<SeekerStrike>();
  registerCardType<Shockwave>();
  registerCardType<Splash>();
  registerCardType<Stratagem>();
  // TagTeam: multiplayer only.
  // TheBall: multiplayer only.
  registerCardType<TheBomb>();
  registerCardType<TheGambit>();
  registerCardType<ThinkingAhead>();
  registerCardType<ThrummingHatchet>();
  registerCardType<UltimateDefend>();
  registerCardType<UltimateStrike>();
  registerCardType<Volley>();
}

}  // namespace sts
