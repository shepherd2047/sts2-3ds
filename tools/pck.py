#!/usr/bin/env python3
"""Minimal reader for unencrypted Godot 4 PCK (format v2/v3)."""
import struct, sys, os, fnmatch

class Pck:
    def __init__(self, path):
        self.f = open(path, 'rb')
        magic, ver, maj, mi, pa = struct.unpack('<4sIIII', self.f.read(20))
        assert magic == b'GDPC', magic
        flags, = struct.unpack('<I', self.f.read(4))
        file_base, = struct.unpack('<Q', self.f.read(8))
        assert not (flags & 1), 'encrypted directory'
        dir_off = None
        if ver >= 3:
            dir_off, = struct.unpack('<Q', self.f.read(8))
        rel = bool(flags & 2)
        if dir_off is not None:
            self.f.seek(dir_off)
        else:
            self.f.seek(20 + 4 + 8 + 16 * 4)
        n, = struct.unpack('<I', self.f.read(4))
        self.files = {}
        for _ in range(n):
            ln, = struct.unpack('<I', self.f.read(4))
            name = self.f.read(ln).rstrip(b'\0').decode()
            off, size = struct.unpack('<QQ', self.f.read(16))
            self.f.read(16)
            fl, = struct.unpack('<I', self.f.read(4))
            if rel: off += file_base
            self.files[name.removeprefix('res://')] = (off, size, fl)

    def read(self, name):
        off, size, fl = self.files[name]
        assert not (fl & 1), 'encrypted file'
        self.f.seek(off)
        return self.f.read(size)

    def glob(self, pat):
        return [k for k in self.files if fnmatch.fnmatch(k, pat)]

if __name__ == '__main__':
    p = Pck(sys.argv[1])
    cmd = sys.argv[2] if len(sys.argv) > 2 else 'ls'
    if cmd == 'ls':
        pat = sys.argv[3] if len(sys.argv) > 3 else '*'
        for k in sorted(p.glob(pat)):
            print(p.files[k][1], k)
    elif cmd == 'x':
        out = sys.argv[3]
        for pat in sys.argv[4:]:
            for k in p.glob(pat):
                dst = os.path.join(out, k)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                with open(dst, 'wb') as o: o.write(p.read(k))
