#include "ImmersiveWinds.h"
#include "HttpSwitch.h"
#include "PlugFinder.h"
#include "EndGameDetect.h"
#include "WindSimulation.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <vector>


namespace ImmersiveWinds {

	namespace
	{
		std::atomic<bool> _keepRunning(true);
		// the wind level 0-6, written by the game state check and the menu handling, read by the switch thread
		std::atomic<long> _currentSwitchState(0);

		using UInt32 = std::uint32_t;
		using RE::BGSKeyword;
		using RE::BGSLocation;
		using RE::TESShout;
		using RE::TESWeather;
		using RE::TESWordOfPower;

		const float WEATHER_CHECK_INTERVAL_SECONDS = 3.0f;
		// simulated wind: the player's movement is read this often, the rest of the game state every WEATHER_CHECK_INTERVAL_SECONDS
		const float MOTION_CHECK_INTERVAL_SECONDS = 0.25f;
		const int SIMULATION_STEP_MS = 50;
		// simulated wind of the first shout level (the second one is full wind)
		const float SHOUT_WIND = 0.85f;
		// faster position changes are teleports (doors, fast travel), not movement (units per second)
		const float MAX_MOVEMENT_SPEED = 4000.0f;
		const int FOCUS_CHECK_INTERVAL_MS = 250;
		// the relay state is sent again after this time, in case a (udp) message got lost or the plug restarted
		const float RELAY_RESEND_INTERVAL_SECONDS = 15.0f;
		// a queued weather check that did not run after this time means the game stands still (frozen, long loading screen)
		const float GAME_STALLED_SECONDS = 10.0f;
		// an on or off time this long keeps the plug in one state
		const float ALWAYS = 999999.0f;
		const int MAX_LEVEL = 6;

		// default name of the TP-Link plug in the Kasa app, [GeneralModConfig] sPlugName
		const char * DEFAULT_PLUG_NAME = "ImmersiveWindsPlug";
		const char * PLUGIN_NAME = "ImmersiveWindsVR.esp";

		// ini sections (names kept for compatibility with existing ini files)
		const char * INI_INTERNAL = "Internal";
		const char * INI_GENERAL = "GeneralModConfig";
		const char * INI_STRENGTH = "IntermittendModeStrengthConfig";
		const char * INI_LEVELS = "IntermittendModeLevelConfig";
		const char * INI_HTTP = "HttpSwitch";
		const char * INI_SIM = "SimulatedWind";

		// ---- settings ----

		bool _logsEnabled = false;
		bool _noWindInMenu = false;
		bool _intermittentModeEnabled = true;
		// iWindMode 1: simulated wind (WindSimulation) instead of the levels
		bool _simulatedWind = false;
		bool _writeSimCsv = false;

		// sSwitchType: "tplink" (TP-Link Kasa plug, found by a network scan) or "http" (sOnUrl / sOffUrl)
		bool _useHttpSwitch = false;
		std::string _plugName = DEFAULT_PLUG_NAME;
		std::string _httpMethod = "GET";
		std::string _httpOnUrl;
		std::string _httpOffUrl;

		// level config, set by LoadSettings
		int iHighAltitudeBegin = 0;
		int iVeryHighAltitudeBegin = 0;
		int iInsideHouseBase = 0;
		int iInsideDungeonBase = 0;
		int iInsideCaveBase = 0;
		int iInsideIceCaveBase = 0;
		int iOutsideBase = 0;
		int iOutsideTimeDayMod = 0;
		int iOutsideTimeNightMod = 0;
		int iOutsideWeatherPleasantMod = 0;
		int iOutsideWeatherCloudyMod = 0;
		int iOutsideWeatherRainMod = 0;
		int iOutsideWeatherSnowMod = 0;
		int iOutsideWeatherStormMod = 0;
		int iOutsideAltitudeHighMod = 0;
		int iOutsideAltitudeVeryHighMod = 0;
		int bAutoStormWeathers = 0;
		int bShelterDetection = 0;
		int iShelteredBase = 0;
		int iShelterMinWalls = 0;
		int iShelterWallDistance = 0;
		int iShelterRoofHeight = 0;

		struct IntSetting
		{
			const char * section;
			const char * key;
			int defaultValue;
			int * value; // nullptr: only written as default, read elsewhere
		};

		// the single place for names and default values of the integer settings
		const IntSetting GENERAL_SETTINGS[] = {
			{ INI_GENERAL, "bUseUdp", 1, nullptr }, // read by TPLinkHelper
			{ INI_GENERAL, "bIntermittentModeEnabled", 1, nullptr },
			{ INI_GENERAL, "bLoggingEnabled", 0, nullptr },
			{ INI_GENERAL, "bNoWindInMenu", 0, nullptr },
			{ INI_GENERAL, "iWindMode", 0, nullptr },
			{ INI_SIM, "bWriteCsv", 0, nullptr },
		};

		// simulated wind settings, set by LoadSettings
		WindSimSettings _simSettings;
		float _gameWindScale = 1.0f;
		float _outsideMinWind = 0.1f;
		float _motionStartSpeed = 150.0f;
		float _motionFullSpeed = 1000.0f;
		float _motionWindMax = 0.6f;

		struct FloatSetting
		{
			const char * key;
			float defaultValue;
			float * value;
		};

		// [SimulatedWind], defaults as in WindSimSettings
		const FloatSetting SIM_SETTINGS[] = {
			{ "fGameWindScale", 1.0f, &_gameWindScale },
			{ "fOutsideMinWind", 0.1f, &_outsideMinWind },
			{ "fTurbulence", 0.25f, &_simSettings.turbulence },
			{ "fTurbulenceSeconds", 4.0f, &_simSettings.turbulenceSeconds },
			{ "fGustsPerMinute", 3.0f, &_simSettings.gustsPerMinute },
			{ "fGustMinSeconds", 3.0f, &_simSettings.gustMinSeconds },
			{ "fGustMaxSeconds", 8.0f, &_simSettings.gustMaxSeconds },
			{ "fGustStrength", 0.6f, &_simSettings.gustStrength },
			{ "fLullShare", 0.25f, &_simSettings.lullShare },
			{ "fMotionStartSpeed", 150.0f, &_motionStartSpeed },
			{ "fMotionFullSpeed", 1000.0f, &_motionFullSpeed },
			{ "fMotionWindMax", 0.6f, &_motionWindMax },
			{ "fFanSpinUpSeconds", 1.5f, &_simSettings.fanSpinUpSeconds },
			{ "fFanSpinDownSeconds", 4.0f, &_simSettings.fanSpinDownSeconds },
			{ "fHysteresis", 0.05f, &_simSettings.hysteresis },
			{ "fMinOnSeconds", 1.0f, &_simSettings.minOnSeconds },
			{ "fMinOffSeconds", 1.0f, &_simSettings.minOffSeconds },
		};

