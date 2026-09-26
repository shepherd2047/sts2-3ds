// Potions (package 8): PotionModel subclasses of IroncladPotionPool + SharedPotionPool, the
// powers they apply, the belt (Player.PotionSlots), PotionCmd, PotionFactory and
// PotionRewardOdds. Translated from MegaCrit.Sts2.Core.Models.Potions / .Powers,
// Commands/PotionCmd.cs, Factories/PotionFactory.cs, Odds/PotionRewardOdds.cs.
//
// Not registered (so never rolled): ColorlessPotion (no colorless card pool yet).
// Other characters' pools (Silent, Defect, Necrobinder, Regent) are not ported.
#include <algorithm>
#include <cstdlib>

#include "cards.h"

namespace sts {

Creature* Potion::owner() const { return run->player.get(); }

namespace {

bool inCombat(const Run* r) { return r->combat && r->combat->inProgress && !r->combat->over; }

// ================================================================ powers

struct ClarityPower : Power {
  POWER_HEADER(ClarityPower, "CLARITY_POWER")
  Dec modifyHandDraw(Dec count) override { return count + Dec(1); }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await cmd::decrement(this);
  }
};

struct DuplicationPower : Power {
  POWER_HEADER(DuplicationPower, "DUPLICATION_POWER")
  int modifyCardPlayCount(Card* card, Creature*, int count) override { return ownerOf(card) == owner ? count + 1 : count; }
  Task<> afterModifyingCardPlayCount(Card*) override { co_await cmd::decrement(this); }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await cmd::removePower(this);
  }
};

// TemporaryStrengthPower / TemporaryDexterityPower with the potion as OriginModel.
template <class Stat, int Sign> struct TemporaryStatPower : Power {
  PowerType type() const override { return Sign > 0 ? PowerType::Buff : PowerType::Debuff; }
  Task<> beforeApplied(Creature* target, Dec amt, Creature* app, Card* src) override {
    co_await applyPower<Stat>(target, amt * Dec(Sign), app, src, true);
  }
  Task<> afterPowerAmountChanged(Power* p, Dec amt, Creature* app, Card* src) override {
    if (!(amt == Dec(amount)) && p == this) co_await applyPower<Stat>(owner, amt * Dec(Sign), app, src, true);
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) {
      flash = 1.f;
      Creature* o = owner;
      int a = amount;
      co_await cmd::removePower(this);
      co_await applyPower<Stat>(o, Dec(-a * Sign), o, nullptr);
    }
  }
};
struct FlexPotionPower : TemporaryStatPower<StrengthPower, 1> {
  POWER_HEADER(FlexPotionPower, "TEMPORARY_STRENGTH_POWER")
};
struct ShacklingPotionPower : TemporaryStatPower<StrengthPower, -1> {
  POWER_HEADER(ShacklingPotionPower, "TEMPORARY_STRENGTH_DOWN")
};
struct SpeedPotionPower : TemporaryStatPower<DexterityPower, 1> {
  POWER_HEADER(SpeedPotionPower, "TEMPORARY_DEXTERITY_POWER")
};

// GigantificationPower: the next attack card's powered damage is tripled. PORT NOTE: the
// C# pins the first AttackCommand (BeforeAttack/AfterAttack); here the first attack card
// played is pinned (BeforeCardPlayed/AfterCardPlayed), the same for every Ironclad card.
struct GigantificationPower : Power {
  POWER_HEADER(GigantificationPower, "GIGANTIFICATION_POWER")
  Card* pinned = nullptr;
  Task<> beforeCardPlayed(const CardPlay& p) override {
    if (!pinned && p.card->type == CardType::Attack && ownerOf(p.card) == owner) pinned = p.card;
    return {};
  }
  Dec modifyDamageMultiplicative(Creature*, Dec, int props, Creature*, Card* src) override {
    if (!src || ownerOf(src) != owner || src->type != CardType::Attack || !isPoweredAttack(props)) return 1;
    return !pinned || src == pinned ? Dec(3) : Dec(1);
  }
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (p.card == pinned) {
      pinned = nullptr;
      co_await cmd::decrement(this);
    }
  }
};

