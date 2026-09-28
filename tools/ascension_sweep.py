#!/usr/bin/env python3
"""Lists every AscensionHelper.GetValueIfAscension in the decompiled monsters (C10 sweep).

python tools/ascension_sweep.py            # summary of the properties and how the C# uses them
python tools/ascension_sweep.py --json     # machine readable
"""
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEC = os.path.join(os.path.dirname(ROOT), 'sts2-decompiled', 'MegaCrit.Sts2.Core.Models.Monsters')
PROP = re.compile(r'(?:public|private|protected)\s+(?:static\s+)?(?:override\s+)?(int|float|decimal|bool)\s+(\w+)\s*=>\s*AscensionHelper\.GetValueIfAscension\(AscensionLevel\.(\w+),\s*([^,]+),\s*([^)]+)\)\s*;')
ANY = re.compile(r'GetValueIfAscension|HasAscension')


def parse(path):
    text = open(path, encoding='utf-8').read()
    name = os.path.basename(path)[:-3]
    props, uses = [], {}
    for m in PROP.finditer(text):
        typ, pname, level, a, b = m.groups()
        props.append(dict(name=pname, type=typ, level=level, asc=a.strip(), base=b.strip(), static=' static ' in m.group(0)))
    covered = len(props)
    total = len(ANY.findall(text))
    for p in props:
        found = []
        for line in text.split('\n'):
            if re.search(r'\b%s\b' % p['name'], line) and 'GetValueIfAscension' not in line:
                found.append(line.strip())
        p['uses'] = found
    return name, props, total - covered


def main():
    out = {}
    odd = {}
    for path in sorted(glob.glob(os.path.join(DEC, '*.cs'))):
        name, props, rest = parse(path)
        if props:
            out[name] = props
        if rest:
            odd[name] = rest
    if '--json' in sys.argv:
        print(json.dumps(dict(monsters=out, other=odd), indent=1))
        return
    print('monsters with ascension values:', len(out), ' properties:', sum(len(v) for v in out.values()))
    print('uses not in a plain property (need a manual look):', odd)


if __name__ == '__main__' and not any(a in sys.argv for a in ('--apply', '--dry', '--fix-max', '--leftovers')):
    main()


# ---------------------------------------------------------------- apply to our C++

LEVELS = {'ToughEnemies': 'kToughEnemies', 'DeadlyEnemies': 'kDeadlyEnemies'}


def categories(p):
    """How the C# uses a property: [(kind, detail)]."""
    cats = []
    name = p['name']
    if name in ('MinInitialHp', 'MaxInitialHp'):
        return [('hp', name)]
    for line in p['uses']:
        if re.search(r'(Single|Multi)AttackIntent\(\s*%s\b' % name, line) or re.search(r'DamageCmd\.Attack\(\s*%s\b' % name, line):
            cats.append(('dmg', ''))
        if re.search(r'MultiAttackIntent\([^,()]+,\s*%s\s*\)' % name, line) or re.search(r'WithHitCount\(\s*%s\s*\)' % name, line):
            cats.append(('hits', ''))
        if re.search(r'GainBlock\([^,]+,\s*%s\b' % name, line):
            cats.append(('block', ''))
        m = re.search(r'Apply<(\w+)>\([^;]*\b%s\b' % name, line)
        if m:
            cats.append(('power', m.group(1)))
    out = []
    for c in cats:
        if c not in out:
            out.append(c)
    return out


def struct_span(text, name):
    m = re.search(r'\bstruct\s+%s\s*:[^{;]*\{' % name, text)
    if not m:
        return None
    depth, i = 0, m.end() - 1
    while i < len(text):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return m.start(), i + 1
        i += 1
    return None


def split_args(s):
    args, depth, cur = [], 0, ''
    for ch in s:
        if ch in '([{<':
            depth += 1
        elif ch in ')]}>':
            depth -= 1
        if ch == ',' and depth == 0:
            args.append(cur)
            cur = ''
        else:
            cur += ch
    args.append(cur)
    return args


def replace_call_arg(text, func_re, arg_index, base, repl):
    """In every call matching func_re(...) replace the argument at arg_index if it is exactly `base`."""
    out, pos, n = '', 0, 0
    for m in re.finditer(func_re + r'\(', text):
        start = m.end()
        depth, i = 1, start
        while i < len(text) and depth:
            depth += (text[i] == '(') - (text[i] == ')')
            i += 1
        inner = text[start:i - 1]
        args = split_args(inner)
        if arg_index < len(args) and args[arg_index].strip() == base:
            lead = args[arg_index][:len(args[arg_index]) - len(args[arg_index].lstrip())]
            args[arg_index] = lead + repl
            out += text[pos:start] + ','.join(args)
            pos = i - 1
            n += 1
    out += text[pos:]
    return out, n


