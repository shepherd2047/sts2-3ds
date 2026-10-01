// Necrobinder systems checks (X4.0): Osty summon/revive/HP/block/attack/redirect, DoomPower,
// SoulboundPower, NecroMasteryPower, the Soul token.
// Build: make -f Makefile.sdl build/necrobinder_test ; run: ./build/necrobinder_test
#include <cstdio>
#include <cstdlib>

#include "../source/core/char_necrobinder.h"

using namespace sts;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    ++checks;                                                           \
    if (!(cond)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #cond); } \
  } while (0)

static Task<> fightTask(Run* r) { co_await r->fight("NibbitsNormal"); }
static Task<> wrap(Task<> t, bool* done) { co_await t; *done = true; }

static bool pump(const std::function<bool()>& done, int maxFrames = 4000) {
  for (int i = 0; i < maxFrames && !done(); ++i) Scheduler::get().update(0.05);
  return done();
}
static void runTask(Task<> t) {
  bool done = false;
  Scheduler::get().spawn(wrap(std::move(t), &done));
  pump([&] { return done; });
}
static Task<> dealDamage(Creature* target, Dec amount, int props, Creature* dealer) {
  co_await cmd::damage(target, amount, props, dealer, nullptr);
}
// A single-hit cmd::Attack, so relic hooks that key off Hook.AfterAttack (BoneFlute,
// UndyingSigil's ModifyDamageMultiplicative via the resulting damage) see a real attack, unlike
// dealDamage's direct cmd::damage call.
static Task<> doAttack(Combat* c, Creature* attacker, Creature* target, Dec dmg) {
  cmd::Attack a;
  a.damagePerHit = dmg;
  a.attacker = attacker;
  a.single = target;
  co_await a.execute(*c);
}

// The first turn of a fight against two Nibbits (500 HP each), mirroring silent_test.cpp.
// `characterId` picks the character (its starting relics/deck come along); `extraRelics` are
// pushed onto the run before the fight starts, so Run::fight wires their `combat` pointer like
// any other owned relic (mirrors how a relic picked up on the map would arrive at the next fight).
struct Fight {
  std::unique_ptr<Run> r = std::make_unique<Run>();
  Combat* c = nullptr;
  Fight(const std::string& characterId = "Ironclad", std::initializer_list<const char*> extraRelics = {}) {
    r->start(3, characterId);
    for (const char* id : extraRelics) {
      auto rel = db::relic(id);
      rel->run = r.get();
      r->relics.push_back(std::move(rel));
    }
    Scheduler::get().spawn(fightTask(r.get()));
    pump([&] { return r->combat && r->combat->playerPhase && r->combat->actions.waiting(); });
    c = r->combat.get();
    for (Creature* e : c->enemies) e->hp = e->maxHp = 500;
  }
  ~Fight() { Scheduler::get().clear(); }
  Creature* enemy(int i = 0) { return c->enemies[i]; }
  void play(Card* k, Creature* t) {
    PlayerAction a;
    a.kind = PlayerAction::PlayCard;
    a.card = k;
    a.target = t;
    c->energy = 10;
    c->actions.fire(a);
    pump([&] { return c->playerPhase && c->actions.waiting(); });
  }
  void endTurn() {
    PlayerAction a;
    a.kind = PlayerAction::EndTurn;
    int turn = c->turnNumber;
    c->actions.fire(a);
    pump([&] { return c->over || (c->turnNumber > turn && c->playerPhase && c->actions.waiting()); });
  }
  template <class P> void apply(Creature* t, int amount) {
    runTask([](Creature* t, int amount, Creature* by) -> Task<> { co_await applyPower<P>(t, amount, by, nullptr); }(t, amount, c->player));
  }
  // Like play(), but answers a cmd::selectCards prompt raised mid-play (Graveblast, SculptingStrike,
  // Snap: X4.2) by picking c->choice.options[pickIndex], or cancelling (empty pick) if out of range.
  void playPick(Card* k, Creature* t, int pickIndex) {
    PlayerAction a;
    a.kind = PlayerAction::PlayCard;
    a.card = k;
    a.target = t;
    c->energy = 10;
    c->actions.fire(a);
    pump([&] { return (c->playerPhase && c->actions.waiting()) || (c->choice.active && c->choice.result.waiting()); });
    if (c->choice.active && c->choice.result.waiting()) {
      std::vector<Card*> pick;
      if (pickIndex >= 0 && pickIndex < (int)c->choice.options.size()) pick = {c->choice.options[pickIndex]};
      c->choice.result.fire(pick);
      pump([&] { return c->playerPhase && c->actions.waiting(); });
    }
  }
};

