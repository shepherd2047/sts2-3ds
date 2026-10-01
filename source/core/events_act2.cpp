// Act 2 (Hive) events, translated from MegaCrit.Sts2.Core.Models.Events (package 5).
// ColorfulPhilosophers came with the other characters (X6). FieldOfManSizedHoles keeps
// ENTER_YOUR_HOLE locked (PerfectFit enchantment is not ported).
#include <algorithm>

#include "cards.h"

namespace sts {

namespace {
template <class E> void reg() { db::registerEvent(E::kId, [] { return std::unique_ptr<Event>(new E()); }); }
#define EVENT_HEADER(Name, Key) \
  static constexpr const char* kId = #Name; \
  Name() { id = #Name; locKey = Key; }
#define MONSTER_HEADER(Name, Key) \
  Name() { id = #Name; locKey = Key; }

using Targets = const std::vector<Creature*>&;

Intent attackIntent(int dmg, int hits = 1) { Intent i; i.kind = Intent::Attack; i.damage = dmg; i.hits = hits; return i; }
Intent kindIntent(Intent::Kind k) { Intent i; i.kind = k; return i; }

// CardFactory.CreateForReward(owner, 1, ForNonCombatWithDefaultOdds(character pool, filter) + flags).
std::unique_ptr<Card> rewardCardWhere(Run& r, std::function<bool(const Card&)> filter, uint32_t flags = 0) {
  auto o = CardCreationOptions::forNonCombat({r.characterId}, false, std::move(filter));
  auto cards = r.createForReward(o.with(flags), 1);
  return cards.empty() ? nullptr : std::move(cards[0]);
}

}  // namespace

// ---------------------------------------------------------------- cards given by events

// UltimateStrike.cs (counts as a Strike)
struct UltimateStrike : IroncladT<UltimateStrike> {
  CARD_HEADER(UltimateStrike, "ULTIMATE_STRIKE", 1, Attack, Uncommon, AnyEnemy)
    tags = tagStrike;
    addVar("Damage", 14);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
  void onUpgrade() override { upgradeVar("Damage", 6); }
};

// UltimateDefend.cs (counts as a Defend)
struct UltimateDefend : IroncladT<UltimateDefend> {
  CARD_HEADER(UltimateDefend, "ULTIMATE_DEFEND", 1, Skill, Uncommon, Self)
    tags = tagDefend;
    addVar("Block", 11);
  }
  Task<> onPlay(CardPlay&) override { co_await block(val("Block")); }
  void onUpgrade() override { upgradeVar("Block", 4); }
};

// Exterminate.cs
struct Exterminate : IroncladT<Exterminate> {
  CARD_HEADER(Exterminate, "EXTERMINATE", 1, Attack, Token, AllEnemies)
    addVar("Damage", 3);
    addVar("Repeat", 4);
  }
  Task<> onPlay(CardPlay&) override { co_await attackAll(val("Damage"), val("Repeat").toInt()); }
  void onUpgrade() override { upgradeVar("Damage", 1); }
};

// Squash.cs
struct Squash : IroncladT<Squash> {
  CARD_HEADER(Squash, "SQUASH", 1, Attack, Token, AnyEnemy)
    addVar("Damage", 10);
    addVar("VulnerablePower", 2);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<VulnerablePower>(p.target, val("VulnerablePower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 2); upgradeVar("VulnerablePower", 1); }
};

// Metamorphosis.cs: shuffle random free Attacks into the draw pile.
struct Metamorphosis : IroncladT<Metamorphosis> {
  CARD_HEADER(Metamorphosis, "METAMORPHOSIS", 2, Skill, Token, Self)
    keywords = kwExhaust;
    addVar("Cards", 3);
  }
  Task<> onPlay(CardPlay&) override {
    // CardFactory.GetForCombat over your pool's Attacks (FilterForCombat: no Basic / Ancient).
    auto made = randomForCombat(*combat, db::characterPool(combat->run->characterId, [](const Card& c) { return c.type == CardType::Attack; }),
                                val("Cards").toInt());
    for (auto& card : made) {
      card->setThisCombat(0);  // SetToFreeThisCombat
      Card* c = combat->addCard(std::move(card));
      co_await cmd::moveCard(*combat, c, Pile::Draw);
      auto& d = combat->draw;
      d.erase(std::remove(d.begin(), d.end(), c), d.end());
      d.insert(d.begin() + combat->rng("CombatCardGeneration").nextInt((int)d.size() + 1), c);
    }
  }
  void onUpgrade() override { upgradeVar("Cards", 2); }
};

// Enlightenment.cs: every card in hand costs at most 1 (this turn; upgraded: this combat).
struct Enlightenment : IroncladT<Enlightenment> {
  CARD_HEADER(Enlightenment, "ENLIGHTENMENT", 0, Skill, Token, Self)
    keywords = kwExhaust;
  }
  Task<> onPlay(CardPlay&) override {
    for (Card* c : combat->pile(Pile::Hand)) {
      if (upgraded()) c->setThisCombat(1, true);
      else c->setThisTurnOrUntilPlayed(1, true);
    }
    co_return;
  }
};

// Decay.cs: 2 damage at the end of the turn while in hand.
struct Decay : IroncladT<Decay> {
  CARD_HEADER(Decay, "DECAY", -1, Curse, Curse, None)
    keywords = kwUnplayable;
    maxUpgradeLevel = 0;
    addVar("Damage", 2);
  }
  bool hasTurnEndInHandEffect() const override { return true; }
  Task<> onTurnEndInHand() override { co_await cmd::damage(me(), val("Damage"), kUnpowered | kMove, nullptr, this); }
};

