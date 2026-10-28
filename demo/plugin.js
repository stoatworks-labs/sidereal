/**
 * Sidereal — browser demo.
 *
 * The sky as the space telescopes photograph it. The plugin builds a picture
 * the way Hubble's and Webb's releases are built: it computes what each filter
 * would record from gas lit by its stars (every ionising photon absorbed is a
 * recombination; [O III] nearest the star, then hydrogen, then [S II], then the
 * aromatics), then gives each filter's grey image the release's own hue and
 * stretches the sum. SW Sidereal (SD01) is a piece of sky on black; SW Sidereal
 * Over (SD02) puts it over the clip, or makes the clip's brightness the gas.
 *
 * Two halves, and they are not equally faithful here:
 *
 *   **The GPU half is the plugin's own GLSL.** `shaders.js` is the nine
 *   `R"( ... )"` pieces of `source/Shaders.cpp` plus kVersion, spliced by
 *   `demo/tools/check_shaders.py --write` and never typed; the same script
 *   compares them character for character and checks that they are joined as
 *   `Assemble()` and InitGL join them, and `tools/verify.sh` runs it. They run
 *   as the plugin's four passes -- the field (two half-float targets, half the
 *   raster plus a margin, the first mipmapped), the light (four half-float
 *   targets), the sprites (additive, half float, the host's raster) and the
 *   composite into the canvas -- reading the same tables the plugin uploads.
 *
 *   **The CPU half is a port.** The plugin builds everything a shader reads on
 *   the CPU, in double: the telescope's tables (Physics.cpp: every filter's
 *   response to each line set and each continuum, integrated by Simpson's rule,
 *   the gains, the colour keys, extinction through each filter), every target
 *   (Scene.cpp: the outlines traced from the releases, the stars, the galaxies,
 *   and the seeded generator behind the generative ones), the meteors and the
 *   supernova (Events.cpp, Physics.cpp's light curve), and the frame's camera,
 *   sources, sprites and uniforms (Sidereal.cpp). Here that is JavaScript,
 *   whose numbers are doubles too. Between the PORT markers below:
 *   Physics.cpp, Controls.cpp, Scene.cpp and Events.cpp. Between the FRAME
 *   markers: the CPU half of Sidereal.cpp's ProcessOpenGL -- the clock, the
 *   scene's and the palette's textures, the camera, the events, the sprites and
 *   every uniform of every pass -- as a class whose `frame()` RETURNS the
 *   uploads and the uniforms, so the GL code below only binds and draws. Both
 *   blocks are held to the plugin by demo/tools/crosscheck/ (verify.sh runs
 *   it): printer.cpp runs a scenario on the plugin's own Physics, Controls,
 *   Scene and Events.cpp, and uniforms.cpp drives the REAL plugin class in a GL
 *   context and records every table it uploads and every active uniform of its
 *   four programs after every frame, against what the FRAME block returns.
 *
 * ---------------------------------------------------------------------------
 * Decisions this page made, and why
 * ---------------------------------------------------------------------------
 *
 * **Both plugins, one page.** One class with a flag in the plugin; the Over
 * declares two more controls (Clip As, Mix) in the Output group, and both
 * constructors set the same defaults. The Plugin dropdown is a new instance,
 * as it would be in Resolume: every control goes back to its default and any
 * meteor or supernova in flight is gone. `?plugin=over` opens the effect; Copy
 * link carries it.
 *
 * **Nothing audio.** The plugin has no audio input (Resolume can map Fire
 * Meteor and Supernova to its own audio or BPM), so nothing is left out.
 *
 * **Events.** Fire Meteor and Supernova are FF_TYPE_EVENT: toggles the page
 * reads on the rising edge, as the plugin does, then puts back to 0, which is
 * what a host does with an event. The transport's two buttons press them.
 *
 * **The clock.** The kit's: seconds accumulated from frame deltas, capped at a
 * tenth of a second a frame, handed to the port as the host's time already in
 * seconds, so the plugin's seconds-or-milliseconds vote is not ported. The
 * plugin's own rule then applies: a step backwards is no time passing, and no
 * frame advances more than a tenth of a second. Paused, nothing flows or drifts.
 *
 * **The tables are built once per telescope and target mix.** The plugin
 * rebuilds its palette when the instrument or the target changes, and builds a
 * second one (Hubble Narrow) each time to normalise the stars' brightness;
 * BuildPalette is a pure function of the instrument and the mix, so the port
 * keeps what it has built and hands back the same tables. The angular diameter
 * distances the composite reads (33 Simpson integrals the plugin does every
 * frame) are likewise computed once.
 *
 * **The harness's test hooks are not ported** (the raw stages, no shadow, one
 * zone, the grey PSF, the wrong spike law, the wrong parallax and the rest):
 * they exist so `sdtest --negative` has wrong models to catch, and the plugin
 * never sets them in a host. `Hooks` is sent as 0, as the plugin sends it.
 *
 * **The About block is not a parameter here**, as on every page in the suite.
 */

import { mountDemo } from './vendor/demo.js';
import { Program, PassBuffer, GLError, bindTexture } from './vendor/gl.js';
import * as S from './shaders.js';

// ==== BEGIN PORT ===========================================================
// Physics.h/Physics.cpp, Controls.h/Controls.cpp, Scene.h/Scene.cpp and
// Events.h/Events.cpp, ported. No DOM and no GL between the two markers:
// demo/tools/crosscheck/run.mjs slices this block out by its markers, runs it
// under node and diffs what it prints against the same printout from the
// plugin's own C++ (printer.cpp).
//
// C++ float arithmetic is written f32( ... ) round each operation: a float
// operation is the double one rounded once, which for + - * / and sqrt is
// exactly the float result. A float function the plugin calls (cosf, powf,
// logf and the rest, which is what std::cos of a float is) is the double one
// rounded, which is what a correctly rounded libm returns.

const f32 = Math.fround;
/** std::clamp, std::min and std::max exactly: comparisons, so -0 stays -0. */
const clamp = (v, lo, hi) => (v < lo ? lo : hi < v ? hi : v);
const min2 = (a, b) => (b < a ? b : a);
const max2 = (a, b) => (a < b ? b : a);
/** std::lround: half away from zero (Math.round alone rounds -2.5 to -2). */
const lround = (v) => (v < 0 ? -Math.round(-v) : Math.round(v));
const kPi = 3.14159265358979323846;
const cosf = (x) => f32(Math.cos(x));
const sinf = (x) => f32(Math.sin(x));
const tanf = (x) => f32(Math.tan(x));
const powf = (x, y) => f32(Math.pow(x, y));
const logf = (x) => f32(Math.log(x));
const sqrtf = (x) => f32(Math.sqrt(x));

//---------------------------------------------------------------------------
// The two libm functions JavaScript lacks, from fdlibm (Sun Microsystems,
// 1993; "Permission to use, copy, modify, and distribute this software is
// freely granted, provided that this notice is preserved"). erfc is s_erf.c's,
// line for line. tgamma is only ever asked for 2 n of a Sersic index, which
// every target holds at 4.0 or 2.2: Lanczos's series (g = 7, nine terms) for
// those, exact for the integers.
//---------------------------------------------------------------------------
const erfcView = new DataView(new ArrayBuffer(8));
function erfc(x) {
  erfcView.setFloat64(0, x);
  const hx = erfcView.getInt32(0);
  const ix = hx & 0x7fffffff;
  const one = 1.0;
  const half = 0.5;
  const two = 2.0;
  const tiny = 1e-300;
  const erx = 8.45062911510467529297e-01;
  if (ix >= 0x7ff00000) return ((hx >>> 31) << 1) + one / x;
  if (ix < 0x3feb0000) {
    if (ix < 0x3c700000) return one - x;
    const z = x * x;
    const r = 1.28379167095512558561e-01 + z * (-3.25042107247001499370e-01 + z * (-2.84817495755985104766e-02 + z * (-5.77027029648944159157e-03 + z * -2.37630166566501626084e-05)));
    const s = one + z * (3.97917223959155352819e-01 + z * (6.50222499887672944485e-02 + z * (5.08130628187576562776e-03 + z * (1.32494738004321644526e-04 + z * -3.96022827877536812320e-06))));
    const y = r / s;
    if (hx < 0x3fd00000) return one - (x + x * y);
    let rr = x * y;
    rr += x - half;
    return half - rr;
  }
  if (ix < 0x3ff40000) {
    const s = Math.abs(x) - one;
    const P = -2.36211856075265944077e-03 + s * (4.14856118683748331666e-01 + s * (-3.72207876035701323847e-01 + s * (3.18346619901161753674e-01 + s * (-1.10894694282396677476e-01 + s * (3.54783043256182359371e-02 + s * -2.16637559486879084300e-03)))));
    const Q = one + s * (1.06420880400844228286e-01 + s * (5.40397917702171048937e-01 + s * (7.18286544141962662868e-02 + s * (1.26171219808761642112e-01 + s * (1.36370839120290507362e-02 + s * 1.19844998467991074170e-02)))));
    if (hx >= 0) return one - erx - P / Q;
    return one + (erx + P / Q);
  }
  if (ix < 0x403c0000) {
    const ax = Math.abs(x);
    const s = one / (ax * ax);
    let R;
    let Ss;
    if (ix < 0x4006db6d) {
      R = -9.86494403484714822705e-03 + s * (-6.93858572707181764372e-01 + s * (-1.05586262253232909814e+01 + s * (-6.23753324503260060396e+01 + s * (-1.62396669462573470355e+02 + s * (-1.84605092906711035994e+02 + s * (-8.12874355063065934246e+01 + s * -9.81432934416914548592e+00))))));
      Ss = one + s * (1.96512716674392571292e+01 + s * (1.37657754143519042600e+02 + s * (4.34565877475229228821e+02 + s * (6.45387271733267880336e+02 + s * (4.29008140027567833386e+02 + s * (1.08635005541779435134e+02 + s * (6.57024977031928170135e+00 + s * -6.04244152148580987438e-02)))))));
    } else {
      if (hx < 0 && ix >= 0x40180000) return two - tiny;
      R = -9.86494292470009928597e-03 + s * (-7.99283237680523006574e-01 + s * (-1.77579549177547519889e+01 + s * (-1.60636384855821916062e+02 + s * (-6.37566443368389627722e+02 + s * (-1.02509513161107724954e+03 + s * -4.83519191608651397019e+02)))));
      Ss = one + s * (3.03380607434824582924e+01 + s * (3.25792512996573918826e+02 + s * (1.53672958608443695994e+03 + s * (3.19985821950859553908e+03 + s * (2.55305040643316442583e+03 + s * (4.74528541206955367215e+02 + s * -2.24409524465858183362e+01))))));
    }
    erfcView.setFloat64(0, ax);
    erfcView.setUint32(4, 0);
    const z = erfcView.getFloat64(0);
    const r = Math.exp(-z * z - 0.5625) * Math.exp((z - ax) * (z + ax) + R / Ss);
    return hx > 0 ? r / ax : two - r / ax;
  }
  return hx > 0 ? tiny * tiny : two - tiny;
}

const LANCZOS = [0.99999999999980993, 676.5203681218851, -1259.1392167224028, 771.32342877765313, -176.61502916214059,
  12.507343278686905, -0.13857109526572012, 9.9843695780195716e-6, 1.5056327351493116e-7];
function tgamma(x) {
  if (Number.isInteger(x) && x > 0 && x < 171) {
    let r = 1;
    for (let k = 2; k < x; k += 1) r *= k;
    return r;
  }
  const z = x - 1;
  let a = LANCZOS[0];
  const t = z + 7.5;
  for (let k = 1; k < 9; k += 1) a += LANCZOS[k] / (z + k);
  return Math.sqrt(2 * kPi) * Math.pow(t, z + 0.5) * Math.exp(-t) * a;
}

//===========================================================================
// Physics.h / Physics.cpp
//===========================================================================

/** 2 h c^2 with lambda in um: W um^4 m^-2 sr^-1. */
const kC1 = 1.191042972e8;
/** h c / k, um K. */
const kC2 = 14387.7688;
/** k in eV / K. */
const kBoltzmannEv = 8.617333262e-5;
/** Stefan-Boltzmann over pi in the units B_lambda integrates to. */
const kSigmaOverPi = 5.670374419e-8 / kPi;

/** The fraction of a Gaussian line inside [ lo, hi ]. */
function gaussianInside(centre, sigma, lo, hi) {
  const s = sigma * Math.sqrt(2.0);
  return 0.5 * (erfc((lo - centre) / s) - erfc((hi - centre) / s));
}

function simpson(f, a, b, panels) {
  if (panels % 2) panels += 1;
  const h = (b - a) / panels;
  let sum = f(a) + f(b);
  for (let i = 1; i < panels; i += 1) sum += f(a + i * h) * (i % 2 ? 4.0 : 2.0);
  return (sum * h) / 3.0;
}

// The components' lines: [ lambda um, strength, sigma (0 a line) ].
const C = {
  OXYGEN: 0, HYDROGEN: 1, SULPHUR: 2, SHOCK: 3, MOLECULAR: 4, METEOR_SLOW: 5, METEOR_FAST: 6, TRAIN: 7, PAH: 8, SYNCHROTRON: 9,
};
const kLineComponents = 10;
const kOxygen = [[0.50068, 1.0, 0], [0.49589, 0.335, 0], [0.46857, 0.10, 0], [0.38688, 0.25, 0]];
const kHydrogen = [
  [0.48613, 1.0, 0], [0.65628, 2.86, 0], [0.43405, 0.468, 0], [0.41017, 0.259, 0],
  [0.65835, 0.90, 0], [0.65480, 0.30, 0], [0.95311, 0.60, 0], [0.90686, 0.25, 0],
  [1.08300, 0.25, 0], [1.09381, 0.090, 0], [1.28181, 0.162, 0], [1.87510, 0.332, 0],
  [2.16553, 0.0275, 0], [2.62515, 0.045, 0], [4.05122, 0.078, 0], [3.73951, 0.026, 0],
  [6.98527, 0.10, 0], [12.8135, 0.40, 0], [15.5551, 0.15, 0],
  [1.73669, 0.0117, 0], [1.68111, 0.0095, 0], [1.64117, 0.0076, 0], [1.61137, 0.0062, 0],
];
const kSulphur = [[0.67164, 1.0, 0], [0.67308, 0.75, 0], [0.63003, 0.45, 0], [0.63639, 0.15, 0], [1.64400, 0.05, 0]];
const kShock = [
  [0.65628, 1.0, 0], [0.48613, 0.32, 0], [0.65835, 1.10, 0], [0.65480, 0.37, 0], [0.67164, 0.35, 0],
  [0.67308, 0.27, 0], [0.63003, 0.35, 0], [0.50068, 0.60, 0], [0.49589, 0.20, 0], [0.95311, 0.30, 0],
  [1.25668, 0.12, 0], [1.64400, 0.10, 0], [1.28181, 0.06, 0], [1.87510, 0.11, 0], [4.05122, 0.03, 0],
  [5.34017, 0.05, 0], [6.98527, 0.10, 0], [12.8135, 0.30, 0], [17.9359, 0.10, 0], [18.7130, 0.20, 0],
];
const kMolecular = [
  [2.12183, 1.0, 0], [4.69468, 0.70, 0], [1.95756, 0.80, 0], [2.03376, 0.40, 0], [4.18096, 0.30, 0],
  [17.0348, 0.20, 0], [12.2786, 0.20, 0], [9.66491, 0.30, 0], [0.67164, 0.25, 0], [0.67308, 0.20, 0],
  [0.65628, 0.15, 0], [1.64400, 0.40, 0],
];
const kMeteorSlow = [
  [0.60, 0.4, 0.12], [0.58900, 1.0, 0], [0.58959, 0.50, 0], [0.43838, 0.35, 0], [0.52704, 0.30, 0],
  [0.44047, 0.20, 0], [0.51837, 0.20, 0], [0.51727, 0.15, 0], [0.42267, 0.10, 0],
];
const kMeteorFast = [
  [0.55, 0.4, 0.12], [0.51837, 1.0, 0], [0.51727, 0.60, 0], [0.51674, 0.30, 0], [0.58900, 0.40, 0], [0.39337, 0.50, 0],
  [0.39685, 0.40, 0], [0.77740, 0.80, 0], [0.86800, 0.30, 0], [0.43838, 0.20, 0],
];
const kTrain = [[0.55774, 1.0, 0], [0.58900, 0.30, 0], [0.58959, 0.15, 0]];
const kPah = [
  [3.29, 0.08, 0.017], [3.40, 0.03, 0.10], [6.22, 0.35, 0.08], [7.60, 0.60, 0.20], [7.85, 0.40, 0.20],
  [8.61, 0.30, 0.10], [11.23, 0.45, 0.06], [11.33, 0.25, 0.10], [12.70, 0.25, 0.15],
  [4.40, 0.10, 0.60],
];
const SPECS = [kOxygen, kHydrogen, kSulphur, kShock, kMolecular, kMeteorSlow, kMeteorFast, kTrain, kPah];
const specOf = (c) => SPECS[c] ?? [];

// The filters, from STScI's instrument handbooks: pivot and full width, um.
const FILTERS = [
  ['F502N', 0.5010, 0.0065], ['F657N', 0.6567, 0.0121], ['F673N', 0.6766, 0.0118],
  ['F435W', 0.4329, 0.0870], ['F555W', 0.5361, 0.1000], ['F606W', 0.5922, 0.1600], ['F775W', 0.7693, 0.1400],
  ['F814W', 0.8046, 0.1500], ['F850LP', 0.9055, 0.1300], ['F658N', 0.6584, 0.0075],
  ['F110W', 1.1534, 0.4430], ['F160W', 1.5369, 0.2683],
  ['F090W', 0.9022, 0.1940], ['F150W', 1.5007, 0.3180], ['F162M', 1.6272, 0.1680], ['F187N', 1.8738, 0.0240],
  ['F200W', 1.9886, 0.4610], ['F212N', 2.1210, 0.0270], ['F277W', 2.7617, 0.6720], ['F300M', 2.9891, 0.3180],
  ['F335M', 3.3621, 0.3470], ['F356W', 3.5684, 0.7870], ['F405N', 4.0523, 0.0450], ['F444W', 4.4393, 1.0240],
  ['F470N', 4.7083, 0.0510],
  ['F770W', 7.639, 2.20], ['F1130W', 11.309, 0.73], ['F1500W', 15.065, 2.92],
  ['OSC-B', 0.465, 0.090], ['OSC-G', 0.530, 0.090], ['OSC-R', 0.620, 0.110],
].map(([name, pivot, width]) => ({ name, pivot, width }));
function FilterNamed(name) {
  return FILTERS.find((f) => f.name === name) ?? FILTERS[0];
}

