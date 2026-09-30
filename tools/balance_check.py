#!/usr/bin/env python3
"""Compares every ported card, relic and potion with the C# model (H5).

  make -f Makefile.sdl build/model_dump && ./build/model_dump > build/model_dump.txt
  python3 tools/balance_check.py            # prints every mismatch not in the allowlist; exit 1 if any
  python3 tools/balance_check.py --all      # also prints the allowlisted ones
  python3 tools/balance_check.py --summary  # counts per category (allowlisted included)

Read from the C# (Models.Cards / Relics / Potions): the constructor's cost, type, rarity and target,
CanonicalKeywords, HasEnergyCostX, CanonicalStarCost, MaxUpgradeLevel, CanBeGeneratedInCombat, CanonicalVars (name + base
value) and OnUpgrade (EnergyCost.UpgradeBy, DynamicVars.X.UpgradeValueBy, Add/RemoveKeyword);
Rarity / Usage / TargetType for relics and potions. A C# value that is not a plain expression
(CalculatedDamageVar, StringVar, ...) is not compared.

Mismatch keys look like `card Bash.var.Damage`, `card Bash.upcost`, `relic Anchor.vars-Block`
(var missing in the port), `potion FirePotion.vars+Foo` (var only in the port). The allowlist
(tools/balance_allowlist.txt) holds `<key>  # reason` lines for intentional / unfixable differences
(`*` / `?` wildcards allowed); an entry that matches nothing is reported as stale and fails the check.
"""
import fnmatch
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ascension_check import Cls, balanced, split_top  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEC = os.path.join(os.path.dirname(ROOT), 'sts2-decompiled')
MODELS = {k: os.path.join(DEC, 'MegaCrit.Sts2.Core.Models.' + d) for k, d in
          (('card', 'Cards'), ('relic', 'Relics'), ('potion', 'Potions'))}
DUMP = os.path.join(ROOT, 'build', 'model_dump.txt')
ALLOW = os.path.join(ROOT, 'tools', 'balance_allowlist.txt')

# DynamicVarSet accessors whose key differs from the accessor name.
ACCESSOR = {'Dexterity': 'DexterityPower', 'Doom': 'DoomPower', 'Poison': 'PoisonPower', 'Strength': 'StrengthPower',
            'Vulnerable': 'VulnerablePower', 'Weak': 'WeakPower'}
# `new XVar(value)` default names (Localization.DynamicVars.*.defaultName).
DEFAULT_NAME = {v: v[:-3] for v in ('BlockVar', 'CardsVar', 'DamageVar', 'EnergyVar', 'ExtraDamageVar', 'ForgeVar', 'GoldVar',
                                    'HealVar', 'HpLossVar', 'MaxHpVar', 'OstyDamageVar', 'RepeatVar', 'StarsVar', 'SummonVar',
                                    'CalculationBaseVar', 'CalculationExtraVar', 'CalculatedDamageVar', 'CalculatedBlockVar',
                                    'IfUpgradedVar')}
NO_VALUE = ('CalculatedDamageVar', 'CalculatedBlockVar', 'CalculatedVar', 'IfUpgradedVar')
# Port ids that differ from the C# class name (a C++ struct of the same name exists).
ALIAS = {'LostWispRelic': 'LostWisp'}
# The port has no CardRarity.Event / Quest: event cards use Token or Ancient, quest cards Token.
RARITY_EQUIV = {'Event': ('Token', 'Ancient'), 'Quest': ('Token',)}
# Single player: every player-targeting kind is Self; TargetedNoCreature is None.
TARGET_EQUIV = {'AnyPlayer': 'Self', 'AnyAlly': 'Self', 'AllAllies': 'Self', 'TargetedNoCreature': 'None'}


def fmt(v):
    if v is None:
        return '?'
    v = float(v)
    return str(int(v)) if v == int(v) else ('%g' % v)


def class_text(folder, name):
    path = os.path.join(folder, name + '.cs')
    if not os.path.exists(path):
        return None
    text = open(path, encoding='utf-8').read()
    base = re.search(r'class %s\s*:\s*(\w+)' % name, text)
    if base and not base.group(1).endswith('Model') and os.path.exists(os.path.join(folder, base.group(1) + '.cs')):
        text += '\n' + open(os.path.join(folder, base.group(1) + '.cs'), encoding='utf-8').read()
    return text


