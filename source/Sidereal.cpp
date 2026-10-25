#include "Sidereal.h"

#include "Diag.h"
#include "GLState.h"
#include "Shaders.h"

//FFGLSDK.h includes every other scoped binding and omits this one (SDK
//b1afaf9), so it has to be asked for by name.
#include <ffglex/FFGLScopedFBOBinding.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

using namespace ffglex;

namespace sidereal
{
namespace
{
/// Frames that must agree before the host's clock unit is settled.
constexpr int kClockVotes = 4;
/// Host seconds a single frame may advance by.
constexpr double kMaxFrameDelta = 0.1;
/// The drift's wander: how far, in frame heights.
constexpr double kDriftReach = 0.06;
/// The diffraction core: sigma px at 1080 lines = kCorePerMicron x lambda / D.
/// Webb at 2 um comes to 0.85 px, Hubble at 0.6 um to 0.69, MIRI at 11 um to 4.8.
constexpr double kCorePerMicron = 0.85 * 6.5 / 2.0;
/// Spike length, px at 1080 lines per micron for a 6.5 m mirror.
constexpr double kSpikePerMicron = 30.0;
/// The detector's pixel: no star is drawn narrower than this sigma.
constexpr double kPixelSigma = 0.55;
/// The display's base exposure (Exposure 0 stops).
constexpr double kBaseExposure = 0.35;
/// Stars' flux units against the gas's: a star of flux 1 through a filter
/// as bright as the reference nebula's glow over a few pixels.
constexpr double kStarGain = 900.0;

const SiderealPlugin::Constants kShipped = {
	24.0,// ionK: in-plane opacity per unit column per unit thickness
	0.15,// hazeIon: the ionised interior's opacity, relative
	3.0, // kO
	0.75,// kS: the partly ionised zone's soft photons go first, just past the front
	0.5, // kP: the far-UV that heats the dust and lights the aromatics, deeper
	0.15,// kV
	0.4, // frontHaze: the share of the haze in front of the dense gas
	0.012,// relief: a column's height per unit column density
	0.5, // faceShadow
	0.8, // faceGain
	0.2, // hazeShadow: the ionised interior absorbs this share of what its emission counts
	0.025,// molSkin: fluorescent H2 per unit of the skin's far-UV
	0.5, // phaseG
	0.25,// selfFrac
	0.1, // hazeDust
	1.0, 0.8, 0.35, 0.07, 0.8, 2.0, 1.2, 0.8, 0.6, 0.9,// emit H O S PAH dust scat shock sync jets oxy: the reference nebula's ratios
	0.01,// dustFloor: the interstellar radiation field
	1.0, // dustRef: the field that holds dust at 90 K
	0.15,// margin: the field buffer's overhang each side, frame heights
	0.5, // fieldScale: the field's raster against the host's
	0.06,// haloFrac: a star's light in its Moffat halo
	6.0, // haloWidth: the halo's alpha in core sigmas
};

double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

/// A table texture. Bound and unbound by hand: this runs between passes and
/// must leave nothing bound.
void uploadTable( GLuint& texture, GLint format, GLenum layout, int width, int height, const float* data, bool linear )
{
	if( texture == 0 )
		glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	glTexImage2D( GL_TEXTURE_2D, 0, format, width, height, 0, layout, GL_FLOAT, data );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
}

void discardTarget()
{
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT );
}

/// Sersic's b_n (Ciotti & Bertin 1999's expansion).
double sersicB( double n )
{
	return 2.0 * n - 1.0 / 3.0 + 4.0 / ( 405.0 * n ) + 46.0 / ( 25515.0 * n * n );
}
} // namespace

static_assert( PT_COUNT - PT_ABOUT_TEXT == stoatworks::about::kParamCount, "the About run no longer matches StoatworksAbout.h" );

const SiderealPlugin::Constants& SiderealPlugin::Shipped()
{
	return kShipped;
}

double SiderealPlugin::IonisingShare( double kelvin )
{
	// Photons above 13.6 eV over all photons: the tail of x^2 / ( e^x - 1 )
	// over its whole integral, 2 zeta( 3 ).
	const auto share = []( double t ) { return physics::PhotonTail( physics::ReducedEnergy( 13.598, t ) ) / 2.4041138; };
	return share( kelvin ) / share( 40000.0 );
}

//---------------------------------------------------------------------------
SiderealPlugin::SiderealPlugin( bool effect ) :
	isEffect( effect ),
	hostOrder( HostOrder( effect ) )
{
	SetMinInputs( isEffect ? 1 : 0 );
	SetMaxInputs( isEffect ? 1 : 0 );
	SetTimeSupported( true );

	std::fill( std::begin( idToHost ), std::end( idToHost ), -1 );
	for( size_t i = 0; i < hostOrder.size(); ++i )
		idToHost[ hostOrder[ i ] ] = static_cast< int >( i );
	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		params[ id ] = InfoOf( id ).defaultValue;

	for( unsigned int host = 0; host < hostOrder.size(); ++host )
	{
		const unsigned int id = hostOrder[ host ];
		if( id >= PT_ABOUT_TEXT )
			break;
		const ControlInfo& info = InfoOf( id );
		switch( info.kind )
		{
		case Kind::Standard: SetParamInfo( host, info.name, FF_TYPE_STANDARD, params[ id ] ); break;
		case Kind::Option:
			SetOptionParamInfo( host, info.name, static_cast< unsigned int >( info.optionCount ), params[ id ] );
			for( int i = 0; i < info.optionCount; ++i )
				SetParamElementInfo( host, static_cast< unsigned int >( i ), OptionName( id, i ), static_cast< float >( i ) );
			break;
		case Kind::Event: SetParamInfo( host, info.name, FF_TYPE_EVENT, false ); break;
		}
		SetParamGroup( host, info.group );
	}

	const unsigned int aboutHost = static_cast< unsigned int >( idToHost[ PT_ABOUT_TEXT ] );
	SetParamInfo( aboutHost, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = aboutHost + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = aboutHost; i < hostOrder.size(); ++i )
		SetParamGroup( i, "About" );
}

