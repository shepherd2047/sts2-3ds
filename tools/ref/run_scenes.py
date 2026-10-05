#!/usr/bin/env python3
"""Records scenes of tools/ref/scenes/<batch>.txt in the original and in our preview and builds one
comparison sheet per scene. Usage (normally through run_scenes.sh):

  run_scenes.py BATCH [SCENE-REGEX] [--list] [--dry] [--ours-only] [--orig-only] [--no-newrun]

Per scene: our preview is recorded in the background (ours.sh), the original is driven with
game.sh (switching character with `game.sh newrun` when the scene's `char:` differs), then sheet.py
puts both side by side. Results: ../ref-captures/<BATCH>/<name>/ (orig recording, ours.mkv),
../ref-captures/<BATCH>/sheets/<name>.png and a row per scene appended to ../ref-captures/<BATCH>/index.md.
A failing step is logged and the scene skipped; the game always gets `game.sh back` at the end.
The scene file format is described in tools/ref/scenes/README.md.
"""
import argparse, os, re, shlex, subprocess, sys, time

REF = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.join(REF, "game.sh")


def main_checkout():
    r = subprocess.run(["git", "-C", REF, "rev-parse", "--path-format=absolute", "--git-common-dir"],
                       capture_output=True, text=True, timeout=10)
    return os.path.dirname(r.stdout.strip())


CAP = os.environ.get("STS_REF_CAP") or os.path.join(os.path.dirname(main_checkout()), "ref-captures")

# per-step timeouts (s)
T_STEP, T_CON, T_NEWRUN, T_OURS, T_SHEET = 40, 30, 180, 150, 180
DEFAULT_TIMES = "-200,0,100,200,300,500,700,1000,1500,2000"


def parse(path):
    scenes, cur = [], None
    for raw in open(path, encoding="utf-8"):
        line = raw.rstrip("\n")
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        m = re.match(r"^(\w+):\s*(.*)$", line)
        if not m:
            raise SystemExit(f"{path}: bad line: {line!r}")
        key, val = m.group(1), m.group(2).strip()
        if key == "name":
            cur = {"name": val}
            scenes.append(cur)
        elif cur is None:
            raise SystemExit(f"{path}: {key}: before the first name:")
        else:
            cur[key] = val
    for s in scenes:
        for k in ("orig", "ours", "mark"):
            if k not in s:
                raise SystemExit(f"{path}: scene {s['name']} has no {k}:")
    return scenes


def parse_mark(text):
    d = dict(tok.split("=", 1) for tok in shlex.split(text) if "=" in tok)
    return {"orig": d.get("orig", "INPUT release"), "nth": int(d.get("nth", 1)),
            "ours": int(d.get("ours", 500)), "shift": d.get("shift")}


def parse_ours(text):
    env, extra = {}, {}
    for tok in shlex.split(text):
        k, _, v = tok.partition("=")
        if k in ("REC", "NAV"):
            extra[k] = v
        else:
            env[k] = v
    return env, extra


def log(f, msg):
    line = time.strftime("%H:%M:%S ") + msg
    print(line, flush=True)
    f.write(line + "\n")
    f.flush()


def game(args, timeout, logf, dry):
    if dry:
        log(logf, "  game.sh " + " ".join(shlex.quote(a) for a in args))
        return ""
    r = subprocess.run(["bash", GAME] + args, capture_output=True, text=True, timeout=timeout)
    if r.returncode != 0:
        raise RuntimeError(f"game.sh {' '.join(args)} -> {r.returncode}: {(r.stdout + r.stderr).strip()[-200:]}")
    return r.stdout.strip()


def orig_steps(text):
    return [s.strip() for s in text.split(";") if s.strip()]


def run_orig(scene, capdir, logf, dry, no_newrun):
    """Drives the original; returns the recording directory."""
    want = scene.get("char", "")
    if want and not no_newrun:
        have = "" if dry else game(["char"], 10, logf, dry)
        if have != want:
            log(logf, f"  newrun {want} (was {have or 'unknown'})")
            game(["newrun", want, "-"], T_NEWRUN, logf, dry)
    steps = orig_steps(scene["orig"])
    if "rec" not in steps:
        steps = ["rec"] + steps
    recording = False
    try:
        for st in steps:
            word, _, rest = st.partition(" ")
            rest = rest.strip()
            if word == "rec":
                game(["rec-start", os.path.relpath(capdir, CAP)], T_STEP, logf, dry)
                recording = True
            elif word == "wait":
                if not dry:
                    time.sleep(float(rest))
            elif word == "con":
                game(["con", rest], T_CON, logf, dry)
            elif word == "type":
                game(["type", rest], T_STEP, logf, dry)
            elif word == "newrun":
                game(["newrun"] + rest.split(), T_NEWRUN, logf, dry)
            elif word == "potion":
                # belt slot N (top bar, x 285/312/339 y 25 in the 1000-px frame) dragged onto the target
                a = rest.split()
                slot, tgt = int(a[0]), (a[1] if len(a) > 1 else "self")
                tx, ty = {"self": (250, 370), "enemy": (750, 370), "enemy2": (820, 370)}.get(tgt, (750, 370))
                game(["drag", str(258 + 27 * slot), "25", str(tx), str(ty), "600"], T_STEP, logf, dry)
            elif word in ("click", "hover", "drag", "key", "play", "endturn", "shot"):
                game([word] + rest.split(), T_STEP, logf, dry)
            else:
                raise RuntimeError(f"unknown orig step {st!r}")
    finally:
        if recording:
            game(["rec-stop"], T_STEP, logf, dry)


