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
// PORT NOTE (n/a: equivalent): CardRarity.Event has no counterpart here; event cards use Token (both are
// kept out of every card pool and CardFactory's in-combat generation); only the rarity label differs.

// ByrdonisEgg.cs: unplayable quest card; rest sites offer Hatch (HatchRestSiteOption, Run::restSite
// option 7: obtain Byrdpip, pets.cpp, which turns the eggs into ByrdSwoops and adds the pet).
struct ByrdonisEgg : IroncladT<ByrdonisEgg> {
  CARD_HEADER(ByrdonisEgg, "BYRDONIS_EGG", -1, Quest, Quest, None)
    keywords = kwUnplayable;
    maxUpgradeLevel = 0;
  }
  bool tryModifyRestSiteOptions(std::vector<int>& options) override {
    if (!inDeck()) return false;
    if (std::find(options.begin(), options.end(), 7) == options.end()) options.push_back(7);  // one Hatch per player
    return true;
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

// Guilty.cs: leaves the deck after 5 combats. CombatsSeen is the "Combats" var (5 - CombatsSeen),
// which the card save keeps.
struct Guilty : IroncladT<Guilty> {
  CARD_HEADER(Guilty, "GUILTY", -1, Curse, Curse, None)
    keywords = kwUnplayable;
    maxUpgradeLevel = 0;
    addVar("Combats", 5);
  }
  Task<> afterCombatEnd() override {
    if (!inDeck()) co_return;
    int seen = 5 - val("Combats").toInt() + 1;  // CombatsSeen++
    var("Combats")->base = Dec(5 - seen);
    if (seen >= 5) run->removeCardFromDeck(this);  // CardPileCmd.RemoveFromDeck (frees this card)
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

// ToricToughnessPower.cs: after block is cleared, gain the stored block and count down. Instanced:
// each play keeps its own block and turns.
struct ToricToughnessPower : Power {
  POWER_HEADER(ToricToughnessPower, "TORIC_TOUGHNESS_POWER")
  PowerInstanceType instanceType() const override { return PowerInstanceType::Instanced; }
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
    auto* pw = co_await applyPowerGet<ToricToughnessPower>(me(), val("Turns"), me(), this);
    if (pw) pw->block = gained;
  }
  void onUpgrade() override { upgradeVar("Block", 2); }
};

// SwordOfJade.cs (SwordOfStone's replacement): +3 Strength at the start of each combat.
struct SwordOfJade : Relic {
  RELIC_HEADER(SwordOfJade, "SWORD_OF_JADE", Event)
    addVar("StrengthPower", 3);
  }
  Task<> beforeCombatStart() override {
    doFlash();
    co_await applyPower<StrengthPower>(owner(), val("StrengthPower"), owner(), nullptr);
  }
};

// SwordOfStone.cs: after 5 elites it turns into Sword of Jade.
struct SwordOfStone : Relic {
  RELIC_HEADER(SwordOfStone, "SWORD_OF_STONE", Event)
    addVar("Elites", 5);
  }
  int elitesDefeated = 0;
  void persist(Archive& a) override { a.io(elitesDefeated); }  // [SavedProperty]
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
  void calculateVars() override { setStr("AromaPrinciple", "characters." + run->character().key + ".aromaPrinciple"); }
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
    // HealRestSiteOption.GetHealAmount: 30% of max HP through the rest-site heal hooks.
    Dec heal = Dec(owner()->maxHp) * Dec::lit(0.3);
    for (Model* m : run->listeners()) heal = m->modifyRestSiteHealAmount(owner(), heal);
    addVar("Heal", heal.toInt());
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
    run->player->combat = nullptr;
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
    // IsAllowed runs on the canonical model: the base Gold var (149), before CalculateVars rolls it
    // down; and RelicGrabBag.HasAvailableRelics (some rarity deque still holds an allowed relic).
    if (r.gold < 149) return false;
    r.removeDisallowedRelics();
    for (RelicRarity k : {RelicRarity::Common, RelicRarity::Uncommon, RelicRarity::Rare, RelicRarity::Shop}) {
      auto it = r.relicBag.find(k);
      if (it != r.relicBag.end() && !it->second.empty()) return true;
    }
    return false;
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

