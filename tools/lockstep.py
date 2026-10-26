#!/usr/bin/env python3
"""Lockstep verification: the original in DOSBox-X (golden) against the recompiled port, stop by stop.

    python3 lockstep.py golden SESSION.json OUTDIR     # scripted DOSBox-X debugger capture
    python3 lockstep.py port   SESSION.json OUTDIR [--golden GOLDEN_DIR]  # port; clock inputs from golden
    python3 lockstep.py compare GOLDEN_DIR PORT_DIR    # first difference, with details

SESSION.json:
  {"bda_keys": ["316E"],                     # BIOS keyboard buffer contents at the unpacked entry
   "breaks": ["19AE:59D3", "0824:22FC"],     # stops: runtime blocking key wait, turn input
   "stop_keys": ["3920", "-", ...],          # key word put in the BIOS buffer at stop k (k = 1..), "-" none
   "pokes": ["0824:AEFE=257A:010A:0001"],    # optional one-shot word writes when execution reaches CS:IP
   "stops": 40}
Both sides stop at the same code events, dump the same state and inject the same key, so the
stop sequences must agree exactly. Compare masks dead stack (STACK_SLACK bytes below the port's SP). Per stop k: Skkk.P0-P3 (EGA planes, 28000 B), Skkk.D (DGROUP
257A:0000, 65535 B), Skkk.B (BIOS data area, 256 B). Whole session: SPEAKER.TXT (speaker port writes,
instrumented DOSBox-X REA_LOG_SPEAKER), compared as an exact sequence with interval differences reported. Emulated time is not a lockstep input: only key
order is. The clock is an input: the port takes the BIOS tick count at the entry (golden ENTRY.B) and
replays every DOS get-time result the original read (golden DOSTIME.TXT, written by the instrumented
DOSBox-X build: REA_LOG_DOSTIME), so TIMER and RANDOMIZE TIMER see the original's values.
"""
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent
# DOSBox-X built with tools/dosbox/dosbox-x-rea-instrumentation.patch (REA_DOSBOX, else dosbox-x on PATH)
DOSBOX = Path(os.environ.get("REA_DOSBOX") or shutil.which("dosbox-x") or "dosbox-x")
ENTRY = "21E5:0944"  # unpacked program entry (after PKLITE and EXEPACK) in the reference machine
DGROUP = 0x257A
STACK_SLACK = 0x400  # bytes below SP treated as dead stack in compare
# QuickBASIC background sound engine state in DGROUP (note queue 46A4-46BB, active flag 4812): how far the
# music has played when the game reaches a stop depends on CPU speed relative to the PIT (it differed
# between real PCs too). Masked in the state compare; the sound itself is compared as the exact speaker
# write sequence (SPEAKER.TXT).
SOUND_ENGINE = set(range(0x46A4, 0x46BC)) | {0x4812}
POST_MS = 100.0      # golden speaker writes before this (emulated ms since power-on) are BIOS POST


def golden(session: dict, out: Path) -> int:
    out.mkdir(parents=True, exist_ok=True)
    conf = (PROJECT / "work/dos/run.conf").read_text().splitlines()
    conf = [l for l in conf if not l.strip().upper().startswith("AUTOTYPE")]  # keys come from the session
    if (out / "c").exists():
        shutil.rmtree(out / "c")
    shutil.copytree(PROJECT / "work/dos/c", out / "c")  # fresh game files per capture: the game writes files
    conf = [l.replace("../c", str(out / "c")) for l in conf]
    (out / "ls.conf").write_text("\n".join(conf) + "\n")
    steps = [{"wait": 4}, {"cmd": f"BP {ENTRY}"}, {"key": "F5"}, {"wait": 6},
             {"cmd": "BPDEL *"}]  # the entry breakpoint is deleted before the session breakpoints are set
    steps += [{"cmd": "MEMDUMPBIN 0040 0 100 ENTRY.B"}, {"wait_file": "ENTRY.B", "timeout": 30}]
    keys = session.get("bda_keys", [])
    if keys:
        words = " ".join(f"W:{k}" for k in keys)
        steps += [{"cmd": f"SM 0040:001E {words}"},
                  {"cmd": f"SM 0040:001A W:1E W:{0x1E + 2 * len(keys):X}"}]
    for pk in session.get("pokes", []):  # one-shot writes: breakpoint at CS:IP, SM, then the session breakpoints
        at, dest = pk.split("=")
        seg_off, val = dest.rsplit(":", 1)
        steps += [{"cmd": f"BP {at}"}, {"key": "F5"}, {"wait": 40}, {"cmd": f"SM {seg_off} W:{val}"}, {"cmd": "BPDEL *"}]
    steps += [{"cmd": f"BP {b}"} for b in session["breaks"]]
    for k in range(1, session["stops"] + 1):
        name = f"S{k:03d}"
        steps += [{"key": "F5"}, {"wait": 40 if k == 1 else 15}, {"snap": name},  # keys typed while running corrupt the debugger command line
                  {"cmd": f"MEMDUMPBIN {DGROUP:04X} 0 FFFF {name}.D"},
                  {"cmd": f"MEMDUMPBIN 0040 0 100 {name}.B"},
                  {"wait_file": f"{name}.B", "timeout": 30}]
        if k <= len(session["stop_keys"]) and session["stop_keys"][k - 1] != "-":
            steps += [{"cmd": f"SM 0040:001E W:{session['stop_keys'][k - 1]}"},
                      {"cmd": "SM 0040:001A W:1E W:20"}]
    spec = {"dosbox": str(DOSBOX),
            "args": ["-conf", "ls.conf", "-break-start", "-time-limit", str(60 + 40 * session["stops"]),
                     "-nogui", "-nomenu"],
            "cwd": str(out), "env": {"SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy",
                                     "REA_LOG_DOSTIME": str(out / "DOSTIME.TXT"),
                                     "REA_LOG_SPEAKER": str(out / "SPEAKER.TXT"),
                                     "REA_LOG_VGA": str(out / "VGA.TXT"),
                                     "REA_LOG_BREAK": str(out / "BREAK.TXT"),
                                     "REA_LOG_CB": str(out / "CB.TXT"),
                                     **({"REA_LOG_INSN": str(out / "INSN.TXT"), "REA_LOG_INSN_AT": os.environ.get("REA_LOG_INSN_AT", ENTRY),
                                         "REA_LOG_INSN_MS": os.environ["REA_LOG_INSN_MS"]}
                                        if os.environ.get("REA_LOG_INSN_MS") else {})}, "steps": steps}
    (out / "steps.json").write_text(json.dumps(spec, indent=1))
    return subprocess.call([sys.executable, str(HERE / "dosbox/dbx_debug.py"), str(out / "steps.json")])