struct BufferPower : Power {
  POWER_HEADER(BufferPower, "BUFFER_POWER")
  bool used = false;
  Dec modifyHpLostAfterOsty(Creature* target, Dec amount, int, Creature*, Card*) override {
    if (target != owner || amount <= Dec(0)) return amount;
    used = true;
    return 0;
  }
  Task<> afterDamageReceived(Creature* target, const DamageResult&, int, Creature*, Card*) override {
    if (target != owner || !used) co_return;
    used = false;
    flash = 1.f;
    co_await cmd::decrement(this);
  }
};

struct DemisePower : Power {
  POWER_HEADER(DemisePower, "DEMISE_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner)) co_return;
    flash = 1.f;
    co_await cmd::damage(owner, Dec(amount), kUnblockable | kUnpowered, nullptr, nullptr);
  }
};

struct RadiancePower : Power {
  POWER_HEADER(RadiancePower, "RADIANCE_POWER")
  Task<> afterEnergyReset() override {
    if (!owner->isPlayer) co_return;
    co_await cmd::gainEnergy(*owner->combat, 1);
    co_await cmd::decrement(this);
  }
};

struct RegenPower : Power {
  POWER_HEADER(RegenPower, "REGEN_POWER")
  Task<> beforeSideTurnEndEarly(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner) || owner->dead()) co_return;
    flash = 1.f;
    co_await cmd::heal(owner, Dec(amount));
    co_await cmd::decrement(this);
  }
};

struct BlockNextTurnPower : Power {
  POWER_HEADER(BlockNextTurnPower, "BLOCK_NEXT_TURN_POWER")
  Task<> afterBlockCleared(Creature* c) override {
    if (c != owner) co_return;
    flash = 1.f;
    co_await cmd::gainBlock(owner, Dec(amount), kUnpowered, nullptr);
    co_await cmd::removePower(this);
  }
};

struct RetainHandPower : Power {
  POWER_HEADER(RetainHandPower, "RETAIN_HAND_POWER")
  bool shouldFlush() override { return false; }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await cmd::decrement(this);
  }
};

// ================================================================ potions

struct PotionBase : Potion {
  Combat& c() { return *run->combat; }
  Creature* me() { return run->player.get(); }
  template <class P> Task<> apply(Creature* t, Dec amt) { co_await applyPower<P>(t, amt, me(), nullptr); }
  Task<> applyId(const char* powerId, Creature* t, Dec amt) {
    auto p = db::power(powerId);
    if (p) co_await cmd::applyPower(std::move(p), t, amt, me(), nullptr);
  }
  // Heal / max HP work in and out of combat.
  Task<> heal(Dec amount) {
    if (inCombat(run)) { co_await cmd::heal(me(), amount); co_return; }
    me()->hp = std::min(me()->maxHp, me()->hp + amount.toInt());
    co_await wait(0.2);
  }
  // CardFactory.GetDistinctForCombat (the character's pool, no Basic/Ancient) +
  // CardSelectCmd.FromChooseACardScreen(canSkip) + SetToFreeThisTurn -> hand.
  Task<> chooseGenerated(CardType type) {
    auto ids = db::ironcladCards([&](const Card& k) { return k.type == type && k.rarity != Rarity::Basic && k.rarity != Rarity::Ancient; });
    c().rng("CombatCardGeneration").shuffle(ids);  // TakeRandom(3)
    std::vector<std::unique_ptr<Card>> made;
    std::vector<Card*> opts;
    for (size_t i = 0; i < ids.size() && i < 3; ++i) {
      auto k = db::card(ids[i]);
      if (!k) continue;
      k->combat = &c();
      opts.push_back(k.get());
      made.push_back(std::move(k));
    }
    auto picked = co_await cmd::selectCards(c(), "选择一张牌加入手牌", opts, 0, 1);
    if (picked.empty()) co_return;
    for (auto& k : made)
      if (k.get() == picked[0]) {
        k->setThisTurnOrUntilPlayed(0);
        co_await cmd::addGeneratedCard(c(), std::move(k), Pile::Hand);
        break;
      }
  }
  std::string prompt() const { return "potions." + locKey + ".selectionScreenPrompt"; }
};

struct Ashwater : PotionBase {
  POTION_HEADER(Ashwater, "ASHWATER", Uncommon, CombatOnly, Self) }
  Task<> onUse(Creature*) override {
    auto picked = co_await cmd::selectCards(c(), prompt(), c().hand, 0, 999);
    for (Card* k : picked) co_await cmd::exhaustCard(c(), k);
  }
};

