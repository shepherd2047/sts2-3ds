// A2: curse / status cards (Debt, Writhe, Beckon), the event card pool (Abundance ... Stack) and the
// quest cards Dowsing / SpoilsMap. AscendersBane, Void and Debris already exist elsewhere.
#include "cards.h"

namespace sts {

namespace {

// ---------------------------------------------------------------- helpers

// CardFactory.GetDistinctForCombat(Owner.Character.CardPool.Where(keep), n, CombatCardGeneration): the shared
// FilterForCombat helper (card_factory.h).
std::vector<std::unique_ptr<Card>> poolDistinctForCombat(Combat& c, std::function<bool(const Card&)> keep, int n) {
  return distinctForCombat(c, db::characterPool(c.run->characterId, std::move(keep)), n);
}

// ---------------------------------------------------------------- curses / status

// Debt.cs: unplayable curse; at the end of the turn in hand it takes up to 10 gold.
struct Debt : IroncladT<Debt> {
  CARD_HEADER(Debt, "DEBT", -1, Curse, Curse, None)
    keywords = kwUnplayable;
    maxUpgradeLevel = 0;
    addVar("Gold", 10);
  }
  bool hasTurnEndInHandEffect() const override { return true; }
  Task<> onTurnEndInHand() override {
    if (!combat || !combat->run) co_return;
    int num = std::min(val("Gold").toInt(), combat->run->gold);
    combat->run->gold -= num;  // PlayerCmd.LoseGold
    co_return;
  }
};

// Writhe.cs: unplayable, Innate curse.
struct Writhe : IroncladT<Writhe> {
  CARD_HEADER(Writhe, "WRITHE", -1, Curse, Curse, None)
    keywords = kwInnate | kwUnplayable;
    maxUpgradeLevel = 0;
  }
};

// Beckon.cs: a playable (does nothing) status card that costs 6 HP if still in hand at turn end.
struct Beckon : IroncladT<Beckon> {
  CARD_HEADER(Beckon, "BECKON", 1, Status, Status, None)
    maxUpgradeLevel = 0;
    addVar("HpLoss", 6);
  }
  bool hasTurnEndInHandEffect() const override { return true; }
  Task<> onTurnEndInHand() override {
    co_await cmd::damage(me(), val("HpLoss"), kUnblockable | kUnpowered | kMove, nullptr, this);
  }
};

// ---------------------------------------------------------------- powers

// FeedingFrenzyPower.cs: a TemporaryStrengthPower (copy of SetupStrikePower in powers.h).
struct FeedingFrenzyPower : Power {
  POWER_HEADER(FeedingFrenzyPower, "FEEDING_FRENZY_POWER")
  const char* internallyAppliedPower() const override { return "StrengthPower"; }  // ITemporaryPower
  Task<> beforeApplied(Creature* target, Dec amt, Creature* app, Card* src) override {
    co_await applyPower<StrengthPower>(target, amt, app, src, true);
  }
  Task<> afterPowerAmountChanged(Power* p, Dec amt, Creature* app, Card* src) override {
    if (!(amt == Dec(amount)) && p == this) co_await applyPower<StrengthPower>(owner, amt, app, src, true);
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) {
      flash = 1.f;
      Creature* o = owner;
      int a = amount;
      co_await cmd::removePower(this);
      co_await applyPower<StrengthPower>(o, -a, o, nullptr);
    }
  }
};

// HelloWorldPower.cs: before the hand draw, add AmountOnTurnStart random Common cards of the
// character's pool to the hand.
struct HelloWorldPower : Power {
  POWER_HEADER(HelloWorldPower, "HELLO_WORLD_POWER")
  Task<> beforeHandDraw() override {
    if (!owner || !owner->combat || amountOnTurnStart < 1) co_return;
    flash = 1.f;
    Combat& c = *owner->combat;
    auto cards = poolDistinctForCombat(c, [](const Card& k) { return k.rarity == Rarity::Common; }, amountOnTurnStart);
    for (auto& k : cards) co_await cmd::addGeneratedCard(c, std::move(k), Pile::Hand);
  }
};

