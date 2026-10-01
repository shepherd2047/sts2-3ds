// A1a: colorless cards 1/3 (ColorlessCardPool order): Alchemize .. GoldAxe, translated from
// Models.Cards\<Name>.cs and Models.Powers\<Name>.cs. The multiplayer-only cards of this range
// (BeaconOfHope, BelieveInYou, Coordinate, GangUp) are skipped, as for the character pools; they stay in
// the pool list (colorless_pool.cpp) so the pool order is the game's. Pool helpers: colorless.h.
#include "cards.h"
#include "colorless.h"

namespace sts {

namespace {

constexpr int kMaxHandCards = 10;  // CardPile.MaxCardsInHand

// CardModel.CompareTo (model id, then upgrade level) + UnstableShuffle: List.StableShuffle(rng).
void stableShuffleCards(std::vector<Card*>& v, Rng& rng) {
  std::stable_sort(v.begin(), v.end(), [](Card* a, Card* b) {
    return a->id != b->id ? a->id < b->id : a->upgradeLevel < b->upgradeLevel;
  });
  rng.shuffle(v);
}

// ================================================================ powers

// AutomationPower.cs: Instanced (one per Automation played, each with its own 10-card counter). Every 10
// cards drawn, gain Amount energy. The HUD shows cardsLeft.
struct AutomationPower : Power {
  POWER_HEADER(AutomationPower, "AUTOMATION_POWER")
  PowerInstanceType instanceType() const override { return PowerInstanceType::Instanced; }
  int displayAmount() const override { return cardsLeft; }
  int cardsLeft = 10;
  Task<> afterCardDrawn(Card* card, bool) override {
    if (ownerOf(card) != owner) co_return;
    --cardsLeft;
    if (cardsLeft <= 0) {
      flash = 1.f;
      co_await cmd::gainEnergy(*owner->combat, amount);
      cardsLeft = 10;
    }
  }
};

// CalamityPower.cs: after each Attack you play, add Amount random Attacks from your pool to the hand.
struct CalamityPower : Power {
  POWER_HEADER(CalamityPower, "CALAMITY_POWER")
  std::vector<Card*> played;  // amountsForPlayedCards
  Task<> beforeCardPlayed(const CardPlay& p) override {
    if (ownerOf(p.card) != owner || p.card->type != CardType::Attack) co_return;
    played.push_back(p.card);
  }
  Task<> afterCardPlayed(const CardPlay& p) override {
    auto it = std::find(played.begin(), played.end(), p.card);
    if (it == played.end()) co_return;
    played.erase(it);
    Combat& c = *owner->combat;
    auto made = randomForCombat(c, db::characterPool(c.run->characterId, [](const Card& k) { return k.type == CardType::Attack; }), amount);
    for (auto& k : made) co_await cmd::addGeneratedCard(c, std::move(k), Pile::Hand);
  }
};

// DarkShacklesPower.cs: TemporaryStrengthPower with IsPositive == false (see DyingStarPower).
struct DarkShacklesPower : Power {
  POWER_HEADER(DarkShacklesPower, "DARK_SHACKLES_POWER")
  const char* internallyAppliedPower() const override { return "StrengthPower"; }  // ITemporaryPower
  PowerType type() const override { return PowerType::Debuff; }
  Task<> beforeApplied(Creature* target, Dec amt, Creature* app, Card* src) override {
    co_await applyPower<StrengthPower>(target, -amt, app, src, true);
  }
  Task<> afterPowerAmountChanged(Power* p, Dec amt, Creature* app, Card* src) override {
    if (!(amt == Dec(amount)) && p == this) co_await applyPower<StrengthPower>(owner, -amt, app, src, true);
  }
  Task<> afterSideTurnEnd(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) {
      flash = 1.f;
      Creature* o = owner;
      int a = amount;
      co_await cmd::removePower(this);
      co_await applyPower<StrengthPower>(o, a, o, nullptr);
    }
  }
};

// EntropyPower.cs: at the start of your turn, transform Amount cards in the hand into random cards.
struct EntropyPower : Power {
  POWER_HEADER(EntropyPower, "ENTROPY_POWER")
  // CardFactory.CreateRandomCardForTransform(original, isInCombat: true, CombatCardSelection): the
  // original's pool (its character's, else the colorless one), Common..Rare, not the same card, generable
  // in combat, then rng.NextItem. PORT NOTE: cards of no pool (Status / Curse) are left alone (the C# has
  // no valid options and throws).
  static std::unique_ptr<Card> randomFor(Combat& c, Card* original) {
    std::vector<std::string> pool;
    auto& chars = db::character(c.run->characterId).cardPool;
    bool inCharacter = std::find(chars.begin(), chars.end(), original->id) != chars.end();
    auto keep = [&](const Card& k) {
      return k.id != original->id && (k.rarity == Rarity::Common || k.rarity == Rarity::Uncommon || k.rarity == Rarity::Rare) &&
             k.canBeGeneratedInCombat();
    };
    if (inCharacter) pool = db::characterCards(c.run->characterId, keep);
    else if (original->rarity != Rarity::Status && original->rarity != Rarity::Curse) pool = db::colorlessCards(keep);
    if (pool.empty()) return nullptr;
    return db::card(c.rng("CombatCardSelection").nextItem(pool));
  }
  Task<> afterPlayerTurnStart() override {
    Combat& c = *owner->combat;
    std::vector<Card*> options;
    for (Card* k : c.hand) if (k->isTransformable()) options.push_back(k);
    int n = std::min(amount, (int)options.size());
    if (n <= 0) co_return;
    auto picked = co_await cmd::selectCards(c, "card_selection.TO_TRANSFORM", options, n, n);
    for (Card* k : picked) {
      auto into = randomFor(c, k);
      if (into) co_await cmd::transform(c, k, std::move(into));
    }
  }
};

// FastenPower.cs: Defend cards gain Amount additional block.
struct FastenPower : Power {
  POWER_HEADER(FastenPower, "FASTEN_POWER")
  Dec modifyBlockAdditive(Creature* target, Dec, int props, Card* src) override {
    if (owner != target) return 0;
    if (!isPoweredBlock(props)) return 0;
    if (src && !(src->tags & tagDefend)) return 0;
    return amount;
  }
};

// ================================================================ cards

// Alchemize.cs: 1 cost, Skill, Self, Exhaust, Rare. Procure a random potion generated for combat.
struct Alchemize : IroncladT<Alchemize> {
  CARD_HEADER(Alchemize, "ALCHEMIZE", 1, Skill, Rare, Self)
    keywords = kwExhaust;
  }
  bool canBeGeneratedInCombat() const override { return false; }
  Task<> onPlay(CardPlay&) override {
    Run* r = combat->run;
    r->procurePotion(r->randomPotion(r->rng("CombatPotionGeneration"), true));  // PotionCmd.TryToProcure
    co_return;
  }
  void onUpgrade() override { cost -= 1; }
};

