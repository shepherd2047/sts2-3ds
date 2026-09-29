"""Nintendo DSP-ADPCM encoder/decoder (numpy) and the romfs audio file format.

Used by tools/audio_extract.py; the C++ audio engine (U2) reads the files described here.

File format `.adpcm` (all little-endian, what the 3DS reads natively)
---------------------------------------------------------------------
    0x00  char[4] magic        "S2AD"
    0x04  u8      version      1
    0x05  u8      channels     1 or 2
    0x06  u8      loop         1 = the sound loops from loop_start to loop_end
    0x07  u8      reserved     0
    0x08  u32     sample_rate  Hz (e.g. 22050, 32000)
    0x0C  u32     num_samples  samples per channel
    0x10  u32     loop_start   sample index (0 when not looping)
    0x14  u32     loop_end     sample index, exclusive (= num_samples when not looping)
    0x18  u32     data_offset  file offset of the ADPCM data (32-byte aligned)
    0x1C  u32     channel_bytes ADPCM bytes per channel = ceil(num_samples / 14) * 8
    0x20  u32     block_bytes  interleave block size per channel (multiple of 8); == channel_bytes
                               for mono files
    0x24  u32[3]  reserved     0
    0x30  channel info, 48 bytes per channel:
          +0x00 s16 coefs[16]  8 predictor pairs; coefs[2*i] weights history 1 (the previous
                               sample), coefs[2*i+1] history 2. Same order as ndspChnSetAdpcmCoefs.
          +0x20 u16 ps         predictor/scale of the first frame (= first data byte)
          +0x22 s16 yn1, yn2   history before sample 0 (always 0)
          +0x26 u16 loop_ps    predictor/scale of the frame holding loop_start
          +0x28 s16 loop_yn1, loop_yn2  decoded history just before loop_start
          +0x2C u8  pad[4]
    data_offset: ADPCM data. For each block b, for each channel c: channel c's bytes
          [b*block_bytes, min((b+1)*block_bytes, channel_bytes)). Mono = one plain stream.

ADPCM frames are the standard DSP ones: 8 bytes = 1 header byte (predictor index << 4 |
scale) + 14 4-bit samples, high nibble first. Decoding one sample:
    s = clamp16((((nibble << scale) << 11) + 1024 + c1*yn1 + c2*yn2) >> 11)
This is what ndsp plays with NDSP_FORMAT_ADPCM (ndspAdpcmData = {ps, yn1, yn2}); the SDL
backend decodes the same data with the same formula (see decode() below).

Encoder: coefficients per channel with Nintendo's DSPADPCM correlation algorithm (as in
dsptool / gc-dspadpcm-encode: per-frame order-2 LPC records, averaged, then split into 8
predictors by iterative refinement). Frames are encoded closed-loop; instead of the
reference encoder's iterative scale search, every predictor x scale pair is tried and the
one with the least squared error wins (a superset of the reference search). The frame loop
is vectorised across many streams at once, so batches of similar length encode fast.
"""
import struct

import numpy as np

MAGIC = b'S2AD'
VERSION = 1
HEADER = 0x30
CHANNEL_INFO = 0x30
SCALES = 13  # 0..12


# ---------------------------------------------------------------------------
# Coefficients (DSPCorrelateCoefs), order 2, vectorised over frames.

def _matrix_filter(rec):
    """MatrixFilter for an array of records (N, 3) -> (N, 3)."""
    m21, m22 = -rec[:, 1], -rec[:, 2]
    val = 1.0 - m22 * m22
    with np.errstate(divide='ignore', invalid='ignore'):
        m11 = (m22 * m21 + m21) / val
    out = np.empty_like(rec)
    out[:, 0] = 1.0
    out[:, 1] = m11
    out[:, 2] = m21 * m11 + m22
    return out


def _merge_finish_record(src):
    """MergeFinishRecord (Levinson step) for one vector (3,)."""
    dst = [1.0, 0.0, 0.0]
    val = src[0]
    dst[1] = -src[1] / val if val > 0.0 else 0.0
    val *= 1.0 - dst[1] * dst[1]
    v2 = dst[1] * src[1]
    dst[2] = -(v2 + src[2]) / val if val > 0.0 else 0.0
    dst[1] += dst[2] * dst[1]
    return np.array(dst)


