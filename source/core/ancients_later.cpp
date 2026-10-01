// Package 11b: the Ancients of Hive (Orobas, Pael, Tezcatara) and Glory (Nonupeipe, Tanx,
// Vakuu), the shared Darv, their relics and the cards and powers those relics need.
// Translated from MegaCrit.Sts2.Core.Models.Events / .Relics / .Cards / .Powers.
//
// Ancient options only offer registered relics (like Neow). Not ported yet, so never
// offered: ElectricShrymp, PaelsClaw, PaelsGrowth, NutritiousSoup, Glitter,
// BeautifulBracelet, TriBoomerang (enchantments); SeaGlass, PrismaticGem (other
// characters); Driftwood (reward reroll), PaelsWing (sacrifice a card reward), PaelsEye
// (extra turn), PaelsLegion (pets), GoldenCompass (golden path map), FurCoat (map marks),
// ToyBox (wax relics), WhisperingEarring (turn-1 autoplay). Eternal curses can be removed.
#include <algorithm>

#include "cards.h"
#include "progress.h"

namespace sts {

namespace {

template <class R> void regRelic() { db::registerRelic(R::kId, [] { return std::unique_ptr<Relic>(new R()); }); }
#define EVENT_HEADER(Name, Key) \
  static constexpr const char* kId = #Name; \
  Name() { id = #Name; locKey = Key; }

bool inFight(const Relic* r) { return r->combat && r->combat->inProgress; }
bool combatRoom(RoomType t) { return t == RoomType::Monster || t == RoomType::Elite || t == RoomType::Boss; }

Task<> healOwner(Relic* r, int amount) {
  if (inFight(r)) { co_await cmd::heal(r->owner(), Dec(amount)); co_return; }
  Creature* p = r->owner();
  p->hp = std::min(p->maxHp, p->hp + amount);
}

// CardPileCmd.AddGeneratedCardToCombat at a random position of the draw pile.
Task<> addToDrawRandom(Combat& c, std::unique_ptr<Card> card) {
  Card* k = co_await cmd::addGeneratedCard(c, std::move(card), Pile::Draw);
  auto& d = c.draw;
  d.erase(std::remove(d.begin(), d.end(), k), d.end());
  d.insert(d.begin() + c.rng("CombatCardGeneration").nextInt((int)d.size() + 1), k);
}

// Pool cards for combat generation (CardFactory.FilterForCombat): not Basic / Ancient.
std::vector<std::string> combatPool(Run& r, std::function<bool(const Card&)> f) {
  return db::characterCards(r.characterId, [&](const Card& c) { return c.rarity != Rarity::Basic && c.rarity != Rarity::Ancient && f(c); });
}

}  // namespace

// ================================================================ powers

struct BlurPower : Power {
  POWER_HEADER(BlurPower, "BLUR_POWER")
  bool shouldClearBlock(Creature* c) override {
    if (c != owner) return true;
    flash = 1.f;
    return false;
  }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await cmd::decrement(this);
  }
};

// Snecko Eye: every card drawn costs a random 0-3 for the rest of combat.
struct ConfusedPower : Power {
  POWER_HEADER(ConfusedPower, "CONFUSED_POWER")
  PowerType type() const override { return PowerType::Debuff; }
  StackType stackType() const override { return StackType::Single; }
  Task<> afterCardDrawn(Card* card, bool) override {
    if (card->canonicalCost < 0 || card->costsX) return {};
    card->setThisCombat(owner->combat->rng("CombatEnergyCosts").nextInt(4));
    return {};
  }
};

// ================================================================ cards

struct Luminesce : IroncladT<Luminesce> {
  CARD_HEADER(Luminesce, "LUMINESCE", 0, Skill, Token, Self)
    keywords = kwExhaust | kwRetain;
    addVar("Energy", 2);
  }
  Task<> onPlay(CardPlay&) override { co_await cmd::gainEnergy(*combat, val("Energy").toInt()); }
  void onUpgrade() override { upgradeVar("Energy", 1); }
};

struct Relax : IroncladT<Relax> {
  CARD_HEADER(Relax, "RELAX", 3, Skill, Ancient, Self)
    keywords = kwExhaust;
    addVar("Block", 16);
    addVar("Cards", 2);
    addVar("Energy", 2);
  }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    co_await applyPower<DrawCardsNextTurnPower>(me(), val("Cards"), me(), this);
    co_await applyPower<EnergyNextTurnPower>(me(), val("Energy"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Block", 2); upgradeVar("Cards", 1); upgradeVar("Energy", 1); }
};

struct Soot : IroncladT<Soot> {
  bool canBeGeneratedInCombat() const override { return false; }
  CARD_HEADER(Soot, "SOOT", -1, Status, Status, None)
    keywords = kwUnplayable;
    maxUpgradeLevel = 0;
  }
};

struct BrightestFlame : IroncladT<BrightestFlame> {
  CARD_HEADER(BrightestFlame, "BRIGHTEST_FLAME", 0, Skill, Ancient, Self)
    addVar("MaxHp", 2);
    addVar("Energy", 2);
    addVar("Cards", 2);
  }
  Task<> onPlay(CardPlay&) override {
    co_await cmd::gainEnergy(*combat, val("Energy").toInt());
    co_await drawCards(val("Cards"));
    co_await cmd::loseMaxHp(me(), val("MaxHp").toInt());
  }
  void onUpgrade() override { upgradeVar("Energy", 1); upgradeVar("Cards", 1); }
};

struct Apotheosis : IroncladT<Apotheosis> {
  CARD_HEADER(Apotheosis, "APOTHEOSIS", 2, Skill, Ancient, Self)
    keywords = kwExhaust | kwInnate;
  }
  Task<> onPlay(CardPlay&) override {
    for (Card* c : combat->allCards()) if (c != this && c->upgradable()) cmd::upgradeCard(c);
    co_return;
  }
  void onUpgrade() override { cost -= 1; }
};

struct Maul : IroncladT<Maul> {
  CARD_HEADER(Maul, "MAUL", 1, Attack, Ancient, AnyEnemy)
    addVar("Damage", 5);
    addVar("Increase", 2);
  }
  Dec extraDamageFromMaulPlays = 0;
  void afterDowngraded() override { upgradeVar("Damage", extraDamageFromMaulPlays); }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"), 2);
    Dec inc = val("Increase");
    for (Card* c : combat->allCards())
      if (c->id == "Maul") {
        if (auto* d = c->var("Damage")) d->base += inc;
        static_cast<Maul*>(c)->extraDamageFromMaulPlays += inc;  // BuffFromMaulPlay
      }
  }
  void onUpgrade() override { upgradeVar("Damage", 1); upgradeVar("Increase", 1); }
};

