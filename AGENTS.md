# AGENTS.md — Sidereal

The why. `CLAUDE.md` is the commands; this is the reasoning, the traps actually hit, what is
verified and what is assumed, and what isn't done.

## The idea

Allan asked (2026-10-25) for "a new resolume effect, which creates generative space scenes,
based on real nasa images (ideally including presets to recreate some famous ones like the
pillars of creation), it should be able to model nebulas, galaxies, starfields, shooting
stars, supernovas etc...".

"Based on real NASA images" is taken at its most literal useful reading: build the scene
the way those images are made. Each release is a stack of single-filter grey images, each
stretched to its own range, given a hue in chromatic order, summed and stretched again. So
the plugin computes what each filter would record from a physically lit scene, and applies
the release's own colour key. No image is shipped or sampled: the famous targets are
outlines traced by eye from the releases (ATTRIBUTIONS.md lists them) plus each release's
published filters and key.

**Two plugins, though the request said "effect".** In Resolume a generator is a source; an
effect needs a clip under it. One core gives both: the source (`SD01`) and the Over
(`SD02`), which either puts the sky over the clip (Behind: the clip is what lies behind the
nebula, dimmed by its dust) or makes the clip's brightness the gas (Gas). The name is
**Sidereal** ("of the stars"), eight letters so that `SW Sidereal Over` is exactly FFGL's
sixteen. Both decided without asking, as the brief says; cheap to change before a release.

## The model

### A picture is flat, the gas is not

Everything is 2D in the picture's plane. Each element carries two numbers: its **column
density** along the line of sight Σ (what dims the light behind it, and what it glows with)
and its **thickness** L. Starlight crosses the picture through the in-plane opacity
α = IonK Σ / L. So a pillar 0.1 wide with Σ 1 and L 0.09 is opaque to ionising photons
across a hundredth of the frame, and its rim is thin; the ionised interior (haze) has Σ ~0.1
spread over L 1.

A **shell** is the exception: its in-plane opacity is the mid-plane slice, the ring between
the inner and outer radii, density / (2R chord) — not its projected column, which is
non-zero across the whole face (the caps in front and behind). Using the column put every
planetary nebula's white dwarf inside gas and absorbed its light next to the star: found by
rendering the Ring and seeing only the star.

### Light: every ionising photon is a recombination

The light pass marches from each pixel to each source (up to twelve) through the field's
opacity, with **quadratically spaced steps** (fine at the pixel, where the front is, coarse
far away) read from the mip level each step spans, and lengthened by d3/r for a star off
the plane. What survives sets the emission per unit area:

- **H** (recombination): E = EmitH × IonK Σ × F e^{−τ}. Integrated along a ray, Σ α e^{−τ} ds
  = 1 − e^{−τ_total}: the photon budget, which `--photons` measures from the picture (0.996
  and 1.001).
- **[O III]**: the zone the hard photons (>35.1 eV) reach, F h e^{−3τ}, h the O++ share of
  the star's ionising photons from the closed-form series of the Planck photon integral.
  Its strength is normalised to a 40 kK star's hardness (`HardRef`) and saturates at four
  times it (oxygen can't be more than all O++; past it, O+++). A forbidden line carries much
  of the cooling where O++ lives, so it is not one photon per ionisation; counting it that
  way made [O III] 5% of H-alpha, where HII regions show 30–100%.
- **[S II]**: the partly ionised skin just past the front, F (e^{−0.75τ} − e^{−τ}).
- **Aromatics (PAH) and warm dust**: the far-UV beyond the front, e^{−0.5τ}; dust radiates
  what it absorbs as a modified blackbody (β = 2), so T = 90 K × (u / u_ref)^{1/6}.
- The order O < H < S < PAH is what `--strata` measures. The first cut had [S II] decaying
  slower than the aromatics (0.4 against 0.6), which put [S II] deeper: backwards. The partly
  ionised zone is at the front, the photodissociation region beyond it.

### The haze is lit round the dense gas

In flatland a pillar shadows everything below it, including the ionised interior in front of
and behind it, which in 3D the starlight reaches round it. So the haze is split: FrontHaze
(40%) in front of the dense gas, lit through the haze alone and dimmed only by foreground
dust; the rest behind, lit through the haze plus HazeShade × the dense gas's depth and
dimmed by the whole column. HazeShade is per target: 0.6 for the Pillars, **0 for the
Horsehead**, where IC 434 is the far cloud's lit skin behind the horse (with it at 0.6 the
head's shadow erased the snout's profile: the horse was a tombstone).