def port(session: dict, out: Path, ticks: str | None, sync: str | None) -> int:
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    run = PROJECT / "recomp/run_ls"  # fresh copy of the game files: the game writes its files
    if run.exists():
        shutil.rmtree(run)
    shutil.copytree(PROJECT / "work/dos/c", run)
    cmd = [str(PROJECT / "recomp/build-rc/rc_headless"), "--data", str(run), "--ms", str(20000 * session["stops"] + 60000),
           "--bda-keys", ",".join(session.get("bda_keys", [])), "--break", ",".join(session["breaks"]),
           "--stop-keys", ",".join(session["stop_keys"]), "--dump-dir", str(out), "--stops", str(session["stops"]),
           "--png", str(out / "end.png"), "--speaker-io", str(out / "SPEAKER.TXT")]
    cmd += os.environ.get("REA_PORT_ARGS", "").split()  # e.g. --verify-native
    if session.get("pokes"):
        cmd += ["--poke", ",".join(session["pokes"])]
    if ticks:
        cmd += ["--ticks", ticks]
    if sync:
        cmd += ["--dos-time", str(Path(sync) / "DOSTIME.TXT")]
        if (Path(sync) / "VGA.TXT").exists() and (Path(sync) / "BREAK.TXT").exists():  # the golden run's display timing
            sys.path.insert(0, str(PROJECT / "recomp/recompiler"))
            import refmachine
            rows = refmachine.vga_timing(str(Path(sync) / "VGA.TXT"), refmachine.entry_ms(str(Path(sync) / "BREAK.TXT"), ENTRY))
            refmachine.write_vga_timing(rows, str(out / "VGATIMING.TXT"))
            cmd += ["--vga-timing", str(out / "VGATIMING.TXT")]
            cmd += ["--pit0-phase", f"{refmachine.pit0_phase(str(Path(sync) / 'BREAK.TXT'), ENTRY):.12f}"]
    return subprocess.call(cmd)


