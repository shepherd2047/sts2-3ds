#!/usr/bin/env python3
"""Build romfs/ for the 3DS port from the user's own Slay the Spire 2 install.

Everything is read from the local PCK; nothing is downloaded. Output:
  romfs/gfx/atlas_N.t3t + atlas.txt   sprites (card art, creatures, icons)
  romfs/gfx/*.t3t                      full-screen backgrounds
  romfs/font/font_N.t3t + font.txt     bitmap font (only glyphs used)
  romfs/loc.txt                        key<TAB>value strings (zhs)
"""
import argparse
import glob
import os
import re
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gdpck import Game  # noqa: E402
import spine_render  # noqa: E402

ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, 'romfs')

def skeleton_res(scene_text):
    """res:// path of the SpineSkeletonDataResource a creature scene uses."""
    m = re.search(r'type="SpineSkeletonDataResource"[^\n]*path="res://([^"]+\.tres)"', scene_text)
    return m.group(1)


def keys_from_source(macro):
    """Loc keys declared in C++ via CARD_HEADER / POWER_HEADER."""
    keys = []
    for src in glob.glob(os.path.join(ROOT, 'source', 'core', '*.[ch]*')):
        keys += re.findall(macro + r'\(\w+,\s*"([A-Z0-9_]+)"', open(src, encoding='utf-8').read())
    return sorted(set(keys))


CARDS_FIXED = ['STRIKE_IRONCLAD', 'DEFEND_IRONCLAD', 'BASH', 'ANGER', 'TWIN_STRIKE', 'SWORD_BOOMERANG',
         'BREAKTHROUGH', 'HEADBUTT', 'THUNDERCLAP', 'BODY_SLAM', 'IRON_WAVE', 'POMMEL_STRIKE', 'CINDER',
         'SETUP_STRIKE', 'MOLTEN_FIST', 'PERFECTED_STRIKE', 'HAVOC', 'TREMBLE', 'SHRUG_IT_OFF',
         'ARMAMENTS', 'TAUNT', 'BLOOD_WALL', 'TRUE_GRIT', 'SLIMED', 'WOUND']
STATUS_CARDS = {'SLIMED', 'WOUND'}
POWERS_FIXED = ['STRENGTH_POWER', 'DEXTERITY_POWER', 'VULNERABLE_POWER', 'WEAK_POWER', 'FRAIL_POWER',
          'SHRINK_POWER', 'SLIPPERY_POWER', 'TERRITORIAL_POWER', 'TEMPORARY_STRENGTH_POWER']
# Monster keys come from MONSTER_HEADER(Name, "KEY") in source/core (see build()).
# Relic keys come from RELIC_HEADER(Name, "KEY", Rarity) in source/core (see build()).
RELIC_ICON = 48  # drawn at 18 px in the top bar, 48-64 px when a relic is shown on its own

# 1920x1080 game space -> 400x240 top screen is ~0.21. Creatures keep the game's own
# proportions against the room, like the RGDSplus port (no enlargement).
CREATURE_SCALE = 0.21
CREATURE_BOX = (170, 150)  # largest sprite that still fits beside the others
# Slots whose pose depends on constraints the offline renderer does not solve.
HIDE_SLOTS = {'VANTOM': ('mega', 'whip', 'tail')}
PORTRAIT_SIZE = (112, 85)


# ---------------------------------------------------------------- texture files

def write_t3t(path, img):
    """T3T1 container: u16 w, u16 h, u8 fmt (0 = RGBA8), 3 pad, RGBA bytes."""
    img = img.convert('RGBA')
    with open(path, 'wb') as f:
        f.write(b'T3T1')
        f.write(struct.pack('<HHB3x', img.width, img.height, 0))
        f.write(img.tobytes())


