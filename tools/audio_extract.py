#!/usr/bin/env python3
"""U1: extract the game's audio into romfs/audio/ as DSP-ADPCM (.adpcm, see audio_dsp.py).

Sources (all inside the game's PCK):
  - FMOD Studio banks banks/desktop/*.bank: RIFF 'FEV ' metadata + an FSB5 sound bank with
    Vorbis subsounds. Subsounds are decoded with vgmstream-cli (Homebrew `vgmstream`).
  - debug_audio/*.mp3 and *.wav (Godot-played one-shots such as "blunt_attack.mp3" used by
    card hit effects), decoded with ffmpeg.
Every sound is downmixed/resampled by ffmpeg and encoded to DSP-ADPCM in Python.

Event names: FMOD event paths ("event:/sfx/block_gain") come from Master.strings.bank,
read through the game's own FMOD Studio library (ctypes, no playback). Event -> sound
mapping comes from the bank metadata: every object (event, timeline, instrument, waveform)
starts with its GUID and refers to children by GUID, so the waveforms an event can play are
the WAV objects reachable from it through events, timelines, transitions and instruments.

Output
  romfs/audio/{music,amb,sfx}/<name>.adpcm
  romfs/audio/index.txt, tab-separated, one record per line ('#' = comment):
    F <id> <category> <path under romfs/audio/> <rate> <channels> <samples> <loop> <source>
    E <event> <category> <groups>
  <groups> is ';'-separated; each group is ','-separated file ids. A group is one top-level
  instrument of the event: a group with several ids is a playlist (FMOD picks one, usually
  at random), different groups are separate instruments that play together or at different
  timeline / parameter positions (music: stems, sections and transitions; U3 picks by name).
  Godot one-shots are listed as events named by their file, e.g. "blunt_attack.mp3".

Sizes: every file is mono by default; music and ambience are 22050 Hz, sfx 32000 Hz (ndsp
mixes at ~32.7 kHz). About 3.9 h of music is the bulk: 22050 Hz mono DSP-ADPCM is
12.6 KB/s. Use --music-rate / --sfx-rate / --amb-rate / --stereo to trade size for quality.

Rerunnable: an existing .adpcm with the same rate and channels is kept (--force redoes it);
files no longer produced are deleted. Needs vgmstream-cli and ffmpeg on PATH.
"""
import argparse
import ctypes
import glob
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ProcessPoolExecutor

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audio_dsp  # noqa: E402
from gamepaths import find_pck, game_dir  # noqa: E402
from pck import Pck  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'romfs', 'audio')
CATS = ('music', 'amb', 'sfx')
CAT_NAME = {'music': 'music', 'amb': 'ambience', 'sfx': 'sfx'}
INSTRUMENTS = {'WAIT', 'MUIT', 'SPIT', 'EVIT', 'CMDI', 'PRIT'}
WALK = {'EVNT', 'TMLN', 'TRAN', 'WAV'} | INSTRUMENTS
FSB_FREQ = {1: 8000, 2: 11000, 3: 11025, 4: 16000, 5: 22050, 6: 24000, 7: 32000, 8: 44100, 9: 48000}
BATCH_SAMPLES = 64_000_000  # per encode job: bounds memory (int32 PCM, ~256 MB)


# ---------------------------------------------------------------------------
# FMOD bank parsing

def parse_bank(name, d, objs):
    """Collect GUID-keyed metadata objects into objs; return (FSB5 offset, subsound list)."""
    lp = d.find(b'LIST', 12)

    def walk(pos, end):
        while pos + 8 <= end:
            cid = d[pos:pos + 4]
            sz, = struct.unpack_from('<I', d, pos + 4)
            if cid == b'LIST':
                body = d[pos + 12:pos + 8 + sz]
                if len(body) >= 24 and body[3:4] == b'B':  # first child "xxxB" holds the GUID
                    objs.setdefault(body[8:24], (d[pos + 8:pos + 12].decode('latin1'), name, body))
                walk(pos + 12, pos + 8 + sz)
            elif cid == b'WAV ' and sz >= 26:  # GUID, ..., u32 subsound index at +22
                objs.setdefault(d[pos + 8:pos + 24], ('WAV', name, d[pos + 8:pos + 8 + sz]))
            pos += 8 + sz + (sz & 1)
    walk(lp, lp + 8 + struct.unpack_from('<I', d, lp + 4)[0])

    i = d.find(b'FSB5')
    if i < 0:
        return i, []
    ver, num, shs, nms, dsz, mode = struct.unpack_from('<6I', d, i + 4)
    pos = i + 60
    subs = []
    for k in range(num):
        v, = struct.unpack_from('<Q', d, pos)
        pos += 8
        extra, freq, ch, samples = v & 1, FSB_FREQ.get((v >> 1) & 0xF, 48000), ((v >> 5) & 1) + 1, v >> 34
        loop = None
        while extra:
            c, = struct.unpack_from('<I', d, pos)
            pos += 4
            extra, size, typ = c & 1, (c >> 1) & 0xFFFFFF, c >> 25
            if typ == 1:
                ch = d[pos]
            elif typ == 2:
                freq, = struct.unpack_from('<I', d, pos)
            elif typ == 3:
                loop = struct.unpack_from('<II', d, pos)
            pos += size
        subs.append(dict(freq=freq, ch=ch, samples=samples, loop=loop))
    nt = i + 60 + shs
    for k, s in enumerate(subs):
        o, = struct.unpack_from('<I', d, nt + 4 * k)
        s['name'] = d[nt + o:d.index(b'\0', nt + o)].decode('utf-8', 'replace') if nms else f'{name}_{k}'
    return i, subs


