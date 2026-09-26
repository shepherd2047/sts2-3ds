// Act 1 (Overgrowth) events, translated from MegaCrit.Sts2.Core.Models.Events.
#include "cards.h"

namespace sts {

namespace {
template <class E> void reg() { db::registerEvent(E::kId, [] { return std::unique_ptr<Event>(new E()); }); }
#define EVENT_HEADER(Name, Key) \
  static constexpr const char* kId = #Name; \
  Name() { id = #Name; locKey = Key; }
}  // namespace

// ---------------------------------------------------------------- cards given by events
// PORT NOTE: CardType.Quest / CardRarity.Event have no counterparts here; the quest card
// uses Status, event cards Token.

// ByrdonisEgg.cs: unplayable quest card. PORT NOTE: no HatchRestSiteOption (rest-site hatch).
struct ByrdonisEgg : IroncladT<ByrdonisEgg> {
  CARD_HEADER(ByrdonisEgg, "BYRDONIS_EGG", -1, Status, Token, None)
    keywords = kwUnplayable;
    maxUpgradeLevel = 0;
  }
};

// SporeMind.cs
struct SporeMind : IroncladT<SporeMind> {
  CARD_HEADER(SporeMind, "SPORE_MIND", 1, Curse, Curse, None)
    keywords = kwExhaust;
    maxUpgradeLevel = 0;
  }
};

// PoorSleep.cs
struct PoorSleep : IroncladT<PoorSleep> {
  CARD_HEADER(PoorSleep, "POOR_SLEEP", -1, Curse, Curse, None)
    keywords = kwUnplayable | kwRetain;
    maxUpgradeLevel = 0;
  }
};

// Guilty.cs. PORT NOTE: deck cards get no AfterCombatEnd hook, so it never expires
// after 5 combats.
struct Guilty : IroncladT<Guilty> {
  CARD_HEADER(Guilty, "GUILTY", -1, Curse, Curse, None)
    keywords = kwUnplayable;
    maxUpgradeLevel = 0;
    addVar("Combats", 5);
  }
};

// Peck.cs
struct Peck : IroncladT<Peck> {
  CARD_HEADER(Peck, "PECK", 1, Attack, Token, AnyEnemy)
    addVar("Damage", 2);
    addVar("Repeat", 3);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage"), val("Repeat").toInt()); }
  void onUpgrade() override { upgradeVar("Repeat", 1); }
};

// ToricToughnessPower.cs: after block is cleared, gain the stored block and count down.
// PORT NOTE: not an instanced power; playing the card twice stacks turns on one power.
struct ToricToughnessPower : Power {
  POWER_HEADER(ToricToughnessPower, "TORIC_TOUGHNESS_POWER")
  int block = 0;
  Task<> afterBlockCleared(Creature* c) override {
    if (c != owner) co_return;
    flash = 1.f;
    co_await cmd::gainBlock(owner, Dec(block), kUnpowered, nullptr);
    co_await cmd::decrement(this);
  }
};

// ToricToughness.cs
struct ToricToughness : IroncladT<ToricToughness> {
  CARD_HEADER(ToricToughness, "TORIC_TOUGHNESS", 2, Skill, Token, Self)
    addVar("Turns", 2);
    addVar("Block", 5);
  }
  Task<> onPlay(CardPlay&) override {
    int before = me()->block;
    co_await block(val("Block"));
    int gained = me()->block - before;
    co_await applyPower<ToricToughnessPower>(me(), val("Turns"), me(), this);
    if (auto* pw = static_cast<ToricToughnessPower*>(me()->power("ToricToughnessPower"))) pw->block = gained;
  }
  void onUpgrade() override { upgradeVar("Block", 2); }
};

// SwordOfJade.cs (SwordOfStone's replacement): +3 Strength at the start of each combat.
struct SwordOfJade : Relic {
  RELIC_HEADER(SwordOfJade, "SWORD_OF_JADE", Event)
    addVar("Strength", 3);
  }
  Task<> beforeCombatStart() override {
    doFlash();
    co_await applyPower<StrengthPower>(owner(), val("Strength"), owner(), nullptr);
  }
};

