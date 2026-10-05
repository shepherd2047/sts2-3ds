#!/usr/bin/env python3
"""tools/compress_romfs.py on synthetic textures (no game files needed; skipped without tex3ds).

Checks the format rules (opaque -> 5 RGB8, alpha -> 4 RGBA8, font/ -> 6 A8), the T3T1 headers,
that every body unpacks (same LZ11 decoder as gfx_3ds.cpp unpackGpuData) to exactly the size
the loader expects, and that the bytes equal what the loader's fmt 0 path would upload
(Morton tiles, PICA byte order); plus the cache (RULES_VERSION) and the LOSSY switch.
Run: python3 test/compress_romfs_test.py
"""
import importlib.util
import os
import shutil
import struct
import sys
import tempfile

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location('compress_romfs', os.path.join(ROOT, 'tools', 'compress_romfs.py'))
cr = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cr)

fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print('FAIL', what)


def write_t3t(path, rgba):  # as tools/build_assets.py write_t3t: fmt 0, linear RGBA bytes
    h, w, _ = rgba.shape
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(b'T3T1' + struct.pack('<HHB3x', w, h, 0) + rgba.astype(np.uint8).tobytes())


def unpack(data):
    """gfx_3ds.cpp unpackGpuData: tex3ds header (0x00 raw / 0x11 LZ11) + payload."""
    size, pos = data[1] | data[2] << 8 | data[3] << 16, 4
    if data[0] & 0x80:
        size, pos = struct.unpack_from('<I', data, 4)[0], 8
    if data[0] == 0x00:
        return bytes(data[pos:pos + size])
    assert data[0] & 0x7F == 0x11, 'unknown compression %02x' % data[0]
    out = bytearray()
    while len(out) < size:
        flags = data[pos]
        pos += 1
        for _ in range(8):
            if len(out) >= size:
                break
            if not flags & 0x80:
                out.append(data[pos])
                pos += 1
            else:
                b0, b1 = data[pos], data[pos + 1]
                pos += 2
                if b0 >> 4 == 0:
                    ln = ((b0 & 0xF) << 4 | b1 >> 4) + 0x11
                    disp = ((b1 & 0xF) << 8 | data[pos]) + 1
                    pos += 1
                elif b0 >> 4 == 1:
                    b2, b3 = data[pos], data[pos + 1]
                    pos += 2
                    ln = ((b0 & 0xF) << 12 | b1 << 4 | b2 >> 4) + 0x111
                    disp = ((b2 & 0xF) << 8 | b3) + 1
                else:
                    ln, disp = (b0 >> 4) + 1, ((b0 & 0xF) << 8 | b1) + 1
                assert disp <= len(out)
                for _ in range(ln):
                    out.append(out[-disp])
            flags <<= 1
    assert len(out) == size
    return bytes(out)


def morton8(x, y):
    return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3)


def tiled(px):
    """gfx_3ds.cpp loadTexture fmt 0: texel (x, y) -> (((y>>3)*(w>>3) + (x>>3)) << 6) + morton8."""
    h, w = px.shape[:2]
    out = np.zeros((w * h,) + px.shape[2:], np.uint8)
    for y in range(h):
        for x in range(w):
            out[(((y >> 3) * (w >> 3) + (x >> 3)) << 6) + morton8(x & 7, y & 7)] = px[y, x]
    return out


def expected(fmt, rgba):
    if fmt == cr.FMT_RGBA8:  # u32 R<<24|G<<16|B<<8|A, little endian: A B G R
        return tiled(rgba[..., ::-1]).tobytes()
    if fmt == cr.FMT_RGB8:  # B G R
        return tiled(rgba[..., 2::-1]).tobytes()
    if fmt == cr.FMT_A8:
        return tiled(rgba[..., 3]).tobytes()
    raise ValueError(fmt)


def read_out(path):
    with open(path, 'rb') as f:
        d = f.read()
    w, h, fmt = struct.unpack('<HHB', d[4:9])
    return d[:4], w, h, fmt, d[12:]


