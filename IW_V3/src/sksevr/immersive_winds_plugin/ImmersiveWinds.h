#pragma once
#include "skse64/PapyrusNativeFunctions.h"
#include "TPLinkSPHelper.h"
#include <thread>
#include <map>
#include <ctime>
#include <chrono>

#include <bitset>
#include <random>

class TESWeather;

namespace ImmersiveWinds
{
	const std::string MOD_VERSION = "1.1.1";

	const int DEFAULT_INTERMITTENT_iInsideHouseBase = 0;
	const int DEFAULT_INTERMITTENT_iInsideDungeonBase = 1;
	const int DEFAULT_INTERMITTENT_iInsideCaveBase = 1;
	const int DEFAULT_INTERMITTENT_iInsideIceCaveBase = 2;
	const int DEFAULT_INTERMITTENT_iOutsideBase = 3;
	const int DEFAULT_INTERMITTENT_iOutsideTimeDayMod = 0;
	const int DEFAULT_INTERMITTENT_iOutsideTimeNightMod = 1;
	const int DEFAULT_INTERMITTENT_iOutsideWeatherPleasantMod = 0;
	const int DEFAULT_INTERMITTENT_iOutsideWeatherCloudyMod = 1;
	const int DEFAULT_INTERMITTENT_iOutsideWeatherRainMod = 1;
	const int DEFAULT_INTERMITTENT_iOutsideWeatherSnowMod = 2;
	const int DEFAULT_INTERMITTENT_iOutsideWeatherStormMod = 3;
	const int DEFAULT_INTERMITTENT_iOutsideAltitudeHighMod = 1;
	const int DEFAULT_INTERMITTENT_iOutsideAltitudeVeryHighMod = 2;
	const int DEFAULT_INTERMITTENT_iHighAltitudeBegin = 4000;
	const int DEFAULT_INTERMITTENT_iVeryHighAltitudeBegin = 18000;

	const float DEFAULT_INTERMITTENT_L1_ON = 1;
	const float DEFAULT_INTERMITTENT_L1_OFF = 2.0f;
	const float DEFAULT_INTERMITTENT_L1_RANDOM_ON = 0.7;
	const float DEFAULT_INTERMITTENT_L1_RANDOM_OFF = 3;

	const float DEFAULT_INTERMITTENT_L2_ON = 1.5f;
	const float DEFAULT_INTERMITTENT_L2_OFF = 1.0f;
	const float DEFAULT_INTERMITTENT_L2_RANDOM_ON = 0.5;
	const float DEFAULT_INTERMITTENT_L2_RANDOM_OFF = 3;

	const float DEFAULT_INTERMITTENT_L3_ON = 1.0f;
	const float DEFAULT_INTERMITTENT_L3_OFF = 1.0f;
	const float DEFAULT_INTERMITTENT_L3_RANDOM_ON = 1.5f;
	const float DEFAULT_INTERMITTENT_L3_RANDOM_OFF = 4.0f;

	const float DEFAULT_INTERMITTENT_L4_ON = 2.0f;
	const float DEFAULT_INTERMITTENT_L4_OFF = 1;
	const float DEFAULT_INTERMITTENT_L4_RANDOM_ON = 1.5f;
	const float DEFAULT_INTERMITTENT_L4_RANDOM_OFF = 1.8f;

	const float DEFAULT_INTERMITTENT_L5_ON = 4;
	const float DEFAULT_INTERMITTENT_L5_OFF = 1;
	const float DEFAULT_INTERMITTENT_L5_RANDOM_ON = 0.2f;
	const float DEFAULT_INTERMITTENT_L5_RANDOM_OFF = 0.8f;

	const float DEFAULT_INTERMITTENT_L6_ON = 999999;
	const float DEFAULT_INTERMITTENT_L6_OFF = 0;
	const float DEFAULT_INTERMITTENT_L6_RANDOM_ON = 0;
	const float DEFAULT_INTERMITTENT_L6_RANDOM_OFF = 0;

