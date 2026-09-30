// The Silent's Rare cards (X1.4), translated from MegaCrit.Sts2.Core.Models.Cards.* / Models.Powers.*
// of the same name (SilentCardPool.cs, CardRarity.Rare). Shared systems (Poison, Shiv, Fan of Knives)
// are in char_silent.h (X1.0). Tokens: the only token these cards create is the Shiv (char_silent.cpp).
#include <map>

#include "cards.h"
#include "char_silent.h"

namespace sts {

namespace {

// ---------------------------------------------------------------- powers

// AfterimagePower.cs: block for every card played, using the amount the power had when the card started
// (so a card that started before the power existed, or Afterimage itself, gives nothing).
struct AfterimagePower : Power {
  POWER_HEADER(AfterimagePower, "AFTERIMAGE_POWER")
  std::map<Card*, int> amountsForPlayedCards;
  Task<> beforeCardPlayed(const CardPlay& p) override {
    amountsForPlayedCards.emplace(p.card, amount);
    co_return;
  }
  Task<> afterCardPlayed(const CardPlay& p) override {
    auto it = amountsForPlayedCards.find(p.card);
    if (it == amountsForPlayedCards.end()) co_return;
    int value = it->second;
    amountsForPlayedCards.erase(it);
    if (value > 0) co_await cmd::gainBlock(owner, Dec(value), kUnpowered, nullptr, true);
  }
};

// BurstPower.cs: the next `amount` Skills are played twice; expires at the end of the turn.
struct BurstPower : Power {
  POWER_HEADER(BurstPower, "BURST_POWER")
  int modifyCardPlayCount(Card* card, Creature*, int count) override {
    if (card->type != CardType::Skill) return count;
    return count + 1;
  }
  Task<> afterModifyingCardPlayCount(Card*) override { co_await cmd::decrement(this); }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await cmd::removePower(this);
  }
};

// CorrosiveWavePower.cs: every card drawn poisons every hittable enemy; expires at the end of the turn.
struct CorrosiveWavePower : Power {
  POWER_HEADER(CorrosiveWavePower, "CORROSIVE_WAVE_POWER")
  Task<> afterCardDrawn(Card*, bool) override {
    flash = 1.f;
    for (Creature* e : owner->combat->hittableEnemies()) co_await applyPower<PoisonPower>(e, Dec(amount), owner, nullptr);
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await cmd::removePower(this);
  }
};

// EnvenomPower.cs: unblocked powered attack damage dealt by the owner adds Poison.
struct EnvenomPower : Power {
  POWER_HEADER(EnvenomPower, "ENVENOM_POWER")
  Task<> afterDamageGiven(Creature* dealer, const DamageResult& r, int props, Creature* target, Card*) override {
    if (dealer == owner && isPoweredAttack(props) && r.unblocked > 0)
      co_await applyPower<PoisonPower>(target, Dec(amount), owner, nullptr);
  }
};

// MasterPlannerPower.cs: every Skill the owner plays gains Sly.
struct MasterPlannerPower : Power {
  POWER_HEADER(MasterPlannerPower, "MASTER_PLANNER_POWER")
  StackType stackType() const override { return StackType::Single; }
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (p.card->type != CardType::Skill) co_return;
    p.card->addKeyword(kwSly);  // CardCmd.ApplyKeyword
  }
};

// NightmarePower.cs: at the next hand draw, `amount` copies of the chosen card join the hand, then the power goes.
// PORT NOTE: the C# power is Instanced (one power per Nightmare played, each with its own card); here a single
// power keeps one (card, copies) entry per play, in application order. `amount` is the sum of the entries.
struct NightmarePower : Power {
  POWER_HEADER(NightmarePower, "NIGHTMARE_POWER")
  struct Entry { std::unique_ptr<Card> card; int copies; };
  std::vector<Entry> entries;
  Task<> beforeHandDraw() override {
    for (Entry& e : entries) {
      for (int i = 0; i < e.copies; ++i) co_await cmd::addGeneratedCard(*owner->combat, e.card->clone(), Pile::Hand);
    }
    entries.clear();
    co_await cmd::removePower(this);
  }
  // SetSelectedCard: a clone of the chosen card, without its affliction.
  void setSelectedCard(Card* c, int copies) {
    auto copy = c->clone();
    cmd::clearAffliction(copy.get());
    entries.push_back({std::move(copy), copies});
  }
};