// ReboundPower.cs: the next Amount cards that would go to the discard pile go on top of the draw
// pile instead (decremented as the location changes, before the card's OnPlay); removed at the end
// of the turn.
struct ReboundPower : Power {
  POWER_HEADER(ReboundPower, "REBOUND_POWER")
  Pile modifyCardPlayResultLocation(Card* card, bool, Pile pile) override {
    if (ownerOf(card) != owner || pile != Pile::Discard) return pile;
    return Pile::Draw;
  }
  Task<> afterModifyingCardPlayResultLocation(Card* card, Pile) override {
    if (ownerOf(card) != owner) co_return;
    flash = 1.f;
    co_await cmd::decrement(this);  // may remove the power: nothing may touch it afterwards
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await cmd::removePower(this);
  }
};

// ---------------------------------------------------------------- event cards (EventCardPool)

// Abundance.cs: 3 random upgraded Powers of the character's pool, pick one, it is free this turn.
struct Abundance : IroncladT<Abundance> {
  bool canBeGeneratedInCombat() const override { return false; }
  CARD_HEADER(Abundance, "ABUNDANCE", 1, Skill, Ancient, Self)
    keywords = kwExhaust;
  }
  Task<> onPlay(CardPlay&) override {
    auto made = poolDistinctForCombat(*combat, [](const Card& k) { return k.type == CardType::Power; }, 3);
    std::vector<Card*> opts;
    for (auto& k : made) {
      cmd::upgradeCard(k.get());
      opts.push_back(k.get());
    }
    auto picked = co_await cmd::selectCards(*combat, "cards.ABUNDANCE.selectionScreenPrompt", opts, 1, 1);
    if (picked.empty()) co_return;
    for (auto& k : made)
      if (k.get() == picked[0]) {
        k->setThisTurnOrUntilPlayed(0);  // SetToFreeThisTurn
        co_await cmd::addGeneratedCard(*combat, std::move(k), Pile::Hand);
        break;
      }
  }
  void onUpgrade() override { cost -= 1; }
};

// ByrdSwoop.cs
struct ByrdSwoop : IroncladT<ByrdSwoop> {
  CARD_HEADER(ByrdSwoop, "BYRD_SWOOP", 0, Attack, Event, AnyEnemy)
    addVar("Damage", 14);
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
  void onUpgrade() override { upgradeVar("Damage", 4); }
};

// Caltrops.cs
struct Caltrops : IroncladT<Caltrops> {
  CARD_HEADER(Caltrops, "CALTROPS", 1, Power, Event, Self)
    addVar("ThornsPower", 3);
  }
  Task<> onPlay(CardPlay&) override {
    auto p = db::power("ThornsPower");
    if (p) co_await cmd::applyPower(std::move(p), me(), val("ThornsPower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("ThornsPower", 2); }
};

// Clash.cs: only playable while every card in hand is an Attack.
struct Clash : IroncladT<Clash> {
  CARD_HEADER(Clash, "CLASH", 0, Attack, Event, AnyEnemy)
    addVar("Damage", 14);
  }
  bool shouldPlay(Card* c) override {
    if (c != this || !combat) return true;
    for (Card* h : combat->hand)
      if (h->type != CardType::Attack) return false;
    return true;
  }
  Task<> onPlay(CardPlay& p) override { co_await attack(p.target, val("Damage")); }
  void onUpgrade() override { upgradeVar("Damage", 4); }
};

// Distraction.cs: a random Skill of the character's pool, free this turn.
struct Distraction : IroncladT<Distraction> {
  CARD_HEADER(Distraction, "DISTRACTION", 1, Skill, Event, Self)
    keywords = kwExhaust;
  }
  Task<> onPlay(CardPlay&) override {
    auto made = poolDistinctForCombat(*combat, [](const Card& k) { return k.type == CardType::Skill; }, 1);
    if (made.empty()) co_return;
    made[0]->setThisTurnOrUntilPlayed(0);  // SetToFreeThisTurn
    co_await cmd::addGeneratedCard(*combat, std::move(made[0]), Pile::Hand);
  }
  void onUpgrade() override { cost -= 1; }
};

// DualWield.cs: copy an Attack or Power in hand.
struct DualWield : IroncladT<DualWield> {
  CARD_HEADER(DualWield, "DUAL_WIELD", 1, Skill, Event, Self)
    addVar("Cards", 1);
  }
  Task<> onPlay(CardPlay&) override {
    std::vector<Card*> opts;
    for (Card* h : combat->hand)
      if (h->type == CardType::Attack || h->type == CardType::Power) opts.push_back(h);
    auto picked = co_await cmd::selectCards(*combat, "cards.DUAL_WIELD.selectionScreenPrompt", opts, 1, 1);
    if (picked.empty()) co_return;
    Card* selection = picked[0];
    for (int i = 0; i < val("Cards").toInt(); ++i) {
      auto copy = selection->createClone();
      copy->isDupe = false;
      co_await cmd::addGeneratedCard(*combat, std::move(copy), Pile::Hand);
    }
  }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

