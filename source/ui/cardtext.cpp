// Split from ui.cpp (F3).
#include "ui_common.h"

namespace ui {

// ================================================================ text helpers

std::string App::cardTitle(Card* c) {
  std::string t = L("cards." + c->locKey + ".title");
  if (c->upgradeLevel > 0) t += "+";
  return t;
}

static std::string enchantmentCardText(Card* c);  // defined after expandSmart

// SmartFormat subset used by these cards: {Var:diff()}, {Var},
// {InCombat:a|b}, {IfUpgraded:show:a|b}.
std::string App::describe(Card* c) {
  std::string src = L("cards." + c->locKey + ".description");
  if (!R().hasLoc("cards." + c->locKey + ".description")) src.clear();
  Combat* cb = c->combat && c->combat->inProgress ? c->combat : nullptr;

  auto value = [&](const std::string& name) -> std::string {
    DynVar* v = c->var(name.c_str());
    int shown, base;
    int canonical = v ? v->canonical.toInt() : 0;
    if (name == "CalculatedDamage") {
      Dec d = c->calculatedDamage();
      base = d.toInt();
      canonical = base;
      shown = cb ? std::max(0, cb->modifyDamage(nullptr, cb->player, d, kMove, c).toInt()) : base;
    } else if (name == "CalculatedBlock") {
      Dec d = c->calculatedBlock();
      base = d.toInt();
      canonical = base;
      shown = cb ? std::max(0, cb->modifyBlock(cb->player, d, kMove, c).toInt()) : base;
    } else if (!v) {
      return "?";
    } else if (name == "Damage") {
      base = v->base.toInt();
      shown = cb ? std::max(0, cb->modifyDamage(nullptr, cb->player, v->base, kMove, c).toInt()) : base;
    } else if (name == "Block") {
      base = v->base.toInt();
      shown = cb ? std::max(0, cb->modifyBlock(cb->player, v->base, kMove, c).toInt()) : base;
    } else {
      base = shown = v->base.toInt();
    }
    std::string s = num(shown);
    if (shown > base || (shown == base && base > canonical)) return "[green]" + s + "[/green]";
    if (shown < base) return "[red]" + s + "[/red]";
    return s;
  };

  // Recursive expansion of {...} blocks.
  std::function<std::string(const std::string&)> expand = [&](const std::string& s) -> std::string {
    std::string out;
    for (size_t i = 0; i < s.size();) {
      if (s[i] != '{') { out += s[i++]; continue; }
      int depth = 0;
      size_t j = i;
      for (; j < s.size(); ++j) {
        if (s[j] == '{') ++depth;
        else if (s[j] == '}' && --depth == 0) break;
      }
      std::string body = s.substr(i + 1, j - i - 1);
      i = j + 1;
      size_t colon = body.find(':');
      std::string name = body.substr(0, colon);
      std::string rest = colon == std::string::npos ? "" : body.substr(colon + 1);
      auto choose = [&](const std::string& alts, bool first) {
        int d = 0;
        for (size_t k = 0; k < alts.size(); ++k) {
          if (alts[k] == '{') ++d;
          else if (alts[k] == '}') --d;
          else if (alts[k] == '|' && d == 0) return expand(first ? alts.substr(0, k) : alts.substr(k + 1));
        }
        return first ? expand(alts) : std::string();
      };
      auto raw = [&](const std::string& n) -> Dec { DynVar* v = c->var(n.c_str()); return v ? v->base : Dec(0); };
      if (name == "InCombat") out += choose(rest, cb != nullptr);
      else if (name == "IfUpgraded") out += choose(rest.substr(rest.find(':') + 1), c->upgraded());
      else if (rest.rfind("energyIcons", 0) == 0) {
        // Energy icons: "{Energy:energyIcons()}" -> "2点能量"; "{energyPrefix:energyIcons(1)}" -> "点能量".
        out += name == "energyPrefix" ? std::string("点能量") : "[gold]" + value(name) + "点能量[/gold]";
      } else if (rest.rfind("percentMore", 0) == 0) {
        out += num(((raw(name) - Dec(1)) * Dec(100)).toInt());
      } else if (rest.rfind("percentLess", 0) == 0) {
        out += num(((Dec(1) - raw(name)) * Dec(100)).toInt());
      } else if (rest.rfind("plural:", 0) == 0) {
        out += choose(rest.substr(7), raw(name) == Dec(1));
      } else {
        out += value(name);
      }
    }
    return out;
  };

  std::string d = expand(src);
  if (c->has(kwUnplayable)) d = "[gold]" + L("card_keywords.UNPLAYABLE.title") + "[/gold]" + L("card_keywords.PERIOD") + (d.empty() ? "" : "\n" + d);
  std::string ench = enchantmentCardText(c);  // enchantment extra text, then the replay line
  if (!ench.empty()) d += (d.empty() ? "" : "\n") + ench;
  if (c->has(kwExhaust)) d += (d.empty() ? "" : "\n") + std::string("[gold]") + L("card_keywords.EXHAUST.title") + "[/gold]" + L("card_keywords.PERIOD");
  return d;
}

// Relic and event text: the card SmartFormat subset, fed by the model's own DynamicVars
// ({Name}, {Name:energyIcons()}, {Name:plural:a|b}, {Name:percentMore/Less()},
// {InCombat:a|b}, {IfUpgraded:show:a|b}), plus an event's string vars (a loc key looked up
// here, falling back to the literal text). Unknown names are left visible as "?".
std::string expandSmart(const std::string& src, const std::vector<DynVar>& vars, bool inCombat,
                         const std::map<std::string, std::string>* strVars) {
  auto find = [&](const std::string& n) -> const DynVar* {
    for (auto& v : vars) if (v.name == n) return &v;
    return nullptr;
  };
  auto raw = [&](const std::string& n) -> Dec { auto* v = find(n); return v ? v->base : Dec(0); };
  std::function<std::string(const std::string&)> expand = [&](const std::string& s) -> std::string {
    std::string out;
    for (size_t i = 0; i < s.size();) {
      if (s[i] != '{') { out += s[i++]; continue; }
      int depth = 0;
      size_t j = i;
      for (; j < s.size(); ++j) {
        if (s[j] == '{') ++depth;
        else if (s[j] == '}' && --depth == 0) break;
      }
      std::string body = s.substr(i + 1, j - i - 1);
      i = j + 1;
      size_t colon = body.find(':');
      std::string name = body.substr(0, colon);
      std::string rest = colon == std::string::npos ? "" : body.substr(colon + 1);
      auto choose = [&](const std::string& alts, bool first) {
        int d = 0;
        for (size_t k = 0; k < alts.size(); ++k) {
          if (alts[k] == '{') ++d;
          else if (alts[k] == '}') --d;
          else if (alts[k] == '|' && d == 0) return expand(first ? alts.substr(0, k) : alts.substr(k + 1));
        }
        return first ? expand(alts) : std::string();
      };
      if (name == "InCombat") out += choose(rest, inCombat);
      else if (name == "IfUpgraded") out += choose(rest.substr(rest.find(':') + 1), false);
      else if (rest.rfind("energyIcons", 0) == 0)
        out += name == "energyPrefix" ? std::string("点能量") : "[gold]" + num(raw(name).toInt()) + "点能量[/gold]";
      else if (rest.rfind("percentMore", 0) == 0) out += num(((raw(name) - Dec(1)) * Dec(100)).toInt());
      else if (rest.rfind("percentLess", 0) == 0) out += num(((Dec(1) - raw(name)) * Dec(100)).toInt());
      else if (rest.rfind("plural:", 0) == 0) out += choose(rest.substr(7), raw(name) == Dec(1));
      else if (find(name)) out += num(raw(name).toInt());
      else if (strVars && strVars->count(name)) {
        const std::string& v = strVars->at(name);
        out += R().hasLoc(v) ? L(v) : v;
      } else out += "?";
    }
    return out;
  };
  return expand(src);
}

// CardModel.GetDescriptionForPile: an enchantment's extraCardText (purple) and, when it adds
// replays, the REPLAY line; nothing for enchantments without extra text or once disabled.
// PORT NOTE: the enchantment badge and the affliction line belong to the card renderer (F5).
static std::string enchantmentCardText(Card* c) {
  std::string out;
  Enchantment* e = c->enchantment.get();
  if (!e) return out;
  if (e->hasExtraCardText() && !e->disabled()) {
    std::vector<DynVar> vars = e->vars;
    vars.push_back({"Amount", Dec(e->amount), Dec(e->amount)});
    std::string key = "enchantments." + e->locKey + ".extraCardText";
    if (R().hasLoc(key)) out += "[purple]" + expandSmart(L(key), vars, c->combat != nullptr) + "[/purple]";
  }
  int times = c->enchantedReplayCount();
  if (times > 0 && R().hasLoc("static_hover_tips.REPLAY.extraText")) {
    std::vector<DynVar> vars{{"Times", Dec(times), Dec(times)}};
    out += (out.empty() ? "" : "\n") + expandSmart(L("static_hover_tips.REPLAY.extraText"), vars, false);
  }
  return out;
}

std::string App::describeRelic(Relic* r) {
  if (!R().hasLoc("relics." + r->locKey + ".description")) return {};
  return expandSmart(L("relics." + r->locKey + ".description"), r->vars, r->combat != nullptr);
}

}  // namespace ui