// SerpentFormPower.cs: after every card played, Unpowered damage to a random hittable enemy (amount at the start of the play).
struct SerpentFormPower : Power {
  POWER_HEADER(SerpentFormPower, "SERPENT_FORM_POWER")
  std::map<Card*, int> amountsForPlayedCards;
  Task<> beforeCardPlayed(const CardPlay& p) override {
    amountsForPlayedCards.emplace(p.card, amount);
    co_return;
  }
  Task<> afterCardPlayed(const CardPlay& p) override {
    auto it = amountsForPlayedCards.find(p.card);
    if (it == amountsForPlayedCards.end()) co_return;
    int damage = it->second;
    amountsForPlayedCards.erase(it);
    if (damage <= 0) co_return;
    co_await wait(0.1);
    Combat* c = owner->combat;
    Creature* t = c->rng("CombatTargets").nextItem(c->hittableEnemies());
    if (t) co_await cmd::damage(t, Dec(damage), kUnpowered, owner, nullptr);
  }
};

// DoubleDamagePower.cs (used by ShadowStepPower): powered attack cards of the owner deal double; ticks down each turn.
struct DoubleDamagePower : Power {
  POWER_HEADER(DoubleDamagePower, "DOUBLE_DAMAGE_POWER")
  Dec modifyDamageMultiplicative(Creature*, Dec, int props, Creature* dealer, Card* card) override {
    if (dealer != owner || !isPoweredAttack(props) || !card) return 1;
    return 2;
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await cmd::decrement(this);
  }
};

// ShadowStepPower.cs: at the start of the owner's next turn, Double Damage for `amount` turns, then it goes.
struct ShadowStepPower : Power {
  POWER_HEADER(ShadowStepPower, "SHADOW_STEP_POWER")
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner)) co_return;
    co_await applyPower<DoubleDamagePower>(owner, Dec(amount), owner, nullptr);
    co_await cmd::removePower(this);
  }
};

// ShadowmeldPower.cs: block gained is multiplied by 2^amount; expires at the end of the turn.
struct ShadowmeldPower : Power {
  POWER_HEADER(ShadowmeldPower, "SHADOWMELD_POWER")
  Dec modifyBlockMultiplicative(Creature* target, Dec, int, Card*) override {
    if (target != owner) return 1;
    Dec m = 1;
    for (int i = 0; i < amount; ++i) m = m * Dec(2);
    return m;
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await cmd::removePower(this);
  }
};

// TheHuntPower.cs: only a marker that The Hunt succeeded (the reward is added by the card).
struct TheHuntPower : Power {
  POWER_HEADER(TheHuntPower, "THE_HUNT_POWER")
};

// ToolsOfTheTradePower.cs: draw `amount` more each turn, then discard `amount` cards.
struct ToolsOfTheTradePower : Power {
  POWER_HEADER(ToolsOfTheTradePower, "TOOLS_OF_THE_TRADE_POWER")
  Dec modifyHandDraw(Dec count) override { return count + Dec(amount); }
  Task<> afterPlayerTurnStart() override {
    Combat* c = owner->combat;
    int n = std::min(amount, (int)c->hand.size());
    if (n <= 0) co_return;
    auto picked = co_await cmd::selectCards(*c, "card_selection.TO_DISCARD", c->hand, n, n);
    if (!picked.empty()) co_await cmd::discardCards(*c, picked);
  }
};

// TrackingPower.cs: the owner's powered attack cards deal +amount% to Weak enemies.
// PORT NOTE: the C# also counts the owner's pets as dealers; a pet's attacks are not card damage here.
struct TrackingPower : Power {
  POWER_HEADER(TrackingPower, "TRACKING_POWER")
  Dec modifyDamageMultiplicative(Creature* target, Dec, int props, Creature* dealer, Card* card) override {
    if (!isPoweredAttack(props) || !card || dealer != owner) return 1;
    if (!target || !target->get<WeakPower>()) return 1;
    return Dec(100 + amount) / Dec(100);
  }
};

