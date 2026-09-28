// Split from ui.cpp (F3).
#include "ui_common.h"

namespace ui {

// ================================================================ creature animation

std::string App::idleAnim(const Visual& v) const { return v.puffed ? "idle_loop_puffed" : "idle_loop"; }

// The player's art id (Spine skeleton and portrait sprite): the run's character. Until that
// character's art is baked (X*.5) the Ironclad's stands in.
std::string playerArt(Run* r) {
  std::string key = r ? r->character().key : std::string("IRONCLAD");
  return R().sprite("creature/" + key) ? key : std::string("IRONCLAD");
}

std::string energyOrbSprite(Run* r) {
  std::string color = r ? r->character().energyColor : std::string("ironclad");
  std::string name = color == "ironclad" ? "ui/energy_orb" : "ui/energy_orb_" + color;
  return R().sprite(name) ? name : "ui/energy_orb";
}

std::string cardEnergySprite(Run* r) {
  std::string color = r ? r->character().energyColor : std::string("ironclad");
  std::string name = color == "ironclad" ? "card/energy" : "card/energy_" + color;
  return R().sprite(name) ? name : "card/energy";
}

App::Visual* App::visual(Creature* c) {
  auto it = visuals_.find(c);
  if (it != visuals_.end()) return it->second.skel ? &it->second : nullptr;
  Visual& v = visuals_[c];
  v.key = c->isPlayer ? playerArt(run_.get()) : c->name;
  v.data = R().skeleton(v.key);
  if (!v.data) return nullptr;
  v.skel = std::make_unique<spine::Skeleton>(v.data);
  v.anim = std::make_unique<spine::AnimationState>(v.data);
  v.anim->play("idle_loop", true);
  // Desynchronise identical monsters.
  v.anim->update((float)((reinterpret_cast<uintptr_t>(c) >> 4) % 97) / 97.f);
  return &v;
}

// CreatureAnimator triggers (MonsterModel.GenerateAnimator and its overrides).
void App::trigger(Creature* c, const std::string& what, int amount) {
  Visual* v = visual(c);
  if (!v || v->dying) return;
  const std::string& k = v->key;
  auto has = [&](const std::string& a) { return v->data->animation(a) != nullptr; };
  auto play = [&](const std::string& a) {
    if (has(a)) v->anim->play(a, false, idleAnim(*v));
  };
  if (what == "Dead") {
    v->dying = true;
    std::string die = v->puffed && has("die_puffed") ? "die_puffed" : "die";
    if (has(die)) v->anim->play(die, false);
    return;
  }
  if (what == "Hit") {
    play(v->puffed && has("hurt_puffed") ? "hurt_puffed" : "hurt");
    return;
  }
  // First animation that exists, else any whose name starts with the first candidate.
  auto playAny = [&](std::initializer_list<const char*> names) {
    for (const char* n : names) if (has(n)) { play(n); return; }
    std::string prefix = *names.begin();
    for (auto& a : v->data->animations)
      if (a.name.rfind(prefix, 0) == 0) { play(a.name); return; }
  };
  if (what == "Attack" || what == "AttackHeavy") {
    int hits = amount / 1000, dmg = amount % 1000;
    v->puffed = false;
    if (k == "VANTOM") play(dmg >= 20 ? "attack_heavy" : hits >= 2 ? "attack_double" : "attack");
    else if (k == "INKLET" && hits >= 3) play("attack_triple");
    else if (k == "KIN_PRIEST") play(hits >= 3 ? "attack_laser" : "attack_grenade");
    else if (k == "KIN_FOLLOWER") play(hits >= 2 ? "attack_boomerang" : "attack_slash");
    else if (what == "AttackHeavy" && has("attack_heavy")) play("attack_heavy");
    else playAny({"attack"});
    return;
  }
  if (what == "Stun") {
    if (has("stun")) v->anim->play("stun", false, has("stun_loop") ? "stun_loop" : idleAnim(*v));
    return;
  }
  if (what == "Unstun") { playAny({"wake_up"}); return; }
  if (what == "Summon") { playAny({"summon", "cast"}); return; }
  // Cast / Debuff
  if (k == "NIBBIT") play("hiss");
  else if (k == "MAWLER") play("roar");
  else if (k == "FUZZY_WURM_CRAWLER") { v->puffed = true; play("inhale"); }
  else if (k == "VANTOM") play(what == "Debuff" ? "debuff" : "buff");
  else if (what == "Debuff") playAny({"debuff", "cast", "buff", "shrill", "rally"});
  else playAny({"cast", "buff", "rally", "shrill", "summon"});
}

}  // namespace ui
