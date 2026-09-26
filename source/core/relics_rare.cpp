// Rare relics (RelicRarity.Rare) from SharedRelicPool, plus IroncladRelicPool.
#include "cards.h"
#include "powers.h"
#include <algorithm>

namespace sts {

namespace {
template <class R> void reg() { db::registerRelic(R::kId, [] { return std::unique_ptr<Relic>(new R()); }); }
}  // namespace

// ---- ArtOfWar: if no attack was played last turn, gain 1 energy this turn. ----
struct ArtOfWar : Relic {
  RELIC_HEADER(ArtOfWar, "ART_OF_WAR", Rare)
    addVar("Energy", 1);
  }
  bool anyAttackLastTurn = false, anyAttackThisTurn = false;
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (!combat || p.card->type != CardType::Attack) co_return;
    if (anyAttackLastTurn) co_return;
    anyAttackThisTurn = true;
  }
  Task<> afterSideTurnEnd(Side side, const std::vector<Creature*>& participants) override {
    if (!combat || !contains(participants, owner())) co_return;
    anyAttackLastTurn = anyAttackThisTurn;
    anyAttackThisTurn = false;
  }
  Task<> afterEnergyReset() override {
    if (!combat) co_return;
    if (combat->turnNumber > 1 && !anyAttackLastTurn) {
      doFlash();
      co_await cmd::gainEnergy(*combat, val("Energy").toInt());
    }
    anyAttackLastTurn = false;
    anyAttackThisTurn = false;
  }
  Task<> afterCombatEnd() override { anyAttackLastTurn = false; anyAttackThisTurn = false; co_return; }

 private:
  static bool contains(const std::vector<Creature*>& v, Creature* c) { return std::find(v.begin(), v.end(), c) != v.end(); }
};

// ---- BeatingRemnant: caps hp loss "after Osty" (trample) at 20 per turn. ----
struct BeatingRemnant : Relic {
  RELIC_HEADER(BeatingRemnant, "BEATING_REMNANT", Rare)
    addVar("MaxHpLoss", 20);
  }
  Dec damageReceivedThisTurn = 0;
  Dec modifyHpLostAfterOsty(Creature* target, Dec amount, int, Creature*, Card*) override {
    if (!combat || target != owner()) return amount;
    Dec capped = dmin(amount, val("MaxHpLoss") - damageReceivedThisTurn);
    if (capped < amount) doFlash();
    return capped;
  }
  Task<> afterDamageReceived(Creature* target, const DamageResult& result, int, Creature*, Card*) override {
    if (!combat || target != owner()) co_return;
    damageReceivedThisTurn += Dec(result.unblocked);
  }
  Task<> beforeSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!combat || std::find(participants.begin(), participants.end(), owner()) == participants.end()) co_return;
    damageReceivedThisTurn = 0;
  }
};

// ---- Bellows: on turn 1, upgrade every card in hand. ----
struct Bellows : Relic {
  RELIC_HEADER(Bellows, "BELLOWS", Rare) }
  Task<> afterPlayerTurnStart() override {
    if (!combat || combat->turnNumber > 1) co_return;
    doFlash();
    for (Card* c : combat->hand) c->upgrade();
  }
};

// ---- CaptainsWheel: on turn 3, gain 18 block. ----
struct CaptainsWheel : Relic {
  RELIC_HEADER(CaptainsWheel, "CAPTAINS_WHEEL", Rare)
    addVar("Block", 18);
  }
  Task<> afterBlockCleared(Creature* creature) override {
    if (!combat || creature != owner() || combat->turnNumber != 3) co_return;
    doFlash();
    co_await cmd::gainBlock(owner(), val("Block"), kUnpowered, nullptr);
  }
};

// ---- Chandelier: on turn 3, gain 3 energy. ----
struct Chandelier : Relic {
  RELIC_HEADER(Chandelier, "CHANDELIER", Rare)
    addVar("Energy", 3);
  }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!combat || std::find(participants.begin(), participants.end(), owner()) == participants.end()) co_return;
    if (combat->turnNumber == 3) {
      doFlash();
      co_await cmd::gainEnergy(*combat, val("Energy").toInt());
    }
  }
};