		const IntSetting LEVEL_SETTINGS[] = {
			{ INI_LEVELS, "iInsideHouseBase", 0, &iInsideHouseBase },
			{ INI_LEVELS, "iInsideDungeonBase", 1, &iInsideDungeonBase },
			{ INI_LEVELS, "iInsideCaveBase", 1, &iInsideCaveBase },
			{ INI_LEVELS, "iInsideIceCaveBase", 2, &iInsideIceCaveBase },
			{ INI_LEVELS, "iOutsideBase", 3, &iOutsideBase },
			{ INI_LEVELS, "iOutsideTimeDayMod", 0, &iOutsideTimeDayMod },
			{ INI_LEVELS, "iOutsideTimeNightMod", 1, &iOutsideTimeNightMod },
			{ INI_LEVELS, "iOutsideWeatherPleasantMod", 0, &iOutsideWeatherPleasantMod },
			{ INI_LEVELS, "iOutsideWeatherCloudyMod", 1, &iOutsideWeatherCloudyMod },
			{ INI_LEVELS, "iOutsideWeatherRainMod", 1, &iOutsideWeatherRainMod },
			{ INI_LEVELS, "iOutsideWeatherSnowMod", 2, &iOutsideWeatherSnowMod },
			{ INI_LEVELS, "iOutsideWeatherStormMod", 3, &iOutsideWeatherStormMod },
			{ INI_LEVELS, "iOutsideAltitudeHighMod", 1, &iOutsideAltitudeHighMod },
			{ INI_LEVELS, "iOutsideAltitudeVeryHighMod", 2, &iOutsideAltitudeVeryHighMod },
			{ INI_LEVELS, "iHighAltitudeBegin", 4000, &iHighAltitudeBegin },
			{ INI_LEVELS, "iVeryHighAltitudeBegin", 18000, &iVeryHighAltitudeBegin },
			{ INI_LEVELS, "bAutoStormWeathers", 1, &bAutoStormWeathers },
			{ INI_LEVELS, "bShelterDetection", 1, &bShelterDetection },
			{ INI_LEVELS, "iShelteredBase", 1, &iShelteredBase },
			{ INI_LEVELS, "iShelterMinWalls", 6, &iShelterMinWalls },
			{ INI_LEVELS, "iShelterWallDistance", 800, &iShelterWallDistance },
			{ INI_LEVELS, "iShelterRoofHeight", 3000, &iShelterRoofHeight },
		};

		// rain and snow weathers with at least this wind speed (0-1) count as storm, see IsStormWeather
		const float DEFAULT_STORM_MIN_WIND_SPEED = 0.5f;
		float _stormMinWindSpeed = DEFAULT_STORM_MIN_WIND_SPEED;

		// timing of the levels 1-6 in seconds: on, off, random time added to on, random time added to off
		struct LevelTiming
		{
			float on;
			float off;
			float randomOn;
			float randomOff;
		};

		const std::array<LevelTiming, MAX_LEVEL> DEFAULT_LEVEL_TIMINGS = { {
			{ 1.0f, 2.0f, 0.7f, 3.0f },
			{ 1.5f, 1.0f, 0.5f, 3.0f },
			{ 1.0f, 1.0f, 1.5f, 4.0f },
			{ 2.0f, 1.0f, 1.5f, 1.8f },
			{ 4.0f, 1.0f, 0.2f, 0.8f },
			{ ALWAYS, 0.0f, 0.0f, 0.0f },
		} };

		// Weathers that count as storm (iOutsideWeatherStormMod). "Plugin:FormID" entries are resolved with the
		// load order, bare "FormID" entries match that id in any plugin (the behaviour of older versions).
		const char * DEFAULT_STORM_WEATHERS =
			"Skyrim.esm:10C32F,Skyrim.esm:10A241,Skyrim.esm:10A23C,Skyrim.esm:0D4886,Skyrim.esm:0C8221,Skyrim.esm:0C8220," // vanilla
			"001330,001318,"                     // True Storms
			"18BE6E,187DD3,"                     // Vivid Weathers
			"00E001,00E002,00E003,"              // Mythical Ages
			"12DDEE,12DDED,12DDEC,16B287";

		std::unordered_set<UInt32> _stormWeatherFormIds;  // full form ids
		std::unordered_set<UInt32> _stormWeatherLocalIds; // ids without load order index

		// ---- plug control state (switch thread) ----

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

		std::map<int, LevelValues> _levelValues;
		long _previousSwitchState = 0;
		bool _s1Swap = true;
		std::chrono::steady_clock::time_point _beginTime;
		std::chrono::steady_clock::time_point _lastRelaySend;
		std::default_random_engine _generator;

		// workingIp and useUDP are written by the scan thread and read by the switch threads
		std::mutex _connectionConfigMutex;
		std::string workingIp;
		int useUDP = -1;

		std::atomic<bool> _scanFinished(false);
		std::atomic<bool> _weatherTaskPending(false);
		// the next queued check reads the whole game state, not only the player's movement
		std::atomic<bool> _fullCheckWanted(false);

		// no wind until the next game state check: menu, game in the background or standing still
		std::atomic<bool> _windPaused(false);
		// simulated wind 0-1, written by the game state check, read by the simulation thread
		std::atomic<float> _simBaseWind(0.0f);
		std::atomic<float> _motionWind(0.0f);

		// ---- game state (main thread) ----

		std::atomic<bool> teach(false);
		long _shoutLevel = 0;

		// last values read from the game
		bool _internalCell = false;
		// outside, but under a roof and between walls (towers and ruins without a load screen), see IsSheltered
		bool _sheltered = false;
		float _altitude = 0;
		long _weatherClass = 0;
		BGSLocation * _currentLocation = nullptr;
		float _inGameTime = 0;
		float _sunglare = 0;
		UInt32 _weatherId = 0;
		bool _isStorm = false;
		float _gameWindSpeed = 0;

		// last player position for the motion wind (main thread)
		RE::NiPoint3 _lastPosition;
		RE::TESObjectCELL * _lastCell = nullptr;
		std::chrono::steady_clock::time_point _lastPositionTime;

