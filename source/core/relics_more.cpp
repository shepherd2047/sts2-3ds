// Package 10: the shared and Ironclad pool relics that needed potions, the merchant, card
// reward / deck hooks, rest site options or death prevention. Translated from
// MegaCrit.Sts2.Core.Models.Relics.
//
// Still skipped (no enchantments / colorless card pool yet): DingyRug, FresnelLens,
// GnarledHammer, Kifuda, MysticLighter, PunchDagger, RoyalStamp, Toolbox, WingCharm; and
// UnsettlingLamp (needs Hook.ModifyPowerAmountGivenMultiplicative). RelicModel.IsAllowed
// (IsBeforeAct3TreasureChest) is not checked for any relic.
#include "cards.h"

namespace sts {

namespace {

template <class R> void reg() { db::registerRelic(R::kId, [] { return std::unique_ptr<Relic>(new R()); }); }

bool inCombat(const Relic* r) { return r->combat && r->combat->inProgress; }
bool isCombatRoom(RoomType t) { return t == RoomType::Monster || t == RoomType::Elite || t == RoomType::Boss; }

// CreatureCmd.Heal / GainMaxHp in or out of combat.
Task<> healPlayer(Relic* r, int amount) {
  if (inCombat(r)) { co_await cmd::heal(r->owner(), Dec(amount)); co_return; }
  Creature* p = r->owner();
  p->hp = std::min(p->maxHp, p->hp + amount);
  co_return;
}
Task<> gainMaxHpPlayer(Relic* r, int amount) {
  if (inCombat(r)) co_await cmd::gainMaxHp(r->owner(), amount);
  else co_await r->run->gainMaxHp(amount);
}

// EggRelicHelper.UpgradeValidCards: upgrade every offered card of the type.
void upgradeOfType(std::vector<std::unique_ptr<Card>>& cards, CardType type) {
  for (auto& c : cards) if (c && c->type == type && c->upgradable()) c->upgrade();
}

}  // namespace

// ---- AmethystAubergine (Common): combat rooms give 15 more gold (not the last boss). ----
struct AmethystAubergine : Relic {
  RELIC_HEADER(AmethystAubergine, "AMETHYST_AUBERGINE", Common) addVar("Gold", 15); }
  bool allowedInShops() const override { return false; }
  int extraCombatGold(RoomType room) override {
    if (!isCombatRoom(room)) return 0;
    doFlash();
    return val("Gold").toInt();
  }
};

// ---- BeltBuckle (Shop): 2 Dexterity while the potion belt is empty. ----
struct BeltBuckle : Relic {
  RELIC_HEADER(BeltBuckle, "BELT_BUCKLE", Shop) addVar("DexterityPower", 2); }
  bool applied = false;
  bool noPotions() const {
    for (auto& p : run->potions) if (p) return false;
    return true;
  }
  Task<> applyDex(int sign) {
    if ((sign > 0) == applied) co_return;
    applied = sign > 0;
    doFlash();
    co_await applyPower<DexterityPower>(owner(), val("DexterityPower") * Dec(sign), nullptr, nullptr);
  }
  Task<> afterObtained() override { if (inCombat(this) && noPotions()) co_await applyDex(1); }
  Task<> beforeCombatStart() override {
    applied = false;
    if (noPotions()) co_await applyDex(1);
  }
  Task<> afterPotionProcured() override { if (inCombat(this) && !noPotions()) co_await applyDex(-1); }
  Task<> afterPotionDiscarded() override { if (inCombat(this) && noPotions()) co_await applyDex(1); }
  Task<> afterPotionUsed() override { if (inCombat(this) && noPotions()) co_await applyDex(1); }
  Task<> afterCombatVictory() override { applied = false; return {}; }
};

// ---- BookOfFiveRings (Common): every 5th card added to the deck heals 20. ----
struct BookOfFiveRings : Relic {
  RELIC_HEADER(BookOfFiveRings, "BOOK_OF_FIVE_RINGS", Common) addVar("Cards", 5); addVar("Heal", 20); }
  int cardsAdded = 0;
  bool showCounter() const override { return true; }
  int displayAmount() const override { return cardsAdded % 5; }
  void afterCardAddedToDeck(Card*) override {
    if (owner()->dead() || ++cardsAdded % val("Cards").toInt() != 0) return;
    doFlash();
    Scheduler::get().spawn(healPlayer(this, val("Heal").toInt()));
  }
};

