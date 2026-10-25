#pragma once

#include <cstdint>
#include <vector>

/**
    Things that happen: meteors and a supernova.

    **Meteors** are bodies a few millimetres across burning up a hundred
    kilometres overhead. Each is a straight path at its own speed and entry
    angle; its brightness along the path is the classical single-body ablation
    curve (Physics.h, `MeteorLight`) against the air density, which grows
    e-fold every scale height on the way down -- so a meteor brightens slowly,
    peaks late and goes out abruptly when the body is used up, at three times
    the peak's density. A shower's meteors all travel parallel, so in the
    picture every trail points back to one radiant, and those near it are
    short (they come towards you). Their colour is their speed: slow ones are
    sodium and iron, fast ones magnesium and oxygen; fast bright ones leave a
    green [O I] train.

    Real space telescopes see none of this: they are above the air. The
    plugin's telescope is on a mountain for as long as a meteor lasts.

    **A supernova** is a type Ia: a star at the target's chosen place that
    rises over two and a half weeks to outshine everything near it, fades on
    the decay of nickel and cobalt (Physics.h, `SupernovaLight`), cools from
    blue to orange, and leaves a remnant that grows by free expansion and then
    Sedov-Taylor, and a light echo racing out through any dust nearby.
*/
namespace sidereal
{
struct Meteor
{
	float x = 0, y = 0;  ///< where it is first seen, frame heights
	float dx = 1, dy = 0;///< its direction on the picture
	float length   = 0.5f;///< the trail's length on the picture when it ends, frame heights
	double start   = 0.0; ///< host seconds
	float duration = 1.0f;///< seconds, first seen to burnt out
	float rate     = 1.0f;///< v sin( entry ) / H: ln( density ) grows this fast, 1/s
	float peak     = 1.0f;///< its brightness at peak, the frame's flux units
	float fast     = 0.5f;///< 0 slow (Na, Fe) .. 1 fast (Mg, O)
	float train    = 0.0f;///< the persistent train's share of the head's light
	float trainLife = 0.5f;///< seconds
	float speed    = 40.0f;///< km/s, for the record
};

struct Supernova
{
	bool active = false;
	float x = 0, y = 0;     ///< frame heights
	double days     = 0.0;  ///< of the light curve
	float flux      = 0.0f; ///< now, the frame's flux units
	float temperature = 10000.0f;
	float remnantRadius = 0.0f;///< frame heights
	float remnantGlow   = 0.0f;
	float echoCt        = 0.0f;///< light travel since the peak, frame heights
};

struct EventInputs
{
	double perMinute = 0.0;
	bool shower      = true;
	int presses      = 0;///< Fire Meteor
	bool supernova   = false;
	double daysPerSecond = 20.0;
	float snX = 0.0f, snY = 0.0f;
	float radiantX = -1.3f, radiantY = 0.9f;
};

class Events
{
public:
	/// The supernova's scales, in the picture.
	static constexpr float kPeakFlux      = 600.0f;
	static constexpr float kSedovRadius   = 0.07f;///< frame heights at the Sedov-Taylor time
	static constexpr double kSedovDays    = 60.0; ///< light-curve days to it: the remnant runs on a compressed clock
	static constexpr double kLightPerDay  = 0.0035;///< frame heights light crosses in a day, for the echo
	static constexpr double kFadeDays     = 3000.0;

	void Advance( double now, double dt, const EventInputs& in, uint32_t seed );
	void Reset();

	const std::vector< Meteor >& Meteors() const
	{
		return meteors;
	}
	const Supernova& Nova() const
	{
		return nova;
	}

	/// For the harness: put one meteor in the sky now, exactly.
	void AddForTest( const Meteor& m )
	{
		meteors.push_back( m );
	}
	/// The record of every meteor launched, for --meteor's radiant check.
	const std::vector< Meteor >& Launched() const
	{
		return launched;
	}

private:
	Meteor launch( double now, const EventInputs& in );

	std::vector< Meteor > meteors, launched;
	Supernova nova;
	double novaStart = -1.0;
	uint64_t rng     = 0x9e3779b97f4a7c15ull;
	bool seeded      = false;
	double carry     = 0.0;
	float uniform();
};

} // namespace sidereal