		// worldspaces that are treated as interior (vanilla, e.g. Blackreach and Sovngarde)
		const std::vector<UInt32> notExteriorWorlds = { 0x69857, 0x1EE62, 0x20DCB, 0x1FAE2, 0x34240, 0x50015, 0x2C965, 0x29AB7, 0x4F838, 0x3A9D6, 0x243DE, 0xC97EB, 0xC350D, 0x1CDD3, 0x1CDD9, 0x21EDB, 0x1E49D, 0x2B101, 0x2A9D8, 0x20BFE };

		// location types that count as a house (iInsideHouseBase), other interiors use the dungeon and cave values
		const UInt32 HOUSE_KEYWORD_IDS[] = {
			0x0130DC, // LocTypeDwelling
			0x018EF0, // LocTypeFarm
			0x01CD5A, // LocTypeGuild
			0x01CB87, // LocTypeInn
			0x01CD59, // LocTypeJail
			0x018EF2, // LocTypeLumberMill
			0x0504F9, // LocTypeStewardsDwelling
			0x01CB86, // LocTypeStore
			0x0FC1A3, // LocTypePlayerHouse
			0x01CB85, // LocTypeHouse
			0x01CD57, // LocTypeCastle
		};
		const UInt32 KEYWORD_ID_LOC_SET_CAVE = 0x0130EF;
		const UInt32 KEYWORD_ID_LOC_SET_CAVE_ICE = 0x100819;
		const UInt32 KEYWORD_ID_LOC_SET_OUTDOOR = 0x0130F3;

		std::vector<BGSKeyword *> _houseKeywords;
		BGSKeyword * keywordLocSetCave = nullptr;
		BGSKeyword * keywordLocSetCaveIce = nullptr;
		BGSKeyword * keywordLocSetOutdoor = nullptr;

		// forms of ImmersiveWindsVR.esp (local ids, resolved in FillFormIds)
		UInt32 ImmersiveWindsCallShoutFormId = 0xC15C;
		UInt32 WordRazenFormId = 0xC160;
		UInt32 WordGanFormId = 0xC161;
		UInt32 ImmersiveWindsCallEffect1FormId = 0xC15E;
		UInt32 ImmersiveWindsCallEffect2FormId = 0xC15F;

		TESShout * ImmersiveWindsCallShout = nullptr;
		TESWordOfPower * WordRazen = nullptr;
		TESWordOfPower * WordGan = nullptr;
	}

	// ---- helpers ----

	float SecondsSince(const std::chrono::steady_clock::time_point & start)
	{
		return std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
	}

	std::string Trim(const std::string & text)
	{
		const size_t begin = text.find_first_not_of(" \t");
		if (begin == std::string::npos) {
			return "";
		}
		return text.substr(begin, text.find_last_not_of(" \t") - begin + 1);
	}

	std::string GetIniPath()
	{
		const auto dir = SKSE::log::log_directory();
		return dir ? (*dir / "immersiveWinds.ini").string() : "immersiveWinds.ini";
	}

	std::string ReadIniString(const char * section, const char * key, const char * defaultValue, const std::string & path)
	{
		char buffer[4096];
		GetPrivateProfileStringA(section, key, defaultValue, buffer, sizeof(buffer), path.c_str());
		return buffer;
	}

	bool IniHasKey(const char * section, const char * key, const std::string & path)
	{
		const char * missing = "<missing>";
		return ReadIniString(section, key, missing, path) != missing;
	}

	void WriteIniIfMissing(const char * section, const char * key, const std::string & value, const std::string & path)
	{
		if (!IniHasKey(section, key, path)) {
			WritePrivateProfileStringA(section, key, value.c_str(), path.c_str());
		}
	}

	std::string StrengthKey(int level, const char * suffix)
	{
		return "fIntermittentL" + std::to_string(level) + suffix;
	}

	// ---- connection ----

	bool LoadConnectionConfig(TPLinkHelper & tpLinkConn)
	{
		std::lock_guard<std::mutex> lock(_connectionConfigMutex);
		return tpLinkConn.LoadIPConfigFromFile(GetIniPath(), "sTpLinkPlugIp", useUDP, workingIp);
	}

	void SetWorkingIp(const std::string & ip)
	{
		std::lock_guard<std::mutex> lock(_connectionConfigMutex);
		workingIp = ip;
	}

	void ScanThread()
	{
		// a http switch is addressed by its urls, there is nothing to search
		if (!_useHttpSwitch) {
			const std::string ip = FindTpLinkPlug(GetIniPath(), "sTpLinkPlugIp", _plugName, _logsEnabled, _keepRunning);
			if (!ip.empty()) {
				SetWorkingIp(ip);
			}
		}
		_scanFinished = true;
	}

	bool SwitchPlug(long on)
	{
		if (_useHttpSwitch) {
			return HttpSwitch::Request(_httpMethod, on ? _httpOnUrl : _httpOffUrl, _logsEnabled);
		}
		TPLinkHelper tpLinkConn(_logsEnabled);
		return LoadConnectionConfig(tpLinkConn) && tpLinkConn.SwitchRelayState(on);
	}

	void SendRelayState(long on)
	{
		if (!SwitchPlug(on)) {
			LOG("Switching the plug %s failed", on ? "on" : "off");
		}
		_lastRelaySend = std::chrono::steady_clock::now();
	}

	// Switches the plug on and off with the timing of the current level.
	void SwitchStateControllThread() {
		// wait until the plug was found, otherwise the first switch commands would get lost
		while (!_scanFinished && _keepRunning) {
			Sleep(100);
		}

		while (_keepRunning)
		{
			// read the state once, it is changed by the weather thread
			const long currentState = _currentSwitchState;
			LevelValues & level = _levelValues[currentState];

			if (_previousSwitchState != currentState) {
				// state change detected, reset all state variables
				LOG("\nSwitching mode to %ld", currentState);

				_s1Swap = true;
				level.counterLeaveCurrentState = 0;
				_previousSwitchState = currentState;
				_beginTime = std::chrono::steady_clock::now();
			}

			if (_s1Swap && level.counterLeaveOnMax != 0) {
				if (level.counterLeaveCurrentState == 0) {
					if (level.randomOnAdd > 0) {
						level.currentRandomOn = level.distributionOn(_generator);
					}
					LOG("SwitchingOn");
					SendRelayState(1);
				}
				else if (SecondsSince(_lastRelaySend) >= RELAY_RESEND_INTERVAL_SECONDS) {
					SendRelayState(1);
				}
				level.counterLeaveCurrentState++;
				if (SecondsSince(_beginTime) >= level.counterLeaveOnMax + level.currentRandomOn) {
					level.counterLeaveCurrentState = 0;
					_beginTime = std::chrono::steady_clock::now();
					_s1Swap = false;
				}
			}
			else if (level.counterLeaveOffMax != 0) {
				if (level.counterLeaveCurrentState == 0) {
					if (level.randomOffAdd > 0) {
						level.currentRandomOff = level.distributionOff(_generator);
					}
					LOG("SwitchingOff");
					SendRelayState(0);
				}
				else if (SecondsSince(_lastRelaySend) >= RELAY_RESEND_INTERVAL_SECONDS) {
					SendRelayState(0);
				}
				level.counterLeaveCurrentState++;
				if (SecondsSince(_beginTime) >= level.counterLeaveOffMax + level.currentRandomOff) {
					level.counterLeaveCurrentState = 0;
					_beginTime = std::chrono::steady_clock::now();
					_s1Swap = true;
				}
			}
			Sleep(100);
		}
		LOG_ERR("Exiting");
	}

