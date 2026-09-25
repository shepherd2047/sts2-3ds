"""Locate the Steam install of Slay the Spire 2 on macOS, Windows or Linux.

Override with STS2_PCK (the .pck file) or STS2_DIR (the game folder).
"""
import glob
import os
import re
import sys

GAME = 'Slay the Spire 2'


def steam_roots():
    home = os.path.expanduser('~')
    if sys.platform == 'darwin':
        roots = [os.path.join(home, 'Library/Application Support/Steam')]
    elif sys.platform.startswith('win') or os.name == 'nt' or sys.platform == 'cygwin' or 'MSYSTEM' in os.environ:
        roots = [r'C:\Program Files (x86)\Steam', r'C:\Program Files\Steam',
                 '/c/Program Files (x86)/Steam', '/c/Program Files/Steam']
    else:
        roots = [os.path.join(home, '.steam/steam'), os.path.join(home, '.local/share/Steam')]
    # Extra Steam libraries (other drives) are listed in libraryfolders.vdf.
    extra = []
    for r in roots:
        vdf = os.path.join(r, 'steamapps', 'libraryfolders.vdf')
        if os.path.exists(vdf):
            for m in re.findall(r'"path"\s+"([^"]+)"', open(vdf, encoding='utf-8', errors='ignore').read()):
                extra.append(m.replace('\\\\', '\\'))
    return [r for r in roots + extra if os.path.isdir(r)]


def game_dir():
    if os.environ.get('STS2_DIR'):
        return os.environ['STS2_DIR']
    for r in steam_roots():
        d = os.path.join(r, 'steamapps', 'common', GAME)
        if os.path.isdir(d):
            return d
    sys.exit(f'{GAME} not found in Steam libraries; set STS2_DIR or STS2_PCK')


def _largest(pattern_ext):
    d = game_dir()
    files = [f for f in glob.glob(os.path.join(d, '**', '*' + pattern_ext), recursive=True)
             if os.sep + 'mods' + os.sep not in f and '/mods/' not in f]
    if not files:
        sys.exit(f'no {pattern_ext} under {d}')
    return max(files, key=os.path.getsize)


def find_pck():
    """The game's main PCK (the largest one outside mods/)."""
    return os.environ.get('STS2_PCK') or _largest('.pck')


def find_dll():
    """The game's C# assembly sts2.dll (for decompiling)."""
    d = game_dir()
    dlls = [f for f in glob.glob(os.path.join(d, '**', 'sts2.dll'), recursive=True)
            if os.sep + 'mods' + os.sep not in f and '/mods/' not in f]
    if not dlls:
        sys.exit(f'sts2.dll not found under {d}')
    dlls.sort(key=lambda f: ('arm64' not in f, f))  # any architecture works; the C# is the same
    return dlls[0]


if __name__ == '__main__':
    print('pck:', find_pck())
    print('dll:', find_dll())
