#pragma once

#include <array>
#include <vector>

/**
    The physics the picture is made of, on the CPU, in double.

    Nothing here touches GL. The shaders never see a wavelength: they see
    tables built from these functions -- what each kind of light looks like
    through each telescope's filters, after a given amount of dust -- so the
    one place a spectrum meets a filter is here, and the harness can check it
    without a GPU (`sdtest --offline`).

    Units: wavelengths in micrometres, temperatures in kelvin, time in days
    (supernova) or seconds (meteors), distances in megaparsecs (cosmology).
*/
namespace sidereal::physics
{
constexpr double kPi = 3.14159265358979323846;

//---------------------------------------------------------------------------
// Blackbodies.
//---------------------------------------------------------------------------

/// Planck's B_lambda, W m^-2 sr^-1 um^-1, lambda in um.
double Planck( double lambdaUm, double kelvin );

/// Of a blackbody's hydrogen-ionising photons (above 13.6 eV), the fraction
/// also above 35.1 eV -- the energy that makes O++ ([O III]). Photon counts,
/// from the closed-form series for the tail of x^2 / ( e^x - 1 ).
double OxygenHardness( double kelvin );

/// The photon-number integral of x^2 / ( e^x - 1 ) from x0 to infinity, by
/// its series sum_n e^{-n x0} ( x0^2/n + 2 x0/n^2 + 2/n^3 ).
double PhotonTail( double x0 );

/// h nu / k T for a photon energy in eV.
double ReducedEnergy( double electronVolts, double kelvin );

//---------------------------------------------------------------------------
// Dust.
//---------------------------------------------------------------------------

/// A_lambda / A_V for diffuse Milky Way dust (R_V = 3.1): Cardelli, Clayton
/// & Mathis 1989 from 0.125 to 3.33 um; beyond it, the infrared power law
/// continued with the 9.7 and 18 um silicate features added (AGENTS.md says
/// where those numbers come from and that they are this repo's, not CCM's).
double Extinction( double lambdaUm, double rv = 3.1 );

/// Dust temperature from the local starlight, in units of the radiation
/// field that makes kRefDustTemperature: grains in equilibrium radiate what
/// they absorb, and a modified blackbody of emissivity index beta radiates
/// as T^( 4 + beta ), so T = T_ref u^( 1 / ( 4 + beta ) ).
constexpr double kDustBeta           = 2.0;
constexpr double kRefDustTemperature = 90.0;
double DustTemperature( double field );

//---------------------------------------------------------------------------
// What the gas and the stars emit. Each component is a spectrum; a filter's
// response to it is that spectrum integrated over the filter's band.
//---------------------------------------------------------------------------
enum Component : int
{
	// Emission lines, each a fixed set of lines in fixed ratio.
	C_OXYGEN = 0,///< [O III] 4959/5007 with He II 4686 and [Ne III] 3869: the high-ionisation zone
	C_HYDROGEN,  ///< case B recombination (Balmer, Paschen, Brackett), [N II] and [S III]
	C_SULPHUR,   ///< [S II] 6716/6731 and [O I] 6300: the partly ionised zone at the front
	C_SHOCK,     ///< a radiative shock's lines (a supernova remnant): strong [S II], [N II], [Fe II]
	C_MOLECULAR, ///< H2 2.12 and 4.69 um, with a Herbig-Haro object's [S II] and H-alpha
	C_METEOR_SLOW,///< a slow meteor's head: Na and Fe
	C_METEOR_FAST,///< a fast one's: Mg, Ca+, O I
	C_TRAIN,    ///< a meteor's persistent train: [O I] 5577 and Na
	C_PAH,       ///< the aromatic bands, 3.3 to 12.7 um
	C_SYNCHROTRON,///< F_nu ~ nu^-0.8
	kLineComponents,
};

/// One line: wavelength (um) and strength in its component's own units.
struct Line
{
	double lambda;
	double strength;
};
const std::vector< Line >& LinesOf( Component component );

//---------------------------------------------------------------------------
// Filters and telescopes.
//---------------------------------------------------------------------------
struct Filter
{
	const char* name;
	double pivot;///< um
	double width;///< um, full width (a top hat of this width at the pivot)
};

/// Every filter any telescope here uses, from STScI's published tables.
const Filter& FilterNamed( const char* name );

enum class Spikes
{
	None,  ///< a refractor
	Four,  ///< Hubble's four-vane spider: four spikes
	Webb,  ///< six from the hexagonal segments, two fainter from the vertical strut
};

constexpr int kMaxFilters = 8;

struct Instrument
{
	const char* name;
	int filterCount;
	const char* filters[ kMaxFilters ];
	float colour[ kMaxFilters ][ 3 ];///< the hue each filter's grey image is given
	double aperture;   ///< m: sets the diffraction core and spikes against wavelength
	Spikes spikes;
	double spikeAngle; ///< degrees, the pattern's rotation on the sky
	double seeing;     ///< FWHM in reference pixels added in quadrature (ground: the air)
	double dustGlow;   ///< 1 if the bands see dust's own thermal light
	double whiteBalance;///< 0: the hues as keyed (narrow-band stars go magenta, as they do); 1: a G star made white
};

/// The telescopes the Telescope control offers, then the per-release sets
/// only "As Released" reaches. Indices are stable: Scene.cpp names them.
enum InstrumentId : int
{
	I_HUBBLE_NARROW = 0,///< WFC3/UVIS F502N F657N F673N (Pillars 2014; WFPC2's 1995 set is the same lines)
	I_HUBBLE_COLOUR,    ///< ACS F435W F555W F814W + F658N (Hubble Heritage galaxies)
	I_HUBBLE_INFRARED,  ///< WFC3/IR F110W F160W (Horsehead 2013, Pillars IR 2014)
	I_WEBB_NIRCAM,      ///< F090W F187N F200W F335M F444W F470N (Pillars 2022)
	I_WEBB_MIRI,        ///< F770W F1130W F1500W (Pillars MIRI 2022)
	I_WEBB_DEEP,        ///< F090W+F150W, F200W+F277W, F356W, F444W (SMACS 0723)
	I_BACKYARD,         ///< a one-shot colour camera on a small refractor
	kTelescopeChoices,  ///< the Telescope control lists the ones above
	I_CLIFFS = kTelescopeChoices,///< Cosmic Cliffs' own colour key
	I_SOUTHERN_RING,    ///< Southern Ring NIRCam 2022
	I_RING_NIRCAM,      ///< Ring Nebula NIRCam 2023
	I_HUBBLE_DEEP,      ///< ACS F435W F606W F775W F850LP (Ultra Deep Field)
	I_HELIX,            ///< F502N blue-green, F658N red (Helix 2004, Ring 1998)
	kInstruments
};
const Instrument& InstrumentOf( int id );

/// A top-hat filter's response to a component's lines: the sum of the lines
/// inside the band (a narrow-band filter passes the whole line).
double LineResponse( const Filter& filter, Component component );

/// A continuum's response: F_lambda integrated over the band, by Simpson's
/// rule on 64 panels. `kind` 0: a blackbody at T; 1: the same scattered by
/// dust (times lambda^-1); 2: thermal dust, a modified blackbody at T
/// normalised by T^( 4 + beta ), so the response is per unit power radiated.
double ContinuumResponse( const Filter& filter, int kind, double kelvin );

/// A redshifted galaxy's starlight through a filter: a blackbody at T with
/// the Lyman break (nothing below 0.1216 um in the rest frame), seen at z,
/// with cosmological surface-brightness dimming ( 1 + z )^-4.
double GalaxyResponse( const Filter& filter, double kelvin, double z );

//---------------------------------------------------------------------------
// The tables the shaders read. Rows of display RGB, per instrument.
//
// Each filter's grey image is normalised to its own range -- as the people
// who make these pictures do -- by a gain that brings a reference nebula's
// signal in that filter to one; then given its hue; then the hues are
// balanced so that a signal equal in every filter is white.
//---------------------------------------------------------------------------
constexpr int kTauSamples = 48;       ///< extinction, tau_V from 0 to kTauMax
constexpr double kTauMax  = 12.0;
constexpr int kTempSamples = 64;      ///< temperature, log-spaced
constexpr double kStarTempMin = 2500.0, kStarTempMax = 50000.0;
constexpr double kDustTempMin = 15.0, kDustTempMax = 600.0;

/// What a filter's gain is set by: the target's own light, as a mix of
/// components. Each filter's grey image is stretched to what THIS target puts
/// in it, as the people who made each release did.
struct Mix
{
	double line[ kLineComponents ];
	double star;    ///< the continuum floor: stars, scattered and recombination continuum
	double starTemp;
	double dust;    ///< thermal dust, where the telescope sees it
	double dustTemp;
};
/// A bright H II region with a photodissociation edge, warm dust and stars.
Mix NebulaMix();
/// A planetary nebula: strong [O III], little dust, few stars.
Mix PlanetaryMix();
/// A supernova remnant: shocked gas and synchrotron.
Mix RemnantMix();
/// Starlight: galaxies, deep fields, reflection nebulae, star fields.
Mix StarlightMix();

struct Palette
{
	int instrument = -1;
	int filterCount = 0;
	double gain[ kMaxFilters ] {};       ///< 1 / the reference nebula's signal
	double ext[ kMaxFilters ] {};        ///< A_filter / A_V at each pivot
	double colour[ kMaxFilters ][ 3 ] {};///< hue, balanced
	double pivot[ kMaxFilters ] {};