//---------------------------------------------------------------------------
GLint SiderealPlugin::where( const FFGLShader& shader, const char* name )
{
	const auto key = std::make_pair( shader.GetGLID(), name );
	for( const auto& entry : uniformCache )
		if( entry.first == key )
			return entry.second;
	const GLint location = glGetUniformLocation( shader.GetGLID(), name );
	uniformCache.emplace_back( key, location );
	return location;
}
void SiderealPlugin::uniform( const FFGLShader& shader, const char* name, int v )
{
	glUniform1i( where( shader, name ), v );
}
void SiderealPlugin::uniform( const FFGLShader& shader, const char* name, float v )
{
	glUniform1f( where( shader, name ), v );
}
void SiderealPlugin::uniform( const FFGLShader& shader, const char* name, float a, float b )
{
	glUniform2f( where( shader, name ), a, b );
}
void SiderealPlugin::uniform( const FFGLShader& shader, const char* name, float a, float b, float c )
{
	glUniform3f( where( shader, name ), a, b, c );
}
void SiderealPlugin::uniform( const FFGLShader& shader, const char* name, float a, float b, float c, float d )
{
	glUniform4f( where( shader, name ), a, b, c, d );
}
void SiderealPlugin::cameraUniforms( const FFGLShader& shader )
{
	glUniformMatrix2fv( where( shader, "Cam" ), 1, GL_FALSE, cam );
	uniform( shader, "CamPan", camPan[ 0 ], camPan[ 1 ] );
	uniform( shader, "CamDrift", camDrift[ 0 ], camDrift[ 1 ] );
	uniform( shader, "Hooks", hooks );
}

