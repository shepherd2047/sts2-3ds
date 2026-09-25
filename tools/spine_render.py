#!/usr/bin/env python3
"""Render the setup pose of a Spine 4.2 binary skeleton to a PNG.

Only what a static pose needs is implemented: bones (all inherit modes),
slots, skins, region/mesh/linked-mesh attachments. Constraints and
animations are parsed only far enough to skip them.
"""
import math
import struct

import numpy as np
from PIL import Image


class Reader:
    def __init__(self, data):
        self.d = data
        self.p = 0
        self.strings = []

    def byte(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def sbyte(self):
        v = self.byte()
        return v - 256 if v > 127 else v

    def boolean(self):
        return self.byte() != 0

    def int32(self):
        v = struct.unpack_from('>i', self.d, self.p)[0]
        self.p += 4
        return v

    def long(self):
        v = struct.unpack_from('>q', self.d, self.p)[0]
        self.p += 8
        return v

    def float(self):
        v = struct.unpack_from('>f', self.d, self.p)[0]
        self.p += 4
        return v

    def varint(self, optimize_positive=True):
        result = 0
        shift = 0
        while True:
            b = self.byte()
            result |= (b & 0x7F) << shift
            if not (b & 0x80) or shift >= 28:
                break
            shift += 7
        result &= 0xFFFFFFFF
        if not optimize_positive:
            result = (result >> 1) ^ -(result & 1)
        elif result > 0x7FFFFFFF:
            result -= 1 << 32
        return result

    def string(self):
        n = self.varint()
        if n == 0:
            return None
        if n == 1:
            return ''
        s = self.d[self.p:self.p + n - 1].decode('utf-8')
        self.p += n - 1
        return s

    def string_ref(self):
        i = self.varint()
        return None if i == 0 else self.strings[i - 1]

    def floats(self, n, scale=1.0):
        return [self.float() * scale for _ in range(n)]

    def shorts(self, n):
        return [self.varint() for _ in range(n)]


def read_vertices(r, weighted, scale=1.0):
    count = r.varint()
    if not weighted:
        return {'count': count, 'weighted': False, 'v': r.floats(count * 2, scale)}
    verts = []
    for _ in range(count):
        bc = r.varint()
        infl = []
        for _ in range(bc):
            bone = r.varint()
            x = r.float() * scale
            y = r.float() * scale
            w = r.float()
            infl.append((bone, x, y, w))
        verts.append(infl)
    return {'count': count, 'weighted': True, 'v': verts}


def read_sequence(r):
    return {'count': r.varint(), 'start': r.varint(), 'digits': r.varint(), 'setup': r.varint()}


def read_attachment(r, name, nonessential):
    flags = r.byte()
    if flags & 8:
        name = r.string_ref()
    kind = flags & 7
    a = {'name': name, 'kind': kind}
    if kind == 0:  # region
        a['path'] = r.string_ref() if flags & 16 else name
        a['color'] = r.int32() if flags & 32 else -1
        a['seq'] = read_sequence(r) if flags & 64 else None
        a['rotation'] = r.float() if flags & 128 else 0.0
        a['x'], a['y'], a['sx'], a['sy'], a['w'], a['h'] = r.floats(6)
    elif kind == 1:  # bounding box
        read_vertices(r, bool(flags & 16))
        if nonessential:
            r.int32()
    elif kind == 2:  # mesh
        a['path'] = r.string_ref() if flags & 16 else name
        a['color'] = r.int32() if flags & 32 else -1
        a['seq'] = read_sequence(r) if flags & 64 else None
        hull = r.varint()
        a['verts'] = read_vertices(r, bool(flags & 128))
        n = a['verts']['count'] * 2
        a['uvs'] = r.floats(n)
        a['tris'] = r.shorts((n - hull - 2) * 3)
        if nonessential:
            r.shorts(r.varint())
            r.float()
            r.float()
    elif kind == 3:  # linked mesh
        a['path'] = r.string_ref() if flags & 16 else name
        a['color'] = r.int32() if flags & 32 else -1
        a['seq'] = read_sequence(r) if flags & 64 else None
        a['skin'] = r.varint()
        a['parent'] = r.string_ref()
        if nonessential:
            r.float()
            r.float()
    elif kind == 4:  # path
        v = read_vertices(r, bool(flags & 64))
        r.floats(v['count'] * 2 // 6)
        if nonessential:
            r.int32()
    elif kind == 5:  # point
        r.floats(3)
        if nonessential:
            r.int32()
    elif kind == 6:  # clipping
        r.varint()
        read_vertices(r, bool(flags & 16))
        if nonessential:
            r.int32()
    return a


def read_skin(r, default, nonessential, skins_out):
    if default:
        slot_count = r.varint()
        if slot_count == 0:
            return None
        skin = {'name': 'default', 'att': {}}
    else:
        skin = {'name': r.string(), 'att': {}}
        if nonessential:
            r.int32()
        for _ in range(5):  # bones, ik, transform, path, physics
            for _ in range(r.varint()):
                r.varint()
        slot_count = r.varint()
    for _ in range(slot_count):
        slot = r.varint()
        for _ in range(r.varint()):
            key = r.string_ref()
            a = read_attachment(r, key, nonessential)
            skin['att'][(slot, key)] = a
    return skin


def parse_skeleton(data):
    r = Reader(data)
    r.long()
    version = r.string()
    if not version.startswith('4.2'):
        raise ValueError('unsupported spine version ' + version)
    r.floats(4)
    r.float()  # reference scale
    nonessential = r.boolean()
    if nonessential:
        r.float()
        r.string()
        r.string()
    r.strings = [r.string() for _ in range(r.varint())]

    bones = []
    for i in range(r.varint()):
        b = {'name': r.string(), 'parent': None if i == 0 else r.varint()}
        (b['rot'], b['x'], b['y'], b['sx'], b['sy'], b['shx'], b['shy'], b['len']) = r.floats(8)
        b['inherit'] = r.byte()
        r.boolean()
        if nonessential:
            r.int32()
            r.string()
            r.boolean()
        bones.append(b)

    slots = []
    for _ in range(r.varint()):
        s = {'name': r.string(), 'bone': r.varint(), 'color': r.int32()}
        r.int32()  # dark colour
        s['attachment'] = r.string_ref()
        s['blend'] = r.varint()
        if nonessential:
            r.boolean()
        slots.append(s)

    for _ in range(r.varint()):  # IK
        r.string(); r.varint()
        for _ in range(r.varint()):
            r.varint()
        r.varint()
        flags = r.byte()
        if flags & 32 and flags & 64:
            r.float()
        if flags & 128:
            r.float()
    for _ in range(r.varint()):  # transform
        r.string(); r.varint()
        for _ in range(r.varint()):
            r.varint()
        r.varint()
        flags = r.byte()
        for bit in (8, 16, 32, 64, 128):
            if flags & bit:
                r.float()
        flags = r.byte()
        for bit in (1, 2, 4, 8, 16, 32, 64):
            if flags & bit:
                r.float()
    for _ in range(r.varint()):  # path
        r.string(); r.varint(); r.boolean()
        for _ in range(r.varint()):
            r.varint()
        r.varint()
        flags = r.byte()
        if flags & 128:
            r.float()
        r.floats(5)
    for _ in range(r.varint()):  # physics
        r.string(); r.varint(); r.varint()
        flags = r.byte()
        for bit in (2, 4, 8, 16, 32, 64):
            if flags & bit:
                r.float()
        r.byte()
        r.floats(3)
        if flags & 128:
            r.float()
        r.floats(2)
        flags = r.byte()
        if flags & 128:
            r.float()

    skins = []
    d = read_skin(r, True, nonessential, skins)
    if d:
        skins.append(d)
    for _ in range(r.varint()):
        skins.append(read_skin(r, False, nonessential, skins))

    events = []
    for _ in range(r.varint()):
        ev = {'name': r.string()}
        r.varint(False)
        r.float()
        r.string()
        ev['audio'] = r.string()
        if ev['audio'] is not None:
            r.float()
            r.float()
        events.append(ev)

    sk = {'bones': bones, 'slots': slots, 'skins': skins, 'events': events, 'animations': {}}
    for _ in range(r.varint()):
        name = r.string()
        sk['animations'][name] = read_animation(r, sk)
    return sk


def skip_bezier(r, n):
    r.floats(4 * n)


def read_curves(r, frames, nvalues, scale=1.0, bezier_per_frame=None):
    """CurveTimeline1/2/N: return the first frame's (time, values)."""
    first = (r.float(), [r.float() * scale for _ in range(nvalues)])
    for _ in range(frames - 1):
        r.float()
        r.floats(nvalues)
        c = r.byte()
        if c == 2:
            skip_bezier(r, bezier_per_frame or nvalues)
    return first


def read_animation(r, sk):
    """Parse one animation, keeping only what is needed to pose frame 0."""
    anim = {'bones': {}, 'slots': {}, 'deform': {}, 'draw_order': None}
    r.varint()  # timeline count
    # slot timelines
    for _ in range(r.varint()):
        slot = r.varint()
        for _ in range(r.varint()):
            kind = r.byte()
            frames = r.varint()
            st = anim['slots'].setdefault(slot, {})
            if kind == 0:  # attachment
                for f in range(frames):
                    t = r.float()
                    name = r.string_ref()
                    if f == 0:
                        st['attachment'] = (t, name)
                continue
            r.varint()  # bezier count
            nbytes = {1: 4, 2: 3, 3: 7, 4: 6, 5: 1}[kind]
            t = r.float()
            vals = [r.byte() / 255 for _ in range(nbytes)]
            if kind in (1, 3):
                st['rgba'] = (t, vals[:4])
            elif kind in (2, 4):
                st['rgb'] = (t, vals[:3])
            elif kind == 5:
                st['alpha'] = (t, vals[0])
            for _ in range(frames - 1):
                r.float()
                for _ in range(nbytes):
                    r.byte()
                if r.byte() == 2:
                    skip_bezier(r, nbytes)
    # bone timelines
    for _ in range(r.varint()):
        bone = r.varint()
        for _ in range(r.varint()):
            kind = r.byte()
            frames = r.varint()
            bt = anim['bones'].setdefault(bone, {})
            if kind == 10:  # inherit
                for f in range(frames):
                    t = r.float()
                    v = r.byte()
                    if f == 0:
                        bt['inherit'] = (t, v)
                continue
            r.varint()
            n = 2 if kind in (1, 4, 7) else 1
            bt[kind] = read_curves(r, frames, n)
    # IK constraint timelines
    for _ in range(r.varint()):
        r.varint()
        frames = r.varint()
        r.varint()
        flags = r.byte()
        r.float()
        if flags & 1 and flags & 2:
            r.float()
        if flags & 4:
            r.float()
        for _ in range(frames - 1):
            flags = r.byte()
            r.float()
            if flags & 1 and flags & 2:
                r.float()
            if flags & 4:
                r.float()
            if flags & 128:
                skip_bezier(r, 2)
    # transform constraint timelines
    for _ in range(r.varint()):
        r.varint()
        frames = r.varint()
        r.varint()
        read_curves(r, frames, 6)
    # path constraint timelines
    for _ in range(r.varint()):
        r.varint()
        for _ in range(r.varint()):
            kind = r.byte()
            frames = r.varint()
            r.varint()
            read_curves(r, frames, 3 if kind == 2 else 1)
    # physics constraint timelines
    for _ in range(r.varint()):
        r.varint()
        for _ in range(r.varint()):
            kind = r.byte()
            frames = r.varint()
            if kind == 8:  # reset
                r.floats(frames)
                continue
            r.varint()
            read_curves(r, frames, 1)
    # attachment (deform / sequence) timelines
    for _ in range(r.varint()):
        skin_i = r.varint()
        skin = sk['skins'][skin_i]
        for _ in range(r.varint()):
            slot = r.varint()
            for _ in range(r.varint()):
                att_name = r.string_ref()
                kind = r.byte()
                frames = r.varint()
                att = skin['att'].get((slot, att_name)) if skin else None
                if kind == 0:  # deform
                    r.varint()
                    verts = att['verts'] if att and 'verts' in att else None
                    if verts and verts['weighted']:
                        length = sum(len(v) for v in verts['v']) * 2
                    else:
                        length = (verts['count'] * 2) if verts else 0
                    t = r.float()
                    for f in range(frames):
                        end = r.varint()
                        deform = None
                        if end:
                            start = r.varint()
                            deform = [0.0] * max(length, start + end)
                            for v in range(start, start + end):
                                deform[v] = r.float()
                        if f == 0:
                            anim['deform'][(slot, att_name)] = (t, deform)
                        if f == frames - 1:
                            break
                        t = r.float()
                        if r.byte() == 2:
                            skip_bezier(r, 1)
                else:  # sequence
                    for _ in range(frames):
                        r.float()
                        r.int32()
                        r.float()
    # draw order
    n = r.varint()
    slot_count = len(sk['slots'])
    for i in range(n):
        t = r.float()
        offsets = r.varint()
        order = [-1] * slot_count
        unchanged = []
        orig = 0
        for _ in range(offsets):
            si = r.varint()
            while orig != si:
                unchanged.append(orig)
                orig += 1
            order[orig + r.varint()] = orig
            orig += 1
        while orig < slot_count:
            unchanged.append(orig)
            orig += 1
        for ii in range(slot_count - 1, -1, -1):
            if order[ii] == -1:
                order[ii] = unchanged.pop()
        if i == 0 and t <= 0:
            anim['draw_order'] = order
    # events
    for _ in range(r.varint()):
        r.float()
        ev = sk['events'][r.varint()]
        r.varint(False)
        r.float()
        r.string()
        if ev['audio'] is not None:
            r.float()
            r.float()
    return anim


def apply_frame0(sk, anim):
    """Pose bones/slots with the animation's first keys (MixBlend.setup, alpha 1)."""
    for bi, tl in anim['bones'].items():
        b = sk['bones'][bi]
        for kind, (t, v) in tl.items():
            if kind == 'inherit':
                b['inherit'] = v
            elif kind == 0:
                b['rot'] += v[0]
            elif kind == 1:
                b['x'] += v[0]; b['y'] += v[1]
            elif kind == 2:
                b['x'] += v[0]
            elif kind == 3:
                b['y'] += v[0]
            elif kind == 4:
                b['sx'] *= v[0]; b['sy'] *= v[1]
            elif kind == 5:
                b['sx'] *= v[0]
            elif kind == 6:
                b['sy'] *= v[0]
            elif kind == 7:
                b['shx'] += v[0]; b['shy'] += v[1]
            elif kind == 8:
                b['shx'] += v[0]
            elif kind == 9:
                b['shy'] += v[0]
    for si, tl in anim['slots'].items():
        s = sk['slots'][si]
        if 'attachment' in tl:
            s['attachment'] = tl['attachment'][1]
        c = color_of(s['color'])
        if 'rgba' in tl:
            c = list(tl['rgba'][1])
        if 'rgb' in tl:
            c = list(tl['rgb'][1]) + [c[3]]
        if 'alpha' in tl:
            c = [c[0], c[1], c[2], tl['alpha'][1]]
        s['color'] = (int(c[0] * 255) << 24) | (int(c[1] * 255) << 16) | (int(c[2] * 255) << 8) | int(c[3] * 255)
    sk['deform'] = {k: v[1] for k, v in anim['deform'].items() if v[1] is not None}
    sk['draw_order'] = anim['draw_order']


def world_transforms(bones):
    """Bone.updateWorldTransform for the setup pose."""
    out = []
    for b in bones:
        rot, x, y, sx, sy, shx, shy = b['rot'], b['x'], b['y'], b['sx'], b['sy'], b['shx'], b['shy']
        if b['parent'] is None:
            rx = math.radians(rot + shx)
            ry = math.radians(rot + 90 + shy)
            out.append((math.cos(rx) * sx, math.cos(ry) * sy, math.sin(rx) * sx, math.sin(ry) * sy, x, y))
            continue
        pa, pb, pc, pd, pwx, pwy = out[b['parent']]
        wx = pa * x + pb * y + pwx
        wy = pc * x + pd * y + pwy
        mode = b['inherit']
        if mode == 0:  # normal
            rx = math.radians(rot + shx)
            ry = math.radians(rot + 90 + shy)
            la, lb = math.cos(rx) * sx, math.cos(ry) * sy
            lc, ld = math.sin(rx) * sx, math.sin(ry) * sy
            out.append((pa * la + pb * lc, pa * lb + pb * ld, pc * la + pd * lc, pc * lb + pd * ld, wx, wy))
            continue
        if mode == 1:  # only translation
            rx = math.radians(rot + shx)
            ry = math.radians(rot + 90 + shy)
            out.append((math.cos(rx) * sx, math.cos(ry) * sy, math.sin(rx) * sx, math.sin(ry) * sy, wx, wy))
            continue
        if mode == 2:  # no rotation or reflection
            s = pa * pa + pc * pc
            if s > 0.0001:
                s = abs(pa * pd - pb * pc) / s
                pb2 = pc * s
                pd2 = pa * s
                prx = math.degrees(math.atan2(pc, pa))
            else:
                pa, pc = 0, 0
                pb2, pd2 = pb, pd
                prx = 90 - math.degrees(math.atan2(pd, pb))
            rx = math.radians(rot + shx - prx)
            ry = math.radians(rot + shy - prx + 90)
            la, lb = math.cos(rx) * sx, math.cos(ry) * sy
            lc, ld = math.sin(rx) * sx, math.sin(ry) * sy
            out.append((pa * la - pb2 * lc, pa * lb - pb2 * ld, pc * la + pd2 * lc, pc * lb + pd2 * ld, wx, wy))
            continue
        # no scale / no scale or reflection
        rr = math.radians(rot)
        cos, sin = math.cos(rr), math.sin(rr)
        za = pa * cos + pb * sin
        zc = pc * cos + pd * sin
        s = math.hypot(za, zc)
        if s > 0.00001:
            s = 1 / s
        za *= s
        zc *= s
        s = math.hypot(za, zc)
        if mode == 3 and (pa * pd - pb * pc < 0):
            s = -s
        r2 = math.pi / 2 + math.atan2(zc, za)
        zb = math.cos(r2) * s
        zd = math.sin(r2) * s
        rx = math.radians(shx)
        ry = math.radians(90 + shy)
        la, lb = math.cos(rx) * sx, math.cos(ry) * sy
        lc, ld = math.sin(rx) * sx, math.sin(ry) * sy
        out.append((za * la + zb * lc, za * lb + zb * ld, zc * la + zd * lc, zc * lb + zd * ld, wx, wy))
    return out


def parse_atlas(text):
    pages = []
    page = None
    region = None
    for line in text.splitlines():
        line = line.strip()
        if not line:
            page = None
            region = None
            continue
        if ':' in line:
            k, v = [s.strip() for s in line.split(':', 1)]
            target = region if region is not None else page
            if target is not None:
                target[k] = v
            continue
        if page is None:
            page = {'file': line, 'regions': {}}
            pages.append(page)
            region = None
        else:
            region = {'page': page}
            page['regions'][line] = region
    regions = {}
    for p in pages:
        for name, reg in p['regions'].items():
            bx, by, bw, bh = [int(v) for v in reg['bounds'].split(',')]
            rot = reg.get('rotate', '0')
            rot = 90 if rot == 'true' else int(rot) if rot.lstrip('-').isdigit() else 0
            if 'offsets' in reg:
                ox, oy, ow, oh = [int(v) for v in reg['offsets'].split(',')]
            else:
                ox, oy, ow, oh = 0, 0, bw, bh
            regions[name] = {'page': p, 'x': bx, 'y': by, 'w': bw, 'h': bh, 'rot': rot,
                             'ox': ox, 'oy': oy, 'ow': ow, 'oh': oh}
    return pages, regions


ROTATE_CW = False  # direction the packer rotated "rotate:90" regions


def unpack_region(reg, page_img):
    """Rebuild the original (unpacked, untrimmed) image for an atlas region."""
    x, y, w, h = reg['x'], reg['y'], reg['w'], reg['h']
    if reg['rot'] == 90:
        piece = page_img.crop((x, y, x + h, y + w))
        # Undo the packer's rotation.
        piece = piece.transpose(Image.ROTATE_90 if ROTATE_CW else Image.ROTATE_270)
    else:
        piece = page_img.crop((x, y, x + w, y + h))
    out = Image.new('RGBA', (reg['ow'], reg['oh']), (0, 0, 0, 0))
    # offsets y is measured from the bottom.
    out.paste(piece, (reg['ox'], reg['oh'] - reg['oy'] - h))
    return out


def draw_triangle(canvas, tex, src, dst, tint):
    """Affine-map one textured triangle into canvas (both float RGBA arrays)."""
    (x0, y0), (x1, y1), (x2, y2) = dst
    minx = max(int(math.floor(min(x0, x1, x2))), 0)
    maxx = min(int(math.ceil(max(x0, x1, x2))), canvas.shape[1] - 1)
    miny = max(int(math.floor(min(y0, y1, y2))), 0)
    maxy = min(int(math.ceil(max(y0, y1, y2))), canvas.shape[0] - 1)
    if minx > maxx or miny > maxy:
        return
    den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
    if abs(den) < 1e-9:
        return
    ys, xs = np.mgrid[miny:maxy + 1, minx:maxx + 1]
    px = xs + 0.5
    py = ys + 0.5
    w0 = ((y1 - y2) * (px - x2) + (x2 - x1) * (py - y2)) / den
    w1 = ((y2 - y0) * (px - x2) + (x0 - x2) * (py - y2)) / den
    w2 = 1 - w0 - w1
    eps = -1e-4
    inside = (w0 >= eps) & (w1 >= eps) & (w2 >= eps)
    if not inside.any():
        return
    (u0, v0), (u1, v1), (u2, v2) = src
    u = w0 * u0 + w1 * u1 + w2 * u2
    v = w0 * v0 + w1 * v1 + w2 * v2
    ui = np.clip(u.astype(int), 0, tex.shape[1] - 1)
    vi = np.clip(v.astype(int), 0, tex.shape[0] - 1)
    col = tex[vi, ui] * tint
    a = col[..., 3:4] * inside[..., None]
    region = canvas[miny:maxy + 1, minx:maxx + 1]
    # Premultiplied "over".
    region[..., :3] = col[..., :3] * a + region[..., :3] * (1 - a)
    region[..., 3:4] = a + region[..., 3:4] * (1 - a)


def color_of(c):
    c &= 0xFFFFFFFF
    return np.array([(c >> 24) & 255, (c >> 16) & 255, (c >> 8) & 255, c & 255], dtype=np.float32) / 255.0


def render(skel_bytes, atlas_text, load_page, scale=1.0, skin_names=None, pad=4, animation='idle_loop', hide=()):
    sk = parse_skeleton(skel_bytes)
    sk['deform'] = {}
    sk['draw_order'] = None
    anim = sk['animations'].get(animation) or sk['animations'].get('idle')
    if anim:
        apply_frame0(sk, anim)
    world = world_transforms(sk['bones'])
    pages, regions = parse_atlas(atlas_text)
    textures = {}

    skins = sk['skins']
    order = []
    if skin_names:
        order += [s for s in skins if s and s['name'] in skin_names]
    order += [s for s in skins if s and s['name'] == 'default']
    order += [s for s in skins if s and s not in order]

    def find_att(slot_i, key):
        for s in order:
            a = s['att'].get((slot_i, key))
            if a:
                return a
        return None

    tris = []  # (page, [(u,v)x3], [(x,y)x3], tint)
    order_idx = sk['draw_order'] or list(range(len(sk['slots'])))
    for si in order_idx:
        slot = sk['slots'][si]
        key = slot['attachment']
        if not key or any(slot['name'].startswith(h) for h in hide):
            continue
        a = find_att(si, key)
        if not a:
            continue
        if a['kind'] == 3:  # linked mesh -> parent's geometry
            parent = None
            for s in order:
                parent = s['att'].get((si, a['parent']))
                if parent:
                    break
            if not parent:
                continue
            a = dict(parent, path=a['path'], color=a['color'])
        if a['kind'] not in (0, 2):
            continue
        path = a['path']
        if a.get('seq'):
            seq = a['seq']
            path = path + str(seq['start'] + seq['setup']).zfill(seq['digits'])
        reg = regions.get(path)
        if not reg:
            continue
        tint = color_of(slot['color']) * color_of(a['color'])
        bone = world[slot['bone']]
        ba, bb, bc, bd, bx, by = bone

        if a['kind'] == 0:
            # RegionAttachment: a quad the size of the original image.
            w, h = a['w'] * a['sx'], a['h'] * a['sy']
            rad = math.radians(a['rotation'])
            cos, sin = math.cos(rad), math.sin(rad)
            corners = []
            for (cx, cy) in ((-w / 2, -h / 2), (-w / 2, h / 2), (w / 2, h / 2), (w / 2, -h / 2)):
                ox = cx * cos - cy * sin + a['x']
                oy = cx * sin + cy * cos + a['y']
                corners.append((ox * ba + oy * bb + bx, ox * bc + oy * bd + by))
            ow, oh = reg['ow'], reg['oh']
            # bottom-left, top-left, top-right, bottom-right in image pixels
            src = [(0, oh), (0, 0), (ow, 0), (ow, oh)]
            tris.append((path, [src[0], src[1], src[2]], [corners[0], corners[1], corners[2]], tint))
            tris.append((path, [src[0], src[2], src[3]], [corners[0], corners[2], corners[3]], tint))
        else:
            verts = a['verts']
            deform = sk['deform'].get((si, key))
            pts = []
            if verts['weighted']:
                di = 0
                for infl in verts['v']:
                    wx = wy = 0.0
                    for (bi, vx, vy, wgt) in infl:
                        if deform:
                            vx += deform[di]
                            vy += deform[di + 1]
                        di += 2
                        ta, tb, tc, td, tx, ty = world[bi]
                        wx += (vx * ta + vy * tb + tx) * wgt
                        wy += (vx * tc + vy * td + ty) * wgt
                    pts.append((wx, wy))
            else:
                v = deform if deform else verts['v']
                for i in range(0, len(v), 2):
                    pts.append((v[i] * ba + v[i + 1] * bb + bx, v[i] * bc + v[i + 1] * bd + by))
            uvs = a['uvs']
            src = [(uvs[i * 2] * reg['ow'], uvs[i * 2 + 1] * reg['oh']) for i in range(len(pts))]
            t = a['tris']
            for i in range(0, len(t) - 2, 3):
                ia, ib, ic = t[i], t[i + 1], t[i + 2]
                if max(ia, ib, ic) >= len(pts):
                    continue
                tris.append((path, [src[ia], src[ib], src[ic]], [pts[ia], pts[ib], pts[ic]], tint))

    if not tris:
        return None
    xs = [p[0] for t in tris for p in t[2]]
    ys = [p[1] for t in tris for p in t[2]]
    minx, maxx, miny, maxy = min(xs), max(xs), min(ys), max(ys)
    W = int(math.ceil((maxx - minx) * scale)) + pad * 2
    H = int(math.ceil((maxy - miny) * scale)) + pad * 2
    # Supersample 2x for smoother downscaled edges.
    ss = 2
    canvas = np.zeros((H * ss, W * ss, 4), dtype=np.float32)
    pages_img = {}
    for key, src, dst, tint in tris:
        tex = textures.get(key)
        if tex is None:
            reg = regions[key]
            page = reg['page']
            pimg = pages_img.get(page['file'])
            if pimg is None:
                pimg = load_page(page['file']).convert('RGBA')
                psize = page.get('size')
                if psize:
                    pw_, ph_ = [int(v) for v in psize.split(',')]
                    if pimg.size != (pw_, ph_):
                        pimg = pimg.resize((pw_, ph_), Image.LANCZOS)
                pages_img[page['file']] = pimg
            tex = np.asarray(unpack_region(reg, pimg), dtype=np.float32) / 255.0
            textures[key] = tex
        # Spine y is up; images are y down.
        d = [(((x - minx) * scale + pad) * ss, ((maxy - y) * scale + pad) * ss) for (x, y) in dst]
        draw_triangle(canvas, tex, src, d, tint)
    img = Image.fromarray((np.clip(canvas, 0, 1) * 255).astype(np.uint8), 'RGBA')
    img = img.resize((W, H), Image.LANCZOS)
    # Origin (skeleton 0,0 = feet) position within the image, for placement.
    origin = ((0 - minx) * scale + pad, (maxy - 0) * scale + pad)
    return img, origin