int main() {
  db::init();
  {  // Summon: HP, side, ownership, DieForYouPower attached.
    Fight f;
    Creature* osty = nullptr;
    runTask([](Combat* c, Creature** out) -> Task<> { *out = co_await summonOsty(*c, 30); }(f.c, &osty));
    CHECK(osty == f.c->osty);
    CHECK(osty->alive() && osty->hp == 30 && osty->maxHp == 30);
    CHECK(osty->side == Side::Player && osty->petOwner == f.c->player);
    CHECK(osty->get<DieForYouPower>() != nullptr);
    // Summoning again while alive raises max HP instead of replacing Osty.
    runTask([](Combat* c, Creature** out) -> Task<> { *out = co_await summonOsty(*c, 5); }(f.c, &osty));
    CHECK(osty == f.c->osty && osty->hp == 35 && osty->maxHp == 35);
  }
  {  // Osty attacking: it works as a normal dealer.
    Fight f;
    Creature* osty = nullptr;
    runTask([](Combat* c, Creature** out) -> Task<> { *out = co_await summonOsty(*c, 20); }(f.c, &osty));
    Creature* e = f.enemy(0);
    int hp0 = e->hp;
    runTask(dealDamage(e, Dec(6), kMove, osty));
    CHECK(hp0 - e->hp == 6);
  }
  {  // Redirect: a powered hit at the player is absorbed by Osty first; block still comes off
     // the player. Partial absorb (no overflow).
    Fight f;
    Creature* osty = nullptr;
    runTask([](Combat* c, Creature** out) -> Task<> { *out = co_await summonOsty(*c, 5); }(f.c, &osty));
    f.c->player->block = 4;
    int hpBefore = f.c->player->hp;
    runTask(dealDamage(f.c->player, Dec(7), kMove, f.enemy(0)));
    CHECK(f.c->player->block == 0);         // 4 blocked
    CHECK(f.c->player->hp == hpBefore);     // the remaining 3 went to Osty, not the player
    CHECK(osty->hp == 2 && osty->alive());  // 5 - 3
  }
  {  // Redirect with overflow: Osty dies to the hit, the excess spills onto the player.
    Fight f;
    Creature* osty = nullptr;
    runTask([](Combat* c, Creature** out) -> Task<> { *out = co_await summonOsty(*c, 5); }(f.c, &osty));
    f.c->player->block = 0;
    int hpBefore = f.c->player->hp;
    runTask(dealDamage(f.c->player, Dec(12), kMove, f.enemy(0)));
    CHECK(osty->dead());
    CHECK(hpBefore - f.c->player->hp == 7);  // 12 - 5 (Osty's HP) overflow
    // Osty stays in combat (dead), not removed, ready for revival; the power survives its death.
    CHECK(f.c->osty == osty && !osty->removed);
    CHECK(osty->get<DieForYouPower>() != nullptr);
    // An unpowered/unblockable hit is never redirected: it lands on the original target.
    Fight g;
    Creature* osty2 = nullptr;
    runTask([](Combat* c, Creature** out) -> Task<> { *out = co_await summonOsty(*c, 5); }(g.c, &osty2));
    int gHpBefore = g.c->player->hp;
    runTask(dealDamage(g.c->player, Dec(4), kUnblockable | kUnpowered, nullptr));
    CHECK(gHpBefore - g.c->player->hp == 4);
    CHECK(osty2->hp == 5);  // untouched
  }
  {  // Revival: after death, summoning again brings Osty back at the new HP.
    Fight f;
    Creature* osty = nullptr;
    runTask([](Combat* c, Creature** out) -> Task<> { *out = co_await summonOsty(*c, 5); }(f.c, &osty));
    runTask(dealDamage(osty, Dec(99), kUnblockable | kUnpowered, nullptr));
    CHECK(osty->dead());
    runTask([](Combat* c, Creature** out) -> Task<> { *out = co_await summonOsty(*c, 15); }(f.c, &osty));
    CHECK(osty->alive() && osty->hp == 15 && osty->maxHp == 15);
    CHECK(f.c->osty == osty);  // the same Creature instance, not a new one
  }
  {  // Doom: kills its owner at the end of the side's turn once CurrentHp <= Amount.
    Fight f;  // an enemy doomed: dies at the end of the enemy's own turn (BeforeSideTurnEnd)
    Creature* e = f.enemy(0);
    e->hp = 5;
    f.apply<DoomPower>(e, 5);
    CHECK(!e->dead());
    f.endTurn();
    CHECK(e->dead());
    CHECK(f.enemy(1)->alive());  // only the doomed one dies
  }
  {
    Fight f;  // the player doomed: dies at the end of their own turn (AfterSideTurnEnd)
    f.c->player->hp = 5;
    f.apply<DoomPower>(f.c->player, 5);
    f.endTurn();
    CHECK(f.c->player->dead());
    CHECK(f.c->over && !f.c->won);
  }
  {  // Soul: 0 cost, Exhaust, draws Cards (2, +1 upgraded).
    Fight f;
    Card* soul = nullptr;
    runTask([](Combat* c, Card** out) -> Task<> { *out = co_await cmd::addGeneratedCard(*c, db::card("Soul"), Pile::Hand); }(f.c, &soul));
    CHECK(soul->id == "Soul" && soul->cost == 0 && soul->has(kwExhaust) && soul->target == TargetType::Self);
    size_t handBefore = f.c->hand.size();
    f.play(soul, nullptr);
    CHECK(f.c->pileOf(soul) == Pile::Exhaust);
    CHECK(f.c->hand.size() == handBefore - 1 /*played*/ + 2 /*drawn*/);
    soul->upgrade();
    CHECK(soul->val("Cards").toInt() == 3);
    // Soul.CreateInHand: several Souls joining the hand directly.
    std::vector<Card*> made;
    runTask([](Combat* c, int n, std::vector<Card*>* out) -> Task<> { *out = co_await createSoulsInHand(*c, n); }(f.c, 2, &made));
    CHECK(made.size() == 2 && made[0]->id == "Soul" && made[1]->id == "Soul");
    CHECK(f.c->pileOf(made[0]) == Pile::Hand && f.c->pileOf(made[1]) == Pile::Hand);
  }
  {  // SoulboundPower: any of its applier's non-Soul, non-Status/Curse cards entering combat
     // also adds Amount Souls to the draw pile.
    Fight f;
    f.apply<SoulboundPower>(f.c->player, 2);
    size_t drawBefore = f.c->draw.size();
    runTask([](Combat* c) -> Task<> { co_await cmd::addGeneratedCard(*c, db::card("StrikeIronclad"), Pile::Discard); }(f.c));
    CHECK(f.c->draw.size() == drawBefore + 2);
    int souls = 0;
    for (Card* k : f.c->draw) if (k->id == "Soul") ++souls;
    CHECK(souls == 2);
    // Adding a Soul itself must not recurse.
    size_t drawBefore2 = f.c->draw.size();
    runTask([](Combat* c) -> Task<> { co_await addSoulToDrawPileRandom(*c); }(f.c));
    CHECK(f.c->draw.size() == drawBefore2 + 1);  // exactly one more, no chain reaction
  }
  {  // NecroMasteryPower: Osty losing HP deals Amount * loss Unblockable/Unpowered damage to
     // every hittable enemy.
    Fight f;
    f.apply<NecroMasteryPower>(f.c->player, 2);
    Creature* osty = nullptr;
    runTask([](Combat* c, Creature** out) -> Task<> { *out = co_await summonOsty(*c, 10); }(f.c, &osty));
    int hp0 = f.enemy(0)->hp, hp1 = f.enemy(1)->hp;
    runTask(dealDamage(osty, Dec(4), kUnblockable | kUnpowered, nullptr));
    CHECK(osty->hp == 6);
    CHECK(hp0 - f.enemy(0)->hp == 8 && hp1 - f.enemy(1)->hp == 8);  // 4 lost * Amount 2
  }

  // ---------------------------------------------------------------- X4.1: starter deck, starting
  // relic, the 8 character relics, the 3 potions.

  {  // Character plumbing: starter deck, starting relic, now fully registered.
    CHECK(db::characterPlayable("Necrobinder"));
    const Character& ch = db::character("Necrobinder");
    CHECK((ch.starterDeck == std::vector<std::string>{"StrikeNecrobinder", "StrikeNecrobinder", "StrikeNecrobinder",
                                                        "StrikeNecrobinder", "DefendNecrobinder", "DefendNecrobinder",
                                                        "DefendNecrobinder", "DefendNecrobinder", "Bodyguard", "Unleash"}));
    CHECK((ch.startingRelics == std::vector<std::string>{"BoundPhylactery"}));
  }
  {  // BoundPhylactery: summons Osty at 1 HP before combat starts, then (AfterEnergyResetLate)
     // raises his max HP by 1 every turn after the first.
    Fight f("Necrobinder");
    CHECK(f.r->relics.size() == 1 && f.r->relics[0]->id == "BoundPhylactery");
    CHECK(f.c->osty && f.c->osty->hp == 1 && f.c->osty->maxHp == 1);
    // Shield Osty from the Nibbits' attacks (DieForYouPower redirects them onto him) so only
    // BoundPhylactery's own growth shows up in his HP.
    f.c->player->block = 999;
    f.endTurn();  // turn 1 -> 2
    CHECK(f.c->osty->hp == 2 && f.c->osty->maxHp == 2);
    f.c->player->block = 999;
    f.endTurn();  // -> 3
    CHECK(f.c->osty->hp == 3 && f.c->osty->maxHp == 3);
  }
  {  // StrikeNecrobinder / DefendNecrobinder: same numbers as the Ironclad's Strike/Defend.
    Fight f("Necrobinder");
    runTask([](Combat* c) -> Task<> { co_await cmd::drawCards(*c, 10); }(f.c));  // whole 10-card deck into hand
    Card *strike = nullptr, *defend = nullptr;
    for (Card* k : f.c->hand) {
      if (k->id == "StrikeNecrobinder") strike = k;
      if (k->id == "DefendNecrobinder") defend = k;
    }
    CHECK(strike && (strike->tags & tagStrike) && strike->val("Damage").toInt() == 6);
    CHECK(defend && (defend->tags & tagDefend) && defend->val("Block").toInt() == 5);
    int hp0 = f.enemy(0)->hp;
    f.play(strike, f.enemy(0));
    CHECK(hp0 - f.enemy(0)->hp == 6);
    f.play(defend, nullptr);
    CHECK(f.c->player->block == 5);
  }
  {  // Bodyguard summons/grows Osty; Unleash deals CalculationBase + Osty's current HP, dealt by
     // Osty (not the player); a missing/dead Osty makes the attack a no-op.
    Fight f("Necrobinder");
    runTask([](Combat* c) -> Task<> { co_await cmd::drawCards(*c, 10); }(f.c));  // whole 10-card deck into hand
    Card *bodyguard = nullptr, *unleash = nullptr;
    for (Card* k : f.c->hand) {
      if (k->id == "Bodyguard") bodyguard = k;
      if (k->id == "Unleash") unleash = k;
    }
    CHECK(bodyguard && unleash);
    CHECK(f.c->osty->hp == 1);
    f.play(bodyguard, nullptr);  // Summon 5, Osty already alive -> +5 max/current HP
    CHECK(f.c->osty->hp == 6 && f.c->osty->maxHp == 6);
    int hp0 = f.enemy(0)->hp;
    f.play(unleash, f.enemy(0));  // 6 (CalculationBase) + 1 (ExtraDamage) * 6 (Osty's HP)
    CHECK(hp0 - f.enemy(0)->hp == 12);
    // A dead attacker (Osty) makes cmd::Attack a no-op, exactly what Unleash's onPlay relies on
    // in place of porting `Osty.CheckMissingWithAnim`.
    runTask(dealDamage(f.c->osty, Dec(999), kUnblockable | kUnpowered, nullptr));
    CHECK(f.c->osty->dead());
    int hp1 = f.enemy(0)->hp;
    runTask(doAttack(f.c, f.c->osty, f.enemy(0), Dec(50)));
    CHECK(hp1 == f.enemy(0)->hp);
  }
  {  // BoneFlute: gain 2 block whenever Osty (and only Osty) lands an attack.
    Fight f("Necrobinder", {"BoneFlute"});
    CHECK(f.r->relics.size() == 2);
    f.c->player->block = 0;
    runTask(doAttack(f.c, f.c->osty, f.enemy(0), Dec(3)));
    CHECK(f.c->player->block == 2);
    f.c->player->block = 0;
    runTask(doAttack(f.c, f.c->player, f.enemy(0), Dec(3)));
    CHECK(f.c->player->block == 0);
  }
  {  // BookRepairKnife: heal 3 per non-owner creature killed by the same doomKill batch.
    Fight f("Necrobinder", {"BookRepairKnife"});
    f.c->player->hp = 50;  // leave room to observe the heal (max HP is 66)
    int hpBefore = f.c->player->hp;
    runTask([](Combat* c) -> Task<> { co_await doomKill(*c, {c->enemies[0], c->enemies[1]}); }(f.c));
    CHECK(f.enemy(0)->dead() && f.enemy(1)->dead());
    CHECK(f.c->player->hp - hpBefore == 6);  // 3 * 2
  }
  {  // Bookmark: after the hand flushes, a retained non-X positive-cost card's cost drops by 1
     // until played.
    Fight f("Necrobinder", {"Bookmark"});
    for (Card* k : f.c->hand) k->singleTurnRetain = true;  // retain the whole hand this turn
    f.endTurn();
    bool anyReduced = false;
    for (Card* k : f.c->hand) if (k->costWithLocalMods() < k->cost) anyReduced = true;
    CHECK(anyReduced);
  }
  {  // FuneraryMask: on turn 1, before the hand draw, add 3 Soul cards to the draw pile.
    Fight f("Necrobinder", {"FuneraryMask"});
    int souls = 0;
    for (Card* k : f.c->draw) if (k->id == "Soul") ++souls;
    for (Card* k : f.c->hand) if (k->id == "Soul") ++souls;  // may have been drawn into turn 1
    CHECK(souls == 3);
  }
  {  // IvoryTile: playing a card costing 3+ energy refunds 1 energy.
    Fight f("Necrobinder", {"IvoryTile"});
    Card* card = f.c->addCard(db::card("Bludgeon"));  // any registered cost-3 card will do
    f.c->hand.push_back(card);
    f.play(card, f.enemy(0));  // Fight::play sets energy to 10 before playing
    CHECK(f.c->energy == 10 - 3 + 1);
  }
  {  // UndyingSigil: a powered attack from a creature doomed to die this turn (CurrentHp <= its
     // own Doom amount) does half damage to the owner; a non-doomed attacker is unaffected.
    Fight f("Necrobinder", {"UndyingSigil"});
    // BoundPhylactery's Osty is alive by default and would otherwise absorb hits meant for the
    // player (DieForYouPower); kill him so the attacks below land on the player directly.
    runTask(dealDamage(f.c->osty, Dec(999), kUnblockable | kUnpowered, nullptr));
    f.c->player->block = 0;
    f.enemy(0)->hp = 3;
    f.apply<DoomPower>(f.enemy(0), 5);  // 3 <= 5: doomed
    int hpBefore = f.c->player->hp;
    runTask(doAttack(f.c, f.enemy(0), f.c->player, Dec(10)));
    CHECK(hpBefore - f.c->player->hp == 5);  // half of 10
    int hpBefore2 = f.c->player->hp;
    runTask(doAttack(f.c, f.enemy(1), f.c->player, Dec(10)));  // not doomed: full damage
    CHECK(hpBefore2 - f.c->player->hp == 10);
  }
  {  // Potions: PotionOfDoom (33 Doom on a target), PotOfGhouls (2 Souls to hand), BoneBrew
     // (summon/grow Osty by 15).
    Fight f("Necrobinder");
    Creature* e = f.enemy(0);
    auto doom = db::potion("PotionOfDoom");
    doom->run = f.r.get();
    doom->combat = f.c;
    runTask([](Potion* p, Creature* t) -> Task<> { co_await p->onUse(t); }(doom.get(), e));
    CHECK(e->get<DoomPower>() && e->powerAmount<DoomPower>() == 33);

    size_t handBefore = f.c->hand.size();
    auto ghouls = db::potion("PotOfGhouls");
    ghouls->run = f.r.get();
    ghouls->combat = f.c;
    runTask([](Potion* p) -> Task<> { co_await p->onUse(nullptr); }(ghouls.get()));
    CHECK(f.c->hand.size() == handBefore + 2);

    int ostyHpBefore = f.c->osty->hp;
    auto brew = db::potion("BoneBrew");
    brew->run = f.r.get();
    brew->combat = f.c;
    runTask([](Potion* p) -> Task<> { co_await p->onUse(nullptr); }(brew.get()));
    CHECK(f.c->osty->hp == ostyHpBefore + 15);
  }

  // ---------------------------------------------------------------- X4.2: the Common card pool
  // (char_necrobinder_cards.cpp). Every Necrobinder fight already has Osty alive at 1 HP
  // (BoundPhylactery), so tests that don't care about him leave it as-is.

  {  // BlightStrike: Strike, deals Damage, then applies Doom equal to the damage actually dealt.
    Fight f("Necrobinder");
    Card* card = f.c->addCard(db::card("BlightStrike"));
    f.c->hand.push_back(card);
    CHECK((card->tags & tagStrike) && card->val("Damage").toInt() == 8);
    Creature* e = f.enemy(0);
    int hp0 = e->hp;
    f.play(card, e);
    CHECK(hp0 - e->hp == 8);
    CHECK(e->get<DoomPower>() && e->powerAmount<DoomPower>() == 8);
    card->upgrade();
    CHECK(card->val("Damage").toInt() == 10);
  }
  {  // Defy: Ethereal, Block + Weak on the target.
    Fight f("Necrobinder");
    Card* card = f.c->addCard(db::card("Defy"));
    f.c->hand.push_back(card);
    CHECK(card->has(kwEthereal));
    f.c->player->block = 0;
    f.play(card, f.enemy(0));
    CHECK(f.c->player->block == 6);
    CHECK(f.enemy(0)->get<WeakPower>() && f.enemy(0)->powerAmount<WeakPower>() == 1);
  }
  {  // DrainPower: damage, then upgrades Cards (2) random upgradable cards from the discard pile.
    Fight f("Necrobinder");
    Card* d1 = f.c->addCard(db::card("Defile"));
    Card* d2 = f.c->addCard(db::card("Reap"));
    f.c->discard.push_back(d1);
    f.c->discard.push_back(d2);
    Card* card = f.c->addCard(db::card("DrainPower"));
    f.c->hand.push_back(card);
    int hp0 = f.enemy(0)->hp;
    f.play(card, f.enemy(0));
    CHECK(hp0 - f.enemy(0)->hp == 10);
    CHECK(d1->upgraded() && d2->upgraded());  // only 2 candidates, Cards == 2: both taken
  }
  {  // Fear: Ethereal, damage + Vulnerable.
    Fight f("Necrobinder");
    Card* card = f.c->addCard(db::card("Fear"));
    f.c->hand.push_back(card);
    int hp0 = f.enemy(0)->hp;
    f.play(card, f.enemy(0));
    CHECK(hp0 - f.enemy(0)->hp == 7);
    CHECK(f.enemy(0)->get<VulnerablePower>() && f.enemy(0)->powerAmount<VulnerablePower>() == 1);
  }
  {  // Flatten: OstyAttack; costs 0 for the rest of the turn once Osty has landed an attack this
     // turn (from any source, not just Flatten itself), reset at the next turn.
    Fight f("Necrobinder");
    Card* card = f.c->addCard(db::card("Flatten"));
    f.c->hand.push_back(card);
    CHECK(card->costWithLocalMods() == 2);
    runTask(doAttack(f.c, f.c->osty, f.enemy(0), Dec(3)));
    CHECK(card->costWithLocalMods() == 0);
    f.endTurn();
    CHECK(card->costWithLocalMods() == 2);
  }
  {  // GraveWarden: Block + a Soul into a random spot in the draw pile.
    Fight f("Necrobinder");
    Card* card = f.c->addCard(db::card("GraveWarden"));
    f.c->hand.push_back(card);
    f.c->player->block = 0;
    size_t drawBefore = f.c->draw.size();
    f.play(card, nullptr);
    CHECK(f.c->player->block == 8);
    int souls = 0;
    for (Card* k : f.c->draw) if (k->id == "Soul") ++souls;
    CHECK(f.c->draw.size() == drawBefore + 1 && souls == 1);
  }
  {  // Graveblast: Exhaust, damage, then look at the discard pile and add a card to hand.
    Fight f("Necrobinder");
    Card* other = f.c->addCard(db::card("Reap"));
    f.c->discard.push_back(other);
    Card* card = f.c->addCard(db::card("Graveblast"));
    f.c->hand.push_back(card);
    CHECK(card->has(kwExhaust));
    int hp0 = f.enemy(0)->hp;
    f.playPick(card, f.enemy(0), 0);
    CHECK(hp0 - f.enemy(0)->hp == 4);
    CHECK(f.c->pileOf(card) == Pile::Exhaust);
    CHECK(f.c->pileOf(other) == Pile::Hand);
  }
  {  // Invoke: next turn, grows Osty's max HP by Summon and grants Energy energy.
    Fight f("Necrobinder");
    f.c->player->block = 999;  // keep the Nibbits off Osty so only Invoke/Phylactery growth shows
    Card* card = f.c->addCard(db::card("Invoke"));
    f.c->hand.push_back(card);
    int ostyHp0 = f.c->osty->hp;
    f.play(card, nullptr);
    CHECK(f.c->osty->hp == ostyHp0);  // not yet -- next turn
    f.endTurn();
    // BoundPhylactery also grows Osty by 1 every turn after the first: +1 (Phylactery) + 2 (Invoke).
    CHECK(f.c->osty->hp == ostyHp0 + 3);
    CHECK(f.c->energy == f.c->maxEnergyNow() + 2);
  }
  {  // NegativePulse: Block + Doom on every hittable enemy.
    Fight f("Necrobinder");
    Card* card = f.c->addCard(db::card("NegativePulse"));
    f.c->hand.push_back(card);
    f.c->player->block = 0;
    f.play(card, nullptr);
    CHECK(f.c->player->block == 5);
    CHECK(f.enemy(0)->get<DoomPower>() && f.enemy(0)->powerAmount<DoomPower>() == 7);
    CHECK(f.enemy(1)->get<DoomPower>() && f.enemy(1)->powerAmount<DoomPower>() == 7);
  }
  {  // Poke: 0 cost OstyAttack; a dead Osty makes it a no-op (like Unleash).
    Fight f("Necrobinder");
    Card* card = f.c->addCard(db::card("Poke"));
    f.c->hand.push_back(card);
    CHECK(card->cost == 0 && (card->tags & tagOstyAttack));
    int hp0 = f.enemy(0)->hp;
    f.play(card, f.enemy(0));
    CHECK(hp0 - f.enemy(0)->hp == 6);
    runTask(dealDamage(f.c->osty, Dec(999), kUnblockable | kUnpowered, nullptr));
    CHECK(f.c->osty->dead());
    Card* card2 = f.c->addCard(db::card("Poke"));
    f.c->hand.push_back(card2);
    int hp1 = f.enemy(0)->hp;
    f.play(card2, f.enemy(0));
    CHECK(hp1 == f.enemy(0)->hp);
  }
  {  // PullAggro: grows Osty's max HP by Summon, gains Block.
    Fight f("Necrobinder");
    Card* card = f.c->addCard(db::card("PullAggro"));
    f.c->hand.push_back(card);
    f.c->player->block = 0;
    int ostyHp0 = f.c->osty->hp;
    f.play(card, nullptr);
    CHECK(f.c->osty->hp == ostyHp0 + 4);
    CHECK(f.c->player->block == 7);
  }
  {  // Reap: Retain, plain high damage.
    Fight f("Necrobinder");
    Card* card = f.c->addCard(db::card("Reap"));
    f.c->hand.push_back(card);
    CHECK(card->has(kwRetain));
    int hp0 = f.enemy(0)->hp;
    f.play(card, f.enemy(0));
    CHECK(hp0 - f.enemy(0)->hp == 27);
  }
  {  // Reave: damage, then a Soul into the draw pile -- pre-upgraded if Reave itself is upgraded.
    Fight f("Necrobinder");
    Card* card = f.c->addCard(db::card("Reave"));
    card->upgrade();
    f.c->hand.push_back(card);
    int hp0 = f.enemy(0)->hp;
    f.play(card, f.enemy(0));
    CHECK(hp0 - f.enemy(0)->hp == 13);  // 10 + 3 upgraded
    Card* soul = nullptr;
    for (Card* k : f.c->draw) if (k->id == "Soul") soul = k;
    CHECK(soul && soul->upgraded());
  }
  {  // Scourge: Doom on the target, draw Cards cards.
    Fight f("Necrobinder");
    Card* card = f.c->addCard(db::card("Scourge"));
    f.c->hand.push_back(card);
    size_t handBefore = f.c->hand.size();
    f.play(card, f.enemy(0));
    CHECK(f.enemy(0)->get<DoomPower>() && f.enemy(0)->powerAmount<DoomPower>() == 13);
    CHECK(f.c->hand.size() == handBefore - 1 /*played*/ + 1 /*drawn*/);
  }
  {  // SculptingStrike: Strike, damage, then Ethereal on a card in hand that lacked it.
    Fight f("Necrobinder");
    Card* card = f.c->addCard(db::card("SculptingStrike"));
    f.c->hand.push_back(card);
    CHECK(card->tags & tagStrike);
    int etherealBefore = 0;
    for (Card* k : f.c->hand) if (k->has(kwEthereal)) ++etherealBefore;
    int hp0 = f.enemy(0)->hp;
    f.playPick(card, f.enemy(0), 0);
    CHECK(hp0 - f.enemy(0)->hp == 9);
    int etherealAfter = 0;
    for (Card* k : f.c->hand) if (k->has(kwEthereal)) ++etherealAfter;
    CHECK(etherealAfter == etherealBefore + 1);
  }
  {  // Snap: OstyAttack, damage, then Retain on a card in hand that lacked it.
    Fight f("Necrobinder");
    Card* card = f.c->addCard(db::card("Snap"));
    f.c->hand.push_back(card);
    CHECK(card->tags & tagOstyAttack);
    int retainBefore = 0;
    for (Card* k : f.c->hand) if (k->has(kwRetain)) ++retainBefore;
    int hp0 = f.enemy(0)->hp;
    f.playPick(card, f.enemy(0), 0);
    CHECK(hp0 - f.enemy(0)->hp == 7);
    int retainAfter = 0;
    for (Card* k : f.c->hand) if (k->has(kwRetain)) ++retainAfter;
    CHECK(retainAfter == retainBefore + 1);
  }
  {  // Sow: Retain, damage to all enemies.
    Fight f("Necrobinder");
    Card* card = f.c->addCard(db::card("Sow"));
    f.c->hand.push_back(card);
    CHECK(card->has(kwRetain));
    int hp0 = f.enemy(0)->hp, hp1 = f.enemy(1)->hp;
    f.play(card, nullptr);
    CHECK(hp0 - f.enemy(0)->hp == 8 && hp1 - f.enemy(1)->hp == 8);
  }
  {  // Wisp: 0 cost Exhaust, gain Energy; upgrading adds Retain instead of changing the numbers.
    Fight f("Necrobinder");
    Card* card = f.c->addCard(db::card("Wisp"));
    f.c->hand.push_back(card);
    CHECK(card->cost == 0 && card->has(kwExhaust));
    f.play(card, nullptr);  // Fight::play sets energy to 10 before playing (see IvoryTile's test)
    CHECK(f.c->energy == 10 - 0 + 1);
    card->upgrade();
    CHECK(card->has(kwRetain));
  }
  {  // Afterlife: Exhaust, grows Osty's max HP by Summon (or raises it further, as summonOsty does).
    Fight f("Necrobinder");
    Card* card = f.c->addCard(db::card("Afterlife"));
    f.c->hand.push_back(card);
    CHECK(card->has(kwExhaust));
    int ostyHp0 = f.c->osty->hp;
    f.play(card, nullptr);
    CHECK(f.c->osty->hp == ostyHp0 + 6);
  }

  // ---- X4.3a: Uncommon cards, first half
  {  // BoneShards: Osty hits all enemies, gain Block, then Osty dies; with no Osty nothing happens.
    Fight f("Necrobinder");
    Card* card = f.c->addCard(db::card("BoneShards"));
    f.c->hand.push_back(card);
    int hp0 = f.enemy(0)->hp, hp1 = f.enemy(1)->hp, b0 = f.c->player->block;
    f.play(card, nullptr);
    CHECK(hp0 - f.enemy(0)->hp == 9 && hp1 - f.enemy(1)->hp == 9);
    CHECK(f.c->player->block - b0 == 9);
    CHECK(f.c->osty->dead());
    Card* again = f.c->addCard(db::card("BoneShards"));
    f.c->hand.push_back(again);
    b0 = f.c->player->block;
    f.play(again, nullptr);
    CHECK(f.c->player->block == b0 && f.enemy(0)->hp == hp0 - 9);
  }
  {  // BorrowedTime: +4 energy, every card costs 1 more; gone after the turn.
    Fight f("Necrobinder");
    Card* card = f.c->addCard(db::card("BorrowedTime"));
    Card* other = f.c->addCard(db::card("Bury"));
    f.c->hand.push_back(card);
    f.c->hand.push_back(other);
    CHECK(f.c->energyCost(other) == 4);
    f.play(card, nullptr);
    CHECK(f.c->energy == 10 - 1 + 4);
    CHECK(f.c->energyCost(other) == 5);
    f.endTurn();
    CHECK(f.c->player->power("BorrowedTimePower") == nullptr);
  }
  {  // Bury / Calcify (+Poke through Osty) / Countdown.
    Fight f("Necrobinder");
    Card* bury = f.c->addCard(db::card("Bury"));
    f.c->hand.push_back(bury);
    int hp0 = f.enemy(0)->hp;
    f.play(bury, f.enemy(0));
    CHECK(hp0 - f.enemy(0)->hp == 52);
    Card* calcify = f.c->addCard(db::card("Calcify"));
    f.c->hand.push_back(calcify);
    f.play(calcify, nullptr);
    Card* poke = f.c->addCard(db::card("Poke"));
    f.c->hand.push_back(poke);
    hp0 = f.enemy(0)->hp;
    f.play(poke, f.enemy(0));
    CHECK(hp0 - f.enemy(0)->hp == 6 + 4);
    Card* cd = f.c->addCard(db::card("Countdown"));
    f.c->hand.push_back(cd);
    f.play(cd, nullptr);
    CHECK(f.c->player->power("CountdownPower")->amount == 6);
    f.endTurn();
    int doom = 0;
    for (Creature* e : f.c->enemies)
      if (Power* d = e->power("DoomPower")) doom += d->amount;
    CHECK(doom == 6);
  }
  {  // Deathbringer: Doom then Weak on all; DeathsDoor: triple Block once Doom was applied this turn.
    Fight f("Necrobinder");
    Card* door = f.c->addCard(db::card("DeathsDoor"));
    f.c->hand.push_back(door);
    int b0 = f.c->player->block;
    f.play(door, nullptr);
    CHECK(f.c->player->block - b0 == 6);
    Card* door2 = f.c->addCard(db::card("DeathsDoor"));
    f.c->hand.push_back(door2);
    Card* db_ = f.c->addCard(db::card("Deathbringer"));
    f.c->hand.push_back(db_);
    f.play(db_, nullptr);
    for (Creature* e : f.c->enemies)
      CHECK(e->power("DoomPower")->amount == 21 && e->power("WeakPower")->amount == 1);
    b0 = f.c->player->block;
    f.play(door2, nullptr);
    CHECK(f.c->player->block - b0 == 18);
  }
  {  // Debilitate: doubles Vulnerable (6 -> 12 instead of 9) and Weak on the owner.
    Fight f("Necrobinder");
    Card* card = f.c->addCard(db::card("Debilitate"));
    f.c->hand.push_back(card);
    f.play(card, f.enemy(0));
    CHECK(f.enemy(0)->power("DebilitatePower")->amount == 2);
    f.apply<VulnerablePower>(f.enemy(0), 1);
    Card* strike = f.c->addCard(db::card("StrikeNecrobinder"));
    f.c->hand.push_back(strike);
    int hp0 = f.enemy(0)->hp;
    f.play(strike, f.enemy(0));
    CHECK(hp0 - f.enemy(0)->hp == 12);
    hp0 = f.enemy(1)->hp;
    f.apply<VulnerablePower>(f.enemy(1), 1);
    Card* strike2 = f.c->addCard(db::card("StrikeNecrobinder"));
    f.c->hand.push_back(strike2);
    f.play(strike2, f.enemy(1));
    CHECK(hp0 - f.enemy(1)->hp == 9);
  }
  {  // Delay / Friendship / EnfeeblingTouch.
    Fight f("Necrobinder");
    Card* delay = f.c->addCard(db::card("Delay"));
    f.c->hand.push_back(delay);
    int b0 = f.c->player->block;
    f.play(delay, nullptr);
    CHECK(f.c->player->block - b0 == 11 && f.c->player->power("EnergyNextTurnPower")->amount == 1);
    Card* fr = f.c->addCard(db::card("Friendship"));
    f.c->hand.push_back(fr);
    int max0 = f.c->maxEnergyNow();
    f.play(fr, nullptr);
    CHECK(f.c->maxEnergyNow() == max0 + 1);
    CHECK(f.c->player->power("StrengthPower")->amount == -2);
    Card* et = f.c->addCard(db::card("EnfeeblingTouch"));
    f.c->hand.push_back(et);
    CHECK(et->has(kwEthereal));
    f.play(et, f.enemy(0));
    CHECK(f.enemy(0)->power("StrengthPower")->amount == -8);
    f.endTurn();
    CHECK(f.enemy(0)->power("StrengthPower") == nullptr || f.enemy(0)->power("StrengthPower")->amount == 0);
  }
  {  // Fetch: Osty hit, draws only on the first play each turn. DeathMarch: +4 per non-hand draw.
    Fight f("Necrobinder");
    Card* fetch = f.c->addCard(db::card("Fetch"));
    f.c->hand.push_back(fetch);
    Card* dm = f.c->addCard(db::card("DeathMarch"));
    f.c->hand.push_back(dm);
    int hp0 = f.enemy(0)->hp;
    size_t hand0 = f.c->hand.size();
    f.play(fetch, f.enemy(0));
    CHECK(hp0 - f.enemy(0)->hp == 3);
    CHECK(f.c->hand.size() == hand0 - 1 + 1);
    hp0 = f.enemy(0)->hp;
    runTask([](Combat* c) -> Task<> { co_await cmd::drawCards(*c, Dec(2)); }(f.c));
    f.play(dm, f.enemy(0));
    CHECK(hp0 - f.enemy(0)->hp == 8 + 4 * 3);  // Fetch's draw plus the two above
  }
  {  // Cleanse (summon + exhaust from draw pile), Dirge (X), CaptureSpirit, Dredge.
    Fight f("Necrobinder");
    Card* cl = f.c->addCard(db::card("Cleanse"));
    f.c->hand.push_back(cl);
    size_t draw0 = f.c->draw.size(), ex0 = f.c->exhaust.size();
    int max0 = f.c->osty->maxHp;
    f.playPick(cl, nullptr, 0);
    CHECK(f.c->draw.size() == draw0 - 1 && f.c->exhaust.size() == ex0 + 1);
    CHECK(f.c->osty->maxHp == max0 + 3);
    Card* dirge = f.c->addCard(db::card("Dirge"));
    f.c->hand.push_back(dirge);
    dirge->xValue = 0;
    draw0 = f.c->draw.size();
    max0 = f.c->osty->maxHp;
    f.play(dirge, nullptr);  // Fight::play gives 10 energy: X = 10
    CHECK(f.c->osty->maxHp == max0 + 3 * 10 && f.c->draw.size() == draw0 + 10);
    Card* cs = f.c->addCard(db::card("CaptureSpirit"));
    f.c->hand.push_back(cs);
    draw0 = f.c->draw.size();
    int hp0 = f.enemy(0)->hp;
    f.play(cs, f.enemy(0));
    CHECK(hp0 - f.enemy(0)->hp == 3 && f.c->draw.size() == draw0 + 3);
    Card* dr = f.c->addCard(db::card("Dredge"));
    f.c->hand.push_back(dr);
    f.c->discard.clear();
    Card* d1 = f.c->addCard(db::card("Poke"));
    f.c->discard.push_back(d1);
    f.playPick(dr, nullptr, 0);
    CHECK(f.c->discard.empty() && std::find(f.c->hand.begin(), f.c->hand.end(), d1) != f.c->hand.end());
  }

  // ---- X4.3b: Uncommon cards, second half
  {  // Haunt: playing a Soul damages a random enemy. Shroud: applying Doom grants Block.
    Fight f("Necrobinder");
    Card* h = f.c->addCard(db::card("Haunt"));
    Card* sh = f.c->addCard(db::card("Shroud"));
    f.c->hand.push_back(h);
    f.c->hand.push_back(sh);
    f.play(h, nullptr);
    f.play(sh, nullptr);
    Card* soul = f.c->addCard(db::card("Soul"));
    f.c->hand.push_back(soul);
    int total0 = f.enemy(0)->hp + f.enemy(1)->hp;
    f.play(soul, nullptr);
    CHECK(total0 - (f.enemy(0)->hp + f.enemy(1)->hp) == 7);
    int blk0 = f.c->player->block;
    f.apply<DoomPower>(f.enemy(0), 5);
    CHECK(f.c->player->block == blk0 + 3);
  }
  {  // NoEscape: 10 Doom, +5 per 10 Doom already on the target.
    Fight f("Necrobinder");
    f.apply<DoomPower>(f.enemy(0), 25);
    Card* ne = f.c->addCard(db::card("NoEscape"));
    f.c->hand.push_back(ne);
    f.play(ne, f.enemy(0));
    CHECK(f.enemy(0)->powerAmount<DoomPower>() == 25 + 10 + 5 * 2);
  }
  {  // E4: target-aware hand preview (Calculate(null) = base, Calculate(target) with the hovered enemy)
    Fight f("Necrobinder");
    f.apply<DoomPower>(f.enemy(0), 25);
    Card* ne = f.c->addCard(db::card("NoEscape"));
    Card* tu = f.c->addCard(db::card("TimesUp"));
    CHECK(ne->calculatedBlock().toInt() == 10 && tu->calculatedDamage().toInt() == 0);
    ne->previewTarget = tu->previewTarget = f.enemy(0);
    CHECK(ne->calculatedBlock().toInt() == 20 && tu->calculatedDamage().toInt() == 25);
  }
  {  // Lethality: +50% on the first Attack of the turn only. Veilpiercer: Ethereal cards cost 0.
    Fight f("Necrobinder");
    Card* l = f.c->addCard(db::card("Lethality"));
    f.c->hand.push_back(l);
    f.play(l, nullptr);
    Card* a1 = f.c->addCard(db::card("Bury"));
    Card* a2 = f.c->addCard(db::card("Bury"));
    f.c->hand.push_back(a1);
    f.c->hand.push_back(a2);
    int hp0 = f.enemy(0)->hp;
    f.play(a1, f.enemy(0));
    CHECK(hp0 - f.enemy(0)->hp == 78);
    hp0 = f.enemy(0)->hp;
    f.play(a2, f.enemy(0));
    CHECK(hp0 - f.enemy(0)->hp == 52);
    Card* v = f.c->addCard(db::card("Veilpiercer"));
    Card* par = f.c->addCard(db::card("Parse"));
    f.c->hand.push_back(v);
    f.c->hand.push_back(par);
    CHECK(f.c->energyCost(par) == 1);
    f.play(v, f.enemy(0));
    CHECK(f.c->energyCost(par) == 0);
  }
  {  // PullFromBelow: one hit per Ethereal play.
    Fight f("Necrobinder");
    Card* pf = f.c->addCard(db::card("PullFromBelow"));
    f.c->hand.push_back(pf);
    Card* p1 = f.c->addCard(db::card("Parse"));
    Card* p2 = f.c->addCard(db::card("Parse"));
    f.c->hand.push_back(p1);
    f.c->hand.push_back(p2);
    f.play(p1, nullptr);
    f.play(p2, nullptr);
    int hp0 = f.enemy(0)->hp;
    f.play(pf, f.enemy(0));
    CHECK(hp0 - f.enemy(0)->hp == 5 * 2);
  }
  {  // Rattle: hits 1 + Osty attacks this turn. HighFive needs Osty. Putrefy, Severance, Spur.
    Fight f("Necrobinder");
    Card* ra = f.c->addCard(db::card("Rattle"));
    f.c->hand.push_back(ra);
    Card* fe = f.c->addCard(db::card("Fetch"));
    f.c->hand.push_back(fe);
    f.play(fe, f.enemy(0));
    int hp0 = f.enemy(0)->hp;
    f.play(ra, f.enemy(0));
    CHECK(hp0 - f.enemy(0)->hp == 7 * 2);
    Card* hf = f.c->addCard(db::card("HighFive"));
    f.c->hand.push_back(hf);
    CHECK(f.c->canPlay(hf));
    hp0 = f.enemy(1)->hp;
    f.play(hf, nullptr);
    CHECK(hp0 - f.enemy(1)->hp == 11 && f.enemy(1)->power("VulnerablePower")->amount == 2);
    Card* pu = f.c->addCard(db::card("Putrefy"));
    f.c->hand.push_back(pu);
    f.play(pu, f.enemy(0));
    CHECK(f.enemy(0)->power("WeakPower")->amount == 2 && f.enemy(0)->power("VulnerablePower")->amount == 4);
    Card* sv = f.c->addCard(db::card("Severance"));
    f.c->hand.push_back(sv);
    size_t draw0 = f.c->draw.size(), disc0 = f.c->discard.size();
    f.play(sv, f.enemy(0));
    CHECK(f.c->draw.size() == draw0 + 1 && f.c->discard.size() == disc0 + 2);  // Severance itself + a Soul
    Card* sp = f.c->addCard(db::card("Spur"));
    f.c->hand.push_back(sp);
    f.c->osty->hp = 1;
    int max0 = f.c->osty->maxHp;
    f.play(sp, nullptr);
    CHECK(f.c->osty->maxHp == max0 + 3 && f.c->osty->hp == std::min(f.c->osty->maxHp, 1 + 3 + 5));
    f.c->osty->hp = 0;
    CHECK(!f.c->canPlay(hf));
  }
  {  // RightHandHand returns from the discard pile after a card that spent 2+ energy.
    Fight f("Necrobinder");
    Card* rh = f.c->addCard(db::card("RightHandHand"));
    f.c->hand.push_back(rh);
    f.play(rh, f.enemy(0));
    CHECK(std::find(f.c->discard.begin(), f.c->discard.end(), rh) != f.c->discard.end());
    Card* big = f.c->addCard(db::card("Bury"));
    f.c->hand.push_back(big);
    f.play(big, f.enemy(0));
    CHECK(std::find(f.c->hand.begin(), f.c->hand.end(), rh) != f.c->hand.end());
  }
  {  // SicEm: the applier's Osty hits summon; SleightOfFlesh: debuffs applied to enemies hurt them.
    Fight f("Necrobinder");
    Card* se = f.c->addCard(db::card("SicEm"));
    f.c->hand.push_back(se);
    f.play(se, f.enemy(0));
    CHECK(f.enemy(0)->power("SicEmPower") && f.enemy(0)->power("SicEmPower")->amount == 3);
    int max0 = f.c->osty->maxHp;
    Card* fe = f.c->addCard(db::card("Fetch"));
    f.c->hand.push_back(fe);
    f.play(fe, f.enemy(0));
    CHECK(f.c->osty->maxHp == max0 + 3);
    Card* sf = f.c->addCard(db::card("SleightOfFlesh"));
    f.c->hand.push_back(sf);
    f.play(sf, nullptr);
    int hp0 = f.enemy(1)->hp;
    Card* pu = f.c->addCard(db::card("Putrefy"));
    f.c->hand.push_back(pu);
    f.play(pu, f.enemy(1));
    CHECK(hp0 - f.enemy(1)->hp == 9 * 2);
  }
  {  // Melancholy gets cheaper when an enemy dies; Pagestorm draws when an Ethereal card is drawn.
    Fight f("Necrobinder");
    Card* me = f.c->addCard(db::card("Melancholy"));
    f.c->hand.push_back(me);
    CHECK(f.c->energyCost(me) == 3);
    runTask([](Creature* e) -> Task<> { co_await cmd::kill({e}); }(f.enemy(1)));
    CHECK(f.c->energyCost(me) == 2);
    Card* pg = f.c->addCard(db::card("Pagestorm"));
    f.c->hand.push_back(pg);
    f.play(pg, nullptr);
    Card* et = f.c->addCard(db::card("Parse"));
    f.c->draw.insert(f.c->draw.begin(), et);
    size_t hand0 = f.c->hand.size();
    runTask([](Combat* c) -> Task<> { co_await cmd::drawCards(*c, Dec(1)); }(f.c));
    CHECK(f.c->hand.size() == hand0 + 2);
  }

  {  // X4.4 rares. Hang: 10, then Hang 2; second Hang hits for 10 * 2 and doubles to 4.
    Fight f("Necrobinder");
    auto give = [&](const char* id) { Card* k = f.c->addCard(db::card(id)); f.c->hand.push_back(k); return k; };
    int hp0 = f.enemy(0)->hp;
    f.play(give("Hang"), f.enemy(0));
    CHECK(hp0 - f.enemy(0)->hp == 10 && f.enemy(0)->power("HangPower")->amount == 2);
    hp0 = f.enemy(0)->hp;
    f.play(give("Hang"), f.enemy(0));
    CHECK(hp0 - f.enemy(0)->hp == 20 && f.enemy(0)->power("HangPower")->amount == 4);
    // Oblivion: a card played afterwards gives the enemy Doom equal to the amount (not itself).
    f.play(give("Oblivion"), f.enemy(1));
    CHECK(f.enemy(1)->power("OblivionPower")->amount == 3 && f.enemy(1)->power("DoomPower") == nullptr);
    f.play(give("Undeath"), nullptr);
    CHECK(f.enemy(1)->power("DoomPower") && f.enemy(1)->power("DoomPower")->amount == 3);
    // TimesUp: 1 damage per Doom. Misery copies the Doom (a debuff) onto the other enemy.
    hp0 = f.enemy(1)->hp;
    f.play(give("TimesUp"), f.enemy(1));
    CHECK(hp0 - f.enemy(1)->hp == 3);
    int hp1 = f.enemy(1)->hp;
    f.play(give("Misery"), f.enemy(0));
    CHECK(hp1 == f.enemy(1)->hp);
    CHECK(f.enemy(1)->power("HangPower") && f.enemy(1)->power("HangPower")->amount == 4);
    // Undeath: block and a copy in the discard pile. Sacrifice: 3 x Osty max HP block, Osty dies.
    size_t disc0 = f.c->discard.size();
    int b0 = f.c->player->block;
    f.play(give("Undeath"), nullptr);
    CHECK(f.c->player->block - b0 == 7 && f.c->discard.size() == disc0 + 2);
    int maxHp = f.c->osty->maxHp;
    b0 = f.c->player->block;
    f.play(give("Sacrifice"), nullptr);
    CHECK(f.c->player->block - b0 == maxHp * 3 && f.c->osty->dead());
  }
  {  // EndOfDays: Doom then kill all; SharedFate; SoulStorm counts exhausted Souls; Seance transforms.
    Fight f("Necrobinder");
    auto give = [&](const char* id) { Card* k = f.c->addCard(db::card(id)); f.c->hand.push_back(k); return k; };
    f.play(give("SharedFate"), f.enemy(0));
    CHECK(f.c->player->powerAmount<StrengthPower>() == -2 && f.enemy(0)->powerAmount<StrengthPower>() == -2);
    f.c->exhaust.push_back(f.c->addCard(db::card("Soul")));
    f.c->exhaust.push_back(f.c->addCard(db::card("Soul")));
    int hp0 = f.enemy(0)->hp;
    f.play(give("SoulStorm"), f.enemy(0));
    CHECK(hp0 - f.enemy(0)->hp == 9 + 4 * 2 - 2);  // minus the -2 Strength from SharedFate
    size_t draw0 = f.c->draw.size();
    Card* top = f.c->draw[0];
    f.playPick(give("Seance"), nullptr, 0);
    CHECK(f.c->draw.size() == draw0 && f.c->draw[0] != top && f.c->draw[0]->id == "Soul");
    for (Creature* e : f.c->enemies) e->hp = e->maxHp = 20;
    f.play(give("EndOfDays"), nullptr);
    CHECK(f.c->over || (f.enemy(0)->dead() && f.enemy(1)->dead()));
  }
  {  // TheScythe grows (and its deck version), Squeeze, Eradicate (X hits), ReaperForm doom, Demesne.
    Fight f("Necrobinder");
    auto give = [&](const char* id) { Card* k = f.c->addCard(db::card(id)); f.c->hand.push_back(k); return k; };
    Card* s = give("TheScythe");
    int hp0 = f.enemy(0)->hp;
    f.play(s, f.enemy(0));
    CHECK(hp0 - f.enemy(0)->hp == 13 && s->val("Damage") == Dec(18));
    hp0 = f.enemy(0)->hp;
    f.play(give("Eradicate"), f.enemy(0));  // 10 energy -> 10 hits of 11
    CHECK(hp0 - f.enemy(0)->hp == 11 * 10);
    f.play(give("ReaperForm"), nullptr);
    hp0 = f.enemy(0)->hp;
    f.play(give("Hang"), f.enemy(0));
    CHECK(f.enemy(0)->power("DoomPower") && f.enemy(0)->power("DoomPower")->amount == 10);
    Card* sq = give("Squeeze");
    give("SweepingGaze");
    hp0 = f.enemy(0)->hp;
    int osty = f.c->osty->hp;
    (void)osty;
    f.play(sq, f.enemy(0));
    CHECK(hp0 - f.enemy(0)->hp >= 30);  // 25 + 5 per other Osty attack in piles (+ Doom from ReaperForm applies later)
    f.play(give("Demesne"), nullptr);
    f.endTurn();
    CHECK(f.c->maxEnergyNow() == 4 && f.c->hand.size() >= 6);
  }
  {  // BansheesCry gets cheaper by 2 per Ethereal play; DevourLife summons on Soul; Transfigure.
    Fight f("Necrobinder");
    auto give = [&](const char* id) { Card* k = f.c->addCard(db::card(id)); f.c->hand.push_back(k); return k; };
    Card* b = give("BansheesCry");
    CHECK(f.c->energyCost(b) == 9);
    Card* se = give("Seance");
    f.playPick(se, nullptr, 0);
    CHECK(f.c->energyCost(b) == 7);
    f.play(give("DevourLife"), nullptr);
    int max0 = f.c->osty->maxHp;
    f.play(give("Soul"), nullptr);
    CHECK(f.c->osty->maxHp == max0 + 1);
    Card* tf = give("Transfigure");
    Card* victim = give("StrikeNecrobinder");
    f.playPick(tf, nullptr, (int)(std::find(f.c->hand.begin(), f.c->hand.end(), victim) - f.c->hand.begin()) - 1);  // Transfigure itself has left the hand
    CHECK(f.c->energyCost(victim) == 2 && victim->baseReplayCount == 1);
  }
  printf("%d checks, %d failed\n", checks, failures);
  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