struct Whistle : IroncladT<Whistle> {
  CARD_HEADER(Whistle, "WHISTLE", 2, Attack, Ancient, AnyEnemy)
    keywords = kwExhaust;
    addVar("Damage", 33);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    if (p.target && p.target->alive() && p.target->monster) p.target->monster->stun();
  }
  void onUpgrade() override { upgradeVar("Damage", 11); }
};

// Enthralled: while it is in hand, only it can be played (except auto-plays).
struct Enthralled : IroncladT<Enthralled> {
  CARD_HEADER(Enthralled, "ENTHRALLED", 2, Curse, Curse, None)
    keywords = kwEternal;
    maxUpgradeLevel = 0;
  }
  bool shouldPlay(Card* c) override {
    if (!combat || c == this || c->id == "Enthralled") return true;
    return combat->pileOf(this) != Pile::Hand;
  }
};

struct Folly : IroncladT<Folly> {
  CARD_HEADER(Folly, "FOLLY", -1, Curse, Curse, None)
    keywords = kwUnplayable | kwInnate | kwEthereal | kwEternal;
    maxUpgradeLevel = 0;
  }
};

struct Wish : IroncladT<Wish> {
  CARD_HEADER(Wish, "WISH", 0, Skill, Ancient, Self)
    keywords = kwExhaust;
  }
  Task<> onPlay(CardPlay&) override {
    auto picked = co_await cmd::selectCards(*combat, "WISH", combat->draw, 1, 1);
    if (!picked.empty()) co_await cmd::moveCard(*combat, picked[0], Pile::Hand);
  }
  void onUpgrade() override { keywords |= kwRetain; }
};

struct Apparition : IroncladT<Apparition> {
  CARD_HEADER(Apparition, "APPARITION", 1, Skill, Ancient, Self)
    keywords = kwEthereal | kwExhaust;
    addVar("IntangiblePower", 1);
  }
  Task<> onPlay(CardPlay&) override {
    co_await cmd::applyPower(db::power("IntangiblePower"), me(), val("IntangiblePower"), me(), this);
  }
  void onUpgrade() override { keywords &= ~kwEthereal; }
};

struct CurseOfTheBell : IroncladT<CurseOfTheBell> {
  CARD_HEADER(CurseOfTheBell, "CURSE_OF_THE_BELL", -1, Curse, Curse, None)
    keywords = kwUnplayable | kwEternal;
    maxUpgradeLevel = 0;
  }
};

// ================================================================ relics

// BlackBlood (Starter, Touch of Orobas upgrade of Burning Blood): heal 12 after a win.
struct BlackBlood : Relic {
  RELIC_HEADER(BlackBlood, "BLACK_BLOOD", Starter) addVar("Heal", 12); }
  Task<> afterCombatVictory() override {
    if (owner()->dead()) co_return;
    doFlash();
    co_await healOwner(this, val("Heal").toInt());
  }
};

// ---------------------------------------------------------------- Orobas

struct GlassEye : Relic {
  RELIC_HEADER(GlassEye, "GLASS_EYE", Ancient) }
  Task<> afterObtained() override {
    for (Rarity rar : {Rarity::Common, Rarity::Common, Rarity::Uncommon, Rarity::Uncommon, Rarity::Rare}) {
      auto pool = db::characterCards(run->characterId, [&](const Card& c) { return c.rarity == rar; });
      std::vector<std::unique_ptr<Card>> cards;
      for (int i = 0; i < 3 && !pool.empty(); ++i) {
        std::string id = run->rng("Rewards").nextItem(pool);
        pool.erase(std::find(pool.begin(), pool.end(), id));
        cards.push_back(db::card(id));
      }
      co_await run->chooseCardFor(std::move(cards));
    }
  }
};

struct AlchemicalCoffer : Relic {
  RELIC_HEADER(AlchemicalCoffer, "ALCHEMICAL_COFFER", Ancient) addVar("PotionSlots", 4); }
  Task<> afterObtained() override {
    int n = val("PotionSlots").toInt();
    run->potions.resize(run->potions.size() + (size_t)n);
    for (auto& p : run->randomPotions(n, run->rng("CombatPotionGeneration"))) run->procurePotion(std::move(p));
    co_return;
  }
};

struct RadiantPearl : Relic {
  RELIC_HEADER(RadiantPearl, "RADIANT_PEARL", Ancient) addVar("Cards", 1); }
  Task<> beforeHandDraw() override {
    if (!combat || combat->turnNumber != 1) co_return;
    for (int i = 0; i < val("Cards").toInt(); ++i) co_await cmd::addGeneratedCard(*combat, db::card("Luminesce"), Pile::Hand);
  }
};

struct SandCastle : Relic {
  RELIC_HEADER(SandCastle, "SAND_CASTLE", Ancient) addVar("Cards", 6); }
  Task<> afterObtained() override {
    std::vector<Card*> up;
    for (auto& c : run->deck) if (c->upgradable()) up.push_back(c.get());
    run->rng("Niche").shuffle(up);
    for (int i = 0; i < (int)up.size() && i < val("Cards").toInt(); ++i) up[i]->upgrade();
    co_return;
  }
};

// TouchOfOrobas: the starter relic becomes its refined version (Burning Blood -> Black Blood).
struct TouchOfOrobas : Relic {
  RELIC_HEADER(TouchOfOrobas, "TOUCH_OF_OROBAS", Ancient) }
  static bool hasStarter(Run& r) {
    for (auto& rel : r.relics) if (rel->rarity == RelicRarity::Starter) return true;
    return false;
  }
  Task<> afterObtained() override {
    for (auto& rel : run->relics) {
      if (rel->rarity != RelicRarity::Starter) continue;
      // TouchOfOrobas.RefinementUpgrades
      std::string into = rel->id == "BurningBlood" ? "BlackBlood"
                       : rel->id == "RingOfTheSnake" ? "RingOfTheDrake"
                       : rel->id == "DivineRight" ? "DivineDestiny"
                       : rel->id == "BoundPhylactery" ? "PhylacteryUnbound"
                       : rel->id == "CrackedCore" ? "InfusedCore" : "Circlet";
      auto r = db::relic(into);
      if (!r) co_return;
      r->run = run;
      rel = std::move(r);  // RelicCmd.Replace
      rel->doFlash();
      co_return;
    }
  }
};

// ArchaicTooth: Bash becomes Break (keeping its upgrade).
struct ArchaicTooth : Relic {
  RELIC_HEADER(ArchaicTooth, "ARCHAIC_TOOTH", Ancient) }
  static Card* bash(Run& r) {
    for (auto& c : r.deck) if (c->id == "Bash") return c.get();
    return nullptr;
  }
  Task<> afterObtained() override {
    Card* b = bash(*run);
    if (!b) co_return;
    auto brk = db::card("Break");
    if (b->upgraded()) brk->upgrade();
    run->transformCard(b, std::move(brk));
  }
};