// The hues NASA's colour keys name, as RGB (floats in the plugin).
const HUE = {
  PURPLE: [0.55, 0.15, 1.00], BLUE: [0.05, 0.25, 1.00], LIGHTBLUE: [0.35, 0.65, 1.00], CYAN: [0.00, 0.85, 1.00],
  BLUEGREEN: [0.00, 0.90, 0.60], GREEN: [0.15, 1.00, 0.10], YELLOW: [1.00, 0.90, 0.00], ORANGE: [1.00, 0.50, 0.00],
  RED: [1.00, 0.08, 0.02],
};
const HUE_NAMES = Object.fromEntries(Object.entries(HUE).map(([k, v]) => [v.join(), k.toLowerCase()]));
const Spikes = { None: 0, Four: 1, Webb: 2 };
const I = {
  HUBBLE_NARROW: 0, HUBBLE_COLOUR: 1, HUBBLE_INFRARED: 2, WEBB_NIRCAM: 3, WEBB_MIRI: 4, WEBB_DEEP: 5, BACKYARD: 6,
  CLIFFS: 7, SOUTHERN_RING: 8, RING_NIRCAM: 9, HUBBLE_DEEP: 10, HELIX: 11,
};
const kTelescopeChoices = 7;
const kInstruments = 12;
/** name, filters, hues, aperture m, spikes, spike angle deg, seeing, dust glow, white balance, exposure stops. */
const instrument = (name, filters, colours, aperture, spikes, spikeAngle, seeing, dustGlow, whiteBalance, exposure = 0.0) => ({
  name, filterCount: filters.length, filters, colour: colours.map((c) => c.map(f32)), aperture, spikes, spikeAngle, seeing, dustGlow, whiteBalance, exposure,
});
const INSTRUMENTS = [
  instrument('Hubble Narrow', ['F502N', 'F657N', 'F673N'], [HUE.BLUE, HUE.GREEN, HUE.RED], 2.4, Spikes.Four, 45.0, 0.0, 0.0, 0.0),
  instrument('Hubble Colour', ['F435W', 'F555W', 'F814W', 'F658N'], [HUE.BLUE, HUE.GREEN, HUE.RED, HUE.RED], 2.4, Spikes.Four, 45.0, 0.0, 0.0, 1.0),
  instrument('Hubble Infrared', ['F110W', 'F160W'], [HUE.BLUE, HUE.YELLOW], 2.4, Spikes.Four, 45.0, 0.0, 0.0, 1.0, 1.5),
  instrument('Webb NIRCam', ['F090W', 'F187N', 'F200W', 'F335M', 'F444W', 'F470N'], [HUE.PURPLE, HUE.BLUE, HUE.CYAN, HUE.YELLOW, HUE.ORANGE, HUE.RED], 6.5, Spikes.Webb, 0.0, 0.0, 1.0, 0.7),
  instrument('Webb MIRI', ['F770W', 'F1130W', 'F1500W'], [HUE.BLUE, HUE.GREEN, HUE.RED], 6.5, Spikes.Webb, 0.0, 0.0, 1.0, 0.0),
  instrument('Webb Deep', ['F090W', 'F150W', 'F200W', 'F277W', 'F356W', 'F444W'], [HUE.BLUE, HUE.BLUE, HUE.GREEN, HUE.GREEN, HUE.ORANGE, HUE.RED], 6.5, Spikes.Webb, 0.0, 0.0, 1.0, 1.0),
  instrument('Backyard', ['OSC-B', 'OSC-G', 'OSC-R'], [HUE.BLUE, HUE.GREEN, HUE.RED], 2.0, Spikes.None, 0.0, 2.6, 0.0, 1.0),
  instrument('Cosmic Cliffs', ['F090W', 'F187N', 'F200W', 'F470N', 'F335M', 'F444W'], [HUE.BLUE, HUE.BLUEGREEN, HUE.GREEN, HUE.YELLOW, HUE.ORANGE, HUE.RED], 6.5, Spikes.Webb, 0.0, 0.0, 1.0, 0.7),
  instrument('Southern Ring', ['F090W', 'F187N', 'F212N', 'F356W', 'F405N', 'F470N'], [HUE.BLUE, HUE.CYAN, HUE.GREEN, HUE.YELLOW, HUE.RED, HUE.RED], 6.5, Spikes.Webb, 0.0, 0.0, 1.0, 0.7),
  instrument('Ring NIRCam', ['F162M', 'F212N', 'F300M', 'F335M'], [HUE.BLUE, HUE.CYAN, HUE.GREEN, HUE.RED], 6.5, Spikes.Webb, 0.0, 0.0, 1.0, 0.5),
  instrument('Hubble Deep', ['F435W', 'F606W', 'F775W', 'F850LP'], [HUE.BLUE, HUE.GREEN, HUE.ORANGE, HUE.RED], 2.4, Spikes.Four, 45.0, 0.0, 0.0, 1.0),
  instrument('Hubble HOO', ['F502N', 'F658N'], [HUE.CYAN, HUE.RED], 2.4, Spikes.Four, 45.0, 0.0, 0.0, 0.0),
];
const InstrumentOf = (id) => INSTRUMENTS[clamp(id, 0, kInstruments - 1)];

function Planck(lambdaUm, kelvin) {
  if (lambdaUm <= 0.0 || kelvin <= 0.0) return 0.0;
  const x = kC2 / (lambdaUm * kelvin);
  if (x > 700.0) return 0.0;
  return kC1 / (Math.pow(lambdaUm, 5.0) * Math.expm1(x));
}

const ReducedEnergy = (electronVolts, kelvin) => electronVolts / (kBoltzmannEv * kelvin);

function PhotonTail(x0) {
  const terms = 200;
  let sum = 0.0;
  for (let n = 1; n <= terms; n += 1) {
    const e = Math.exp(-n * x0);
    const term = e * ((x0 * x0) / n + (2.0 * x0) / (n * n) + 2.0 / (n * n * n));
    sum += term;
    if (term < 1e-17 * sum) break;
  }
  return sum;
}

function OxygenHardness(kelvin) {
  const hydrogen = PhotonTail(ReducedEnergy(13.598, kelvin));
  const oxygen = PhotonTail(ReducedEnergy(35.121, kelvin));
  return hydrogen > 0.0 ? oxygen / hydrogen : 0.0;
}

function Extinction(lambdaUm, rv = 3.1) {
  const x = 1.0 / max2(lambdaUm, 0.125);
  let a = 0.0;
  let b = 0.0;
  if (x < 1.1) {
    const p = Math.pow(x, 1.61);
    a = 0.574 * p;
    b = -0.527 * p;
    let value = a + b / rv;
    if (x < 0.3) {
      const l = lambdaUm;
      value += 0.025 * (1.0 - Math.exp(-(l - 1.0 / 0.3) / 1.2));
      value += 0.055 * Math.exp(-0.5 * Math.pow((l - 9.7) / 0.9, 2.0));
      value += 0.022 * Math.exp(-0.5 * Math.pow((l - 18.0) / 2.5, 2.0));
    }
    return value;
  }
  if (x < 3.3) {
    const y = x - 1.82;
    a = 1.0 + y * (0.17699 + y * (-0.50447 + y * (-0.02427 + y * (0.72085 + y * (0.01979 + y * (-0.77530 + y * 0.32999))))));
    b = y * (1.41338 + y * (2.28305 + y * (1.07233 + y * (-5.38434 + y * (-0.62251 + y * (5.30260 + y * -2.09002))))));
    return a + b / rv;
  }
  let fa = 0.0;
  let fb = 0.0;
  if (x >= 5.9) {
    const d = x - 5.9;
    fa = -0.04473 * d * d - 0.009779 * d * d * d;
    fb = 0.2130 * d * d + 0.1207 * d * d * d;
  }
  a = 1.752 - 0.316 * x - 0.104 / ((x - 4.67) * (x - 4.67) + 0.341) + fa;
  b = -3.090 + 1.825 * x + 1.206 / ((x - 4.62) * (x - 4.62) + 0.263) + fb;
  return a + b / rv;
}

function lineResponseOf(f, c) {
  const lo = f.pivot - 0.5 * f.width;
  const hi = f.pivot + 0.5 * f.width;
  if (c === C.SYNCHROTRON) {
    const p = -0.2;
    return (Math.pow(hi, p) - Math.pow(lo, p)) / p;
  }
  let sum = 0.0;
  for (const [lambda, strength, sigma] of specOf(c)) {
    if (sigma > 0.0) sum += strength * gaussianInside(lambda, sigma, lo, hi);
    else if (lambda >= lo && lambda <= hi) sum += strength;
  }
  return sum;
}

function continuumOf(f, kind, kelvin) {
  const lo = f.pivot - 0.5 * f.width;
  const hi = f.pivot + 0.5 * f.width;
  if (kind === 2) {
    const norm = kC1 * Math.pow(kelvin / kC2, 6.0) * 120.0 * 1.0173430620;
    return simpson((l) => Planck(l, kelvin) / (l * l), lo, hi, 64) / norm;
  }
  const norm = kSigmaOverPi * Math.pow(kelvin, 4.0);
  return simpson((l) => {
    const b = Planck(l, kelvin);
    return kind === 1 ? b * (0.55 / l) : b;
  }, lo, hi, 64) / norm;
}

function mixFilters(p, signal, tauV) {
  const out = [0.0, 0.0, 0.0];
  for (let f = 0; f < p.filterCount; f += 1) {
    const s = p.gain[f] * signal[f] * Math.exp(-tauV * p.ext[f]);
    for (let c = 0; c < 3; c += 1) out[c] += s * p.colour[f][c];
  }
  return out;
}

function GalaxyResponse(f, kelvin, z) {
  const lo = f.pivot - 0.5 * f.width;
  const hi = f.pivot + 0.5 * f.width;
  const s = 1.0 + z;
  const forest = Math.exp(-0.0036 * Math.pow(s, 3.46));
  const norm = kSigmaOverPi * Math.pow(kelvin, 4.0);
  const flux = simpson((l) => {
    const rest = l / s;
    if (rest < 0.0912) return 0.0;
    const b = Planck(rest, kelvin);
    return rest < 0.1216 ? b * forest : b;
  }, lo, hi, 32);
  return flux / norm / Math.pow(s, 5.0);
}

const kTauSamples = 48;
const kTauMax = 12.0;
const kTempSamples = 64;
const kStarTempMin = 2500.0;
const kStarTempMax = 50000.0;
const kDustTempMin = 15.0;
const kDustTempMax = 600.0;

function TempOfSample(kind, index) {
  const lo = kind === 2 ? kDustTempMin : kStarTempMin;
  const hi = kind === 2 ? kDustTempMax : kStarTempMax;
  return lo * Math.pow(hi / lo, index / (kTempSamples - 1));
}
function SampleOfTemp(kind, kelvin) {
  const lo = kind === 2 ? kDustTempMin : kStarTempMin;
  const hi = kind === 2 ? kDustTempMax : kStarTempMax;
  return (Math.log(clamp(kelvin, lo, hi) / lo) / Math.log(hi / lo)) * (kTempSamples - 1);
}

function DisplayOf(p, component, tauV) {
  const signal = new Array(8).fill(0.0);
  const inst = InstrumentOf(p.instrument);
  for (let f = 0; f < p.filterCount; f += 1) signal[f] = lineResponseOf(FilterNamed(inst.filters[f]), component);
  return mixFilters(p, signal, tauV);
}
function DisplayOfContinuum(p, kind, kelvin, tauV) {
  const signal = new Array(8).fill(0.0);
  const inst = InstrumentOf(p.instrument);
  for (let f = 0; f < p.filterCount; f += 1) signal[f] = continuumOf(FilterNamed(inst.filters[f]), kind, kelvin);
  return mixFilters(p, signal, tauV);
}

//      O     H     S     shock mol   slow  fast  train PAH   sync
const NebulaMix = () => ({ line: [0.6, 1.0, 0.35, 0.0, 0.02, 0.0, 0.0, 0.0, 0.4, 0.0], star: 40.0, starTemp: 7000.0, dust: 4.0, dustTemp: 90.0 });
const PlanetaryMix = () => ({ line: [2.0, 1.0, 0.15, 0.0, 0.3, 0.0, 0.0, 0.0, 0.2, 0.0], star: 3.0, starTemp: 7000.0, dust: 1.0, dustTemp: 120.0 });
const RemnantMix = () => ({ line: [0.4, 0.2, 0.05, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 2.0], star: 30.0, starTemp: 7000.0, dust: 1.0, dustTemp: 90.0 });
const StarlightMix = () => ({ line: [0.05, 0.2, 0.05, 0.0, 0.0, 0.0, 0.0, 0.0, 0.05, 0.0], star: 400.0, starTemp: 6000.0, dust: 1.0, dustTemp: 60.0 });

/** BuildPalette is a pure function of the instrument and the mix: what has been built is kept. */
const paletteMemo = new Map();
function BuildPalette(id, mix = NebulaMix()) {
  const key = `${id}|${mix.line.join(',')}|${mix.star}|${mix.starTemp}|${mix.dust}|${mix.dustTemp}`;
  const known = paletteMemo.get(key);
  if (known) return known;
  const p = {
    instrument: id, filterCount: 0, gain: new Array(8).fill(0.0), ext: new Array(8).fill(0.0),
    colour: Array.from({ length: 8 }, () => [0.0, 0.0, 0.0]), pivot: new Array(8).fill(0.0),
    lines: null, continua: null, galaxies: null,
  };
  const inst = InstrumentOf(id);
  p.filterCount = inst.filterCount;

  const reference = new Array(8).fill(0.0);
  for (let f = 0; f < p.filterCount; f += 1) {
    const filter = FilterNamed(inst.filters[f]);
    p.pivot[f] = filter.pivot;
    p.ext[f] = Extinction(filter.pivot);
    let r = 0.0;
    for (let c = 0; c < kLineComponents; c += 1) r += mix.line[c] * lineResponseOf(filter, c);
    r += mix.star * continuumOf(filter, 0, mix.starTemp);
    r += mix.dust * inst.dustGlow * continuumOf(filter, 2, mix.dustTemp);
    reference[f] = max2(r, 1e-12);
  }
  const sum = [0.0, 0.0, 0.0];
  for (let f = 0; f < p.filterCount; f += 1) {
    p.gain[f] = 1.0 / reference[f];
    for (let c = 0; c < 3; c += 1) sum[c] += inst.colour[f][c];
  }
  for (let f = 0; f < p.filterCount; f += 1) {
    for (let c = 0; c < 3; c += 1) p.colour[f][c] = sum[c] > 0.0 ? inst.colour[f][c] / sum[c] : 0.0;
  }
  if (inst.whiteBalance > 0.0) {
    const star = DisplayOfContinuum(p, 0, 5800.0, 0.0);
    const mean = (star[0] + star[1] + star[2]) / 3.0;
    for (let c = 0; c < 3; c += 1) {
      const k = star[c] > 1e-30 ? Math.pow(mean / star[c], inst.whiteBalance) : 1.0;
      for (let f = 0; f < p.filterCount; f += 1) p.colour[f][c] *= k;
    }
  }

  const tauOf = (j) => (kTauMax * j) / (kTauSamples - 1);

  p.lines = new Float32Array(kLineComponents * kTauSamples * 3);
  for (let c = 0; c < kLineComponents; c += 1) {
    for (let j = 0; j < kTauSamples; j += 1) {
      const rgb = DisplayOf(p, c, tauOf(j));
      for (let k = 0; k < 3; k += 1) p.lines[(c * kTauSamples + j) * 3 + k] = rgb[k];
    }
  }

  p.continua = new Float32Array(3 * kTempSamples * kTauSamples * 3);
  for (let kind = 0; kind < 3; kind += 1) {
    for (let i = 0; i < kTempSamples; i += 1) {
      const t = TempOfSample(kind, i);
      const signal = new Array(8).fill(0.0);
      for (let f = 0; f < p.filterCount; f += 1) signal[f] = continuumOf(FilterNamed(inst.filters[f]), kind, t);
      for (let j = 0; j < kTauSamples; j += 1) {
        const rgb = mixFilters(p, signal, tauOf(j));
        for (let k = 0; k < 3; k += 1) p.continua[((kind * kTempSamples + i) * kTauSamples + j) * 3 + k] = rgb[k];
      }
    }
  }

  p.galaxies = new Float32Array(kTempSamples * 32 * 3);
  for (let i = 0; i < kTempSamples; i += 1) {
    for (let k = 0; k < 32; k += 1) {
      const t = TempOfSample(0, i);
      const z = (8.0 * k) / 31.0;
      const signal = new Array(8).fill(0.0);
      for (let f = 0; f < p.filterCount; f += 1) signal[f] = GalaxyResponse(FilterNamed(inst.filters[f]), t, z);
      const rgb = mixFilters(p, signal, 0.0);
      for (let c = 0; c < 3; c += 1) p.galaxies[(i * 32 + k) * 3 + c] = rgb[c];
    }
  }
  paletteMemo.set(key, p);
  return p;
}

function AngularDiameterDistance(z) {
  const hubbleDistance = 299792.458 / 70.0;
  const comoving = simpson((s) => 1.0 / Math.sqrt(0.3 * Math.pow(1.0 + s, 3.0) + 0.7), 0.0, z, 400);
  return (hubbleDistance * comoving) / (1.0 + z);
}

function MeteorLight(x) {
  if (x <= 0.0 || x >= 3.0) return 0.0;
  const r = 1.0 - x / 3.0;
  return 2.25 * x * r * r;
}
const kScaleHeightKm = 7.0;
const kMeteorStartX = 0.02;

const kNickelLife = 8.8;
const kCobaltLife = 111.3;
const kDiffusionTime = 13.0;
const kGammaTrap = 35.0;

function DecayHeating(days) {
  const t = max2(days, 0.0);
  const deposited = t <= 0.0 ? 1.0 : 1.0 - Math.exp(-(kGammaTrap / t) * (kGammaTrap / t));
  const nickel = 6.45 * Math.exp(-t / kNickelLife) * deposited;
  const cobalt = 1.45 * Math.exp(-t / kCobaltLife) * (0.965 * deposited + 0.035);
  return (nickel + cobalt) / 7.90;
}

const kTableStep = 0.05;
const kTableDays = 400;
let lightTableMemo = null;
function lightTable() {
  if (lightTableMemo) return lightTableMemo;
  const n = Math.trunc(kTableDays / kTableStep) + 1;
  const tm2 = kDiffusionTime * kDiffusionTime;
  const light = new Float64Array(n);
  let integral = 0.0;
  for (let i = 1; i < n; i += 1) {
    const t0 = (i - 1) * kTableStep;
    const t1 = i * kTableStep;
    const decay = Math.exp(-(t1 * t1 - t0 * t0) / tm2);
    integral = integral * decay + 0.5 * kTableStep * (t0 * DecayHeating(t0) * decay + t1 * DecayHeating(t1));
    light[i] = (2.0 / tm2) * integral;
  }
  let peak = 0.0;
  let peakDay = 0.0;
  for (let i = 0; i < n; i += 1) {
    if (light[i] > peak) {
      peak = light[i];
      peakDay = i * kTableStep;
    }
  }
  lightTableMemo = { light, peak, peakDay };
  return lightTableMemo;
}

function SupernovaLight(days) {
  if (days <= 0.0) return 0.0;
  const table = lightTable();
  const index = days / kTableStep;
  const n = table.light.length;
  if (index >= n - 1) return (table.light[n - 1] * DecayHeating(days)) / DecayHeating(kTableDays) / table.peak;
  const i = Math.trunc(index);
  const f = index - i;
  const v = table.light[i] * (1.0 - f) + table.light[i + 1] * f;
  return v / table.peak;
}
const SupernovaPeakDay = () => lightTable().peakDay;
function SupernovaTemperature(days) {
  const peak = SupernovaPeakDay();
  return 4500.0 + 7000.0 * Math.exp(-max2(days - 0.6 * peak, 0.0) / 25.0);
}
function RemnantRadius(age) {
  if (age <= 0.0) return 0.0;
  return age < 1.0 ? age : Math.pow(age, 0.4);
}

//===========================================================================
// Controls.h / Controls.cpp
//===========================================================================

const PT = {
  TARGET: 0, TELESCOPE: 1, SEED: 2,
  GAS: 3, TURBULENCE: 4, FLOW: 5, HAZE: 6, DUST: 7,
  STARLIGHT: 8, STAR_HEAT: 9, LIGHT_ANGLE: 10,
  STARS: 11, SPIKES: 12, SEEING: 13,
  ZOOM: 14, PAN_X: 15, PAN_Y: 16, SPIN: 17, DRIFT: 18, DEPTH: 19, FLY: 20,
  METEORS: 21, METEOR_KIND: 22, FIRE_METEOR: 23, SUPERNOVA: 24, SN_SPEED: 25, ECHO: 26,
  EXPOSURE: 27, STRETCH: 28, SATURATION: 29, CLIP_AS: 30, MIX: 31,
  ABOUT_TEXT: 32,
};
const Kind = { Standard: 0, Option: 1, Event: 2 };
const MeteorKind = { Sporadic: 0, Shower: 1, Count: 2 };
const ClipAs = { Behind: 0, Gas: 1, Count: 2 };