// Normality.cs: while it is in hand, at most 3 cards can be played per turn.
struct Normality : IroncladT<Normality> {
  CARD_HEADER(Normality, "NORMALITY", -1, Curse, Curse, None)
    keywords = kwUnplayable;
    maxUpgradeLevel = 0;
    addVar("CalculationBase", 3);
    addVar("CalculationExtra", -1);
    addVar("CalculatedCards", 0);
    calcMultiplier = [](Card* c) { return c->combat ? std::min(3, c->combat->cardsPlayedThisTurn) : 0; };
  }
  bool shouldPlay(Card*) override {
    if (!combat || combat->pileOf(this) != Pile::Hand) return true;
    return combat->cardsPlayedThisTurn < 3;
  }
};

// LanternKey.cs: unplayable quest card. In act 3 (Glory, index 2) every "?" room is an event and
// every event is WarHistorianRepy (events_shared3.cpp), which takes the key.
struct LanternKey : IroncladT<LanternKey> {
  CARD_HEADER(LanternKey, "LANTERN_KEY", -1, Quest, Quest, Self)
    keywords = kwUnplayable;
    maxUpgradeLevel = 0;
  }
  int modifyUnknownMapPointRoomTypes(int types) override {
    return run && run->actIndex == 2 ? roomBit(RoomType::Unknown) : types;
  }
  std::string modifyNextEvent(const std::string& eventId) override {
    return run && run->actIndex == 2 ? std::string("WarHistorianRepy") : eventId;
  }
};

// ---------------------------------------------------------------- relics

// LostWisp.cs: 8 damage to all enemies whenever a Power is played.
struct LostWispRelic : Relic {
  RELIC_HEADER(LostWispRelic, "LOST_WISP", Event)
    addVar("Damage", 8);
  }
  Task<> afterCardPlayed(const CardPlay& p) override {
    if (!combat || combat->over || p.card->type != CardType::Power) co_return;
    doFlash();
    std::vector<Creature*> targets;
    for (Creature* e : combat->aliveEnemies()) targets.push_back(e);
    co_await cmd::damage(targets, val("Damage"), kUnpowered, owner(), nullptr);
  }
};

// PollinousCore.cs: every 4th turn, draw 2 more cards.
struct PollinousCore : Relic {
  RELIC_HEADER(PollinousCore, "POLLINOUS_CORE", Event)
    addVar("Cards", 2);
    addVar("Turns", 4);
  }
  int turnsSeen = 0;
  void persist(Archive& a) override { a.io(turnsSeen); }  // [SavedProperty]
  bool showCounter() const override { return true; }
  int displayAmount() const override { return turnsSeen; }
  Task<> beforeHandDraw() override {
    turnsSeen = (turnsSeen + 1) % val("Turns").toInt();  // reset once the bonus draw is applied
    if (turnsSeen == 0) doFlash();
    return {};
  }
  Dec modifyHandDraw(Dec amount) override { return turnsSeen == 0 ? amount + val("Cards") : amount; }
  Task<> afterCombatEnd() override {
    turnsSeen = 0;
    return {};
  }
};