// Anointed.cs: 1 cost, Skill, Self, Exhaust, Rare. Move Rare cards from the draw pile to the hand
// (random, up to the free hand slots).
struct Anointed : IroncladT<Anointed> {
  CARD_HEADER(Anointed, "ANOINTED", 1, Skill, Rare, Self)
    keywords = kwExhaust;
  }
  Task<> onPlay(CardPlay&) override {
    int count = kMaxHandCards - (int)combat->hand.size();
    std::vector<Card*> rares;
    for (Card* k : combat->draw) if (k->rarity == Rarity::Rare) rares.push_back(k);
    combat->rng("CombatCardSelection").shuffle(rares);  // TakeRandom
    for (int i = 0; i < (int)rares.size() && i < count; ++i) co_await cmd::moveCard(*combat, rares[(size_t)i], Pile::Hand);
  }
  void onUpgrade() override { keywords |= kwRetain; }
};

// Automation.cs: 1 cost, Power, Self, Uncommon. AutomationPower (Energy 1).
struct Automation : IroncladT<Automation> {
  CARD_HEADER(Automation, "AUTOMATION", 1, Power, Uncommon, Self)
    addVar("Energy", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<AutomationPower>(me(), val("Energy"), me(), this); }
  void onUpgrade() override { cost -= 1; }
};