def fmod_strings(strings_bank):
    """GUID (16 raw bytes) -> path, via the game's FMOD Studio library."""
    base = game_dir()
    pats = {'darwin': ('libfmod.dylib', 'libfmodstudio.dylib'),
            'win32': ('fmod.dll', 'fmodstudio.dll')}.get(sys.platform, ('libfmod.so*', 'libfmodstudio.so*'))
    libs = []
    for p in pats:
        hits = sorted(glob.glob(os.path.join(base, '**', p), recursive=True))
        if not hits:
            sys.exit(f'{p} not found under {base} (needed to read FMOD event names)')
        libs.append(hits[0])
    core = ctypes.CDLL(libs[0], mode=getattr(ctypes, 'RTLD_GLOBAL', 0))
    st = ctypes.CDLL(libs[1], mode=getattr(ctypes, 'RTLD_GLOBAL', 0))
    sysp = ctypes.c_void_p()
    for ver in list(range(0x20300, 0x20320)) + list(range(0x20200, 0x20240)):
        if st.FMOD_Studio_System_Create(ctypes.byref(sysp), ver) == 0:
            break
    else:
        sys.exit('FMOD_Studio_System_Create failed')
    cs = ctypes.c_void_p()
    st.FMOD_Studio_System_GetCoreSystem(sysp, ctypes.byref(cs))
    core.FMOD_System_SetOutput(cs, 2)  # FMOD_OUTPUTTYPE_NOSOUND
    if st.FMOD_Studio_System_Initialize(sysp, 32, 0, 0, None):
        sys.exit('FMOD Studio init failed')
    bank = ctypes.c_void_p()
    buf = ctypes.create_string_buffer(strings_bank)
    if st.FMOD_Studio_System_LoadBankMemory(sysp, buf, len(strings_bank), 1, 0, ctypes.byref(bank)):
        sys.exit('loading Master.strings.bank failed')
    n = ctypes.c_int()
    st.FMOD_Studio_Bank_GetStringCount(bank, ctypes.byref(n))
    out = {}
    path = ctypes.create_string_buffer(1024)
    guid = ctypes.create_string_buffer(16)
    rl = ctypes.c_int()
    for i in range(n.value):
        st.FMOD_Studio_Bank_GetStringInfo(bank, i, guid, path, 1024, ctypes.byref(rl))
        out[guid.raw] = path.value.decode()
    st.FMOD_Studio_System_Release(sysp)
    return out


def event_groups(g0, objs, wav_of):
    """Top-level instrument groups (lists of wav keys) reachable from event g0."""
    guids = objs.keys()
    refs = {}

    def refs_of(g):
        if g not in refs:
            body = objs[g][2]
            refs[g] = {body[i:i + 16] for i in range(len(body) - 15)} & guids
            refs[g].discard(g)
        return refs[g]

    def reach(start, nested=True):
        seen, stack = {start}, [start]
        while stack:
            g = stack.pop()
            if objs[g][0] == 'WAV':
                continue
            for r in refs_of(g):
                if r not in seen and objs[r][0] in WALK and (nested or objs[r][0] != 'EVNT'):
                    seen.add(r)
                    stack.append(r)
        return seen

    def reach_inst(start):
        # Instruments inside a timeline point back at it: only go down (instruments, waveforms,
        # and the whole of a nested event behind an event instrument).
        seen, stack, out = {start}, [start], set()
        while stack:
            g = stack.pop()
            t = objs[g][0]
            if t == 'EVNT':
                out |= reach(g)
                continue
            for r in refs_of(g):
                rt = objs[r][0]
                if r not in seen and (rt in INSTRUMENTS or rt == 'WAV' or (t == 'EVIT' and rt == 'EVNT')):
                    seen.add(r)
                    stack.append(r)
        return out | seen

    allr = reach(g0)
    inst = sorted(g for g in reach(g0, nested=False) if objs[g][0] in INSTRUMENTS)  # the event's own
    child = set()
    for g in inst:
        child |= {r for r in refs_of(g) if objs[r][0] in INSTRUMENTS}
    groups, covered = [], set()
    for g in inst:
        if g in child:
            continue
        w = sorted({wav_of[x] for x in reach_inst(g) if x in wav_of})
        if w and w not in groups:
            groups.append(w)
            covered.update(w)
    rest = sorted({wav_of[x] for x in allr if x in wav_of} - covered)
    if rest:
        groups.append(rest)
    return groups