// Entrench.cs: double the block (gain block equal to the current block, unpowered).
struct Entrench : IroncladT<Entrench> {
  CARD_HEADER(Entrench, "ENTRENCH", 2, Skill, Event, Self)
  }
  bool gainsBlock() const override { return true; }
  Task<> onPlay(CardPlay&) override {
    co_await cmd::gainBlock(me(), Dec(me()->block), kUnpowered | kMove, this);
  }
  void onUpgrade() override { cost -= 1; }
};

// FeedingFrenzy.cs: temporary Strength.
struct FeedingFrenzy : IroncladT<FeedingFrenzy> {
  CARD_HEADER(FeedingFrenzy, "FEEDING_FRENZY", 0, Skill, Event, Self)
    addVar("StrengthPower", 5);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<FeedingFrenzyPower>(me(), val("StrengthPower"), me(), this);
  }
  void onUpgrade() override { upgradeVar("StrengthPower", 2); }
};

// HelloWorld.cs
struct HelloWorld : IroncladT<HelloWorld> {
  CARD_HEADER(HelloWorld, "HELLO_WORLD", 1, Power, Event, Self)
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<HelloWorldPower>(me(), Dec(1), me(), this); }
  void onUpgrade() override { addKeyword(kwInnate); }
};

// Outmaneuver.cs
struct Outmaneuver : IroncladT<Outmaneuver> {
  CARD_HEADER(Outmaneuver, "OUTMANEUVER", 1, Skill, Event, Self)
    addVar("Energy", 2);
  }
  Task<> onPlay(CardPlay&) override {
    co_await applyPower<EnergyNextTurnPower>(me(), Dec(val("Energy").toInt()), me(), this);
  }
  void onUpgrade() override { upgradeVar("Energy", 1); }
};

// Rebound.cs
struct Rebound : IroncladT<Rebound> {
  CARD_HEADER(Rebound, "REBOUND", 1, Attack, Event, AnyEnemy)
    addVar("Damage", 9);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await applyPower<ReboundPower>(me(), Dec(1), me(), this);
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

// RipAndTear.cs: 2 hits at random enemies.
struct RipAndTear : IroncladT<RipAndTear> {
  CARD_HEADER(RipAndTear, "RIP_AND_TEAR", 1, Attack, Event, RandomEnemy)
    addVar("Damage", 7);
  }
  Task<> onPlay(CardPlay&) override { co_await attackRandom(val("Damage"), 2); }
  void onUpgrade() override { upgradeVar("Damage", 2); }
};

// Stack.cs: block equal to the discard pile size (+3 base when upgraded).
struct Stack : IroncladT<Stack> {
  CARD_HEADER(Stack, "STACK", 1, Skill, Event, Self)
    addVar("CalculationBase", 0);
    addVar("CalculationExtra", 1);
    addVar("CalculatedBlock", 0);
    calcMultiplier = [](Card* c) { return c->combat ? (int)c->combat->discard.size() : 0; };
  }
  bool gainsBlock() const override { return true; }
  Task<> onPlay(CardPlay&) override { co_await block(calculatedBlock()); }
  void onUpgrade() override { upgradeVar("CalculationBase", 3); }
};

// ---------------------------------------------------------------- quest cards

// Dowsing.cs (Quest, DowsingRod): counts the "?" map points entered while it is in the deck; at 5 it
// completes its quest and transforms into Abundance. RoomsEntered is the "Rooms" var (5 - RoomsEntered),
// which the card save keeps.
struct Dowsing : IroncladT<Dowsing> {
  CARD_HEADER(Dowsing, "DOWSING", -1, Quest, Quest, None)
    keywords = kwUnplayable;
    maxUpgradeLevel = 0;
    addVar("Rooms", 5);
  }
  Task<> beforeRoomEntered(RoomType) override {
    if (!inDeck() || run->currentRoomCount > 1) co_return;
    if (run->currentNode < 0 || run->nodes[run->currentNode].type != RoomType::Unknown) co_return;  // MapPointType.Unknown
    int roomsEntered = 5 - val("Rooms").toInt() + 1;  // RoomsEntered++
    var("Rooms")->base = Dec(5 - roomsEntered);
    if (roomsEntered >= 5) run->transformCard(this, db::card("Abundance"));  // CompleteQuest + TransformTo<Abundance>
  }
};