// BeatDown.cs: 3 cost, Skill, RandomEnemy, Rare. Auto-play Cards (3) random playable Attacks from the
// discard pile.
struct BeatDown : IroncladT<BeatDown> {
  CARD_HEADER(BeatDown, "BEAT_DOWN", 3, Skill, Rare, RandomEnemy)
    addVar("Cards", 3);
  }
  Task<> onPlay(CardPlay&) override {
    std::vector<Card*> pick;
    for (Card* k : combat->discard) if (k->type == CardType::Attack && !k->has(kwUnplayable)) pick.push_back(k);
    stableShuffleCards(pick, combat->rng("Shuffle"));
    if ((int)pick.size() > val("Cards").toInt()) pick.resize((size_t)val("Cards").toInt());
    for (Card* item : pick) {
      if (combat->over || combat->ending) break;  // CombatManager.IsOverOrEnding
      Creature* t = nullptr;
      if (item->target == TargetType::AnyEnemy) t = combat->rng("CombatTargets").nextItem(combat->hittableEnemies());
      co_await cmd::autoPlay(*combat, item, t);
    }
  }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

// Bolas.cs: 0 cost, Attack, AnyEnemy, Rare. Damage 3 (+1). If it was played last player turn, it returns to
// the hand before the next hand draw.
struct Bolas : IroncladT<Bolas> {
  CARD_HEADER(Bolas, "BOLAS", 0, Attack, Rare, AnyEnemy)
    addVar("Damage", 3);
  }
  // CardPlaysFinished entry of this card: round it happened in. `playedBy` guards against a copy
  // (clone()) inheriting the original's history.
  const Card* playedBy = nullptr;
  int playedRound = -100;
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    playedBy = this;
    playedRound = combat->roundNumber;
  }
  Task<> beforeHandDraw() override {
    if (playedBy == this && playedRound == combat->roundNumber - 1 && combat->pileOf(this) != Pile::Hand)
      co_await cmd::moveCard(*combat, this, Pile::Hand);
  }
  void onUpgrade() override { upgradeVar("Damage", 1); }
};

// Calamity.cs: 3 cost, Power, Self, Rare. CalamityPower (1).
struct Calamity : IroncladT<Calamity> {
  CARD_HEADER(Calamity, "CALAMITY", 3, Power, Rare, Self)
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<CalamityPower>(me(), 1, me(), this); }
  void onUpgrade() override { cost -= 1; }
};

// Catastrophe.cs: 2 cost, Skill, Self, Uncommon. Cards (2) times: auto-play a random playable card from
// the draw pile (any card when none is playable).
struct Catastrophe : IroncladT<Catastrophe> {
  CARD_HEADER(Catastrophe, "CATASTROPHE", 2, Skill, Uncommon, Self)
    addVar("Cards", 2);
  }
  Task<> onPlay(CardPlay&) override {
    int n = val("Cards").toInt();
    for (int i = 0; i < n; ++i) {
      std::vector<Card*> playable;
      for (Card* k : combat->draw) if (!k->has(kwUnplayable)) playable.push_back(k);
      stableShuffleCards(playable, combat->rng("Shuffle"));
      Card* pick = playable.empty() ? nullptr : playable[0];
      if (!pick) {
        std::vector<Card*> all = combat->draw;
        stableShuffleCards(all, combat->rng("Shuffle"));
        pick = all.empty() ? nullptr : all[0];
      }
      if (pick) co_await cmd::autoPlay(*combat, pick, nullptr);
    }
  }
  void onUpgrade() override { upgradeVar("Cards", 1); }
};