class Packer:
    """Shelf packer into power-of-two pages (3DS max 1024x1024)."""

    def __init__(self, size=1024, pad=1):
        self.size = size
        self.pad = pad
        self.pages = []
        self.entries = []

    def _new_page(self):
        self.pages.append({'img': Image.new('RGBA', (self.size, self.size), (0, 0, 0, 0)),
                           'shelves': [], 'y': 0})
        return self.pages[-1]

    def add(self, name, img, anchor=(0, 0)):
        w, h = img.width + self.pad * 2, img.height + self.pad * 2
        if w > self.size or h > self.size:
            raise ValueError(f'{name} too big: {img.size}')
        for pi, page in enumerate(self.pages + [None]):
            if page is None:
                page = self._new_page()
                pi = len(self.pages) - 1
            for shelf in page['shelves']:
                if shelf['h'] >= h and shelf['x'] + w <= self.size and shelf['h'] <= h * 1.5:
                    x, y = shelf['x'], shelf['y']
                    shelf['x'] += w
                    break
            else:
                if page['y'] + h > self.size:
                    continue
                shelf = {'y': page['y'], 'h': h, 'x': w}
                page['shelves'].append(shelf)
                page['y'] += h
                x, y = 0, shelf['y']
            page['img'].paste(img, (x + self.pad, y + self.pad))
            self.entries.append((name, pi, x + self.pad, y + self.pad, img.width, img.height, anchor[0], anchor[1]))
            return
        raise RuntimeError('unreachable')


def shrink_page(img):
    """Trim unused bottom of the last page to the next power of two."""
    bbox = img.getbbox()
    if not bbox:
        return img
    h = 8
    while h < bbox[3]:
        h *= 2
    return img.crop((0, 0, img.width, h))


def fit(img, size):
    return img.resize(size, Image.LANCZOS)


def fit_height(img, h):
    return img.resize((max(1, round(img.width * h / img.height)), h), Image.LANCZOS)


# ---------------------------------------------------------------- sources

class Assets:
    def __init__(self, game):
        self.g = game
        self._atlas_pages = {}

    def sprite(self, tres):
        """AtlasTexture .tres -> cropped image."""
        t = self.g.pck.read(tres).decode()
        page = re.search(r'path="res://([^"]+)"', t).group(1)
        x, y, w, h = [float(v) for v in re.search(r'region = Rect2\(([^)]+)\)', t).group(1).split(',')]
        img = self._atlas_pages.get(page)
        if img is None:
            img = self.g.image(page)
            self._atlas_pages[page] = img
        return img.crop((int(x), int(y), int(x + w), int(y + h)))

    def card_portrait(self, key):
        snake = key.lower()
        cands = [f'images/packed/card_portraits/ironclad/{snake}.png',
                 f'images/packed/card_portraits/status/{snake}.png',
                 f'images/packed/card_portraits/status/beta/{snake}.png',
                 f'images/packed/card_portraits/colorless/{snake}.png',
                 f'images/packed/card_portraits/token/{snake}.png']
        for c in cands:
            if c + '.import' in self.g.pck.files:
                return self.g.image(c)
        # Some portraits only live in the card atlas.
        tres = f'images/atlases/card_atlas.sprites/ironclad/{snake}.tres'
        if tres in self.g.pck.files:
            return self.sprite(tres)
        print('  missing portrait', key)
        return Image.new('RGBA', (1000, 760), (60, 60, 60, 255))

    def creature(self, key):
        snake = key.lower()
        scene = f'scenes/creature_visuals/{snake}.tscn'
        t = self.g.pck.read(scene).decode()
        skel_res = skeleton_res(t)
        # Scale of the SpineSprite "Visuals" node.
        vis = t[t.find('[node name="Visuals"'):]
        m = re.search(r'\nscale = Vector2\(([-\d.]+), ([-\d.]+)\)', vis.split('\n[node', 1)[0])
        vscale = float(m.group(1)) if m else 1.0
        skel, atlas, load = self.g.spine(skel_res)
        img, origin = spine_render.render(skel, atlas, load, scale=vscale * CREATURE_SCALE, hide=HIDE_SLOTS.get(key, ()))
        return img, origin


