// Uncommon relics (RelicRarity.Uncommon) from SharedRelicPool.
#include "cards.h"

namespace sts {

namespace {
template <class R> void reg() { db::registerRelic(R::kId, [] { return std::unique_ptr<Relic>(new R()); }); }

// VigorPower (MegaCrit.Sts2.Core.Models.Powers.VigorPower): the next Attack
// card deals extra damage; consumed once that card finishes resolving (all
// hits of it, since it's tracked by card identity rather than per-hit).
struct VigorPower : Power {
  POWER_HEADER(VigorPower, "VIGOR_POWER")
  Card* consuming = nullptr;
  Dec modifyDamageAdditive(Creature*, Dec, int props, Creature* dealer, Card* src) override {
    if (owner != dealer || !isPoweredAttack(props)) return 0;
    if (consuming && src != consuming) return 0;
    return amount;
  }
  Task<> beforeCardPlayed(const CardPlay& p) override {
    if (p.card->type != CardType::Attack || ownerOf(p.card) != owner) return {};
    if (!consuming) consuming = p.card;
    return {};
  }
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (consuming && p.card == consuming) { consuming = nullptr; return cmd::removePower(this); }
    return {};
  }
};

// SelfFormingClayPower: next time block is cleared from the owner, gain the
// stacked amount of block, then the power is consumed.
struct SelfFormingClayPower : Power {
  POWER_HEADER(SelfFormingClayPower, "SELF_FORMING_CLAY_POWER")
  Task<> afterBlockCleared(Creature* creature) override {
    if (creature != owner) co_return;
    co_await cmd::gainBlock(owner, Dec(amount), kUnpowered, nullptr);
    co_await cmd::removePower(this);
  }
};

}  // namespace

struct Akabeko : Relic {
  RELIC_HEADER(Akabeko, "AKABEKO", Uncommon)
    addVar("VigorPower", 8);
  }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner()) || combat->turnNumber > 1) return {};
    doFlash();
    return applyPower<VigorPower>(owner(), val("VigorPower"), owner(), nullptr);
  }
};

struct BowlerHat : Relic {
  RELIC_HEADER(BowlerHat, "BOWLER_HAT", Uncommon)
    addVar("GoldIncrease", Dec::lit(1.25));
  }
  // PORT NOTE: IsAllowedInShops=false / IsAllowed(IsBeforeAct3TreasureChest) drop
  // this relic from shops and after act 3's treasure chest; no shops exist yet.
  Dec modifyGoldGained(Dec amount) override { return amount * val("GoldIncrease"); }
  Task<> afterGoldGained(int) override { doFlash(); return {}; }
};

struct Candelabra : Relic {
  RELIC_HEADER(Candelabra, "CANDELABRA", Uncommon)
    addVar("Energy", 2);
  }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner()) || combat->turnNumber != 2) return {};
    doFlash();
    return cmd::gainEnergy(*combat, val("Energy").toInt());
  }
};

struct EternalFeather : Relic {
  RELIC_HEADER(EternalFeather, "ETERNAL_FEATHER", Uncommon)
    addVar("Cards", 5);
    addVar("Heal", 3);
  }
  Task<> afterRoomEntered(RoomType room) override {
    if (room != RoomType::Rest || owner()->dead()) return {};
    doFlash();
    int n = (int)run->deck.size() / val("Cards").toInt();
    return cmd::heal(owner(), val("Heal") * Dec(n));
  }
};

struct GremlinHorn : Relic {
  RELIC_HEADER(GremlinHorn, "GREMLIN_HORN", Uncommon)
    addVar("Energy", 1);
    addVar("Cards", 1);
  }
  Task<> afterDeath(Creature* creature) override {
    if (creature->side == owner()->side) co_return;
    doFlash();
    co_await cmd::gainEnergy(*combat, val("Energy").toInt());
    co_await cmd::drawCards(*combat, val("Cards"));
  }
};

struct HornCleat : Relic {
  RELIC_HEADER(HornCleat, "HORN_CLEAT", Uncommon)
    addVar("Block", 14);
  }
  Task<> afterBlockCleared(Creature* creature) override {
    if (creature != owner() || combat->turnNumber != 2) co_return;
    doFlash();
    co_await cmd::gainBlock(owner(), val("Block"), kUnpowered, nullptr);
  }
};