/** kInfo: id, name, group, kind, default (the host's 0..1, or the element index), option count. */
const kInfo = [
  [PT.TARGET, 'Target', 'Scene', Kind.Option, 0.0, 0],
  [PT.TELESCOPE, 'Telescope', 'Scene', Kind.Option, 0.0, 1 + kTelescopeChoices],
  [PT.SEED, 'Seed', 'Scene', Kind.Standard, 0.0, 0],
  [PT.GAS, 'Gas Density', 'Gas', Kind.Standard, 0.5, 0],
  [PT.TURBULENCE, 'Turbulence', 'Gas', Kind.Standard, 0.5, 0],
  [PT.FLOW, 'Flow', 'Gas', Kind.Standard, 0.25, 0],
  [PT.HAZE, 'Haze', 'Gas', Kind.Standard, 0.5, 0],
  [PT.DUST, 'Dust', 'Gas', Kind.Standard, 0.5, 0],
  [PT.STARLIGHT, 'Starlight', 'Light', Kind.Standard, 0.5, 0],
  [PT.STAR_HEAT, 'Star Heat', 'Light', Kind.Standard, 0.5, 0],
  [PT.LIGHT_ANGLE, 'Light Angle', 'Light', Kind.Standard, 0.5, 0],
  [PT.STARS, 'Stars', 'Stars', Kind.Standard, 0.5, 0],
  [PT.SPIKES, 'Spikes', 'Stars', Kind.Standard, 0.5, 0],
  [PT.SEEING, 'Seeing', 'Stars', Kind.Standard, 0.0, 0],
  [PT.ZOOM, 'Zoom', 'Camera', Kind.Standard, 0.5, 0],
  [PT.PAN_X, 'Pan X', 'Camera', Kind.Standard, 0.5, 0],
  [PT.PAN_Y, 'Pan Y', 'Camera', Kind.Standard, 0.5, 0],
  [PT.SPIN, 'Spin', 'Camera', Kind.Standard, 0.5, 0],
  [PT.DRIFT, 'Drift', 'Camera', Kind.Standard, 0.25, 0],
  [PT.DEPTH, 'Depth', 'Camera', Kind.Standard, 0.5, 0],
  [PT.FLY, 'Fly', 'Camera', Kind.Standard, 0.0, 0],
  [PT.METEORS, 'Meteors', 'Events', Kind.Standard, 0.0, 0],
  [PT.METEOR_KIND, 'Meteor Kind', 'Events', Kind.Option, 1.0, MeteorKind.Count],
  [PT.FIRE_METEOR, 'Fire Meteor', 'Events', Kind.Event, 0.0, 0],
  [PT.SUPERNOVA, 'Supernova', 'Events', Kind.Event, 0.0, 0],
  [PT.SN_SPEED, 'SN Speed', 'Events', Kind.Standard, 0.5, 0],
  [PT.ECHO, 'Light Echo', 'Events', Kind.Standard, 0.5, 0],
  [PT.EXPOSURE, 'Exposure', 'Output', Kind.Standard, 0.5, 0],
  [PT.STRETCH, 'Stretch', 'Output', Kind.Standard, 0.5, 0],
  [PT.SATURATION, 'Saturation', 'Output', Kind.Standard, 0.5, 0],
  [PT.CLIP_AS, 'Clip As', 'Output', Kind.Option, 0.0, ClipAs.Count],
  [PT.MIX, 'Mix', 'Output', Kind.Standard, 1.0, 0],
].map(([id, name, group, kind, defaultValue, optionCount]) => ({ id, name, group, kind, defaultValue: f32(defaultValue), optionCount }));

function InfoOf(id) {
  if (id === PT.TARGET) return { ...kInfo[PT.TARGET], optionCount: TargetCount() };
  return kInfo[min2(id, PT.ABOUT_TEXT - 1)];
}

function OptionName(id, index) {
  switch (id) {
    case PT.TARGET: return TargetName(index);
    case PT.TELESCOPE: return index === 0 ? 'As Released' : InstrumentOf(index - 1).name;
    case PT.METEOR_KIND: return ['Sporadic', 'Shower'][clamp(index, 0, 1)];
    case PT.CLIP_AS: return ['Behind', 'Gas'][clamp(index, 0, 1)];
    default: return '';
  }
}

/** HostOrder(): the ids each plugin declares, in host order, the About block left off. */
function HostOrder(effect) {
  const order = [];
  for (let id = 0; id < PT.ABOUT_TEXT; id += 1) {
    if (effect || (id !== PT.CLIP_AS && id !== PT.MIX)) order.push(id);
  }
  return order;
}

const OptionIndex = (value, count) => clamp(lround(value), 0, max2(count - 1, 0));

function Multiple(value, lo, hi) {
  const v = clamp(value, 0.0, 1.0);
  return v <= 0.5 ? Math.pow(lo, 1.0 - 2.0 * v) : Math.pow(hi, 2.0 * v - 1.0);
}
/** Linear in two halves: 0 at 0, 1 at 0.5, hi at 1. */
function twoHalves(value, hi) {
  const v = clamp(value, 0.0, 1.0);
  return v <= 0.5 ? 2.0 * v : 1.0 + (hi - 1.0) * (2.0 * v - 1.0);
}
function geometric(value, lo, hi) {
  const v = clamp(value, 0.0, 1.0);
  return lo * Math.pow(hi / lo, v);
}
const GasFromParam = (v) => Multiple(v, 0.25, 4.0);
const TurbulenceFromParam = (v) => twoHalves(v, 3.0);
const FlowFromParam = (v) => 0.12 * clamp(v, 0.0, 1.0);
const HazeFromParam = (v) => twoHalves(v, 3.0);
const DustFromParam = (v) => Multiple(v, 0.1, 10.0);
const StarlightFromParam = (v) => Multiple(v, 0.1, 10.0);
const HeatFromParam = (v) => Multiple(v, 0.6, 1.6);
const LightAngleFromParam = (v) => (clamp(v, 0.0, 1.0) - 0.5) * 2.0 * kPi;
const StarsFromParam = (v) => twoHalves(v, 3.0);
const SpikesFromParam = (v) => twoHalves(v, 3.0);
const SeeingFromParam = (v) => 8.0 * clamp(v, 0.0, 1.0);
const ZoomFromParam = (v) => Multiple(v, 0.25, 4.0);
const PanFromParam = (v) => (clamp(v, 0.0, 1.0) - 0.5) * 2.0;
const SpinFromParam = (v) => (clamp(v, 0.0, 1.0) - 0.5) * 2.0 * kPi;
const DriftFromParam = (v) => 0.05 * clamp(v, 0.0, 1.0);
const DepthFromParam = (v) => 2.0 * clamp(v, 0.0, 1.0);
const FlyFromParam = (v) => 1.5 * clamp(v, 0.0, 1.0);
const MeteorsFromParam = (v) => (v <= 0.0 ? 0.0 : geometric(v, 0.5, 120.0));
const SupernovaSpeedFromParam = (v) => geometric(v, 2.0, 200.0);
const EchoFromParam = (v) => clamp(v, 0.0, 1.0);
const ExposureFromParam = (v) => (clamp(v, 0.0, 1.0) - 0.5) * 8.0;
function StretchFromParam(value) {
  const v = clamp(value, 0.0, 1.0);
  return v <= 0.0 ? 0.0 : geometric(f32(v), 0.3, 40.0);
}
const SaturationFromParam = (v) => 2.0 * clamp(v, 0.0, 1.0);

//===========================================================================
// Scene.h / Scene.cpp
//===========================================================================

const Shape = { Blob: 0, Column: 1, Polygon: 2, Shell: 3, Filaments: 4, Sheet: 5 };
const Channel = { Dense: 0, Haze: 1, Foreground: 2, Shock: 3, Oxygen: 4, Synchrotron: 5, Jets: 6 };
const F = (v) => f32(v);

/** PCG32, as Scene.cpp's Random: 64-bit state in BigInt. */
const MUL64 = 6364136223846793005n;
const INC64 = 1442695040888963407n;
const MASK64 = (1n << 64n) - 1n;
class Random {
  constructor(seed) {
    this.state = (BigInt.asUintN(64, BigInt(seed)) * MUL64 + INC64) & MASK64;
    this.next();
  }

  next() {
    const old = this.state;
    this.state = (old * MUL64 + INC64) & MASK64;
    const xorshifted = Number((((old >> 18n) ^ old) >> 27n) & 0xffffffffn);
    const rot = Number(old >> 59n);
    return ((xorshifted >>> rot) | (xorshifted << ((32 - rot) & 31))) >>> 0;
  }

  /** [ 0, 1 ), a float. */
  uniform() {
    return f32((this.next() >>> 8) * f32(1.0 / 16777216.0));
  }

  range(lo, hi) {
    return f32(lo + f32(f32(hi - lo) * this.uniform()));
  }
}

const element = () => ({
  shape: Shape.Blob, channel: Channel.Dense, x: 0, y: 0, x2: 0, y2: 0, a: F(0.1), b: F(0.1), angle: 0, density: 1,
  thickness: F(0.1), edge: F(0.01), rough: F(0.02), detail: F(0.5), depth: 1, extra: 0, extra2: 0, polyStart: 0, polyCount: 0,
});

function newTarget() {
  return {
    name: '', asReleased: 0, elements: [], polygon: [], sources: [], stars: [], galaxies: [],
    deep: { on: false, density: 0, lens: false, lensX: 0, lensY: 0, einstein: F(0.1), zLens: F(0.39), members: 0, coreRadius: F(0.15) },
    band: { amount: 0, angle: 0, width: F(0.3), offset: 0 },
    haze: F(0.1), hazeShade: F(0.6), hazeScale: F(2.0), warpScale: F(3.0), warp: F(0.03), fieldStars: 2000, embedded: 0,
    dustRatio: F(4.0), exposure: 0, stretch: 1, snX: F(0.1), snY: F(0.1), echoDistance: F(0.15), galaxyScene: false,
    mix: NebulaMix(),
  };
}

const TARGET_NAMES = [
  'Pillars', 'Cosmic Cliffs', 'Horsehead', 'Orion', 'Crab',
  'Ring', 'Southern Ring', 'Helix', 'Veil', 'Pleiades',
  'Andromeda', 'Whirlpool', 'Sombrero', 'Ultra Deep', 'First Deep',
  'Gen Nebula', 'Gen Dark', 'Gen Planetary', 'Gen Remnant', 'Gen Spiral',
  'Gen Cluster', 'Gen Starfield',
];
const kTargetCount = TARGET_NAMES.length;
const kFirstGenerative = 15;
const TargetCount = () => kTargetCount;
const TargetName = (index) => TARGET_NAMES[clamp(index, 0, kTargetCount - 1)];

/** A published picture's ( u, v ) -> the frame, filling its height. */
const picture = (aspect) => ({ X: (u) => f32(f32(u - F(0.5)) * aspect), Y: (v) => f32(F(0.5) - v) });

function addPolygon(t, p, uv, e) {
  e.shape = Shape.Polygon;
  e.polyStart = t.polygon.length / 2;
  e.polyCount = uv.length / 2;
  for (let i = 0; i + 1 < uv.length; i += 2) {
    t.polygon.push(p.X(F(uv[i])));
    t.polygon.push(p.Y(F(uv[i + 1])));
  }
  t.elements.push(e);
}

function dense(density, thickness, edge, rough, detail) {
  return { ...element(), channel: Channel.Dense, density: F(density), thickness: F(thickness), edge: F(edge), rough: F(rough), detail: F(detail) };
}

function blob(channel, x, y, a, b, angle, density, thickness, edge, rough, detail) {
  return {
    ...element(), shape: Shape.Blob, channel, x: F(x), y: F(y), a: F(a), b: F(b), angle: F(angle), density: F(density),
    thickness: F(thickness), edge: F(edge), rough: F(rough), detail: F(detail),
  };
}

function column(x, y, x2, y2, r0, r1, density, thickness, rough) {
  return {
    ...element(), shape: Shape.Column, channel: Channel.Dense, x: F(x), y: F(y), x2: F(x2), y2: F(y2), a: F(r0), b: F(r1),
    density: F(density), thickness: F(thickness), edge: f32(f32(F(0.25) * F(r1)) + F(0.004)), rough: F(rough), detail: F(0.55),
  };
}

function sheet(channel, x, y, dx, dy, density, thickness, edge, detail, anisotropy) {
  dx = F(dx);
  dy = F(dy);
  const l = sqrtf(f32(f32(dx * dx) + f32(dy * dy)));
  return {
    ...element(), shape: Shape.Sheet, channel, x: F(x), y: F(y), x2: f32(dx / l), y2: f32(dy / l), density: F(density),
    thickness: F(thickness), edge: F(edge), detail: F(detail), rough: 0, extra2: F(anisotropy),
  };
}

function shell(channel, x, y, a, b, angle, fraction, density, thickness, knots) {
  return {
    ...element(), shape: Shape.Shell, channel, x: F(x), y: F(y), a: F(a), b: F(b), angle: F(angle), extra: F(fraction),
    extra2: F(knots), density: F(density), thickness: F(thickness), edge: F(0.004), rough: F(0.01), detail: F(0.5),
  };
}

function filaments(channel, x, y, a, b, angle, density, scale, anisotropy) {
  return {
    ...element(), shape: Shape.Filaments, channel, x: F(x), y: F(y), a: F(a), b: F(b), angle: F(angle), density: F(density),
    thickness: F(0.05), edge: F(0.08), rough: 0, detail: F(0.6), extra: F(scale), extra2: F(anisotropy),
  };
}

function source(x, y, h, luminosity, temperature, visible = false) {
  return { x: F(x), y: F(y), h: F(h), luminosity: F(luminosity), temperature: F(temperature), soft: 0, visible };
}

function star(t, x, y, flux, temperature, layer = 1, depth = 1.0) {
  t.stars.push({ x: F(x), y: F(y), flux: F(flux), temperature: F(temperature), depth: F(depth), layer });
}

/** A star's temperature, drawn from what a field of bright stars holds. */
function fieldTemperature(r) {
  const u = r.uniform();
  if (u < F(0.45)) return r.range(F(3300.0), F(4900.0));
  if (u < F(0.78)) return r.range(F(5000.0), F(7200.0));
  if (u < F(0.95)) return r.range(F(7500.0), F(11000.0));
  return r.range(F(12000.0), F(30000.0));
}

/** Bright field stars: fluxes from a Euclidean count, N( > F ) ~ F^-1.5. */
function fieldStars(t, r, count, fluxMin, fluxMax, halfWidth = 1.1, halfHeight = 0.62) {
  fluxMin = F(fluxMin);
  fluxMax = F(fluxMax);
  halfWidth = F(halfWidth);
  halfHeight = F(halfHeight);
  for (let i = 0; i < count; i += 1) {
    const flux = min2(fluxMax, f32(fluxMin * powf(max2(r.uniform(), F(1e-4)), f32(F(-2.0) / F(3.0)))));
    const x = r.range(-halfWidth, halfWidth);
    const y = r.range(-halfHeight, halfHeight);
    const layer = r.uniform() < F(0.35) ? 0 : 1;
    // Argument order as C++ evaluates it here (clang: left to right).
    const temperature = fieldTemperature(r);
    star(t, x, y, flux, temperature, layer, r.range(F(0.5), F(3.0)));
  }
}

/** A cluster of hot young stars, King-like: dense core, sparse halo. */
function cluster(t, r, x, y, radius, count, flux) {
  x = F(x);
  y = F(y);
  radius = F(radius);
  flux = F(flux);
  for (let i = 0; i < count; i += 1) {
    const u1 = r.uniform();
    const u2 = r.uniform();
    const rr = f32(f32(radius * sqrtf(f32(u1 / max2(f32(F(1.0) - f32(u2 * F(0.9))), F(0.05))))) * F(0.4));
    const a = r.range(F(0.0), F(6.2831853));
    const px = f32(x + f32(rr * cosf(a)));
    const py = f32(y + f32(rr * sinf(a)));
    const fl = f32(flux * powf(r.range(F(0.05), F(1.0)), F(1.5)));
    star(t, px, py, fl, r.range(F(14000.0), F(42000.0)), 0, F(1.0));
  }
}

//---------------------------------------------------------------------------
// The pictures.
//---------------------------------------------------------------------------

function pillars(r) {
  const t = newTarget();
  t.name = 'Pillars';
  t.asReleased = I.HUBBLE_NARROW;
  const p = picture(F(1280.0 / 1335.0));
  const d = () => dense(1.0, 0.09, 0.006, 0.022, 0.75);
  addPolygon(t, p, [0.36, 0.135, 0.42, 0.14, 0.46, 0.165, 0.47, 0.20, 0.44, 0.235, 0.41, 0.26, 0.405, 0.32, 0.42, 0.40, 0.44, 0.43,
    0.43, 0.47, 0.45, 0.52, 0.47, 0.60, 0.47, 0.70, 0.48, 0.78, 0.30, 0.80, 0.25, 0.74, 0.24, 0.62, 0.25, 0.52,
    0.27, 0.45, 0.28, 0.36, 0.29, 0.28, 0.29, 0.22, 0.30, 0.17], d());
  addPolygon(t, p, [0.585, 0.265, 0.61, 0.28, 0.62, 0.33, 0.61, 0.40, 0.62, 0.45, 0.64, 0.50, 0.65, 0.60, 0.66, 0.70, 0.66, 0.80,
    0.50, 0.80, 0.47, 0.70, 0.48, 0.60, 0.50, 0.50, 0.52, 0.42, 0.535, 0.35, 0.55, 0.30, 0.565, 0.27], d());
  addPolygon(t, p, [0.715, 0.41, 0.735, 0.43, 0.74, 0.50, 0.73, 0.57, 0.735, 0.62, 0.72, 0.70, 0.70, 0.80, 0.65, 0.80, 0.66, 0.70,
    0.67, 0.60, 0.68, 0.52, 0.69, 0.45, 0.70, 0.42], d());
  addPolygon(t, p, [0.22, 1.08, 0.20, 0.90, 0.24, 0.80, 0.35, 0.77, 0.50, 0.76, 0.62, 0.76, 0.72, 0.78, 0.78, 0.85, 0.80, 0.95, 0.82, 1.08],
    dense(1.3, 0.2, 0.015, 0.02, 0.5));
  t.elements.push(blob(Channel.Dense, p.X(F(0.885)), p.Y(F(0.905)), 0.010, 0.016, 0.3, 1.0, 0.03, 0.004, 0.004, 0.3));
  t.elements.push(sheet(Channel.Haze, 0.0, -0.9, 0.0, 1.0, 0.18, 1.0, 1.6, 0.6, 0.0));
  t.haze = F(0.07);
  t.hazeScale = F(2.5);
  t.warpScale = F(5.0);
  t.warp = F(0.012);
  t.exposure = F(-0.3);
  t.sources.push(source(0.10, 1.05, 0.35, 1.6, 42000.0));
  t.sources.push(source(-0.45, 0.95, 0.5, 0.6, 38000.0));
  star(t, p.X(F(0.11)), p.Y(F(0.27)), 40.0, 4200.0, 0);
  star(t, p.X(F(0.32)), p.Y(F(0.035)), 18.0, 4800.0, 0);
  star(t, p.X(F(0.41)), p.Y(F(0.37)), 10.0, 6500.0, 0);
  star(t, p.X(F(0.95)), p.Y(F(0.82)), 6.0, 5200.0, 1);
  fieldStars(t, r, 26, 0.6, 12.0);
  t.fieldStars = 900;
  t.embedded = 350;
  t.dustRatio = F(4.5);
  t.snX = p.X(F(0.8));
  t.snY = p.Y(F(0.2));
  t.echoDistance = F(0.08);
  return t;
}

function cliffs(r) {
  const t = newTarget();
  t.name = 'Cosmic Cliffs';
  t.asReleased = I.CLIFFS;
  const p = picture(F(1280.0 / 742.0));
  addPolygon(t, p, [-0.06, 0.24, 0.0, 0.22, 0.04, 0.21, 0.08, 0.24, 0.12, 0.27, 0.155, 0.30, 0.18, 0.29, 0.19, 0.265, 0.197, 0.30,
    0.21, 0.37, 0.25, 0.44, 0.30, 0.43, 0.35, 0.41, 0.40, 0.38, 0.44, 0.39, 0.48, 0.38, 0.52, 0.41, 0.56, 0.40,
    0.60, 0.43, 0.64, 0.41, 0.70, 0.40, 0.76, 0.36, 0.82, 0.34, 0.87, 0.30, 0.92, 0.26, 0.96, 0.23, 1.0, 0.22,
    1.06, 0.25, 1.06, 1.08, -0.06, 1.08], dense(1.1, 0.25, 0.01, 0.05, 0.85));
  t.elements.push(sheet(Channel.Haze, 0.0, 0.2, 0.0, -1.0, 0.3, 0.6, 0.3, 0.8, 5.0));
  t.elements.push(blob(Channel.Jets, p.X(F(0.075)), p.Y(F(0.45)), 0.045, 0.03, 0.6, 0.5, 0.05, 0.03, 0.02, 1.0));
  t.elements.push(blob(Channel.Jets, p.X(F(0.42)), p.Y(F(0.47)), 0.01, 0.035, -0.2, 0.35, 0.05, 0.012, 0.008, 1.0));
  t.haze = F(0.06);
  t.hazeScale = F(3.0);
  t.warpScale = F(4.0);
  t.warp = F(0.025);
  t.sources.push(source(0.05, 1.0, 0.6, 1.4, 44000.0));
  star(t, p.X(F(0.36)), p.Y(F(0.11)), 30.0, 7000.0, 0);
  star(t, p.X(F(0.405)), p.Y(F(0.205)), 22.0, 9000.0, 0);
  star(t, p.X(F(0.87)), p.Y(F(0.245)), 25.0, 5500.0, 0);
  star(t, p.X(F(0.76)), p.Y(F(0.31)), 12.0, 6000.0, 1);
  star(t, p.X(F(0.93)), p.Y(F(0.95)), 15.0, 4500.0, 0);
  fieldStars(t, r, 24, 0.6, 10.0);
  t.fieldStars = 1600;
  t.embedded = 400;
  t.dustRatio = F(4.0);
  t.snX = p.X(F(0.6));
  t.snY = p.Y(F(0.15));
  return t;
}

