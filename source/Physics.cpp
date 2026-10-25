#include "Physics.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

namespace sidereal::physics
{
namespace
{
int g_hooks = 0;

/// 2 h c^2 with lambda in um: W um^4 m^-2 sr^-1.
constexpr double kC1 = 1.191042972e8;
/// h c / k, um K.
constexpr double kC2 = 14387.7688;
/// k in eV / K.
constexpr double kBoltzmannEv = 8.617333262e-5;
/// Stefan-Boltzmann over pi in the units B_lambda integrates to: sigma T^4 / pi.
constexpr double kSigmaOverPi = 5.670374419e-8 / kPi;

double erfc_( double x )
{
	return std::erfc( x );
}

/// The fraction of a Gaussian line inside [ lo, hi ].
double gaussianInside( double centre, double sigma, double lo, double hi )
{
	const double s = sigma * std::sqrt( 2.0 );
	return 0.5 * ( erfc_( ( lo - centre ) / s ) - erfc_( ( hi - centre ) / s ) );
}

template< typename F >
double simpson( F f, double a, double b, int panels )
{
	if( panels % 2 )
		++panels;
	const double h = ( b - a ) / panels;
	double sum     = f( a ) + f( b );
	for( int i = 1; i < panels; ++i )
		sum += f( a + i * h ) * ( i % 2 ? 4.0 : 2.0 );
	return sum * h / 3.0;
}

struct LineSpec
{
	double lambda, strength, sigma;
};

//---------------------------------------------------------------------------
// The components' lines. Recombination ratios are case B at 10^4 K, Hbeta = 1
// (Osterbrock & Ferland, Tables 4.2 and 4.4); the rest are typical of their
// zones in Galactic H II regions, shocks and photodissociation regions, at the
// level that decides which filter a component lights and roughly how much.
//---------------------------------------------------------------------------
const std::vector< LineSpec > kOxygen = {
	{ 0.50068, 1.0, 0 }, { 0.49589, 0.335, 0 }, { 0.46857, 0.10, 0 }, { 0.38688, 0.25, 0 },
};
const std::vector< LineSpec > kHydrogen = {
	{ 0.48613, 1.0, 0 },   { 0.65628, 2.86, 0 },  { 0.43405, 0.468, 0 }, { 0.41017, 0.259, 0 },
	{ 0.65835, 0.90, 0 },  { 0.65480, 0.30, 0 },  { 0.95311, 0.60, 0 },  { 0.90686, 0.25, 0 },
	{ 1.08300, 0.25, 0 },  { 1.09381, 0.090, 0 }, { 1.28181, 0.162, 0 }, { 1.87510, 0.332, 0 },
	{ 2.16553, 0.0275, 0 },{ 2.62515, 0.045, 0 }, { 4.05122, 0.078, 0 }, { 3.73951, 0.026, 0 },
	{ 6.98527, 0.10, 0 },  { 12.8135, 0.40, 0 },  { 15.5551, 0.15, 0 },
	// The Brackett series' high members, which F162M sits among.
	{ 1.73669, 0.0117, 0 },{ 1.68111, 0.0095, 0 },{ 1.64117, 0.0076, 0 },{ 1.61137, 0.0062, 0 },
};
const std::vector< LineSpec > kSulphur = {
	{ 0.67164, 1.0, 0 }, { 0.67308, 0.75, 0 }, { 0.63003, 0.45, 0 }, { 0.63639, 0.15, 0 }, { 1.64400, 0.05, 0 },
};
const std::vector< LineSpec > kShock = {
	{ 0.65628, 1.0, 0 },  { 0.48613, 0.32, 0 }, { 0.65835, 1.10, 0 }, { 0.65480, 0.37, 0 }, { 0.67164, 0.35, 0 },
	{ 0.67308, 0.27, 0 }, { 0.63003, 0.35, 0 }, { 0.50068, 0.60, 0 }, { 0.49589, 0.20, 0 }, { 0.95311, 0.30, 0 },
	{ 1.25668, 0.12, 0 }, { 1.64400, 0.10, 0 }, { 1.28181, 0.06, 0 }, { 1.87510, 0.11, 0 }, { 4.05122, 0.03, 0 },
	{ 5.34017, 0.05, 0 }, { 6.98527, 0.10, 0 }, { 12.8135, 0.30, 0 }, { 17.9359, 0.10, 0 }, { 18.7130, 0.20, 0 },
};
const std::vector< LineSpec > kMolecular = {
	{ 2.12183, 1.0, 0 },  { 4.69468, 0.70, 0 }, { 1.95756, 0.80, 0 }, { 2.03376, 0.40, 0 }, { 4.18096, 0.30, 0 },
	{ 17.0348, 0.20, 0 }, { 12.2786, 0.20, 0 }, { 9.66491, 0.30, 0 }, { 0.67164, 0.25, 0 }, { 0.67308, 0.20, 0 },
	{ 0.65628, 0.15, 0 }, { 1.64400, 0.40, 0 },
};
const std::vector< LineSpec > kMeteorSlow = {
	{ 0.58900, 1.0, 0 }, { 0.58959, 0.50, 0 }, { 0.43838, 0.35, 0 }, { 0.52704, 0.30, 0 },
	{ 0.44047, 0.20, 0 }, { 0.51837, 0.20, 0 }, { 0.51727, 0.15, 0 }, { 0.42267, 0.10, 0 },
};
const std::vector< LineSpec > kMeteorFast = {
	{ 0.51837, 1.0, 0 }, { 0.51727, 0.60, 0 }, { 0.51674, 0.30, 0 }, { 0.58900, 0.40, 0 }, { 0.39337, 0.50, 0 },
	{ 0.39685, 0.40, 0 }, { 0.77740, 0.80, 0 }, { 0.86800, 0.30, 0 }, { 0.43838, 0.20, 0 },
};
const std::vector< LineSpec > kTrain = {
	{ 0.55774, 1.0, 0 }, { 0.58900, 0.30, 0 }, { 0.58959, 0.15, 0 },
};
/// The aromatic bands, as Gaussians (centre, share, sigma): Draine & Li's
/// profiles reduced to their widths.
const std::vector< LineSpec > kPah = {
	{ 3.29, 0.08, 0.017 }, { 3.40, 0.03, 0.10 }, { 6.22, 0.35, 0.08 }, { 7.60, 0.60, 0.20 }, { 7.85, 0.40, 0.20 },
	{ 8.61, 0.30, 0.10 },  { 11.23, 0.45, 0.06 },{ 11.33, 0.25, 0.10 },{ 12.70, 0.25, 0.15 },
	// The hot continuum of the same small grains, stochastically heated:
	// what makes Webb's 4.4 um filter see the photodissociation edges.
	{ 4.40, 0.10, 0.60 },
};

const std::vector< LineSpec >& specOf( Component c )
{
	switch( c )
	{
	case C_OXYGEN: return kOxygen;
	case C_HYDROGEN: return kHydrogen;
	case C_SULPHUR: return kSulphur;
	case C_SHOCK: return kShock;
	case C_MOLECULAR: return kMolecular;
	case C_METEOR_SLOW: return kMeteorSlow;
	case C_METEOR_FAST: return kMeteorFast;
	case C_TRAIN: return kTrain;
	case C_PAH: return kPah;
	default: break;
	}
	static const std::vector< LineSpec > none;
	return none;
}

//---------------------------------------------------------------------------
// The filters, from STScI's instrument handbooks: pivot and full width.
// "OSC" is a one-shot-colour camera's Bayer dyes with an astro-modified
// sensor (no IR-cut over H-alpha), which is what a backyard picture of a
// nebula is taken with.
//---------------------------------------------------------------------------
const Filter kFilters[] = {
	// Hubble WFC3/UVIS and WFPC2 narrow bands
	{ "F502N", 0.5010, 0.0065 },
	{ "F657N", 0.6567, 0.0121 },
	{ "F673N", 0.6766, 0.0118 },
	// Hubble ACS/WFC broad bands and H-alpha
	{ "F435W", 0.4329, 0.0870 },
	{ "F555W", 0.5361, 0.1000 },
	{ "F606W", 0.5922, 0.1600 },
	{ "F775W", 0.7693, 0.1400 },
	{ "F814W", 0.8046, 0.1500 },
	{ "F850LP", 0.9055, 0.1300 },
	{ "F658N", 0.6584, 0.0075 },
	// Hubble WFC3/IR
	{ "F110W", 1.1534, 0.4430 },
	{ "F160W", 1.5369, 0.2683 },
	// Webb NIRCam
	{ "F090W", 0.9022, 0.1940 },
	{ "F150W", 1.5007, 0.3180 },
	{ "F162M", 1.6272, 0.1680 },
	{ "F187N", 1.8738, 0.0240 },
	{ "F200W", 1.9886, 0.4610 },
	{ "F212N", 2.1210, 0.0270 },
	{ "F277W", 2.7617, 0.6720 },
	{ "F300M", 2.9891, 0.3180 },
	{ "F335M", 3.3621, 0.3470 },
	{ "F356W", 3.5684, 0.7870 },
	{ "F405N", 4.0523, 0.0450 },
	{ "F444W", 4.4393, 1.0240 },
	{ "F470N", 4.7083, 0.0510 },
	// Webb MIRI
	{ "F770W", 7.639, 2.20 },
	{ "F1130W", 11.309, 0.73 },
	{ "F1500W", 15.065, 2.92 },
	// A backyard camera
	{ "OSC-B", 0.465, 0.090 },
	{ "OSC-G", 0.530, 0.090 },
	{ "OSC-R", 0.620, 0.110 },
};

// The hues NASA's colour keys name, as RGB.
#define PURPLE { 0.55f, 0.15f, 1.00f }
#define BLUE { 0.05f, 0.25f, 1.00f }
#define LIGHTBLUE { 0.35f, 0.65f, 1.00f }
#define CYAN { 0.00f, 0.85f, 1.00f }
#define BLUEGREEN { 0.00f, 0.90f, 0.60f }
#define GREEN { 0.15f, 1.00f, 0.10f }
#define YELLOW { 1.00f, 0.90f, 0.00f }
#define ORANGE { 1.00f, 0.50f, 0.00f }
#define RED { 1.00f, 0.08f, 0.02f }

const Instrument kInstrumentTable[ kInstruments ] = {
	{ "Hubble Narrow", 3, { "F502N", "F657N", "F673N" }, { BLUE, GREEN, RED }, 2.4, Spikes::Four, 45.0, 0.0, 0.0, 0.0 },
	{ "Hubble Colour", 4, { "F435W", "F555W", "F814W", "F658N" }, { BLUE, GREEN, RED, RED }, 2.4, Spikes::Four, 45.0, 0.0, 0.0, 1.0 },
	{ "Hubble Infrared", 2, { "F110W", "F160W" }, { BLUE, YELLOW }, 2.4, Spikes::Four, 45.0, 0.0, 0.0, 1.0 },
	{ "Webb NIRCam", 6, { "F090W", "F187N", "F200W", "F335M", "F444W", "F470N" }, { PURPLE, BLUE, CYAN, YELLOW, ORANGE, RED }, 6.5,
	  Spikes::Webb, 0.0, 0.0, 1.0, 0.7 },
	{ "Webb MIRI", 3, { "F770W", "F1130W", "F1500W" }, { BLUE, GREEN, RED }, 6.5, Spikes::Webb, 0.0, 0.0, 1.0, 0.0 },
	{ "Webb Deep", 6, { "F090W", "F150W", "F200W", "F277W", "F356W", "F444W" }, { BLUE, BLUE, GREEN, GREEN, ORANGE, RED }, 6.5,
	  Spikes::Webb, 0.0, 0.0, 1.0, 1.0 },
	{ "Backyard", 3, { "OSC-B", "OSC-G", "OSC-R" }, { BLUE, GREEN, RED }, 2.0, Spikes::None, 0.0, 2.6, 0.0, 1.0 },
	{ "Cosmic Cliffs", 6, { "F090W", "F187N", "F200W", "F470N", "F335M", "F444W" }, { BLUE, BLUEGREEN, GREEN, YELLOW, ORANGE, RED },
	  6.5, Spikes::Webb, 0.0, 0.0, 1.0, 0.7 },
	{ "Southern Ring", 6, { "F090W", "F187N", "F212N", "F356W", "F405N", "F470N" }, { BLUE, CYAN, GREEN, YELLOW, RED, RED }, 6.5,
	  Spikes::Webb, 0.0, 0.0, 1.0, 0.7 },
	{ "Ring NIRCam", 4, { "F162M", "F212N", "F300M", "F335M" }, { BLUE, CYAN, GREEN, RED }, 6.5, Spikes::Webb, 0.0, 0.0, 1.0, 0.5 },
	{ "Hubble Deep", 4, { "F435W", "F606W", "F775W", "F850LP" }, { BLUE, GREEN, ORANGE, RED }, 2.4, Spikes::Four, 45.0, 0.0, 0.0, 1.0 },
	{ "Hubble HOO", 2, { "F502N", "F658N" }, { CYAN, RED }, 2.4, Spikes::Four, 45.0, 0.0, 0.0, 0.0 },
};

double lineResponseOf( const Filter& f, Component c )
{
	const double lo = f.pivot - 0.5 * f.width, hi = f.pivot + 0.5 * f.width;
	if( c == C_SYNCHROTRON )
	{
		// F_lambda = lambda^( alpha - 2 ), alpha = 0.8, normalised at 1 um:
		// integral of lambda^-1.2 over the band.
		const double p = -0.2;
		return ( std::pow( hi, p ) - std::pow( lo, p ) ) / p;
	}
	double sum = 0.0;
	for( const LineSpec& l : specOf( c ) )
	{
		if( l.sigma > 0.0 )
			sum += l.strength * gaussianInside( l.lambda, l.sigma, lo, hi );
		else if( l.lambda >= lo && l.lambda <= hi )
			sum += l.strength;
	}
	return sum;
}

double continuumOf( const Filter& f, int kind, double kelvin )
{
	const double lo = f.pivot - 0.5 * f.width, hi = f.pivot + 0.5 * f.width;
	if( kind == 2 )
	{
		// lambda^-2 B_lambda( T ) per unit power: the integral over all
		// wavelengths is C1 ( T / C2 )^6 Gamma( 6 ) zeta( 6 ).
		const double norm = kC1 * std::pow( kelvin / kC2, 6.0 ) * 120.0 * 1.0173430620;
		return simpson( [ & ]( double l ) { return Planck( l, kelvin ) / ( l * l ); }, lo, hi, 64 ) / norm;
	}
	const double norm = kSigmaOverPi * std::pow( kelvin, 4.0 );
	return simpson(
	           [ & ]( double l ) {
		           const double b = Planck( l, kelvin );
		           return kind == 1 ? b * ( 0.55 / l ) : b;
	           },
	           lo, hi, 64 )
	       / norm;
}

std::array< double, 3 > mixFilters( const Palette& p, const double signal[ kMaxFilters ], double tauV )
{
	std::array< double, 3 > out { 0.0, 0.0, 0.0 };
	for( int f = 0; f < p.filterCount; ++f )
	{
		const double s = p.gain[ f ] * signal[ f ] * std::exp( -tauV * p.ext[ f ] );
		for( int c = 0; c < 3; ++c )
			out[ c ] += s * p.colour[ f ][ c ];
	}
	return out;
}
} // namespace

void SetHooksForTest( int bits )
{
	g_hooks = bits;
}
int HooksForTest()
{
	return g_hooks;
}

//---------------------------------------------------------------------------
double Planck( double lambdaUm, double kelvin )
{
	if( lambdaUm <= 0.0 || kelvin <= 0.0 )
		return 0.0;
	const double x = kC2 / ( lambdaUm * kelvin );
	if( x > 700.0 )
		return 0.0;
	return kC1 / ( std::pow( lambdaUm, 5.0 ) * std::expm1( x ) );
}

double ReducedEnergy( double electronVolts, double kelvin )
{
	return electronVolts / ( kBoltzmannEv * kelvin );
}

double PhotonTail( double x0 )
{
	const int terms = ( g_hooks & PH_SOFT_STARS ) ? 1 : 200;
	double sum      = 0.0;
	for( int n = 1; n <= terms; ++n )
	{
		const double e    = std::exp( -n * x0 );
		const double term = e * ( x0 * x0 / n + 2.0 * x0 / ( double( n ) * n ) + 2.0 / ( double( n ) * n * n ) );
		sum += term;
		if( term < 1e-17 * sum )
			break;
	}
	return sum;
}

double OxygenHardness( double kelvin )
{
	const double hydrogen = PhotonTail( ReducedEnergy( 13.598, kelvin ) );
	const double oxygen   = PhotonTail( ReducedEnergy( 35.121, kelvin ) );
	return hydrogen > 0.0 ? oxygen / hydrogen : 0.0;
}

//---------------------------------------------------------------------------
double Extinction( double lambdaUm, double rv )
{
	if( g_hooks & PH_FLAT_EXTINCTION )
		return 1.0;
	const double x = 1.0 / std::max( lambdaUm, 0.125 );
	double a = 0.0, b = 0.0;
	if( x < 1.1 )
	{
		// Infrared, 0.3 <= x < 1.1 in CCM; the same power law continued past it.
		const double p = std::pow( x, 1.61 );
		a              = 0.574 * p;
		b              = -0.527 * p;
		double value   = a + b / rv;
		if( x < 0.3 )
		{
			// Past 3.33 um the curve flattens (Indebetouw et al. 2005, A_lambda
			// about 0.4 A_K from 4 to 8 um) and the silicates add their bumps
			// at 9.7 and 18 um. Zero at 3.33 um, so the joint is continuous.
			const double l = lambdaUm;
			value += 0.025 * ( 1.0 - std::exp( -( l - 1.0 / 0.3 ) / 1.2 ) );
			value += 0.055 * std::exp( -0.5 * std::pow( ( l - 9.7 ) / 0.9, 2.0 ) );
			value += 0.022 * std::exp( -0.5 * std::pow( ( l - 18.0 ) / 2.5, 2.0 ) );
		}
		return value;
	}
	if( x < 3.3 )
	{
		const double y = x - 1.82;
		a = 1.0 + y * ( 0.17699 + y * ( -0.50447 + y * ( -0.02427 + y * ( 0.72085 + y * ( 0.01979 + y * ( -0.77530 + y * 0.32999 ) ) ) ) ) );
		b = y * ( 1.41338 + y * ( 2.28305 + y * ( 1.07233 + y * ( -5.38434 + y * ( -0.62251 + y * ( 5.30260 + y * -2.09002 ) ) ) ) ) );
		return a + b / rv;
	}
	double fa = 0.0, fb = 0.0;
	if( x >= 5.9 )
	{
		const double d = x - 5.9;
		fa             = -0.04473 * d * d - 0.009779 * d * d * d;
		fb             = 0.2130 * d * d + 0.1207 * d * d * d;
	}
	a = 1.752 - 0.316 * x - 0.104 / ( ( x - 4.67 ) * ( x - 4.67 ) + 0.341 ) + fa;
	b = -3.090 + 1.825 * x + 1.206 / ( ( x - 4.62 ) * ( x - 4.62 ) + 0.263 ) + fb;
	return a + b / rv;
}

double DustTemperature( double field )
{
	return kRefDustTemperature * std::pow( std::max( field, 1e-9 ), 1.0 / ( 4.0 + kDustBeta ) );
}

//---------------------------------------------------------------------------
const std::vector< Line >& LinesOf( Component component )
{
	static std::vector< Line > tables[ kLineComponents ];
	static std::once_flag once;
	std::call_once( once, [] {
		for( int c = 0; c < kLineComponents; ++c )
			for( const LineSpec& s : specOf( static_cast< Component >( c ) ) )
				tables[ c ].push_back( Line { s.lambda, s.strength } );
	} );
	return tables[ std::clamp( static_cast< int >( component ), 0, kLineComponents - 1 ) ];
}

const Filter& FilterNamed( const char* name )
{
	for( const Filter& f : kFilters )
		if( std::strcmp( f.name, name ) == 0 )
			return f;
	return kFilters[ 0 ];
}

const Instrument& InstrumentOf( int id )
{
	return kInstrumentTable[ std::clamp( id, 0, static_cast< int >( kInstruments ) - 1 ) ];
}

double LineResponse( const Filter& filter, Component component )
{
	return lineResponseOf( filter, component );
}

double ContinuumResponse( const Filter& filter, int kind, double kelvin )
{
	return continuumOf( filter, kind, kelvin );
}

double GalaxyResponse( const Filter& f, double kelvin, double z )
{
	const double lo = f.pivot - 0.5 * f.width, hi = f.pivot + 0.5 * f.width;
	const double s  = 1.0 + z;
	// The Lyman-alpha forest's effective depth (Madau 1995, reduced): what is
	// left of the light between Lyman-alpha and the Lyman limit.
	const double forest = std::exp( -0.0036 * std::pow( s, 3.46 ) );
	const double norm   = kSigmaOverPi * std::pow( kelvin, 4.0 );
	const double flux   = simpson(
        [ & ]( double l ) {
            const double rest = l / s;
            if( rest < 0.0912 )
                return 0.0;
            const double b = Planck( rest, kelvin );
            return rest < 0.1216 ? b * forest : b;
        },
        lo, hi, 32 );
	return flux / norm / std::pow( s, 5.0 );
}

//---------------------------------------------------------------------------
double TempOfSample( int kind, int index )
{
	const double lo = kind == 2 ? kDustTempMin : kStarTempMin;
	const double hi = kind == 2 ? kDustTempMax : kStarTempMax;
	return lo * std::pow( hi / lo, static_cast< double >( index ) / ( kTempSamples - 1 ) );
}

double SampleOfTemp( int kind, double kelvin )
{
	const double lo = kind == 2 ? kDustTempMin : kStarTempMin;
	const double hi = kind == 2 ? kDustTempMax : kStarTempMax;
	return std::log( std::clamp( kelvin, lo, hi ) / lo ) / std::log( hi / lo ) * ( kTempSamples - 1 );
}

std::array< double, 3 > DisplayOf( const Palette& p, Component component, double tauV )
{
	double signal[ kMaxFilters ] = {};
	const Instrument& inst       = InstrumentOf( p.instrument );
	for( int f = 0; f < p.filterCount; ++f )
		signal[ f ] = lineResponseOf( FilterNamed( inst.filters[ f ] ), component );
	return mixFilters( p, signal, tauV );
}

std::array< double, 3 > DisplayOfContinuum( const Palette& p, int kind, double kelvin, double tauV )
{
	double signal[ kMaxFilters ] = {};
	const Instrument& inst       = InstrumentOf( p.instrument );
	for( int f = 0; f < p.filterCount; ++f )
		signal[ f ] = continuumOf( FilterNamed( inst.filters[ f ] ), kind, kelvin );
	return mixFilters( p, signal, tauV );
}

Mix NebulaMix()
{
	//      O     H     S     shock mol   slow  fast  train PAH   sync
	// The continuum floor: a field's stars and the nebula's scattered and
	// recombination continuum, enough that a filter with no lines in it is
	// stretched to its starlight rather than to nothing.
	return Mix { { 0.6, 1.0, 0.35, 0.0, 0.02, 0.0, 0.0, 0.0, 0.4, 0.0 }, 40.0, 7000.0, 4.0, 90.0 };
}
Mix PlanetaryMix()
{
	return Mix { { 2.0, 1.0, 0.15, 0.0, 0.3, 0.0, 0.0, 0.0, 0.2, 0.0 }, 3.0, 7000.0, 1.0, 120.0 };
}
Mix RemnantMix()
{
	return Mix { { 0.4, 0.2, 0.05, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 2.0 }, 30.0, 7000.0, 1.0, 90.0 };
}
Mix StarlightMix()
{
	return Mix { { 0.05, 0.2, 0.05, 0.0, 0.0, 0.0, 0.0, 0.0, 0.05, 0.0 }, 400.0, 6000.0, 1.0, 60.0 };
}

Palette BuildPalette( int id, const Mix& mix )
{
	Palette p;
	p.instrument           = id;
	const Instrument& inst = InstrumentOf( id );
	p.filterCount          = inst.filterCount;

	double reference[ kMaxFilters ] = {};
	double mean                     = 0.0;
	for( int f = 0; f < p.filterCount; ++f )
	{
		const Filter& filter = FilterNamed( inst.filters[ f ] );
		p.pivot[ f ]         = filter.pivot;
		p.ext[ f ]           = Extinction( filter.pivot );
		double r             = 0.0;
		for( int c = 0; c < kLineComponents; ++c )
			r += mix.line[ c ] * lineResponseOf( filter, static_cast< Component >( c ) );
		r += mix.star * continuumOf( filter, 0, mix.starTemp );
		r += mix.dust * inst.dustGlow * continuumOf( filter, 2, mix.dustTemp );
		reference[ f ] = std::max( r, 1e-12 );
		mean += reference[ f ] / p.filterCount;
	}
	double sum[ 3 ] = { 0.0, 0.0, 0.0 };
	for( int f = 0; f < p.filterCount; ++f )
	{
		p.gain[ f ] = ( g_hooks & PH_GREY_PALETTE ) ? 1.0 / mean : 1.0 / reference[ f ];
		for( int c = 0; c < 3; ++c )
			sum[ c ] += inst.colour[ f ][ c ];
	}
	for( int f = 0; f < p.filterCount; ++f )
		for( int c = 0; c < 3; ++c )
			p.colour[ f ][ c ] = sum[ c ] > 0.0 ? inst.colour[ f ][ c ] / sum[ c ] : 0.0;
	if( inst.whiteBalance > 0.0 )
	{
		// Make a G star white, as broad-band pictures are balanced; a filter
		// that sees only starlight otherwise turns every star its hue.
		const auto star = DisplayOfContinuum( p, 0, 5800.0, 0.0 );
		const double mean = ( star[ 0 ] + star[ 1 ] + star[ 2 ] ) / 3.0;
		for( int c = 0; c < 3; ++c )
		{
			const double k = star[ c ] > 1e-30 ? std::pow( mean / star[ c ], inst.whiteBalance ) : 1.0;
			for( int f = 0; f < p.filterCount; ++f )
				p.colour[ f ][ c ] *= k;
		}
	}

	const auto tauOf = []( int j ) { return kTauMax * j / ( kTauSamples - 1 ); };

	p.lines.assign( static_cast< size_t >( kLineComponents ) * kTauSamples * 3, 0.0f );
	for( int c = 0; c < kLineComponents; ++c )
		for( int j = 0; j < kTauSamples; ++j )
		{
			const auto rgb = DisplayOf( p, static_cast< Component >( c ), tauOf( j ) );
			for( int k = 0; k < 3; ++k )
				p.lines[ ( static_cast< size_t >( c ) * kTauSamples + j ) * 3 + k ] = static_cast< float >( rgb[ k ] );
		}

	p.continua.assign( static_cast< size_t >( 3 ) * kTempSamples * kTauSamples * 3, 0.0f );
	for( int kind = 0; kind < 3; ++kind )
		for( int i = 0; i < kTempSamples; ++i )
		{
			const double t = TempOfSample( kind, i );
			double signal[ kMaxFilters ] = {};
			for( int f = 0; f < p.filterCount; ++f )
				signal[ f ] = continuumOf( FilterNamed( inst.filters[ f ] ), kind, t );
			for( int j = 0; j < kTauSamples; ++j )
			{
				const auto rgb = mixFilters( p, signal, tauOf( j ) );
				for( int k = 0; k < 3; ++k )
					p.continua[ ( ( static_cast< size_t >( kind ) * kTempSamples + i ) * kTauSamples + j ) * 3 + k ] = static_cast< float >( rgb[ k ] );
			}
		}

	p.galaxies.assign( static_cast< size_t >( kTempSamples ) * 32 * 3, 0.0f );
	for( int i = 0; i < kTempSamples; ++i )
		for( int k = 0; k < 32; ++k )
		{
			const double t = TempOfSample( 0, i ), z = 8.0 * k / 31.0;
			double signal[ kMaxFilters ] = {};
			for( int f = 0; f < p.filterCount; ++f )
				signal[ f ] = GalaxyResponse( FilterNamed( inst.filters[ f ] ), t, z );
			const auto rgb = mixFilters( p, signal, 0.0 );
			for( int c = 0; c < 3; ++c )
				p.galaxies[ ( static_cast< size_t >( i ) * 32 + k ) * 3 + c ] = static_cast< float >( rgb[ c ] );
		}
	return p;
}

//---------------------------------------------------------------------------
double AngularDiameterDistance( double z )
{
	constexpr double hubbleDistance = 299792.458 / 70.0;
	if( g_hooks & PH_LINEAR_REDSHIFT )
		return hubbleDistance * z / ( 1.0 + z );
	const double comoving = simpson( []( double s ) { return 1.0 / std::sqrt( 0.3 * std::pow( 1.0 + s, 3.0 ) + 0.7 ); }, 0.0, z, 400 );
	return hubbleDistance * comoving / ( 1.0 + z );
}

//---------------------------------------------------------------------------
double MeteorLight( double x )
{
	if( x <= 0.0 || x >= 3.0 )
		return 0.0;
	const double r = 1.0 - x / 3.0;
	return 2.25 * x * r * r;
}

//---------------------------------------------------------------------------
namespace
{
constexpr double kTableStep = 0.05;
constexpr int kTableDays    = 400;

struct LightTable
{
	std::vector< double > light;///< unnormalised, the same units as DecayHeating
	double peakDay = 0.0, peak = 1.0;
	int hooks      = -1;
};

const LightTable& lightTable()
{
	static LightTable table;
	static std::mutex lock;
	std::lock_guard< std::mutex > guard( lock );
	if( table.hooks == g_hooks )
		return table;
	table.hooks   = g_hooks;
	const int n   = static_cast< int >( kTableDays / kTableStep ) + 1;
	const double tm2 = kDiffusionTime * kDiffusionTime;
	table.light.assign( static_cast< size_t >( n ), 0.0 );
	// I( t ) = int_0^t z e^{( z^2 - t^2 ) / tm^2} Q( z ) dz, stepped so the
	// exponent is never positive: I( t + h ) = I( t ) e^{-( ( t + h )^2 - t^2 ) / tm^2}
	// plus the new panel (trapezoid).
	double integral = 0.0;
	for( int i = 1; i < n; ++i )
	{
		const double t0 = ( i - 1 ) * kTableStep, t1 = i * kTableStep;
		const double decay = std::exp( -( t1 * t1 - t0 * t0 ) / tm2 );
		integral = integral * decay + 0.5 * kTableStep * ( t0 * DecayHeating( t0 ) * decay + t1 * DecayHeating( t1 ) );
		table.light[ static_cast< size_t >( i ) ] = 2.0 / tm2 * integral;
	}
	table.peak    = 0.0;
	table.peakDay = 0.0;
	for( int i = 0; i < n; ++i )
		if( table.light[ static_cast< size_t >( i ) ] > table.peak )
		{
			table.peak    = table.light[ static_cast< size_t >( i ) ];
			table.peakDay = i * kTableStep;
		}
	return table;
}
} // namespace

double DecayHeating( double days )
{
	const double t = std::max( days, 0.0 );
	// Gamma rays deposited: all of them early, ( T0 / t )^2 of them late.
	const double deposited = ( g_hooks & PH_NO_ESCAPE ) || t <= 0.0 ? 1.0 : 1.0 - std::exp( -( kGammaTrap / t ) * ( kGammaTrap / t ) );
	// Nadyozhin 1994, per solar mass of nickel, in 1e43 erg/s: 6.45 e^{-t/8.8} + 1.45 e^{-t/111.3}.
	// Cobalt's positrons (3.5% of its energy) stay whatever the gammas do.
	const double nickel = 6.45 * std::exp( -t / kNickelLife ) * deposited;
	const double cobalt = 1.45 * std::exp( -t / kCobaltLife ) * ( 0.965 * deposited + 0.035 );
	return ( nickel + cobalt ) / 7.90;
}

double SupernovaLight( double days )
{
	if( days <= 0.0 )
		return 0.0;
	const LightTable& table = lightTable();
	const double index      = days / kTableStep;
	const int n             = static_cast< int >( table.light.size() );
	if( index >= n - 1 )
		return table.light.back() * DecayHeating( days ) / DecayHeating( kTableDays ) / table.peak;
	const int i     = static_cast< int >( index );
	const double f  = index - i;
	const double v  = table.light[ static_cast< size_t >( i ) ] * ( 1.0 - f ) + table.light[ static_cast< size_t >( i + 1 ) ] * f;
	return v / table.peak;
}

double SupernovaPeakDay()
{
	return lightTable().peakDay;
}

double SupernovaTemperature( double days )
{
	const double peak = SupernovaPeakDay();
	return 4500.0 + 7000.0 * std::exp( -std::max( days - 0.6 * peak, 0.0 ) / 25.0 );
}

double RemnantRadius( double age )
{
	if( age <= 0.0 )
		return 0.0;
	return age < 1.0 ? age : std::pow( age, 0.4 );
}

double EchoRadius( double ct, double z )
{
	if( ct <= 0.0 )
		return 0.0;
	return std::sqrt( 2.0 * z * ct + ct * ct );
}

} // namespace sidereal::physics