// SwordOfStone.cs: after 5 elites it turns into Sword of Jade.
struct SwordOfStone : Relic {
  RELIC_HEADER(SwordOfStone, "SWORD_OF_STONE", Event)
    addVar("Elites", 5);
  }
  int elitesDefeated = 0;
  bool showCounter() const override { return true; }
  int displayAmount() const override { return elitesDefeated; }
  Task<> afterCombatVictory() override {
    if (!combat || !combat->isElite) co_return;
    ++elitesDefeated;
    doFlash();
    if (elitesDefeated < val("Elites").toInt()) co_return;
    // RelicCmd.Replace: this object's coroutine is running, so park it instead of freeing it.
    static std::vector<std::unique_ptr<Relic>> retired;
    for (size_t i = 0; i < run->relics.size(); ++i)
      if (run->relics[i].get() == this) {
        retired.push_back(std::move(run->relics[i]));
        run->relics.erase(run->relics.begin() + (long)i);
        break;
      }
    Run* r = run;
    co_await r->obtainRelic(db::relic("SwordOfJade"));
  }
};

// ---------------------------------------------------------------- events

// AromaOfChaos.cs: transform a card into a random one, or upgrade one.
struct AromaOfChaos : Event {
  EVENT_HEADER(AromaOfChaos, "AROMA_OF_CHAOS")
  void calculateVars() override { setStr("AromaPrinciple", "characters.IRONCLAD.aromaPrinciple"); }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "LET_GO", [this] { return letGo(); }),
            option("INITIAL", "MAINTAIN_CONTROL", [this] { return maintainControl(); })};
  }
  Task<> letGo() {
    auto picked = co_await run->selectFromDeck("card_selection.TO_TRANSFORM", [](Card*) { return true; }, 1);
    if (!picked.empty()) run->transformCard(picked[0], run->randomTransformFor(picked[0], rng()));
    setFinished("LET_GO");
  }
  Task<> maintainControl() {
    auto picked = co_await run->selectFromDeck("card_selection.TO_UPGRADE", [](Card* c) { return c->upgradable(); }, 1, false, true);
    if (!picked.empty()) picked[0]->upgrade();
    setFinished("MAINTAIN_CONTROL");
  }
};

// ByrdonisNest.cs
struct ByrdonisNest : Event {
  EVENT_HEADER(ByrdonisNest, "BYRDONIS_NEST")
  void calculateVars() override { addVar("MaxHp", 7); setStr("Card", "cards.BYRDONIS_EGG.title"); }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "EAT", [this] { return eat(); }),
            option("INITIAL", "TAKE", [this] { return take(); })};
  }
  Task<> eat() {
    co_await run->gainMaxHp(val("MaxHp").toInt());
    setFinished("EAT");
  }
  Task<> take() {
    run->addCardToDeck(db::card("ByrdonisEgg"));
    setFinished("TAKE");
    co_return;
  }
};

// DenseVegetation.cs: TRUDGE_ON loses HP for gold; REST heals like a rest site (30% max HP)
// and is followed by a fight against four Wrigglers, without rewards.
struct DenseVegetation : Event {
  EVENT_HEADER(DenseVegetation, "DENSE_VEGETATION")
  void calculateVars() override {
    addVar("Gold", rng().nextInt(61, 100));
    addVar("Heal", std::min(owner()->maxHp - owner()->hp, owner()->maxHp * 3 / 10));
    addVar("HpLoss", 8);
  }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "TRUDGE_ON", [this] { return trudgeOn(); }),
            option("INITIAL", "REST", [this] { return rest(); })};
  }
  Task<> trudgeOn() {
    co_await run->loseHp(val("HpLoss").toInt());
    if (run->died) co_return;
    co_await run->gainGold(val("Gold").toInt());
    setFinished("TRUDGE_ON");
  }
  Task<> rest() {
    Creature* p = owner();
    p->hp = std::min(p->maxHp, p->hp + val("Heal").toInt());
    setPage("REST", {option("REST", "FIGHT", [this] { return fight(); })});
    co_return;
  }
  Task<> fight() {
    bool won = co_await run->fight("DenseVegetationEventEncounter");
    if (!won) { run->died = true; co_return; }
    run->combat.reset();
    for (auto& rel : run->relics) rel->combat = nullptr;
    finished = true;
    options.clear();
  }
};