# ---------------------------------------------------------------------------
# Decoding + encoding (runs in worker processes)

def decode_pcm(src, rate, chans):
    """src: ('fsb', path, subsong0, channels) or ('mem', bytes, None, channels).
    Returns int16 (chans, n): source decoded, resampled by ffmpeg, downmixed by averaging."""
    ff = ['ffmpeg', '-v', 'error', '-i', 'pipe:0', '-ar', str(rate), '-f', 'f32le', 'pipe:1']
    if src[0] == 'fsb':
        v = subprocess.Popen(['vgmstream-cli', '-i', '-p', '-s', str(src[2] + 1), src[1]],
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        r = subprocess.run(ff[:3] + ['-f', 'wav'] + ff[3:], stdin=v.stdout, capture_output=True)
        v.stdout.close()
        v.wait()
    else:
        r = subprocess.run(ff, input=src[1], capture_output=True)
    if r.returncode:
        raise RuntimeError(f'ffmpeg failed on {src[:1]}: {r.stderr.decode()[:300]}')
    nch = src[3]  # ffmpeg keeps the source channel count (no -ac)
    x = np.frombuffer(r.stdout, np.float32)
    x = x[:len(x) // nch * nch].reshape(-1, nch).T
    if chans == 1:
        x = x.mean(0, keepdims=True)
    elif nch == 1:
        x = np.repeat(x, 2, 0)
    return np.clip(np.rint(x * 32767.0), -32768, 32767).astype(np.int16)


def encode_job(items):
    """items: list of dict(path, src, rate, chans, loop). Writes the files, returns sizes."""
    pcms = [decode_pcm(it['src'], it['rate'], it['chans']) for it in items]
    streams, coefs, owner = [], [], []
    for k, p in enumerate(pcms):
        for ch in range(p.shape[0]):
            streams.append(p[ch])
            coefs.append(audio_dsp.correlate_coefs(p[ch]))
            owner.append(k)
    enc = audio_dsp.encode_streams(streams, coefs)
    res = []
    for k, it in enumerate(items):
        idx = [i for i, o in enumerate(owner) if o == k]
        n = pcms[k].shape[1]
        loop = None
        if it['loop']:
            ls, le = it['loop']
            loop = (min(ls, n), min(max(le, ls), n))
        blob = audio_dsp.build_file(it['rate'], len(idx), n, [coefs[i] for i in idx], [enc[i] for i in idx], loop)
        os.makedirs(os.path.dirname(it['path']), exist_ok=True)
        with open(it['path'] + '.tmp', 'wb') as f:
            f.write(blob)
        os.replace(it['path'] + '.tmp', it['path'])
        res.append((it['path'], n, len(blob)))
    return res


def probe_channels(data):
    r = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'a:0', '-show_entries',
                        'stream=channels', '-of', 'csv=p=0', 'pipe:0'], input=data, capture_output=True)
    try:
        return int(r.stdout.decode().strip().split()[0])
    except (ValueError, IndexError):
        return 2


# ---------------------------------------------------------------------------

