#!/usr/bin/env python3
"""Small screen classifiers for game.sh (no OCR: the macOS Vision API is not usable from here).

  screen.py menu SHOT     main-menu rows "y1 y2 ..." (1000-px coords) if SHOT is the main menu, else nothing
  screen.py console SHOT  prints 1 when the dev console is open
  screen.py pause SHOT    prints 1 when the pause menu is open

SHOT is a game.sh shot (any width; coordinates are returned in the 1000-px-wide frame).
"""
import sys
from PIL import Image


def load(path):
    im = Image.open(path).convert("RGB")
    s = 1000 / im.width
    return im.resize((1000, round(im.height * s))), s


def menu_rows(im):
    """The main menu is a centred column of cream labels (x 340..470) between y 370 and 570, over a
    dark sky; every other screen fails the 'few bright pixels outside the labels' test."""
    # the title logo (gold) sits above: require it at y 160..340
    gold = sum(1 for x in range(240, 600, 4) for y in range(160, 340, 4)
               if (lambda p: p[0] > 200 and p[1] > 160 and p[2] < 120)(im.getpixel((x, y))))
    if gold < 150:
        return []
    rows, run = [], None
    for y in range(370, 580):
        n = sum(1 for x in range(330, 480) if min(im.getpixel((x, y))) > 150)
        if n >= 2 and run is None:
            run = y
        elif n < 2 and run is not None:
            if y - run >= 6:
                rows.append((run + y) // 2)
            run = None
    if len(rows) < 3:
        return []
    return rows


def menu_rows_all(im):
    """Like menu_rows but also finds greyed-out labels (Singleplayer before the timeline is opened)."""
    rows, run = [], None
    for y in range(370, 580):
        n = sum(1 for x in range(330, 480) if min(im.getpixel((x, y))) > 105 and
                max(im.getpixel((x, y))) - min(im.getpixel((x, y))) < 40)
        if n >= 2 and run is None:
            run = y
        elif n < 2 and run is not None:
            if y - run >= 6:
                rows.append((run + y) // 2)
            run = None
    return rows


def console_open(im):
    cyan = lambda p: p[2] > 170 and p[1] > 140 and p[0] < 130
    return any(all(cyan(im.getpixel((int(1000 * x), y))) for x in (.1, .3, .5, .7, .9)) for y in range(12))



def paused(im):
    """Pause menu (Esc in a run): gold "Paused", teal buttons, the red Give Up at y 374."""
    g = lambda x, y: im.getpixel((x, y))
    red = lambda p: p[0] > 140 and p[0] > p[1] + 40
    teal = lambda p: p[1] > p[0] + 30 and p[2] > p[0] + 30
    gold = any(g(x, y)[0] > 200 and g(x, y)[1] > 160 and g(x, y)[2] < 140 for x in range(450, 550, 3) for y in range(192, 208, 2))
    return gold and red(g(440, 374)) and teal(g(440, 416)) and teal(g(560, 416)) and teal(g(440, 250))


def main():
    cmd, path = sys.argv[1], sys.argv[2]
    im, _ = load(path)
    if cmd == "menu":
        if menu_rows(im):
            print(" ".join(map(str, menu_rows_all(im))))
    elif cmd == "console":
        print(1 if console_open(im) else 0)
    elif cmd == "pause":
        print(1 if paused(im) else 0)


if __name__ == "__main__":
    main()