// DarkShackles.cs: 0 cost, Skill, AnyEnemy, Exhaust, Uncommon. DarkShacklesPower StrengthLoss 9 (+6).
struct DarkShackles : IroncladT<DarkShackles> {
  CARD_HEADER(DarkShackles, "DARK_SHACKLES", 0, Skill, Uncommon, AnyEnemy)
    keywords = kwExhaust;
    addVar("StrengthLoss", 9);
  }
  Task<> onPlay(CardPlay& p) override { co_await applyPower<DarkShacklesPower>(p.target, val("StrengthLoss"), me(), this); }
  void onUpgrade() override { upgradeVar("StrengthLoss", 6); }
};

// Discovery.cs: 1 cost, Skill, Self, Exhaust, Uncommon. Choose 1 of 3 distinct cards of your pool
// (skippable); it is free this turn and goes to the hand. Upgrade: no Exhaust.
struct Discovery : IroncladT<Discovery> {
  CARD_HEADER(Discovery, "DISCOVERY", 1, Skill, Uncommon, Self)
    keywords = kwExhaust;
  }
  Task<> onPlay(CardPlay&) override {
    auto ids = db::characterCards(combat->run->characterId, [](const Card&) { return true; });
    co_await chooseGeneratedToHand(*combat, distinctForCombat(*combat, std::move(ids), 3), true);
  }
  void onUpgrade() override { keywords &= ~kwExhaust; }
};

// DramaticEntrance.cs: 0 cost, Attack, AllEnemies, Exhaust + Innate, Uncommon. Damage 11 (+4).
struct DramaticEntrance : IroncladT<DramaticEntrance> {
  CARD_HEADER(DramaticEntrance, "DRAMATIC_ENTRANCE", 0, Attack, Uncommon, AllEnemies)
    keywords = kwExhaust | kwInnate;
    addVar("Damage", 11);
  }
  Task<> onPlay(CardPlay&) override { co_await attackAll(val("Damage")); }
  void onUpgrade() override { upgradeVar("Damage", 4); }
};

// Entropy.cs: 1 cost, Power, Self, Rare. EntropyPower (Cards 1). Upgrade: Innate.
struct Entropy : IroncladT<Entropy> {
  CARD_HEADER(Entropy, "ENTROPY", 1, Power, Rare, Self)
    addVar("Cards", 1);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<EntropyPower>(me(), val("Cards"), me(), this); }
  void onUpgrade() override { keywords |= kwInnate; }
};

// Equilibrium.cs: 2 cost, Skill, Self, Uncommon. Block 13 (+3), then RetainHandPower (1).
struct Equilibrium : IroncladT<Equilibrium> {
  CARD_HEADER(Equilibrium, "EQUILIBRIUM", 2, Skill, Uncommon, Self)
    addVar("Block", 13);
    addVar("Equilibrium", 1);
  }
  bool gainsBlock() const override { return true; }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    co_await applyPower<RetainHandPower>(me(), val("Equilibrium"), me(), this);
  }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

// EternalArmor.cs: 3 cost, Power, Self, Rare. PlatingPower 9 (+3).
struct EternalArmor : IroncladT<EternalArmor> {
  CARD_HEADER(EternalArmor, "ETERNAL_ARMOR", 3, Power, Rare, Self)
    addVar("PlatingPower", 9);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<PlatingPower>(me(), val("PlatingPower"), me(), this); }
  void onUpgrade() override { upgradeVar("PlatingPower", 3); }
};

// Fasten.cs: 1 cost, Power, Self, Uncommon. FastenPower ExtraBlock 4 (+2).
struct Fasten : IroncladT<Fasten> {
  CARD_HEADER(Fasten, "FASTEN", 1, Power, Uncommon, Self)
    addVar("ExtraBlock", 4);
  }
  Task<> onPlay(CardPlay&) override { co_await applyPower<FastenPower>(me(), val("ExtraBlock"), me(), this); }
  void onUpgrade() override { upgradeVar("ExtraBlock", 2); }
};