// ---- CloakClasp: at end of turn, gain block per card left in hand. ----
struct CloakClasp : Relic {
  RELIC_HEADER(CloakClasp, "CLOAK_CLASP", Rare)
    addVar("Block", 1);
  }
  Task<> beforeSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (!combat || std::find(participants.begin(), participants.end(), owner()) == participants.end()) co_return;
    if (!combat->hand.empty()) {
      int amount = (int)(Dec((int)combat->hand.size()) * val("Block")).toInt();
      doFlash();
      co_await cmd::gainBlock(owner(), amount, kUnpowered, nullptr);
    }
  }
};

// ---- Girya: SKIPPED. Needs a rest-site "Lift" option (rest site only supports heal/smith).

// ---- IceCream: SKIPPED. Needs a should-reset-energy hook (energy always resets each turn here).

// ---- IntimidatingHelmet: before playing a card costing >=2 energy, gain 4 block. ----
struct IntimidatingHelmet : Relic {
  RELIC_HEADER(IntimidatingHelmet, "INTIMIDATING_HELMET", Rare)
    addVar("Block", 4);
    addVar("Energy", 2);
  }
  Task<> beforeCardPlayed(const CardPlay& p) override {
    if (!combat) co_return;
    if (p.energySpent >= val("Energy").toInt()) {
      doFlash();
      co_await cmd::gainBlock(owner(), val("Block"), kUnpowered, nullptr);
    }
  }
};

// ---- Kunai: every 3rd attack played, gain 1 dexterity. ----
struct Kunai : Relic {
  RELIC_HEADER(Kunai, "KUNAI", Rare)
    addVar("Cards", 3);
    addVar("DexterityPower", 1);
  }
  int attacksPlayedThisTurn = 0;
  bool showCounter() const override { return combat != nullptr; }
  int displayAmount() const override {
    int n = const_cast<Kunai*>(this)->val("Cards").toInt();
    return n > 0 ? attacksPlayedThisTurn % n : 0;
  }
  Task<> beforeSideTurnStart(Side, const std::vector<Creature*>&) override { attacksPlayedThisTurn = 0; co_return; }
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (!combat || p.card->type != CardType::Attack) co_return;
    ++attacksPlayedThisTurn;
    int n = val("Cards").toInt();
    if (n > 0 && attacksPlayedThisTurn % n == 0) {
      doFlash();
      co_await applyPower<DexterityPower>(owner(), val("DexterityPower"), owner(), nullptr);
    }
  }
  Task<> afterCombatEnd() override { attacksPlayedThisTurn = 0; co_return; }
};

// ---- LizardTail: SKIPPED. Needs a death-prevention hook (ShouldDieLate / AfterPreventingDeath).

// ---- Mango: on pickup, gain 14 max hp. ----
struct Mango : Relic {
  RELIC_HEADER(Mango, "MANGO", Rare)
    addVar("MaxHp", 14);
  }
  Task<> afterObtained() override { co_await cmd::gainMaxHp(owner(), val("MaxHp").toInt()); }
};

// ---- MeatOnTheBone: heal 12 hp after combat if at or below 50% hp. ----
struct MeatOnTheBone : Relic {
  RELIC_HEADER(MeatOnTheBone, "MEAT_ON_THE_BONE", Rare)
    addVar("HpThreshold", 50);
    addVar("Heal", 12);
  }
  bool willHeal() const {
    Creature* p = owner();
    Dec threshold = const_cast<MeatOnTheBone*>(this)->val("HpThreshold");
    int cap = (Dec(p->maxHp) * (threshold / Dec(100))).toInt();
    return p->hp <= cap;
  }
  Task<> afterCombatVictory() override {
    if (!owner()->dead() && willHeal()) {
      doFlash();
      co_await cmd::heal(owner(), val("Heal"));
    }
  }
};

// ---- MoltenEgg: SKIPPED. Needs card-reward/merchant-reward and deck-add upgrade hooks.

// ---- MummifiedHand: after playing a power card, a random hand card costing >0 becomes free this turn. ----
struct MummifiedHand : Relic {
  RELIC_HEADER(MummifiedHand, "MUMMIFIED_HAND", Rare) }
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (!combat || p.card->type != CardType::Power) co_return;
    std::vector<Card*> costing;
    for (Card* c : combat->hand) if (c->costWithLocalMods() > 0) costing.push_back(c);
    Card* pick = costing.empty() ? combat->rng("CombatCardSelection").nextItem(combat->hand)
                                  : combat->rng("CombatCardSelection").nextItem(costing);
    if (pick) pick->setThisTurn(0);
    co_return;
  }
};