	// Switches the plug so the fan follows the simulated wind (iWindMode 1).
	void SimulatedWindThread()
	{
		while (!_scanFinished && _keepRunning) {
			Sleep(100);
		}

		WindSimulation simulation(_simSettings, std::random_device()());
		std::ofstream csv;
		if (_writeSimCsv) {
			const auto dir = SKSE::log::log_directory();
			csv.open(dir ? (*dir / "ImmersiveWindsSim.csv").string() : "ImmersiveWindsSim.csv");
			csv << "seconds,base,motion,target,fan,relay\n";
		}

		const auto start = std::chrono::steady_clock::now();
		auto lastStep = start;
		bool relayOn = false;
		SendRelayState(0);

		while (_keepRunning)
		{
			const auto now = std::chrono::steady_clock::now();
			const float dt = std::chrono::duration<float>(now - lastStep).count();
			lastStep = now;

			const bool paused = _windPaused;
			const float base = paused ? 0.0f : _simBaseWind.load();
			const float motion = paused ? 0.0f : _motionWind.load();
			const bool on = simulation.Update(dt, base, motion);

			if (on != relayOn) {
				relayOn = on;
				SendRelayState(on ? 1 : 0);
			}
			else if (SecondsSince(_lastRelaySend) >= RELAY_RESEND_INTERVAL_SECONDS) {
				SendRelayState(on ? 1 : 0);
			}

			if (csv.is_open()) {
				char line[128];
				snprintf(line, sizeof(line), "%.2f,%.3f,%.3f,%.3f,%.3f,%d\n", SecondsSince(start), base, motion, simulation.Target(), simulation.FanSpeed(), relayOn ? 1 : 0);
				csv << line << std::flush;
			}
			Sleep(SIMULATION_STEP_MS);
		}
		LOG_ERR("Exiting");
	}

	// ---- settings ----

	// Adds every missing setting with its default value, existing values are kept.
	void WriteDefaultsToIniFile() {
		const std::string sExecPath = GetIniPath();

		if (ReadIniString(INI_INTERNAL, "sModVersion", "", sExecPath) != MOD_VERSION) {
			WritePrivateProfileStringA(INI_INTERNAL, "sModVersion", MOD_VERSION.c_str(), sExecPath.c_str());
		}
		WriteIniIfMissing(INI_INTERNAL, "sTpLinkPlugIp", "", sExecPath);
		WriteIniIfMissing(INI_GENERAL, "sSwitchType", "tplink", sExecPath);
		WriteIniIfMissing(INI_GENERAL, "sPlugName", DEFAULT_PLUG_NAME, sExecPath);
		WriteIniIfMissing(INI_HTTP, "sHttpMethod", "GET", sExecPath);
		WriteIniIfMissing(INI_HTTP, "sOnUrl", "", sExecPath);
		WriteIniIfMissing(INI_HTTP, "sOffUrl", "", sExecPath);

		for (const IntSetting & setting : GENERAL_SETTINGS) {
			WriteIniIfMissing(setting.section, setting.key, std::to_string(setting.defaultValue), sExecPath);
		}

		for (int i = 0; i < MAX_LEVEL; i++) {
			const LevelTiming & timing = DEFAULT_LEVEL_TIMINGS[i];
			WriteIniIfMissing(INI_STRENGTH, StrengthKey(i + 1, "On").c_str(), std::to_string(timing.on), sExecPath);
			WriteIniIfMissing(INI_STRENGTH, StrengthKey(i + 1, "Off").c_str(), std::to_string(timing.off), sExecPath);
			WriteIniIfMissing(INI_STRENGTH, StrengthKey(i + 1, "RandomAddOn").c_str(), std::to_string(timing.randomOn), sExecPath);
			WriteIniIfMissing(INI_STRENGTH, StrengthKey(i + 1, "RandomAddOff").c_str(), std::to_string(timing.randomOff), sExecPath);
		}

		for (const IntSetting & setting : LEVEL_SETTINGS) {
			WriteIniIfMissing(setting.section, setting.key, std::to_string(setting.defaultValue), sExecPath);
		}
		WriteIniIfMissing(INI_LEVELS, "sStormWeathers", DEFAULT_STORM_WEATHERS, sExecPath);
		WriteIniIfMissing(INI_LEVELS, "fStormMinWindSpeed", std::to_string(DEFAULT_STORM_MIN_WIND_SPEED), sExecPath);

		for (const FloatSetting & setting : SIM_SETTINGS) {
			WriteIniIfMissing(INI_SIM, setting.key, std::to_string(setting.defaultValue), sExecPath);
		}
	}

	int ReadIniInt(const IntSetting & setting, const std::string & path)
	{
		return GetPrivateProfileIntA(setting.section, setting.key, setting.defaultValue, path.c_str());
	}

	int ReadGeneralSetting(const char * key, const std::string & path)
	{
		for (const IntSetting & setting : GENERAL_SETTINGS) {
			if (strcmp(setting.key, key) == 0) {
				return ReadIniInt(setting, path);
			}
		}
		return 0;
	}

	float ReadIniFloat(const char * section, const std::string & key, float defaultValue, const std::string & path)
	{
		return strtof(ReadIniString(section, key.c_str(), std::to_string(defaultValue).c_str(), path).c_str(), nullptr);
	}