The haze absorbs only `hazeShadow` (20%) of what its photon count would say. An ionised
interior's true brightness goes as density squared, not as photons absorbed; at full
opacity the haze shadowed itself into darkness a frame from the star. A documented cheat.

### Faces

The stars stand off the plane (h), so the faces of the dense gas towards us are lit: an
ionisation front seen face on, brightness F cos(incidence), the normal from the column's
gradient as a height field (Relief 0.012). What lies between a face and the star is bounded:
FaceShadow × 6 (1 − e^{−τ/6}) × e^{−4h/r}, because past a few optical depths the ray has
risen clear of the gas. Unbounded, every big wall's face was black. Relief 0.08 made faces
flip lit/unlit: tiger stripes.

### The telescope: tables

`physics::BuildPalette(instrument, mix)`:

- every filter's **response** to each component (lines summed inside a top-hat band, broad
  Gaussian features integrated; continua by Simpson's rule on 64 panels);
- each filter's **gain** brings the TARGET's own light in it to 1 (`Mix`: nebula, planetary,
  remnant, starlight). This is the honest reading of "each filter stretched to its own
  range". A single generic reference made the Webb Ring invisible (its filters were
  stretched to starlight) and broadband filters with no lines in them (F814W) amplified
  every star into an orange blob. The nebula mix has a continuum floor (star weight 40) for
  the same reason;
- the release's **hues**, balanced so an equal signal in every filter is grey; broadband
  sets then white-balanced (`whiteBalance`) so a 5800 K star is white, as broad-band
  pictures are. Narrow-band sets are not, so their stars go magenta, as Hubble's narrow-band
  stars do;
- **tables** of display RGB: each line component through τ_V 0..12; stars, scattered light
  and dust through temperature and τ_V; galaxies through temperature and redshift (the Lyman
  break, the forest, (1 + z)^{−5} per unit wavelength).

**Continua are normalised per palette** (`continuumNorm`, `scatterNorm`, `deepNorm`,
`meteorNorm`, `starGain`): one unit of each looks the same brightness through every
telescope, and only its colour changes. Without it galaxies vanished through broadband sets,
the Pleiades' reflection nebula never showed, and meteors divided by zero through Hubble's
narrow bands (none of their lines fall in F502N/F657N/F673N: a NaN in every pixel). Meteors
now carry a broad continuum band, as their vapour does.

### Stars