struct AttackPotion : PotionBase {
  POTION_HEADER(AttackPotion, "ATTACK_POTION", Common, CombatOnly, Self) }
  Task<> onUse(Creature*) override { co_await chooseGenerated(CardType::Attack); }
};
struct SkillPotion : PotionBase {
  POTION_HEADER(SkillPotion, "SKILL_POTION", Common, CombatOnly, Self) }
  Task<> onUse(Creature*) override { co_await chooseGenerated(CardType::Skill); }
};
struct PowerPotion : PotionBase {
  POTION_HEADER(PowerPotion, "POWER_POTION", Common, CombatOnly, Self) }
  Task<> onUse(Creature*) override { co_await chooseGenerated(CardType::Power); }
};

struct BeetleJuice : PotionBase {
  POTION_HEADER(BeetleJuice, "BEETLE_JUICE", Rare, CombatOnly, AnyEnemy) addVar("DamageDecrease", 30); addVar("Repeat", 4); }
  Task<> onUse(Creature* t) override { co_await apply<ShrinkPower>(t, val("Repeat")); }
};

struct BlessingOfTheForge : PotionBase {
  POTION_HEADER(BlessingOfTheForge, "BLESSING_OF_THE_FORGE", Uncommon, CombatOnly, Self) }
  Task<> onUse(Creature*) override {
    for (Card* k : c().hand) if (k->upgradable()) cmd::upgradeCard(k);
    co_return;
  }
};

struct BlockPotion : PotionBase {
  POTION_HEADER(BlockPotion, "BLOCK_POTION", Common, CombatOnly, Self) addVar("Block", 12); }
  Task<> onUse(Creature* t) override { co_await cmd::gainBlock(t, val("Block"), kUnpowered, nullptr); }
};

struct BloodPotion : PotionBase {
  POTION_HEADER(BloodPotion, "BLOOD_POTION", Common, AnyTime, Self) addVar("HealPercent", 20); }
  Task<> onUse(Creature* t) override { co_await heal(Dec(t->maxHp) * val("HealPercent") / Dec(100)); }
};

struct BottledPotential : PotionBase {
  POTION_HEADER(BottledPotential, "BOTTLED_POTENTIAL", Rare, CombatOnly, Self) addVar("Cards", 5); }
  Task<> onUse(Creature*) override {
    std::vector<Card*> hand = c().hand;
    for (Card* k : hand) co_await cmd::moveCard(c(), k, Pile::Draw);
    co_await cmd::shuffle(c());
    co_await cmd::drawCards(c(), val("Cards"));
  }
};

struct Clarity : PotionBase {
  POTION_HEADER(Clarity, "CLARITY", Uncommon, CombatOnly, Self) addVar("ClarityPower", 3); addVar("Cards", 1); }
  Task<> onUse(Creature* t) override {
    co_await cmd::drawCards(c(), val("Cards"));
    co_await apply<ClarityPower>(t, val("ClarityPower"));
  }
};

struct CureAll : PotionBase {
  POTION_HEADER(CureAll, "CURE_ALL", Uncommon, CombatOnly, Self) addVar("Energy", 1); addVar("Cards", 2); }
  Task<> onUse(Creature*) override {
    co_await cmd::gainEnergy(c(), val("Energy").toInt());
    co_await cmd::drawCards(c(), val("Cards"));
  }
};

struct DexterityPotion : PotionBase {
  POTION_HEADER(DexterityPotion, "DEXTERITY_POTION", Common, CombatOnly, Self) addVar("DexterityPower", 2); }
  Task<> onUse(Creature* t) override { co_await apply<DexterityPower>(t, val("DexterityPower")); }
};

struct DistilledChaos : PotionBase {
  POTION_HEADER(DistilledChaos, "DISTILLED_CHAOS", Rare, CombatOnly, Self) addVar("Repeat", 3); }
  Task<> onUse(Creature*) override { co_await cmd::autoPlayFromDrawPile(c(), val("Repeat").toInt(), false); }
};

struct DropletOfPrecognition : PotionBase {
  POTION_HEADER(DropletOfPrecognition, "DROPLET_OF_PRECOGNITION", Rare, CombatOnly, Self) }
  Task<> onUse(Creature*) override {
    auto picked = co_await cmd::selectCards(c(), prompt(), c().draw, 1, 1);
    if (!picked.empty()) co_await cmd::moveCard(c(), picked[0], Pile::Hand);
  }
};