	// Reads the list of storm weathers, see DEFAULT_STORM_WEATHERS. Needs the game data (load order).
	void LoadStormWeathers(const std::string & list)
	{
		_stormWeatherFormIds.clear();
		_stormWeatherLocalIds.clear();

		auto dataHandler = RE::TESDataHandler::GetSingleton();
		std::stringstream stream(list);
		std::string entry;
		while (std::getline(stream, entry, ',')) {
			entry = Trim(entry);
			if (entry.empty()) {
				continue;
			}

			// plugin file names can not contain ':'
			const size_t separator = entry.rfind(':');
			const std::string idText = Trim(separator == std::string::npos ? entry : entry.substr(separator + 1));
			char * end = nullptr;
			const UInt32 id = strtoul(idText.c_str(), &end, 16);
			if (idText.empty() || *end != '\0') {
				LOG_ERR("Invalid storm weather entry: %s", entry.c_str());
				continue;
			}

			if (separator == std::string::npos) {
				_stormWeatherLocalIds.insert(id & 0x00FFFFFF);
				continue;
			}

			const std::string plugin = Trim(entry.substr(0, separator));
			const auto weather = dataHandler ? dataHandler->LookupForm<TESWeather>(id, plugin) : nullptr;
			if (!weather) {
				LOG("Storm weather %s not found (plugin not loaded?)", entry.c_str());
				continue;
			}
			_stormWeatherFormIds.insert(weather->GetFormID());
		}
		LOG("Storm weathers: %zu resolved, %zu without plugin", _stormWeatherFormIds.size(), _stormWeatherLocalIds.size());
	}

	void LoadSettings() {
		const std::string sExecPath = GetIniPath();
		_logsEnabled = ReadGeneralSetting("bLoggingEnabled", sExecPath) > 0;
		_noWindInMenu = ReadGeneralSetting("bNoWindInMenu", sExecPath) > 0;
		_intermittentModeEnabled = ReadGeneralSetting("bIntermittentModeEnabled", sExecPath) > 0;
		_simulatedWind = ReadGeneralSetting("iWindMode", sExecPath) == 1;
		_writeSimCsv = ReadGeneralSetting("bWriteCsv", sExecPath) > 0;
		for (const FloatSetting & setting : SIM_SETTINGS) {
			*setting.value = std::max(0.0f, ReadIniFloat(INI_SIM, setting.key, setting.defaultValue, sExecPath));
		}
		_motionFullSpeed = std::max(_motionFullSpeed, _motionStartSpeed + 1.0f);
		if (_simulatedWind) {
			LOG_ERR("Using the simulated wind");
		}

		_useHttpSwitch = _stricmp(Trim(ReadIniString(INI_GENERAL, "sSwitchType", "tplink", sExecPath)).c_str(), "http") == 0;
		_plugName = Trim(ReadIniString(INI_GENERAL, "sPlugName", DEFAULT_PLUG_NAME, sExecPath));
		if (_plugName.empty()) {
			_plugName = DEFAULT_PLUG_NAME;
		}
		_httpMethod = Trim(ReadIniString(INI_HTTP, "sHttpMethod", "GET", sExecPath));
		_httpOnUrl = Trim(ReadIniString(INI_HTTP, "sOnUrl", "", sExecPath));
		_httpOffUrl = Trim(ReadIniString(INI_HTTP, "sOffUrl", "", sExecPath));
		if (_useHttpSwitch) {
			LOG_ERR("Using the http switch: %s %s / %s", _httpMethod.c_str(), _httpOnUrl.c_str(), _httpOffUrl.c_str());
		}

		_levelValues[0] = LevelValues(0, ALWAYS, 0, 0);

		if (!_intermittentModeEnabled && !_simulatedWind) {
			// simple mode: fan on outside, off inside
			for (const IntSetting & setting : LEVEL_SETTINGS) {
				*setting.value = 0;
			}
			iOutsideBase = 1;
			iHighAltitudeBegin = 4000;
			iVeryHighAltitudeBegin = 18000;
			_levelValues[1] = LevelValues(ALWAYS, 0, 0, 0);
			return;
		}

		for (int i = 0; i < MAX_LEVEL; i++) {
			const LevelTiming & defaults = DEFAULT_LEVEL_TIMINGS[i];
			float on = ReadIniFloat(INI_STRENGTH, StrengthKey(i + 1, "On"), defaults.on, sExecPath);
			float off = ReadIniFloat(INI_STRENGTH, StrengthKey(i + 1, "Off"), defaults.off, sExecPath);
			const float randomOn = ReadIniFloat(INI_STRENGTH, StrengthKey(i + 1, "RandomAddOn"), defaults.randomOn, sExecPath);
			const float randomOff = ReadIniFloat(INI_STRENGTH, StrengthKey(i + 1, "RandomAddOff"), defaults.randomOff, sExecPath);

			// 0 for on or off keeps the plug in the other state, at least one second otherwise
			if (on <= 0 && off <= 0) {
				on = defaults.on;
				off = defaults.off;
			}
			else if (on <= 0) {
				off = ALWAYS;
			}
			else if (off <= 0) {
				on = ALWAYS;
			}
			else {
				on = std::max(on, 1.0f);
				off = std::max(off, 1.0f);
			}
			_levelValues[i + 1] = LevelValues(on, off, randomOn, randomOff);
		}
		LOG("corrected intermittent strengths");

		for (const IntSetting & setting : LEVEL_SETTINGS) {
			*setting.value = ReadIniInt(setting, sExecPath);
		}
		LoadStormWeathers(ReadIniString(INI_LEVELS, "sStormWeathers", DEFAULT_STORM_WEATHERS, sExecPath));
		_stormMinWindSpeed = ReadIniFloat(INI_LEVELS, "fStormMinWindSpeed", DEFAULT_STORM_MIN_WIND_SPEED, sExecPath);
	}

	// ---- wind level ----

	// 0-1
	float GetSunGlare(TESWeather* thisWeather)
	{
		return (thisWeather) ? static_cast<std::uint8_t>(thisWeather->data.sunGlare) / 255.0f : 0.0f; // CommonLib types it signed, the value is 0-255
	}

	// The weather the sky mostly shows: during a transition the previous weather until it is half done. Weather
	// mods often use long transitions, the storm should not start while the sky is still clear.
	TESWeather * GetVisibleWeather(RE::Sky * sky)
	{
		return (sky->lastWeather && sky->currentWeatherPct < 0.5f) ? sky->lastWeather : sky->currentWeather;
	}

