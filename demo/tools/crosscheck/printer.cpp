// The C++ half of the browser demo's PORT cross-check: the plugin's own
// GL-free sources -- Controls.cpp, Physics.cpp, Scene.cpp and Events.cpp --
// driven through a fixed scenario and printed. run.mjs prints the same from
// the PORT block of demo/plugin.js; crosscheck.sh compares the two.
//
//     printer > cpp.txt
//
// Floats print as their 8 hex digits, doubles as 16, so neither side parses a
// decimal. Read this beside run.mjs: each block mirrors one there.

#include "Controls.h"
#include "Events.h"
#include "Physics.h"
#include "Scene.h"

#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace sidereal;

namespace
{
std::string d( double v )
{
	uint64_t bits;
	std::memcpy( &bits, &v, sizeof bits );
	char buffer[ 24 ];
	std::snprintf( buffer, sizeof buffer, "%016" PRIx64, bits );
	return buffer;
}
std::string f( float v )
{
	uint32_t bits;
	std::memcpy( &bits, &v, sizeof bits );
	char buffer[ 12 ];
	std::snprintf( buffer, sizeof buffer, "%08" PRIx32, bits );
	return buffer;
}
std::string floats( const float* v, size_t n )
{
	std::string out;
	for( size_t i = 0; i < n; ++i )
		out += ( i ? "," : "" ) + f( v[ i ] );
	return out;
}

void element( int index, const scene::Element& e )
{
	std::printf( "el %d %d %d %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %d %d\n", index, static_cast< int >( e.shape ),
	             static_cast< int >( e.channel ), f( e.x ).c_str(), f( e.y ).c_str(), f( e.x2 ).c_str(), f( e.y2 ).c_str(), f( e.a ).c_str(),
	             f( e.b ).c_str(), f( e.angle ).c_str(), f( e.density ).c_str(), f( e.thickness ).c_str(), f( e.edge ).c_str(),
	             f( e.rough ).c_str(), f( e.detail ).c_str(), f( e.depth ).c_str(), f( e.extra ).c_str(), f( e.extra2 ).c_str(), e.polyStart,
	             e.polyCount );
}

void target( int index, uint32_t seed )
{
	const scene::Target t = scene::Build( index, seed );
	std::printf( "target %d %u %s as=%d haze=%s shade=%s hscale=%s wscale=%s warp=%s field=%s emb=%s dust=%s exp=%s str=%s sn=%s,%s echo=%s gal=%d\n",
	             index, seed, t.name, t.asReleased, f( t.haze ).c_str(), f( t.hazeShade ).c_str(), f( t.hazeScale ).c_str(),
	             f( t.warpScale ).c_str(), f( t.warp ).c_str(), f( t.fieldStars ).c_str(), f( t.embedded ).c_str(), f( t.dustRatio ).c_str(),
	             f( t.exposure ).c_str(), f( t.stretch ).c_str(), f( t.snX ).c_str(), f( t.snY ).c_str(), f( t.echoDistance ).c_str(),
	             t.galaxyScene ? 1 : 0 );
	std::printf( "mix %d %u", index, seed );
	for( double v : t.mix.line )
		std::printf( " %s", d( v ).c_str() );
	std::printf( " %s %s %s %s\n", d( t.mix.star ).c_str(), d( t.mix.starTemp ).c_str(), d( t.mix.dust ).c_str(), d( t.mix.dustTemp ).c_str() );
	for( size_t i = 0; i < t.elements.size(); ++i )
		element( static_cast< int >( i ), t.elements[ i ] );
	std::printf( "poly %zu %s\n", t.polygon.size(), floats( t.polygon.data(), t.polygon.size() ).c_str() );
	for( const scene::Source& s : t.sources )
		std::printf( "src %s %s %s %s %s %s %d\n", f( s.x ).c_str(), f( s.y ).c_str(), f( s.h ).c_str(), f( s.luminosity ).c_str(),
		             f( s.temperature ).c_str(), f( s.soft ).c_str(), s.visible ? 1 : 0 );
	for( const scene::Star& s : t.stars )
		std::printf( "star %s %s %s %s %s %d\n", f( s.x ).c_str(), f( s.y ).c_str(), f( s.flux ).c_str(), f( s.temperature ).c_str(),
		             f( s.depth ).c_str(), s.layer );
	for( const scene::Galaxy& g : t.galaxies )
		std::printf( "galaxy %d %s %s %s %s %s %d %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s\n", g.kind, f( g.x ).c_str(), f( g.y ).c_str(),
		             f( g.scale ).c_str(), f( g.incline ).c_str(), f( g.pa ).c_str(), g.arms, f( g.pitch ).c_str(), f( g.armContrast ).c_str(),
		             f( g.bulge ).c_str(), f( g.bulgeRe ).c_str(), f( g.sersic ).c_str(), f( g.bulgeAxis ).c_str(), f( g.dust ).c_str(),
		             f( g.dustRing ).c_str(), f( g.hii ).c_str(), f( g.brightness ).c_str(), f( g.diskT ).c_str(), f( g.bulgeT ).c_str(),
		             f( g.armT ).c_str(), f( g.phase ).c_str(), f( g.depth ).c_str() );
	const scene::DeepField& deep = t.deep;
	std::printf( "deep %d %s %d %s %s %s %s %s %s\n", deep.on ? 1 : 0, f( deep.density ).c_str(), deep.lens ? 1 : 0, f( deep.lensX ).c_str(),
	             f( deep.lensY ).c_str(), f( deep.einstein ).c_str(), f( deep.zLens ).c_str(), f( deep.members ).c_str(), f( deep.coreRadius ).c_str() );
	std::printf( "band %s %s %s %s\n", f( t.band.amount ).c_str(), f( t.band.angle ).c_str(), f( t.band.width ).c_str(), f( t.band.offset ).c_str() );
}
} // namespace

