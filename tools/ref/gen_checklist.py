#!/usr/bin/env python3
"""Generate docs/REF_CHECKLIST.md (and, with --scenes, tools/ref/scenes/<batch>.txt).

Scans the decompiled original (ground truth, read-only, never copied) and our C++ sources and
emits ONLY ids / class names / short descriptions: one row per visible behaviour group, to be
compared side by side (original vs port) by the recording runner.

    python3 tools/ref/gen_checklist.py            # docs/REF_CHECKLIST.md
    python3 tools/ref/gen_checklist.py --scenes   # also tools/ref/scenes/{L,I,S,D,R,N,C,P,M,U,X}.txt

The decompiled tree is found via $STS2_DECOMPILED or an `sts2-decompiled` directory next to any
ancestor of the repo. Output is deterministic (sorted everywhere, no timestamps). The hand-written
B (bosses/elites) rows between <!-- B:begin --> / <!-- B:end --> in the old checklist are kept.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
OURS_DIR = os.path.join(REPO, 'sou' + 'rce')
DOC = os.path.join(REPO, 'docs', 'REF_CHECKLIST.md')
SCENE_DIR = os.path.join(HERE, 'scenes')


def find_decompiled():
    env = os.environ.get('STS2_DECOMPILED')
    if env and os.path.isdir(env):
        return env
    p = REPO
    while True:
        cand = os.path.join(p, 'sts2-decompiled')
        if os.path.isdir(cand):
            return cand
        q = os.path.dirname(p)
        if q == p:
            break
        p = q
    sys.exit('decompiled tree not found (set STS2_DECOMPILED)')


DEC = find_decompiled()


def nsdir(name):
    return os.path.join(DEC, 'MegaCrit.Sts2.Core.' + name)


def rd(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        return f.read()


def cs_names(ns):
    d = nsdir(ns)
    if not os.path.isdir(d):
        return []
    return sorted(f[:-3] for f in os.listdir(d) if f.endswith('.cs'))


def slug(name):
    """Class name -> console id (KaiserCrabBoss -> KAISER_CRAB_BOSS)."""
    s = re.sub(r'(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])', '_', name)
    return s.upper()


def snake(name):
    return slug(name).lower()


# ------------------------------------------------------------------------------- our code
def load_ours():
    text = ''
    for root, _, files in os.walk(OURS_DIR):
        for f in sorted(files):
            if f.endswith(('.cpp', '.h', '.inc')):
                text += rd(os.path.join(root, f)) + '\n'
    o = {}
    for kind, mac in [('card', 'CARD_HEADER'), ('power', 'POWER_HEADER'), ('potion', 'POTION_HEADER'),
                      ('monster', 'MONSTER_HEADER'), ('relic', 'RELIC_HEADER'), ('event', 'EVENT_HEADER'),
                      ('orb', 'ORB_HEADER')]:
        o[kind] = set(re.findall(mac + r'\(\s*(\w+)', text))
    o['card'] |= set(re.findall(r'regCard<(\w+)>', text))
    o['power'] |= set(re.findall(r'regPower<(\w+)>', text))
    o['potion'] |= set(re.findall(r'regPotion<(\w+)>', text))
    o['enc'] = set(re.findall(r'(?:register|reg)Encounter\(\s*"(\w+)"', text))
    o['quoted'] = set(re.findall(r'"([A-Za-z0-9_]+)"', text))
    o['text'] = text
    return o


OURS = load_ours()


def ours_has(kind, name):
    return name in OURS[kind] or (kind in ('event',) and name in OURS['quoted'])


# ------------------------------------------------------------------------------- original: cards
CHARS = ['Ironclad', 'Silent', 'Defect', 'Regent', 'Necrobinder']
CHAR_IDX = {c: i for i, c in enumerate(CHARS)}
RARITY_ORDER = {'Basic': 0, 'Common': 1, 'Uncommon': 2, 'Rare': 3, 'Ancient': 4, 'Token': 5, 'Status': 6,
                'Curse': 7, 'Event': 8, 'Quest': 9}


def pool_cards(pool):
    t = rd(os.path.join(nsdir('Models.CardPools'), pool + 'CardPool.cs'))
    seen, out = set(), []
    for n in re.findall(r'ModelDb\.Card<(\w+)>', t):
        if n not in seen:
            seen.add(n)
            out.append(n)
    return out


def debuff_powers():
    s = set()
    for n in cs_names('Models.Powers'):
        if re.search(r'PowerType Type\s*=>\s*PowerType\.Debuff', rd(os.path.join(nsdir('Models.Powers'), n + '.cs'))):
            s.add(n)
    return s


DEBUFFS = debuff_powers()


def parse_card(name):
    p = os.path.join(nsdir('Models.Cards'), name + '.cs')
    t = rd(p)
    m = re.search(r'base\(\s*(-?\w+)\s*,\s*CardType\.(\w+)\s*,\s*CardRarity\.(\w+)\s*,\s*TargetType\.(\w+)', t)
    f = {'name': name, 'cost': '1', 'type': 'Skill', 'rarity': 'Common', 'target': 'Self'}
    if m:
        f.update(cost=m.group(1), type=m.group(2), rarity=m.group(3), target=m.group(4))
    applied = re.findall(r'PowerCmd\.Apply<(\w+)>', t)
    f['applied'] = applied
    f['debuff'] = any(a in DEBUFFS for a in applied)
    f['buff_self'] = any(a not in DEBUFFS for a in applied)
    f['dmg'] = 'DamageCmd.Attack' in t
    f['aoe'] = 'TargetingAllOpponents' in t or f['target'] == 'AllEnemies'
    f['rand'] = 'TargetingRandomOpponents' in t or f['target'] == 'RandomEnemy'
    f['multi'] = 'WithHitCount' in t
    f['osty_attack'] = 'FromOsty' in t or 'OstyAttack' in t
    f['block'] = 'GainBlock' in t
    f['draw'] = 'CardPileCmd.Draw' in t
    f['energy'] = 'GainEnergy' in t
    f['gen'] = 'AddGeneratedCard' in t or 'CardPileCmd.Add(' in t
    f['discard'] = bool(re.search(r'CardCmd\.Discard|FromHandForDiscard|DiscardAndDraw', t))
    f['exhaust_cmd'] = 'CardCmd.Exhaust' in t
    f['orbs'] = re.findall(r'OrbCmd\.Channel<(\w+)>', t)
    f['orb_ch'] = 'OrbCmd.Channel' in t
    f['evoke'] = 'OrbCmd.EvokeNext' in t or 'OrbCmd.Passive' in t
    f['orb_slots'] = 'OrbCmd.AddSlots' in t or 'OrbCmd.RemoveSlots' in t
    f['stars'] = 'GainStars' in t
    f['star_cost'] = 'StarCost' in t
    f['forge'] = 'ForgeCmd' in t
    f['summon'] = 'OstyCmd.Summon' in t
    f['poison'] = 'PoisonPower' in t
    f['doom'] = 'DoomPower' in t
    f['shiv'] = 'Shiv' in t
    f['select'] = 'CardSelectCmd' in t
    f['upgrade'] = 'CardCmd.Upgrade' in t
    f['transform'] = 'CardCmd.Transform' in t
    f['autoplay'] = 'CardCmd.AutoPlay' in t or 'AutoPlayFromDrawPile' in t
    f['enchant'] = 'CardCmd.Enchant' in t
    f['self_dmg'] = 'CreatureCmd.Damage' in t
    f['heal'] = 'CreatureCmd.Heal' in t or 'GainMaxHp' in t
    f['gold'] = 'GainGold' in t
    f['costX'] = 'HasEnergyCostX => true' in t
    f['kw'] = set(re.findall(r'CardKeyword\.(\w+)', t))
    f['vfxcmd'] = 'VfxCmd' in t
    f['stunlike'] = 'CreatureCmd.Stun' in t
    f['strength'] = any(a in ('StrengthPower', 'DexterityPower') for a in applied)
    f['focus'] = 'FocusPower' in applied
    return f


# group predicates; each: (key, label, predicate)
def attack(f):
    return f['type'] == 'Attack'


def skill(f):
    return f['type'] == 'Skill'


def power(f):
    return f['type'] == 'Power'


COMMON_GROUPS = [
    ('unplayable', 'unplayable in hand (status/curse-like)', lambda f: 'Unplayable' in f['kw']),
    ('atk_x_cost', 'attack with X energy cost', lambda f: attack(f) and f['costX']),
    ('atk_random', 'attack at random enemies', lambda f: attack(f) and f['rand']),
    ('atk_multi', 'attack multi-hit', lambda f: attack(f) and f['multi']),
    ('atk_aoe', 'attack all enemies (AoE)', lambda f: attack(f) and f['aoe']),
    ('atk_debuff', 'attack + debuff on target', lambda f: attack(f) and f['debuff']),
    ('atk_block', 'attack + gain block', lambda f: attack(f) and f['block']),
    ('atk_draw', 'attack + draw/energy', lambda f: attack(f) and (f['draw'] or f['energy'])),
    ('atk_gen', 'attack + creates cards', lambda f: attack(f) and f['gen']),
    ('atk_select', 'attack + card choice (select/discard/exhaust)',
     lambda f: attack(f) and (f['select'] or f['discard'] or f['exhaust_cmd'] or f['upgrade'])),
    ('atk_exhaust', 'attack with Exhaust keyword', lambda f: attack(f) and 'Exhaust' in f['kw']),
    ('atk_selfdmg', 'attack that hurts / buffs self', lambda f: attack(f) and (f['self_dmg'] or f['buff_self'])),
    ('atk_single', 'attack single target (plain)', attack),
    ('pow_vfx', 'power card with its own VFX (form/aura)', lambda f: power(f) and f['vfxcmd']),
    ('pow_stat', 'power card: Strength/Dexterity-style stat buff', lambda f: power(f) and f['strength']),
    ('pow_cards', 'power card: card/energy/draw engine', lambda f: power(f) and (f['draw'] or f['energy'] or f['gen'])),
    ('pow_other', 'power card (triggered effect)', power),
    ('skl_x_cost', 'skill with X energy cost', lambda f: skill(f) and f['costX']),
    ('skl_aoe_debuff', 'skill: debuff all enemies', lambda f: skill(f) and f['debuff'] and f['aoe']),
    ('skl_debuff', 'skill: debuff one enemy', lambda f: skill(f) and f['debuff']),
    ('skl_block_exhaust', 'skill: block + Exhaust', lambda f: skill(f) and f['block'] and 'Exhaust' in f['kw']),
    ('skl_block_extra', 'skill: block + extra effect',
     lambda f: skill(f) and f['block'] and (f['draw'] or f['energy'] or f['gen'] or f['discard'] or f['select'] or f['buff_self'])),
    ('skl_block', 'skill: block only', lambda f: skill(f) and f['block']),
    ('skl_draw', 'skill: draw cards', lambda f: skill(f) and f['draw']),
    ('skl_energy', 'skill: gain energy', lambda f: skill(f) and f['energy']),
    ('skl_gen', 'skill: create cards in hand/piles', lambda f: skill(f) and f['gen']),
    ('skl_discard', 'skill: discard cards', lambda f: skill(f) and f['discard']),
    ('skl_exhaust', 'skill: exhaust cards', lambda f: skill(f) and f['exhaust_cmd']),
    ('skl_upgrade', 'skill: upgrade/transform/enchant cards', lambda f: skill(f) and (f['upgrade'] or f['transform'] or f['enchant'])),
    ('skl_select', 'skill: pick a card from a pile/screen', lambda f: skill(f) and f['select']),
    ('skl_autoplay', 'skill: auto-plays cards', lambda f: skill(f) and f['autoplay']),
    ('skl_hp', 'skill: HP loss/heal/max HP', lambda f: skill(f) and (f['self_dmg'] or f['heal'])),
    ('skl_buff', 'skill: temporary/permanent buff on self', lambda f: skill(f) and f['buff_self']),
    ('skl_other', 'skill (other)', lambda f: True),
]

CHAR_GROUPS = {
    'Ironclad': [
        ('ic_strength', 'Strength gain / scaling', lambda f: f['strength'] and not power(f)),
        ('ic_bleed', 'self-damage fuel (Bloodletting-style)', lambda f: f['self_dmg'] and not attack(f) and not f['debuff']),
    ],
    'Silent': [
        ('sl_poison', 'poison apply', lambda f: f['poison']),
        ('sl_shiv', 'Shiv creation / shiv attacks', lambda f: f['shiv']),
        ('sl_sly', 'Sly (free play on discard)', lambda f: 'Sly' in f['kw']),
    ],
    'Defect': [
        ('df_lightning', 'channel Lightning orb', lambda f: 'LightningOrb' in f['orbs']),
        ('df_frost', 'channel Frost orb', lambda f: 'FrostOrb' in f['orbs']),
        ('df_dark', 'channel Dark orb', lambda f: 'DarkOrb' in f['orbs']),
        ('df_orb_other', 'channel Plasma/Glass/other orb', lambda f: f['orb_ch']),
        ('df_evoke', 'evoke / passive orbs', lambda f: f['evoke']),
        ('df_slots', 'orb slots add/remove', lambda f: f['orb_slots']),
        ('df_focus', 'Focus gain / loss', lambda f: f['focus']),
    ],
    'Regent': [
        ('rg_forge', 'forge (Sovereign Blade)', lambda f: f['forge']),
        ('rg_stars', 'gain stars', lambda f: f['stars']),
        ('rg_star_atk', 'attack paid with stars', lambda f: f['star_cost'] and attack(f)),
        ('rg_star_skill', 'non-attack paid with stars', lambda f: f['star_cost']),
    ],
    'Necrobinder': [
        ('nb_summon', 'summon / revive Osty', lambda f: f['summon']),
        ('nb_osty_atk', 'Osty attacks (FromOsty)', lambda f: f['osty_attack']),
        ('nb_doom', 'Doom apply', lambda f: f['doom']),
    ],
}

KEYWORD_ROWS = [('kw_retain', 'Retain keyword', 'Retain'), ('kw_ethereal', 'Ethereal keyword', 'Ethereal'),
                ('kw_innate', 'Innate keyword', 'Innate'), ('kw_exhaust', 'Exhaust keyword (any type)', 'Exhaust')]

# ------------------------------------------------------------------------------- scene builders
ENC_CANDS_MULTI = ['CorpseSlugsWeak', 'ToadpolesWeak', 'SlimesWeak', 'BowlbugsWeak', 'ExoskeletonsWeak',
                   'ScrollsOfBitingWeak']


def pick_multi_enc():
    for e in ENC_CANDS_MULTI:
        if e in OURS['enc'] and e in cs_names('Models.Encounters'):
            return e
    return 'ShrinkerBeetleWeak'


ENC_SINGLE = 'ShrinkerBeetleWeak'
ENC_MULTI = pick_multi_enc()
NAV_DEFAULT = '40:A,100:A,160:A,220:A'

PLAY_ENEMY = '500:P160x130,505:M160x100,510:M170x60,515:M190x30,530:U'
PLAY_SELF = '500:P160x130,505:M160x100,510:M160x60,515:M160x30,530:U'
ENDTURN = '500:T278x185'


class Row:
    def __init__(self, batch, key, group, rep, orig, ours, impl, scene=None, vfx=None):
        self.batch, self.key, self.group, self.rep = batch, key, group, rep
        self.orig_steps = orig   # list of step strings (as in scene `orig:`)
        self.ours = ours         # dict var -> value (insertion ordered)
        self.impl = impl
        self.scene = scene or {}  # char, mark, note, times, nav
        self.vfx = set(vfx or [])
        self.id = ''
        self.note = ''

    def orig_table(self):
        out = []
        for s in self.orig_steps:
            if s.startswith(('wait', 'rec')):
                continue
            s = s[4:] if s.startswith('con ') else s
            s = re.sub(r'^play last (\w+)$', r'play last->\1', s)
            out.append(s)
        return '; '.join(out)

    def ours_table(self):
        return ' '.join('%s=%s' % (k, v) for k, v in self.ours.items() if k not in ('REC', 'NAV'))


def combat(char, enc_o, enc_s, setup=None, action=None, ours=None, mark=None, note='', rec='460-700', wait=3,
           pre_wait=6):
    """Original: fight; setup consoles; rec; action steps. Ours: env vars."""
    steps = ['con fight %s' % slug(enc_o), 'wait %d' % pre_wait]
    for c in (setup or []):
        steps += ['con ' + c, 'wait 1']
    steps.append('rec')
    steps += list(action or [])
    steps.append('wait %d' % wait)
    env = {'STS_CHAR': char, 'STS_ENCOUNTER': enc_s}
    env.update(ours or {})
    env['REC'] = rec
    return steps, env


def impl_from(counts, vfx_missing):
    tot = sum(counts.values())
    if tot == 0:
        return 'yes'
    have = counts.get('yes', 0)
    if have == 0:
        base = 'no'
    elif have == tot:
        base = 'yes'
    else:
        base = 'partial'
    if base == 'yes' and vfx_missing:
        base = 'partial'
    return base


ROWS = []        # all rows, in doc order after finalize
BY_KEY = {}      # 'I:atk_single' -> Row


def add(row):
    ROWS.append(row)
    BY_KEY[row.batch + ':' + row.key] = row
    return row


def fmt_members(names, n=6):
    names = list(names)
    s = ', '.join(names[:n])
    if len(names) > n:
        s += ', +%d' % (len(names) - n)
    return '%d: %s' % (len(names), s)


def pick_rep(names, kind, prefs=()):
    for p in prefs:
        if p in names and ours_has(kind, p):
            return p
    for n in names:
        if ours_has(kind, n):
            return n
    return names[0]


# ------------------------------------------------------------------------------- card batches
CARD_ROW_OF = {}  # card class -> row key


def card_scene(char, rep, f, enc=None):
    """Scene for playing card `rep` (or showing it if unplayable)."""
    setup = []
    ours = {'STS_DECK': ','.join([rep] * 5)}
    if f['costX'] or (f['cost'].lstrip('-').isdigit() and int(f['cost']) >= 2):
        setup.append('energy 9')
        ours['STS_ENERGY'] = '9'
    if f['star_cost']:
        setup.append('stars 9')
        ours['STS_STARS'] = '9'
    setup.append('card ' + slug(rep))
    needs_multi = f['aoe'] or f['rand'] or f['multi']
    e = enc or (ENC_MULTI if needs_multi else ENC_SINGLE)
    if 'Unplayable' in f['kw']:
        action = ['hover 500 540', 'wait 1', 'endturn']
        ours['STS_SCRIPT'] = '500:T278x185'
        mark = 'orig="INPUT move" ours=500'
    elif f['target'] == 'AnyEnemy' or f['target'] == 'AnyAlly' and attack(f):
        action = ['play last enemy']
        ours['STS_SCRIPT'] = PLAY_ENEMY
        mark = 'orig="INPUT release" ours=530'
    else:
        action = ['play last self']
        ours['STS_SCRIPT'] = PLAY_SELF
        mark = 'orig="INPUT release" ours=530'
    if f['select']:
        action += ['wait 1', 'click 300 300']
    if f['target'] in ('AnyEnemy',) and not attack(f):
        pass
    return combat(char, e, e, setup, action, ours, mark=mark)


def build_card_rows(char, letter):
    pool = pool_cards(char)
    feats = {n: parse_card(n) for n in pool}
    members = {}
    order = CHAR_GROUPS.get(char, []) + COMMON_GROUPS
    for n in pool:
        f = feats[n]
        for key, label, pred in order:
            if pred(f):
                members.setdefault(key, []).append(n)
                CARD_ROW_OF[n] = letter + ':' + key
                break
    labels = {k: l for k, l, _ in order}
    for key, label, pred in order:
        names = members.get(key)
        if not names:
            continue
        names.sort(key=lambda n: (RARITY_ORDER.get(feats[n]['rarity'], 9), n))
        rep = pick_rep(names, 'card')
        f = feats[rep]
        orig, env = card_scene(char, rep, f)
        counts = {}
        for n in names:
            counts['yes' if ours_has('card', n) else 'no'] = counts.get('yes' if ours_has('card', n) else 'no', 0) + 1
        impl = impl_from(counts, False)
        add(Row(letter, key, '%s [%s]' % (label, fmt_members(names)), slug(rep), orig, env, impl,
                scene={'char': char, 'mark': env_mark(orig, f), 'rep': rep}))
    for key, label, kw in KEYWORD_ROWS:
        names = sorted([n for n in pool if kw in feats[n]['kw']],
                       key=lambda n: (RARITY_ORDER.get(feats[n]['rarity'], 9), n))
        if not names:
            continue
        rep = pick_rep([n for n in names if 'Unplayable' not in feats[n]['kw']] or names, 'card')
        f = feats[rep]
        orig, env = card_scene(char, rep, f)
        counts = {}
        for n in names:
            k2 = 'yes' if ours_has('card', n) else 'no'
            counts[k2] = counts.get(k2, 0) + 1
        add(Row(letter, key, 'cross-cut: %s [%s]' % (label, fmt_members(names)), slug(rep), orig, env,
                impl_from(counts, False), scene={'char': char, 'mark': env_mark(orig, f), 'rep': rep}))


def env_mark(orig, f):
    if 'Unplayable' in f['kw']:
        return 'orig="INPUT move" ours=500'
    return 'orig="INPUT release" ours=530'


# colorless / status / curse / token / event cards
def build_colorless_rows():
    sets = [('Colorless', 'colorless'), ('Status', 'status'), ('Curse', 'curse'), ('Token', 'token'),
            ('Event', 'event'), ('Quest', 'quest')]
    allf = {}
    order_all = []
    for pool, tag in sets:
        for n in pool_cards(pool):
            if n in allf:
                continue
            f = parse_card(n)
            f['pool'] = tag
            allf[n] = f
            order_all.append(n)
    members = {}
    # cross-pool behaviour groups: pool tag first for status/curse/quest/event, behaviours for rest
    for n in order_all:
        f = allf[n]
        if f['pool'] in ('status', 'curse'):
            if 'Unplayable' in f['kw']:
                key = 'c_unplay_' + f['pool']
                label = f['pool'] + ' card: unplayable, effect from sitting in hand/deck'
            else:
                key = 'c_play_' + f['pool']
                label = f['pool'] + ' card: playable'
        elif f['pool'] == 'quest':
            key, label = 'c_quest', 'quest cards (event reward)'
        elif f['pool'] == 'event':
            key, label = 'c_event', 'event-only cards'
        else:
            key = None
            for k, l, pred in COMMON_GROUPS:
                if pred(f):
                    key, label = 'c_' + f['pool'] + '_' + k, f['pool'] + ' ' + l
                    break
        members.setdefault((key, label), []).append(n)
        CARD_ROW_OF[n] = 'C:' + key
    for (key, label), names in sorted(members.items(), key=lambda kv: (kv[0][0])):
        names.sort(key=lambda n: (RARITY_ORDER.get(allf[n]['rarity'], 9), n))
        rep = pick_rep(names, 'card')
        f = allf[rep]
        orig, env = card_scene('Ironclad', rep, f)
        counts = {}
        for n in names:
            k2 = 'yes' if ours_has('card', n) else 'no'
            counts[k2] = counts.get(k2, 0) + 1
        add(Row('C', key, '%s [%s]' % (label, fmt_members(names)), slug(rep), orig, env, impl_from(counts, False),
                scene={'char': 'Ironclad', 'mark': env_mark(orig, f), 'rep': rep}))


# ------------------------------------------------------------------------------- powers
PREF_POWERS = ['StrengthPower', 'DexterityPower', 'WeakPower', 'VulnerablePower', 'FrailPower', 'PoisonPower',
               'ArtifactPower', 'IntangiblePower', 'RegenPower', 'FocusPower', 'ThornsPower', 'PlatingPower']

HOOK_CATS = [
    ('dmg_mod', 'modifies damage dealt/taken', r'Modify(Damage|HpLost)'),
    ('block_mod', 'modifies block', r'ModifyBlock|AfterBlock|ShouldClearBlock'),
    ('card_hook', 'reacts to cards played/drawn/exhausted', r'AfterCard|BeforeCardPlayed|ModifyCard|ShouldPlay'),
    ('turn_start', 'triggers at turn start / draw / energy', r'AfterPlayerTurnStart|AfterSideTurnStart|BeforeSideTurnStart|BeforeHandDraw|ModifyHandDraw|AfterEnergyReset|ModifyMaxEnergy|ModifyEnergyGain'),
    ('dmg_react', 'reacts to damage (thorns/retaliation)', r'AfterDamageReceived|AfterDamageGiven|AfterAttack|BeforeAttack'),
    ('death', 'death / revive / combat-end hooks', r'AfterDeath|ShouldDie|BeforeDeath|ShouldCreatureBeRemoved|AfterCombatEnd|ShouldStopCombat'),
    ('turn_end', 'triggers at turn end', r'AfterSideTurnEnd|BeforeSideTurnEnd|BeforeSideTurnEndEarly'),
    ('orb_star', 'orb / star interaction', r'Orb|Star'),
]


def parse_power(name):
    t = rd(os.path.join(nsdir('Models.Powers'), name + '.cs'))
    f = {'name': name}
    f['debuff'] = bool(re.search(r'PowerType Type\s*=>\s*PowerType\.Debuff', t))
    f['single'] = 'StackType.Single' in t or 'StackType.None' in t
    f['decrement'] = bool(re.search(r'PowerCmd\.(Decrement|TickDownDuration)', t))
    f['cat'] = 'misc'
    for key, label, rx in HOOK_CATS:
        if re.search(rx, t):
            f['cat'] = key
            break
    f['vfx'] = bool(re.search(r'N\w+Vfx|VfxCmd', t))
    return f


def build_power_rows():
    names = [n for n in cs_names('Models.Powers') if n.endswith('Power')]
    feats = {n: parse_power(n) for n in names}
    # monster-only powers (used by Models.Monsters, not by cards/potions/relics/events)
    users = {}
    for ns, tag in [('Models.Cards', 'c'), ('Models.Potions', 'p'), ('Models.Relics', 'r'), ('Models.Events', 'e'),
                    ('Models.Monsters', 'm'), ('Models.Enchantments', 'x'), ('Models.Afflictions', 'x')]:
        for fn in cs_names(ns):
            t = rd(os.path.join(nsdir(ns), fn + '.cs'))
            for pn in re.findall(r'<(\w+Power)>', t):
                users.setdefault(pn, set()).add(tag)
    cat_label = {k: l for k, l, _ in HOOK_CATS}
    cat_label['misc'] = 'other'
    groups = {}
    for n in names:
        f = feats[n]
        kind = 'debuff' if f['debuff'] else 'buff'
        u = users.get(n, set())
        mon = u == {'m'}
        if f['decrement']:
            key, label = kind + '_countdown', '%s that counts down each turn' % kind
        elif f['single']:
            key, label = kind + '_single', '%s without a number (on/off)' % kind
        else:
            key = '%s_%s' % (kind, f['cat'])
            label = '%s: %s' % (kind, cat_label[f['cat']])
        if mon:
            key, label = 'mon_' + key, 'monster-only ' + label
        groups.setdefault((key, label), []).append(n)
    for (key, label), ns_ in sorted(groups.items(), key=lambda kv: kv[0][0]):
        rep = pick_rep(ns_, 'power', PREF_POWERS)
        for n in ns_:
            CARD_ROW_OF['P:' + n] = 'P:' + key
        f = feats[rep]
        idx = 1 if (f['debuff'] or key.startswith('mon_')) else 0
        amt = 1 if f['single'] else 3
        steps = ['con fight %s' % slug(ENC_SINGLE), 'wait 6', 'rec', 'con power %s %d %d' % (slug(rep), amt, idx), 'wait 3']
        env = {'STS_CHAR': 'Ironclad', 'STS_ENCOUNTER': ENC_SINGLE,
               'STS_SCRIPT': '500:Cpower_%s_%d_%d' % (rep, amt, idx), 'REC': '460-700'}
        counts = {}
        for n in ns_:
            k2 = 'yes' if ours_has('power', n) else 'no'
            counts[k2] = counts.get(k2, 0) + 1
        add(Row('P', key, '%s [%s]' % (label, fmt_members(ns_)), slug(rep), steps, env, impl_from(counts, False),
                scene={'char': 'Ironclad', 'mark': 'orig="INPUT con power" ours=500', 'rep': rep}))


# ------------------------------------------------------------------------------- potions
def parse_potion(name):
    t = rd(os.path.join(nsdir('Models.Potions'), name + '.cs'))
    f = {'name': name}
    m = re.search(r'TargetType => TargetType\.(\w+)', t)
    f['target'] = m.group(1) if m else 'AnyPlayer'
    m = re.search(r'Usage => PotionUsage\.(\w+)', t)
    f['usage'] = m.group(1) if m else 'CombatOnly'
    m = re.search(r'Rarity => PotionRarity\.(\w+)', t)
    f['rarity'] = m.group(1) if m else 'Common'
    f['dmg'] = 'CreatureCmd.Damage' in t or 'DamageCmd' in t
    f['block'] = 'GainBlock' in t
    f['heal'] = bool(re.search(r'CreatureCmd\.Heal|GainMaxHp|LoseMaxHp', t))
    f['draw'] = 'CardPileCmd.Draw' in t or 'GainEnergy' in t
    f['gen'] = 'AddGeneratedCard' in t or 'CardPileCmd.Add' in t
    f['select'] = 'CardSelectCmd' in t
    f['char'] = bool(re.search(r'Orb|Star|Osty|Forge|Doom|Focus', t))
    f['buff'] = 'PowerCmd.Apply' in t
    f['vfx'] = bool(re.search(r'N\w+Vfx|VfxCmd', t))
    f['potion'] = 'PotionCmd' in t
    return f


POTION_GROUPS = [
    ('thrown_aoe', 'thrown at all enemies', lambda f: f['target'] == 'AllEnemies'),
    ('thrown_dmg', 'thrown at one enemy: damage', lambda f: f['target'] == 'AnyEnemy' and f['dmg']),
    ('thrown_debuff', 'thrown at one enemy: debuff', lambda f: f['target'] == 'AnyEnemy'),
    ('auto', 'triggers by itself (revive etc.)', lambda f: f['usage'] == 'Automatic'),
    ('anytime', 'usable outside combat too', lambda f: f['usage'] == 'AnyTime'),
    ('char_potion', 'character-specific (orbs/stars/Osty/forge/doom)', lambda f: f['char']),
    ('drink_heal', 'drink: heal / max HP', lambda f: f['heal']),
    ('drink_block', 'drink: block', lambda f: f['block']),
    ('drink_cards', 'drink: create cards / pick a card', lambda f: f['gen'] or f['select']),
    ('drink_draw', 'drink: draw / energy', lambda f: f['draw']),
    ('drink_buff', 'drink: buff power on self', lambda f: f['buff']),
    ('drink_other', 'drink: other', lambda f: True),
]


def build_potion_rows():
    names = [n for n in cs_names('Models.Potions') if n != 'DeprecatedPotion']
    feats = {n: parse_potion(n) for n in names}
    groups = {}
    for n in names:
        for key, label, pred in POTION_GROUPS:
            if pred(feats[n]):
                groups.setdefault(key, []).append(n)
                CARD_ROW_OF['PO:' + n] = 'P:pot_' + key
                break
    for key, label, pred in POTION_GROUPS:
        ns_ = groups.get(key)
        if not ns_:
            continue
        rep = pick_rep(ns_, 'potion')
        f = feats[rep]
        enc = ENC_MULTI if f['target'] == 'AllEnemies' else ENC_SINGLE
        tgt = 'enemy' if f['target'] == 'AnyEnemy' else 'self'
        steps = ['con fight %s' % slug(enc), 'wait 6', 'con potion %s' % slug(rep), 'wait 1', 'rec',
                 'potion 1 %s' % tgt, 'wait 3']
        env = {'STS_CHAR': 'Ironclad', 'STS_ENCOUNTER': enc, 'STS_POTIONS': rep,
               'STS_SCRIPT': '500:Cusepotion_1_%s' % tgt, 'REC': '460-700'}
        counts = {}
        for n in ns_:
            k2 = 'yes' if ours_has('potion', n) else 'no'
            counts[k2] = counts.get(k2, 0) + 1
        add(Row('P', 'pot_' + key, 'potion, %s [%s]' % (label, fmt_members(ns_)), slug(rep), steps, env,
                impl_from(counts, False),
                scene={'char': 'Ironclad', 'mark': 'orig="INPUT (click|con potion)" ours=500', 'rep': rep}))


# ------------------------------------------------------------------------------- monsters / intents
def enc_info():
    info = {}
    for n in cs_names('Models.Encounters'):
        t = rd(os.path.join(nsdir('Models.Encounters'), n + '.cs'))
        m = re.search(r'RoomType => RoomType\.(\w+)', t)
        room = m.group(1) if m else 'Monster'
        mons = re.findall(r'ModelDb\.Monster<(\w+)>', t)
        cnt = len(re.findall(r'\.ToMutable\(\)', t))
        info[n] = {'room': room, 'weak': 'IsWeak => true' in t, 'monsters': sorted(set(mons)), 'count': cnt}
    return info


ENC = enc_info()


def monster_intents():
    out = {}
    for n in cs_names('Models.Monsters'):
        t = rd(os.path.join(nsdir('Models.Monsters'), n + '.cs'))
        out[n] = {'intents': set(re.findall(r'new (\w+Intent)\(', t)),
                  'anims': set(re.findall(r'TriggerAnim\([^,]*,\s*"(\w+)"', t) + re.findall(r'SetTrigger\("(\w+)"', t)),
                  'random': 'RandomBranchState' in t, 'cond': 'ConditionalBranchState' in t,
                  'summon': 'SummonIntent' in t or 'Summon' in t}
    return out


MON = monster_intents()
ENC_RANK = {'Monster': 1, 'Elite': 2, 'Boss': 3}


def best_encounter(pred, prefer_ours=True):
    cands = []
    for n, e in ENC.items():
        if n.startswith(('Deprecated', 'BattlewornDummyEvent')):
            continue
        if not any(pred(m) for m in e['monsters']):
            continue
        rank = (0 if (n in OURS['enc']) else 1, ENC_RANK.get(e['room'], 4), 0 if e['weak'] else 1,
                len(e['monsters']), n)
        cands.append((rank, n))
    cands.sort()
    return cands[0][1] if cands else None


INTENT_DESC = {
    'AbstractIntent': 'intent bubble framework (icon + number + hover tip)',
    'SingleAttackIntent': 'single attack intent (sword icon + damage)',
    'MultiAttackIntent': 'multi attack intent (damage x hits)',
    'DefendIntent': 'defend intent (shield icon)',
    'BuffIntent': 'buff intent (self buff arrow)',
    'DebuffIntent': 'debuff intent (player debuff)',
    'StatusIntent': 'status card intent (adds status cards)',
    'CardDebuffIntent': 'card debuff intent (card-affecting debuff)',
    'SummonIntent': 'summon intent',
    'EscapeIntent': 'escape intent (leaves combat)',
    'SleepIntent': 'sleep intent (Zzz)',
    'StunIntent': 'stun intent (stunned this turn)',
    'HealIntent': 'heal intent',
    'HiddenIntent': 'hidden intent (?)',
    'UnknownIntent': 'unknown intent (question mark)',
    'DeathBlowIntent': 'death blow intent (lethal attack)',
    'AttackIntent': 'attack intent base (damage display)',
}
INTENT_AFTER = [  # enemy turns to record per intent
]


def build_monster_rows():
    intents = [n for n in cs_names('MonsterMoves.Intents') if n.endswith('Intent') and n != 'AbstractIntent']
    intents = ['AbstractIntent'] + intents
    for it in intents:
        users = sorted(m for m, d in MON.items() if it in d['intents'])
        if it in ('AbstractIntent', 'AttackIntent', 'UnknownIntent'):
            users = sorted(m for m, d in MON.items() if 'SingleAttackIntent' in d['intents'])
        enc = best_encounter(lambda m: m in users) or ENC_SINGLE
        e = enc
        steps = ['con fight %s' % slug(e), 'wait 6', 'rec', 'wait 2', 'endturn', 'wait 5', 'endturn', 'wait 5']
        env = {'STS_CHAR': 'Ironclad', 'STS_ENCOUNTER': e, 'STS_SCRIPT': '500:T278x185,640:T278x185',
               'REC': '460-800'}
        mons = [m for m in ENC.get(e, {}).get('monsters', []) if m in users] or users[:1]
        counts = {}
        for m in users[:12]:
            k2 = 'yes' if ours_has('monster', m) else 'no'
            counts[k2] = counts.get(k2, 0) + 1
        enc_ok = e in OURS['enc']
        impl = impl_from(counts, False) if enc_ok else 'no'
        if it in ('AbstractIntent', 'AttackIntent', 'UnknownIntent', 'HiddenIntent', 'DeathBlowIntent'):
            if impl == 'yes' and it in ('HiddenIntent', 'DeathBlowIntent'):
                impl = 'partial'
        add(Row('M', 'intent_' + snake(it), '%s [%s; used by %s, e.g. %s]' % (INTENT_DESC.get(it, it), it, len(users),
                                                                         ', '.join(mons[:3]) or '-'),
                slug(mons[0]) if mons else slug(it), steps, env, impl,
                scene={'char': 'Ironclad', 'mark': 'orig="INPUT click" ours=500',
                       'rep': mons[0] if mons else it, 'note': 'enc %s' % e}))
    # intent number reacts to buffs/debuffs
    for key, label, power_o, power_s, idx in [
            ('intent_vs_vuln', 'intent damage when player is Vulnerable (number rises)', 'VULNERABLE_POWER', 'VulnerablePower', 0),
            ('intent_vs_strength', 'intent damage when enemy has Strength', 'STRENGTH_POWER', 'StrengthPower', 1),
            ('intent_vs_weak', 'intent damage when enemy is Weak (number drops)', 'WEAK_POWER', 'WeakPower', 1)]:
        steps = ['con fight %s' % slug(ENC_SINGLE), 'wait 6', 'rec', 'con power %s 2 %d' % (power_o, idx), 'wait 3']
        env = {'STS_CHAR': 'Ironclad', 'STS_ENCOUNTER': ENC_SINGLE,
               'STS_SCRIPT': '500:Cpower_%s_2_%d' % (power_s, idx), 'REC': '460-700'}
        add(Row('M', key, label, slug(ENC_SINGLE), steps, env, 'yes',
                scene={'char': 'Ironclad', 'mark': 'orig="INPUT con power" ours=500', 'rep': ENC_SINGLE}))
    # state machines
    for key, label, flag in [('sm_random', 'RandomBranchState: random move pick', 'random'),
                             ('sm_cond', 'ConditionalBranchState: condition-driven move', 'cond')]:
        users = sorted(m for m, d in MON.items() if d[flag])
        enc = best_encounter(lambda m: m in users) or ENC_SINGLE
        steps = ['con fight %s' % slug(enc), 'wait 6', 'rec', 'endturn', 'wait 5', 'endturn', 'wait 5', 'endturn', 'wait 5']
        env = {'STS_CHAR': 'Ironclad', 'STS_ENCOUNTER': enc, 'STS_SCRIPT': '500:T278x185,640:T278x185,780:T278x185',
               'REC': '460-900'}
        add(Row('M', key, '%s [%s monsters, e.g. %s]' % (label, len(users), ', '.join(users[:3])), slug(users[0]) if users else key,
                steps, env, 'yes' if enc in OURS['enc'] else 'no',
                scene={'char': 'Ironclad', 'mark': 'orig="INPUT click" ours=500', 'rep': users[0] if users else key,
                       'note': 'enc %s' % enc}))
    # animations
    anim_rows = [
        ('anim_hit', 'enemy hit reaction (shake/flash) + damage number', ['play last enemy'], 'Bash'),
        ('anim_death', 'enemy death (fade/dissolve, kill all)', ['con kill all'], None),
        ('anim_attack', 'enemy attack animation lunging at the player (Attack trigger)', ['endturn'], None),
        ('anim_cast', 'enemy cast animation (Cast trigger, buffs/debuffs)', ['endturn'], None),
    ]
    for key, label, act, deck in anim_rows:
        if key == 'anim_cast':
            enc = best_encounter(lambda m: 'Cast' in MON.get(m, {}).get('anims', ()) and 'BuffIntent' in MON[m]['intents']) or ENC_SINGLE
        else:
            enc = ENC_SINGLE
        steps = ['con fight %s' % slug(enc), 'wait 6']
        env = {'STS_CHAR': 'Ironclad', 'STS_ENCOUNTER': enc}
        if key == 'anim_hit':
            steps += ['con card BASH', 'wait 1', 'rec', 'play last enemy', 'wait 3']
            env.update(STS_DECK='Bash,Bash,Bash,Bash,Bash', STS_SCRIPT=PLAY_ENEMY)
            mark = 'orig="INPUT release" ours=530'
        elif key == 'anim_death':
            steps += ['rec', 'con kill all', 'wait 4']
            env.update(STS_SCRIPT='500:Ckill_all')
            mark = 'orig="INPUT con kill" ours=500'
        else:
            steps += ['rec', 'endturn', 'wait 5']
            env.update(STS_SCRIPT=ENDTURN)
            mark = 'orig="INPUT click" ours=500'
        env['REC'] = '460-760'
        add(Row('M', key, label, slug(enc), steps, env, 'yes' if key != 'anim_cast' else 'partial',
                scene={'char': 'Ironclad', 'mark': mark, 'rep': enc}))
    for key, label, anim in [('anim_stun', 'stun / unstun animation', 'Stun'), ('anim_sleep_wake', 'sleep -> wake up animation', 'WakeUp'),
                             ('anim_heal', 'heal animation', 'Heal'), ('anim_summon', 'summon / spawn animation (new enemy appears)', 'Summon')]:
        enc = best_encounter(lambda m, a=anim: a in MON.get(m, {}).get('anims', ())) or ENC_SINGLE
        steps = ['con fight %s' % slug(enc), 'wait 6', 'rec', 'endturn', 'wait 5', 'endturn', 'wait 5']
        env = {'STS_CHAR': 'Ironclad', 'STS_ENCOUNTER': enc, 'STS_SCRIPT': '500:T278x185,640:T278x185', 'REC': '460-800'}
        add(Row('M', key, label + ' [enc %s]' % enc, slug(enc), steps, env, 'yes' if enc in OURS['enc'] else 'no',
                scene={'char': 'Ironclad', 'mark': 'orig="INPUT click" ours=500', 'rep': enc}))
    # layouts by enemy count (normal/weak encounters only)
    bycount = {}
    for n, e in sorted(ENC.items()):
        if e['room'] == 'Monster' and 1 <= e['count'] <= 6 and not n.startswith('Deprecated') and 'Event' not in n:
            bycount.setdefault(e['count'], []).append(n)
    for c in sorted(bycount):
        encs = bycount[c]
        rep = next((n for n in encs if n in OURS['enc']), encs[0])
        steps = ['con fight %s' % slug(rep), 'wait 6', 'rec', 'wait 3']
        env = {'STS_CHAR': 'Ironclad', 'STS_ENCOUNTER': rep, 'REC': '460-700'}
        add(Row('M', 'layout_%d' % c, 'enemy layout: %d enemies on screen (positions, scale) [%d encounters, e.g. %s]' % (c, len(encs), ', '.join(encs[:3])),
                slug(rep), steps, env, 'yes' if rep in OURS['enc'] else 'no',
                scene={'char': 'Ironclad', 'mark': 'orig="INPUT con fight" ours=400', 'rep': rep}))


# ------------------------------------------------------------------------------- lifecycle
def build_lifecycle_rows():
    char_note = {
        'Ironclad': 'BurningBlood relic, 3 energy',
        'Silent': 'Ring of the Snake relic (extra draw), 3 energy',
        'Defect': 'orb slots, Cracked Core (starting Lightning)',
        'Regent': 'star counter, Divine Right relic',
        'Necrobinder': 'Osty companion, Bound Phylactery',
    }
    for c in CHARS:
        i = CHAR_IDX[c]
        rep = slug(c)
        n = lambda s: s  # noqa
        # fight start
        steps = ['rec', 'con fight %s' % slug(ENC_SINGLE), 'wait 7']
        add(Row('L', 'fight_start_' + c.lower(), '%s lifecycle: fight start (turn banner, hand draw, energy; %s)' % (c, char_note[c]),
                rep, steps, {'STS_CHAR': c, 'STS_ENCOUNTER': ENC_SINGLE, 'REC': '380-640'}, 'yes',
                scene={'char': c, 'mark': 'orig="INPUT con fight" ours=400', 'rep': c}))
        steps = ['con fight %s' % slug(ENC_SINGLE), 'wait 6', 'rec', 'endturn', 'wait 6']
        add(Row('L', 'end_turn_' + c.lower(), '%s lifecycle: end turn, discard hand, enemy turn, new draw' % c, rep, steps,
                {'STS_CHAR': c, 'STS_ENCOUNTER': ENC_SINGLE, 'STS_SCRIPT': ENDTURN, 'REC': '460-800'}, 'yes',
                scene={'char': c, 'mark': 'orig="INPUT click" ours=500', 'rep': c}))
        steps = ['con fight %s' % slug(ENC_SINGLE), 'wait 6', 'rec', 'con win', 'wait 6']
        add(Row('L', 'victory_' + c.lower(), '%s lifecycle: victory (enemies die, combat-end heal, rewards screen, card reward)' % c, rep, steps,
                {'STS_CHAR': c, 'STS_ENCOUNTER': ENC_SINGLE, 'STS_SCRIPT': '500:Cwin', 'REC': '460-900'}, 'yes',
                scene={'char': c, 'mark': 'orig="INPUT con win" ours=500', 'rep': c}))
        steps = ['con fight %s' % slug(ENC_SINGLE), 'wait 6', 'rec', 'con die', 'wait 8']
        add(Row('L', 'death_' + c.lower(), '%s lifecycle: death (death anim, game over screen, run summary)' % c, rep, steps,
                {'STS_CHAR': c, 'STS_ENCOUNTER': ENC_SINGLE, 'STS_SCRIPT': '500:Cdie', 'REC': '460-1000'}, 'yes',
                scene={'char': c, 'mark': 'orig="INPUT con die" ours=500', 'rep': c}))
        steps = ['rec', 'click 500 330', 'wait 2', 'click 500 300', 'wait 2'] + ['key 124'] * i + ['wait 3']
        script = '60:A,120:A' + ''.join(',%d:RIGHT' % (180 + 15 * k) for k in range(i))
        add(Row('L', 'char_select_' + c.lower(), '%s lifecycle: character select entry (portrait, bg, description, starting deck/relic)' % c, rep, steps,
                {'STS_SCRIPT': script, 'REC': '40-400'}, 'partial',
                scene={'char': c, 'mark': 'orig="INPUT click" ours=60', 'rep': c,
                       'note': 'coords approximate; main menu -> singleplayer -> character %d of 5 (right arrow x%d). Ours: no STS_CHAR so the select screen shows.' % (i + 1, i)}))


# ------------------------------------------------------------------------------- UI (hand-written)
# (key, group text, representative original id, orig steps, ours env, impl, vfx classes, note)
def U(key, group, rep, steps, env, impl, vfx=(), char='Ironclad', note='', mark='orig="INPUT move" ours=500'):
    return key, group, rep, steps, env, impl, tuple(vfx), char, note, mark


def combat_u(setup, action, ours, enc=ENC_SINGLE):
    steps, env = combat('Ironclad', enc, enc, setup, action, ours)
    return steps, env


UI_ROWS = []


def u_row(key, group, rep, setup=None, action=None, ours=None, impl='yes', char='Ironclad', enc=ENC_SINGLE, note='',
          mark='orig="INPUT move" ours=500', rec='460-700', vfx=(), pre=None):
    steps, env = combat(char, enc, enc, setup, action, ours, rec=rec)
    UI_ROWS.append((key, group, rep, steps, env, impl, tuple(vfx), char, note, mark))


def build_ui_rows():
    energy_cls = {'Ironclad': 'NEnergyCounter (ironclad orb)', 'Silent': 'NEnergyCounter (silent orb)',
                  'Defect': 'NEnergyCounter (defect orb)', 'Regent': 'NEnergyCounter (regent orb)',
                  'Necrobinder': 'NEnergyCounter (necrobinder orb)'}
    for c in CHARS:
        u_row('energy_' + c.lower(), '%s: energy counter scene (own orb art, E/M label, outline colour, pop on gain; %s)' % (c, energy_cls[c]),
              'N_ENERGY_COUNTER', setup=['energy 0', 'energy 3'], ours={'STS_ENERGY': '0'}, char=c, impl='yes',
              mark='orig="INPUT con energy" ours=500', action=[], note='compare orb colour/outline for %s at 0 and full energy' % c)
    u_row('stars', 'Regent star counter (NStarCounter: icon, number, gain pop, hover tip)', 'N_STAR_COUNTER',
          setup=['stars 5'], ours={'STS_STARS': '5'}, char='Regent', impl='yes', mark='orig="INPUT con stars" ours=500')
    u_row('orbs', 'Defect orb slots + orbs on the player (NOrbManager / NOrb: idle bob, passive value text, empty slots)', 'N_ORB_MANAGER',
          setup=['card ZAP', 'card COOLHEADED', 'card DUALCAST'], action=['play last self'], char='Defect', impl='partial',
          ours={'STS_ORBS': 'Lightning,Frost,Dark', 'STS_DECK': 'Zap,Zap,Zap,Zap,Zap', 'STS_SCRIPT': PLAY_SELF},
          vfx=['NLightningOrbVfx', 'NFrostOrbVfx', 'NDarkOrbVfx', 'NPlasmaOrbVfx', 'NGlassOrbVfx', 'NOrbVfx'],
          mark='orig="INPUT release" ours=530')
    u_row('orb_evoke', 'Defect orb evoke/passive trigger (per-orb evoke VFX: Lightning bolt, Frost, Dark, Plasma, Glass)', 'LIGHTNING_ORB',
          setup=['card DUALCAST'], action=['play last enemy'], char='Defect', impl='partial',
          ours={'STS_ORBS': 'Lightning,Frost,Dark', 'STS_DECK': 'Dualcast,Dualcast,Dualcast,Dualcast,Dualcast', 'STS_SCRIPT': PLAY_ENEMY},
          mark='orig="INPUT release" ours=530')
    u_row('draw_pile', 'draw pile button (NDrawPileButton: count, bump on draw) + pile screen (NCombatCardPile)', 'N_DRAW_PILE_BUTTON',
          setup=[], action=['hover 45 520', 'wait 1', 'click 45 520', 'wait 2'], ours={'STS_SCRIPT': '500:T20x215,520:T20x215'}, impl='yes',
          mark='orig="INPUT click" ours=500')
    u_row('discard_pile', 'discard pile button (NDiscardPileButton) + pile screen', 'N_DISCARD_PILE_BUTTON',
          setup=[], action=['endturn', 'wait 6', 'endturn', 'wait 2'], ours={'STS_SCRIPT': '500:T278x185,700:T278x185'}, impl='yes',
          mark='orig="INPUT click" ours=500', rec='460-900')
    u_row('exhaust_pile', 'exhaust pile button appears after first exhaust (NExhaustPileButton)', 'N_EXHAUST_PILE_BUTTON',
          setup=['card TRUE_GRIT'], action=['play last self', 'wait 2', 'click 500 400'], ours={'STS_DECK': 'TrueGrit,TrueGrit,TrueGrit,TrueGrit,TrueGrit', 'STS_SCRIPT': PLAY_SELF},
          impl='yes', mark='orig="INPUT release" ours=530')
    u_row('end_turn_btn', 'end turn button states (enabled glow when nothing playable, disabled during enemy turn, NEndTurnButton)', 'N_END_TURN_BUTTON',
          action=['hover 880 480', 'wait 1', 'endturn', 'wait 3'], ours={'STS_SCRIPT': ENDTURN}, impl='yes', mark='orig="INPUT click" ours=500')
    u_row('end_turn_hold', 'end turn long-press confirmation bar (NEndTurnLongPressBar, setting on)', 'N_END_TURN_LONG_PRESS_BAR',
          action=['hover 880 480', 'wait 2'], impl='no', note='needs the long-press setting enabled in the original; 3DS has a tap instead',
          ours={'STS_SCRIPT': ENDTURN})
    u_row('intent_ui', 'intent icon above enemy (NIntent: bob, damage number, multi-hit count)', 'N_INTENT',
          action=['wait 2'], impl='yes', mark='orig="INPUT con fight" ours=400')
    u_row('intent_tip', 'intent hover tip (HoverTip text box next to the enemy)', 'N_INTENT',
          action=['hover 700 200', 'wait 2'], ours={'STS_TIPS': '1'}, impl='partial', note='3DS shows tips via touch on the intent; compare box layout')
    u_row('hand_fan', 'player hand fan layout (NPlayerHand: arc, spacing, card hover raise & enlarge)', 'N_PLAYER_HAND',
          action=['hover 500 540', 'wait 1', 'hover 400 540', 'wait 2'], ours={'STS_SCRIPT': '500:P160x130,505:M160x120'}, impl='partial',
          note='3DS hand is on the bottom touch screen: different geometry by design')
    u_row('card_drag', 'dragging a card out of the hand (NMouseCardPlay: card follows cursor, targeting arrow, release plays)', 'N_MOUSE_CARD_PLAY',
          setup=['card BASH'], action=['play last enemy'], ours={'STS_DECK': 'Bash,Bash,Bash,Bash,Bash', 'STS_SCRIPT': PLAY_ENEMY}, impl='yes',
          mark='orig="INPUT release" ours=530')
    u_row('targeting_arrow', 'targeting arrow + enemy selection reticle (NTargetingArrow, NSelectionReticle, NTargetManager)', 'N_TARGETING_ARROW',
          setup=['card BASH'], action=['drag 500 540 700 300 800'], ours={'STS_DECK': 'Bash,Bash,Bash,Bash,Bash', 'STS_SCRIPT': '500:P160x130,505:M160x100,510:M170x60,515:M190x30'},
          impl='partial', vfx=['NSelectionReticle', 'NTargetingArrow'], mark='orig="INPUT press" ours=500')
    u_row('card_glow', 'playable card glow / unplayable grey, upgrade preview glow (NCardHighlight, NCardRareGlow, NCardUncommonGlow)', 'N_CARD_HIGHLIGHT',
          setup=['energy 0'], action=['hover 500 540', 'wait 2'], ours={'STS_ENERGY': '0'}, impl='partial', vfx=['NCardRareGlow', 'NCardUncommonGlow'],
          mark='orig="INPUT con energy" ours=500')
    u_row('card_play_queue', 'card play queue / fly to discard (NCardPlayQueue, NCardFlyVfx, NSelectedHandCardContainer)', 'N_CARD_PLAY_QUEUE',
          setup=['card DEFEND_IRONCLAD'], action=['play last self'], ours={'STS_DECK': 'DefendIronclad,DefendIronclad,DefendIronclad,DefendIronclad,DefendIronclad', 'STS_SCRIPT': PLAY_SELF},
          impl='yes', mark='orig="INPUT release" ours=530')
    u_row('hp_bar', 'creature health bar (NHealthBar: fill colour, text, block overlay shield, poison/doom preview)', 'N_HEALTH_BAR',
          setup=['damage 20 1', 'block 10'], action=[], ours={'STS_ENEMY_HP': '20'}, impl='partial', note='compare player and enemy bars after damage + block',
          mark='orig="INPUT con damage" ours=500')
    u_row('hp_bar_poison', 'health bar with poison counter / doom bar', 'N_HEALTH_BAR',
          setup=[], action=['con power POISON_POWER 8 1', 'wait 1', 'con power DOOM_POWER 10 1', 'wait 3'],
          ours={'STS_SCRIPT': '500:Cpower_PoisonPower_8_1,520:Cpower_DoomPower_10_1'}, impl='partial', mark='orig="INPUT con power" ours=500')
    u_row('creature_state', 'creature state display (NCreatureStateDisplay: HP + block + power row under feet)', 'N_CREATURE_STATE_DISPLAY',
          setup=['block 5', 'power STRENGTH_POWER 2 0', 'power WEAK_POWER 2 1'], ours={'STS_POWERS': 'StrengthPower:2:0,WeakPower:2:1'}, impl='yes',
          mark='orig="INPUT con power" ours=500')
    u_row('power_icons', 'power icons row with stack numbers (NPower, NPowerContainer; many powers wrap)', 'N_POWER_CONTAINER',
          setup=['power STRENGTH_POWER 2 0', 'power DEXTERITY_POWER 3 0', 'power VULNERABLE_POWER 1 0', 'power WEAK_POWER 1 0', 'power FRAIL_POWER 1 0', 'power ARTIFACT_POWER 1 0'],
          ours={'STS_POWERS': 'StrengthPower:2:0,DexterityPower:3:0,VulnerablePower:1:0,WeakPower:1:0,FrailPower:1:0,ArtifactPower:1:0'}, impl='yes',
          mark='orig="INPUT con power" ours=500')
    u_row('power_tip', 'power hover tip (name + description box, NPower hover)', 'N_POWER', setup=['power STRENGTH_POWER 3 0'], action=['hover 250 380', 'wait 2'],
          ours={'STS_POWERS': 'StrengthPower:3:0', 'STS_TIPS': '1'}, impl='partial', mark='orig="INPUT move" ours=500')
    u_row('potion_belt', 'potion belt top bar (NPotionContainer, NPotionHolder slots, empty slots)', 'N_POTION_CONTAINER', setup=['potion FIRE_POTION', 'potion BLOCK_POTION'],
          ours={'STS_POTIONS': 'FirePotion,BlockPotion'}, impl='yes', mark='orig="INPUT con potion" ours=500')
    u_row('potion_popup', 'potion popup (use / discard buttons, NPotionPopup)', 'N_POTION_POPUP', setup=['potion FIRE_POTION'], action=['click 520 25', 'wait 2'],
          ours={'STS_POTIONS': 'FirePotion', 'STS_SCRIPT': '500:T60x20'}, impl='partial', mark='orig="INPUT click" ours=500')
    u_row('relic_bar', 'relic bar below the top bar (NRelicInventory: icons, counters, flash on trigger NRelicFlashVfx)', 'N_RELIC_INVENTORY',
          setup=['relic VAJRA', 'relic ANCHOR', 'relic LANTERN'], ours={'STS_RELICS': 'Vajra,Anchor,Lantern'}, impl='yes', vfx=['NRelicFlashVfx'],
          mark='orig="INPUT con relic" ours=500')
    u_row('relic_tip', 'relic hover tip', 'N_RELIC', setup=['relic VAJRA'], action=['hover 40 70', 'wait 2'], ours={'STS_RELICS': 'Vajra', 'STS_TIPS': '1'}, impl='partial')
    u_row('banner_start', 'combat start banner (NCombatStartBanner)', 'N_COMBAT_START_BANNER', action=[], impl='yes', rec='380-560',
          mark='orig="INPUT con fight" ours=400')
    u_row('banner_turn', 'player/enemy turn banners (NPlayerTurnBanner, NEnemyTurnBanner)', 'N_PLAYER_TURN_BANNER', action=['endturn', 'wait 6'],
          ours={'STS_SCRIPT': ENDTURN}, impl='yes', mark='orig="INPUT click" ours=500', rec='460-800')
    u_row('damage_numbers', 'floating damage / heal numbers (NDamageNumVfx, NHealNumVfx)', 'N_DAMAGE_NUM_VFX', setup=['card BASH'], action=['play last enemy'],
          ours={'STS_DECK': 'Bash,Bash,Bash,Bash,Bash', 'STS_SCRIPT': PLAY_ENEMY}, impl='yes', vfx=['NDamageNumVfx', 'NHealNumVfx'],
          mark='orig="INPUT release" ours=530')
    u_row('low_hp_vignette', 'low HP red border + hurt vignette (NLowHpBorderVfx, PlayerHurtVignetteHelper)', 'N_LOW_HP_BORDER_VFX', setup=['damage 55'],
          ours={'STS_HP': '15'}, impl='yes', vfx=['NLowHpBorderVfx', 'PlayerHurtVignetteHelper'], mark='orig="INPUT con damage" ours=500')
    u_row('screen_shake', 'screen shake / hit stop on heavy hits (NScreenShake, NHitStop)', 'N_SCREEN_SHAKE', setup=['card BLUDGEON'], action=['play last enemy'],
          ours={'STS_DECK': 'Bludgeon,Bludgeon,Bludgeon,Bludgeon,Bludgeon', 'STS_SCRIPT': PLAY_ENEMY, 'STS_ENERGY': '9'}, impl='partial', vfx=['NScreenShake', 'NHitStop'],
          mark='orig="INPUT release" ours=530')
    u_row('peek_button', 'peek button when a card-choice overlay hides the board (NPeekButton)', 'N_PEEK_BUTTON', setup=['card TRUE_GRIT'],
          action=['play last self', 'wait 2', 'hover 900 100', 'wait 1'], ours={'STS_DECK': 'TrueGrit,TrueGrit,TrueGrit,TrueGrit,TrueGrit', 'STS_SCRIPT': PLAY_SELF}, impl='no',
          mark='orig="INPUT release" ours=530')
    u_row('enchant_afflict', 'enchanted / afflicted card in hand (enchantment banner, affliction overlay NCardEnchantVfx)', 'N_CARD', setup=['card BASH', 'enchant', 'afflict'],
          ours={'STS_ENCHANT': 'Sharp', 'STS_AFFLICT': 'Bound', 'STS_DECK': 'Bash,Bash,Bash,Bash,Bash'}, impl='partial', vfx=['NCardEnchantVfx'],
          mark='orig="INPUT con enchant" ours=500')
    u_row('card_hover_tips', 'card hover tip cards (keyword + generated-card tips, NHoverTipSet / NHoverTipCardContainer)', 'N_HOVER_TIP_SET', setup=['card BASH'],
          action=['hover 500 540', 'wait 2'], ours={'STS_TIPS': '1', 'STS_DECK': 'Bash,Bash,Bash,Bash,Bash'}, impl='partial')
    u_row('combat_bg', 'combat background by act (NCombatBackground layers + parallax)', 'N_COMBAT_BACKGROUND', action=['wait 2'], impl='yes', mark='orig="INPUT con fight" ours=400')
    u_row('combat_bg_act2', 'combat background act 2', 'N_COMBAT_BACKGROUND', setup=[], action=['wait 2'], ours={'STS_ACT': '2'}, impl='yes',
          note='orig: use `con act 2` before fight', mark='orig="INPUT con fight" ours=400')
    u_row('combat_bg_act3', 'combat background act 3', 'N_COMBAT_BACKGROUND', setup=[], action=['wait 2'], ours={'STS_ACT': '3'}, impl='yes',
          note='orig: use `con act 3` before fight', mark='orig="INPUT con fight" ours=400')
    # top bar
    for key, label, rep, act, note in [
        ('top_bar', 'top bar overall (NTopBar: portrait, name, HP, gold, floor, room icons, buttons)', 'N_TOP_BAR', [], ''),
        ('top_hp_gold', 'top bar HP + gold with change animation (NTopBarHp, NTopBarGold)', 'N_TOP_BAR_HP', ['con damage 10', 'wait 1', 'con gold 50', 'wait 2'], ''),
        ('top_deck_btn', 'deck button with count + deck view (NTopBarDeckButton)', 'N_TOP_BAR_DECK_BUTTON', ['click 700 25', 'wait 2'], ''),
        ('top_map_btn', 'map button + map overlay in combat (NTopBarMapButton)', 'N_TOP_BAR_MAP_BUTTON', ['click 760 25', 'wait 2'], ''),
        ('top_pause_btn', 'pause button / pause menu (NTopBarPauseButton)', 'N_TOP_BAR_PAUSE_BUTTON', ['click 960 25', 'wait 2'], ''),
        ('top_floor_room', 'floor + room type icons, boss icon (NTopBarFloorIcon, NTopBarRoomIcon, NTopBarBossIcon)', 'N_TOP_BAR_FLOOR_ICON', [], ''),
        ('top_portrait_tip', 'portrait hover tip + run timer (NTopBarPortraitTip, NRunTimer)', 'N_TOP_BAR_PORTRAIT_TIP', ['hover 40 25', 'wait 2'], ''),
    ]:
        u_row(key, label, rep, action=act, ours={'STS_TIPS': '1'} if 'tip' in key else None, impl='yes' if key != 'top_portrait_tip' else 'partial',
              mark='orig="INPUT click" ours=500' if act else 'orig="INPUT con fight" ours=400')


# ------------------------------------------------------------------------------- X screens (hand-written)
def x_row(key, group, rep, orig, ours, impl, char='Ironclad', note='', mark='orig="INPUT click" ours=60', rec='40-400', vfx=()):
    env = dict(ours)
    env['REC'] = rec
    UI_ROWS.append(('X:' + key, group, rep, orig, env, impl, tuple(vfx), char, note, mark))


MENU_NAV_NOTE = 'menu coords approximate (1000 px wide shot); calibrate on first run'


def pick_event(prefs):
    names = cs_names('Models.Events')
    for p in prefs:
        if p in names and ours_has('event', p):
            return p
    for n in names:
        if ours_has('event', n) and not n.startswith('Deprecated'):
            return n
    return names[0]


def build_screen_rows():
    ev1 = pick_event(['Wellspring', 'SunkenStatue', 'TrashHeap', 'ColossalFlower', 'DollRoom'])
    ev2 = pick_event(['RoundTeaParty', 'DenseVegetation', 'PunchOff', 'BattlewornDummy', 'TeaMaster'])
    ev3 = pick_event(['Reflections', 'SelfHelpBook', 'TinkerTime', 'MorphicGrove'])
    run = lambda e, a: (['con ' + e, 'wait 6', 'rec'] + a)  # noqa
    x_row('main_menu', 'main menu (NMainMenu: logo, background, text buttons, continue info)', 'N_MAIN_MENU',
          ['rec', 'wait 4'], {}, 'yes', mark='orig="INPUT click" ours=60', note='first screen after boot', rec='40-300')
    x_row('submenu_single', 'singleplayer submenu (standard / daily / custom, NSingleplayerSubmenu)', 'N_SINGLEPLAYER_SUBMENU',
          ['rec', 'click 500 300', 'wait 3'], {'STS_SCRIPT': '60:A'}, 'partial', note=MENU_NAV_NOTE)
    x_row('char_select', 'character select screen (NCharacterSelectScreen: portrait carousel, ascension panel, act dropdown)', 'N_CHARACTER_SELECT_SCREEN',
          ['rec', 'click 500 300', 'wait 2', 'click 500 300', 'wait 3'], {'STS_SCRIPT': '60:A,120:A'}, 'partial', note=MENU_NAV_NOTE,
          vfx=['NRegentCharacterSelectBg'] and [])
    x_row('ascension_panel', 'ascension picker panel (NAscensionPanel arrows + text)', 'N_ASCENSION_PANEL',
          ['rec', 'click 500 300', 'wait 2', 'click 500 300', 'wait 2', 'click 600 460', 'wait 2'], {'STS_SCRIPT': '60:A,120:A,180:UP'}, 'yes', note=MENU_NAV_NOTE)
    x_row('map', 'map screen (NMapScreen: nodes, paths, legend, boss icon, current-position marker)', 'N_MAP_SCREEN',
          ['rec', 'key 36', 'wait 3'], {'STS_NO_NEOW': '1', 'STS_SCRIPT': '300:A'}, 'yes', mark='orig="INPUT key" ours=300', rec='200-500',
          note='original: open map with the top-bar map button after `con act 1`', vfx=[])
    x_row('map_select', 'map node selection (NNormalMapPoint hover/press, NMapNodeSelectVfx, path trail)', 'N_NORMAL_MAP_POINT',
          ['rec', 'click 500 420', 'wait 3'], {'STS_NO_NEOW': '1', 'STS_SCRIPT': '300:A,340:A'}, 'yes', rec='200-500')
    x_row('map_draw', 'map drawing tools (NMapDrawButton, NMapEraseButton; multiplayer-oriented)', 'N_MAP_DRAW_BUTTON',
          ['rec', 'wait 2'], {'STS_NO_NEOW': '1'}, 'no', note='probably omitted on 3DS (drawing on the map)')
    x_row('event_a', 'event room: option buttons + text (%s)' % ev1, slug(ev1), ['con event %s' % slug(ev1), 'wait 5', 'rec', 'click 500 400', 'wait 3'],
          {'STS_ROOM': 'Event', 'STS_EVENT': ev1, 'STS_SCRIPT': '500:A'}, 'yes', char='Ironclad', mark='orig="INPUT click" ours=500', rec='400-700')
    x_row('event_b', 'event room: event with a fight / card choice (%s)' % ev2, slug(ev2), ['con event %s' % slug(ev2), 'wait 5', 'rec', 'click 500 400', 'wait 3'],
          {'STS_ROOM': 'Event', 'STS_EVENT': ev2, 'STS_SCRIPT': '500:A'}, 'yes', mark='orig="INPUT click" ours=500', rec='400-700')
    x_row('event_c', 'event room: event with relic/card reward (%s)' % ev3, slug(ev3), ['con event %s' % slug(ev3), 'wait 5', 'rec', 'click 500 400', 'wait 3'],
          {'STS_ROOM': 'Event', 'STS_EVENT': ev3, 'STS_SCRIPT': '500:A'}, 'yes', mark='orig="INPUT click" ours=500', rec='400-700')
    x_row('ancient', 'ancient event: Neow dialogue + relic choice (NAncientBgContainer, dialogue speech bubbles)', 'NEOW',
          ['con ancient NEOW', 'wait 6', 'rec', 'click 500 450', 'wait 3'], {'STS_ANCIENT': 'Neow', 'STS_SCRIPT': '220:A,260:A'}, 'yes', rec='300-600',
          mark='orig="INPUT click" ours=260')
    x_row('shop', 'shop room (NMerchantInventory: cards, relics, potions, card removal, merchant character + dialogue)', 'N_MERCHANT_INVENTORY',
          ['con room Shop', 'wait 6', 'rec', 'hover 500 300', 'wait 3'], {'STS_ROOM': 'Shop'}, 'yes', rec='400-700', mark='orig="INPUT move" ours=420')
    x_row('shop_buy', 'shop: buy an item (price tag, gold change, slot sold-out)', 'N_MERCHANT_CARD',
          ['con room Shop', 'wait 6', 'con gold 500', 'wait 1', 'rec', 'click 300 250', 'wait 3'], {'STS_ROOM': 'Shop', 'STS_SCRIPT': '420:T100x80,440:A'}, 'yes',
          rec='400-700', mark='orig="INPUT click" ours=440')
    x_row('rest', 'rest site (NRestSiteRoom: campfire, character rest pose, buttons rest/smith/...)', 'N_REST_SITE_ROOM',
          ['con room RestSite', 'wait 6', 'rec', 'wait 3'], {'STS_ROOM': 'Rest'}, 'yes', rec='400-700', mark='orig="INPUT con room" ours=400',
          vfx=[])
    x_row('rest_heal', 'rest site: heal option + effect', 'N_REST_SITE_BUTTON',
          ['con room RestSite', 'wait 6', 'con damage 30', 'wait 1', 'rec', 'click 400 400', 'wait 4'], {'STS_ROOM': 'Rest', 'STS_HP': '40', 'STS_SCRIPT': '420:A'},
          'yes', rec='400-760', mark='orig="INPUT click" ours=420')
    x_row('rest_smith', 'rest site: smith / upgrade card selection (NDeckUpgradeSelectScreen, NUpgradePreview)', 'N_DECK_UPGRADE_SELECT_SCREEN',
          ['con room RestSite', 'wait 6', 'rec', 'click 600 400', 'wait 3'], {'STS_ROOM': 'Rest', 'STS_SCRIPT': '420:RIGHT,430:A'}, 'yes', rec='400-760',
          mark='orig="INPUT click" ours=430')
    x_row('treasure', 'treasure room: chest + relic pick (NTreasureRoom, NTreasureButton, relic holders)', 'N_TREASURE_ROOM',
          ['con room Treasure', 'wait 6', 'rec', 'click 500 350', 'wait 4'], {'STS_ROOM': 'Treasure', 'STS_SCRIPT': '420:A'}, 'yes', rec='400-760',
          mark='orig="INPUT click" ours=420')
    x_row('rewards', 'rewards screen after combat (NRewardsScreen, NRewardButton: gold, potion, card, relic rows)', 'N_REWARDS_SCREEN',
          ['con fight SHRINKER_BEETLE_WEAK', 'wait 6', 'rec', 'con win', 'wait 5'], {'STS_CHAR': 'Ironclad', 'STS_ENCOUNTER': ENC_SINGLE, 'STS_SCRIPT': '500:Cwin'}, 'yes',
          rec='460-900', mark='orig="INPUT con win" ours=500')
    x_row('card_reward', 'card reward choice (NCardRewardSelectionScreen: 3 cards, skip, reroll alternatives)', 'N_CARD_REWARD_SELECTION_SCREEN',
          ['con fight SHRINKER_BEETLE_WEAK', 'wait 6', 'con win', 'wait 5', 'rec', 'click 500 300', 'wait 3'],
          {'STS_CHAR': 'Ironclad', 'STS_ENCOUNTER': ENC_SINGLE, 'STS_SCRIPT': '500:Cwin,700:A,720:A'}, 'yes', rec='680-900', mark='orig="INPUT click" ours=720')
    x_row('choose_card', 'choose-a-card / bundle selection screens (NChooseACardSelectionScreen, NChooseABundleSelectionScreen)', 'N_CHOOSE_A_BUNDLE_SELECTION_SCREEN',
          ['con event %s' % slug(ev3), 'wait 5', 'rec', 'wait 2'], {'STS_ROOM': 'Event', 'STS_EVENT': ev3}, 'partial', rec='400-700', mark='orig="INPUT con event" ours=400')
    x_row('card_grid', 'card grid select (remove/transform/enchant: NDeckCardSelectScreen, NDeckTransformSelectScreen, NDeckEnchantSelectScreen)', 'N_DECK_TRANSFORM_SELECT_SCREEN',
          ['con room Shop', 'wait 6', 'con gold 500', 'wait 1', 'rec', 'click 500 450', 'wait 3'], {'STS_ROOM': 'Shop', 'STS_SCRIPT': '420:DOWN,430:A'}, 'yes',
          rec='400-700', mark='orig="INPUT click" ours=430')
    x_row('deck_view', 'deck view overlay (NDeckViewScreen: grid, sort, scroll)', 'N_DECK_VIEW_SCREEN',
          ['con fight SHRINKER_BEETLE_WEAK', 'wait 6', 'rec', 'click 700 25', 'wait 3'], {'STS_CHAR': 'Ironclad', 'STS_ENCOUNTER': ENC_SINGLE, 'STS_SCRIPT': '500:T160x215'},
          'yes', rec='460-700', mark='orig="INPUT click" ours=500')
    x_row('inspect_card', 'inspect card screen (NInspectCardScreen: big card, upgrade preview tickbox)', 'N_INSPECT_CARD_SCREEN',
          ['con fight SHRINKER_BEETLE_WEAK', 'wait 6', 'click 700 25', 'wait 2', 'rec', 'click 300 200', 'wait 3'],
          {'STS_CHAR': 'Ironclad', 'STS_ENCOUNTER': ENC_SINGLE, 'STS_SCRIPT': '500:T160x215,520:A'}, 'yes', rec='460-700', mark='orig="INPUT click" ours=520')
    x_row('card_library', 'card library (NCardLibrary: filters by pool/rarity/type/cost, search, stats)', 'N_CARD_LIBRARY',
          ['rec', 'click 500 400', 'wait 3'], {'STS_SCRIPT': '60:DOWN,80:DOWN,100:A,140:A'}, 'yes', note=MENU_NAV_NOTE)
    x_row('bestiary', 'bestiary (NBestiary: monster list, entries, move buttons, layouts)', 'N_BESTIARY',
          ['con bestiary', 'wait 4', 'rec', 'click 300 250', 'wait 3'], {'STS_BESTIARY_FOCUS': '1'}, 'yes', note='original: `con bestiary` opens it from a run',
          mark='orig="INPUT click" ours=60')
    x_row('relic_collection', 'relic collection (NRelicCollection: categories, locked silhouettes)', 'N_RELIC_COLLECTION',
          ['rec', 'click 500 400', 'wait 3'], {'STS_COLLECTION_FOCUS': '1'}, 'yes', note=MENU_NAV_NOTE)
    x_row('potion_lab', 'potion lab (NPotionLab: potions by rarity, locked)', 'N_POTION_LAB',
          ['rec', 'click 500 400', 'wait 3'], {'STS_COLLECTION_FOCUS': '1'}, 'partial', note=MENU_NAV_NOTE)
    x_row('run_history', 'run history screen (NRunHistory: list, deck, relics, map-point history)', 'N_RUN_HISTORY',
          ['rec', 'click 500 400', 'wait 3'], {'STS_OPEN_HISTORY': '1'}, 'yes', note=MENU_NAV_NOTE)
    x_row('stats', 'statistics screen (NStatsScreen: general stats, character stats tabs)', 'N_STATS_SCREEN',
          ['rec', 'click 500 400', 'wait 3'], {'STS_OPEN_ACHIEVEMENTS': '1'}, 'yes', note=MENU_NAV_NOTE)
    x_row('achievements', 'achievements grid (NAchievementsGrid, toast on unlock)', 'N_ACHIEVEMENTS_GRID',
          ['rec', 'click 500 400', 'wait 3'], {'STS_OPEN_ACHIEVEMENTS': '1', 'STS_ACHIEVE_TOAST': '1'}, 'yes', note=MENU_NAV_NOTE)
    x_row('settings', 'settings screen (NSettingsScreen: tabs, sliders, tickboxes, dropdowns)', 'N_SETTINGS_SCREEN',
          ['rec', 'click 500 450', 'wait 3'], {'STS_SCRIPT': '60:DOWN,80:A'}, 'partial', note='3DS has fewer settings (no resolution/vsync/modding rows); ' + MENU_NAV_NOTE)
    x_row('pause_menu', 'pause menu in a run (NPauseMenu: resume/settings/abandon/menu)', 'N_PAUSE_MENU',
          ['con fight SHRINKER_BEETLE_WEAK', 'wait 6', 'rec', 'key 53', 'wait 3'], {'STS_CHAR': 'Ironclad', 'STS_ENCOUNTER': ENC_SINGLE, 'STS_SCRIPT': '500:START'}, 'yes',
          rec='460-700', mark='orig="INPUT key" ours=500')
    x_row('game_over', 'game over screen (NGameOverScreen: score lines, badges, discovered items, continue)', 'N_GAME_OVER_SCREEN',
          ['con fight SHRINKER_BEETLE_WEAK', 'wait 6', 'rec', 'con die', 'wait 8'], {'STS_CHAR': 'Ironclad', 'STS_ENCOUNTER': ENC_SINGLE, 'STS_SCRIPT': '500:Cdie'}, 'yes',
          rec='460-1000', mark='orig="INPUT con die" ours=500')
    x_row('victory', 'victory screen after the final boss (run summary, badges)', 'N_GAME_OVER_SCREEN',
          ['con act 3', 'wait 6', 'con room Boss', 'wait 6', 'rec', 'con win', 'wait 8'], {'STS_ACT': '3', 'STS_ROOM': 'Boss', 'STS_SCRIPT': '500:Cwin'}, 'yes',
          rec='460-1000', mark='orig="INPUT con win" ours=500')
    x_row('timeline', 'timeline screen (NTimelineScreen: epoch slots, eras, chapters)', 'N_TIMELINE_SCREEN',
          ['rec', 'click 500 400', 'wait 3'], {'STS_OPEN_HISTORY': '1'}, 'no', note='original menu: Timeline; port has no timeline screen (only epoch reveal on game over). ' + MENU_NAV_NOTE)
    x_row('unlock_screens', 'epoch unlock screens (NUnlockCardsScreen, NUnlockRelicsScreen, NUnlockCharacterScreen, ...)', 'N_UNLOCK_SCREEN',
          ['rec', 'wait 3'], {'STS_SEEN_ALL': '0'}, 'no', note='triggered by timeline progress; check game over epoch reveal in ours')
    x_row('daily_run', 'daily run screen (NDailyRunScreen: today seed, modifiers, leaderboard)', 'N_DAILY_RUN_SCREEN',
          ['rec', 'click 500 400', 'wait 3'], {'STS_OPEN_DAILY': '1'}, 'partial', note='leaderboard needs online: expected offline in both. ' + MENU_NAV_NOTE)
    x_row('custom_run', 'custom run screen (NCustomRunScreen: modifier tickboxes, character row, seed)', 'N_CUSTOM_RUN_SCREEN',
          ['rec', 'click 500 400', 'wait 3'], {'STS_OPEN_CUSTOM': '1'}, 'yes', note=MENU_NAV_NOTE)
    x_row('credits', 'credits scroll (NCreditsScreen)', 'N_CREDITS_SCREEN',
          ['rec', 'click 500 400', 'wait 5'], {'STS_OPEN_CREDITS': '1'}, 'yes', note=MENU_NAV_NOTE)
    x_row('profile', 'profile screen (NProfileScreen: profile slots, icons, delete)', 'N_PROFILE_SCREEN',
          ['rec', 'click 900 40', 'wait 3'], {'STS_SCRIPT': '60:X'}, 'yes', note=MENU_NAV_NOTE)
    x_row('compendium', 'compendium submenu (NCompendiumSubmenu: cards/relics/potions/bestiary/stats buttons)', 'N_COMPENDIUM_SUBMENU',
          ['rec', 'click 500 330', 'wait 3'], {'STS_SCRIPT': '60:DOWN,80:A'}, 'yes', note=MENU_NAV_NOTE)
    x_row('card_pile_screen', 'combat pile card screen (NCardPileScreen / NCombatPileCardSelectScreen)', 'N_CARD_PILE_SCREEN',
          ['con fight SHRINKER_BEETLE_WEAK', 'wait 6', 'rec', 'click 45 520', 'wait 3'], {'STS_CHAR': 'Ironclad', 'STS_ENCOUNTER': ENC_SINGLE, 'STS_SCRIPT': '500:T20x215'}, 'yes',
          rec='460-700', mark='orig="INPUT click" ours=500')
    x_row('inspect_relic', 'inspect relic screen (NInspectRelicScreen)', 'N_INSPECT_RELIC_SCREEN',
          ['con relic VAJRA', 'wait 2', 'rec', 'click 40 70', 'wait 3'], {'STS_ROOM': 'Treasure', 'STS_RELICS': 'Vajra'}, 'partial', mark='orig="INPUT click" ours=420', rec='400-700')
    x_row('boot_disclaimer', 'boot logo animation / early-access disclaimer / patch notes (NLogoAnimation, NEarlyAccessDisclaimer, NPatchNotesScreen)', 'N_LOGO_ANIMATION',
          ['rec', 'wait 6'], {}, 'partial', note='3DS has its own boot sequence; compare logo timing only', rec='0-300')
    x_row('mp_lobby', 'multiplayer: host/join lobby screens (NMultiplayerHostSubmenu, NJoinFriendScreen)', 'N_MULTIPLAYER_SUBMENU',
          ['rec', 'wait 2'], {}, 'no', note='multiplayer is out of scope on 3DS (expected no)')
    x_row('feedback_modding', 'feedback / modding screens (NSendFeedbackScreen, NModdingScreen)', 'N_SEND_FEEDBACK_SCREEN',
          ['rec', 'wait 2'], {}, 'no', note='not applicable on 3DS (expected no)')


# ------------------------------------------------------------------------------- VFX mapping
def vfx_classes():
    out = {}
    for d in sorted(os.listdir(DEC)):
        if d.startswith('MegaCrit.Sts2.Core.Nodes.Vfx'):
            sub = d[len('MegaCrit.Sts2.Core.'):]
            for f in sorted(os.listdir(os.path.join(DEC, d))):
                if f.endswith('.cs'):
                    out[f[:-3]] = sub
    return out


VFX = vfx_classes()
NONVISUAL = {'IDeathDelayer', 'VfxColor', 'VfxDuration', 'VfxPosition', 'DialogueSide', 'DialogueStyle', 'LocalizedTexture',
             'RumbleStyle', 'ShakeDuration', 'ShakeStrength', 'ShakeInstance', 'ScreenPunchInstance', 'ScreenRumbleInstance',
             'ScreenTraumaRumble', 'NParticlesContainer', 'NVfxParticleSystem', 'NVfxProjectile', 'NVfxProjectileHandler',
             'NSpineSpriteBoneFollower', 'NSpineSpriteCopier', 'NSpriteAnimator', 'NTrail2D', 'NValueRamp', 'NVfxSpine',
             'NShaker', 'NFollowCursor', 'NVfxSpawner', 'NBasicTrail', 'NBezierTrail', 'NNoiseScroller', 'NGlowExampleVfx',
             'NSaturationExampleVfx', 'NAdditiveOverlayVfx', 'NLiquidOverlayVfx'}
OURS_VFX_DONE = {'NHitSparkVfx', 'NBlockSparkVfx', 'NBlockBrokenVfx', 'NPoisonImpactVfx', 'NPowerAppliedBuffVfx',
                 'NPowerAppliedDebuffVfx', 'NDamageNumVfx', 'NHealNumVfx', 'NLowHpBorderVfx', 'NScreenShake',
                 'PlayerHurtVignetteHelper', 'NCardFlyVfx'}

# hand mapping (overrides auto): class -> row keys (resolved against BY_KEY)
HAND_VFX = {
    'NHitSparkVfx': ['U:damage_numbers'], 'NBlockSparkVfx': ['U:hp_bar'], 'NBlockBrokenVfx': ['U:hp_bar'],
    'NDamageBlockedVfx': ['U:hp_bar'], 'NDamageNumVfx': ['U:damage_numbers'], 'NHealNumVfx': ['U:damage_numbers'],
    'PlayerFullscreenHealVfx': ['X:rest_heal'], 'NLowHpBorderVfx': ['U:low_hp_vignette'],
    'PlayerHurtVignetteHelper': ['U:low_hp_vignette'], 'NScreenShake': ['U:screen_shake'], 'NHitStop': ['U:screen_shake'],
    'NPowerAppliedVfx': ['U:power_icons'], 'NPowerAppliedBuffVfx': ['U:power_icons'], 'NPowerAppliedDebuffVfx': ['U:power_icons'],
    'NPowerFlashVfx': ['U:power_icons'], 'NPowerRemovedVfx': ['U:power_icons'], 'NPowerUpVfx': ['U:power_icons'],
    'NCardFlyVfx': ['U:card_play_queue'], 'NCardFlyPowerVfx': ['U:card_play_queue'], 'NCardFlyShuffleVfx': ['U:card_play_queue'],
    'NCardTrail': ['U:card_play_queue'], 'NCardTrailVfx': ['U:card_play_queue'], 'NCardExhaustVfx': ['U:exhaust_pile'],
    'NCardExhaustQuickVfx': ['U:exhaust_pile'], 'NCardRemoveVfx': ['X:card_grid'], 'NCardTransformVfx': ['X:card_grid'],
    'NCardTransformShineVfx': ['X:card_grid'], 'NCardUpgradeVfx': ['X:rest_smith'], 'NCardSmithVfx': ['X:rest_smith'],
    'NCardEnchantVfx': ['U:enchant_afflict'], 'NCardRareGlow': ['U:card_glow'], 'NCardUncommonGlow': ['U:card_glow'],
    'NPotionFlashVfx': ['U:potion_belt'], 'NRelicFlashVfx': ['U:relic_bar'], 'NUiFlashVfx': ['U:top_bar'],
    'NRestSiteFireVfx': ['X:rest'], 'NRestSmokeVfx': ['X:rest'], 'NMapCircleVfx': ['X:map'], 'NMapNodeSelectVfx': ['X:map_select'],
    'NMapPingVfx': ['X:map_draw'], 'NEpochHighlightVfx': ['X:timeline'], 'NEpochOffscreenVfx': ['X:timeline'],
    'NEpochSlotParticle': ['X:timeline'], 'NGainEpochVfx': ['X:timeline'], 'NEpochChains': ['X:timeline'],
    'NFailedJoinVfx': ['X:mp_lobby'], 'NGaseousScreenVfx': ['X:event_b'], 'NSymbioteEyeMovementVfx': ['X:event_b'],
    'NSpeechBubbleVfx': ['X:ancient'], 'NThoughtBubbleVfx': ['X:ancient'], 'NFullscreenTextVfx': ['U:banner_start'],
    'NDesaturateTransitionVfx': ['L:death_ironclad'], 'NSmokyVignetteVfx': ['U:low_hp_vignette'],
    'NRadialBlurVfx': ['U:screen_shake'], 'NMonsterDeathVfx': ['M:anim_death'], 'NSleepingVfx': ['M:anim_sleep_wake'],
    'NStunnedVfx': ['M:anim_stun'], 'NRainVfx': ['U:combat_bg'], 'NFireBurningVfx': ['M:anim_hit'],
    'NFireBurstVfx': ['M:anim_hit'], 'NFireSmokePuffVfx': ['M:anim_hit'], 'NSmokePuffVfx': ['M:anim_hit'],
    'NGroundFireVfx': ['P:pot_thrown_dmg'], 'NItemThrowVfx': ['P:pot_thrown_dmg'], 'NGasBombVfx': ['P:pot_thrown_aoe'],
    'NSplashVfx': ['P:pot_thrown_debuff'], 'NOilSpillVfx': ['P:pot_thrown_debuff'],
    'NPoisonImpactVfx': ['I:sl_poison', 'S:sl_poison'], 'NDoomVfx': ['N:nb_doom'], 'NDoomOverlayVfx': ['N:nb_doom'],
    'NDoomSubEmitterVfx': ['N:nb_doom'], 'NShivThrowVfx': ['S:sl_shiv'], 'NSovereignBladeVfx': ['R:rg_forge'],
    'NStarryImpactVfx': ['R:rg_stars'], 'NIroncladVfx': ['L:fight_start_ironclad'], 'NDefectVfx': ['L:fight_start_defect'],
    'NRegentVfx': ['L:fight_start_regent'], 'NNecrobinderVfx': ['L:fight_start_necrobinder'],
    'NAeonGlassVfx': ['B'], 'NAmalgamVfx': ['X:event_b'], 'NBounceSparkVfx': ['M:anim_hit'], 'NHeavyBluntVfx': ['card:Bludgeon'],
    'NLaserVfx': ['M:anim_attack'], 'NLivingGasVfx': ['M:anim_attack'], 'NLostAndForgottenVfx': ['M:anim_attack'],
    'NScreamVfx': ['M:anim_cast'], 'NSpookyScreamVfx': ['M:anim_cast'], 'NSpookyHandVfx': ['M:anim_cast'],
    'NBgGroundSpikeVfx': ['M:anim_attack'], 'NFgGroundSpikeVfx': ['M:anim_attack'],
}

FOLDER_ROWS = {
    'Models.Relics': 'U:relic_bar', 'Models.Events': 'X:event_a', 'Nodes.Events': 'X:event_a', 'Nodes.Rooms': 'U:combat_bg',
    'Nodes.Orbs': 'U:orbs', 'Models.Orbs': 'U:orb_evoke', 'Nodes.Combat': 'U:card_drag', 'Nodes.Cards': 'U:card_glow',
    'Nodes.Screens.Map': 'X:map', 'Nodes.RestSite': 'X:rest', 'Nodes.CommonUi': 'U:top_bar', 'Models.Enchantments': 'U:enchant_afflict',
    'Models.Afflictions': 'U:enchant_afflict', 'Nodes.Screens.Timeline': 'X:timeline', 'Nodes.Screens.Shops': 'X:shop',
    'Nodes.TreasureRooms': 'X:treasure', 'Nodes.Rewards': 'X:rewards', 'Nodes.Screens.GameOverScreen': 'X:game_over',
    'Nodes.Screens': 'X:deck_view', 'Nodes.Potions': 'U:potion_belt', 'Nodes.Relics': 'U:relic_bar',
}


def index_vfx_users():
    names = sorted(VFX)
    rx = re.compile(r'\b(' + '|'.join(re.escape(n) for n in names) + r')\b')
    users = {n: set() for n in names}
    for root, _, files in os.walk(DEC):
        rel = os.path.relpath(root, DEC)
        folder = rel[len('MegaCrit.Sts2.Core.'):] if rel.startswith('MegaCrit.Sts2.Core.') else rel
        for f in sorted(files):
            if not f.endswith('.cs'):
                continue
            stem = f[:-3]
            t = rd(os.path.join(root, f))
            for n in set(rx.findall(t)):
                if n != stem:
                    users[n].add((folder, stem))
    return users


def name_fallback(cls, boss_monsters, normal_monsters):
    stem = cls[1:] if cls.startswith('N') else cls
    stem = re.sub(r'Vfx$', '', stem)
    for suf in ('Background', 'Bg', 'Explosion', 'Impact', 'Trail', 'Segment', 'Vines', 'Rocks', 'Attack', 'Sword', 'Hand', 'Scream', 'Beam'):
        if stem.endswith(suf) and len(stem) > len(suf) + 3:
            stem = stem[:-len(suf)]
    stem2 = re.sub(r'Boss$', '', stem)
    if stem in CARD_ROW_OF:
        return CARD_ROW_OF[stem]
    for c, k in sorted(CARD_ROW_OF.items()):
        if len(c) >= 6 and not c.startswith(('P:', 'PO:')) and (stem.startswith(c) or c.startswith(stem2) and len(stem2) >= 6):
            return k
    for m in sorted(MON):
        if len(m) >= 5 and (m.startswith(stem2) or stem2.startswith(m)):
            if m in normal_monsters:
                return 'M:anim_attack'
            if m in boss_monsters:
                return 'B'
    for e in sorted(ENC):
        if len(stem2) >= 5 and e.startswith(stem2):
            return 'B' if ENC[e]['room'] != 'Monster' else 'M:anim_attack'
    return None


def map_vfx():
    users = index_vfx_users()
    boss_monsters, normal_monsters = set(), set()
    for n, e in ENC.items():
        for m in e['monsters']:
            (normal_monsters if e['room'] in ('Monster',) else boss_monsters).add(m)
    mapping, b_bucket, nonvisual, unmapped = {}, [], [], []
    for cls in sorted(VFX):
        if cls in NONVISUAL:
            nonvisual.append(cls)
            continue
        rows = []
        if cls in HAND_VFX:
            rows = []
            for k in HAND_VFX[cls]:
                if k.startswith('card:'):
                    k = CARD_ROW_OF.get(k[5:], k)
                if k == 'B' or k in BY_KEY:
                    rows.append(k)
        if not rows:
            for folder, stem in sorted(users[cls]):
                k = None
                if folder == 'Models.Cards' or folder == 'Models.Cards.Mocks':
                    k = CARD_ROW_OF.get(stem)
                elif folder == 'Models.Powers':
                    k = CARD_ROW_OF.get('P:' + stem)
                elif folder == 'Models.Potions':
                    k = CARD_ROW_OF.get('PO:' + stem)
                elif folder == 'Models.Monsters':
                    if stem in normal_monsters:
                        k = 'M:anim_attack'
                    elif stem in boss_monsters:
                        k = 'B'
                elif folder in FOLDER_ROWS:
                    k = FOLDER_ROWS[folder]
                elif folder.startswith('Nodes.Vfx') and any(s.startswith(cls[:6]) for s in ()):
                    k = None
                if k and (k == 'B' or k in BY_KEY) and k not in rows:
                    rows.append(k)
        if not rows:
            k = name_fallback(cls, boss_monsters, normal_monsters)
            if k:
                rows = [k]
        if 'B' in rows and len(rows) > 1:
            rows = [r for r in rows if r != 'B']
        if rows == ['B']:
            b_bucket.append(cls)
        elif rows:
            mapping[cls] = rows[:3]
        else:
            # boss-specific class names (NKaiserCrabBossVfx ...) are referenced only through scene paths
            unmapped.append(cls)
    return mapping, b_bucket, nonvisual, unmapped


# ------------------------------------------------------------------------------- B rows
B_BEGIN, B_END = '<!-- B:begin -->', '<!-- B:end -->'
B_MARK = '<!-- B batch: hand-written, see tools/ref/scenes/B.txt -->'
HEADER = '| ID | batch | group | representative original id | orig setup | ours setup | ours implemented | status | finding |'
SEP = '|---|---|---|---|---|---|---|---|---|'


def old_b_block():
    if os.path.exists(DOC):
        t = rd(DOC)
        i, j = t.find(B_BEGIN), t.find(B_END)
        if 0 <= i < j:
            return t[i + len(B_BEGIN):j].strip('\n')
    return None


def esc(s):
    return s.replace('|', '\\|').replace('\n', ' ')


# ------------------------------------------------------------------------------- main
BATCH_TITLES = [
    ('L', 'Lifecycle per character (fight start, end turn, victory, death, character select)'),
    ('I', 'Ironclad card pool by visible behaviour'),
    ('S', 'Silent card pool by visible behaviour'),
    ('D', 'Defect card pool by visible behaviour'),
    ('R', 'Regent card pool by visible behaviour'),
    ('N', 'Necrobinder card pool by visible behaviour'),
    ('C', 'Colorless, status, curse, token, event, quest cards'),
    ('P', 'Powers by visual / behaviour group and potions by usage group'),
    ('M', 'Monster intents, move state machines, animations, enemy layouts'),
    ('B', 'Bosses and elites (hand-written)'),
    ('U', 'Combat UI nodes (Nodes.Combat, TopBar, Orbs, counters, piles, belts)'),
    ('X', 'Non-combat screens (Nodes.Screens.*, rooms)'),
]
SCENE_BATCHES = 'LISDRNCPMUX'


def finalize_rows():
    # U and X rows from hand tables
    for key, group, rep, steps, env, impl, vfx, char, note, mark in UI_ROWS:
        batch = 'X' if key.startswith('X:') else 'U'
        k = key[2:] if key.startswith('X:') else key
        add(Row(batch, k, group, rep, steps, env, impl, scene={'char': char, 'mark': mark, 'note': note, 'rep': rep}, vfx=[v for v in vfx if v in VFX]))
    letter_order = {b: i for i, (b, _) in enumerate(BATCH_TITLES)}
    # stable: group by batch keeping creation order
    ROWS.sort(key=lambda r: letter_order[r.batch])
    counters = {}
    for r in ROWS:
        counters[r.batch] = counters.get(r.batch, 0) + 1
        r.id = '%s%02d' % (r.batch, counters[r.batch])


def apply_vfx(mapping):
    for cls, keys in mapping.items():
        for k in keys:
            BY_KEY[k].vfx.add(cls)
    for r in ROWS:
        if r.vfx and r.impl in ('yes',) and any(v not in OURS_VFX_DONE for v in r.vfx):
            r.impl = 'partial'
            r.note = 'bespoke VFX not ported'


def scene_text(r):
    sc = r.scene
    lines = ['name: %s_%s' % (r.id, snake(sc.get('rep', r.rep)) or r.key)]
    lines.append('char: ' + sc.get('char', 'Ironclad'))
    lines.append('orig: ' + '; '.join(r.orig_steps))
    ours = ' '.join('%s=%s' % (k, v) for k, v in r.ours.items())
    lines.append('ours: ' + ours)
    lines.append('mark: ' + sc.get('mark', 'orig="INPUT click" ours=500'))
    note = sc.get('note', '')
    lines.append('note: ' + (r.group.split(' [')[0] + ('; ' + note if note else '') + (' (ours %s)' % r.impl)))
    return '\n'.join(lines)


def main():
    want_scenes = '--scenes' in sys.argv
    build_lifecycle_rows()
    for c, letter in zip(CHARS, 'ISDRN'):
        build_card_rows(c, letter)
    build_colorless_rows()
    build_power_rows()
    build_potion_rows()
    build_monster_rows()
    build_ui_rows()
    build_screen_rows()
    finalize_rows()
    mapping, b_bucket, nonvisual, unmapped = map_vfx()
    apply_vfx(mapping)

    # ---- markdown
    out = []
    w = out.append
    w('# Original vs port side-by-side checklist')
    w('')
    w('Generated by `python3 tools/ref/gen_checklist.py` (add `--scenes` to also write `tools/ref/scenes/<batch>.txt`).')
    w('Do not edit by hand outside the B block (kept between the B markers) and the `status` / `finding` cells,')
    w('which are reset to `todo` / empty on regeneration unless you copy them out first. Only ids, class names and')
    w('short descriptions are emitted: no game code or text.')
    w('')
    w('Purpose: every row is one visible behaviour that the recording runner plays in the original game (dev console,')
    w('see `tools/ref/game.sh`) and in our SDL preview (`STS_*` env vars), so frames can be compared side by side.')
    w('')
    w('## Columns')
    w('')
    w('- **ID**: batch letter + number; scene name prefix in `tools/ref/scenes/` (e.g. `I07_bash`).')
    w('- **batch**: L lifecycle, I/S/D/R/N card pools of Ironclad/Silent/Defect/Regent/Necrobinder, C colorless+status+curse+token,')
    w('  P powers + potions, M monsters/intents, B bosses+elites (hand-written), U combat UI, X non-combat screens.')
    w('- **group**: what the row shows; `[n: members...]` is the member count and first members; `vfx:` lists original VFX classes that')
    w('  appear in this row (cap 6, the full mapping is in the VFX index at the end).')
    w('- **representative original id**: the dev-console id (Slugified class name) of one member (`KaiserCrabBoss` -> `KAISER_CRAB_BOSS`).')
    w('- **orig setup**: original dev console commands / runner actions (`play last->enemy` drags the rightmost hand card).')
    w('- **ours setup**: env vars of our SDL preview. `STS_POWERS`, `STS_ORBS`, `STS_STARS`, `STS_ENERGY`, `STS_HAND`, `STS_PILE_DRAW`,')
    w('  `STS_PILE_DISCARD` and the script items `F:C<cmd>` are PLANNED switches that may not exist yet (`_` in `<cmd>` = space).')
    w('- **ours implemented**: `yes` / `partial` / `no`. Cards, powers, potions, monsters and encounters are matched against the')
    w('  registrations in our C++ sources (`partial` = only some members registered, or the rules exist but the bespoke VFX class')
    w('  mapped to the row is not ported). L/U/X rows are hand-set in the script (`build_lifecycle_rows`, `build_ui_rows`, `build_screen_rows`).')
    w('- **status**: `todo` until compared; **finding**: free text after comparing.')
    w('')

    counts = {}
    for r in ROWS:
        counts.setdefault(r.batch, {'n': 0, 'yes': 0, 'partial': 0, 'no': 0})
        counts[r.batch]['n'] += 1
        counts[r.batch][r.impl] += 1
    old_b = old_b_block()
    b_rows = 0
    if old_b:
        b_rows = len([l for l in old_b.splitlines() if re.match(r'\|\s*B\d', l)])
    w('## Summary')
    w('')
    w('| batch | title | rows | yes | partial | no |')
    w('|---|---|---|---|---|---|')
    tot = {'n': 0, 'yes': 0, 'partial': 0, 'no': 0}
    for b, title in BATCH_TITLES:
        if b == 'B':
            w('| B | %s | %d | - | - | - |' % (title, b_rows))
            tot['n'] += b_rows
            continue
        c = counts.get(b, {'n': 0, 'yes': 0, 'partial': 0, 'no': 0})
        w('| %s | %s | %d | %d | %d | %d |' % (b, title, c['n'], c['yes'], c['partial'], c['no']))
        for k in tot:
            tot[k] += c[k]
    w('| | **total** | %d | %d | %d | %d |' % (tot['n'], tot['yes'], tot['partial'], tot['no']))
    w('')
    w('Generic card/power/potion scenes use encounter `%s` (single target) and `%s` (several enemies).' % (ENC_SINGLE, ENC_MULTI))
    w('')

    for b, title in BATCH_TITLES:
        w('## %s: %s' % (b, title))
        w('')
        if b == 'B':
            w(B_MARK)
            w('')
            w(B_BEGIN)
            if old_b:
                w(old_b)
            else:
                w(HEADER)
                w(SEP)
            w(B_END)
            w('')
            continue
        w(HEADER)
        w(SEP)
        for r in [x for x in ROWS if x.batch == b]:
            grp = r.group
            if r.vfx:
                vs = sorted(r.vfx)
                grp += '; vfx: ' + ', '.join(vs[:6]) + (' +%d' % (len(vs) - 6) if len(vs) > 6 else '')
            fin = ''
            w('| %s | %s | %s | %s | %s | %s | %s | todo | %s |' % (
                r.id, r.batch, esc(grp), esc(r.rep), esc(r.orig_table()), esc(r.ours_table()), r.impl, fin))
        w('')

    # ---- VFX index
    w('## VFX index (Nodes.Vfx*)')
    w('')
    w('%d classes scanned: %d mapped to rows, %d boss/elite specific (B batch), %d non-visual helpers, %d unmapped.' % (
        len(VFX), len(mapping), len(b_bucket), len(nonvisual), len(unmapped)))
    w('')
    w('| VFX class | folder | rows | ported in ours |')
    w('|---|---|---|---|')
    row_ids = {k: r.id for k, r in BY_KEY.items()}
    for cls in sorted(mapping):
        w('| %s | %s | %s | %s |' % (cls, VFX[cls].replace('Nodes.', ''), ', '.join(row_ids[k] for k in mapping[cls]),
                                     'yes' if cls in OURS_VFX_DONE else 'no'))
    w('')
    w('### VFX handled by the B batch (boss / elite specific)')
    w('')
    w(', '.join(b_bucket) if b_bucket else '(none)')
    w('')
    w('### Non-visual helper types (no row)')
    w('')
    w(', '.join(nonvisual))
    w('')
    w('### Unmapped VFX')
    w('')
    if unmapped:
        w('| VFX class | folder |')
        w('|---|---|')
        for cls in unmapped:
            w('| %s | %s |' % (cls, VFX[cls]))
    else:
        w('(none)')
    w('')

    os.makedirs(os.path.dirname(DOC), exist_ok=True)
    with open(DOC, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(out) + '\n')

    if want_scenes:
        os.makedirs(SCENE_DIR, exist_ok=True)
        for b in SCENE_BATCHES:
            blocks = ['# Scenes for batch %s (%s). Generated by tools/ref/gen_checklist.py --scenes; do not edit.' % (b, dict(BATCH_TITLES)[b])]
            for r in [x for x in ROWS if x.batch == b]:
                blocks.append(scene_text(r))
            with open(os.path.join(SCENE_DIR, b + '.txt'), 'w', encoding='utf-8', newline='\n') as f:
                f.write('\n\n'.join(blocks) + '\n')

    # ---- console summary
    for b, _ in BATCH_TITLES:
        c = counts.get(b)
        if c:
            print('%s rows=%d yes=%d partial=%d no=%d' % (b, c['n'], c['yes'], c['partial'], c['no']))
    print('VFX total=%d mapped=%d B=%d nonvisual=%d unmapped=%d' % (len(VFX), len(mapping), len(b_bucket), len(nonvisual), len(unmapped)))
    if unmapped:
        print('unmapped: ' + ', '.join(unmapped))


if __name__ == '__main__':
    main()
