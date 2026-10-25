#include "Events.h"

#include "Physics.h"

#include <algorithm>
#include <cmath>

namespace sidereal
{
namespace
{
/// One frame height of the picture, as an angle on the sky: twenty degrees.
constexpr double kFrameAngle = 20.0 * physics::kPi / 180.0;
/// A meteor's distance from the observer, km (100 km up, 40 degrees up).
constexpr double kMeteorRange = 150.0;
/// A shower's speed, km/s: the Perseids'.
constexpr double kShowerSpeed = 59.0;
} // namespace

float Events::uniform()
{
	rng = rng * 6364136223846793005ull + 1442695040888963407ull;
	return static_cast< float >( ( rng >> 40 ) & 0xFFFFFF ) * ( 1.0f / 16777216.0f );
}

void Events::Reset()
{
	meteors.clear();
	launched.clear();
	nova      = Supernova {};
	novaStart = -1.0;
	carry     = 0.0;
}

Meteor Events::launch( double now, const EventInputs& in, bool pressed )
{
	Meteor m;
	m.start = now;
	// Where it is first seen: anywhere on or just off the picture. A pressed
	// one is somebody asking to see a meteor, so its trail is placed by its
	// middle, well inside any picture from 4:3 to 21:9, and it is a fireball
	// on a shallow path: one that starts off the edge heading out, or a faint
	// one gone in a few frames, was a press that did nothing.
	m.x = ( uniform() * 2.0f - 1.0f ) * ( pressed ? 0.55f : 1.0f );
	m.y = ( uniform() * 2.0f - 1.0f ) * ( pressed ? 0.32f : 0.6f );
	const float midX = m.x, midY = m.y;
	double sinD = 1.0;
	float spin  = 0.0f;
	if( !in.shower )
	{
		spin    = uniform() * 6.2831853f;
		sinD    = 0.35 + 0.65 * uniform();
		m.speed = 12.0f + 58.0f * uniform();
	}
	else
		m.speed = static_cast< float >( kShowerSpeed );
	// Entry angle above the horizon, then the classical curve: visible from
	// x = kMeteorStartX to x = 3, x growing as e^( v sin( entry ) t / H ).
	const double entry = ( pressed ? 15.0 + 20.0 * uniform() : 20.0 + 55.0 * uniform() ) * physics::kPi / 180.0;
	m.rate             = static_cast< float >( m.speed * std::sin( entry ) / physics::kScaleHeightKm );
	const double span  = std::log( 3.0 / physics::kMeteorStartX );
	m.duration         = static_cast< float >( span / m.rate );
	const double path  = m.speed * m.duration;// km along the path
	// Twice for a pressed shower meteor: once from its middle, then again from
	// where that puts its start, so it still lies exactly on its radiant line.
	for( int pass = 0; pass < ( pressed ? 2 : 1 ); ++pass )
	{
		if( in.shower )
		{
			// Parallel paths in space, seen in perspective: each trail lies on the
			// great circle through the radiant, moving away from it, and its length
			// on the sky goes as the sine of its distance from the radiant.
			const double ex = m.x - in.radiantX, ey = m.y - in.radiantY;
			const double d  = std::sqrt( ex * ex + ey * ey );
			m.dx            = static_cast< float >( ex / d );
			m.dy            = static_cast< float >( ey / d );
			sinD            = std::sin( std::min( d * kFrameAngle, physics::kPi * 0.5 ) );
		}
		else
		{
			m.dx = std::cos( spin );
			m.dy = std::sin( spin );
		}
		m.length = static_cast< float >( path * sinD / kMeteorRange / kFrameAngle );
		if( pressed )
		{
			const float half = 0.5f * std::min( m.length, 0.8f );
			m.x              = midX - m.dx * half;
			m.y              = midY - m.dy * half;
		}
	}
	// Brightness: a steep mass function, most faint, a few fireballs.
	m.peak = pressed ? 40.0f * std::pow( std::max( uniform(), 0.01f ), -0.5f )
	                 : 6.0f * std::pow( std::max( uniform(), 0.002f ), -0.9f );
	m.fast  = std::clamp( ( m.speed - 20.0f ) / 40.0f, 0.0f, 1.0f );
	m.train = m.fast > 0.6f ? 0.12f * std::min( m.peak / 30.0f, 1.0f ) : 0.0f;
	m.trainLife = 0.3f + 1.2f * std::min( m.peak / 60.0f, 1.0f );
	return m;
}

void Events::Advance( double now, double dt, const EventInputs& in, uint32_t seed )
{
	if( !seeded )
	{
		rng    = 0x9e3779b97f4a7c15ull ^ ( static_cast< uint64_t >( seed ) * 0x2545F4914F6CDD1Dull );
		seeded = true;
	}

	// Retire what has burnt out and whose train has faded.
	meteors.erase( std::remove_if( meteors.begin(), meteors.end(),
	                               [ & ]( const Meteor& m ) { return now > m.start + m.duration + 5.0 * m.trainLife + 0.2; } ),
	               meteors.end() );

	// Poisson arrivals at the rate, carried between frames so a slow rate
	// still comes out right on average; and one for every press.
	int arrivals = in.presses;
	if( in.perMinute > 0.0 && dt > 0.0 )
	{
		// `carry` is the expected count still to run before the next arrival:
		// exponential, drawn when one arrives, spent at the rate.
		if( carry <= 0.0 )
			carry = -std::log( std::max( 1.0 - uniform(), 1e-7 ) );
		carry -= in.perMinute / 60.0 * dt;
		while( carry <= 0.0 && arrivals < 8 )
		{
			++arrivals;
			carry += -std::log( std::max( 1.0 - uniform(), 1e-7 ) );
		}
	}
	for( int i = 0; i < arrivals && meteors.size() < 24; ++i )
	{
		meteors.push_back( launch( now, in, i < in.presses ) );
		if( launched.size() < 4096 )
			launched.push_back( meteors.back() );
	}

	// The supernova.
	if( in.supernova )
	{
		novaStart = now;
		nova      = Supernova {};
	}
	if( novaStart >= 0.0 )
	{
		nova.active = true;
		nova.x      = in.snX;
		nova.y      = in.snY;
		nova.days   = ( now - novaStart ) * in.daysPerSecond;
		nova.flux   = static_cast< float >( kPeakFlux * physics::SupernovaLight( nova.days ) );
		nova.temperature = static_cast< float >( physics::SupernovaTemperature( nova.days ) );
		const double age   = nova.days / kSedovDays;
		nova.remnantRadius = static_cast< float >( kSedovRadius * physics::RemnantRadius( age ) );
		// The shell lights as it sweeps up gas and dims as it slows.
		nova.remnantGlow = static_cast< float >( std::min( age, 1.0 ) * std::exp( -nova.days / kFadeDays ) );
		nova.echoCt      = static_cast< float >( std::max( nova.days - physics::SupernovaPeakDay(), 0.0 ) * kLightPerDay );
		if( nova.days > 4.0 * kFadeDays )
		{
			novaStart   = -1.0;
			nova.active = false;
		}
	}
}

} // namespace sidereal
