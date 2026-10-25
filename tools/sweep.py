"""Render every parameter at both ends of its range and fail if any made no
difference -- for both plugins.

**This is the only thing in the repo that catches a dead control.** A GLSL
uniform whose name does not match the C++ is silently ignored --
`glGetUniformLocation` returns -1 and `glUniform` on -1 is a documented no-op
-- so a slider can be stone dead while everything compiles, links, loads and
renders.

Each control is swept where it can act. Every render is 180 frames (3 s at
60 fps) at 320x180 (CI's raster). Most act on the default target (the
Pillars); the generative Seed is swept on a generative target, the galaxies'
and deep field's on theirs; the events are presses (`--at`). An option
parameter reads back 0..1 whatever its count (the fleet's trap), so options are
set here by name. A control whose ends differ by less than `--floor` (mean
8-bit difference per channel) is barely alive: vectrix's lesson.

Usage::

    tools/sweep.py [--build BUILD_DIR] [--verbose] [--jobs N] [--bare]

`--bare` drops every per-control context (keeping only the base), the
addendum's honesty check: what goes dead there is the list of controls that
only act with another one set, which AGENTS.md records.
"""

import argparse
import concurrent.futures
import pathlib
import re
import subprocess
import sys
import tempfile
import zlib

REPO = pathlib.Path(__file__).resolve().parent.parent

BASE = []

SN = [["--at", "20", "Supernova=1"]]
# Events are a few pixels of a frame: sweep them where they show, on a dark
# sky, a few at once, the supernova near its peak.
DARK = ["Target=Gen Starfield", "Seed=0.3"]
FIRE = [["--at", "120", "Fire Meteor=1"], ["--at", "135", "Fire Meteor=1"], ["--at", "150", "Fire Meteor=1"]]

# name -> (low settings, high settings, context settings)
SWEEP = {
    "Target": (["Target=Pillars"], ["Target=Whirlpool"], []),
    "Telescope": (["Telescope=As Released"], ["Telescope=Webb NIRCam"], []),
    "Seed": (["Seed=0"], ["Seed=1"], ["Target=Gen Nebula"]),
    "Gas Density": (["Gas Density=0.2"], ["Gas Density=0.8"], []),
    "Turbulence": (["Turbulence=0"], ["Turbulence=1"], []),
    "Flow": (["Flow=0"], ["Flow=1"], []),
    "Haze": (["Haze=0"], ["Haze=1"], []),
    "Dust": (["Dust=0.2"], ["Dust=0.8"], []),
    "Starlight": (["Starlight=0.2"], ["Starlight=0.8"], []),
    "Star Heat": (["Star Heat=0.2"], ["Star Heat=0.8"], []),
    "Light Angle": (["Light Angle=0.3"], ["Light Angle=0.7"], []),
    "Stars": (["Stars=0"], ["Stars=1"], []),
    "Spikes": (["Spikes=0"], ["Spikes=1"], []),
    "Seeing": (["Seeing=0"], ["Seeing=1"], []),
    "Zoom": (["Zoom=0.3"], ["Zoom=0.7"], []),
    "Pan X": (["Pan X=0.4"], ["Pan X=0.6"], []),
    "Pan Y": (["Pan Y=0.4"], ["Pan Y=0.6"], []),
    "Spin": (["Spin=0.4"], ["Spin=0.6"], []),
    "Drift": (["Drift=0"], ["Drift=1"], []),
    # Parallax needs the camera off its centre.
    "Depth": (["Depth=0"], ["Depth=1"], ["Pan X=0.7"]),
    "Fly": (["Fly=0"], ["Fly=1"], []),
    "Meteors": (["Meteors=0"], ["Meteors=1"], DARK),
    "Meteor Kind": (["Meteor Kind=Sporadic"], ["Meteor Kind=Shower"], DARK + ["Meteors=1"]),
    "Fire Meteor": ([], FIRE, DARK),
    "Supernova": ([], [["--at", "120", "Supernova=1"]], ["Target=Whirlpool"]),
    "SN Speed": (["SN Speed=0"], ["SN Speed=1"], SN),
    # The echo needs dust and a supernova well past its peak.
    "Light Echo": (["Light Echo=0"], ["Light Echo=1"], SN + ["SN Speed=0.9"]),
    "Exposure": (["Exposure=0.3"], ["Exposure=0.7"], []),
    "Stretch": (["Stretch=0.2"], ["Stretch=0.8"], []),
    "Saturation": (["Saturation=0.2"], ["Saturation=0.8"], []),
    "Clip As": (["Clip As=Behind"], ["Clip As=Gas"], []),
    "Mix": (["Mix=0"], ["Mix=1"], []),
}

