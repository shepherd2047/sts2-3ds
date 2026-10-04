#!/usr/bin/env python3
"""Film strips for comparing an animation in the original game with our port.

  sheet.py --orig CAPDIR --event REGEX [--nth N] [--ours VIDEO --at SEC] [--times ms,ms,...] -o out.png

--orig CAPDIR: a `game.sh rec-start/rec-stop` session (video.mkv with wall-clock frame times,
  frames.txt, events.txt). Time 0 is the Nth log line matching REGEX (e.g. "playing card STRIKE").
  Use "INPUT release" (written by game.sh drag) to anchor on the input itself; --orig-shift SEC
  (default 0.13, the measured input-to-capture latency) moves time 0.
--ours VIDEO --at SEC: a STS_RECORD video of the preview; time 0 is SEC into it (with
  STS_FIXED_STEP, (event frame - first recorded frame) / 60).
Rows: original on top, ours below, one column per time offset.
"""
import argparse, bisect, os, re, subprocess, tempfile
from PIL import Image, ImageDraw, ImageFont


def grab(video, indices, tmp, tag):
    """Decodes the given frame indices; returns {index: Image}."""
    idx = sorted(set(indices))
    sel = "+".join(f"eq(n\\,{i})" for i in idx)
    pat = os.path.join(tmp, f"{tag}_%04d.png")
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", video, "-vf",
                    f"select='{sel}'", "-fps_mode", "passthrough", pat], check=True, timeout=120)
    return {i: Image.open(pat % (k + 1)).convert("RGB") for k, i in enumerate(idx)}


def orig_frames(cap, regex, nth, shift, times):
    frames = [float(l) for l in open(os.path.join(cap, "frames.txt")) if l.strip()]
    hits = []
    for line in open(os.path.join(cap, "events.txt"), errors="replace"):
        t, _, msg = line.partition(" ")
        if re.search(regex, msg):
            hits.append((float(t), msg.strip()))
    if len(hits) < nth:
        raise SystemExit(f"{cap}: only {len(hits)} events match {regex!r}")
    t0, msg = hits[nth - 1]
    t0 += shift
    idx = []
    for ms in times:
        i = bisect.bisect_left(frames, t0 + ms / 1000)
        idx.append(min(max(i, 0), len(frames) - 1))
    return idx, msg


def fit(img, h):
    return img.resize((round(img.width * h / img.height), h), Image.LANCZOS)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--orig"); ap.add_argument("--event"); ap.add_argument("--nth", type=int, default=1)
    # INPUT lines are written just before cliclick runs; cliclick start-up plus screen capture put the
    # visible effect ~0.13 s later (measured with cursor jumps, 118-161 ms).
    ap.add_argument("--orig-shift", type=float, default=0.13)
    ap.add_argument("--ours"); ap.add_argument("--at", type=float, default=0)
    ap.add_argument("--times", default="-100,0,50,100,150,200,300,400,600,800")
    ap.add_argument("--height", type=int, default=300)
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args()
    times = [int(x) for x in a.times.split(",")]
    rows = []
    with tempfile.TemporaryDirectory() as tmp:
        if a.orig:
            idx, msg = orig_frames(a.orig, a.event, a.nth, a.orig_shift, times)
            imgs = grab(os.path.join(a.orig, "video.mkv"), idx, tmp, "o")
            rows.append(("original: " + msg[:80], [fit(imgs[i], a.height) for i in idx]))
        if a.ours:
            idx = [max(0, round((a.at + ms / 1000) * 60)) for ms in times]
            imgs = grab(a.ours, idx, tmp, "p")
            rows.append(("ours", [fit(imgs[i], a.height) for i in idx]))
    font = ImageFont.load_default(size=16)
    pad, lab = 6, 22
    cw = [max(r[1][c].width for r in rows) for c in range(len(times))]
    W = sum(cw) + pad * (len(times) + 1)
    H = len(rows) * (a.height + lab + pad) + lab + pad
    sheet = Image.new("RGB", (W, H), (24, 24, 28))
    d = ImageDraw.Draw(sheet)
    x = pad
    for c, ms in enumerate(times):
        d.text((x, 4), f"{ms:+d} ms", fill=(255, 220, 120), font=font)
        x += cw[c] + pad
    y = lab
    for name, cells in rows:
        d.text((pad, y), name, fill=(200, 200, 210), font=font)
        y += lab
        x = pad
        for c, im in enumerate(cells):
            sheet.paste(im, (x, y))
            x += cw[c] + pad
        y += a.height + pad
    sheet.save(a.out)
    print(a.out)


if __name__ == "__main__":
    main()
