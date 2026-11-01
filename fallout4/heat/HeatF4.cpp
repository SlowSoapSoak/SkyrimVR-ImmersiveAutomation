#include "HeatF4.h"
#include "HttpSwitch.h"
#include "PlugFinder.h"
#include "TPLinkSPHelper.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ImmersiveHeatF4
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		// how often the timer thread wakes up (focus check, heat check interval)
		const int TIMER_INTERVAL_MS = 25;
		// the search for fire nearby goes through all references of the loaded cells, so it runs less often
		const float FIRE_SCAN_INTERVAL_SECONDS = 1.0f;
		const float MIN_CHECK_INTERVAL_SECONDS = 0.05f;
		// the plug state is sent again after this time, in case a (udp) message got lost or the plug restarted
		const float RELAY_RESEND_INTERVAL_SECONDS = 15.0f;
		// a queued check that did not run after this time means the game stands still (frozen, long loading screen)
		const float GAME_STALLED_SECONDS = 10.0f;
		// the ray towards the sun starts at the head and ends this far away (game units, about 430 m)
		const float HEAD_HEIGHT = 110.0f;
		const float SUN_RAY_LENGTH = 30000.0f;
		// highest position of the sun at noon, in degrees above the horizon
		const float SUN_NOON_ELEVATION = 60.0f;
		const float PI = 3.14159265f;
		// share of the climate's sunrise and sunset that gives the hours found by testing in vanilla Skyrim
		constexpr float DAY_TRANSITION_SHARE = 0.385f;

		const char * INI_INTERNAL = "Internal";
		const char * INI_GENERAL = "GeneralModConfig";
		const char * INI_HEAT = "HeatSources";

		// menus that pause the game
		const std::unordered_set<std::string> PAUSING_MENUS{
			"BarterMenu",
			"BookMenu",
			"Console",
			"ContainerMenu",
			"CookingMenu",
			"CreditsMenu",
			"ExamineMenu",
			"LevelUpMenu",
			"LoadingMenu",
			"LooksMenu",
			"MainMenu",
			"MessageBoxMenu",
			"PauseMenu",
			"SleepWaitMenu",
		};

		// ---- settings ----

		const int SWITCH_COUNT = 2;

		// one device: a TP-Link plug found by its name, or a http switch
		struct SwitchSettings
		{
			bool http = false;            // sSwitchType: "tplink" or "http"
			std::string plugName;         // TP-Link: name of the plug in the Kasa app
			std::string httpMethod = "GET";
			std::string onUrl;
			std::string offUrl;
		};

		struct Settings
		{
			bool logging = false;
			bool noHeatInMenu = true;

			std::array<SwitchSettings, SWITCH_COUNT> switches;
			// switch (1 or 2) of each heat source, 0 turns the source off; the same switch for both is on for either
			int sunSwitch = 1;
			int fireSwitch = 1;

			// direct sunlight: outside, at day, bright sun and nothing between the player and the sun
			bool sunlight = true;
			float minSunGlare = 0.4f; // 0-1
			bool sunShadow = true;    // a ray towards the sun decides between sun and shadow
			int sunRayLayer = 41;     // collision layer of that ray (line of sight)
			// day from the sunrise and sunset of the current climate, dayStartHour and dayEndHour without one
			bool climateDayHours = true;
			float dayStartHour = 7.2f;
			float dayEndHour = 17.76f;

			// fire nearby: a reference whose model path contains one of fireModels (and none of fireModelsExclude)
			bool nearFire = true;
			float fireRadius = 350.0f;      // game units, about 5 m
			float smallFireRadius = 100.0f; // candles only count this close
			std::vector<std::string> fireModels;
			std::vector<std::string> smallFireModels;
			std::vector<std::string> fireModelsExclude;

			// fire attacks: hit by a weapon, projectile or ammo with a fire model (flamer, molotov, incendiary), or a
			// magic effect with a fire art (burning)
			bool fireHits = true;
			float fireHitSeconds = 3.0f;
			std::vector<std::string> fireWeaponModels;

			// time between two checks of the heat sources
			float checkIntervalSeconds = 0.1f;
		};

		const char * DEFAULT_FIRE_MODELS = "fire,flame,burning,embers,brazier,molten,lava";
		const char * DEFAULT_SMALL_FIRE_MODELS = "candle";
		const char * DEFAULT_FIRE_MODELS_EXCLUDE = "firehydrant,fireextinguisher,fireescape,firestation,firework,firefly,fireaxe,fireman,firetruck,fireproof,fireplace,firehouse,firedept,nofire,unlit,off";
		const char * DEFAULT_FIRE_WEAPON_MODELS = "flamer,molotov,incendiar,inferno,napalm,fire,flame,burn";

		Settings _settings;

		// ---- state ----

		struct SwitchState
		{
			// whether a heat source uses this switch, set by LoadSettings
			bool used = false;
			// whether the device should be on, written by the heat check, the events and the focus handling
			std::atomic<bool> wanted = false;

			// TP-Link: workingIp and useUDP are written by the scan thread and read by the switch thread
			std::mutex connectionMutex;
			std::string workingIp;
			int useUDP = -1;
		};

		std::array<SwitchState, SWITCH_COUNT> _switches;

		std::atomic<bool> _keepRunning(true);
		std::atomic<bool> _started(false);
		// wakes the switch thread when a wanted state changes
		std::mutex _signalMutex;
		std::condition_variable _switchSignal;

		std::atomic<bool> _scanFinished(false);
		std::atomic<bool> _checkPending(false);
		std::atomic<Clock::rep> _checkQueuedAt(0);
		// end of the heat after a fire event (steady_clock ticks)
		std::atomic<Clock::rep> _fireEventUntil(0);
		std::atomic<bool> _menuOpen(false);
		// result of the last switch command for the message in the game: 0 unknown, 1 worked, -1 failed
		std::atomic<int> _switchResult(0);

		// model check results per base form, only used on the main thread
		enum class FireSize
		{
			kNone,
			kSmall,
			kLarge
		};

		std::unordered_map<RE::TESFormID, FireSize> _fireModelCache;
		// main thread only
		bool _nearFire = false;
		Clock::time_point _lastFireScan;
		std::string _lastReason;
		bool _lastSunBlocked = false;
	}

	// ---- helpers ----

	void Log(const char * fmt, ...)
	{
		if (!_settings.logging) {
			return;
		}
		char buffer[4096];
		va_list args;
		va_start(args, fmt);
		vsnprintf(buffer, sizeof(buffer), fmt, args);
		va_end(args);
		spdlog::info("{}", buffer);
	}

	float SecondsSince(Clock::time_point start)
	{
		return std::chrono::duration<float>(Clock::now() - start).count();
	}

	Clock::time_point FromTicks(Clock::rep ticks)
	{
		return Clock::time_point(Clock::duration(ticks));
	}

	std::string Trim(const std::string & text)
	{
		const size_t begin = text.find_first_not_of(" \t");
		if (begin == std::string::npos) {
			return "";
		}
		return text.substr(begin, text.find_last_not_of(" \t") - begin + 1);
	}

	std::string ToLower(std::string text)
	{
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return text;
	}

	std::vector<std::string> SplitLowerList(const std::string & list)
	{
		std::vector<std::string> words;
		std::stringstream stream(list);
		std::string word;
		while (std::getline(stream, word, ',')) {
			word = ToLower(Trim(word));
			if (!word.empty()) {
				words.push_back(word);
			}
		}
		return words;
	}

	bool ContainsAny(const std::string & lowerText, const std::vector<std::string> & words)
	{
		return std::any_of(words.begin(), words.end(), [&lowerText](const std::string & word) { return lowerText.find(word) != std::string::npos; });
	}

	std::string GetIniPath()
	{
		const auto dir = F4SE::log::log_directory();
		return dir ? (*dir / "immersiveHeat.ini").string() : "immersiveHeat.ini";
	}

	std::string ReadIniString(const char * section, const char * key, const char * defaultValue, const std::string & path)
	{
		char buffer[4096];
		GetPrivateProfileStringA(section, key, defaultValue, buffer, sizeof(buffer), path.c_str());
		return buffer;
	}

	void WriteIniIfMissing(const char * section, const char * key, const std::string & value, const std::string & path)
	{
		const char * missing = "<missing>";
		if (ReadIniString(section, key, missing, path) == missing) {
			WritePrivateProfileStringA(section, key, value.c_str(), path.c_str());
		}
	}

	bool ReadIniBool(const char * section, const char * key, bool defaultValue, const std::string & path)
	{
		return GetPrivateProfileIntA(section, key, defaultValue ? 1 : 0, path.c_str()) > 0;
	}

	float ReadIniFloat(const char * section, const char * key, float defaultValue, const std::string & path)
	{
		return strtof(ReadIniString(section, key, std::to_string(defaultValue).c_str(), path).c_str(), nullptr);
	}

	std::string SwitchSection(int index)
	{
		return "Switch" + std::to_string(index + 1);
	}

	// [Internal] sSwitch1Ip, sSwitch2Ip: ip of the TP-Link plug, found and written by the plugin
	std::string SwitchIpKey(int index)
	{
		return "sSwitch" + std::to_string(index + 1) + "Ip";
	}

	// same plug names as the Skyrim plugin, so the same lamps work for both games
	std::string DefaultPlugName(int index)
	{
		return "ImmersiveHeatPlug" + std::to_string(index + 1);
	}

	// ---- settings ----

	void WriteDefaultsToIniFile()
	{
		const std::string path = GetIniPath();
		const Settings defaults;

		if (ReadIniString(INI_INTERNAL, "sModVersion", "", path) != MOD_VERSION) {
			WritePrivateProfileStringA(INI_INTERNAL, "sModVersion", MOD_VERSION.c_str(), path.c_str());
		}

		WriteIniIfMissing(INI_GENERAL, "sHomeAssistantUrl", "", path);
		WriteIniIfMissing(INI_GENERAL, "bUseUdp", "1", path);
		WriteIniIfMissing(INI_GENERAL, "bLoggingEnabled", "0", path);
		WriteIniIfMissing(INI_GENERAL, "bNoHeatInMenu", "1", path);

		for (int i = 0; i < SWITCH_COUNT; i++) {
			const std::string section = SwitchSection(i);
			WriteIniIfMissing(INI_INTERNAL, SwitchIpKey(i).c_str(), "", path);
			WriteIniIfMissing(section.c_str(), "sSwitchType", "tplink", path);
			WriteIniIfMissing(section.c_str(), "sPlugName", DefaultPlugName(i), path);
			WriteIniIfMissing(section.c_str(), "sHttpMethod", "GET", path);
			WriteIniIfMissing(section.c_str(), "sOnUrl", "", path);
			WriteIniIfMissing(section.c_str(), "sOffUrl", "", path);
		}

		WriteIniIfMissing(INI_HEAT, "iSunSwitch", std::to_string(defaults.sunSwitch), path);
		WriteIniIfMissing(INI_HEAT, "iFireSwitch", std::to_string(defaults.fireSwitch), path);
		WriteIniIfMissing(INI_HEAT, "bSunlight", "1", path);
		WriteIniIfMissing(INI_HEAT, "fMinSunGlare", std::to_string(defaults.minSunGlare), path);
		WriteIniIfMissing(INI_HEAT, "bSunShadow", "1", path);
		WriteIniIfMissing(INI_HEAT, "iSunRayLayer", std::to_string(defaults.sunRayLayer), path);
		WriteIniIfMissing(INI_HEAT, "bClimateDayHours", "1", path);
		WriteIniIfMissing(INI_HEAT, "fDayStartHour", std::to_string(defaults.dayStartHour), path);
		WriteIniIfMissing(INI_HEAT, "fDayEndHour", std::to_string(defaults.dayEndHour), path);
		WriteIniIfMissing(INI_HEAT, "bNearFire", "1", path);
		WriteIniIfMissing(INI_HEAT, "fFireRadius", std::to_string(defaults.fireRadius), path);
		WriteIniIfMissing(INI_HEAT, "fSmallFireRadius", std::to_string(defaults.smallFireRadius), path);
		WriteIniIfMissing(INI_HEAT, "sSmallFireModels", DEFAULT_SMALL_FIRE_MODELS, path);
		WriteIniIfMissing(INI_HEAT, "sFireModels", DEFAULT_FIRE_MODELS, path);
		WriteIniIfMissing(INI_HEAT, "sFireModelsExclude", DEFAULT_FIRE_MODELS_EXCLUDE, path);
		WriteIniIfMissing(INI_HEAT, "bFireHits", "1", path);
		WriteIniIfMissing(INI_HEAT, "fFireHitSeconds", std::to_string(defaults.fireHitSeconds), path);
		WriteIniIfMissing(INI_HEAT, "sFireWeaponModels", DEFAULT_FIRE_WEAPON_MODELS, path);
		WriteIniIfMissing(INI_HEAT, "fCheckIntervalSeconds", std::to_string(defaults.checkIntervalSeconds), path);
	}

	int ReadSwitchNumber(const char * key, int defaultValue, const std::string & path)
	{
		const int number = GetPrivateProfileIntA(INI_HEAT, key, defaultValue, path.c_str());
		if (number < 0 || number > SWITCH_COUNT) {
			spdlog::info("{}={} is not a switch (0-{}), using 0", key, number, SWITCH_COUNT);
			return 0;
		}
		return number;
	}

	void LoadSettings()
	{
		const std::string path = GetIniPath();
		const Settings defaults;
		Settings & s = _settings;

		s.logging = ReadIniBool(INI_GENERAL, "bLoggingEnabled", defaults.logging, path);
		s.noHeatInMenu = ReadIniBool(INI_GENERAL, "bNoHeatInMenu", defaults.noHeatInMenu, path);

		const std::string homeAssistant = Trim(ReadIniString(INI_GENERAL, "sHomeAssistantUrl", "", path));
		for (int i = 0; i < SWITCH_COUNT; i++) {
			const std::string section = SwitchSection(i);
			SwitchSettings & sw = s.switches[i];
			sw.http = _stricmp(Trim(ReadIniString(section.c_str(), "sSwitchType", "tplink", path)).c_str(), "http") == 0;
			sw.plugName = Trim(ReadIniString(section.c_str(), "sPlugName", DefaultPlugName(i).c_str(), path));
			sw.httpMethod = Trim(ReadIniString(section.c_str(), "sHttpMethod", "GET", path));
			sw.onUrl = Trim(ReadIniString(section.c_str(), "sOnUrl", "", path));
			sw.offUrl = Trim(ReadIniString(section.c_str(), "sOffUrl", "", path));
			if (!homeAssistant.empty()) {
				const std::string webhook = "ImmersiveHeat" + std::to_string(i + 1);
				sw.http = true;
				sw.httpMethod = "POST";
				sw.onUrl = HttpSwitch::HomeAssistantUrl(homeAssistant, webhook, true);
				sw.offUrl = HttpSwitch::HomeAssistantUrl(homeAssistant, webhook, false);
			}
		}

		s.sunSwitch = ReadSwitchNumber("iSunSwitch", defaults.sunSwitch, path);
		s.fireSwitch = ReadSwitchNumber("iFireSwitch", defaults.fireSwitch, path);

		s.sunlight = ReadIniBool(INI_HEAT, "bSunlight", defaults.sunlight, path) && s.sunSwitch != 0;
		s.minSunGlare = ReadIniFloat(INI_HEAT, "fMinSunGlare", defaults.minSunGlare, path);
		s.sunShadow = ReadIniBool(INI_HEAT, "bSunShadow", defaults.sunShadow, path);
		s.sunRayLayer = GetPrivateProfileIntA(INI_HEAT, "iSunRayLayer", defaults.sunRayLayer, path.c_str()) & 0x7F;
		s.climateDayHours = ReadIniBool(INI_HEAT, "bClimateDayHours", defaults.climateDayHours, path);
		s.dayStartHour = ReadIniFloat(INI_HEAT, "fDayStartHour", defaults.dayStartHour, path);
		s.dayEndHour = ReadIniFloat(INI_HEAT, "fDayEndHour", defaults.dayEndHour, path);
		s.nearFire = ReadIniBool(INI_HEAT, "bNearFire", defaults.nearFire, path) && s.fireSwitch != 0;
		s.fireRadius = ReadIniFloat(INI_HEAT, "fFireRadius", defaults.fireRadius, path);
		s.smallFireRadius = ReadIniFloat(INI_HEAT, "fSmallFireRadius", defaults.smallFireRadius, path);
		s.smallFireModels = SplitLowerList(ReadIniString(INI_HEAT, "sSmallFireModels", DEFAULT_SMALL_FIRE_MODELS, path));
		s.fireModels = SplitLowerList(ReadIniString(INI_HEAT, "sFireModels", DEFAULT_FIRE_MODELS, path));
		s.fireModelsExclude = SplitLowerList(ReadIniString(INI_HEAT, "sFireModelsExclude", DEFAULT_FIRE_MODELS_EXCLUDE, path));
		s.fireHits = ReadIniBool(INI_HEAT, "bFireHits", defaults.fireHits, path) && s.fireSwitch != 0;
		s.fireHitSeconds = ReadIniFloat(INI_HEAT, "fFireHitSeconds", defaults.fireHitSeconds, path);
		s.fireWeaponModels = SplitLowerList(ReadIniString(INI_HEAT, "sFireWeaponModels", DEFAULT_FIRE_WEAPON_MODELS, path));
		s.checkIntervalSeconds = std::max(MIN_CHECK_INTERVAL_SECONDS, ReadIniFloat(INI_HEAT, "fCheckIntervalSeconds", defaults.checkIntervalSeconds, path));

		// only switches a heat source points at are searched and switched
		const bool anyFire = s.nearFire || s.fireHits;
		for (int i = 0; i < SWITCH_COUNT; i++) {
			_switches[i].used = (s.sunlight && s.sunSwitch == i + 1) || (anyFire && s.fireSwitch == i + 1);
			if (_switches[i].used) {
				const SwitchSettings & sw = s.switches[i];
				spdlog::info("Switch {}: {} {}", i + 1, sw.http ? "http" : "tplink", sw.http ? sw.onUrl : sw.plugName);
			}
		}

		spdlog::info("Heat sources: sunlight {} on switch {}; fire nearby {} (radius {}), fire hits {} on switch {}",
			s.sunlight, s.sunSwitch, s.nearFire, s.fireRadius, s.fireHits, s.fireSwitch);
	}

	// ---- switches ----

	bool LoadConnectionConfig(int index, TPLinkHelper & tpLinkConn)
	{
		SwitchState & state = _switches[index];
		std::lock_guard<std::mutex> lock(state.connectionMutex);
		return tpLinkConn.LoadIPConfigFromFile(GetIniPath(), SwitchIpKey(index).c_str(), state.useUDP, state.workingIp);
	}

	void ScanThread()
	{
		for (int i = 0; i < SWITCH_COUNT && _keepRunning; i++) {
			// a http switch is addressed by its urls, there is nothing to search
			if (!_switches[i].used || _settings.switches[i].http) {
				continue;
			}
			const std::string ip = FindTpLinkPlug(GetIniPath(), SwitchIpKey(i).c_str(), _settings.switches[i].plugName, _settings.logging, _keepRunning);
			if (!ip.empty()) {
				std::lock_guard<std::mutex> lock(_switches[i].connectionMutex);
				_switches[i].workingIp = ip;
			}
		}
		_scanFinished = true;
	}

	bool SwitchPlug(int index, bool on)
	{
		const SwitchSettings & sw = _settings.switches[index];
		bool worked = false;
		if (sw.http) {
			worked = HttpSwitch::Request(sw.httpMethod, on ? sw.onUrl : sw.offUrl, _settings.logging);
		}
		else {
			TPLinkHelper tpLinkConn(_settings.logging);
			worked = LoadConnectionConfig(index, tpLinkConn) && tpLinkConn.SwitchRelayState(on ? 1 : 0);
		}
		_switchResult = worked ? 1 : -1;
		return worked;
	}

	void SetWanted(int index, bool on)
	{
		if (index < 0 || index >= SWITCH_COUNT) {
			return;
		}
		if (_switches[index].wanted.exchange(on) != on) {
			// the lock makes sure the switch thread is either before its check or waiting, so the signal is not lost
			{ std::lock_guard<std::mutex> lock(_signalMutex); }
			_switchSignal.notify_one();
		}
	}

	// sets every switch from the two heat sources; a switch used by both is on for either
	void SetHeat(bool sun, bool fire)
	{
		for (int i = 0; i < SWITCH_COUNT; i++) {
			SetWanted(i, (sun && _settings.sunSwitch == i + 1) || (fire && _settings.fireSwitch == i + 1));
		}
	}

	// Sends the wanted state to each used switch when it changes, and again every RELAY_RESEND_INTERVAL_SECONDS.
	void SwitchThread()
	{
		// wait until the plugs were found, otherwise the first switch commands would get lost
		while (!_scanFinished && _keepRunning) {
			Sleep(100);
		}

		std::array<bool, SWITCH_COUNT> sent{};
		std::array<Clock::time_point, SWITCH_COUNT> lastSend{};
		std::array<bool, SWITCH_COUNT> firstSend;
		firstSend.fill(true);

		const auto anyChange = [&sent] {
			for (int i = 0; i < SWITCH_COUNT; i++) {
				if (_switches[i].used && _switches[i].wanted != sent[i]) {
					return true;
				}
			}
			return false;
		};

		while (_keepRunning) {
			for (int i = 0; i < SWITCH_COUNT; i++) {
				if (!_switches[i].used) {
					continue;
				}
				const bool wanted = _switches[i].wanted;
				if (firstSend[i] || wanted != sent[i] || SecondsSince(lastSend[i]) >= RELAY_RESEND_INTERVAL_SECONDS) {
					if (firstSend[i] || wanted != sent[i]) {
						Log("Switching switch %d %s", i + 1, wanted ? "on" : "off");
					}
					if (!SwitchPlug(i, wanted)) {
						Log("Switching switch %d %s failed", i + 1, wanted ? "on" : "off");
					}
					sent[i] = wanted;
					lastSend[i] = Clock::now();
					firstSend[i] = false;
				}
			}
			// wake up right when a wanted state changes, at the latest for the resend
			std::unique_lock<std::mutex> lock(_signalMutex);
			_switchSignal.wait_for(lock, std::chrono::milliseconds(500), [&anyChange] { return !_keepRunning || anyChange(); });
		}
	}

	// ---- game access ----
	// CommonLibF4 only knows some addresses for the original game; these take the next gen id as second id.

	RE::Sky * GetSky()
	{
		using func_t = RE::Sky * (*)();
		static REL::Relocation<func_t> func{ REL::RelocationID(484694, 2192448) };
		return func();
	}

	// A ray through the collision of the world (RE::bhkPickData, built without its constructor of CommonLibF4).
	bool RayHits(RE::TESObjectCELL * cell, const RE::NiPoint3 & from, const RE::NiPoint3 & to)
	{
		using ctor_t = void * (*)(void *);
		using setStartEnd_t = void (*)(void *, const RE::NiPoint3 &, const RE::NiPoint3 &);
		using hasHit_t = bool (*)(void *);
		using pick_t = RE::NiAVObject * (*)(RE::TESObjectCELL *, void *);
		static REL::Relocation<ctor_t> ctor{ REL::RelocationID(526783, 2230668) };
		static REL::Relocation<setStartEnd_t> setStartEnd{ REL::RelocationID(747470, 2236622) };
		static REL::Relocation<hasHit_t> hasHit{ REL::RelocationID(1181584, 2277770) };
		static REL::Relocation<pick_t> pick{ REL::RelocationID(434717, 2200263) };

		alignas(16) std::byte storage[sizeof(RE::bhkPickData)]{};
		auto data = reinterpret_cast<RE::bhkPickData *>(storage);
		ctor(data);
		setStartEnd(data, from, to);
		data->collisionFilter.filter = static_cast<std::uint32_t>(_settings.sunRayLayer);
		pick(cell, data);
		return hasHit(data);
	}

	// ---- heat sources ----

	// the model path of a form with a model, "" for none
	std::string LowerModelPath(const RE::TESForm * form)
	{
		auto model = form ? form->As<RE::TESModel>() : nullptr;
		const char * path = model ? model->model.c_str() : nullptr;
		return ToLower(path ? path : "");
	}

	// only placed objects can be a fire nearby (not weapons, ammo, items)
	bool CanBeFire(RE::ENUM_FORM_ID type)
	{
		using T = RE::ENUM_FORM_ID;
		return type == T::kSTAT || type == T::kMSTT || type == T::kACTI || type == T::kFURN || type == T::kLIGH || type == T::kHAZD;
	}

	FireSize GetFireSize(RE::TESBoundObject * base)
	{
		if (!base) {
			return FireSize::kNone;
		}
		const auto cached = _fireModelCache.find(base->GetFormID());
		if (cached != _fireModelCache.end()) {
			return cached->second;
		}

		FireSize size = FireSize::kNone;
		if (CanBeFire(base->GetFormType())) {
			const std::string path = LowerModelPath(base);
			if (!path.empty() && !ContainsAny(path, _settings.fireModelsExclude)) {
				if (ContainsAny(path, _settings.smallFireModels)) {
					size = FireSize::kSmall;
				}
				else if (ContainsAny(path, _settings.fireModels)) {
					size = FireSize::kLarge;
				}
			}
			if (size != FireSize::kNone) {
				Log("Fire model (%s): %s (%08X)", size == FireSize::kSmall ? "small" : "large", path.c_str(), base->GetFormID());
			}
		}
		_fireModelCache[base->GetFormID()] = size;
		return size;
	}

	bool IsFireInCell(RE::TESObjectCELL * cell, RE::PlayerCharacter * player, const RE::NiPoint3 & playerPosition)
	{
		for (const auto & ref : cell->references) {
			if (!ref || ref.get() == player || ref->IsDisabled()) {
				continue;
			}
			const FireSize size = GetFireSize(ref->GetObjectReference());
			if (size == FireSize::kNone) {
				continue;
			}
			const RE::NiPoint3 delta = ref->GetPosition() - playerPosition;
			const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
			if ((size == FireSize::kLarge && distance <= _settings.fireRadius) || (size == FireSize::kSmall && distance <= _settings.smallFireRadius)) {
				return true;
			}
		}
		return false;
	}

	// the cell of the player, outside all loaded cells of the worldspace (the fire may be across a cell border)
	bool IsNearFire(RE::PlayerCharacter * player)
	{
		auto cell = player->GetParentCell();
		if (!cell) {
			return false;
		}
		const RE::NiPoint3 position = player->GetPosition();
		if (cell->IsInterior() || !cell->worldSpace) {
			return IsFireInCell(cell, player, position);
		}
		for (const auto & entry : cell->worldSpace->cellMap) {
			RE::TESObjectCELL * worldCell = entry.second;
			if (worldCell && worldCell->cellState.get() == RE::TESObjectCELL::CELL_STATE::kAttached && IsFireInCell(worldCell, player, position)) {
				return true;
			}
		}
		return false;
	}

	bool IsNearFireCached(RE::PlayerCharacter * player)
	{
		if (SecondsSince(_lastFireScan) >= FIRE_SCAN_INTERVAL_SECONDS) {
			_lastFireScan = Clock::now();
			_nearFire = IsNearFire(player);
		}
		return _nearFire;
	}

	// 0-1, the weather data starts with the wind speed, the sun glare is the fifth byte (as in Skyrim)
	float GetSunGlare(const RE::TESWeather * weather)
	{
		return weather ? static_cast<std::uint8_t>(weather->weatherData[4]) / 255.0f : 0.0f;
	}

	// Sun glare of the sky, blended during a weather transition.
	float GetSkySunGlare(RE::Sky * sky)
	{
		const float sunGlare = GetSunGlare(sky->currentWeather);
		const float share = std::clamp(sky->currentWeatherPct, 0.0f, 1.0f);
		if (!sky->lastWeather || share >= 1.0f) {
			return sunGlare;
		}
		return GetSunGlare(sky->lastWeather) * (1.0f - share) + sunGlare * share;
	}

	// The hours of daylight, from the climate if it has its sunrise and sunset (the same share of the sunrise and
	// sunset as in the Skyrim plugin), otherwise fDayStartHour / fDayEndHour.
	void GetDayHours(RE::Sky * sky, float & start, float & end)
	{
		start = _settings.dayStartHour;
		end = _settings.dayEndHour;
		if (!_settings.climateDayHours || !sky->currentClimate) {
			return;
		}
		// TNAM: sunrise begin, sunrise end, sunset begin, sunset end, in steps of 10 minutes
		const auto * timing = reinterpret_cast<const std::uint8_t *>(sky->currentClimate->data);
		const auto hourInTransition = [](std::uint8_t begin, std::uint8_t finish) { return (begin + DAY_TRANSITION_SHARE * (finish - begin)) / 6.0f; };
		const float sunUp = hourInTransition(timing[0], timing[1]);
		const float sunDown = hourInTransition(timing[2], timing[3]);
		if (sunUp > 0.0f && sunUp < sunDown && sunDown < 24.0f) {
			start = sunUp;
			end = sunDown;
		}

		static RE::TESClimate * loggedClimate = nullptr;
		if (loggedClimate != sky->currentClimate) {
			loggedClimate = sky->currentClimate;
			Log("Climate %08X: day from %.2f to %.2f", loggedClimate->GetFormID(), start, end);
		}
	}

	// Direction to the sun: it rises in the east (+x), passes the south (-y) at noon and sets in the west.
	RE::NiPoint3 SunDirection(float hour, float dayStart, float dayEnd)
	{
		const float share = std::clamp((hour - dayStart) / (dayEnd - dayStart), 0.0f, 1.0f);
		const float azimuth = PI * share;
		const float elevation = std::max(0.05f, std::sin(PI * share)) * SUN_NOON_ELEVATION * PI / 180.0f;
		const float horizontal = std::cos(elevation);
		return RE::NiPoint3(horizontal * std::cos(azimuth), -horizontal * std::sin(azimuth), std::sin(elevation));
	}

	bool IsInSunlight(RE::PlayerCharacter * player)
	{
		auto sky = GetSky();
		auto cell = player->GetParentCell();
		if (!sky || !sky->currentWeather || !cell || cell->IsInterior()) {
			return false;
		}

		const float hour = sky->currentGameHour;
		float dayStart = 0.0f;
		float dayEnd = 0.0f;
		GetDayHours(sky, dayStart, dayEnd);
		if (hour <= dayStart || hour >= dayEnd || GetSkySunGlare(sky) <= _settings.minSunGlare) {
			return false;
		}
		if (!_settings.sunShadow) {
			return true;
		}

		const RE::NiPoint3 head = player->GetPosition() + RE::NiPoint3(0.0f, 0.0f, HEAD_HEIGHT);
		const RE::NiPoint3 sun = SunDirection(hour, dayStart, dayEnd);
		const bool blocked = RayHits(cell, head, head + RE::NiPoint3(sun.x * SUN_RAY_LENGTH, sun.y * SUN_RAY_LENGTH, sun.z * SUN_RAY_LENGTH));
		if (blocked != _lastSunBlocked) {
			_lastSunBlocked = blocked;
			Log("Sun %s (hour %.2f)", blocked ? "blocked: shadow" : "free", hour);
		}
		return !blocked;
	}

	// ---- check on the main thread ----

	// Tells the player once per game start if the switch works, so nobody has to read the log for that.
	void ShowSwitchResult()
	{
		static bool shown = false;
		const int result = _switchResult;
		if (shown || result == 0) {
			return;
		}
		shown = true;
		RE::SendHUDMessage::ShowHUDMessage(result > 0 ? "Immersive Heat: connected" : "Immersive Heat: no connection, see the log", "", false, result < 0);
	}

	// Reads the heat sources and sets the switches. Calls into the engine, so it must run on the game's main thread.
	void UpdateFromGameState()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->GetParentCell()) {
			return;
		}
		ShowSwitchResult();
		if (_menuOpen && _settings.noHeatInMenu) {
			SetHeat(false, false);
			return;
		}

		const bool sun = _settings.sunlight && IsInSunlight(player);
		std::string fireReason;
		if (Clock::now() < FromTicks(_fireEventUntil.load())) {
			fireReason = "fire event"; // hit by fire or burning, see the events below
		}
		else if (_settings.nearFire && IsNearFireCached(player)) {
			fireReason = "fire nearby";
		}

		SetHeat(sun, !fireReason.empty());

		const std::string reason = std::string(sun ? "sunlight" : "") + (sun && !fireReason.empty() ? ", " : "") + fireReason;
		if (reason != _lastReason) {
			Log("Heat: %s", reason.empty() ? "none" : reason.c_str());
			_lastReason = reason;
		}
	}

	bool RequestGameStateCheck()
	{
		// only one check at a time, so tasks do not pile up while the game does not process them
		if (_checkPending.exchange(true)) {
			return false;
		}
		_checkQueuedAt = Clock::now().time_since_epoch().count();
		F4SE::GetTaskInterface()->AddTask([]() {
			UpdateFromGameState();
			_checkPending = false;
		});
		return true;
	}

	bool IsGameInForeground()
	{
		// the VR game keeps running without the window focus
		if (REL::Module::IsVR()) {
			return true;
		}
		DWORD processId = 0;
		GetWindowThreadProcessId(GetForegroundWindow(), &processId);
		return processId == GetCurrentProcessId();
	}

	// Background timer: queues the heat check on the main thread and switches off when nobody plays.
	void HeatCheckThread()
	{
		Clock::time_point lastCheck;
		while (_keepRunning) {
			if (!IsGameInForeground() || (_menuOpen && _settings.noHeatInMenu)) {
				SetHeat(false, false);
				lastCheck = {}; // check right when the game is back
			}
			else if (SecondsSince(lastCheck) >= _settings.checkIntervalSeconds) {
				lastCheck = Clock::now();
				if (!RequestGameStateCheck() && SecondsSince(FromTicks(_checkQueuedAt.load())) >= GAME_STALLED_SECONDS) {
					SetHeat(false, false);
				}
			}
			Sleep(TIMER_INTERVAL_MS);
		}
	}

	// ---- events ----
	// The fire events switch the fire switch on directly, without waiting for the next check on the main thread.
	// The heat then lasts at least fFireHitSeconds.

	bool IsPlayer(const RE::NiPointer<RE::TESObjectREFR> & ref)
	{
		return ref && ref.get() == RE::PlayerCharacter::GetSingleton();
	}

	void HeatFromFireEvent(const char * what)
	{
		const auto until = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(_settings.fireHitSeconds));
		_fireEventUntil = until.time_since_epoch().count();
		Log("Fire event: %s", what);
		SetWanted(_settings.fireSwitch - 1, true);
	}

	std::string LowerModelPathOf(RE::TESFormID formId)
	{
		return formId ? LowerModelPath(RE::TESForm::GetFormByID(formId)) : "";
	}

	// hit by a weapon, projectile or ammo with a fire model: flamer, molotov, incendiary ammo
	class HitEventHandler : public RE::BSTEventSink<RE::TESHitEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent & event, RE::BSTEventSource<RE::TESHitEvent> *) override
		{
			if (!_settings.fireHits || !IsPlayer(event.target)) {
				return RE::BSEventNotifyControl::kContinue;
			}
			const std::string weapon = LowerModelPathOf(event.sourceFormID);
			const std::string projectile = LowerModelPathOf(event.projectileFormID);
			const std::string ammo = event.usesHitData ? LowerModelPath(event.hitData.ammo) : "";
			Log("Hit: weapon '%s', projectile '%s', ammo '%s'", weapon.c_str(), projectile.c_str(), ammo.c_str());
			if (ContainsAny(weapon, _settings.fireWeaponModels) || ContainsAny(projectile, _settings.fireWeaponModels) || ContainsAny(ammo, _settings.fireWeaponModels)) {
				HeatFromFireEvent("hit by fire");
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	// a magic effect with a fire art applied to the player: burning
	class MagicEffectApplyEventHandler : public RE::BSTEventSink<RE::TESMagicEffectApplyEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESMagicEffectApplyEvent & event, RE::BSTEventSource<RE::TESMagicEffectApplyEvent> *) override
		{
			if (!_settings.fireHits || !IsPlayer(event.target)) {
				return RE::BSEventNotifyControl::kContinue;
			}
			auto form = RE::TESForm::GetFormByID(event.magicEffectFormID);
			auto effect = form ? form->As<RE::EffectSetting>() : nullptr;
			if (!effect) {
				return RE::BSEventNotifyControl::kContinue;
			}
			const std::string hitArt = LowerModelPath(effect->data.hitEffectArt);
			const std::string enchantArt = LowerModelPath(effect->data.enchantEffectArt);
			Log("Magic effect %08X: hit art '%s', enchant art '%s'", event.magicEffectFormID, hitArt.c_str(), enchantArt.c_str());
			if (ContainsAny(hitArt, _settings.fireWeaponModels) || ContainsAny(enchantArt, _settings.fireWeaponModels)) {
				HeatFromFireEvent("burning");
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	// Tracks the open menus that pause the game.
	class MenuWatcher : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent & a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent> *) override
		{
			const std::string name = a_event.menuName.c_str();
			if (PAUSING_MENUS.contains(name)) {
				if (a_event.opening) {
					_open.insert(name);
				}
				else {
					_open.erase(name);
				}
				_menuOpen = !_open.empty();
				if (_menuOpen && _settings.noHeatInMenu) {
					SetHeat(false, false);
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}

	private:
		std::unordered_set<std::string> _open;
	};

	HitEventHandler _hitEventHandler;
	MagicEffectApplyEventHandler _magicEffectApplyEventHandler;
	MenuWatcher _menuWatcher;

	// ---- start / exit ----

	void StartMod()
	{
		if (_started.exchange(true)) {
			return;
		}
		WriteDefaultsToIniFile();
		LoadSettings();
		spdlog::info("Immersive Heat {} started, {}", MOD_VERSION, REL::Module::IsVR() ? "Fallout 4 VR" : "Fallout 4");

		if (auto source = RE::TESHitEvent::GetEventSource()) {
			source->RegisterSink(&_hitEventHandler);
		}
		if (auto source = RE::TESMagicEffectApplyEvent::GetEventSource()) {
			source->RegisterSink(&_magicEffectApplyEventHandler);
		}
		if (auto ui = RE::UI::GetSingleton()) {
			ui->RegisterSink<RE::MenuOpenCloseEvent>(&_menuWatcher);
		}

		std::thread(ScanThread).detach();
		std::thread(SwitchThread).detach();
		std::thread(HeatCheckThread).detach();
	}

	void SwitchOffOnExit()
	{
		_keepRunning = false;
		// only the used switches; before the settings are loaded no switch is used
		for (int i = 0; i < SWITCH_COUNT; i++) {
			if (_switches[i].used) {
				_switches[i].wanted = false;
				SwitchPlug(i, false);
			}
		}
	}

	namespace
	{
		// Switches the plugs off when the game exits. Defined last in this file on purpose: objects of one
		// file are destroyed in reverse order, so the settings and connection data above still exist then.
		struct ExitHandler
		{
			~ExitHandler() { SwitchOffOnExit(); }
		} _exitHandler;
	}
}