// ---------------------------------------------------------------- monsters

// FlailKnight.cs (the Mysterious Knight of The Lantern Key is a stronger one).
struct FlailKnight : Monster {
  FlailKnight() { id = "FlailKnight"; locKey = "FLAIL_KNIGHT"; }
  int minHp() const override { return asc(kToughEnemies, 108, 101); }
  int maxHp() const override { return minHp(); }
  void buildMoves() override {
    auto* chant = machine.add<MoveState>("WAR_CHANT");
    chant->perform = [this](Targets) { return applyToSelf<StrengthPower>(3); };
    chant->intents = {kindIntent(Intent::Buff)};
    auto* flail = machine.add<MoveState>("FLAIL_MOVE");
    flail->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 10, 9), 2); };
    flail->intents = {attackIntent(asc(kDeadlyEnemies, 10, 9), 2)};
    auto* ram = machine.add<MoveState>("RAM_MOVE");
    ram->perform = [this](Targets) { return attack(asc(kDeadlyEnemies, 17, 15)); };
    ram->intents = {attackIntent(asc(kDeadlyEnemies, 17, 15))};
    auto* rand = machine.add<RandomBranchState>("RAND");
    rand->add(chant, MoveRepeat::CannotRepeat);
    rand->add(flail, MoveRepeat::CanRepeatForever, 2.f);
    rand->add(ram, MoveRepeat::CanRepeatForever, 2.f);
    chant->followUp = flail->followUp = ram->followUp = rand;
    machine.start(ram);
  }
};

// MysteriousKnight.cs
struct MysteriousKnight : FlailKnight {
  MysteriousKnight() { id = "MysteriousKnight"; locKey = "MYSTERIOUS_KNIGHT"; }
  Task<> afterAddedToRoom() override {
    co_await applyToSelf<StrengthPower>(6);
    co_await applyToSelf<PlatingPower>(6);
  }
};

// ---------------------------------------------------------------- events

// Amalgamator.cs: merge two basic Strikes / Defends into an Ultimate one.
struct Amalgamator : Event {
  EVENT_HEADER(Amalgamator, "AMALGAMATOR")
  void calculateVars() override { setStr("Card1", "cards.ULTIMATE_STRIKE.title"); setStr("Card2", "cards.ULTIMATE_DEFEND.title"); }
  static bool valid(int tag, Card* c) { return (c->tags & tag) && c->rarity == Rarity::Basic; }
  static int count(Run& r, int tag) {
    int n = 0;
    for (auto& c : r.deck) if (valid(tag, c.get())) ++n;
    return n;
  }
  bool isAllowed(Run& r) override { return count(r, tagStrike) >= 2 && count(r, tagDefend) >= 2; }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "COMBINE_STRIKES", [this] { return combine(tagStrike, "UltimateStrike", "COMBINE_STRIKES"); }),
            option("INITIAL", "COMBINE_DEFENDS", [this] { return combine(tagDefend, "UltimateDefend", "COMBINE_DEFENDS"); })};
  }
  Task<> combine(int tag, std::string into, std::string pageName) {
    auto picked = co_await run->selectFromDeck("card_selection.TO_REMOVE", [tag](Card* c) { return valid(tag, c); }, 2);
    for (Card* c : picked) run->removeCardFromDeck(c);
    run->addCardToDeck(db::card(into));
    setFinished(pageName);
  }
};

// Bugslayer.cs
struct Bugslayer : Event {
  EVENT_HEADER(Bugslayer, "BUGSLAYER")
  void calculateVars() override { setStr("Card1", "cards.EXTERMINATE.title"); setStr("Card2", "cards.SQUASH.title"); }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "EXTERMINATION", [this] { return take("Exterminate", "EXTERMINATION"); }),
            option("INITIAL", "SQUASH", [this] { return take("Squash", "SQUASH"); })};
  }
  Task<> take(std::string card, std::string pageName) {
    run->addCardToDeck(db::card(card));
    setFinished(pageName);
    co_return;
  }
};