Bright stars (each target's list, the visible sources, a supernova) are sprites, drawn per
filter: a Gaussian core of σ ∝ λ/D (0.85 px at 2 µm on Webb, scaled to the raster), a 6%
Moffat halo of six core sigmas, seeing and the detector's pixel in quadrature, and spikes —
Hubble's four from the spider at 45°, Webb's six from the segments and two fainter from the
strut — falling as r0²/(r0² + a²) and lengthening with wavelength (so spikes go red at their
ends). Faint stars are a hashed grid in three depth layers, plus young stars embedded in the
dense gas that only the infrared sees (τ through 0.6 of the column).

### Galaxies, deep fields, lensing

A galaxy is a Sérsic bulge (b_n from Ciotti & Bertin, its total normalised in closed form),
an exponential disk seen at inclination (thin, 1/cos i, a little thickness), logarithmic arms
with dust on their inner edges and H II regions along them inside 4 scale lengths, a dust
ring for the Sombrero. The deep field is two hashed layers of galaxies at redshifts to 5.7,
angular size from a physical size over D_A(z) (flat ΛCDM, a table), brightness evolving as
(1 + z)^{2.5} (star formation was busier) against the (1 + z)^{−5} dimming. A lens is a
singular isothermal sphere: each pixel looks at β = θ − θ_E θ̂ in the source plane, so arcs
and conserved surface brightness both fall out.

### Events

Meteors: the classical single-body curve (2.25 x (1 − x/3)², x = ρ/ρ_peak growing as
e^{v sin(entry) t / H}), first seen at x = 0.02, gone at x = 3; on-sky length from the path
over a 150 km range and a 20° frame, times sin of the distance from the radiant; colour by
speed (Na/Fe slow, Mg/O fast), a green [O I] train for fast bright ones. The supernova:
Nadyozhin's decay heating with γ-ray escape (T0 35 d) and positrons, Arnett's diffusion
(τ_m 13 d) integrated stably (the e^{(t/τ)²} growth would overflow at day 400 if done
literally), a remnant on a compressed clock (60 light-curve days to Sedov-Taylor), and a
light echo off a dust sheet in front: delay (√(ρ² + z²) − z)/c.

### Display

Lupton's asinh of the mean, applied to every channel (colour survives a star's core), times
the target's stretch factor (galaxies 4–6×: their releases are stretched hard), then
saturation and a per-channel clip.

## The traps

- **The drift's Lissajous wasn't zero at phase zero.** sin(0.77φ + 1.3) is 0.96 at φ = 0, so
  Drift 0 still left the camera 0.035 frame heights off: Pan's centre wasn't the centre.
  Found by `--spikes`, whose star wasn't where it should have been.
- **A parallax check through the sprites tests the CPU's camera, not the shader's.** The
  bright stars are placed on the CPU; a one-character mutant of the GLSL's `sceneAt`
  survived. The check now measures gas too.
- **An observational range can't see a parameter it doesn't constrain.** The γ-trap mutant
  (35 → 95 days) stayed inside the observed tail decline (1.2–1.6 mag per 100 days). Arnett's
  rule, exact for this formulation, now guards the diffusion integral.
- **A lensed field's galaxies are behind the lens, so fainter**: comparing a lensed field to
  an unlensed one compares redshifts, not lenses. The test lens sits at z ≈ 0.
- **Comparing the brightest pixels can't see a geometry error**: any remapping conserves
  surface brightness. The Einstein ring's radius, read from the source radius each pixel sees
  (`kHookLensMap`), does.
- **`--offline`'s verdicts accumulated** across checks (one failure made every later check
  report FAIL). Each check now starts from zero.
- **A test PSF has to be resolved.** Hubble's core is 0.6 px at 1080 lines and under a
  pixel at CI's raster; `--psf` runs at 1920×1080 and 3840×2160, and measures each channel's
  peak over its total (exact for a Gaussian and a halo that scales with it) rather than
  second moments, which the halo and the box biased to 1.19 against 1.31.
- **A spike ring has to sit inside the spikes and outside the halo**: at 30 px Hubble's had
  faded into noise; at 10 px Webb's wider halo hid its two weak spikes. 10 px for Hubble,
  20 for Webb, with runs above twice the ring's median as peaks.
- **A Ia's light echo is brightest at the centre** (still 25% of peak at day 100, and the
  inverse square): the ring is the light curve's peak only once the geometric dilution is
  divided out. Physics, not a bug; the check divides.