// Finesse.cs: 0 cost, Skill, Self, Uncommon. Block 4 (+3), draw 1.
struct Finesse : IroncladT<Finesse> {
  CARD_HEADER(Finesse, "FINESSE", 0, Skill, Uncommon, Self)
    addVar("Block", 4);
    addVar("Cards", 1);
  }
  bool gainsBlock() const override { return true; }
  Task<> onPlay(CardPlay&) override {
    co_await block(val("Block"));
    co_await drawCards(val("Cards"));
  }
  void onUpgrade() override { upgradeVar("Block", 3); }
};

// Fisticuffs.cs: 1 cost, Attack, AnyEnemy, Uncommon. Damage 7 (+2), then gain Block equal to the damage
// dealt (unblocked + blocked + overkill).
struct Fisticuffs : IroncladT<Fisticuffs> {
  CARD_HEADER(Fisticuffs, "FISTICUFFS", 1, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 7);
  }
  bool gainsBlock() const override { return true; }
  Task<> onPlay(CardPlay& p) override {
    cmd::Attack a;
    a.damagePerHit = val("Damage");
    a.attacker = me();
    a.source = this;
    a.single = p.target;
    co_await a.execute(*combat);
    int total = 0;
    for (auto& hit : a.results)
      for (auto& r : hit) total += r.blocked + r.unblocked + r.overkill;
    co_await block(Dec(total));
  }
  void onUpgrade() override { upgradeVar("Damage", 2); }
};

// FlashOfSteel.cs: 0 cost, Attack, AnyEnemy, Uncommon. Damage 5 (+3), draw 1.
struct FlashOfSteel : IroncladT<FlashOfSteel> {
  CARD_HEADER(FlashOfSteel, "FLASH_OF_STEEL", 0, Attack, Uncommon, AnyEnemy)
    addVar("Damage", 5);
    addVar("Cards", 1);
  }
  Task<> onPlay(CardPlay& p) override {
    co_await attack(p.target, val("Damage"));
    co_await drawCards(val("Cards"));
  }
  void onUpgrade() override { upgradeVar("Damage", 3); }
};

// GoldAxe.cs: 1 cost, Attack, AnyEnemy, Rare. Damage = 1 per card played this combat (CardPlaysFinished
// count). Upgrade: Retain.
struct GoldAxe : IroncladT<GoldAxe> {
  CARD_HEADER(GoldAxe, "GOLD_AXE", 1, Attack, Rare, AnyEnemy)
    addVar("CalculationBase", 0);
    addVar("ExtraDamage", 1);
    addVar("CalculatedDamage", 0);
    calcMultiplier = [](Card* c) { return c->combat ? c->combat->cardPlaysFinishedThisCombat : 0; };
  }
  Task<> onPlay(CardPlay& p) override { co_await attackCalculated(p.target); }
  void onUpgrade() override { keywords |= kwRetain; }
};

}  // namespace

void registerColorlessPool();  // colorless_pool.cpp (ColorlessPotion)

void registerColorlessA() {
  registerColorlessPool();
  registerPowerType<AutomationPower>();
  registerPowerType<CalamityPower>();
  registerPowerType<DarkShacklesPower>();
  registerPowerType<EntropyPower>();
  registerPowerType<FastenPower>();

  registerCardType<Alchemize>();
  registerCardType<Anointed>();
  registerCardType<Automation>();
  // BeaconOfHope: multiplayer only.
  registerCardType<BeatDown>();
  // BelieveInYou: multiplayer only.
  registerCardType<Bolas>();
  registerCardType<Calamity>();
  registerCardType<Catastrophe>();
  // Coordinate: multiplayer only.
  registerCardType<DarkShackles>();
  registerCardType<Discovery>();
  registerCardType<DramaticEntrance>();
  registerCardType<Entropy>();
  registerCardType<Equilibrium>();
  registerCardType<EternalArmor>();
  registerCardType<Fasten>();
  registerCardType<Finesse>();
  registerCardType<Fisticuffs>();
  registerCardType<FlashOfSteel>();
  // GangUp: multiplayer only.
  registerCardType<GoldAxe>();
}

}  // namespace sts