// SapphireSeed.cs
struct SapphireSeed : Event {
  EVENT_HEADER(SapphireSeed, "SAPPHIRE_SEED")
  void calculateVars() override { setStr("Enchantment", "enchantments.SOWN.title"); addVar("Heal", 9); }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "EAT", [this] { return eat(); }),
            option("INITIAL", "PLANT", [this] { return plant(); })};
  }
  Task<> plant() {
    auto picked = co_await run->selectForEnchantment("Sown", 1);
    if (!picked.empty()) run->enchantCard(picked[0], "Sown", 1);
    setFinished("PLANT");
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

// Wellspring.cs
struct Wellspring : Event {
  EVENT_HEADER(Wellspring, "WELLSPRING")
  void calculateVars() override { addVar("BatheCurses", 1); }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "BOTTLE", [this] { return bottle(); }),
            option("INITIAL", "BATHE", [this] { return bathe(); })};
  }
  Task<> bottle() {
    // Character potion pool + SharedPotionPool, one NextItem from the Rewards stream.
    std::vector<std::string> items;
    for (auto& id : db::potionPool(run->characterId)) if (db::potion(id)) items.push_back(id);
    std::string pick = run->rng("Rewards").nextItem(items);
    if (!pick.empty()) {
      auto p = db::potion(pick);
      p->run = run;
      co_await run->offerPotion(std::move(p));  // RewardsCmd.OfferCustom(PotionReward)
    }
    setFinished("BOTTLE");
  }
  Task<> bathe() {
    auto picked = co_await run->selectFromDeck("card_selection.TO_REMOVE", [](Card*) { return true; }, 1);
    for (Card* c : picked) run->removeCardFromDeck(c);
    for (int i = 0; i < val("BatheCurses").toInt(); ++i) run->addCardToDeck(db::card("Guilty"));
    setFinished("BATHE");
  }
};

// WhisperingHollow.cs
struct WhisperingHollow : Event {
  EVENT_HEADER(WhisperingHollow, "WHISPERING_HOLLOW")
  void calculateVars() override { addVar("Gold", 35 + rng().nextInt(-9, 10)); addVar("HpLoss", 9); }
  bool isAllowed(Run& r) override { return r.gold >= 44; }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "GOLD", [this] { return gold(); }),
            option("INITIAL", "HUG", [this] { return hug(); })};
  }
  Task<> gold() {
    run->gold -= val("Gold").toInt();  // PlayerCmd.LoseGold(Spent)
    // Two PotionRewards, rolled first (Populate), then offered in order.
    auto p1 = run->randomPotion(run->rng("Rewards"), false);
    auto p2 = run->randomPotion(run->rng("Rewards"), false);
    co_await run->offerPotion(std::move(p1));
    co_await run->offerPotion(std::move(p2));
    setFinished("GOLD");
  }
  Task<> hug() {
    auto picked = co_await run->selectFromDeck("card_selection.TO_TRANSFORM", [](Card*) { return true; }, 1);
    for (Card* c : picked) run->transformCard(c, run->randomTransformFor(c, rng()));
    co_await run->loseHp(val("HpLoss").toInt());
    if (run->died) co_return;
    setFinished("HUG");
  }
};

// WoodCarvings.cs
struct WoodCarvings : Event {
  EVENT_HEADER(WoodCarvings, "WOOD_CARVINGS")
  void calculateVars() override {
    setStr("BirdCard", "cards.PECK.title");
    setStr("SnakeEnchantment", "enchantments.SLITHER.title");
    setStr("ToricCard", "cards.TORIC_TOUGHNESS.title");
  }
  static bool basic(Card* c) { return c->rarity == Rarity::Basic; }
  bool isAllowed(Run& r) override {
    for (auto& c : r.deck) if (basic(c.get())) return true;
    return false;
  }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "BIRD", [this] { return transformTo("BIRD", "Peck"); }),
            run->canEnchantAny("Slither") ? option("INITIAL", "SNAKE", [this] { return snake(); })
                                          : EventOption{page("INITIAL") + ".options.SNAKE_LOCKED", nullptr},
            option("INITIAL", "TORUS", [this] { return transformTo("TORUS", "ToricToughness"); })};
  }
  Task<> snake() {
    auto picked = co_await run->selectForEnchantment("Slither", 1);
    if (!picked.empty()) run->enchantCard(picked[0], "Slither", 1);
    setFinished("SNAKE");
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
