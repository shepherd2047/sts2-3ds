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
import json
import os
import re
import struct
import sys

import numpy as np
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


def creature_skeleton(g, key):
    """(skeleton resource, Visuals scale, hidden slot prefixes) of a monster's creature scene."""
    hide = HIDE_SLOTS.get(key, ())
    if key in KAISER_CRAB_KEEP:
        skel = g.spine(KAISER_CRAB_RES)[0]
        hide = tuple(sl['name'] for sl in spine_render.parse_skeleton(skel)['slots']
                     if not sl['name'].startswith(KAISER_CRAB_KEEP[key]))
        return KAISER_CRAB_RES, 0.5, hide
    t = g.pck.read(f'scenes/creature_visuals/{key.lower()}.tscn').decode()
    # Scale of the SpineSprite "Visuals" node.
    vis = t[t.find('[node name="Visuals"'):]
    m = re.search(r'\nscale = Vector2\(([-\d.]+), ([-\d.]+)\)', vis.split('\n[node', 1)[0])
    return skeleton_res(t), (abs(float(m.group(1))) if m else 1.0), hide



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
# Kaiser Crab: the two claws are separate creatures whose scenes have no skeleton (the C#
# animates one shared background skeleton). Each claw draws only its own arm's slots.
KAISER_CRAB_RES = 'animations/monsters/kaiser_crab/kaiser_crab_skeleton_data.tres'
# Monsters whose art is not centred on the skeleton root: drawn centred, standing on the ground.
RECENTER = ('CRUSHER', 'ROCKET', 'DECIMILLIPEDE_SEGMENT_FRONT', 'DECIMILLIPEDE_SEGMENT_MIDDLE',
            'DECIMILLIPEDE_SEGMENT_BACK')
KAISER_CRAB_KEEP = {
    'CRUSHER': ('arm_l_', 'claw_l_', 'claw_shine', 'claw_glow', 'stolen_shadow_l'),
    'ROCKET': ('arm_r_', 'claw_r_', 'claw_slime', 'claw_hole', 'thruster_attach', 'stolen_shadow'),
}
PORTRAIT_SIZE = (112, 85)

# Playable characters other than the Ironclad (X1.5-X4.5): scene / atlas key fragments used
# throughout this file, keyed like Character::key but lower-case.
OTHER_CHARS = ('silent', 'defect', 'regent', 'necrobinder')
# Card frame materials (CardPoolModel.CardFrameMaterialPath) keyed by the pool's EnergyColorName
# (Character::energyColor); Colorless/Status/Token/Event pools use card_frame_colorless, the
# Curse pool card_frame_curse. Baked as card/frame_<kind>_<key>.
CARD_FRAME_MATS = {'ironclad': 'red', 'silent': 'green', 'defect': 'blue', 'regent': 'orange',
                   'necrobinder': 'pink', 'colorless': 'colorless', 'curse': 'curse'}
# Portrait border / title banner materials (CardModel.BannerMaterialPath, by rarity; Basic,
# Common and Token fall back to common). Baked as card/border_<kind>_<rarity>, card/banner_<rarity>.
CARD_BANNER_RARITIES = ('common', 'uncommon', 'rare', 'curse', 'status', 'event')

# ---------------------------------------------------------------- texture files

# NTSC RGB<->YIQ matrix, exactly as shaders/hsv.gdshader's RGB_to_YIQ (mat3 columns).
_YIQ = np.array([[0.2989, 0.5870, 0.1140],
                  [0.5959, -0.2774, -0.3216],
                  [0.2115, -0.5229, 0.3114]])
_YIQ_INV = np.linalg.inv(_YIQ)


def _hue_rot(hue):
    c, s = np.cos(hue), np.sin(hue)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def hsv_shader(img, h, s, v):
    """Apply the game's hsv.gdshader ShaderMaterial to an RGBA image, matching its per-pixel
    math exactly (card frames, portrait borders and banners, F5/X*.5)."""
    arr = np.asarray(img.convert('RGBA'), dtype=np.float64) / 255.0
    rgb, a = arr[..., :3], arr[..., 3:4]
    yiq = rgb @ _YIQ.T
    yiq = yiq @ _hue_rot((1 - h) * 2 * np.pi).T
    yiq = yiq * np.array([1.0, s, s]) * v
    out = np.concatenate([np.clip(yiq @ _YIQ_INV.T, 0, 1), a], axis=-1)
    return Image.fromarray((out * 255).round().astype(np.uint8), 'RGBA')


def material_hsv(g, path):
    """(h, s, v) of an hsv.gdshader ShaderMaterial .tres (shader defaults 1, 1, 1)."""
    t = g.pck.read(path).decode()
    val = lambda k: float(m.group(1)) if (m := re.search(rf'shader_parameter/{k} = ([-\d.]+)', t)) else 1.0
    return val('h'), val('s'), val('v')


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