// WellLaidPlansPower.cs: the hand is not discarded at the end of the turn (ShouldFlush false for the owner).
struct WellLaidPlansPower : Power {
  POWER_HEADER(WellLaidPlansPower, "WELL_LAID_PLANS_POWER")
  StackType stackType() const override { return StackType::Single; }
  bool shouldFlush() override { return false; }
};

// ---------------------------------------------------------------- cards

// Abrasive.cs: Sly power, Dexterity then Thorns.
struct Abrasive : IroncladT<Abrasive> {
  CARD_HEADER(Abrasive, "ABRASIVE", 3, Power, Rare, Self)
    keywords = kwSly;
    addVar("ThornsPower", 4);
    addVar("DexterityPower", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<DexterityPower>(me(), val("DexterityPower"), me(), this);
    // ThornsPower lives in content_act2a.cpp (file-local), so it is applied by id.
    if (auto pw = db::power("ThornsPower")) co_await cmd::applyPower(std::move(pw), me(), val("ThornsPower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("ThornsPower", 2); }
};

// Adrenaline.cs: 0 cost, Exhaust; energy, then draw.
struct Adrenaline : IroncladT<Adrenaline> {
  CARD_HEADER(Adrenaline, "ADRENALINE", 0, Skill, Rare, Self)
    keywords = kwExhaust;
    addVar("Energy", 1);
    addVar("Cards", 2);
  }
  Task<> onPlay(CardPlay&) override {
    co_await cmd::gainEnergy(*combat, val("Energy").toInt());
    co_await drawCards(val("Cards"));
  }
  void onUpgrade() override { upgradeVar("Energy", 1); }
};

// Afterimage.cs: Power; upgrade adds Innate.
struct Afterimage : IroncladT<Afterimage> {
  CARD_HEADER(Afterimage, "AFTERIMAGE", 1, Power, Rare, Self)
    addVar("AfterimagePower", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<AfterimagePower>(me(), val("AfterimagePower"), me(), this); }
  void onUpgrade() override { addKeyword(kwInnate); }
};

// Assassinate.cs: 0 cost, Innate, Exhaust; damage then Vulnerable.
struct Assassinate : IroncladT<Assassinate> {
  CARD_HEADER(Assassinate, "ASSASSINATE", 0, Attack, Rare, AnyEnemy)
    keywords = kwInnate | kwExhaust;
    addVar("Damage", 10);
    addVar("VulnerablePower", 1);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<VulnerablePower>(p.target, val("VulnerablePower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 3); upgradeVar("VulnerablePower", 1); }
};

// BladeOfInk.cs: Shivs in hand, each enchanted with Inky (amount 1).
struct BladeOfInk : IroncladT<BladeOfInk> {
  CARD_HEADER(BladeOfInk, "BLADE_OF_INK", 1, Skill, Rare, Self)
    addVar("Cards", 2);
  }
  Task<> onPlay(CardPlay&) override {
    auto made = co_await createShivsInHand(*combat, val("Cards").toInt());
    for (Card* k : made)
      if (auto e = db::enchantment("Inky")) cmd::enchant(k, std::move(e), 1);
  }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

// BulletTime.cs: every non-X card in hand is free this turn; no more drawing this turn.
struct BulletTime : IroncladT<BulletTime> {
  CARD_HEADER(BulletTime, "BULLET_TIME", 3, Skill, Rare, Self)
  }
  Task<> onPlay(CardPlay&) override {
    for (Card* k : combat->hand)
      if (!k->costsX) k->setThisTurn(0);  // SetToFreeThisTurn
    co_await applyPower<NoDrawPower>(me(), Dec(1), me(), this);
  }
  void onUpgrade() override { cost -= 1; }
};

// Burst.cs: the next Skill(s) are played twice.
struct Burst : IroncladT<Burst> {
  CARD_HEADER(Burst, "BURST", 1, Skill, Rare, Self)
    addVar("Skills", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<BurstPower>(me(), val("Skills"), me(), this); }
  void onUpgrade() override { upgradeVar("Skills", 1); }
};

// CorrosiveWave.cs: this turn every card drawn poisons all enemies.
struct CorrosiveWave : IroncladT<CorrosiveWave> {
  CARD_HEADER(CorrosiveWave, "CORROSIVE_WAVE", 1, Skill, Rare, Self)
    addVar("CorrosiveWave", 2);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<CorrosiveWavePower>(me(), val("CorrosiveWave"), me(), this); }
  void onUpgrade() override { upgradeVar("CorrosiveWave", 1); }
};

// Envenom.cs: Power, attacks add Poison.
struct Envenom : IroncladT<Envenom> {
  CARD_HEADER(Envenom, "ENVENOM", 2, Power, Rare, Self)
    addVar("EnvenomPower", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<EnvenomPower>(me(), val("EnvenomPower"), me(), this); }
  void onUpgrade() override { upgradeVar("EnvenomPower", 1); }
};

// FanOfKnives.cs: Power (Shivs hit everything), then Shivs one at a time.
struct FanOfKnives : IroncladT<FanOfKnives> {
  CARD_HEADER(FanOfKnives, "FAN_OF_KNIVES", 2, Power, Rare, Self)
    addVar("Shivs", 4);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<FanOfKnivesPower>(me(), Dec(1), me(), this);
    int n = val("Shivs").toInt();
    for (int i = 0; i < n; ++i) co_await createShivsInHand(*combat, 1);
  }
  void onUpgrade() override { upgradeVar("Shivs", 1); }
};

// GrandFinale.cs: 0 cost, only playable with an empty draw pile; damage to all enemies.
struct GrandFinale : IroncladT<GrandFinale> {
  CARD_HEADER(GrandFinale, "GRAND_FINALE", 0, Attack, Rare, AllEnemies)
    addVar("Damage", 60);
  }
  // IsPlayable: the card is a hook listener, so it vetoes its own play through ShouldPlay.
  bool shouldPlay(Card* c) override { return c != this || !combat || combat->draw.empty(); }
  Task<> onPlay(CardPlay&) override { co_await attackAll(val("Damage")); }
  void onUpgrade() override { upgradeVar("Damage", 15); }
};