//---------------------------------------------------------------------------
FFResult SiderealPlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) + ( isEffect ? " (Over)" : " (source)" ) );

	const std::string quadVertex = std::string( shaders::kVersion ) + shaders::kQuadVertex;
	struct Stage
	{
		FFGLShader* shader;
		std::string vertex;
		shaders::Pass pass;
		const char* name;
	};
	const Stage stages[] = {
		{ &fieldShader, quadVertex, shaders::Pass::Field, "field" },
		{ &lightShader, quadVertex, shaders::Pass::Light, "light" },
		{ &spriteShader, shaders::Assemble( shaders::Pass::SpriteVertex ), shaders::Pass::Sprite, "sprite" },
		{ &compositeShader, quadVertex, shaders::Pass::Composite, "composite" },
	};
	for( const Stage& stage : stages )
	{
		const std::string fragment = shaders::Assemble( stage.pass );
		if( stage.shader->Compile( stage.vertex.c_str(), fragment.c_str() ) )
			continue;
		diag::error( std::string( "the " ) + stage.name + " shader failed to compile - the plugin will do nothing" );
		FFGLLog::LogToHost( "Sidereal: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}
	if( !quad.Initialise() )
	{
		diag::error( "quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}
	glGenFramebuffers( 1, &mrtFBO );
	// The sprites are drawn from gl_VertexID alone; core profile still wants
	// a vertex array bound.
	glGenVertexArrays( 1, &spriteVAO );

	// The supernova's light curve, once.
	std::vector< float > curve( shaders::kSupernovaSamples );
	for( int i = 0; i < shaders::kSupernovaSamples; ++i )
		curve[ static_cast< size_t >( i ) ] =
			static_cast< float >( physics::SupernovaLight( shaders::kSupernovaDays * i / ( shaders::kSupernovaSamples - 1 ) ) );
	uploadTable( supernovaTexture, GL_R32F, GL_RED, shaders::kSupernovaSamples, 1, curve.data(), true );

	sceneKey = -1;
	palette  = physics::Palette {};
	diag::info( "initialised" );
	return CFFGLPlugin::InitGL( vp );
}

//---------------------------------------------------------------------------
bool SiderealPlugin::ensureBuffers( int width, int height )
{
	const double aspect = static_cast< double >( width ) / height;
	const double m      = kShipped.margin;
	fieldExtent[ 0 ]    = aspect * ( 1.0 + 2.0 * m );
	fieldExtent[ 1 ]    = 1.0 + 2.0 * m;
	fieldWidth  = std::max( 16, static_cast< int >( std::lround( width * ( 1.0 + 2.0 * m / aspect ) * kShipped.fieldScale ) ) );
	fieldHeight = std::max( 16, static_cast< int >( std::lround( height * ( 1.0 + 2.0 * m ) * kShipped.fieldScale ) ) );
	bool ok = fieldA.Ensure( fieldWidth, fieldHeight, GL_RGBA16F, PassBuffer::Sampling::Mipmapped );
	ok      = ok && fieldB.Ensure( fieldWidth, fieldHeight, GL_RGBA16F, PassBuffer::Sampling::Linear );
	ok      = ok && light0.Ensure( fieldWidth, fieldHeight, GL_RGBA16F, PassBuffer::Sampling::Linear );
	ok      = ok && light1.Ensure( fieldWidth, fieldHeight, GL_RGBA16F, PassBuffer::Sampling::Linear );
	ok      = ok && light2.Ensure( fieldWidth, fieldHeight, GL_RGBA16F, PassBuffer::Sampling::Linear );
	ok      = ok && light3.Ensure( fieldWidth, fieldHeight, GL_RGBA16F, PassBuffer::Sampling::Linear );
	ok      = ok && sprites.Ensure( width, height, GL_RGBA16F, PassBuffer::Sampling::Nearest );
	hostWidth  = width;
	hostHeight = height;
	return ok;
}

//---------------------------------------------------------------------------
void SiderealPlugin::rebuildScene()
{
	const int index      = OptionIndex( params[ PT_TARGET ], scene::TargetCount() );
	const uint32_t seed  = static_cast< uint32_t >( std::lround( std::clamp( params[ PT_SEED ], 0.0f, 1.0f ) * 1000.0f ) );
	const int key        = hasTestTarget ? -2 : index * 4096 + static_cast< int >( seed );
	if( key == sceneKey )
		return;
	sceneKey = key;
	target   = hasTestTarget ? testTarget : scene::Build( index, seed );
	events.Reset();

	using shaders::kTexelsPerElement;
	elementCount = std::min( static_cast< int >( target.elements.size() ), shaders::kMaxElements );
	std::vector< float > texels( static_cast< size_t >( std::max( elementCount, 1 ) ) * kTexelsPerElement * 4, 0.0f );
	for( int i = 0; i < elementCount; ++i )
	{
		const scene::Element& e = target.elements[ static_cast< size_t >( i ) ];
		float* t                = &texels[ static_cast< size_t >( i ) * kTexelsPerElement * 4 ];
		const float v[ kTexelsPerElement * 4 ] = {
			static_cast< float >( e.shape ), static_cast< float >( e.channel ), e.depth, static_cast< float >( e.polyStart ),
			e.x, e.y, e.x2, e.y2,
			e.a, e.b, std::cos( e.angle ), std::sin( e.angle ),
			e.density, e.thickness, e.edge, e.rough,
			e.detail, e.extra, e.extra2, static_cast< float >( e.polyCount ),
		};
		std::copy( std::begin( v ), std::end( v ), t );
	}
	uploadTable( elementTexture, GL_RGBA32F, GL_RGBA, std::max( elementCount, 1 ) * kTexelsPerElement, 1, texels.data(), false );

	std::vector< float > poly = target.polygon;
	poly.resize( std::min< size_t >( poly.size(), shaders::kMaxPolygon * 2 ) );
	if( poly.empty() )
		poly = { 0.0f, 0.0f };
	uploadTable( polygonTexture, GL_RG32F, GL_RG, static_cast< int >( poly.size() / 2 ), 1, poly.data(), false );
	diag::info( std::string( "target " ) + target.name + ": " + std::to_string( elementCount ) + " elements, "
	            + std::to_string( target.sources.size() ) + " sources, " + std::to_string( target.stars.size() ) + " bright stars" );
}

void SiderealPlugin::rebuildPalette( int instrument )
{
	if( palette.instrument == instrument && palette.filterCount > 0 && paletteScene == sceneKey )
		return;
	paletteScene = sceneKey;
	palette      = physics::BuildPalette( instrument, target.mix );
	using namespace physics;
	uploadTable( lineTexture, GL_RGB32F, GL_RGB, kTauSamples, kLineComponents, palette.lines.data(), true );
	uploadTable( continuumTexture, GL_RGB32F, GL_RGB, kTauSamples, 3 * kTempSamples, palette.continua.data(), true );
	// The galaxy table is stored temperature-major ( T rows, z columns ).
	uploadTable( galaxyTexture, GL_RGB32F, GL_RGB, 32, kTempSamples, palette.galaxies.data(), true );
	// Each filter's gain x its response to a blackbody, for the sprites.
	std::vector< float > stars( static_cast< size_t >( kTempSamples ) * 8, 0.0f );
	const Instrument& inst = InstrumentOf( instrument );
	for( int f = 0; f < palette.filterCount; ++f )
		for( int i = 0; i < kTempSamples; ++i )
			stars[ static_cast< size_t >( f ) * kTempSamples + i ] = static_cast< float >(
				palette.gain[ f ] * ContinuumResponse( FilterNamed( inst.filters[ f ] ), 0, TempOfSample( 0, i ) ) );
	uploadTable( starTexture, GL_R32F, GL_RED, kTempSamples, 8, stars.data(), true );
	// Stars as bright on every telescope: a narrow band passes a sliver of a
	// star's continuum and a broad one a slab, and the pictures are stretched
	// to suit. Normalised to Hubble's narrow bands, where kStarGain was set.
	const physics::Palette reference = physics::BuildPalette( physics::I_HUBBLE_NARROW, target.mix );
	const auto lum = []( const std::array< double, 3 >& c ) { return 0.2126 * c[ 0 ] + 0.7152 * c[ 1 ] + 0.0722 * c[ 2 ]; };
	starGain = kStarGain * lum( DisplayOfContinuum( reference, 0, 5800.0, 0.0 ) ) / std::max( lum( DisplayOfContinuum( palette, 0, 5800.0, 0.0 ) ), 1e-30 );
	// Every other continuum the same way: what one unit of it looks like
	// through this palette, so a galaxy or a reflection nebula is as bright
	// on every telescope and only its colour changes.
	continuumNorm = 1.0 / std::max( lum( DisplayOfContinuum( palette, 0, 6000.0, 0.0 ) ), 1e-30 );
	scatterNorm   = 1.0 / std::max( lum( DisplayOfContinuum( palette, 1, 10000.0, 0.0 ) ), 1e-30 );
	meteorNorm    = 1.0 / std::max( lum( DisplayOf( palette, C_METEOR_SLOW, 0.0 ) ), 1e-30 );
	{
		const double row = SampleOfTemp( 0, 6500.0 );
		const int i      = std::clamp( static_cast< int >( std::lround( row ) ), 0, kTempSamples - 1 );
		const int k      = static_cast< int >( std::lround( 0.5 / 8.0 * 31.0 ) );
		std::array< double, 3 > c {};
		for( int j = 0; j < 3; ++j )
			c[ j ] = palette.galaxies[ ( static_cast< size_t >( i ) * 32 + k ) * 3 + j ];
		deepNorm = 1.0 / std::max( lum( c ), 1e-30 );
	}
	diag::info( std::string( "telescope " ) + inst.name );
}

//---------------------------------------------------------------------------
std::array< double, 2 > SiderealPlugin::ScreenPx( double x, double y, double depth ) const
{
	const bool wrong = ( hooks & shaders::kHookWrongParallax ) != 0;
	const double k   = ( wrong ? depth : 1.0 / std::max( depth, 0.05 ) ) - 1.0;
	const double ox = x - camPan[ 0 ] - camDrift[ 0 ] * k, oy = y - camPan[ 1 ] - camDrift[ 1 ] * k;
	// The inverse of cam: zoom times the rotation's transpose.
	const double a = cam[ 0 ], b = cam[ 1 ], c = cam[ 2 ], d = cam[ 3 ];
	const double det = a * d - b * c;
	const double sx  = ( d * ox - c * oy ) / det;
	const double sy  = ( -b * ox + a * oy ) / det;
	const double aspect = static_cast< double >( hostWidth ) / std::max( hostHeight, 1 );
	return { ( sx / aspect + 0.5 ) * hostWidth, ( sy + 0.5 ) * hostHeight };
}

void SiderealPlugin::updateCamera( double dt, int width, int height )
{
	(void)width;
	(void)height;
	zoom = ZoomFromParam( params[ PT_ZOOM ] );
	spin = SpinFromParam( params[ PT_SPIN ] );
	const double c = std::cos( spin ) / zoom, s = std::sin( spin ) / zoom;
	cam[ 0 ] = static_cast< float >( c );
	cam[ 1 ] = static_cast< float >( s );
	cam[ 2 ] = static_cast< float >( -s );
	cam[ 3 ] = static_cast< float >( c );
	// The drift: a slow Lissajous wander, phase in double.
	driftPhase += dt * DriftFromParam( params[ PT_DRIFT ] ) / kDriftReach;
	driftPhase = std::fmod( driftPhase, 2.0 * sidereal::kPi * 1000.0 );
	// Zero at phase zero: Drift at 0 leaves the camera where Pan puts it.
	const double ox = kDriftReach * std::sin( driftPhase ), oy = 0.6 * kDriftReach * ( std::sin( 0.77 * driftPhase + 1.3 ) - std::sin( 1.3 ) );
	const double px = PanFromParam( params[ PT_PAN_X ] ) + ox, py = PanFromParam( params[ PT_PAN_Y ] ) + oy;
	camPan[ 0 ] = static_cast< float >( px );
	camPan[ 1 ] = static_cast< float >( py );
	// Everything moved by the camera moves by depth: things nearer than the
	// nebula more, further less.
	const double depth = DepthFromParam( params[ PT_DEPTH ] );
	camDrift[ 0 ] = static_cast< float >( px * depth * 0.5 );
	camDrift[ 1 ] = static_cast< float >( py * depth * 0.5 );
	flowTime = std::fmod( flowTime + dt * FlowFromParam( params[ PT_FLOW ] ), 1000.0 );
	flyPhase = std::fmod( flyPhase + dt * FlyFromParam( params[ PT_FLY ] ) / 3.88, 4096.0 );
}

//---------------------------------------------------------------------------
void SiderealPlugin::fieldPass( const FFGLTextureStruct* input )
{
	glBindFramebuffer( GL_FRAMEBUFFER, mrtFBO );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fieldA.TextureID(), 0 );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, fieldB.TextureID(), 0 );
	const GLenum targets[ 2 ] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	glDrawBuffers( 2, targets );
	glViewport( 0, 0, fieldWidth, fieldHeight );
	discardTarget();
	glUseProgram( fieldShader.GetGLID() );
	cameraUniforms( fieldShader );
	bindUnit( 0, elementTexture );
	bindUnit( 1, polygonTexture );
	bindUnit( 2, input != nullptr ? input->Handle : supernovaTexture );
	uniform( fieldShader, "Elements", 0 );
	uniform( fieldShader, "Poly", 1 );
	uniform( fieldShader, "Clip", 2 );
	uniform( fieldShader, "FieldExtent", static_cast< float >( fieldExtent[ 0 ] ), static_cast< float >( fieldExtent[ 1 ] ) );
	uniform( fieldShader, "ElementCount", elementCount );
	uniform( fieldShader, "FlowTime", static_cast< float >( flowTime ) );
	uniform( fieldShader, "Turb", static_cast< float >( TurbulenceFromParam( params[ PT_TURBULENCE ] ) ) );
	uniform( fieldShader, "WarpScale", target.warpScale );
	uniform( fieldShader, "HazeLevel", target.haze );
	uniform( fieldShader, "HazeScale", target.hazeScale );
	uniform( fieldShader, "HazeMul", static_cast< float >( HazeFromParam( params[ PT_HAZE ] ) ) );
	uniform( fieldShader, "GasScale", static_cast< float >( GasFromParam( params[ PT_GAS ] ) ) );
	uniform( fieldShader, "IonK", static_cast< float >( kShipped.ionK ) );
	const bool clipGas = input != nullptr && OptionIndex( params[ PT_CLIP_AS ], static_cast< int >( ClipAs::Count ) ) == static_cast< int >( ClipAs::Gas );
	uniform( fieldShader, "ClipGas", clipGas ? 1 : 0 );
	if( input != nullptr )
	{
		const double aspect = static_cast< double >( hostWidth ) / hostHeight;
		const double sx = static_cast< double >( input->Width ) / std::max< FFUInt32 >( input->HardwareWidth, 1 );
		const double sy = static_cast< double >( input->Height ) / std::max< FFUInt32 >( input->HardwareHeight, 1 );
		uniform( fieldShader, "ClipMap", static_cast< float >( sx / aspect ), static_cast< float >( sy ), static_cast< float >( 0.5 * sx ),
		         static_cast< float >( 0.5 * sy ) );
	}
	quad.Draw();
	unbindTextureUnits( 3 );
	const GLenum one[ 1 ] = { GL_COLOR_ATTACHMENT0 };
	glDrawBuffers( 1, one );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, 0, 0 );
	fieldA.GenerateMipmaps();
}

