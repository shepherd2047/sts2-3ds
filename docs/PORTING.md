# Porting StS2 C# models to this engine

Source of truth: the decompiled game in `../sts2-decompiled/`
(`MegaCrit.Sts2.Core.Models.Cards/<Name>.cs`, `...Models.Powers/<Name>.cs`).
Translate behaviour and numbers exactly (use the non-ascension value where
`AscensionHelper.GetValueIfAscension(level, ascended, normal)` appears: pick
`normal`, the last argument). Drop VFX/SFX/animation/UI code (`VfxCmd`,
`SfxCmd`, `CreatureCmd.TriggerAnim`, `NCombatRoom`, `ThinkCmd`, `Cmd.Wait`,
`WithHitFx`, `CardCmd.Preview*`, `OnEnqueuePlayVfx`, `HoverTips`).

Read first: `source/core/game.h` (engine API), `source/core/powers.h`
(existing powers), `source/core/cards.h` (card base), and the existing
translations in `source/core/content.cpp` — copy their style.

## Engine API cheat sheet

| C# | C++ |
|---|---|
| `class X : CardModel` | `struct X : IroncladT<X> { CARD_HEADER(X, "X_SNAKE", cost, Attack/Skill/Power, Common/Uncommon/Rare/Ancient?, AnyEnemy/Self/AllEnemies/RandomEnemy/None) ... }` (see content.cpp). `Rarity` has Basic, Common, Uncommon, Rare, Status, Curse — use `Rare` for Ancient. The loc key is the class name in UPPER_SNAKE_CASE. |
| `CanonicalVars` `new DamageVar(9m, ...)` | `addVar("Damage", 9);` Var names: Damage, Block, Cards, Repeat, HpLoss, Energy, Heal, MaxHp, Magic..., and `PowerVar<XPower>(n)` → `addVar("XPower", n)` (e.g. "VulnerablePower", "StrengthPower"). Plain `new DynamicVar("Name", v)` → `addVar("Name", v)`. |
| `CalculationBaseVar`, `ExtraDamageVar`, `CalculatedDamageVar(...).WithMultiplier(f)` | `addVar("CalculationBase", b); addVar("ExtraDamage", e); addVar("CalculatedDamage", 0); calcMultiplier = [](Card* c) { ... };` then `attackCalculated(target)` (see BodySlam/PerfectedStrike). |
| `CalculationExtraVar`, `CalculatedBlockVar` | `addVar("CalculationBase", b); addVar("CalculationExtra", e); addVar("CalculatedBlock", 0); calcMultiplier = ...;` then `co_await block(calculatedBlock());` |
| `CanonicalKeywords` Exhaust / Innate / Ethereal / Retain / Unplayable | `keywords = kwExhaust | kwInnate;` |
| `CanonicalTags` Strike | `tags = tagStrike;` |
| `HasEnergyCostX => true` | `costsX = true;` and `ResolveEnergyXValue()` → `xValue` |
| `base.DynamicVars.Damage.BaseValue` | `val("Damage")` (a `Dec`; `.toInt()` for int) |
| `DynamicVars.X.UpgradeValueBy(n)` | `upgradeVar("X", n);` |
| `EnergyCost.UpgradeBy(-1)` | `cost -= 1;` |
| `EnergyCost.AddThisCombat(n)` / `AddThisTurn` / `SetThisTurnOrUntilPlayed` ... | same names on Card: `addThisCombat(n)`, `addThisTurn(n)`, `setThisTurnOrUntilPlayed(c)` ... |
| `IsUpgraded` | `upgraded()` |
| `OnPlay(ctx, cardPlay)` | `Task<> onPlay(CardPlay& p) override` — `p.target` |
| `OnUpgrade()` | `void onUpgrade() override` |
| `DamageCmd.Attack(d).FromCard(this, cardPlay).Targeting(t).Execute()` | `co_await attack(p.target, d);` `.WithHitCount(n)` → `attack(t, d, n)`; `TargetingAllOpponents` → `attackAll(d, n)`; `TargetingRandomOpponents` → `attackRandom(d, n)`. For access to results use `cmd::Attack` directly (see IroncladCard::attack in cards.h; `a.results` holds DamageResults per hit). |
| `CreatureCmd.GainBlock(Owner.Creature, BlockVar, cardPlay)` | `co_await block(val("Block"));` |
| `CreatureCmd.GainBlock(creature, n, ValueProp.Unpowered, null)` | `co_await cmd::gainBlock(creature, n, kUnpowered, nullptr);` |
| `CreatureCmd.Damage(ctx, target, n, props, dealer/card...)` | `co_await cmd::damage(target, n, props, dealer, cardOrNull);` props: `kUnblockable|kUnpowered|kMove`. Self HP loss from a card: `co_await loseHp(n);` |
| `CreatureCmd.Heal(c, n)` / `GainMaxHp` / `LoseMaxHp` | `cmd::heal(c, n)`, `cmd::gainMaxHp(c, n)`, `cmd::loseMaxHp(c, n)` |
| `PowerCmd.Apply<P>(ctx, target, n, applier, card)` | `co_await applyPower<P>(target, n, applier, this);` (targets list → loop) |
| `PowerCmd.Decrement(p)` / `Remove(p)` / `TickDownDuration(p)` / `ModifyAmount(ctx, p, d, ...)` | `cmd::decrement(p)` / `cmd::removePower(p)` / `cmd::tickDownDuration(p)` / `cmd::modifyPowerAmount(p, d, applier, card)` |
| `creature.GetPower<P>()` / `GetPowerAmount<P>()` / `HasPower<P>()` | `c->get<P>()` / `c->powerAmount<P>()` / `c->get<P>() != nullptr` |
| `CardPileCmd.Draw(ctx, n, player)` | `co_await drawCards(n);` |
| `CardCmd.Exhaust(ctx, card)` | `co_await cmd::exhaustCard(*combat, card);` |
| `CardCmd.Upgrade(card)` | `cmd::upgradeCard(card);` |
| `CardCmd.AutoPlay(ctx, card, target)` | `co_await cmd::autoPlay(*combat, card, target);` |
| `CardCmd.Transform(card, newCard)` | `co_await cmd::transform(*combat, card, db::card("Id"));` |
| `CardPileCmd.AddGeneratedCardToCombat(card, PileType.Hand, owner)` | `co_await cmd::addGeneratedCard(*combat, std::move(uniquePtrCard), Pile::Hand);` A copy of this card: `clone()`; a new one by id: `db::card("Id")`. |
| `CardPileCmd.Add(card, PileType.X)` | `co_await cmd::moveCard(*combat, card, Pile::X);` |
| `CardPileCmd.AutoPlayFromDrawPile(...)` | `co_await cmd::autoPlayFromDrawPile(*combat, n, forceExhaust);` |
| `CardSelectCmd.FromHand/FromCombatPile(...)` | `auto picked = co_await cmd::selectCards(*combat, "LOC_KEY_OF_THIS_CARD", options, min, max);` |
| `PlayerCmd.GainEnergy(n, player)` | `co_await cmd::gainEnergy(*combat, n);` |
| `PileType.Hand.GetPile(Owner).Cards` | `combat->hand` (also `draw`, `discard`, `exhaust`); all cards: `combat->allCards()` |
| `CombatState.HittableEnemies` | `combat->hittableEnemies()` |
| `Owner.Creature` | `me()` (in cards) / `owner` (in powers) |
| `RunState.Rng.CombatCardSelection` / `CombatTargets` / `Shuffle` | `combat->rng("CombatCardSelection")` etc.; `.nextItem(vec)`, `.shuffle(vec)`, `.nextInt(n)` |
| random card from the Ironclad pool | `db::ironcladCards([](const Card& c) { return c.type == CardType::Attack; })` then pick with an rng |
| `card.Type`, `Tags.Contains(CardTag.Strike)` | `c->type == CardType::Attack`, `c->tags & tagStrike` |
| `card.Owner.Creature == Owner` (in a power) | `ownerOf(card) == owner` |

