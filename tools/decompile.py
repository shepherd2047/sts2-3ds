#!/usr/bin/env python3
"""Decompile the game's sts2.dll into ../sts2-decompiled (the porting reference).

Needs the .NET SDK and ILSpy's command line tool:
    dotnet tool install -g ilspycmd
"""
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(__file__))
from gamepaths import find_dll  # noqa: E402

out = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..', 'sts2-decompiled'))
exe = shutil.which('ilspycmd') or os.path.expanduser('~/.dotnet/tools/ilspycmd')
dll = find_dll()
print(f'{dll} -> {out}')
os.makedirs(out, exist_ok=True)
subprocess.run([exe, '-p', '-o', out, dll], check=True)