void SiderealPlugin::lightPass()
{
	// The stars that light the gas, turned by Light Angle about the middle,
	// as field coordinates: a source off the frame lies off the buffer too.
	const double turn  = LightAngleFromParam( params[ PT_LIGHT_ANGLE ] );
	const double power = StarlightFromParam( params[ PT_STARLIGHT ] );
	const double heat  = HeatFromParam( params[ PT_STAR_HEAT ] );
	sourceCount        = std::min( static_cast< int >( target.sources.size() ), shaders::kMaxSources );
	sourceA.assign( static_cast< size_t >( shaders::kMaxSources ) * 4, 0.0f );
	sourceB.assign( static_cast< size_t >( shaders::kMaxSources ) * 4, 0.0f );
	for( int k = 0; k < sourceCount; ++k )
	{
		const scene::Source& src = target.sources[ static_cast< size_t >( k ) ];
		const double x = src.x * std::cos( turn ) - src.y * std::sin( turn );
		const double y = src.x * std::sin( turn ) + src.y * std::cos( turn );
		const auto px  = ScreenPx( x, y, 1.0 );
		const double sx = ( px[ 0 ] / hostWidth - 0.5 ) * hostWidth / hostHeight, sy = px[ 1 ] / hostHeight - 0.5;
		const double t  = src.temperature * heat;
		sourceA[ k * 4 + 0 ] = static_cast< float >( sx / fieldExtent[ 0 ] + 0.5 );
		sourceA[ k * 4 + 1 ] = static_cast< float >( sy / fieldExtent[ 1 ] + 0.5 );
		sourceA[ k * 4 + 2 ] = src.h;
		sourceA[ k * 4 + 3 ] = static_cast< float >( src.luminosity * power * IonisingShare( t ) );
		// [O III] grows with the star's hard photons only until the oxygen is
		// all O++ (and past it, O+++): the share saturates at four times a
		// 40 kK star's.
		const double hardRef = physics::OxygenHardness( 40000.0 );
		const double q       = physics::OxygenHardness( t ) / hardRef;
		sourceB[ k * 4 + 0 ] = static_cast< float >( hardRef * q / ( 1.0 + q / 4.0 ) );
		sourceB[ k * 4 + 1 ] = static_cast< float >( src.luminosity * power );
		sourceB[ k * 4 + 2 ] = static_cast< float >( t );
		sourceB[ k * 4 + 3 ] = src.soft;
	}

	glBindFramebuffer( GL_FRAMEBUFFER, mrtFBO );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, light0.TextureID(), 0 );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, light1.TextureID(), 0 );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, light2.TextureID(), 0 );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, light3.TextureID(), 0 );
	const GLenum targets[ 4 ] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3 };
	glDrawBuffers( 4, targets );
	glViewport( 0, 0, fieldWidth, fieldHeight );
	discardTarget();
	glUseProgram( lightShader.GetGLID() );
	cameraUniforms( lightShader );
	bindUnit( 0, fieldA.TextureID() );
	uniform( lightShader, "FieldA", 0 );
	// Distances in the light pass are the scene's: the screen's over the zoom.
	uniform( lightShader, "FieldExtent", static_cast< float >( fieldExtent[ 0 ] / zoom ), static_cast< float >( fieldExtent[ 1 ] / zoom ) );
	uniform( lightShader, "FieldTexels", static_cast< float >( fieldWidth ), static_cast< float >( fieldHeight ) );
	uniform( lightShader, "SourceCount", sourceCount );
	glUniform4fv( where( lightShader, "SrcA" ), shaders::kMaxSources, sourceA.data() );
	glUniform4fv( where( lightShader, "SrcB" ), shaders::kMaxSources, sourceB.data() );
	uniform( lightShader, "Relief", static_cast< float >( kShipped.relief ) );
	uniform( lightShader, "FaceShadow", static_cast< float >( kShipped.faceShadow ) );
	uniform( lightShader, "HazeShade", target.hazeShade );
	uniform( lightShader, "HazeOpacity", static_cast< float >( kShipped.ionK * kShipped.hazeIon * kShipped.hazeShadow ) );
	uniform( lightShader, "KO", static_cast< float >( kShipped.kO ) );
	uniform( lightShader, "KS", static_cast< float >( kShipped.kS ) );
	uniform( lightShader, "KP", static_cast< float >( kShipped.kP ) );
	uniform( lightShader, "KV", static_cast< float >( kShipped.kV ) );
	uniform( lightShader, "PhaseG", static_cast< float >( kShipped.phaseG ) );
	quad.Draw();
	unbindTextureUnits( 1 );
	const GLenum one[ 1 ] = { GL_COLOR_ATTACHMENT0 };
	glDrawBuffers( 1, one );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, 0, 0 );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, 0, 0 );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, 0, 0 );
}