def add_fields(cls, text):
    """Properties backed by a field with an initializer (`int X { get { return _x; } }`, `int _x = 1;`)."""
    fields = {m.group(1): m.group(2).strip() for m in re.finditer(r'(?:private|protected)\s+(?:int|decimal)\s+(_\w+)\s*=\s*([^;]+);', text)}
    cls.syms.update(fields)
    for m in re.finditer(r'(?:public|private|protected)\s+(?:int|decimal)\s+(\w+)\s*\{\s*get\s*\{\s*return\s+(_\w+);', text):
        if m.group(2) in fields:
            cls.syms[m.group(1)] = m.group(2)


def ev(cls, expr):
    try:
        v = cls.ev(expr, 0)
        return float(v)
    except Exception:
        return None


def parse_vars(text, cls):
    m = re.search(r'CanonicalVars\s*=>', text)
    if not m:
        return {}
    # the expression runs to the ';' that closes the property
    body, depth, i = '', 0, m.end()
    while i < len(text):
        ch = text[i]
        if ch in '([{':
            depth += 1
        elif ch in ')]}':
            depth -= 1
        if ch == ';' and depth <= 0:
            break
        body += ch
        i += 1
    out = {}
    for vm in re.finditer(r'new (\w+Var)(?:<(\w+)>)?\(', body):
        kind, generic = vm.group(1), vm.group(2)
        inner, _ = balanced(body, vm.end() - 1)
        args = split_top(inner)
        name, value = None, None
        if args and args[0].startswith('"'):
            name = args[0].strip('"')
            rest = args[1:]
        else:
            rest = args
            if kind == 'PowerVar':
                name = generic
            else:
                name = DEFAULT_NAME.get(kind)
        if name is None or kind == 'StringVar':  # a StringVar is text (a card / relic name), not a number
            continue
        if kind not in NO_VALUE and rest:
            value = ev(cls, rest[0]) if kind != 'BoolVar' else (1.0 if rest[0] == 'true' else 0.0)
        out[name] = value
    return out


KEYWORDS = ('Exhaust', 'Unplayable', 'Ethereal', 'Innate', 'Retain', 'Sly', 'Eternal')


def csharp_card(name):
    text = class_text(MODELS['card'], name)
    if text is None:
        return None
    cls = Cls(text)
    add_fields(cls, text)
    info = {}
    m = re.search(r'public %s\(\)\s*:\s*base\(([^;{]*)\)' % name, text)
    if m:
        a = split_top(m.group(1))
        info['cost'] = int(ev(cls, a[0])) if ev(cls, a[0]) is not None else None
        info['type'] = a[1].split('.')[-1]
        info['rarity'] = a[2].split('.')[-1]
        info['target'] = a[3].split('.')[-1]
    km = re.search(r'CanonicalKeywords\s*=>([^;]*);', text)
    kws = set(re.findall(r'CardKeyword\.(\w+)', km.group(1))) if km else set()
    info['kw'] = kws
    info['x'] = 1 if re.search(r'override bool HasEnergyCostX\s*=>\s*true', text) else 0
    sm = re.search(r'override int CanonicalStarCost\s*=>\s*(-?\d+)', text)
    info['star'] = int(sm.group(1)) if sm else -1
    info['gen'] = 0 if re.search(r'override bool CanBeGeneratedInCombat\s*=>\s*false', text) else 1
    mm = re.search(r'override int MaxUpgradeLevel\s*=>\s*(\d+)', text)
    info['maxup'] = int(mm.group(1)) if mm else 1
    info['vars'] = parse_vars(text, cls)
    up = dict(info['vars'])
    upkw = set(kws)
    upcost = info.get('cost')
    um = re.search(r'void OnUpgrade\(\)\s*\{', text)
    if um:
        body, _ = balanced(text.replace('{', '(').replace('}', ')'), um.end() - 1)
        for line in body.split(';'):
            line = line.strip()
            vm = re.search(r'DynamicVars(?:\.(\w+)|\["(\w+)"\])\.UpgradeValueBy\((.+)\)$', line)
            if vm:
                key = vm.group(2) or ACCESSOR.get(vm.group(1), vm.group(1))
                d = ev(cls, vm.group(3))
                if key in up and up[key] is not None and d is not None:
                    up[key] = up[key] + d
                elif d is not None:
                    up[key] = None
                continue
            cm = re.search(r'EnergyCost\.UpgradeBy\((.+)\)$', line)
            if cm and upcost is not None:
                d = ev(cls, cm.group(1))
                upcost = upcost + int(d) if d is not None else None
                continue
            am = re.search(r'(Add|Remove)Keyword\(CardKeyword\.(\w+)\)', line)
            if am:
                (upkw.add if am.group(1) == 'Add' else upkw.discard)(am.group(2))
    info['upvars'] = up
    info['upkw'] = upkw
    info['upcost'] = upcost
    return info


