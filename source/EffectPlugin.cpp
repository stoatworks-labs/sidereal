#include "Sidereal.h"

/**
    The effect: the sky over the clip -- the clip is what lies behind the
    nebula, dimmed and reddened by its dust -- or the clip made into the gas,
    so the stars light your picture as they lit the Pillars.

    See SourcePlugin.cpp for why this file is listed in its own target.
    `SW Sidereal Over` is exactly sixteen characters, the FFGL name field's
    whole width; `oxbow probe` reads it back to the last letter.
*/
namespace
{
class SiderealEffect : public sidereal::SiderealPlugin
{
public:
	SiderealEffect() :
		SiderealPlugin( true )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< SiderealEffect >,// Create method
	"SD02",                         // Plugin unique ID of maximum length 4
	"SW Sidereal Over",             // Plugin name
	2,                              // API major version number
	1,                              // API minor version number
	0,                              // Plugin major version number
	1,                              // Plugin minor version number
	FF_EFFECT,                      // Plugin type
	"The space telescopes' sky over the clip: the clip lies behind the nebula and its dust dims and reddens it, or the clip "
	"becomes the gas and the stars light it, ionising its skin, as they lit the Pillars of Creation.",
	"Sidereal FFGL effect"// About
);

extern "C" const char* SiderealEffectBuildStamp()
{
	return "sidereal " SIDEREAL_VERSION " effect, built " __DATE__ " " __TIME__;
}