struct Duplicator : PotionBase {
  POTION_HEADER(Duplicator, "DUPLICATOR", Uncommon, CombatOnly, Self) }
  Task<> onUse(Creature* t) override { co_await applyPower<DuplicationPower>(t, 1, t, nullptr); }
};

struct EnergyPotion : PotionBase {
  POTION_HEADER(EnergyPotion, "ENERGY_POTION", Common, CombatOnly, Self) addVar("Energy", 2); }
  Task<> onUse(Creature*) override { co_await cmd::gainEnergy(c(), val("Energy").toInt()); }
};

struct EntropicBrew : PotionBase {
  POTION_HEADER(EntropicBrew, "ENTROPIC_BREW", Rare, AnyTime, Self) }
  Task<> onUse(Creature*) override {
    while (run->hasOpenPotionSlot())
      if (!run->procurePotion(run->randomPotion(run->rng("CombatPotionGeneration"), false))) break;
    co_await wait(0.2);
  }
};

struct ExplosiveAmpoule : PotionBase {
  POTION_HEADER(ExplosiveAmpoule, "EXPLOSIVE_AMPOULE", Common, CombatOnly, AllEnemies) addVar("Damage", 10); }
  Task<> onUse(Creature*) override {
    co_await wait(0.2);
    co_await cmd::damage(c().hittableEnemies(), val("Damage"), kUnpowered, me(), nullptr);
  }
};

// Automatic: never used by hand; Run::preventDeath uses it (ShouldDie + AfterPreventingDeath).
struct FairyInABottle : PotionBase {
  POTION_HEADER(FairyInABottle, "FAIRY_IN_A_BOTTLE", Rare, Automatic, Self) addVar("HealPercent", 30); }
  bool canBeGeneratedInCombat() const override { return false; }
  Task<> onUse(Creature* t) override { co_await heal(dmax(Dec(t->maxHp) * Dec::lit(0.3), Dec(1))); }
};

struct FirePotion : PotionBase {
  POTION_HEADER(FirePotion, "FIRE_POTION", Common, CombatOnly, AnyEnemy) addVar("Damage", 20); }
  Task<> onUse(Creature* t) override { co_await cmd::damage(t, val("Damage"), kUnpowered, me(), nullptr); }
};

struct FlexPotion : PotionBase {
  POTION_HEADER(FlexPotion, "FLEX_POTION", Common, CombatOnly, Self) addVar("StrengthPower", 5); }
  Task<> onUse(Creature* t) override { co_await apply<FlexPotionPower>(t, val("StrengthPower")); }
};

struct Fortifier : PotionBase {
  POTION_HEADER(Fortifier, "FORTIFIER", Uncommon, CombatOnly, Self) }
  Task<> onUse(Creature* t) override { co_await cmd::gainBlock(t, Dec(t->block * 2), kUnpowered, nullptr); }
};

struct FruitJuice : PotionBase {
  POTION_HEADER(FruitJuice, "FRUIT_JUICE", Rare, AnyTime, Self) addVar("MaxHp", 5); }
  bool canBeGeneratedInCombat() const override { return false; }
  Task<> onUse(Creature* t) override {
    if (inCombat(run)) co_await cmd::gainMaxHp(t, val("MaxHp").toInt());
    else co_await run->gainMaxHp(val("MaxHp").toInt());
  }
};

struct FyshOil : PotionBase {
  POTION_HEADER(FyshOil, "FYSH_OIL", Uncommon, CombatOnly, Self) addVar("StrengthPower", 1); addVar("DexterityPower", 1); }
  Task<> onUse(Creature* t) override {
    co_await apply<StrengthPower>(t, val("StrengthPower"));
    co_await apply<DexterityPower>(t, val("DexterityPower"));
  }
};

// CardCmd.DiscardAndDraw: discard the chosen cards, then draw that many.
struct GamblersBrew : PotionBase {
  POTION_HEADER(GamblersBrew, "GAMBLERS_BREW", Uncommon, CombatOnly, Self) }
  Task<> onUse(Creature*) override {
    auto picked = co_await cmd::selectCards(c(), prompt(), c().hand, 0, 999);
    for (Card* k : picked) co_await cmd::moveCard(c(), k, Pile::Discard);
    if (!picked.empty()) co_await cmd::drawCards(c(), Dec((int)picked.size()));
  }
};

