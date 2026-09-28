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
    skel, atlas, load = g.spine('animations/backgrounds/mainmenu/logo/main_menu_logo_skel_data.tres')
    logo, _ = spine_render.render(skel, atlas, load, scale=0.15, animation='animation')
    logo.thumbnail((225, 140), Image.LANCZOS)
    menu.alpha_composite(logo, ((400 - logo.width) // 2, 20))
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
        skel_res, vscale, hide = creature_skeleton(self.g, key)
        skel, atlas, load = self.g.spine(skel_res)
        img, origin = spine_render.render(skel, atlas, load, scale=vscale * CREATURE_SCALE, hide=hide)
        return img, origin


def export_spine(g, key, skel_res, skel, atlas, load, scale, hide=(), shift=None):
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
    lines += [f'hide {h}' for h in hide]
    if shift:
        lines.append(f'shift {shift[0]:.2f} {shift[1]:.2f}')
    with open(os.path.join(out, key + '.txt'), 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')


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
        packer.add(f'card/frame_{kind}', fit(a.sprite(f'images/atlases/ui_atlas.sprites/card/card_frame_{kind}_s.tres'), (120, 169)))
        packer.add(f'card/border_{kind}', fit_height(a.sprite(f'images/atlases/ui_atlas.sprites/card/card_portrait_border_{kind}_s.tres'), 96))
    packer.add('card/banner', fit_height(a.sprite('images/atlases/ui_atlas.sprites/card/card_banner.tres'), 28))
    packer.add('card/energy', fit(a.sprite('images/atlases/ui_atlas.sprites/card/energy_ironclad.tres'), (28, 28)))
    packer.add('card/unplayable', fit(a.sprite('images/atlases/ui_atlas.sprites/card/card_unplayable_icon.tres'), (24, 24)))

    print('creatures')
    os.makedirs(os.path.join(OUT, 'spine'), exist_ok=True)
    for key in MONSTERS + ['IRONCLAD']:
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
                      ('TestSubjectBoss', 'images/map/placeholder/test_subject_boss_icon.png')):
        packer.add('map/boss_' + enc, fit_height(g.image(path), 64))
    add_ui_art(g, a, packer, {e[0] for e in packer.entries})
    select = g.image('images/packed/character_select/char_select_ironclad.png')
    packer.add('ui/ironclad_select', fit_height(select, 120))
    icon = Image.new('RGBA', (48, 48), (40, 10, 10, 255))
    head = select.crop((0, 0, select.width, select.width)).resize((48, 48), Image.LANCZOS)
    icon.alpha_composite(head)
    icon.save(os.path.join(ROOT, 'icon.png'))

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
    for act in ('overgrowth', 'hive', 'glory'):
        # Combat room: all layers composited (StS2 layers are full-frame images).
        layers = [g.image(f'images/rooms/{act}/{act}_{n}.png') for n in ('00', '01_a', '02_a', '03_a', '04_a')]
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
    take('ancients', lambda k: k.split('.')[0] in EVENTS)
    take('potions', lambda k: k.split('.')[0] in POTIONS)
    take('enchantments', lambda k: k.split('.')[0] in ENCHANTMENTS)
    take('static_hover_tips', lambda k: k.startswith('REPLAY'))  # the enchantment replay line
    take('merchant_room')
    for t in ('card_keywords', 'gameplay_ui', 'rest_site_ui', 'card_reward_ui', 'map', 'combat_messages',
              'card_selection', 'intents', 'game_over_screen', 'characters'):
        take(t, (lambda k: not k.startswith(('DAILY', 'DISCOVERY'))) if t == 'game_over_screen'
             else (lambda k: k.split('.')[0] in ('IRONCLAD', 'SILENT', 'DEFECT', 'REGENT', 'NECROBINDER')) if t == 'characters' else (lambda k: True))
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