def main():
    if not (os.environ.get('DEVKITPRO') or os.path.exists('/opt/devkitpro') or shutil.which('tex3ds')):
        print('compress_romfs_test skipped (needs devkitPro tex3ds)')
        return 0
    tmp = tempfile.mkdtemp(prefix='compress_romfs_test_')
    src, dst = os.path.join(tmp, 'romfs'), os.path.join(tmp, 'romfs_3ds')
    yy, xx = np.mgrid[0:64, 0:128]
    opaque = np.stack([xx * 2, yy * 4, (xx + yy) % 256, np.full_like(xx, 255)], -1)
    alpha = np.stack([255 - xx * 2, yy * 4, np.full_like(xx, 77), (xx * 2 + yy) % 256], -1)
    alpha[alpha[..., 3] == 0, :3] = 0  # tex3ds writes RGB 0 into fully transparent texels
    glyphs = np.zeros((64, 64, 4), np.int64)
    glyphs[..., :3] = 255
    glyphs[8:40, 10:14, 3] = 255                        # stroke
    glyphs[20:24, 4:30, 3] = np.arange(26) * 9 + 20     # anti-aliased bar
    glyphs[50, 50, 3] = 1
    images = {'gfx/bg_test.t3t': (opaque, cr.FMT_RGB8),
              'spine/thing_0.t3t': (alpha, cr.FMT_RGBA8),
              'font/font_0.t3t': (glyphs, cr.FMT_A8)}
    for rel, (px, _) in images.items():
        write_t3t(os.path.join(src, rel), px)
    os.makedirs(os.path.join(src, 'gfx'), exist_ok=True)
    with open(os.path.join(src, 'gfx', 'atlas.txt'), 'w', newline='\n') as f:
        f.write('x 0 0 0 1 1 0 0\n')
    bpp = {cr.FMT_RGBA8: 4, cr.FMT_RGB8: 3, cr.FMT_A8: 1}
    cr.main(src, dst)
    for rel, (px, fmt) in images.items():
        magic, w, h, got, body = read_out(os.path.join(dst, rel))
        check(magic == b'T3T1' and (w, h) == (px.shape[1], px.shape[0]), f'{rel}: header')
        check(got == fmt, f'{rel}: fmt {got}, want {fmt}')
        data = unpack(body)
        check(len(data) == w * h * bpp[fmt], f'{rel}: {len(data)} bytes unpacked')
        check(data == expected(fmt, px.astype(np.uint8)), f'{rel}: bytes differ from the loader fmt 0 upload')
    with open(os.path.join(dst, 'gfx', 'atlas.txt')) as f:
        check(f.read() == 'x 0 0 0 1 1 0 0\n', 'non-texture file copied as is')
    check(os.path.exists(dst + '.hashes.json'), 'cache written next to DST')

    # Unchanged sources: nothing converted again; a new RULES_VERSION converts every texture.
    stamp = {rel: os.path.getmtime(os.path.join(dst, rel)) for rel in images}
    os.utime(os.path.join(src, 'gfx', 'bg_test.t3t'))  # new mtime, same bytes (build_assets rewrites)
    cr.main(src, dst)
    check(all(os.path.getmtime(os.path.join(dst, r)) == t for r, t in stamp.items()), 'cache hit')
    cr.RULES_VERSION += 1
    cr.LOSSY = ('gfx/', 'spine/')
    cr.main(src, dst)
    check(read_out(os.path.join(dst, 'gfx/bg_test.t3t'))[3] == cr.FMT_ETC1, 'LOSSY opaque -> ETC1')
    check(read_out(os.path.join(dst, 'spine/thing_0.t3t'))[3] == cr.FMT_ETC1A4, 'LOSSY alpha -> ETC1A4')
    check(read_out(os.path.join(dst, 'font/font_0.t3t'))[3] == cr.FMT_A8, 'font after rules change')
    etc = unpack(read_out(os.path.join(dst, 'spine/thing_0.t3t'))[4])
    check(len(etc) == 128 * 64, 'ETC1A4 size (1 B/px)')
    shutil.rmtree(tmp)
    print('compress_romfs_test: %s' % ('ok' if not fails else f'{fails} failures'))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
