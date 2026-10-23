#!/usr/bin/env python3
"""Real-input GUI runner for FabriCAD guided tours (docs/plans/tutorial.md section 5). Stdlib only.

Replays a tour's canonical input (`do` of every step) as real X events (xdotool) against the running app
with the overlay active, and checks through the app's read-only TestProbe (FABRICAD_TEST_PROBE) that
the tour advances step by step and that every `assert` holds. The probe only answers queries; every
action is an X event. Opt-in (decision 2): without FABRICAD_GUI=1 (or FABRICAD_GL_DISPLAY) the runner
exits 77, which ctest reports as skipped.

Backends, first that works (never the user's desktop session unless explicitly allowed):
  1. FABRICAD_GL_DISPLAY=:N   an existing private X display
  2. Xvfb :N (+GLX, Mesa llvmpipe)                                     (CI)
  3. FABRICAD_GUI_ALLOW_DESKTOP=1: a private rootful Xwayland :N (like tools/uitest/ui.sh)
  otherwise exit 77. Missing xdotool also exits 77.

Artefacts in <out>/<run>/: step PNGs (ImageMagick `import`), app log, probe transcript, report.json,
checkpoints/, and an ffmpeg x11grab video kept only on failure (decision 11) or FABRICAD_UITEST_VIDEO=1.

usage: tutorial_run.py --bin build/bin/fabricad --tour resources/tutorials/sample/tutorial.json
                       [--layout default|alt|light] [--out build/tutorial-out] [--display 77]
                       [--expect-stl bracket.stl --stl-volume V --stl-box x0,x1,y0,y1,z0,z1] [--update-checkpoints]
SPDX-License-Identifier: LGPL-2.1-or-later
"""
import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

SKIP = 77
LAYOUTS = {
    "default": {},
    "alt": {"ui/cardPlacement": "sidebar", "ui/sidebarSide": "right", "ui/toolbarArea": "left"},
    "light": {"ui/theme": "light", "ui/orientation": "hexDial", "ui/timelineStyle": "strip"},
}


class Failure(Exception):
    pass


class Probe:
    """JSON-lines client of the app's TestProbe (a QLocalServer on a Unix socket)."""

    def __init__(self, path, transcript):
        self.path = path
        self.sock = None
        self.buf = b""
        self.transcript = transcript

    def connect(self, timeout=30.0):
        end = time.time() + timeout
        while time.time() < end:
            try:
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s.connect(self.path)
                self.sock = s
                return
            except OSError:
                time.sleep(0.1)
        raise Failure("probe did not come up at " + self.path)

    def ask(self, **req):
        line = json.dumps(req)
        self.sock.sendall(line.encode() + b"\n")
        while b"\n" not in self.buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise Failure("probe closed the connection")
            self.buf += chunk
        raw, self.buf = self.buf.split(b"\n", 1)
        out = json.loads(raw)
        self.transcript.write(line + "\n<- " + raw.decode() + "\n")
        return out

    def state(self):
        return self.ask(cmd="state")

    def wait(self, pred, timeout, what):
        end = time.time() + timeout
        st = None
        while time.time() < end:
            st = self.state()
            if pred(st):
                return st
            time.sleep(0.02)  # poll every 20 ms (plan 5.5)
        raise Failure("timeout waiting for %s (last state: %s)" % (what, json.dumps(st)[:400]))


def have(tool):
    return shutil.which(tool) is not None


