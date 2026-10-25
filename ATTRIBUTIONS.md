# Attributions

Sidereal is built on other people's work. This file lists what that work is, who did it,
and what it is doing here.

**Provisional hand copy (2026-10-25).** The fleet's attributions files are generated: the
master lists live in the `stoatworks-backend` repo and are pushed out by
`scripts/sync-attributions.py`. Sidereal isn't registered there yet, so this one was
written by hand in the same shape. Once it is registered, the generated file replaces
this one and the entries below move into the backend's lists.

## Code derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The harness, the tooling and the plugin's shape — Stoatworks ferrofluid

<https://github.com/stoatworks-labs/ferrofluid>  
Licence: MIT  
Copyright: Stoatworks Labs

Two bundles from one OBJECT library (`CMakeLists.txt`), a host order per plugin with the
About block last (`source/Controls.cpp`), the clock-unit vote and the GL state capture
(`source/Sidereal.cpp`, `source/GLState.h`), `source/PassBuffer.{h,cpp}` and
`source/Diag.{h,cpp}`; the harness's rig, PNG writer, look card, cue sheets,
`--pipe`/`--film` (SIGPIPE ignored, a closed stdout an exit of 1), `--negative` with its
Perturb, `--state` and `--offline`; `tools/verify.sh`, `tools/mutate.sh`,
`tools/glslc.sh`, `tools/sweep.py`, the workflows and `scripts/release-lib.sh`. Ferrofluid
had them from schlieren, fiche, honeydew, millpond, radar and vectrix. All adapted.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl.

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Taken from vcpkg on Windows (`vcpkg.json`). Resolves OpenGL entry points there, where the
system headers stop at OpenGL 1.1. macOS uses the system OpenGL framework.

## Pictures the targets were traced from

No image is shipped, sampled or embedded. The targets drawn from pictures are outlines of a
few dozen points each, traced by eye from these releases, plus the colour key each release
published.

- **Pillars of Creation** — NASA, ESA and the Hubble Heritage Team (STScI/AURA), 2014
  (heic1501a); and NASA, ESA, CSA, STScI (J. DePasquale, A. Koekemoer, A. Pagan), 2022
  NIRCam and MIRI (weic2216a, weic2218b).
- **Cosmic Cliffs** — NASA, ESA, CSA, STScI, 2022 (carinanebula3).
- **Horsehead** — ESO, 2002 VLT (eso0202a, CC BY 4.0); NASA, ESA and the Hubble Heritage
  Team, 2013 (heic1307a).
- **Colour keys** — the filter-to-hue tables published with the Webb releases of the
  Pillars (NIRCam: F090W purple … F470N red; MIRI: F770W blue, F1130W green, F1500W red),
  the Cosmic Cliffs, the Southern Ring, the Ring Nebula (ESA/Webb weic2320) and SMACS 0723,
  and with Hubble's 2014 Pillars (F502N blue, F657N green, F673N red).

The other famous targets are built from their published descriptions (structure, filters,
orientation), not traced.

## Work checked against

No code was taken from these — but they were how I knew I had it right, or where the
numbers came from.

### Interstellar extinction — J. A. Cardelli, G. C. Clayton and J. S. Mathis, ApJ 345 (1989) 245; R. Indebetouw et al., ApJ 619 (2005) 931; B. T. Draine, ARA&A 41 (2003) 241

CCM's a(x) + b(x)/R_V from 0.125 to 3.33 µm in `source/Physics.cpp`, and their Table 3,
which `sdtest --extinction` holds it to. Past 3.33 µm the infrared power law is continued
with Indebetouw's flattening (A_λ about 0.4 A_K from 4 to 8 µm) and the silicate features
at 9.7 and 18 µm from Draine's review: that extension is this repo's, not CCM's.

### Recombination and nebular lines — D. E. Osterbrock and G. J. Ferland, Astrophysics of Gaseous Nebulae and Active Galactic Nuclei (2006), tables 4.2 and 4.4

Case B ratios at 10⁴ K for the Balmer, Paschen and Brackett lines in `source/Physics.cpp`.

### Supernova light — D. K. Nadyozhin, ApJS 92 (1994) 527; W. D. Arnett, ApJ 253 (1982) 785; B. Leibundgut, A&ARv 10 (2000) 179

The ⁵⁶Ni → ⁵⁶Co → ⁵⁶Fe heating (6.45 e^{−t/8.8} + 1.45 e^{−t/111.3} × 10⁴³ erg/s per solar
mass of nickel), Arnett's diffusion integral, and the observed decline of type Ia tails
(about 1.4 mag per 100 days) that `sdtest --supernova` holds the tail to.

### Meteors — E. J. Öpik, Physics of Meteor Flight in the Atmosphere (1958); D. W. R. McKinley, Meteor Science and Engineering (1961)

The classical single-body light curve, I ∝ (ρ/ρ_max)(1 − ρ/3ρ_max)², used in
`source/Physics.cpp` and measured in the picture by `sdtest --meteor`.

### Colour from a stretch — R. Lupton et al., PASP 116 (2004) 133

The asinh stretch of the mean, applied to every channel, in the composite shader.

### Cosmology — E. L. Wright, PASP 118 (2006) 1711

His calculator's 1651.9 Mpc at z = 1 (H0 = 70, Ωm = 0.3, flat) is what
`sdtest --cosmology` holds the angular diameter distance to.

### Filters — the STScI instrument handbooks for WFC3, ACS, NIRCam and MIRI

Pivot wavelengths and widths of every filter in `source/Physics.cpp`.
