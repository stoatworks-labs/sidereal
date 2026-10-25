// The checks: included into main.cpp's anonymous namespace.
//
// Each says what it measures and what it is measured AGAINST, and the
// reference is never the plugin's own arithmetic re-typed: a quadrature where
// the plugin uses a series, a published table where it uses a polynomial,
// a geometric identity, an observed decline rate, or the picture itself.

//---------------------------------------------------------------------------
// Helpers.
//---------------------------------------------------------------------------
double luma( const float* p )
{
	return 0.2126 * p[ 0 ] + 0.7152 * p[ 1 ] + 0.0722 * p[ 2 ];
}

/// A rig ready to measure: a test target, the turbulence off, the drift and
/// flow stopped, every star but the ones asked for gone.
struct Bench
{
	Rig rig;
	explicit Bench( bool over = false ) :
		rig( over )
	{
	}
	bool Start( int w, int h, const scene::Target& target, int hooks, int instrument = -1 )
	{
		if( !rig.Init( w, h ) )
			return false;
		rig.plugin.SetTargetForTest( target );
		rig.plugin.SetHooksForTest( hooks );
		rig.plugin.SetInstrumentForTest( instrument );
		rig.Set( PT_TURBULENCE, 0.0f );
		rig.Set( PT_DRIFT, 0.0f );
		rig.Set( PT_FLOW, 0.0f );
		rig.Set( PT_STARS, 0.0f );
		return true;
	}
};

scene::Target emptyTarget()
{
	scene::Target t;
	t.name       = "test";
	t.asReleased = physics::I_HUBBLE_NARROW;
	t.haze       = 0.0f;
	t.fieldStars = 0.0f;
	t.embedded   = 0.0f;
	t.warpScale  = 3.0f;
	return t;
}

/// A uniform slab of dense gas from x = edge rightwards, lit by one star on
/// the axis to the left: the photon budget's test bed.
scene::Target slabTarget( float density, float thickness, float edge, float srcX, float srcH, float lum, float temperature )
{
	scene::Target t = emptyTarget();
	scene::Element e;
	e.shape     = scene::Shape::Sheet;
	e.channel   = scene::Channel::Dense;
	e.x         = edge;
	e.y         = 0.0f;
	e.x2        = 1.0f;
	e.y2        = 0.0f;
	e.density   = density;
	e.thickness = thickness;
	e.edge      = 0.001f;
	e.rough     = 0.0f;
	e.detail    = 0.0f;
	t.elements.push_back( e );
	scene::Source s;
	s.x           = srcX;
	s.y           = 0.0f;
	s.h           = srcH;
	s.luminosity  = lum;
	s.temperature = temperature;
	t.sources.push_back( s );
	return t;
}

/// Screen x (frame heights from the centre) of pixel column i.
double screenX( int i, int width, int height )
{
	return ( i + 0.5 - 0.5 * width ) / height;
}

//===========================================================================
// --hardness (no GL): the O++ share of a blackbody's ionising photons.
//===========================================================================
int runHardness( const Perturb& p )
{
	physics::SetHooksForTest( p.physicsHooks );
	std::printf( "\n=== hardness: the photon tail by its series, against Simpson on x^2 / ( e^x - 1 )\n" );
	auto quad = []( double x0 ) {
		// The same integral by brute force: Simpson on [ x0, x0 + 60 ], 20000 panels.
		const int n = 20000;
		const double a = x0, b = x0 + 60.0, h = ( b - a ) / n;
		auto f = []( double x ) { return x * x / std::expm1( x ); };
		double s = f( a ) + f( b );
		for( int i = 1; i < n; ++i )
			s += f( a + i * h ) * ( i % 2 ? 4.0 : 2.0 );
		return s * h / 3.0;
	};
	for( double x0 : { 0.5, 1.26, 3.95, 9.7, 20.0 } )
	{
		const double series = physics::PhotonTail( x0 ), q = quad( x0 );
		Check( std::fabs( series - q ) <= 1e-7 * q + 1e-15, fmt( "tail from x0 = %.2f: series %.10g, quadrature %.10g", x0, series, q ) );
	}
	double last = 0.0;
	bool rising = true;
	for( double t = 20000.0; t <= 150000.0; t += 5000.0 )
	{
		const double h = physics::OxygenHardness( t );
		rising         = rising && h > last;
		last           = h;
	}
	Check( rising, "the O++ share rises with temperature from 20 to 150 kK" );
	physics::SetHooksForTest( 0 );
	return Verdict();
}

//===========================================================================
// --extinction (no GL): CCM's own Table 3 at its effective wavelengths.
//===========================================================================
int runExtinction( const Perturb& p )
{
	physics::SetHooksForTest( p.physicsHooks );
	std::printf( "\n=== extinction: A_lambda / A_V against Cardelli, Clayton & Mathis 1989, Table 3 (R_V = 3.1)\n" );
	struct Band
	{
		const char* name;
		double lambda, published;
	};
	// CCM tabulate these as their law at these effective wavelengths; 2% is
	// the rounding of the published table plus their stated fit residual.
	const Band bands[] = {
		{ "U", 0.365, 1.569 }, { "B", 0.440, 1.337 }, { "V", 0.550, 1.000 }, { "R", 0.700, 0.751 },
		{ "I", 0.900, 0.479 }, { "J", 1.250, 0.282 }, { "H", 1.600, 0.190 }, { "K", 2.200, 0.114 },
	};
	for( const Band& b : bands )
	{
		const double got = physics::Extinction( b.lambda );
		Check( std::fabs( got / b.published - 1.0 ) < 0.02, fmt( "%s (%.3f um): %.3f, CCM %.3f", b.name, b.lambda, got, b.published ) );
	}
	// The extension past 3.3 um: continuous there, flatter than the power law
	// out to 8 um (Indebetouw et al. 2005: A_8 / A_K about 0.4), the 9.7 um bump.
	const double below = physics::Extinction( 1.0 / 0.3 - 1e-6 ), above = physics::Extinction( 1.0 / 0.3 + 1e-6 );
	Check( std::fabs( below - above ) < 1e-4, fmt( "continuous at 3.33 um: %.5f / %.5f", below, above ) );
	const double ak = physics::Extinction( 2.2 ), a8 = physics::Extinction( 8.0 );
	Check( a8 / ak > 0.25 && a8 / ak < 0.6, fmt( "A_8 / A_K = %.2f (Indebetouw: ~0.4)", a8 / ak ) );
	Check( physics::Extinction( 9.7 ) > physics::Extinction( 8.0 ) && physics::Extinction( 9.7 ) > physics::Extinction( 12.0 ),
	       "the silicate feature peaks at 9.7 um" );
	physics::SetHooksForTest( 0 );
	return Verdict();
}

//===========================================================================
// --cosmology (no GL): D_A against Gauss-Legendre, and the turnover.
//===========================================================================
int runCosmology( const Perturb& p )
{
	physics::SetHooksForTest( p.physicsHooks );
	std::printf( "\n=== cosmology: angular diameter distance, flat LCDM (70, 0.3), against 10-point Gauss-Legendre on 64 panels\n" );
	auto reference = []( double z ) {
		static const double x[ 5 ] = { 0.1488743389816312, 0.4333953941292472, 0.6794095682990244, 0.8650633666889845, 0.9739065285171717 };
		static const double w[ 5 ] = { 0.2955242247147529, 0.2692667193099963, 0.2190863625159820, 0.1494513491505806, 0.0666713443086881 };
		const int panels = 64;
		double sum = 0.0;
		for( int k = 0; k < panels; ++k )
		{
			const double a = z * k / panels, b = z * ( k + 1 ) / panels, m = 0.5 * ( a + b ), r = 0.5 * ( b - a );
			for( int i = 0; i < 5; ++i )
				for( int s : { -1, 1 } )
				{
					const double zz = m + s * r * x[ i ];
					sum += r * w[ i ] / std::sqrt( 0.3 * std::pow( 1.0 + zz, 3.0 ) + 0.7 );
				}
		}
		return 299792.458 / 70.0 * sum / ( 1.0 + z );
	};
	for( double z : { 0.5, 1.0, 2.0, 4.0, 8.0 } )
	{
		const double got = physics::AngularDiameterDistance( z ), want = reference( z );
		Check( std::fabs( got / want - 1.0 ) < 1e-6, fmt( "D_A( %.1f ) = %.2f Mpc (reference %.2f)", z, got, want ) );
	}
	// Wright's calculator gives 1651.9 Mpc at z = 1 for these parameters.
	Check( std::fabs( physics::AngularDiameterDistance( 1.0 ) - 1651.9 ) < 2.0, "D_A( 1 ) is Wright's 1651.9 Mpc" );
	double best = 0.0, bestZ = 0.0;
	for( double z = 0.5; z < 4.0; z += 0.01 )
		if( physics::AngularDiameterDistance( z ) > best )
		{
			best  = physics::AngularDiameterDistance( z );
			bestZ = z;
		}
	Check( bestZ > 1.4 && bestZ < 1.8, fmt( "the turnover: galaxies stop shrinking at z = %.2f (expected ~1.6)", bestZ ) );
	physics::SetHooksForTest( 0 );
	return Verdict();
}

