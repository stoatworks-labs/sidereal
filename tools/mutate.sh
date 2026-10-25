#!/usr/bin/env bash
#
# Mutation testing: change ONE character of the shipped code and require that a
# check fails. A check that still passes against a mutant was not looking at
# the code it claims to cover -- and a harness that compiled its own copy of
# the shaders would pass every GLSL mutant here.
#
# Each mutant is a copy of the tree in a temporary directory (the FFGL SDK is
# symlinked, not copied), built arm64-only, with only the named check run,
# under a time limit (a mutant can hang: honeydew's lesson; a timeout counts
# as caught).
#
#     tools/mutate.sh
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# file | the exact text | its one-character mutant | the check that must fail | what it is
# The fields are split on '|', so neither a target nor a mutant may contain one
# (conway's trap).
MUTANTS=(
	"source/Shaders.cpp|		tauD *= d3;|		tauD /= d3;|--photons|GLSL: the march's path not lengthened off the plane, * -> /"
	"source/Shaders.cpp|		a.y += F * hard * exp( -ko * tau );|		a.y += F * hard * exp( +ko * tau );|--strata|GLSL: [O III]'s photons growing with depth, - -> +"
	"source/Shaders.cpp|		a.z += F * max( exp( -ks * tau ) - exp( -tau ), 0.0 );|		a.z += F * max( exp( -ks * tau ) + exp( -tau ), 0.0 );|--strata|GLSL: the partly ionised zone summed, not differenced, - -> +"
	"source/Shaders.cpp|			float sig  = sqrt( sq( CoreSigma * sc ) + sq( SeeingSigma ) );|			float sig  = sqrt( sq( CoreSigma / sc ) + sq( SeeingSigma ) );|--psf|GLSL: the core shrinking with wavelength, * -> /"
	"source/Shaders.cpp|r0 * r0 / ( r0 * r0 + a * a );|r0 * r0 / ( r0 * r0 + a + a );|--spikes|GLSL: the spike's 1 / r^2 broken, * -> +"
	"source/Shaders.cpp|rho : sqrt( rho * rho + z * z ) - z ) / SnC.x;|rho : sqrt( rho * rho + z * z ) + z ) / SnC.x;|--echo|GLSL: the echo's delay with the sheet's distance added, - -> +"
	"source/Shaders.cpp|1.0 / max( depth, 0.05 ) ) - 1.0;|1.0 / max( depth, 0.05 ) ) + 1.0;|--parallax|GLSL: parallax offset by two depths, - -> +"
	"source/Shaders.cpp|		beta    = p - DeepA.w * th / max( r, 1e-5 );|		beta    = p - DeepA.w * th * max( r, 1e-5 );|--lens|GLSL: the deflection grows with radius (not a lens), / -> *"
	"source/Physics.cpp|a = 1.0 + y * ( 0.17699|a = 1.0 + y * ( 0.27699|--extinction|C++: CCM's optical a( x ), first coefficient, 1 -> 2"
	"source/Physics.cpp|		const double term = e * ( x0 * x0 / n + 2.0 * x0|		const double term = e * ( x0 * x0 / n + 3.0 * x0|--hardness|C++: the photon tail's second term, 2 -> 3"
	"source/Physics.cpp|return 1.0 / std::sqrt( 0.3 * std::pow( 1.0 + s, 3.0 ) + 0.7 ); }|return 1.0 / std::sqrt( 0.4 * std::pow( 1.0 + s, 3.0 ) + 0.7 ); }|--cosmology|C++: Omega_m 0.4 against Lambda 0.7, 3 -> 4"
	"source/Physics.cpp|		table.light[ static_cast< size_t >( i ) ] = 2.0 / tm2 * integral;|		table.light[ static_cast< size_t >( i ) ] = 3.0 / tm2 * integral;|--supernova|C++: Arnett's diffusion integral's prefactor, 2 -> 3"
	"source/Physics.cpp|	return 2.25 * x * r * r;|	return 2.25 * x * r + r;|--meteorcurve|C++: the classical meteor curve broken, * -> +"
	"source/Events.cpp|		m.dx            = static_cast< float >( ex / d );|		m.dx            = static_cast< float >( ey / d );|--meteorcurve|C++: a shower meteor's direction off its radiant line, x -> y"
	"source/Physics.cpp|		p.gain[ f ] = ( g_hooks & PH_GREY_PALETTE ) ? 1.0 / mean : 1.0 / reference[ f ];|		p.gain[ f ] = ( g_hooks & PH_GREY_PALETTE ) ? 1.0 / mean : 2.0 / reference[ f ];|--palette|C++: each filter stretched to twice its own light, 1 -> 2"
	"source/Physics.cpp|			value += 0.055 * std::exp( -0.5 * std::pow( ( l - 9.7 ) / 0.9, 2.0 ) );|			value += 0.055 * std::exp( -0.5 * std::pow( ( l - 8.7 ) / 0.9, 2.0 ) );|--extinction|C++: the silicate feature at 8.7 um, 9 -> 8"
)

limit() {
	# macOS has no timeout(1): perl's alarm is enough.
	perl -e 'alarm shift; exec @ARGV' "$@"
}

caught=0
for entry in "${MUTANTS[@]}"; do
	IFS='|' read -r file original mutant check what <<<"$entry"
	tree="$WORK/tree"
	rm -rf "$tree"
	mkdir -p "$tree/external"
	cp -R "$REPO/source" "$REPO/tools" "$REPO/cmake" "$REPO/CMakeLists.txt" "$tree/"
	ln -s "$REPO/external/ffgl" "$tree/external/ffgl"

	python3 - "$tree/$file" "$original" "$mutant" <<'PY'
import sys, pathlib
path, original, mutant = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
text = path.read_text()
if text.count(original) != 1:
    sys.exit(f"mutation target found {text.count(original)} times in {path}: '{original}'")
if sum(a != b for a, b in zip(original, mutant)) != 1 or len(original) != len(mutant):
    sys.exit("a mutant must differ by exactly one character")
path.write_text(text.replace(original, mutant))
PY

	printf '\n== mutant: %s\n' "$what"
	cmake -S "$tree" -B "$tree/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 >/dev/null
	cmake --build "$tree/build" --target sdtest -j8 >/dev/null 2>&1
	status=0
	limit 600 "$tree/build/sdtest" "$check" >"$WORK/log" 2>&1 || status=$?
	if [[ "$status" -eq 0 ]]; then
		printf '   FAIL  %s still PASSES -- the check does not cover this code\n' "$check"
	else
		printf '   ok    %s fails against the mutant (exit %d):\n' "$check" "$status"
		# A mutant that timed out printed no FAIL: grep finding none must not end the run.
		{ grep -E '^  FAIL' "$WORK/log" || true; } | head -2 | cut -c1-160 | sed 's/^/        /'
		caught=$(( caught + 1 ))
	fi
done

printf '\nmutants: %d, caught: %d\n' "${#MUTANTS[@]}" "$caught"
[[ "$caught" -eq "${#MUTANTS[@]}" ]]
