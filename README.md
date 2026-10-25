# Sidereal

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. It has **never been loaded into
> Resolume**, on either platform. What's measured is measured by an offline harness that
> drives the real plugin class in a headless GL context on a synthetic clock, on this Mac's
> GPU at two rasters, and reads the light back out of the plugin's own buffers and pixels.
> Along a ray from a star, the gas's recombination light adds up to **0.991** and **0.995**
> of the ionising photons the star sent that way (1 is every photon accounted for). The
> ionisation zones stand in the physics' order, [O III] nearest the star, then H, then
> [S II], then the aromatics, each more than a field texel apart. A star's diffraction core
> through F658N is **1.312** times its core through F502N, against the pivots' **1.314**;
> Hubble's spikes come out four at 45°, Webb's six and two, each falling as **r^−2.00**. A
> star behind a dust column dims in red, green and blue exactly as Cardelli, Clayton &
> Mathis's law through each filter predicts, to three figures. A lens's Einstein ring lands
> within a pixel of its radius; a light echo's ring at **0.3424** frame heights against the
> paraboloid's **0.3428**. **13** deliberately wrong models and **16** one-character mutants
> of the shipped code are all caught. What the pictures look like next to the real releases
> is a judgement, not a measurement: see [Status](#status).

The sky as the space telescopes photograph it, for [Resolume](https://resolume.com) Arena and
Avenue, as two FFGL plugins: **SW Sidereal**, a source that is a piece of sky on black, and
**SW Sidereal Over**, an effect that puts the sky over your clip, or turns your clip into the
gas and lets the stars light it.

![Four frames in a grid. Top left, three dark pillars of gas rising into a teal-green glow,
their tips rimmed in gold: the Pillars of Creation as Hubble's narrow bands show them. Top
right, the same pillars through Webb's infrared filters: a blue haze, translucent pillars
with stars showing through them, and a bright star with Webb's eight-pointed spikes. Bottom
left, a face-on spiral galaxy with blue arms, a yellow core and pink knots, a yellowish
companion at the end of one arm. Bottom right, a bright elliptical galaxy among hundreds of
tiny galaxies, some stretched into arcs round it](docs/hero.png)

<sub>Four frames of the source, rendered by `sdtest`, the offline harness, not captured from
Resolume, on 25 October 2026: the Pillars through As Released (Hubble's WFC3 narrow bands)
and through Webb NIRCam, the Whirlpool, and Webb's First Deep Field (`--set "Target=..."`,
one second in).</sub>

## The one idea

**Build the scene the way the pictures are made: gas lit by the stars inside it, seen
through the telescope's real filters, each filter's grey image given the hue NASA gave it.**

The famous space pictures aren't photographs in the holiday sense. Each is a stack of grey
images, one per filter, every filter passing a narrow slice of light: one spectral line of
oxygen, of hydrogen, of sulphur, or a band of the infrared. The people who make them stretch
each grey image to its own range, give it a colour (blue for the shortest wavelength, red
for the longest), add them up, and stretch the result. The Pillars of Creation are gold and
teal because [S II] went in red, H-alpha green and [O III] blue.

So Sidereal is the gas, the stars and the telescope:

- **the gas** is columns, clouds, curtains, shells and filaments, each with a column
  density along the line of sight and a thickness. Dense clouds hold dust; the thin ionised
  interior fills the gaps; remnants glow from their own shocks;
- **the stars that light it** sit wherever the target says, usually off the frame. Their
  light is marched across the picture through the gas, and every ionising photon absorbed
  is a recombination: the gas glows with exactly the light it caught. Oxygen's harder
  photons are used up first, so [O III] sits nearest the star; [S II] lives in the partly
  ionised skin just past the front; the aromatic molecules and the warm dust further in;
- **the telescope** is a real filter set with its real colour key: Hubble's WFC3 narrow
  bands, Hubble's broad bands, Webb's NIRCam and MIRI, a backyard camera. Dust dims and
  reddens through each filter as the extinction law says, glows in the mid-infrared at the
  temperature the starlight holds it at, and the stars get each telescope's diffraction
  spikes.

## What falls out

None of this is drawn as a look. It's the light doing what it does, and the filters
deciding what you see of it.

- **Rims on the side facing the star.** A pillar is lit on its tip and its star-facing flank,
  and casts a shadow down the gas behind it. Light Angle swings the star round and the rims
  follow.
- **The colours of the zones.** Through Hubble's narrow bands the gas nearest the star goes
  teal ([O III] and H-alpha), the fronts gold (H-alpha and [S II]), the far gas olive. A
  hotter star (Star Heat) makes more [O III] and the teal spreads.
- **Infrared sees through the dust.** The same pillars through Webb NIRCam turn
  translucent: dust dims 2 µm light about a tenth as much as visible light, so the young
  stars still inside the pillars appear, and the photodissociation skins glow in the
  aromatic band at 3.3 µm. Through MIRI the dust glows on its own.
- **Narrow-band stars go magenta.** A narrow filter passes a sliver of a star's continuum,
  so a narrow-band picture's stars come out in whatever hue the filters' balance leaves them,
  which in Hubble's palette is pink, as in the real thing. Broad-band sets are balanced so a
  Sun-like star is white.
- **Planetary nebulae ring.** A shell seen face on is brightest at its limb, [O III] fills the
  cavity and H-alpha and [N II] the outer edge: the Ring Nebula's teal and orange through
  Hubble's narrow bands.
- **Galaxies have their parts.** A Sérsic bulge, an exponential disk with logarithmic spiral
  arms, dust lanes on the arms' inner edges, pink H II regions strung along them; inclined,
  the disk brightens as 1/cos i and the near side's dust crosses the bulge (the Sombrero).
- **Deep fields lens.** A cluster bends the light of the galaxies behind it, which come out
  as arcs round the Einstein radius with their surface brightness unchanged; distant
  galaxies stop getting smaller past redshift 1.6, as the angular diameter distance turns
  over, and fade as (1 + z)^−4.
- **Meteors brighten slowly and go out fast.** Each one follows the classical ablation curve
  against the air's density, peaking 78% of the way through its flight and gone when the
  body is used up. A shower's trails all point back to one radiant; fast ones burn green with
  magnesium and leave an oxygen train.
- **Supernovae are type Ia.** A star rises over two weeks to outshine everything near it,
  fades on the decay of nickel and then cobalt, cools from blue to orange, leaves a remnant
  that grows as t then t^(2/5), and sends a light echo racing out through any dust in front.

## The targets

**Drawn from pictures.** Each one is the target's structure placed as its release shows it,
with the release's own filter set and colour key reachable through Telescope = As Released.

| Target | As released | |
| --- | --- | --- |
| **Pillars** | Hubble WFC3 narrow bands (2014) | M16's three pillars traced from Hubble's 2014 framing; NGC 6611 off the top. Webb NIRCam and MIRI are a Telescope away. |
| **Cosmic Cliffs** | Webb NIRCam, the release's own key | NGC 3324's ridge traced from Webb's 2022 picture, steam rising off it. |
| **Horsehead** | Hubble broad bands + H-alpha | Barnard 33 traced from ESO's 2002 picture, back-lit against IC 434 by σ Orionis. |
| **Orion** | Hubble broad bands + H-alpha | The Trapezium's blister on OMC-1, the Bar, the Dark Bay, M43. |
| **Crab** | Hubble broad bands + H-alpha | A cage of shock-lit filaments round a pulsar wind's synchrotron glow. |
| **Ring** | Webb NIRCam, the 2023 key | M57's shell round its white dwarf. |
| **Southern Ring** | Webb NIRCam, the 2022 key | NGC 3132, its shell and its two central stars. |
| **Helix** | Hubble [O III] and H-alpha | NGC 7293, with cometary knots on the shell's inner face. |
| **Veil** | Hubble [O III] and H-alpha | The Cygnus Loop's shock front: oxygen and hydrogen filaments a little apart. |
| **Pleiades** | A backyard camera | Hot B stars in a dust cloud they only light: too few ionising photons to make it glow. |
| **Andromeda** | A backyard camera | M31 at 77°, with M32 and M110. |
| **Whirlpool** | Hubble broad bands + H-alpha | M51, its two arms and NGC 5195. |
| **Sombrero** | Hubble broad bands + H-alpha | M104 edge-on, its bulge behind its dust ring. |
| **Ultra Deep** | Hubble ACS, B V i z | Thousands of galaxies at every redshift. |
| **First Deep** | Webb NIRCam, SMACS 0723's key | A cluster at z = 0.39 bending the galaxies behind it into arcs. |

**Generative.** Built from the Seed, a different one each time: Gen Nebula (clouds and
pillars round a cluster), Gen Dark (dust silhouettes against the Milky Way), Gen Planetary,
Gen Remnant, Gen Spiral, Gen Cluster (a lensing cluster) and Gen Starfield (the Milky Way's
band and rifts; turn Fly up to go through it).

![Twenty-two small frames in a grid of four: each target in turn, from the Pillars to a
generative starfield](docs/targets.png)

## Controls

| Group | |
| --- | --- |
| **Scene** | Target (the fifteen pictures and seven generative skies), Telescope (As Released, Hubble Narrow, Hubble Colour, Hubble Infrared, Webb NIRCam, Webb MIRI, Webb Deep, Backyard), Seed. |
| **Gas** | Gas Density (0.25–4× the target's own), Turbulence (0–3×), Flow (how fast the turbulence evolves), Haze (the ionised interior, 0–3×), Dust (0.1–10× its dimming per unit of gas). |
| **Light** | Starlight (0.1–10× the stars' ionising output), Star Heat (0.6–1.6× their temperature), Light Angle (swing the stars round, ±180°). |
| **Stars** | Stars (0–3× the field's density), Spikes (0–3× the telescope's), Seeing (blur, up to 8 px). |
| **Camera** | Zoom (0.25–4×), Pan X, Pan Y, Spin, Drift (a slow wander), Depth (parallax between near and far), Fly (forward through the stars). |
| **Events** | Meteors (0, then 0.5–120 a minute), Meteor Kind (Sporadic, Shower), Fire Meteor, Supernova, SN Speed (2–200 days of the light curve a second), Light Echo. |
| **Output** | Exposure (±4 stops), Stretch (Lupton's asinh, 0 is linear), Saturation (0–2), Clip As and Mix (Over only). |

**Most controls are relative to the target**: at 0.5 a target looks as it was drawn, and the
control bends it either way. Gas Density 0.5 is the Pillars' own gas, not an absolute number
that would suit one target and drown the next.

**The Over's Clip As**: Behind puts your clip behind the nebula, dimmed and reddened by its
dust; Gas makes your clip's brightness the gas, so the stars light your logo as they lit the
Pillars.

## Status

**v0.1.0, unreleased, 25 October 2026: local only, early days.**

### Measured offline, on macOS

`tools/verify.sh` passes on this machine (Apple Silicon, macOS 26) against a fresh universal
Release build. It runs every picture check at **two rasters**, 1280×720 and 320×180
(except where a check says otherwise), and five of them again on **Apple's software
renderer** at 320×180. What it establishes:

| check | result |
| --- | --- |
| `--photons` | a slab of gas lit along its axis: the recombination light summed along the ray, Σ E d² dx, over what the star sent, **0.991** at 1280×720, **0.995** at 320×180, **1.007** on the software renderer (bound 5%, the half-float buffers and a bilinear front) |
| `--strata` | mean depth into the gas of each zone's light: [O III] **0.017** < H **0.047** < [S II] **0.107** < aromatics **0.127** frame heights, each more than a field texel (0.003) apart; a hotter star makes relatively more [O III] |
| `--psf` | the core's width through Hubble's F658N over F502N, from each channel's peak over its total: **1.312** at 1920×1080, **1.310** at 3840×2160, against the pivots' **1.314** (bound 5%) |
| `--spikes` | Hubble: spikes at **45°, 135°, 225°, 315°**; Webb: **0°, 30°, 90°, 150°, 180°, 210°, 270°, 330°**; Hubble's along its length, halo subtracted, **r^−2.00** (bound 0.3) |
| `--reddening` | a 6000 K star behind τ_V = 1.6 of dust, its transmission in the picture R **0.360**, G **0.197**, B **0.130**, against **0.360, 0.197, 0.130** from the CCM law through each filter in double; both rasters |
| `--parallax` | stars and gas at depths 0.5 and 2 as the camera pans 0.2: moved **−0.4000** and **−0.1000** frame heights, the law exactly; the gas within a field texel |
| `--lens` | a lensed deep field's brightest pixels no brighter than the unlensed field's (**0.759** against **0.774**, 99.9th percentile); the Einstein ring at **0.1493** frame heights for θ_E 0.15 (one pixel) |
| `--echo` | a light echo off a dust sheet 0.05 in front, day 100: the ring at **0.3424** and **0.3417** frame heights, the paraboloid of constant delay at **0.3428** |
| `--meteor` | one meteor's light frame by frame: brightest at **0.833 s** and **0.800 s** of a 1.000 s flight, theory **0.781 s** plus its 0.12 s wake; gone to **4%** of peak 0.3 s after the end |
| `--hardness` | the O++ share of a blackbody's ionising photons, by its series, against Simpson's rule on x²/(eˣ − 1): equal to **1e-7** from x₀ 0.5 to 20 |
| `--extinction` | A_λ/A_V against Cardelli, Clayton & Mathis's Table 3 from U to K: within **2%** (the table's rounding); continuous past 3.33 µm; A_8/A_K **0.42** (Indebetouw: about 0.4); the 9.7 µm silicate bump |
| `--cosmology` | D_A against 10-point Gauss–Legendre to **1e-6**; **1651.91 Mpc** at z = 1 (Wright: 1651.9); galaxies stop shrinking at z = **1.61** |
| `--supernova` | peak on day **15.3**; Arnett's rule, light at peak against the decay heating then, to **0.04%**; the tail at **1.29 mag per 100 days** (observed type Ia tails 1.2–1.6, cobalt alone 0.98); the remnant as t¹ then t^0.4 |
| `--meteorcurve` | the classical curve peaks at x = 1.00000 at height 1, is gone at x = 3; 24 shower meteors all on their radiant lines to **3e-8** |
| `--palette` | each narrow band passes its own line; every filter of every telescope stretched to its own light to **2e-16**; unbalanced keys make an equal signal grey, balanced ones a 5800 K star; the tables are the exact sums to a float's rounding |
| `--names`, `--state`, `--resize`, `--alpha` | names under 16 characters and unique, the GLSL's sizes, rows and hook bits the C++'s; the host's blend, viewport and units back as found; a resized instance **0** different from a fresh one; the Over at Mix 0 returns the clip, its alpha mix(clip, 1, Mix) |
| `--pipe` | both plugins: 2.5 frames in, exactly 2 out; a closed stdout (`\| head -c 1`) exits 1, not 141 |
| `--negative` | **13** deliberately wrong models, **13** caught |
| mutation | **16** one-character mutants of the shipped GLSL and C++, **16** caught |
| `tools/sweep.py` | all **30** controls of the source and **32** of the Over that have pixels measurably change the picture |
| shaders | all 6 compile through `glslc`, not merely through Apple's driver |
| the bundles | both universal (`x86_64 arm64`), export `plugMain`, ad-hoc sign; `oxbow` reports `SW Sidereal` / `SD01` / `source` and `SW Sidereal Over` / `SD02` / `effect` and renders through both |

Render cost, GPU time by `GL_TIME_ELAPSED`, median of 60 frames after a warm-up, on an M4 Max
with other work on the machine: the Pillars **0.96 ms** at 720p, **1.98 ms** at 1080p,
**7.6 ms** at 4K; the Cosmic Cliffs 1.07, 2.15 and 8.2 ms; the Whirlpool 1.35, 2.89 and
11.3 ms; the lensed First Deep 1.27, 2.65 and 10.1 ms; the Over on the Pillars 0.99, 2.00 and
7.7 ms. Most of it is per pixel, so it scales with the raster. macOS figures only.

### Not established

It's **never been loaded into Resolume**, on either platform. Everything above was compiled,
rendered and measured offline against the plugin's own class in a headless CGL context, plus
an `oxbow` load. I haven't seen how the controls read in Arena's inspector, how the events
feel on a show, or how it runs on anything slower than this Mac. Nothing has been built or run
on Windows, and the workflows in `.github/` have never run.

**The pictures are like the releases, not copies of them.** The Pillars, Cosmic Cliffs and
Horsehead are traced from their releases and land recognisably; the rest are built from what
their releases show. The colour of each telescope comes from its real filters and colour key,
but the strengths of the lines, the dust's glow and the stars against each other are this
model's, tuned by eye against the releases. Some land well (the Ring and the Pillars through
Hubble, the Horsehead, the galaxies); some don't yet: the Pillars through Webb come out grey
where the release is amber, the Cosmic Cliffs' ridge is a harder line than Webb's, and the
planetaries through Webb's own keys are faint.

**The model's approximations**, all in AGENTS.md: the gas is a picture, flat, with a depth
along the line of sight that the light pass knows only as each element's thickness; the
light's path to a star off the plane is lengthened as if straight; the ionised interior's
light is counted by photons for its brightness but absorbs only a fifth of what that would
say (its true brightness goes as density squared); [O III] is normalised to a 40 kK star's
hard photons, not computed from oxygen's cooling; the faces of the gas towards us are lit as
a height field of its column; meteors appear through space telescopes, which are above the
air and would see none.

There's no user guide, no browser demo, no OpenFX port, no video and no presets yet.

## Build

macOS (universal: Apple Silicon and Intel). The repo isn't published yet; from a checkout:

```bash
git submodule update --init
```

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

```bash
cmake --install build
```

The last one copies `Sidereal.bundle` and `Sidereal Over.bundle` into
`~/Documents/Resolume Arena/Extra Effects`. Restart Arena and they're under Sources as
**SW Sidereal** and under Effects as **SW Sidereal Over**.

Windows needs Visual Studio 2022 and vcpkg (for GLEW); see `.github/workflows/ci.yml`.
It hasn't been built there.

## Building and testing

```bash
tools/verify.sh
```

Everything: shader hygiene, `glslc`, a fresh universal build with no warnings, both bundles
through lipo, the plist, an ad-hoc signature and `oxbow`, every check at both rasters, five
again on the software renderer, the pipe contract, the negative controls, the mutants, the
sweep and the bench. About forty minutes here, most of it the mutants. In a sandboxed Claude
session set `TMPDIR` to a writable scratch directory first. `CLAUDE.md` has the harness's
commands.

## Licence

MIT. See [LICENSE](LICENSE), and [ATTRIBUTIONS.md](ATTRIBUTIONS.md) for the work this rests
on and the releases the targets were traced from.