//===========================================================================
// --supernova (no GL): Arnett's rule, the rise, and the tail against the
// decline observed in type Ia light curves.
//===========================================================================
int runSupernova( const Perturb& p )
{
	physics::SetHooksForTest( p.physicsHooks );
	std::printf( "\n=== supernova: Arnett's rule, the rise time, the radioactive tail\n" );
	const double peak = physics::SupernovaPeakDay();
	Check( peak > 14.0 && peak < 22.0, fmt( "peak on day %.2f (type Ia rise times: 15-20 days)", peak ) );
	// Arnett's rule: dL/dt = 0 exactly where L equals the instantaneous decay
	// heating, for any diffusion time. The table steps 0.05 d, over which the
	// heating near peak changes 0.4%.
	const double before = physics::SupernovaLight( peak - 0.5 ), after = physics::SupernovaLight( peak + 0.5 );
	Check( before < 1.0 && after < 1.0, "the curve peaks where the table says" );
	const double heating = physics::DecayHeating( peak ), raw = physics::SupernovaRawPeak();
	Check( std::fabs( raw / heating - 1.0 ) < 0.005, fmt( "Arnett's rule: light at peak %.5f, decay heating then %.5f", raw, heating ) );
	// The tail: between days 200 and 300 the diffusion is long over and the
	// light is the deposited heating; observed type Ia tails decline at
	// 1.4 +- 0.2 mag per 100 days (Leibundgut 2000), cobalt alone at 0.98.
	const double l200 = physics::SupernovaLight( 200.0 ), l300 = physics::SupernovaLight( 300.0 );
	const double rate = -2.5 * std::log10( l300 / l200 );
	Check( rate > 1.2 && rate < 1.7, fmt( "tail declines %.2f mag / 100 days (observed 1.2-1.6; cobalt alone 0.98)", rate ) );
	// The remnant: free expansion, then Sedov-Taylor's t^(2/5).
	const double early = std::log( physics::RemnantRadius( 0.5 ) / physics::RemnantRadius( 0.25 ) ) / std::log( 2.0 );
	const double late  = std::log( physics::RemnantRadius( 20.0 ) / physics::RemnantRadius( 10.0 ) ) / std::log( 2.0 );
	Check( std::fabs( early - 1.0 ) < 1e-9 && std::fabs( late - 0.4 ) < 1e-9, fmt( "remnant: R ~ t^%.3f free, t^%.3f Sedov-Taylor", early, late ) );
	// The echo: a point on the sheet at radius rho is seen a delay
	// ( sqrt( rho^2 + z^2 ) - z ) / c late; the echo radius must invert it.
	for( double ct : { 0.01, 0.1, 0.5 } )
	{
		const double z = 0.15, rho = physics::EchoRadius( ct, z );
		Check( std::fabs( std::sqrt( rho * rho + z * z ) - z - ct ) < 1e-12, fmt( "echo at c t = %.2f: rho %.4f has delay %.4f", ct, rho, std::sqrt( rho * rho + z * z ) - z ) );
	}
	physics::SetHooksForTest( 0 );
	return Verdict();
}

//===========================================================================
// --meteorcurve (no GL): the classical light curve, and launched meteors.
//===========================================================================
int runMeteorCurve( const Perturb& )
{
	std::printf( "\n=== meteor: the classical single-body light curve and a shower's geometry\n" );
	double best = 0.0, bestX = 0.0;
	for( double x = 0.0; x <= 3.0; x += 1e-5 )
		if( physics::MeteorLight( x ) > best )
		{
			best  = physics::MeteorLight( x );
			bestX = x;
		}
	Check( std::fabs( bestX - 1.0 ) < 1e-4 && std::fabs( best - 1.0 ) < 1e-9, fmt( "peak at x = %.5f, height %.9f (theory: 1 at x = 1)", bestX, best ) );
	Check( physics::MeteorLight( 3.0 ) == 0.0 && physics::MeteorLight( 2.999 ) < 1e-5, "gone at x = 3: the body is used up" );
	// A shower: every launched meteor travels directly away from the radiant,
	// and its length goes as the sine of its distance from it.
	Events events;
	EventInputs in;
	in.shower = true;
	in.presses = 200;
	events.Advance( 0.0, 0.0, in, 7 );
	double worst = 0.0;
	for( const Meteor& m : events.Launched() )
	{
		const double ex = m.x - in.radiantX, ey = m.y - in.radiantY;
		const double d  = std::sqrt( ex * ex + ey * ey );
		worst           = std::max( worst, std::fabs( ex * m.dy - ey * m.dx ) / d );
	}
	Check( events.Launched().size() >= 20 && worst < 1e-5, fmt( "%zu shower meteors, worst miss of the radiant line %.2g", events.Launched().size(), worst ) );
	// Brightening as e^( v sin( entry ) t / H ): the time from first seen to
	// peak over the whole duration is ln( 1 / x0 ) / ln( 3 / x0 ), whatever the meteor.
	double share = 0.0;
	for( const Meteor& m : events.Launched() )
		share = std::max( share, std::fabs( std::log( 1.0 / physics::kMeteorStartX ) / m.rate / m.duration
		                                    - std::log( 1.0 / physics::kMeteorStartX ) / std::log( 3.0 / physics::kMeteorStartX ) ) );
	Check( share < 1e-5, fmt( "every meteor peaks %.1f%% of the way through its flight", 100.0 * std::log( 1.0 / physics::kMeteorStartX ) / std::log( 3.0 / physics::kMeteorStartX ) ) );
	// A press always shows: sporadic and shower, every pressed meteor puts at
	// least 0.25 frame heights of its trail (or all of a shorter one) on a 4:3
	// picture, is a fireball, and lasts long enough to see.
	{
		size_t presses = 0;
		double worstOn = 1e9, worstPeak = 1e9, worstLife = 1e9;
		for( const bool shower : { false, true } )
		{
			Events pressed;
			EventInputs pin;
			pin.shower  = shower;
			pin.presses = 24;
			for( int k = 0; k < 20; ++k )
				pressed.Advance( 100.0 * k, 0.0, pin, 11 + k );
			for( const Meteor& m : pressed.Launched() )
			{
				int on = 0;
				for( int i = 0; i <= 200; ++i )
				{
					const double s = m.length * i / 200.0;
					on += std::fabs( m.x + m.dx * s ) < 2.0 / 3.0 && std::fabs( m.y + m.dy * s ) < 0.5;
				}
				worstOn   = std::min( worstOn, m.length * on / 201.0 / std::min( static_cast< double >( m.length ), 0.25 ) );
				worstPeak = std::min( worstPeak, static_cast< double >( m.peak ) );
				worstLife = std::min( worstLife, static_cast< double >( m.duration ) );
				++presses;
			}
		}
		Check( presses >= 400 && worstOn >= 0.99 && worstPeak >= 40.0 && worstLife >= 0.4,
		       fmt( "%zu presses: the least of a trail on a 4:3 picture %.2f of what is asked, the faintest peak %.0f, the shortest %.2f s",
		            presses, worstOn, worstPeak, worstLife ) );
	}
	return Verdict();
}