// ColorfulPhilosophers.cs: up to three of the other characters' colours (CardPoolColorOrder:
// Necrobinder, Ironclad, Regent, Silent, Defect; the extra ones removed with Rng.NextInt), each
// giving three card rewards (Common, Uncommon, Rare) from that character's pool.
// IsAllowed: every player has more than one character card pool unlocked -- always true here.
struct ColorfulPhilosophers : Event {
  EVENT_HEADER(ColorfulPhilosophers, "COLORFUL_PHILOSOPHERS")
  void calculateVars() override { addVar("Cards", 3); }
  std::vector<EventOption> initialOptions() override {
    static const char* const kColorOrder[] = {"Necrobinder", "Ironclad", "Regent", "Silent", "Defect"};
    std::vector<EventOption> list;
    for (const char* pool : kColorOrder) {
      // PORT NOTE (n/a: owner): UnlockState.CharacterCardPools = every character whose cards are ported (everything is unlocked).
      if (pool == run->characterId || !db::characterPlayable(pool)) continue;
      std::string ch = pool;
      // "...options." + CardPoolModel.EnergyColorName.ToUpperInvariant()
      list.push_back(option("INITIAL", db::character(ch).key, [this, ch] { return offer(ch); }));
    }
    int keep = std::min(3, (int)list.size());
    while ((int)list.size() > keep) list.erase(list.begin() + rng().nextInt((int)list.size()));
    return list;
  }
  // CardReward(CardCreationOptions([pool], Other, Uniform, rarity == r, NoRarityModification |
  // NoCardPoolModifications), Cards): the three rewards are populated in order.
  Task<> offer(std::string ch) {
    std::vector<Run::RewardItem> rows;
    for (Rarity rarity : {Rarity::Common, Rarity::Uncommon, Rarity::Rare}) {
      CardCreationOptions o;
      o.pools = {ch};
      o.odds = RarityOdds::Uniform;
      o.filter = [rarity](const Card& c) { return c.rarity == rarity; };
      o.with(ccNoRarityModification | ccNoCardPoolModifications);
      Run::RewardItem row = run->makeCardReward(o, val("Cards").toInt());
      if (!row.cards.empty()) rows.push_back(std::move(row));
    }
    co_await run->offerRewards(std::move(rows));
    setFinished("DONE");
  }
};

// ColossalFlower.cs: dig for gold (5, 6 damage), then Pollinous Core for 7 more.
struct ColossalFlower : Event {
  EVENT_HEADER(ColossalFlower, "COLOSSAL_FLOWER")
  static constexpr int kCost[3] = {35, 75, 135};
  static constexpr int kDamage[3] = {5, 6, 7};
  int digs = 0;
  void calculateVars() override { addVar("Prize1", kCost[0]); addVar("Prize2", kCost[1]); addVar("Prize3", kCost[2]); }
  bool isAllowed(Run& r) override { return r.player->hp >= 19; }
  std::vector<EventOption> initialOptions() override {
    std::string n = std::to_string(digs + 1);
    return {option("INITIAL", "EXTRACT_CURRENT_PRIZE_" + n, [this] { return extract("EXTRACT_CURRENT_PRIZE"); }),
            option("INITIAL", "REACH_DEEPER_" + n, [this] { return reachDeeper(); })};
  }
  Task<> extract(std::string pageName) {
    co_await run->gainGold(kCost[digs]);
    setFinished(pageName);
  }
  Task<> reachDeeper() {
    co_await run->loseHp(kDamage[digs]);
    if (run->died) co_return;
    ++digs;
    std::string n = std::to_string(digs);
    if (digs < 2) {
      setPage("REACH_DEEPER_" + n,
              {option("REACH_DEEPER_" + n, "EXTRACT_CURRENT_PRIZE_" + std::to_string(digs + 1), [this] { return extract("EXTRACT_CURRENT_PRIZE"); }),
               option("REACH_DEEPER_" + n, "REACH_DEEPER_" + std::to_string(digs + 1), [this] { return reachDeeper(); })});
    } else {
      setPage("REACH_DEEPER_2", {option("REACH_DEEPER_2", "EXTRACT_INSTEAD", [this] { return extract("EXTRACT_INSTEAD"); }),
                                 option("REACH_DEEPER_2", "POLLINOUS_CORE", [this] { return core(); })});
    }
  }
  Task<> core() {
    co_await run->loseHp(kDamage[digs]);
    if (run->died) co_return;
    co_await run->obtainRelic(db::relic("PollinousCore"));
    setFinished("POLLINOUS_CORE");
  }
};

