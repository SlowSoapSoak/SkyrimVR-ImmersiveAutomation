#pragma once
#include <thread>
#include <map>
#include <ctime>
#include <chrono>
#include <bitset>
#include <random>
#include <shlobj.h>
#include <intrin.h>
#include <vector>
#include <iostream>
#include <fstream> 
#include <stdio.h>
#include <shlobj.h>
#include <intrin.h>
#include <string>
#include "SPScanner.h"
#include "TPLinkSPHelper.h"
#include "skse64/PapyrusNativeFunctions.h"
#include "skse64_common/SafeWrite.h"
#include "skse64/GameForms.h"
#include "skse64/GameData.h"
#include "skse64/GameRTTI.h"
#include "skse64/GameMenus.h"
#include "skse64/PapyrusEvents.h"
#include "MenuChecker.h"
#include "Sky.h"
#include <atomic>

class TESWeather;

namespace ImmersiveWinds
{
	const std::string MOD_VERSION = "1.2.0";

	typedef void(*_ActorAddShoutNative)(Actor *actor, TESShout* shout);
	static RelocAddr<_ActorAddShoutNative> ActorAddShoutNative(0x00638750);

	typedef void(*_TeachWordNative)(VMClassRegistry* registry, UInt32 stackId, void*, TESWordOfPower* word);
	static RelocAddr<_TeachWordNative> TeachWordNative(0x009AD920);

	typedef void(*_UnlockWordNative)(VMClassRegistry* registry, UInt32 stackId, void*, TESWordOfPower* word);
	static RelocAddr<_UnlockWordNative> UnlockWordNative(0x009AD9B0);

	typedef bool(*_PlayerKnowsNative)(VMClassRegistry* registry, UInt32 stackId, TESForm* form);
	static RelocAddr<_PlayerKnowsNative> PlayerKnowsNative(0x09C2F50);


	static Sky ** g_SkyPtr = RelocPtr<Sky *>(0x02FC62C8);
	static std::vector<UInt32> notExteriorWorlds = { 0x69857, 0x1EE62, 0x20DCB, 0x1FAE2, 0x34240, 0x50015, 0x2C965, 0x29AB7, 0x4F838, 0x3A9D6, 0x243DE, 0xC97EB, 0xC350D, 0x1CDD3, 0x1CDD9, 0x21EDB, 0x1E49D, 0x2B101, 0x2A9D8, 0x20BFE };

	static UInt32 funcAddress = 0x0127B460;

	static RelocAddr<uintptr_t *> addressStart = funcAddress + 0x38D;
	static RelocAddr<uintptr_t *> addressEnd = funcAddress + 0x3BF;
	
	void Log(const int msgLogLevel, const char * fmt, ...);

	enum eLogLevels
	{
		LOGLEVEL_ERR = 0,
		LOGLEVEL_WARN,
		LOGLEVEL_INFO,
	};

	#define LOG(fmt, ...) Log(LOGLEVEL_WARN, fmt, ##__VA_ARGS__)
	#define LOG_ERR(fmt, ...) Log(LOGLEVEL_ERR, fmt, ##__VA_ARGS__)
	#define LOG_INFO(fmt, ...) Log(LOGLEVEL_INFO, fmt, ##__VA_ARGS__)
	
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

	static std::string pluginName = "ImmersiveWindsVR.esp";

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
	static bool _keepRunning = true;
	static long _previousSwitchState = 0;
	static long _currentSwitchState = 0;
	static std::map<int, LevelValues> _levelValues;
	static bool _s1Swap = true;
	static clock_t _beginTime;
	static std::default_random_engine _generator;
	static bool _logsEnabled = false;
	static std::string workingIp;
	static int useUDP= -1;

	static std::atomic<bool> interiorCell = false;
	static std::atomic<bool> teach = false;
	
	// last update values from the engine (papyrus):
	static bool _internalCell = false;
	static float _altitude = 0;
	static long _weatherClass = 0;
	static BGSLocation* _currentLocation;
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

	//Shout FormId
	static UInt32 ImmersiveWindsCallShoutFormId = 0xC15C;
	static UInt32 ImmersiveWindsCall1FormId = 0xC157;
	static UInt32 ImmersiveWindsCall2FormId = 0xC15B;
	static UInt32 WordRazenFormId = 0xC160;
	static UInt32 WordGanFormId = 0xC161;
	static UInt32 ImmersiveWindsCallEffect1FormId = 0xC15E;
	static UInt32 ImmersiveWindsCallEffect2FormId = 0xC15F;

	static TESShout* ImmersiveWindsCallShout;
	static TESWordOfPower* WordRazen;
	static TESWordOfPower* WordGan;