def start_backend(args, env):
    """Returns (display, process or None)."""
    if os.environ.get("FABRICAD_GL_DISPLAY"):
        return os.environ["FABRICAD_GL_DISPLAY"], None
    n = args.display
    size = "%dx%d" % (args.width, args.height)
    for _ in range(50):  # a server of a previous run may still be shutting down
        if not os.path.exists("/tmp/.X%d-lock" % n):
            break
        time.sleep(0.1)
    if os.path.exists("/tmp/.X%d-lock" % n):
        print("tutorial_run: display :%d is in use (pick another with --display)" % n)
        return None, None
    if have("Xvfb"):
        p = subprocess.Popen(["Xvfb", ":%d" % n, "-screen", "0", size + "x24", "+extension", "GLX", "-nolisten", "tcp"],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    elif os.environ.get("FABRICAD_GUI_ALLOW_DESKTOP") == "1" and have("Xwayland"):
        # a private rootful Xwayland server: only its own window exists on it; the runner never
        # touches the compositor's outputs, workspaces or other windows
        p = subprocess.Popen(["Xwayland", ":%d" % n, "-geometry", size], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        return None, None
    disp = ":%d" % n
    for _ in range(100):
        if root_size(dict(env, DISPLAY=disp)):
            return disp, p
        time.sleep(0.1)
    p.terminate()
    return None, None


def root_size(env):
    r = subprocess.run(["xdotool", "getdisplaygeometry"], env=env, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    if r.returncode != 0:
        return None
    try:
        w, h = r.stdout.split()
        return int(w), int(h)
    except ValueError:
        return None


def write_prefs(cfg, layout):
    keys = LAYOUTS[layout]
    if not keys:
        return
    d = os.path.join(cfg, "FabriCAD")
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "FabriCAD.conf"), "w") as f:
        f.write("[ui]\n")
        for k, v in keys.items():
            f.write("%s=%s\n" % (k.split("/", 1)[1], v))


class Runner:
    def __init__(self, args, env, probe, outdir):
        self.args = args
        self.env = env
        self.probe = probe
        self.out = outdir
        self.last = None  # last pointer position

    def xdo(self, *cmd):
        r = subprocess.run(["xdotool"] + list(cmd), env=self.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if r.returncode != 0:
            raise Failure("xdotool %s failed: %s" % (" ".join(cmd), r.stderr.decode().strip()))

    def input_seq(self):
        return self.probe.state()["inputSeq"]

    def after_event(self, seq0, timeout=5.0):
        # each X event must have reached the app before the next one (fixes "one action stale")
        self.probe.wait(lambda st: st["inputSeq"] > seq0, timeout, "input to arrive")

    def locate(self, target):
        anchor = "@step" if target == "@anchor" else target
        r = self.probe.ask(cmd="locate", anchor=anchor)
        if not r.get("ok") or not r.get("visible"):
            # a model target may be off screen: Home once, then retry (plan 3)
            if r.get("via") in ("offscreen", "occluded"):
                self.key("Home")
                time.sleep(0.4)
                r = self.probe.ask(cmd="locate", anchor=anchor)
        if not r.get("visible"):
            raise Failure("target %s not visible: %s" % (json.dumps(target), r.get("why") or r.get("error")))
        return r["point"]

    def move(self, x, y):
        self.xdo("mousemove", "--sync", str(x), str(y))
        self.last = (x, y)

    def open_way(self, target):
        """A target inside a closed menu or a collapsed MODEL folder resolves to the menu's button
        (via "menu") or the folder row (via "expand"): open that first, as a user would, so one
        `click` works in every layout (a tool is a toolbar button in one preset, a menu item in
        another)."""
        anchor = "@step" if target == "@anchor" else target
        for _ in range(3):
            r = self.probe.ask(cmd="locate", anchor=anchor)
            via = r.get("via")
            if not r.get("visible") or via not in ("menu", "expand"):
                return
            px, py = r["point"]
            self.move(px, py)
            time.sleep(0.05)
            seq = self.input_seq()
            if via == "menu":
                # a combo opens with a left click; a toolbar button, section caption or title-bar
                # button with a dropdown opens it with a right click (user decision 2026-10-23: a
                # left click on a split button runs its tool)
                self.xdo("click", "1" if "ComboBox" in r.get("widget", "") else "3")
            else:
                self.xdo("click", "--repeat", "2", "--delay", "80", "1")
            self.after_event(seq)
            self.idle()
            time.sleep(0.15)  # the popup maps and is placed under its button

    def idle(self, timeout=10.0):
        self.probe.wait(lambda st: st.get("idle", True), timeout, "the app to be idle (camera animation)")

    def click(self, target, button=1, count=1, mods=""):
        if button == 1 and count == 1:
            self.open_way(target)
        x, y = self.locate(target)
        self.move(x, y)
        time.sleep(0.05)
        seq = self.input_seq()
        cmd = []
        for m in filter(None, mods.split("+")):
            cmd += ["keydown", m]
        cmd += ["click", "--repeat", str(count), "--delay", "80", str(button)]
        for m in filter(None, mods.split("+")):
            cmd += ["keyup", m]
        self.xdo(*cmd)
        self.after_event(seq)

    def key(self, chord):
        seq = self.input_seq()
        self.xdo("key", "--clearmodifiers", chord)
        self.after_event(seq)

    def type(self, text):
        seq = self.input_seq()
        self.xdo("type", "--delay", "25", text)
        self.after_event(seq)

    def drag(self, d):
        x0, y0 = self.locate(d["from"])
        to = d["to"]
        if isinstance(to, dict):
            x1, y1 = x0 + to.get("dx", 0), y0 + to.get("dy", 0)
        else:
            x1, y1 = self.locate(to)
        button = {"left": 1, "middle": 2, "right": 3}.get(d.get("button", "left"), 1)
        steps = max(2, int(d.get("steps", 10)))
        self.move(x0, y0)
        seq = self.input_seq()
        self.xdo("mousedown", str(button))
        for i in range(1, steps + 1):
            self.move(int(x0 + (x1 - x0) * i / steps), int(y0 + (y1 - y0) * i / steps))
            time.sleep(0.015)
        self.xdo("mouseup", str(button))
        self.after_event(seq)

    def act(self, a):
        self.act1(a)
        self.idle()

    def act1(self, a):
        verb = next(k for k in a if k in ("click", "dclick", "rclick", "move", "drag", "wheel", "type", "key", "wait", "shot", "assert"))
        v = a[verb]
        if verb in ("click", "dclick", "rclick"):
            self.click(v, button=3 if verb == "rclick" else 1, count=2 if verb == "dclick" else 1, mods=a.get("mods", ""))
        elif verb == "move":
            if isinstance(v, dict) and ("dx" in v or "dy" in v):
                x, y = self.last or (0, 0)
                self.move(x + v.get("dx", 0), y + v.get("dy", 0))
            else:
                self.move(*self.locate(v))
        elif verb == "drag":
            self.drag(v)
        elif verb == "wheel":
            self.move(*self.locate(v))
            clicks = int(a.get("clicks", 1))
            seq = self.input_seq()
            self.xdo("click", "--repeat", str(abs(clicks)), "4" if clicks > 0 else "5")
            self.after_event(seq)
        elif verb == "type":
            self.type(v)
        elif verb == "key":
            self.key(v)
        elif verb == "wait":
            self.probe.wait(lambda st: self.probe.ask(cmd="eval", cond=v).get("value"), 20, "wait " + json.dumps(v))
        elif verb == "shot":
            self.shot(v)
        elif verb == "assert":
            self.asserts(v if isinstance(v, list) else [v])

    def asserts(self, exprs):
        for e in exprs:
            r = self.probe.ask(cmd="check", expr=e)
            if not r.get("value"):
                raise Failure("CHECK failed: " + r.get("message", e))

    def shot(self, name):
        if have("import"):
            subprocess.run(["import", "-window", "root", os.path.join(self.out, name + ".png")], env=self.env,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def steps_of(tour):
    for ch in tour["chapters"]:
        for s in ch["steps"]:
            yield ch, s


def run(args):
    if os.environ.get("FABRICAD_GUI") != "1" and not os.environ.get("FABRICAD_GL_DISPLAY"):
        print("tutorial_run: skipped (opt-in: set FABRICAD_GUI=1)")
        return SKIP
    if not have("xdotool"):
        print("tutorial_run: skipped (xdotool missing)")
        return SKIP
    with open(args.tour) as f:
        tour = json.load(f)
    w, h = tour.get("window", [1600, 1000])
    args.width, args.height = w, h
    run_id = time.strftime("%Y%m%d-%H%M%S") + "-%s-%s" % (tour["id"], args.layout)
    out = os.path.join(args.out, run_id)
    os.makedirs(os.path.join(out, "checkpoints"), exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="fabricad-tour-")
    cfg, data = os.path.join(tmp, "cfg"), os.path.join(tmp, "data")
    os.makedirs(cfg)
    os.makedirs(data)
    write_prefs(cfg, args.layout)
    sock = os.path.join(tmp, "probe.sock")
    env = dict(os.environ)
    disp, xserver = start_backend(args, env)
    if not disp:
        print("tutorial_run: skipped (no private X backend: Xvfb, FABRICAD_GL_DISPLAY or FABRICAD_GUI_ALLOW_DESKTOP=1)")
        shutil.rmtree(tmp, ignore_errors=True)
        return SKIP
    # a rootful Xwayland window may be tiled by the compositor: the display follows the tile. The
    # runner never touches the compositor; it runs at the size it got and records it.
    time.sleep(0.5)
    got = root_size(dict(env, DISPLAY=disp))
    if got and (got[0] < w or got[1] < h):
        print("tutorial_run: note: display %s is %dx%d, not %dx%d (tiled); running at that size" % (disp, got[0], got[1], w, h))
        w, h = got
    tour_dir = os.path.dirname(os.path.abspath(args.tour))
    env.update({
        "DISPLAY": disp, "XDG_CONFIG_HOME": cfg, "XDG_DATA_HOME": data, "QT_QPA_PLATFORM": "xcb",
        "QT_ENABLE_HIGHDPI_SCALING": "0", "QT_SCALE_FACTOR": "1", "QT_FONT_DPI": "96", "LANG": "C.UTF-8",
        "FABRICAD_NO_RECOVERY": "1", "FABRICAD_TEST_PROBE": sock, "FABRICAD_NO_TUTORIAL": "0",
        "FABRICAD_TUTORIAL_WELCOME": "1", "FABRICAD_TUTORIAL_CHECKPOINT_DIR": os.path.join(out, "checkpoints"),
        "FABRICAD_TUTORIAL_PATH": os.path.dirname(tour_dir), "FABRICAD_WINDOW_SIZE": "%dx%d" % (w, h),
    })
    env.pop("WAYLAND_DISPLAY", None)
    video = None
    if have("ffmpeg"):
        video = subprocess.Popen(["ffmpeg", "-loglevel", "error", "-y", "-f", "x11grab", "-framerate", "10",
                                  "-video_size", "%dx%d" % (w, h), "-i", disp, os.path.join(out, "run.mp4")],
                                 env=env, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    log = open(os.path.join(out, "app.log"), "w")
    # the run folder is the app's working directory: file dialogs open there, so the files a tour
    # saves or exports ("bracket", "bracket.stl") land next to the report
    app = subprocess.Popen([os.path.abspath(args.bin)], env=env, stdout=log, stderr=subprocess.STDOUT, cwd=out)
    transcript = open(os.path.join(out, "probe.txt"), "w")
    probe = Probe(sock, transcript)
    report = {"tour": tour["id"], "layout": args.layout, "size": [w, h], "steps": [], "ok": False}
    code = 1
    try:
        probe.connect()
        st = probe.wait(lambda s: s.get("ready"), 30, "window ready")
        # the window on a WM-less private display: top-left corner (only our own window)
        subprocess.run(["xdotool", "search", "--sync", "--pid", str(app.pid), "--name", "FabriCAD", "windowmove", "0", "0"],
                       env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
        r = Runner(args, env, probe, out)
        # start through real input: the welcome card, else Help > Tutorials > <title>
        loc = probe.ask(cmd="locate", anchor="TutorialStart")
        started = False
        if loc.get("visible"):
            # the card offers one tour (the probe says which): ours, or "Not now" and the Help menu
            if probe.state().get("welcome", tour["id"]) == tour["id"]:
                r.click("TutorialStart")
                started = probe.state()["tour"].get("id") == tour["id"]
            else:
                r.click("TutorialNotNow")
        if not started:
            if probe.state()["tour"].get("id"):
                raise Failure("another tour started from the welcome card")
            r.click("HelpButton")
            r.click("menu:Help/Tutorials")
            r.click("menu:Help/Tutorials/" + tour["title"])
        probe.wait(lambda s: s["tour"].get("id") == tour["id"], 10, "tour start")
        for ch, s in steps_of(tour):
            st = probe.state()
            cur = st["tour"].get("step")
            if cur != s["id"]:
                if "skip_if" in s:
                    report["steps"].append({"id": s["id"], "skipped": True})
                    continue
                raise Failure("expected step %s, the tour shows %s" % (s["id"], cur))
            until = s.get("until", {"next": True})
            if probe.ask(cmd="eval", cond=until).get("value") and "next" not in json.dumps(until):
                raise Failure("step %s: 'until' is already true before any input" % s["id"])
            t0 = time.time()
            for a in s.get("do", []):
                r.act(a)
            if "next" in json.dumps(until):
                r.click("TutorialNext")
            timeout = s.get("timeout_ms", 20000) / 1000.0
            nxt = probe.wait(lambda x: x["tour"].get("step") != s["id"], timeout, "step %s to complete" % s["id"])
            r.asserts(s.get("assert", []))
            r.shot("step-" + s["id"].replace("/", "_"))
            report["steps"].append({"id": s["id"], "ms": int((time.time() - t0) * 1000)})
            if not nxt["tour"].get("id"):
                break
        st = probe.state()
        if st["tour"].get("id"):
            raise Failure("the tour did not finish (still at %s)" % st["tour"].get("step"))
        if root_size(env) != (w, h) and root_size(env) is not None and not got:
            raise Failure("the display size changed during the run: %s" % (root_size(env),))
        if args.expect_stl:
            sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
            import stlcheck  # noqa: E402 (tools/uitest/stlcheck.py)
            b = [float(x) for x in args.stl_box.split(",")]
            fails, info = stlcheck.check(os.path.join(out, args.expect_stl), args.stl_volume,
                                         [(b[0], b[1]), (b[2], b[3]), (b[4], b[5])], vol_tol=args.stl_vol_tol)
            report["stl"] = {"file": args.expect_stl, "volume": info and info["volume"], "fails": fails}
            if fails:
                raise Failure("STL checks: " + "; ".join(fails))
        report["ok"] = True
        code = 0
        if args.update_checkpoints:
            for f in os.listdir(os.path.join(out, "checkpoints")):
                shutil.copy(os.path.join(out, "checkpoints", f), os.path.join(tour_dir, f))
    except Failure as e:
        report["error"] = str(e)
        print("tutorial_run: FAILED: %s" % e)
        try:
            with open(os.path.join(out, "state.json"), "w") as f:
                json.dump(probe.state(), f, indent=1)
            with open(os.path.join(out, "document.json"), "w") as f:
                json.dump(probe.ask(cmd="docjson").get("doc"), f, indent=1)
            Runner(args, env, probe, out).shot("failure")
        except Exception:
            pass
    finally:
        transcript.close()
        app.terminate()
        try:
            app.wait(10)
        except subprocess.TimeoutExpired:
            app.kill()
        log.close()
        if video:
            try:
                video.communicate(b"q", timeout=10)
            except Exception:
                video.kill()
            if code == 0 and os.environ.get("FABRICAD_UITEST_VIDEO") != "1":
                try:
                    os.remove(os.path.join(out, "run.mp4"))  # decision 11: video on failure only
                except OSError:
                    pass
        if xserver:
            xserver.terminate()
            try:
                xserver.wait(10)  # the next run may reuse the display number
            except subprocess.TimeoutExpired:
                xserver.kill()
        with open(os.path.join(out, "report.json"), "w") as f:
            json.dump(report, f, indent=1)
        shutil.rmtree(tmp, ignore_errors=True)
    print("tutorial_run: %s (%s)" % ("ok" if code == 0 else "failed", out))
    return code


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--bin", required=True)
    ap.add_argument("--tour", required=True)
    ap.add_argument("--layout", default="default", choices=sorted(LAYOUTS))
    ap.add_argument("--out", default="build/tutorial-out")
    ap.add_argument("--display", type=int, default=int(os.environ.get("FABRICAD_GUI_DISPLAY", "77")))
    ap.add_argument("--expect-stl", help="after the tour: this STL (in the run folder) must pass tools/uitest/stlcheck.py")
    ap.add_argument("--stl-volume", type=float, default=0.0)
    ap.add_argument("--stl-box", default="0,0,0,0,0,0", help="xmin,xmax,ymin,ymax,zmin,zmax")
    ap.add_argument("--stl-vol-tol", type=float, default=0.003)
    ap.add_argument("--update-checkpoints", action="store_true",
                    help="copy the chapter checkpoints of a passing run into the tour folder")
    sys.exit(run(ap.parse_args()))


if __name__ == "__main__":
    main()