// JungleMazeAdventure.cs
struct JungleMazeAdventure : Event {
  EVENT_HEADER(JungleMazeAdventure, "JUNGLE_MAZE_ADVENTURE")
  void calculateVars() override {
    addVar("SoloGold", 150 + (int)(rng().nextFloat(30.f) - 15.f));
    addVar("SoloHp", 18);
    addVar("JoinForcesGold", 50 + (int)(rng().nextFloat(30.f) - 15.f));
  }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "SOLO_QUEST", [this] { return solo(); }),
            option("INITIAL", "JOIN_FORCES", [this] { return join(); })};
  }
  Task<> solo() {
    co_await run->loseHp(val("SoloHp").toInt());
    if (run->died) co_return;
    co_await run->gainGold(val("SoloGold").toInt());
    setFinished("SOLO_QUEST");
  }
  Task<> join() {
    co_await run->gainGold(val("JoinForcesGold").toInt());
    setFinished("JOIN_FORCES");
  }
};

// LuminousChoir.cs: the tribute needs the gold; the event itself needs a relic left in the bag.
struct LuminousChoir : Event {
  EVENT_HEADER(LuminousChoir, "LUMINOUS_CHOIR")
  void calculateVars() override { addVar("Gold", 149 - rng().nextInt(0, 50)); }
  bool isAllowed(Run& r) override {
    // PORT NOTE: IsAllowed compares against the rolled gold (99..149); the lowest is used here.
    return r.gold >= 100;
  }
  std::vector<EventOption> initialOptions() override {
    std::vector<EventOption> o;
    o.push_back(option("INITIAL", "REACH_INTO_THE_FLESH", [this] { return reach(); }));
    if (run->gold >= val("Gold").toInt()) o.push_back(option("INITIAL", "OFFER_TRIBUTE", [this] { return tribute(); }));
    else o.push_back(EventOption{page("INITIAL") + ".options.OFFER_TRIBUTE_LOCKED", nullptr});
    return o;
  }
  Task<> reach() {
    auto picked = co_await run->selectFromDeck("card_selection.TO_REMOVE", [](Card*) { return true; }, 2);
    for (Card* c : picked) run->removeCardFromDeck(c);
    run->addCardToDeck(db::card("SporeMind"));
    setFinished("REACH_INTO_THE_FLESH");
  }
  Task<> tribute() {
    run->gold -= val("Gold").toInt();
    co_await run->obtainRelic(run->pullRelicFromFront(run->relicBag, run->rollRelicRarity(run->rng("Rewards"))));
    setFinished("OFFER_TRIBUTE");
  }
};

// MorphicGrove.cs
struct MorphicGrove : Event {
  EVENT_HEADER(MorphicGrove, "MORPHIC_GROVE")
  void calculateVars() override { addVar("MaxHp", 5); }
  bool isAllowed(Run& r) override { return r.gold >= 100 && r.deck.size() >= 2; }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "GROUP", [this] { return group(); }),
            option("INITIAL", "LONER", [this] { return loner(); })};
  }
  Task<> loner() {
    co_await run->gainMaxHp(val("MaxHp").toInt());
    setFinished("LONER");
  }
  Task<> group() {
    run->gold = 0;  // LoseGold(all, Stolen)
    auto picked = co_await run->selectFromDeck("card_selection.TO_TRANSFORM", [](Card*) { return true; }, 2);
    for (Card* c : picked) run->transformCard(c, run->randomTransformFor(c, rng()));
    setFinished("GROUP");
  }
};

// SapphireSeed.cs. PORT NOTE: PLANT (Sown enchantment) is locked, enchantments are not ported.
struct SapphireSeed : Event {
  EVENT_HEADER(SapphireSeed, "SAPPHIRE_SEED")
  void calculateVars() override { setStr("Enchantment", "Sown"); addVar("Heal", 9); }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "EAT", [this] { return eat(); }),
            EventOption{page("INITIAL") + ".options.PLANT", nullptr}};
  }
  Task<> eat() {
    Creature* p = owner();
    p->hp = std::min(p->maxHp, p->hp + val("Heal").toInt());
    auto picked = co_await run->selectFromDeck("card_selection.TO_UPGRADE", [](Card* c) { return c->upgradable(); }, 1, false, true);
    if (!picked.empty()) picked[0]->upgrade();
    setFinished("EAT");
  }
};

