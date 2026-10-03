"""Helpers for reading Godot-imported resources out of the game's PCK."""
import io
import json
import os
import re
import struct
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
from pck import Pck  # noqa: E402
from gamepaths import find_pck  # noqa: E402

BCN = {17: 1, 18: 2, 19: 3, 22: 7}  # Godot Image::Format -> Pillow bcn decoder


class Game:
    def __init__(self, path=None):
        self.pck = Pck(path or find_pck())

    def imported_path(self, res):
        imp = self.pck.read(res + '.import').decode()
        paths = re.findall(r'path(?:\.[a-z0-9_]+)?="res://(\.godot/imported/[^"]+)"', imp)
        # Prefer lossless/lossy (webp/png) over VRAM variants, then s3tc/bptc.
        paths.sort(key=lambda p: (0 if '.s3tc.' not in p and '.bptc.' not in p and '.etc2' not in p and '.astc' not in p
                                   else 1 if ('.s3tc.' in p or '.bptc.' in p) else 2))
        return paths[0]

    def image(self, res):
        d = self.pck.read(self.imported_path(res))
        i = d.find(b'RIFF')
        j = d.find(b'\x89PNG')
        k = min([x for x in (i, j) if x >= 0], default=-1)
        if k >= 0 and k < 128:
            return Image.open(io.BytesIO(d[k:])).convert('RGBA')
        df, w, h, mips, fmt = struct.unpack_from('<IHHII', d, 36)
        if fmt in BCN:
            return Image.frombytes('RGBA', (w, h), d[52:], 'bcn', BCN[fmt])
        if fmt == 5:  # RGBA8
            return Image.frombytes('RGBA', (w, h), d[52:52 + w * h * 4])
        if fmt == 4:
            return Image.frombytes('RGB', (w, h), d[52:52 + w * h * 3]).convert('RGBA')
        if fmt == 15:  # RGBAH (half float, e.g. the card SDF): clamp to 8 bit
            import numpy as np
            a = np.frombuffer(d, dtype='<f2', count=w * h * 4, offset=52).astype(np.float32).reshape(h, w, 4)
            return Image.fromarray((np.clip(a, 0, 1) * 255 + 0.5).astype(np.uint8), 'RGBA')
        raise ValueError(f'unsupported texture format {fmt} in {res}')

    def spine(self, skel_data_tres):
        """Return (skel bytes, atlas text, page loader) for a SpineSkeletonDataResource."""
        tres = self.pck.read(skel_data_tres).decode()
        atlas_res = re.search(r'type="SpineAtlasResource"[^\n]*path="res://([^"]+)"', tres).group(1)
        skel_res = re.search(r'type="SpineSkeletonFileResource"[^\n]*path="res://([^"]+)"', tres).group(1)
        skel = self.pck.read(self.imported_path(skel_res))
        atlas_json = json.loads(self.pck.read(self.imported_path(atlas_res)).decode())
        base = os.path.dirname(atlas_res)
        return skel, atlas_json['atlas_data'], lambda f: self.image(base + '/' + f)

    def loc(self, lang, table):
        return json.loads(self.pck.read(f'localization/{lang}/{table}.json').decode())