// ---- Bread (Shop): lose 2 energy on turn 1, then 1 more energy every turn. ----
struct Bread : Relic {
  RELIC_HEADER(Bread, "BREAD", Shop) addVar("GainEnergy", 1); addVar("LoseEnergy", 2); }
  Dec modifyMaxEnergy(Dec amount) override {
    if (combat && combat->turnNumber == 1) return amount;
    return amount + val("GainEnergy");
  }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!combat || !contains(participants, owner()) || combat->turnNumber != 1) co_return;
    doFlash();
    combat->energy = std::max(0, combat->energy - val("LoseEnergy").toInt());
  }
};

// ---- BronzeScales (Common): start each combat with 3 Thorns. ----
struct BronzeScales : Relic {
  RELIC_HEADER(BronzeScales, "BRONZE_SCALES", Common) addVar("ThornsPower", 3); }
  Task<> afterRoomEntered(RoomType room) override {
    if (!isCombatRoom(room)) co_return;
    doFlash();
    co_await cmd::applyPower(db::power("ThornsPower"), owner(), val("ThornsPower"), owner(), nullptr);
  }
};

// ---- BurningSticks (Shop): the first Skill exhausted each combat returns a copy to hand. ----
struct BurningSticks : Relic {
  RELIC_HEADER(BurningSticks, "BURNING_STICKS", Shop) }
  bool usedThisCombat = false;
  Task<> afterRoomEntered(RoomType room) override {
    if (isCombatRoom(room)) usedThisCombat = false;
    return {};
  }
  Task<> afterCardExhausted(Card* card, bool) override {
    if (usedThisCombat || !combat || card->type != CardType::Skill) co_return;
    doFlash();
    usedThisCombat = true;
    co_await cmd::addGeneratedCard(*combat, card->clone(), Pile::Hand);
  }
};

// ---- Cauldron (Shop): on pickup, five potion rewards. ----
struct Cauldron : Relic {
  RELIC_HEADER(Cauldron, "CAULDRON", Shop) addVar("Potions", 5); }
  Task<> afterObtained() override {
    for (int i = 0; i < val("Potions").toInt(); ++i) co_await run->offerPotion(run->randomPotion(run->rng("Rewards"), false));
  }
};

// ---- ChemicalX (Shop): X costs count 2 more. ----
struct ChemicalX : Relic {
  RELIC_HEADER(ChemicalX, "CHEMICAL_X", Shop) addVar("Increase", 2); }
  int modifyXValue(Card*, int x) override {
    doFlash();
    return x + val("Increase").toInt();
  }
};

// ---- DollysMirror (Shop): on pickup, duplicate a card in the deck. ----
struct DollysMirror : Relic {
  RELIC_HEADER(DollysMirror, "DOLLYS_MIRROR", Shop) }
  Task<> afterObtained() override {
    auto picked = co_await run->selectFromDeck("relics.DOLLYS_MIRROR.selectionScreenPrompt", nullptr, 1);
    if (!picked.empty()) run->addCardToDeck(picked[0]->clone());
  }
};

// ---- DragonFruit (Shop): gaining gold also gives 1 max HP. ----
struct DragonFruit : Relic {
  RELIC_HEADER(DragonFruit, "DRAGON_FRUIT", Shop) addVar("MaxHp", 1); }
  Task<> afterGoldGained(int) override {
    doFlash();
    co_await gainMaxHpPlayer(this, val("MaxHp").toInt());
  }
};

// ---- The eggs (Rare): new cards of one type are upgraded (rewards, merchant, deck). ----
template <CardType T> struct EggRelic : Relic {
  bool upgradesNewCard(const Card& c) override { return c.type == T && c.upgradable(); }
  void modifyCardReward(std::vector<std::unique_ptr<Card>>& cards, RoomType, bool late) override {
    if (late) upgradeOfType(cards, T);
  }
};
struct FrozenEgg : EggRelic<CardType::Power> { RELIC_HEADER(FrozenEgg, "FROZEN_EGG", Rare) } };
struct MoltenEgg : EggRelic<CardType::Attack> { RELIC_HEADER(MoltenEgg, "MOLTEN_EGG", Rare) } };
struct ToxicEgg : EggRelic<CardType::Skill> { RELIC_HEADER(ToxicEgg, "TOXIC_EGG", Rare) } };

// ---- GamePiece (Rare): playing a Power draws a card. ----
struct GamePiece : Relic {
  RELIC_HEADER(GamePiece, "GAME_PIECE", Rare) addVar("Cards", 1); }
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (!inCombat(this) || p.card->type != CardType::Power) co_return;
    doFlash();
    co_await cmd::drawCards(*combat, val("Cards"));
  }
};