function horsehead(r) {
  const t = newTarget();
  t.name = 'Horsehead';
  t.asReleased = I.HUBBLE_COLOUR;
  const p = picture(F(1280.0 / 1317.0));
  addPolygon(t, p, [0.18, 0.345, 0.24, 0.49, 0.28, 0.51, 0.38, 0.56, 0.34, 0.71, 0.28, 0.86, 0.18, 1.06, 0.90, 1.06, 0.83, 0.79,
    0.85, 0.60, 0.86, 0.43, 0.80, 0.26, 0.62, 0.18, 0.46, 0.14, 0.40, 0.18, 0.32, 0.24, 0.22, 0.30], dense(1.6, 0.18, 0.012, 0.02, 0.5));
  addPolygon(t, p, [-0.25, 0.88, 0.10, 0.92, 0.30, 0.95, 0.60, 0.97, 1.25, 0.95, 1.25, 1.1, -0.25, 1.1], dense(1.4, 0.4, 0.04, 0.03, 0.6));
  t.elements.push(sheet(Channel.Haze, 0.0, -0.65, 0.0, 1.0, 0.5, 1.0, 0.9, 0.6, 4.0));
  t.haze = F(0.05);
  t.hazeScale = F(3.0);
  t.warpScale = F(4.5);
  t.warp = F(0.015);
  t.hazeShade = F(0.0);
  t.sources.push(source(0.25, 2.6, -0.6, 9.0, 33000.0));
  star(t, p.X(F(0.04)), p.Y(F(0.31)), 14.0, 6000.0, 0);
  star(t, p.X(F(0.79)), p.Y(F(0.07)), 12.0, 5200.0, 0);
  star(t, p.X(F(0.84)), p.Y(F(0.98)), 10.0, 7000.0, 0);
  fieldStars(t, r, 18, 0.5, 6.0);
  t.fieldStars = 900;
  t.embedded = 150;
  t.dustRatio = F(3.5);
  t.snX = p.X(F(0.7));
  t.snY = p.Y(F(0.12));
  return t;
}

function orion(r) {
  const t = newTarget();
  t.name = 'Orion';
  t.asReleased = I.HUBBLE_COLOUR;
  t.elements.push(blob(Channel.Dense, -0.55, 0.25, 0.45, 0.3, 0.5, 0.55, 0.6, 0.15, 0.06, 0.8));
  t.elements.push(blob(Channel.Dense, 0.65, -0.3, 0.4, 0.28, -0.4, 0.5, 0.6, 0.15, 0.06, 0.8));
  t.elements.push(blob(Channel.Dense, 0.6, 0.45, 0.35, 0.2, 0.3, 0.45, 0.5, 0.12, 0.05, 0.8));
  t.elements.push(column(0.05, -0.24, 0.38, -0.08, 0.008, 0.025, 0.4, 0.12, 0.01));
  t.elements.push(blob(Channel.Foreground, -0.32, 0.12, 0.2, 0.07, -0.5, 1.2, 0.1, 0.05, 0.03, 0.7));
  t.elements.push(blob(Channel.Foreground, -0.15, 0.05, 0.09, 0.035, -0.3, 0.9, 0.1, 0.03, 0.02, 0.7));
  t.elements.push(blob(Channel.Haze, 0.0, 0.0, 0.22, 0.18, 0.2, 0.6, 0.5, 0.15, 0.03, 0.6));
  t.elements.push(blob(Channel.Haze, -0.25, -0.25, 0.4, 0.18, 0.8, 0.25, 0.6, 0.2, 0.06, 0.7));
  t.elements.push(blob(Channel.Haze, 0.35, 0.15, 0.45, 0.2, -0.6, 0.2, 0.6, 0.2, 0.06, 0.7));
  t.elements.push(blob(Channel.Haze, 0.05, 0.42, 0.09, 0.08, 0.0, 0.4, 0.3, 0.05, 0.02, 0.5));
  t.elements.push(blob(Channel.Dense, 0.0, 0.37, 0.03, 0.1, 0.3, 0.9, 0.1, 0.02, 0.01, 0.5));
  t.haze = F(0.05);
  t.hazeScale = F(3.0);
  t.warpScale = F(3.5);
  t.warp = F(0.03);
  t.sources.push(source(0.0, 0.0, 0.12, 0.22, 39000.0, true));
  t.sources.push(source(0.06, 0.43, 0.06, 0.05, 26000.0, true));
  star(t, -0.012, 0.008, 30.0, 32000.0, 0);
  star(t, 0.01, -0.01, 25.0, 30000.0, 0);
  star(t, 0.004, 0.018, 20.0, 25000.0, 0);
  cluster(t, r, 0.0, 0.0, 0.4, 30, 6.0);
  fieldStars(t, r, 16, 0.5, 6.0);
  t.fieldStars = 1200;
  t.embedded = 400;
  t.dustRatio = F(3.0);
  t.snX = F(0.35);
  t.snY = F(0.3);
  return t;
}

function crab(r) {
  const t = newTarget();
  t.name = 'Crab';
  t.mix = RemnantMix();
  t.asReleased = I.HUBBLE_COLOUR;
  t.exposure = F(1.5);
  t.elements.push(filaments(Channel.Shock, 0.0, 0.0, 0.43, 0.30, -0.55, 1.6, 9.0, 0.4));
  t.elements.push(filaments(Channel.Oxygen, 0.01, 0.005, 0.40, 0.28, -0.55, 0.6, 13.0, 0.3));
  t.elements.push(blob(Channel.Synchrotron, 0.0, 0.0, 0.36, 0.25, -0.55, 1.2, 0.3, 0.2, 0.02, 0.5));
  t.elements.push(blob(Channel.Synchrotron, 0.015, 0.01, 0.06, 0.04, -0.55, 1.5, 0.1, 0.04, 0.01, 0.4));
  t.haze = F(0.0);
  t.warpScale = F(4.0);
  t.warp = F(0.02);
  star(t, 0.012, 0.008, 3.0, 9000.0, 0);
  star(t, 0.03, 0.02, 2.0, 6000.0, 0);
  fieldStars(t, r, 20, 0.5, 8.0);
  t.fieldStars = 700;
  t.dustRatio = F(2.0);
  t.snX = F(-0.5);
  t.snY = F(0.3);
  return t;
}

/** A planetary nebula's shell, lit from inside by its white dwarf. */
function planetary(t, x, y, a, b, angle, fraction, knots, halo, wd, open = 0.0) {
  [x, y, a, b, angle, fraction, knots, halo, wd, open] = [x, y, a, b, angle, fraction, knots, halo, wd, open].map(F);
  t.mix = PlanetaryMix();
  t.elements.push(shell(Channel.Dense, x, y, a, b, angle, fraction, 0.9, 0.05, knots));
  t.elements[t.elements.length - 1].x2 = open;
  if (halo > 0.0) {
    t.elements.push(shell(Channel.Haze, x, y, f32(a * F(1.55)), f32(b * F(1.55)), angle, 0.35, halo, 0.6, 0.0));
    t.elements[t.elements.length - 1].x2 = open;
    t.elements.push(shell(Channel.Haze, x, y, f32(a * F(2.1)), f32(b * F(2.1)), angle, 0.12, f32(F(0.4) * halo), 0.6, 0.0));
    t.elements[t.elements.length - 1].x2 = open;
  }
  t.sources.push(source(x, y, 0.0, wd, 110000.0, true));
  t.sources[t.sources.length - 1].soft = f32(f32(F(1.0) - fraction) * min2(a, b));
}

function ring(r) {
  const t = newTarget();
  t.name = 'Ring';
  t.asReleased = I.RING_NIRCAM;
  planetary(t, 0.0, 0.0, 0.2, 0.155, 0.25, 0.24, 0.6, 0.08, 0.004, 0.8);
  t.exposure = F(2.0);
  t.haze = F(0.0);
  t.warpScale = F(8.0);
  t.warp = F(0.006);
  fieldStars(t, r, 22, 0.5, 8.0);
  t.fieldStars = 700;
  t.dustRatio = F(1.5);
  t.snX = F(0.55);
  t.snY = F(0.3);
  return t;
}

function southernRing(r) {
  const t = newTarget();
  t.name = 'Southern Ring';
  t.asReleased = I.SOUTHERN_RING;
  planetary(t, 0.0, 0.0, 0.33, 0.21, 0.35, 0.28, 0.35, 0.12, 0.0016, 0.6);
  star(t, 0.004, 0.003, 22.0, 9500.0, 0);
  t.haze = F(0.0);
  t.warpScale = F(6.0);
  t.warp = F(0.012);
  fieldStars(t, r, 20, 0.5, 8.0);
  t.fieldStars = 1000;
  t.dustRatio = F(1.5);
  t.snX = F(-0.6);
  t.snY = F(0.25);
  return t;
}

function helix(r) {
  const t = newTarget();
  t.name = 'Helix';
  t.asReleased = I.HELIX;
  planetary(t, 0.0, 0.0, 0.36, 0.31, 0.4, 0.32, 1.0, 0.05, 0.0016, 0.7);
  t.elements.push(shell(Channel.Dense, 0.04, -0.03, 0.44, 0.36, 0.6, 0.18, 0.5, 0.04, 0.4));
  t.elements[t.elements.length - 1].x2 = F(0.7);
  t.haze = F(0.0);
  t.warpScale = F(6.0);
  t.warp = F(0.012);
  fieldStars(t, r, 20, 0.5, 8.0);
  t.fieldStars = 900;
  t.dustRatio = F(1.5);
  t.snX = F(0.6);
  t.snY = F(-0.3);
  return t;
}

function veil(r) {
  const t = newTarget();
  t.name = 'Veil';
  t.mix = RemnantMix();
  t.asReleased = I.HELIX;
  t.elements.push(filaments(Channel.Shock, 0.0, 0.0, 1.3, 0.9, 0.5, 1.4, 4.0, 0.85));
  t.elements.push(filaments(Channel.Oxygen, 0.025, 0.015, 1.3, 0.9, 0.5, 1.2, 4.0, 0.85));
  t.haze = F(0.0);
  t.warpScale = F(2.0);
  t.warp = F(0.04);
  fieldStars(t, r, 26, 0.5, 8.0);
  t.fieldStars = 2400;
  t.dustRatio = F(1.0);
  t.snX = F(0.3);
  t.snY = F(0.2);
  return t;
}

function pleiades(r) {
  const t = newTarget();
  t.name = 'Pleiades';
  t.mix = StarlightMix();
  t.asReleased = I.BACKYARD;
  t.exposure = F(3.0);
  const s = F(0.62);
  const sisters = [
    [0.0, 0.0, 2.87, 13000], [-0.52, -0.04, 3.62, 12300], [-0.53, 0.04, 5.05, 12000],
    [0.23, -0.17, 4.18, 14000], [0.48, -0.04, 3.70, 13400], [0.38, 0.12, 3.87, 13600],
    [0.52, 0.24, 4.30, 13000], [0.62, 0.05, 5.45, 12300], [0.50, 0.33, 5.76, 11500],
  ];
  for (const [mx, my, mag, temperature] of sisters) {
    const flux = f32(F(400.0) * powf(F(10.0), f32(F(-0.4) * f32(F(mag) - F(2.87)))));
    const x = f32(f32(F(mx) - F(0.05)) * s);
    const y = f32(f32(F(my) + F(0.03)) * s);
    star(t, x, y, flux, temperature, 0);
    t.sources.push(source(x, y, 0.06, f32(f32(F(0.04) * flux) / F(400.0)), temperature));
  }
  t.elements.push(blob(Channel.Dense, 0.0, 0.0, 0.75, 0.45, -0.4, 0.22, 0.8, 0.3, 0.05, 0.85));
  t.elements.push(blob(Channel.Dense, 0.13, -0.16, 0.12, 0.07, -0.6, 0.35, 0.3, 0.06, 0.02, 0.8));
  t.elements[t.elements.length - 1].extra2 = F(2.5);
  t.elements[0].extra2 = F(2.0);
  t.haze = F(0.0);
  t.warpScale = F(5.0);
  t.warp = F(0.03);
  fieldStars(t, r, 30, 0.4, 10.0);
  t.fieldStars = 1000;
  t.dustRatio = F(2.0);
  t.snX = F(-0.6);
  t.snY = F(0.3);
  return t;
}

const galaxy = () => ({
  kind: 0, x: 0, y: 0, scale: F(0.1), incline: 0, pa: 0, arms: 2, pitch: F(0.3), armContrast: F(0.6), bulge: F(0.3),
  bulgeRe: F(0.03), sersic: F(4.0), bulgeAxis: F(0.8), dust: F(0.6), dustRing: 0, hii: F(0.5), brightness: 1,
  diskT: 6500, bulgeT: 4300, armT: 14000, phase: 0, depth: 1,
});

function spiral(x, y, scale, incline, pa, arms, pitch, bulge, dust, hii) {
  return {
    ...galaxy(), kind: 0, x: F(x), y: F(y), scale: F(scale), incline: F(incline), pa: F(pa), arms, pitch: F(pitch), bulge: F(bulge),
    bulgeRe: f32(F(0.3) * F(scale)), dust: F(dust), hii: F(hii),
  };
}

function elliptical(x, y, re, axis, pa, brightness) {
  return {
    ...galaxy(), kind: 1, x: F(x), y: F(y), scale: F(re), bulgeRe: F(re), bulge: F(1.0), bulgeAxis: F(axis), pa: F(pa), sersic: F(4.0),
    dust: F(0.0), hii: F(0.0), brightness: F(brightness),
  };
}

function andromeda(r) {
  const t = newTarget();
  t.name = 'Andromeda';
  t.stretch = F(6.0);
  t.mix = StarlightMix();
  t.asReleased = I.BACKYARD;
  t.galaxyScene = true;
  const m31 = spiral(0.0, 0.0, 0.13, f32(f32(F(77.0) * F(kPi)) / F(180.0)), 0.62, 2, 0.12, 0.35, 0.9, 0.25);
  m31.sersic = F(2.2);
  m31.bulgeRe = F(0.045);
  m31.dustRing = F(0.0);
  m31.armContrast = F(0.45);
  t.galaxies.push(m31);
  t.galaxies.push(elliptical(0.07, -0.11, 0.010, 0.85, 0.3, 0.18));
  t.galaxies.push(elliptical(-0.30, 0.33, 0.05, 0.5, 2.3, 0.25));
  fieldStars(t, r, 24, 0.5, 8.0);
  t.fieldStars = 900;
  t.haze = F(0.0);
  t.snX = F(0.2);
  t.snY = F(0.08);
  return t;
}

function whirlpool(r) {
  const t = newTarget();
  t.name = 'Whirlpool';
  t.stretch = F(6.0);
  t.mix = StarlightMix();
  t.asReleased = I.HUBBLE_COLOUR;
  t.galaxyScene = true;
  const m51 = spiral(-0.05, -0.06, 0.12, 0.35, 0.4, 2, 0.32, 0.12, 1.0, 1.0);
  m51.armContrast = F(0.75);
  t.galaxies.push(m51);
  const ngc5195 = elliptical(0.24, 0.36, 0.05, 0.75, 0.8, 0.55);
  ngc5195.kind = 2;
  ngc5195.dust = F(0.6);
  ngc5195.scale = F(0.05);
  t.galaxies.push(ngc5195);
  fieldStars(t, r, 10, 0.4, 5.0);
  t.fieldStars = 300;
  t.haze = F(0.0);
  t.snX = F(-0.2);
  t.snY = F(-0.12);
  return t;
}

function sombrero(r) {
  const t = newTarget();
  t.name = 'Sombrero';
  t.stretch = F(6.0);
  t.mix = StarlightMix();
  t.asReleased = I.HUBBLE_COLOUR;
  t.galaxyScene = true;
  const m104 = spiral(0.0, 0.0, 0.2, f32(f32(F(84.0) * F(kPi)) / F(180.0)), 0.0, 2, 0.2, 0.7, 1.2, 0.05);
  m104.sersic = F(4.0);
  m104.bulgeRe = F(0.2);
  m104.bulge = F(0.85);
  m104.bulgeAxis = F(0.62);
  m104.dustRing = F(0.34);
  m104.armContrast = F(0.15);
  t.galaxies.push(m104);
  fieldStars(t, r, 12, 0.4, 5.0);
  t.fieldStars = 300;
  t.haze = F(0.0);
  t.snX = F(0.4);
  t.snY = F(0.02);
  return t;
}

function ultraDeep(r) {
  const t = newTarget();
  t.name = 'Ultra Deep';
  t.stretch = F(4.0);
  t.mix = StarlightMix();
  t.asReleased = I.HUBBLE_DEEP;
  t.galaxyScene = true;
  t.deep.on = true;
  t.deep.density = F(2600.0);
  fieldStars(t, r, 3, 0.5, 3.0);
  t.fieldStars = 30;
  t.haze = F(0.0);
  t.snX = F(0.1);
  t.snY = F(0.1);
  return t;
}

function firstDeep(r) {
  const t = newTarget();
  t.name = 'First Deep';
  t.asReleased = I.WEBB_DEEP;
  t.galaxyScene = true;
  t.deep.on = true;
  t.deep.density = F(2000.0);
  t.deep.lens = true;
  t.deep.lensX = F(0.02);
  t.deep.lensY = F(0.04);
  t.deep.einstein = F(0.11);
  t.deep.members = F(160.0);
  t.deep.coreRadius = F(0.18);
  t.galaxies.push(elliptical(0.02, 0.04, 0.04, 0.7, 0.5, 0.9));
  star(t, -0.12, 0.32, 120.0, 5600.0, 0);
  star(t, 0.55, -0.28, 40.0, 4800.0, 0);
  fieldStars(t, r, 4, 0.5, 6.0);
  t.fieldStars = 40;
  t.haze = F(0.0);
  t.snX = F(0.25);
  t.snY = F(-0.1);
  return t;
}

//---------------------------------------------------------------------------
// The generative ones: everything from the seed. Every call to the generator
// is made in the order the C++ makes it; where one expression makes several,
// clang evaluates a call's arguments left to right, and so does this.
//---------------------------------------------------------------------------

