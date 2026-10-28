"""Compares the cross-check's printouts. Called by crosscheck.sh.

    compare.py port CPP JS      the PORT check, line by line: every word that is not a number
                                must be the same, and every number within its limit (below);
                                a report of how many lines are identical to the bit, by tag
    compare.py frames CPP JS    the FRAME check: every uniform the plugin's GL programs hold
                                equal to the port's (or GL's initial zero where the port has
                                not set it yet), every table it uploaded equal to the port's,
                                every CPU line equal, all within the same limits
    compare.py port|frames CPP JS --report
                                the same comparison measured, never a failure: for the
                                plugin as built for arm64, whose fused multiply-adds round
                                differently from both its x86_64 slice and this port

Numbers are hex words: 16 digits a double, 8 a float (an int in an `i:` or `u:`
uniform, which must be identical). They are compared in units in the last place.

THE LIMITS, and why there are limits at all. JavaScript's Math.exp, Math.pow,
Math.cos and the rest are V8's, the plugin's std::exp and friends are Apple's
libm, and the two disagree in the last bit of a few results in a hundred;
neither promises correct rounding. The JavaScript has no erfc or tgamma at all,
so the port carries fdlibm's erfc and a Lanczos gamma. A port compared through
them can only be compared to within a tolerance, so the tolerance is set where
a libm's last bit can carry a result and no further:

  - FLOATS within 64 ulps (a few parts in ten million). A float is what the GPU
    reads, and a value that has been through a float function (cosf, powf) or
    a single-precision accumulation has had its last bit moved by the libm and
    then amplified once or twice by the arithmetic after it.
  - DOUBLES within a relative 1e-10, or within 1e-300 of zero. Planck's law,
    Simpson's rule over 64 panels and a 200-term series are sums of libm
    results; a port error moves a result by far more than that.

A port error -- a wrong constant, a term dropped, a float rounded where the C++
keeps a double, an argument evaluated in the wrong order (which moves the
seeded generator and with it every star) -- is far outside both, and it is also
usually a different word (a count, a name, a branch). The report says how many
lines and values are identical to the bit, and how far the rest are.
"""
import re
import struct
import sys
from collections import defaultdict

HEX = re.compile(r"[0-9a-f]{16}|[0-9a-f]{8}")
SPLIT = re.compile(r"[ ,=|:]")
FLOAT_ULPS = 64
DOUBLE_REL = 1e-10
REPORT = False


def read(path):
    with open(path) as handle:
        return handle.read().splitlines()


def ordered(word, width):
    """The word's bits as an integer that counts up with the value (for ulps)."""
    i = int(word, 16)
    sign = 1 << (width - 1)
    return -(i & (sign - 1)) if i & sign else i


def value(word):
    return struct.unpack(">d" if len(word) == 16 else ">f", bytes.fromhex(word))[0]


class Tally:
    def __init__(self):
        self.lines = self.same_lines = 0
        self.values = self.same_values = 0
        self.worst_float = 0
        self.worst_double = 0.0
        self.worst_float_abs = 0.0
        self.past = 0
        self.shape = 0

    def note(self, cpp, js):
        """One number; returns a complaint, or None."""
        self.values += 1
        if cpp == js:
            self.same_values += 1
            return None
        if len(cpp) == 8:
            u = abs(ordered(cpp, 32) - ordered(js, 32))
            self.worst_float = max(self.worst_float, u)
            self.worst_float_abs = max(self.worst_float_abs, abs(value(cpp) - value(js)))
            if u > FLOAT_ULPS:
                self.past += 1
                if not REPORT:
                    return f"float {value(cpp)!r} against {value(js)!r} ({u} ulps)"
            return None
        a, b = value(cpp), value(js)
        if a != a or b != b:
            return f"double {a!r} against {b!r}"
        rel = abs(a - b) / max(abs(a), abs(b))
        self.worst_double = max(self.worst_double, rel)
        if rel > DOUBLE_REL and abs(a - b) > 1e-300:
            self.past += 1
            if not REPORT:
                return f"double {a!r} against {b!r} (relative {rel:.2g})"
        return None

    def describe(self):
        if self.lines == self.same_lines:
            return f"{self.lines:7} lines: identical"
        parts = [f"{self.lines:7} lines: {self.same_lines} identical, {self.values - self.same_values} of {self.values} values differ"]
        if self.worst_float:
            parts.append(f"floats within {self.worst_float} ulp{'s' if self.worst_float != 1 else ''}"
                         + (f" (largest difference {self.worst_float_abs:.2g})" if REPORT else ""))
        if self.worst_double:
            parts.append(f"doubles within {self.worst_double:.1g} relative")
        if self.past:
            parts.append(f"{self.past} past the limits")
        if self.shape:
            parts.append(f"{self.shape} lines differ in more than a number")
        return ", ".join(parts)


