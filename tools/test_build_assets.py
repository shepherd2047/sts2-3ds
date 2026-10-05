#!/usr/bin/env python3
"""Checks for build_assets helpers that need no game files: the Spine atlas texel-density
scaling and repacking (synthetic atlas). Run: python3 tools/test_build_assets.py
(also part of `make -f Makefile.sdl check`)."""
import os
import struct
import sys
import unittest

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_assets as ba  # noqa: E402
import spine_render  # noqa: E402


def read_t3t(path):
    with open(path, 'rb') as f:
        d = f.read()
    assert d[:4] == b'T3T1'
    w, h, fmt = struct.unpack_from('<HHB', d, 4)
    return Image.frombytes('RGBA', (w, h), d[12:12 + w * h * 4])


def is_pow2(n):
    return n >= 1 and n & (n - 1) == 0


class SpineScale(unittest.TestCase):
    def test_factor(self):
        # 20 atlas px per skeleton unit, drawn at 0.21 px per unit: at 1.6x zoom that is
        # 20 / (0.21 * 1.6) = 59.5 texels per pixel; the target 1.25 needs a factor of 0.021.
        f = ba.spine_atlas_scale(20.0, 0.21, max_zoom=1.6, target=1.25)
        self.assertAlmostEqual(f, 1.25 * 0.21 * 1.6 / 20.0)
        self.assertAlmostEqual(20.0 * f / (0.21 * 1.6), 1.25)
        # Never upscaled; nothing measurable = untouched.
        self.assertEqual(ba.spine_atlas_scale(0.1, 0.21), 1.0)
        self.assertEqual(ba.spine_atlas_scale(None, 0.21), 1.0)
        self.assertEqual(ba.spine_atlas_scale(0.0, 0.21), 1.0)
        # Defaults are the named constants.
        self.assertAlmostEqual(ba.spine_atlas_scale(10.0, 0.5),
                               ba.SPINE_TEXELS_PER_PIXEL * 0.5 * ba.SPINE_MAX_ZOOM / 10.0)

    def test_density(self):
        def quad(tex, units, blend=0):
            src = [(0, 0), (tex, 0), (tex, tex)]
            dst = [(0, 0), (units, 0), (units, units)]
            return ('r', src, dst, None, blend)
        # 100 texels over 10 units = 10 texels/unit.
        self.assertAlmostEqual(spine_render.texel_density([quad(100, 10)]), 10.0)
        # Area-weighted median: a 20-unit part at 5 texels/unit outweighs a 10-unit part at 10.
        self.assertAlmostEqual(spine_render.texel_density([quad(100, 10), quad(100, 20)]), 5.0)
        # Additive (glow) slots and degenerate triangles do not count.
        self.assertAlmostEqual(spine_render.texel_density([quad(100, 10), quad(10, 100, blend=1)]), 10.0)
        self.assertIsNone(spine_render.texel_density([quad(0, 10)]))


ATLAS = """
synthetic.png
size: 256,128
filter: Linear,Linear
a
  bounds: 2,2,64,32
  offsets: 4,6,72,40
b
  bounds: 100,2,40,20
  rotate: 90
c
  bounds: 2,60,120,60
"""


def quadrants(img, box, colours):
    """Fill box (x, y, w, h) with four colours: top-left, top-right, bottom-left, bottom-right."""
    x, y, w, h = box
    for i, c in enumerate(colours):
        qx, qy = x + (i % 2) * (w // 2), y + (i // 2) * (h // 2)
        img.paste(Image.new('RGBA', (w // 2, h // 2), c), (qx, qy))


class SpinePack(unittest.TestCase):
    def setUp(self):
        self.pages, self.regions = spine_render.parse_atlas(ATLAS)
        page = Image.new('RGBA', (256, 128), (0, 0, 0, 0))
        palette = iter([(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255), (255, 255, 0, 255),
                        (255, 0, 255, 255), (0, 255, 255, 255), (128, 0, 0, 255), (0, 128, 0, 255),
                        (0, 0, 128, 255), (128, 128, 0, 255), (128, 0, 128, 255), (0, 128, 128, 255)])
        for r in self.regions.values():
            bw, bh = (r['h'], r['w']) if r['rot'] == 90 else (r['w'], r['h'])
            quadrants(page, (r['x'], r['y'], bw, bh), [next(palette) for _ in range(4)])
        self.page = page
        self.imgs = {id(self.pages[0]): page}

    def old_map(self, r, u, v):
        """The original atlas mapping (export_spine without scaling), in old-page pixels."""
        if r['rot'] == 90:
            A, B, C = 0.0, r['oh'], r['x'] - (r['oh'] - r['oy'] - r['h'])
            D, E, F = -r['ow'], 0.0, r['y'] + r['w'] + r['ox']
        else:
            A, B, C = r['ow'], 0.0, r['x'] - r['ox']
            D, E, F = 0.0, r['oh'], r['y'] - (r['oh'] - r['oy'] - r['h'])
        return A * u + B * v + C, D * u + E * v + F

    def check(self, factor, max_page=1024):
        pages, maps, used = ba.pack_spine_atlas(self.imgs, self.regions, factor, max_page)
        for p in pages:
            self.assertTrue(is_pow2(p.width) and is_pow2(p.height) and p.width >= 8 and p.height >= 8, p.size)
            self.assertLessEqual(max(p.size), max_page)
        for name, r in self.regions.items():
            pi, A, B, C, D, E, F = maps[name]
            tw, th = pages[pi].size
            # Quadrant centres of the packed part, as attachment uv.
            u0, u1 = r['ox'] / r['ow'], (r['ox'] + r['w']) / r['ow']
            v0, v1 = (r['oh'] - r['oy'] - r['h']) / r['oh'], (r['oh'] - r['oy']) / r['oh']
            for fu in (0.25, 0.75):
                for fv in (0.25, 0.75):
                    u, v = u0 + (u1 - u0) * fu, v0 + (v1 - v0) * fv
                    ox, oy = self.old_map(r, u, v)
                    want = self.page.getpixel((int(ox), int(oy)))
                    tu, tv = A * u + B * v + C, D * u + E * v + F
                    got = pages[pi].getpixel((int(tu * tw), int(tv * th)))
                    # Same quadrant colour (resampling may move a channel by a step or two).
                    self.assertLessEqual(max(abs(a - b) for a, b in zip(got, want)), 8,
                                         f'{name} at uv {u:.2f},{v:.2f} (factor {factor}): {got} != {want}')
            # The whole packed part maps inside the texture.
            for u, v in ((u0, v0), (u1, v1)):
                tu, tv = A * u + B * v + C, D * u + E * v + F
                self.assertTrue(-1e-6 <= tu <= 1 + 1e-6 and -1e-6 <= tv <= 1 + 1e-6)
        return pages, used

    def test_identity(self):
        pages, used = self.check(1.0)
        self.assertEqual(used, 1.0)

    def test_half(self):
        pages, used = self.check(0.5)
        self.assertEqual(used, 0.5)
        # 3 regions (64x32, 20x40, 120x60 in the page) at half size: a quarter of the 256x128 page.
        self.assertLessEqual(pages[0].width * pages[0].height, 256 * 128 // 4)

    def test_max_page_caps_factor(self):
        # The 120 px region cannot exceed a 64 px page: the factor drops to fit it.
        pages, used = self.check(1.0, max_page=64)
        self.assertLess(used, 1.0)
        self.assertLessEqual(round(120 * used), 64 - 2 * ba.SPINE_REGION_PAD)


if __name__ == '__main__':
    unittest.main(verbosity=1)