// SunkenStatue.cs
struct SunkenStatue : Event {
  EVENT_HEADER(SunkenStatue, "SUNKEN_STATUE")
  void calculateVars() override {
    setStr("Relic", "relics.SWORD_OF_STONE.title");
    addVar("Gold", 111 + rng().nextInt(-10, 11));
    addVar("HpLoss", 7);
  }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "GRAB_SWORD", [this] { return grab(); }),
            option("INITIAL", "DIVE_INTO_WATER", [this] { return dive(); })};
  }
  Task<> grab() {
    co_await run->obtainRelic(db::relic("SwordOfStone"));
    setFinished("GRAB_SWORD");
  }
  Task<> dive() {
    co_await run->gainGold(val("Gold").toInt());
    co_await run->loseHp(val("HpLoss").toInt());
    if (run->died) co_return;
    setFinished("DIVE_INTO_WATER");
  }
};

// TabletOfTruth.cs
struct TabletOfTruth : Event {
  EVENT_HEADER(TabletOfTruth, "TABLET_OF_TRUTH")
  int decipherCount = 0;
  void calculateVars() override { addVar("SmashHPGain", 20); addVar("DecipherMaxHpLoss", 3); }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "DECIPHER_1", [this] { return decipher(); }),
            option("INITIAL", "SMASH", [this] { return smash(); })};
  }
  Task<> smash() {
    Creature* p = owner();
    p->hp = std::min(p->maxHp, p->hp + val("SmashHPGain").toInt());
    setFinished("SMASH");
    co_return;
  }
  int decipherCost() {
    switch (decipherCount) {
      case 1: return 6;
      case 2: return 12;
      case 3: return 24;
      default: return owner()->maxHp - 1;  // case 4
    }
  }
  Task<> decipher() {
    Creature* p = owner();
    int hp = val("DecipherMaxHpLoss").toInt();
    if (hp >= p->maxHp) {  // LoseMaxHp(MaxHp - 1) then Kill
      p->maxHp = 1;
      p->hp = 0;
      run->died = true;
      co_return;
    }
    co_await run->loseMaxHp(hp);
    std::vector<Card*> up;
    for (auto& c : run->deck) if (c->upgradable()) up.push_back(c.get());
    if (decipherCount == 4) {
      for (Card* c : up) c->upgrade();
    } else if (!up.empty()) {
      up[(size_t)rng().nextInt((int)up.size())]->upgrade();
    }
    ++decipherCount;
    std::string n = std::to_string(decipherCount);
    if (decipherCount == 5) { setFinished("DECIPHER_5"); co_return; }
    setVar("DecipherMaxHpLoss", decipherCost());
    setPage("DECIPHER_" + n, {option("DECIPHER_" + n, "DECIPHER", [this] { return decipher(); }),
                              option("DECIPHER", "GIVE_UP", [this] { return giveUp(); })});
  }
  Task<> giveUp() {
    setFinished("GIVE_UP");
    co_return;
  }
};

// UnrestSite.cs: only when HP <= 70%.
struct UnrestSite : Event {
  EVENT_HEADER(UnrestSite, "UNREST_SITE")
  void calculateVars() override {
    addVar("Heal", owner()->maxHp - owner()->hp);
    addVar("MaxHpLoss", 8);
  }
  bool isAllowed(Run& r) override { return Dec(r.player->hp) <= Dec(r.player->maxHp) * Dec::lit(0.70); }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "REST", [this] { return rest(); }),
            option("INITIAL", "KILL", [this] { return kill(); })};
  }
  Task<> rest() {
    Creature* p = owner();
    p->hp = std::min(p->maxHp, p->hp + val("Heal").toInt());
    run->addCardToDeck(db::card("PoorSleep"));
    setFinished("REST");
    co_return;
  }
  Task<> kill() {
    co_await run->loseMaxHp(val("MaxHpLoss").toInt());
    co_await run->obtainRelic(run->pullRelicFromFront(run->relicBag, run->rollRelicRarity(run->rng("Rewards"))));
    setFinished("KILL");
  }
};