def compare_words(x, y, tally):
    tx, ty = SPLIT.split(x), SPLIT.split(y)
    if len(tx) != len(ty):
        return f"the lines differ in shape:\n    C++: {x[:200]}\n    js : {y[:200]}"
    for p, q in zip(tx, ty):
        if p == q and not HEX.fullmatch(p):
            continue
        if HEX.fullmatch(p) and HEX.fullmatch(q) and len(p) == len(q):
            complaint = tally.note(p, q)
            if complaint:
                return f"{complaint}\n    C++: {x[:200]}\n    js : {y[:200]}"
        else:
            return f"'{p}' against '{q}':\n    C++: {x[:200]}\n    js : {y[:200]}"
    return None


def port(cpp_path, js_path):
    a, b = read(cpp_path), read(js_path)
    if len(a) != len(b):
        print(f"FAIL  {len(a)} lines from the C++ against {len(b)} from the port")
        return 1
    tallies = defaultdict(Tally)
    for i, (x, y) in enumerate(zip(a, b)):
        tag = x.split(" ", 1)[0]
        t = tallies[tag]
        t.lines += 1
        if x == y:
            t.same_lines += 1
            t.values += len(HEX.findall(x))
            t.same_values += len(HEX.findall(x))
            continue
        complaint = compare_words(x, y, t)
        if complaint:
            if REPORT:
                t.shape += 1
                continue
            print(f"FAIL  line {i + 1} ({tag}): {complaint}")
            return 1
    same = sum(t.same_lines for t in tallies.values())
    if REPORT:
        print(f"{len(a)} lines, {same} identical to the bit:")
    else:
        print(f"{len(a)} lines, {same} identical to the bit; every other number within the limits:")
    width = max(len(k) for k in tallies)
    for tag, t in tallies.items():
        print(f"   {tag:<{width}} {t.describe()}")
    return 0


def parse_u(line):
    words = line.split()
    return words[1], words[2], dict(field.split("=", 1) for field in words[3:])


def frames(cpp_path, js_path):
    a, b = read(cpp_path), read(js_path)
    if len(a) != len(b):
        print(f"FAIL  {len(a)} lines from the plugin against {len(b)} from the port")
        return 1
    tallies = defaultdict(Tally)
    inactive = defaultdict(set)
    seen = defaultdict(set)
    frames_seen = 0
    for i, (x, y) in enumerate(zip(a, b)):
        if x.startswith("U "):
            frame, program, plugin = parse_u(x)
            frame2, program2, port_values = parse_u(y)
            if (frame, program) != (frame2, program2):
                print(f"FAIL  line {i + 1}: out of step ({frame} {program} against {frame2} {program2})")
                return 1
            t = tallies[f"U {program}"]
            t.lines += 1
            same = True
            for name, held in plugin.items():
                seen[program].add(name)
                if name not in port_values:
                    if not re.fullmatch(r"[fiu]:0{8}(,0{8})*", held):
                        print(f"FAIL  frame {frame} {program}: {name} is {held} in the plugin and never set by the port")
                        return 1
                    continue
                mine = port_values[name]
                if held == mine:
                    t.values += held.count(",") + 1
                    t.same_values += held.count(",") + 1
                    continue
                same = False
                if held[0] != mine[0] or held[0] in "iu":
                    print(f"FAIL  frame {frame} {program}: {name} is {held} in the plugin, {mine} in the port (line {i + 1})")
                    if REPORT:
                        continue
                    return 1
                for p, q in zip(held[2:].split(","), mine[2:].split(",")):
                    complaint = t.note(p, q)
                    if complaint:
                        print(f"FAIL  frame {frame} {program}: {name}: {complaint} (line {i + 1})")
                        return 1
            t.same_lines += same
            for name in port_values:
                if name not in plugin:
                    inactive[program].add(name)
            continue
        tag = x.split(" ", 1)[0]
        if tag == "C":
            frames_seen += 1
        t = tallies[tag if tag != "T" else f"T {x.split()[2]}"]
        t.lines += 1
        if x == y:
            t.same_lines += 1
            t.values += len(HEX.findall(x))
            t.same_values += len(HEX.findall(x))
            continue
        complaint = compare_words(x, y, t)
        if complaint:
            if REPORT:
                t.shape += 1
                continue
            print(f"FAIL  line {i + 1}: {complaint}")
            return 1
    if REPORT:
        print(f"{frames_seen} frames, measured:")
    else:
        print(f"{frames_seen} frames; every uniform the plugin's programs hold and every table it uploaded within the limits:")
    width = max(len(k) for k in tallies)
    for tag, t in sorted(tallies.items()):
        print(f"   {tag:<{width}} {t.describe()}")
    print("   active uniforms: " + ", ".join(f"{p} {len(n)}" for p, n in sorted(seen.items())))
    for program, names in sorted(inactive.items()):
        print(f"   set by the port, not active in the plugin's {program} program: {', '.join(sorted(names))}")
    return 0


def main(argv):
    global REPORT
    REPORT = "--report" in argv
    argv = [a for a in argv if a != "--report"]
    if len(argv) >= 3 and argv[0] == "port":
        return port(argv[1], argv[2])
    if len(argv) >= 3 and argv[0] == "frames":
        return frames(argv[1], argv[2])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