// KnifeTrap.cs: auto-play every Shiv in the exhaust pile at the target (upgraded: upgrade them first).
struct KnifeTrap : IroncladT<KnifeTrap> {
  CARD_HEADER(KnifeTrap, "KNIFE_TRAP", 2, Skill, Rare, AnyEnemy)
    addVar("CalculationBase", 0);
    addVar("CalculationExtra", 1);
    addVar("CalculatedShivs", 0);
    calcMultiplier = [](Card* c) {
      int n = 0;
      if (c->combat) for (Card* k : c->combat->exhaust) if (k->tags & tagShiv) ++n;
      return n;
    };
  }
  Task<> onPlay(CardPlay& p) override {
    std::vector<Card*> shivs;
    for (Card* k : combat->exhaust) if (k->tags & tagShiv) shivs.push_back(k);
    for (Card* k : shivs) {
      if (upgraded()) cmd::upgradeCard(k);
      co_await cmd::autoPlay(*combat, k, p.target);
    }
  }
};

// Malaise.cs: X cost, Exhaust; the target loses X Strength and gains X Weak (+1 each when upgraded).
struct Malaise : IroncladT<Malaise> {
  CARD_HEADER(Malaise, "MALAISE", 0, Skill, Rare, AnyEnemy)
    costsX = true;
    keywords = kwExhaust;
  }
  Task<> onPlay(CardPlay& p) override {
    int n = xValue;
    if (upgraded()) ++n;
    co_await applyPower<StrengthPower>(p.target, Dec(-n), me(), this);
    co_await applyPower<WeakPower>(p.target, Dec(n), me(), this);
  }
};

// MasterPlanner.cs: Power, Skills you play gain Sly.
struct MasterPlanner : IroncladT<MasterPlanner> {
  CARD_HEADER(MasterPlanner, "MASTER_PLANNER", 2, Power, Rare, Self)
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<MasterPlannerPower>(me(), Dec(1), me(), this); }
  void onUpgrade() override { cost -= 1; }
};