def start_ours(scene, out, logf, dry):
    env, extra = parse_ours(scene["ours"])
    script = env.pop("STS_SCRIPT", "")
    deck = env.pop("STS_DECK", "")
    e = dict(os.environ)
    e.update(env)
    e["ENC"] = env.pop("STS_ENCOUNTER", "")
    e.pop("STS_ENCOUNTER", None)
    if "NAV" in extra:
        e["NAV"] = extra["NAV"] + ("," if extra["NAV"] and not extra["NAV"].endswith(",") else "")
    elif not any(k in env for k in ("STS_ROOM", "STS_EVENT")) and not e["ENC"]:
        e["NAV"] = ""  # menu / screen scenes drive everything from frame 0
    e["OURS_OUT"] = out
    rec = extra.get("REC", "460-700")
    args = ["bash", os.path.join(REF, "ours.sh"), "scene", deck, script, rec]
    if dry:
        log(logf, "  " + " ".join(f"{k}={shlex.quote(v)}" for k, v in sorted(env.items())) +
            f" ENC={e['ENC']} NAV={e.get('NAV', '(default)')} ours.sh scene {deck!r} {script!r} {rec}")
        return None, rec
    p = subprocess.Popen(args, env=e, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return p, rec


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("batch")
    ap.add_argument("regex", nargs="?", default="")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--dry", action="store_true", help="print what would run")
    ap.add_argument("--ours-only", action="store_true")
    ap.add_argument("--orig-only", action="store_true")
    ap.add_argument("--no-newrun", action="store_true", help="never switch character")
    a = ap.parse_args()
    scenes = parse(os.path.join(REF, "scenes", f"{a.batch}.txt"))
    scenes = [s for s in scenes if re.search(a.regex, s["name"])]
    if a.list:
        for s in scenes:
            print(s["name"], s.get("char", ""))
        return
    bdir = os.path.join(CAP, a.batch)
    os.makedirs(os.path.join(bdir, "sheets"), exist_ok=True)
    logf = open(os.path.join(bdir, "run.log"), "a", encoding="utf-8")
    index = os.path.join(bdir, "index.md")
    if not os.path.exists(index):
        with open(index, "w", encoding="utf-8", newline="\n") as f:
            f.write(f"# Batch {a.batch}\n\n| scene | sheet | status | finding |\n|---|---|---|---|\n")
    ok = 0
    try:
        for s in scenes:
            name = s["name"]
            log(logf, f"== {name}")
            sdir = os.path.join(bdir, name)
            os.makedirs(sdir, exist_ok=True)
            ours_mkv = os.path.join(sdir, "ours.mkv")
            sheet = os.path.join(bdir, "sheets", f"{name}.png")
            mark = parse_mark(s["mark"])
            status = "todo"
            proc, rec = None, parse_ours(s["ours"])[1].get("REC", "460-700")
            try:
                if not a.orig_only:
                    proc, rec = start_ours(s, ours_mkv, logf, a.dry)
                if not a.ours_only:
                    run_orig(s, os.path.join(sdir, "orig"), logf, a.dry, a.no_newrun)
                if proc:
                    try:
                        out, _ = proc.communicate(timeout=T_OURS)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        raise RuntimeError("ours.sh timed out")
                    if proc.returncode != 0:
                        raise RuntimeError("ours.sh: " + out.strip()[-200:])
                at = (mark["ours"] - int(rec.split("-")[0])) / 60
                cmd = ["python3", os.path.join(REF, "sheet.py"), "--times", s.get("times", DEFAULT_TIMES),
                       "--wrap", "5", "--height", "240", "-o", sheet]
                if not a.ours_only:
                    cmd += ["--orig", os.path.join(sdir, "orig"), "--event", mark["orig"], "--nth", str(mark["nth"])]
                    if mark["shift"]:
                        cmd += ["--orig-shift", mark["shift"]]
                if not a.orig_only:
                    cmd += ["--ours", ours_mkv, "--at", f"{at:.3f}"]
                if a.dry:
                    log(logf, "  " + " ".join(shlex.quote(c) for c in cmd))
                else:
                    r = subprocess.run(cmd, capture_output=True, text=True, timeout=T_SHEET)
                    if r.returncode != 0:
                        raise RuntimeError("sheet.py: " + (r.stdout + r.stderr).strip()[-200:])
                ok += 1
            except Exception as e:  # noqa: BLE001 - log and go on with the next scene
                status = f"FAILED: {str(e).splitlines()[0][:150] if str(e) else type(e).__name__}"
                log(logf, "  " + status)
                if proc and proc.poll() is None:
                    proc.kill()
            if not a.dry:
                rel = os.path.relpath(sheet, bdir) if os.path.exists(sheet) and status == "todo" else "-"
                with open(index, "a", encoding="utf-8", newline="\n") as f:
                    f.write(f"| {name} | {rel} | {status} | |\n")
            log(logf, f"  -> {status}" + (f" {sheet}" if status == "todo" else ""))
    finally:
        if not a.ours_only and not a.dry:
            subprocess.run(["bash", GAME, "back"], timeout=10)
    log(logf, f"done: {ok}/{len(scenes)} sheets, index {index}")


if __name__ == "__main__":
    main()