// ---------------------------------------------------------------- Pael

struct PaelsFlesh : Relic {
  RELIC_HEADER(PaelsFlesh, "PAELS_FLESH", Ancient) addVar("Energy", 1); }
  Dec modifyMaxEnergy(Dec amount) override {
    return combat && combat->turnNumber >= 3 ? amount + val("Energy") : amount;
  }
};

struct PaelsHorn : Relic {
  RELIC_HEADER(PaelsHorn, "PAELS_HORN", Ancient) }
  Task<> afterObtained() override {
    for (int i = 0; i < 2; ++i) run->addCardToDeck(db::card("Relax"));
    co_return;
  }
};

struct PaelsTears : Relic {
  RELIC_HEADER(PaelsTears, "PAELS_TEARS", Ancient) addVar("Energy", 2); }
  bool leftover = false;
  Task<> beforeSideTurnEnd(Side side, const std::vector<Creature*>&) override {
    if (combat && side == Side::Player) leftover = combat->energy > 0;
    return {};
  }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!combat || !contains(participants, owner()) || !leftover) co_return;
    doFlash();
    co_await cmd::gainEnergy(*combat, val("Energy").toInt());
  }
  Task<> afterCombatEnd() override { leftover = false; return {}; }
};

struct PaelsBlood : Relic {
  RELIC_HEADER(PaelsBlood, "PAELS_BLOOD", Ancient) addVar("Cards", 1); }
  Dec modifyHandDraw(Dec count) override { return count + val("Cards"); }
};

// PaelsTooth: five upgradable cards leave the deck; one returns (upgraded) after each fight.
struct PaelsTooth : Relic {
  RELIC_HEADER(PaelsTooth, "PAELS_TOOTH", Ancient) addVar("Cards", 5); }
  std::vector<std::shared_ptr<Card>> stored;  // shared: relics are copied when shown as options
  // SerializableCards ([SavedProperty]): each stored card in full (id, upgrade, keywords, cost,
  // vars, enchantment), the same fidelity as CardModel.ToSerializable() / a deck card's own save.
  void persist(Archive& a) override {
    int n = (int)stored.size();
    a.io(n);
    if (a.reading) stored.assign((size_t)std::max(0, n), nullptr);
    for (int i = 0; i < n; ++i) {
      Card* sc = stored[i].get();
      std::string id = sc ? sc->id : "";
      int level = sc ? sc->upgradeLevel : 0, keywords = sc ? sc->keywords : 0, cost = sc ? sc->cost : 0;
      std::vector<std::string> names;
      std::vector<int64_t> values;
      if (sc) for (auto& v : sc->vars) { names.push_back(v.name); values.push_back(v.base.raw); }
      a.io(id);
      a.io(level);
      a.io(keywords);
      a.io(cost);
      a.io(names);
      a.io(values);
      std::string enchId = sc && sc->enchantment ? sc->enchantment->id : "";
      int enchAmount = sc && sc->enchantment ? sc->enchantment->amount : 0;
      int enchStatus = sc && sc->enchantment ? (int)sc->enchantment->status : 0;
      std::vector<std::string> enchNames;
      std::vector<int64_t> enchValues;
      if (sc && sc->enchantment) for (auto& v : sc->enchantment->vars) { enchNames.push_back(v.name); enchValues.push_back(v.base.raw); }
      a.io(enchId);
      if (!enchId.empty()) {
        a.io(enchAmount);
        a.io(enchStatus);
        a.io(enchNames);
        a.io(enchValues);
      }
      if (!a.reading) continue;
      auto c = db::card(id);
      if (!c) { a.ok = false; return; }
      for (int k = 0; k < level; ++k) c->upgrade();
      c->keywords = keywords;
      c->cost = cost;
      for (size_t j = 0; j < names.size() && j < values.size(); ++j)
        if (auto* v = c->var(names[j].c_str())) v->base = Dec::fromRaw(values[j]);
      if (!enchId.empty()) {
        auto e = db::enchantment(enchId);
        if (!e) { a.ok = false; return; }
        e->card = c.get();
        e->amount = enchAmount;
        e->status = (EnchantStatus)enchStatus;
        for (size_t j = 0; j < enchNames.size() && j < enchValues.size(); ++j)
          if (auto* v = e->var(enchNames[j].c_str())) v->base = Dec::fromRaw(enchValues[j]);
        c->enchantment.p = std::move(e);
      }
      stored[i] = std::shared_ptr<Card>(c.release());
    }
  }
  bool showCounter() const override { return !stored.empty(); }
  int displayAmount() const override { return (int)stored.size(); }
  Task<> afterObtained() override {
    auto picked = co_await run->selectFromDeck("card_selection.TO_REMOVE", [](Card* c) { return c->upgradable(); },
                                               val("Cards").toInt());
    for (Card* c : picked) {
      stored.push_back(std::shared_ptr<Card>(c->clone().release()));
      run->removeCardFromDeck(c);
    }
    usedUp = stored.empty();
  }
  Task<> afterCombatEnd() override {
    if (owner()->dead() || stored.empty()) co_return;
    doFlash();
    size_t i = (size_t)run->rng("Rewards").nextInt((int)stored.size());
    auto c = stored[i]->clone();
    stored.erase(stored.begin() + (long)i);
    if (c->upgradable()) c->upgrade();
    run->addCardToDeck(std::move(c));
    usedUp = stored.empty();
  }
};

// ---------------------------------------------------------------- Tezcatara

struct YummyCookie : Relic {
  RELIC_HEADER(YummyCookie, "YUMMY_COOKIE", Ancient) addVar("Cards", 4); }
  Task<> afterObtained() override {
    auto picked = co_await run->selectFromDeck("card_selection.TO_UPGRADE", [](Card* c) { return c->upgradable(); },
                                               val("Cards").toInt(), false, true);
    for (Card* c : picked) c->upgrade();
  }
};

struct BiiigHug : Relic {
  RELIC_HEADER(BiiigHug, "BIIIG_HUG", Ancient) addVar("Cards", 4); }
  Task<> afterObtained() override {
    auto picked = co_await run->selectFromDeck("card_selection.TO_REMOVE", nullptr, val("Cards").toInt());
    for (Card* c : picked) run->removeCardFromDeck(c);
  }
  Task<> afterShuffle() override {
    if (!combat) co_return;
    doFlash();
    co_await addToDrawRandom(*combat, db::card("Soot"));
  }
};