// ---- OldCoin: on pickup, gain 300 gold. Not sold in shops. ----
struct OldCoin : Relic {
  RELIC_HEADER(OldCoin, "OLD_COIN", Rare)
    addVar("Gold", 300);
  }
  bool allowedInShops() const override { return false; }
  Task<> afterObtained() override { co_await run->gainGold(val("Gold").toInt()); }
};

// ---- Pocketwatch: if <=3 cards were played last turn, draw 3 extra cards this turn. ----
struct Pocketwatch : Relic {
  RELIC_HEADER(Pocketwatch, "POCKETWATCH", Rare)
    addVar("CardThreshold", 3);
    addVar("Cards", 3);
  }
  int cardsPlayedThisTurn = 0, cardsPlayedLastTurn = 0;
  bool showCounter() const override { return combat != nullptr; }
  int displayAmount() const override { return cardsPlayedThisTurn; }
  Task<> afterCardPlayed(const CardPlay&) override {
    if (!combat) co_return;
    ++cardsPlayedThisTurn;
  }
  Dec modifyHandDraw(Dec count) override {
    if (!combat || combat->turnNumber == 1) return count;
    if (Dec(cardsPlayedLastTurn) > val("CardThreshold")) return count;
    doFlash();
    return count + val("Cards");
  }
  Task<> beforeSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!combat || std::find(participants.begin(), participants.end(), owner()) == participants.end()) co_return;
    cardsPlayedLastTurn = cardsPlayedThisTurn;
    cardsPlayedThisTurn = 0;
  }
  Task<> afterCombatEnd() override { cardsPlayedThisTurn = 0; cardsPlayedLastTurn = 0; co_return; }
};

// ---- PrayerWheel: SKIPPED. Needs card-reward modification (add an extra reward).

// ---- RainbowRing: play an attack, skill and power in the same turn -> gain 1 str, 1 dex. ----
struct RainbowRing : Relic {
  RELIC_HEADER(RainbowRing, "RAINBOW_RING", Rare)
    addVar("StrengthPower", 1);
    addVar("DexterityPower", 1);
  }
  int attacks = 0, skills = 0, powers = 0, activations = 0;
  void persist(Archive& a) override { a.io(attacks); a.io(skills); a.io(powers); a.io(activations); }
  Task<> beforeSideTurnStart(Side, const std::vector<Creature*>&) override {
    attacks = skills = powers = activations = 0;
    co_return;
  }
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (!combat || activations >= 1) co_return;
    if (p.card->type == CardType::Attack) ++attacks;
    else if (p.card->type == CardType::Skill) ++skills;
    else if (p.card->type == CardType::Power) ++powers;
    if (attacks > 0 && skills > 0 && powers > 0) {
      doFlash();
      co_await applyPower<StrengthPower>(owner(), val("StrengthPower"), owner(), nullptr);
      co_await applyPower<DexterityPower>(owner(), val("DexterityPower"), owner(), nullptr);
      ++activations;
    }
  }
  Task<> afterCombatEnd() override { attacks = skills = powers = activations = 0; co_return; }
};

// ---- RazorTooth: upgrade attacks and skills as they're played. ----
struct RazorTooth : Relic {
  RELIC_HEADER(RazorTooth, "RAZOR_TOOTH", Rare) }
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (p.card->type != CardType::Attack && p.card->type != CardType::Skill) co_return;
    if (!p.card->upgradable()) co_return;
    p.card->upgrade();
    co_return;
  }
};

// ---- Shovel: SKIPPED. Needs a rest-site "Dig" option (rest site only supports heal/smith).

// ---- Shuriken: every 3rd attack played, gain 1 strength. ----
struct Shuriken : Relic {
  RELIC_HEADER(Shuriken, "SHURIKEN", Rare)
    addVar("Cards", 3);
    addVar("StrengthPower", 1);
  }
  int attacksPlayedThisTurn = 0;
  bool showCounter() const override { return combat != nullptr; }
  int displayAmount() const override {
    int n = const_cast<Shuriken*>(this)->val("Cards").toInt();
    return n > 0 ? attacksPlayedThisTurn % n : 0;
  }
  Task<> beforeSideTurnStart(Side, const std::vector<Creature*>&) override { attacksPlayedThisTurn = 0; co_return; }
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (!combat || p.card->type != CardType::Attack) co_return;
    ++attacksPlayedThisTurn;
    int n = val("Cards").toInt();
    if (n > 0 && attacksPlayedThisTurn % n == 0) {
      doFlash();
      co_await applyPower<StrengthPower>(owner(), val("StrengthPower"), owner(), nullptr);
    }
  }
  Task<> afterCombatEnd() override { attacksPlayedThisTurn = 0; co_return; }
};