struct GigantificationPotion : PotionBase {
  POTION_HEADER(GigantificationPotion, "GIGANTIFICATION_POTION", Rare, CombatOnly, Self) addVar("GigantificationPower", 1); }
  Task<> onUse(Creature* t) override { co_await apply<GigantificationPower>(t, val("GigantificationPower")); }
};

struct HeartOfIron : PotionBase {
  POTION_HEADER(HeartOfIron, "HEART_OF_IRON", Uncommon, CombatOnly, Self) addVar("PlatingPower", 7); }
  Task<> onUse(Creature* t) override { co_await apply<PlatingPower>(t, val("PlatingPower")); }
};

struct LiquidBronze : PotionBase {
  POTION_HEADER(LiquidBronze, "LIQUID_BRONZE", Uncommon, CombatOnly, Self) addVar("ThornsPower", 3); }
  Task<> onUse(Creature* t) override { co_await applyId("ThornsPower", t, val("ThornsPower")); }
};

struct LiquidMemories : PotionBase {
  POTION_HEADER(LiquidMemories, "LIQUID_MEMORIES", Rare, CombatOnly, Self) }
  Task<> onUse(Creature*) override {
    auto picked = co_await cmd::selectCards(c(), prompt(), c().discard, 1, 1);
    if (picked.empty()) co_return;
    picked[0]->setThisTurnOrUntilPlayed(0);
    co_await cmd::moveCard(c(), picked[0], Pile::Hand);
  }
};

struct LuckyTonic : PotionBase {
  POTION_HEADER(LuckyTonic, "LUCKY_TONIC", Rare, CombatOnly, Self) addVar("BufferPower", 1); }
  Task<> onUse(Creature* t) override { co_await apply<BufferPower>(t, val("BufferPower")); }
};

struct MazalethsGift : PotionBase {
  POTION_HEADER(MazalethsGift, "MAZALETHS_GIFT", Rare, CombatOnly, Self) addVar("RitualPower", 1); }
  Task<> onUse(Creature* t) override { co_await applyId("RitualPower", t, val("RitualPower")); }
};

struct OrobicAcid : PotionBase {
  POTION_HEADER(OrobicAcid, "OROBIC_ACID", Rare, CombatOnly, Self) }
  Task<> onUse(Creature*) override {
    for (CardType type : {CardType::Attack, CardType::Skill, CardType::Power}) {
      auto ids = db::ironcladCards([&](const Card& k) { return k.type == type && k.rarity != Rarity::Basic && k.rarity != Rarity::Ancient; });
      if (ids.empty()) continue;
      auto k = db::card(c().rng("CombatCardGeneration").nextItem(ids));
      if (!k) continue;
      k->setThisTurnOrUntilPlayed(0);
      co_await cmd::addGeneratedCard(c(), std::move(k), Pile::Hand);
    }
  }
};

struct PotionOfBinding : PotionBase {
  POTION_HEADER(PotionOfBinding, "POTION_OF_BINDING", Uncommon, CombatOnly, AllEnemies) addVar("VulnerablePower", 1); addVar("WeakPower", 1); }
  Task<> onUse(Creature*) override {
    auto targets = c().hittableEnemies();
    for (Creature* e : targets) co_await apply<WeakPower>(e, val("VulnerablePower"));
    for (Creature* e : targets) co_await apply<VulnerablePower>(e, val("WeakPower"));
  }
};

struct PowderedDemise : PotionBase {
  POTION_HEADER(PowderedDemise, "POWDERED_DEMISE", Uncommon, CombatOnly, AnyEnemy) addVar("Demise", 9); }
  Task<> onUse(Creature* t) override { co_await apply<DemisePower>(t, val("Demise")); }
};

struct RadiantTincture : PotionBase {
  POTION_HEADER(RadiantTincture, "RADIANT_TINCTURE", Uncommon, CombatOnly, Self) addVar("Energy", 1); addVar("RadiancePower", 3); }
  Task<> onUse(Creature* t) override {
    co_await cmd::gainEnergy(c(), val("Energy").toInt());
    co_await apply<RadiancePower>(t, val("RadiancePower"));
  }
};

struct RegenPotion : PotionBase {
  POTION_HEADER(RegenPotion, "REGEN_POTION", Uncommon, CombatOnly, Self) addVar("RegenPower", 5); }
  bool canBeGeneratedInCombat() const override { return false; }
  Task<> onUse(Creature* t) override { co_await apply<RegenPower>(t, val("RegenPower")); }
};