struct Storybook : Relic {
  RELIC_HEADER(Storybook, "STORYBOOK", Ancient) }
  Task<> afterObtained() override { run->addCardToDeck(db::card("BrightestFlame")); co_return; }
};

struct ToastyMittens : Relic {
  RELIC_HEADER(ToastyMittens, "TOASTY_MITTENS", Ancient) addVar("StrengthPower", 1); }
  Task<> afterPlayerTurnStart() override {
    if (!combat) co_return;
    doFlash();
    auto picked = co_await cmd::selectCards(*combat, "card_selection.TO_EXHAUST", combat->hand, 1, 1);
    for (Card* c : picked) co_await cmd::exhaustCard(*combat, c);
    co_await applyPower<StrengthPower>(owner(), val("StrengthPower"), owner(), nullptr);
  }
};

struct PumpkinCandle : Relic {
  RELIC_HEADER(PumpkinCandle, "PUMPKIN_CANDLE", Ancient) addVar("CombatCount", 5); addVar("Energy", 1); }
  int kindle = 0;
  void persist(Archive& a) override { a.io(kindle); }
  bool showCounter() const override { return true; }
  int displayAmount() const override { return kindle; }
  Task<> afterObtained() override { kindle += 5; doFlash(); return {}; }
  Dec modifyMaxEnergy(Dec amount) override { return kindle > 0 ? amount + val("Energy") : amount; }
  Task<> afterCombatEnd() override {
    kindle = std::max(kindle - 1, 0);
    usedUp = kindle <= 0;
    return {};
  }
  void restSiteAction(int) override { kindle += 5; usedUp = false; }
};

struct SealOfGold : Relic {
  RELIC_HEADER(SealOfGold, "SEAL_OF_GOLD", Ancient) addVar("Energy", 1); addVar("Gold", 3); }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!combat || !contains(participants, owner()) || run->gold < val("Gold").toInt()) co_return;
    doFlash();
    co_await cmd::gainEnergy(*combat, val("Energy").toInt());
    run->gold -= val("Gold").toInt();
  }
};

// ---------------------------------------------------------------- Nonupeipe

struct BlessedAntler : Relic {
  RELIC_HEADER(BlessedAntler, "BLESSED_ANTLER", Ancient) addVar("Energy", 1); addVar("Cards", 3); }
  Dec modifyMaxEnergy(Dec amount) override { return amount + val("Energy"); }
  Task<> beforeHandDraw() override {
    if (!combat || combat->turnNumber != 1) co_return;
    doFlash();
    for (int i = 0; i < val("Cards").toInt(); ++i) co_await addToDrawRandom(*combat, db::card("Dazed"));
  }
};

// BrilliantScarf: the 5th card each turn is free.
struct BrilliantScarf : Relic {
  RELIC_HEADER(BrilliantScarf, "BRILLIANT_SCARF", Ancient) addVar("Cards", 5); }
  int played = 0;
  bool showCounter() const override { return combat && played < 5; }
  int displayAmount() const override { return played; }
  int modifyEnergyCostLate(Card* c, int cost) override {
    if (!inFight(this) || played != val("Cards").toInt() - 1) return cost;
    Pile p = combat->pileOf(c);
    return p == Pile::Hand || p == Pile::Play ? 0 : cost;
  }
  Task<> beforeSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner())) played = 0;
    return {};
  }
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (inFight(this) && !p.autoPlay) ++played;
    return {};
  }
  Task<> afterCombatEnd() override { played = 0; return {}; }
};

struct DelicateFrond : Relic {
  RELIC_HEADER(DelicateFrond, "DELICATE_FROND", Ancient) }
  Task<> beforeCombatStart() override {
    doFlash();
    while (run->hasOpenPotionSlot())
      if (!run->procurePotion(run->randomPotion(run->rng("CombatPotionGeneration"), false))) break;
    return {};
  }
};

struct DiamondDiadem : Relic {
  RELIC_HEADER(DiamondDiadem, "DIAMOND_DIADEM", Ancient) addVar("Block", 20); }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!combat || !contains(participants, owner()) || combat->turnNumber > 1) co_return;
    doFlash();
    co_await cmd::gainBlock(owner(), val("Block"), kUnpowered, nullptr);
    co_await applyPower<BlurPower>(owner(), 1, owner(), nullptr);
  }
};

struct JewelryBox : Relic {
  RELIC_HEADER(JewelryBox, "JEWELRY_BOX", Ancient) }
  Task<> afterObtained() override { run->addCardToDeck(db::card("Apotheosis")); co_return; }
};

struct SignetRing : Relic {
  RELIC_HEADER(SignetRing, "SIGNET_RING", Ancient) addVar("Gold", 888); }
  Task<> afterObtained() override { co_await run->gainGold(val("Gold").toInt()); }
};

// ---------------------------------------------------------------- Tanx

// Claws: up to 6 cards become Maul (upgraded if the original was).
struct Claws : Relic {
  RELIC_HEADER(Claws, "CLAWS", Ancient) addVar("Cards", 6); }
  Task<> afterObtained() override {
    auto picked = co_await run->selectFromDeck("relics.CLAWS.selectionScreenPrompt", nullptr, val("Cards").toInt(), false, false, 0);
    for (Card* c : picked) {
      auto maul = db::card("Maul");
      if (c->upgraded() && maul->upgradable()) maul->upgrade();
      run->transformCard(c, std::move(maul));
    }
  }
};

struct Crossbow : Relic {
  RELIC_HEADER(Crossbow, "CROSSBOW", Ancient) }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!combat || !contains(participants, owner())) co_return;
    auto ids = combatPool(*run, [](const Card& c) { return c.type == CardType::Attack; });
    if (ids.empty()) co_return;
    doFlash();
    auto card = db::card(combat->rng("CombatCardGeneration").nextItem(ids));
    card->setThisTurnOrUntilPlayed(0);
    co_await cmd::addGeneratedCard(*combat, std::move(card), Pile::Hand);
  }
};

struct IronClub : Relic {
  RELIC_HEADER(IronClub, "IRON_CLUB", Ancient) addVar("Cards", 4); }
  int played = 0;
  void persist(Archive& a) override { a.io(played); }
  bool showCounter() const override { return true; }
  int displayAmount() const override { return played % 4; }
  Task<> afterCardPlayed(const CardPlay&) override {
    ++played;
    if (!inFight(this) || played % val("Cards").toInt() != 0) co_return;
    doFlash();
    co_await cmd::drawCards(*combat, 1);
  }
};

struct MeatCleaver : Relic {  // rest site Cook (Run::restSite)
  RELIC_HEADER(MeatCleaver, "MEAT_CLEAVER", Ancient) }
};