struct JossPaper : Relic {
  RELIC_HEADER(JossPaper, "JOSS_PAPER", Uncommon)
    addVar("ExhaustAmount", 5);
    addVar("Cards", 1);
  }
  int cardsExhausted = 0;
  int etherealCount = 0;
  void persist(Archive& a) override { a.io(cardsExhausted); a.io(etherealCount); }

  bool showCounter() const override { return true; }
  int displayAmount() const override { return cardsExhausted; }

  Task<> drawIfThresholdMet() {
    int exhaustAmt = val("ExhaustAmount").toInt();
    if (cardsExhausted < exhaustAmt) co_return;
    doFlash();
    int n = cardsExhausted / exhaustAmt;
    co_await cmd::drawCards(*combat, n);
    cardsExhausted %= exhaustAmt;
  }
  Task<> afterCardExhausted(Card* card, bool causedByEthereal) override {
    if (ownerOf(card) != owner()) co_return;
    if (causedByEthereal) { ++etherealCount; co_return; }
    ++cardsExhausted;
    co_await drawIfThresholdMet();
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner())) co_return;
    cardsExhausted += etherealCount;
    etherealCount = 0;
    co_await drawIfThresholdMet();
  }
  Task<> afterCombatEnd() override { etherealCount = 0; return {}; }
};

struct Kusarigama : Relic {
  RELIC_HEADER(Kusarigama, "KUSARIGAMA", Uncommon)
    addVar("Cards", 3);
    addVar("Damage", 6);
  }
  int attacksPlayedThisTurn = 0;

  bool showCounter() const override { return true; }
  int displayAmount() const override {
    int n = 3;
    for (auto& v : vars) if (v.name == "Cards") { n = v.base.toInt(); break; }
    return n > 0 ? attacksPlayedThisTurn % n : 0;
  }

  Task<> beforeCombatStart() override { attacksPlayedThisTurn = 0; return {}; }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner())) attacksPlayedThisTurn = 0;
    return {};
  }
  Task<> afterCardPlayed(const CardPlay& cp) override {
    if (ownerOf(cp.card) != owner() || cp.card->type != CardType::Attack) co_return;
    ++attacksPlayedThisTurn;
    int n = val("Cards").toInt();
    if (attacksPlayedThisTurn % n != 0) co_return;
    Creature* target = combat->rng("CombatTargets").nextItem(combat->hittableEnemies());
    if (!target) co_return;
    doFlash();
    co_await cmd::damage(target, val("Damage"), kUnpowered, owner(), nullptr);
  }
};

struct LetterOpener : Relic {
  RELIC_HEADER(LetterOpener, "LETTER_OPENER", Uncommon)
    addVar("Cards", 3);
    addVar("Damage", 5);
  }
  int skillsPlayedThisTurn = 0;

  bool showCounter() const override { return true; }
  int displayAmount() const override {
    int n = 3;
    for (auto& v : vars) if (v.name == "Cards") { n = v.base.toInt(); break; }
    return n > 0 ? skillsPlayedThisTurn % n : 0;
  }

  Task<> beforeCombatStart() override { skillsPlayedThisTurn = 0; return {}; }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner())) skillsPlayedThisTurn = 0;
    return {};
  }
  Task<> afterCardPlayed(const CardPlay& cp) override {
    if (ownerOf(cp.card) != owner() || cp.card->type != CardType::Skill) co_return;
    ++skillsPlayedThisTurn;
    int n = val("Cards").toInt();
    if (skillsPlayedThisTurn % n != 0) co_return;
    doFlash();
    co_await cmd::damage(combat->hittableEnemies(), val("Damage"), kUnpowered, owner(), nullptr);
  }
};

struct MercuryHourglass : Relic {
  RELIC_HEADER(MercuryHourglass, "MERCURY_HOURGLASS", Uncommon)
    addVar("Damage", 3);
  }
  Task<> afterPlayerTurnStart() override {
    doFlash();
    co_await cmd::damage(combat->hittableEnemies(), val("Damage"), kUnpowered, owner(), nullptr);
  }
};