	const std::vector<std::vector<float>> DEFAULTS_INTERMITTENT = {
		{ 
			DEFAULT_INTERMITTENT_L1_ON,
			DEFAULT_INTERMITTENT_L1_OFF,
			DEFAULT_INTERMITTENT_L1_RANDOM_ON,
			DEFAULT_INTERMITTENT_L1_RANDOM_OFF,
		},
		{
			DEFAULT_INTERMITTENT_L2_ON,
			DEFAULT_INTERMITTENT_L2_OFF,
			DEFAULT_INTERMITTENT_L2_RANDOM_ON,
			DEFAULT_INTERMITTENT_L2_RANDOM_OFF,
		},
		{
			DEFAULT_INTERMITTENT_L3_ON,
			DEFAULT_INTERMITTENT_L3_OFF,
			DEFAULT_INTERMITTENT_L3_RANDOM_ON,
			DEFAULT_INTERMITTENT_L3_RANDOM_OFF,
		},
		{
			DEFAULT_INTERMITTENT_L4_ON,
			DEFAULT_INTERMITTENT_L4_OFF,
			DEFAULT_INTERMITTENT_L4_RANDOM_ON,
			DEFAULT_INTERMITTENT_L4_RANDOM_OFF,
		},
		{
			DEFAULT_INTERMITTENT_L5_ON,
			DEFAULT_INTERMITTENT_L5_OFF,
			DEFAULT_INTERMITTENT_L5_RANDOM_ON,
			DEFAULT_INTERMITTENT_L5_RANDOM_OFF,
		},
		{
			DEFAULT_INTERMITTENT_L6_ON,
			DEFAULT_INTERMITTENT_L6_OFF,
			DEFAULT_INTERMITTENT_L6_RANDOM_ON,
			DEFAULT_INTERMITTENT_L6_RANDOM_OFF,
		}
	};


	struct LevelValues {
		LevelValues() {

		}
		LevelValues(float counterLeaveOnMax, float counterLeaveOffMax, float randomOnAdd, float randomOffAdd) :
			distributionOn(0, randomOnAdd),
			distributionOff(0, randomOffAdd) {
			this->counterLeaveOnMax = counterLeaveOnMax;
			this->counterLeaveOffMax = counterLeaveOffMax;
			this->randomOnAdd = randomOnAdd;
			this->randomOffAdd = randomOffAdd;
		}
		int counterLeaveCurrentState = 0;
		float counterLeaveOnMax = 1;
		float counterLeaveOffMax = 1;
		float randomOffAdd = 0;
		float randomOnAdd = 0;
		float currentRandomOff = 0;
		float currentRandomOn = 0;
		std::uniform_real_distribution<float> distributionOn;
		std::uniform_real_distribution<float> distributionOff;
	};

	static long _shoutLevel = 0;
	static clock_t _shoutBeginTime;
	static bool _keepRunning = true;
	static long _previousSwitchState = 0;
	static long _currentSwitchState = 0;
	static std::map<int, LevelValues> _levelValues;
	static bool _s1Swap = true;
	static clock_t _beginTime;
	static std::default_random_engine _generator;
	static bool _logsEnabled = false;

	// last update values from the engine (papyrus):
	static long _internalCell = 0;
	static float _altitude = 0;
	static long _weatherClass = 0;
	static long _keyWordMatches = 0;
	static float _inGameTime = 0;
	static float _sunglare = 0;
	static long _locationId = 0;
	static long _weatherId = 0;
	static float _windSpeed = 0;

	// user config
	static int iHighAltitudeBegin = 4000;
	static int iVeryHighAltitudeBegin = 18000;
	static int iInsideHouseBase = 0;
	static int iInsideDungeonBase = 1;
	static int iInsideCaveBase = 1;
	static int iInsideIceCaveBase = 2;
	static int iOutsideBase = 2;
	static int iOutsideTimeDayMod = 0;
	static int iOutsideTimeNightMod = 1;
	static int iOutsideWeatherPleasantMod = 0;
	static int iOutsideWeatherCloudyMod = 1;
	static int iOutsideWeatherRainMod = 2;
	static int iOutsideWeatherSnowMod = 2;
	static int iOutsideWeatherStormMod = 3;
	static int iOutsideAltitudeHighMod = 1;
	static int iOutsideAltitudeVeryHighMod = 2;

	std::string GetIniPath();

	void WriteDefaultsToIniFile();

	void UpdateNativeWindStateInternal();

	float GetWindSpeed(TESWeather* thisWeather);

	float SetSwitchState(StaticFunctionTag *base, long bool_param);

	void UpdateNativeWindState(StaticFunctionTag *base, long bool_internalCell, float altitude, long weatherClass, long keyWordMatches, float inGameTime, float sunglare, long locationId, long weatherId, float windSpeed);

	void TriggerShout(StaticFunctionTag *base, long level);
	void EndShout(StaticFunctionTag *base);

	bool RegisterFuncs(VMClassRegistry* registry);

	void InitBgThread();
}