struct Sai : Relic {
  RELIC_HEADER(Sai, "SAI", Ancient) addVar("Block", 7); }
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (!combat || !contains(participants, owner())) co_return;
    doFlash();
    co_await cmd::gainBlock(owner(), val("Block"), kUnpowered, nullptr);
  }
};

struct SpikedGauntlets : Relic {
  RELIC_HEADER(SpikedGauntlets, "SPIKED_GAUNTLETS", Ancient) addVar("Energy", 1); }
  Dec modifyMaxEnergy(Dec amount) override { return amount + val("Energy"); }
  int modifyEnergyCost(Card* c, int cost) override { return c->type == CardType::Power && cost >= 0 ? cost + 1 : cost; }
};

struct TanxsWhistle : Relic {
  RELIC_HEADER(TanxsWhistle, "TANXS_WHISTLE", Ancient) }
  Task<> afterObtained() override { run->addCardToDeck(db::card("Whistle")); co_return; }
};

// ThrowingAxe: the first card each combat is played twice.
struct ThrowingAxe : Relic {
  RELIC_HEADER(ThrowingAxe, "THROWING_AXE", Ancient) }
  bool used = false;
  Task<> afterRoomEntered(RoomType room) override { if (combatRoom(room)) used = false; return {}; }
  int modifyCardPlayCount(Card*, Creature*, int count) override { return used ? count : count + 1; }
  Task<> afterModifyingCardPlayCount(Card*) override { used = true; doFlash(); return {}; }
  Task<> afterCombatEnd() override { used = false; return {}; }
};

struct WarHammer : Relic {
  RELIC_HEADER(WarHammer, "WAR_HAMMER", Ancient) addVar("Cards", 4); }
  Task<> afterCombatVictory() override {
    if (!combat || !combat->isElite) return {};
    doFlash();
    std::vector<Card*> up;
    for (auto& c : run->deck) if (c->upgradable()) up.push_back(c.get());
    run->rng("Niche").shuffle(up);
    for (int i = 0; i < (int)up.size() && i < val("Cards").toInt(); ++i) up[i]->upgrade();
    return {};
  }
};

// ---------------------------------------------------------------- Vakuu

struct BloodSoakedRose : Relic {
  RELIC_HEADER(BloodSoakedRose, "BLOOD_SOAKED_ROSE", Ancient) addVar("Energy", 1); }
  Task<> afterObtained() override { run->addCardToDeck(db::card("Enthralled")); co_return; }
  Dec modifyMaxEnergy(Dec amount) override { return amount + val("Energy"); }
};

// Fiddle: draw 2 more each turn, but nothing else draws cards during your turn.
struct Fiddle : Relic {
  RELIC_HEADER(Fiddle, "FIDDLE", Ancient) addVar("Cards", 2); }
  Dec modifyHandDraw(Dec count) override { return count + val("Cards"); }
  bool shouldDraw(bool fromHandDraw) override {
    if (fromHandDraw || !combat || combat->currentSide != Side::Player) return true;
    doFlash();
    return false;
  }
};

struct PreservedFog : Relic {
  RELIC_HEADER(PreservedFog, "PRESERVED_FOG", Ancient) addVar("Cards", 3); }
  Task<> afterObtained() override {
    auto picked = co_await run->selectFromDeck("card_selection.TO_REMOVE", nullptr, val("Cards").toInt());
    for (Card* c : picked) run->removeCardFromDeck(c);
    run->addCardToDeck(db::card("Folly"));
  }
};

struct SereTalon : Relic {
  RELIC_HEADER(SereTalon, "SERE_TALON", Ancient) addVar("HpLoss", 9); addVar("Wishes", 3); }
  Task<> afterObtained() override {
    co_await run->loseMaxHp(val("HpLoss").toInt());
    for (int i = 0; i < val("Wishes").toInt(); ++i) run->addCardToDeck(db::card("Wish"));
  }
};

struct DistinguishedCape : Relic {
  RELIC_HEADER(DistinguishedCape, "DISTINGUISHED_CAPE", Ancient) addVar("Curses", 2); addVar("Cards", 3); }
  Task<> afterObtained() override {
    // CurseCardPool with CanBeGeneratedByModifiers, ordered by id (registered ones).
    std::vector<std::string> curses;
    for (const char* id : {"Clumsy", "Debt", "Decay", "Doubt", "Guilty", "Injury", "Normality", "Regret", "Shame", "Writhe"})
      if (db::card(id)) curses.push_back(id);
    for (int i = 0; i < val("Curses").toInt() && !curses.empty(); ++i) {
      std::string id = run->rng("Niche").nextItem(curses);
      curses.erase(std::find(curses.begin(), curses.end(), id));
      run->addCardToDeck(db::card(id));
    }
    for (int i = 0; i < val("Cards").toInt(); ++i) run->addCardToDeck(db::card("Apparition"));
    co_return;
  }
};

// ChoicesParadox: on turn 1, choose 1 of 5 random cards (with Retain) for your hand.
struct ChoicesParadox : Relic {
  RELIC_HEADER(ChoicesParadox, "CHOICES_PARADOX", Ancient) addVar("Cards", 5); }
  Task<> afterPlayerTurnStart() override {
    if (!combat || combat->turnNumber != 1) co_return;
    auto ids = combatPool(*run, [](const Card&) { return true; });
    combat->rng("CombatCardGeneration").shuffle(ids);
    std::vector<std::unique_ptr<Card>> made;
    std::vector<Card*> opts;
    for (size_t i = 0; i < ids.size() && i < (size_t)val("Cards").toInt(); ++i) {
      auto c = db::card(ids[i]);
      c->keywords |= kwRetain;
      c->combat = combat;
      opts.push_back(c.get());
      made.push_back(std::move(c));
    }
    if (opts.empty()) co_return;
    doFlash();
    auto picked = co_await cmd::selectCards(*combat, "relics.CHOICES_PARADOX.selectionScreenPrompt", opts, 1, 1);
    for (auto& c : made)
      if (!picked.empty() && c.get() == picked[0]) { co_await cmd::addGeneratedCard(*combat, std::move(c), Pile::Hand); break; }
  }
};

// MusicBox: the first Attack each turn returns an Ethereal copy to hand.
struct MusicBox : Relic {
  RELIC_HEADER(MusicBox, "MUSIC_BOX", Ancient) }
  bool usedThisTurn = false;
  Card* playing = nullptr;
  Task<> beforeCardPlayed(const CardPlay& p) override {
    if (!playing && !usedThisTurn && p.card->type == CardType::Attack) playing = p.card;
    return {};
  }
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (p.card != playing || !combat) co_return;
    doFlash();
    auto copy = p.card->clone();
    copy->keywords |= kwEthereal;
    usedThisTurn = true;
    playing = nullptr;
    co_await cmd::addGeneratedCard(*combat, std::move(copy), Pile::Hand);
  }
  Task<> beforeSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner())) { usedThisTurn = false; playing = nullptr; }
    return {};
  }
  Task<> afterCombatEnd() override { usedThisTurn = false; playing = nullptr; return {}; }
};