	/// The lines: kLineComponents rows x kTauSamples, RGB. Row c, column j is
	/// what a unit of component c looks like through tau_V = j kTauMax / ( n - 1 ).
	std::vector< float > lines;
	/// The continua: 3 kinds x kTempSamples (rows) x kTauSamples, RGB.
	/// Kind 0, 1: stars and scattered starlight, kStarTempMin..Max; kind 2:
	/// dust, kDustTempMin..Max (log-spaced both).
	std::vector< float > continua;
	/// Galaxies at redshift: kTempSamples temperatures x 32 redshifts (0..8), RGB.
	std::vector< float > galaxies;
};

/// Build every table for one instrument and the target's mix. Pure function.
Palette BuildPalette( int instrument, const Mix& mix = NebulaMix() );

/// The display RGB of a component's unit through the palette, exactly (no
/// table): the reference the harness holds the tables to.
std::array< double, 3 > DisplayOf( const Palette& palette, Component component, double tauV );
std::array< double, 3 > DisplayOfContinuum( const Palette& palette, int kind, double kelvin, double tauV );

double TempOfSample( int kind, int index );
double SampleOfTemp( int kind, double kelvin );

//---------------------------------------------------------------------------
// Cosmology: flat Lambda-CDM, H0 = 70, Omega_m = 0.3.
//---------------------------------------------------------------------------
double AngularDiameterDistance( double z );///< Mpc, Simpson on 400 panels

//---------------------------------------------------------------------------
// Meteors: the classical single-body ablation light curve.
//---------------------------------------------------------------------------

/// Brightness against x = rho / rho_peak (air density over the density at
/// peak): (9/4) x ( 1 - x/3 )^2, 1 at x = 1, 0 at x = 3 when the body is gone.
double MeteorLight( double x );
/// The atmosphere's scale height on the way down, km.
constexpr double kScaleHeightKm = 7.0;
/// Where a meteor starts to be seen, as x.
constexpr double kMeteorStartX = 0.02;

//---------------------------------------------------------------------------
// Supernovae: a type Ia, powered by 56Ni -> 56Co -> 56Fe.
//---------------------------------------------------------------------------
constexpr double kNickelLife = 8.8;   ///< days, e-folding
constexpr double kCobaltLife = 111.3; ///< days, e-folding
constexpr double kDiffusionTime = 13.0;///< days, Arnett's tau_m for a Ia
constexpr double kGammaTrap = 35.0;   ///< days, T0: gamma rays escape as ( T0 / t )^2

/// The decay heating, normalised to 1 at t = 0, with gamma-ray escape.
double DecayHeating( double days );
/// Arnett's light curve for that heating: the heating diffused out over
/// tau_m, L( t ) = ( 2 / tau_m^2 ) e^{-( t / tau_m )^2} int_0^t z e^{( z / tau_m )^2} Q( z ) dz.
/// Tabulated once (trapezoid, 0.05 d steps), normalised to 1 at peak.
double SupernovaLight( double days );
double SupernovaPeakDay();
/// The unnormalised peak, in DecayHeating's units: Arnett's rule says it is
/// the heating on the peak day.
double SupernovaRawPeak();
/// The photosphere: hot and blue at peak, cooling and reddening after.
double SupernovaTemperature( double days );

/// The remnant's radius against its age, in units of its radius at the
/// Sedov-Taylor time: free expansion ( R ~ t ) and then Sedov-Taylor
/// ( R ~ t^2/5 ), joined at t = 1 with matching radius.
double RemnantRadius( double age );

/// A light echo off a dust sheet a distance z in front of the star, seen
/// face-on: the paraboloid of constant delay crosses it at a ring of radius
/// sqrt( 2 z c t + ( c t )^2 ). c t and z in the same units.
double EchoRadius( double ct, double z );

//---------------------------------------------------------------------------
// The harness's way in: break one piece of the physics on purpose.
//---------------------------------------------------------------------------
enum PhysicsHook : int
{
	PH_NONE            = 0,
	PH_FLAT_EXTINCTION = 1 << 0,///< every wavelength reddened like V: no reddening at all
	PH_SOFT_STARS      = 1 << 1,///< the hardness without its series' n > 1 terms
	PH_LINEAR_REDSHIFT = 1 << 2,///< D_A = c z / H0 / ( 1 + z ): no deceleration, no turnover
	PH_NO_ESCAPE       = 1 << 3,///< gamma rays never escape: the tail follows cobalt exactly
	PH_GREY_PALETTE    = 1 << 4,///< the gains not balanced per filter
};
void SetHooksForTest( int bits );
int HooksForTest();

} // namespace sidereal::physics
