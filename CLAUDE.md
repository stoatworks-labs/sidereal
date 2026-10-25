# sidereal

The sky as the space telescopes photograph it, for Resolume Arena/Avenue, as two FFGL
plugins from one core: gas lit by its stars, seen through Hubble's and Webb's real filters
and colour keys. C++/GLSL, CMake MODULE → universal `.bundle` (macOS) + Windows `.dll`. MIT.

| | id | display name | bundle | type |
| --- | --- | --- | --- | --- |
| the sky on black | `SD01` | `SW Sidereal` | `Sidereal.bundle`, `com.stoatworks.ffgl.sidereal` | source |
| the sky over the clip, or the clip as gas | `SD02` | `SW Sidereal Over` | `Sidereal Over.bundle`, `com.stoatworks.ffgl.sidereal.over` | effect |

Read `AGENTS.md` before touching the light pass (the march, the zones, the faces), the
gain rule (`physics::BuildPalette` and each target's `Mix`), the continuum normalisations in
`rebuildPalette`, or a target's traced outline.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build -j8`
- Install to Resolume: `cmake --install build` (both bundles into Arena's Extra Effects)
- Render offline: `./build/sdtest --out /tmp/frame.png` (the Pillars, 3 s in). The summary
  line gives the picture's mean, median, 99th percentile and clipped share: set exposures by
  number, not by eye.
- Another target or telescope: `--set "Target=Cosmic Cliffs" --set "Telescope=Webb MIRI"`
- The Over: `./build/sdtest --over --out /tmp/over.png` (the look card as the clip),
  or `--clip clip.rgba` (raw RGBA at `--size`, top row first); `--set "Clip As=Gas"`
- List parameters: `./build/sdtest --list` (`--over` for the effect's)
- Press something later: `--at 30 "Supernova=1"`, `--at 20 "Fire Meteor=1"`
- A stage on its own: `--hooks N` with shaders::Hook bits (1 raw emission O/H/S/PAH, 2 the
  field, 4 light 0, 8 no stars, 16 linear, 2048 the sprite buffer alone)
- Film: `./build/sdtest --film 600 --size 1280x720 --script cues | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 60 -i - out.mp4`
- A clip through it: `ffmpeg -i in.mov -vf fps=60 -f rawvideo -pix_fmt rgba - | ./build/sdtest --over --pipe --size WxH | ffmpeg …`

## Verify
- Everything: `tools/verify.sh` (under eight minutes on an M4 Max; set `TMPDIR` to a scratch directory in
  a sandboxed session or `glslc.sh` hangs on its heredoc; `OXBOW=` points at an oxbow binary
  if `~/Projects/resolume/oxbow/build/oxbow` isn't built).
- **No GL** (`--offline` runs these and their negatives): `--hardness`, `--extinction`,
  `--cosmology`, `--supernova`, `--meteorcurve`, `--palette`, `--names`.
- **The gas and its light**: `--photons` (the photon budget along a ray), `--strata` (the
  zones' order).
- **The telescope**: `--psf` (the core against wavelength, 1920x1080 and 3840x2160),
  `--spikes` (count, angles, 1 / r^2), `--reddening` (CCM through each filter, in the picture).
- **The sky**: `--parallax`, `--lens` (surface brightness and the Einstein ring), `--echo`,
  `--meteor`.
- **The rest**: `--state`, `--resize`, `--alpha`.
- The checks can fail: `./build/sdtest --negative` (13 wrong models), `tools/mutate.sh`
  (16 mutants). `SDTEST_DUMP=dir` makes `--spikes` write its frames.
- No dead controls: `python3 tools/sweep.py` (both plugins; `--bare` drops the contexts).
- Render cost: `./build/sdtest --bench`.
- `SDTEST_RENDERER=software` runs any of it on Apple's software renderer (verify runs
  photons, strata, reddening, parallax and alpha there).

## Notes
- **Five passes a frame**: field (MRT, half raster plus 15% margin each side, A mipmapped),
  light (four targets), sprites (additive RGBA16F at the host raster), composite. No state
  survives a frame in GPU buffers: a resize is a fresh start.
- **A target is data** (`source/Scene.cpp`): elements (blob, column, polygon, shell,
  filaments, sheet) on channels (dense, haze, foreground, shock, oxygen, synchrotron, jets),
  sources, bright stars, galaxies, a deep field, a Milky Way band, and the `Mix` its filters
  are stretched to. Polygons are traced in the release's (u, v), v down.
- **The telescope is tables** (`physics::BuildPalette`): a line component through tau_V, a
  continuum (star, scattered, dust) through temperature and tau_V, galaxies through
  temperature and redshift, all as display RGB. Rebuilt when the instrument or the target
  changes.
- All host parameters are 0..1 and mapped in `Controls.cpp`; options hold the element index;
  Fire Meteor and Supernova act on the rising edge.
- GLSL reserved words (`patch sample input output filter common active half layout flat
  packed`, `noise1`–`noise4`) must not be identifiers; `verify.sh` greps for them. Every
  shader piece stays under 15 000 bytes (MSVC C2026).
- Override `SetTextParameter` to return `FF_SUCCESS` for the About block, or no host can
  instantiate the plugin.
- `sidereal_core` is an OBJECT library: each plugin registers itself from a file-scope
  constructor nothing references.
- The macOS build must be universal. Check with `lipo`, never the build log.
- **Provisional hand copies:** `source/StoatworksAbout*.h` (guide = "", no user guide yet)
  and `ATTRIBUTIONS.md`. Once the project is registered in stoatworks-backend,
  `sync-about.py` and `sync-attributions.py` generate them; edit them there then.
- Local only: no remote, nothing released, not on the website.

## Not done yet
- Never loaded into Resolume on either platform. No Windows build. The workflows in
  `.github/` have never run.
- No GitHub repo, no release, no website page, no user guide, no browser demo, no OpenFX
  port, no video, no presets.

## Diagnostics

`source/Diag.{h,cpp}`: log file only, no crash handler (this runs inside Resolume).

    ~/Library/Logs/sidereal/sidereal.YYYY-MM-DD.log
    %LOCALAPPDATA%\sidereal\logs\sidereal.YYYY-MM-DD.log

`SIDEREAL_LOG_DIR` overrides the folder.
