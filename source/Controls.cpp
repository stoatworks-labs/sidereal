#include "Controls.h"

#include "Physics.h"
#include "Scene.h"

#include <algorithm>
#include <cmath>

namespace sidereal
{
namespace
{
constexpr int kTelescopeOptions = 1 + physics::kTelescopeChoices;

const ControlInfo kInfo[ PT_ABOUT_TEXT ] = {
	{ PT_TARGET, "Target", "Scene", Kind::Option, 0.0f, 0 },
	{ PT_TELESCOPE, "Telescope", "Scene", Kind::Option, 0.0f, kTelescopeOptions },
	{ PT_SEED, "Seed", "Scene", Kind::Standard, 0.0f, 0 },

	{ PT_GAS, "Gas Density", "Gas", Kind::Standard, 0.5f, 0 },
	{ PT_TURBULENCE, "Turbulence", "Gas", Kind::Standard, 0.5f, 0 },
	{ PT_FLOW, "Flow", "Gas", Kind::Standard, 0.25f, 0 },
	{ PT_HAZE, "Haze", "Gas", Kind::Standard, 0.5f, 0 },
	{ PT_DUST, "Dust", "Gas", Kind::Standard, 0.5f, 0 },

	{ PT_STARLIGHT, "Starlight", "Light", Kind::Standard, 0.5f, 0 },
	{ PT_STAR_HEAT, "Star Heat", "Light", Kind::Standard, 0.5f, 0 },
	{ PT_LIGHT_ANGLE, "Light Angle", "Light", Kind::Standard, 0.5f, 0 },

	{ PT_STARS, "Stars", "Stars", Kind::Standard, 0.5f, 0 },
	{ PT_SPIKES, "Spikes", "Stars", Kind::Standard, 0.5f, 0 },
	{ PT_SEEING, "Seeing", "Stars", Kind::Standard, 0.0f, 0 },

	{ PT_ZOOM, "Zoom", "Camera", Kind::Standard, 0.5f, 0 },
	{ PT_PAN_X, "Pan X", "Camera", Kind::Standard, 0.5f, 0 },
	{ PT_PAN_Y, "Pan Y", "Camera", Kind::Standard, 0.5f, 0 },
	{ PT_SPIN, "Spin", "Camera", Kind::Standard, 0.5f, 0 },
	{ PT_DRIFT, "Drift", "Camera", Kind::Standard, 0.25f, 0 },
	{ PT_DEPTH, "Depth", "Camera", Kind::Standard, 0.5f, 0 },
	{ PT_FLY, "Fly", "Camera", Kind::Standard, 0.0f, 0 },

	{ PT_METEORS, "Meteors", "Events", Kind::Standard, 0.0f, 0 },
	{ PT_METEOR_KIND, "Meteor Kind", "Events", Kind::Option, 1.0f, static_cast< int >( MeteorKind::Count ) },
	{ PT_FIRE_METEOR, "Fire Meteor", "Events", Kind::Event, 0.0f, 0 },
	{ PT_SUPERNOVA, "Supernova", "Events", Kind::Event, 0.0f, 0 },
	{ PT_SN_SPEED, "SN Speed", "Events", Kind::Standard, 0.5f, 0 },
	{ PT_ECHO, "Light Echo", "Events", Kind::Standard, 0.5f, 0 },

	{ PT_EXPOSURE, "Exposure", "Output", Kind::Standard, 0.5f, 0 },
	{ PT_STRETCH, "Stretch", "Output", Kind::Standard, 0.5f, 0 },
	{ PT_SATURATION, "Saturation", "Output", Kind::Standard, 0.5f, 0 },
	{ PT_CLIP_AS, "Clip As", "Output", Kind::Option, 0.0f, static_cast< int >( ClipAs::Count ) },
	{ PT_MIX, "Mix", "Output", Kind::Standard, 1.0f, 0 },
};

/// Linear in two halves: 0 at 0, 1 at 0.5, hi at 1.
double twoHalves( float value, double hi )
{
	const double v = std::clamp( static_cast< double >( value ), 0.0, 1.0 );
	return v <= 0.5 ? 2.0 * v : 1.0 + ( hi - 1.0 ) * ( 2.0 * v - 1.0 );
}

double geometric( float value, double lo, double hi )
{
	const double v = std::clamp( static_cast< double >( value ), 0.0, 1.0 );
	return lo * std::pow( hi / lo, v );
}
} // namespace

const ControlInfo& InfoOf( unsigned int id )
{
	static ControlInfo target = kInfo[ PT_TARGET ];
	if( id == PT_TARGET )
	{
		target.optionCount = scene::TargetCount();
		return target;
	}
	return kInfo[ std::min( id, static_cast< unsigned int >( PT_ABOUT_TEXT ) - 1 ) ];
}

const char* OptionName( unsigned int id, int index )
{
	switch( id )
	{
	case PT_TARGET: return scene::TargetName( index );
	case PT_TELESCOPE: return index == 0 ? "As Released" : physics::InstrumentOf( index - 1 ).name;
	case PT_METEOR_KIND:
	{
		static const char* const names[] = { "Sporadic", "Shower" };
		return names[ std::clamp( index, 0, 1 ) ];
	}
	case PT_CLIP_AS:
	{
		static const char* const names[] = { "Behind", "Gas" };
		return names[ std::clamp( index, 0, 1 ) ];
	}
	default: return "";
	}
}

const std::vector< unsigned int >& HostOrder( bool effect )
{
	static const std::vector< unsigned int > source = [] {
		std::vector< unsigned int > order;
		for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
			if( id != PT_CLIP_AS && id != PT_MIX )
				order.push_back( id );
		for( unsigned int id = PT_ABOUT_TEXT; id < PT_COUNT; ++id )
			order.push_back( id );
		return order;
	}();
	static const std::vector< unsigned int > over = [] {
		std::vector< unsigned int > order;
		for( unsigned int id = 0; id < PT_COUNT; ++id )
			order.push_back( id );
		return order;
	}();
	return effect ? over : source;
}

int OptionIndex( float value, int count )
{
	return std::clamp( static_cast< int >( std::lround( value ) ), 0, std::max( count - 1, 0 ) );
}

double Multiple( float value, double lo, double hi )
{
	const double v = std::clamp( static_cast< double >( value ), 0.0, 1.0 );
	return v <= 0.5 ? std::pow( lo, 1.0 - 2.0 * v ) : std::pow( hi, 2.0 * v - 1.0 );
}

float ParamFromMultiple( double multiple, double lo, double hi )
{
	if( multiple <= 1.0 )
		return static_cast< float >( 0.5 * ( 1.0 - std::log( multiple ) / std::log( lo ) ) );
	return static_cast< float >( 0.5 * ( 1.0 + std::log( multiple ) / std::log( hi ) ) );
}

double GasFromParam( float value )
{
	return Multiple( value, 0.25, 4.0 );
}
double TurbulenceFromParam( float value )
{
	return twoHalves( value, 3.0 );
}
double FlowFromParam( float value )
{
	return 0.12 * std::clamp( static_cast< double >( value ), 0.0, 1.0 );
}
double HazeFromParam( float value )
{
	return twoHalves( value, 3.0 );
}
double DustFromParam( float value )
{
	return Multiple( value, 0.1, 10.0 );
}
double StarlightFromParam( float value )
{
	return Multiple( value, 0.1, 10.0 );
}
double HeatFromParam( float value )
{
	return Multiple( value, 0.6, 1.6 );
}
double LightAngleFromParam( float value )
{
	return ( std::clamp( static_cast< double >( value ), 0.0, 1.0 ) - 0.5 ) * 2.0 * kPi;
}
double StarsFromParam( float value )
{
	return twoHalves( value, 3.0 );
}
double SpikesFromParam( float value )
{
	return twoHalves( value, 3.0 );
}
double SeeingFromParam( float value )
{
	return 8.0 * std::clamp( static_cast< double >( value ), 0.0, 1.0 );
}
double ZoomFromParam( float value )
{
	return Multiple( value, 0.25, 4.0 );
}
double PanFromParam( float value )
{
	return ( std::clamp( static_cast< double >( value ), 0.0, 1.0 ) - 0.5 ) * 2.0;
}
double SpinFromParam( float value )
{
	return ( std::clamp( static_cast< double >( value ), 0.0, 1.0 ) - 0.5 ) * 2.0 * kPi;
}
double DriftFromParam( float value )
{
	return 0.05 * std::clamp( static_cast< double >( value ), 0.0, 1.0 );
}
double DepthFromParam( float value )
{
	return 2.0 * std::clamp( static_cast< double >( value ), 0.0, 1.0 );
}
double FlyFromParam( float value )
{
	return 1.5 * std::clamp( static_cast< double >( value ), 0.0, 1.0 );
}
double MeteorsFromParam( float value )
{
	return value <= 0.0f ? 0.0 : geometric( value, 0.5, 120.0 );
}
double SupernovaSpeedFromParam( float value )
{
	return geometric( value, 2.0, 200.0 );
}
double EchoFromParam( float value )
{
	return std::clamp( static_cast< double >( value ), 0.0, 1.0 );
}
double ExposureFromParam( float value )
{
	return ( std::clamp( static_cast< double >( value ), 0.0, 1.0 ) - 0.5 ) * 8.0;
}
double StretchFromParam( float value )
{
	const double v = std::clamp( static_cast< double >( value ), 0.0, 1.0 );
	return v <= 0.0 ? 0.0 : geometric( static_cast< float >( v ), 0.3, 40.0 );
}
double SaturationFromParam( float value )
{
	return 2.0 * std::clamp( static_cast< double >( value ), 0.0, 1.0 );
}

} // namespace sidereal
