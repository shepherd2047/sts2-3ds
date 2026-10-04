#!/usr/bin/env python3
"""Make romfs_3ds/ (what the 3DS build packs) from romfs/ (RGBA8, used by the preview).

Every RGBA8 .t3t is re-encoded with devkitPro's tex3ds into the GPU-native layout (8x8 Morton
tiles, PICA byte order) so the 3DS loader only unpacks it. Lossless by default (New 3DS only,
124MB); the old blurry ETC rules remain for the prefixes listed in LOSSY:
    font/ pages          -> fmt 6: A8 on disk, uploaded as LA8 with L = 255 (2 B/px in memory)
    fully opaque images  -> fmt 5: RGB8 (3 B/px)
    everything else      -> fmt 4: RGBA8, pre-tiled (4 B/px)
Container stays T3T1: u16 w, u16 h, u8 fmt, 3 pad, then the GPU data with tex3ds' 4-byte
compression header (0x00 raw, 0x11 LZ11) that the loader unpacks.
    fmt 0 = RGBA8 (linear, swizzled by the loader; what build_assets writes), 1 = ETC1A4,
        2 = ETC1, 3 = RGBA4444, 4 = RGBA8 tiled, 5 = RGB8 tiled, 6 = A8 tiled (-> GPU LA8)
tex3ds writes RGB 0 into fully transparent texels (every format); build_assets' pages already
hold black in ~94% of those, so this changes nothing visible.

Only files whose source bytes (or RULES_VERSION) changed are converted, so rerun it after
build_assets.py (the Makefile does). Other files are copied unchanged.
Usage: compress_romfs.py [SRC DST]   (default romfs/ -> romfs_3ds/; cache in DST.hashes.json)
"""
import os
import json
import hashlib
import shutil
import struct
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'romfs')
DST = os.path.join(ROOT, 'romfs_3ds')

# Part of every texture's cache key: bump it whenever the rules below change, so the next run
# converts everything again (no need to delete romfs_3ds.hashes.json by hand).
RULES_VERSION = 2

KEEP_RGBA8 = ()  # copied as fmt 0 (linear RGBA8, swizzled by the loader)
FONT_PREFIXES = ('font/',)
# Lossy fallback if linear memory runs short: textures under these prefixes get the old rules
# (ETC1 when opaque = 1/8 of RGBA8, else ETC1A4 = 1/4), e.g. ('spine/',) or ('gfx/bg_',).
LOSSY = ()
FMT_ETC1A4, FMT_ETC1, FMT_RGBA4, FMT_RGBA8, FMT_RGB8, FMT_A8 = 1, 2, 3, 4, 5, 6
TEX3DS_NAME = {FMT_ETC1A4: 'etc1a4', FMT_ETC1: 'etc1', FMT_RGBA4: 'rgba4',
               FMT_RGBA8: 'rgba8', FMT_RGB8: 'rgb8', FMT_A8: 'a8'}


def pick_format(rel, opaque):
    if rel.startswith(LOSSY):
        return FMT_ETC1 if opaque else FMT_ETC1A4
    if rel.startswith(FONT_PREFIXES):
        return FMT_A8  # white glyphs: only alpha is stored, the loader sets L = 255
    return FMT_RGB8 if opaque else FMT_RGBA8


def find_tex3ds():
    for d in (os.environ.get('DEVKITPRO'), '/opt/devkitpro', 'C:/msys64/opt/devkitpro', 'C:/devkitPro'):
        if not d:
            continue
        for exe in ('tex3ds.exe', 'tex3ds'):
            p = os.path.join(d, 'tools', 'bin', exe)
            if os.path.exists(p):
                return p
    p = shutil.which('tex3ds')
    if p:
        return p
    sys.exit('tex3ds not found (needs devkitPro 3ds-dev; set DEVKITPRO)')


def read_t3t(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != b'T3T1':
        return None
    w, h, fmt = struct.unpack('<HHB', data[4:9])
    if fmt != 0:
        return None
    return Image.frombytes('RGBA', (w, h), data[12:12 + w * h * 4])


def bleed(img, rounds=6):
    """Spread colour into fully transparent pixels so ETC1 blocks on sprite edges don't
    average in black (which shows as dark fringes once the sprite is filtered)."""
    a = np.asarray(img).copy()
    rgb = a[..., :3].astype(np.float32)
    known = a[..., 3] > 0
    if known.all() or not known.any():
        return img
    for _ in range(rounds):
        acc = np.zeros_like(rgb)
        cnt = np.zeros(known.shape, np.float32)
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (-1, 1), (1, -1), (1, 1)):
            k = np.roll(known, (dy, dx), (0, 1))
            acc += np.roll(rgb, (dy, dx), (0, 1)) * k[..., None]
            cnt += k
        fill = ~known & (cnt > 0)
        if not fill.any():
            break
        rgb[fill] = acc[fill] / cnt[fill][:, None]
        known = known | fill
    a[..., :3] = np.clip(rgb + 0.5, 0, 255).astype(np.uint8)
    return Image.fromarray(a, 'RGBA')


