#pragma once

#include "Controls.h"
#include "Events.h"
#include "PassBuffer.h"
#include "Physics.h"
#include "Scene.h"

#include <FFGLSDK.h>

// After FFGLSDK.h, which is where FFUInt32 comes from.
#include "StoatworksAboutParams.h"

#include <array>
#include <vector>

namespace sidereal
{
/**
    The plugin: the source (`SW Sidereal`, the sky on black) and, with
    `isEffect`, the Over (`SW Sidereal Over`, the sky over the clip, or the
    clip made into gas). One class, two registrations; see SourcePlugin.cpp
    and EffectPlugin.cpp.

    Host indices are NOT ParamIds: each plugin declares its own dense list,
    `HostOrder( isEffect )`. The host-facing calls translate.
*/
class SiderealPlugin : public CFFGLPlugin
{
public:
	explicit SiderealPlugin( bool isEffect );

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* input ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetTextParameter( unsigned int index ) override;
	/// The base class's stub fails, and a failed default deletes the instance
	/// -- so without this no real host can load the plugin.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	FFResult SetTime( double time ) override;

	bool IsEffect() const
	{
		return isEffect;
	}
	int HostIndexOf( unsigned int id ) const
	{
		return id < PT_COUNT ? idToHost[ id ] : -1;
	}
	/// Set a control by what it is, whatever its host index.
	void SetById( unsigned int id, float value );
	float GetById( unsigned int id ) const
	{
		return id < PT_COUNT ? params[ id ] : 0.0f;
	}

	//-------------------------------------------------------------------
	// For the harness. Nothing in the plugin's own operation calls these.
	//-------------------------------------------------------------------
	void SetClockScaleForTest( double scale )
	{
		clockScale = scale;
	}
	/// shaders::Hook bits.
	void SetHooksForTest( int bits )
	{
		hooks = bits;
	}
	/// Replace the target with this one (rebuilt into textures next frame).
	void SetTargetForTest( const scene::Target& target )
	{
		testTarget    = target;
		hasTestTarget = true;
		sceneKey      = -1;
	}
	void ClearTargetForTest()
	{
		hasTestTarget = false;
		sceneKey      = -1;
	}
	/// The instrument by id, whatever the Telescope control says.
	void SetInstrumentForTest( int id )
	{
		instrumentOverride = id;
	}
	Events& EventsForTest()
	{
		return events;
	}
	const scene::Target& CurrentTarget() const
	{
		return target;
	}
	const physics::Palette& CurrentPalette() const
	{
		return palette;
	}
	int CurrentInstrument() const
	{
		return palette.instrument;
	}
	GLuint FieldATextureID() const
	{
		return fieldA.TextureID();
	}
	GLuint FieldBTextureID() const
	{
		return fieldB.TextureID();
	}
	GLuint Light0TextureID() const
	{
		return light0.TextureID();
	}
	GLuint SpritesTextureID() const
	{
		return sprites.TextureID();
	}
	int FieldWidth() const
	{
		return fieldWidth;
	}
	int FieldHeight() const
	{
		return fieldHeight;
	}
	/// The buffer's extent in frame heights (it spans the screen plus a margin).
	double FieldExtentX() const
	{
		return fieldExtent[ 0 ];
	}
	double FieldExtentY() const
	{
		return fieldExtent[ 1 ];
	}
	double HostNow() const
	{
		return now;
	}
	/// What a frame's star PSF and spikes came to, px.
	double CoreSigmaPx() const
	{
		return coreSigmaPx;
	}
	double SeeingSigmaPx() const
	{
		return seeingSigmaPx;
	}
	double SpikeLengthPx() const
	{
		return spikeLengthPx;
	}
	/// Screen position (px, GL's y up) of a scene point at a depth, as the
	/// last frame's camera had it.
	std::array< double, 2 > ScreenPx( double x, double y, double depth ) const;
	/// The display exposure the composite multiplies by.
	double DisplayExposure() const
	{
		return exposure;
	}
	/// The ionising photon fraction of a blackbody, relative to 40 kK's.
	static double IonisingShare( double kelvin );