// ---- GhostSeed (Shop): basic Strikes and Defends are Ethereal. ----
struct GhostSeed : Relic {
  RELIC_HEADER(GhostSeed, "GHOST_SEED", Shop) }
  static bool canAffect(const Card* c) { return c->rarity == Rarity::Basic && (c->tags & (tagStrike | tagDefend)); }
  Task<> afterRoomEntered(RoomType room) override {
    if (!isCombatRoom(room) || !combat) return {};
    for (Card* c : combat->allCards()) if (canAffect(c)) c->keywords |= kwEthereal;
    return {};
  }
  Task<> afterCardEnteredCombat(Card* c) override {
    if (canAffect(c)) c->keywords |= kwEthereal;
    return {};
  }
};

// ---- Girya (Rare): rest site Lift (3 times): +1 Strength at the start of each combat. ----
struct Girya : Relic {
  RELIC_HEADER(Girya, "GIRYA", Rare) }
  int timesLifted = 0;
  bool showCounter() const override { return true; }
  int displayAmount() const override { return timesLifted; }
  void restSiteAction(int) override { ++timesLifted; }
  Task<> afterRoomEntered(RoomType room) override {
    if (timesLifted <= 0 || !isCombatRoom(room)) co_return;
    doFlash();
    co_await applyPower<StrengthPower>(owner(), timesLifted, owner(), nullptr);
  }
};

// ---- IceCream (Rare): energy carries over between turns. ----
struct IceCream : Relic {
  RELIC_HEADER(IceCream, "ICE_CREAM", Rare) }
  bool shouldResetEnergy() override { return !combat || combat->turnNumber == 1; }
};

// ---- JuzuBracelet (Common): "?" rooms are never fights (Run::rollUnknownRoom). ----
struct JuzuBracelet : Relic {
  RELIC_HEADER(JuzuBracelet, "JUZU_BRACELET", Common) }
};

// ---- LastingCandy (Uncommon): every other combat's card reward offers an extra Power. ----
struct LastingCandy : Relic {
  RELIC_HEADER(LastingCandy, "LASTING_CANDY", Uncommon) }
  int combatRewardsSeen = 0;
  bool showCounter() const override { return true; }
  int displayAmount() const override { return combatRewardsSeen % 2; }
  Task<> afterCombatVictory() override { ++combatRewardsSeen; return {}; }
  void modifyCardReward(std::vector<std::unique_ptr<Card>>& cards, RoomType room, bool late) override {
    if (late || !isCombatRoom(room) || combatRewardsSeen % 2 != 0) return;
    auto ids = db::ironcladCards([&](const Card& c) {
      if (c.type != CardType::Power || c.rarity == Rarity::Basic || c.rarity == Rarity::Ancient) return false;
      for (auto& o : cards) if (o && o->id == c.id) return false;
      return true;
    });
    if (ids.empty()) return;
    doFlash();
    cards.push_back(db::card(run->rng("Rewards").nextItem(ids)));
  }
};

// ---- LavaLamp (Shop): no unblocked damage taken this fight -> the card reward is upgraded. ----
struct LavaLamp : Relic {
  RELIC_HEADER(LavaLamp, "LAVA_LAMP", Shop) }
  bool tookDamage = false;
  Task<> afterRoomEntered(RoomType) override { tookDamage = false; return {}; }
  Task<> afterDamageReceived(Creature* target, const DamageResult& r, int props, Creature*, Card*) override {
    if (target == owner() && r.unblocked > 0 && !(props & kUnblockable)) tookDamage = true;
    return {};
  }
  void modifyCardReward(std::vector<std::unique_ptr<Card>>& cards, RoomType room, bool late) override {
    if (!late || !isCombatRoom(room) || tookDamage) return;
    for (auto& c : cards) if (c && c->upgradable()) c->upgrade();
  }
};

// ---- LeesWaffle (Shop): on pickup, +7 max HP and heal to full. ----
struct LeesWaffle : Relic {
  RELIC_HEADER(LeesWaffle, "LEES_WAFFLE", Shop) addVar("MaxHp", 7); }
  Task<> afterObtained() override {
    co_await gainMaxHpPlayer(this, val("MaxHp").toInt());
    co_await healPlayer(this, owner()->maxHp - owner()->hp);
  }
};

// ---- LizardTail (Rare): once, survive death at 50% HP (Run::preventDeath). ----
struct LizardTail : Relic {
  RELIC_HEADER(LizardTail, "LIZARD_TAIL", Rare) addVar("Heal", 50); }
};

