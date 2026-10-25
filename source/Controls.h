#pragma once

#include "StoatworksAboutLinks.h"

#include <vector>

/**
    The host's parameters, and what each one means.

    Every ranged parameter the host sees is 0..1, because `SetParamInfo`
    clamps an `FF_TYPE_STANDARD` default into 0..1 before `SetParamRange`
    could widen it. The conversions live in Controls.cpp, one function per
    control. Option parameters hold the element index (an option reads back
    0..1 whatever its count -- map by index); Fire Meteor and Supernova are
    presses.

    Most of the scene's controls are RELATIVE to the target: at 0.5 a target
    looks as it was drawn (or as the release looked), and the control bends it
    either way. Gas Density 0.5 is the Pillars' own gas, not some absolute
    number that would suit one target and drown the next.
*/
namespace sidereal
{
/// Every control either plugin has, by what it is -- NOT by the index a host
/// sees. Each plugin declares its own subset, densely, in `HostOrder()`'s
/// order, with the About block LAST in both.
enum ParamId : unsigned int
{
	// -- Scene -------------------------------------------------------------------
	PT_TARGET = 0,
	PT_TELESCOPE,
	PT_SEED,

	// -- Gas ---------------------------------------------------------------------
	PT_GAS,
	PT_TURBULENCE,
	PT_FLOW,
	PT_HAZE,
	PT_DUST,

	// -- Light -------------------------------------------------------------------
	PT_STARLIGHT,
	PT_STAR_HEAT,
	PT_LIGHT_ANGLE,

	// -- Stars -------------------------------------------------------------------
	PT_STARS,
	PT_SPIKES,
	PT_SEEING,

	// -- Camera ------------------------------------------------------------------
	PT_ZOOM,
	PT_PAN_X,
	PT_PAN_Y,
	PT_SPIN,
	PT_DRIFT,
	PT_DEPTH,
	PT_FLY,

	// -- Events ------------------------------------------------------------------
	PT_METEORS,
	PT_METEOR_KIND,
	PT_FIRE_METEOR,
	PT_SUPERNOVA,
	PT_SN_SPEED,
	PT_ECHO,

	// -- Output ------------------------------------------------------------------
	PT_EXPOSURE,
	PT_STRETCH,
	PT_SATURATION,
	PT_CLIP_AS,///< Over only
	PT_MIX,    ///< Over only

	// -- The Stoatworks About block: a text line, then one button per link.
	PT_ABOUT_TEXT,
	PT_COUNT = PT_ABOUT_TEXT + 1 + stoatworks::about::kButtonCount
};

enum class Kind
{
	Standard,
	Option,
	Event
};

/// One control as the host is told about it.
struct ControlInfo
{
	unsigned int id;
	const char* name;///< at most 16 characters: FFGL's name field is not terminated
	const char* group;
	Kind kind;
	float defaultValue;///< the host's 0..1, or the element index
	int optionCount;   ///< for an option; its names come from OptionName()
};

const ControlInfo& InfoOf( unsigned int id );
const char* OptionName( unsigned int id, int index );

/// The ids a plugin declares, in the order the host sees them.
const std::vector< unsigned int >& HostOrder( bool effect );

/// An option's index from the value the host stored.
int OptionIndex( float value, int count );

enum class MeteorKind
{
	Sporadic = 0,///< from anywhere
	Shower,      ///< from one radiant: every trail points back to it
	Count
};

enum class ClipAs
{
	Behind = 0,///< the clip is what lies behind the nebula: its dust dims it
	Gas,       ///< the clip's brightness is the gas: the stars light your picture
	Count
};

//---------------------------------------------------------------------------
// The mappings. Each says its range and its shape.
//---------------------------------------------------------------------------

/// A multiple, geometric about 0.5: lo at 0, 1 at 0.5, hi at 1.
double Multiple( float value, double lo, double hi );
float ParamFromMultiple( double multiple, double lo, double hi );

/// Gas Density: 0.25x to 4x the target's own.
double GasFromParam( float value );
/// Turbulence: 0 to 3x the target's own, linearly in two halves (0, 1, 3).
double TurbulenceFromParam( float value );
/// Flow: how fast the turbulence evolves, 0 to 0.12 frame heights a second.
double FlowFromParam( float value );
/// Haze: 0 to 3x the target's own, linearly in two halves.
double HazeFromParam( float value );
/// Dust: 0.1x to 10x the target's tau_V per unit column.
double DustFromParam( float value );
/// Starlight: 0.1x to 10x the ionising luminosity.
double StarlightFromParam( float value );
/// Star Heat: 0.6x to 1.6x the temperature.
double HeatFromParam( float value );
/// Light Angle: -180 to +180 degrees, radians.
double LightAngleFromParam( float value );
/// Stars: 0 to 3x the target's density, linearly in two halves.
double StarsFromParam( float value );
/// Spikes: 0 to 3x the telescope's own, linearly in two halves.
double SpikesFromParam( float value );
/// Seeing: extra blur, 0 to 8 reference pixels FWHM.
double SeeingFromParam( float value );
/// Zoom: 0.25x to 4x.
double ZoomFromParam( float value );
/// Pan: -1 to +1 frame heights.
double PanFromParam( float value );
/// Spin: -180 to +180 degrees, radians.
double SpinFromParam( float value );
/// Drift: 0 to 0.05 frame heights a second, a slow wander.
double DriftFromParam( float value );
/// Depth: the parallax between depths, 0 to 2.
double DepthFromParam( float value );
/// Fly: forward through the stars, 0 to 1.5 star depths a second.
double FlyFromParam( float value );
/// Meteors: per minute. Exactly 0 at the bottom, then 0.5 to 120, geometrically.
double MeteorsFromParam( float value );
/// SN Speed: days of the light curve a second, 2 to 200, geometrically.
double SupernovaSpeedFromParam( float value );
/// Light Echo: 0 to 1.
double EchoFromParam( float value );
/// Exposure: -4 to +4 stops.
double ExposureFromParam( float value );
/// Stretch: asinh's softening Q, 0 (linear) to 40.
double StretchFromParam( float value );
/// Saturation: 0 to 2.
double SaturationFromParam( float value );

constexpr double kPi = 3.14159265358979323846;

} // namespace sidereal