// FieldOfManSizedHoles.cs
struct FieldOfManSizedHoles : Event {
  EVENT_HEADER(FieldOfManSizedHoles, "FIELD_OF_MAN_SIZED_HOLES")
  void calculateVars() override {
    addVar("Gold", 75);
    addVar("Cards", 2);
    setStr("ResistCurse", "cards.NORMALITY.title");
    setStr("Enchantment", "enchantments.PERFECT_FIT.title");
  }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "RESIST", [this] { return resist(); }),
            option("INITIAL", "ENTER_YOUR_HOLE", [this] { return enterYourHole(); })};
  }
  bool isAllowed(Run& r) override { return r.canEnchantAny("PerfectFit"); }
  Task<> enterYourHole() {
    auto picked = co_await run->selectForEnchantment("PerfectFit", 1);
    if (!picked.empty()) run->enchantCard(picked[0], "PerfectFit", 1);
    setFinished("ENTER_YOUR_HOLE");
  }
  Task<> resist() {
    auto picked = co_await run->selectFromDeck("card_selection.TO_REMOVE", [](Card*) { return true; }, val("Cards").toInt());
    for (Card* c : picked) run->removeCardFromDeck(c);
    run->addCardToDeck(db::card("Normality"));
    setFinished("RESIST");
  }
};

// InfestedAutomaton.cs: a random Power, or a random 0-cost card.
struct InfestedAutomaton : Event {
  EVENT_HEADER(InfestedAutomaton, "INFESTED_AUTOMATON")
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "STUDY", [this] { return study(); }),
            option("INITIAL", "TOUCH_CORE", [this] { return touchCore(); })};
  }
  Task<> study() {
    if (auto c = rewardCardWhere(*run, [](const Card& c) { return c.type == CardType::Power; })) run->addCardToDeck(std::move(c));
    setFinished("STUDY");
    co_return;
  }
  Task<> touchCore() {
    if (auto c = rewardCardWhere(*run, [](const Card& c) { return c.canonicalCost == 0 && !c.costsX; }, ccNoCardPoolModifications)) run->addCardToDeck(std::move(c));
    setFinished("TOUCH_CORE");
    co_return;
  }
};

// LostWisp.cs
struct LostWisp : Event {
  EVENT_HEADER(LostWisp, "LOST_WISP")
  void calculateVars() override {
    addVar("Gold", 60 + rng().nextInt(-15, 16));
    setStr("Relic", "relics.LOST_WISP.title");
    setStr("Curse", "cards.DECAY.title");
  }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "CLAIM", [this] { return claim(); }),
            option("INITIAL", "SEARCH", [this] { return search(); })};
  }
  Task<> claim() {
    run->addCardToDeck(db::card("Decay"));
    co_await run->obtainRelic(db::relic("LostWispRelic"));
    setFinished("CLAIM");
  }
  Task<> search() {
    co_await run->gainGold(val("Gold").toInt());
    setFinished("SEARCH");
  }
};

// SpiritGrafter.cs
struct SpiritGrafter : Event {
  EVENT_HEADER(SpiritGrafter, "SPIRIT_GRAFTER")
  void calculateVars() override { addVar("RejectionHpLoss", 10); addVar("LetItInHealAmount", 25); }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "LET_IT_IN", [this] { return letItIn(); }),
            option("INITIAL", "REJECTION", [this] { return rejection(); })};
  }
  Task<> letItIn() {
    Creature* p = owner();
    p->hp = std::min(p->maxHp, p->hp + val("LetItInHealAmount").toInt());
    run->addCardToDeck(db::card("Metamorphosis"));
    setFinished("LET_IT_IN");
    co_return;
  }
  Task<> rejection() {
    auto picked = co_await run->selectFromDeck("card_selection.TO_UPGRADE", [](Card* c) { return c->upgradable(); }, 1, false, true);
    if (!picked.empty()) picked[0]->upgrade();
    co_await run->loseHp(val("RejectionHpLoss").toInt());
    if (run->died) co_return;
    setFinished("REJECTION");
  }
};