struct MiniatureCannon : Relic {
  RELIC_HEADER(MiniatureCannon, "MINIATURE_CANNON", Uncommon)
    addVar("ExtraDamage", 3);
  }
  Dec modifyDamageAdditive(Creature*, Dec, int props, Creature* dealer, Card* card) override {
    if (!isPoweredAttack(props) || !card || !card->upgraded()) return 0;
    if (dealer != owner() && ownerOf(card) != owner()) return 0;
    return val("ExtraDamage");
  }
};

struct Nunchaku : Relic {
  RELIC_HEADER(Nunchaku, "NUNCHAKU", Uncommon)
    addVar("Cards", 10);
    addVar("Energy", 1);
  }
  int attacksPlayed = 0;
  void persist(Archive& a) override { a.io(attacksPlayed); }

  bool showCounter() const override { return true; }
  int displayAmount() const override { return attacksPlayed; }

  Task<> afterCardPlayed(const CardPlay& cp) override {
    if (ownerOf(cp.card) != owner() || cp.card->type != CardType::Attack) co_return;
    attacksPlayed = (attacksPlayed + 1) % val("Cards").toInt();
    if (attacksPlayed != 0) co_return;
    doFlash();
    co_await cmd::gainEnergy(*combat, val("Energy").toInt());
  }
};

struct Orichalcum : Relic {
  RELIC_HEADER(Orichalcum, "ORICHALCUM", Uncommon)
    addVar("Block", 6);
  }
  bool shouldTrigger = false;

  // PORT NOTE: C# uses a "very early" hook so this checks Block before
  // PlatingPower would react; no Defect power exists in this port yet, so
  // beforeSideTurnEndEarly is an exact stand-in.
  Task<> beforeSideTurnEndEarly(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner()) || owner()->block > 0) return {};
    shouldTrigger = true;
    return {};
  }
  Task<> beforeSideTurnEnd(Side, const std::vector<Creature*>&) override {
    if (!shouldTrigger) co_return;
    shouldTrigger = false;
    doFlash();
    co_await cmd::gainBlock(owner(), val("Block"), kUnpowered, nullptr);
  }
  Task<> beforeSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner())) shouldTrigger = false;
    return {};
  }
};

struct OrnamentalFan : Relic {
  RELIC_HEADER(OrnamentalFan, "ORNAMENTAL_FAN", Uncommon)
    addVar("Cards", 3);
    addVar("Block", 4);
  }
  int attacksPlayedThisTurn = 0;

  bool showCounter() const override { return true; }
  int displayAmount() const override {
    int n = 3;
    for (auto& v : vars) if (v.name == "Cards") { n = v.base.toInt(); break; }
    return n > 0 ? attacksPlayedThisTurn % n : 0;
  }

  Task<> beforeSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner())) attacksPlayedThisTurn = 0;
    return {};
  }
  Task<> afterCardPlayed(const CardPlay& cp) override {
    if (ownerOf(cp.card) != owner() || cp.card->type != CardType::Attack) co_return;
    ++attacksPlayedThisTurn;
    int n = val("Cards").toInt();
    if (attacksPlayedThisTurn % n != 0) co_return;
    doFlash();
    co_await cmd::gainBlock(owner(), val("Block"), kUnpowered, nullptr);
  }
};

struct Pantograph : Relic {
  RELIC_HEADER(Pantograph, "PANTOGRAPH", Uncommon)
    addVar("Heal", 25);
  }
  Task<> beforeCombatStart() override {
    if (owner()->dead() || !combat->isBoss) return {};
    doFlash();
    return cmd::heal(owner(), val("Heal"));
  }
};

struct ParryingShield : Relic {
  RELIC_HEADER(ParryingShield, "PARRYING_SHIELD", Uncommon)
    addVar("Block", 10);
    addVar("Damage", 6);
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner()) || owner()->block < val("Block").toInt()) co_return;
    Creature* target = combat->rng("CombatTargets").nextItem(combat->hittableEnemies());
    if (!target) co_return;
    doFlash();
    co_await cmd::damage(target, val("Damage"), kUnpowered, owner(), nullptr);
  }
};

struct Pear : Relic {
  RELIC_HEADER(Pear, "PEAR", Uncommon)
    addVar("MaxHp", 10);
  }
  Task<> afterObtained() override { return cmd::gainMaxHp(owner(), val("MaxHp").toInt()); }
};