function genNebula(r) {
  const t = newTarget();
  t.name = 'Gen Nebula';
  t.asReleased = r.uniform() < F(0.5) ? I.HUBBLE_NARROW : I.WEBB_NIRCAM;
  const sa = r.range(F(0.0), F(6.2831853));
  const sx = f32(f32(F(0.95) * cosf(sa)) * F(1.2));
  const sy = clamp(f32(f32(F(0.95) * sinf(sa)) + F(0.3)), F(-0.4), F(1.1));
  {
    const h = r.range(F(0.15), F(0.5));
    const l = r.range(F(1.2), F(2.5));
    const k = r.range(F(32000.0), F(50000.0));
    t.sources.push(source(sx, sy, h, l, k));
  }
  const clouds = 2 + Math.trunc(f32(r.uniform() * 3));
  for (let i = 0; i < clouds; i += 1) {
    const ca = f32(sa + kPi + r.range(F(-1.2), F(1.2)));
    const cr = r.range(F(0.3), F(0.8));
    const cx = f32(f32(cosf(ca) * cr) * F(1.2));
    const cy = f32(f32(sinf(ca) * cr) * F(0.8));
    {
      const a = r.range(F(0.2), F(0.45));
      const b = r.range(F(0.12), F(0.3));
      const ang = r.range(F(0.0), F(3.14));
      const den = r.range(F(0.8), F(1.3));
      t.elements.push(blob(Channel.Dense, cx, cy, a, b, ang, den, 0.2, 0.03, 0.04, 0.7));
    }
    const columns = Math.trunc(f32(r.uniform() * 3));
    for (let k = 0; k < columns; k += 1) {
      const dx = f32(sx - cx);
      const dy = f32(sy - cy);
      const l = sqrtf(f32(f32(dx * dx) + f32(dy * dy)));
      const len = r.range(F(0.15), F(0.45));
      const bx = f32(cx + r.range(F(-0.15), F(0.15)));
      const by = f32(cy + r.range(F(-0.1), F(0.1)));
      const wob = r.range(F(-0.25), F(0.25));
      const tx = f32(bx + f32(len * f32(f32(dx / l) + f32(f32(wob * dy) / l))));
      const ty = f32(by + f32(len * f32(f32(dy / l) - f32(f32(wob * dx) / l))));
      const r0 = r.range(F(0.05), F(0.1));
      const r1 = r.range(F(0.015), F(0.04));
      t.elements.push(column(bx, by, tx, ty, r0, r1, 1.0, 0.09, 0.012));
    }
  }
  t.elements.push(sheet(Channel.Haze, f32(sx * F(0.5)), f32(f32(sy * F(0.5)) - F(0.6)), sx, sy, 0.15, 1.0, 1.4, 0.6, 0.0));
  t.haze = r.range(F(0.06), F(0.16));
  t.hazeScale = r.range(F(2.0), F(3.5));
  t.warpScale = r.range(F(3.0), F(5.0));
  t.warp = r.range(F(0.012), F(0.03));
  if (Math.abs(sx) < F(0.8) && Math.abs(sy) < F(0.45)) cluster(t, r, sx, sy, 0.15, 16, 12.0);
  fieldStars(t, r, 24, 0.5, 10.0);
  t.fieldStars = r.range(F(1200.0), F(2600.0));
  t.embedded = r.range(F(300.0), F(1200.0));
  t.dustRatio = r.range(F(3.5), F(5.0));
  t.snX = r.range(F(-0.6), F(0.6));
  t.snY = r.range(F(-0.35), F(0.35));
  return t;
}

function genDark(r) {
  const t = newTarget();
  t.name = 'Gen Dark';
  t.mix = StarlightMix();
  t.asReleased = I.BACKYARD;
  t.band.amount = r.range(F(0.6), F(1.2));
  t.band.angle = r.range(F(-0.6), F(0.6));
  t.band.width = r.range(F(0.35), F(0.6));
  t.band.offset = r.range(F(-0.15), F(0.15));
  const clouds = 3 + Math.trunc(f32(r.uniform() * 4));
  for (let i = 0; i < clouds; i += 1) {
    const x = r.range(F(-0.8), F(0.8));
    const y = r.range(F(-0.4), F(0.4));
    const a = r.range(F(0.05), F(0.35));
    const b = r.range(F(0.03), F(0.12));
    const ang = r.range(F(0.0), F(3.14));
    const den = r.range(F(0.4), F(1.5));
    const e = blob(Channel.Foreground, x, y, a, b, ang, den, 0.1, 0.05, 0.04, 0.8);
    e.extra2 = r.range(F(0.0), F(3.0));
    t.elements.push(e);
  }
  t.haze = F(0.02);
  t.hazeScale = F(2.0);
  t.warpScale = r.range(F(3.0), F(6.0));
  t.warp = F(0.03);
  t.sources.push(source(r.range(F(-1.0), F(1.0)), 1.2, 1.0, 0.4, 35000.0));
  fieldStars(t, r, 30, 0.4, 8.0);
  t.fieldStars = r.range(F(1500.0), F(2500.0));
  t.dustRatio = F(4.0);
  t.snX = r.range(F(-0.6), F(0.6));
  t.snY = r.range(F(-0.35), F(0.35));
  return t;
}

function genPlanetary(r) {
  const t = newTarget();
  t.name = 'Gen Planetary';
  t.asReleased = r.uniform() < F(0.5) ? I.HUBBLE_COLOUR : I.WEBB_NIRCAM;
  const a = r.range(F(0.12), F(0.3));
  {
    const b = f32(a * r.range(F(0.55), F(1.0)));
    const angle = r.range(F(0.0), F(3.14));
    const fraction = r.range(F(0.2), F(0.38));
    const knots = r.range(F(0.0), F(1.0));
    const halo = r.range(F(0.0), F(0.15));
    planetary(t, 0.0, 0.0, a, b, angle, fraction, knots, halo, 0.0012);
  }
  if (r.uniform() < F(0.45)) {
    const ang = r.range(F(0.0), F(3.14));
    const l = f32(a * r.range(F(1.0), F(1.8)));
    const cx = f32(cosf(ang) * l);
    const cy = f32(sinf(ang) * l);
    t.elements.push(shell(Channel.Haze, cx, cy, f32(l * F(0.7)), f32(l * F(0.45)), ang, 0.4, 1.2, 0.6, 0.0));
    t.elements.push(shell(Channel.Haze, -cx, -cy, f32(l * F(0.7)), f32(l * F(0.45)), ang, 0.4, 1.2, 0.6, 0.0));
  }
  t.haze = F(0.0);
  t.warpScale = r.range(F(5.0), F(9.0));
  t.warp = r.range(F(0.004), F(0.015));
  fieldStars(t, r, 22, 0.5, 8.0);
  t.fieldStars = r.range(F(600.0), F(2000.0));
  t.dustRatio = F(1.5);
  t.snX = F(0.55);
  t.snY = F(0.3);
  const open = r.range(F(0.3), F(0.9));
  for (const e of t.elements) if (e.shape === Shape.Shell) e.x2 = open;
  return t;
}

function genRemnant(r) {
  const t = newTarget();
  t.name = 'Gen Remnant';
  t.mix = RemnantMix();
  t.asReleased = r.uniform() < F(0.5) ? I.HUBBLE_NARROW : I.HUBBLE_COLOUR;
  const a = r.range(F(0.25), F(0.42));
  const b = f32(a * r.range(F(0.65), F(1.0)));
  const ang = r.range(F(0.0), F(3.14));
  {
    const den = r.range(F(1.0), F(1.8));
    const scale = r.range(F(6.0), F(12.0));
    const an = r.range(F(0.2), F(0.6));
    t.elements.push(filaments(Channel.Shock, 0.0, 0.0, a, b, ang, den, scale, an));
  }
  {
    const den = r.range(F(0.3), F(1.2));
    const scale = r.range(F(8.0), F(14.0));
    const an = r.range(F(0.2), F(0.6));
    t.elements.push(filaments(Channel.Oxygen, 0.01, 0.0, f32(a * F(0.95)), f32(b * F(0.95)), ang, den, scale, an));
  }
  t.elements.push(shell(Channel.Shock, 0.0, 0.0, f32(a * F(1.05)), f32(b * F(1.05)), ang, 0.08, 0.4, 0.05, 0.0));
  if (r.uniform() < F(0.5)) {
    t.elements.push(blob(Channel.Synchrotron, 0.0, 0.0, f32(a * F(0.8)), f32(b * F(0.8)), ang, 1.0, 0.3, f32(a * F(0.4)), 0.02, 0.5));
    star(t, 0.0, 0.0, 3.0, 9000.0, 0);
  }
  t.haze = F(0.0);
  t.warpScale = F(4.0);
  t.warp = F(0.02);
  fieldStars(t, r, 20, 0.5, 8.0);
  t.fieldStars = r.range(F(600.0), F(2000.0));
  t.dustRatio = F(1.5);
  t.snX = F(0.6);
  t.snY = F(-0.3);
  return t;
}

function genSpiral(r) {
  const t = newTarget();
  t.name = 'Gen Spiral';
  t.stretch = F(6.0);
  t.mix = StarlightMix();
  t.asReleased = r.uniform() < F(0.6) ? I.HUBBLE_COLOUR : I.WEBB_NIRCAM;
  t.galaxyScene = true;
  let g;
  {
    const scale = r.range(F(0.08), F(0.14));
    const incline = r.range(F(0.0), F(1.35));
    const pa = r.range(F(0.0), F(3.14));
    const arms = r.uniform() < F(0.7) ? 2 : 3;
    const pitch = r.range(F(0.18), F(0.45));
    const bulge = r.range(F(0.08), F(0.45));
    const dust = r.range(F(0.3), F(1.1));
    const hii = r.range(F(0.2), F(1.0));
    g = spiral(0.0, 0.0, scale, incline, pa, arms, pitch, bulge, dust, hii);
  }
  g.armContrast = r.range(F(0.35), F(0.8));
  t.galaxies.push(g);
  const extras = Math.trunc(f32(r.uniform() * 3));
  for (let i = 0; i < extras; i += 1) {
    if (r.uniform() < F(0.5)) {
      const x = r.range(F(-0.8), F(0.8));
      const y = r.range(F(-0.4), F(0.4));
      const re = r.range(F(0.008), F(0.02));
      const axis = r.range(F(0.5), F(1.0));
      const pa = r.range(F(0.0), F(3.14));
      const brightness = r.range(F(0.05), F(0.15));
      t.galaxies.push(elliptical(x, y, re, axis, pa, brightness));
    } else {
      const x = r.range(F(-0.8), F(0.8));
      const y = r.range(F(-0.4), F(0.4));
      const scale = r.range(F(0.012), F(0.03));
      const incline = r.range(F(0.0), F(1.4));
      const pa = r.range(F(0.0), F(3.14));
      const pitch = r.range(F(0.2), F(0.4));
      const bulge = r.range(F(0.1), F(0.4));
      const s = spiral(x, y, scale, incline, pa, 2, pitch, bulge, 0.6, 0.5);
      s.brightness = r.range(F(0.05), F(0.2));
      t.galaxies.push(s);
    }
  }
  t.deep.on = true;
  t.deep.density = F(400.0);
  fieldStars(t, r, 10, 0.4, 5.0);
  t.fieldStars = r.range(F(100.0), F(600.0));
  t.haze = F(0.0);
  {
    const k = r.range(F(1.0), F(2.0));
    const sign = r.uniform() < F(0.5) ? F(-1.0) : F(1.0);
    t.snX = f32(f32(g.scale * k) * sign);
  }
  t.snY = f32(g.scale * r.range(F(-0.5), F(0.5)));
  return t;
}

function genCluster(r) {
  const t = newTarget();
  t.name = 'Gen Cluster';
  t.stretch = F(4.0);
  t.mix = StarlightMix();
  t.asReleased = r.uniform() < F(0.5) ? I.WEBB_DEEP : I.HUBBLE_DEEP;
  t.galaxyScene = true;
  t.deep.on = true;
  t.deep.density = r.range(F(1600.0), F(2800.0));
  t.deep.lens = true;
  t.deep.lensX = r.range(F(-0.25), F(0.25));
  t.deep.lensY = r.range(F(-0.15), F(0.15));
  t.deep.einstein = r.range(F(0.07), F(0.16));
  t.deep.members = r.range(F(100.0), F(220.0));
  t.deep.coreRadius = r.range(F(0.12), F(0.25));
  {
    const re = r.range(F(0.03), F(0.05));
    const axis = r.range(F(0.6), F(0.9));
    const pa = r.range(F(0.0), F(3.14));
    t.galaxies.push(elliptical(t.deep.lensX, t.deep.lensY, re, axis, pa, 0.9));
  }
  fieldStars(t, r, 4, 0.5, 40.0);
  t.fieldStars = 40;
  t.haze = F(0.0);
  t.snX = f32(t.deep.lensX + F(0.2));
  t.snY = t.deep.lensY;
  return t;
}

function genStarfield(r) {
  const t = newTarget();
  t.name = 'Gen Starfield';
  t.mix = StarlightMix();
  t.asReleased = I.BACKYARD;
  if (r.uniform() < F(0.65)) {
    t.band.amount = r.range(F(0.4), F(1.0));
    t.band.angle = r.range(F(-1.2), F(1.2));
    t.band.width = r.range(F(0.25), F(0.5));
    t.band.offset = r.range(F(-0.2), F(0.2));
    for (let i = 0; i < 4; i += 1) {
      const u = r.range(F(-0.9), F(0.9));
      const a = r.range(F(0.15), F(0.4));
      const b = r.range(F(0.02), F(0.06));
      const ang = f32(t.band.angle + r.range(F(-0.3), F(0.3)));
      const den = r.range(F(0.3), F(0.9));
      const e = blob(Channel.Foreground, f32(u * cosf(t.band.angle)), f32(u * sinf(t.band.angle)), a, b, ang, den, 0.1, 0.04, 0.03, 0.8);
      e.extra2 = F(2.0);
      t.elements.push(e);
    }
  }
  t.haze = F(0.0);
  t.warpScale = F(4.0);
  t.warp = F(0.03);
  fieldStars(t, r, 36, 0.4, 12.0);
  t.fieldStars = r.range(F(1000.0), F(2200.0));
  t.dustRatio = F(3.0);
  t.snX = r.range(F(-0.6), F(0.6));
  t.snY = r.range(F(-0.35), F(0.35));
  return t;
}

const BUILDERS = [
  pillars, cliffs, horsehead, orion, crab, ring, southernRing, helix, veil, pleiades,
  andromeda, whirlpool, sombrero, ultraDeep, firstDeep,
  genNebula, genDark, genPlanetary, genRemnant, genSpiral, genCluster, genStarfield,
];

/** Build a target. `seed` places the random stars and, for a generative target, everything else. */
function Build(index, seed) {
  const s = (0x5ee0 + Math.imul(seed >>> 0, 7919) + Math.imul(index >>> 0, 104729)) >>> 0;
  const r = new Random(s);
  return BUILDERS[clamp(index, 0, kTargetCount - 1)](r);
}

//===========================================================================
// Events.h / Events.cpp
//===========================================================================

/** One frame height of the picture, as an angle on the sky: twenty degrees. */
const kFrameAngle = (20.0 * kPi) / 180.0;
/** A meteor's distance from the observer, km. */
const kMeteorRange = 150.0;
/** A shower's speed, km/s: the Perseids'. */
const kShowerSpeed = 59.0;

const newNova = () => ({
  active: false, x: 0, y: 0, days: 0.0, flux: 0, temperature: F(10000.0), remnantRadius: 0, remnantGlow: 0, echoCt: 0,
});

class Events {
  static kPeakFlux = F(600.0);
  static kSedovRadius = F(0.07);
  static kSedovDays = 60.0;
  static kLightPerDay = 0.0035;
  static kFadeDays = 3000.0;

  constructor() {
    this.meteors = [];
    this.launched = [];
    this.nova = newNova();
    this.novaStart = -1.0;
    this.rng = 0x9e3779b97f4a7c15n;
    this.seeded = false;
    this.carry = 0.0;
  }

  uniform() {
    this.rng = (this.rng * MUL64 + INC64) & MASK64;
    return f32(Number((this.rng >> 40n) & 0xffffffn) * f32(1.0 / 16777216.0));
  }

  Reset() {
    this.meteors = [];
    this.launched = [];
    this.nova = newNova();
    this.novaStart = -1.0;
    this.carry = 0.0;
  }

  launch(now, input, pressed) {
    const m = {
      x: 0, y: 0, dx: 1, dy: 0, length: F(0.5), start: now, duration: F(1.0), rate: F(1.0), peak: F(1.0), fast: F(0.5),
      train: 0, trainLife: F(0.5), speed: F(40.0),
    };
    m.x = f32(f32(f32(this.uniform() * F(2.0)) - F(1.0)) * (pressed ? F(0.55) : F(1.0)));
    m.y = f32(f32(f32(this.uniform() * F(2.0)) - F(1.0)) * (pressed ? F(0.32) : F(0.6)));
    const midX = m.x;
    const midY = m.y;
    let sinD = 1.0;
    let spin = F(0.0);
    if (!input.shower) {
      spin = f32(this.uniform() * F(6.2831853));
      sinD = 0.35 + 0.65 * this.uniform();
      m.speed = f32(F(12.0) + f32(F(58.0) * this.uniform()));
    } else {
      m.speed = F(kShowerSpeed);
    }
    const entry = ((pressed ? 15.0 + 20.0 * this.uniform() : 20.0 + 55.0 * this.uniform()) * kPi) / 180.0;
    m.rate = f32((m.speed * Math.sin(entry)) / kScaleHeightKm);
    const span = Math.log(3.0 / kMeteorStartX);
    m.duration = f32(span / m.rate);
    const path = f32(m.speed * m.duration);
    for (let pass = 0; pass < (pressed ? 2 : 1); pass += 1) {
      if (input.shower) {
        const ex = f32(m.x - input.radiantX);
        const ey = f32(m.y - input.radiantY);
        const d = Math.sqrt(ex * ex + ey * ey);
        m.dx = f32(ex / d);
        m.dy = f32(ey / d);
        sinD = Math.sin(min2(d * kFrameAngle, kPi * 0.5));
      } else {
        m.dx = cosf(spin);
        m.dy = sinf(spin);
      }
      m.length = f32((path * sinD) / kMeteorRange / kFrameAngle);
      if (pressed) {
        const half = f32(F(0.5) * min2(m.length, F(0.8)));
        m.x = f32(midX - f32(m.dx * half));
        m.y = f32(midY - f32(m.dy * half));
      }
    }
    m.peak = pressed
      ? f32(F(40.0) * powf(max2(this.uniform(), F(0.01)), F(-0.5)))
      : f32(F(6.0) * powf(max2(this.uniform(), F(0.002)), F(-0.9)));
    m.fast = clamp(f32(f32(m.speed - F(20.0)) / F(40.0)), F(0.0), F(1.0));
    m.train = m.fast > F(0.6) ? f32(F(0.12) * min2(f32(m.peak / F(30.0)), F(1.0))) : F(0.0);
    m.trainLife = f32(F(0.3) + f32(F(1.2) * min2(f32(m.peak / F(60.0)), F(1.0))));
    return m;
  }

  /** `input`: { perMinute, shower, presses, supernova, daysPerSecond, snX, snY, radiantX, radiantY }. */
  Advance(now, dt, input, seed) {
    if (!this.seeded) {
      this.rng = 0x9e3779b97f4a7c15n ^ ((BigInt(seed >>> 0) * 0x2545f4914f6cdd1dn) & MASK64);
      this.seeded = true;
    }

    this.meteors = this.meteors.filter((m) => !(now > m.start + m.duration + 5.0 * m.trainLife + 0.2));

    let arrivals = input.presses;
    if (input.perMinute > 0.0 && dt > 0.0) {
      if (this.carry <= 0.0) this.carry = -Math.log(max2(1.0 - this.uniform(), 1e-7));
      this.carry -= (input.perMinute / 60.0) * dt;
      while (this.carry <= 0.0 && arrivals < 8) {
        arrivals += 1;
        this.carry += -Math.log(max2(1.0 - this.uniform(), 1e-7));
      }
    }
    for (let i = 0; i < arrivals && this.meteors.length < 24; i += 1) {
      this.meteors.push(this.launch(now, input, i < input.presses));
      if (this.launched.length < 4096) this.launched.push(this.meteors[this.meteors.length - 1]);
    }

    if (input.supernova) {
      this.novaStart = now;
      this.nova = newNova();
    }
    if (this.novaStart >= 0.0) {
      const nova = this.nova;
      nova.active = true;
      nova.x = input.snX;
      nova.y = input.snY;
      nova.days = (now - this.novaStart) * input.daysPerSecond;
      nova.flux = f32(Events.kPeakFlux * SupernovaLight(nova.days));
      nova.temperature = f32(SupernovaTemperature(nova.days));
      const age = nova.days / Events.kSedovDays;
      nova.remnantRadius = f32(Events.kSedovRadius * RemnantRadius(age));
      nova.remnantGlow = f32(min2(age, 1.0) * Math.exp(-nova.days / Events.kFadeDays));
      nova.echoCt = f32(max2(nova.days - SupernovaPeakDay(), 0.0) * Events.kLightPerDay);
      if (nova.days > 4.0 * Events.kFadeDays) {
        this.novaStart = -1.0;
        nova.active = false;
      }
    }
  }
}