struct LordsParasol : Relic {  // the merchant gives everything away (Run::enterShop)
  RELIC_HEADER(LordsParasol, "LORDS_PARASOL", Ancient) }
};

// JeweledMask: on turn 1 a Power from the draw pile goes to your hand, free this turn.
struct JeweledMask : Relic {
  RELIC_HEADER(JeweledMask, "JEWELED_MASK", Ancient) }
  Task<> beforeHandDraw() override {
    if (!combat || combat->turnNumber > 1) co_return;
    std::vector<Card*> powers, notInnate;
    for (Card* c : combat->draw) if (c->type == CardType::Power) powers.push_back(c);
    for (Card* c : powers) if (!c->has(kwInnate)) notInnate.push_back(c);
    if (!notInnate.empty()) powers = notInnate;
    if (powers.empty()) co_return;
    Card* pick = combat->rng("CombatCardSelection").nextItem(powers);
    doFlash();
    pick->setThisTurnOrUntilPlayed(0);
    co_await cmd::moveCard(*combat, pick, Pile::Hand);
  }
};

// ---------------------------------------------------------------- Darv

struct Astrolabe : Relic {
  RELIC_HEADER(Astrolabe, "ASTROLABE", Ancient) addVar("Cards", 3); }
  Task<> afterObtained() override {
    auto picked = co_await run->selectFromDeck("card_selection.TO_TRANSFORM", nullptr, val("Cards").toInt());
    for (Card* c : picked) {
      auto into = run->randomTransformFor(c, run->rng("Niche"));
      if (!into) continue;
      into->upgrade();
      run->transformCard(c, std::move(into));
    }
  }
};

struct BlackStar : Relic {
  RELIC_HEADER(BlackStar, "BLACK_STAR", Ancient) }
  int bonusRelicRewards(RoomType room) override {
    if (room != RoomType::Elite) return 0;
    doFlash();
    return 1;
  }
};

struct CallingBell : Relic {
  RELIC_HEADER(CallingBell, "CALLING_BELL", Ancient) addVar("Relics", 3); }
  Task<> afterObtained() override {
    run->addCardToDeck(db::card("CurseOfTheBell"));
    co_await wait(0.75);
    for (RelicRarity r : {RelicRarity::Common, RelicRarity::Uncommon, RelicRarity::Rare})
      co_await run->offerRelic(run->pullRelicFromFront(run->relicBag, r), false);
  }
};

struct EmptyCage : Relic {
  RELIC_HEADER(EmptyCage, "EMPTY_CAGE", Ancient) addVar("Cards", 2); }
  Task<> afterObtained() override {
    auto picked = co_await run->selectFromDeck("card_selection.TO_REMOVE", nullptr, val("Cards").toInt());
    for (Card* c : picked) run->removeCardFromDeck(c);
  }
};

struct PandorasBox : Relic {
  RELIC_HEADER(PandorasBox, "PANDORAS_BOX", Ancient) }
  Task<> afterObtained() override {
    std::vector<Card*> basics;
    for (auto& c : run->deck) if (c->rarity == Rarity::Basic && (c->tags & (tagStrike | tagDefend))) basics.push_back(c.get());
    for (Card* c : basics) run->transformCard(c, run->randomTransformFor(c, run->rng("Niche")));
    co_return;
  }
};

struct RunicPyramid : Relic {
  RELIC_HEADER(RunicPyramid, "RUNIC_PYRAMID", Ancient) }
  bool shouldFlush() override { return false; }
};

struct SneckoEye : Relic {
  RELIC_HEADER(SneckoEye, "SNECKO_EYE", Ancient) addVar("Cards", 2); }
  Dec modifyHandDraw(Dec count) override { return count + val("Cards"); }
  Task<> beforeCombatStart() override { co_await applyPower<ConfusedPower>(owner(), 1, owner(), nullptr); }
  Task<> afterObtained() override {
    if (inFight(this)) co_await applyPower<ConfusedPower>(owner(), 1, owner(), nullptr);
  }
};

struct Ectoplasm : Relic {
  RELIC_HEADER(Ectoplasm, "ECTOPLASM", Ancient) addVar("Energy", 1); }
  Dec modifyGoldGained(Dec) override { doFlash(); return 0; }
  Dec modifyMaxEnergy(Dec amount) override { return amount + val("Energy"); }
};

struct Sozu : Relic {
  RELIC_HEADER(Sozu, "SOZU", Ancient) addVar("Energy", 1); }
  bool shouldProcurePotion() override { return false; }
  Dec modifyMaxEnergy(Dec amount) override { return amount + val("Energy"); }
};

struct PhilosophersStone : Relic {
  RELIC_HEADER(PhilosophersStone, "PHILOSOPHERS_STONE", Ancient) addVar("StrengthPower", 1); addVar("Energy", 1); }
  Dec modifyMaxEnergy(Dec amount) override { return amount + val("Energy"); }
  Task<> afterRoomEntered(RoomType room) override {
    if (!combatRoom(room) || !combat) co_return;
    doFlash();
    for (Creature* e : combat->aliveEnemies()) co_await applyPower<StrengthPower>(e, val("StrengthPower"), nullptr, nullptr);
  }
  Task<> afterCreatureAddedToCombat(Creature* c) override {
    if (c->side != Side::Enemy) co_return;
    doFlash();
    co_await applyPower<StrengthPower>(c, val("StrengthPower"), nullptr, nullptr);
  }
};

struct VelvetChoker : Relic {
  RELIC_HEADER(VelvetChoker, "VELVET_CHOKER", Ancient) addVar("Cards", 6); addVar("Energy", 1); }
  int played = 0;
  bool showCounter() const override { return combat != nullptr; }
  int displayAmount() const override { return played; }
  Dec modifyMaxEnergy(Dec amount) override { return amount + val("Energy"); }
  bool shouldPlay(Card*) override { return played < val("Cards").toInt(); }
  Task<> afterCardPlayed(const CardPlay&) override { ++played; return {}; }
  Task<> afterRoomEntered(RoomType room) override { if (combatRoom(room)) played = 0; return {}; }
  Task<> beforeSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner())) played = 0;
    return {};
  }
};

