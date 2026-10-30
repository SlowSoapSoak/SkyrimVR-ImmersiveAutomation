#include "WindSimulation.h"
#include <algorithm>
#include <cmath>

namespace ImmersiveWinds
{
	namespace
	{
		const float PI = 3.14159265f;
		// changes of the base wind (weather transition, door) are faded in over about this time
		const float BASE_SMOOTHING_SECONDS = 1.0f;
		const float MOTION_SMOOTHING_SECONDS = 0.5f;
		// below this target the fan stays off, above the upper one it stays on
		const float TARGET_OFF = 0.02f;
		const float TARGET_ON = 0.98f;

		// share of the way to the new value after dt, for a time constant of seconds
		float Approach(float dt, float seconds)
		{
			return seconds > 0.0f ? 1.0f - std::exp(-dt / seconds) : 1.0f;
		}
	}

	WindSimulation::WindSimulation(const WindSimSettings & settings, unsigned seed) :
		_settings(settings),
		_random(seed)
	{
		if (_settings.gustsPerMinute > 0.0f) {
			_nextGust = std::exponential_distribution<float>(_settings.gustsPerMinute / 60.0f)(_random);
		}
	}

	float WindSimulation::Gust(float dt)
	{
		if (_gustLength <= 0.0f) {
			if (_settings.gustsPerMinute <= 0.0f || _base < TARGET_OFF) {
				return 0.0f;
			}
			_nextGust -= dt;
			if (_nextGust > 0.0f) {
				return 0.0f;
			}
			// a new gust or lull
			const float minSeconds = std::max(0.5f, _settings.gustMinSeconds);
			_gustLength = minSeconds + _uniform(_random) * std::max(0.0f, _settings.gustMaxSeconds - minSeconds);
			_gustTime = 0.0f;
			if (_uniform(_random) < _settings.lullShare) {
				_gustAmount = -(0.3f + 0.3f * _uniform(_random));
			}
			else {
				_gustAmount = _settings.gustStrength * (0.3f + 0.7f * _uniform(_random));
			}
		}

		_gustTime += dt;
		if (_gustTime >= _gustLength) {
			_gustLength = 0.0f;
			_nextGust = std::exponential_distribution<float>(_settings.gustsPerMinute / 60.0f)(_random);
			return 0.0f;
		}
		// rises and falls softly (1 - cos shape)
		return _base * _gustAmount * 0.5f * (1.0f - std::cos(2.0f * PI * _gustTime / _gustLength));
	}

	bool WindSimulation::Update(float dt, float baseWind, float motionWind)
	{
		dt = std::clamp(dt, 0.0f, 1.0f);

		if (baseWind <= 0.0f && motionWind <= 0.0f) {
			// no wind at all (house, menu, game stopped): off right away instead of fading out
			_base = 0.0f;
			_motion = 0.0f;
			_gustLength = 0.0f;
			_target = 0.0f;
		}
		else {
			_base += (baseWind - _base) * Approach(dt, BASE_SMOOTHING_SECONDS);
			_motion += (motionWind - _motion) * Approach(dt, MOTION_SMOOTHING_SECONDS);

			// Ornstein-Uhlenbeck process: random, but always drawn back to 0, variance 1
			const float tau = std::max(0.1f, _settings.turbulenceSeconds);
			_turbulence += -_turbulence * dt / tau + std::sqrt(2.0f * dt / tau) * _normal(_random);

			const float gust = Gust(dt);
			if (baseWind >= 1.0f) {
				_target = 1.0f; // full wind (strongest shout, storm at the limit)
			}
			else {
				_target = std::clamp(_base * (1.0f + _settings.turbulence * _turbulence) + gust + _motion, 0.0f, 1.0f);
			}
		}

		// estimated fan speed, it approaches full speed while on and stands still while off
		if (_relayOn) {
			_fanSpeed += (1.0f - _fanSpeed) * Approach(dt, _settings.fanSpinUpSeconds);
		}
		else {
			_fanSpeed -= _fanSpeed * Approach(dt, _settings.fanSpinDownSeconds);
		}

		_stateTime += dt;
		bool on = _relayOn;
		if (_target <= TARGET_OFF) {
			on = false;
		}
		else if (_target >= TARGET_ON) {
			on = true;
		}
		else if (_relayOn) {
			on = !(_fanSpeed > _target + _settings.hysteresis && _stateTime >= _settings.minOnSeconds);
		}
		else {
			on = _fanSpeed < _target - _settings.hysteresis && _stateTime >= _settings.minOffSeconds;
		}

		if (on != _relayOn) {
			_relayOn = on;
			_stateTime = 0.0f;
		}
		return _relayOn;
	}
}