// ---- LoomingFruit (Ancient): on pickup, +31 max HP. ----
struct LoomingFruit : Relic {
  RELIC_HEADER(LoomingFruit, "LOOMING_FRUIT", Ancient) addVar("MaxHp", 31); }
  Task<> afterObtained() override { co_await gainMaxHpPlayer(this, val("MaxHp").toInt()); }
};

// ---- LuckyFysh (Uncommon): 15 gold for every card added to the deck. ----
struct LuckyFysh : Relic {
  RELIC_HEADER(LuckyFysh, "LUCKY_FYSH", Uncommon) addVar("Gold", 15); }
  bool allowedInShops() const override { return false; }
  void afterCardAddedToDeck(Card*) override {
    doFlash();
    Scheduler::get().spawn(run->gainGold(val("Gold").toInt()));
  }
};

// ---- MiniatureTent (Shop): rest site options stay open after using one (Run::restSite). ----
struct MiniatureTent : Relic {
  RELIC_HEADER(MiniatureTent, "MINIATURE_TENT", Shop) }
};

// ---- Orrery (Shop): on pickup, five card rewards. ----
struct Orrery : Relic {
  RELIC_HEADER(Orrery, "ORRERY", Shop) addVar("Cards", 5); }
  Task<> afterObtained() override {
    for (int i = 0; i < val("Cards").toInt(); ++i) co_await run->chooseCardFor(run->cardReward(RoomType::Monster, 3));
  }
};

// ---- PetrifiedToad (Uncommon): a Potion-Shaped Rock at the start of each combat. ----
struct PetrifiedToad : Relic {
  RELIC_HEADER(PetrifiedToad, "PETRIFIED_TOAD", Uncommon) }
  Task<> beforeCombatStart() override {
    if (run->procurePotion(db::potion("PotionShapedRock"))) doFlash();
    return {};
  }
};

// ---- Planisphere (Uncommon): heal 5 entering a "?" room. ----
struct Planisphere : Relic {
  RELIC_HEADER(Planisphere, "PLANISPHERE", Uncommon) addVar("Heal", 5); }
  Task<> afterUnknownRoomEntered() override {
    if (owner()->dead()) co_return;
    doFlash();
    co_await healPlayer(this, val("Heal").toInt());
  }
};

// ---- PotionBelt (Common): two more potion slots. ----
struct PotionBelt : Relic {
  RELIC_HEADER(PotionBelt, "POTION_BELT", Common) addVar("PotionSlots", 2); }
  Task<> afterObtained() override {
    run->potions.resize(run->potions.size() + (size_t)val("PotionSlots").toInt());
    return {};
  }
};

// ---- PrayerWheel (Rare): normal fights give a second card reward. ----
struct PrayerWheel : Relic {
  RELIC_HEADER(PrayerWheel, "PRAYER_WHEEL", Rare) }
  std::vector<RoomType> extraCardRewards(RoomType room) override {
    if (room != RoomType::Monster) return {};
    doFlash();
    return {RoomType::Monster};
  }
};

// ---- ReptileTrinket (Uncommon): drinking a potion gives 3 Strength this turn. ----
struct ReptileTrinket : Relic {
  RELIC_HEADER(ReptileTrinket, "REPTILE_TRINKET", Uncommon) addVar("StrengthPower", 3); }
  Task<> afterPotionUsed() override {
    if (!inCombat(this)) co_return;
    doFlash();
    co_await cmd::applyPower(db::power("ReptileTrinketPower"), owner(), val("StrengthPower"), owner(), nullptr);
  }
};

// ---- RingingTriangle (Shop): the hand is kept at the end of turn 1. ----
struct RingingTriangle : Relic {
  RELIC_HEADER(RingingTriangle, "RINGING_TRIANGLE", Shop) }
  bool shouldFlush() override { return !combat || combat->turnNumber > 1; }
};

// ---- ScreamingFlagon (Shop): ending the turn with an empty hand deals 20 to all enemies. ----
struct ScreamingFlagon : Relic {
  RELIC_HEADER(ScreamingFlagon, "SCREAMING_FLAGON", Shop) addVar("Damage", 20); }
  Task<> beforeSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (!combat || !contains(participants, owner()) || !combat->hand.empty()) co_return;
    doFlash();
    co_await cmd::damage(combat->hittableEnemies(), val("Damage"), kUnpowered, owner(), nullptr);
  }
};

// ---- Shovel (Rare): rest site Dig for a relic (Run::restSite). ----
struct Shovel : Relic {
  RELIC_HEADER(Shovel, "SHOVEL", Rare) }
};