// DustyTome: a random Ancient card of the character (not the Archaic Tooth ones), upgraded.
struct DustyTome : Relic {
  RELIC_HEADER(DustyTome, "DUSTY_TOME", Ancient) }
  std::string card;
  void persist(Archive& a) override { a.io(card); }
  void setup(Run& r) {
    auto ids = db::characterCards(r.characterId, [](const Card& c) { return c.rarity == Rarity::Ancient && c.id != "Break"; });
    if (!ids.empty()) card = r.rng("Rewards").nextItem(ids);
  }
  Task<> afterObtained() override {
    if (card.empty()) setup(*run);
    auto c = db::card(card);
    if (!c) co_return;
    c->upgrade();
    run->addCardToDeck(std::move(c));
  }
};

// ================================================================ the Ancients

namespace {

// AncientEventModel: the dialogue NEventRoom picks for this character and visit
// (db::ancientDialogueFor), and RelicOption (take the relic, then Done). Only registered relics are offered.
struct AncientBase : Event {
  void talk() {
    ancient = true;
    dialogue = db::ancientDialogueFor(*run, id);
  }
  static bool ok(const std::string& id) { return db::relicRegistered(id); }
  static std::vector<std::string> ported(std::vector<std::string> ids) {
    ids.erase(std::remove_if(ids.begin(), ids.end(), [](const std::string& id) { return !ok(id); }), ids.end());
    return ids;
  }
  EventOption relicOption(std::shared_ptr<Relic> shown) {
    EventOption o;
    o.relic = shown;
    o.action = [this, shown]() -> Task<> {
      auto fresh = db::relic(shown->id);
      // Keep what the option was set up with (Dusty Tome's card).
      if (shown->id == "DustyTome") static_cast<DustyTome*>(fresh.get())->card = static_cast<DustyTome*>(shown.get())->card;
      if (shown->id == "SeaGlass") fresh->vars = shown->vars;  // its character (A6)
      co_await run->obtainRelic(std::move(fresh));
      finished = true;
      options.clear();
      descKey.clear();
    };
    return o;
  }
  EventOption relicOption(const std::string& id) {
    auto r = db::relic(id);
    r->run = run;
    return relicOption(std::shared_ptr<Relic>(r.release()));
  }
  EventOption lockedOption(const std::string& key) { return {key, nullptr}; }
  // Rng.NextItem over the registered ones (none: nothing).
  std::vector<EventOption> pick1(const std::vector<std::string>& ids) {
    auto p = ported(ids);
    if (p.empty()) return {};
    return {relicOption(rng().nextItem(p))};
  }
};

struct Orobas : AncientBase {
  EVENT_HEADER(Orobas, "OROBAS")
  std::vector<EventOption> initialOptions() override {
    talk();
    // Pool 1: Electric Shrymp, Glass Eye, and Prismatic Gem (1/3) or Sea Glass.
    // The Sea Glass character: NextItem over the other unlocked characters (own character if none).
    std::vector<std::string> others;
    for (auto& id : db::allCharacters())  // UnlockState.Characters order
      if (id != run->characterId && db::characterPlayable(id)) others.push_back(id);
    std::string seaChar = others.empty() ? run->characterId : rng().nextItem(others);
    std::vector<std::string> pool1 = {"ElectricShrymp", "GlassEye", rng().nextFloat() < 0.3333333f ? "PrismaticGem" : "SeaGlass"};
    std::vector<EventOption> out;
    auto p1 = ported(pool1);
    if (!p1.empty()) {
      std::string pick = rng().nextItem(p1);
      if (pick == "SeaGlass") {
        auto sg = db::relic("SeaGlass");
        sg->run = run;
        const auto& ids = db::characterIds();
        sg->var("CharacterIndex")->base = Dec((int)(std::find(ids.begin(), ids.end(), seaChar) - ids.begin()));
        out.push_back(relicOption(std::shared_ptr<Relic>(sg.release())));
      } else {
        out.push_back(relicOption(pick));
      }
    }
    auto o2 = pick1({"AlchemicalCoffer", "Driftwood", "RadiantPearl", "SandCastle"});
    out.insert(out.end(), o2.begin(), o2.end());
    std::vector<std::string> pool3;
    if (TouchOfOrobas::hasStarter(*run)) pool3.push_back("TouchOfOrobas");
    if (ArchaicTooth::bash(*run)) pool3.push_back("ArchaicTooth");
    auto o3 = pick1(pool3);
    if (o3.empty()) o3.push_back(lockedOption("OROBAS.pages.INITIAL.options.OPTION_POOL_3_LOCKED"));
    out.insert(out.end(), o3.begin(), o3.end());
    return out;
  }
};

struct Pael : AncientBase {
  EVENT_HEADER(Pael, "PAEL")
  std::vector<EventOption> initialOptions() override {
    talk();
    auto out = pick1({"PaelsFlesh", "PaelsHorn", "PaelsTears"});
    std::vector<std::string> pool2 = {"PaelsWing"};
    if (run->deck.size() >= 5) pool2.push_back("PaelsTooth");
    pool2.insert(pool2.end(), pool2.begin(), pool2.end());  // list.AddRange(list)
    pool2.push_back("PaelsGrowth");
    auto o2 = pick1(pool2);
    out.insert(out.end(), o2.begin(), o2.end());
    auto o3 = pick1({"PaelsEye", "PaelsBlood", "PaelsLegion"});
    out.insert(out.end(), o3.begin(), o3.end());
    return out;
  }
};

struct Tezcatara : AncientBase {
  EVENT_HEADER(Tezcatara, "TEZCATARA")
  std::vector<EventOption> initialOptions() override {
    talk();
    std::vector<std::string> pool1 = {"VeryHotCocoa", "YummyCookie"};
    for (auto& c : run->deck)
      if ((c->tags & tagStrike) && c->rarity == Rarity::Basic) { pool1.push_back("NutritiousSoup"); break; }
    auto out = pick1(pool1);
    auto o2 = pick1({"BiiigHug", "Storybook", "ToastyMittens"});
    auto o3 = pick1({"GoldenCompass", "PumpkinCandle", "ToyBox", "SealOfGold"});
    out.insert(out.end(), o2.begin(), o2.end());
    out.insert(out.end(), o3.begin(), o3.end());
    return out;
  }
};

// Three of the pool, shuffled (UnstableShuffle(Rng).Take(3)).
std::vector<std::string> take3(Rng& r, std::vector<std::string> ids) {
  ids = AncientBase::ported(ids);
  r.shuffle(ids);
  if (ids.size() > 3) ids.resize(3);
  return ids;
}

struct Nonupeipe : AncientBase {
  EVENT_HEADER(Nonupeipe, "NONUPEIPE")
  std::vector<EventOption> initialOptions() override {
    talk();
    std::vector<EventOption> out;
    for (auto& id : take3(rng(), {"BlessedAntler", "BrilliantScarf", "DelicateFrond", "DiamondDiadem", "FurCoat", "Glitter",
                                  "JewelryBox", "LoomingFruit", "SignetRing", "BeautifulBracelet"}))
      out.push_back(relicOption(id));
    return out;
  }
};

struct Tanx : AncientBase {
  EVENT_HEADER(Tanx, "TANX")
  std::vector<EventOption> initialOptions() override {
    talk();
    std::vector<EventOption> out;
    for (auto& id : take3(rng(), {"Claws", "Crossbow", "IronClub", "MeatCleaver", "Sai", "SpikedGauntlets", "TanxsWhistle",
                                  "ThrowingAxe", "WarHammer", "TriBoomerang"}))
      out.push_back(relicOption(id));
    return out;
  }
};

struct Vakuu : AncientBase {
  EVENT_HEADER(Vakuu, "VAKUU")
  // CalculateVars: "Visits" = this character's visits so far + 1 (VAKUU.talk.ANY.1-0r).
  void calculateVars() override { addVar("Visits", progress::ancientVisits(id, run->characterId) + 1); }
  std::vector<EventOption> initialOptions() override {
    talk();
    std::vector<EventOption> out;
    for (auto pool : {std::vector<std::string>{"BloodSoakedRose", "WhisperingEarring", "Fiddle"},
                      std::vector<std::string>{"PreservedFog", "SereTalon", "DistinguishedCape"},
                      std::vector<std::string>{"ChoicesParadox", "MusicBox", "LordsParasol", "JeweledMask"}}) {
      auto p = ported(pool);
      rng().shuffle(p);
      if (!p.empty()) out.push_back(relicOption(p[0]));
    }
    return out;
  }
};

struct Darv : AncientBase {
  EVENT_HEADER(Darv, "DARV")
  std::vector<EventOption> initialOptions() override {
    talk();
    // _validRelicSets with their act filters (no run modifiers).
    int act = run->actIndex;
    std::vector<std::string> sets = {"Astrolabe", "BlackStar", "CallingBell", "EmptyCage", "PandorasBox", "RunicPyramid", "SneckoEye"};
    if (act == 1) { sets.push_back("Ectoplasm"); sets.push_back("Sozu"); }
    if (act >= 1) { sets.push_back("PhilosophersStone"); sets.push_back("VelvetChoker"); }
    // PandorasBox's ValidRelicSet: not when a modifier replaced the starting deck (M11).
    if (run->modifiersClearDeck()) sets.erase(std::remove(sets.begin(), sets.end(), "PandorasBox"), sets.end());
    sets = ported(sets);
    rng().shuffle(sets);
    std::vector<EventOption> out;
    if (rng().nextBool() && ok("DustyTome")) {
      for (size_t i = 0; i < sets.size() && i < 2; ++i) out.push_back(relicOption(sets[i]));
      auto tome = db::relic("DustyTome");
      static_cast<DustyTome*>(tome.get())->setup(*run);
      tome->run = run;
      out.push_back(relicOption(std::shared_ptr<Relic>(tome.release())));
    } else {
      for (size_t i = 0; i < sets.size() && i < 3; ++i) out.push_back(relicOption(sets[i]));
    }
    return out;
  }
};

}  // namespace

