#include "ImmersiveWinds.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <vector>


namespace ImmersiveWinds {

	std::atomic<bool> _keepRunning(true);
	std::atomic<long> _currentSwitchState(0);

	namespace
	{
		using UInt32 = std::uint32_t;
		using RE::BGSKeyword;
		using RE::BGSLocation;
		using RE::TESShout;
		using RE::TESWeather;
		using RE::TESWordOfPower;

		const int SCAN_MAX_ATTEMPTS = 5;
		const int SCAN_RETRY_DELAY_MS = 10000;
		const float WEATHER_CHECK_INTERVAL_SECONDS = 3.0f;
		const int FOCUS_CHECK_INTERVAL_MS = 250;
		// the relay state is sent again after this time, in case a (udp) message got lost or the plug restarted
		const float RELAY_RESEND_INTERVAL_SECONDS = 15.0f;
		// a queued weather check that did not run after this time means the game stands still (frozen, long loading screen)
		const float GAME_STALLED_SECONDS = 10.0f;
		// an on or off time this long keeps the plug in one state
		const float ALWAYS = 999999.0f;
		const int MAX_LEVEL = 6;

		const char * PLUG_NAME = "ImmersiveWindsPlug";
		const char * PLUGIN_NAME = "ImmersiveWindsVR.esp";

		// ini sections (names kept for compatibility with existing ini files)
		const char * INI_INTERNAL = "Internal";
		const char * INI_GENERAL = "GeneralModConfig";
		const char * INI_STRENGTH = "IntermittendModeStrengthConfig";
		const char * INI_LEVELS = "IntermittendModeLevelConfig";

		// ---- settings ----

		bool _logsEnabled = false;
		bool _noWindInMenu = false;
		bool _intermittentModeEnabled = true;

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
		};

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

		// ---- game state (main thread) ----

		std::atomic<bool> teach(false);
		long _shoutLevel = 0;

