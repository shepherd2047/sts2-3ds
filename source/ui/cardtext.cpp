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

// {X:energyIcons()}: 简体中文 "N点能量" (the RGDSplus wording); English as the C#
// EnergyIconsFormatter: 1..3 icons repeated, otherwise the amount then one icon.
static std::string energyText(const std::string& name, int n, const std::string& shown) {
  if (!english()) return name == "energyPrefix" ? std::string("点能量") : "[gold]" + shown + "点能量[/gold]";
  if (name == "energyPrefix") return "[icon:energy]";
  if (n > 0 && n < 4) {
    std::string out;
    for (int i = 0; i < n; ++i) out += "[icon:energy]";
    return out;
  }
  return shown + "[icon:energy]";
}

// Splits a top-level (brace-depth 0) '|'-separated list.
static std::vector<std::string> splitAltsTop(const std::string& s) {
  std::vector<std::string> out;
  int d = 0;
  size_t start = 0;
  for (size_t k = 0; k <= s.size(); ++k) {
    if (k < s.size() && s[k] == '{') ++d;
    else if (k < s.size() && s[k] == '}') --d;
    if (k == s.size() || (s[k] == '|' && d == 0)) { out.push_back(s.substr(start, k - start)); start = k + 1; }
  }
  return out;
}

// {X:plural:a|b}: the chosen alternative with its "{}" replaced by the amount (English texts use it).
static std::string pluralAlt(const std::string& alt, int n) {
  std::string s = alt;
  for (size_t p; (p = s.find("{}")) != std::string::npos;) s.replace(p, 2, num(n));
  return s;
}

// {X:cond:<0?a|b} / {X:cond:==1?a|>1?b|c} / {X:cond:a|b} on a number: the first alternative whose
// test passes; a plain alternative (no test) is taken as is, a plain two-way list means "non-zero".
static std::string condAlt(const std::string& alts, int v, const std::vector<std::string>& parts) {
  (void)alts;
  bool tests = false;
  for (auto& a : parts) {
    size_t q = a.find('?');
    const char c0 = a.empty() ? 0 : a[0];
    if (q == std::string::npos || !(c0 == '<' || c0 == '>' || c0 == '=' || c0 == '!')) {
      if (!tests) return parts.size() > 1 ? (v != 0 ? parts[0] : parts[1]) : (v != 0 ? parts[0] : std::string());
      return pluralAlt(a, v);
    }
    tests = true;
    std::string op = a.substr(0, (a.size() > 1 && a[1] == '=') ? 2 : 1);
    int n = std::atoi(a.substr(op.size(), q - op.size()).c_str());
    bool ok = op == "<" ? v < n : op == ">" ? v > n : op == "<=" ? v <= n : op == ">=" ? v >= n : op == "!=" ? v != n : v == n;
    if (ok) return pluralAlt(a.substr(q + 1), v);
  }
  return {};
}

// {Name:choose(V1|V2|...):alt1|alt2|...[|fallback]}: the alternative whose option equals the value
// (numeric compare when `numeric`, else string compare); a trailing extra alternative is the
// fallback. Inside it "{}" is the value as shown and "{:fmt}" is "{Name:fmt}". Returns the
// still-unexpanded alternative.
static std::string chooseAlt(const std::string& name, const std::string& rest, bool numeric, int nv,
                             const std::string& sv, const std::string& shown) {
  size_t close = rest.find(')');
  if (close == std::string::npos) return {};
  auto options = splitAltsTop(rest.substr(7, close - 7));
  size_t colon = rest.find(':', close);
  auto alts = splitAltsTop(colon == std::string::npos ? std::string() : rest.substr(colon + 1));
  int match = -1;
  for (size_t k = 0; k < options.size(); ++k) {
    if (numeric ? nv == std::atoi(options[k].c_str()) : sv == options[k]) { match = (int)k; break; }
  }
  if (alts.empty()) return {};
  std::string chosen = match >= 0 ? alts[std::min((size_t)match, alts.size() - 1)]
                       : alts.size() > options.size() ? alts.back() : std::string();
  for (size_t p = 0; (p = chosen.find("{:", p)) != std::string::npos; p += name.size() + 2) chosen.replace(p, 1, "{" + name);
  for (size_t p; (p = chosen.find("{}")) != std::string::npos;) chosen.replace(p, 2, shown);
  return chosen;
}