//---------------------------------------------------------------------------
void SiderealPlugin::spritePass( int width, int height )
{
	using namespace physics;
	const Instrument& inst = InstrumentOf( palette.instrument );
	const double raster    = height / 1080.0;
	double meanPivot = 0.0;
	for( int f = 0; f < palette.filterCount; ++f )
		meanPivot += palette.pivot[ f ] / palette.filterCount;
	const double core   = kCorePerMicron / inst.aperture * raster;// px per micron of pivot
	const double seeing = std::sqrt( std::pow( ( inst.seeing / 2.3548 + SeeingFromParam( params[ PT_SEEING ] ) / 2.3548 ) * raster, 2.0 )
	                                 + kPixelSigma * kPixelSigma );
	coreSigmaPx   = core;
	seeingSigmaPx = seeing;
	spikeLengthPx = kSpikePerMicron * 6.5 / inst.aperture * raster;
	const double spikes = SpikesFromParam( params[ PT_SPIKES ] ) * ( inst.spikes == Spikes::None ? 0.0 : 1.0 );
	const double sigmaMax = std::sqrt( std::pow( core * palette.pivot[ std::max( palette.filterCount - 1, 0 ) ], 2.0 ) + seeing * seeing );

	std::vector< float > data;
	data.reserve( static_cast< size_t >( shaders::kMaxSprites ) * 16 );
	auto addStar = [ & ]( double px, double py, double flux, double temperature, int layer ) {
		if( data.size() / 16 >= static_cast< size_t >( shaders::kMaxSprites ) || flux <= 0.0 )
			return;
		// How far the spikes stay visible: where flux x amp x ( r0 / a )^2 falls
		// to a hundredth of a display unit, capped at three e-folds.
		const double r0    = 2.0 * sigmaMax;
		const double reach = spikes > 0.0 ? std::min( r0 * std::sqrt( flux * starGain * spikes * 0.4 * 0.003 / 0.004 ), 3.0 * spikeLengthPx * palette.pivot[ std::max( palette.filterCount - 1, 0 ) ] ) : 0.0;
		const double extent = std::max( { 8.0 * sigmaMax, reach, 6.0 } ) + 2.0;
		if( px < -extent || py < -extent || px > width + extent || py > height + extent )
			return;
		const float v[ 16 ] = { static_cast< float >( px ), static_cast< float >( py ), static_cast< float >( flux * starGain ), static_cast< float >( temperature ),
			                    0.0f, 0.0f, static_cast< float >( layer ), static_cast< float >( extent ),
			                    0, 0, 0, 0, 0, 0, 0, 0 };
		data.insert( data.end(), std::begin( v ), std::end( v ) );
	};
	const double starScale = StarsFromParam( params[ PT_STARS ] ) > 0.0 ? 1.0 : 0.0;
	for( const scene::Star& s : target.stars )
	{
		const auto p = ScreenPx( s.x, s.y, s.depth );
		addStar( p[ 0 ], p[ 1 ], s.flux * starScale, s.temperature, s.layer );
	}
	for( const scene::Source& src : target.sources )
		if( src.visible )
		{
			const auto p = ScreenPx( src.x, src.y, 1.0 );
			addStar( p[ 0 ], p[ 1 ], 0.6, src.temperature, 0 );
		}
	const Supernova& nova = events.Nova();
	if( nova.active && nova.flux > 0.0f )
	{
		const auto p = ScreenPx( nova.x, nova.y, 1.0 );
		addStar( p[ 0 ], p[ 1 ], nova.flux, nova.temperature, 1 );
	}
	// Meteors live in the air, on the screen: no camera, no parallax.
	const double aspect = static_cast< double >( width ) / height;
	for( const Meteor& m : events.Meteors() )
	{
		if( data.size() / 16 >= static_cast< size_t >( shaders::kMaxSprites ) )
			break;
		const double elapsed = ( now - m.start ) / m.duration;
		if( elapsed < 0.0 )
			continue;
		const double sig = std::max( 1.2 * raster, kPixelSigma );
		const double x0  = physics::kMeteorStartX;
		const float v[ 16 ] = {
			static_cast< float >( ( m.x / aspect + 0.5 ) * width ), static_cast< float >( ( m.y + 0.5 ) * height ), m.dx, m.dy,
			1.0f, static_cast< float >( m.length * height ), static_cast< float >( elapsed ), static_cast< float >( 6.0 * sig + 2.0 ),
			static_cast< float >( std::log( 3.0 / x0 ) ), static_cast< float >( x0 ), m.peak, m.fast,
			m.train, m.trainLife / m.duration, static_cast< float >( 0.12 / m.duration ), static_cast< float >( sig ),
		};
		data.insert( data.end(), std::begin( v ), std::end( v ) );
	}
	spriteCount = static_cast< int >( data.size() / 16 );
	if( data.empty() )
		data.assign( 16, 0.0f );
	uploadTable( spriteTexture, GL_RGBA32F, GL_RGBA, static_cast< int >( data.size() / 4 ), 1, data.data(), false );

	glBindFramebuffer( GL_FRAMEBUFFER, sprites.GetGLID() );
	glViewport( 0, 0, width, height );
	discardTarget();
	if( spriteCount == 0 || ( hooks & shaders::kHookNoStars ) != 0 )
		return;
	setAdditiveBlend();
	glUseProgram( spriteShader.GetGLID() );
	cameraUniforms( spriteShader );
	bindUnit( 0, spriteTexture );
	bindUnit( 1, fieldA.TextureID() );
	bindUnit( 2, starTexture );
	bindUnit( 3, lineTexture );
	uniform( spriteShader, "SpriteData", 0 );
	uniform( spriteShader, "FieldA", 1 );
	uniform( spriteShader, "StarLut", 2 );
	uniform( spriteShader, "LineLut", 3 );
	uniform( spriteShader, "Raster", static_cast< float >( width ), static_cast< float >( height ) );
	// px -> field uv: px over the raster is screen uv, the screen sits in the
	// middle of the field.
	const float fx = static_cast< float >( aspect / fieldExtent[ 0 ] / width ), fy = static_cast< float >( 1.0 / fieldExtent[ 1 ] / height );
	uniform( spriteShader, "PxToField", fx, fy, static_cast< float >( 0.5 - 0.5 * aspect / fieldExtent[ 0 ] ),
	         static_cast< float >( 0.5 - 0.5 / fieldExtent[ 1 ] ) );
	uniform( spriteShader, "DustK", static_cast< float >( target.dustRatio * DustFromParam( params[ PT_DUST ] ) ) );
	uniform( spriteShader, "FilterCount", palette.filterCount );
	float colours[ 24 ] = {}, ext[ 8 ] = {}, scale[ 8 ] = {};
	for( int f = 0; f < palette.filterCount; ++f )
	{
		for( int c = 0; c < 3; ++c )
			colours[ f * 3 + c ] = static_cast< float >( palette.colour[ f ][ c ] );
		ext[ f ]   = static_cast< float >( palette.ext[ f ] );
		scale[ f ] = static_cast< float >( palette.pivot[ f ] );
	}
	glUniform3fv( where( spriteShader, "FilterColour" ), 8, colours );
	glUniform1fv( where( spriteShader, "FilterExt" ), 8, ext );
	glUniform1fv( where( spriteShader, "FilterScale" ), 8, scale );
	uniform( spriteShader, "CoreSigma", static_cast< float >( core ) );
	uniform( spriteShader, "SeeingSigma", static_cast< float >( seeing ) );
	uniform( spriteShader, "HaloFrac", static_cast< float >( kShipped.haloFrac ) );
	uniform( spriteShader, "HaloWidth", static_cast< float >( kShipped.haloWidth ) );
	// The spike pattern, fixed to the telescope (the camera's spin turns the
	// sky, not the spider).
	float dirs[ 32 ] = {};
	int count        = 0;
	const double base = inst.spikeAngle * sidereal::kPi / 180.0;
	auto addSpike     = [ & ]( double angle, double weight ) {
        dirs[ count * 4 + 0 ] = static_cast< float >( std::cos( angle ) );
        dirs[ count * 4 + 1 ] = static_cast< float >( std::sin( angle ) );
        dirs[ count * 4 + 2 ] = static_cast< float >( weight );
        ++count;
	};
	if( inst.spikes == Spikes::Four )
		for( int k = 0; k < 4; ++k )
			addSpike( base + k * sidereal::kPi / 2.0, 1.0 );
	else if( inst.spikes == Spikes::Webb )
	{
		for( int k = 0; k < 6; ++k )
			addSpike( base + sidereal::kPi / 2.0 + k * sidereal::kPi / 3.0, 1.0 );
		addSpike( base, 0.35 );
		addSpike( base + sidereal::kPi, 0.35 );
	}
	uniform( spriteShader, "SpikeCount", count );
	glUniform4fv( where( spriteShader, "SpikeDir" ), 8, dirs );
	uniform( spriteShader, "SpikeAmp", static_cast< float >( 0.4 * spikes ) );
	uniform( spriteShader, "SpikeLen", static_cast< float >( spikeLengthPx ) );
	uniform( spriteShader, "WakeGain", 0.25f );
	uniform( spriteShader, "MeteorGain", static_cast< float >( 1.2 * meteorNorm ) );
	glBindVertexArray( spriteVAO );
	glDrawArrays( GL_TRIANGLES, 0, 6 * spriteCount );
	glBindVertexArray( 0 );
	unbindTextureUnits( 4 );
	glDisable( GL_BLEND );
}