	/// The shipped constants of the light and the emission, for the harness's
	/// independent arithmetic.
	struct Constants
	{
		double ionK, hazeIon, kO, kS, kP, kV, frontHaze, relief, faceShadow, faceGain, hazeShadow, molSkin, phaseG, selfFrac, hazeDust;
		double emitH, emitO, emitS, emitPah, emitDust, emitScat, emitShock, emitSync, emitJets, emitOxy;
		double dustFloor, dustRef, margin, fieldScale, haloFrac, haloWidth;
	};
	static const Constants& Shipped();

private:
	void UpdateClock();
	bool ensureBuffers( int width, int height );
	void rebuildScene();
	void rebuildPalette( int instrument );
	void updateCamera( double dt, int width, int height );
	void fieldPass( const FFGLTextureStruct* input );
	void lightPass();
	void spritePass( int width, int height );
	void composite( const FFGLTextureStruct* input, GLuint hostFBO, const GLint* viewport, int width, int height );

	GLint where( const ffglex::FFGLShader& shader, const char* name );
	void uniform( const ffglex::FFGLShader& shader, const char* name, int v );
	void uniform( const ffglex::FFGLShader& shader, const char* name, float v );
	void uniform( const ffglex::FFGLShader& shader, const char* name, float a, float b );
	void uniform( const ffglex::FFGLShader& shader, const char* name, float a, float b, float c );
	void uniform( const ffglex::FFGLShader& shader, const char* name, float a, float b, float c, float d );
	void cameraUniforms( const ffglex::FFGLShader& shader );
	std::vector< std::pair< std::pair< GLuint, const char* >, GLint > > uniformCache;

	const bool isEffect;
	const std::vector< unsigned int >& hostOrder;
	int idToHost[ PT_COUNT ];
	float params[ PT_COUNT ] = {};

	ffglex::FFGLShader fieldShader, lightShader, spriteShader, compositeShader;
	ffglex::FFGLScreenQuad quad;
	GLuint spriteVAO = 0;
	GLuint mrtFBO    = 0;

	PassBuffer fieldA, fieldB, light0, light1, light2, light3, sprites;
	int fieldWidth = 0, fieldHeight = 0;
	double fieldExtent[ 2 ] = { 1.0, 1.0 };
	int hostWidth = 0, hostHeight = 0;

	// The target, as textures.
	scene::Target target, testTarget;
	bool hasTestTarget = false;
	int sceneKey       = -1;
	int paletteScene   = -2;
	GLuint elementTexture = 0, polygonTexture = 0;
	int elementCount      = 0;
	std::vector< float > sourceA, sourceB;
	int sourceCount = 0;

	// The telescope, as tables.
	physics::Palette palette;
	int instrumentOverride = -1;
	GLuint lineTexture = 0, continuumTexture = 0, galaxyTexture = 0, starTexture = 0, supernovaTexture = 0;
	GLuint spriteTexture = 0;
	int spriteCount      = 0;

	// The camera, this frame.
	float cam[ 4 ]     = { 1, 0, 0, 1 };///< column-major mat2: screen -> scene
	float camPan[ 2 ]  = { 0, 0 };
	float camDrift[ 2 ] = { 0, 0 };
	double zoom = 1.0, spin = 0.0;
	double driftPhase = 0.0, flowTime = 0.0, flyPhase = 0.0;
	double exposure = 1.0;
	double starGain = 900.0;
	double continuumNorm = 1.0, scatterNorm = 1.0, deepNorm = 1.0, meteorNorm = 1.0;
	double coreSigmaPx = 1.0, seeingSigmaPx = 0.5, spikeLengthPx = 60.0;

	// Time (the clock unit vote: rosette's, by way of ferrofluid).
	double hostTime = -1.0, lastRawTime = -1.0, lastWallTime = -1.0, wallStart = -1.0;
	double clockScale = 0.0;
	int secondsVotes = 0, millisVotes = 0, clockFrames = 0;
	double now = 0.0, lastNow = -1.0;
	bool settledJump = false;

	// Presses.
	int meteorPresses = 0;
	bool meteorHeld   = false;
	bool novaWanted   = false;
	bool novaHeld     = false;

	Events events;
	int hooks = 0;
};

} // namespace sidereal