// String-valued card vars of the C# CardModel description (CardType / TargetType).
static const char* cardTypeName(CardType t) {
  switch (t) {
    case CardType::Attack: return "Attack";
    case CardType::Skill: return "Skill";
    case CardType::Power: return "Power";
    case CardType::Status: return "Status";
    default: return "Curse";
  }
}
static const char* targetTypeName(TargetType t) {
  switch (t) {
    case TargetType::None: return "None";
    case TargetType::Self: return "Self";
    case TargetType::AnyEnemy: return "AnyEnemy";
    case TargetType::AllEnemies: return "AllEnemies";
    default: return "RandomEnemy";
  }
}

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
    } else if (name.rfind("Calculated", 0) == 0 && c->calcMultiplier && c->var("CalculationBase")) {
      // CalculatedVar (Hits / Cards / Shivs / Channels ...): CalculationBase + CalculationExtra * multiplier.
      base = shown = (c->val("CalculationBase") + c->val("CalculationExtra") * Dec(c->calcMultiplier(c))).toInt();
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
      // Boolean description flags the card model computes (others: a non-zero var of that name).
      auto flag = [&](const std::string& n) -> bool {
        if (n == "IsOstyAlive") return cb && cb->osty && cb->osty->alive();
        if (n == "GainsBlock") return c->gainsBlock();
        return c->var(n.c_str()) && raw(n) != Dec(0);
      };
      if (name == "InCombat") out += choose(rest, cb != nullptr);
      else if (name == "IfUpgraded") out += choose(rest.substr(rest.find(':') + 1), c->upgraded());
      else if (rest.rfind("choose(", 0) == 0) {
        DynVar* v = c->var(name.c_str());
        if (name == "CardType" || name == "TargetType") {
          // MadScience keeps CardType as a 0/1/2 var (the option's position); otherwise the card's own type.
          bool idx = v != nullptr;
          size_t cl = rest.find(')');
          auto opts = splitAltsTop(cl == std::string::npos ? std::string() : rest.substr(7, cl - 7));
          int i = v ? v->base.toInt() : 0;
          std::string sv = idx ? (i >= 0 && i < (int)opts.size() ? opts[i] : std::string())
                               : name == "CardType" ? cardTypeName(c->type) : targetTypeName(c->target);
          out += expand(chooseAlt(name, rest, false, 0, sv, sv));
        } else if (v) {
          out += expand(chooseAlt(name, rest, true, v->base.toInt(), "", value(name)));
        } else {
          out += expand(chooseAlt(name, rest, false, 0, "", ""));
        }
      } else if (rest.rfind("abs", 0) == 0) {
        out += num(std::abs(raw(name).toInt()));
      } else if (rest.rfind("energyIcons", 0) == 0) {
        // Energy icons: "{Energy:energyIcons()}" -> "2点能量"; "{energyPrefix:energyIcons(1)}" -> "点能量".
        out += energyText(name, raw(name).toInt(), name == "energyPrefix" ? std::string() : value(name));
      } else if (rest.rfind("starIcons", 0) == 0) {
        out += value(name) + "[icon:star]";  // {Stars:starIcons()}: the amount and a star (X3)
      } else if (rest.rfind("percentMore", 0) == 0) {
        out += num(((raw(name) - Dec(1)) * Dec(100)).toInt());
      } else if (rest.rfind("percentLess", 0) == 0) {
        out += num(((Dec(1) - raw(name)) * Dec(100)).toInt());
      } else if (rest.rfind("plural:", 0) == 0) {
        auto parts = splitAltsTop(rest.substr(7));
        int n = raw(name).toInt();
        out += expand(pluralAlt(parts.empty() ? std::string() : n == 1 || parts.size() < 2 ? parts[0] : parts[1], n));
      } else if (rest.rfind("cond:", 0) == 0) {
        out += expand(condAlt(rest.substr(5), (name == "IsOstyAlive" || name == "GainsBlock") ? (int)flag(name) : c->var(name.c_str()) ? raw(name).toInt() : 0, splitAltsTop(rest.substr(5))));
      } else if (colon != std::string::npos && rest.find('|') != std::string::npos) {
        // A plain {Name:a|b} conditional on a flag this port doesn't model (e.g. IsMultiplayer,
        // or a per-card extra arg like MadScience's rider flags): missing means false, not "?".
        out += choose(rest, flag(name));
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

// Splits a top-level (brace-depth 0) '|'-separated list, e.g. an alt list or a choose() option
// list. Returns the raw (unexpanded) pieces.
static std::vector<std::string> splitAlts(const std::string& s) {
  std::vector<std::string> out;
  int d = 0;
  size_t start = 0;
  for (size_t k = 0; k <= s.size(); ++k) {
    if (k < s.size() && s[k] == '{') ++d;
    else if (k < s.size() && s[k] == '}') --d;
    if ((k == s.size() || (s[k] == '|' && d == 0))) { out.push_back(s.substr(start, k - start)); start = k + 1; }
  }
  return out;
}

// Relic and event text: the card SmartFormat subset, fed by the model's own DynamicVars
// ({Name}, {Name:energyIcons()}, {Name:plural:a|b}, {Name:percentMore/Less()},
// {InCombat:a|b}, {IfUpgraded:show:a|b}, {Name:choose(V1|V2|...):alt1|alt2|...[|fallback]} --
// V compared against Name's numeric value or, if Name isn't a DynVar, its string var; a bare
// "{}" inside the chosen alt is replaced with Name's value), plus an event's string vars (a loc
// key looked up here, falling back to the literal text). Unknown names are left visible as "?".
// `upgraded` feeds {IfUpgraded:...} (potions/relics never upgrade, so callers normally pass false).
std::string expandSmart(const std::string& src, const std::vector<DynVar>& vars, bool inCombat,
                         const std::map<std::string, std::string>* strVars, bool upgraded) {
  auto find = [&](const std::string& n) -> const DynVar* {
    for (auto& v : vars) if (v.name == n) return &v;
    return nullptr;
  };
  auto raw = [&](const std::string& n) -> Dec { auto* v = find(n); return v ? v->base : Dec(0); };
  auto strOf = [&](const std::string& n) -> std::string {
    if (strVars && strVars->count(n)) { const std::string& v = strVars->at(n); return R().hasLoc(v) ? L(v) : v; }
    return {};
  };
  int nest = 0;
  std::function<std::string(const std::string&)> expand = [&](const std::string& s) -> std::string {
    std::string out;
    for (size_t i = 0; i < s.size();) {
      if (s[i] != '{') { out += s[i++]; continue; }
      if (i + 1 < s.size() && s[i + 1] == '}') { out += "{}"; i += 2; continue; }  // literal self-ref marker
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
        auto parts = splitAlts(alts);
        if (parts.empty()) return std::string();
        return expand(first ? parts[0] : parts.size() > 1 ? parts[1] : std::string());
      };
      if (name == "InCombat") out += choose(rest, inCombat);
      else if (name == "IfUpgraded") out += choose(rest.substr(rest.find(':') + 1), upgraded);
      else if (rest.rfind("choose(", 0) == 0) {
        bool isNumeric = find(name) != nullptr;
        std::string sv = isNumeric ? std::string() : strOf(name);
        int nv = isNumeric ? raw(name).toInt() : 0;
        out += expand(chooseAlt(name, rest, isNumeric, nv, sv, isNumeric ? num(nv) : sv));
      } else if (rest.rfind("energyIcons", 0) == 0)
        out += energyText(name, raw(name).toInt(), num(raw(name).toInt()));
      else if (rest.rfind("abs", 0) == 0) out += num(std::abs(raw(name).toInt()));
      else if (rest.rfind("starIcons", 0) == 0) out += num(raw(name).toInt()) + "[icon:star]";
      else if (rest.rfind("percentMore", 0) == 0) out += num(((raw(name) - Dec(1)) * Dec(100)).toInt());
      else if (rest.rfind("percentLess", 0) == 0) out += num(((Dec(1) - raw(name)) * Dec(100)).toInt());
      else if (rest.rfind("plural:", 0) == 0) {
        auto parts = splitAlts(rest.substr(7));
        int n = raw(name).toInt();
        out += expand(pluralAlt(parts.empty() ? std::string() : n == 1 || parts.size() < 2 ? parts[0] : parts[1], n));
      } else if (rest.rfind("cond:", 0) == 0 && find(name))
        out += expand(condAlt(rest.substr(5), raw(name).toInt(), splitAlts(rest.substr(5))));
      else if (colon != std::string::npos && rest.find('|') != std::string::npos) {
        // A plain {Name:a|b} conditional on a flag this port doesn't model: missing means
        // false (e.g. IsMultiplayer), not "?".
        bool v = find(name) ? raw(name) != Dec(0) : strVars && strVars->count(name) && strOf(name) != "";
        out += choose(rest, v);
      } else if (find(name)) out += num(raw(name).toInt());
      else if (strVars && strVars->count(name)) {
        // A string var may itself hold loc text with {...} (recursive string vars); bounded nesting.
        std::string sv = strOf(name);
        if (sv.find('{') == std::string::npos || nest >= 4) out += sv;
        else { ++nest; out += expand(sv); --nest; }
      }
      else out += "?";
    }
    return out;
  };
  return expand(src);
}

// CardModel.GetDescriptionForPile: an enchantment's extraCardText (purple) and, when it adds
// replays, the REPLAY line; nothing for enchantments without extra text or once disabled.
// The affliction's extra text sits between them (A4). Badges are drawn by card_view.cpp.
static std::string enchantmentCardText(Card* c) {
  std::string out;
  Enchantment* e = c->enchantment.get();
  if (e && e->hasExtraCardText() && !e->disabled()) {
    std::vector<DynVar> vars = e->vars;
    vars.push_back({"Amount", Dec(e->amount), Dec(e->amount)});
    std::string key = "enchantments." + e->locKey + ".extraCardText";
    std::map<std::string, std::string> sv{{"TargetType", targetTypeName(c->target)}, {"CardType", cardTypeName(c->type)}};
    if (R().hasLoc(key)) out += "[purple]" + expandSmart(L(key), vars, c->combat != nullptr, &sv) + "[/purple]";
  }
  // A4: then the affliction's extraCardText (AfflictionModel.DynamicExtraCardText), also purple.
  if (Affliction* a = c->affliction.get(); a && a->hasExtraCardText()) {
    std::vector<DynVar> vars{{"Amount", Dec(a->amount), Dec(a->amount)}};
    std::string key = "afflictions." + a->locKey + ".extraCardText";
    if (R().hasLoc(key)) out += (out.empty() ? "" : "\n") + std::string("[purple]") + expandSmart(L(key), vars, false) + "[/purple]";
  }
  if (!e) return out;
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