//===========================================================================
// --palette (no GL): filters, gains, balance, tables.
//===========================================================================
int runPalette( const Perturb& p )
{
	physics::SetHooksForTest( p.physicsHooks );
	std::printf( "\n=== palette: each filter's lines, each filter stretched to its target, the tables\n" );
	using physics::FilterNamed;
	using physics::LineResponse;
	struct Pass
	{
		const char* filter;
		physics::Component yes, no;
	};
	const Pass passes[] = {
		{ "F502N", physics::C_OXYGEN, physics::C_SULPHUR }, { "F657N", physics::C_HYDROGEN, physics::C_OXYGEN },
		{ "F673N", physics::C_SULPHUR, physics::C_OXYGEN }, { "F187N", physics::C_HYDROGEN, physics::C_PAH },
		{ "F470N", physics::C_MOLECULAR, physics::C_OXYGEN }, { "F335M", physics::C_PAH, physics::C_OXYGEN },
	};
	// A broad band's Gaussian wings reach everywhere at the 1e-5 level, so
	// "not the other" is a thousandth of it.
	for( const Pass& s : passes )
		Check( LineResponse( FilterNamed( s.filter ), s.yes ) > 0.0
		           && LineResponse( FilterNamed( s.filter ), s.no ) < 1e-3 * LineResponse( FilterNamed( s.filter ), s.yes ),
		       fmt( "%s sees its line and not the other", s.filter ) );
	// Each filter's gain brings the target's own light in it to one.
	for( int id = 0; id < physics::kInstruments; ++id )
	{
		const physics::Mix mix     = physics::NebulaMix();
		const physics::Palette pal = physics::BuildPalette( id, mix );
		const physics::Instrument& inst = physics::InstrumentOf( id );
		double worst = 0.0;
		for( int f = 0; f < pal.filterCount; ++f )
		{
			const physics::Filter& filter = FilterNamed( inst.filters[ f ] );
			double ref = mix.star * physics::ContinuumResponse( filter, 0, mix.starTemp ) + mix.dust * inst.dustGlow * physics::ContinuumResponse( filter, 2, mix.dustTemp );
			for( int c = 0; c < physics::kLineComponents; ++c )
				ref += mix.line[ c ] * LineResponse( filter, static_cast< physics::Component >( c ) );
			worst = std::max( worst, std::fabs( pal.gain[ f ] * ref - 1.0 ) );
		}
		Check( worst < 1e-9, fmt( "%-15s every filter's own light stretched to 1 (worst %.1e)", inst.name, worst ) );
		// Balance: unbalanced keys make a flat signal white; balanced ones a G star.
		double flat[ physics::kMaxFilters ];
		for( int f = 0; f < pal.filterCount; ++f )
			flat[ f ] = 1.0 / pal.gain[ f ];
		std::array< double, 3 > rgb { 0, 0, 0 };
		if( inst.whiteBalance == 0.0 )
			for( int f = 0; f < pal.filterCount; ++f )
				for( int c = 0; c < 3; ++c )
					rgb[ c ] += pal.gain[ f ] * flat[ f ] * pal.colour[ f ][ c ];
		else if( inst.whiteBalance == 1.0 )
			rgb = physics::DisplayOfContinuum( pal, 0, 5800.0, 0.0 );
		else
			continue;
		const double mean = ( rgb[ 0 ] + rgb[ 1 ] + rgb[ 2 ] ) / 3.0;
		Check( std::fabs( rgb[ 0 ] - mean ) < 1e-9 * mean + 1e-30 && std::fabs( rgb[ 2 ] - mean ) < 1e-9 * mean + 1e-30,
		       fmt( "%-15s %s is grey", inst.name, inst.whiteBalance == 0.0 ? "a signal equal in every filter" : "a 5800 K star" ) );
	}
	// The tables against the exact sums they tabulate, at their own samples.
	const physics::Palette pal = physics::BuildPalette( physics::I_WEBB_NIRCAM );
	double worst = 0.0;
	for( int c = 0; c < physics::kLineComponents; ++c )
		for( int j : { 0, 7, 47 } )
		{
			const auto rgb = physics::DisplayOf( pal, static_cast< physics::Component >( c ), physics::kTauMax * j / ( physics::kTauSamples - 1 ) );
			for( int k = 0; k < 3; ++k )
				worst = std::max( worst, std::fabs( pal.lines[ ( static_cast< size_t >( c ) * physics::kTauSamples + j ) * 3 + k ] - rgb[ k ] ) / ( std::fabs( rgb[ k ] ) + 1e-12 ) );
		}
	Check( worst < 1e-6, fmt( "the line table is the exact sum at its samples (worst relative %.1e: a float's rounding)", worst ) );
	physics::SetHooksForTest( 0 );
	return Verdict();
}

//===========================================================================
// --names (no GL): what a host and the GLSL are told.
//===========================================================================
int runNames( const Perturb& )
{
	std::printf( "\n=== names: parameter names, options, and the GLSL's constants\n" );
	for( bool effect : { false, true } )
	{
		SiderealPlugin plugin( effect );
		std::vector< std::string > seen;
		bool ok = true;
		for( unsigned int i = 0; i < plugin.GetNumParams(); ++i )
		{
			const std::string name = plugin.GetParamName( i );
			ok = ok && name.size() <= 16 && !name.empty() && std::find( seen.begin(), seen.end(), name ) == seen.end();
			seen.push_back( name );
		}
		Check( ok, fmt( "%s: %u names, unique, none past 16 characters", effect ? "Over" : "source", plugin.GetNumParams() ) );
	}
	Check( std::strlen( "SW Sidereal Over" ) == 16 && std::strlen( "SW Sidereal" ) <= 16, "the display names fit FFGL's 16" );
	for( int t = 0; t < scene::TargetCount(); ++t )
		if( std::strlen( scene::TargetName( t ) ) > 16 )
			Check( false, fmt( "target name '%s' is too long for an option", scene::TargetName( t ) ) );
	const std::string common = shaders::kCommon;
	auto defines             = [ & ]( const char* name, long value ) {
        const std::string key = std::string( "#define " ) + name + " ";
        const size_t at       = common.find( key );
        return at != std::string::npos && std::strtol( common.c_str() + at + key.size(), nullptr, 10 ) == value;
	};
	Check( defines( "TAU_N", physics::kTauSamples ) && defines( "TEMP_N", physics::kTempSamples ) && defines( "LINE_N", physics::kLineComponents )
	           && defines( "MAX_SOURCES", shaders::kMaxSources ) && defines( "MAX_GALAXIES", shaders::kMaxGalaxies ) && defines( "SN_N", shaders::kSupernovaSamples ),
	       "the GLSL's sizes are the C++'s" );
	Check( defines( "C_OXYGEN", physics::C_OXYGEN ) && defines( "C_HYDROGEN", physics::C_HYDROGEN ) && defines( "C_SULPHUR", physics::C_SULPHUR )
	           && defines( "C_SHOCK", physics::C_SHOCK ) && defines( "C_MOLECULAR", physics::C_MOLECULAR ) && defines( "C_METEOR_SLOW", physics::C_METEOR_SLOW )
	           && defines( "C_METEOR_FAST", physics::C_METEOR_FAST ) && defines( "C_TRAIN", physics::C_TRAIN ) && defines( "C_PAH", physics::C_PAH )
	           && defines( "C_SYNCHROTRON", physics::C_SYNCHROTRON ),
	       "the GLSL's component rows are the C++'s" );
	Check( defines( "HK_RAW_EMISSION", shaders::kHookRawEmission ) && defines( "HK_NO_SHADOW", shaders::kHookNoShadow ) && defines( "HK_ONE_ZONE", shaders::kHookOneZone )
	           && defines( "HK_GREY_PSF", shaders::kHookGreyPsf ) && defines( "HK_SPIKE_LAW", shaders::kHookSpikeLaw ) && defines( "HK_LENS_BRIGHT", shaders::kHookLensBright )
	           && defines( "HK_COARSE_MARCH", shaders::kHookCoarseMarch ) && defines( "HK_WRONG_PARALLAX", shaders::kHookWrongParallax )
	           && defines( "HK_ECHO_SPHERE", shaders::kHookEchoSphere ) && defines( "HK_RAW_SPRITES", shaders::kHookRawSprites )
	           && defines( "HK_LENS_MAP", shaders::kHookLensMap ),
	       "the GLSL's hook bits are the C++'s" );
	return Verdict();
}

//===========================================================================
// --photons: every ionising photon is accounted for.
//===========================================================================
int runPhotons( const Perturb& p )
{
	std::printf( "\n=== photons: along a ray from the star, the gas's recombination light adds up to what the star sent that way\n" );
	const SiderealPlugin::Constants& k = SiderealPlugin::Shipped();
	for( const Raster& r : kRasters )
	{
		// A slab from x = -0.2 to past the buffer, density 0.3 over thickness
		// 0.4: its front's e-folding length 1 / alpha = 0.056 frame heights,
		// five of the 320x180 field's texels, so the front is resolved there.
		const float density = 0.3f, thickness = 0.4f, edge = -0.2f, srcX = -0.75f, lum = 0.05f;
		Bench b;
		if( !b.Start( r.w, r.h, slabTarget( density, thickness, edge, srcX, 0.0f, lum, 40000.0f ), shaders::kHookRawEmission | p.hooks ) || !b.rig.Render( 2 ) )
			return 1;
		const Floats out = b.rig.Output();
		const int row    = r.h / 2;
		// Sum E d^2 dx over the row, from the slab's front to the frame's edge;
		// the slab runs on past the frame, so add the tail the frame cuts off
		// as what is left of the beam there: E d^2 / alpha at the last pixel.
		const double dx = 1.0 / r.h;
		const double alpha = k.ionK * density / thickness;
		double sum = 0.0, last = 0.0;
		for( int i = 0; i < r.w; ++i )
		{
			const double x = screenX( i, r.w, r.h );
			if( x < edge - 0.05 )
				continue;
			const double d = x - srcX;
			last           = out[ ( static_cast< size_t >( row ) * r.w + i ) * 4 + 1 ] * d * d;
			sum += last * dx;
		}
		sum += last / alpha;
		const double budget = sum / ( k.emitH * thickness * lum );
		// 1 - e^-tau over the whole slab is 1 to many places. The tolerance is
		// the half-float the field and light are stored in (2^-11 relative)
		// summed over the front, plus the bilinear reading of a resolved front
		// (within 3%): 5%.
		Check( std::fabs( budget - 1.0 ) < 0.05, fmt( "%dx%d: recombinations / photons sent = %.4f", r.w, r.h, budget ) );
	}
	return Verdict();
}