		// last values read from the game
		bool _internalCell = false;
		float _altitude = 0;
		long _weatherClass = 0;
		BGSLocation * _currentLocation = nullptr;
		float _inGameTime = 0;
		float _sunglare = 0;
		UInt32 _weatherId = 0;

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
		return tpLinkConn.LoadIPConfigFromFile(useUDP, workingIp);
	}

	void SetWorkingIp(const std::string & ip)
	{
		std::lock_guard<std::mutex> lock(_connectionConfigMutex);
		workingIp = ip;
	}

	void ScanAndCheckConnection()
	{
		const std::string sExecPath = GetIniPath();
		const std::string savedIp = ReadIniString(INI_INTERNAL, "sTpLinkPlugIp", "", sExecPath);
		LOG("READ ini file: %s", savedIp.c_str());

		if (savedIp.empty()) {
			LOG("no ip specified");
		}
		else {
			TPLinkHelper tpLinkConn(_logsEnabled);
			if (!tpLinkConn.ConnectToHost(9999, savedIp.c_str())) {
				LOG("no connection");
			}
			else {
				LOG("Connect Succeeded");
				// check if correct device:
				if (LoadConnectionConfig(tpLinkConn) && tpLinkConn.GetSystemInfo().alias() == PLUG_NAME) {
					return;
				}
				LOG("wrong device");
			}
		}

		// search for the device until one is found and save its ip to the ini file.
		// A failed scan keeps the saved ip, so a plug that is only temporarily offline is not forgotten.
		for (int attempt = 1; attempt <= SCAN_MAX_ATTEMPTS && _keepRunning; attempt++) {
			SPScanner scanner;
			const std::string ipOfDevice = scanner.ScanForTpPlug(PLUG_NAME);
			LOG("scan %d returned: %s", attempt, ipOfDevice.c_str());
			if (!ipOfDevice.empty()) {
				WritePrivateProfileStringA(INI_INTERNAL, "sTpLinkPlugIp", ipOfDevice.c_str(), sExecPath.c_str());
				SetWorkingIp(ipOfDevice);
				return;
			}
			if (attempt < SCAN_MAX_ATTEMPTS) {
				Sleep(SCAN_RETRY_DELAY_MS);
			}
		}
		LOG_ERR("Could not find a plug named %s", PLUG_NAME);
	}

	void ScanThread()
	{
		ScanAndCheckConnection();
		_scanFinished = true;
	}

	void SendRelayState(long on)
	{
		TPLinkHelper tpLinkConn(_logsEnabled);
		if (LoadConnectionConfig(tpLinkConn) && !tpLinkConn.SwitchRelayState(on)) {
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

	// ---- settings ----

	// Adds every missing setting with its default value, existing values are kept.
	void WriteDefaultsToIniFile() {
		const std::string sExecPath = GetIniPath();

		if (ReadIniString(INI_INTERNAL, "sModVersion", "", sExecPath) != MOD_VERSION) {
			WritePrivateProfileStringA(INI_INTERNAL, "sModVersion", MOD_VERSION.c_str(), sExecPath.c_str());
		}
		WriteIniIfMissing(INI_INTERNAL, "sTpLinkPlugIp", "", sExecPath);

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

		_levelValues[0] = LevelValues(0, ALWAYS, 0, 0);

		if (!_intermittentModeEnabled) {
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
	}

	// ---- wind level ----

	float GetSunGlare(TESWeather* thisWeather)
	{
		return (thisWeather) ? static_cast<float>(static_cast<std::uint8_t>(thisWeather->data.sunGlare)) : 0.0f; // CommonLib types it signed, the value is 0-255
	}

	bool IsStormWeather(UInt32 weatherId)
	{
		return _stormWeatherFormIds.contains(weatherId) || _stormWeatherLocalIds.contains(weatherId & 0x00FFFFFF);
	}

	bool IsHouse(BGSLocation * location)
	{
		return std::any_of(_houseKeywords.begin(), _houseKeywords.end(), [location](BGSKeyword * keyword) { return location->HasKeyword(keyword); });
	}

	void UpdateNativeWindStateInternal()
	{
		int resultingLevel = 0;
		int mod = 0;
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
		else {
			resultingLevel = iOutsideBase;

			// altitude mod
			if (_altitude > iVeryHighAltitudeBegin) {
				mod += iOutsideAltitudeVeryHighMod;
				LOG("very high altitude!");
			}
			else if (_altitude > iHighAltitudeBegin) {
				mod += iOutsideAltitudeHighMod;
				LOG("high altitude!");
			}

			// daytime mod
			if (_inGameTime > 7 && _inGameTime < 19) {
				mod += iOutsideTimeDayMod;
			}
			else {
				mod += iOutsideTimeNightMod;
			}

			// weather mod
			if (IsStormWeather(_weatherId)) {
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

		LOG("Resulted in: %d", result);
	}

	void UpdateNativeWindState(bool internalCell, float altitude, long weatherClass, BGSLocation* curLocation, float inGameTime, float sunglare, UInt32 weatherId)
	{
		LOG("\nUpdateNativeWindState called %d; %g; %ld; %g; %g; %08X", (internalCell ? 1 : 0), altitude, weatherClass, inGameTime, sunglare, weatherId);

		_internalCell = internalCell;
		_altitude = altitude;
		_weatherClass = weatherClass;
		_currentLocation = curLocation;
		_inGameTime = inGameTime;
		_sunglare = sunglare;
		_weatherId = weatherId;

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

	// Reads the game state (player, cell, weather, shout effects) and updates the wind level.
	// Calls into the engine, so it must run on the game's main thread (see WeatherCheck).
	void UpdateFromGameState()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->Is3DLoaded())
		{
			return;
		}

		if (menuStopsGameNoDialogue && _noWindInMenu)
		{
			_currentSwitchState = 0; //No fan in menu.
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
			UpdateNativeWindState(isInterior, player->GetPosition().z, GetClassification(skyPtr->currentWeather), player->GetCurrentLocation(), skyPtr->currentGameHour, GetSunGlare(skyPtr->currentWeather), skyPtr->currentWeather->GetFormID());
		}
	}

	// time the pending game state check was queued (steady_clock ticks), written by the timer and the menu events
	std::atomic<std::chrono::steady_clock::rep> _checkQueuedAt(0);

	// Queues UpdateFromGameState on the main thread. Returns false if a check is already queued.
	bool RequestGameStateCheck()
	{
		// only one check at a time, so tasks do not pile up while the game does not process them (e.g. loading screens)
		if (_weatherTaskPending.exchange(true))
		{
			return false;
		}
		_checkQueuedAt = std::chrono::steady_clock::now().time_since_epoch().count();
		SKSE::GetTaskInterface()->AddTask([]() {
			UpdateFromGameState();
			_weatherTaskPending = false;
		});
		return true;
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

	// Background timer: watches the window focus and queues UpdateFromGameState on the main thread every few seconds.
	void WeatherCheck()
	{
		std::chrono::steady_clock::time_point lastCheck;

		while (_keepRunning)
		{
			if (!IsGameInForeground())
			{
				if (_currentSwitchState != 0)
				{
					LOG("Game is in the background, stopping the fan.");
					_currentSwitchState = 0;
				}
				lastCheck = {}; // restores the level right when the game is back
			}
			else if (menuStopsGameNoDialogue && _noWindInMenu)
			{
				// also handled here, so the fan stops even if the game does not process tasks while the menu is open
				_currentSwitchState = 0; //No fan in menu.
			}
			else if (SecondsSince(lastCheck) >= WEATHER_CHECK_INTERVAL_SECONDS)
			{
				lastCheck = std::chrono::steady_clock::now();
				// a check that is still queued after GAME_STALLED_SECONDS means the game stands still
				if (!RequestGameStateCheck() && SecondsSinceCheckQueued() >= GAME_STALLED_SECONDS && _currentSwitchState != 0)
				{
					// no fan while nobody plays; the next check after the game continues restores the level
					LOG("Game stands still, stopping the fan.");
					_currentSwitchState = 0;
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
		std::thread(SwitchStateControllThread).detach();
		// checks the game state
		std::thread(WeatherCheck).detach();
	}

	void ResetTeach()
	{
		teach.store(false);
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
}