def slug(s):
    return re.sub(r'[^a-z0-9_.-]+', '_', s.lower()).strip('_') or 'x'


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--music-rate', type=int, default=22050)
    ap.add_argument('--amb-rate', type=int, default=22050)
    ap.add_argument('--sfx-rate', type=int, default=32000)
    ap.add_argument('--stereo', default='', help='comma list of categories kept stereo (music,amb,sfx)')
    ap.add_argument('--only', default='', help='comma list of categories to (re)encode')
    ap.add_argument('--limit', type=int, default=0, help='encode at most N files per category (testing)')
    ap.add_argument('--jobs', type=int, default=max(1, min(5, (os.cpu_count() or 2) - 1)))
    ap.add_argument('--force', action='store_true')
    a = ap.parse_args()
    for tool in ('vgmstream-cli', 'ffmpeg', 'ffprobe'):
        if not shutil.which(tool):
            sys.exit(f'{tool} not found (macOS: brew install vgmstream ffmpeg)')
    rate = {'music': a.music_rate, 'amb': a.amb_rate, 'sfx': a.sfx_rate}
    stereo = set(filter(None, a.stereo.split(',')))
    only = set(filter(None, a.only.split(','))) or set(CATS)
    t0 = time.time()

    pck = Pck(find_pck())
    bank_names = sorted(n for n in pck.files if n.startswith('banks/desktop/') and n.endswith('.bank'))
    objs, fsbs, subs = {}, {}, {}
    strings = {}
    tmp = tempfile.TemporaryDirectory(prefix='sts2audio')
    for bn in bank_names:
        name = os.path.basename(bn)[:-5]
        d = pck.read(bn)
        if name.endswith('.strings'):
            strings = fmod_strings(d)
            continue
        off, ss = parse_bank(name, d, objs)
        if ss:
            path = os.path.join(tmp.name, name + '.fsb')
            with open(path, 'wb') as f:
                f.write(d[off:])
            fsbs[name] = path
            for k, s in enumerate(ss):
                subs[(name, k)] = s
    wav_of = {}
    for g, (t, bank, body) in objs.items():
        if t == 'WAV':
            k = (bank, struct.unpack_from('<I', body, 22)[0])
            if k in subs:
                wav_of[g] = k

    # Events -> groups of subsounds. The same waveform can sit in several banks: one copy.
    events = {}
    for g, path in strings.items():
        if path.startswith('event:/') and g in objs and objs[g][0] == 'EVNT':
            events[path] = event_groups(g, objs, wav_of)

    def ev_cat(p):
        return 'music' if p.startswith('event:/music/') else 'amb' if '/ambience/' in p else 'sfx'

    canon = {}  # (name, samples) -> first (bank, idx)
    for k in sorted(subs):
        canon.setdefault((subs[k]['name'], subs[k]['samples']), k)
    cat_of = {}
    for p, groups in events.items():
        for grp in groups:
            for k in grp:
                c = canon[(subs[k]['name'], subs[k]['samples'])]
                old = cat_of.get(c)
                cat_of[c] = ev_cat(p) if old is None else min(old, ev_cat(p), key=CATS.index)

    # Files: FMOD subsounds referenced by some event, then Godot one-shots.
    files = []
    for k in cat_of:
        s = subs[k]
        files.append(dict(key=k, cat=cat_of[k], name=s['name'], src=('fsb', fsbs[k[0]], k[1], s['ch']),
                          srcrate=s['freq'], samples=s['samples'], loop=s['loop'],
                          source=f'{k[0]}.bank#{k[1]}:{s["name"]}'))
    one_shots = sorted(n for n in pck.files if n.startswith('debug_audio/') and n.endswith(('.mp3', '.wav', '.ogg')))
    for n in one_shots:
        base = os.path.basename(n)
        data = pck.read(n)
        files.append(dict(key=base, cat='amb' if '_amb' in base else 'sfx', name=os.path.splitext(base)[0],
                          src=('mem', data, None, probe_channels(data)), srcrate=None,
                          samples=len(data) * 4, loop=None, source=n))
        events[base] = [[base]]
    files.sort(key=lambda f: (CATS.index(f['cat']), f['name'].lower(), str(f['key'])))
    used, fid = set(), {}
    for i, f in enumerate(files):
        nm = slug(f['name'])
        while f'{f["cat"]}/{nm}' in used:
            nm += '_2'
        used.add(f'{f["cat"]}/{nm}')
        f['rel'] = f'{f["cat"]}/{nm}.adpcm'
        f['path'] = os.path.join(OUT, f['rel'])
        f['rate'] = rate[f['cat']]
        f['chans'] = 2 if f['cat'] in stereo else 1
        fid[f['key']] = i
        if f['loop'] and f['srcrate']:
            f['loop'] = tuple(round(x * f['rate'] / f['srcrate']) for x in f['loop'])

    # Encode what is missing or stale.
    todo, fresh = [], 0
    counted = {c: 0 for c in CATS}
    for f in files:
        ok = False
        if os.path.exists(f['path']) and not a.force:
            try:
                with open(f['path'], 'rb') as fh:
                    h = audio_dsp.read_header(fh.read(0x100))
                ok = h['rate'] == f['rate'] and h['channels'] == f['chans']
            except (ValueError, struct.error):
                ok = False
        fresh += ok
        if not ok and f['cat'] in only and (not a.limit or counted[f['cat']] < a.limit):
            counted[f['cat']] += 1
            todo.append(f)

    # Batches of similar length (the encoder steps all streams of a batch together).
    def est(f):
        return f['samples'] * f['rate'] // (f['srcrate'] or 44100) * f['chans']
    todo.sort(key=est, reverse=True)
    batches, cur, tot = [], [], 0
    for f in todo:
        e = est(f)
        if cur and (tot + e > BATCH_SAMPLES or len(cur) >= 96):
            batches.append(cur)
            cur, tot = [], 0
        cur.append(f)
        tot += e
    if cur:
        batches.append(cur)
    print(f'{len(files)} files ({fresh} up to date), {len(todo)} to encode in {len(batches)} batches, '
          f'{a.jobs} jobs', flush=True)
    done = 0
    if batches:
        with ProcessPoolExecutor(a.jobs) as ex:
            futs = [ex.submit(encode_job, [dict(path=f['path'], src=f['src'], rate=f['rate'], chans=f['chans'],
                                                loop=f['loop']) for f in b]) for b in batches]
            for fu in futs:
                done += len(fu.result())
                print(f'  {done}/{len(todo)} encoded ({time.time() - t0:.0f} s)', flush=True)
    tmp.cleanup()

    # Stale files.
    keep = {os.path.normpath(f['path']) for f in files}
    for c in CATS:
        for p in glob.glob(os.path.join(OUT, c, '*')):
            if os.path.normpath(p) not in keep:
                os.remove(p)

    # Index.
    lines = ['# StS2 audio index, generated by tools/audio_extract.py (format: docstring there and in audio_dsp.py)',
             '# F id category path rate channels samples loop source',
             '# E event category groups   (groups ";"-separated, file ids ","-separated)']
    size = {c: [0, 0, 0.0] for c in CATS}
    for i, f in enumerate(files):
        if not os.path.exists(f['path']):
            continue
        with open(f['path'], 'rb') as fh:
            h = audio_dsp.read_header(fh.read(0x100))
        f['ok'] = True
        size[f['cat']][0] += 1
        size[f['cat']][1] += os.path.getsize(f['path'])
        size[f['cat']][2] += h['samples'] / h['rate']
        lines.append('\t'.join(['F', str(i), CAT_NAME[f['cat']], f['rel'], str(h['rate']), str(h['channels']),
                                str(h['samples']), str(h['loop']), f['source']]))
    for p in sorted(events):
        gs = []
        for grp in events[p]:
            ids = sorted({fid[canon[(subs[k]['name'], subs[k]['samples'])] if isinstance(k, tuple) else k]
                          for k in grp})
            ids = tuple(x for x in ids if files[x].get('ok'))
            if ids and ids not in gs:
                gs.append(ids)
        gs = [','.join(map(str, g)) for g in sorted(gs)]
        if gs:
            cat = ev_cat(p) if p.startswith('event:') else files[fid[p]]['cat']
            lines.append('\t'.join(['E', p, CAT_NAME[cat], ';'.join(gs)]))
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, 'index.txt'), 'w', encoding='utf-8', newline='\n') as fh:
        fh.write('\n'.join(lines) + '\n')

    unref = [k for k in canon.values() if k not in cat_of]
    print(f'events: {sum(1 for p in events if p.startswith("event:"))} FMOD + {len(one_shots)} one-shots; '
          f'{len(unref)} FMOD waveforms used by no event skipped '
          f'({sum(subs[k]["samples"] / subs[k]["freq"] for k in unref) / 60:.1f} min)')
    print(f'{"category":10s} {"files":>6s} {"minutes":>8s} {"rate":>6s} {"ch":>3s} {"MB":>8s}')
    total = 0
    for c in CATS:
        n, b, s = size[c]
        total += b
        print(f'{CAT_NAME[c]:10s} {n:6d} {s / 60:8.1f} {rate[c]:6d} {2 if c in stereo else 1:3d} {b / 1e6:8.1f}')
    print(f'{"total":10s} {sum(v[0] for v in size.values()):6d} {sum(v[2] for v in size.values()) / 60:8.1f} '
          f'{"":6s} {"":3s} {total / 1e6:8.1f}   (romfs/audio, {time.time() - t0:.0f} s)')


if __name__ == '__main__':
    main()
