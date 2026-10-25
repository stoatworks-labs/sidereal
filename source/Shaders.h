#pragma once

#include <string>

/**
    The passes, once a frame.

        1. **field**      two targets, half the host's raster plus a margin all
                          round (so gas off the frame still casts shadows on it):
                          A = ( dense column, in-plane opacity, haze column,
                          foreground dust column ), B = ( shock, synchrotron,
                          jets, oxygen-rich shock ). Every element of the target
                          evaluated at its own parallax, roughened by shared
                          turbulence. A is mipmapped after: the march reads it
                          at the level its step spans.
        2. **light**      two targets, the field's raster: for each star that
                          lights the gas, its light marched across the picture
                          to here through the in-plane opacity (quadratically
                          spaced steps, fine near the pixel where the front is,
                          coarse far away), and what survives in each band:
                          hydrogen-ionising, oxygen-ionising (absorbed faster),
                          the partly ionised zone's difference, far-UV for the
                          dust and the aromatics, and visible light for
                          scattering, with Henyey-Greenstein's phase.
        3. **sprites**    additive, the host's raster, RGBA16F: every star
                          bright enough for spikes, drawn per filter (the core
                          and spikes scale with wavelength), every meteor's
                          trail and train, the supernova itself.
        4. **composite**  the host's frame: the gas's emission from the light
                          that reached it (each ionising photon absorbed is a
                          recombination), dust's own glow at the temperature
                          the starlight holds it at, scattered starlight, the
                          remnants' shocks; the faint stars, galaxies and the
                          deep field, a supernova's remnant and light echo; all
                          through the telescope's filters as tables, reddened by
                          the dust in front; then Lupton's asinh stretch.

    The fragment shaders are assembled at InitGL -- kVersion + kCommon + the
    pass's pieces -- by `Assemble()`, the one place the order is written;
    tools/glslc.sh's ASSEMBLED table mirrors it.
*/
namespace sidereal::shaders
{

extern const char* const kVersion;
extern const char* const kQuadVertex;
extern const char* const kCommon;
extern const char* const kFieldFragment;
extern const char* const kLightFragment;
extern const char* const kSpriteVertex;
extern const char* const kSpriteFragment;
extern const char* const kCompositeSky;
extern const char* const kCompositeGalaxies;
extern const char* const kCompositeMain;

enum class Pass
{
	Field,
	Light,
	SpriteVertex,
	Sprite,
	Composite
};
std::string Assemble( Pass pass );

/// Sizes shared with the GLSL's #defines (the harness's --names checks they agree).
constexpr int kMaxElements = 64;
constexpr int kMaxPolygon  = 1024;
constexpr int kMaxSources  = 12;
constexpr int kMaxGalaxies = 8;
constexpr int kMaxSprites  = 160;
constexpr int kTexelsPerElement = 5;
constexpr int kTexelsPerSprite  = 4;
constexpr int kSupernovaSamples = 512;
constexpr double kSupernovaDays = 800.0;

/// Harness hooks: bits of the `Hooks` uniform. A shipped instance sets 0,
/// and every branch they guard is then the shipped model.
enum Hook : int
{
	kHookRawEmission  = 1 << 0,///< composite writes ( O, H, S, PAH ) emission, before any filter
	kHookRawField     = 1 << 1,///< composite writes field A
	kHookRawLight     = 1 << 2,///< composite writes light 0
	kHookNoStars      = 1 << 3,///< no faint stars, no band, no sprites
	kHookLinear       = 1 << 4,///< no stretch, no clip: the display values themselves
	kHookNoShadow     = 1 << 5,///< the light pass ignores the gas: nothing is ever absorbed
	kHookOneZone      = 1 << 6,///< oxygen ionised as easily as hydrogen; no partly ionised zone
	kHookGreyPsf      = 1 << 7,///< every filter's core and spikes the same size
	kHookSpikeLaw     = 1 << 8,///< spikes fall as 1 / r, not 1 / r^2
	kHookLensBright   = 1 << 9,///< lensed images brightened by the magnification
	kHookNoGas        = 1 << 10,///< no nebula at all (galaxies and stars only)
	kHookRawSprites   = 1 << 11,///< composite writes the sprite buffer alone, linear
	kHookCoarseMarch  = 1 << 12,///< the march's steps evenly spaced, not fine near the pixel
	kHookWrongParallax = 1 << 13,///< parallax grows with depth instead of falling
	kHookEchoSphere   = 1 << 14,///< the echo's delay as if the dust were round the star, not in front
};

} // namespace sidereal::shaders