// SpoilsMap.cs (Quest, TheLegendsWereTrue): act 2 (SpoilsActIndex, set by AfterCreated) becomes a
// SpoilsActMap whose treasure point carries the quest; opening that chest gives 600 gold and removes
// the card (Run::handleSpoilsMap -> OnQuestComplete). SpoilsCoord is not saved, as in the C#: the
// late map hook finds it again on a loaded map.
struct SpoilsMap : IroncladT<SpoilsMap> {
  CARD_HEADER(SpoilsMap, "SPOILS_MAP", -1, Quest, Quest, Self)
    keywords = kwUnplayable;
    maxUpgradeLevel = 0;
    addVar("Gold", 600);
  }
  int spoilsActIndex = 1;  // [SavedProperty] SpoilsActIndex: AfterCreated always sets 1
  int spoilsCol = -1, spoilsRow = -1;  // SpoilsCoord (our map indexing)
  int spoilsNode() const {
    if (!run) return -1;
    for (int i = 0; i < (int)run->nodes.size(); ++i)
      if (run->nodes[i].col == spoilsCol && run->nodes[i].row == spoilsRow && run->nodes[i].type != RoomType::Ancient) return i;
    return -1;
  }
  void modifyGeneratedMap(int actIndex) override {
    if (actIndex != spoilsActIndex || !inDeck()) return;
    Rng rng(run->seed, "spoils_map");  // new SpoilsActMap(runState): Rng(seed, "spoils_map")
    run->nodes = generateSpoilsActMap(rng, actIndex, run->hasAscension(kSwarmingElites) ? 8 : 5, nullptr);
    run->spoilsActMap = true;
  }
  void modifyGeneratedMapLate(int actIndex) override {
    if (actIndex != spoilsActIndex || !inDeck()) return;
    // GetAllMapPoints().FirstOrDefault(Treasure): column by column, then by row.
    const MapNode* best = nullptr;
    for (auto& n : run->nodes)
      if (n.type == RoomType::Treasure && (!best || n.col < best->col || (n.col == best->col && n.row < best->row))) best = &n;
    if (best) { spoilsCol = best->col; spoilsRow = best->row; }
  }
  void afterMapGenerated(int actIndex) override {
    if (!inDeck() || actIndex != spoilsActIndex) return;
    if (int n = spoilsNode(); n >= 0) run->nodes[n].quests.push_back(id);  // MapPoint.AddQuest
  }
  void beforeCardRemoved(Card* card) override {
    if (card != this || !run || spoilsActIndex != run->actIndex || spoilsCol < 0) return;
    if (int n = spoilsNode(); n >= 0) {  // MapPoint.RemoveQuest
      auto& q = run->nodes[n].quests;
      if (auto it = std::find(q.begin(), q.end(), id); it != q.end()) q.erase(it);
    }
  }
  Task<int> onQuestComplete() override {
    int gold = val("Gold").toInt();
    Run* r = run;
    co_await r->gainGold(gold);
    r->removeCardFromDeck(this);  // CompleteQuest + CardPileCmd.RemoveFromDeck (frees this card)
    co_return gold;
  }
};

}  // namespace

void registerMiscCards() {
  registerCardType<Debt>();
  registerCardType<Writhe>();
  registerCardType<Beckon>();
  registerPowerType<FeedingFrenzyPower>();
  registerPowerType<HelloWorldPower>();
  registerPowerType<ReboundPower>();
  registerCardType<Abundance>();
  registerCardType<ByrdSwoop>();
  registerCardType<Caltrops>();
  registerCardType<Clash>();
  registerCardType<Distraction>();
  registerCardType<DualWield>();
  registerCardType<Entrench>();
  registerCardType<FeedingFrenzy>();
  registerCardType<HelloWorld>();
  registerCardType<Outmaneuver>();
  registerCardType<Rebound>();
  registerCardType<RipAndTear>();
  registerCardType<Stack>();
  registerCardType<Dowsing>();
  registerCardType<SpoilsMap>();
}

}  // namespace sts