	//Level Keywords
	static UInt32 keywordIdLocSetCave = 0x0130EF;
	static UInt32 keywordIdLocSetCaveIce = 0x0100819;
	static UInt32 keywordIdLocSetDwarvenRuin = 0x0130F0;
	static UInt32 keywordIdLocSetNordicRuin = 0x0130F2;
	static UInt32 keywordIdLocSetOutdoor = 0x0130F3;
	static UInt32 keywordIdLocTypeDungeon = 0x0130DB;
	static UInt32 keywordIdLocTypeCity = 0x013168;
	static UInt32 keywordIdLocTypeDragonPriestLair = 0x0130E1;
	static UInt32 keywordIdLocTypeDragonLair = 0x0130E0;
	static UInt32 keywordIdLocTypeDraugrCrypt = 0x0130E2;
	static UInt32 keywordIdLocTypeFalmerHive = 0x0130E4;
	static UInt32 keywordIdLocTypeMine = 0x018EF1;
	static UInt32 keywordIdLocTypeTemple = 0x01CD56;
	static UInt32 keywordIdLocTypeDwelling = 0x0130DC;
	static UInt32 keywordIdLocTypeFarm = 0x018EF0;
	static UInt32 keywordIdLocTypeGuild = 0x01CD5A;
	static UInt32 keywordIdLocTypeHold = 0x016771;
	static UInt32 keywordIdLocTypeHoldCapital = 0x0868E2;
	static UInt32 keywordIdLocTypeHoldMajor = 0x0868E1;
	static UInt32 keywordIdLocTypeHoldMinor = 0x0868E3;
	static UInt32 keywordIdLocTypeInn = 0x01CB87;
	static UInt32 keywordIdLocTypeJail = 0x01CD59;
	static UInt32 keywordIdLocTypeLumberMill = 0x018EF2;
	static UInt32 keywordIdLocTypeShip = 0x01CD5B;
	static UInt32 keywordIdLocTypeShipwreck = 0x01929F;
	static UInt32 keywordIdLocTypeStewardsDwelling = 0x0504F9;
	static UInt32 keywordIdLocTypeStore = 0x01CB86;
	static UInt32 keywordIdLocTypePlayerHouse = 0x0FC1A3;
	static UInt32 keywordIdLocTypeHouse = 0x01CB85;
	static UInt32 keywordIdLocTypeCastle = 0x01CD57;
	
	static BGSKeyword * keywordLocSetCave;
	static BGSKeyword * keywordLocSetCaveIce;
	static BGSKeyword * keywordLocSetDwarvenRuin;
	static BGSKeyword * keywordLocSetNordicRuin;
	static BGSKeyword * keywordLocSetOutdoor;
	static BGSKeyword * keywordLocTypeDungeon;
	static BGSKeyword * keywordLocTypeCity;
	static BGSKeyword * keywordLocTypeDragonPriestLair;
	static BGSKeyword * keywordLocTypeDragonLair;
	static BGSKeyword * keywordLocTypeDraugrCrypt;
	static BGSKeyword * keywordLocTypeFalmerHive;
	static BGSKeyword * keywordLocTypeMine;
	static BGSKeyword * keywordLocTypeTemple;
	static BGSKeyword * keywordLocTypeDwelling;
	static BGSKeyword * keywordLocTypeFarm;
	static BGSKeyword * keywordLocTypeGuild;
	static BGSKeyword * keywordLocTypeHold;
	static BGSKeyword * keywordLocTypeHoldCapital;
	static BGSKeyword * keywordLocTypeHoldMajor;
	static BGSKeyword * keywordLocTypeHoldMinor;
	static BGSKeyword * keywordLocTypeInn;
	static BGSKeyword * keywordLocTypeJail;
	static BGSKeyword * keywordLocTypeLumberMill;
	static BGSKeyword * keywordLocTypeShip;
	static BGSKeyword * keywordLocTypeShipwreck;
	static BGSKeyword * keywordLocTypeStewardsDwelling;
	static BGSKeyword * keywordLocTypeStore;
	static BGSKeyword * keywordLocTypePlayerHouse;
	static BGSKeyword * keywordLocTypeHouse;
	static BGSKeyword * keywordLocTypeCastle;

	std::string GetIniPath();

	void WriteDefaultsToIniFile();
	void ResetTeach();

	void UpdateNativeWindStateInternal();

	float GetWindSpeed(TESWeather* thisWeather);

	void UpdateNativeWindState(bool bool_internalCell, float altitude, long weatherClass, BGSLocation* curLocation, float inGameTime, float sunglare, long locationId, long weatherId, float windSpeed);

	void TriggerShout(long level);

	void InitBgThread();	
	
	void StartMod();

}