def _contrast(best, rec):
    """ContrastVectors(best[i], rec[z]) for all pairs -> (N, K)."""
    s1 = best[None, :, :]
    r1, r2 = rec[:, 1:2], rec[:, 2:3]
    with np.errstate(divide='ignore', invalid='ignore'):
        val = (r2 * r1 - r1) / (1.0 - r2 * r2)
    v1 = s1[..., 0] ** 2 + s1[..., 1] ** 2 + s1[..., 2] ** 2
    v2 = s1[..., 0] * s1[..., 1] + s1[..., 1] * s1[..., 2]
    v3 = s1[..., 0] * s1[..., 2]
    return v1 + 2.0 * val * v2 + 2.0 * (-r1 * val - r2) * v3


def correlate_coefs(pcm):
    """8 predictor pairs (8, 2) int16 for one channel of int16 PCM."""
    n = len(pcm)
    frames = (n + 13) // 14
    s = np.zeros(frames * 14 + 2, np.float64)
    s[2:2 + n] = pcm
    idx = np.arange(frames)[:, None] * 14 + 2 + np.arange(14)[None, :]
    x0, x1, x2 = s[idx], s[idx - 1], s[idx - 2]
    e0 = -(x0 * x0).sum(1)
    r1 = -(x1 * x0).sum(1)
    r2 = -(x2 * x0).sum(1)
    a11 = (x1 * x1).sum(1)
    a12 = (x1 * x2).sum(1)
    a22 = (x2 * x2).sum(1)
    ok = np.abs(e0) > 10.0
    # AnalyzeRanges: reject empty rows and ill-conditioned matrices (partial-pivot LU).
    row1, row2 = np.maximum(np.abs(a11), np.abs(a12)), np.maximum(np.abs(a12), np.abs(a22))
    ok &= (row1 >= np.finfo(float).eps) & (row2 >= np.finfo(float).eps)
    det = a11 * a22 - a12 * a12
    with np.errstate(divide='ignore', invalid='ignore'):
        piv = np.where(np.abs(a11) / row1 >= np.abs(a12) / row2, np.abs(a11), np.abs(a12))
        u22 = np.abs(det) / piv
        ok &= (det != 0.0) & (np.minimum(piv, u22) / np.maximum(piv, u22) >= 1e-10)
        # BidirectionalFilter: solve [[a11 a12][a12 a22]] x = [r1 r2].
        v1 = (r1 * a22 - r2 * a12) / det
        v2 = (a11 * r2 - a12 * r1) / det
        # QuadraticMerge.
        tmp = 1.0 - v2 * v2
        v1 = (v1 - v1 * v2) / tmp
    ok &= (tmp != 0.0) & (np.abs(v1) <= 1.0)
    v1, v2 = np.clip(v1[ok], -0.9999999999, 0.9999999999), np.clip(v2[ok], -0.9999999999, 0.9999999999)
    if len(v1) == 0:
        return np.zeros((8, 2), np.int16)
    rec = np.stack([np.ones_like(v1), v2 * v1 + v1, v2], 1)  # FinishRecord
    filt = _matrix_filter(rec)
    avg = np.array([1.0, filt[:, 1].mean(), filt[:, 2].mean()])
    best = np.zeros((8, 3))
    best[0] = _merge_finish_record(avg)
    exp = 1
    for w in range(3):
        for i in range(exp):
            best[exp + i] = best[i] + 0.01 * np.array([0.0, -1.0, 0.0])
        exp = 1 << (w + 1)
        for _ in range(2):  # FilterRecords
            near = np.argmin(_contrast(best[:exp], rec), 1)
            for i in range(exp):
                sel = near == i
                acc = filt[sel].mean(0) if sel.any() else np.zeros(3)
                best[i] = _merge_finish_record(acc)
    c = np.rint(-best[:, 1:3] * 2048.0)
    c = np.nan_to_num(c, nan=0.0)
    return np.clip(c, -32768, 32767).astype(np.int16)


# ---------------------------------------------------------------------------
# Frame encoder, vectorised across streams.