def apply_all(dry):
    ours = {}
    for f in sorted(glob.glob(os.path.join(ROOT, 'source', 'core', '*.cpp'))):
        for m in re.finditer(r'MONSTER_HEADER\((\w+),', open(f, encoding='utf-8').read()):
            ours[m.group(1)] = f
    report = []
    files = {}
    for path in sorted(glob.glob(os.path.join(DEC, '*.cs'))):
        name, props, odd = parse(path)
        if '--only-static' in sys.argv:
            props = [p for p in props if p['static']]
        if not props or name not in ours:
            continue
        f = ours[name]
        text = files.get(f) or open(f, encoding='utf-8').read()
        span = struct_span(text, name)
        if not span:
            report.append((name, 'STRUCT NOT FOUND'))
            continue
        body = text[span[0]:span[1]]
        # ambiguity: two int props of one category with the same base but a different ascension value
        seen = {}
        for p in props:
            for c in categories(p):
                seen.setdefault((c, p['base']), set()).add((p['asc'], p['level']))
        for p in props:
            if p['type'] != 'int' or not re.fullmatch(r'-?\d+', p['base']) or not re.fullmatch(r'-?\d+', p['asc']):
                report.append((name, 'non-int %s' % p['name']))
                continue
            lvl = LEVELS[p['level']]
            expr = 'asc(%s, %s, %s)' % (lvl, p['asc'], p['base'])
            cats = categories(p)
            if not cats:
                report.append((name, 'no category: %s = %s/%s (%s)' % (p['name'], p['asc'], p['base'], '; '.join(p['uses'][:2]))))
                continue
            for c in cats:
                if len(seen[(c, p['base'])]) > 1:
                    report.append((name, 'ambiguous %s %s (base %s)' % (c[0], p['name'], p['base'])))
                    continue
                n = 0
                if c[0] == 'hp':
                    fn = 'minHp' if p['name'] == 'MinInitialHp' else 'maxHp'
                    body2, n = re.subn(r'(int %s\(\) const override \{ return )%s;' % (fn, re.escape(p['base'])), lambda m: m.group(1) + expr + ';', body)
                    body = body2
                elif c[0] == 'dmg':
                    for fr in (r'\battackIntent', r'\battack', r'\battackAll'):
                        body, k = replace_call_arg(body, fr, 0, p['base'], expr)
                        n += k
                elif c[0] == 'hits':
                    for fr in (r'\battackIntent', r'\battack'):
                        body, k = replace_call_arg(body, fr, 1, p['base'], expr)
                        n += k
                elif c[0] == 'block':
                    body, n = replace_call_arg(body, r'\bgainBlock', 0, p['base'], expr)
                elif c[0] == 'power':
                    t = re.escape(c[1])
                    body, k = replace_call_arg(body, r'\bapplyToSelf<%s>' % t, 0, p['base'], expr)
                    n += k
                    body, k = replace_call_arg(body, r'\bapplyToTargets<%s>' % t, 1, p['base'], expr)
                    n += k
                    body, k = replace_call_arg(body, r'\bapplyPower<%s>' % t, 1, p['base'], expr)
                    n += k
                if n == 0:
                    report.append((name, 'NOT FOUND %s %s: %s/%s %s' % (c[0], p['name'], p['asc'], p['base'], c[1])))
        text = text[:span[0]] + body + text[span[1]:]
        files[f] = text
    for f, text in files.items():
        if not dry:
            open(f, 'w', encoding='utf-8', newline='').write(text)
    return report


if '--apply' in sys.argv or '--dry' in sys.argv:
    for r in apply_all('--dry' in sys.argv):
        print(*r)


def fix_max_hp():
    """MaxInitialHp => MinInitialHp in the C#: ours returns the same value from maxHp()."""
    n = 0
    for path in sorted(glob.glob(os.path.join(DEC, '*.cs'))):
        name = os.path.basename(path)[:-3]
        text = open(path, encoding='utf-8').read()
        if not re.search(r'int MaxInitialHp\s*=>\s*MinInitialHp\s*;', text):
            continue
        for f in glob.glob(os.path.join(ROOT, 'source', 'core', '*.cpp')):
            src = open(f, encoding='utf-8').read()
            span = struct_span(src, name)
            if not span:
                continue
            body = src[span[0]:span[1]]
            new = re.sub(r'(int maxHp\(\) const override \{ return )\d+;', lambda m: m.group(1) + 'minHp();', body)
            if new != body:
                open(f, 'w', encoding='utf-8', newline='').write(src[:span[0]] + new + src[span[1]:])
                n += 1
    return n


if '--fix-max' in sys.argv:
    print('maxHp fixed in', fix_max_hp(), 'monsters')


def leftovers():
    """Literals still equal to the non-ascension value of a property, in calls that could take it."""
    ours = {}
    for f in sorted(glob.glob(os.path.join(ROOT, 'source', 'core', '*.cpp'))):
        for m in re.finditer(r'MONSTER_HEADER\((\w+),', open(f, encoding='utf-8').read()):
            ours[m.group(1)] = f
    calls = r'\b(attackIntent|attack|attackAll|gainBlock|applyToSelf<\w+>|applyToTargets<\w+>|applyPower<\w+>|addStatusCards|kindIntent)\('
    for path in sorted(glob.glob(os.path.join(DEC, '*.cs'))):
        name, props, odd = parse(path)
        if not props or name not in ours:
            continue
        text = open(ours[name], encoding='utf-8').read()
        span = struct_span(text, name)
        if not span:
            continue
        body = text[span[0]:span[1]]
        bases = {p['base']: p for p in props if re.fullmatch(r'-?\d+', p['base'])}
        for m in re.finditer(calls, body):
            start = m.end()
            depth, i = 1, start
            while i < len(body) and depth:
                depth += (body[i] == '(') - (body[i] == ')')
                i += 1
            for a in split_args(body[start:i - 1]):
                a = a.strip()
                if a in bases:
                    print(name, m.group(1), a, '<- %s %s/%s' % (bases[a]['name'], bases[a]['asc'], bases[a]['base']))


if '--leftovers' in sys.argv:
    leftovers()