int main()
{
	//-----------------------------------------------------------------------
	// Controls.
	//-----------------------------------------------------------------------
	for( int k = -10; k <= 1010; ++k )
	{
		const float v = static_cast< float >( k / 1000.0 );
		std::printf( "ctl %d %s gas=%s turb=%s flow=%s haze=%s dust=%s light=%s heat=%s angle=%s stars=%s spikes=%s seeing=%s zoom=%s pan=%s spin=%s "
		             "drift=%s depth=%s fly=%s met=%s sn=%s echo=%s exp=%s str=%s sat=%s\n",
		             k, f( v ).c_str(), d( GasFromParam( v ) ).c_str(), d( TurbulenceFromParam( v ) ).c_str(), d( FlowFromParam( v ) ).c_str(),
		             d( HazeFromParam( v ) ).c_str(), d( DustFromParam( v ) ).c_str(), d( StarlightFromParam( v ) ).c_str(),
		             d( HeatFromParam( v ) ).c_str(), d( LightAngleFromParam( v ) ).c_str(), d( StarsFromParam( v ) ).c_str(),
		             d( SpikesFromParam( v ) ).c_str(), d( SeeingFromParam( v ) ).c_str(), d( ZoomFromParam( v ) ).c_str(),
		             d( PanFromParam( v ) ).c_str(), d( SpinFromParam( v ) ).c_str(), d( DriftFromParam( v ) ).c_str(),
		             d( DepthFromParam( v ) ).c_str(), d( FlyFromParam( v ) ).c_str(), d( MeteorsFromParam( v ) ).c_str(),
		             d( SupernovaSpeedFromParam( v ) ).c_str(), d( EchoFromParam( v ) ).c_str(), d( ExposureFromParam( v ) ).c_str(),
		             d( StretchFromParam( v ) ).c_str(), d( SaturationFromParam( v ) ).c_str() );
	}
	for( int k = -40; k <= 2440; ++k )
	{
		const float v = static_cast< float >( k / 100.0 );
		std::printf( "opt %d %d %d %d %d\n", k, OptionIndex( v, 22 ), OptionIndex( v, 8 ), OptionIndex( v, 2 ), OptionIndex( -v, 4 ) );
	}
	for( int effect = 0; effect < 2; ++effect )
	{
		std::printf( "order %d", effect );
		for( unsigned int id : HostOrder( effect == 1 ) )
			if( id < PT_ABOUT_TEXT )
				std::printf( " %u", id );
		std::printf( "\n" );
	}
	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
	{
		const ControlInfo& info = InfoOf( id );
		std::printf( "param %u %s|%s kind=%d default=%s options=%d", id, info.name, info.group, static_cast< int >( info.kind ),
		             f( info.defaultValue ).c_str(), info.optionCount );
		for( int i = 0; i < info.optionCount; ++i )
			std::printf( "|%s", OptionName( id, i ) );
		std::printf( "\n" );
	}

	//-----------------------------------------------------------------------
	// Physics: the functions, then every palette the plugin can build.
	//-----------------------------------------------------------------------
	using namespace physics;
	for( int k = 0; k <= 400; ++k )
	{
		const double t = 2000.0 * std::pow( 60.0, k / 400.0 );
		std::printf( "bb %d %s %s %s %s %s %s\n", k, d( Planck( 0.1 + 0.05 * k, t ) ).c_str(), d( PhotonTail( k / 20.0 ) ).c_str(),
		             d( OxygenHardness( t ) ).c_str(), d( ReducedEnergy( 13.598, t ) ).c_str(), d( Extinction( 0.1 + 0.06 * k ) ).c_str(),
		             d( Extinction( 0.1 + 0.06 * k, 5.0 ) ).c_str() );
	}
	for( int i = 0; i < kTempSamples; ++i )
		std::printf( "temp %d %s %s %s %s\n", i, d( TempOfSample( 0, i ) ).c_str(), d( TempOfSample( 2, i ) ).c_str(),
		             d( SampleOfTemp( 0, 2000.0 + 900.0 * i ) ).c_str(), d( SampleOfTemp( 2, 10.0 + 10.0 * i ) ).c_str() );
	{
		const char* const names[] = { "F502N", "F657N", "F673N", "F435W", "F555W", "F606W", "F775W", "F814W", "F850LP", "F658N",
			                          "F110W", "F160W", "F090W", "F150W", "F162M", "F187N", "F200W", "F212N", "F277W", "F300M",
			                          "F335M", "F356W", "F405N", "F444W", "F470N", "F770W", "F1130W", "F1500W", "OSC-B", "OSC-G",
			                          "OSC-R", "NOPE" };
		for( const char* name : names )
		{
			const Filter& filter = FilterNamed( name );
			std::printf( "filter %s %s %s %s", name, filter.name, d( filter.pivot ).c_str(), d( filter.width ).c_str() );
			for( int c = 0; c < kLineComponents; ++c )
				std::printf( " %s", d( LineResponse( filter, static_cast< Component >( c ) ) ).c_str() );
			for( int kind = 0; kind < 3; ++kind )
				for( double t : { 20.0, 90.0, 3000.0, 5800.0, 12000.0, 40000.0 } )
					std::printf( " %s", d( ContinuumResponse( filter, kind, t ) ).c_str() );
			for( double z : { 0.0, 0.5, 2.0, 6.0 } )
				std::printf( " %s", d( GalaxyResponse( filter, 6500.0, z ) ).c_str() );
			std::printf( "\n" );
		}
	}
	for( int id = 0; id < kInstruments + 1; ++id )
	{
		const Instrument& inst = InstrumentOf( id );
		std::printf( "inst %d %s n=%d ap=%s sp=%d ang=%s see=%s glow=%s wb=%s exp=%s", id, inst.name, inst.filterCount, d( inst.aperture ).c_str(),
		             static_cast< int >( inst.spikes ), d( inst.spikeAngle ).c_str(), d( inst.seeing ).c_str(), d( inst.dustGlow ).c_str(),
		             d( inst.whiteBalance ).c_str(), d( inst.exposure ).c_str() );
		for( int k = 0; k < inst.filterCount; ++k )
			std::printf( " %s=%s,%s,%s", inst.filters[ k ], f( inst.colour[ k ][ 0 ] ).c_str(), f( inst.colour[ k ][ 1 ] ).c_str(),
			             f( inst.colour[ k ][ 2 ] ).c_str() );
		std::printf( "\n" );
	}
	const Mix mixes[] = { NebulaMix(), PlanetaryMix(), RemnantMix(), StarlightMix() };
	for( int id = 0; id < kInstruments; ++id )
		for( int m = 0; m < 4; ++m )
		{
			const Palette p = BuildPalette( id, mixes[ m ] );
			std::printf( "pal %d %d n=%d", id, m, p.filterCount );
			for( int k = 0; k < p.filterCount; ++k )
				std::printf( " %s,%s,%s,%s,%s,%s", d( p.gain[ k ] ).c_str(), d( p.ext[ k ] ).c_str(), d( p.pivot[ k ] ).c_str(),
				             d( p.colour[ k ][ 0 ] ).c_str(), d( p.colour[ k ][ 1 ] ).c_str(), d( p.colour[ k ][ 2 ] ).c_str() );
			std::printf( "\n" );
			for( int c = 0; c < kLineComponents; ++c )
				std::printf( "lines %d %d %d %s\n", id, m, c, floats( p.lines.data() + c * kTauSamples * 3, kTauSamples * 3 ).c_str() );
			for( int r = 0; r < 3 * kTempSamples; ++r )
				std::printf( "cont %d %d %d %s\n", id, m, r, floats( p.continua.data() + r * kTauSamples * 3, kTauSamples * 3 ).c_str() );
			for( int r = 0; r < kTempSamples; ++r )
				std::printf( "galx %d %d %d %s\n", id, m, r, floats( p.galaxies.data() + r * 32 * 3, 32 * 3 ).c_str() );
			for( int c = 0; c < kLineComponents; ++c )
			{
				const auto a = DisplayOf( p, static_cast< Component >( c ), 0.7 );
				std::printf( "disp %d %d %d %s %s %s\n", id, m, c, d( a[ 0 ] ).c_str(), d( a[ 1 ] ).c_str(), d( a[ 2 ] ).c_str() );
			}
			for( int kind = 0; kind < 3; ++kind )
			{
				const auto a = DisplayOfContinuum( p, kind, kind == 2 ? 60.0 : 6100.0, 1.3 );
				std::printf( "dispc %d %d %d %s %s %s\n", id, m, kind, d( a[ 0 ] ).c_str(), d( a[ 1 ] ).c_str(), d( a[ 2 ] ).c_str() );
			}
		}
	for( int i = 0; i <= 64; ++i )
		std::printf( "cosmo %d %s\n", i, d( AngularDiameterDistance( 0.125 * i ) ).c_str() );
	for( int i = -10; i <= 2000; ++i )
		std::printf( "sn %d %s %s %s %s %s\n", i, d( SupernovaLight( 0.45 * i ) ).c_str(), d( DecayHeating( 0.45 * i ) ).c_str(),
		             d( SupernovaTemperature( 0.45 * i ) ).c_str(), d( RemnantRadius( i / 300.0 ) ).c_str(), d( MeteorLight( i / 500.0 ) ).c_str() );
	std::printf( "snpeak %s\n", d( SupernovaPeakDay() ).c_str() );

	//-----------------------------------------------------------------------
	// Scene: every target, at seeds that cover the generator's range.
	//-----------------------------------------------------------------------
	std::printf( "targets %d first=%d\n", scene::TargetCount(), scene::FirstGenerative() );
	for( int index = 0; index < scene::TargetCount(); ++index )
		for( uint32_t seed : { 0u, 1u, 2u, 7u, 137u, 499u, 500u, 731u, 999u, 1000u } )
			target( index, seed );
	for( uint32_t seed = 0; seed <= 1000; seed += 3 )
		for( int index = scene::FirstGenerative(); index < scene::TargetCount(); ++index )
			target( index, seed );

	//-----------------------------------------------------------------------
	// Events: meteors and the supernova, frame by frame.
	//-----------------------------------------------------------------------
	for( int run = 0; run < 6; ++run )
	{
		Events events;
		const double fps = run % 2 ? 50.0 : 60.0;
		for( int frame = 0; frame < 4000; ++frame )
		{
			const double now = 3.25 + frame / fps;
			const double dt  = frame == 0 ? 0.0 : 1.0 / fps;
			EventInputs in;
			in.perMinute     = MeteorsFromParam( static_cast< float >( ( frame / 400 ) % 5 * 0.25 * ( run / 2 + 1 ) / 3.0 ) );
			in.shower        = ( frame / 700 + run ) % 2 == 0;
			in.presses       = frame % 233 == 17 ? 1 + frame % 3 : 0;
			in.supernova     = frame == 30 || frame == 2100;
			in.daysPerSecond = SupernovaSpeedFromParam( static_cast< float >( 0.3 + 0.15 * run ) );
			in.snX           = 0.1f * run;
			in.snY           = -0.05f * run;
			if( frame == 1500 )
				events.Reset();
			events.Advance( now, dt, in, static_cast< uint32_t >( run * 4096 + 17 * run ) );
			const Supernova& n = events.Nova();
			std::printf( "ev %d %d m=%zu l=%zu nova=%d %s %s %s %s %s %s %s %s %s\n", run, frame, events.Meteors().size(), events.Launched().size(),
			             n.active ? 1 : 0, f( n.x ).c_str(), f( n.y ).c_str(), d( n.days ).c_str(), f( n.flux ).c_str(), f( n.temperature ).c_str(),
			             f( n.remnantRadius ).c_str(), f( n.remnantGlow ).c_str(), f( n.echoCt ).c_str(), d( now ).c_str() );
			if( in.presses > 0 || frame % 97 == 0 )
				for( const Meteor& m : events.Meteors() )
					std::printf( "met %d %d %s %s %s %s %s %s %s %s %s %s %s %s %s\n", run, frame, f( m.x ).c_str(), f( m.y ).c_str(), f( m.dx ).c_str(),
					             f( m.dy ).c_str(), f( m.length ).c_str(), d( m.start ).c_str(), f( m.duration ).c_str(), f( m.rate ).c_str(),
					             f( m.peak ).c_str(), f( m.fast ).c_str(), f( m.train ).c_str(), f( m.trainLife ).c_str(), f( m.speed ).c_str() );
		}
	}
	return 0;
}