def csharp_simple(kind, name):
    text = class_text(MODELS[kind], ALIAS.get(name, name))
    if text is None:
        return None
    cls = Cls(text)
    add_fields(cls, text)
    info = {'vars': parse_vars(text, cls)}
    for prop in ('Rarity', 'Usage', 'TargetType'):
        m = re.search(r'override \w+ %s\s*=>\s*\w+\.(\w+)' % prop, text)
        if m:
            info[prop] = m.group(1)
    return info


def read_dump():
    cards, relics, potions = {}, {}, {}
    cur = None
    for line in open(DUMP, encoding='utf-8'):
        p = line.split()
        if not p:
            continue
        if p[0] == 'CARD':
            kv = dict(zip(p[5::2], p[6::2]))
            cur = cards[p[1]] = dict(type=p[2], rarity=p[3], target=p[4], cost=int(kv['cost']), upcost=int(kv['upcost']),
                                     x=int(kv['x']), star=int(kv['star']), maxup=int(kv['maxup']), gen=int(kv['gen']),
                                     kw=set() if kv['kw'] == '-' else set(kv['kw'].split(',')),
                                     upkw=set() if kv['upkw'] == '-' else set(kv['upkw'].split(',')), vars={}, upvars={})
        elif p[0] == 'RELIC':
            cur = relics[p[1]] = dict(Rarity=p[2], vars={}, upvars={})
        elif p[0] == 'POTION':
            cur = potions[p[1]] = dict(Rarity=p[2], Usage=p[3], TargetType=p[4], vars={}, upvars={})
        elif p[0] == 'VAR':
            cur['vars'][p[2]] = float(p[3])
            cur['upvars'][p[2]] = float(p[4])
    return cards, relics, potions


def card_pools():
    pools = {}
    for path in glob.glob(os.path.join(DEC, 'MegaCrit.Sts2.Core.Models.CardPools', '*CardPool.cs')):
        pool = os.path.basename(path)[:-len('CardPool.cs')]
        for n in re.findall(r'ModelDb\.Card<(\w+)>', open(path, encoding='utf-8').read()):
            pools.setdefault(n, pool)
    return pools


def relic_pools():
    pools = {}
    for path in glob.glob(os.path.join(DEC, 'MegaCrit.Sts2.Core.Models.RelicPools', '*RelicPool.cs')):
        pool = os.path.basename(path)[:-len('RelicPool.cs')]
        for n in re.findall(r'ModelDb\.Relic<(\w+)>', open(path, encoding='utf-8').read()):
            pools.setdefault(n, pool)
    return pools


def cmp_vars(key, exp, got, out, prefix='var'):
    for n, v in exp.items():
        if n not in got:
            if prefix == 'var':
                out.append(('%s.vars-%s' % (key, n), 'C# var %s=%s missing' % (n, fmt(v))))
            continue
        if v is not None and abs(v - got[n]) > 1e-6:
            out.append(('%s.%s.%s' % (key, prefix, n), 'C# %s ours %s' % (fmt(v), fmt(got[n]))))
    if prefix == 'var':
        for n in got:
            if n not in exp:
                out.append(('%s.vars+%s' % (key, n), 'port-only var %s=%s' % (n, fmt(got[n]))))