# A meteor is a line a pixel or two wide: three of them across a 320x180 frame
# cover about 1% of it, so its mean difference sits under the floor however
# bright it is. Its own floor is a tenth of that (vectrix's "barely alive" is
# for a control that moves the whole picture).
FLOORS = {"Fire Meteor": 0.01}

# The About block: a text line and browser buttons, with no pixel to sweep.
SKIP = {"About", "Project page", "Source on GitHub", "Support the work", "User guide"}


def read_png(path):
    """Enough of PNG for fetest's own writer: 8-bit RGBA, filter 0 rows."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, width, height, idat = 8, 0, 0, b""
    while pos < len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width = int.from_bytes(body[0:4], "big")
            height = int.from_bytes(body[4:8], "big")
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    out = bytearray()
    for y in range(height):
        start = y * (stride + 1)
        out += raw[start + 1:start + 1 + stride]
    return bytes(out)


def difference(a, b):
    if len(a) != len(b):
        return 255.0
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def render(fetest, out, settings, over):
    args = [str(fetest), "--out", str(out), "--size", "320x180", "--frames", "180"] + (["--over"] if over else [])
    for setting in settings:
        args += list(setting) if isinstance(setting, list) else ["--set", setting]
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"fetest failed: {' '.join(args)}\n{result.stderr.strip()}")
    return read_png(out)


def parameters(fetest, over):
    result = subprocess.run([str(fetest), "--list"] + (["--over"] if over else []), capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"fetest --list failed: {result.stderr.strip()}")
    names = []
    for line in result.stdout.splitlines()[1:]:
        parts = re.split(r"\s{2,}", line.strip())
        if len(parts) >= 3 and parts[0].isdigit():
            names.append(parts[1].strip())
    return names


def sweep_one(fetest, scratch, name, bare, over):
    low, high, context = SWEEP[name]
    context = BASE + ([] if bare else context)
    tag = f"p{abs(hash(name))}{'o' if over else 's'}"
    before = render(fetest, scratch / f"{tag}-a.png", context + low, over)
    after = render(fetest, scratch / f"{tag}-b.png", context + high, over)
    return name, difference(before, after)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=pathlib.Path, default=REPO / "build")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--bare", action="store_true")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--floor", type=float, default=0.05,
                        help="mean 8-bit difference below which a control is 'barely alive'")
    args = parser.parse_args()

    build = args.build if args.build.is_absolute() else REPO / args.build
    fetest = build / "sdtest"
    if not fetest.exists():
        print(f"{fetest} not found", file=sys.stderr)
        return 1

    failed = False
    for over in (False, True):
        plugin = "SW Sidereal Over" if over else "SW Sidereal"
        declared = parameters(fetest, over)
        unknown = [n for n in declared if n not in SWEEP and n not in SKIP]
        if unknown:
            # A new parameter with no sweep is a hole, not a pass.
            print(f"{plugin}: no sweep defined for: {', '.join(unknown)}", file=sys.stderr)
            return 1
        dead, weak = [], []
        with tempfile.TemporaryDirectory() as scratch:
            scratch = pathlib.Path(scratch)
            with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
                names = [n for n in declared if n in SWEEP]
                results = list(pool.map(lambda n: sweep_one(fetest, scratch, n, args.bare, over), names))
        for name, delta in results:
            if delta == 0.0:
                dead.append(name)
                print(f"  DEAD {plugin}: {name:16s} both ends identical")
            elif delta < FLOORS.get(name, args.floor):
                weak.append(name)
                print(f"  WEAK {plugin}: {name:16s} mean delta {delta:.4f}")
            elif args.verbose:
                print(f"  ok   {plugin}: {name:16s} mean delta {delta:.3f}")
        print(f"{plugin}: {len(results)} parameters swept{' (bare: base context only)' if args.bare else ''}, "
              f"{len(dead)} dead, {len(weak)} barely alive")
        failed = failed or bool(dead or weak)
    if failed:
        if not args.bare:
            print("\nA parameter that changes nothing is usually a uniform name that does not match\n"
                  "the C++, or a setting nothing reads. Both are silent everywhere else.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