// ==== END PORT =============================================================

// ==== BEGIN FRAME ==========================================================
// The CPU half of Sidereal.cpp: the constants, InitGL's light curve, and
// ProcessOpenGL up to the GL calls -- the clock, ensureBuffers, rebuildScene,
// rebuildPalette, updateCamera, the events' inputs, and what fieldPass,
// lightPass, spritePass and composite compute -- as `frame()`, which returns
// every table it would upload and every uniform each pass would set, in the
// order the plugin does both. demo/tools/crosscheck/frames.mjs slices from
// BEGIN PORT to END FRAME and holds it to the real plugin class.

/** Host seconds a single frame may advance by. */
const kMaxFrameDelta = 0.1;
/** The drift's wander: how far, in frame heights. */
const kDriftReach = 0.06;
/** The diffraction core: sigma px at 1080 lines = kCorePerMicron x lambda / D. */
const kCorePerMicron = (0.85 * 6.5) / 2.0;
/** Spike length, px at 1080 lines per micron for a 6.5 m mirror. */
const kSpikePerMicron = 30.0;
/** The detector's pixel: no star is drawn narrower than this sigma. */
const kPixelSigma = 0.55;
/** The display's base exposure (Exposure 0 stops). */
const kBaseExposure = 0.35;
/** Stars' flux units against the gas's. */
const kStarGain = 900.0;

const kShipped = {
  ionK: 24.0, hazeIon: 0.15, kO: 3.0, kS: 0.75, kP: 0.5, kV: 0.15, frontHaze: 0.4, relief: 0.012, faceShadow: 0.5, faceGain: 0.8,
  hazeShadow: 0.2, molSkin: 0.025, phaseG: 0.5, selfFrac: 0.25, hazeDust: 0.1,
  emitH: 1.0, emitO: 0.8, emitS: 0.35, emitPah: 0.07, emitDust: 0.8, emitScat: 2.0, emitShock: 1.2, emitSync: 0.8, emitJets: 0.6, emitOxy: 0.9,
  dustFloor: 0.01, dustRef: 1.0, margin: 0.15, fieldScale: 0.5, haloFrac: 0.06, haloWidth: 6.0,
};

// Shaders.h's sizes, which the GLSL's #defines share.
const kMaxElements = 64;
const kMaxPolygon = 1024;
const kMaxSources = 12;
const kMaxGalaxies = 8;
const kMaxSprites = 160;
const kTexelsPerElement = 5;
const kSupernovaSamples = 512;
const kSupernovaDays = 800.0;

/** Sersic's b_n (Ciotti & Bertin 1999's expansion). */
const sersicB = (n) => 2.0 * n - 1.0 / 3.0 + 4.0 / (405.0 * n) + 46.0 / (25515.0 * n * n);

/** The ionising photon fraction of a blackbody, relative to 40 kK's. */
function IonisingShare(kelvin) {
  const share = (t) => PhotonTail(ReducedEnergy(13.598, t)) / 2.4041138;
  return share(kelvin) / share(40000.0);
}

/** The constructor's params[]: every control's default, declared by this plugin or not. */
const kDefaults = Float32Array.from({ length: PT.ABOUT_TEXT }, (_, id) => InfoOf(id).defaultValue);

/** InitGL's one table: the supernova's light curve. */
function SupernovaCurve() {
  const curve = new Float32Array(kSupernovaSamples);
  for (let i = 0; i < kSupernovaSamples; i += 1) curve[i] = SupernovaLight((kSupernovaDays * i) / (kSupernovaSamples - 1));
  return { name: 'supernova', format: 'R32F', width: kSupernovaSamples, height: 1, data: curve, linear: true };
}

/** The angular diameter distances the composite hands the deep field, z = 0 .. 8 by 0.25. */
let daMemo = null;
function DistanceTable() {
  if (!daMemo) daMemo = Float32Array.from({ length: 33 }, (_, i) => AngularDiameterDistance(0.25 * i));
  return daMemo;
}

const lum = (c) => 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];

class SiderealFrames {
  constructor(isEffect) {
    this.isEffect = isEffect;
    this.declared = new Set(HostOrder(isEffect));
    this.params = Float32Array.from(kDefaults);
    this.meteorPresses = 0;
    this.meteorHeld = false;
    this.novaWanted = false;
    this.novaHeld = false;
    this.events = new Events();

    this.target = null;
    this.sceneKey = -1;
    this.paletteScene = -2;
    this.palette = { instrument: -1, filterCount: 0 };
    this.elementCount = 0;
    this.sourceCount = 0;
    this.spriteCount = 0;

    this.hostWidth = 0;
    this.hostHeight = 0;
    this.fieldWidth = 0;
    this.fieldHeight = 0;
    this.fieldExtent = [1.0, 1.0];

    this.cam = Float32Array.of(1, 0, 0, 1);
    this.camPan = new Float32Array(2);
    this.camDrift = new Float32Array(2);
    this.zoom = 1.0;
    this.spin = 0.0;
    this.driftPhase = 0.0;
    this.flowTime = 0.0;
    this.flyPhase = 0.0;
    this.exposure = 1.0;
    this.starGain = 900.0;
    this.continuumNorm = 1.0;
    this.scatterNorm = 1.0;
    this.deepNorm = 1.0;
    this.meteorNorm = 1.0;
    this.coreSigmaPx = 1.0;
    this.seeingSigmaPx = 0.5;
    this.spikeLengthPx = 60.0;

    this.now = 0.0;
    this.lastNow = -1.0;
  }

  /** SetById: SetFloatParameter for a control by what it is. One this plugin does not declare has no host index. */
  set(id, value) {
    if (!this.declared.has(id)) return;
    value = f32(value);
    // Presses act once, on the way down.
    const down = value >= 0.5;
    if (id === PT.FIRE_METEOR) {
      if (down && !this.meteorHeld) this.meteorPresses += 1;
      this.meteorHeld = down;
    } else if (id === PT.SUPERNOVA) {
      if (down && !this.novaHeld) this.novaWanted = true;
      this.novaHeld = down;
    }
    this.params[id] = value;
  }

  ensureBuffers(width, height) {
    const aspect = width / height;
    const m = kShipped.margin;
    this.fieldExtent = [aspect * (1.0 + 2.0 * m), 1.0 + 2.0 * m];
    this.fieldWidth = max2(16, lround(width * (1.0 + (2.0 * m) / aspect) * kShipped.fieldScale));
    this.fieldHeight = max2(16, lround(height * (1.0 + 2.0 * m) * kShipped.fieldScale));
    this.hostWidth = width;
    this.hostHeight = height;
  }

  rebuildScene(uploads) {
    const p = this.params;
    const index = OptionIndex(p[PT.TARGET], TargetCount());
    const seed = lround(f32(clamp(p[PT.SEED], F(0.0), F(1.0)) * F(1000.0))) >>> 0;
    const key = index * 4096 + seed;
    if (key === this.sceneKey) return;
    this.sceneKey = key;
    this.target = Build(index, seed);
    this.events.Reset();

    const t = this.target;
    this.elementCount = min2(t.elements.length, kMaxElements);
    const texels = new Float32Array(max2(this.elementCount, 1) * kTexelsPerElement * 4);
    for (let i = 0; i < this.elementCount; i += 1) {
      const e = t.elements[i];
      texels.set([
        e.shape, e.channel, e.depth, e.polyStart,
        e.x, e.y, e.x2, e.y2,
        e.a, e.b, cosf(e.angle), sinf(e.angle),
        e.density, e.thickness, e.edge, e.rough,
        e.detail, e.extra, e.extra2, e.polyCount,
      ], i * kTexelsPerElement * 4);
    }
    uploads.push({ name: 'elements', format: 'RGBA32F', width: max2(this.elementCount, 1) * kTexelsPerElement, height: 1, data: texels, linear: false });

    let poly = t.polygon.slice(0, min2(t.polygon.length, kMaxPolygon * 2));
    if (poly.length === 0) poly = [0.0, 0.0];
    uploads.push({ name: 'polygon', format: 'RG32F', width: poly.length / 2, height: 1, data: Float32Array.from(poly), linear: false });
  }

  rebuildPalette(instrument, uploads) {
    if (this.palette.instrument === instrument && this.palette.filterCount > 0 && this.paletteScene === this.sceneKey) return;
    this.paletteScene = this.sceneKey;
    const t = this.target;
    const palette = BuildPalette(instrument, t.mix);
    this.palette = palette;
    uploads.push({ name: 'line', format: 'RGB32F', width: kTauSamples, height: kLineComponents, data: palette.lines, linear: true });
    uploads.push({ name: 'continuum', format: 'RGB32F', width: kTauSamples, height: 3 * kTempSamples, data: palette.continua, linear: true });
    uploads.push({ name: 'galaxy', format: 'RGB32F', width: 32, height: kTempSamples, data: palette.galaxies, linear: true });
    const stars = new Float32Array(kTempSamples * 8);
    const inst = InstrumentOf(instrument);
    for (let f = 0; f < palette.filterCount; f += 1) {
      for (let i = 0; i < kTempSamples; i += 1) {
        stars[f * kTempSamples + i] = palette.gain[f] * continuumOf(FilterNamed(inst.filters[f]), 0, TempOfSample(0, i));
      }
    }
    uploads.push({ name: 'star', format: 'R32F', width: kTempSamples, height: 8, data: stars, linear: true });
    const reference = BuildPalette(I.HUBBLE_NARROW, t.mix);
    this.starGain = (kStarGain * lum(DisplayOfContinuum(reference, 0, 5800.0, 0.0))) / max2(lum(DisplayOfContinuum(palette, 0, 5800.0, 0.0)), 1e-30);
    this.continuumNorm = 1.0 / max2(lum(DisplayOfContinuum(palette, 0, 6000.0, 0.0)), 1e-30);
    this.scatterNorm = 1.0 / max2(lum(DisplayOfContinuum(palette, 1, 10000.0, 0.0)), 1e-30);
    this.meteorNorm = 1.0 / max2(lum(DisplayOf(palette, C.METEOR_SLOW, 0.0)), 1e-30);
    {
      const row = SampleOfTemp(0, 6500.0);
      const i = clamp(lround(row), 0, kTempSamples - 1);
      const k = lround((0.5 / 8.0) * 31.0);
      const c = [0, 1, 2].map((j) => palette.galaxies[(i * 32 + k) * 3 + j]);
      this.deepNorm = 1.0 / max2(lum(c), 1e-30);
    }
  }

  /** Screen position (px, GL's y up) of a scene point at a depth, as this frame's camera has it. */
  screenPx(x, y, depth) {
    const k = 1.0 / max2(depth, 0.05) - 1.0;
    const ox = x - this.camPan[0] - this.camDrift[0] * k;
    const oy = y - this.camPan[1] - this.camDrift[1] * k;
    const [a, b, c, d] = this.cam;
    const det = a * d - b * c;
    const sx = (d * ox - c * oy) / det;
    const sy = (-b * ox + a * oy) / det;
    const aspect = this.hostWidth / max2(this.hostHeight, 1);
    return [(sx / aspect + 0.5) * this.hostWidth, (sy + 0.5) * this.hostHeight];
  }

  updateCamera(dt) {
    const p = this.params;
    this.zoom = ZoomFromParam(p[PT.ZOOM]);
    this.spin = SpinFromParam(p[PT.SPIN]);
    const c = Math.cos(this.spin) / this.zoom;
    const s = Math.sin(this.spin) / this.zoom;
    this.cam.set([c, s, -s, c]);
    this.driftPhase += (dt * DriftFromParam(p[PT.DRIFT])) / kDriftReach;
    this.driftPhase %= 2.0 * kPi * 1000.0;
    const ox = kDriftReach * Math.sin(this.driftPhase);
    const oy = 0.6 * kDriftReach * (Math.sin(0.77 * this.driftPhase + 1.3) - Math.sin(1.3));
    const px = PanFromParam(p[PT.PAN_X]) + ox;
    const py = PanFromParam(p[PT.PAN_Y]) + oy;
    this.camPan.set([px, py]);
    const depth = DepthFromParam(p[PT.DEPTH]);
    this.camDrift.set([px * depth * 0.5, py * depth * 0.5]);
    this.flowTime = (this.flowTime + dt * FlowFromParam(p[PT.FLOW])) % 1000.0;
    this.flyPhase = (this.flyPhase + (dt * FlyFromParam(p[PT.FLY])) / 3.88) % 4096.0;
  }

  cameraUniforms() {
    return [['Cam', 'm2', [...this.cam]], ['CamPan', 'f', [...this.camPan]], ['CamDrift', 'f', [...this.camDrift]], ['Hooks', 'i', [0]]];
  }

  clipMap(input) {
    const aspect = this.hostWidth / this.hostHeight;
    const sx = input.width / max2(input.hardwareWidth, 1);
    const sy = input.height / max2(input.hardwareHeight, 1);
    return ['ClipMap', 'f', [sx / aspect, sy, 0.5 * sx, 0.5 * sy]];
  }

  fieldUniforms(input) {
    const p = this.params;
    const t = this.target;
    const clipGas = input !== null && OptionIndex(p[PT.CLIP_AS], ClipAs.Count) === ClipAs.Gas;
    const u = [
      ...this.cameraUniforms(),
      ['Elements', 'i', [0]], ['Poly', 'i', [1]], ['Clip', 'i', [2]],
      ['FieldExtent', 'f', this.fieldExtent],
      ['ElementCount', 'i', [this.elementCount]],
      ['FlowTime', 'f', [this.flowTime]],
      ['Turb', 'f', [TurbulenceFromParam(p[PT.TURBULENCE])]],
      ['WarpScale', 'f', [t.warpScale]],
      ['HazeLevel', 'f', [t.haze]],
      ['HazeScale', 'f', [t.hazeScale]],
      ['HazeMul', 'f', [HazeFromParam(p[PT.HAZE])]],
      ['GasScale', 'f', [GasFromParam(p[PT.GAS])]],
      ['IonK', 'f', [kShipped.ionK]],
      ['ClipGas', 'i', [clipGas ? 1 : 0]],
    ];
    if (input !== null) u.push(this.clipMap(input));
    return u;
  }

  lightUniforms() {
    const p = this.params;
    const t = this.target;
    const turn = LightAngleFromParam(p[PT.LIGHT_ANGLE]);
    const power = StarlightFromParam(p[PT.STARLIGHT]);
    const heat = HeatFromParam(p[PT.STAR_HEAT]);
    this.sourceCount = min2(t.sources.length, kMaxSources);
    const A = new Float32Array(kMaxSources * 4);
    const B = new Float32Array(kMaxSources * 4);
    for (let k = 0; k < this.sourceCount; k += 1) {
      const src = t.sources[k];
      const x = src.x * Math.cos(turn) - src.y * Math.sin(turn);
      const y = src.x * Math.sin(turn) + src.y * Math.cos(turn);
      const px = this.screenPx(x, y, 1.0);
      const sx = ((px[0] / this.hostWidth - 0.5) * this.hostWidth) / this.hostHeight;
      const sy = px[1] / this.hostHeight - 0.5;
      const temperature = src.temperature * heat;
      A[k * 4 + 0] = sx / this.fieldExtent[0] + 0.5;
      A[k * 4 + 1] = sy / this.fieldExtent[1] + 0.5;
      A[k * 4 + 2] = src.h;
      A[k * 4 + 3] = src.luminosity * power * IonisingShare(temperature);
      const hardRef = OxygenHardness(40000.0);
      const q = OxygenHardness(temperature) / hardRef;
      B[k * 4 + 0] = (hardRef * q) / (1.0 + q / 4.0);
      B[k * 4 + 1] = src.luminosity * power;
      B[k * 4 + 2] = temperature;
      B[k * 4 + 3] = src.soft;
    }
    return [
      ...this.cameraUniforms(),
      ['FieldA', 'i', [0]],
      ['FieldExtent', 'f', [this.fieldExtent[0] / this.zoom, this.fieldExtent[1] / this.zoom]],
      ['FieldTexels', 'f', [this.fieldWidth, this.fieldHeight]],
      ['SourceCount', 'i', [this.sourceCount]],
      ['SrcA', 'v4', A],
      ['SrcB', 'v4', B],
      ['Relief', 'f', [kShipped.relief]],
      ['FaceShadow', 'f', [kShipped.faceShadow]],
      ['HazeShade', 'f', [t.hazeShade]],
      ['HazeOpacity', 'f', [kShipped.ionK * kShipped.hazeIon * kShipped.hazeShadow]],
      ['KO', 'f', [kShipped.kO]],
      ['KS', 'f', [kShipped.kS]],
      ['KP', 'f', [kShipped.kP]],
      ['KV', 'f', [kShipped.kV]],
      ['PhaseG', 'f', [kShipped.phaseG]],
    ];
  }

  /** spritePass: the sprite table (uploaded every frame) and, when there is anything to draw, the uniforms. */
  sprites(width, height, uploads) {
    const p = this.params;
    const t = this.target;
    const palette = this.palette;
    const inst = InstrumentOf(palette.instrument);
    const raster = height / 1080.0;
    const last = max2(palette.filterCount - 1, 0);
    const core = (kCorePerMicron / inst.aperture) * raster;
    const seeing = Math.sqrt(Math.pow((inst.seeing / 2.3548 + SeeingFromParam(p[PT.SEEING]) / 2.3548) * raster, 2.0) + kPixelSigma * kPixelSigma);
    this.coreSigmaPx = core;
    this.seeingSigmaPx = seeing;
    this.spikeLengthPx = ((kSpikePerMicron * 6.5) / inst.aperture) * raster;
    const spikes = SpikesFromParam(p[PT.SPIKES]) * (inst.spikes === Spikes.None ? 0.0 : 1.0);
    const sigmaMax = Math.sqrt(Math.pow(core * palette.pivot[last], 2.0) + seeing * seeing);

    const data = [];
    const addStar = (px, py, flux, temperature, layer) => {
      if (data.length / 16 >= kMaxSprites || flux <= 0.0) return;
      const r0 = 2.0 * sigmaMax;
      const reach = spikes > 0.0
        ? min2(r0 * Math.sqrt((flux * this.starGain * spikes * 0.4 * 0.003) / 0.004), 3.0 * this.spikeLengthPx * palette.pivot[last])
        : 0.0;
      const extent = max2(max2(8.0 * sigmaMax, reach), 6.0) + 2.0;
      if (px < -extent || py < -extent || px > width + extent || py > height + extent) return;
      data.push(px, py, flux * this.starGain, temperature, 0, 0, layer, extent, 0, 0, 0, 0, 0, 0, 0, 0);
    };
    const starScale = StarsFromParam(p[PT.STARS]) > 0.0 ? 1.0 : 0.0;
    for (const s of t.stars) {
      const at = this.screenPx(s.x, s.y, s.depth);
      addStar(at[0], at[1], s.flux * starScale, s.temperature, s.layer);
    }
    for (const src of t.sources) {
      if (!src.visible) continue;
      const at = this.screenPx(src.x, src.y, 1.0);
      addStar(at[0], at[1], 0.6, src.temperature, 0);
    }
    const nova = this.events.nova;
    if (nova.active && nova.flux > 0.0) {
      const at = this.screenPx(nova.x, nova.y, 1.0);
      addStar(at[0], at[1], nova.flux, nova.temperature, 1);
    }
    // Meteors live in the air, on the screen: no camera, no parallax.
    const aspect = width / height;
    for (const m of this.events.meteors) {
      if (data.length / 16 >= kMaxSprites) break;
      const elapsed = (this.now - m.start) / m.duration;
      if (elapsed < 0.0) continue;
      const sig = max2(1.2 * raster, kPixelSigma);
      const x0 = kMeteorStartX;
      data.push(
        (m.x / aspect + 0.5) * width, (m.y + 0.5) * height, m.dx, m.dy,
        1.0, f32(m.length * height), elapsed, 6.0 * sig + 2.0,
        Math.log(3.0 / x0), x0, m.peak, m.fast,
        m.train, f32(m.trainLife / m.duration), 0.12 / m.duration, sig,
      );
    }
    this.spriteCount = data.length / 16;
    if (data.length === 0) data.push(...new Array(16).fill(0.0));
    uploads.push({ name: 'sprites', format: 'RGBA32F', width: data.length / 4, height: 1, data: Float32Array.from(data), linear: false });

    if (this.spriteCount === 0) return null;
    const colours = new Float32Array(24);
    const ext = new Float32Array(8);
    const scale = new Float32Array(8);
    for (let f = 0; f < palette.filterCount; f += 1) {
      for (let c = 0; c < 3; c += 1) colours[f * 3 + c] = palette.colour[f][c];
      ext[f] = palette.ext[f];
      scale[f] = palette.pivot[f];
    }
    // The spike pattern, fixed to the telescope (the camera's spin turns the sky, not the spider).
    const dirs = new Float32Array(32);
    let count = 0;
    const base = (inst.spikeAngle * kPi) / 180.0;
    const addSpike = (angle, weight) => {
      dirs[count * 4 + 0] = Math.cos(angle);
      dirs[count * 4 + 1] = Math.sin(angle);
      dirs[count * 4 + 2] = weight;
      count += 1;
    };
    if (inst.spikes === Spikes.Four) {
      for (let k = 0; k < 4; k += 1) addSpike(base + (k * kPi) / 2.0, 1.0);
    } else if (inst.spikes === Spikes.Webb) {
      for (let k = 0; k < 6; k += 1) addSpike(base + kPi / 2.0 + (k * kPi) / 3.0, 1.0);
      addSpike(base, 0.35);
      addSpike(base + kPi, 0.35);
    }
    const fx = aspect / this.fieldExtent[0] / width;
    const fy = 1.0 / this.fieldExtent[1] / height;
    return [
      ...this.cameraUniforms(),
      ['SpriteData', 'i', [0]], ['FieldA', 'i', [1]], ['StarLut', 'i', [2]], ['LineLut', 'i', [3]],
      ['Raster', 'f', [width, height]],
      ['PxToField', 'f', [fx, fy, 0.5 - (0.5 * aspect) / this.fieldExtent[0], 0.5 - 0.5 / this.fieldExtent[1]]],
      ['DustK', 'f', [t.dustRatio * DustFromParam(p[PT.DUST])]],
      ['FilterCount', 'i', [palette.filterCount]],
      ['FilterColour', 'v3', colours],
      ['FilterExt', 'v1', ext],
      ['FilterScale', 'v1', scale],
      ['CoreSigma', 'f', [core]],
      ['SeeingSigma', 'f', [seeing]],
      ['HaloFrac', 'f', [kShipped.haloFrac]],
      ['HaloWidth', 'f', [kShipped.haloWidth]],
      ['SpikeCount', 'i', [count]],
      ['SpikeDir', 'v4', dirs],
      ['SpikeAmp', 'f', [0.4 * spikes]],
      ['SpikeLen', 'f', [this.spikeLengthPx]],
      ['WakeGain', 'f', [0.25]],
      ['MeteorGain', 'f', [1.2 * this.meteorNorm]],
    ];
  }