	// The weathers of sStormWeathers, and with bAutoStormWeathers every rain or snow weather with strong wind.
	// The latter works for weathers of any mod without knowing their ids.
	bool IsStormWeather(TESWeather * weather)
	{
		const UInt32 weatherId = weather->GetFormID();
		if (_stormWeatherFormIds.contains(weatherId) || _stormWeatherLocalIds.contains(weatherId & 0x00FFFFFF)) {
			return true;
		}
		using Flag = TESWeather::WeatherDataFlag;
		const float windSpeed = weather->data.windSpeed / 255.0f;
		return bAutoStormWeathers && weather->data.flags.any(Flag::kRainy, Flag::kSnow) && windSpeed >= _stormMinWindSpeed;
	}

	bool IsHouse(BGSLocation * location)
	{
		return std::any_of(_houseKeywords.begin(), _houseKeywords.end(), [location](BGSKeyword * keyword) { return location->HasKeyword(keyword); });
	}

	void UpdateNativeWindStateInternal()
	{
		int resultingLevel = 0;
		int mod = 0;
		int altitudeMod = 0;
		if (_internalCell)
		{
			resultingLevel = iInsideHouseBase;

			if (_currentLocation && !IsHouse(_currentLocation))
			{
				// dungeon base is the default if house was not detected.
				resultingLevel = iInsideDungeonBase;

				if (keywordLocSetCave && _currentLocation->HasKeyword(keywordLocSetCave)) {
					resultingLevel = std::max(resultingLevel, iInsideCaveBase);
				}
				if (keywordLocSetCaveIce && _currentLocation->HasKeyword(keywordLocSetCaveIce)) {
					resultingLevel = std::max(resultingLevel, iInsideIceCaveBase);
				}
				if (keywordLocSetOutdoor && _currentLocation->HasKeyword(keywordLocSetOutdoor)) { // outside actually
					resultingLevel = std::max(resultingLevel, iOutsideBase);
				}
			}
		}
		else if (_sheltered) {
			resultingLevel = iShelteredBase;
		}
		else {
			resultingLevel = iOutsideBase;

			// altitude mod
			if (_altitude > iVeryHighAltitudeBegin) {
				altitudeMod = iOutsideAltitudeVeryHighMod;
				LOG("very high altitude!");
			}
			else if (_altitude > iHighAltitudeBegin) {
				altitudeMod = iOutsideAltitudeHighMod;
				LOG("high altitude!");
			}
			mod += altitudeMod;

			// daytime mod
			if (_inGameTime > 7 && _inGameTime < 19) {
				mod += iOutsideTimeDayMod;
			}
			else {
				mod += iOutsideTimeNightMod;
			}

			// weather mod
			if (_isStorm) {
				mod += iOutsideWeatherStormMod;
			}
			else if (_weatherClass == 0) { // pleasant
				mod += iOutsideWeatherPleasantMod;
			}
			else if (_weatherClass == 1) { // cloudy, still good weather with a lot of sun glare
				mod += (_sunglare > 0.5) ? iOutsideWeatherPleasantMod : iOutsideWeatherCloudyMod;
			}
			else if (_weatherClass == 2) { // rain
				mod += iOutsideWeatherRainMod;
			}
			else if (_weatherClass == 3) { // snow
				mod += iOutsideWeatherSnowMod;
			}
		}
		int result = resultingLevel + mod;

		// shout has the highest prio
		if (_shoutLevel == 1 && result < 5) {
			result = 5;
		}
		if (_shoutLevel == 2) {
			result = MAX_LEVEL;
		}
		result = std::min(result, MAX_LEVEL);
		_currentSwitchState = result;
		_windPaused = false;

		LOG("Resulted in: %d", result);

		if (_simulatedWind) {
			// inside the level of the location, outside the wind of the weather in the game
			float wind = (_internalCell || _sheltered)
				? static_cast<float>(resultingLevel) / MAX_LEVEL
				: _outsideMinWind + _gameWindScale * _gameWindSpeed + static_cast<float>(altitudeMod) / MAX_LEVEL;
			if (_shoutLevel == 1) {
				wind = std::max(wind, SHOUT_WIND);
			}
			if (_shoutLevel == 2) {
				wind = 1.0f;
			}
			_simBaseWind = std::clamp(wind, 0.0f, 1.0f);
			LOG("Simulated base wind: %.2f (wind in the game %.2f)", _simBaseWind.load(), _gameWindSpeed);
		}
	}

	void UpdateNativeWindState(bool internalCell, bool sheltered, float altitude, long weatherClass, BGSLocation* curLocation, float inGameTime, float sunglare, UInt32 weatherId, bool isStorm)
	{
		LOG("\nUpdateNativeWindState called %d; %g; %ld; %g; %g; %08X%s%s", (internalCell ? 1 : 0), altitude, weatherClass, inGameTime, sunglare, weatherId, isStorm ? " (storm)" : "", sheltered ? " (sheltered)" : "");

		_internalCell = internalCell;
		_sheltered = sheltered;
		_altitude = altitude;
		_weatherClass = weatherClass;
		_currentLocation = curLocation;
		_inGameTime = inGameTime;
		_sunglare = sunglare;
		_weatherId = weatherId;
		_isStorm = isStorm;

		UpdateNativeWindStateInternal();
	}

	void SetShoutLevel(long level)
	{
		LOG("Shout level: %ld", level);
		_shoutLevel = level;
		UpdateNativeWindStateInternal();
	}

	long CheckShoutEffects()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto effects = player ? player->AsMagicTarget()->GetActiveEffectList() : nullptr;
		if (!effects)
			return 0;