//---------------------------------------------------------------------------
void SiderealPlugin::composite( const FFGLTextureStruct* input, GLuint hostFBO, const GLint* viewport, int width, int height )
{
	using namespace physics;
	glBindFramebuffer( GL_FRAMEBUFFER, hostFBO );
	glViewport( viewport[ 0 ], viewport[ 1 ], viewport[ 2 ], viewport[ 3 ] );
	glUseProgram( compositeShader.GetGLID() );
	cameraUniforms( compositeShader );
	// With no clip, the Clip sampler still needs a texture that exists: the
	// light curve's stands in (Apple's driver complains of an unloadable unit).
	const GLuint units[ 12 ] = { fieldA.TextureID(),  fieldB.TextureID(), light0.TextureID(),  light1.TextureID(), light2.TextureID(),
		                         light3.TextureID(),  sprites.TextureID(), lineTexture,        continuumTexture,   galaxyTexture,
		                         supernovaTexture,    input != nullptr ? input->Handle : supernovaTexture };
	const char* names[ 12 ] = { "FieldA", "FieldB", "Light0", "Light1", "Light2", "Light3", "Sprites", "LineLut", "ContLut", "GalLut", "SnLut", "Clip" };
	for( int i = 0; i < 12; ++i )
	{
		bindUnit( i, units[ i ] );
		uniform( compositeShader, names[ i ], i );
	}
	const double aspect = static_cast< double >( width ) / height;
	const double raster = height / 1080.0;
	uniform( compositeShader, "Aspect", static_cast< float >( aspect ) );
	uniform( compositeShader, "FieldMap", static_cast< float >( 1.0 / fieldExtent[ 0 ] ), static_cast< float >( 1.0 / fieldExtent[ 1 ] ), 0.5f,
	         0.5f );
	const double dustK = target.dustRatio * DustFromParam( params[ PT_DUST ] );
	uniform( compositeShader, "DustK", static_cast< float >( dustK ) );
	uniform( compositeShader, "IonK", static_cast< float >( kShipped.ionK ) );
	uniform( compositeShader, "HazeIon", static_cast< float >( kShipped.hazeIon ) );
	uniform( compositeShader, "FrontHaze", static_cast< float >( kShipped.frontHaze ) );
	uniform( compositeShader, "FaceGain", static_cast< float >( kShipped.faceGain ) );
	uniform( compositeShader, "MolSkin", static_cast< float >( kShipped.molSkin ) );
	uniform( compositeShader, "HardRef", static_cast< float >( physics::OxygenHardness( 40000.0 ) ) );
	uniform( compositeShader, "SelfFrac", static_cast< float >( kShipped.selfFrac ) );
	uniform( compositeShader, "HazeDust", static_cast< float >( kShipped.hazeDust ) );
	uniform( compositeShader, "EmitA", static_cast< float >( kShipped.emitH ), static_cast< float >( kShipped.emitO ),
	         static_cast< float >( kShipped.emitS ), static_cast< float >( kShipped.emitPah ) );
	uniform( compositeShader, "EmitB", static_cast< float >( kShipped.emitDust ), static_cast< float >( kShipped.emitScat ),
	         static_cast< float >( kShipped.emitShock ), static_cast< float >( kShipped.emitSync ) );
	uniform( compositeShader, "EmitC", static_cast< float >( kShipped.emitJets ), static_cast< float >( kShipped.emitOxy ),
	         static_cast< float >( kShipped.dustFloor ), static_cast< float >( kShipped.dustRef ) );

	// The faint stars.
	const double stars = StarsFromParam( params[ PT_STARS ] );
	const Instrument& inst = InstrumentOf( palette.instrument );
	double meanPivot = 0.0;
	for( int f = 0; f < palette.filterCount; ++f )
		meanPivot += palette.pivot[ f ] / palette.filterCount;
	const double starSigma = std::sqrt( std::pow( kCorePerMicron / inst.aperture * raster * meanPivot, 2.0 ) + seeingSigmaPx * seeingSigmaPx );
	uniform( compositeShader, "StarDensity", static_cast< float >( target.fieldStars * stars ) );
	uniform( compositeShader, "EmbeddedDensity", static_cast< float >( target.embedded * stars ) );
	uniform( compositeShader, "StarFluxMin", static_cast< float >( 0.15 * starGain ) );
	uniform( compositeShader, "StarFluxMax", static_cast< float >( 4.0 * starGain ) );
	uniform( compositeShader, "StarSigma", static_cast< float >( starSigma ) );
	uniform( compositeShader, "SceneToPx", static_cast< float >( height * zoom ) );
	uniform( compositeShader, "ScreenToPx", static_cast< float >( height ) );
	glUniform1ui( where( compositeShader, "StarSalt" ), static_cast< GLuint >( 7919u + static_cast< unsigned >( sceneKey & 0xFFFF ) * 31u ) );
	uniform( compositeShader, "FlySpeed", static_cast< float >( FlyFromParam( params[ PT_FLY ] ) ) );
	uniform( compositeShader, "FlyPhase", static_cast< float >( flyPhase ) );
	uniform( compositeShader, "Band", target.band.amount, std::cos( target.band.angle ), std::sin( target.band.angle ), target.band.width );
	uniform( compositeShader, "BandOffset", target.band.offset );

	// The supernova.
	const Supernova& nova = events.Nova();
	uniform( compositeShader, "SnA", nova.x, nova.y, static_cast< float >( nova.days ), nova.active ? 1.0f : 0.0f );
	uniform( compositeShader, "SnB", nova.remnantRadius, nova.remnantGlow, target.echoDistance,
	         static_cast< float >( EchoFromParam( params[ PT_ECHO ] ) ) );
	uniform( compositeShader, "SnC", static_cast< float >( Events::kLightPerDay ), Events::kPeakFlux, 9500.0f, 0.0f );
	uniform( compositeShader, "RemnantGain", 2.5f );
	uniform( compositeShader, "EchoGain", static_cast< float >( 0.01 * scatterNorm ) );
	uniform( compositeShader, "BandGain", static_cast< float >( 0.4 * continuumNorm ) );
	uniform( compositeShader, "ScatterGain", static_cast< float >( 0.12 * scatterNorm ) );

	// The galaxies.
	const int galaxies = std::min( static_cast< int >( target.galaxies.size() ), shaders::kMaxGalaxies );
	std::vector< float > gal( static_cast< size_t >( shaders::kMaxGalaxies ) * 24, 0.0f );
	for( int i = 0; i < galaxies; ++i )
	{
		const scene::Galaxy& g = target.galaxies[ static_cast< size_t >( i ) ];
		const double ci = std::cos( g.incline ), si = std::sin( g.incline );
		const double n  = g.sersic, b = sersicB( n );
		const double q  = g.kind == 1 ? g.bulgeAxis : std::sqrt( ci * ci + g.bulgeAxis * g.bulgeAxis * si * si );
		const double bulgeTotal = 2.0 * sidereal::kPi * n * g.bulgeRe * g.bulgeRe * q * std::exp( b ) * std::pow( b, -2.0 * n ) * std::tgamma( 2.0 * n );
		const double ib = g.brightness * g.bulge / std::max( bulgeTotal, 1e-12 );
		const double id = g.kind == 1 ? 0.0 : g.brightness * ( 1.0 - g.bulge ) / ( 2.0 * sidereal::kPi * g.scale * g.scale );
		const double phase = g.phase + flowTime * 0.6;
		const float v[ 24 ] = {
			g.x, g.y, g.scale, static_cast< float >( g.kind ),
			static_cast< float >( ci ), static_cast< float >( si ), std::cos( g.pa ), std::sin( g.pa ),
			static_cast< float >( g.arms ), std::tan( g.pitch ), g.armContrast, static_cast< float >( phase ),
			static_cast< float >( ib ), g.bulgeRe, static_cast< float >( n ), static_cast< float >( b ),
			static_cast< float >( q ), g.dust, g.dustRing, g.hii,
			g.diskT, g.bulgeT, g.armT, static_cast< float >( id ),
		};
		std::copy( std::begin( v ), std::end( v ), gal.begin() + i * 24 );
	}
	uniform( compositeShader, "GalaxyCount", galaxies );
	glUniform4fv( where( compositeShader, "Gal" ), shaders::kMaxGalaxies * 6, gal.data() );
	uniform( compositeShader, "GalaxyGain", static_cast< float >( 0.25 * continuumNorm ) );
	const scene::DeepField& deep = target.deep;
	uniform( compositeShader, "DeepOn", deep.on ? 1 : 0 );
	uniform( compositeShader, "LensOn", deep.lens ? 1 : 0 );
	uniform( compositeShader, "DeepA", deep.density, deep.lensX, deep.lensY, deep.einstein );
	uniform( compositeShader, "DeepB", deep.zLens, deep.members, deep.coreRadius, 6.98e-4f );
	float da[ 33 ];
	for( int i = 0; i < 33; ++i )
		da[ i ] = static_cast< float >( AngularDiameterDistance( 0.25 * i ) );
	glUniform1fv( where( compositeShader, "DA" ), 33, da );
	uniform( compositeShader, "DeepGain", static_cast< float >( 0.12 * deepNorm ) );

	// The display.
	exposure = kBaseExposure * std::pow( 2.0, ExposureFromParam( params[ PT_EXPOSURE ] ) + target.exposure );
	uniform( compositeShader, "Exposure", static_cast< float >( exposure ) );
	uniform( compositeShader, "StretchQ", static_cast< float >( StretchFromParam( params[ PT_STRETCH ] ) * target.stretch ) );
	uniform( compositeShader, "Saturation", static_cast< float >( SaturationFromParam( params[ PT_SATURATION ] ) ) );
	uniform( compositeShader, "Effect", input != nullptr ? 1 : 0 );
	uniform( compositeShader, "ClipMode", OptionIndex( params[ PT_CLIP_AS ], static_cast< int >( ClipAs::Count ) ) );
	uniform( compositeShader, "Mix", params[ PT_MIX ] );
	double ext[ 3 ] = { 0, 0, 0 }, weight[ 3 ] = { 0, 0, 0 };
	for( int f = 0; f < palette.filterCount; ++f )
		for( int c = 0; c < 3; ++c )
		{
			ext[ c ] += palette.colour[ f ][ c ] * palette.ext[ f ];
			weight[ c ] += palette.colour[ f ][ c ];
		}
	uniform( compositeShader, "ClipExt", static_cast< float >( ext[ 0 ] / std::max( weight[ 0 ], 1e-9 ) ),
	         static_cast< float >( ext[ 1 ] / std::max( weight[ 1 ], 1e-9 ) ), static_cast< float >( ext[ 2 ] / std::max( weight[ 2 ], 1e-9 ) ) );
	if( input != nullptr )
	{
		const double sx = static_cast< double >( input->Width ) / std::max< FFUInt32 >( input->HardwareWidth, 1 );
		const double sy = static_cast< double >( input->Height ) / std::max< FFUInt32 >( input->HardwareHeight, 1 );
		uniform( compositeShader, "ClipMap", static_cast< float >( sx / aspect ), static_cast< float >( sy ), static_cast< float >( 0.5 * sx ),
		         static_cast< float >( 0.5 * sy ) );
	}
	quad.Draw();
	unbindTextureUnits( 12 );
}

