#pragma once

#include "Physics.h"

#include <cstdint>
#include <vector>

/**
    What is in the sky: the gas, the stars that light it, the stars in front
    and behind, the galaxies -- as data. A Target builds one.

    Units: the frame's height is 1, the origin is its centre, y is up. A 16:9
    frame spans x = -0.889 .. 0.889. Targets drawn from a published picture are
    authored in that picture's own (u, v), u right and v down, both 0..1, and
    converted with the picture's aspect so it fills the frame's height.

    Columns of gas have two numbers, because a projected picture of a cloud has
    two directions in it: its COLUMN DENSITY along the line of sight (what dims
    the light behind it and what it glows with) and its THICKNESS along the line
    of sight. The starlight that ionises it travels across the picture, through
    column / thickness per unit length (AGENTS.md: "a picture is flat, the gas
    is not").
*/
namespace sidereal::scene
{
enum class Shape : int
{
	Blob = 0, ///< an ellipse with a soft edge
	Column,   ///< a tapered capsule from base to tip: a pillar
	Polygon,  ///< an outline traced from a picture, soft-edged
	Shell,    ///< a projected thick shell (planetary nebula, remnant), limb-brightened
	Filaments,///< a ridged network of thin sheets, inside an ellipse
	Sheet,    ///< a soft half-plane: a curtain of gas brightening one way
};

enum class Channel : int
{
	Dense = 0,  ///< molecular cloud and its dust: shadowed, lit at its skin
	Haze,       ///< the ionised interior: thin, everywhere
	Foreground, ///< dust in front of everything but the nearest stars
	Shock,      ///< gas lit by a shock, not a star (a supernova remnant)
	Oxygen,     ///< the same, oxygen-rich (a remnant's other filaments)
	Synchrotron,///< a pulsar wind's continuum
	Jets,       ///< H2 knots and Herbig-Haro objects
	Count
};

struct Element
{
	Shape shape     = Shape::Blob;
	Channel channel = Channel::Dense;
	float x = 0, y = 0;  ///< centre, or a column's base, or a sheet's edge
	float x2 = 0, y2 = 0;///< a column's tip; a sheet's direction (towards denser); x2, a shell's openness (0 closed, 1 a barrel seen end on)
	float a = 0.1f, b = 0.1f;///< radii (blob, shell, filaments); a column's base and tip radii
	float angle     = 0;  ///< radians, anticlockwise
	float density   = 1;  ///< column density at the core
	float thickness = 0.1f;///< along the line of sight, frame heights
	float edge      = 0.01f;///< the soft edge's width
	float rough     = 0.02f;///< how far the turbulence moves the edge
	float detail    = 0.5f;///< how much the turbulence varies the density, 0..1
	float depth     = 1;  ///< parallax: 1 is the nebula, larger is further
	float extra     = 0;  ///< shell: its thickness as a fraction of radius; filaments: the network's scale
	float extra2    = 0;  ///< shell: knots; filaments: anisotropy
	int polyStart = 0, polyCount = 0;
};

/// A star that lights the gas. Usually off the frame.
struct Source
{
	float x = 0, y = 0;
	float h          = 0.2f;///< its distance in front of (+) or behind (-) the gas's plane
	float luminosity = 1;
	float temperature = 40000;
	float soft = 0;      ///< the nearest the gas comes in depth: a shell's cavity keeps its caps at the inner radius
	bool visible = false;///< also drawn, as a bright star at its place
};

/// One of the stars bright enough for the telescope's spikes.
struct Star
{
	float x = 0, y = 0;
	float flux        = 1;///< bolometric, in the frame's own units
	float temperature = 6000;
	float depth       = 1;///< parallax
	int layer         = 1;///< 0 in front of the gas, 1 behind it, 2 inside it
};

struct Galaxy
{
	int kind = 0;///< 0 spiral, 1 elliptical, 2 lenticular
	float x = 0, y = 0;
	float scale     = 0.1f;///< the disk's exponential scale length
	float incline   = 0;   ///< radians, 0 face-on
	float pa        = 0;   ///< radians, the major axis's angle
	int arms        = 2;
	float pitch     = 0.3f;///< radians
	float armContrast = 0.6f;
	float bulge     = 0.3f;///< the bulge's share of the light
	float bulgeRe   = 0.03f;
	float sersic    = 4.0f;
	float bulgeAxis = 0.8f;///< the bulge's intrinsic flattening
	float dust      = 0.6f;
	float dustRing  = 0;   ///< > 0: the dust is a ring at this radius (Sombrero)
	float hii       = 0.5f;
	float brightness = 1;
	float diskT = 6500, bulgeT = 4300, armT = 14000;
	float phase     = 0;   ///< the arms' rotation
	float depth     = 1;
};

/// A deep field: thousands of galaxies at every redshift, and perhaps a
/// cluster in front of them bending their light.
struct DeepField
{
	bool on       = false;
	float density = 0;    ///< galaxies per square frame height
	bool lens     = false;
	float lensX = 0, lensY = 0;
	float einstein = 0.1f;///< the cluster's Einstein radius
	float zLens    = 0.39f;
	float members  = 0;   ///< cluster ellipticals per square frame height at its core
	float coreRadius = 0.15f;
};

/// The unresolved stars of the Milky Way as a band of light.
struct Band
{
	float amount = 0, angle = 0, width = 0.3f, offset = 0;
};

struct Target
{
	const char* name = "";
	int asReleased   = 0; ///< physics::InstrumentId
	std::vector< Element > elements;
	std::vector< float > polygon;///< x, y pairs
	std::vector< Source > sources;
	std::vector< Star > stars;
	std::vector< Galaxy > galaxies;
	DeepField deep;
	Band band;

	float haze        = 0.1f;///< the ionised interior's column, everywhere
	float hazeShade   = 0.6f;///< how much of the haze behind the dense gas is in its shadow (0: a lit wall behind)
	float hazeScale   = 2.0f;///< its turbulence's frequency, per frame height
	float warpScale   = 3.0f;///< the turbulence's frequency
	float warp        = 0.03f;///< its amplitude, frame heights
	float fieldStars  = 2000;///< faint stars per square frame height
	float embedded    = 0;   ///< young stars inside the dense gas, per unit column
	float dustRatio   = 4.0f;///< tau_V per unit column
	float exposure    = 0;   ///< stops, added to the control's
	float stretch     = 1;   ///< multiplies Lupton's softening: galaxies want a harder stretch
	float snX = 0.1f, snY = 0.1f;///< where a supernova goes off
	float echoDistance = 0.15f;///< the dust sheet a light echo crosses
	bool galaxyScene  = false;
	physics::Mix mix  = physics::NebulaMix();///< what each filter is stretched to
};

/// Every target the Target control offers, in order.
int TargetCount();
const char* TargetName( int index );
/// The first generative target; the ones before it are drawn from pictures.
int FirstGenerative();

/// Build a target. `seed` places the random stars and, for a generative
/// target, everything else.
Target Build( int index, uint32_t seed );

/// The seeded generator every builder uses: PCG32.
struct Random
{
	uint64_t state;
	explicit Random( uint64_t seed );
	uint32_t next();
	float uniform();                 ///< [ 0, 1 )
	float range( float lo, float hi );
	float normal();
};

} // namespace sidereal::scene
