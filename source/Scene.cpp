#include "Scene.h"

#include "Physics.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace sidereal::scene
{
using namespace physics;

//---------------------------------------------------------------------------
Random::Random( uint64_t seed ) :
	state( seed * 6364136223846793005ull + 1442695040888963407ull )
{
	next();
}
uint32_t Random::next()
{
	const uint64_t old = state;
	state              = old * 6364136223846793005ull + 1442695040888963407ull;
	const uint32_t xorshifted = static_cast< uint32_t >( ( ( old >> 18u ) ^ old ) >> 27u );
	const uint32_t rot        = static_cast< uint32_t >( old >> 59u );
	return ( xorshifted >> rot ) | ( xorshifted << ( ( 32u - rot ) & 31u ) );
}
float Random::uniform()
{
	return static_cast< float >( next() >> 8 ) * ( 1.0f / 16777216.0f );
}
float Random::range( float lo, float hi )
{
	return lo + ( hi - lo ) * uniform();
}
float Random::normal()
{
	const float u1 = std::max( uniform(), 1e-7f ), u2 = uniform();
	return std::sqrt( -2.0f * std::log( u1 ) ) * std::cos( 6.2831853f * u2 );
}

namespace
{
const char* const kNames[] = {
	"Pillars",        "Cosmic Cliffs", "Horsehead",     "Orion",          "Crab",
	"Ring",           "Southern Ring", "Helix",         "Veil",           "Pleiades",
	"Andromeda",      "Whirlpool",     "Sombrero",      "Ultra Deep",     "First Deep",
	"Gen Nebula",     "Gen Dark",      "Gen Planetary", "Gen Remnant",    "Gen Spiral",
	"Gen Cluster",    "Gen Starfield",
};
constexpr int kTargetCount   = static_cast< int >( sizeof( kNames ) / sizeof( kNames[ 0 ] ) );
constexpr int kFirstGenerative = 15;

/// A published picture's ( u, v ) -> the frame, filling its height.
struct Picture
{
	float aspect;
	float X( float u ) const
	{
		return ( u - 0.5f ) * aspect;
	}
	float Y( float v ) const
	{
		return 0.5f - v;
	}
};

void addPolygon( Target& t, const Picture& p, std::initializer_list< float > uv, Element e )
{
	e.shape     = Shape::Polygon;
	e.polyStart = static_cast< int >( t.polygon.size() / 2 );
	e.polyCount = static_cast< int >( uv.size() / 2 );
	const float* q = uv.begin();
	for( size_t i = 0; i + 1 < uv.size(); i += 2 )
	{
		t.polygon.push_back( p.X( q[ i ] ) );
		t.polygon.push_back( p.Y( q[ i + 1 ] ) );
	}
	t.elements.push_back( e );
}

Element dense( float density, float thickness, float edge, float rough, float detail )
{
	Element e;
	e.channel   = Channel::Dense;
	e.density   = density;
	e.thickness = thickness;
	e.edge      = edge;
	e.rough     = rough;
	e.detail    = detail;
	return e;
}

Element blob( Channel channel, float x, float y, float a, float b, float angle, float density, float thickness, float edge, float rough,
              float detail )
{
	Element e;
	e.shape     = Shape::Blob;
	e.channel   = channel;
	e.x         = x;
	e.y         = y;
	e.a         = a;
	e.b         = b;
	e.angle     = angle;
	e.density   = density;
	e.thickness = thickness;
	e.edge      = edge;
	e.rough     = rough;
	e.detail    = detail;
	return e;
}

Element column( float x, float y, float x2, float y2, float r0, float r1, float density, float thickness, float rough )
{
	Element e;
	e.shape     = Shape::Column;
	e.channel   = Channel::Dense;
	e.x         = x;
	e.y         = y;
	e.x2        = x2;
	e.y2        = y2;
	e.a         = r0;
	e.b         = r1;
	e.density   = density;
	e.thickness = thickness;
	e.edge      = 0.25f * r1 + 0.004f;
	e.rough     = rough;
	e.detail    = 0.55f;
	return e;
}

Element sheet( Channel channel, float x, float y, float dx, float dy, float density, float thickness, float edge, float detail,
               float anisotropy )
{
	Element e;
	e.shape     = Shape::Sheet;
	e.channel   = channel;
	e.x         = x;
	e.y         = y;
	const float l = std::sqrt( dx * dx + dy * dy );
	e.x2        = dx / l;
	e.y2        = dy / l;
	e.density   = density;
	e.thickness = thickness;
	e.edge      = edge;
	e.detail    = detail;
	e.rough     = 0.0f;
	e.extra2    = anisotropy;
	return e;
}

Element shell( Channel channel, float x, float y, float a, float b, float angle, float fraction, float density, float thickness,
               float knots )
{
	Element e;
	e.shape     = Shape::Shell;
	e.channel   = channel;
	e.x         = x;
	e.y         = y;
	e.a         = a;
	e.b         = b;
	e.angle     = angle;
	e.extra     = fraction;
	e.extra2    = knots;
	e.density   = density;
	e.thickness = thickness;
	e.edge      = 0.004f;
	e.rough     = 0.01f;
	e.detail    = 0.5f;
	return e;
}

Element filaments( Channel channel, float x, float y, float a, float b, float angle, float density, float scale, float anisotropy )
{
	Element e;
	e.shape     = Shape::Filaments;
	e.channel   = channel;
	e.x         = x;
	e.y         = y;
	e.a         = a;
	e.b         = b;
	e.angle     = angle;
	e.density   = density;
	e.thickness = 0.05f;
	e.edge      = 0.08f;
	e.rough     = 0.0f;
	e.detail    = 0.6f;
	e.extra     = scale;
	e.extra2    = anisotropy;
	return e;
}

Source source( float x, float y, float h, float luminosity, float temperature, bool visible = false )
{
	Source s;
	s.x           = x;
	s.y           = y;
	s.h           = h;
	s.luminosity  = luminosity;
	s.temperature = temperature;
	s.visible     = visible;
	return s;
}

void star( Target& t, float x, float y, float flux, float temperature, int layer = 1, float depth = 1.0f )
{
	Star s;
	s.x           = x;
	s.y           = y;
	s.flux        = flux;
	s.temperature = temperature;
	s.layer       = layer;
	s.depth       = depth;
	t.stars.push_back( s );
}

/// A star's temperature, drawn from what a field of bright stars holds: many
/// K giants, some G and F, a few hot A and B stars.
float fieldTemperature( Random& r )
{
	const float u = r.uniform();
	if( u < 0.45f )
		return r.range( 3300.0f, 4900.0f );
	if( u < 0.78f )
		return r.range( 5000.0f, 7200.0f );
	if( u < 0.95f )
		return r.range( 7500.0f, 11000.0f );
	return r.range( 12000.0f, 30000.0f );
}

/// Bright field stars: fluxes from a Euclidean count, N( > F ) ~ F^-1.5.
void fieldStars( Target& t, Random& r, int count, float fluxMin, float fluxMax, float halfWidth = 1.1f, float halfHeight = 0.62f )
{
	for( int i = 0; i < count; ++i )
	{
		const float flux = std::min( fluxMax, fluxMin * std::pow( std::max( r.uniform(), 1e-4f ), -2.0f / 3.0f ) );
		const float x = r.range( -halfWidth, halfWidth ), y = r.range( -halfHeight, halfHeight );
		const int layer = r.uniform() < 0.35f ? 0 : 1;
		star( t, x, y, flux, fieldTemperature( r ), layer, r.range( 0.5f, 3.0f ) );
	}
}

/// A cluster of hot young stars, King-like: dense core, sparse halo.
void cluster( Target& t, Random& r, float x, float y, float radius, int count, float flux )
{
	for( int i = 0; i < count; ++i )
	{
		const float rr = radius * std::sqrt( r.uniform() / std::max( 1.0f - r.uniform() * 0.9f, 0.05f ) ) * 0.4f;
		const float a  = r.range( 0.0f, 6.2831853f );
		star( t, x + rr * std::cos( a ), y + rr * std::sin( a ), flux * std::pow( r.range( 0.05f, 1.0f ), 1.5f ),
		      r.range( 14000.0f, 42000.0f ), 0, 1.0f );
	}
}

//===========================================================================
// The pictures.
//===========================================================================

/// Pillars of Creation, M16: Hubble's 2014 WFC3/UVIS framing (1280 x 1335).
/// Outlines traced from the release; NGC 6611 is off the top of the frame.
Target pillars( Random& r )
{
	Target t;
	t.name       = "Pillars";
	t.asReleased = I_HUBBLE_NARROW;
	const Picture p { 1280.0f / 1335.0f };
	Element d = dense( 1.0f, 0.09f, 0.006f, 0.022f, 0.75f );
	// The left pillar, its head and long neck.
	addPolygon( t, p,
	            { 0.36f, 0.135f, 0.42f, 0.14f, 0.46f, 0.165f, 0.47f, 0.20f, 0.44f, 0.235f, 0.41f, 0.26f, 0.405f, 0.32f, 0.42f, 0.40f, 0.44f, 0.43f,
	              0.43f, 0.47f, 0.45f, 0.52f, 0.47f, 0.60f, 0.47f, 0.70f, 0.48f, 0.78f, 0.30f, 0.80f, 0.25f, 0.74f, 0.24f, 0.62f, 0.25f, 0.52f,
	              0.27f, 0.45f, 0.28f, 0.36f, 0.29f, 0.28f, 0.29f, 0.22f, 0.30f, 0.17f },
	            d );
	// The middle pillar.
	addPolygon( t, p,
	            { 0.585f, 0.265f, 0.61f, 0.28f, 0.62f, 0.33f, 0.61f, 0.40f, 0.62f, 0.45f, 0.64f, 0.50f, 0.65f, 0.60f, 0.66f, 0.70f, 0.66f, 0.80f,
	              0.50f, 0.80f, 0.47f, 0.70f, 0.48f, 0.60f, 0.50f, 0.50f, 0.52f, 0.42f, 0.535f, 0.35f, 0.55f, 0.30f, 0.565f, 0.27f },
	            d );
	// The right pillar.
	addPolygon( t, p,
	            { 0.715f, 0.41f, 0.735f, 0.43f, 0.74f, 0.50f, 0.73f, 0.57f, 0.735f, 0.62f, 0.72f, 0.70f, 0.70f, 0.80f, 0.65f, 0.80f, 0.66f, 0.70f,
	              0.67f, 0.60f, 0.68f, 0.52f, 0.69f, 0.45f, 0.70f, 0.42f },
	            d );
	// The cloud they stand on.
	Element base = dense( 1.3f, 0.2f, 0.015f, 0.02f, 0.5f );
	addPolygon( t, p, { 0.22f, 1.08f, 0.20f, 0.90f, 0.24f, 0.80f, 0.35f, 0.77f, 0.50f, 0.76f, 0.62f, 0.76f, 0.72f, 0.78f, 0.78f, 0.85f, 0.80f, 0.95f, 0.82f, 1.08f },
	            base );
	// The evaporating gaseous globule off to the right.
	t.elements.push_back( blob( Channel::Dense, p.X( 0.885f ), p.Y( 0.905f ), 0.010f, 0.016f, 0.3f, 1.0f, 0.03f, 0.004f, 0.004f, 0.3f ) );
	// The H II region's far wall, brighter towards the cluster.
	t.elements.push_back( sheet( Channel::Haze, 0.0f, -0.9f, 0.0f, 1.0f, 0.18f, 1.0f, 1.6f, 0.6f, 0.0f ) );
	t.haze      = 0.07f;
	t.hazeScale = 2.5f;
	t.warpScale = 5.0f;
	t.warp      = 0.012f;
	t.exposure  = -0.3f;
	// NGC 6611, above and a little in front.
	t.sources.push_back( source( 0.10f, 1.05f, 0.35f, 1.6f, 42000.0f ) );
	t.sources.push_back( source( -0.45f, 0.95f, 0.5f, 0.6f, 38000.0f ) );
	star( t, p.X( 0.11f ), p.Y( 0.27f ), 40.0f, 4200.0f, 0 );
	star( t, p.X( 0.32f ), p.Y( 0.035f ), 18.0f, 4800.0f, 0 );
	star( t, p.X( 0.41f ), p.Y( 0.37f ), 10.0f, 6500.0f, 0 );
	star( t, p.X( 0.95f ), p.Y( 0.82f ), 6.0f, 5200.0f, 1 );
	fieldStars( t, r, 26, 0.6f, 12.0f );
	t.fieldStars = 900;
	t.embedded   = 350;
	t.dustRatio  = 4.5f;
	t.snX        = p.X( 0.8f );
	t.snY        = p.Y( 0.2f );
	t.echoDistance = 0.08f;
	return t;
}

/// Cosmic Cliffs, NGC 3324 in Carina: Webb's NIRCam release (1280 x 742).
Target cliffs( Random& r )
{
	Target t;
	t.name       = "Cosmic Cliffs";
	t.asReleased = I_CLIFFS;
	const Picture p { 1280.0f / 742.0f };
	Element d = dense( 1.1f, 0.25f, 0.01f, 0.05f, 0.85f );
	addPolygon( t, p,
	            { -0.06f, 0.24f, 0.0f, 0.22f, 0.04f, 0.21f, 0.08f, 0.24f, 0.12f, 0.27f, 0.155f, 0.30f, 0.18f, 0.29f, 0.19f, 0.265f, 0.197f, 0.30f,
	              0.21f, 0.37f, 0.25f, 0.44f, 0.30f, 0.43f, 0.35f, 0.41f, 0.40f, 0.38f, 0.44f, 0.39f, 0.48f, 0.38f, 0.52f, 0.41f, 0.56f, 0.40f,
	              0.60f, 0.43f, 0.64f, 0.41f, 0.70f, 0.40f, 0.76f, 0.36f, 0.82f, 0.34f, 0.87f, 0.30f, 0.92f, 0.26f, 0.96f, 0.23f, 1.0f, 0.22f,
	              1.06f, 0.25f, 1.06f, 1.08f, -0.06f, 1.08f },
	            d );
	// Steam: the ionised flow off the cliff tops, streaked upwards.
	Element steam = sheet( Channel::Haze, 0.0f, 0.2f, 0.0f, -1.0f, 0.3f, 0.6f, 0.3f, 0.8f, 5.0f );
	t.elements.push_back( steam );
	// The glowing knot at the left, and a jet.
	t.elements.push_back( blob( Channel::Jets, p.X( 0.075f ), p.Y( 0.45f ), 0.045f, 0.03f, 0.6f, 0.5f, 0.05f, 0.03f, 0.02f, 1.0f ) );
	t.elements.push_back( blob( Channel::Jets, p.X( 0.42f ), p.Y( 0.47f ), 0.01f, 0.035f, -0.2f, 0.35f, 0.05f, 0.012f, 0.008f, 1.0f ) );
	t.haze      = 0.06f;
	t.hazeScale = 3.0f;
	t.warpScale = 4.0f;
	t.warp      = 0.025f;
	t.sources.push_back( source( 0.05f, 1.0f, 0.6f, 1.4f, 44000.0f ) );
	star( t, p.X( 0.36f ), p.Y( 0.11f ), 30.0f, 7000.0f, 0 );
	star( t, p.X( 0.405f ), p.Y( 0.205f ), 22.0f, 9000.0f, 0 );
	star( t, p.X( 0.87f ), p.Y( 0.245f ), 25.0f, 5500.0f, 0 );
	star( t, p.X( 0.76f ), p.Y( 0.31f ), 12.0f, 6000.0f, 1 );
	star( t, p.X( 0.93f ), p.Y( 0.95f ), 15.0f, 4500.0f, 0 );
	fieldStars( t, r, 24, 0.6f, 10.0f );
	t.fieldStars = 1600;
	t.embedded   = 400;
	t.dustRatio  = 4.0f;
	t.snX        = p.X( 0.6f );
	t.snY        = p.Y( 0.15f );
	return t;
}

/// The Horsehead, Barnard 33, against IC 434: framed as ESO's 2002 VLT
/// picture (1280 x 1317); sigma Orionis lights it from above.
Target horsehead( Random& r )
{
	Target t;
	t.name       = "Horsehead";
	t.asReleased = I_HUBBLE_COLOUR;
	const Picture p { 1280.0f / 1317.0f };
	Element d = dense( 1.6f, 0.18f, 0.012f, 0.02f, 0.5f );
	addPolygon( t, p,
	            { 0.18f, 0.345f, 0.24f, 0.49f, 0.28f, 0.51f, 0.38f, 0.56f, 0.34f, 0.71f, 0.28f, 0.86f, 0.18f, 1.06f, 0.90f, 1.06f, 0.83f, 0.79f,
	              0.85f, 0.60f, 0.86f, 0.43f, 0.80f, 0.26f, 0.62f, 0.18f, 0.46f, 0.14f, 0.40f, 0.18f, 0.32f, 0.24f, 0.22f, 0.30f },
	            d );
	// L1630, the cloud the horse rises from.
	addPolygon( t, p, { -0.25f, 0.88f, 0.10f, 0.92f, 0.30f, 0.95f, 0.60f, 0.97f, 1.25f, 0.95f, 1.25f, 1.1f, -0.25f, 1.1f },
	            dense( 1.4f, 0.4f, 0.04f, 0.03f, 0.6f ) );
	// IC 434: the cloud's ionised edge, a curtain streaked towards the star.
	t.elements.push_back( sheet( Channel::Haze, 0.0f, -0.65f, 0.0f, 1.0f, 0.5f, 1.0f, 0.9f, 0.6f, 4.0f ) );
	t.haze      = 0.05f;
	t.hazeScale = 3.0f;
	t.warpScale = 4.5f;
	t.warp      = 0.015f;
	t.hazeShade = 0.0f;
	// Behind the cloud: IC 434 is the far face's ionised skin, and the horse
	// is a silhouette against it.
	t.sources.push_back( source( 0.25f, 2.6f, -0.6f, 9.0f, 33000.0f ) );
	star( t, p.X( 0.04f ), p.Y( 0.31f ), 14.0f, 6000.0f, 0 );
	star( t, p.X( 0.79f ), p.Y( 0.07f ), 12.0f, 5200.0f, 0 );
	star( t, p.X( 0.84f ), p.Y( 0.98f ), 10.0f, 7000.0f, 0 );
	fieldStars( t, r, 18, 0.5f, 6.0f );
	t.fieldStars = 900;
	t.embedded   = 150;
	t.dustRatio  = 3.5f;
	t.snX        = p.X( 0.7f );
	t.snY        = p.Y( 0.12f );
	return t;
}

/// The Orion Nebula, M42: the Trapezium's blister on the face of OMC-1,
/// Hubble's 2006 ACS framing.
Target orion( Random& r )
{
	Target t;
	t.name       = "Orion";
	t.asReleased = I_HUBBLE_COLOUR;
	// The background molecular cloud, patchy, all round.
	t.elements.push_back( blob( Channel::Dense, -0.55f, 0.25f, 0.45f, 0.3f, 0.5f, 0.55f, 0.6f, 0.15f, 0.06f, 0.8f ) );
	t.elements.push_back( blob( Channel::Dense, 0.65f, -0.3f, 0.4f, 0.28f, -0.4f, 0.5f, 0.6f, 0.15f, 0.06f, 0.8f ) );
	t.elements.push_back( blob( Channel::Dense, 0.6f, 0.45f, 0.35f, 0.2f, 0.3f, 0.45f, 0.5f, 0.12f, 0.05f, 0.8f ) );
	// The Orion Bar: the ionisation front seen edge-on, south-east of the Trapezium.
	// Thin enough to let light through and tapered at its near end: a dense,
	// blunt Bar threw a hard black wedge of shadow to the corner of the frame.
	t.elements.push_back( column( 0.05f, -0.24f, 0.38f, -0.08f, 0.008f, 0.025f, 0.4f, 0.12f, 0.01f ) );
	// The Dark Bay: dust in front, intruding from the north-east.
	t.elements.push_back( blob( Channel::Foreground, -0.32f, 0.12f, 0.2f, 0.07f, -0.5f, 1.2f, 0.1f, 0.05f, 0.03f, 0.7f ) );
	t.elements.push_back( blob( Channel::Foreground, -0.15f, 0.05f, 0.09f, 0.035f, -0.3f, 0.9f, 0.1f, 0.03f, 0.02f, 0.7f ) );
	// The bright core and the wings.
	t.elements.push_back( blob( Channel::Haze, 0.0f, 0.0f, 0.22f, 0.18f, 0.2f, 0.6f, 0.5f, 0.15f, 0.03f, 0.6f ) );
	t.elements.push_back( blob( Channel::Haze, -0.25f, -0.25f, 0.4f, 0.18f, 0.8f, 0.25f, 0.6f, 0.2f, 0.06f, 0.7f ) );
	t.elements.push_back( blob( Channel::Haze, 0.35f, 0.15f, 0.45f, 0.2f, -0.6f, 0.2f, 0.6f, 0.2f, 0.06f, 0.7f ) );
	// M43, De Mairan's nebula, to the north with its own star.
	t.elements.push_back( blob( Channel::Haze, 0.05f, 0.42f, 0.09f, 0.08f, 0.0f, 0.4f, 0.3f, 0.05f, 0.02f, 0.5f ) );
	t.elements.push_back( blob( Channel::Dense, 0.0f, 0.37f, 0.03f, 0.1f, 0.3f, 0.9f, 0.1f, 0.02f, 0.01f, 0.5f ) );
	t.haze      = 0.05f;
	t.hazeScale = 3.0f;
	t.warpScale = 3.5f;
	t.warp      = 0.03f;
	// Theta-1 Orionis C and its companions, in front of the cloud.
	t.sources.push_back( source( 0.0f, 0.0f, 0.12f, 0.22f, 39000.0f, true ) );
	t.sources.push_back( source( 0.06f, 0.43f, 0.06f, 0.05f, 26000.0f, true ) );
	star( t, -0.012f, 0.008f, 30.0f, 32000.0f, 0 );
	star( t, 0.01f, -0.01f, 25.0f, 30000.0f, 0 );
	star( t, 0.004f, 0.018f, 20.0f, 25000.0f, 0 );
	cluster( t, r, 0.0f, 0.0f, 0.4f, 30, 6.0f );
	fieldStars( t, r, 16, 0.5f, 6.0f );
	t.fieldStars = 1200;
	t.embedded   = 400;
	t.dustRatio  = 3.0f;
	t.snX        = 0.35f;
	t.snY        = 0.3f;
	return t;
}

/// The Crab, M1: Hubble's 2005 mosaic framing. A pulsar wind nebula in a
/// cage of shock-lit filaments.
Target crab( Random& r )
{
	Target t;
	t.name       = "Crab";
	t.mix        = RemnantMix();
	t.asReleased = I_HUBBLE_COLOUR;
	t.exposure   = 1.5f;
	t.elements.push_back( filaments( Channel::Shock, 0.0f, 0.0f, 0.43f, 0.30f, -0.55f, 1.6f, 9.0f, 0.4f ) );
	t.elements.push_back( filaments( Channel::Oxygen, 0.01f, 0.005f, 0.40f, 0.28f, -0.55f, 0.6f, 13.0f, 0.3f ) );
	t.elements.push_back( blob( Channel::Synchrotron, 0.0f, 0.0f, 0.36f, 0.25f, -0.55f, 1.2f, 0.3f, 0.2f, 0.02f, 0.5f ) );
	t.elements.push_back( blob( Channel::Synchrotron, 0.015f, 0.01f, 0.06f, 0.04f, -0.55f, 1.5f, 0.1f, 0.04f, 0.01f, 0.4f ) );
	t.haze = 0.0f;
	t.warpScale = 4.0f;
	t.warp      = 0.02f;
	star( t, 0.012f, 0.008f, 3.0f, 9000.0f, 0 );
	star( t, 0.03f, 0.02f, 2.0f, 6000.0f, 0 );
	fieldStars( t, r, 20, 0.5f, 8.0f );
	t.fieldStars = 700;
	t.dustRatio  = 2.0f;
	t.snX        = -0.5f;
	t.snY        = 0.3f;
	return t;
}

/// A planetary nebula's shell, lit from inside by its white dwarf.
void planetary( Target& t, float x, float y, float a, float b, float angle, float fraction, float knots, float halo, float wd,
                float open = 0.0f )
{
	t.mix      = PlanetaryMix();
	t.elements.push_back( shell( Channel::Dense, x, y, a, b, angle, fraction, 0.9f, 0.05f, knots ) );
	t.elements.back().x2 = open;
	if( halo > 0.0f )
	{
		t.elements.push_back( shell( Channel::Haze, x, y, a * 1.55f, b * 1.55f, angle, 0.35f, halo, 0.6f, 0.0f ) );
		t.elements.back().x2 = open;
		t.elements.push_back( shell( Channel::Haze, x, y, a * 2.1f, b * 2.1f, angle, 0.12f, 0.4f * halo, 0.6f, 0.0f ) );
		t.elements.back().x2 = open;
	}
	t.sources.push_back( source( x, y, 0.0f, wd, 110000.0f, true ) );
	t.sources.back().soft = ( 1.0f - fraction ) * std::min( a, b );
}

Target ring( Random& r )
{
	Target t;
	t.name       = "Ring";
	t.asReleased = I_RING_NIRCAM;
	// A barrel seen nearly down its axis: open. Two stops up, as Webb's
	// picture is: its rim is thin.
	planetary( t, 0.0f, 0.0f, 0.2f, 0.155f, 0.25f, 0.24f, 0.6f, 0.08f, 0.004f, 0.8f );
	t.exposure  = 2.0f;
	t.haze      = 0.0f;
	t.warpScale = 8.0f;
	t.warp      = 0.006f;
	fieldStars( t, r, 22, 0.5f, 8.0f );
	t.fieldStars = 700;
	t.dustRatio  = 1.5f;
	t.snX        = 0.55f;
	t.snY        = 0.3f;
	return t;
}

Target southernRing( Random& r )
{
	Target t;
	t.name       = "Southern Ring";
	t.asReleased = I_SOUTHERN_RING;
	planetary( t, 0.0f, 0.0f, 0.33f, 0.21f, 0.35f, 0.28f, 0.35f, 0.12f, 0.0016f, 0.6f );
	// The visible companion beside the white dwarf.
	star( t, 0.004f, 0.003f, 22.0f, 9500.0f, 0 );
	t.haze      = 0.0f;
	t.warpScale = 6.0f;
	t.warp      = 0.012f;
	fieldStars( t, r, 20, 0.5f, 8.0f );
	t.fieldStars = 1000;
	t.dustRatio  = 1.5f;
	t.snX        = -0.6f;
	t.snY        = 0.25f;
	return t;
}

Target helix( Random& r )
{
	Target t;
	t.name       = "Helix";
	t.asReleased = I_HELIX;
	planetary( t, 0.0f, 0.0f, 0.36f, 0.31f, 0.4f, 0.32f, 1.0f, 0.05f, 0.0016f, 0.7f );
	// The outer arc, offset: the two rings of a helix seen end on.
	t.elements.push_back( shell( Channel::Dense, 0.04f, -0.03f, 0.44f, 0.36f, 0.6f, 0.18f, 0.5f, 0.04f, 0.4f ) );
	t.elements.back().x2 = 0.7f;
	t.haze      = 0.0f;
	t.warpScale = 6.0f;
	t.warp      = 0.012f;
	fieldStars( t, r, 20, 0.5f, 8.0f );
	t.fieldStars = 900;
	t.dustRatio  = 1.5f;
	t.snX        = 0.6f;
	t.snY        = -0.3f;
	return t;
}

/// The Veil: a slice of the Cygnus Loop's shock front, Hubble's 2021 framing.
/// Oxygen and hydrogen filaments sit apart: the gas behind a shock cools
/// through [O III] first.
Target veil( Random& r )
{
	Target t;
	t.name       = "Veil";
	t.mix        = RemnantMix();
	t.asReleased = I_HELIX;
	t.elements.push_back( filaments( Channel::Shock, 0.0f, 0.0f, 1.3f, 0.9f, 0.5f, 1.4f, 4.0f, 0.85f ) );
	t.elements.push_back( filaments( Channel::Oxygen, 0.025f, 0.015f, 1.3f, 0.9f, 0.5f, 1.2f, 4.0f, 0.85f ) );
	t.haze = 0.0f;
	t.warpScale = 2.0f;
	t.warp      = 0.04f;
	fieldStars( t, r, 26, 0.5f, 8.0f );
	t.fieldStars = 2400;
	t.dustRatio  = 1.0f;
	t.snX        = 0.3f;
	t.snY        = 0.2f;
	return t;
}

/// The Pleiades, M45: hot B stars drifting through a dust cloud that is
/// only lit, never ionised -- they make too few ionising photons.
Target pleiades( Random& r )
{
	Target t;
	t.name       = "Pleiades";
	t.mix        = StarlightMix();
	t.asReleased = I_BACKYARD;
	// Lit dust with no glow of its own is faint next to emission: the
	// backyard pictures of it are long exposures.
	t.exposure   = 3.0f;
	const float s = 0.62f;// frame heights per degree
	struct Sister
	{
		float x, y, mag, temperature;
	};
	const Sister sisters[] = {
		{ 0.0f, 0.0f, 2.87f, 13000 },   { -0.52f, -0.04f, 3.62f, 12300 }, { -0.53f, 0.04f, 5.05f, 12000 },
		{ 0.23f, -0.17f, 4.18f, 14000 }, { 0.48f, -0.04f, 3.70f, 13400 }, { 0.38f, 0.12f, 3.87f, 13600 },
		{ 0.52f, 0.24f, 4.30f, 13000 },  { 0.62f, 0.05f, 5.45f, 12300 }, { 0.50f, 0.33f, 5.76f, 11500 },
	};
	for( const Sister& m : sisters )
	{
		const float flux = 400.0f * std::pow( 10.0f, -0.4f * ( m.mag - 2.87f ) );
		const float x = ( m.x - 0.05f ) * s, y = ( m.y + 0.03f ) * s;
		star( t, x, y, flux, m.temperature, 0 );
		t.sources.push_back( source( x, y, 0.06f, 0.04f * flux / 400.0f, m.temperature ) );
	}
	// The dust, streaked by the cluster's passage, densest south of Merope.
	t.elements.push_back( blob( Channel::Dense, 0.0f, 0.0f, 0.75f, 0.45f, -0.4f, 0.22f, 0.8f, 0.3f, 0.05f, 0.85f ) );
	t.elements.push_back( blob( Channel::Dense, 0.13f, -0.16f, 0.12f, 0.07f, -0.6f, 0.35f, 0.3f, 0.06f, 0.02f, 0.8f ) );
	t.elements.back().extra2 = 2.5f;
	t.elements.front().extra2 = 2.0f;
	t.haze      = 0.0f;
	t.warpScale = 5.0f;
	t.warp      = 0.03f;
	fieldStars( t, r, 30, 0.4f, 10.0f );
	t.fieldStars = 1000;
	t.dustRatio  = 2.0f;
	t.snX        = -0.6f;
	t.snY        = 0.3f;
	return t;
}

Galaxy spiral( float x, float y, float scale, float incline, float pa, int arms, float pitch, float bulge, float dust, float hii )
{
	Galaxy g;
	g.kind    = 0;
	g.x       = x;
	g.y       = y;
	g.scale   = scale;
	g.incline = incline;
	g.pa      = pa;
	g.arms    = arms;
	g.pitch   = pitch;
	g.bulge   = bulge;
	g.bulgeRe = 0.3f * scale;
	g.dust    = dust;
	g.hii     = hii;
	return g;
}

Galaxy elliptical( float x, float y, float re, float axis, float pa, float brightness )
{
	Galaxy g;
	g.kind      = 1;
	g.x         = x;
	g.y         = y;
	g.scale     = re;
	g.bulgeRe   = re;
	g.bulge     = 1.0f;
	g.bulgeAxis = axis;
	g.pa        = pa;
	g.sersic    = 4.0f;
	g.dust      = 0.0f;
	g.hii       = 0.0f;
	g.brightness = brightness;
	return g;
}

Target andromeda( Random& r )
{
	Target t;
	t.name        = "Andromeda";
	t.stretch     = 6.0f;
	t.mix         = StarlightMix();
	t.asReleased  = I_BACKYARD;
	t.galaxyScene = true;
	Galaxy m31 = spiral( 0.0f, 0.0f, 0.13f, 77.0f * float( kPi ) / 180.0f, 0.62f, 2, 0.12f, 0.35f, 0.9f, 0.25f );
	m31.sersic  = 2.2f;
	m31.bulgeRe = 0.045f;
	m31.dustRing = 0.0f;
	m31.armContrast = 0.45f;
	t.galaxies.push_back( m31 );
	t.galaxies.push_back( elliptical( 0.07f, -0.11f, 0.010f, 0.85f, 0.3f, 0.18f ) );// M32
	t.galaxies.push_back( elliptical( -0.30f, 0.33f, 0.05f, 0.5f, 2.3f, 0.25f ) );// M110
	fieldStars( t, r, 24, 0.5f, 8.0f );
	t.fieldStars = 900;
	t.haze       = 0.0f;
	t.snX        = 0.2f;
	t.snY        = 0.08f;
	return t;
}

Target whirlpool( Random& r )
{
	Target t;
	t.name        = "Whirlpool";
	t.stretch     = 6.0f;
	t.mix         = StarlightMix();
	t.asReleased  = I_HUBBLE_COLOUR;
	t.galaxyScene = true;
	Galaxy m51 = spiral( -0.05f, -0.06f, 0.12f, 0.35f, 0.4f, 2, 0.32f, 0.12f, 1.0f, 1.0f );
	m51.armContrast = 0.75f;
	t.galaxies.push_back( m51 );
	Galaxy ngc5195 = elliptical( 0.24f, 0.36f, 0.05f, 0.75f, 0.8f, 0.55f );
	ngc5195.kind = 2;
	ngc5195.dust = 0.6f;
	ngc5195.scale = 0.05f;
	t.galaxies.push_back( ngc5195 );
	fieldStars( t, r, 10, 0.4f, 5.0f );
	t.fieldStars = 300;
	t.haze       = 0.0f;
	t.snX        = -0.2f;
	t.snY        = -0.12f;
	return t;
}

Target sombrero( Random& r )
{
	Target t;
	t.name        = "Sombrero";
	t.stretch     = 6.0f;
	t.mix         = StarlightMix();
	t.asReleased  = I_HUBBLE_COLOUR;
	t.galaxyScene = true;
	Galaxy m104 = spiral( 0.0f, 0.0f, 0.2f, 84.0f * float( kPi ) / 180.0f, 0.0f, 2, 0.2f, 0.7f, 1.2f, 0.05f );
	m104.sersic    = 4.0f;
	m104.bulgeRe   = 0.2f;
	m104.bulge     = 0.85f;
	m104.bulgeAxis = 0.62f;
	m104.dustRing  = 0.34f;
	m104.armContrast = 0.15f;
	t.galaxies.push_back( m104 );
	fieldStars( t, r, 12, 0.4f, 5.0f );
	t.fieldStars = 300;
	t.haze       = 0.0f;
	t.snX        = 0.4f;
	t.snY        = 0.02f;
	return t;
}

Target ultraDeep( Random& r )
{
	Target t;
	t.name        = "Ultra Deep";
	t.stretch     = 4.0f;
	t.mix         = StarlightMix();
	t.asReleased  = I_HUBBLE_DEEP;
	t.galaxyScene = true;
	t.deep.on      = true;
	t.deep.density = 2600.0f;
	fieldStars( t, r, 3, 0.5f, 3.0f );
	t.fieldStars = 30;
	t.haze       = 0.0f;
	t.snX        = 0.1f;
	t.snY        = 0.1f;
	return t;
}

/// Webb's First Deep Field, SMACS 0723: a cluster at z = 0.39 bending the
/// galaxies behind it into arcs round its Einstein radius.
Target firstDeep( Random& r )
{
	Target t;
	t.name         = "First Deep";
	t.asReleased   = I_WEBB_DEEP;
	t.galaxyScene  = true;
	t.deep.on      = true;
	t.deep.density = 2000.0f;
	t.deep.lens    = true;
	t.deep.lensX   = 0.02f;
	t.deep.lensY   = 0.04f;
	t.deep.einstein = 0.11f;
	t.deep.members = 160.0f;
	t.deep.coreRadius = 0.18f;
	Galaxy bcg = elliptical( 0.02f, 0.04f, 0.04f, 0.7f, 0.5f, 0.9f );
	t.galaxies.push_back( bcg );
	// The bright foreground star with Webb's eight spikes.
	star( t, -0.12f, 0.32f, 120.0f, 5600.0f, 0 );
	star( t, 0.55f, -0.28f, 40.0f, 4800.0f, 0 );
	fieldStars( t, r, 4, 0.5f, 6.0f );
	t.fieldStars = 40;
	t.haze       = 0.0f;
	t.snX        = 0.25f;
	t.snY        = -0.1f;
	return t;
}

//===========================================================================
// The generative ones: everything from the seed.
//===========================================================================
Target genNebula( Random& r )
{
	Target t;
	t.name       = "Gen Nebula";
	t.asReleased = r.uniform() < 0.5f ? I_HUBBLE_NARROW : I_WEBB_NIRCAM;
	const float sa = r.range( 0.0f, 6.2831853f );
	const float sx = 0.95f * std::cos( sa ) * 1.2f, sy = std::clamp( 0.95f * std::sin( sa ) + 0.3f, -0.4f, 1.1f );
	t.sources.push_back( source( sx, sy, r.range( 0.15f, 0.5f ), r.range( 1.2f, 2.5f ), r.range( 32000.0f, 50000.0f ) ) );
	const int clouds = 2 + static_cast< int >( r.uniform() * 3 );
	for( int i = 0; i < clouds; ++i )
	{
		const float ca = sa + kPi + r.range( -1.2f, 1.2f );
		const float cr = r.range( 0.3f, 0.8f );
		const float cx = std::cos( ca ) * cr * 1.2f, cy = std::sin( ca ) * cr * 0.8f;
		t.elements.push_back( blob( Channel::Dense, cx, cy, r.range( 0.2f, 0.45f ), r.range( 0.12f, 0.3f ), r.range( 0.0f, 3.14f ), r.range( 0.8f, 1.3f ),
		                            0.2f, 0.03f, 0.04f, 0.7f ) );
		const int columns = static_cast< int >( r.uniform() * 3 );
		for( int k = 0; k < columns; ++k )
		{
			const float dx = sx - cx, dy = sy - cy, l = std::sqrt( dx * dx + dy * dy );
			const float len = r.range( 0.15f, 0.45f );
			const float bx = cx + r.range( -0.15f, 0.15f ), by = cy + r.range( -0.1f, 0.1f );
			const float wob = r.range( -0.25f, 0.25f );
			const float tx = bx + len * ( dx / l + wob * dy / l ), ty = by + len * ( dy / l - wob * dx / l );
			t.elements.push_back( column( bx, by, tx, ty, r.range( 0.05f, 0.1f ), r.range( 0.015f, 0.04f ), 1.0f, 0.09f, 0.012f ) );
		}
	}
	t.elements.push_back( sheet( Channel::Haze, sx * 0.5f, sy * 0.5f - 0.6f, sx, sy, 0.15f, 1.0f, 1.4f, 0.6f, 0.0f ) );
	t.haze      = r.range( 0.06f, 0.16f );
	t.hazeScale = r.range( 2.0f, 3.5f );
	t.warpScale = r.range( 3.0f, 5.0f );
	t.warp      = r.range( 0.012f, 0.03f );
	if( std::fabs( sx ) < 0.8f && std::fabs( sy ) < 0.45f )
		cluster( t, r, sx, sy, 0.15f, 16, 12.0f );
	fieldStars( t, r, 24, 0.5f, 10.0f );
	t.fieldStars = r.range( 1200.0f, 2600.0f );
	t.embedded   = r.range( 300.0f, 1200.0f );
	t.dustRatio  = r.range( 3.5f, 5.0f );
	t.snX        = r.range( -0.6f, 0.6f );
	t.snY        = r.range( -0.35f, 0.35f );
	return t;
}

Target genDark( Random& r )
{
	Target t;
	t.name       = "Gen Dark";
	t.mix        = StarlightMix();
	t.asReleased = I_BACKYARD;
	t.band.amount = r.range( 0.6f, 1.2f );
	t.band.angle  = r.range( -0.6f, 0.6f );
	t.band.width  = r.range( 0.35f, 0.6f );
	t.band.offset = r.range( -0.15f, 0.15f );
	const int clouds = 3 + static_cast< int >( r.uniform() * 4 );
	for( int i = 0; i < clouds; ++i )
	{
		Element e = blob( Channel::Foreground, r.range( -0.8f, 0.8f ), r.range( -0.4f, 0.4f ), r.range( 0.05f, 0.35f ), r.range( 0.03f, 0.12f ),
		                  r.range( 0.0f, 3.14f ), r.range( 0.4f, 1.5f ), 0.1f, 0.05f, 0.04f, 0.8f );
		e.extra2 = r.range( 0.0f, 3.0f );
		t.elements.push_back( e );
	}
	// A faint red glow behind, from the Galaxy's diffuse ionised gas.
	t.haze      = 0.02f;
	t.hazeScale = 2.0f;
	t.warpScale = r.range( 3.0f, 6.0f );
	t.warp      = 0.03f;
	t.sources.push_back( source( r.range( -1.0f, 1.0f ), 1.2f, 1.0f, 0.4f, 35000.0f ) );
	fieldStars( t, r, 30, 0.4f, 8.0f );
	t.fieldStars = r.range( 1500.0f, 2500.0f );
	t.dustRatio  = 4.0f;
	t.snX        = r.range( -0.6f, 0.6f );
	t.snY        = r.range( -0.35f, 0.35f );
	return t;
}

Target genPlanetary( Random& r )
{
	Target t;
	t.name       = "Gen Planetary";
	t.asReleased = r.uniform() < 0.5f ? I_HUBBLE_COLOUR : I_WEBB_NIRCAM;
	const float a = r.range( 0.12f, 0.3f );
	planetary( t, 0.0f, 0.0f, a, a * r.range( 0.55f, 1.0f ), r.range( 0.0f, 3.14f ), r.range( 0.2f, 0.38f ), r.range( 0.0f, 1.0f ),
	           r.range( 0.0f, 0.15f ), 0.0012f );
	if( r.uniform() < 0.45f )
	{
		// Bipolar: two lobes along the axis, pinched by a dusty waist. The
		// lobes are ionised gas the light reaches round the waist in depth:
		// as dense shells outside the main one they caught none of it, and
		// stood over the nebula as two dark bites.
		const float ang = r.range( 0.0f, 3.14f ), l = a * r.range( 1.0f, 1.8f );
		const float cx = std::cos( ang ) * l, cy = std::sin( ang ) * l;
		t.elements.push_back( shell( Channel::Haze, cx, cy, l * 0.7f, l * 0.45f, ang, 0.4f, 1.2f, 0.6f, 0.0f ) );
		t.elements.push_back( shell( Channel::Haze, -cx, -cy, l * 0.7f, l * 0.45f, ang, 0.4f, 1.2f, 0.6f, 0.0f ) );
	}
	t.haze      = 0.0f;
	t.warpScale = r.range( 5.0f, 9.0f );
	t.warp      = r.range( 0.004f, 0.015f );
	fieldStars( t, r, 22, 0.5f, 8.0f );
	t.fieldStars = r.range( 600.0f, 2000.0f );
	t.dustRatio  = 1.5f;
	t.snX        = 0.55f;
	t.snY        = 0.3f;
	// Most are barrels or rings seen near their axis, some closed. Drawn last,
	// so a seed's shape and stars are what they were before.
	const float open = r.range( 0.3f, 0.9f );
	for( Element& e : t.elements )
		if( e.shape == Shape::Shell )
			e.x2 = open;
	return t;
}

Target genRemnant( Random& r )
{
	Target t;
	t.name       = "Gen Remnant";
	t.mix        = RemnantMix();
	t.asReleased = r.uniform() < 0.5f ? I_HUBBLE_NARROW : I_HUBBLE_COLOUR;
	const float a = r.range( 0.25f, 0.42f ), b = a * r.range( 0.65f, 1.0f ), ang = r.range( 0.0f, 3.14f );
	t.elements.push_back( filaments( Channel::Shock, 0.0f, 0.0f, a, b, ang, r.range( 1.0f, 1.8f ), r.range( 6.0f, 12.0f ), r.range( 0.2f, 0.6f ) ) );
	t.elements.push_back( filaments( Channel::Oxygen, 0.01f, 0.0f, a * 0.95f, b * 0.95f, ang, r.range( 0.3f, 1.2f ), r.range( 8.0f, 14.0f ),
	                                 r.range( 0.2f, 0.6f ) ) );
	t.elements.push_back( shell( Channel::Shock, 0.0f, 0.0f, a * 1.05f, b * 1.05f, ang, 0.08f, 0.4f, 0.05f, 0.0f ) );
	if( r.uniform() < 0.5f )
	{
		t.elements.push_back( blob( Channel::Synchrotron, 0.0f, 0.0f, a * 0.8f, b * 0.8f, ang, 1.0f, 0.3f, a * 0.4f, 0.02f, 0.5f ) );
		star( t, 0.0f, 0.0f, 3.0f, 9000.0f, 0 );
	}
	t.haze      = 0.0f;
	t.warpScale = 4.0f;
	t.warp      = 0.02f;
	fieldStars( t, r, 20, 0.5f, 8.0f );
	t.fieldStars = r.range( 600.0f, 2000.0f );
	t.dustRatio  = 1.5f;
	t.snX        = 0.6f;
	t.snY        = -0.3f;
	return t;
}

Target genSpiral( Random& r )
{
	Target t;
	t.name        = "Gen Spiral";
	t.stretch     = 6.0f;
	t.mix         = StarlightMix();
	t.asReleased  = r.uniform() < 0.6f ? I_HUBBLE_COLOUR : I_WEBB_NIRCAM;
	t.galaxyScene = true;
	Galaxy g = spiral( 0.0f, 0.0f, r.range( 0.08f, 0.14f ), r.range( 0.0f, 1.35f ), r.range( 0.0f, 3.14f ), r.uniform() < 0.7f ? 2 : 3,
	                   r.range( 0.18f, 0.45f ), r.range( 0.08f, 0.45f ), r.range( 0.3f, 1.1f ), r.range( 0.2f, 1.0f ) );
	g.armContrast = r.range( 0.35f, 0.8f );
	t.galaxies.push_back( g );
	const int extras = static_cast< int >( r.uniform() * 3 );
	for( int i = 0; i < extras; ++i )
	{
		if( r.uniform() < 0.5f )
			t.galaxies.push_back( elliptical( r.range( -0.8f, 0.8f ), r.range( -0.4f, 0.4f ), r.range( 0.008f, 0.02f ), r.range( 0.5f, 1.0f ),
			                                  r.range( 0.0f, 3.14f ), r.range( 0.05f, 0.15f ) ) );
		else
		{
			Galaxy s = spiral( r.range( -0.8f, 0.8f ), r.range( -0.4f, 0.4f ), r.range( 0.012f, 0.03f ), r.range( 0.0f, 1.4f ),
			                   r.range( 0.0f, 3.14f ), 2, r.range( 0.2f, 0.4f ), r.range( 0.1f, 0.4f ), 0.6f, 0.5f );
			s.brightness = r.range( 0.05f, 0.2f );
			t.galaxies.push_back( s );
		}
	}
	t.deep.on      = true;
	t.deep.density = 400.0f;
	fieldStars( t, r, 10, 0.4f, 5.0f );
	t.fieldStars = r.range( 100.0f, 600.0f );
	t.haze       = 0.0f;
	t.snX        = g.scale * r.range( 1.0f, 2.0f ) * ( r.uniform() < 0.5f ? -1.0f : 1.0f );
	t.snY        = g.scale * r.range( -0.5f, 0.5f );
	return t;
}

Target genCluster( Random& r )
{
	Target t;
	t.name          = "Gen Cluster";
	t.stretch       = 4.0f;
	t.mix           = StarlightMix();
	t.asReleased    = r.uniform() < 0.5f ? I_WEBB_DEEP : I_HUBBLE_DEEP;
	t.galaxyScene   = true;
	t.deep.on       = true;
	t.deep.density  = r.range( 1600.0f, 2800.0f );
	t.deep.lens     = true;
	t.deep.lensX    = r.range( -0.25f, 0.25f );
	t.deep.lensY    = r.range( -0.15f, 0.15f );
	t.deep.einstein = r.range( 0.07f, 0.16f );
	t.deep.members  = r.range( 100.0f, 220.0f );
	t.deep.coreRadius = r.range( 0.12f, 0.25f );
	t.galaxies.push_back( elliptical( t.deep.lensX, t.deep.lensY, r.range( 0.03f, 0.05f ), r.range( 0.6f, 0.9f ), r.range( 0.0f, 3.14f ), 0.9f ) );
	fieldStars( t, r, 4, 0.5f, 40.0f );
	t.fieldStars = 40;
	t.haze       = 0.0f;
	t.snX        = t.deep.lensX + 0.2f;
	t.snY        = t.deep.lensY;
	return t;
}

Target genStarfield( Random& r )
{
	Target t;
	t.name       = "Gen Starfield";
	t.mix        = StarlightMix();
	t.asReleased = I_BACKYARD;
	if( r.uniform() < 0.65f )
	{
		t.band.amount = r.range( 0.4f, 1.0f );
		t.band.angle  = r.range( -1.2f, 1.2f );
		t.band.width  = r.range( 0.25f, 0.5f );
		t.band.offset = r.range( -0.2f, 0.2f );
		for( int i = 0; i < 4; ++i )
		{
			const float u = r.range( -0.9f, 0.9f );
			Element e = blob( Channel::Foreground, u * std::cos( t.band.angle ), u * std::sin( t.band.angle ), r.range( 0.15f, 0.4f ),
			                  r.range( 0.02f, 0.06f ), t.band.angle + r.range( -0.3f, 0.3f ), r.range( 0.3f, 0.9f ), 0.1f, 0.04f, 0.03f, 0.8f );
			e.extra2 = 2.0f;
			t.elements.push_back( e );
		}
	}
	t.haze      = 0.0f;
	t.warpScale = 4.0f;
	t.warp      = 0.03f;
	fieldStars( t, r, 36, 0.4f, 12.0f );
	t.fieldStars = r.range( 1000.0f, 2200.0f );
	t.dustRatio  = 3.0f;
	t.snX        = r.range( -0.6f, 0.6f );
	t.snY        = r.range( -0.35f, 0.35f );
	return t;
}
} // namespace

int TargetCount()
{
	return kTargetCount;
}

const char* TargetName( int index )
{
	return kNames[ std::clamp( index, 0, kTargetCount - 1 ) ];
}

int FirstGenerative()
{
	return kFirstGenerative;
}

Target Build( int index, uint32_t seed )
{
	Random r( 0x5ee0u + seed * 7919u + static_cast< uint32_t >( index ) * 104729u );
	switch( std::clamp( index, 0, kTargetCount - 1 ) )
	{
	case 0: return pillars( r );
	case 1: return cliffs( r );
	case 2: return horsehead( r );
	case 3: return orion( r );
	case 4: return crab( r );
	case 5: return ring( r );
	case 6: return southernRing( r );
	case 7: return helix( r );
	case 8: return veil( r );
	case 9: return pleiades( r );
	case 10: return andromeda( r );
	case 11: return whirlpool( r );
	case 12: return sombrero( r );
	case 13: return ultraDeep( r );
	case 14: return firstDeep( r );
	case 15: return genNebula( r );
	case 16: return genDark( r );
	case 17: return genPlanetary( r );
	case 18: return genRemnant( r );
	case 19: return genSpiral( r );
	case 20: return genCluster( r );
	default: return genStarfield( r );
	}
}

} // namespace sidereal::scene