//---------------------------------------------------------------------------
void SiderealPlugin::UpdateClock()
{
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;
	const double raw = hostTime;
	// Resolume has been seen sending both seconds and milliseconds through
	// SetTime, and nothing in the call says which. Vote on it against the wall
	// clock for a few frames, then stop asking.
	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;
			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale  = millisVotes > secondsVotes ? 0.001 : 1.0;
				settledJump = true;
			}
		}
	}
	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;
	// Double throughout: Resolume's clock has been measured at ~499 million
	// ms, where a float resolves 0.03 s.
	now = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;
	if( ++clockFrames == 60 )
		diag::info( "host clock at frame 60: raw=" + std::to_string( raw ) + " scale=" + std::to_string( clockScale ) );
}

FFResult SiderealPlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )
{
	if( pgl == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct* input = nullptr;
	if( isEffect )
	{
		if( pgl->numInputTextures < 1 || pgl->inputTextures[ 0 ] == nullptr )
			return FF_FAIL;
		input = pgl->inputTextures[ 0 ];
	}
	ScopedGLState restore;
	const GLint* hostViewport = restore.saved.viewport;
	glDisable( GL_BLEND );
	const int width  = input != nullptr ? static_cast< int >( input->Width ) : hostViewport[ 2 ];
	const int height = input != nullptr ? static_cast< int >( input->Height ) : hostViewport[ 3 ];
	if( width <= 0 || height <= 0 )
		return FF_FAIL;

	UpdateClock();
	if( settledJump )
	{
		lastNow     = -1.0;
		settledJump = false;
	}
	const double dt = lastNow >= 0.0 ? std::clamp( now - lastNow, 0.0, kMaxFrameDelta ) : 0.0;
	lastNow         = now;

	if( !ensureBuffers( width, height ) )
	{
		diag::error( "could not allocate the field's buffers" );
		return FF_FAIL;
	}
	rebuildScene();
	const int telescope = OptionIndex( params[ PT_TELESCOPE ], 1 + physics::kTelescopeChoices );
	rebuildPalette( instrumentOverride >= 0 ? instrumentOverride : ( telescope == 0 ? target.asReleased : telescope - 1 ) );
	updateCamera( dt, width, height );

	EventInputs in;
	in.perMinute     = MeteorsFromParam( params[ PT_METEORS ] );
	in.shower        = OptionIndex( params[ PT_METEOR_KIND ], static_cast< int >( MeteorKind::Count ) ) == static_cast< int >( MeteorKind::Shower );
	in.presses       = meteorPresses;
	in.supernova     = novaWanted;
	in.daysPerSecond = SupernovaSpeedFromParam( params[ PT_SN_SPEED ] );
	in.snX           = target.snX;
	in.snY           = target.snY;
	meteorPresses    = 0;
	novaWanted       = false;
	events.Advance( now, dt, in, static_cast< uint32_t >( sceneKey ) );

	fieldPass( input );
	lightPass();
	spritePass( width, height );
	composite( input, pgl->HostFBO, hostViewport, width, height );
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult SiderealPlugin::DeInitGL()
{
	uniformCache.clear();
	fieldShader.FreeGLResources();
	lightShader.FreeGLResources();
	spriteShader.FreeGLResources();
	compositeShader.FreeGLResources();
	quad.Release();
	for( PassBuffer* b : { &fieldA, &fieldB, &light0, &light1, &light2, &light3, &sprites } )
		b->Destroy();
	for( GLuint* t : { &elementTexture, &polygonTexture, &lineTexture, &continuumTexture, &galaxyTexture, &starTexture, &supernovaTexture,
	                   &spriteTexture } )
	{
		if( *t != 0 )
			glDeleteTextures( 1, t );
		*t = 0;
	}
	if( mrtFBO != 0 )
		glDeleteFramebuffers( 1, &mrtFBO );
	mrtFBO = 0;
	if( spriteVAO != 0 )
		glDeleteVertexArrays( 1, &spriteVAO );
	spriteVAO = 0;
	sceneKey  = -1;
	palette   = physics::Palette {};
	return FF_SUCCESS;
}

FFResult SiderealPlugin::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

char* SiderealPlugin::GetTextParameter( unsigned int index )
{
	if( index < hostOrder.size() && hostOrder[ index ] == PT_ABOUT_TEXT )
	{
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult SiderealPlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index < hostOrder.size() && hostOrder[ index ] == PT_ABOUT_TEXT )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, value );
}

void SiderealPlugin::SetById( unsigned int id, float value )
{
	if( id >= PT_COUNT || idToHost[ id ] < 0 )
		return;
	SetFloatParameter( static_cast< unsigned int >( idToHost[ id ] ), value );
}

FFResult SiderealPlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= hostOrder.size() )
		return FF_FAIL;
	const unsigned int id = hostOrder[ index ];
	if( id >= PT_ABOUT_TEXT )
		return stoatworks::about::handleParam( id - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;
	// Presses act once, on the way down: an event arrives as 1 then 0, and a
	// host is free to restate either.
	const bool down = value >= 0.5f;
	if( id == PT_FIRE_METEOR )
	{
		if( down && !meteorHeld )
			++meteorPresses;
		meteorHeld = down;
	}
	else if( id == PT_SUPERNOVA )
	{
		if( down && !novaHeld )
			novaWanted = true;
		novaHeld = down;
	}
	params[ id ] = value;
	return FF_SUCCESS;
}

float SiderealPlugin::GetFloatParameter( unsigned int index )
{
	return index < hostOrder.size() ? params[ hostOrder[ index ] ] : 0.0f;
}

} // namespace sidereal
