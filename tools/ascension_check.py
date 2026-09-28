#!/usr/bin/env python3
"""Compares our monsters' HP and attack intents at an ascension level with the C# (C10).

  make -f Makefile.sdl build/ascension_dump   # then: for l in 0 8 9 10; do ./build/ascension_dump $l > build/asc_dump_$l.txt; done
  python tools/ascension_check.py             # prints every mismatch; exit code 1 if any
Only what can be read from the C# source is compared: MinInitialHp / MaxInitialHp and the
Single/MultiAttackIntent / StatusIntent numbers of each MoveState.
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEC = os.path.join(os.path.dirname(ROOT), 'sts2-decompiled', 'MegaCrit.Sts2.Core.Models.Monsters')
LEVELS = {'None': 0, 'SwarmingElites': 1, 'WearyTraveler': 2, 'Poverty': 3, 'TightBelt': 4, 'AscendersBane': 5,
          'Inflation': 6, 'Scarcity': 7, 'ToughEnemies': 8, 'DeadlyEnemies': 9, 'DoubleBoss': 10}


def balanced(text, start):
    """text[start] is '(' ; returns (inner, end index after ')')."""
    depth, i = 0, start
    while i < len(text):
        if text[i] == '(':
            depth += 1
        elif text[i] == ')':
            depth -= 1
            if depth == 0:
                return text[start + 1:i], i + 1
        i += 1
    return text[start + 1:], len(text)


def split_top(s):
    args, depth, cur = [], 0, ''
    for ch in s:
        if ch in '([{<':
            depth += 1
        elif ch in ')]}>':
            depth -= 1
        if ch == ',' and depth == 0:
            args.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        args.append(cur.strip())
    return args


class Cls:
    def __init__(self, text):
        self.text = text
        self.syms = {}
        for m in re.finditer(r'(?:public|private|protected)\s+(?:static\s+)?(?:override\s+)?(?:readonly\s+)?(?:int|float|decimal)\s+(\w+)\s*=>\s*([^;]+);', text):
            self.syms[m.group(1)] = m.group(2).strip()
        for m in re.finditer(r'const\s+(?:int|float)\s+(\w+)\s*=\s*([^;]+);', text):
            self.syms[m.group(1)] = m.group(2).strip()

    def ev(self, expr, level, depth=0):
        if depth > 8:
            raise ValueError('deep')
        expr = expr.strip()
        m = re.fullmatch(r'AscensionHelper\.GetValueIfAscension\(AscensionLevel\.(\w+),\s*(.+),\s*(.+)\)', expr, re.S)
        if m:
            parts = split_top(expr[expr.index('(') + 1:-1])
            return self.ev(parts[1], level, depth + 1) if level >= LEVELS[m.group(1)] else self.ev(parts[2], level, depth + 1)

        def sub(mm):
            name = mm.group(0)
            if name in self.syms:
                return '(%s)' % self.ev(self.syms[name], level, depth + 1)
            raise ValueError(name)
        e = re.sub(r'AscensionHelper\.GetValueIfAscension\(AscensionLevel\.(\w+),', lambda mm: 'GVA(%d,' % LEVELS[mm.group(1)], expr)
        e = re.sub(r'\b[A-Za-z_]\w*\b', lambda mm: mm.group(0) if mm.group(0) in ('GVA', 'int', 'Math', 'Min', 'Max') else sub(mm), e)
        e = re.sub(r'\(int\)', 'int', e)
        e = e.replace('Math.Min', 'min').replace('Math.Max', 'max')
        e = re.sub(r'(\d)m\b', r'\1', e)
        e = re.sub(r'(\d)f\b', r'\1', e)
        val = eval(e, {'GVA': (lambda l, a, b: a if level >= l else b), 'int': int, 'min': min, 'max': max})
        return val


def expected(level):
    out = {}
    for path in sorted(glob.glob(os.path.join(DEC, '*.cs'))):
        name = os.path.basename(path)[:-3]
        text = open(path, encoding='utf-8').read()
        base = re.search(r'class %s\s*:\s*(\w+)' % name, text)
        if base and os.path.exists(os.path.join(DEC, base.group(1) + '.cs')):  # shared base class (Decimillipede)
            text += open(os.path.join(DEC, base.group(1) + '.cs'), encoding='utf-8').read()
        c = Cls(text)
        info = {'moves': {}}
        for key in ('MinInitialHp', 'MaxInitialHp'):
            try:
                info[key] = int(c.ev(c.syms[key], level))
            except Exception:
                pass
        for m in re.finditer(r'new MoveState\(', text):
            inner, _ = balanced(text, m.end() - 1)
            args = split_top(inner)
            if len(args) < 3:
                continue
            mid = args[0].strip('"')
            desc = []
            ok = True
            for a in args[2:]:
                mm = re.match(r'new (SingleAttackIntent|MultiAttackIntent|StatusIntent)\((.*)\)$', a, re.S)
                if not mm:
                    continue
                parts = split_top(mm.group(2))
                try:
                    if mm.group(1) == 'SingleAttackIntent':
                        desc.append('A%dx1' % c.ev(parts[0], level))
                    elif mm.group(1) == 'MultiAttackIntent':
                        desc.append('A%dx%d' % (c.ev(parts[0], level), c.ev(parts[1], level)))
                    else:
                        desc.append('S%d' % c.ev(parts[0], level))
                except Exception:
                    ok = False
            if ok:
                info['moves'][mid] = desc
        out[name] = info
    return out


def ours(level):
    path = os.path.join(ROOT, 'build', 'asc_dump_%d.txt' % level)
    mons = {}
    for line in open(path, encoding='utf-8'):
        p = line.split()
        if p[0] == 'MONSTER':
            mons[p[1]] = {'hp': (int(p[3]), int(p[4])), 'moves': {}}
        elif p[0] == 'MOVE':
            mons[p[1]]['moves'][p[2]] = p[3:]
    return mons


def main():
    bad = 0
    for level in (0, 8, 9, 10):
        exp, got = expected(level), ours(level)
        for name, g in got.items():
            e = exp.get(name)
            if not e:
                continue
            if 'MinInitialHp' in e and 'MaxInitialHp' in e and (e['MinInitialHp'], e['MaxInitialHp']) != g['hp']:
                print('A%d %s hp: C# %d-%d ours %d-%d' % (level, name, e['MinInitialHp'], e['MaxInitialHp'], *g['hp']))
                bad += 1
            for mid, desc in g['moves'].items():
                if mid in e['moves'] and e['moves'][mid] != desc:
                    print('A%d %s %s: C# %s ours %s' % (level, name, mid, ' '.join(e['moves'][mid]), ' '.join(desc)))
                    bad += 1
    print('mismatches:', bad)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