//===========================================================================
// --strata: the ionisation zones stand in order from the star.
//===========================================================================
int runStrata( const Perturb& p )
{
	std::printf( "\n=== strata: [O III] nearest the star, then H, then [S II], then the aromatics\n" );
	for( const Raster& r : kRasters )
	{
		const float density = 0.3f, thickness = 0.4f, edge = -0.2f, srcX = -0.75f;
		Bench b;
		if( !b.Start( r.w, r.h, slabTarget( density, thickness, edge, srcX, 0.0f, 0.05f, 40000.0f ), shaders::kHookRawEmission | p.hooks ) || !b.rig.Render( 2 ) )
			return 1;
		const Floats out = b.rig.Output();
		const int row    = r.h / 2;
		double depth[ 4 ] = {}, weight[ 4 ] = {};
		for( int i = 0; i < r.w; ++i )
		{
			const double x = screenX( i, r.w, r.h );
			if( x < edge - 0.05 )
				continue;
			for( int c = 0; c < 4; ++c )
			{
				const double e = out[ ( static_cast< size_t >( row ) * r.w + i ) * 4 + c ];
				depth[ c ] += e * ( x - edge );
				weight[ c ] += e;
			}
		}
		for( int c = 0; c < 4; ++c )
			depth[ c ] /= std::max( weight[ c ], 1e-30 );
		// A zone is a zone when it is a field texel apart from the next.
		const double texel = ( 1.0 + 2.0 * SiderealPlugin::Shipped().margin ) / b.rig.plugin.FieldHeight();
		Check( depth[ 0 ] + texel < depth[ 1 ] && depth[ 1 ] + texel < depth[ 2 ] && depth[ 2 ] + texel < depth[ 3 ],
		       fmt( "%dx%d: mean depth into the gas: O %.4f < H %.4f < S %.4f < PAH %.4f (a texel is %.4f)", r.w, r.h, depth[ 0 ], depth[ 1 ], depth[ 2 ], depth[ 3 ], texel ) );
	}
	// A hotter star makes more O++: the [O III] / H ratio rises with Star Heat.
	double last = 0.0;
	bool rising = true;
	for( float heat : { 0.2f, 0.5f, 0.8f } )
	{
		Bench b;
		if( !b.Start( 320, 180, slabTarget( 0.3f, 0.4f, -0.2f, -0.75f, 0.0f, 0.05f, 40000.0f ), shaders::kHookRawEmission | p.hooks ) )
			return 1;
		b.rig.Set( PT_STAR_HEAT, heat );
		b.rig.Render( 2 );
		const Floats out = b.rig.Output();
		double o = 0.0, h = 0.0;
		for( size_t i = 0; i < out.size(); i += 4 )
		{
			o += out[ i ];
			h += out[ i + 1 ];
		}
		rising = rising && o / h > last;
		last   = o / h;
	}
	Check( rising, "a hotter star makes relatively more [O III]" );
	return Verdict();
}

//===========================================================================
// --psf: the diffraction core grows with wavelength; the spikes.
//===========================================================================
struct StarFit
{
	double cx, cy, s2r, s2b, total;
};

/// Second moments of the sprite buffer's R and B about the brightest pixel,
/// inside a box: the core's width per channel.
StarFit fitStar( const Floats& out, int w, int h, int box )
{
	int bx = 0, by = 0;
	double best = -1.0;
	for( int y = 0; y < h; ++y )
		for( int x = 0; x < w; ++x )
		{
			const float* q = &out[ ( static_cast< size_t >( y ) * w + x ) * 4 ];
			if( q[ 0 ] + q[ 2 ] > best )
			{
				best = q[ 0 ] + q[ 2 ];
				bx   = x;
				by   = y;
			}
		}
	StarFit f { static_cast< double >( bx ), static_cast< double >( by ), 0, 0, 0 };
	double wr = 0, wb = 0;
	for( int y = std::max( 0, by - box ); y <= std::min( h - 1, by + box ); ++y )
		for( int x = std::max( 0, bx - box ); x <= std::min( w - 1, bx + box ); ++x )
		{
			const float* q = &out[ ( static_cast< size_t >( y ) * w + x ) * 4 ];
			const double r2 = ( x - bx ) * ( x - bx ) + ( y - by ) * ( y - by );
			// Only the core: inside four sigma of the narrowest, outside the spikes' reach.
			f.s2r += q[ 0 ] * r2;
			wr += q[ 0 ];
			f.s2b += q[ 2 ] * r2;
			wb += q[ 2 ];
		}
	f.s2r /= std::max( wr, 1e-30 );
	f.s2b /= std::max( wb, 1e-30 );
	f.total = wr + wb;
	return f;
}

int runPsf( const Perturb& p )
{
	std::printf( "\n=== psf: Hubble's F502N (blue) and F658N (red): the diffraction core's width against the wavelength\n" );
	// The HOO key's channels are nearly pure filters: red is F658N alone, blue
	// F502N with 2% of F658N. The core is narrower than a pixel at CI's raster
	// (0.6 px at 2.4 m and 0.5 um on 1080 lines scaled to 180), so this check
	// runs where the core is resolved: 1920x1080 and 3840x2160.
	const Raster rasters[] = { { 1920, 1080 }, { 3840, 2160 } };
	for( const Raster& r : rasters )
	{
		// The star on a pixel's centre, so the brightest pixel is the peak.
		scene::Target t = emptyTarget();
		scene::Star s;
		s.x           = 0.5f / r.h;
		s.y           = 0.5f / r.h;
		s.flux        = 2.0f;
		s.temperature = 6000.0f;
		s.layer       = 0;
		t.stars.push_back( s );
		Bench b;
		if( !b.Start( r.w, r.h, t, shaders::kHookRawSprites | p.hooks, physics::I_HELIX ) )
			return 1;
		b.rig.Set( PT_STARS, 0.5f );
		b.rig.Set( PT_SPIKES, 0.0f );
		b.rig.Render( 2 );
		const Floats out = b.rig.Output();
		// Peak over total: a Gaussian core of sigma and a halo that scales
		// with it give peak / total = k / sigma^2, the same k for both filters.
		const int cx = r.w / 2, cy = r.h / 2, box = 60;
		double total[ 3 ] = {}, peak[ 3 ] = {};
		for( int y = cy - box; y <= cy + box; ++y )
			for( int x = cx - box; x <= cx + box; ++x )
				for( int c = 0; c < 3; ++c )
					total[ c ] += out[ ( static_cast< size_t >( y ) * r.w + x ) * 4 + c ];
		for( int c = 0; c < 3; ++c )
			peak[ c ] = out[ ( static_cast< size_t >( cy ) * r.w + cx ) * 4 + c ];
		const SiderealPlugin::Constants& k = SiderealPlugin::Shipped();
		const double shape = ( 1.0 - k.haloFrac ) / ( 2.0 * kPi ) + 1.5 * k.haloFrac / ( kPi * k.haloWidth * k.haloWidth );
		const double floor2 = std::pow( b.rig.plugin.SeeingSigmaPx(), 2.0 );
		const double vr = shape * total[ 0 ] / peak[ 0 ] - floor2, vb = shape * total[ 2 ] / peak[ 2 ] - floor2;
		const double got = std::sqrt( std::max( vr, 1e-9 ) / std::max( vb, 1e-9 ) ), lambda = 0.6584 / 0.5010;
		// The peak is a sample, not a pixel's mean: a Gaussian of sigma 0.6
		// sampled at its centre reads 1.5% above the pixel average. With the 2%
		// of F658N in blue and the half-float buffer: 5%.
		Check( std::fabs( got / lambda - 1.0 ) < 0.05, fmt( "%dx%d: red core / blue core = %.3f (pivots: %.3f)", r.w, r.h, got, lambda ) );
	}
	return Verdict();
}

/// The azimuthal profile on a ring round a bright star, read bilinearly: the
/// spikes' angles, each the middle of a run of bins standing above the ring's
/// median (the halo and the core are round; a spike is not).
std::vector< int > spikeAngles( const Floats& out, int w, int h, double cx, double cy, double radius )
{
	const int bins = 360;
	std::vector< double > ring( bins, 0.0 );
	auto sample = [ & ]( double x, double y ) {
		const int x0 = static_cast< int >( std::floor( x ) ), y0 = static_cast< int >( std::floor( y ) );
		const double fx = x - x0, fy = y - y0;
		auto at = [ & ]( int xx, int yy ) { return luma( &out[ ( static_cast< size_t >( std::clamp( yy, 0, h - 1 ) ) * w + std::clamp( xx, 0, w - 1 ) ) * 4 ] ); };
		return ( at( x0, y0 ) * ( 1 - fx ) + at( x0 + 1, y0 ) * fx ) * ( 1 - fy ) + ( at( x0, y0 + 1 ) * ( 1 - fx ) + at( x0 + 1, y0 + 1 ) * fx ) * fy;
	};
	for( int a = 0; a < bins; ++a )
	{
		const double th = a * 2.0 * kPi / bins;
		// Pixel centres sit at +0.5.
		ring[ static_cast< size_t >( a ) ] = sample( cx + radius * std::cos( th ) - 0.5, cy + radius * std::sin( th ) - 0.5 );
	}
	std::vector< double > sorted = ring;
	std::sort( sorted.begin(), sorted.end() );
	const double median = sorted[ bins / 2 ];
	std::vector< int > peaks;
	for( int a = 0; a < bins; ++a )
	{
		const bool on = ring[ static_cast< size_t >( a ) ] > 2.0 * median + 1e-9;
		const bool before = ring[ static_cast< size_t >( ( a + bins - 1 ) % bins ) ] > 2.0 * median + 1e-9;
		if( !on || before )
			continue;
		int end = a;
		while( ring[ static_cast< size_t >( ( end + 1 ) % bins ) ] > 2.0 * median + 1e-9 && end - a < bins )
			++end;
		peaks.push_back( ( ( a + end ) / 2 ) % bins );
	}
	std::sort( peaks.begin(), peaks.end() );
	return peaks;
}

