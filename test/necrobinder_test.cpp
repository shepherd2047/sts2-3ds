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

  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
