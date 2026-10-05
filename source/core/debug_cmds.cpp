// Preview / debug switches and runtime debug commands (debug_cmds.h).
#include "debug_cmds.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace sts {
namespace dbg {

namespace {

// "STRENGTH_POWER" / "body_slam" -> "StrengthPower" / "BodySlam"; other names are returned unchanged.
std::string snakeToPascal(const std::string& s) {
  if (s.find('_') == std::string::npos) {
    bool allUpper = !s.empty();
    for (char ch : s) allUpper = allUpper && !std::islower((unsigned char)ch);
    if (!allUpper) return s;
  }
  std::string out;
  bool up = true;
  for (char ch : s) {
    if (ch == '_') { up = true; continue; }
    out += up ? (char)std::toupper((unsigned char)ch) : (char)std::tolower((unsigned char)ch);
    up = false;
  }
  return out;
}

template <class Exists>
std::string lookup(const std::string& raw, const char* suffix, Exists exists) {
  if (raw.empty()) return "";
  const std::string pascal = snakeToPascal(raw);
  for (const std::string& cand : {raw, raw + suffix, pascal, pascal + suffix})
    if (exists(cand)) return cand;
  return "";
}

std::string env(const char* name) {
  const char* v = getenv(name);
  return v ? std::string(v) : std::string();
}

// A combat card for a debug list: a matching copy taken out of the draw pile when there is one,
// else a new card owned by the combat. Null for an unknown id.
Card* takeOrMake(Combat& c, const CardSpec& spec) {
  for (Card* k : c.draw)
    if (k->id == spec.id && k->upgraded() == spec.upgraded) {
      c.removeFromPiles(k);
      return k;
    }
  auto made = db::card(spec.id);
  if (!made) return nullptr;
  if (spec.upgraded) made->upgrade();
  Card* k = c.addCard(std::move(made));
  k->run = c.run;
  return k;
}

int toInt(const std::string& s, int fallback) {
  if (s.empty()) return fallback;
  char* end = nullptr;
  long v = std::strtol(s.c_str(), &end, 10);
  return end && *end == '\0' ? (int)v : fallback;
}

Creature* at(Combat& c, int idx) {
  auto list = creatures(c);
  return idx >= 0 && idx < (int)list.size() ? list[idx] : nullptr;
}

}  // namespace

std::string powerId(const std::string& name) {
  return lookup(name, "Power", [](const std::string& id) { return db::power(id) != nullptr; });
}
std::string cardId(const std::string& name) {
  return lookup(name, "", [](const std::string& id) { return db::card(id) != nullptr; });
}
std::string potionId(const std::string& name) {
  return lookup(name, "Potion", [](const std::string& id) { return db::potion(id) != nullptr; });
}
std::string orbId(const std::string& name) {
  return lookup(name, "Orb", [](const std::string& id) { return db::orb(id) != nullptr; });
}

std::vector<std::string> splitList(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t a = 0;
  while (a <= s.size()) {
    size_t b = s.find(sep, a);
    if (b == std::string::npos) b = s.size();
    std::string item = s.substr(a, b - a);
    while (!item.empty() && std::isspace((unsigned char)item.front())) item.erase(item.begin());
    while (!item.empty() && std::isspace((unsigned char)item.back())) item.pop_back();
    if (!item.empty()) out.push_back(item);
    a = b + 1;
  }
  return out;
}

std::vector<PowerSpec> parsePowers(const std::string& s) {
  std::vector<PowerSpec> out;
  for (const std::string& item : splitList(s)) {
    auto parts = splitList(item, ':');
    if (parts.empty()) continue;
    PowerSpec p;
    p.id = powerId(parts[0]);
    if (p.id.empty()) { printf("[debug] unknown power %s\n", parts[0].c_str()); continue; }
    p.amount = parts.size() > 1 ? toInt(parts[1], 1) : 1;
    p.idx = parts.size() > 2 ? toInt(parts[2], 0) : 0;
    out.push_back(p);
  }
  return out;
}

std::vector<CardSpec> parseCards(const std::string& s) {
  std::vector<CardSpec> out;
  for (std::string item : splitList(s)) {
    CardSpec spec;
    if (!item.empty() && item.back() == '+') { spec.upgraded = true; item.pop_back(); }
    spec.id = cardId(item);
    if (spec.id.empty()) { printf("[debug] unknown card %s\n", item.c_str()); continue; }
    out.push_back(spec);
  }
  return out;
}

std::vector<std::string> parseOrbs(const std::string& s) {
  std::vector<std::string> out;
  for (const std::string& item : splitList(s)) {
    std::string id = orbId(item);
    if (id.empty()) printf("[debug] unknown orb %s\n", item.c_str());
    else out.push_back(id);
  }
  return out;
}

std::string decodeCommand(std::string item) {
  for (char& ch : item) if (ch == '_') ch = ' ';
  return item;
}

std::vector<Creature*> creatures(Combat& c) {
  std::vector<Creature*> out;
  if (c.player) out.push_back(c.player);
  if (c.osty && !c.osty->removed) out.push_back(c.osty);
  for (Creature* p : c.pets) if (p && !p->removed) out.push_back(p);
  for (Creature* e : c.enemies) if (e && !e->removed) out.push_back(e);
  return out;
}

bool anyCombatStartSwitch() {
  for (const char* k : {"STS_POWERS", "STS_ORBS", "STS_STARS", "STS_PILE_DRAW", "STS_PILE_DISCARD"})
    if (getenv(k)) return true;
  return false;
}

Task<> applyCombatStart(Combat& c) {
  // Draw pile: the listed cards on top, first listed = top.
  auto drawSpecs = parseCards(env("STS_PILE_DRAW"));
  for (auto it = drawSpecs.rbegin(); it != drawSpecs.rend(); ++it)
    if (Card* k = takeOrMake(c, *it)) c.draw.insert(c.draw.begin(), k);
  for (const CardSpec& spec : parseCards(env("STS_PILE_DISCARD")))
    if (Card* k = takeOrMake(c, spec)) c.discard.push_back(k);
  for (const PowerSpec& p : parsePowers(env("STS_POWERS"))) {
    Creature* target = at(c, p.idx);
    if (!target) { printf("[debug] STS_POWERS: no creature at index %d\n", p.idx); continue; }
    co_await cmd::applyPower(db::power(p.id), target, Dec(p.amount), nullptr, nullptr);
  }
  for (const std::string& id : parseOrbs(env("STS_ORBS"))) co_await cmd::channelOrb(c, db::orb(id));
  if (const char* s = getenv("STS_STARS")) co_await cmd::gainStars(c, std::atoi(s));
}

int forceOpeningHand(Combat& c) {
  auto specs = parseCards(env("STS_HAND"));
  if (specs.empty()) return 0;
  std::vector<Card*> cards;
  for (const CardSpec& spec : specs)
    if (Card* k = takeOrMake(c, spec)) cards.push_back(k);
  if (cards.size() > 10) cards.resize(10);  // CardPile.MaxCardsInHand
  c.draw.insert(c.draw.begin(), cards.begin(), cards.end());
  return (int)cards.size();
}

void applyStartEnergy(Combat& c) {
  if (const char* s = getenv("STS_ENERGY")) c.energy = std::max(0, std::atoi(s));
}

Task<bool> runCommand(Run& r, std::string line) {
  auto args = splitList(decodeCommand(line), ' ');
  if (args.empty()) co_return false;
  const std::string verb = args[0];
  auto arg = [&](size_t i) { return i < args.size() ? args[i] : std::string(); };
  auto fail = [&](const char* why) {
    printf("[debug] command '%s': %s\n", line.c_str(), why);
    return false;
  };
  printf("[debug] command: %s\n", decodeCommand(line).c_str());

  if (verb == "potion") {
    std::string id = potionId(arg(1));
    if (id.empty()) co_return fail("unknown potion");
    if (!r.procurePotion(db::potion(id))) co_return fail("belt full");
    co_return true;
  }

  Combat* cp = r.combat.get();
  if (!cp || !cp->inProgress || cp->over || cp->ending) co_return fail("not in a combat");
  Combat& c = *cp;

  if (verb == "power") {
    std::string id = powerId(arg(1));
    if (id.empty()) co_return fail("unknown power");
    Creature* target = at(c, toInt(arg(3), 0));
    if (!target) co_return fail("bad target index");
    co_await cmd::applyPower(db::power(id), target, Dec(toInt(arg(2), 1)), nullptr, nullptr);
    co_return true;
  }
  if (verb == "card") {
    auto specs = parseCards(arg(1));
    if (specs.empty()) co_return fail("unknown card");
    std::string where = arg(2).empty() ? "hand" : arg(2);
    for (char& ch : where) ch = (char)std::tolower((unsigned char)ch);
    Pile pile = where == "hand" ? Pile::Hand : where == "draw" ? Pile::Draw : where == "discard" ? Pile::Discard
              : where == "exhaust" ? Pile::Exhaust : Pile::None;
    if (pile == Pile::None) co_return fail("pile must be hand, draw, discard or exhaust");
    auto made = db::card(specs[0].id);
    if (specs[0].upgraded) made->upgrade();
    made->run = &r;
    co_await cmd::addGeneratedCard(c, std::move(made), pile, /*top=*/true);  // draw: on top
    co_return true;
  }
  if (verb == "kill") {
    std::vector<Creature*> enemies;
    for (Creature* e : c.enemies) if (!e->removed && e->alive()) enemies.push_back(e);
    if (enemies.empty()) co_return fail("no enemy");
    std::vector<Creature*> victims;
    if (arg(1).empty()) victims.push_back(enemies[0]);
    else if (arg(1) == "all") victims = enemies;
    else {
      int i = toInt(arg(1), -1);
      if (i < 0 || i >= (int)enemies.size()) co_return fail("bad enemy index");
      victims.push_back(enemies[i]);
    }
    co_await cmd::kill(victims);
    co_await c.checkWinCondition();
    co_return true;
  }
  if (verb == "damage" || verb == "block") {
    int n = toInt(arg(1), -1);
    if (n < 0) co_return fail("amount must be a number >= 0");
    std::vector<Creature*> targets;
    if (!arg(2).empty()) {
      Creature* t = at(c, toInt(arg(2), -1));
      if (!t) co_return fail("bad target index");
      targets.push_back(t);
    } else if (verb == "damage") {
      for (Creature* e : c.enemies) if (!e->removed && e->alive()) targets.push_back(e);
    } else {
      targets.push_back(c.player);
    }
    if (verb == "damage") {
      co_await cmd::damage(targets, Dec(n), kUnpowered, nullptr, nullptr);
      co_await c.checkWinCondition();
    } else {
      co_await cmd::gainBlock(targets[0], Dec(n), kUnpowered, nullptr);
    }
    co_return true;
  }
  if (verb == "stars") {
    co_await cmd::gainStars(c, toInt(arg(1), 0));
    co_return true;
  }
  if (verb == "energy") {
    co_await cmd::gainEnergy(c, toInt(arg(1), 0));
    co_return true;
  }
  if (verb == "orb") {
    std::string id = orbId(arg(1));
    if (id.empty()) co_return fail("unknown orb");
    co_await cmd::channelOrb(c, db::orb(id));
    co_return true;
  }
  co_return fail("unknown command");
}

namespace {
Task<> runDetached(Run* r, std::string line) { co_await runCommand(*r, std::move(line)); }
}  // namespace

void startCommand(Run& r, const std::string& line) { Scheduler::get().spawn(runDetached(&r, line)); }

}  // namespace dbg
}  // namespace sts