void registerAncientsLater() {
  registerPowerType<BlurPower>();
  registerPowerType<ConfusedPower>();
  registerPowerType<DrawCardsNextTurnPower>();
  registerPowerType<EnergyNextTurnPower>();
  registerCardType<Luminesce>();
  registerCardType<Relax>();
  registerCardType<Soot>();
  registerCardType<BrightestFlame>();
  registerCardType<Apotheosis>();
  registerCardType<Maul>();
  registerCardType<Whistle>();
  registerCardType<Enthralled>();
  registerCardType<Folly>();
  registerCardType<Wish>();
  registerCardType<Apparition>();
  registerCardType<CurseOfTheBell>();
  regRelic<BlackBlood>();
  regRelic<GlassEye>();
  regRelic<AlchemicalCoffer>();
  regRelic<RadiantPearl>();
  regRelic<SandCastle>();
  regRelic<TouchOfOrobas>();
  regRelic<ArchaicTooth>();
  regRelic<PaelsFlesh>();
  regRelic<PaelsHorn>();
  regRelic<PaelsTears>();
  regRelic<PaelsBlood>();
  regRelic<PaelsTooth>();
  regRelic<YummyCookie>();
  regRelic<BiiigHug>();
  regRelic<Storybook>();
  regRelic<ToastyMittens>();
  regRelic<PumpkinCandle>();
  regRelic<SealOfGold>();
  regRelic<BlessedAntler>();
  regRelic<BrilliantScarf>();
  regRelic<DelicateFrond>();
  regRelic<DiamondDiadem>();
  regRelic<JewelryBox>();
  regRelic<SignetRing>();
  regRelic<Claws>();
  regRelic<Crossbow>();
  regRelic<IronClub>();
  regRelic<MeatCleaver>();
  regRelic<Sai>();
  regRelic<SpikedGauntlets>();
  regRelic<TanxsWhistle>();
  regRelic<ThrowingAxe>();
  regRelic<WarHammer>();
  regRelic<BloodSoakedRose>();
  regRelic<Fiddle>();
  regRelic<PreservedFog>();
  regRelic<SereTalon>();
  regRelic<DistinguishedCape>();
  regRelic<ChoicesParadox>();
  regRelic<MusicBox>();
  regRelic<LordsParasol>();
  regRelic<JeweledMask>();
  regRelic<Astrolabe>();
  regRelic<BlackStar>();
  regRelic<CallingBell>();
  regRelic<EmptyCage>();
  regRelic<PandorasBox>();
  regRelic<RunicPyramid>();
  regRelic<SneckoEye>();
  regRelic<Ectoplasm>();
  regRelic<Sozu>();
  regRelic<PhilosophersStone>();
  regRelic<VelvetChoker>();
  regRelic<DustyTome>();
  db::registerEvent(Orobas::kId, [] { return std::unique_ptr<Event>(new Orobas()); });
  db::registerEvent(Pael::kId, [] { return std::unique_ptr<Event>(new Pael()); });
  db::registerEvent(Tezcatara::kId, [] { return std::unique_ptr<Event>(new Tezcatara()); });
  db::registerEvent(Nonupeipe::kId, [] { return std::unique_ptr<Event>(new Nonupeipe()); });
  db::registerEvent(Tanx::kId, [] { return std::unique_ptr<Event>(new Tanx()); });
  db::registerEvent(Vakuu::kId, [] { return std::unique_ptr<Event>(new Vakuu()); });
  db::registerEvent(Darv::kId, [] { return std::unique_ptr<Event>(new Darv()); });
}

}  // namespace sts