struct PenNib : Relic {
  RELIC_HEADER(PenNib, "PEN_NIB", Uncommon)
  }
  int attacksPlayed = 0;
  void persist(Archive& a) override { a.io(attacksPlayed); }
  Card* attackToDouble = nullptr;

  bool showCounter() const override { return true; }
  int displayAmount() const override { return attacksPlayed; }

  Dec modifyDamageMultiplicative(Creature*, Dec, int props, Creature* dealer, Card* card) override {
    if (!isPoweredAttack(props) || !card || dealer != owner()) return 1;
    return card == attackToDouble ? Dec(2) : Dec(1);
  }
  Task<> beforeCardPlayed(const CardPlay& p) override {
    if (p.card->type != CardType::Attack || ownerOf(p.card) != owner()) return {};
    attacksPlayed = (attacksPlayed + 1) % 10;
    if (attacksPlayed == 0) { doFlash(); attackToDouble = p.card; }
    return {};
  }
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (attackToDouble && p.card == attackToDouble) attackToDouble = nullptr;
    return {};
  }
};

struct Permafrost : Relic {
  RELIC_HEADER(Permafrost, "PERMAFROST", Uncommon)
    addVar("Block", 7);
  }
  bool activatedThisCombat = false;

  Task<> afterRoomEntered(RoomType room) override {
    if (room == RoomType::Monster || room == RoomType::Elite || room == RoomType::Boss) activatedThisCombat = false;
    return {};
  }
  Task<> afterCardPlayed(const CardPlay& cp) override {
    if (activatedThisCombat || ownerOf(cp.card) != owner() || cp.card->type != CardType::Power) co_return;
    doFlash();
    co_await cmd::gainBlock(owner(), val("Block"), kUnpowered, nullptr);
    activatedThisCombat = true;
  }
};

struct RippleBasin : Relic {
  RELIC_HEADER(RippleBasin, "RIPPLE_BASIN", Uncommon)
    addVar("Block", 4);
  }
  bool attackPlayedThisTurn = false;

  Task<> beforeSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner())) attackPlayedThisTurn = false;
    return {};
  }
  Task<> afterCardPlayed(const CardPlay& cp) override {
    if (ownerOf(cp.card) == owner() && cp.card->type == CardType::Attack) attackPlayedThisTurn = true;
    return {};
  }
  Task<> beforeSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (!contains(participants, owner()) || attackPlayedThisTurn) co_return;
    doFlash();
    co_await cmd::gainBlock(owner(), val("Block"), kUnpowered, nullptr);
  }
};

struct SelfFormingClay : Relic {
  RELIC_HEADER(SelfFormingClay, "SELF_FORMING_CLAY", Uncommon)
    addVar("BlockNextTurn", 3);
  }
  Task<> afterDamageReceived(Creature* target, const DamageResult& r, int, Creature*, Card*) override {
    if (target != owner() || r.unblocked <= 0) return {};
    return applyPower<SelfFormingClayPower>(owner(), val("BlockNextTurn"), owner(), nullptr);
  }
};

struct SparklingRouge : Relic {
  RELIC_HEADER(SparklingRouge, "SPARKLING_ROUGE", Uncommon)
    addVar("StrengthPower", 1);
    addVar("DexterityPower", 1);
  }
  Task<> afterBlockCleared(Creature* creature) override {
    if (creature != owner() || combat->turnNumber != 3) co_return;
    doFlash();
    co_await applyPower<StrengthPower>(owner(), val("StrengthPower"), owner(), nullptr);
    co_await applyPower<DexterityPower>(owner(), val("DexterityPower"), owner(), nullptr);
  }
};

struct StoneCracker : Relic {
  RELIC_HEADER(StoneCracker, "STONE_CRACKER", Uncommon)
    addVar("Cards", 2);
  }
  Task<> afterRoomEntered(RoomType room) override {
    if (room != RoomType::Monster && room != RoomType::Elite && room != RoomType::Boss) return {};
    doFlash();
    std::vector<Card*> pool;
    for (Card* c : combat->draw) if (c->upgradable()) pool.push_back(c);
    combat->rng("CombatCardSelection").shuffle(pool);
    int n = std::min((int)pool.size(), val("Cards").toInt());
    for (int i = 0; i < n; ++i) cmd::upgradeCard(pool[i]);
    return {};
  }
};