// Wellspring.cs. PORT NOTE: BOTTLE (a random potion) is locked until potions exist.
struct Wellspring : Event {
  EVENT_HEADER(Wellspring, "WELLSPRING")
  void calculateVars() override { addVar("BatheCurses", 1); }
  std::vector<EventOption> initialOptions() override {
    return {EventOption{page("INITIAL") + ".options.BOTTLE", nullptr},
            option("INITIAL", "BATHE", [this] { return bathe(); })};
  }
  Task<> bathe() {
    auto picked = co_await run->selectFromDeck("card_selection.TO_REMOVE", [](Card*) { return true; }, 1);
    for (Card* c : picked) run->removeCardFromDeck(c);
    for (int i = 0; i < val("BatheCurses").toInt(); ++i) run->addCardToDeck(db::card("Guilty"));
    setFinished("BATHE");
  }
};

// WhisperingHollow.cs. PORT NOTE: GOLD (two potions) is locked until potions exist.
struct WhisperingHollow : Event {
  EVENT_HEADER(WhisperingHollow, "WHISPERING_HOLLOW")
  void calculateVars() override { addVar("Gold", 35 + rng().nextInt(-9, 10)); addVar("HpLoss", 9); }
  bool isAllowed(Run& r) override { return r.gold >= 44; }
  std::vector<EventOption> initialOptions() override {
    return {EventOption{page("INITIAL") + ".options.GOLD", nullptr},
            option("INITIAL", "HUG", [this] { return hug(); })};
  }
  Task<> hug() {
    auto picked = co_await run->selectFromDeck("card_selection.TO_TRANSFORM", [](Card*) { return true; }, 1);
    for (Card* c : picked) run->transformCard(c, run->randomTransformFor(c, rng()));
    co_await run->loseHp(val("HpLoss").toInt());
    if (run->died) co_return;
    setFinished("HUG");
  }
};

// WoodCarvings.cs. PORT NOTE: SNAKE (Slither enchantment) is locked.
struct WoodCarvings : Event {
  EVENT_HEADER(WoodCarvings, "WOOD_CARVINGS")
  void calculateVars() override {
    setStr("BirdCard", "cards.PECK.title");
    setStr("SnakeEnchantment", "Slither");
    setStr("ToricCard", "cards.TORIC_TOUGHNESS.title");
  }
  static bool basic(Card* c) { return c->rarity == Rarity::Basic; }
  bool isAllowed(Run& r) override {
    for (auto& c : r.deck) if (basic(c.get())) return true;
    return false;
  }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "BIRD", [this] { return transformTo("BIRD", "Peck"); }),
            EventOption{page("INITIAL") + ".options.SNAKE_LOCKED", nullptr},
            option("INITIAL", "TORUS", [this] { return transformTo("TORUS", "ToricToughness"); })};
  }
  Task<> transformTo(std::string pageName, std::string cardId) {
    auto picked = co_await run->selectFromDeck("card_selection.TO_TRANSFORM", basic, 1);
    if (!picked.empty()) run->transformCard(picked[0], db::card(cardId));
    setFinished(pageName);
  }
};

void registerAct1Events() {
  registerCardType<ByrdonisEgg>();
  registerCardType<SporeMind>();
  registerCardType<PoorSleep>();
  registerCardType<Guilty>();
  registerCardType<Peck>();
  registerCardType<ToricToughness>();
  registerPowerType<ToricToughnessPower>();
  db::registerRelic(SwordOfStone::kId, [] { return std::unique_ptr<Relic>(new SwordOfStone()); });
  db::registerRelic(SwordOfJade::kId, [] { return std::unique_ptr<Relic>(new SwordOfJade()); });
  reg<AromaOfChaos>();
  reg<ByrdonisNest>();
  reg<DenseVegetation>();
  reg<JungleMazeAdventure>();
  reg<LuminousChoir>();
  reg<MorphicGrove>();
  reg<SapphireSeed>();
  reg<SunkenStatue>();
  reg<TabletOfTruth>();
  reg<UnrestSite>();
  reg<Wellspring>();
  reg<WhisperingHollow>();
  reg<WoodCarvings>();
}

}  // namespace sts