struct ShacklingPotion : PotionBase {
  POTION_HEADER(ShacklingPotion, "SHACKLING_POTION", Rare, CombatOnly, AllEnemies) addVar("StrengthPower", 7); }
  Task<> onUse(Creature*) override {
    for (Creature* e : c().hittableEnemies()) co_await apply<ShacklingPotionPower>(e, val("StrengthPower"));
  }
};

struct ShipInABottle : PotionBase {
  POTION_HEADER(ShipInABottle, "SHIP_IN_A_BOTTLE", Rare, CombatOnly, Self) addVar("Block", 10); }
  Task<> onUse(Creature* t) override {
    co_await cmd::gainBlock(t, val("Block"), kUnpowered, nullptr);
    co_await apply<BlockNextTurnPower>(t, val("Block"));
  }
};

struct SneckoOil : PotionBase {
  POTION_HEADER(SneckoOil, "SNECKO_OIL", Rare, CombatOnly, Self) addVar("Cards", 7); }
  Task<> onUse(Creature*) override {
    co_await cmd::drawCards(c(), val("Cards"));
    for (Card* k : c().hand)
      if (!k->costsX && k->cost >= 0) k->setThisTurnOrUntilPlayed(run->rng("CombatEnergyCosts").nextInt(4));
  }
};

struct SoldiersStew : PotionBase {
  POTION_HEADER(SoldiersStew, "SOLDIERS_STEW", Rare, CombatOnly, Self) }
  Task<> onUse(Creature*) override {
    for (Card* k : c().allCards()) if (k->tags & tagStrike) ++k->baseReplayCount;
    co_return;
  }
};

struct SpeedPotion : PotionBase {
  POTION_HEADER(SpeedPotion, "SPEED_POTION", Common, CombatOnly, Self) addVar("DexterityPower", 5); }
  Task<> onUse(Creature* t) override { co_await apply<SpeedPotionPower>(t, val("DexterityPower")); }
};

struct StableSerum : PotionBase {
  POTION_HEADER(StableSerum, "STABLE_SERUM", Uncommon, CombatOnly, Self) addVar("Repeat", 2); }
  Task<> onUse(Creature* t) override { co_await apply<RetainHandPower>(t, val("Repeat")); }
};

struct StrengthPotion : PotionBase {
  POTION_HEADER(StrengthPotion, "STRENGTH_POTION", Common, CombatOnly, Self) addVar("StrengthPower", 2); }
  Task<> onUse(Creature* t) override { co_await apply<StrengthPower>(t, val("StrengthPower")); }
};

struct SwiftPotion : PotionBase {
  POTION_HEADER(SwiftPotion, "SWIFT_POTION", Common, CombatOnly, Self) addVar("Cards", 3); }
  Task<> onUse(Creature*) override { co_await cmd::drawCards(c(), val("Cards")); }
};

// CardSelectCmd.FromHand (cards that cost energy) -> SetToFreeThisCombat.
struct TouchOfInsanity : PotionBase {
  POTION_HEADER(TouchOfInsanity, "TOUCH_OF_INSANITY", Uncommon, CombatOnly, Self) }
  Task<> onUse(Creature*) override {
    std::vector<Card*> opts;
    for (Card* k : c().hand) if (!k->costsX && (k->costWithLocalMods() > 0 || c().energyCost(k) > 0)) opts.push_back(k);
    auto picked = co_await cmd::selectCards(c(), prompt(), opts, 1, 1);
    if (!picked.empty()) picked[0]->setThisCombat(0);
  }
};

struct VulnerablePotion : PotionBase {
  POTION_HEADER(VulnerablePotion, "VULNERABLE_POTION", Common, CombatOnly, AnyEnemy) addVar("VulnerablePower", 3); }
  Task<> onUse(Creature* t) override { co_await apply<VulnerablePower>(t, val("VulnerablePower")); }
};

struct WeakPotion : PotionBase {
  POTION_HEADER(WeakPotion, "WEAK_POTION", Common, CombatOnly, AnyEnemy) addVar("WeakPower", 3); }
  Task<> onUse(Creature* t) override { co_await apply<WeakPower>(t, val("WeakPower")); }
};

std::map<std::string, PotionFactoryFn>& potionReg() {
  static std::map<std::string, PotionFactoryFn> m;
  return m;
}
template <class P> void registerPotionType() { db::registerPotion(P::kId, [] { return std::unique_ptr<Potion>(new P()); }); }

}  // namespace