struct TuningFork : Relic {
  RELIC_HEADER(TuningFork, "TUNING_FORK", Uncommon)
    addVar("Cards", 10);
    addVar("Block", 7);
  }
  int skillsPlayed = 0;
  void persist(Archive& a) override { a.io(skillsPlayed); }

  bool showCounter() const override { return true; }
  int displayAmount() const override { return skillsPlayed; }

  Task<> afterCardPlayed(const CardPlay& cp) override {
    if (ownerOf(cp.card) != owner() || cp.card->type != CardType::Skill) co_return;
    ++skillsPlayed;
    int threshold = val("Cards").toInt();
    if (skillsPlayed < threshold) co_return;
    doFlash();
    co_await cmd::gainBlock(owner(), val("Block"), kUnpowered, nullptr);
    skillsPlayed -= threshold;
  }
};

struct Vambrace : Relic {
  RELIC_HEADER(Vambrace, "VAMBRACE", Uncommon)
  }
  Card* triggeringCard = nullptr;
  bool blockGainedThisCombat = false;

  Task<> beforeCombatStart() override {
    triggeringCard = nullptr;
    blockGainedThisCombat = false;
    return {};
  }
  Dec modifyBlockMultiplicative(Creature*, Dec, int props, Card* card) override {
    if (!(props & kMove) || !card) return 1;
    if (triggeringCard && triggeringCard != card) return 1;
    if (ownerOf(card) != owner() || blockGainedThisCombat) return 1;
    return 2;
  }
  // Stand-in for AfterModifyingBlockAmount: afterBlockGained is called with
  // the already-modified amount and the card source, same as the C# hook.
  Task<> afterBlockGained(Creature*, Dec amount, int, Card* card) override {
    if (amount <= Dec(0) || !card) return {};
    doFlash();
    triggeringCard = card;
    return {};
  }
  Task<> afterCardPlayed(const CardPlay& cp) override {
    if (!triggeringCard || cp.card != triggeringCard || blockGainedThisCombat) return {};
    blockGainedThisCombat = true;
    return {};
  }
};

void registerRelicsUncommon() {
  reg<Akabeko>();
  reg<BowlerHat>();
  reg<Candelabra>();
  reg<EternalFeather>();
  reg<GremlinHorn>();
  reg<HornCleat>();
  reg<JossPaper>();
  reg<Kusarigama>();
  reg<LetterOpener>();
  reg<MercuryHourglass>();
  reg<MiniatureCannon>();
  reg<Nunchaku>();
  reg<Orichalcum>();
  reg<OrnamentalFan>();
  reg<Pantograph>();
  reg<ParryingShield>();
  reg<Pear>();
  reg<PenNib>();
  reg<Permafrost>();
  reg<RippleBasin>();
  reg<SelfFormingClay>();
  reg<SparklingRouge>();
  reg<StoneCracker>();
  reg<TuningFork>();
  reg<Vambrace>();

  // Not registered (missing engine feature):
  //  - LastingCandy: modifies card reward options (TryModifyCardRewardOptions) —
  //    no card-reward-modification hook exists.
  //  - LuckyFysh: fires when a card is added to the deck (AfterCardChangedPiles
  //    into PileType.Deck) — no such hook exists (only combat card-pile moves).
  //  - PaperPhrog: hooks into VulnerablePower's multiplier via a hardcoded
  //    per-relic lookup (ModifyVulnerableMultiplier) baked into
  //    VulnerablePower.ModifyDamageMultiplicative in the C#; our
  //    VulnerablePower (source/core/powers.h) only special-cases
  //    CrueltyPower the same way, and extending it would mean editing an
  //    engine file.
  //  - PetrifiedToad / ReptileTrinket: potions aren't implemented
  //    (PotionCmd.TryToProcure / AfterPotionUsed).
  //  - Planisphere: needs the real map generator's "Unknown" room type
  //    reached as the very first room of the run; not modelled (map is
  //    simplified, no Unknown/Treasure/Shop nodes with that semantics).
  //  - TinyMailbox: modifies rest-site rewards to add potions
  //    (TryModifyRestSiteHealRewards) — no rest-site-reward hook, and no
  //    potions.
}

}  // namespace sts
