#pragma once
#include <random>

namespace ImmersiveWinds
{
	// [SimulatedWind] settings, see the README for their meaning
	struct WindSimSettings
	{
		float turbulence = 0.25f;         // strength of the random variation, share of the base wind
		float turbulenceSeconds = 4.0f;   // how long a variation lasts on average
		float gustsPerMinute = 3.0f;
		float gustMinSeconds = 3.0f;
		float gustMaxSeconds = 8.0f;
		float gustStrength = 0.6f;        // strongest gust, share of the base wind
		float lullShare = 0.25f;          // share of the gusts that are lulls instead
		float fanSpinUpSeconds = 1.5f;    // time constant of the fan when switched on
		float fanSpinDownSeconds = 4.0f;  // time constant of the fan when switched off
		float hysteresis = 0.05f;
		float minOnSeconds = 1.0f;
		float minOffSeconds = 1.0f;
	};

	// Simulates gusty wind and turns it into on/off switching of a fan. Pure computation without game or
	// network calls, so it can be tested outside the game.
	//
	// target wind = base wind * (1 + turbulence) + gust/lull + motion wind (all 0-1)
	// The fan speed is estimated with its spin up and spin down times. The relay switches on when the
	// estimate falls below the target and off when it rises above it, so the fan follows the target.
	class WindSimulation
	{
	public:
		WindSimulation(const WindSimSettings & settings, unsigned seed);

		// Advances by dt seconds. baseWind: wind of weather and location, motionWind: wind of the player's
		// own movement. Returns the wanted relay state.
		bool Update(float dt, float baseWind, float motionWind);

		float Target() const { return _target; }
		float FanSpeed() const { return _fanSpeed; }
		bool RelayOn() const { return _relayOn; }

	private:
		float Gust(float dt);

		WindSimSettings _settings;
		std::mt19937 _random;
		std::normal_distribution<float> _normal{ 0.0f, 1.0f };
		std::uniform_real_distribution<float> _uniform{ 0.0f, 1.0f };

		float _base = 0.0f;        // smoothed base wind
		float _motion = 0.0f;      // smoothed motion wind
		float _turbulence = 0.0f;  // mean reverting noise, about -1..1

		float _gustTime = 0.0f;    // time since the gust started
		float _gustLength = 0.0f;  // 0: no gust running
		float _gustAmount = 0.0f;  // share of the base wind, negative for a lull
		float _nextGust = 0.0f;    // time until the next gust starts

		float _target = 0.0f;
		float _fanSpeed = 0.0f;
		bool _relayOn = false;
		float _stateTime = 0.0f;   // time in the current relay state
	};
}
