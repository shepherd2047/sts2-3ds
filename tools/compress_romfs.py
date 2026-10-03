#!/usr/bin/env python3
"""Make romfs_3ds/ (what the 3DS build packs) from romfs/ (lossless, used by the preview).

Every RGBA8 .t3t is re-encoded with devkitPro's tex3ds into a GPU-native format so the
3DS uses 1/4 (ETC1A4), 1/8 (ETC1, opaque images) or 1/2 (RGBA4444) of the memory and
romfs size. Container stays T3T1: u16 w, u16 h, u8 fmt, 3 pad, then the GPU data with tex3ds'
4-byte compression header (0x00 raw, 0x11 LZ11) that the loader unpacks.
    fmt 0 = RGBA8 (linear, swizzled by the loader), 1 = ETC1A4, 2 = ETC1, 3 = RGBA4444

Only files newer than their romfs_3ds copy are converted, so rerun it after
build_assets.py (the Makefile does). Other files are copied unchanged.
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

# Font glyphs are thin white strokes: ETC1 blocks smear them, so they use 4444.
KEEP_RGBA8 = ()
RGBA4_PREFIXES = ('font/',)
FMT_ETC1A4, FMT_ETC1, FMT_RGBA4 = 1, 2, 3


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


def convert(tex3ds, rel):
    src = os.path.join(SRC, rel)
    dst = os.path.join(DST, rel)
    img = read_t3t(src)
    if img is None or rel in KEEP_RGBA8:
        shutil.copyfile(src, dst)
        return rel, os.path.getsize(src), os.path.getsize(dst)
    opaque = int(np.asarray(img)[..., 3].min()) == 255
    if rel.startswith(RGBA4_PREFIXES):
        fmt, name = FMT_RGBA4, 'rgba4'
    elif opaque:
        fmt, name = FMT_ETC1, 'etc1'
    else:
        fmt, name = FMT_ETC1A4, 'etc1a4'
    if fmt != FMT_RGBA4:
        img = bleed(img)
    with tempfile.TemporaryDirectory() as tmp:
        png, raw = os.path.join(tmp, 'i.png'), os.path.join(tmp, 'o.bin')
        img.save(png)
        r = subprocess.run([tex3ds, '-f', name, '-q', 'high', '-z', 'lz11', '-r', '-o', raw, png],
                           capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit(f'tex3ds failed on {rel}: {r.stderr}')
        with open(raw, 'rb') as f:
            body = f.read()  # starts with tex3ds' compression header (0x11 = LZ11)
    with open(dst, 'wb') as f:
        f.write(b'T3T1' + struct.pack('<HHB3x', img.width, img.height, fmt) + body)
    return rel, os.path.getsize(src), os.path.getsize(dst)


HASHES = os.path.join(os.path.dirname(DST), 'romfs_3ds.hashes.json')


def sha(path):
    with open(path, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()


def main():
    tex3ds = find_tex3ds()
    # Up-to-date check by content hash: build_assets rewrites every .t3t (new mtimes, same bytes), and
    # re-encoding all of them with tex3ds took minutes. Without a recorded hash, fall back to mtime.
    try:
        with open(HASHES) as f:
            hashes = json.load(f)
    except (OSError, ValueError):
        hashes = {}
    jobs, keep, cur = [], set(), {}
    for dp, _, files in os.walk(SRC):
        for fn in files:
            rel = os.path.relpath(os.path.join(dp, fn), SRC).replace(os.sep, '/')
            keep.add(rel)
            s, d = os.path.join(SRC, rel), os.path.join(DST, rel)
            os.makedirs(os.path.dirname(d), exist_ok=True)
            cur[rel] = sha(s)
            if os.path.exists(d) and (hashes.get(rel) == cur[rel] if rel in hashes
                                      else os.path.getmtime(d) >= os.path.getmtime(s)):
                continue
            jobs.append(rel)
    # drop files that no longer exist in romfs/
    for dp, _, files in os.walk(DST):
        for fn in files:
            rel = os.path.relpath(os.path.join(dp, fn), DST).replace(os.sep, '/')
            if rel not in keep:
                os.remove(os.path.join(dp, fn))
    tex = [j for j in jobs if j.endswith('.t3t')]
    for j in jobs:
        if j not in tex:
            shutil.copyfile(os.path.join(SRC, j), os.path.join(DST, j))
    # 3 workers: each tex3ds runs ~2 threads, and the Mac has 8 GB (more only swaps).
    with ThreadPoolExecutor(max_workers=min(3, os.cpu_count() or 3)) as ex:
        for rel, a, b in ex.map(lambda j: convert(tex3ds, j), tex):
            print(f'{rel}: {a // 1024} KB -> {b // 1024} KB')
    with open(HASHES + '.tmp', 'w') as f:
        json.dump(cur, f)
    os.replace(HASHES + '.tmp', HASHES)
    total = sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(DST) for f in fs)
    print(f'romfs_3ds: {total / 1048576:.1f} MB ({len(tex)} textures converted)')


if __name__ == '__main__':
    main()