		for (const auto ae : *effects)
		{
			const auto mgef = ae ? ae->GetBaseObject() : nullptr;
			if (!mgef)
				continue;
			if (mgef->GetFormID() == ImmersiveWindsCallEffect1FormId)
				return 1;
			if (mgef->GetFormID() == ImmersiveWindsCallEffect2FormId)
				return 2;
		}
		return 0;
	}

	// ---- game ----

	bool FillFormIds()
	{
		_houseKeywords.clear();
		for (const UInt32 id : HOUSE_KEYWORD_IDS) {
			if (auto keyword = RE::TESForm::LookupByID<BGSKeyword>(id)) {
				_houseKeywords.push_back(keyword);
			}
		}
		keywordLocSetCave = RE::TESForm::LookupByID<BGSKeyword>(KEYWORD_ID_LOC_SET_CAVE);
		keywordLocSetCaveIce = RE::TESForm::LookupByID<BGSKeyword>(KEYWORD_ID_LOC_SET_CAVE_ICE);
		keywordLocSetOutdoor = RE::TESForm::LookupByID<BGSKeyword>(KEYWORD_ID_LOC_SET_OUTDOOR);

		auto dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler || !dataHandler->LookupModByName(PLUGIN_NAME))
		{
			LOG_ERR("%s is not loaded, the shout is not available.", PLUGIN_NAME);
			return false;
		}

		LOG("Plugin is loaded. Trying to find forms.");

		// LookupFormID resolves the load order index (regular and light plugins)
		ImmersiveWindsCallEffect1FormId = dataHandler->LookupFormID(ImmersiveWindsCallEffect1FormId, PLUGIN_NAME);
		ImmersiveWindsCallEffect2FormId = dataHandler->LookupFormID(ImmersiveWindsCallEffect2FormId, PLUGIN_NAME);
		ImmersiveWindsCallShout = dataHandler->LookupForm<TESShout>(ImmersiveWindsCallShoutFormId, PLUGIN_NAME);
		WordRazen = dataHandler->LookupForm<TESWordOfPower>(WordRazenFormId, PLUGIN_NAME);
		WordGan = dataHandler->LookupForm<TESWordOfPower>(WordGanFormId, PLUGIN_NAME);
		return true;
	}

	int GetClassification(TESWeather * weather)
	{
		using Flag = TESWeather::WeatherDataFlag;
		const auto& flags = weather->data.flags;

		if (flags.any(Flag::kPleasant))
			return 0;
		if (flags.any(Flag::kCloudy))
			return 1;
		if (flags.any(Flag::kRainy))
			return 2;
		if (flags.any(Flag::kSnow))
			return 3;

		return -1;
	}

	// Calls a vanilla Papyrus global (Game.TeachWord / Game.UnlockWord), same behaviour on SE, AE and VR
	void CallGameFunction(const char * function, TESWordOfPower * wop)
	{
		auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!vm)
			return;
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
		vm->DispatchStaticCall("Game", function, RE::MakeFunctionArguments(std::move(wop)), callback);
	}

	// Gives the player the shout of ImmersiveWindsVR.esp once per game, if not known yet.
	void TeachShoutOnce(RE::PlayerCharacter * player)
	{
		if (teach.exchange(true)) {
			return;
		}
		if (WordRazen->GetKnown()) {
			LOG("Player knows the word.");
			return;
		}
		LOG("Player doesn't know the word.");
		if (ImmersiveWindsCallShout)
		{
			player->AddShout(ImmersiveWindsCallShout);
			CallGameFunction("TeachWord", WordRazen);
			CallGameFunction("TeachWord", WordGan);
			CallGameFunction("UnlockWord", WordRazen);
			CallGameFunction("UnlockWord", WordGan);
			LOG("Words learned and unlocked.");
		}
	}

	// Whether a ray hits the collision of the world (walls, roofs, rocks, terrain). Main thread only.
	bool RayHits(const RE::NiPoint3 & from, const RE::NiPoint3 & to)
	{
		auto tes = RE::TES::GetSingleton();
		if (!tes) {
			return false;
		}
		const float scale = RE::bhkWorld::GetWorldScale();
		RE::bhkPickData pick;
		pick.rayInput.from = RE::hkVector4(from.x * scale, from.y * scale, from.z * scale, 0.0f);
		pick.rayInput.to = RE::hkVector4(to.x * scale, to.y * scale, to.z * scale, 0.0f);
		// the line of sight layer: blocked by buildings, rocks and terrain, not by actors
		pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kLOS);
		tes->Pick(pick);
		return pick.rayOutput.HasHit();
	}

	// Outside, but inside a building without a load screen (towers, ruins): the game does not tell, so rays are cast
	// from the player: one up to find a roof and eight around to find walls.
	bool IsSheltered(RE::PlayerCharacter * player)
	{
		const float CHEST_HEIGHT = 90.0f;
		const RE::NiPoint3 chest = player->GetPosition() + RE::NiPoint3(0.0f, 0.0f, CHEST_HEIGHT);

		const float roofHeight = static_cast<float>(iShelterRoofHeight);
		const bool roof = RayHits(chest, chest + RE::NiPoint3(0.0f, 0.0f, roofHeight));

		const float wallDistance = static_cast<float>(iShelterWallDistance);
		int walls = 0;
		const int DIRECTIONS = 8;
		for (int i = 0; i < DIRECTIONS; i++) {
			const float angle = i * 2.0f * RE::NI_PI / DIRECTIONS;
			const RE::NiPoint3 to = chest + RE::NiPoint3(std::cos(angle) * wallDistance, std::sin(angle) * wallDistance, 0.0f);
			if (RayHits(chest, to)) {
				walls++;
			}
		}

		LOG("Shelter: roof %s, walls %d of %d", roof ? "yes" : "no", walls, DIRECTIONS);
		return roof && walls >= iShelterMinWalls;
	}

	// Reads the game state (player, cell, weather, shout effects) and updates the wind level.
	// Calls into the engine, so it must run on the game's main thread (see WeatherCheck).
	void UpdateFromGameState()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->Is3DLoaded())
		{
			return;
		}

		const auto cell = player->GetParentCell();
		if (!cell)
		{
			return;
		}

		if (WordRazen && WordGan)
		{
			TeachShoutOnce(player);
			const long shoutLevel = CheckShoutEffects();
			if (_shoutLevel != shoutLevel)
			{
				SetShoutLevel(shoutLevel);
			}
		}

		const auto worldSpace = cell->GetRuntimeData().worldSpace;
		const bool isInterior = !worldSpace || (std::find(notExteriorWorlds.begin(), notExteriorWorlds.end(), worldSpace->GetFormID()) != notExteriorWorlds.end());

		const auto skyPtr = RE::Sky::GetSingleton();
		if (skyPtr != nullptr && skyPtr->currentWeather != nullptr)
		{
			// 0-1 (the weather records store 0-255, in case a game version keeps that scale)
			_gameWindSpeed = skyPtr->windSpeed > 1.0f ? skyPtr->windSpeed / 255.0f : skyPtr->windSpeed;
			const auto weather = GetVisibleWeather(skyPtr);
			const bool sheltered = !isInterior && bShelterDetection && IsSheltered(player);
			UpdateNativeWindState(isInterior, sheltered, player->GetPosition().z, GetClassification(weather), player->GetCurrentLocation(), skyPtr->currentGameHour, GetSunGlare(weather), weather->GetFormID(), IsStormWeather(weather));
		}
	}

	// no wind until the next game state check, see _windPaused
	void PauseWind()
	{
		_currentSwitchState = 0;
		_windPaused = true;
	}

	// Simulated wind: the wind of the player's own movement (running, riding, falling), from the change of the position.
	void UpdateMotion()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->Is3DLoaded())
		{
			return;
		}

		const auto now = std::chrono::steady_clock::now();
		const float seconds = SecondsSince(_lastPositionTime);
		const RE::NiPoint3 position = player->GetPosition();
		const auto cell = player->GetParentCell();

		// an interior cell change is a door; after a long pause (menu, loading) the old position says nothing
		const bool samePlace = cell == _lastCell || (cell && _lastCell && cell->IsExteriorCell() && _lastCell->IsExteriorCell());
		if (samePlace && seconds > 0.0f && seconds < 1.0f)
		{
			const float speed = position.GetDistance(_lastPosition) / seconds;
			if (speed < MAX_MOVEMENT_SPEED)
			{
				const float share = std::clamp((speed - _motionStartSpeed) / (_motionFullSpeed - _motionStartSpeed), 0.0f, 1.0f);
				_motionWind = share * _motionWindMax;
			}
		}
		else
		{
			_motionWind = 0.0f;
		}
		_lastPosition = position;
		_lastCell = cell;
		_lastPositionTime = now;
	}

	// The queued check on the main thread.
	void RunGameStateCheck()
	{
		const bool full = _fullCheckWanted.exchange(false);
		if (menuStopsGameNoDialogue && _noWindInMenu)
		{
			PauseWind(); //No fan in menu.
			return;
		}
		// after a pause the whole state is read again, it restores the level
		if (full || _windPaused)
		{
			UpdateFromGameState();
		}
		if (_simulatedWind)
		{
			UpdateMotion();
		}
	}

	// time the pending game state check was queued (steady_clock ticks), written by the timer and the menu events
	std::atomic<std::chrono::steady_clock::rep> _checkQueuedAt(0);

	// Queues RunGameStateCheck on the main thread. full: reads the whole game state, otherwise only the movement
	// (simulated wind). Returns false if a check is already queued.
	bool QueueGameStateCheck(bool full)
	{
		if (full)
		{
			_fullCheckWanted = true; // also for a check that is already queued
		}
		// only one check at a time, so tasks do not pile up while the game does not process them (e.g. loading screens)
		if (_weatherTaskPending.exchange(true))
		{
			return false;
		}
		_checkQueuedAt = std::chrono::steady_clock::now().time_since_epoch().count();
		SKSE::GetTaskInterface()->AddTask([]() {
			RunGameStateCheck();
			_weatherTaskPending = false;
		});
		return true;
	}

	bool RequestGameStateCheck()
	{
		return QueueGameStateCheck(true);
	}

	float SecondsSinceCheckQueued()
	{
		return SecondsSince(std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(_checkQueuedAt.load())));
	}

	// Skyrim VR keeps running without window focus, so it always counts as in the foreground there.
	bool IsGameInForeground()
	{
		if (REL::Module::IsVR())
			return true;

		DWORD processId = 0;
		GetWindowThreadProcessId(GetForegroundWindow(), &processId);
		return processId == GetCurrentProcessId();
	}

	// Background timer: watches the window focus and queues the game state check on the main thread every few
	// seconds, with the simulated wind the movement check in between.
	void WeatherCheck()
	{
		std::chrono::steady_clock::time_point lastCheck;
		std::chrono::steady_clock::time_point lastFullCheck;
		const float interval = _simulatedWind ? MOTION_CHECK_INTERVAL_SECONDS : WEATHER_CHECK_INTERVAL_SECONDS;

		while (_keepRunning)
		{
			if (!IsGameInForeground())
			{
				if (!_windPaused)
				{
					LOG("Game is in the background, stopping the fan.");
				}
				PauseWind();
				lastCheck = {}; // restores the level right when the game is back
				lastFullCheck = {};
			}
			else if (menuStopsGameNoDialogue && _noWindInMenu)
			{
				// also handled here, so the fan stops even if the game does not process tasks while the menu is open
				PauseWind(); //No fan in menu.
			}
			else if (SecondsSince(lastCheck) >= interval)
			{
				lastCheck = std::chrono::steady_clock::now();
				const bool full = SecondsSince(lastFullCheck) >= WEATHER_CHECK_INTERVAL_SECONDS;
				if (full)
				{
					lastFullCheck = lastCheck;
				}
				// a check that is still queued after GAME_STALLED_SECONDS means the game stands still
				if (!QueueGameStateCheck(full) && SecondsSinceCheckQueued() >= GAME_STALLED_SECONDS && !_windPaused)
				{
					// no fan while nobody plays; the next check after the game continues restores the level
					LOG("Game stands still, stopping the fan.");
					PauseWind();
				}
			}
			Sleep(FOCUS_CHECK_INTERVAL_MS);
		}
	}

	void InitBgThread() {
		LOG("InitBgThread");

		// the settings first, the threads use them
		WriteDefaultsToIniFile();
		LoadSettings();
		_generator.seed(std::random_device()());

		std::thread(ScanThread).detach();
		// plug control, waits for the scan
		if (_simulatedWind) {
			std::thread(SimulatedWindThread).detach();
		}
		else {
			std::thread(SwitchStateControllThread).detach();
		}
		// checks the game state
		std::thread(WeatherCheck).detach();
	}

	void ResetTeach()
	{
		teach.store(false);
	}

	void SwitchOffOnExit()
	{
		_currentSwitchState = 0;
		_windPaused = true;
		_keepRunning = false;
		// before the settings are loaded _useHttpSwitch is false and the plug ip comes from the ini file
		SwitchPlug(0);
	}

	void StartMod()
	{
		// ponytail: the VR build patched an unknown game function here (SafeWriteJump at VR 0x127B460+0x38D);
		// dropped in the port because its purpose and SE/AE address are unknown. Re-add via Address Library ID if needed.
		if (auto ui = RE::UI::GetSingleton())
			ui->AddEventSink<RE::MenuOpenCloseEvent>(&ImmersiveWinds::menuEvent);

		teach.store(false);
		FillFormIds();
		// starts all threads, after the settings are loaded
		InitBgThread();
	}

	void Log(const int msgLogLevel, const char * fmt, ...)
	{
		if (msgLogLevel > 0 && !_logsEnabled)
		{
			return;
		}

		va_list args;
		char logBuffer[4096];

		va_start(args, fmt);
		vsprintf_s(logBuffer, sizeof(logBuffer), fmt, args);
		va_end(args);

		_MESSAGE("%s", logBuffer);
	}

	namespace
	{
		// Switches the plug off when the game exits. Defined last in this file on purpose: objects of one
		// file are destroyed in reverse order, so the settings and connection data above still exist then.
		EndGameDetect endGameDetect;
	}
}
