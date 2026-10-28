#!/usr/bin/env bash
#
# The browser demo's JavaScript port of the CPU half, against the plugin's C++.
#
#     demo/tools/crosscheck/crosscheck.sh        exit 1 on any difference past the limits
#
# Two checks, each a C++ program and a JavaScript one that print the same thing:
#
#   1. THE PORT. printer.cpp drives the plugin's own GL-free sources --
#      Controls.cpp, Physics.cpp, Scene.cpp, Events.cpp -- through a fixed
#      scenario: every slider position's mapping, every control's name, group,
#      kind, default and elements, Planck, the photon tail, extinction, every
#      filter's response to every line set and continuum, every palette (twelve
#      instruments by four target mixes, every table float), the cosmology, the
#      supernova's light curve, every target at ten seeds and the seven
#      generative ones at every third seed, and six runs of meteors and
#      supernovae. run.mjs runs the PORT block of demo/plugin.js through the same.
#   2. THE FRAMES. uniforms.cpp drives the REAL plugin class (Sidereal.cpp and
#      everything it calls, with the FFGL SDK) in a headless GL 4.1 context
#      through scenario.mjs's frames of both plugins, records every table it
#      uploads at the glTexImage2D call (Sidereal.cpp is compiled with that one
#      function renamed to a recorder), and after every frame reads every active
#      uniform back out of its four programs. frames.mjs runs the FRAME block
#      (ProcessOpenGL's CPU half) through the same frames.
#
# Floats and doubles print as their bits, and compare.py measures them in units
# in the last place against limits it explains (V8's Math is not Apple's libm,
# and neither promises correct rounding). It reports how many lines are
# identical to the bit, and fails on anything past a libm's reach.
#
# THE FUSED MULTIPLY-ADD. Apple clang's default -ffp-contract=on fuses a*b+c
# on arm64, which JavaScript cannot do, so the plugin's Apple Silicon slice
# rounds differently from its x86_64 slice (whose baseline has no FMA) and from
# this port. The checks hold the port to a -ffp-contract=off build, the x86_64
# slice's arithmetic; the native build is compared too, and reported.
#
# Run by tools/verify.sh. Needs node, a C++ compiler, the FFGL submodule and an
# OpenGL context (check 2 drives the plugin's shaders for real).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
CXX="${CXX:-c++}"

command -v node >/dev/null 2>&1 || { echo "crosscheck: node is not installed"; exit 2; }
[[ -f "$REPO/external/ffgl/CMakeLists.txt" ]] || { echo "crosscheck: FFGL SDK missing -- git submodule update --init --recursive"; exit 2; }

# CROSSCHECK_KEEP=dir keeps the builds and the printouts there, to read a failure.
if [[ -n "${CROSSCHECK_KEEP:-}" ]]; then
	tmp="$CROSSCHECK_KEEP"
	mkdir -p "$tmp"
else
	tmp="$(mktemp -d)"
	trap 'rm -rf "$tmp"' EXIT
fi

version="$( sed -n 's/^[[:space:]]*VERSION \([0-9.]*\)$/\1/p' "$REPO/CMakeLists.txt" | head -1 )"
port_sources=( "$REPO/source/Controls.cpp" "$REPO/source/Physics.cpp" "$REPO/source/Scene.cpp" "$REPO/source/Events.cpp" )
plugin_sources=( "$REPO/source/Diag.cpp" "${port_sources[@]}" "$REPO/source/PassBuffer.cpp" "$REPO/source/Shaders.cpp"
                 "$REPO/external/ffgl/source/lib/FFGLSDK.cpp" )
plugin_flags=( -std=c++17 -O2 -DGL_SILENCE_DEPRECATION -DSIDEREAL_VERSION="\"$version\"" -I "$REPO/source"
               -I "$REPO/external/ffgl/source/lib" -w )

build() {
	local mode="$1"; shift
	local contract=()
	[[ "$mode" == strict ]] && contract=( -ffp-contract=off )
	"$CXX" -std=c++17 -O2 ${contract[@]+"${contract[@]}"} -I "$REPO/source" "$HERE/printer.cpp" "${port_sources[@]}" -o "$tmp/printer-$mode"
	# Sidereal.cpp alone has its glTexImage2D calls (uploadTable's) renamed to
	# the recorder in uniforms.cpp; PassBuffer.cpp's allocations are not tables.
	"$CXX" ${contract[@]+"${contract[@]}"} "${plugin_flags[@]}" -DglTexImage2D=recordTexImage2D -c "$REPO/source/Sidereal.cpp" -o "$tmp/sidereal-$mode.o"
	"$CXX" ${contract[@]+"${contract[@]}"} "${plugin_flags[@]}" "$HERE/uniforms.cpp" "$tmp/sidereal-$mode.o" "${plugin_sources[@]}" \
		-framework OpenGL -framework AppKit -o "$tmp/uniforms-$mode"
}
build strict
build native

#---------------------------------------------------------------------------
# 1. The port.
#---------------------------------------------------------------------------
"$tmp/printer-strict" > "$tmp/port-cpp.txt"
"$tmp/printer-native" > "$tmp/port-cpp-native.txt"
node "$HERE/run.mjs" > "$tmp/port-js.txt"

lines=$( wc -l < "$tmp/port-js.txt" | tr -d ' ' )
[[ "$lines" -gt 100000 ]] || { echo "FAIL the JavaScript side printed only $lines lines"; exit 1; }
if ! out=$( python3 "$HERE/compare.py" port "$tmp/port-cpp.txt" "$tmp/port-js.txt" ); then
	echo "FAIL the JavaScript port differs from the plugin's Controls/Physics/Scene/Events.cpp (contraction off):"
	printf '%s\n' "$out" | sed 's/^/      /'
	exit 1
fi
echo "ok   the port against the plugin's C++ (contraction off, the x86_64 slice's arithmetic):"
printf '%s\n' "$out" | sed 's/^/   /'
echo "     against the plugin as built for arm64 (fused multiply-adds), reported, not required:"
python3 "$HERE/compare.py" port "$tmp/port-cpp-native.txt" "$tmp/port-js.txt" --report | grep -v ": identical$" | sed "s/^/   /"

#---------------------------------------------------------------------------
# 2. The frames.
#---------------------------------------------------------------------------
node "$HERE/scenario.mjs" > "$tmp/scenario.txt"
"$tmp/uniforms-strict" < "$tmp/scenario.txt" > "$tmp/frames-cpp.txt"
"$tmp/uniforms-native" < "$tmp/scenario.txt" > "$tmp/frames-cpp-native.txt"
node "$HERE/frames.mjs" < "$tmp/scenario.txt" > "$tmp/frames-js.txt"

if ! out=$( python3 "$HERE/compare.py" frames "$tmp/frames-cpp.txt" "$tmp/frames-js.txt" ); then
	echo "FAIL the JavaScript frame logic differs from the real plugin's (contraction off):"
	printf '%s\n' "$out" | sed 's/^/      /'
	exit 1
fi
echo "ok   the frames, the real plugin class against the FRAME block (contraction off):"
printf '%s\n' "$out" | sed 's/^/   /'
echo "     against the plugin as built for arm64, reported, not required:"
python3 "$HERE/compare.py" frames "$tmp/frames-cpp-native.txt" "$tmp/frames-js.txt" --report | grep -v ": identical$" | sed "s/^/   /"