// ================================================================ registry

namespace db {
void registerPotion(const std::string& id, PotionFactoryFn f) { potionReg()[id] = f; }
std::unique_ptr<Potion> potion(const std::string& id) {
  auto it = potionReg().find(id);
  return it == potionReg().end() ? nullptr : it->second();
}
// IroncladPotionPool (Ironclad4Epoch.Potions) then SharedPotionPool, in the game's order.
const std::vector<std::string>& potionPool() {
  static const std::vector<std::string> v = {
      "BloodPotion", "SoldiersStew", "Ashwater",
      "AttackPotion", "BeetleJuice", "BlessingOfTheForge", "BlockPotion", "BottledPotential", "Clarity",
      "ColorlessPotion", "CureAll", "DexterityPotion", "DistilledChaos", "DropletOfPrecognition", "Duplicator",
      "EnergyPotion", "EntropicBrew", "ExplosiveAmpoule", "FairyInABottle", "FirePotion", "FlexPotion", "Fortifier",
      "FruitJuice", "FyshOil", "GamblersBrew", "GigantificationPotion", "HeartOfIron", "LiquidBronze",
      "LiquidMemories", "LuckyTonic", "MazalethsGift", "OrobicAcid", "PotionOfBinding", "PowderedDemise",
      "PowerPotion", "RadiantTincture", "RegenPotion", "ShacklingPotion", "ShipInABottle", "SkillPotion",
      "SneckoOil", "SpeedPotion", "StableSerum", "StrengthPotion", "SwiftPotion", "TouchOfInsanity",
      "VulnerablePotion", "WeakPotion"};
  return v;
}
}  // namespace db

void registerPotions() {
  registerPowerType<ClarityPower>();
  registerPowerType<DuplicationPower>();
  registerPowerType<FlexPotionPower>();
  registerPowerType<ShacklingPotionPower>();
  registerPowerType<SpeedPotionPower>();
  registerPowerType<GigantificationPower>();
  registerPowerType<BufferPower>();
  registerPowerType<DemisePower>();
  registerPowerType<RadiancePower>();
  registerPowerType<RegenPower>();
  registerPowerType<BlockNextTurnPower>();
  registerPowerType<RetainHandPower>();

  registerPotionType<Ashwater>();
  registerPotionType<AttackPotion>();
  registerPotionType<BeetleJuice>();
  registerPotionType<BlessingOfTheForge>();
  registerPotionType<BlockPotion>();
  registerPotionType<BloodPotion>();
  registerPotionType<BottledPotential>();
  registerPotionType<Clarity>();
  registerPotionType<CureAll>();
  registerPotionType<DexterityPotion>();
  registerPotionType<DistilledChaos>();
  registerPotionType<DropletOfPrecognition>();
  registerPotionType<Duplicator>();
  registerPotionType<EnergyPotion>();
  registerPotionType<EntropicBrew>();
  registerPotionType<ExplosiveAmpoule>();
  registerPotionType<FairyInABottle>();
  registerPotionType<FirePotion>();
  registerPotionType<FlexPotion>();
  registerPotionType<Fortifier>();
  registerPotionType<FruitJuice>();
  registerPotionType<FyshOil>();
  registerPotionType<GamblersBrew>();
  registerPotionType<GigantificationPotion>();
  registerPotionType<HeartOfIron>();
  registerPotionType<LiquidBronze>();
  registerPotionType<LiquidMemories>();
  registerPotionType<LuckyTonic>();
  registerPotionType<MazalethsGift>();
  registerPotionType<OrobicAcid>();
  registerPotionType<PotionOfBinding>();
  registerPotionType<PowderedDemise>();
  registerPotionType<PowerPotion>();
  registerPotionType<RadiantTincture>();
  registerPotionType<RegenPotion>();
  registerPotionType<ShacklingPotion>();
  registerPotionType<ShipInABottle>();
  registerPotionType<SkillPotion>();
  registerPotionType<SneckoOil>();
  registerPotionType<SoldiersStew>();
  registerPotionType<SpeedPotion>();
  registerPotionType<StableSerum>();
  registerPotionType<StrengthPotion>();
  registerPotionType<SwiftPotion>();
  registerPotionType<TouchOfInsanity>();
  registerPotionType<VulnerablePotion>();
  registerPotionType<WeakPotion>();
}