- **Apple's driver logs an unloadable texture unit** when a sampler points at unit with
  texture 0 (the source's Clip): it gets the light-curve table instead.
- **BSD sed has no `\b`**; perl does.

### Found by writing the user guide, fixed before v0.1.0

The guide-writer renders every claim. These were its, and each fix is in the code with a
comment saying what it was:

- **Fire Meteor did nothing on one press in ten**: a meteor could start just off the frame
  heading out, or be a faint scratch gone in a few frames. A press is now placed by its middle,
  well inside any picture from 4:3 to 21:9, as a fireball on a shallow path; `--meteorcurve`
  checks 960 presses.
- **The Pleiades was black at its defaults** (99th percentile 0.075). It gets three stops of
  its own exposure; `--targets` checks every target at 640×360 (at 320×180 the crowded stars
  lift the percentile, and the black Pleiades passed).
- **Deep-field galaxies had square edges**: the hashed layers search a 3×3 of cells and a big
  galaxy ran past it. Each is now kept to a quarter of a cell and faded to nothing a cell from
  its centre; a 5×5 search then draws the same pixels, bit for bit.
- **The planetaries were filled discs.** Three things together: the shell's flux softened as
  r² + r_in² (a ring's gas is at its drawn radius, so now the larger of the two), its wall at a
  neutral wall's opacity (the ionised skin a texel or two thick; now a third, as a mostly
  density-bounded shell is), and closed caps over a barrel the Ring is seen down. A shell has
  an openness now (`Element::x2`), set on the Ring, Southern Ring and Helix and drawn for Gen
  Planetary. The faces term was suspected and wasn't it; it is now off inside a shell's cavity
  all the same, since the caps' column already counts that gas.
- **Bipolar Gen Planetary lobes were dark bites**: dense shells outside the main shell, which
  took all the light in a flat picture. They are ionised haze now.
- **Orion's Bar threw a hard black wedge** to the corner: a dense, blunt column. It is thinner
  and tapered.
- **A ridge across the Pillars' feet**: the pillars' polygons ran on into the base's and the
  columns added. Traced polygons now join by the denser of the two. Trimming them to meet
  made a groove instead, since two soft edges meeting dip.
- **A one-row step along the Sombrero**: the bulge's far half switched behind the dust at the
  major axis with `?:`. It ramps across the disk's projected thickness.
- **Hubble Infrared was two stops darker** than any other telescope on a nebula: broad bands
  catch little line light. Instruments carry an exposure offset; it has +1.5.

## Would this hold on another rasteriser, at another raster?

| check | rasters | why it holds / the tolerance's source |
| --- | --- | --- |
| `--photons` | 1280×720, 320×180, software | an integral of resolved quantities: the front is five field texels wide at 320×180 by construction (density 0.3 over thickness 0.4); 5% is the half-float buffers summed over the front plus the bilinear front |
| `--strata` | both, software | the zones must be a field texel apart, measured in that raster's texels |
| `--psf` | 1920×1080, 3840×2160 | peak over total is exact for the shape; 5% is the centre sample of a 0.6 px Gaussian (1.5%) plus 2% channel contamination |
| `--spikes` | 1280×720 | angles to ±2°; the slope is a ratio over 6–18 px with the halo subtracted |
| `--reddening` | both, software | totals over the frame; 2% for half floats and the star table's linear temperature filtering |
| `--parallax` | both, software | centroids to a pixel (stars) and two field texels (gas) |
| `--lens` | both | percentiles over thousands of galaxies; the ring to a pixel |
| `--echo` | both | 0.01 frame heights (the curve's peak is flat to 1% over two days) plus a pixel |
| `--meteor` | both | two frames, plus the 0.12 s wake that delays the summed light's peak |
| `--alpha` | both, software | exact on the GPU; 6e-8 on the software renderer, under the 1e-6 bound |
| offline checks | none | double precision; tolerances are the references' own (CCM's table rounding, Wright's four figures) |

## Negative controls and mutants

`sdtest --negative`, all must fail: the photon tail's series cut to one term; every
wavelength dimmed like V (twice: `--extinction` and `--reddening`); D_A without deceleration;
γ rays that never escape; one gain for every filter; gas that absorbs nothing; oxygen ionised
as easily as hydrogen; every filter's core the same size; spikes falling as 1/r; parallax
growing with depth; lensed images brightened by the magnification; an echo delayed as if
the dust were a shell round the star.

`tools/mutate.sh`, one character each: the march's path not lengthened off the plane
(`--photons`); [O III]'s photons growing with depth, the partly ionised zone summed
(`--strata`); the core shrinking with wavelength (`--psf`); the spike's 1/r² broken
(`--spikes`); the echo's delay with the distance added (`--echo`); parallax offset by two
depths (`--parallax`, through the gas); a deflection that grows with radius (`--lens`, the
ring); CCM's first optical coefficient and the silicate feature (`--extinction`); the photon
tail's second term (`--hardness`); Ω_m 0.4 (`--cosmology`); Arnett's prefactor
(`--supernova`); the meteor curve, a shower meteor off its radiant (`--meteorcurve`); each
filter stretched to twice its own light (`--palette`).

## Verified, assumed, tuned

**Verified** (numbers in the README): the photon budget; the zones' order; the core against
wavelength; the spikes' count, angles and law; reddening through each filter; parallax in the
CPU and the shaders; the lens's surface brightness and Einstein radius; the light echo's
paraboloid; the meteor's peak and end; the Planck series; CCM; the cosmology; Arnett's rule
and the Ia tail; the palette's gains and balance; GL state; resize; alpha; the pipe contract.

**Assumed** (physics simplified on purpose): flatland with thicknesses; straight 3D paths
lengthened by d3/r; [O III] normalised to hardness, not cooling; haze opacity at 20%; faces
as a height field; scattering by Henyey-Greenstein g 0.5 with a λ^{−1} efficiency; dust at
β 2; PAH bands as Gaussians; a fixed 20° frame for meteors; the remnant's compressed clock.

**Tuned by eye against the releases**: every Emit constant, the stars' gain, each target's
exposure and stretch, the continuum normalisations' constants, the galaxies' gains. The
`--stats` style summary line of `sdtest --out` was used to set them by number.

## Open questions

- The Pillars through Webb NIRCam are grey where the release is amber: the hot small-grain
  continuum (a 4.4 µm band in the PAH spectrum) may be too weak, or the release's processing
  weights F335M/F444W harder.
- The Cosmic Cliffs' ridge is a harder line than Webb's steam; the ridge's polygon could be
  rougher, or the haze sheet's streaks stronger.
- The planetaries through Webb's own keys (Ring, Southern Ring) are blue and lilac where the
  releases are orange and red: their NIR lines (H2, Brackett) and PAH are weak against the
  PlanetaryMix's normalisation. The Ring through Hubble Narrow clips in its middle.
- Dust has a ceiling: the extinction tables stop at τ_V 12, so Dust 1 barely dims a lit
  pillar through NIRCam and never hides the stars inside it.
- Flow turns the galaxies' arms (the Whirlpool's arm leaves NGC 5195 within a minute), the
  remnant is the same size on every target (a third of the Whirlpool), and Fly replaces the
  faint field, the infrared's embedded stars with it.
- Hubble Infrared's gas is one flat blue; Andromeda's lanes are concentric ellipses; the
  Horsehead's base is two lit blocks in the bottom corners.
- Faint-star density against raster: at 320×180 the PSF floor makes rich fields crowd.
- Audio: none. Resolume can map Fire Meteor and Supernova to its own audio and BPM; a built-in
  onset drive (regauss's) is the obvious v0.2.
- Presets beyond the targets: Resolume saves parameter presets itself; nothing shipped.

## Not done

Never loaded into Resolume on macOS. No browser demo, OpenFX port, presets or audio drive.
Released as v0.1.0 (public repo, site page and user guide); the Windows build is gated in
Arena on win-lab by plugin-bench's `arena/expect/sidereal.json`.