// ---- StoneCalendar: on turn 7's end, deal 52 unblockable-unpowered damage to all enemies. ----
struct StoneCalendar : Relic {
  RELIC_HEADER(StoneCalendar, "STONE_CALENDAR", Rare)
    addVar("Damage", 52);
    addVar("DamageTurn", 7);
  }
  Task<> beforeSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (!combat || std::find(participants.begin(), participants.end(), owner()) == participants.end()) co_return;
    if (combat->turnNumber == val("DamageTurn").toInt()) {
      doFlash();
      co_await cmd::damage(combat->hittableEnemies(), val("Damage"), kUnpowered, owner(), nullptr);
    }
  }
};

// ---- SturdyClamp: your block doesn't clear; anything over 10 is lost. ----
struct SturdyClamp : Relic {
  RELIC_HEADER(SturdyClamp, "STURDY_CLAMP", Rare)
    addVar("Block", 10);
  }
  bool clamped = false;
  bool shouldClearBlock(Creature* creature) override {
    if (!combat || creature != owner()) return true;
    clamped = true;
    return false;
  }
  Task<> afterBlockCleared(Creature*) override { co_return; }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>&) override {
    if (!combat || !clamped) co_return;
    clamped = false;
    Creature* p = owner();
    int cap = val("Block").toInt();
    if (p->block > cap) {
      p->block = cap;
      doFlash();
    }
  }
};

// ---- TheCourier: the merchant restocks what you buy, and everything costs 20% less. ----
struct TheCourier : Relic {
  RELIC_HEADER(TheCourier, "THE_COURIER", Rare) addVar("Discount", 20); }
  bool allowedInShops() const override { return false; }
  Dec modifyMerchantPrice(Dec price) override { return price * (Dec(1) - val("Discount") / Dec(100)); }
  bool shouldRefillMerchantEntry() override { return true; }
};

// ---- MembershipCard (Shop): the merchant's prices are halved. ----
struct MembershipCard : Relic {
  RELIC_HEADER(MembershipCard, "MEMBERSHIP_CARD", Shop) addVar("Discount", 50); }
  Dec modifyMerchantPrice(Dec price) override { return price * (val("Discount") / Dec(100)); }
};

// ---- ToxicEgg: SKIPPED. Needs card-reward/merchant-reward and deck-add upgrade hooks.

// ---- TungstenRod: reduce hp loss "after Osty" (trample) by 1. ----
struct TungstenRod : Relic {
  RELIC_HEADER(TungstenRod, "TUNGSTEN_ROD", Rare)
    addVar("HpLossReduction", 1);
  }
  Dec modifyHpLostAfterOsty(Creature* target, Dec amount, int, Creature*, Card*) override {
    if (!combat || target != owner()) return amount;
    Dec reduced = dmax(Dec(0), amount - val("HpLossReduction"));
    if (reduced < amount) doFlash();
    return reduced;
  }
};

// ---- UnceasingTop: whenever your hand is empty (after playing a card), draw 1. ----
// PORT NOTE: the C# version only triggers during the main play phase (not while cards
// are auto-drawing/discarding); approximated here as "hand empties from a card play".
struct UnceasingTop : Relic {
  RELIC_HEADER(UnceasingTop, "UNCEASING_TOP", Rare) }
  Task<> afterCardPlayed(const CardPlay&) override {
    if (!combat || !combat->hand.empty()) co_return;
    doFlash();
    co_await cmd::drawCards(*combat, 1);
  }
};

// ---- GamblingChip: on turn 1, discard any number of cards and redraw that many. ----
struct GamblingChip : Relic {
  RELIC_HEADER(GamblingChip, "GAMBLING_CHIP", Rare) }
  Task<> afterPlayerTurnStart() override {
    if (!combat || combat->turnNumber > 1) co_return;
    auto picked = co_await cmd::selectCards(*combat, locKey, combat->hand, 0, (int)combat->hand.size());
    if (picked.empty()) co_return;
    doFlash();
    for (Card* c : picked) co_await cmd::moveCard(*combat, c, Pile::Discard);
    co_await cmd::drawCards(*combat, (int)picked.size());
  }
};