def compare():
    cards, relics, potions = read_dump()
    cpools, rpools = card_pools(), relic_pools()
    found = []  # (category, key, text)
    for name, g in sorted(cards.items()):
        e = csharp_card(name)
        cat = 'card/' + cpools.get(name, 'Other')
        if e is None:
            found.append((cat, 'card %s.missing' % name, 'no C# class'))
            continue
        out = []
        for f in ('type', 'target', 'cost', 'upcost', 'x', 'star', 'maxup', 'gen'):
            ev_, gv = e.get(f), g[f]
            if f == 'target':
                ev_ = TARGET_EQUIV.get(ev_, ev_)
            if ev_ is not None and ev_ != gv:
                out.append(('%s.%s' % (name, f), 'C# %s ours %s' % (ev_, gv)))
        if e.get('rarity') and e['rarity'] != g['rarity'] and g['rarity'] not in RARITY_EQUIV.get(e['rarity'], ()):
            out.append(('%s.rarity' % name, 'C# %s ours %s' % (e['rarity'], g['rarity'])))
        for f in ('kw', 'upkw'):
            if e[f] != g[f]:
                out.append(('%s.%s' % (name, f), 'C# %s ours %s' % (','.join(sorted(e[f])) or '-', ','.join(sorted(g[f])) or '-')))
        cmp_vars(name, e['vars'], g['vars'], out)
        cmp_vars(name, {k: v for k, v in e['upvars'].items() if k in e['vars']}, g['upvars'], out, 'upvar')
        found += [(cat, 'card ' + k, t) for k, t in out]
    for kind, table in (('relic', relics), ('potion', potions)):
        for name, g in sorted(table.items()):
            e = csharp_simple(kind, name)
            cat = kind + ('/' + rpools.get(name, 'Other') if kind == 'relic' else '')
            if e is None:
                found.append((cat, '%s %s.missing' % (kind, name), 'no C# class'))
                continue
            out = []
            for f in ('Rarity', 'Usage', 'TargetType'):
                if f in e and f in g:
                    ev_ = TARGET_EQUIV.get(e[f], e[f]) if f == 'TargetType' else e[f]
                    if ev_ != g[f]:
                        out.append(('%s.%s' % (name, f.lower()), 'C# %s ours %s' % (ev_, g[f])))
            cmp_vars(name, e['vars'], g['vars'], out)
            found += [(cat, kind + ' ' + k, t) for k, t in out]
    return found, len(cards), len(relics), len(potions)


def read_allow():
    allow = {}
    if os.path.exists(ALLOW):
        for line in open(ALLOW, encoding='utf-8'):
            s = line.strip()
            if not s or s.startswith('#'):
                continue
            key, _, reason = s.partition('#')
            allow[' '.join(key.split())] = reason.strip()
    return allow


def main():
    if not os.path.exists(DUMP):
        print('balance_check: %s missing (./build/model_dump > build/model_dump.txt)' % DUMP)
        return 1
    found, nc, nr, np_ = compare()
    allow = read_allow()
    if '--summary' in sys.argv:
        counts = {}
        for cat, key, _ in found:
            c = counts.setdefault(cat, [0, 0])
            c[1 if any(fnmatch.fnmatchcase(key, k) for k in allow) else 0] += 1
        for cat in sorted(counts):
            print('%-24s unexplained %3d  allowlisted %3d' % (cat, *counts[cat]))
    def allowed(key):
        return next((k for k in allow if fnmatch.fnmatchcase(key, k)), None)
    used = set()
    for cat, key, _ in found:
        if allowed(key):
            used.add(allowed(key))
    bad = 0
    for cat, key, text in found:
        if allowed(key):
            if '--all' in sys.argv:
                print('allowed  %-44s %s  (%s)' % (key, text, allow[allowed(key)]))
            continue
        bad += 1
        if '--summary' not in sys.argv:
            print('%-50s %s' % (key, text))
    stale = [k for k in allow if k not in used]
    for k in stale:
        print('stale allowlist entry:', k)
    print('balance_check: %d cards, %d relics, %d potions; mismatches: %d unexplained, %d allowlisted%s' % (
        nc, nr, np_, bad, len(found) - bad, ', %d stale' % len(stale) if stale else ''))
    return 1 if bad or stale else 0


if __name__ == '__main__':
    sys.exit(main())