def export_spine(g, key, skel_res, skel, atlas, load, scale):
    """Skeleton + atlas for the runtime: romfs/spine/KEY.skel, KEY.txt, KEY_N.t3t.

    KEY.txt lines:
      scale S                     skeleton units -> screen pixels
      page N FILE                 texture for atlas page N
      region PAGE A B C D E F U0 V0 U1 V1 NAME
                                  maps attachment uv (0..1 over the original,
                                    untrimmed image, v down) to texture uv:
                                    tu = A*u + B*v + C, tv = D*u + E*v + F
      mix FROM TO SECONDS
    """
    out = os.path.join(OUT, 'spine')
    with open(os.path.join(out, key + '.skel'), 'wb') as f:
        f.write(skel)
    pages, regions = spine_render.parse_atlas(atlas)
    lines = [f'scale {scale:.6f}']
    page_index = {}
    for i, page in enumerate(pages):
        img = load(page['file']).convert('RGBA')
        pw, ph = [int(v) for v in page['size'].split(',')] if 'size' in page else img.size
        if img.size != (pw, ph):
            img = img.resize((pw, ph), Image.LANCZOS)
        # Largest 3DS texture is 1024; pad to a power of two.
        s = min(1.0, 1024 / pw, 1024 / ph)
        sw, sh = max(1, round(pw * s)), max(1, round(ph * s))
        if s < 1.0:
            img = img.resize((sw, sh), Image.LANCZOS)
        tw, th = 8, 8
        while tw < sw:
            tw *= 2
        while th < sh:
            th *= 2
        canvas = Image.new('RGBA', (tw, th), (0, 0, 0, 0))
        canvas.paste(img, (0, 0))
        name = f'{key}_{i}.t3t'
        write_t3t(os.path.join(out, name), canvas)
        lines.append(f'page {i} spine/{name}')
        page_index[id(page)] = (i, sw / pw / tw, sh / ph / th)
    for name, r in regions.items():
        pi, kx, ky = page_index[id(r['page'])]
        if r['rot'] == 90:
            A, B, C = 0.0, r['oh'], r['x'] - (r['oh'] - r['oy'] - r['h'])
            D, E, F = -r['ow'], 0.0, r['y'] + r['w'] + r['ox']
        else:
            A, B, C = r['ow'], 0.0, r['x'] - r['ox']
            D, E, F = 0.0, r['oh'], r['y'] - (r['oh'] - r['oy'] - r['h'])
        # Packed (untrimmed) part of the original image, as uv bounds.
        u0, u1 = r['ox'] / r['ow'], (r['ox'] + r['w']) / r['ow']
        v0, v1 = (r['oh'] - r['oy'] - r['h']) / r['oh'], (r['oh'] - r['oy']) / r['oh']
        lines.append(f'region {pi} {A*kx:.7f} {B*kx:.7f} {C*kx:.7f} {D*ky:.7f} {E*ky:.7f} {F*ky:.7f} '
                     f'{u0:.6f} {v0:.6f} {u1:.6f} {v1:.6f} {name}')
    tres = g.pck.read(skel_res).decode()
    for m in re.finditer(r'from = "([^"]+)"\s*\nto = "([^"]+)"\s*\nmix = ([\d.]+)', tres):
        lines.append(f'mix {m.group(1)} {m.group(2)} {m.group(3)}')
    dm = re.search(r'default_mix = ([\d.]+)', tres)
    lines.append(f'defaultmix {dm.group(1) if dm else "0.1"}')
    with open(os.path.join(out, key + '.txt'), 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')


def build(args):
    global CARDS, POWERS, MONSTERS, RELICS
    CARDS = sorted(set(CARDS_FIXED) | set(keys_from_source('CARD_HEADER')))
    POWERS = sorted(set(POWERS_FIXED) | set(keys_from_source('POWER_HEADER')))
    MONSTERS = keys_from_source('MONSTER_HEADER')
    RELICS = keys_from_source('RELIC_HEADER')
    g = Game(args.pck) if args.pck else Game()
    a = Assets(g)
    os.makedirs(os.path.join(OUT, 'gfx'), exist_ok=True)
    os.makedirs(os.path.join(OUT, 'font'), exist_ok=True)
    packer = Packer()

    print('card art')
    for key in CARDS:
        packer.add('portrait/' + key, fit(a.card_portrait(key), PORTRAIT_SIZE))
    for kind in ('attack', 'skill', 'power'):
        packer.add(f'card/frame_{kind}', fit(a.sprite(f'images/atlases/ui_atlas.sprites/card/card_frame_{kind}_s.tres'), (120, 169)))
        packer.add(f'card/border_{kind}', fit_height(a.sprite(f'images/atlases/ui_atlas.sprites/card/card_portrait_border_{kind}_s.tres'), 96))
    packer.add('card/banner', fit_height(a.sprite('images/atlases/ui_atlas.sprites/card/card_banner.tres'), 28))
    packer.add('card/energy', fit(a.sprite('images/atlases/ui_atlas.sprites/card/energy_ironclad.tres'), (28, 28)))
    packer.add('card/unplayable', fit(a.sprite('images/atlases/ui_atlas.sprites/card/card_unplayable_icon.tres'), (24, 24)))

    print('creatures')
    os.makedirs(os.path.join(OUT, 'spine'), exist_ok=True)
    for key in MONSTERS + ['IRONCLAD']:
        t = g.pck.read(f'scenes/creature_visuals/{key.lower()}.tscn').decode()
        skel_res = skeleton_res(t)
        vis = t[t.find('[node name="Visuals"'):]
        m = re.search(r'\nscale = Vector2\(([-\d.]+), ([-\d.]+)\)', vis.split('\n[node', 1)[0])
        vscale = float(m.group(1)) if m else 1.0
        skel, atlas, load = g.spine(skel_res)
        scale = vscale * CREATURE_SCALE
        img, origin = spine_render.render(skel, atlas, load, scale=scale, hide=HIDE_SLOTS.get(key, ()))
        # Keep everything inside the top screen.
        f = min(1.0, CREATURE_BOX[0] / img.width, CREATURE_BOX[1] / img.height)
        if f < 1.0:
            img = img.resize((max(1, round(img.width * f)), max(1, round(img.height * f))), Image.LANCZOS)
            origin = (origin[0] * f, origin[1] * f)
        print(f'  {key}: {img.size}')
        packer.add('creature/' + key, img, (round(origin[0]), round(origin[1])))
        export_spine(g, key, skel_res, skel, atlas, load, scale * f)

    print('icons')
    for key in POWERS:
        snake = key.lower()
        path = f'images/powers/{snake}.png'
        if path + '.import' not in g.pck.files:
            tres = f'images/atlases/power_atlas.sprites/{snake}.tres'
            img = a.sprite(tres) if tres in g.pck.files else Image.new('RGBA', (64, 64), (200, 200, 200, 255))
        else:
            img = g.image(path)
        packer.add('power/' + key, fit(img, (24, 24)))
    for key in RELICS:
        packer.add('relic/' + key, fit(g.image(f'images/relics/{key.lower()}.png'), (RELIC_ICON, RELIC_ICON)))
    for i in range(1, 6):
        packer.add(f'intent/attack_{i}', fit(g.image(f'images/packed/intents/attack/intent_attack_{i}.png'), (30, 30)))
    for name, path in [('buff', 'buff/intent_buff_00'), ('defend', 'defend/intent_defend_00'),
                       ('debuff', 'debuff/intent_megadebuff_00'), ('status', 'card_debuff/intent_carddebuff_00')]:
        packer.add('intent/' + name, fit(g.image(f'images/packed/intents/{path}.png'), (30, 30)))
    for name in ('stun', 'summon', 'heal', 'unknown', 'escape', 'sleep'):
        packer.add('intent/' + name, fit(g.image(f'images/packed/intents/intent_{name}.png'), (30, 30)))
    for extra in ('debuff/intent_debuff_00', 'weak/intent_weak_00'):
        p = f'images/packed/intents/{extra}.png'
        if p + '.import' in g.pck.files:
            packer.add('intent/debuff_small', fit(g.image(p), (30, 30)))
            break
    packer.add('ui/block', fit(g.image('images/ui/combat/block.png'), (22, 22)))
    packer.add('ui/block_big', fit(g.image('images/ui/combat/block.png'), (32, 32)))
    packer.add('ui/reticle', fit(g.image('images/ui/combat/combat_reticle.png'), (40, 40)))
    # NTargetingArrow pieces, at the scale they are drawn (segments 0.28..0.42, head 0.95).
    head = g.image('images/ui/combat/targeting_arrow_head.png')
    seg = g.image('images/ui/combat/targeting_arrow_segment.png')
    print('  arrow', head.size, seg.size)
    packer.add('ui/arrow_head', fit(head, (max(1, round(head.width * 0.22)), max(1, round(head.height * 0.22)))))
    packer.add('ui/arrow_segment', fit(seg, (max(1, round(seg.width * 0.22)), max(1, round(seg.height * 0.22)))))
    packer.add('ui/end_turn', fit(g.image('images/packed/combat_ui/end_turn_button.png'), (84, 42)))
    packer.add('ui/draw_pile', fit(g.image('images/packed/combat_ui/draw_pile.png'), (30, 30)))
    packer.add('ui/discard_pile', fit(g.image('images/packed/combat_ui/discard_pile.png'), (30, 30)))
    orb = None
    for i in range(1, 6):
        layer = g.image(f'images/ui/combat/energy_counters/ironclad/ironclad_orb_layer_{i}.png')
        orb = layer if orb is None else Image.alpha_composite(orb, layer.resize(orb.size))
    packer.add('ui/energy_orb', fit(orb, (44, 44)))
    for name in ('monster', 'elite', 'rest', 'unknown', 'chest', 'shop', 'node_background'):
        packer.add('map/' + name, fit(a.sprite(f'images/atlases/ui_atlas.sprites/map/icons/map_{name}.tres'), (22, 22)))
    packer.add('map/marker', fit(a.sprite('images/atlases/ui_atlas.sprites/map/icons/map_marker_ironclad.tres'), (26, 26)))
    # Boss map nodes, one per act 1 boss (map/boss_<EncounterId>).
    # (Ceremonial Beast's map node is a Spine animation; the UI inks its creature sprite.)
    for enc, path in (('VantomBoss', 'images/map/placeholder/vantom_boss_icon.png'),
                      ('TheKinBoss', 'images/map/placeholder/the_kin_boss_icon.png')):
        packer.add('map/boss_' + enc, fit_height(g.image(path), 64))
    select = g.image('images/packed/character_select/char_select_ironclad.png')
    packer.add('ui/ironclad_select', fit_height(select, 120))
    icon = Image.new('RGBA', (48, 48), (40, 10, 10, 255))
    head = select.crop((0, 0, select.width, select.width)).resize((48, 48), Image.LANCZOS)
    icon.alpha_composite(head)
    icon.save(os.path.join(ROOT, 'icon.png'))

    with open(os.path.join(OUT, 'gfx', 'atlas.txt'), 'w', newline='\n') as f:
        for (name, page, x, y, w, h, ax, ay) in packer.entries:
            f.write(f'{name} {page} {x} {y} {w} {h} {ax} {ay}\n')
    for i, page in enumerate(packer.pages):
        img = shrink_page(page['img']) if i == len(packer.pages) - 1 else page['img']
        write_t3t(os.path.join(OUT, 'gfx', f'atlas_{i}.t3t'), img)
        if args.preview:
            os.makedirs(os.path.join(ROOT, 'build'), exist_ok=True); img.save(os.path.join(ROOT, 'build', f'preview_atlas_{i}.png'))
    print(f'  {len(packer.entries)} sprites in {len(packer.pages)} page(s)')

    print('backgrounds')
    # Combat room: all layers composited (StS2 layers are full-frame images).
    layers = [g.image(f'images/rooms/overgrowth/overgrowth_{n}.png') for n in ('00', '01_a', '02_a', '03_a', '04_a')]
    W, H = layers[0].size
    scene = Image.new('RGBA', (W, H), (0, 0, 0, 255))
    for im in layers:
        scene.alpha_composite(im.resize((W, H)))
    # Top: the camera's view (1920x1080 of the 2764.8x1296 scene), cropped to 5:3.
    k = W / 2764.8
    cw = 1920 * k
    ch = cw * 240 / 400
    cx, cy = W / 2, H / 2
    top = scene.crop((round(cx - cw / 2), round(cy - ch / 2), round(cx + cw / 2), round(cy + ch / 2)))
    top = top.resize((400, 240), Image.LANCZOS)
    canvas = Image.new('RGBA', (512, 256), (0, 0, 0, 255))
    canvas.paste(top, (0, 0))
    write_t3t(os.path.join(OUT, 'gfx', 'bg_overgrowth.t3t'), canvas)
    # Bottom: the floor of the same room (4:3), dimmed so cards stay readable.
    fh = H * 0.42
    fw = fh * 4 / 3
    floor = scene.crop((round(cx - fw / 2), round(H - fh), round(cx + fw / 2), H)).resize((320, 240), Image.LANCZOS)
    floor = Image.blend(floor, Image.new('RGBA', floor.size, (8, 10, 8, 255)), 0.35)
    fcanvas = Image.new('RGBA', (512, 256), (0, 0, 0, 255))
    fcanvas.paste(floor, (0, 0))
    write_t3t(os.path.join(OUT, 'gfx', 'bg_overgrowth_floor.t3t'), fcanvas)
    if args.preview:
        os.makedirs(os.path.join(ROOT, 'build'), exist_ok=True)
        canvas.save(os.path.join(ROOT, 'build', 'preview_bg.png'))
        fcanvas.save(os.path.join(ROOT, 'build', 'preview_floor.png'))
    # Map paper: the game's own MapBg (map_screen.tscn): top / middle / bottom parchment,
    # each fitted into a 1920x1080 box (keep-aspect -> 1527x1080) and stacked from y=-1620
    # to +1620. Baked at MAP_SCALE (ui.cpp kMapS) into one 260x551 strip.
    MAP_SCALE = 0.17
    parts = [g.image(f'images/packed/map/map_bgs/overgrowth/map_{p}_overgrowth.png') for p in ('top', 'middle', 'bottom')]
    pw, ph = round(1527 * MAP_SCALE), round(1080 * MAP_SCALE)
    sheet = Image.new('RGBA', (pw, ph * 3), (0, 0, 0, 0))
    for i, part in enumerate(parts):
        sheet.alpha_composite(part.resize((pw, ph), Image.LANCZOS), (0, i * ph))
    print('  map paper', sheet.size)
    mb = Image.new('RGBA', (512, 1024), (0, 0, 0, 0))
    mb.paste(sheet, (0, 0))
    write_t3t(os.path.join(OUT, 'gfx', 'bg_map.t3t'), mb)
    if args.preview:
        canvas.save(os.path.join(ROOT, 'build', 'preview_bg.png'))

    print('text')
    strings = {}

    def take(table, pred=lambda k: True):
        for k, v in g.loc('zhs', table).items():
            if pred(k):
                strings[f'{table}.{k}'] = v

    take('cards', lambda k: k.split('.')[0] in CARDS)
    take('powers', lambda k: k.split('.')[0] in POWERS)
    take('monsters', lambda k: k.split('.')[0] in MONSTERS)
    take('relics', lambda k: k.split('.')[0] in RELICS)
    for t in ('card_keywords', 'gameplay_ui', 'rest_site_ui', 'card_reward_ui', 'map', 'combat_messages',
              'card_selection', 'intents', 'game_over_screen', 'characters'):
        take(t, (lambda k: not k.startswith(('DAILY', 'DISCOVERY'))) if t == 'game_over_screen'
             else (lambda k: k.startswith('IRONCLAD')) if t == 'characters' else (lambda k: True))
    with open(os.path.join(OUT, 'loc.txt'), 'w', encoding='utf-8', newline='\n') as f:
        for k in sorted(strings):
            v = strings[k].replace('\\', '\\\\').replace('\n', '\\n').replace('\t', ' ')
            f.write(f'{k}\t{v}\n')
    print(f'  {len(strings)} strings')

    print('font')
    chars = set(chr(c) for c in range(32, 127))
    for v in strings.values():
        chars.update(v)
    # Strings written directly in the UI code.
    for src in glob.glob(os.path.join(ROOT, 'source', '**', '*.[ch]*'), recursive=True):
        chars.update(c for c in open(src, encoding='utf-8').read() if ord(c) > 127)
    chars.update('×→←↑↓…—“”·●○')
    chars = sorted(c for c in chars if c.isprintable() and c not in '\n\t')
    build_font(chars, args.font)


def default_font():
    """A font committed under tools/fonts/ keeps both machines' output identical;
    otherwise fall back to the system CJK font."""
    here = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fonts')
    for f in sorted(glob.glob(os.path.join(here, '*'))):
        if f.lower().endswith(('.otf', '.ttf', '.ttc')):
            return f
    for f in ('/System/Library/Fonts/STHeiti Medium.ttc', r'C:\Windows\Fonts\msyh.ttc',
              '/c/Windows/Fonts/msyh.ttc', '/usr/share/fonts/opentype/noto/NotoSansCJK-Medium.ttc'):
        if os.path.exists(f):
            return f
    sys.exit('no CJK font found; pass --font')


def build_font(chars, font_path):
    sizes = [12, 16]
    page_size = 1024
    page = Image.new('RGBA', (page_size, page_size), (255, 255, 255, 0))
    draw = ImageDraw.Draw(page)
    x = y = 1
    row_h = 0
    lines = []
    for si, size in enumerate(sizes):
        font = ImageFont.truetype(font_path, size, index=0)
        ascent, descent = font.getmetrics()
        lines.append(f'size {si} {size} {ascent + descent} {ascent}')
        for ch in chars:
            bbox = font.getbbox(ch)
            adv = round(font.getlength(ch))
            if ch == ' ':
                lines.append(f'g {si} {ord(ch)} 0 0 0 0 0 0 {adv}')
                continue
            if bbox[2] <= bbox[0] or bbox[3] <= bbox[1]:
                continue
            w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
            if x + w + 1 > page_size:
                x = 1
                y += row_h + 1
                row_h = 0
            if y + h + 1 > page_size:
                raise RuntimeError('font page full')
            draw.text((x - bbox[0], y - bbox[1]), ch, font=font, fill=(255, 255, 255, 255))
            lines.append(f'g {si} {ord(ch)} {x} {y} {w} {h} {bbox[0]} {bbox[1]} {adv}')
            x += w + 1
            row_h = max(row_h, h)
    page = shrink_page(page)
    write_t3t(os.path.join(OUT, 'font', 'font_0.t3t'), page)
    with open(os.path.join(OUT, 'font', 'font.txt'), 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    print(f'  {len(chars)} glyphs x {len(sizes)} sizes, page {page.size}')


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--pck', help='path to "Slay the Spire 2.pck" (default: Steam install)')
    ap.add_argument('--font', default=None, help='CJK font (default: tools/fonts/*, else a system font)')
    ap.add_argument('--preview', action='store_true', help='also write PNG previews')
    args = ap.parse_args()
    args.font = args.font or default_font()
    build(args)