  compositeUniforms(input, width, height) {
    const p = this.params;
    const t = this.target;
    const palette = this.palette;
    const names = ['FieldA', 'FieldB', 'Light0', 'Light1', 'Light2', 'Light3', 'Sprites', 'LineLut', 'ContLut', 'GalLut', 'SnLut', 'Clip'];
    const u = [...this.cameraUniforms(), ...names.map((name, i) => [name, 'i', [i]])];
    const aspect = width / height;
    const raster = height / 1080.0;
    const dustK = t.dustRatio * DustFromParam(p[PT.DUST]);
    u.push(
      ['Aspect', 'f', [aspect]],
      ['FieldMap', 'f', [1.0 / this.fieldExtent[0], 1.0 / this.fieldExtent[1], 0.5, 0.5]],
      ['DustK', 'f', [dustK]],
      ['IonK', 'f', [kShipped.ionK]],
      ['HazeIon', 'f', [kShipped.hazeIon]],
      ['FrontHaze', 'f', [kShipped.frontHaze]],
      ['FaceGain', 'f', [kShipped.faceGain]],
      ['MolSkin', 'f', [kShipped.molSkin]],
      ['HardRef', 'f', [OxygenHardness(40000.0)]],
      ['SelfFrac', 'f', [kShipped.selfFrac]],
      ['HazeDust', 'f', [kShipped.hazeDust]],
      ['EmitA', 'f', [kShipped.emitH, kShipped.emitO, kShipped.emitS, kShipped.emitPah]],
      ['EmitB', 'f', [kShipped.emitDust, kShipped.emitScat, kShipped.emitShock, kShipped.emitSync]],
      ['EmitC', 'f', [kShipped.emitJets, kShipped.emitOxy, kShipped.dustFloor, kShipped.dustRef]],
    );

    // The faint stars.
    const stars = StarsFromParam(p[PT.STARS]);
    const inst = InstrumentOf(palette.instrument);
    let meanPivot = 0.0;
    for (let f = 0; f < palette.filterCount; f += 1) meanPivot += palette.pivot[f] / palette.filterCount;
    const starSigma = Math.sqrt(Math.pow((kCorePerMicron / inst.aperture) * raster * meanPivot, 2.0) + this.seeingSigmaPx * this.seeingSigmaPx);
    u.push(
      ['StarDensity', 'f', [t.fieldStars * stars]],
      ['EmbeddedDensity', 'f', [t.embedded * stars]],
      ['StarFluxMin', 'f', [0.15 * this.starGain]],
      ['StarFluxMax', 'f', [4.0 * this.starGain]],
      ['StarSigma', 'f', [starSigma]],
      ['SceneToPx', 'f', [height * this.zoom]],
      ['ScreenToPx', 'f', [height]],
      ['StarSalt', 'u', [(7919 + Math.imul(this.sceneKey & 0xffff, 31)) >>> 0]],
      ['FlySpeed', 'f', [FlyFromParam(p[PT.FLY])]],
      ['FlyPhase', 'f', [this.flyPhase]],
      ['Band', 'f', [t.band.amount, cosf(t.band.angle), sinf(t.band.angle), t.band.width]],
      ['BandOffset', 'f', [t.band.offset]],
    );

    // The supernova.
    const nova = this.events.nova;
    u.push(
      ['SnA', 'f', [nova.x, nova.y, nova.days, nova.active ? 1.0 : 0.0]],
      ['SnB', 'f', [nova.remnantRadius, nova.remnantGlow, t.echoDistance, EchoFromParam(p[PT.ECHO])]],
      ['SnC', 'f', [Events.kLightPerDay, Events.kPeakFlux, 9500.0, 0.0]],
      ['RemnantGain', 'f', [2.5]],
      ['EchoGain', 'f', [0.01 * this.scatterNorm]],
      ['BandGain', 'f', [0.4 * this.continuumNorm]],
      ['ScatterGain', 'f', [0.12 * this.scatterNorm]],
    );

    // The galaxies.
    const galaxies = min2(t.galaxies.length, kMaxGalaxies);
    const gal = new Float32Array(kMaxGalaxies * 24);
    for (let i = 0; i < galaxies; i += 1) {
      const g = t.galaxies[i];
      // std::cos of a float is cosf: these two are floats, then doubles.
      const ci = cosf(g.incline);
      const si = sinf(g.incline);
      const n = g.sersic;
      const b = sersicB(n);
      const q = g.kind === 1 ? g.bulgeAxis : Math.sqrt(ci * ci + f32(g.bulgeAxis * g.bulgeAxis) * si * si);
      const bulgeTotal = 2.0 * kPi * n * g.bulgeRe * g.bulgeRe * q * Math.exp(b) * Math.pow(b, -2.0 * n) * tgamma(2.0 * n);
      const ib = f32(g.brightness * g.bulge) / max2(bulgeTotal, 1e-12);
      const id = g.kind === 1 ? 0.0 : (g.brightness * (1.0 - g.bulge)) / (2.0 * kPi * g.scale * g.scale);
      const phase = g.phase + this.flowTime * 0.6;
      gal.set([
        g.x, g.y, g.scale, g.kind,
        ci, si, cosf(g.pa), sinf(g.pa),
        g.arms, tanf(g.pitch), g.armContrast, phase,
        ib, g.bulgeRe, n, b,
        q, g.dust, g.dustRing, g.hii,
        g.diskT, g.bulgeT, g.armT, id,
      ], i * 24);
    }
    const deep = t.deep;
    u.push(
      ['GalaxyCount', 'i', [galaxies]],
      ['Gal', 'v4', gal],
      ['GalaxyGain', 'f', [0.25 * this.continuumNorm]],
      ['DeepOn', 'i', [deep.on ? 1 : 0]],
      ['LensOn', 'i', [deep.lens ? 1 : 0]],
      ['DeepA', 'f', [deep.density, deep.lensX, deep.lensY, deep.einstein]],
      ['DeepB', 'f', [deep.zLens, deep.members, deep.coreRadius, F(6.98e-4)]],
      ['DA', 'v1', DistanceTable()],
      ['DeepGain', 'f', [0.12 * this.deepNorm]],
    );

    // The display.
    this.exposure = kBaseExposure * Math.pow(2.0, ExposureFromParam(p[PT.EXPOSURE]) + t.exposure + InstrumentOf(palette.instrument).exposure);
    const ext = [0, 0, 0];
    const weight = [0, 0, 0];
    for (let f = 0; f < palette.filterCount; f += 1) {
      for (let c = 0; c < 3; c += 1) {
        ext[c] += palette.colour[f][c] * palette.ext[f];
        weight[c] += palette.colour[f][c];
      }
    }
    u.push(
      ['Exposure', 'f', [this.exposure]],
      ['StretchQ', 'f', [StretchFromParam(p[PT.STRETCH]) * t.stretch]],
      ['Saturation', 'f', [SaturationFromParam(p[PT.SATURATION])]],
      ['Effect', 'i', [input !== null ? 1 : 0]],
      ['ClipMode', 'i', [OptionIndex(p[PT.CLIP_AS], ClipAs.Count)]],
      ['Mix', 'f', [p[PT.MIX]]],
      ['ClipExt', 'f', [0, 1, 2].map((c) => ext[c] / max2(weight[c], 1e-9))],
    );
    if (input !== null) u.push(this.clipMap(input));
    return u;
  }

  /**
   * One ProcessOpenGL, up to the GL calls. `now` is the host's time in
   * seconds; `input` is null for the source, or the clip's { width, height,
   * hardwareWidth, hardwareHeight } for the Over, whose raster it sets.
   */
  frame(now, hostWidth, hostHeight, input) {
    const width = input !== null ? input.width : hostWidth;
    const height = input !== null ? input.height : hostHeight;
    const uploads = [];

    this.now = now;
    const dt = this.lastNow >= 0.0 ? clamp(this.now - this.lastNow, 0.0, kMaxFrameDelta) : 0.0;
    this.lastNow = this.now;
    this.frameDt = dt;

    this.ensureBuffers(width, height);
    this.rebuildScene(uploads);
    const telescope = OptionIndex(this.params[PT.TELESCOPE], 1 + kTelescopeChoices);
    this.rebuildPalette(telescope === 0 ? this.target.asReleased : telescope - 1, uploads);
    this.updateCamera(dt);

    const p = this.params;
    const events = {
      perMinute: MeteorsFromParam(p[PT.METEORS]),
      shower: OptionIndex(p[PT.METEOR_KIND], MeteorKind.Count) === MeteorKind.Shower,
      presses: this.meteorPresses,
      supernova: this.novaWanted,
      daysPerSecond: SupernovaSpeedFromParam(p[PT.SN_SPEED]),
      snX: this.target.snX,
      snY: this.target.snY,
      radiantX: F(-1.3),
      radiantY: F(0.9),
    };
    this.meteorPresses = 0;
    this.novaWanted = false;
    this.events.Advance(this.now, dt, events, this.sceneKey >>> 0);

    const field = this.fieldUniforms(input);
    const light = this.lightUniforms();
    const sprite = this.sprites(width, height, uploads);
    const composite = this.compositeUniforms(input, width, height);
    return {
      uploads, width, height, fieldWidth: this.fieldWidth, fieldHeight: this.fieldHeight, spriteCount: this.spriteCount,
      passes: { field, light, sprite, composite },
    };
  }
}

// ==== END FRAME ============================================================

//===========================================================================
// The GL half: the plugin's textures, buffers and draws, with the uploads
// and uniforms the FRAME block returned.
//===========================================================================

/** What the page has spent building tables, for the line under the picture. */
const tableTime = { ms: 0, builds: 0 };

function createRenderer(gl, quad) {
  // InitGL: the quad's vertex stage is kVersion + kQuadVertex; every other
  // stage is Assemble( pass ) = kVersion + kCommon + the pass's pieces, the
  // sprite pass's vertex stage included.
  const quadVertex = S.VERSION + S.QUAD_VERTEX;
  const head = S.VERSION + S.COMMON;
  const programs = {
    field: new Program(gl, quadVertex, head + S.FIELD_FRAGMENT, 'field'),
    light: new Program(gl, quadVertex, head + S.LIGHT_FRAGMENT, 'light'),
    sprite: new Program(gl, head + S.SPRITE_VERTEX, head + S.SPRITE_FRAGMENT, 'sprite'),
    composite: new Program(gl, quadVertex, head + S.COMPOSITE_SKY + S.COMPOSITE_GALAXIES + S.COMPOSITE_MAIN, 'composite'),
  };

  // uploadTable(). The palette's tables and the light curve are filtered
  // (GL_LINEAR) 32-bit floats in the plugin, and a linearly filtered float
  // texture needs OES_texture_float_linear in WebGL2. Without it those five
  // fall back to half float, and the line under the picture says so.
  const floatLinear = !!gl.getExtension('OES_texture_float_linear');
  const FORMATS = {
    RGBA32F: [gl.RGBA32F, gl.RGBA32F, gl.RGBA],
    RGB32F: [gl.RGB32F, gl.RGB16F, gl.RGB],
    RG32F: [gl.RG32F, gl.RG16F, gl.RG],
    R32F: [gl.R32F, gl.R16F, gl.RED],
  };
  const textures = {};
  function upload(u) {
    const [full, half, layout] = FORMATS[u.format];
    const internal = u.linear && !floatLinear ? half : full;
    if (!textures[u.name]) textures[u.name] = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, textures[u.name]);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
    gl.texImage2D(gl.TEXTURE_2D, 0, internal, u.width, u.height, 0, layout, gl.FLOAT, u.data);
    const filter = u.linear ? gl.LINEAR : gl.NEAREST;
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, filter);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, filter);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.bindTexture(gl.TEXTURE_2D, null);
  }
  upload(SupernovaCurve());

  // ensureBuffers(): the field (A mipmapped), the light, the sprites.
  const fieldA = new PassBuffer(gl, { filter: 'linear', mip: true });
  const fieldB = new PassBuffer(gl, { filter: 'linear' });
  const lights = [0, 1, 2, 3].map(() => new PassBuffer(gl, { filter: 'linear' }));
  const sprites = new PassBuffer(gl, { filter: 'nearest' });
  // The plugin's one MRT framebuffer, re-pointed per pass; here one each.
  const fieldFbo = gl.createFramebuffer();
  const lightFbo = gl.createFramebuffer();
  // The sprites are drawn from gl_VertexID alone.
  const spriteArray = gl.createVertexArray();

  const attach = (fbo, buffers) => {
    gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
    buffers.forEach((b, i) => gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0 + i, gl.TEXTURE_2D, b.texture, 0));
    gl.drawBuffers(buffers.map((_, i) => gl.COLOR_ATTACHMENT0 + i));
    gl.viewport(0, 0, buffers[0].width, buffers[0].height);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);
  };

  const apply = (program, uniforms) => {
    for (const [name, kind, v] of uniforms) {
      const loc = program.location(name);
      if (loc === null) {
        program.missing.add(name);
        continue;
      }
      if (kind === 'i') gl.uniform1i(loc, v[0]);
      else if (kind === 'u') gl.uniform1ui(loc, v[0]);
      else if (kind === 'm2') gl.uniformMatrix2fv(loc, false, v);
      else if (kind === 'v1') gl.uniform1fv(loc, v);
      else if (kind === 'v3') gl.uniform3fv(loc, v);
      else if (kind === 'v4') gl.uniform4fv(loc, v);
      else if (v.length === 1) gl.uniform1f(loc, v[0]);
      else if (v.length === 2) gl.uniform2f(loc, v[0], v[1]);
      else if (v.length === 3) gl.uniform3f(loc, v[0], v[1], v[2]);
      else gl.uniform4f(loc, v[0], v[1], v[2], v[3]);
    }
  };
  const unbind = (count) => {
    for (let unit = count - 1; unit >= 0; unit -= 1) bindTexture(gl, unit, null);
    gl.activeTexture(gl.TEXTURE0);
  };

  let instance = null;
  let instanceVariant = null;
  let stats = null;

  /**
   * One ProcessOpenGL. `input` is the kit's clip (premultiplied, 8-bit, as
   * Resolume hands an effect its input) and is read only by the Over.
   */
  function processFrame(st, input, width, height, now, target = null) {
    const clip = st.isEffect ? { width: input.width, height: input.height, hardwareWidth: input.width, hardwareHeight: input.height } : null;
    const t0 = performance.now();
    const fr = st.frame(now, width, height, clip);
    if (fr.uploads.some((u) => u.name === 'line')) {
      tableTime.ms = performance.now() - t0;
      tableTime.builds += 1;
    }
    const { passes } = fr;
    gl.disable(gl.BLEND);

    fieldA.ensure(fr.fieldWidth, fr.fieldHeight, gl.RGBA16F);
    fieldB.ensure(fr.fieldWidth, fr.fieldHeight, gl.RGBA16F);
    for (const b of lights) b.ensure(fr.fieldWidth, fr.fieldHeight, gl.RGBA16F);
    sprites.ensure(fr.width, fr.height, gl.RGBA16F);
    for (const u of fr.uploads) upload(u);
    const clipTexture = st.isEffect ? input.texture : textures.supernova;

    // 1. The field.
    attach(fieldFbo, [fieldA, fieldB]);
    {
      const p = programs.field.use();
      bindTexture(gl, 0, textures.elements);
      bindTexture(gl, 1, textures.polygon);
      bindTexture(gl, 2, clipTexture);
      apply(p, passes.field);
      quad.draw();
      unbind(3);
    }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    fieldA.generateMipmap();

    // 2. The light.
    attach(lightFbo, lights);
    {
      const p = programs.light.use();
      bindTexture(gl, 0, fieldA.texture);
      apply(p, passes.light);
      quad.draw();
      unbind(1);
    }

    // 3. The sprites.
    sprites.clearTo(0, 0, 0, 0);
    if (passes.sprite) {
      const p = programs.sprite.use();
      bindTexture(gl, 0, textures.sprites);
      bindTexture(gl, 1, fieldA.texture);
      bindTexture(gl, 2, textures.star);
      bindTexture(gl, 3, textures.line);
      apply(p, passes.sprite);
      gl.enable(gl.BLEND);
      gl.blendFunc(gl.ONE, gl.ONE);
      gl.bindVertexArray(spriteArray);
      gl.drawArrays(gl.TRIANGLES, 0, 6 * fr.spriteCount);
      gl.bindVertexArray(null);
      gl.disable(gl.BLEND);
      unbind(4);
    }

    // 4. The composite, into the host's frame.
    gl.bindFramebuffer(gl.FRAMEBUFFER, target);
    gl.viewport(0, 0, fr.width, fr.height);
    {
      const p = programs.composite.use();
      const units = [fieldA.texture, fieldB.texture, ...lights.map((b) => b.texture), sprites.texture,
        textures.line, textures.continuum, textures.galaxy, textures.supernova, clipTexture];
      units.forEach((t, i) => bindTexture(gl, i, t));
      apply(p, passes.composite);
      quad.draw();
      unbind(12);
    }
    return fr;
  }

  return {
    get stats() { return stats; },
    get programs() { return programs; },
    floatLinear,
    processFrame,
    newInstance: (isEffect) => new SiderealFrames(isEffect),

    render({ input, params, width, height, time, variant }) {
      const isEffect = variant === 'over';
      if (!instance || instanceVariant !== variant) {
        instance = new SiderealFrames(isEffect);
        instanceVariant = variant;
      }
      const st = instance;
      // SetFloatParameter, for every control this plugin declares.
      for (const p of PARAMS) st.set(p.slot, params.get(p.id));
      const fr = processFrame(st, input, width, height, time);

      // A host lets go of an event; the kit has none, so the page does.
      for (const id of ['fireMeteor', 'supernova']) if (params.get(id) > 0.5) params.set(id, 0);

      const nova = st.events.nova;
      stats = {
        target: st.target.name,
        instrument: st.palette.instrument,
        asReleased: OptionIndex(st.params[PT.TELESCOPE], 1 + kTelescopeChoices) === 0,
        elements: st.elementCount,
        sources: st.sourceCount,
        sprites: fr.spriteCount,
        meteors: st.events.meteors.length,
        nova: nova.active ? nova.days : null,
        field: `${fr.fieldWidth} × ${fr.fieldHeight}`,
      };
    },
  };
}