// Murder.cs: damage = 1 + 1 per card drawn this combat.
struct Murder : IroncladT<Murder> {
  CARD_HEADER(Murder, "MURDER", 3, Attack, Rare, AnyEnemy)
    addVar("CalculationBase", 1);
    addVar("ExtraDamage", 1);
    addVar("CalculatedDamage", 0);
    calcMultiplier = [](Card* c) { return c->combat ? c->combat->cardsDrawnThisCombat : 0; };
  }
  Task<> onPlay(CardPlay& p) override { co_await attackCalculated(p.target); }
  void onUpgrade() override { cost -= 1; }
};

// Nightmare.cs: Exhaust; choose a card in hand, 3 copies of it join the next hand.
struct Nightmare : IroncladT<Nightmare> {
  bool canBeGeneratedInCombat() const override { return false; }
  CARD_HEADER(Nightmare, "NIGHTMARE", 3, Skill, Rare, Self)
    keywords = kwExhaust;
  }
  Task<> onPlay(CardPlay&) override {
    auto picked = co_await cmd::selectCards(*combat, "NIGHTMARE", combat->hand, 1, 1);
    if (picked.empty()) co_return;
    co_await applyPower<NightmarePower>(me(), Dec(3), me(), this);
    if (auto* pw = me()->get<NightmarePower>()) pw->setSelectedCard(picked[0], 3);
  }
  void onUpgrade() override { cost -= 1; }
};

// Outbreak.cs: Poison to every hittable enemy, then each poisoned enemy's Poison triggers at once.
struct Outbreak : IroncladT<Outbreak> {
  CARD_HEADER(Outbreak, "OUTBREAK", 3, Skill, Rare, AllEnemies)
    addVar("PoisonPower", 9);
  }
  Task<> onPlay(CardPlay&) override {
    for (Creature* e : combat->hittableEnemies()) co_await applyPower<PoisonPower>(e, val("PoisonPower"), me(), this);
    for (Creature* e : combat->hittableEnemies()) {
      if (auto* pw = e->get<PoisonPower>()) co_await pw->trigger();
    }
  }
  void onUpgrade() override { upgradeVar("PoisonPower", 3); }
};

// SerpentForm.cs: Power.
struct SerpentForm : IroncladT<SerpentForm> {
  CARD_HEADER(SerpentForm, "SERPENT_FORM", 3, Power, Rare, Self)
    addVar("SerpentFormPower", 4);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<SerpentFormPower>(me(), val("SerpentFormPower"), me(), this); }
  void onUpgrade() override { upgradeVar("SerpentFormPower", 2); }
};

// ShadowStep.cs: discard the whole hand; next turn's attacks deal double.
struct ShadowStep : IroncladT<ShadowStep> {
  CARD_HEADER(ShadowStep, "SHADOW_STEP", 1, Skill, Rare, Self)
    addVar("Cards", 3);  // unused by the C# OnPlay
  }
  Task<> onPlay(CardPlay&) override {
    std::vector<Card*> cards = combat->hand;
    co_await cmd::discardCards(*combat, cards);
    co_await applyPower<ShadowStepPower>(me(), Dec(1), me(), this);
  }
  void onUpgrade() override { cost -= 1; }
};

// Shadowmeld.cs: this turn block is multiplied by 2 per stack.
struct Shadowmeld : IroncladT<Shadowmeld> {
  CARD_HEADER(Shadowmeld, "SHADOWMELD", 1, Skill, Rare, Self)
    addVar("Power", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<ShadowmeldPower>(me(), val("Power"), me(), this); }
  void onUpgrade() override { cost -= 1; }
};

// StormOfSteel.cs: discard the hand, then one Shiv per card discarded (upgraded Shivs when upgraded).
struct StormOfSteel : IroncladT<StormOfSteel> {
  CARD_HEADER(StormOfSteel, "STORM_OF_STEEL", 1, Skill, Rare, Self)
  }
  Task<> onPlay(CardPlay&) override {
    std::vector<Card*> cards = combat->hand;
    int handSize = (int)cards.size();
    co_await cmd::discardCards(*combat, cards);
    co_await wait(0.25);
    auto made = co_await createShivsInHand(*combat, handSize);
    if (!upgraded()) co_return;
    for (Card* k : made) cmd::upgradeCard(k);
  }
};

