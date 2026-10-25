# Attributions

Sidereal is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is generated — the master lists live in the `stoatworks-backend` repo and are
pushed out by `scripts/sync-attributions.py`. Edit it there, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The harness, the tooling and the plugin's shape — Stoatworks schlieren

<https://github.com/stoatworks-labs/schlieren>  
Licence: MIT  
Copyright: Stoatworks Labs

The harness's rig, PNG writer, look card, cue sheets, --pipe/--film (SIGPIPE ignored, a closed stdout an exit of 1), --negative with its Perturb, --state and --offline; the host-clock unit vote; tools/verify.sh, tools/mutate.sh, tools/glslc.sh, tools/sweep.py, the CI and release workflows and scripts/release-lib.sh. By way of a sibling plugin that is not public yet, which had them from schlieren and fiche. All adapted.

### Two plugins from one core, and the control table's shape — Stoatworks honeydew

<https://github.com/stoatworks-labs/honeydew>  
Licence: MIT  
Copyright: Stoatworks Labs

CMakeLists.txt's two bundles from one OBJECT library, a host order per plugin with the About block last (source/Controls.cpp), and the harness's rule that an option is set by its name.

### Pass buffers, GL state and the log — Stoatworks millpond

<https://github.com/stoatworks-labs/millpond>  
Licence: MIT  
Copyright: Stoatworks Labs

source/PassBuffer.{h,cpp} and source/Diag.{h,cpp} are millpond's; source/GLState.h is radar's, which is vectrix's, from resolume-scopes.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl (third_party/ffgl in oxbow).

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth saying out loud.

### The relationship between infrared, optical and ultraviolet extinction — J. A. Cardelli, G. C. Clayton and J. S. Mathis, ApJ 345 (1989) 245

a(x) + b(x)/R_V from 0.125 to 3.33 um in source/Physics.cpp, and their Table 3, which sdtest --extinction holds it to. Past 3.33 um the curve is continued with Indebetouw et al.'s flattening (ApJ 619, 2005) and the silicate features from Draine's review (ARA&A 41, 2003); that extension is this repo's.

### Astrophysics of Gaseous Nebulae and Active Galactic Nuclei — D. E. Osterbrock and G. J. Ferland (2006), tables 4.2 and 4.4

The case B recombination ratios at 10^4 K for the Balmer, Paschen and Brackett lines in source/Physics.cpp.

### Supernova light: the decay heating, the diffusion and the tail — D. K. Nadyozhin, ApJS 92 (1994) 527; W. D. Arnett, ApJ 253 (1982) 785; B. Leibundgut, A&ARv 10 (2000) 179

The 56Ni -> 56Co -> 56Fe heating, Arnett's diffusion integral (whose rule sdtest --supernova checks), and the observed decline of type Ia tails that the tail is held to.

### The classical meteor light curve — E. J. Opik, Physics of Meteor Flight in the Atmosphere (1958); D. W. R. McKinley, Meteor Science and Engineering (1961)

I ~ (rho/rho_max)(1 - rho/3 rho_max)^2, in source/Physics.cpp, measured in the picture by sdtest --meteor.

### Preparing red-green-blue images from CCD data — R. Lupton et al., PASP 116 (2004) 133

The asinh stretch of the mean, applied to every channel, in the composite shader.

### A cosmology calculator for the World Wide Web — E. L. Wright, PASP 118 (2006) 1711

1651.9 Mpc at z = 1 (H0 70, Omega_m 0.3, flat), which sdtest --cosmology holds the angular diameter distance to.

### Analytical expressions for the Sersic profile's b_n — L. Ciotti and G. Bertin, A&A 352 (1999) 447

The expansion of b_n in every galaxy's bulge.

### Radiative transfer in a clumpy universe — P. Madau, ApJ 441 (1995) 18

The Lyman-alpha forest's effective depth against redshift, reduced to one power law, in the deep field's galaxy colours.

### The WFC3, ACS, NIRCam and MIRI instrument handbooks — Space Telescope Science Institute

The pivot wavelength and width of every filter in source/Physics.cpp.

## Inspirations

What this set out to be. No code, assets or binaries from any of these were used or examined — the debt is to the idea.

### Hubble's and Webb's published pictures

The Pillars of Creation (NASA, ESA, Hubble Heritage Team, 2014; NASA, ESA, CSA, STScI, 2022), the Cosmic Cliffs (NASA, ESA, CSA, STScI, 2022) and the Horsehead (ESO, 2002, CC BY 4.0; NASA, ESA, Hubble Heritage Team, 2013): their outlines traced by eye, a few dozen points each, and the filter-to-colour keys published with each release. No image is shipped, sampled or embedded.

## Standards and published specifications

What the implementation is measured against.

- **Melissa E. O'Neill, PCG (2014)** — the integer hash behind every star, galaxy and turbulence cell, exact in 32 bits on every GPU.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