// TheLanternKey.cs: give the key back for gold, or fight the Mysterious Knight for it.
struct TheLanternKey : Event {
  EVENT_HEADER(TheLanternKey, "THE_LANTERN_KEY")
  void calculateVars() override { addVar("Gold", 100); }
  std::vector<EventOption> initialOptions() override {
    return {option("INITIAL", "RETURN_THE_KEY", [this] { return returnKey(); }),
            option("INITIAL", "KEEP_THE_KEY", [this] { return keepKey(); })};
  }
  Task<> returnKey() {
    co_await run->gainGold(val("Gold").toInt());
    descKey = page("DONE") + ".options.RETURN_THE_KEY.description";
    options.clear();
    finished = true;
  }
  Task<> keepKey() {
    setPage("KEEP_THE_KEY", {option("KEEP_THE_KEY", "FIGHT", [this] { return fight(); })});
    co_return;
  }
  Task<> fight() {
    // EnterCombatWithoutExitingEvent(extraRewards: SpecialCardReward(LanternKey)).
    Run::RewardItem key;
    key.kind = Run::RewardKind::SpecialCard;
    key.card = db::card("LanternKey");
    run->roomExtraRewards.push_back(std::move(key));
    bool won = co_await run->eventFight("MysteriousKnightEventEncounter");
    if (!won) co_return;
    finished = true;
    options.clear();
  }
};

// ZenWeaver.cs
struct ZenWeaver : Event {
  EVENT_HEADER(ZenWeaver, "ZEN_WEAVER")
  void calculateVars() override { addVar("BreathingTechniquesCost", 50); addVar("EmotionalAwarenessCost", 125); addVar("ArachnidAcupunctureCost", 250); }
  bool isAllowed(Run& r) override { return r.gold >= 125; }
  std::vector<EventOption> initialOptions() override {
    int emo = val("EmotionalAwarenessCost").toInt(), ara = val("ArachnidAcupunctureCost").toInt();
    EventOption locked{page("INITIAL") + ".options.LOCKED", nullptr};
    return {option("INITIAL", "BREATHING_TECHNIQUES", [this] { return breathing(); }),
            run->gold < emo ? locked : option("INITIAL", "EMOTIONAL_AWARENESS", [this] { return remove("EmotionalAwarenessCost", 1, "EMOTIONAL_AWARENESS"); }),
            run->gold < ara ? locked : option("INITIAL", "ARACHNID_ACUPUNCTURE", [this] { return remove("ArachnidAcupunctureCost", 2, "ARACHNID_ACUPUNCTURE"); })};
  }
  Task<> breathing() {
    run->gold -= val("BreathingTechniquesCost").toInt();  // LoseGold(Spent)
    for (int i = 0; i < 2; ++i) run->addCardToDeck(db::card("Enlightenment"));
    setFinished("BREATHING_TECHNIQUES");
    co_return;
  }
  Task<> remove(const char* costVar, int count, std::string pageName) {
    auto picked = co_await run->selectFromDeck("card_selection.TO_REMOVE", [](Card*) { return true; }, count);
    for (Card* c : picked) run->removeCardFromDeck(c);
    run->gold -= val(costVar).toInt();
    setFinished(pageName);
  }
};

void registerAct2Events() {
  registerCardType<UltimateStrike>();
  registerCardType<UltimateDefend>();
  registerCardType<Exterminate>();
  registerCardType<Squash>();
  registerCardType<Metamorphosis>();
  registerCardType<Enlightenment>();
  registerCardType<Decay>();
  registerCardType<Normality>();
  registerCardType<LanternKey>();
  db::registerRelic("LostWispRelic", [] { return std::unique_ptr<Relic>(new LostWispRelic()); });
  db::registerRelic(PollinousCore::kId, [] { return std::unique_ptr<Relic>(new PollinousCore()); });
  db::registerEncounter("MysteriousKnightEventEncounter", RoomType::Monster, false, [](Rng&) {
    std::vector<std::unique_ptr<Monster>> v;
    v.push_back(std::make_unique<MysteriousKnight>());
    return v;
  });
  reg<Amalgamator>();
  reg<Bugslayer>();
  reg<ColorfulPhilosophers>();
  reg<ColossalFlower>();
  reg<FieldOfManSizedHoles>();
  reg<InfestedAutomaton>();
  reg<LostWisp>();
  reg<SpiritGrafter>();
  reg<TheLanternKey>();
  reg<ZenWeaver>();
}

namespace db {
// Hive.AllEvents.
const std::vector<std::string>& act2Events() {
  static const std::vector<std::string> ids = {"Amalgamator", "Bugslayer", "ColorfulPhilosophers", "ColossalFlower", "FieldOfManSizedHoles",
                                               "InfestedAutomaton", "LostWisp", "SpiritGrafter", "TheLanternKey", "ZenWeaver"};
  return ids;
}
}  // namespace db

}  // namespace sts