// TheHunt.cs: Exhaust; on a fatal hit an extra 3-card reward is added and TheHuntPower marks it.
// PORT NOTE: the "current room is a combat room" check is always true here (event fights also give rewards); the reward is queued through
// Run::bonusCardRewards and rolled by combatRewards for the room type.
struct TheHunt : IroncladT<TheHunt> {
  bool canBeGeneratedInCombat() const override { return false; }
  CARD_HEADER(TheHunt, "THE_HUNT", 1, Attack, Rare, AnyEnemy)
    keywords = kwExhaust;
    addVar("Damage", 10);
  }
  Task<> onPlay(CardPlay& p) override {
    Creature* t = p.target;
    if (!t) co_return;
    bool shouldTriggerFatal = true;
    for (auto& pw : t->powers) shouldTriggerFatal = shouldTriggerFatal && pw->shouldOwnerDeathTriggerFatal();
    cmd::Attack a;
    a.damagePerHit = val("Damage");
    a.attacker = me();
    a.source = this;
    a.single = t;
    co_await a.execute(*combat);
    bool killed = false;
    for (auto& hit : a.results) for (auto& r : hit) if (r.killed) killed = true;
    if (shouldTriggerFatal && killed) {
      if (combat->run) ++combat->run->bonusCardRewards;  // CombatRoom.AddExtraReward(CardReward, 3 cards)
      co_await applyPower<TheHuntPower>(me(), Dec(1), me(), this);
    }
  }
  void onUpgrade() override { upgradeVar("Damage", 5); }
};

// ToolsOfTheTrade.cs: Power.
struct ToolsOfTheTrade : IroncladT<ToolsOfTheTrade> {
  CARD_HEADER(ToolsOfTheTrade, "TOOLS_OF_THE_TRADE", 1, Power, Rare, Self)
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<ToolsOfTheTradePower>(me(), Dec(1), me(), this); }
  void onUpgrade() override { cost -= 1; }
};

// Tracking.cs: Power, +50% damage to Weak enemies.
struct Tracking : IroncladT<Tracking> {
  CARD_HEADER(Tracking, "TRACKING", 2, Power, Rare, Self)
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<TrackingPower>(me(), Dec(50), me(), this); }
  void onUpgrade() override { cost -= 1; }
};

// WellLaidPlans.cs: Power, the hand is kept at the end of the turn.
struct WellLaidPlans : IroncladT<WellLaidPlans> {
  CARD_HEADER(WellLaidPlans, "WELL_LAID_PLANS", 2, Power, Rare, Self)
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<WellLaidPlansPower>(me(), Dec(1), me(), this); }
  void onUpgrade() override { cost -= 1; }
};

}  // namespace

void registerSilentRareCards() {
  registerPowerType<AfterimagePower>();
  registerPowerType<BurstPower>();
  registerPowerType<CorrosiveWavePower>();
  registerPowerType<EnvenomPower>();
  registerPowerType<MasterPlannerPower>();
  registerPowerType<NightmarePower>();
  registerPowerType<SerpentFormPower>();
  registerPowerType<DoubleDamagePower>();
  registerPowerType<ShadowStepPower>();
  registerPowerType<ShadowmeldPower>();
  registerPowerType<TheHuntPower>();
  registerPowerType<ToolsOfTheTradePower>();
  registerPowerType<TrackingPower>();
  registerPowerType<WellLaidPlansPower>();
  registerCardType<Abrasive>();
  registerCardType<Adrenaline>();
  registerCardType<Afterimage>();
  registerCardType<Assassinate>();
  registerCardType<BladeOfInk>();
  registerCardType<BulletTime>();
  registerCardType<Burst>();
  registerCardType<CorrosiveWave>();
  registerCardType<Envenom>();
  registerCardType<FanOfKnives>();
  registerCardType<GrandFinale>();
  registerCardType<KnifeTrap>();
  registerCardType<Malaise>();
  registerCardType<MasterPlanner>();
  registerCardType<Murder>();
  registerCardType<Nightmare>();
  registerCardType<Outbreak>();
  registerCardType<SerpentForm>();
  registerCardType<ShadowStep>();
  registerCardType<Shadowmeld>();
  registerCardType<StormOfSteel>();
  registerCardType<TheHunt>();
  registerCardType<ToolsOfTheTrade>();
  registerCardType<Tracking>();
  registerCardType<WellLaidPlans>();
}

}  // namespace sts