// ---- SlingOfCourage (Shop): 2 Strength in elite fights. ----
struct SlingOfCourage : Relic {
  RELIC_HEADER(SlingOfCourage, "SLING_OF_COURAGE", Shop) addVar("StrengthPower", 2); }
  Task<> afterRoomEntered(RoomType room) override {
    if (room != RoomType::Elite) co_return;
    doFlash();
    co_await applyPower<StrengthPower>(owner(), val("StrengthPower"), owner(), nullptr);
  }
};

// ---- TheAbacus (Shop): 6 Block whenever the draw pile is shuffled. ----
struct TheAbacus : Relic {
  RELIC_HEADER(TheAbacus, "THE_ABACUS", Shop) addVar("Block", 6); }
  Task<> afterShuffle() override {
    doFlash();
    co_await cmd::gainBlock(owner(), val("Block"), kUnpowered, nullptr);
  }
};

// ---- TinyMailbox (Uncommon): resting also gives two potion rewards (Run::restSite). ----
struct TinyMailbox : Relic {
  RELIC_HEADER(TinyMailbox, "TINY_MAILBOX", Uncommon) }
};

// ---- VeryHotCocoa (Ancient): 4 energy on turn 1. ----
struct VeryHotCocoa : Relic {
  RELIC_HEADER(VeryHotCocoa, "VERY_HOT_COCOA", Ancient) addVar("Energy", 4); }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!combat || !contains(participants, owner()) || combat->turnNumber > 1) co_return;
    doFlash();
    co_await cmd::gainEnergy(*combat, val("Energy").toInt());
  }
};

// ---- WhiteBeastStatue (Rare): every combat drops a potion. ----
struct WhiteBeastStatue : Relic {
  RELIC_HEADER(WhiteBeastStatue, "WHITE_BEAST_STATUE", Rare) }
  bool shouldForcePotionReward(RoomType room) override { return isCombatRoom(room); }
};

// ---- WhiteStar (Rare): elites give an extra card reward with boss odds. ----
struct WhiteStar : Relic {
  RELIC_HEADER(WhiteStar, "WHITE_STAR", Rare) }
  std::vector<RoomType> extraCardRewards(RoomType room) override {
    if (room != RoomType::Elite) return {};
    doFlash();
    return {RoomType::Boss};
  }
};

// ---- Brimstone (Shop, Ironclad): each turn +2 Strength, enemies +1 Strength. ----
struct Brimstone : Relic {
  RELIC_HEADER(Brimstone, "BRIMSTONE", Shop) addVar("SelfStrength", 2); addVar("EnemyStrength", 1); }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!combat || !contains(participants, owner())) co_return;
    doFlash();
    co_await applyPower<StrengthPower>(owner(), val("SelfStrength"), owner(), nullptr);
    for (Creature* e : combat->aliveEnemies()) co_await applyPower<StrengthPower>(e, val("EnemyStrength"), nullptr, nullptr);
  }
};

// ---- PaperPhrog (Uncommon, Ironclad): Vulnerable enemies take 75% more (VulnerablePower). ----
struct PaperPhrog : Relic {
  RELIC_HEADER(PaperPhrog, "PAPER_PHROG", Uncommon) }
};

void registerRelicsMore() {
  reg<AmethystAubergine>();
  reg<BeltBuckle>();
  reg<BookOfFiveRings>();
  reg<Bread>();
  reg<BronzeScales>();
  reg<BurningSticks>();
  reg<Cauldron>();
  reg<ChemicalX>();
  reg<DollysMirror>();
  reg<DragonFruit>();
  reg<FrozenEgg>();
  reg<MoltenEgg>();
  reg<ToxicEgg>();
  reg<GamePiece>();
  reg<GhostSeed>();
  reg<Girya>();
  reg<IceCream>();
  reg<JuzuBracelet>();
  reg<LastingCandy>();
  reg<LavaLamp>();
  reg<LeesWaffle>();
  reg<LizardTail>();
  reg<LoomingFruit>();
  reg<LuckyFysh>();
  reg<MiniatureTent>();
  reg<Orrery>();
  reg<PetrifiedToad>();
  reg<Planisphere>();
  reg<PotionBelt>();
  reg<PrayerWheel>();
  reg<ReptileTrinket>();
  reg<RingingTriangle>();
  reg<ScreamingFlagon>();
  reg<Shovel>();
  reg<SlingOfCourage>();
  reg<TheAbacus>();
  reg<TinyMailbox>();
  reg<VeryHotCocoa>();
  reg<WhiteBeastStatue>();
  reg<WhiteStar>();
  reg<Brimstone>();
  reg<PaperPhrog>();
}

}  // namespace sts