int runSpikes( const Perturb& p )
{
	std::printf( "\n=== spikes: Hubble's four, Webb's six and two, and the 1 / r^2 along each\n" );
	struct Case
	{
		int instrument;
		std::vector< int > want;
		const char* what;
		double radius;
	};
	// The ring sits past the core and inside the spikes: Webb's halo (its
	// core is wider at 2-4 um) still stands at twice the ring's median within
	// 12 px of a star this bright, so its ring is further out.
	const Case cases[] = {
		{ physics::I_HUBBLE_NARROW, { 45, 135, 225, 315 }, "Hubble: four spikes from the spider, at 45 degrees on the frame", 10.0 },
		{ physics::I_WEBB_NIRCAM, { 0, 30, 90, 150, 180, 210, 270, 330 }, "Webb: six from the segments' edges, two from the vertical strut", 20.0 },
	};
	for( const Case& c : cases )
	{
		scene::Target t = emptyTarget();
		scene::Star s;
		s.flux        = 40.0f;
		s.temperature = 6000.0f;
		s.layer       = 0;
		t.stars.push_back( s );
		Bench b;
		if( !b.Start( 1280, 720, t, shaders::kHookRawSprites | p.hooks, c.instrument ) )
			return 1;
		b.rig.Set( PT_STARS, 0.5f );
		b.rig.Render( 2 );
		const Floats out = b.rig.Output();
		if( const char* dump = std::getenv( "SDTEST_DUMP" ) )
			writePng( std::string( dump ) + "/spikes-" + std::to_string( c.instrument ) + ".png", 1280, 720, out );
		const auto peaks = spikeAngles( out, 1280, 720, 640.0, 360.0, c.radius );
		bool match       = peaks.size() == c.want.size();
		for( size_t i = 0; match && i < peaks.size(); ++i )
			match = std::abs( peaks[ i ] - c.want[ i ] ) <= 2;
		std::string seen;
		for( int a : peaks )
			seen += fmt( " %d", a );
		Check( match, fmt( "%s:%s", c.what, seen.c_str() ) );
		if( c.instrument == physics::I_HUBBLE_NARROW )
		{
			// Along the 45 degree spike, less the same radius between spikes
			// (the core and the halo are round, the spike is not): I( r ) e^( r / L )
			// ~ r^-2, with r0 = 2 sigma under 2 px and L tens of pixels.
			const double L = b.rig.plugin.SpikeLengthPx();
			auto at        = [ & ]( double rr, double degrees ) {
                const double th = degrees * kPi / 180.0;
                const int x = static_cast< int >( std::lround( 640.0 + rr * std::cos( th ) ) ), y = static_cast< int >( std::lround( 360.0 + rr * std::sin( th ) ) );
                return luma( &out[ ( static_cast< size_t >( y ) * 1280 + x ) * 4 ] );
			};
			auto spike = [ & ]( double rr ) { return at( rr, 45.0 ) - at( rr, 67.5 ); };
			// The spike's length goes as each filter's pivot; F657N's carries
			// most of the luminance.
			const double Lg = L * 0.6567;
			const double r1 = 6.0, r2 = 18.0;
			const double slope = std::log( ( spike( r2 ) * std::exp( r2 / Lg ) ) / ( spike( r1 ) * std::exp( r1 / Lg ) ) ) / std::log( r2 / r1 );
			// r0^2 / ( r0^2 + r^2 ) is r^-2 only for r >> r0: at 6 px with r0
			// near 1.5 the local slope is -1.88; 1 / r gives -1.
			Check( std::fabs( slope + 2.0 ) < 0.3, fmt( "Hubble's spike falls as r^%.2f between %.0f and %.0f px", slope, r1, r2 ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --reddening: a star behind dust, measured in the picture, dims and
// reddens as CCM's law through each filter says.
//===========================================================================
int runReddening( const Perturb& p )
{
	std::printf( "\n=== reddening: a star behind a dust column, its colour in the picture against the CCM prediction\n" );
	for( const Raster& r : kRasters )
	{
		double measured[ 2 ][ 3 ] = {};
		const float columns[ 2 ] = { 0.0f, 0.4f };
		for( int k = 0; k < 2; ++k )
		{
			scene::Target t = emptyTarget();
			t.dustRatio     = 4.0f;
			if( columns[ k ] > 0.0f )
			{
				scene::Element e;
				e.shape   = scene::Shape::Sheet;
				e.channel = scene::Channel::Foreground;
				e.x       = -5.0f;
				e.x2      = 1.0f;
				e.y2      = 0.0f;
				e.density = columns[ k ];
				e.edge    = 0.01f;
				e.detail  = 0.0f;
				e.rough   = 0.0f;
				t.elements.push_back( e );
			}
			scene::Star s;
			s.flux        = 3.0f;
			s.temperature = 6000.0f;
			s.layer       = 1;
			t.stars.push_back( s );
			Bench b;
			if( !b.Start( r.w, r.h, t, shaders::kHookRawSprites | p.hooks, physics::I_HUBBLE_COLOUR ) )
				return 1;
			b.rig.Set( PT_STARS, 0.5f );
			b.rig.Set( PT_SPIKES, 0.0f );
			b.rig.Render( 2 );
			const Floats out = b.rig.Output();
			for( size_t i = 0; i < out.size(); i += 4 )
				for( int c = 0; c < 3; ++c )
					measured[ k ][ c ] += out[ i + c ];
		}
		// The prediction from the palette's filters directly, in double.
		const physics::Palette pal = physics::BuildPalette( physics::I_HUBBLE_COLOUR, physics::NebulaMix() );
		const auto clear = physics::DisplayOfContinuum( pal, 0, 6000.0, 0.0 );
		const auto dusty = physics::DisplayOfContinuum( pal, 0, 6000.0, 4.0 * 0.4 );
		double worst     = 0.0;
		for( int c = 0; c < 3; ++c )
			worst = std::max( worst, std::fabs( ( measured[ 1 ][ c ] / measured[ 0 ][ c ] ) / ( dusty[ c ] / clear[ c ] ) - 1.0 ) );
		// tau_V 1.6: the half-float field and the star texture's linear
		// filtering of the temperature row agree to 1%.
		Check( worst < 0.02, fmt( "%dx%d: transmission R %.3f G %.3f B %.3f, predicted %.3f %.3f %.3f", r.w, r.h, measured[ 1 ][ 0 ] / measured[ 0 ][ 0 ],
		                          measured[ 1 ][ 1 ] / measured[ 0 ][ 1 ], measured[ 1 ][ 2 ] / measured[ 0 ][ 2 ], dusty[ 0 ] / clear[ 0 ], dusty[ 1 ] / clear[ 1 ],
		                          dusty[ 2 ] / clear[ 2 ] ) );
		Check( measured[ 1 ][ 2 ] / measured[ 0 ][ 2 ] < measured[ 1 ][ 0 ] / measured[ 0 ][ 0 ], fmt( "%dx%d: and blue is dimmed more than red", r.w, r.h ) );
	}
	return Verdict();
}

//===========================================================================
// --parallax: near things move more than far ones when the camera moves.
//===========================================================================
int runParallax( const Perturb& p )
{
	std::printf( "\n=== parallax: two stars at depths 0.5 and 2 as the camera pans, against pan x ( 1 / depth - 1 ) x Depth / 2\n" );
	for( const Raster& r : kRasters )
	{
		double shift[ 2 ] = {};
		const float depths[ 2 ] = { 0.5f, 2.0f };
		for( int k = 0; k < 2; ++k )
		{
			double where[ 2 ] = {};
			for( int pass = 0; pass < 2; ++pass )
			{
				scene::Target t = emptyTarget();
				scene::Star s;
				s.flux        = 4.0f;
				s.temperature = 6000.0f;
				s.layer       = 0;
				s.depth       = depths[ k ];
				t.stars.push_back( s );
				Bench b;
				if( !b.Start( r.w, r.h, t, shaders::kHookRawSprites | p.hooks ) )
					return 1;
				b.rig.Set( PT_STARS, 0.5f );
				b.rig.Set( PT_SPIKES, 0.0f );
				b.rig.Set( PT_DEPTH, 1.0f );
				b.rig.Set( PT_PAN_X, pass == 0 ? 0.5f : 0.6f );
				b.rig.Render( 2 );
				const Floats out = b.rig.Output();
				double sx = 0.0, sw = 0.0;
				for( int y = 0; y < r.h; ++y )
					for( int x = 0; x < r.w; ++x )
					{
						const double v = luma( &out[ ( static_cast< size_t >( y ) * r.w + x ) * 4 ] );
						sx += v * x;
						sw += v;
					}
				where[ pass ] = sx / std::max( sw, 1e-30 );
			}
			shift[ k ] = ( where[ 1 ] - where[ 0 ] ) / r.h;
		}
		// Pan X 0.6 is 0.2 frame heights; Depth 1 scales the drift by 2 x 0.5:
		// scene shift = 0.2 ( 1 + ( 1 / d - 1 ) ), and the picture moves the other way.
		const double want0 = -0.2 * ( 1.0 + ( 1.0 / depths[ 0 ] - 1.0 ) ), want1 = -0.2 * ( 1.0 + ( 1.0 / depths[ 1 ] - 1.0 ) );
		// One pixel of centroid at 320x180 is 0.0056 frame heights.
		const double tol = 1.0 / r.h;
		Check( std::fabs( shift[ 0 ] - want0 ) < tol && std::fabs( shift[ 1 ] - want1 ) < tol,
		       fmt( "%dx%d: near star moved %.4f (want %.4f), far star %.4f (want %.4f)", r.w, r.h, shift[ 0 ], want0, shift[ 1 ], want1 ) );

		// The same law in the shaders (the stars above are placed by the CPU):
		// a blob of gas at each depth, its centroid read from the raw field.
		for( int k = 0; k < 2; ++k )
		{
			double where[ 2 ] = {};
			for( int pass = 0; pass < 2; ++pass )
			{
				scene::Target t = emptyTarget();
				scene::Element e;
				e.shape   = scene::Shape::Blob;
				e.channel = scene::Channel::Dense;
				e.a = e.b = 0.06f;
				e.density = 1.0f;
				e.edge    = 0.01f;
				e.rough   = 0.0f;
				e.detail  = 0.0f;
				e.depth   = depths[ k ];
				t.elements.push_back( e );
				Bench b;
				if( !b.Start( r.w, r.h, t, shaders::kHookRawField | p.hooks ) )
					return 1;
				b.rig.Set( PT_DEPTH, 1.0f );
				b.rig.Set( PT_PAN_X, pass == 0 ? 0.5f : 0.6f );
				b.rig.Render( 2 );
				const Floats out = b.rig.Output();
				double sx = 0.0, sw = 0.0;
				for( int y = 0; y < r.h; ++y )
					for( int x = 0; x < r.w; ++x )
					{
						const double v = out[ ( static_cast< size_t >( y ) * r.w + x ) * 4 ];
						sx += v * x;
						sw += v;
					}
				where[ pass ] = sx / std::max( sw, 1e-30 );
			}
			shift[ k ] = ( where[ 1 ] - where[ 0 ] ) / r.h;
		}
		// The field is half the raster, read bilinearly: two of its texels.
		const double ftol = 2.0 * ( 1.0 + 2.0 * SiderealPlugin::Shipped().margin ) / ( 0.5 * r.h * ( 1.0 + 2.0 * SiderealPlugin::Shipped().margin ) );
		Check( std::fabs( shift[ 0 ] - want0 ) < ftol && std::fabs( shift[ 1 ] - want1 ) < ftol,
		       fmt( "%dx%d: near gas moved %.4f (want %.4f), far gas %.4f (want %.4f)", r.w, r.h, shift[ 0 ], want0, shift[ 1 ], want1 ) );
	}
	return Verdict();
}

//===========================================================================
// --lens: the Einstein radius, and surface brightness conserved.
//===========================================================================
int runLens( const Perturb& p )
{
	std::printf( "\n=== lens: a lensed deep field's surface brightness, against the same field unlensed\n" );
	for( const Raster& r : kRasters )
	{
		double top[ 2 ] = {};
		for( int lensed = 0; lensed < 2; ++lensed )
		{
			scene::Target t = emptyTarget();
			t.asReleased    = physics::I_WEBB_DEEP;
			t.mix           = physics::StarlightMix();
			t.deep.on       = true;
			t.deep.density  = 2600.0f;
			t.deep.lens     = lensed == 1;
			t.deep.einstein = 0.15f;
			t.deep.members  = 0.0f;
			// The lens at z ~ 0, so the galaxies behind it are drawn from the
			// same redshifts as the unlensed field's (a real cluster lenses only
			// what is behind it, which is fainter: not what is being measured).
			t.deep.zLens = -0.15f;
			Bench b;
			if( !b.Start( r.w, r.h, t, shaders::kHookLinear | shaders::kHookNoStars | p.hooks ) )
				return 1;
			b.rig.Render( 2 );
			const Floats out = b.rig.Output();
			std::vector< double > l;
			for( size_t i = 0; i < out.size(); i += 4 )
				l.push_back( luma( &out[ i ] ) );
			std::sort( l.begin(), l.end() );
			top[ lensed ] = l[ l.size() * 999 / 1000 ];
		}
		// A lens stretches images but cannot brighten them: the brightest
		// pixels of the lensed field are as bright as the unlensed field's.
		// Different galaxies sit in them, so allow 30%; the magnification is
		// over 3 from 0.75 to 1.5 Einstein radii, a wide annulus.
		Check( top[ 1 ] < 1.3 * top[ 0 ], fmt( "%dx%d: 99.9th percentile lensed %.4f, unlensed %.4f", r.w, r.h, top[ 1 ], top[ 0 ] ) );

		// The Einstein ring: the pixels that see the source plane's centre lie
		// on a circle of the Einstein radius round the lens. Read the source
		// radius each pixel sees along the row through the lens.
		scene::Target t = emptyTarget();
		t.deep.on       = true;
		t.deep.density  = 100.0f;
		t.deep.lens     = true;
		t.deep.einstein = 0.15f;
		Bench b;
		if( !b.Start( r.w, r.h, t, shaders::kHookLinear | shaders::kHookNoStars | shaders::kHookLensMap | p.hooks ) )
			return 1;
		b.rig.Set( PT_EXPOSURE, 0.5f );
		b.rig.Render( 2 );
		const Floats out = b.rig.Output();
		const int row    = r.h / 2;
		int bestI        = r.w / 2;
		double best      = 1e9;
		for( int i = r.w / 2 + 1; i < r.w; ++i )
		{
			const double v = out[ ( static_cast< size_t >( row ) * r.w + i ) * 4 ] / b.rig.plugin.DisplayExposure();
			if( v < best )
			{
				best  = v;
				bestI = i;
			}
		}
		// The deep field sits at depth 4, which a still camera does not move.
		const double ring = screenX( bestI, r.w, r.h );
		Check( std::fabs( ring - 0.15 ) <= 1.0 / r.h, fmt( "%dx%d: the Einstein ring at %.4f frame heights (theta_E 0.15)", r.w, r.h, ring ) );
	}
	return Verdict();
}

//===========================================================================
// --echo: a light echo's ring, measured, against the paraboloid's radius.
//===========================================================================
int runEcho( const Perturb& p )
{
	std::printf( "\n=== echo: the light echo's ring radius against sqrt( 2 z c t + ( c t )^2 )\n" );
	for( const Raster& r : kRasters )
	{
		// The echo alone: the frame with Light Echo 1 less the same frame with 0
		// (the remnant and everything else cancel).
		Floats frames[ 2 ];
		double days = 0.0;
		for( int k = 0; k < 2; ++k )
		{
			scene::Target t = emptyTarget();
			scene::Element e;
			e.shape   = scene::Shape::Sheet;
			e.channel = scene::Channel::Foreground;
			e.x       = -5.0f;
			e.x2      = 1.0f;
			e.y2      = 0.0f;
			e.density = 0.3f;
			e.edge    = 0.01f;
			e.detail  = 0.0f;
			e.rough   = 0.0f;
			t.elements.push_back( e );
			t.snX          = 0.0f;
			t.snY          = 0.0f;
			t.echoDistance = 0.05f;
			Bench b;
			if( !b.Start( r.w, r.h, t, shaders::kHookLinear | shaders::kHookNoStars | p.hooks ) )
				return 1;
			b.rig.Set( PT_SN_SPEED, 0.5f );// 20 days a second
			b.rig.Set( PT_ECHO, k == 0 ? 1.0f : 0.0f );
			b.rig.Render( 1 );
			b.rig.Press( PT_SUPERNOVA );
			const int count = 300;
			b.rig.Render( count );
			days      = count / 60.0 * SupernovaSpeedFromParam( 0.5f );
			frames[ k ] = b.rig.Output();
		}
		// A Ia is still a quarter of its peak at day 100, so the echo is
		// brightest straight in front of the star, by the inverse square of the
		// light's path to the sheet. Divide the known dilution out, z^2 / ( rho^2
		// + z^2 ) for a sheet a distance z in front, and what is left is the
		// light curve read at each radius's delay: its peak is the ring.
		int bestI   = 0;
		double best = -1.0;
		const int row = r.h / 2;
		for( int i = r.w / 2 + 2; i < r.w; ++i )
		{
			const size_t at = ( static_cast< size_t >( row ) * r.w + i ) * 4;
			const double x  = screenX( i, r.w, r.h );
			const double v  = ( luma( &frames[ 0 ][ at ] ) - luma( &frames[ 1 ][ at ] ) ) * ( x * x + 0.05 * 0.05 ) / ( 0.05 * 0.05 );
			if( v > best )
			{
				best  = v;
				bestI = i;
			}
		}
		const double rho  = screenX( bestI, r.w, r.h );
		const double ct   = ( days - physics::SupernovaPeakDay() ) * Events::kLightPerDay;
		const double want = physics::EchoRadius( ct, 0.05 );
		// The light curve's peak is flat to 1% over two days, which is 0.007
		// frame heights of radius at day 100; and a pixel.
		Check( std::fabs( rho - want ) < 0.01 + 1.0 / r.h, fmt( "%dx%d: day %.0f, ring at %.4f frame heights, paraboloid %.4f", r.w, r.h, days, rho, want ) );
	}
	return Verdict();
}

//===========================================================================
// --meteor: a meteor's light, measured in the picture, peaks when theory says.
//===========================================================================
int runMeteor( const Perturb& p )
{
	std::printf( "\n=== meteor: one meteor's light frame by frame, its peak against ln( 1 / x0 ) / ln( 3 / x0 ) of its flight\n" );
	for( const Raster& r : kRasters )
	{
		scene::Target t = emptyTarget();
		Bench b;
		if( !b.Start( r.w, r.h, t, shaders::kHookRawSprites | p.hooks ) )
			return 1;
		b.rig.Render( 1 );
		Meteor m;
		m.x        = -0.5f;
		m.y        = 0.2f;
		m.dx       = 1.0f;
		m.dy       = -0.3f;
		const float l = std::sqrt( m.dx * m.dx + m.dy * m.dy );
		m.dx /= l;
		m.dy /= l;
		m.duration  = 1.0f;
		m.rate      = static_cast< float >( std::log( 3.0 / physics::kMeteorStartX ) );
		m.length    = 0.8f;
		m.peak      = 20.0f;
		m.fast      = 0.5f;
		m.train     = 0.0f;
		m.trainLife = 0.3f;
		m.start     = b.rig.Seconds( b.rig.frame );
		b.rig.plugin.EventsForTest().AddForTest( m );
		std::vector< double > light;
		for( int f = 0; f < 90; ++f )
		{
			b.rig.Render( 1 );
			const Floats out = b.rig.Output();
			double sum       = 0.0;
			for( size_t i = 0; i < out.size(); i += 4 )
				sum += luma( &out[ i ] );
			light.push_back( sum );
		}
		// The head and its short wake: the frame of the brightest is the peak.
		// Rendered at frame f the meteor is ( f + 1 ) / 60 s old (Render
		// stamps the frame before drawing).
		const size_t peakFrame = static_cast< size_t >( std::max_element( light.begin(), light.end() ) - light.begin() );
		const double peakTime  = ( peakFrame ) / 60.0;
		const double want      = std::log( 1.0 / physics::kMeteorStartX ) / std::log( 3.0 / physics::kMeteorStartX );
		// The wake (0.12 s) adds the recent past to each frame, which moves the
		// summed light's peak later by up to a wake's length; two frames' and
		// a wake's tolerance.
		Check( peakTime > want - 2.0 / 60.0 && peakTime < want + 0.12 + 2.0 / 60.0,
		       fmt( "%dx%d: brightest at %.3f s of 1.000 (theory %.3f, plus the wake)", r.w, r.h, peakTime, want ) );
		// It goes out abruptly at the end: by 1.3 s, a quarter of a second
		// later, the wake (0.12 s) has let go of everything it passed.
		Check( light[ 77 ] < 0.05 * light[ peakFrame ], fmt( "%dx%d: gone after the end: %.4f of peak at 1.3 s", r.w, r.h, light[ 77 ] / light[ peakFrame ] ) );
	}
	return Verdict();
}

//===========================================================================
// --state, --resize, --alpha.
//===========================================================================
int runState( const Perturb& )
{
	std::printf( "\n=== state: what the host had bound before is what it has after\n" );
	Rig rig( true );
	if( !rig.Init( 320, 180 ) )
		return 1;
	GLuint dummy = 0;
	glGenTextures( 1, &dummy );
	glActiveTexture( GL_TEXTURE3 );
	glBindTexture( GL_TEXTURE_2D, dummy );
	glActiveTexture( GL_TEXTURE0 );
	glEnable( GL_BLEND );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	rig.Render( 3 );
	GLint blendOn = 0, src = 0, unit = 0, program = 0, bound3 = 0, viewport[ 4 ] = {};
	blendOn = glIsEnabled( GL_BLEND );
	glGetIntegerv( GL_BLEND_SRC_RGB, &src );
	glGetIntegerv( GL_ACTIVE_TEXTURE, &unit );
	glGetIntegerv( GL_CURRENT_PROGRAM, &program );
	glGetIntegerv( GL_VIEWPORT, viewport );
	glActiveTexture( GL_TEXTURE3 );
	glGetIntegerv( GL_TEXTURE_BINDING_2D, &bound3 );
	glActiveTexture( GL_TEXTURE0 );
	Check( blendOn == GL_TRUE && src == GL_SRC_ALPHA, "the host's blend is put back" );
	Check( unit == GL_TEXTURE0, "texture unit 0 is active after" );
	Check( viewport[ 2 ] == 320 && viewport[ 3 ] == 180, "the viewport is the host's" );
	GLint units = 0;
	bool clean  = true;
	glGetIntegerv( GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &units );
	for( int u = 0; u < std::min( units, 16 ); ++u )
	{
		if( u == 3 )
			continue;
		GLint t = 0;
		glActiveTexture( static_cast< GLenum >( GL_TEXTURE0 + u ) );
		glGetIntegerv( GL_TEXTURE_BINDING_2D, &t );
		clean = clean && t == 0;
	}
	glActiveTexture( GL_TEXTURE0 );
	// FFGL asks for default state back: units unbound. The host's own binding
	// on unit 3 is cleared too (every ffglex scoped binding does the same);
	// Resolume binds what it draws with afresh.
	Check( clean, "no texture left bound on units 0-15 (bar unit 3, the host's, which is cleared)" );
	(void)bound3;
	(void)program;
	glDeleteTextures( 1, &dummy );
	glDisable( GL_BLEND );
	return Verdict();
}

int runResize( const Perturb& )
{
	std::printf( "\n=== resize: the raster changes under a running instance\n" );
	Rig rig;
	if( !rig.Init( 640, 360 ) )
		return 1;
	rig.Set( PT_DRIFT, 0.0f );
	rig.Set( PT_FLOW, 0.0f );
	rig.Render( 10 );
	rig.Resize( 320, 180 );
	bool ok          = rig.Render( 2 );
	const Floats a   = rig.Output();
	Rig fresh;
	fresh.Init( 320, 180 );
	fresh.Set( PT_DRIFT, 0.0f );
	fresh.Set( PT_FLOW, 0.0f );
	fresh.frame = rig.frame - 2;
	ok          = ok && fresh.Render( 2 );
	const Floats b = fresh.Output();
	double diff    = 0.0;
	for( size_t i = 0; i < a.size(); ++i )
		diff = std::max( diff, static_cast< double >( std::fabs( a[ i ] - b[ i ] ) ) );
	// No state is carried in GPU buffers across frames, so the resized
	// instance draws what a fresh one at that raster draws.
	Check( ok && diff < 1.0 / 255.0, fmt( "after a resize the picture is a fresh instance's (max difference %.5f)", diff ) );
	return Verdict();
}

int runAlpha( const Perturb& )
{
	std::printf( "\n=== alpha: the Over at Mix 0 is the clip; its alpha is mix( clip, 1, Mix )\n" );
	for( const Raster& r : kRasters )
	{
		const Floats clip = flatCard( r.w, r.h, 0.3f, 0.6f, 0.9f, 0.5f );
		for( float mix : { 0.0f, 0.5f } )
		{
			Rig rig( true );
			if( !rig.Init( r.w, r.h, &clip ) )
				return 1;
			rig.Set( PT_MIX, mix );
			rig.Render( 2 );
			const Floats out = rig.Output();
			double worstRgb = 0.0, worstA = 0.0;
			for( size_t i = 0; i < out.size(); i += 4 )
			{
				if( mix == 0.0f )
					for( int c = 0; c < 3; ++c )
						worstRgb = std::max( worstRgb, static_cast< double >( std::fabs( out[ i + c ] - clip[ i + c ] ) ) );
				worstA = std::max( worstA, static_cast< double >( std::fabs( out[ i + 3 ] - ( 0.5f + 0.5f * mix ) ) ) );
			}
			if( mix == 0.0f )
				Check( worstRgb < 1e-6, fmt( "%dx%d: Mix 0 returns the clip (worst %.2g)", r.w, r.h, worstRgb ) );
			Check( worstA < 1e-6, fmt( "%dx%d: Mix %.1f: alpha %.2f everywhere (worst error %.2g)", r.w, r.h, mix, 0.5 + 0.5 * mix, worstA ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --targets: every target, at its defaults, is a picture you can see.
//===========================================================================
int runTargets( const Perturb& )
{
	std::printf( "\n=== targets: every target at its defaults shows something, and nothing is blown out\n" );
	// The Pleiades came out of the first build at a 99th percentile of 0.075:
	// black, to anyone who loaded it. The floor is about half the dimmest
	// target that looks right (the deep fields, about 0.35). At 640x360, not
	// the small raster: at 320x180 the stars crowd and lift the percentile, so
	// the black Pleiades read 0.26 there.
	int dark = 0, blown = 0, broken = 0;
	double dimmest = 1e9, hottest = 0.0;
	const char* dimName = "";
	for( int t = 0; t < scene::TargetCount(); ++t )
	{
		Rig rig;
		if( !rig.Init( 640, 360 ) )
			return 1;
		rig.Set( PT_TARGET, static_cast< float >( t ) );
		rig.Set( PT_DRIFT, 0.0f );
		rig.Set( PT_FLOW, 0.0f );
		rig.Render( 30 );
		const Floats out = rig.Output();
		std::vector< float > luma;
		bool finite = true;
		for( size_t i = 0; i < out.size(); i += 4 )
		{
			finite = finite && std::isfinite( out[ i ] ) && std::isfinite( out[ i + 1 ] ) && std::isfinite( out[ i + 2 ] );
			luma.push_back( 0.2126f * out[ i ] + 0.7152f * out[ i + 1 ] + 0.0722f * out[ i + 2 ] );
		}
		std::sort( luma.begin(), luma.end() );
		const double p99     = luma[ luma.size() * 99 / 100 ];
		const double clipped = static_cast< double >( std::count_if( luma.begin(), luma.end(), []( float l ) { return l >= 0.999f; } ) ) / luma.size();
		if( p99 < dimmest )
		{
			dimmest = p99;
			dimName = scene::TargetName( t );
		}
		hottest = std::max( hottest, clipped );
		dark += p99 < 0.18;
		blown += clipped > 0.02;
		broken += !finite;
		if( p99 < 0.18 || clipped > 0.02 || !finite )
			std::printf( "        %s: 99th percentile %.3f, clipped %.2f%%%s\n", scene::TargetName( t ), p99, 100.0 * clipped, finite ? "" : ", NOT FINITE" );
	}
	Check( dark == 0 && broken == 0, fmt( "%d targets, none dark: the dimmest's 99th percentile %.3f (%s), floor 0.18", scene::TargetCount(), dimmest, dimName ) );
	Check( blown == 0, fmt( "none blown out: at most %.2f%% of a target's pixels clipped (limit 2%%)", 100.0 * hottest ) );
	return Verdict();
}

//===========================================================================
// The registry.
//===========================================================================
struct CheckEntry
{
	const char* flag;
	CheckFn run;
	bool offline;///< needs no GL context
};

const std::vector< CheckEntry >& checks()
{
	static const std::vector< CheckEntry > list = {
		{ "hardness", runHardness, true },
		{ "extinction", runExtinction, true },
		{ "cosmology", runCosmology, true },
		{ "supernova", runSupernova, true },
		{ "meteorcurve", runMeteorCurve, true },
		{ "palette", runPalette, true },
		{ "names", runNames, true },
		{ "photons", runPhotons, false },
		{ "strata", runStrata, false },
		{ "psf", runPsf, false },
		{ "spikes", runSpikes, false },
		{ "reddening", runReddening, false },
		{ "parallax", runParallax, false },
		{ "lens", runLens, false },
		{ "echo", runEcho, false },
		{ "meteor", runMeteor, false },
		{ "state", runState, false },
		{ "resize", runResize, false },
		{ "alpha", runAlpha, false },
		{ "targets", runTargets, false },
	};
	return list;
}

//===========================================================================
// --negative: every check against a model that is wrong on purpose.
//===========================================================================
int runNegative( bool offlineOnly = false )
{
	struct Case
	{
		const char* name;
		CheckFn check;
		Perturb perturb;
		const char* what;
		bool offline;
	};
	std::vector< Case > cases;
	auto add = [ & ]( const char* name, CheckFn fn, const char* what, int hooks, int physicsHooks, bool offline ) {
		Perturb p;
		p.hooks        = hooks;
		p.physicsHooks = physicsHooks;
		cases.push_back( { name, fn, p, what, offline } );
	};
	using namespace shaders;
	add( "hardness", runHardness, "the photon tail's series cut to its first term", 0, physics::PH_SOFT_STARS, true );
	add( "extinction", runExtinction, "every wavelength dimmed like V", 0, physics::PH_FLAT_EXTINCTION, true );
	add( "cosmology", runCosmology, "D_A = c z / H0 / ( 1 + z ): no deceleration", 0, physics::PH_LINEAR_REDSHIFT, true );
	add( "supernova", runSupernova, "gamma rays never escape: a pure cobalt tail", 0, physics::PH_NO_ESCAPE, true );
	add( "palette", runPalette, "one gain for every filter: nothing stretched to its own range", 0, physics::PH_GREY_PALETTE, true );
	add( "photons", runPhotons, "the gas absorbs nothing on the way", kHookNoShadow, 0, false );
	add( "strata", runStrata, "oxygen ionised as easily as hydrogen, no partly ionised zone", kHookOneZone, 0, false );
	add( "psf", runPsf, "every filter's core the same size", kHookGreyPsf, 0, false );
	add( "spikes", runSpikes, "spikes falling as 1 / r", kHookSpikeLaw, 0, false );
	add( "reddening", runReddening, "every wavelength dimmed like V", 0, physics::PH_FLAT_EXTINCTION, false );
	add( "parallax", runParallax, "parallax growing with depth", kHookWrongParallax, 0, false );
	add( "lens", runLens, "lensed images brightened by the magnification", kHookLensBright, 0, false );
	add( "echo", runEcho, "the echo's delay as if the dust were a shell round the star", kHookEchoSphere, 0, false );
	int undetected = 0, run = 0;
	std::printf( "\n=== negative controls: each must FAIL\n" );
	for( const Case& c : cases )
	{
		if( offlineOnly && !c.offline )
			continue;
		++run;
		physics::SetHooksForTest( c.perturb.physicsHooks );
		const int savedFailures = g_failures, savedChecks = g_checks;
		g_failures = 0;
		std::printf( "\n--- %s, against: %s\n", c.name, c.what );
		const int verdict = c.check( c.perturb );
		physics::SetHooksForTest( 0 );
		if( verdict == 0 )
		{
			++undetected;
			std::printf( "  !! %s PASSED against %s\n", c.name, c.what );
		}
		g_failures = savedFailures;
		g_checks   = savedChecks;
	}
	std::printf( "\nnegative controls: %d run, %d caught\n", run, run - undetected );
	return undetected == 0 ? 0 : 1;
}

int runOffline()
{
	int failed = 0;
	for( const CheckEntry& check : checks() )
		if( check.offline )
		{
			g_failures = 0;
			failed |= check.run( Perturb {} );
		}
	g_failures = 0;
	failed |= runNegative( true );
	std::printf( "\n  offline: the checks that need no GL context. The picture checks were NOT run --\n"
	             "  tools/verify.sh runs them against a real driver, at 320x180 and above, and again on the software renderer.\n"
	             "\n  %s\n",
	             failed == 0 ? "PASS" : "FAIL" );
	return failed == 0 ? 0 : 1;
}

//===========================================================================
// --bench: GPU time per frame from GL_TIME_ELAPSED.
//===========================================================================
int runBench()
{
	struct Size
	{
		int w, h;
		const char* name;
	};
	const Size sizes[] = { { 1280, 720, "720p" }, { 1920, 1080, "1080p" }, { 3840, 2160, "4K" } };
	struct Case
	{
		const char* what;
		bool over;
		const char* target;
	};
	const Case cases[] = {
		{ "source, Pillars (defaults)", false, "Pillars" },
		{ "source, Cosmic Cliffs", false, "Cosmic Cliffs" },
		{ "source, Whirlpool", false, "Whirlpool" },
		{ "source, First Deep (lensed)", false, "First Deep" },
		{ "Over, Pillars", true, "Pillars" },
	};
	std::printf( "\n=== bench: GPU time per frame (GL_TIME_ELAPSED), after 60 frames of warm-up, median and worst of 60\n" );
	GLuint query = 0;
	glGenQueries( 1, &query );
	for( const Case& c : cases )
		for( const Size& size : sizes )
		{
			Rig rig( c.over );
			if( !rig.Init( size.w, size.h ) )
				return 1;
			std::string error;
			applySetting( rig.plugin, std::string( "Target=" ) + c.target, error );
			rig.Render( 60 );
			std::vector< double > ms;
			for( int i = 0; i < 60; ++i )
			{
				glBeginQuery( GL_TIME_ELAPSED, query );
				rig.Render( 1 );
				glEndQuery( GL_TIME_ELAPSED );
				GLuint64 ns = 0;
				glGetQueryObjectui64v( query, GL_QUERY_RESULT, &ns );
				ms.push_back( ns * 1e-6 );
			}
			std::sort( ms.begin(), ms.end() );
			std::printf( "  %-30s %-6s median %6.2f ms, worst %6.2f ms\n", c.what, size.name, ms[ ms.size() / 2 ], ms.back() );
		}
	glDeleteQueries( 1, &query );
	return 0;
}