// ================================================================ belt, PotionCmd, rewards

bool Run::hasOpenPotionSlot() const {
  for (auto& p : potions) if (!p) return true;
  return false;
}

bool Run::procurePotion(std::unique_ptr<Potion> p) {
  if (!p) return false;
  for (auto& slot : potions)
    if (!slot) {
      p->run = this;
      slot = std::move(p);
      return true;
    }
  return false;  // PotionProcureFailureReason.TooFull
}

void Run::discardPotion(int slot) {
  if (slot >= 0 && slot < (int)potions.size()) potions[slot].reset();
}

// NPotionPopup: CombatOnly potions only during the player's turn; AnyTime potions also
// outside combat (not while a fight is being set up or cleaned up); Automatic never.
bool Run::canUsePotion(int slot) const {
  if (slot < 0 || slot >= (int)potions.size() || !potions[slot]) return false;
  const Potion& p = *potions[slot];
  if (p.usage == PotionUsage::Automatic) return false;
  if (inCombat(this)) return combat->playerPhase;
  return p.usage == PotionUsage::AnyTime && !combat;
}

Task<> Run::usePotion(int slot, Creature* target) {
  // Checked by the caller (canUsePotion); the combat loop has already left playerPhase.
  if (slot < 0 || slot >= (int)potions.size() || !potions[slot] || potions[slot]->usage == PotionUsage::Automatic) co_return;
  std::unique_ptr<Potion> p = std::move(potions[slot]);  // RemoveBeforeUse
  if (!target) target = player.get();
  p->run = this;
  p->combat = inCombat(this) ? combat.get() : nullptr;
  if (p->combat) p->combat->push({VisualEvent::Anim, player.get(), 0, "Cast"});
  co_await wait(0.2);
  co_await p->onUse(target);
  if (p->combat) co_await p->combat->checkWinCondition();
}

// PotionFactory.CreateRandomPotions: rarity 10% Rare / 25% Uncommon / 65% Common, then a
// random registered potion of that rarity from the Ironclad + shared pools.
std::unique_ptr<Potion> Run::randomPotion(Rng& r, bool forCombat) {
  float roll = r.nextFloat();
  PotionRarity rarity = roll <= 0.1f ? PotionRarity::Rare : roll <= 0.35f ? PotionRarity::Uncommon : PotionRarity::Common;
  std::vector<std::string> ids;
  for (auto& id : db::potionPool()) {
    auto p = db::potion(id);
    if (p && p->rarity == rarity && (!forCombat || p->canBeGeneratedInCombat())) ids.push_back(id);
  }
  if (ids.empty()) return db::potion("BlockPotion");
  auto p = db::potion(r.nextItem(ids));
  p->run = this;
  return p;
}

// PotionRewardOdds.Roll: 40% to start, -10% after a potion, +10% after none; elites add
// half of their 25% bonus.
bool Run::rollPotionReward(RoomType room) {
  if (getenv("STS_POTION_REWARD")) return true;  // debug: every fight drops a potion
  float bonus = room == RoomType::Elite ? 0.25f : 0.f;
  float chance = potionRewardOdds + bonus * 0.5f;
  if (rng("Rewards").nextFloat() < chance) {
    potionRewardOdds -= 0.1f;
    return true;
  }
  potionRewardOdds += 0.1f;
  return false;
}

// PotionReward.OnSelect: taking it fails while the belt is full (the UI lets the player
// discard one first, or skip).
Task<> Run::offerPotion(std::unique_ptr<Potion> p) {
  if (!p) co_return;
  p->run = this;
  potionOffer = std::move(p);
  screen = Screen::PotionOffer;
  for (;;) {
    int take = co_await potionOfferChoice.next();
    if (take != 1) break;
    if (hasOpenPotionSlot()) { procurePotion(std::move(potionOffer)); break; }
  }
  potionOffer.reset();
}

// FairyInABottle.ShouldDie / AfterPreventingDeath: the first Fairy in the belt is used up
// and heals 30% of max HP.
bool Run::preventDeath() {
  for (auto& slot : potions)
    if (slot && slot->id == "FairyInABottle") {
      slot.reset();
      player->hp = std::max(1, (int)(Dec(player->maxHp) * Dec::lit(0.3)).toInt());
      if (combat) combat->push({VisualEvent::Heal, player.get(), player->hp});
      return true;
    }
  return false;
}

}  // namespace sts