def convert(tex3ds, rel, src_root=SRC, dst_root=DST):
    src = os.path.join(src_root, rel)
    dst = os.path.join(dst_root, rel)
    img = read_t3t(src)
    if img is None or rel in KEEP_RGBA8:
        shutil.copyfile(src, dst)
        return rel, os.path.getsize(src), os.path.getsize(dst)
    opaque = int(np.asarray(img)[..., 3].min()) == 255
    fmt = pick_format(rel, opaque)
    if fmt in (FMT_ETC1, FMT_ETC1A4):
        img = bleed(img)
    with tempfile.TemporaryDirectory() as tmp:
        png, raw = os.path.join(tmp, 'i.png'), os.path.join(tmp, 'o.bin')
        img.save(png)
        r = subprocess.run([tex3ds, '-f', TEX3DS_NAME[fmt], '-q', 'high', '-z', 'lz11', '-r', '-o', raw, png],
                           capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit(f'tex3ds failed on {rel}: {r.stderr}')
        with open(raw, 'rb') as f:
            body = f.read()  # starts with tex3ds' compression header (0x11 = LZ11)
    with open(dst, 'wb') as f:
        f.write(b'T3T1' + struct.pack('<HHB3x', img.width, img.height, fmt) + body)
    return rel, os.path.getsize(src), os.path.getsize(dst)


def sha(path):
    with open(path, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()


def cache_key(rel, path):
    return f'r{RULES_VERSION}:{sha(path)}' if rel.endswith('.t3t') else sha(path)


def main(src_root=SRC, dst_root=DST):
    tex3ds = find_tex3ds()
    hashes_path = dst_root.rstrip('/\\') + '.hashes.json'  # romfs_3ds.hashes.json
    # Up-to-date check by content hash: build_assets rewrites every .t3t (new mtimes, same bytes), and
    # re-encoding all of them with tex3ds took minutes. Without a recorded hash, fall back to mtime.
    try:
        with open(hashes_path) as f:
            hashes = json.load(f)
    except (OSError, ValueError):
        hashes = {}
    jobs, keep, cur = [], set(), {}
    for dp, _, files in os.walk(src_root):
        for fn in files:
            rel = os.path.relpath(os.path.join(dp, fn), src_root).replace(os.sep, '/')
            keep.add(rel)
            s, d = os.path.join(src_root, rel), os.path.join(dst_root, rel)
            os.makedirs(os.path.dirname(d), exist_ok=True)
            cur[rel] = cache_key(rel, s)
            if os.path.exists(d) and (hashes.get(rel) == cur[rel] if rel in hashes
                                      else os.path.getmtime(d) >= os.path.getmtime(s)):
                continue
            jobs.append(rel)
    # drop files that no longer exist in romfs/
    for dp, _, files in os.walk(dst_root):
        for fn in files:
            rel = os.path.relpath(os.path.join(dp, fn), dst_root).replace(os.sep, '/')
            if rel not in keep:
                os.remove(os.path.join(dp, fn))
    tex = [j for j in jobs if j.endswith('.t3t')]
    for j in jobs:
        if j not in tex:
            shutil.copyfile(os.path.join(src_root, j), os.path.join(dst_root, j))
    # 3 workers: each tex3ds runs ~2 threads, and the Mac has 8 GB (more only swaps).
    with ThreadPoolExecutor(max_workers=min(3, os.cpu_count() or 3)) as ex:
        for rel, a, b in ex.map(lambda j: convert(tex3ds, j, src_root, dst_root), tex):
            print(f'{rel}: {a // 1024} KB -> {b // 1024} KB')
    with open(hashes_path + '.tmp', 'w', newline='\n') as f:
        json.dump(cur, f)
    os.replace(hashes_path + '.tmp', hashes_path)
    total = sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(dst_root) for f in fs)
    print(f'romfs_3ds: {total / 1048576:.1f} MB ({len(tex)} textures converted)')


if __name__ == '__main__':
    if len(sys.argv) == 3:
        main(os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2]))
    elif len(sys.argv) == 1:
        main()
    else:
        sys.exit('usage: compress_romfs.py [SRC DST]')
