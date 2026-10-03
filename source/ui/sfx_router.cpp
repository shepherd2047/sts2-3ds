#include "sfx_router.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../audio/audio.h"
#include "../core/game.h"

using namespace sts;

namespace sfx {
namespace {

struct SfxRow { const char* key; const char* sound; };
#include "sfx_tables.inc"

const char* lookup(const SfxRow* rows, const std::string& key) {
  for (const SfxRow* r = rows; r->key; ++r)
    if (key == r->key) return r->sound;
  return nullptr;
}

// StringHelper snake_case of a class name ("TwigSlimeM" -> "twig_slime_m"); Id.Entry.ToLowerInvariant().
std::string snake(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); ++i) {
    if (i && std::isupper((unsigned char)s[i])) o += '_';
    o += (char)std::tolower((unsigned char)s[i]);
  }
  return o;
}

std::string lower(std::string s) {
  for (auto& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

std::string monsterId(const Creature* c) { return c && c->monster ? c->monster->id : std::string(); }

// MonsterModel.AttackSfx / CastSfx / DeathSfx: event:/sfx/enemy/enemy_attacks/<id>/<id>_<what>.
std::string monsterSfx(const std::string& id, const char* what) {
  std::string s = snake(id);
  return "event:/sfx/enemy/enemy_attacks/" + s + "/" + s + "_" + what;
}

std::string charSfx(const Run& r, const char* what) {
  std::string s = lower(r.characterId);
  return "event:/sfx/characters/" + s + "/" + s + "_" + what;
}

bool logOn() {
  static const bool on = getenv("STS_SFX_LOG") != nullptr;
  return on;
}

}  // namespace

void play(const std::string& name) {
  if (name.empty()) return;
  bool ok = audio::playSfx(name);
  if (logOn()) printf("SFX %s%s\n", name.c_str(), ok ? "" : " (silent)");
}

static Creature* movePlayedFor = nullptr;  // the enemy whose current move already played its own sound

void combatEvent(const VisualEvent& e, Combat& c, Run& r) {
  Creature* who = e.who;
  bool enemy = who && !who->isPlayer && who->side == Side::Enemy && who->monster;
  switch (e.kind) {
    case VisualEvent::MoveStart:  // the move method's own SfxCmd.Play, replacing the generic attack / cast sound
      movePlayedFor = nullptr;
      if (enemy)
        if (const char* s = lookup(MON_MOVE, monsterId(who) + "/" + e.text)) {
          play(s);
          movePlayedFor = who;
        }
      break;
    case VisualEvent::Anim: {
      if (enemy && who == movePlayedFor) {  // the move already played its own sound
        movePlayedFor = nullptr;
        break;
      }
      bool attack = e.text == "Attack" || e.text == "AttackHeavy";
      bool cast = e.text == "Cast" || e.text == "Debuff";
      if (who && who->isPlayer) {  // CreatureCmd.TriggerAnim: the character's attack / cast sound
        if (attack) play(charSfx(r, "attack"));
        else if (cast) play(charSfx(r, "cast"));
      } else if (enemy) {
        const std::string id = monsterId(who);
        if (attack) {
          const char* s = lookup(MON_ATTACK, id);
          play(s ? s : monsterSfx(id, "attack"));
        } else if (cast) {
          const char* s = lookup(MON_CAST, id);
          play(s ? s : monsterSfx(id, "cast"));
        }
      }
      break;
    }
    case VisualEvent::Hit: {  // AttackCommand.HitSfx: once per hit
      const char* s = nullptr;
      if (who && who->isPlayer) s = lookup(CARD_HIT, e.text);
      else if (enemy) s = lookup(MON_HIT, monsterId(who));
      if (s) play(s);
      break;
    }
    case VisualEvent::CardPlayed: {
      if (const char* s = lookup(CARD_PLAY, e.text)) play(s);
      // NCardFlyPowerVfx / NCardFlyVfx swoosh: powers get their own, the rest fly to the discard pile.
      play(e.amount == (int)CardType::Power ? "event:/sfx/ui/cards/card_movement_B_power"
                                             : "event:/sfx/ui/cards/card_movement_B_play_into_discard");
      break;
    }
    case VisualEvent::Damage:
      if (enemy && e.amount > 0) {  // SfxCmd.PlayDamage + MonsterModel.HurtSfx
        const char* type = lookup(MON_DMG, monsterId(who));
        play(std::string("event:/sfx/enemy/enemy_impact_enemy_size/enemy_impact_") + (type ? type : "armor"));
        if (const char* h = lookup(MON_HURT, monsterId(who))) play(h);
      }
      break;
    case VisualEvent::BlockBroken: play("event:/sfx/block_break"); break;
    case VisualEvent::Blocked: play("event:/sfx/block_hit"); break;
    case VisualEvent::Block: play("event:/sfx/block_gain"); break;
    case VisualEvent::Heal:
      if (e.amount > 0) play("event:/sfx/heal");
      break;
    case VisualEvent::PowerUp:
    case VisualEvent::PowerDown: {  // NCreature.OnPowerIncreased: buff or debuff by the power's type
      bool buff = e.kind == VisualEvent::PowerUp;
      if (who)
        for (auto& p : who->powers)
          if (p->locKey == e.text) { buff = p->typeForAmount(Dec(p->amount)) == PowerType::Buff; break; }
      play(buff ? "event:/sfx/buff" : "event:/sfx/debuff");
      break;
    }
    case VisualEvent::Death: {  // SfxCmd.PlayDeath
      if (who && who->isPlayer) { play(charSfx(r, "die")); break; }
      if (!enemy) break;
      const std::string id = monsterId(who);
      const char* s = lookup(MON_DEATH, id);
      if (s && !*s) break;  // HasDeathSfx == false
      play(s ? s : monsterSfx(id, "die"));
      break;
    }
    case VisualEvent::CardExhaust: play("card_exhaust.mp3"); break;
    case VisualEvent::Shuffle: play("event:/sfx/ui/cards/card_movement_B_into_draw"); break;
    case VisualEvent::Banner: play("event:/sfx/ui/cards/card_movement_B_into_discard"); break;
  }
  (void)c;
}

void frame(Run& r) {
  static int gold = -1, relics = -1, potions = -1, energy = 0;
  static const Combat* combat = nullptr;
  static int turn = -1;
  static Side side = Side::Player;
  static Screen screen = Screen::Title;
  static std::vector<float> flash;

  if (r.screen == Screen::Title) gold = relics = potions = -1;  // a new run starts from its own baseline

  // PlayerCmd.GainGold / LoseGold.
  if (gold >= 0 && r.gold != gold) {
    int d = r.gold - gold;
    play(d > 0 ? (d >= 100 ? "event:/sfx/ui/gold/gold_3" : d > 30 ? "event:/sfx/ui/gold/gold_2" : "event:/sfx/ui/gold/gold_1")
               : "event:/sfx/ui/gold/gold_1");
  }
  gold = r.gold;

  // RelicCmd.Obtain / PotionCmd.TryToProcure.
  int nr = (int)r.relics.size();
  if (relics >= 0 && nr > relics) play("relic_get.mp3");
  relics = nr;
  int np = 0;
  for (auto& p : r.potions) if (p) ++np;
  if (potions >= 0 && np > potions) play("gain_potion.mp3");
  potions = np;

  // Player.RelicFlash: the relic's FlashSfx (the "draw" variant for a few relics).
  flash.resize(r.relics.size(), 0.f);
  for (size_t i = 0; i < r.relics.size(); ++i) {
    float f = r.relics[i]->flash;
    if (f > 0.9f && flash[i] <= 0.9f) {
      static const char* drawFlash[] = {"GamePiece", "Pendulum", "JossPaper", "UnceasingTop", "CentennialPuzzle", "IronClub"};
      bool draw = false;
      for (const char* d : drawFlash) draw |= r.relics[i]->id == d;
      play(draw ? "event:/sfx/ui/relic_activate_draw" : "event:/sfx/ui/relic_activate_general");
    }
    flash[i] = f;
  }

  // NRewardsScreen.Ready plays the victory jingle.
  if (r.screen != screen) {
    if (r.screen == Screen::Reward && screen == Screen::Combat) play("victory.mp3");
    screen = r.screen;
  }

  // Turn banners (NPlayerTurnBanner / NEnemyTurnBanner) and PlayerCmd.GainEnergy.
  Combat* cb = r.combat.get();
  if (cb != combat) {
    combat = cb;
    turn = -1;
    side = Side::Player;
    energy = cb ? cb->energy : 0;
  }
  if (cb) {
    if (cb->playerPhase && cb->turnNumber != turn) { turn = cb->turnNumber; play("player_turn.mp3"); }
    if (cb->currentSide == Side::Enemy && side != Side::Enemy && !cb->over) play("enemy_turn.mp3");
    if (cb->energy > energy) play("event:/sfx/ui/gain_energy");
    side = cb->currentSide;
    energy = cb->energy;
  }
}

void click() { play("event:/sfx/ui/clicks/ui_click"); }
void cardDeal() { play("card_deal.mp3"); }
void cardsDiscarded() { play("event:/sfx/ui/cards/card_movement_B_into_discard"); }
void mapSelect() { play("event:/sfx/ui/map/map_select"); }
void potionUsed() { play("potion_slosh_1.mp3"); }
void smith() { play("card_smith.mp3"); }

}  // namespace sfx