//===========================================================================
// The parameters: the constructor's, in HostOrder()'s order and groups, with
// the plugin's names, defaults and option elements -- all read from the
// PORT block's InfoOf() and OptionName(), which the cross-check holds to
// Controls.cpp. `only` says which plugin declares it.
//===========================================================================

const fix = (n, d = 2) => n.toFixed(d);
const times = (n) => `×${n >= 10 ? n.toFixed(1) : n >= 1 ? n.toFixed(2) : n.toFixed(3)}`;
const signed = (n, d = 2) => `${n >= 0 ? '+' : '−'}${Math.abs(n).toFixed(d)}`;
const degrees = (radians) => `${signed((radians * 180) / kPi, 0)}°`;
const V = (v) => f32(v);

/** id, ParamId, display, hint. Names, groups, types, defaults and elements come from the port. */
const SPEC = [
  ['target', PT.TARGET, null, 'What is in the sky. Fifteen pictures traced from the releases (the Pillars of Creation, the Cosmic Cliffs, the Horsehead, Orion, the Crab, the Ring, the Southern Ring, the Helix, the Veil, the Pleiades, Andromeda, the Whirlpool, the Sombrero, the Ultra Deep Field and Webb’s First Deep Field) and seven generative skies built from the Seed.'],
  ['telescope', PT.TELESCOPE, null, 'Whose filters and colour key. As Released is each target’s own release; the others re-shoot it through Hubble’s narrow bands, its broad colours, its infrared, Webb’s NIRCam, MIRI or deep-field set, or a backyard colour camera on a small refractor.'],
  ['seed', PT.SEED, (v) => `seed ${lround(f32(clamp(V(v), F(0.0), F(1.0)) * F(1000.0)))}`, 'Places the bright field stars on every target and builds everything on a generative one: 1,001 seeds.'],
  ['gas', PT.GAS, (v) => times(GasFromParam(V(v))), 'The dense gas, 0.25 to 4 times the target’s own (0.5 is the target as drawn).'],
  ['turbulence', PT.TURBULENCE, (v) => times(TurbulenceFromParam(V(v))), 'How far the turbulence moves the gas’s edges and varies its density, 0 to 3 times the target’s own.'],
  ['flow', PT.FLOW, (v) => `${fix(FlowFromParam(V(v)), 3)} h/s`, 'How fast the turbulence evolves, 0 to 0.12 frame heights a second.'],
  ['haze', PT.HAZE, (v) => times(HazeFromParam(V(v))), 'The ionised interior, thin and everywhere, 0 to 3 times the target’s own.'],
  ['dust', PT.DUST, (v) => times(DustFromParam(V(v))), 'How much the dust dims and reddens what is behind it, 0.1 to 10 times per unit of gas.'],
  ['starlight', PT.STARLIGHT, (v) => times(StarlightFromParam(V(v))), 'The stars’ ionising output, 0.1 to 10 times: the fronts move out into the gas or back towards the star.'],
  ['starHeat', PT.STAR_HEAT, (v) => times(HeatFromParam(V(v))), 'The stars’ temperature, 0.6 to 1.6 times. Hotter stars make more of the hard photons that light [O III].'],
  ['lightAngle', PT.LIGHT_ANGLE, (v) => degrees(LightAngleFromParam(V(v))), 'Swing the stars that light the gas round the middle of the frame, ±180°.'],
  ['stars', PT.STARS, (v) => times(StarsFromParam(V(v))), 'The faint field’s density, 0 to 3 times the target’s. At 0 the bright stars go too.'],
  ['spikes', PT.SPIKES, (v) => times(SpikesFromParam(V(v))), 'The diffraction spikes, 0 to 3 times the telescope’s own: Hubble’s four, Webb’s six and two, none on a refractor.'],
  ['seeing', PT.SEEING, (v) => `${fix(SeeingFromParam(V(v)), 1)} px`, 'Extra blur, 0 to 8 reference pixels FWHM.'],
  ['zoom', PT.ZOOM, (v) => times(ZoomFromParam(V(v))), '0.25 to 4 times.'],
  ['panX', PT.PAN_X, (v) => signed(PanFromParam(V(v))), 'Across, −1 to +1 frame heights.'],
  ['panY', PT.PAN_Y, (v) => signed(PanFromParam(V(v))), 'Up and down, −1 to +1 frame heights.'],
  ['spin', PT.SPIN, (v) => degrees(SpinFromParam(V(v))), 'Turns the sky, ±180°. The spikes stay with the telescope.'],
  ['drift', PT.DRIFT, (v) => `${fix(DriftFromParam(V(v)), 3)} h/s`, 'A slow wander, 0 to 0.05 frame heights a second.'],
  ['depth', PT.DEPTH, (v) => fix(DepthFromParam(V(v))), 'The parallax between depths, 0 to 2: as the camera moves, near stars and gas move more than far.'],
  ['fly', PT.FLY, (v) => (FlyFromParam(V(v)) === 0 ? 'still' : `${fix(FlyFromParam(V(v)))} /s`), 'Forward through the stars, 0 to 1.5 star depths a second.'],
  ['meteors', PT.METEORS, (v) => (MeteorsFromParam(V(v)) === 0 ? 'none' : `${fix(MeteorsFromParam(V(v)), 1)} /min`), 'Meteors a minute: none at 0, then 0.5 to 120. The telescope is on a mountain for as long as one lasts.'],
  ['meteorKind', PT.METEOR_KIND, null, 'Sporadic meteors come from anywhere; a Shower’s all point back to one radiant.'],
  ['fireMeteor', PT.FIRE_METEOR, null, 'One meteor now, a fireball on a shallow path. FF_TYPE_EVENT in the plugin; a toggle the page releases after one frame here.'],
  ['supernova', PT.SUPERNOVA, null, 'A type Ia goes off at the target’s chosen place: it rises over two and a half weeks, fades on nickel and cobalt, leaves a remnant and a light echo. FF_TYPE_EVENT in the plugin; a toggle the page releases after one frame here.'],
  ['snSpeed', PT.SN_SPEED, (v) => `${fix(SupernovaSpeedFromParam(V(v)), 1)} d/s`, 'Days of the light curve a second, 2 to 200.'],
  ['echo', PT.ECHO, (v) => fix(EchoFromParam(V(v))), 'The light echo off the dust in front of the supernova, 0 to 1.'],
  ['exposure', PT.EXPOSURE, (v) => `${signed(ExposureFromParam(V(v)), 1)} stops`, '±4 stops.'],
  ['stretch', PT.STRETCH, (v) => (StretchFromParam(V(v)) === 0 ? 'linear' : `Q ${fix(StretchFromParam(V(v)), 1)}`), 'Lupton’s asinh stretch, applied to every channel so a star’s colour survives its core: 0 is linear, then Q 0.3 to 40.'],
  ['saturation', PT.SATURATION, (v) => fix(SaturationFromParam(V(v))), '0 to 2.'],
  ['clipAs', PT.CLIP_AS, null, 'Behind: your clip is what lies behind the nebula, dimmed and reddened by its dust. Gas: your clip’s brightness is the gas, and the stars light it.', 'over'],
  ['mix', PT.MIX, (v) => fix(V(v)), 'The whole effect against the clip. At 0 the clip comes back as it went in.', 'over'],
];

const query = new URLSearchParams(window.location.search);
const initialVariant = query.get('plugin') === 'over' ? 'over' : 'source';

const PARAMS = SPEC.map(([id, slot, display, hint, only]) => {
  if (!HostOrder(true).includes(slot)) throw new Error(`${id}: not a declared parameter`);
  const info = InfoOf(slot);
  const p = { id, slot, name: info.name, group: info.group, hint, only, default: info.defaultValue };
  if (info.kind === Kind.Option) {
    Object.assign(p, { type: 'option', elements: Array.from({ length: info.optionCount }, (_, i) => OptionName(slot, i)) });
  } else if (info.kind === Kind.Event) {
    p.type = 'boolean';
  } else {
    Object.assign(p, { type: 'standard', display });
  }
  return p;
});
if (PARAMS.map((p) => p.slot).join() !== HostOrder(true).join()) throw new Error('the page’s controls are not the plugin’s host order');

let renderer = null;

const mounted = mountDemo({
  name: 'Sidereal',
  pluginId: 'SD01 · SD02',
  kind: ['source', 'effect'],
  tagline:
    'The sky as the space telescopes photograph it. Each Hubble or Webb release is a stack of single-filter grey images, each stretched to its own range, given a hue and summed; Sidereal computes what each filter would record from gas lit by its stars — every ionising photon a recombination, [O III] nearest the star, then hydrogen, [S II] at the front, the aromatics beyond it, dust that dims and reddens — and applies the release’s own colour key, with the telescope’s diffraction spikes. Fifteen famous targets traced from the releases, seven generative skies, meteors and a type Ia supernova. SW Sidereal is the sky on black; SW Sidereal Over puts it over your clip, or makes your clip the gas.',
  repo: 'https://github.com/stoatworks-labs/sidereal',
  page: 'https://stoatworks-labs.com/software/sidereal/',
  video: 'https://www.youtube.com/watch?v=jBJAr09OeGM',

  blurb:
    'It is Sidereal’s own four GLSL passes, ported from the repository to WebGL2 unedited, fed by a JavaScript port of the plugin’s CPU half — the telescopes’ filter tables, every target, the meteors, the supernova’s light curve and every control’s mapping — which a check in the repository holds to the plugin’s C++, uniform for uniform and table for table. The plugin has no audio input, so nothing is left out for want of one. SW Sidereal reads no video; SW Sidereal Over runs on generated clips or your own.',

  showBackdrop: true,
  needFloat: true,

  variants: {
    label: 'Plugin',
    default: initialVariant,
    options: [
      { id: 'source', name: 'SW Sidereal (source)', hint: 'SD01, FF_SOURCE: a piece of sky on black. Reads no clip.' },
      { id: 'over', name: 'SW Sidereal Over (effect)', hint: 'SD02, FF_EFFECT: the sky over your clip (Clip As Behind), or your clip made into the gas (Clip As Gas).' },
    ],
  },

  // For the Over only. Lights on black and the scene show Clip As Gas best
  // (the stars light what is bright); the geometry card shows Behind's
  // dimming under the dust.
  sources: ['scene', 'spot', 'grid', 'bars', 'ramp', 'detail', 'alpha'],

  params: PARAMS,

  differences: [
    'The CPU half is a PORT, not the plugin’s code. The plugin builds everything its shaders read on the CPU, in double: the telescope’s tables (Physics.cpp: every filter’s response to each set of lines and each continuum, by Simpson’s rule, the gains each target is stretched by, the colour keys, extinction through each filter), every target (Scene.cpp: the outlines traced from the releases, the stars and galaxies, and the seeded generator behind the generative skies), the meteors and the supernova (Events.cpp, and Physics.cpp’s light curve), and each frame’s camera, light sources, sprites and uniforms (Sidereal.cpp). Here all of that is JavaScript, whose numbers are doubles too; the two libm functions JavaScript lacks are fdlibm’s erfc and a Lanczos gamma. A check in the repository runs the port and the plugin’s own C++ side by side and compares what they print: every control’s mapping at every slider position, every filter’s response, all forty-eight palettes (twelve telescopes by four kinds of target, every table float), every target at ten seeds and the generative ones at every third, and six runs of meteors and supernovae. Of 123,432 lines, 122,418 are identical to the bit, every float the GPU reads from a palette is identical, and the rest differ in the last place or two of a libm result (V8’s Math is not Apple’s libm). The same check drives the real plugin class through 4,430 frames of both plugins in a GL context, records every table it hands to glTexImage2D and reads every uniform back out of its four shader programs after every frame: every uniform this page sets is the same float; the palette tables, the outlines and the light curve it uploads are identical, and of the two million values in its element and sprite tables 68 differ, each by one unit in the last place. It does not check this page’s GL calls (the binding, the blending, the buffers), which only a reader checks. Frames drawn here were compared once with the plugin’s own harness at the same settings (180 frames from a standing start, eight targets and telescopes at 1280 × 720 and the Over both ways at 640 × 360): 99.86% to 99.99% of pixels identical, every other one within one level of 255.',
    'The GPU half is not a port: the field, light, sprite and composite passes are the plugin’s own GLSL, on the same half-float buffers (WebGL2 needs EXT_color_buffer_float to render into them, and the page stops and says so without it), the field mipmapped and read at the level each step of the light’s march spans, as in the plugin. demo/tools/check_shaders.py fails the repository’s verify script if a character of them, or the way they are joined, drifts.',
    'Precision: GLSL ES 3.00 is not GLSL 4.10. The kit declares highp everywhere it can, but the sprite pass reads the field in its VERTEX shader, where GLSL ES’s default sampler precision is lowp and the kit does not override it; desktop drivers ignore that, a phone’s may not. The plugin’s palette tables and the supernova’s light curve are linearly filtered 32-bit floats; without OES_texture_float_linear the page falls back to half float for those five, and the line under the picture says so.',
    'Nothing audio: the plugin has no audio input. In Resolume, Fire Meteor and Supernova can be mapped to its own audio or BPM; here they are buttons.',
    'Fire Meteor and Supernova are FF_TYPE_EVENT in the plugin: here each is a toggle the page releases after one frame, one rising edge, which is what the plugin counts. The About block is not on the panel.',
    'Switching the Plugin dropdown is a new instance, as it would be in Resolume: every control goes back to its default, and a meteor or supernova in flight is gone. SW Sidereal Over declares two more controls (Clip As and Mix, in Output), shown only when it is chosen.',
    'The clock is the page’s: seconds from frame deltas, capped at a tenth of a second a frame. The plugin’s vote on whether the host sends seconds or milliseconds is not ported, because this host’s unit is known. Restart is a step backwards, which the plugin takes as no time passing, so the flow, the drift and a supernova carry on from where they were. Paused, nothing moves.',
    'The tables are built in this page, in double, when the telescope or the target changes, as the plugin builds them: about a million Planck evaluations for the galaxies’ redshift table alone, which a phone feels for a moment. The page keeps what it has built for a telescope and a target’s mix, because the plugin’s BuildPalette is a pure function of the two; the line under the picture says what the last build took.',
    'Over: the clip is the kit’s generated one (or your own file), premultiplied and 8-bit as Resolume hands an effect its input. The harness’s look card is not here.',
    'Nothing here is measured. The plugin’s harness checks, among much else, that the gas’s recombination light adds up to the star’s ionising photons along a ray (0.996 and 1.001), that the ionisation zones stand in the physics’ order, that each filter’s diffraction core grows with its wavelength, that Hubble’s and Webb’s spikes fall as r⁻², that dust reddens a star as Cardelli, Clayton and Mathis’s law predicts through each filter, and that a lens’s Einstein ring and a light echo land where the geometry says, with thirteen deliberately wrong models and eighteen mutants all caught — and that harness, not this page, is the reason to believe the picture. What the targets look like next to the real releases is a judgement, not a measurement. The plugin itself has never been loaded into Resolume on macOS.',
  ],

  createRenderer: (gl, quad) => {
    renderer = createRenderer(gl, quad);
    return renderer;
  },
});

//---------------------------------------------------------------------------
// Which controls belong to which plugin; the event buttons; the status line.
//
// By inline style, not the `hidden` attribute: kit.css gives these elements a
// `display` of their own, which beats the attribute's user-agent rule. Rows
// are found by their control's id, falling back to the name for the toggles,
// which carry no id.
//---------------------------------------------------------------------------
if (mounted) {
  const params = mounted.params;
  const embed = query.has('embed') && query.get('embed') !== '0';
  let shownVariant = mounted.state.variant;

  // Copy link carries the plugin, since the two declare different controls.
  const toQuery = params.toQuery.bind(params);
  params.toQuery = () => {
    const q = toQuery();
    if (mounted.state.variant === 'over') q.set('plugin', 'over');
    return q;
  };

  const show = (node, on) => { if (node) node.style.display = on ? '' : 'none'; };
  const rowOf = (p) => document.getElementById(`p-${p.id}`)?.closest('.prow')
    ?? [...document.querySelectorAll('.prow')].find((row) => row.querySelector('.prow__name')?.textContent === p.name);

  function showForVariant(variant) {
    for (const p of PARAMS) {
      if (p.only) show(rowOf(p), p.only === variant);
    }
    const effect = variant === 'over';
    for (const field of document.querySelectorAll('.transport__field')) {
      if (field.querySelector('.transport__label')?.textContent === 'Clip') show(field, effect);
    }
    show(document.querySelector('.transport__file'), effect);
  }

  document.addEventListener('demo:state', () => {
    const variant = mounted.state.variant;
    if (variant !== shownVariant) {
      shownVariant = variant;
      // A new instance: the constructor's defaults (the same for both).
      params.reset();
    }
    if (!embed) showForVariant(variant);
  });

  if (!embed) {
    showForVariant(shownVariant);

    const stage = document.querySelector('.stage');
    if (stage) {
      //---------------------------------------------------------------------
      // The two events, as buttons: each presses the plugin's own control.
      //---------------------------------------------------------------------
      const bar = document.createElement('div');
      bar.className = 'transport';
      bar.setAttribute('role', 'group');
      bar.setAttribute('aria-label', 'Events');
      const label = document.createElement('span');
      label.className = 'transport__label';
      label.textContent = 'Events';
      label.title = 'The plugin’s Fire Meteor and Supernova controls (they are also in the panel’s Events group)';
      bar.append(label);
      for (const [id, text] of [['fireMeteor', 'Fire Meteor'], ['supernova', 'Supernova']]) {
        const b = document.createElement('button');
        b.type = 'button';
        b.className = 'btn';
        b.textContent = text;
        b.addEventListener('click', () => params.set(id, 1));
        bar.append(b);
      }
      stage.querySelector('.transport')?.after(bar);

      //---------------------------------------------------------------------
      // A status line: reports what the port computed, measures nothing.
      //---------------------------------------------------------------------
      const line = document.createElement('p');
      line.className = 'stage__status';
      line.id = 'sidereal-stats';
      line.setAttribute('aria-live', 'off');
      stage.append(line);
      const note = document.createElement('p');
      note.className = 'stage__status';
      note.id = 'sidereal-note';
      stage.append(note);
      const key = (id) => {
        const inst = InstrumentOf(id);
        return inst.filters.map((f, i) => `${f} ${HUE_NAMES[inst.colour[i].map((c) => Math.round(c * 100) / 100).join()] ?? '?'}`).join(', ');
      };
      setInterval(() => {
        const s = renderer?.stats;
        if (!s) return;
        line.textContent =
          `${s.target} through ${InstrumentOf(s.instrument).name}${s.asReleased ? ' (as released)' : ''}: ${key(s.instrument)}`
          + ` · ${s.elements} gas element${s.elements === 1 ? '' : 's'}, ${s.sources} lighting star${s.sources === 1 ? '' : 's'}, ${s.sprites} sprite${s.sprites === 1 ? '' : 's'}`
          + (s.meteors ? ` · ${s.meteors} meteor${s.meteors === 1 ? '' : 's'}` : '')
          + (s.nova !== null ? ` · supernova day ${Math.round(s.nova)}` : '');
        note.textContent =
          `Field ${s.field}. Telescope tables built in this page in ${Math.round(tableTime.ms)} ms (${tableTime.builds} build${tableTime.builds === 1 ? '' : 's'}).`
          + (renderer.floatLinear ? '' : ' This browser has no OES_texture_float_linear, so the palette tables and the light curve are half float here, not the plugin’s 32-bit float.');
      }, 250);
    }
  }

  // For the headless checks in AGENTS.md: the page's pieces, read-only by convention.
  window.siderealDemo = { mounted, get renderer() { return renderer; }, PARAMS, PT, createRenderer };
}