// ---- UnsettlingLamp: SKIPPED. Needs power-amount-given-multiplicative / before-power-amount
// hooks to double the first debuff a card applies to an enemy.

// ---- VexingPuzzlebox: on turn 1, add a random Ironclad card to hand, free this turn. ----
// PORT NOTE: picks from the whole Ironclad pool rather than the player's unlocked pool
// (no unlock system in this build).
struct VexingPuzzlebox : Relic {
  RELIC_HEADER(VexingPuzzlebox, "VEXING_PUZZLEBOX", Rare) }
  Task<> afterPlayerTurnStart() override {
    if (!combat || combat->turnNumber != 1) co_return;
    doFlash();
    auto ids = db::ironcladCards([](const Card&) { return true; });
    if (ids.empty()) co_return;
    const std::string& id = combat->rng("CombatCardGeneration").nextItem(ids);
    auto card = db::card(id);
    if (!card) co_return;
    card->setThisTurn(0);
    co_await cmd::addGeneratedCard(*combat, std::move(card), Pile::Hand);
  }
};

// ---- WhiteBeastStatue: SKIPPED. Needs the potion system (not implemented).

// ---- WhiteStar: SKIPPED. Needs card-reward modification (add an extra reward).

// ---- CharonsAshes (Ironclad): whenever a card is exhausted, deal 3 unpowered damage to all enemies.
struct CharonsAshes : Relic {
  RELIC_HEADER(CharonsAshes, "CHARONS_ASHES", Rare)
    addVar("Damage", 3);
  }
  Task<> afterCardExhausted(Card*, bool) override {
    if (!combat) co_return;
    doFlash();
    co_await cmd::damage(combat->hittableEnemies(), val("Damage"), kUnpowered, owner(), nullptr);
  }
};

// ---- DemonTongue (Ironclad): the first time you take damage on your turn, heal that much. ----
struct DemonTongue : Relic {
  RELIC_HEADER(DemonTongue, "DEMON_TONGUE", Rare) }
  bool triggeredThisTurn = false;
  Task<> afterDamageReceived(Creature* target, const DamageResult& result, int, Creature*, Card*) override {
    if (!combat || combat->currentSide != Side::Player || target != owner()) co_return;
    if (result.unblocked <= 0 || triggeredThisTurn) co_return;
    triggeredThisTurn = true;
    doFlash();
    co_await cmd::heal(owner(), Dec(result.unblocked));
  }
  Task<> beforeSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!combat || std::find(participants.begin(), participants.end(), owner()) == participants.end()) co_return;
    triggeredThisTurn = false;
  }
};

// ---- RuinedHelmet (Ironclad): the first Strength you gain each combat is doubled. ----
struct RuinedHelmet : Relic {
  RELIC_HEADER(RuinedHelmet, "RUINED_HELMET", Rare) }
  bool usedThisCombat = false;
  bool tryModifyPowerAmountReceived(Power* incoming, Creature* target, Dec amount, Creature*, Dec& out) override {
    if (incoming->id != StrengthPower::kId) return false;
    if (target != owner() || amount <= Dec(0) || usedThisCombat) return false;
    out = amount * Dec(2);
    return true;
  }
  Task<> afterModifyingPowerAmountReceived(Power*) override {
    doFlash();
    usedThisCombat = true;
    co_return;
  }
  Task<> afterCombatEnd() override { usedThisCombat = false; co_return; }
};

void registerRelicsRare() {
  reg<ArtOfWar>();
  reg<BeatingRemnant>();
  reg<Bellows>();
  reg<CaptainsWheel>();
  reg<Chandelier>();
  reg<CloakClasp>();
  reg<GamblingChip>();
  reg<IntimidatingHelmet>();
  reg<Kunai>();
  reg<Mango>();
  reg<MeatOnTheBone>();
  reg<MummifiedHand>();
  reg<OldCoin>();
  reg<TheCourier>();
  reg<MembershipCard>();
  reg<Pocketwatch>();
  reg<RainbowRing>();
  reg<RazorTooth>();
  reg<Shuriken>();
  reg<StoneCalendar>();
  reg<SturdyClamp>();
  reg<TungstenRod>();
  reg<UnceasingTop>();
  reg<VexingPuzzlebox>();
  reg<CharonsAshes>();
  reg<DemonTongue>();
  reg<RuinedHelmet>();
}

}  // namespace sts