### Powers

```cpp
struct DemonFormPower : Power {
  POWER_HEADER(DemonFormPower, "DEMON_FORM_POWER")   // loc key = UPPER_SNAKE of class name
  // PowerType: override type() (default Buff); StackType Single: override stackType();
  // AllowNegative: override allowNegative(); private Data class: plain member fields.
  Task<> afterSideTurnStart(Side, const std::vector<Creature*>& participants) override {
    if (contains(participants, owner)) co_await applyPower<StrengthPower>(owner, amount, owner, nullptr);
  }
};
```

Hook names (virtual methods on `Model` in game.h): modifyDamageAdditive,
modifyDamageMultiplicative, modifyBlockAdditive, modifyBlockMultiplicative,
modifyHpLostAfterOsty, modifyHandDraw, modifyMaxEnergy, modifyEnergyCost,
modifyEnergyCostLate (TryModifyEnergyCostInCombat[Late]: return the new cost),
modifyCardPlayCount, afterModifyingCardPlayCount, modifyCardPlayResultLocation,
shouldDraw, shouldClearBlock, beforeCombatStart, afterCombatVictory,
beforeSideTurnStart, afterSideTurnStart, afterPlayerTurnStart,
afterAutoPostPlayPhaseEntered, beforeSideTurnEndEarly, beforeSideTurnEnd,
afterSideTurnEnd, afterDamageReceived, afterDeath, beforeCardPlayed,
afterCardPlayed, afterPowerAmountChanged, afterCardExhausted, afterCardDrawn
(AfterCardDrawnEarly too), afterBlockGained, afterCardEnteredCombat,
afterEnergySpent. Power-only: beforeApplied, afterApplied, afterRemoved.
Also (E7): modifyDamageCap (return `kNoDamageCap` for "no cap"), beforeDamageReceived,
afterBlockBroken, beforeAttack / afterAttack (take the `cmd::Attack`; group plain damage calls with
`cmd::beginAttackContext` / `endAttackContext` like the C#'s AttackContext),
afterModifyingCardPlayResultLocation, shouldAllowHitting (`Combat::canReceivePowers`),
shouldTakeExtraTurn / afterTakingExtraTurn (`Combat::extraTurn`), and the Early/Late variants
(...Early / ...Late suffix on the C# name) for the hooks that have users.
The `PlayerChoiceContext` / `ICombatState` parameters of C# hooks are dropped.
`CombatSide` → `Side::Player` / `Side::Enemy`. `combat->currentSide`,
`combat->roundNumber`, `combat->turnNumber` exist.

A `TemporaryStrengthPower` subclass: copy `SetupStrikePower` in powers.h
(for `IsPositive => false` apply negative strength and restore at turn end).

## Rules

* Coroutine functions: take parameters **by value** (not `const&`) when the
  coroutine may outlive the caller's temporaries; hooks in game.h are fine as declared.
* If something cannot be expressed with the API, implement the closest
  faithful version and mark it `// PORT NOTE: ...`. Do not edit engine files.
* Register every class in the file's `register...()` function with
  `registerCardType<X>();` / `registerPowerType<X>();`.
* Check your file compiles: `clang++ -std=c++20 -fsyntax-only -Isource <your file>`.
