#!/usr/bin/env python3
"""Drive the DOSBox-X heavy-debug debugger non-interactively through a pseudo-terminal.

    python3 -I dbx_debug.py STEPS.json

STEPS.json:
{
  "dosbox": "/home/u/opt/dosbox-x/bin/dosbox-x",
  "args": ["-conf", "run.conf", "-break-start", "-time-limit", "120"],
  "cwd": "/path/to/run/dir",              # MEMDUMPBIN files land here
  "env": {"SDL_VIDEODRIVER": "dummy"},    # optional: no window
  "steps": [
    {"wait": 2},                             # seconds
    {"cmd": "BPINT 10"},                     # typed into the debugger command line + Enter
    {"key": "F5"},                           # special keys: F5 (run), F10 (step over), F11 (step)
    {"wait_for": "Breakpoint", "timeout": 30},   # substring in the debugger output
    {"wait_file": "MEM.BIN", "timeout": 10},     # wait until a dump file exists
    {"cmd": "MEMDUMPBIN 0 0 A0000 MEM.BIN"},
    {"snap": "shot01"},                          # EGA mode-10h screenshot: shot01.png + shot01.idx
    {"loop": 20, "steps": [{"key": "F5"}, {"wait": 20}, {"snap": "stop{i:02d}"}]}   # repeat, {i} = 1..n
  ]
}
A snap waits up to 60 s for its dumps; when the emulator is not stopped at a breakpoint the
dump never appears and the snap fails, which ends the run (check the last PNG to see why).
The debugger UI is ncurses on the controlling terminal; this script owns that terminal.
Output is appended to dbx_debug.log in cwd. Exits non-zero if a wait times out.
Note: the debugger uppercases typed commands, so dump files are created with UPPERCASE names.
Only type commands while the emulator is stopped: text typed while it runs corrupts the debugger's command
line, and later commands then fail silently. Give each F5 a wait long enough to reach the next breakpoint.
"""
import json
import os
import pty
import select
import signal
import sys
import time

KEYS = {"F5": "\x1b[15~", "F10": "\x1b[21~", "F11": "\x1b[23~", "ENTER": "\r", "ESC": "\x1b"}


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    spec = json.load(open(sys.argv[1]))
    cwd = spec.get("cwd", os.getcwd())
    env = dict(os.environ, TERM="xterm", COLUMNS="160", LINES="50", **spec.get("env", {}))
    log = open(os.path.join(cwd, "dbx_debug.log"), "ab")
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(cwd)
        os.execve(spec["dosbox"], [spec["dosbox"], *spec.get("args", [])], env)
    buf = bytearray()

    def pump(seconds: float) -> None:
        end = time.time() + seconds
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.1)
            if r:
                try:
                    data = os.read(fd, 65536)
                except OSError:
                    return
                if not data:
                    return
                buf.extend(data)
                log.write(data)

    def send(text: str) -> None:
        for ch in text:  # the curses input loop reads one key at a time
            os.write(fd, ch.encode())
            pump(0.02)

    planes_tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ega_planes_to_png.py")

    def expand(steps, i=None):
        for step in steps:
            if "loop" in step:
                for n in range(1, int(step["loop"]) + 1):
                    yield from expand(step["steps"], n)
            elif i is not None:
                yield json.loads(json.dumps(step).replace("{i:02d}", f"{i:02d}").replace("{i}", str(i)))
            else:
                yield step

    status = 0
    mark = 0  # buffer offset of the previous snap
    try:
        for step in expand(spec["steps"]):
            if "snap" in step:
                name = step["snap"].upper()  # the debugger uppercases its command line, file names included
                # Keep the debugger output since the previous snap: its code view shows where we stopped.
                with open(os.path.join(cwd, name + ".TXT"), "wb") as f:
                    f.write(bytes(buf[mark:]))
                mark = len(buf)
                files = []
                for n in range(4):
                    for cmd in ("OUTB 3CE 4", f"OUTB 3CF {n}", f"MEMDUMPBIN A000 0 6D60 {name}.P{n}"):
                        send(cmd)
                        send("\r")
                        pump(0.2)
                    files.append(os.path.join(cwd, f"{name}.P{n}"))
                deadline = time.time() + 60
                while time.time() < deadline and not all(os.path.exists(f) for f in files):
                    pump(0.3)
                if not all(os.path.exists(f) for f in files):
                    print(f"snap {name}: not stopped at a breakpoint (no dump)", file=sys.stderr)
                    status = 1
                    break
                pump(0.5)
                import subprocess
                subprocess.run([sys.executable, "-I", planes_tool, *files, os.path.join(cwd, name + ".png"),
                                "--raw", os.path.join(cwd, name + ".idx")], check=False, capture_output=True)
                print("snap:", name, flush=True)
                continue
            if "wait" in step:
                pump(float(step["wait"]))
            elif "cmd" in step:
                send(step["cmd"])
                send("\r")
                pump(0.3)
            elif "key" in step:
                send(KEYS.get(step["key"], step["key"]))
                pump(0.3)
            elif "wait_for" in step:
                start = len(buf)
                deadline = time.time() + float(step.get("timeout", 30))
                while time.time() < deadline and step["wait_for"].encode() not in buf[start:]:
                    pump(0.2)
                if step["wait_for"].encode() not in buf[start:]:
                    print(f"timeout waiting for {step['wait_for']!r}", file=sys.stderr)
                    status = 1
                    break
            elif "wait_file" in step:
                path = os.path.join(cwd, step["wait_file"].upper())
                deadline = time.time() + float(step.get("timeout", 10))
                while time.time() < deadline and not os.path.exists(path):
                    pump(0.2)
                if not os.path.exists(path):
                    print(f"timeout waiting for file {path}", file=sys.stderr)
                    status = 1
                    break
            print("done:", json.dumps(step), flush=True)
    finally:
        # SIGTERM can open DOSBox-X's "quit anyway?" dialog and never exit: escalate to SIGKILL.
        # (Also set `quit warning = false` in [dosbox] of the run config.)
        for sig in (signal.SIGTERM, signal.SIGKILL):
            try:
                os.kill(pid, sig)
            except ProcessLookupError:
                break
            deadline = time.time() + 3
            while time.time() < deadline:
                pump(0.2)
                done, _ = os.waitpid(pid, os.WNOHANG)
                if done:
                    break
            else:
                continue
            break
        log.close()
    return status


if __name__ == "__main__":
    sys.exit(main())
