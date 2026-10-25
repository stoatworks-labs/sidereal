#include "Sidereal.h"

/**
    The source: a piece of sky on black, through a telescope.

    Listed directly in the SiderealSource target, not in sidereal_core: both
    plugins share the class and not the `CFFGLPluginInfo` below, and putting
    either registration in the shared library would register both plugins
    into both bundles. The core is an OBJECT library because this registers
    itself from a file-scope constructor nothing references.

    `SW Sidereal` is eleven characters; the FFGL name field is char[ 16 ] and
    not null-terminated. `oxbow probe` reads it back the way a host does.
*/
namespace
{
class SiderealSource : public sidereal::SiderealPlugin
{
public:
	SiderealSource() :
		SiderealPlugin( false )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< SiderealSource >,// Create method
	"SD01",                         // Plugin unique ID of maximum length 4
	"SW Sidereal",                  // Plugin name
	2,                              // API major version number
	1,                              // API minor version number
	0,                              // Plugin major version number
	1,                              // Plugin minor version number
	FF_SOURCE,                      // Plugin type
	"Nebulae, galaxies and starfields built the way the space telescopes' pictures are: gas lit by its stars (each ionising "
	"photon a recombination, oxygen nearest the star, sulphur at the front), dust that dims and reddens, through Hubble's or "
	"Webb's real filters and colour keys, with their diffraction spikes. Famous targets traced from the releases, generative "
	"ones from a seed, meteors and a type Ia supernova on demand.",
	"Sidereal FFGL source"// About
);

extern "C" const char* SiderealSourceBuildStamp()
{
	return "sidereal " SIDEREAL_VERSION " source, built " __DATE__ " " __TIME__;
}