def bake_title_art(g, args):
    """Build the two-screen menu from the game's main-menu and Ironclad Spine art."""
    menu = Image.new('RGBA', (400, 480), (12, 13, 24, 255))
    # The two skeletons share the same scene origin. A centre crop keeps the tower
    # continuous through the 40 px side inset of the bottom screen.
    for part, x in (('bottom', -525), ('top', -506)):
        skel, atlas, load = g.spine(
            f'animations/backgrounds/mainmenu/{part}/main_menu_{part}_skel_data.tres')
        layer, _ = spine_render.render(skel, atlas, load, scale=0.15, animation='animation')
        layer = layer.resize((round(layer.width * 1.25), round(layer.height * 1.25)), Image.LANCZOS)
        menu.alpha_composite(layer, (x, 0))
    # S02: the sky layer leaves a few transparent rows at the top and bottom of the tall
    # background; stretch the covered rows over the whole 480 so neither screen has a dark
    # band. The logo is no longer baked in: it is the atlas sprite ui/menu_logo (add_ui_art).
    rgb = np.array(menu)[:, :, :3].astype(int)
    covered = np.where(np.abs(rgb - np.array((12, 13, 24))).sum(axis=2).max(axis=1) > 12)[0]
    if len(covered):
        menu = menu.crop((0, int(covered[0]), 400, int(covered[-1]) + 1)).resize((400, 480), Image.LANCZOS)
    canvas = Image.new('RGBA', (512, 512), (0, 0, 0, 255))
    canvas.paste(menu, (0, 0))
    write_t3t(os.path.join(OUT, 'gfx', 'bg_menu.t3t'), canvas)

    skel, atlas, load = g.spine('animations/character_select/ironclad/characterselect_ironclad_skel_data.tres')
    character, _ = spine_render.render(skel, atlas, load, scale=0.11, animation='animation')
    crop_w = round(character.height * 400 / 240)
    character = character.crop(((character.width - crop_w) // 2, 0,
                                (character.width + crop_w) // 2, character.height))
    character = character.resize((400, 240), Image.LANCZOS)
    canvas = Image.new('RGBA', (512, 256), (0, 0, 0, 255))
    canvas.paste(character, (0, 0))
    write_t3t(os.path.join(OUT, 'gfx', 'bg_character_ironclad.t3t'), canvas)
    if args.preview:
        menu.save(os.path.join(ROOT, 'build', 'preview_bg_menu.png'))
        character.save(os.path.join(ROOT, 'build', 'preview_bg_character_ironclad.png'))


def bake_other_character_art(g, packer, args):
    """X1.5-X4.5: the other three characters' select art (S04, not wired to a screen yet) —
    gfx/bg_character_<key>.t3t exactly like the Ironclad's above, plus the 120px-tall
    ui/<key>_select strip used for the character-select roster art."""
    for c in OTHER_CHARS:
        skel, atlas, load = g.spine(f'animations/character_select/{c}/characterselect_{c}_skel_data.tres')
        character, _ = spine_render.render(skel, atlas, load, scale=0.11, animation='animation')
        crop_w = round(character.height * 400 / 240)
        character = character.crop(((character.width - crop_w) // 2, 0,
                                    (character.width + crop_w) // 2, character.height))
        character = character.resize((400, 240), Image.LANCZOS)
        canvas = Image.new('RGBA', (512, 256), (0, 0, 0, 255))
        canvas.paste(character, (0, 0))
        write_t3t(os.path.join(OUT, 'gfx', f'bg_character_{c}.t3t'), canvas)
        select = g.image(f'images/packed/character_select/char_select_{c}.png')
        packer.add(f'ui/{c}_select', fit_height(select, 120))
        if args.preview:
            canvas.save(os.path.join(ROOT, 'build', f'preview_bg_character_{c}.png'))


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

    # X1.5-X4.5: card_portraits/ has one directory per pool (images/packed/card_portraits/<dir>/),
    # not just the Ironclad's; a card whose portrait was only ever drawn for one release shows up
    # under a "beta" subdirectory instead of the pool's own. Order matters only for cards that
    # exist in more than one place (there are none in practice), so this is just "try every pool".
    PORTRAIT_DIRS = ('ironclad', 'silent', 'defect', 'regent', 'necrobinder', 'colorless', 'status',
                      'curse', 'token', 'event', 'quest')

    def card_portrait(self, key):
        snake = key.lower()
        for d in self.PORTRAIT_DIRS:
            for sub in ('', 'beta/'):
                c = f'images/packed/card_portraits/{d}/{sub}{snake}.png'
                if c + '.import' in self.g.pck.files:
                    return self.g.image(c)
        # Some portraits only live in the card atlas.
        for d in self.PORTRAIT_DIRS:
            tres = f'images/atlases/card_atlas.sprites/{d}/{snake}.tres'
            if tres in self.g.pck.files:
                return self.sprite(tres)
        print('  missing portrait', key)
        return Image.new('RGBA', (1000, 760), (60, 60, 60, 255))

    def creature(self, key):
        snake = key.lower()
        skel_res, vscale, hide = creature_skeleton(self.g, key)
        skel, atlas, load = self.g.spine(skel_res)
        img, origin = spine_render.render(skel, atlas, load, scale=vscale * CREATURE_SCALE, hide=hide)
        return img, origin


def export_spine(g, key, skel_res, skel, atlas, load, scale, hide=(), shift=None, max_page=1024):
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
        # Largest 3DS texture is 1024 (max_page: smaller for art shown small); pad to a power of two.
        s = min(1.0, max_page / pw, max_page / ph)
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
    lines += [f'hide {h}' for h in hide]
    if shift:
        lines.append(f'shift {shift[0]:.2f} {shift[1]:.2f}')
    with open(os.path.join(out, key + '.txt'), 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')


def game_font(g, res, fallback):
    """One of the game's own font files (res: e.g. fonts/zhs/SourceHanSerifSC-Bold.otf) as a path
    under build/, or fallback. Godot stores it in a zstd-compressed (RSCC) .fontdata resource;
    decompressed with the zstandard module or the zstd CLI when either exists."""
    import shutil
    import subprocess
    cache = os.path.join(ROOT, 'build', 'fonts', os.path.basename(res))
    if os.path.exists(cache):
        return cache
    try:
        d = g.pck.read(g.imported_path(res))
    except Exception:  # noqa: BLE001 -- a font the PCK does not have: use the fallback
        return fallback
    if d[:4] == b'RSCC':  # FileAccessCompressed: mode, block size, total, block sizes, blocks
        mode, bs, total = struct.unpack_from('<3I', d, 4)
        body = d[16 + 4 * (total // bs + 1):]
        if mode != 2:
            return fallback
        try:
            import zstandard
            d = zstandard.ZstdDecompressor().decompressobj().decompress(body)
        except ImportError:
            if not shutil.which('zstd'):
                print(f'  (no zstd: {os.path.basename(res)} -> {os.path.basename(fallback)})')
                return fallback
            d = subprocess.run(['zstd', '-dc'], input=body, capture_output=True).stdout
    # The font file sits in the resource as a PackedByteArray: u32 length, then the bytes.
    for magic in (b'OTTO\x00', b'\x00\x01\x00\x00\x00'):
        i = d.find(magic)
        while i >= 4:
            n = struct.unpack_from('<I', d, i - 4)[0]
            if 1024 < n <= len(d) - i:
                os.makedirs(os.path.dirname(cache), exist_ok=True)
                with open(cache, 'wb') as f:
                    f.write(d[i:i + n])
                return cache
            i = d.find(magic, i + 1)
    return fallback


def text_image(font_path, size, text, rgb, shadow=(0, 0, 0, 0)):
    """A trimmed RGBA image of text in one colour, with an optional drop shadow (dx, dy, alpha)."""
    font = ImageFont.truetype(font_path, size, index=0)
    x0, y0, x1, y1 = font.getbbox(text)
    dx, dy, sa = shadow
    img = Image.new('RGBA', (x1 - x0 + 2 + dx, y1 - y0 + 2 + dy), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    if sa:
        draw.text((1 - x0 + dx, 1 - y0 + dy), text, font=font, fill=(0, 0, 0, sa))
    draw.text((1 - x0, 1 - y0), text, font=font, fill=rgb + (255,))
    return img


def bake_boot_and_act_titles(g, packer, args):
    """S01 (RGDSplus U01). Boot: the MegaCrit logo Spine of NLogoAnimation
    (scenes/screens/main_menu/logo_animation.tscn) -> romfs/spine/LOGO_MEGACRIT.*, page capped at
    512 px (it is shown ~100 px tall; boot.cpp scales it from its bounds, so scale 1 here).
    Act banner (NActBanner, scenes/ui/act_banner.tscn): the act name (ActModel.Title =
    acts.<ID>.title, Spectral Bold 120 -> zhs Source Han Serif SC Bold, #EFC851) and
    gameplay_ui.ACT_NUMBER (Kreon 40 -> Source Han Serif SC Medium, #87CEEB), pre-rendered in the
    game's fonts at the 3DS size (1080 -> 240 px: 120 -> 27 px; 40 px would be 9, so 13):
    act/name_<ID> for every act in the loc table, act/number_<n> for n = 1..4."""
    skel_res = 'animations/ui/logo/logo_megacrit_animate_skel_data.tres'
    skel, atlas, load = g.spine(skel_res)
    export_spine(g, 'LOGO_MEGACRIT', skel_res, skel, atlas, load, 1.0, max_page=512)
    bold = game_font(g, 'fonts/zhs/SourceHanSerifSC-Bold.otf', args.font)
    medium = game_font(g, 'fonts/zhs/SourceHanSerifSC-Medium.otf', args.font)
    for k, v in g.loc('zhs', 'acts').items():
        act = k.split('.')[0]
        if k.endswith('.title') and act != 'DEPRECATED_ACT':
            packer.add('act/name_' + act, text_image(bold, 27, v, (0xEF, 0xC8, 0x51), (1, 1, 60)))
    number = g.loc('zhs', 'gameplay_ui')['ACT_NUMBER']
    for n in range(1, 5):
        packer.add(f'act/number_{n}', text_image(medium, 13, number.replace('{actNumber}', str(n)),
                                                 (0x87, 0xCE, 0xEB), (1, 1, 60)))


def bake_ancient(g, anc, args):
    """gfx/bg_<anc>.t3t: an Ancient's room (scenes/events/background_scenes/<anc>.tscn), its root
    TextureRects and SpineSprites composited in scene units at half scale, centred 5:3 crop."""
    t = g.pck.read(f'scenes/events/background_scenes/{anc}.tscn').decode()
    exts = dict((m.group(2), m.group(1)) for m in re.finditer(r'\[ext_resource[^\n]*path="res://([^"]+)" id="([^"]+)"', t))
    S = 0.5
    X0, Y0 = -330, -49
    W, H = round(2582 * S), round(1221 * S)
    scene = Image.new('RGBA', (W, H), (0, 0, 0, 255))

    def num(body, key, default):
        m = re.search(key + r' = ([-\d.]+)', body)
        return float(m.group(1)) if m else default

    def vec(body, key, default):
        m = re.search(key + r' = Vector2\(([-\d.]+), ([-\d.]+)\)', body)
        return (float(m.group(1)), float(m.group(2))) if m else default

    for block in t.split('\n[node ')[1:]:
        head, _, body = block.partition('\n')
        if 'parent="."' not in head or 'visible = false' in body:
            continue
        if 'type="TextureRect"' in head:
            tex = re.search(r'texture = ExtResource\("([^"]+)"\)', body)
            if not tex:
                continue
            l, tp = num(body, 'offset_left', 0), num(body, 'offset_top', 0)
            r_, bt = num(body, 'offset_right', 0), num(body, 'offset_bottom', 0)
            img = g.image(exts[tex.group(1)]).convert('RGBA').resize((max(1, round((r_ - l) * S)), max(1, round((bt - tp) * S))), Image.LANCZOS)
            scene.alpha_composite(img, (round((l - X0) * S), round((tp - Y0) * S)))
        elif 'type="SpineSprite"' in head:
            res = re.search(r'skeleton_data_res = ExtResource\("([^"]+)"\)', body)
            if not res:
                continue
            px, py = vec(body, 'position', (0, 0))
            sc = vec(body, 'scale', (1, 1))[0]
            skel, atlas, load = g.spine(exts[res.group(1)])
            img, origin = spine_render.render(skel, atlas, load, scale=sc * S)
            scene.alpha_composite(img, (round((px - X0) * S - origin[0]), round((py - Y0) * S - origin[1])))
    cw = round(H * 400 / 240)
    cx = round((960 - X0) * S)
    top = scene.crop((cx - cw // 2, 0, cx - cw // 2 + cw, H)).resize((400, 240), Image.LANCZOS)
    canvas = Image.new('RGBA', (512, 256), (0, 0, 0, 255))
    canvas.paste(top, (0, 0))
    write_t3t(os.path.join(OUT, 'gfx', f'bg_{anc}.t3t'), canvas)
    if args.preview:
        canvas.save(os.path.join(ROOT, 'build', f'preview_bg_{anc}.png'))


# ---------------------------------------------------------------- UI kit art (F1)

NINE = []  # (name, l, t, r, b): 9-slice margins in baked pixels, written to gfx/nine.txt
UI_ATLAS = 'images/atlases/ui_atlas.sprites/'


# A7d: the CrystalSphere minigame (scenes/events/custom/crystal_sphere/crystal_sphere_screen.tscn).
# Cells are 20 px on the bottom screen (57 units in the game); the grid's top-left corner sits at
# CS_GRID on the bottom screen. gfx/bg_crystal_sphere.t3t: the minigame room, top screen at (0, 0)
# (5:3 crop around the sphere), bottom screen at (0, 256) (the sphere at the grid's scale).
CS_CELL = 20
CS_GRID = (6, 10)


def bake_crystal_sphere(g, packer, args):
    d = 'images/events/crystal_sphere/'
    bg = g.image(d + 'crystal_sphere_minigame_bg.png').convert('RGBA')
    # Bg: 2560x1200 rect, keep-aspect (0.7426 texture px -> units, 2.7 units side bars); Sphere
    # centred at anchor (0.424, 0.5635) + offsets, Cells at the sphere centre + (6, 9).
    k = 1200 / bg.height
    gx = (0.424 * 2560 + (-394.863 + 397.137) / 2 + 6 - 57 * 11 / 2 - (2560 - bg.width * k) / 2) / k
    gy = (0.5635 * 1200 + (-396.515 + 391.885) / 2 + 9 - 57 * 11 / 2) / k
    gw = 57 * 11 / k                      # the grid in texture pixels
    s = CS_CELL * 11 / gw                 # texture px -> bottom screen px
    bx, by = gx - CS_GRID[0] / s, gy - CS_GRID[1] / s
    bottom = bg.crop((round(bx), round(by), round(bx + 320 / s), round(by + 240 / s))).resize((320, 240), Image.LANCZOS)
    tw = bg.height * 400 / 240
    cx = min(max(gx + gw / 2, tw / 2), bg.width - tw / 2)
    top = bg.crop((round(cx - tw / 2), 0, round(cx + tw / 2), bg.height)).resize((400, 240), Image.LANCZOS)
    canvas = Image.new('RGBA', (512, 512), (0, 0, 0, 255))
    canvas.paste(top, (0, 0))
    canvas.paste(bottom, (0, 256))
    write_t3t(os.path.join(OUT, 'gfx', 'bg_crystal_sphere.t3t'), canvas)
    if args.preview:
        canvas.save(os.path.join(ROOT, 'build', 'preview_bg_crystal_sphere.png'))
    # The fog (ScryMask: scry_reveal.gdshader over crystal_sphere_noise, lavender ramp, pale
    # borders between tiles), baked once for the whole grid; the UI draws one cell of it per
    # fogged cell.
    n = CS_CELL * 11
    noise = np.asarray(g.image('images/vfx/crystal_sphere_noise.png').convert('L').resize((n, n), Image.BILINEAR), dtype=np.float32) / 255
    c0, c1 = np.array([0.251, 0.231, 0.855]), np.array([0.541, 0.553, 0.980])
    rgb = c0 + (c1 - c0) * noise[..., None]
    fog = np.dstack([rgb * 255, np.full((n, n), 240.0)])
    edge = (np.arange(n) % CS_CELL == 0) | (np.arange(n) % CS_CELL == CS_CELL - 1)
    border = np.array([0.871, 0.878, 1.0]) * 255
    fog[edge, :, :3] = fog[edge, :, :3] * 0.5 + border * 0.5
    fog[:, edge, :3] = fog[:, edge, :3] * 0.5 + border * 0.5
    packer.add('crystal/fog', Image.fromarray(fog.clip(0, 255).astype(np.uint8), 'RGBA'))
    # Items (CrystalSphereItem.TexturePath), stretched into their cells like the Icon TextureRect
    # (inset 6/4/4/6 of 57 units).
    def item(name, path, w, h):
        img = Image.new('RGBA', (w * CS_CELL, h * CS_CELL), (0, 0, 0, 0))
        l, t, r, b = (round(v * CS_CELL / 57) for v in (6, 4, 4, 6))
        img.alpha_composite(g.image(d + path).convert('RGBA').resize((w * CS_CELL - l - r, h * CS_CELL - t - b), Image.LANCZOS), (l, t))
        packer.add('crystal/' + name, img)
    item('relic', 'crystal_sphere_relic.png', 4, 4)
    item('curse', 'crystal_sphere_curse.png', 2, 2)
    for r in ('common', 'uncommon', 'rare'):
        item('card_' + r, f'crystal_sphere_{r}_card_reward.png', 2, 2)
    item('potion_common', 'crystal_sphere_common_potion.png', 1, 3)
    item('potion_rare', 'crystal_sphere_rare_potion.png', 2, 2)
    item('gold', 'crystal_sphere_gold.png', 1, 1)
    item('big_gold', 'crystal_sphere_big_gold.png', 2, 1)
    # The highlighted-cell tile (NCrystalSphereCell Icon) and the divination buttons.
    packer.add('crystal/highlight', fit(g.image(d + 'crystal_ball_single_square_ui.png'), (CS_CELL, CS_CELL)))
    packer.add('crystal/button', fit(g.image(d + 'divine_button.png'), (86, 34)))
    packer.add('crystal/button_outline', fit(g.image(d + 'divine_button_outline.png'), (86, 34)))
    packer.add('crystal/icon_big', fit(g.image(d + 'big_divination_icon.png'), (26, 26)))
    packer.add('crystal/icon_small', fit(g.image(d + 'small_divination_icon.png'), (26, 26)))


# ---------------------------------------------------------------- S18 rest site

def _tscn(text):
    """(ext_resource id -> res path, [(node attrs, node props)]) of a .tscn."""
    ext = {m.group(2): m.group(1) for m in re.finditer(r'\[ext_resource[^\]]*path="res://([^"]+)"[^\]]*id="([^"]+)"', text)}
    nodes = []
    for m in re.finditer(r'\[node ([^\]]*)\]\n((?:(?!\n\[).)*)', text, re.S):
        attrs = dict(re.findall(r'(\w+)="([^"]*)"', m.group(1)))
        if 'instance=' in m.group(1):
            attrs['instance'] = '1'
        props = dict(line.split(' = ', 1) for line in m.group(2).split('\n') if ' = ' in line)
        nodes.append((attrs, props))
    return ext, nodes


def _v2(s, d=(0.0, 0.0)):
    m = re.match(r'Vector2\(([^,]+), ([^)]+)\)', s or '')
    return (float(m.group(1)), float(m.group(2))) if m else d


def _alpha(s):
    if not s:
        return 1.0
    v = [float(x) for x in re.match(r'Color\(([^)]*)\)', s).group(1).split(',')]
    return v[3] if len(v) > 3 else 1.0


def _aff(a, b):  # 2x3 affine product
    return (a[0] * b[0] + a[1] * b[3], a[0] * b[1] + a[1] * b[4], a[0] * b[2] + a[1] * b[5] + a[2],
            a[3] * b[0] + a[4] * b[3], a[3] * b[1] + a[4] * b[4], a[3] * b[2] + a[4] * b[5] + a[5])


# Where rest.cpp finds the extra pieces baked into the spare strip right of the 400x240 scene
# (x 400..511 of the 512x256 texture): a flame mask and a soft round light (both white; the
# screen tints and adds them). Keep in sync with kFlameSrc / kGlowSrc in source/ui/screens/rest.cpp.
REST_FLAME = (400, 0, 48, 72)
REST_GLOW = (400, 80, 96, 96)


def bake_rest_sites(g, args):
    """gfx/bg_rest_<act>.t3t: the act's campfire (scenes/rest_site/<act>_rest_site.tscn) as
    NRestSiteRoom shows it -- the scene in BgContainer at (26, 74) of the 1920x1080 room --
    with its static TextureRects / Sprite2Ds, the ground light the fire casts, then a 5:3 crop
    around the fire. The Spine fire, particles and light shaders are the screen's (a flickering
    flame and glow drawn from the strip at x 400+)."""
    W, H = 1920, 1080
    root = (1, 0, 26, 0, 1, 74)
    flame = g.image('images/vfx/fire/fire_base_campfire.png').convert('L').crop((150, 110, 390, 470))
    light = g.image('images/vfx/light.png').convert('L')
    for act in ('overgrowth', 'underdocks', 'hive', 'glory'):
        ext, nodes = _tscn(g.pck.read(f'scenes/rest_site/{act}_rest_site.tscn').decode())
        scene = Image.new('RGBA', (W, H), (0, 0, 0, 255))
        xf, alpha, skip = {}, {}, set()
        fire = None
        for attrs, p in nodes:
            name, t, par = attrs.get('name'), attrs.get('type', ''), attrs.get('parent')
            if par is None:
                xf['.'], alpha['.'] = root, 1.0
                continue
            pk = par
            path = name if par == '.' else par + '/' + name
            r = float(p.get('rotation', 0))
            sx, sy = _v2(p.get('scale'), (1.0, 1.0))
            if 'position' in p:
                x, y = _v2(p.get('position'))
            else:
                x, y = float(p.get('offset_left', 0)), float(p.get('offset_top', 0))
            c, s = np.cos(r), np.sin(r)
            m = _aff(xf.get(pk, root), (c * sx, -s * sy, x, s * sx, c * sy, y))
            xf[path] = m
            a = alpha.get(pk, 1.0) * _alpha(p.get('modulate'))
            alpha[path] = a
            if pk in skip or p.get('visible') == 'false' or 'SteppedFire' in name or 'instance' in attrs:
                skip.add(path)
                continue
            tex = re.match(r'ExtResource\("([^"]+)"\)', p.get('texture', ''))
            if t not in ('TextureRect', 'Sprite2D') or not tex or 'additive' in p.get('material', ''):
                continue
            sa = a * _alpha(p.get('self_modulate'))
            res = ext.get(tex.group(1), '')
            # Light overlays (rest_site_light_shader, faint) and water glints are additive in-game.
            if sa < 0.3 or not res.endswith('.png') or res.endswith('vfx/light.png'):
                continue
            img = g.image(res).convert('RGBA')
            if t == 'TextureRect':
                w = float(p.get('offset_right', 0)) - float(p.get('offset_left', 0))
                h = float(p.get('offset_bottom', 0)) - float(p.get('offset_top', 0))
                if w <= 0 or h <= 0:
                    w, h = img.size
                loc = (w / img.width, 0, 0, 0, h / img.height, 0)
            else:
                ox, oy = _v2(p.get('offset'))
                cen = p.get('centered', 'true') != 'false'
                loc = (1, 0, ox - (img.width / 2 if cen else 0), 0, 1, oy - (img.height / 2 if cen else 0))
            A = _aff(m, loc)
            det = A[0] * A[4] - A[1] * A[3]
            if abs(det) < 1e-9:
                continue
            inv = (A[4] / det, -A[1] / det, (A[1] * A[5] - A[4] * A[2]) / det,
                   -A[3] / det, A[0] / det, (A[3] * A[2] - A[0] * A[5]) / det)
            layer = img.transform((W, H), Image.AFFINE, inv, resample=Image.BILINEAR)
            if sa < 1:
                arr = np.array(layer)
                arr[..., 3] = (arr[..., 3] * sa).astype(np.uint8)
                layer = Image.fromarray(arr)
            scene.alpha_composite(layer)
            if 'FireLogs' in name and fire is None:  # the fire pit: its rect's centre
                fire = (A[0] * img.width / 2 + A[1] * img.height / 2 + A[2], A[3] * img.width / 2 + A[4] * img.height / 2 + A[5])
        if fire is None:
            fire = (960, 790)
        # The fire's warm light on the ground (RestSiteGroundLighting, additive).
        rgb = np.array(scene.convert('RGB')).astype(np.float32)
        yy, xx = np.mgrid[0:H, 0:W]
        d = np.sqrt(((xx - fire[0]) / 620.0) ** 2 + ((yy - fire[1]) / 360.0) ** 2)
        k = np.clip(1 - d, 0, 1) ** 1.6
        rgb *= (1.3 + 0.6 * k)[..., None]  # the scene's lights are shaders here; lift it a little
        rgb += k[..., None] * np.array([120, 62, 18], np.float32)
        scene = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8)).convert('RGBA')
        # 5:3 crop, 1600x960, the fire at x 200 and about 70 % down.
        cx0 = round(fire[0] - 800)
        cy0 = max(0, min(H - 960, round(fire[1] - 0.7 * 960)))
        top = scene.crop((cx0, cy0, cx0 + 1600, cy0 + 960)).resize((400, 240), Image.LANCZOS)
        canvas = Image.new('RGBA', (512, 256), (0, 0, 0, 0))
        canvas.paste(top, (0, 0))
        fx, fy, fw, fh = REST_FLAME
        fm = flame.resize((fw, fh), Image.LANCZOS)
        canvas.paste(Image.merge('RGBA', (Image.new('L', (fw, fh), 255),) * 3 + (fm,)), (fx, fy))
        gx, gy, gw, gh = REST_GLOW
        gl = light.resize((gw, gh), Image.LANCZOS)
        canvas.paste(Image.merge('RGBA', (Image.new('L', (gw, gh), 255),) * 3 + (gl,)), (gx, gy))
        write_t3t(os.path.join(OUT, 'gfx', f'bg_rest_{act}.t3t'), canvas)
        print('  rest site', act, 'fire at', round((fire[0] - cx0) / 4), round((fire[1] - cy0) / 4))
        if args.preview:
            canvas.save(os.path.join(ROOT, 'build', f'preview_bg_rest_{act}.png'))


def add_ui_art(g, a, packer, known):
    """Buttons, panels, top bar, controls, reward / rest icons, character orbs and icons.
    Names are ui/<name>; sizes are chosen for the 400x240 / 320x240 screens (docs/UI_STYLE.md)."""
    def src(path):
        if path.endswith('.tres'):
            return a.sprite(UI_ATLAS + path)
        return g.image('images/' + path)

    def put(name, path, size, nine=None):
        if name in known:
            return
        try:
            img = src(path)
        except Exception as e:  # a missing source must not break the whole build
            print('  ui art missing', name, path, e)
            return
        packer.add(name, fit(img, size))
        known.add(name)
        if nine:
            NINE.append((name,) + tuple(nine))

    # Buttons
    put('ui/btn_proceed', 'proceed_button.tres', (84, 39), (12, 12, 12, 12))
    put('ui/btn_confirm', 'confirm_button.tres', (66, 34), (10, 10, 10, 10))
    put('ui/btn_back', 'back_button.tres', (66, 34), (10, 10, 10, 10))
    put('ui/btn_ok_s', 'popup_confirm_button.tres', (64, 28), (8, 8, 8, 8))
    put('ui/btn_cancel_s', 'popup_cancel_button.tres', (64, 28), (8, 8, 8, 8))
    put('ui/btn_peek', 'peek_button.tres', (40, 29))
    put('ui/btn_skip', 'ui/reward_screen/reward_skip_button.png', (110, 27), (8, 8, 8, 8))
    put('ui/btn_row', 'ui/reward_screen/reward_item_button.png', (168, 36), (12, 12, 12, 12))
    put('ui/btn_event', 'packed/common_ui/event_button.png', (142, 44), (14, 14, 14, 14))
    put('ui/btn_event_outline', 'packed/common_ui/event_button_outline.png', (142, 45), (14, 14, 14, 14))
    put('ui/btn_ancient', 'packed/common_ui/ancient_event_option_button.png', (48, 48), (14, 14, 14, 14))
    put('ui/btn_ancient_outline', 'packed/common_ui/ancient_event_option_button_outline.png', (48, 48), (14, 14, 14, 14))
    put('ui/btn_compendium', 'packed/common_ui/submenu_compendium_button.png', (93, 66))
    put('ui/btn_delete', 'packed/main_menu/delete_button.png', (32, 32))
    # S02 main menu (NMainMenu): button reticles either side of the focused text button
    # (ButtonReticleLeft / flip_h ButtonReticleRight), the logo (main_menu_logo Spine, first
    # frame), NSubmenuButton art for the singleplayer / compendium submenus.
    put('ui/menu_reticle', 'packed/main_menu/main_menu_button_highlight.png', (20, 20))
    if 'ui/menu_reticle_r' not in known:
        packer.add('ui/menu_reticle_r', fit(src('packed/main_menu/main_menu_button_highlight.png'), (20, 20))
                   .transpose(Image.FLIP_LEFT_RIGHT))
        known.add('ui/menu_reticle_r')
    if 'ui/menu_logo' not in known:
        skel, atlas, load = g.spine('animations/backgrounds/mainmenu/logo/main_menu_logo_skel_data.tres')
        logo, _ = spine_render.render(skel, atlas, load, scale=0.15, animation='animation')
        logo = logo.crop(logo.getbbox())
        logo.thumbnail((236, 146), Image.LANCZOS)
        packer.add('ui/menu_logo', logo)
        known.add('ui/menu_logo')
    for nm in ('standard', 'daily', 'custom', 'card_library', 'relic_collection', 'potion_lab', 'bestiary'):
        put('ui/sub_' + nm, f'ui/main_menu/submenu_{nm}.png', (84, 64))
    put('ui/sub_lock', 'packed/main_menu/submenu_lock.png', (40, 30))
    put('ui/sub_stats', 'packed/main_menu/submenu_stats_icon.png', (44, 25))
    put('ui/sub_history', 'packed/main_menu/submenu_history_icon.png', (40, 25))
    # M8 card library (NCardLibrary): the Ancients / misc pool filter icons.
    put('ui/lib_ancient', 'ui/run_history/neow.png', (24, 24))
    put('ui/lib_misc', 'packed/card_library/pool_filter_other.png', (24, 24))
    # M6: stats screen icons (stats_screen_atlas, NGeneralStatsGrid / NCharacterStats), run history
    # map point icons (NMapPointHistoryEntry: ui/run_history/<type>.png) and badge art (NBadge:
    # ui/game_over_screen/badge_<id>.png on a badge_<rarity>.png plate).
    try:
        sheet = json.loads(g.pck.read('images/atlases/stats_screen_atlas.tpsheet'))['textures'][0]
        page = g.image('images/atlases/' + sheet['image'])
        for sp in sheet['sprites']:
            nm = sp['filename'][:-4]
            if not nm.startswith('stats_') or 'ui/' + nm in known:
                continue
            r = sp['region']
            packer.add('ui/' + nm, fit(page.crop((r['x'], r['y'], r['x'] + r['w'], r['y'] + r['h'])), (28, 28)))
            known.add('ui/' + nm)
    except Exception as e:
        print('  ui art missing stats_screen_atlas', e)
    for f in sorted(g.pck.files):
        m = re.match(r'images/ui/run_history/([a-z_]+)\.png\.import$', f)
        if m and not m.group(1).endswith('_outline'):
            put('hist/' + m.group(1), f'ui/run_history/{m.group(1)}.png', (18, 18))
        m = re.match(r'images/ui/game_over_screen/badge_([a-z_]+)\.png\.import$', f)
        if m and m.group(1) != 'outline':
            put('badge/' + m.group(1), f'ui/game_over_screen/badge_{m.group(1)}.png', (30, 30))
    put('ui/end_turn_glow', 'packed/combat_ui/end_turn_button_glow.png', (84, 42))
    put('ui/exhaust_pile', 'packed/combat_ui/exhaust_pile.png', (30, 30))
    put('ui/pile_count', 'packed/combat_ui/pile_button_count.png', (24, 20))
    # Panels, frames, banners
    put('ui/panel_popup', 'popup_vertical.tres', (143, 163), (14, 14, 14, 14))
    put('ui/panel_reward', 'ui/reward_screen/reward_panel.png', (169, 215), (14, 14, 14, 14))
    put('ui/panel_submenu', 'packed/common_ui/submenu_panel.png', (82, 176), (16, 16, 16, 16))
    put('ui/panel_submenu_short', 'packed/common_ui/submenu_panel_short.png', (98, 132), (16, 16, 16, 16))
    put('ui/panel_legend', 'map/map_legend.tres', (92, 128), (12, 12, 12, 12))
    put('ui/hover_tip', 'ui/hover_tip.png', (160, 48), (14, 14, 14, 14))
    put('ui/nine_dialogue', 'ui/dialogue_nine_patch.png', (57, 41), (12, 12, 12, 12))
    put('ui/nine_tiny', 'ui/tiny_nine_patch.png', (24, 24), (7, 7, 7, 7))
    put('ui/nine_keyboard', 'ui/keyboard_icon_ninepatch.png', (48, 48), (14, 14, 14, 14))
    put('ui/nameplate', 'ui/combat/combat_nameplate_background.png', (100, 26), (10, 10, 10, 10))
    put('ui/dialogue_tail', 'ui/dialogue_tail.png', (18, 20))
    put('ui/thought_tail', 'ui/thought_tail.png', (18, 20))
    put('ui/reward_banner', 'ui/reward_screen/reward_banner.png', (300, 54))
    put('ui/reward_chain', 'ui/reward_screen/reward_chain.png', (32, 32))
    # Tabs, arrows, controls
    put('ui/tab_selected', 'settings_tab_selected.tres', (101, 34), (16, 12, 16, 12))
    put('ui/tab_stroke', 'settings_tab_stroke.tres', (103, 36), (16, 12, 16, 12))
    put('ui/tab_bar', 'ui/color_tab_bar.png', (128, 6))
    put('ui/arrow_left', 'settings_tiny_left_arrow.tres', (24, 26))
    put('ui/arrow_right', 'settings_tiny_right_arrow.tres', (24, 26))
    put('ui/sort_desc', 'sort_descending.tres', (22, 16))
    put('ui/little_arrow', 'packed/common_ui/little_arrow.png', (16, 16))
    put('ui/notify_dot', 'packed/common_ui/notification_dot2.png', (16, 16))
    put('ui/checkbox_on', 'checkbox_ticked.tres', (32, 32))
    put('ui/checkbox_off', 'checkbox_unticked.tres', (32, 32))
    put('ui/scroll_track', 'small_scrollbar_track_center.tres', (8, 8))
    put('ui/scroll_edge', 'small_scrollbar_track_edge.tres', (8, 8))
    put('ui/scroll_thumb', 'small_scrollbar_train.tres', (8, 15), (3, 4, 3, 4))
    put('ui/locked_card', 'packed/common_ui/locked_card.png', (63, 48))
    put('ui/locked_model', 'packed/common_ui/locked_model.png', (32, 32))
    put('ui/hp_bg', 'ui/combat/health_bar_bg.png', (10, 8), (3, 3, 3, 3))
    put('ui/hp_fill', 'ui/combat/health_bar_fill.png', (8, 8), (3, 3, 3, 3))
    put('ui/hp_stroke', 'ui/combat/health_bar_stroke.png', (11, 8), (3, 3, 3, 3))
    put('ui/infinity_hp', 'ui/combat/combat_infinity_hp.png', (50, 28))
    # Top bar: the strip, stat icons and the three icon buttons
    put('ui/top_bar', 'top_bar/top_bar.tres', (512, 20))
    for nm, sz in (('floor', (18, 17)), ('gold', (18, 17)), ('heart', (20, 17)), ('deck', (25, 22)), ('map', (25, 23)),
                   ('settings', (25, 24)), ('timer', (18, 17)), ('ascension', (19, 28)), ('char_backdrop', (20, 19))):
        put('ui/tb_' + nm, f'top_bar/top_bar_{nm}.tres' if nm != 'timer' else 'top_bar/timer_icon.tres', sz)
    for nm, sz in (('deck', (32, 28)), ('map', (32, 29)), ('settings', (32, 31))):
        put('ui/btn_' + nm, f'top_bar/top_bar_{nm}.tres', sz)
    # Reward and rest-site icons
    for nm in ('card', 'card_removal', 'money', 'rare', 'shared_relic', 'special_card', 'uncommon'):
        put('ui/reward_' + nm, f'ui/reward_screen/reward_icon_{nm}.png', (32, 32))
    for nm in ('clone', 'cook', 'dig', 'hatch', 'heal', 'kindle', 'lift', 'mend', 'smith', 'toke'):
        put('ui/rest_' + nm, f'ui/rest_site/option_{nm}.png', (64, 42))
    for i in (1, 2, 3):
        put(f'ui/profile_{i}', f'ui/profile/profile_icon_{i}.png', (36, 36))
    # Characters: top-bar / select icons, cost gems, energy orbs (layers composited)
    chars = ('ironclad', 'silent', 'defect', 'regent', 'necrobinder')
    for c in chars + ('random_character',):
        put('ui/char_' + c.replace('_character', ''), f'ui/top_panel/character_icon_{c}.png', (24, 24))
    put('card/energy_colorless', 'card/energy_colorless.tres', (28, 28))
    for c in chars[1:]:
        put('card/energy_' + c, f'card/energy_{c}.tres', (28, 28))
        prefix = f'images/ui/combat/energy_counters/{c}/{c}_orb_layer_'
        layers = sorted(k for k in g.pck.files if k.startswith(prefix) and k.endswith('.png.import'))
        orb = None
        for k in layers:
            layer = g.image(k[:-len('.import')])
            orb = layer if orb is None else Image.alpha_composite(orb, layer.resize(orb.size))
        if orb is not None and f'ui/energy_orb_{c}' not in known:
            packer.add(f'ui/energy_orb_{c}', fit(orb, (44, 44)))
            known.add(f'ui/energy_orb_{c}')
    put('ui/star', 'ui/combat/energy_star.png', (24, 24))
    for o in ('dark', 'empty', 'frost', 'glass', 'lightning', 'plasma'):
        put('orb/' + o, f'orbs/{o}_orb.png', (32, 32))
    # Intents not used by the ported monsters yet
    for nm in ('card_debuff', 'death_blow', 'hidden', 'status_card'):
        put('intent/' + nm, f'packed/intents/intent_{nm}.png', (30, 30))
    # Every power icon (the ported ones are already in as power/<KEY>)
    stems = set()
    for k in g.pck.files:
        if k.startswith('images/powers/') and k.endswith('.png.import') and '/beta/' not in k:
            stems.add(k[len('images/powers/'):-len('.png.import')])
        if k.startswith('images/atlases/power_atlas.sprites/') and k.endswith('.tres'):
            stems.add(k[len('images/atlases/power_atlas.sprites/'):-len('.tres')])
    for stem in sorted(stems):
        name = 'power/' + stem.upper()
        if name in known or '/' in stem:
            continue
        try:
            if f'images/powers/{stem}.png.import' in g.pck.files:
                img = g.image(f'images/powers/{stem}.png')
            else:
                img = a.sprite(f'images/atlases/power_atlas.sprites/{stem}.tres')
            packer.add(name, fit(img, (24, 24)))
            known.add(name)
        except Exception as e:
            print('  power icon skipped', stem, e)


def build(args):
    global CARDS, POWERS, MONSTERS, RELICS, EVENTS, POTIONS, ENCHANTMENTS
    CARDS = sorted(set(CARDS_FIXED) | set(keys_from_source('CARD_HEADER')))
    POWERS = sorted(set(POWERS_FIXED) | set(keys_from_source('POWER_HEADER')))
    MONSTERS = keys_from_source('MONSTER_HEADER')
    RELICS = keys_from_source('RELIC_HEADER')
    EVENTS = keys_from_source('EVENT_HEADER')
    POTIONS = keys_from_source('POTION_HEADER')
    ENCHANTMENTS = keys_from_source('ENCHANTMENT_HEADER')
    g = Game(args.pck) if args.pck else Game()
    a = Assets(g)
    os.makedirs(os.path.join(OUT, 'gfx'), exist_ok=True)
    os.makedirs(os.path.join(OUT, 'font'), exist_ok=True)
    packer = Packer()

    print('card art')
    for key in CARDS:
        packer.add('portrait/' + key, fit(a.card_portrait(key), PORTRAIT_SIZE))
    for kind in ('attack', 'skill', 'power'):
        frame = fit(a.sprite(f'images/atlases/ui_atlas.sprites/card/card_frame_{kind}_s.tres'), (120, 169))
        border = fit_height(a.sprite(f'images/atlases/ui_atlas.sprites/card/card_portrait_border_{kind}_s.tres'), 96)
        packer.add(f'card/frame_{kind}', frame)
        packer.add(f'card/border_{kind}', border)
        # X1.5-X4.5 (NCard.UpdateVisuals): Frame.Material = the card pool's frame material,
        # PortraitBorder/TitleBanner.Material = the rarity's banner material, each an hsv.gdshader
        # applied straight to the raw atlas sprite (the raw frame is reddish, the raw border cyan).
        for key, mat in CARD_FRAME_MATS.items():
            hsv = material_hsv(g, f'materials/cards/frames/card_frame_{mat}_mat.tres')
            packer.add(f'card/frame_{kind}_{key}', hsv_shader(frame, *hsv))
        for r in CARD_BANNER_RARITIES:
            hsv = material_hsv(g, f'materials/cards/banners/card_banner_{r}_mat.tres')
            packer.add(f'card/border_{kind}_{r}', hsv_shader(border, *hsv))
    packer.add('card/frame_ancient', fit(a.sprite('images/atlases/ui_atlas.sprites/card/card_frame_ancient_s.tres'), (120, 169)))
    banner = fit_height(a.sprite('images/atlases/ui_atlas.sprites/card/card_banner.tres'), 28)
    packer.add('card/banner', banner)
    for r in CARD_BANNER_RARITIES:
        packer.add(f'card/banner_{r}', hsv_shader(banner, *material_hsv(g, f'materials/cards/banners/card_banner_{r}_mat.tres')))
    packer.add('card/ancient_banner', fit_height(a.sprite('images/atlases/ui_atlas.sprites/card/ancient_banner.tres'), 28))
    packer.add('card/energy', fit(a.sprite('images/atlases/ui_atlas.sprites/card/energy_ironclad.tres'), (28, 28)))
    packer.add('card/unplayable', fit(a.sprite('images/atlases/ui_atlas.sprites/card/card_unplayable_icon.tres'), (24, 24)))
    # F5: enchantment badge (NCard.UpdateEnchantmentVisuals: an icon in the card's corner, an
    # amount label when the enchantment shows one). Frame is card_enchant_s; icons are
    # images/enchantments/<snake of the KEY>.png, falling back to missing_enchantment.png.
    packer.add('card/enchant_badge', fit(a.sprite('images/atlases/ui_atlas.sprites/card/card_enchant_s.tres'), (28, 22)))
    for key in ENCHANTMENTS:
        snake = key.lower()
        path = f'images/enchantments/{snake}.png'
        if path + '.import' not in g.pck.files:
            path = 'images/enchantments/missing_enchantment.png'
        packer.add('enchant/' + key, fit(g.image(path), (20, 20)))

    print('creatures')
    os.makedirs(os.path.join(OUT, 'spine'), exist_ok=True)
    # X1.5-X4.5: the other three playable characters' combat Spine, baked exactly like the
    # Ironclad's (creature_skeleton reads scenes/creature_visuals/<key>.tscn for any key). Osty
    # (Combat::osty, char_necrobinder.cpp) is a summoned creature with its own Spine and is baked
    # the same way, keyed "Osty" to match Creature::name set by summonOsty().
    for key in MONSTERS + ['IRONCLAD'] + [c.upper() for c in OTHER_CHARS] + ['Osty']:
        skel_res, vscale, hide = creature_skeleton(g, key)
        skel, atlas, load = g.spine(skel_res)
        scale = vscale * CREATURE_SCALE
        img, origin = spine_render.render(skel, atlas, load, scale=scale, hide=hide)
        # Keep everything inside the top screen.
        box_w = 110 if key in KAISER_CRAB_KEEP else CREATURE_BOX[0]  # three creatures share the 400 px screen
        f = min(1.0, box_w / img.width, CREATURE_BOX[1] / img.height)
        if f < 1.0:
            img = img.resize((max(1, round(img.width * f)), max(1, round(img.height * f))), Image.LANCZOS)
            origin = (origin[0] * f, origin[1] * f)
        print(f'  {key}: {img.size}')
        shift = None
        if key in RECENTER:
            shift = (origin[0] - img.width / 2, origin[1] - img.height)
            origin = (img.width / 2, img.height)
        packer.add('creature/' + key, img, (round(origin[0]), round(origin[1])))
        export_spine(g, key, skel_res, skel, atlas, load, scale * f, hide if key in KAISER_CRAB_KEEP else (), shift)

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
        path = f'images/relics/{key.lower()}.png'
        if path + '.import' not in g.pck.files:  # per-character icons (Yummy Cookie)
            path = f'images/relics/{key.lower()}_ironclad.png'
        packer.add('relic/' + key, fit(g.image(path), (RELIC_ICON, RELIC_ICON)))
    for key in POTIONS:  # PotionModel.ImagePath (potion_atlas)
        packer.add('potion/' + key, fit(a.sprite(f'images/atlases/potion_atlas.sprites/{key.lower()}.tres'), (48, 48)))
    for key in EVENTS:  # event art for the top screen (RGDSplus U21)
        path = f'images/events/{key.lower()}.png'
        if path + '.import' in g.pck.files:
            packer.add('event/' + key, fit(g.image(path), (200, 112)))
        else:
            print('  missing event art', key)
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
    for anc in ('neow', 'orobas', 'pael', 'tezcatara', 'nonupeipe', 'tanx', 'vakuu', 'darv'):
        packer.add('map/ancient_' + anc, fit(g.image(f'images/packed/map/ancients/ancient_node_{anc}.png'), (40, 40)))
    packer.add('ui/sale_tag', fit(g.image('images/rooms/merchant_room/shop_sales_tag.png'), (28, 28)))
    packer.add('ui/card_removal', fit(g.image('images/rooms/merchant_room/card_removal_00.png'), (40, 40)))
    packer.add('map/marker', fit(a.sprite('images/atlases/ui_atlas.sprites/map/icons/map_marker_ironclad.tres'), (26, 26)))
    # Boss map nodes (map/boss_<EncounterId>). Ceremonial Beast, The Insatiable and the Queen
    # have Spine map nodes instead; the UI falls back (creature sprite or the elite icon).
    for enc, path in (('VantomBoss', 'images/map/placeholder/vantom_boss_icon.png'),
                      ('TheKinBoss', 'images/map/placeholder/the_kin_boss_icon.png'),
                      ('KaiserCrabBoss', 'images/map/placeholder/kaiser_crab_boss_icon.png'),
                      ('KnowledgeDemonBoss', 'images/map/placeholder/knowledge_demon_boss_icon.png'),
                      ('AeonglassBoss', 'images/map/placeholder/aeonglass_boss_icon.png'),
                      ('TestSubjectBoss', 'images/map/placeholder/test_subject_boss_icon.png'),
                      ('WaterfallGiantBoss', 'images/map/placeholder/waterfall_giant_boss_icon.png'),
                      ('SoulFyshBoss', 'images/map/placeholder/soul_fysh_boss_icon.png'),
                      ('LagavulinMatriarchBoss', 'images/map/placeholder/lagavulin_matriarch_boss_icon.png')):
        packer.add('map/boss_' + enc, fit_height(g.image(path), 64))
    add_ui_art(g, a, packer, {e[0] for e in packer.entries})
    bake_crystal_sphere(g, packer, args)  # A7d
    select = g.image('images/packed/character_select/char_select_ironclad.png')
    packer.add('ui/ironclad_select', fit_height(select, 120))
    bake_other_character_art(g, packer, args)  # X1.5-X4.5: ui/<key>_select for the other four
    # S04: the Random button and the selected-button outline (scenes/screens/char_select/char_select_button.tscn).
    packer.add('ui/random_select', fit_height(g.image('images/packed/character_select/char_select_random.png'), 120))
    packer.add('ui/char_select_outline', fit_height(g.image('images/packed/character_select/char_select_outline.png'), 120))
    icon = Image.new('RGBA', (48, 48), (40, 10, 10, 255))
    head = select.crop((0, 0, select.width, select.width)).resize((48, 48), Image.LANCZOS)
    icon.alpha_composite(head)
    icon.save(os.path.join(ROOT, 'icon.png'))

    print('boot and act titles')
    bake_boot_and_act_titles(g, packer, args)

    with open(os.path.join(OUT, 'gfx', 'atlas.txt'), 'w', newline='\n') as f:
        for (name, page, x, y, w, h, ax, ay) in packer.entries:
            f.write(f'{name} {page} {x} {y} {w} {h} {ax} {ay}\n')
    with open(os.path.join(OUT, 'gfx', 'nine.txt'), 'w', newline='\n') as f:
        for row in NINE:
            f.write(' '.join(str(v) for v in row) + '\n')
    for i, page in enumerate(packer.pages):
        img = shrink_page(page['img']) if i == len(packer.pages) - 1 else page['img']
        write_t3t(os.path.join(OUT, 'gfx', f'atlas_{i}.t3t'), img)
        if args.preview:
            os.makedirs(os.path.join(ROOT, 'build'), exist_ok=True); img.save(os.path.join(ROOT, 'build', f'preview_atlas_{i}.png'))
    print(f'  {len(packer.entries)} sprites in {len(packer.pages)} page(s)')

    print('backgrounds')
    bake_title_art(g, args)
    # Per act (ActModel.FilePathIdentifier): gfx/bg_<act>.t3t (room) and gfx/bg_map_<act>.t3t.
    # Underdocks' layers are not all full-frame: (image, scene rect) from the
    # scenes/backgrounds/underdocks/layers/*_a.tscn TextureRects (scene units, centred,
    # 2764.8x1296; None = full frame). 03_a draws underdocks_03_c with its water shadow behind.
    UNDERDOCKS_LAYERS = [('00', None), ('01_a', None), ('02_a', None),
                         ('03_c_shadow', (-1384.0, -10.0, 1380.8, 629.0)), ('03_c', (-1383.4, -647.0, 1381.4, 119.0)),
                         ('04_a', (-1382.0, -198.0, 1382.8, 626.0))]
    for act in ('overgrowth', 'underdocks', 'hive', 'glory'):
        # Combat room: all layers composited (StS2 layers are mostly full-frame images).
        if act == 'underdocks':
            layers = [(g.image(f'images/rooms/{act}/{act}_{n}.png'), r) for n, r in UNDERDOCKS_LAYERS]
        else:
            layers = [(g.image(f'images/rooms/{act}/{act}_{n}.png'), None) for n in ('00', '01_a', '02_a', '03_a', '04_a')]
        W, H = layers[0][0].size
        scene = Image.new('RGBA', (W, H), (0, 0, 0, 255))
        for im, r in layers:
            if r is None:
                scene.alpha_composite(im.resize((W, H)))
            else:
                u = W / 2764.8
                x0, y0 = round((r[0] + 1382.4) * u), round((r[1] + 648) * u)
                w, h = round((r[2] - r[0]) * u), round((r[3] - r[1]) * u)
                piece = im.resize((w, h), Image.LANCZOS)
                sx, sy = max(0, -x0), max(0, -y0)
                piece = piece.crop((sx, sy, min(w, W - x0), min(h, H - y0)))
                scene.alpha_composite(piece, (max(0, x0), max(0, y0)))
        # Top: the camera's view (1920x1080 of the 2764.8x1296 scene), cropped to 5:3.
        k = W / 2764.8
        cw = 1920 * k
        ch = cw * 240 / 400
        cx, cy = W / 2, H / 2
        top = scene.crop((round(cx - cw / 2), round(cy - ch / 2), round(cx + cw / 2), round(cy + ch / 2)))
        top = top.resize((400, 240), Image.LANCZOS)
        canvas = Image.new('RGBA', (512, 256), (0, 0, 0, 255))
        canvas.paste(top, (0, 0))
        write_t3t(os.path.join(OUT, 'gfx', f'bg_{act}.t3t'), canvas)
        # Map paper: the game's own MapBg (map_screen.tscn): top / middle / bottom parchment,
        # each fitted into a 1920x1080 box (keep-aspect -> 1527x1080) and stacked from y=-1620
        # to +1620. Baked at MAP_SCALE (ui.cpp kMapS) into one 260x551 strip.
        MAP_SCALE = 0.17
        parts = [g.image(f'images/packed/map/map_bgs/{act}/map_{p}_{act}.png') for p in ('top', 'middle', 'bottom')]
        pw, ph = round(1527 * MAP_SCALE), round(1080 * MAP_SCALE)
        sheet = Image.new('RGBA', (pw, ph * 3), (0, 0, 0, 0))
        for i, part in enumerate(parts):
            sheet.alpha_composite(part.resize((pw, ph), Image.LANCZOS), (0, i * ph))
        print('  ', act, 'room + map paper', sheet.size)
        mb = Image.new('RGBA', (512, 1024), (0, 0, 0, 0))
        mb.paste(sheet, (0, 0))
        write_t3t(os.path.join(OUT, 'gfx', f'bg_map_{act}.t3t'), mb)
        if args.preview:
            os.makedirs(os.path.join(ROOT, 'build'), exist_ok=True)
            canvas.save(os.path.join(ROOT, 'build', f'preview_bg_{act}.png'))

    # Neow's room (scenes/events/background_scenes/neow.tscn): bg + Spine Neow + vignette,
    # composited in scene units at half scale, then a centred 5:3 crop for the top screen.
    S = 0.5
    X0, Y0 = -330, -49
    W, H = round(2582 * S), round(1221 * S)
    scene = Image.new('RGBA', (W, H), (0, 0, 0, 255))
    scene.alpha_composite(g.image('animations/backgrounds/neow_room/neow_bg.png').convert('RGBA').resize((W, H), Image.LANCZOS))
    skel, atlas, load = g.spine('animations/backgrounds/neow_room/neow.tres')
    img, origin = spine_render.render(skel, atlas, load, scale=0.58 * S)
    scene.alpha_composite(img, (round((-390 - X0) * S - origin[0]), round((-57 - Y0) * S - origin[1])))
    scene.alpha_composite(g.image('animations/backgrounds/neow_room/neow_vignette.png').convert('RGBA').resize((W, H), Image.LANCZOS))
    cw = round(H * 400 / 240)
    cx = round((960 - X0) * S)
    top = scene.crop((cx - cw // 2, 0, cx - cw // 2 + cw, H)).resize((400, 240), Image.LANCZOS)
    canvas = Image.new('RGBA', (512, 256), (0, 0, 0, 255))
    canvas.paste(top, (0, 0))
    write_t3t(os.path.join(OUT, 'gfx', 'bg_neow.t3t'), canvas)
    if args.preview:
        canvas.save(os.path.join(ROOT, 'build', 'preview_bg_neow.png'))

    # The other Ancients (scenes/events/background_scenes/<id>.tscn): the root TextureRects and
    # SpineSprites in scene order, composited like Neow's room, same crop.
    for anc in ('orobas', 'pael', 'tezcatara', 'nonupeipe', 'tanx', 'vakuu', 'darv'):
        bake_ancient(g, anc, args)

    # A10 TheArchitect (the ending, drawn with the Ancient layout): its victory room
    # (images/rooms/architect_victory/architect_victory_bg.png) with the Architect's Spine
    # standing on the right, centred 5:3 crop.
    bg = g.image('images/rooms/architect_victory/architect_victory_bg.png').convert('RGBA')
    ch = bg.height
    cw = min(bg.width, round(ch * 400 / 240))
    scene = bg.crop(((bg.width - cw) // 2, 0, (bg.width - cw) // 2 + cw, ch)).resize((400, 240), Image.LANCZOS)
    skel_res, vscale, hide = creature_skeleton(g, 'ARCHITECT')
    skel, atlas, load = g.spine(skel_res)
    img, origin = spine_render.render(skel, atlas, load, scale=vscale * CREATURE_SCALE * 1.3, hide=hide)
    scene.alpha_composite(img, (max(0, min(400 - img.width, round(280 - origin[0]))), max(0, round(185 - origin[1]))))
    canvas = Image.new('RGBA', (512, 256), (0, 0, 0, 255))
    canvas.paste(scene, (0, 0))
    write_t3t(os.path.join(OUT, 'gfx', 'bg_thearchitect.t3t'), canvas)
    if args.preview:
        canvas.save(os.path.join(ROOT, 'build', 'preview_bg_thearchitect.png'))

    # Merchant room (scenes/rooms/merchant_room.tscn): the tent (BgContainer, Spine at 0.5,
    # scaled 1.01) and the merchant (MerchantButton's MerchantVisual), centred 5:3 crop.
    S = 0.5
    scene = Image.new('RGBA', (round(1920 * S), round(1080 * S)), (0, 0, 0, 255))

    def put(tres, x, y, scale):
        skel, atlas, load = g.spine(tres)
        img, origin = spine_render.render(skel, atlas, load, scale=scale * S)
        scene.alpha_composite(img, (round(x * S - origin[0]), round(y * S - origin[1])))
    put('animations/backgrounds/merchant_room/bottom/shop_merchant_bottom.tres', -10, 20, 0.5 * 1.01)
    put('animations/backgrounds/merchant_room/top/shop_merchant_top.tres', 960 + 246 - 1122.7, 540 - 72 - 396.68, 0.470095)
    cw = round(scene.height * 400 / 240)
    top = scene.crop(((scene.width - cw) // 2, 0, (scene.width - cw) // 2 + cw, scene.height)).resize((400, 240), Image.LANCZOS)
    canvas = Image.new('RGBA', (512, 256), (0, 0, 0, 255))
    canvas.paste(top, (0, 0))
    write_t3t(os.path.join(OUT, 'gfx', 'bg_merchant.t3t'), canvas)
    if args.preview:
        canvas.save(os.path.join(ROOT, 'build', 'preview_bg_merchant.png'))
    bake_rest_sites(g, args)  # S18: gfx/bg_rest_<act>.t3t

    print('text')
    strings = {}

    def take(table, pred=lambda k: True):
        for k, v in g.loc('zhs', table).items():
            if pred(k):
                strings[f'{table}.{k}'] = v

    take('cards', lambda k: k.split('.')[0] in CARDS)
    take('powers', lambda k: k.split('.')[0] in POWERS)
    take('monsters', lambda k: k.split('.')[0] in MONSTERS or k.split('.')[0] == 'HATCHLING')
    take('relics', lambda k: k.split('.')[0] in RELICS)
    take('events', lambda k: k.split('.')[0] in EVENTS or k.startswith('GENERIC'))
    take('ancients', lambda k: k.split('.')[0] in EVENTS or k.startswith('PROCEED.'))  # PROCEED: TheArchitect's option
    take('potions', lambda k: k.split('.')[0] in POTIONS)
    take('enchantments', lambda k: k.split('.')[0] in ENCHANTMENTS)
    # the enchantment replay line; C11: the map's boss preview (NTopBarBossIcon: BOSS / DOUBLE_BOSS)
    take('static_hover_tips', lambda k: k.startswith(('REPLAY', 'BOSS.', 'DOUBLE_BOSS.')))
    # C11: boss names (EncounterModel.Title); M6: every encounter's title and loss line (the run
    # history's killed-by quote, EncounterModel.GetLossMessageFor)
    take('encounters', lambda k: k.endswith(('.title', '.loss')))
    take('stats_screen')  # M6: NGeneralStatsGrid / NCharacterStats entries
    take('run_history', lambda k: k.startswith(('MAP_POINT_HISTORY.', 'INFO.SEED', 'DECK_HISTORY.header',
                                               'RELIC_HISTORY.header', 'DEFAULT_EVENT_LOSS_MESSAGE')))  # M6
    take('merchant_room')
    take('card_library')  # M8: the card library's filters, counts and the unseen card's title / text
    take('relic_collection')  # M9: the relic collection's category headers
    take('potion_lab')  # M9: the potion lab's category headers
    take('badges')  # M7: the end-of-run badges' names and descriptions (badges.h locKeys)
    take('acts', lambda k: k.endswith('.title'))  # S01: the act banner's name (ActModel.Title)
    take('ascension', lambda k: k.startswith('LEVEL_'))  # S04: the character select's ascension panel
    # S02: main menu buttons, its submenus (NSingleplayerSubmenu / NCompendiumSubmenu), the
    # continue-run info and the abandon / quit popups.
    take('main_menu_ui', lambda k: k.split('.')[0] in (
        'CONTINUE', 'ABANDON_RUN', 'ABANDON_RUN_CONFIRMATION', 'SINGLE_PLAYER', 'COMPENDIUM', 'STATISTICS',
        'SETTINGS', 'QUIT', 'QUIT_CONFIRM_POPUP', 'GENERIC_POPUP', 'STANDARD', 'DAILY', 'CUSTOM',
        'COMPENDIUM_CARD_LIBRARY', 'COMPENDIUM_RELIC_COLLECTION', 'COMPENDIUM_POTION_LAB', 'COMPENDIUM_BESTIARY',
        'POTION_LAB_COLLECTION',  # M9: the potion lab's unseen tip
        'RUN_HISTORY', 'CONTINUE_RUN_INFO',
        'PROFILE_SCREEN', 'OPEN_PROFILE_SCREEN'))  # S03: profile screen + the main menu's profile button
    for t in ('card_keywords', 'gameplay_ui', 'rest_site_ui', 'card_reward_ui', 'map', 'combat_messages',
              'card_selection', 'intents', 'game_over_screen', 'characters'):
        take(t, (lambda k: not k.startswith(('DAILY', 'DISCOVERY'))) if t == 'game_over_screen'
             else (lambda k: k.split('.')[0] in ('IRONCLAD', 'SILENT', 'DEFECT', 'REGENT', 'NECROBINDER', 'RANDOM_CHARACTER')) if t == 'characters' else (lambda k: True))
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
    # One page per size (font_<size index>.t3t): both sizes no longer fit one 1024 page.
    sizes = [12, 16]
    page_size = 1024
    lines = []
    for si, size in enumerate(sizes):
        page = Image.new('RGBA', (page_size, page_size), (255, 255, 255, 0))
        draw = ImageDraw.Draw(page)
        x = y = 1
        row_h = 0
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
        write_t3t(os.path.join(OUT, 'font', f'font_{si}.t3t'), shrink_page(page))
    with open(os.path.join(OUT, 'font', 'font.txt'), 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    print(f'  {len(chars)} glyphs x {len(sizes)} sizes, one page each')


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--pck', help='path to "Slay the Spire 2.pck" (default: Steam install)')
    ap.add_argument('--font', default=None, help='CJK font (default: tools/fonts/*, else a system font)')
    ap.add_argument('--preview', action='store_true', help='also write PNG previews')
    args = ap.parse_args()
    args.font = args.font or default_font()
    build(args)