def speaker(g: Path, p: Path) -> int:
    """Speaker port writes (PIT ch2 data/mode, port 61h): the sequences must be identical. Tone timing is
    reported as the largest difference between golden and port intervals (emulated ms) between writes."""
    def load(f: Path, after_ms: float):
        rows = [l.split() for l in f.read_text().splitlines() if l.strip()] if f.exists() else []
        rows = [r for r in rows if float(r[2]) >= after_ms]
        # Port 61h bits 4-7 are read-only on an AT (refresh toggle, timer 2 output): the program's
        # read-modify-write only echoes what it read at that instant, and the hardware ignores them.
        return ([(r[0], r[1] if r[0] != "61" else f"{int(r[1], 16) & 0x0F:02X}") for r in rows],
                [float(r[2]) for r in rows])
    if not (g / "SPEAKER.TXT").exists():
        print("speaker: not compared (the golden capture has no SPEAKER.TXT)")
        return 0
    gs, gt = load(g / "SPEAKER.TXT", POST_MS)  # the reference BIOS POST writes port 61h before DOS boots
    ps, pt = load(p / "SPEAKER.TXT", 0.0)
    n = min(len(gs), len(ps))
    first = next((i for i in range(n) if gs[i] != ps[i]), None)
    if first is None and len(gs) != len(ps):
        first = n
    if first is not None:
        print(f"speaker: DIFFERENT at write {first} of {len(gs)} golden / {len(ps)} port: "
              f"golden {gs[first:first + 6]} port {ps[first:first + 6]}")
        return 1
    worst = max((abs((gt[i + 1] - gt[i]) - (pt[i + 1] - pt[i])), i) for i in range(n - 1)) if n > 1 else (0.0, 0)
    gi = gt[worst[1] + 1] - gt[worst[1]] if n > 1 else 0.0
    print(f"speaker: {n} writes equal; largest interval difference {worst[0]:.2f} ms at write {worst[1]} "
          f"(golden interval {gi:.2f} ms)")
    import statistics
    gi_all = [gt[i + 1] - gt[i] for i in range(n - 1)]
    pi_all = [pt[i + 1] - pt[i] for i in range(n - 1)]
    for lo, hi in ((1, 10), (10, 30), (30, 100), (100, 1e12)):
        pairs = [(a, b) for a, b in zip(gi_all, pi_all) if lo <= a < hi]
        if pairs:
            print(f"  intervals {lo}-{hi if hi < 1e12 else 'inf'} ms: {len(pairs)}, median golden "
                  f"{statistics.median(a for a, _ in pairs):.2f} port {statistics.median(b for _, b in pairs):.2f}")
    return 0


def compare(g: Path, p: Path) -> int:
    def ticks(b: bytes) -> int:
        return int.from_bytes(b[0x6C:0x70], "little")
    k = 0
    while True:
        k += 1
        name = f"S{k:03d}"
        complete = lambda d: all((d / f"{name}.{x}").exists() for x in ("P0", "P1", "P2", "P3", "D", "B"))
        if not complete(g) or not complete(p):  # a capture cut off mid-stop ends the comparison there
            print(f"compared {k - 1} stops: all equal (golden has {name}: {(g / f'{name}.P0').exists()}, "
                  f"port has {name}: {(p / f'{name}.P0').exists()})")
            return speaker(g, p)
        diffs = []
        for n in range(4):
            a, b = (g / f"{name}.P{n}").read_bytes(), (p / f"{name}.P{n}").read_bytes()
            nd = sum(x != y for x, y in zip(a, b))
            if nd:
                first = next(i for i, (x, y) in enumerate(zip(a, b)) if x != y)
                diffs.append(f"plane {n}: {nd} bytes differ, first at {first:04X} (row {first // 80}, col {first % 80 * 8})")
        a, b = (g / f"{name}.D").read_bytes(), (p / f"{name}.D").read_bytes()
        dd = [i for i, (x, y) in enumerate(zip(a, b)) if x != y]
        # Dead stack: bytes below SP hold whatever earlier calls and interrupts left there, and interrupts
        # arrive at different instructions in the two machines. Masked, and counted in the report.
        ip_sp = (p / f"{name}.R").read_bytes()
        ss, sp = int.from_bytes(ip_sp[6:8], "little"), int.from_bytes(ip_sp[4:6], "little")
        masked = [i for i in dd if ss == DGROUP and sp - STACK_SLACK <= i < sp] if (p / f"{name}.R").exists() else []
        snd = [i for i in dd if i in SOUND_ENGINE]
        dd = [i for i in dd if i not in set(masked) and i not in SOUND_ENGINE]
        if dd:
            diffs.append(f"DGROUP: {len(dd)} bytes differ: " + " ".join(f"{i:04X}:{a[i]:02X}/{b[i]:02X}" for i in dd[:24]))
        gb, pb = (g / f"{name}.B").read_bytes(), (p / f"{name}.B").read_bytes()
        info = f"ticks golden {ticks(gb):06X} port {ticks(pb):06X}, {len(masked)} dead-stack + {len(snd)} sound-engine bytes masked"
        if diffs:
            print(f"{name}: DIFFERENT ({info})")
            for d in diffs:
                print("  " + d)
            return 1
        print(f"{name}: equal ({info})")


def main() -> int:
    a = sys.argv[1:]
    if len(a) >= 3 and a[0] == "golden":
        return golden(json.loads(Path(a[1]).read_text()), Path(a[2]).resolve())
    if len(a) >= 3 and a[0] == "port":
        golden_dir = a[a.index("--golden") + 1] if "--golden" in a else None
        t = None
        if golden_dir:  # clock inputs from the golden capture: ticks at the entry and at every stop
            t = hex(int.from_bytes((Path(golden_dir) / "ENTRY.B").read_bytes()[0x6C:0x70], "little"))
        return port(json.loads(Path(a[1]).read_text()), Path(a[2]).resolve(), t,
                    str(Path(golden_dir).resolve()) if golden_dir else None)
    if len(a) == 3 and a[0] == "compare":
        return compare(Path(a[1]), Path(a[2]))
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