def encode_streams(streams, coefs):
    """Encode int16 streams with their (8, 2) coefs. Returns a list of uint8 arrays."""
    S = len(streams)
    frames = np.array([(len(p) + 13) // 14 for p in streams])
    order = np.argsort(-frames, kind='stable')
    maxf = int(frames.max()) if S else 0
    pcm = np.zeros((S, maxf * 14), np.int32)
    for k, p in enumerate(streams):
        pcm[k, :len(p)] = p
    c = np.array(coefs, np.int64).reshape(S, 8, 2)
    c1 = c[:, :, 0][:, :, None]  # (S, 8, 1) weights yn1
    c2 = c[:, :, 1][:, :, None]
    step = (1 << np.arange(SCALES, dtype=np.int64))[None, None, :]  # (1, 1, 13)
    stepf = step.astype(np.float64)
    out = np.zeros((S, maxf, 8), np.uint8)
    h1 = np.zeros(S, np.int64)
    h2 = np.zeros(S, np.int64)
    active = S
    for f in range(maxf):
        while active and frames[order[active - 1]] <= f:
            active -= 1
        sel = np.sort(order[:active])
        x = pcm[sel, f * 14:f * 14 + 14].astype(np.int64)  # (A, 14)
        A = len(sel)
        y1 = np.broadcast_to(h1[sel][:, None, None], (A, 8, SCALES)).copy()
        y2 = np.broadcast_to(h2[sel][:, None, None], (A, 8, SCALES)).copy()
        a1, a2 = c1[sel], c2[sel]
        err = np.zeros((A, 8, SCALES), np.int64)
        nib = np.empty((14, A, 8, SCALES), np.int64)
        dec = np.empty((14, A, 8, SCALES), np.int64)
        for s in range(14):
            pred = a1 * y1 + a2 * y2
            xs = x[:, s][:, None, None]
            d = (xs << 11) - pred
            v2 = np.where(d >= 0, d >> 11, -((-d) >> 11))  # C division truncates to zero
            t = v2 / stepf
            q = np.trunc(np.where(v2 > 0, t + 0.4999999, t - 0.4999999)).astype(np.int64)
            np.clip(q, -8, 7, out=q)
            v = (pred + ((q * step) << 11) + 1024) >> 11
            np.clip(v, -32768, 32767, out=v)
            e = xs - v
            err += e * e
            nib[s] = q
            dec[s] = v
            y2 = y1
            y1 = v
        flat = err.reshape(A, -1).argmin(1)
        ci, si = flat // SCALES, flat % SCALES
        ar = np.arange(A)
        n = nib[:, ar, ci, si].T & 0xF  # (A, 14)
        o = out[sel, f]
        o[:, 0] = (ci << 4) | si
        o[:, 1:] = (n[:, 0::2] << 4) | n[:, 1::2]
        out[sel, f] = o
        h1[sel] = dec[13, ar, ci, si]
        h2[sel] = dec[12, ar, ci, si]
    return [out[k, :frames[k]].reshape(-1) for k in range(S)]


# ---------------------------------------------------------------------------
# Decoder (reference for the SDL backend and for checks).

def decode(data, coefs, num_samples, yn1=0, yn2=0):
    """Decode one channel of DSP-ADPCM bytes -> int16 array."""
    data = np.frombuffer(bytes(data), np.uint8)
    frames = (num_samples + 13) // 14
    c = np.asarray(coefs, np.int64).reshape(8, 2)
    hdr = data[0:frames * 8:8]
    b = data[:frames * 8].reshape(frames, 8)[:, 1:]
    nib = np.empty((frames, 14), np.int64)
    nib[:, 0::2] = b >> 4
    nib[:, 1::2] = b & 0xF
    nib[nib >= 8] -= 16
    scale = (1 << (hdr & 0xF).astype(np.int64))[:, None]
    pred = (hdr >> 4).astype(np.int64)
    base = ((nib * scale) << 11) + 1024
    out = np.empty(frames * 14, np.int64)
    for f in range(frames):  # recursive filter: sequential, fine for checks
        k1, k2 = c[pred[f]]
        row = base[f]
        for s in range(14):
            v = (row[s] + k1 * yn1 + k2 * yn2) >> 11
            v = -32768 if v < -32768 else 32767 if v > 32767 else v
            out[f * 14 + s] = v
            yn2, yn1 = yn1, v
    return out[:num_samples].astype(np.int16)


# ---------------------------------------------------------------------------
# File I/O.

def build_file(rate, chans, num_samples, coefs, data, loop=None, block_bytes=0x2000):
    """chans: channel count; coefs/data: per channel (8,2) int16 and uint8 bytes."""
    loop_start, loop_end = loop if loop else (0, num_samples)
    ch_bytes = (num_samples + 13) // 14 * 8
    if chans == 1:
        block_bytes = ch_bytes
    data_off = (HEADER + CHANNEL_INFO * chans + 31) & ~31
    hdr = bytearray(data_off)
    struct.pack_into('<4sBBBBIIIIIII', hdr, 0, MAGIC, VERSION, chans, 1 if loop else 0, 0,
                     rate, num_samples, loop_start, loop_end, data_off, ch_bytes, block_bytes)
    for ch in range(chans):
        d = data[ch]
        o = HEADER + CHANNEL_INFO * ch
        struct.pack_into('<16h', hdr, o, *np.asarray(coefs[ch]).reshape(-1).tolist())
        lf = loop_start // 14
        lps = d[lf * 8] if loop_start else d[0]
        ly1 = ly2 = 0
        if loop_start:
            full = decode(d, coefs[ch], loop_start)
            ly1 = int(full[-1]) if loop_start >= 1 else 0
            ly2 = int(full[-2]) if loop_start >= 2 else 0
        struct.pack_into('<HhhHhh', hdr, o + 0x20, d[0], 0, 0, lps, ly1, ly2)
    body = bytearray()
    for b0 in range(0, ch_bytes, block_bytes):
        for ch in range(chans):
            body += bytes(data[ch][b0:b0 + block_bytes])
    return bytes(hdr) + bytes(body)


def read_header(buf):
    magic, ver, chans, loop, _, rate, n, ls, le, off, chb, blk = struct.unpack_from('<4sBBBBIIIIIII', buf, 0)
    if magic != MAGIC:
        raise ValueError('not an S2AD file')
    chinfo = []
    for ch in range(chans):
        o = HEADER + CHANNEL_INFO * ch
        coefs = np.array(struct.unpack_from('<16h', buf, o), np.int16).reshape(8, 2)
        chinfo.append(coefs)
    return dict(version=ver, channels=chans, loop=loop, rate=rate, samples=n, loop_start=ls,
                loop_end=le, data_offset=off, channel_bytes=chb, block_bytes=blk, coefs=chinfo)


def raw_channels(buf):
    """-> (header dict, list of per-channel ADPCM bytes), undoing the block interleave."""
    h = read_header(buf)
    per = [bytearray() for _ in range(h['channels'])]
    pos = h['data_offset']
    for b0 in range(0, h['channel_bytes'], h['block_bytes']):
        size = min(h['block_bytes'], h['channel_bytes'] - b0)
        for ch in range(h['channels']):
            per[ch] += buf[pos:pos + size]
            pos += size
    return h, per


def read_file(buf):
    """-> (header dict, list of int16 arrays per channel)."""
    h, per = raw_channels(buf)
    return h, [decode(per[ch], h['coefs'][ch], h['samples']) for ch in range(h['channels'])]


def nintendo_dsp(h, coefs, data):
    """One channel as a standard big-endian Nintendo .dsp file (for cross-checks with vgmstream)."""
    n = h['samples']
    nibbles = n // 14 * 16 + (n % 14 + 2 if n % 14 else 0)
    hdr = struct.pack('>IIIHHIII16hHHhhHhh', n, nibbles, h['rate'], 0, 0, 2, nibbles - 1, 2,
                      *np.asarray(coefs).reshape(-1).tolist(), 0, data[0], 0, 0, 0, 0, 0)
    return hdr + bytes(0x60 - len(hdr)) + bytes(data)


def write_wav(path, rate, chans):
    x = np.stack(chans, 1).astype('<i2')
    with open(path, 'wb') as f:
        f.write(struct.pack('<4sI4s4sIHHIIHH4sI', b'RIFF', 36 + x.nbytes, b'WAVE', b'fmt ', 16, 1, len(chans),
                            rate, rate * 2 * len(chans), 2 * len(chans), 16, b'data', x.nbytes))
        f.write(x.tobytes())


if __name__ == '__main__':
    import sys
    if len(sys.argv) == 4 and sys.argv[1] in ('wav', 'dsp'):
        # audio_dsp.py wav in.adpcm out.wav    (decode with this module's decoder)
        # audio_dsp.py dsp in.adpcm out.dsp    (channel 0 as a standard Nintendo .dsp)
        with open(sys.argv[2], 'rb') as fh:
            blob = fh.read()
        if sys.argv[1] == 'wav':
            h, pcm = read_file(blob)
            write_wav(sys.argv[3], h['rate'], pcm)
        else:
            h, per = raw_channels(blob)
            with open(sys.argv[3], 'wb') as fh:
                fh.write(nintendo_dsp(h, h['coefs'][0], per[0]))
    else:
        sys.exit('usage: audio_dsp.py wav|dsp in.adpcm out')
