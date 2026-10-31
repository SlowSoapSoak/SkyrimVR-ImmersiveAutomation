#include "ImmersiveHeat.h"
#include "HttpSwitch.h"
#include "PlugFinder.h"
#include "TPLinkSPHelper.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>


namespace ImmersiveHeat {

	namespace
	{
		using Clock = std::chrono::steady_clock;

		// ini sections
		const char * INI_INTERNAL = "Internal";
		const char * INI_GENERAL = "GeneralModConfig";
		const char * INI_HEAT = "HeatSources";

		// how often the timer thread wakes up (focus check, heat check interval)
		const int TIMER_INTERVAL_MS = 25;
		// the search for fire nearby goes through all references in range, so it runs less often than the other checks
		const float FIRE_SCAN_INTERVAL_SECONDS = 1.0f;
		const float MIN_CHECK_INTERVAL_SECONDS = 0.05f;
		// the plug state is sent again after this time, in case a (udp) message got lost or the plug restarted
		const float RELAY_RESEND_INTERVAL_SECONDS = 15.0f;
		// a queued check that did not run after this time means the game stands still (frozen, long loading screen)
		const float GAME_STALLED_SECONDS = 10.0f;

		const RE::FormID KEYWORD_ID_LOC_SET_OUTDOOR = 0x0130F3;

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

			// direct sunlight: outside, at day, bright sun and the player standing in the light
			bool sunlight = true;
			float minSunGlare = 0.4f;     // 0-1
			float minLightLevel = 180.0f; // light level at the player (Actor.GetLightLevel), 0 disables the check
			// sunlight stays on until the light level drops this much below minLightLevel, so the plug does not
			// flicker at the edge of a shadow
			float lightLevelHysteresis = 4.0f;
			// the light level includes fire light, so next to a fire it takes this much more to count as sunlight
			float fireLightMargin = 40.0f;
			// day from the sunrise and sunset of the current climate (weather and season mods change them),
			// dayStartHour and dayEndHour without a climate or with climateDayHours off
			bool climateDayHours = true;
			float dayStartHour = 7.2f;
			float dayEndHour = 17.76f;

			// fire nearby: a reference whose model path contains one of fireModels (and none of fireModelsExclude)
			bool nearFire = true;
			float fireRadius = 350.0f;    // game units, about 5 m
			float smallFireRadius = 100.0f; // candles and torches only count this close
			std::vector<std::string> fireModels;
			std::vector<std::string> smallFireModels;
			std::vector<std::string> fireModelsExclude;

			// fire attacks: hit by fire, burning (fire damage effects on the player), casting own fire spells
			bool fireHits = true;
			float fireHitSeconds = 3.0f;
			bool ownFireSpells = true;

			// time between two checks of the heat sources
			float checkIntervalSeconds = 0.1f;
		};

		const char * DEFAULT_FIRE_MODELS = "fire,flame,brazier,forge,smelter,lava,embers,hearth";
		const char * DEFAULT_SMALL_FIRE_MODELS = "candle,chandel,sconce,torch";
		const char * DEFAULT_FIRE_MODELS_EXCLUDE = "firewood,fireplace,nofire,unlit,off,nocandle,candlehearth,signage,firefly,spit,cookingstand,underforge,wrskyforge,fireweapon,trap,forgebase";

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
		// wakes the switch thread when a wanted state changes
		std::mutex _signalMutex;
		std::condition_variable _switchSignal;

		std::atomic<bool> _scanFinished(false);
		std::atomic<bool> _checkPending(false);
		std::atomic<Clock::rep> _checkQueuedAt(0);
		// end of the heat after a fire event (steady_clock ticks)
		std::atomic<Clock::rep> _fireEventUntil(0);

		RE::BGSKeyword * keywordLocSetOutdoor = nullptr;
		// model check results per base form, only used on the main thread
		enum class FireSize
		{
			kNone,
			kSmall,
			kLarge
		};

		std::unordered_map<RE::FormID, FireSize> _fireModelCache;
		float _lightLevel = 0.0f; // last light level at the player, for the log
		// result of the last switch command for the message in the game: 0 unknown, 1 worked, -1 failed
		std::atomic<int> _switchResult(0);
		// last logged heat state, to log only changes
		std::string _lastReason;
		// main thread only: result of the last fire search and of the last sunlight check (for the hysteresis)
		bool _nearFire = false;
		Clock::time_point _lastFireScan;
		bool _inSunlight = false;
	}

	// ---- helpers ----

	void Log(const char * fmt, ...)
	{
		if (!_settings.logging) {
			return;
		}
		va_list args;
		char buffer[4096];
		va_start(args, fmt);
		vsprintf_s(buffer, sizeof(buffer), fmt, args);
		va_end(args);
		_MESSAGE("%s", buffer);
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
		std::vector<std::string> result;
		std::stringstream stream(list);
		std::string entry;
		while (std::getline(stream, entry, ',')) {
			entry = ToLower(Trim(entry));
			if (!entry.empty()) {
				result.push_back(entry);
			}
		}
		return result;
	}

	std::string GetIniPath()
	{
		const auto dir = SKSE::log::log_directory();
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

	// [Switch1], [Switch2]
	std::string SwitchSection(int index)
	{
		return "Switch" + std::to_string(index + 1);
	}

	// [Internal] sSwitch1Ip, sSwitch2Ip: ip of the TP-Link plug, found and written by the plugin
	std::string SwitchIpKey(int index)
	{
		return "sSwitch" + std::to_string(index + 1) + "Ip";
	}

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
		WriteIniIfMissing(INI_HEAT, "fMinLightLevel", std::to_string(defaults.minLightLevel), path);
		WriteIniIfMissing(INI_HEAT, "fLightLevelHysteresis", std::to_string(defaults.lightLevelHysteresis), path);
		WriteIniIfMissing(INI_HEAT, "bClimateDayHours", "1", path);
		WriteIniIfMissing(INI_HEAT, "fDayStartHour", std::to_string(defaults.dayStartHour), path);
		WriteIniIfMissing(INI_HEAT, "fDayEndHour", std::to_string(defaults.dayEndHour), path);
		WriteIniIfMissing(INI_HEAT, "bNearFire", "1", path);
		WriteIniIfMissing(INI_HEAT, "fFireLightMargin", std::to_string(defaults.fireLightMargin), path);
		WriteIniIfMissing(INI_HEAT, "fFireRadius", std::to_string(defaults.fireRadius), path);
		WriteIniIfMissing(INI_HEAT, "fSmallFireRadius", std::to_string(defaults.smallFireRadius), path);
		WriteIniIfMissing(INI_HEAT, "sSmallFireModels", DEFAULT_SMALL_FIRE_MODELS, path);
		WriteIniIfMissing(INI_HEAT, "sFireModels", DEFAULT_FIRE_MODELS, path);
		WriteIniIfMissing(INI_HEAT, "sFireModelsExclude", DEFAULT_FIRE_MODELS_EXCLUDE, path);
		WriteIniIfMissing(INI_HEAT, "bFireHits", "1", path);
		WriteIniIfMissing(INI_HEAT, "fFireHitSeconds", std::to_string(defaults.fireHitSeconds), path);
		WriteIniIfMissing(INI_HEAT, "bOwnFireSpells", "1", path);
		WriteIniIfMissing(INI_HEAT, "fCheckIntervalSeconds", std::to_string(defaults.checkIntervalSeconds), path);
	}

	int ReadSwitchNumber(const char * key, int defaultValue, const std::string & path)
	{
		const int number = GetPrivateProfileIntA(INI_HEAT, key, defaultValue, path.c_str());
		if (number < 0 || number > SWITCH_COUNT) {
			_MESSAGE("%s=%d is not a switch (0-%d), using 0", key, number, SWITCH_COUNT);
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
		s.minLightLevel = ReadIniFloat(INI_HEAT, "fMinLightLevel", defaults.minLightLevel, path);
		s.lightLevelHysteresis = std::max(0.0f, ReadIniFloat(INI_HEAT, "fLightLevelHysteresis", defaults.lightLevelHysteresis, path));
		s.climateDayHours = ReadIniBool(INI_HEAT, "bClimateDayHours", defaults.climateDayHours, path);
		s.dayStartHour = ReadIniFloat(INI_HEAT, "fDayStartHour", defaults.dayStartHour, path);
		s.dayEndHour = ReadIniFloat(INI_HEAT, "fDayEndHour", defaults.dayEndHour, path);
		s.nearFire = ReadIniBool(INI_HEAT, "bNearFire", defaults.nearFire, path) && s.fireSwitch != 0;
		s.fireLightMargin = std::max(0.0f, ReadIniFloat(INI_HEAT, "fFireLightMargin", defaults.fireLightMargin, path));
		s.fireRadius = ReadIniFloat(INI_HEAT, "fFireRadius", defaults.fireRadius, path);
		s.smallFireRadius = ReadIniFloat(INI_HEAT, "fSmallFireRadius", defaults.smallFireRadius, path);
		s.smallFireModels = SplitLowerList(ReadIniString(INI_HEAT, "sSmallFireModels", DEFAULT_SMALL_FIRE_MODELS, path));
		s.fireModels = SplitLowerList(ReadIniString(INI_HEAT, "sFireModels", DEFAULT_FIRE_MODELS, path));
		s.fireModelsExclude = SplitLowerList(ReadIniString(INI_HEAT, "sFireModelsExclude", DEFAULT_FIRE_MODELS_EXCLUDE, path));
		s.fireHits = ReadIniBool(INI_HEAT, "bFireHits", defaults.fireHits, path) && s.fireSwitch != 0;
		s.fireHitSeconds = ReadIniFloat(INI_HEAT, "fFireHitSeconds", defaults.fireHitSeconds, path);
		s.ownFireSpells = ReadIniBool(INI_HEAT, "bOwnFireSpells", defaults.ownFireSpells, path) && s.fireSwitch != 0;
		s.checkIntervalSeconds = std::max(MIN_CHECK_INTERVAL_SECONDS, ReadIniFloat(INI_HEAT, "fCheckIntervalSeconds", defaults.checkIntervalSeconds, path));

		// only switches a heat source points at are searched and switched
		const bool anyFire = s.nearFire || s.fireHits || s.ownFireSpells;
		for (int i = 0; i < SWITCH_COUNT; i++) {
			_switches[i].used = (s.sunlight && s.sunSwitch == i + 1) || (anyFire && s.fireSwitch == i + 1);
			if (_switches[i].used) {
				const SwitchSettings & sw = s.switches[i];
				_MESSAGE("Switch %d: %s %s", i + 1, sw.http ? "http" : "tplink", sw.http ? sw.onUrl.c_str() : sw.plugName.c_str());
			}
		}

		_MESSAGE("Heat sources: sunlight %d on switch %d; fire nearby %d (radius %g), fire hits %d, own fire spells %d on switch %d",
			s.sunlight, s.sunSwitch, s.nearFire, s.fireRadius, s.fireHits, s.ownFireSpells, s.fireSwitch);
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

	// ---- heat sources ----

	bool IsFireEffect(const RE::EffectSetting * effect)
	{
		// fire damage of spells, shouts, enchantments and hazards is resisted with fire resistance
		return effect && effect->IsHostile() && effect->data.resistVariable == RE::ActorValue::kResistFire;
	}

	bool IsFireMagic(RE::MagicItem * magic)
	{
		if (!magic) {
			return false;
		}
		return std::any_of(magic->effects.begin(), magic->effects.end(), [](RE::Effect * effect) { return effect && IsFireEffect(effect->baseEffect); });
	}

	// a spell / shout, or a weapon with a fire enchantment
	bool IsFireSource(RE::FormID formId)
	{
		auto form = formId ? RE::TESForm::LookupByID(formId) : nullptr;
		if (!form) {
			return false;
		}
		if (auto magic = form->As<RE::MagicItem>()) {
			return IsFireMagic(magic);
		}
		if (auto enchantable = form->As<RE::TESEnchantableForm>()) {
			return IsFireMagic(enchantable->formEnchanting);
		}
		return false;
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
		auto model = base->As<RE::TESModel>();
		if (model && !base->IsInventoryObject()) { // a potion of fire resistance is no fire
			const char * path = model->GetModel();
			const std::string lower = ToLower(path ? path : "");
			const auto contains = [&lower](const std::string & word) { return lower.find(word) != std::string::npos; };
			const auto containsAny = [&contains](const std::vector<std::string> & words) { return std::any_of(words.begin(), words.end(), contains); };
			if (!lower.empty() && !containsAny(_settings.fireModelsExclude)) {
				if (containsAny(_settings.smallFireModels)) {
					size = FireSize::kSmall;
				}
				else if (containsAny(_settings.fireModels)) {
					size = FireSize::kLarge;
				}
			}
			if (size != FireSize::kNone) {
				Log("Fire model (%s): %s (%08X)", size == FireSize::kSmall ? "small" : "large", path, base->GetFormID());
			}
		}
		_fireModelCache[base->GetFormID()] = size;
		return size;
	}

	bool IsNearFire(RE::PlayerCharacter * player)
	{
		auto tes = RE::TES::GetSingleton();
		if (!tes) {
			return false;
		}
		bool found = false;
		const auto playerPosition = player->GetPosition();
		tes->ForEachReferenceInRange(player, std::max(_settings.fireRadius, _settings.smallFireRadius), [&](RE::TESObjectREFR * ref) {
			if (ref && ref != player && !ref->IsDisabled() && ref->Is3DLoaded()) {
				const FireSize size = GetFireSize(ref->GetBaseObject());
				const float distance = playerPosition.GetDistance(ref->GetPosition());
				if ((size == FireSize::kLarge && distance <= _settings.fireRadius) || (size == FireSize::kSmall && distance <= _settings.smallFireRadius)) {
					found = true;
					return RE::BSContainer::ForEachResult::kStop;
				}
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		return found;
	}

	bool IsNearFireCached(RE::PlayerCharacter * player)
	{
		if (SecondsSince(_lastFireScan) >= FIRE_SCAN_INTERVAL_SECONDS) {
			_lastFireScan = Clock::now();
			_nearFire = IsNearFire(player);
		}
		return _nearFire;
	}

	// burning: fire damage effects active on the player (fire spells, dragon breath, hazards, enchantments)
	bool IsBurning(RE::PlayerCharacter * player)
	{
		auto effects = player->AsMagicTarget()->GetActiveEffectList();
		if (!effects) {
			return false;
		}
		for (auto effect : *effects) {
			if (effect && !effect->flags.any(RE::ActiveEffect::Flag::kInactive) && !effect->flags.any(RE::ActiveEffect::Flag::kDispelled)
				&& IsFireEffect(effect->GetBaseObject())) {
				return true;
			}
		}
		return false;
	}

	bool IsCastingFire(RE::PlayerCharacter * player)
	{
		using Source = RE::MagicSystem::CastingSource;
		using State = RE::MagicCaster::State;
		for (const Source source : { Source::kLeftHand, Source::kRightHand, Source::kOther }) {
			auto caster = player->GetMagicCaster(source);
			if (caster && (caster->state == State::kCharging || caster->state == State::kCasting) && IsFireMagic(caster->currentSpell)) {
				return true;
			}
		}
		return false;
	}

	// 0-1, CommonLib types sunGlare signed, the value is 0-255
	float GetSunGlare(const RE::TESWeather * weather)
	{
		return static_cast<std::uint8_t>(weather->data.sunGlare) / 255.0f;
	}

	// Sun glare of the sky, blended during a weather transition (weather mods often use long transitions).
	float GetSkySunGlare(RE::Sky * sky)
	{
		const float sunGlare = GetSunGlare(sky->currentWeather);
		const float share = std::clamp(sky->currentWeatherPct, 0.0f, 1.0f);
		if (!sky->lastWeather || share >= 1.0f) {
			return sunGlare;
		}
		return GetSunGlare(sky->lastWeather) * (1.0f - share) + sunGlare * share;
	}

	// The hours of full daylight: from the end of the sunrise to the begin of the sunset of the current climate,
	// so weather and season mods with their own sunrise and sunset times are followed.
	void GetDayHours(RE::Sky * sky, float & start, float & end)
	{
		start = _settings.dayStartHour;
		end = _settings.dayEndHour;
		if (!_settings.climateDayHours || !sky->currentClimate) {
			return;
		}
		const auto & timing = sky->currentClimate->timing;
		// the climate counts in steps of 10 minutes. Vanilla sunrise is 5:30 to 10:00, sunset 16:00 to 20:30:
		// this share of each gives the hours found by testing in vanilla, about 7:12 to 17:46
		const float DAY_TRANSITION_SHARE = 0.385f;
		const auto hourInTransition = [&](const auto & interval) { return (interval.begin + DAY_TRANSITION_SHARE * (interval.end - interval.begin)) / 6.0f; };
		const float sunUp = hourInTransition(timing.sunrise);
		const float sunDown = hourInTransition(timing.sunset);
		if (sunUp < sunDown) {
			start = sunUp;
			end = sunDown;
		}

		static RE::TESClimate * loggedClimate = nullptr;
		if (loggedClimate != sky->currentClimate) {
			loggedClimate = sky->currentClimate;
			Log("Climate %08X: day from %.2f to %.2f", loggedClimate->GetFormID(), start, end);
		}
	}

	bool IsInSunlight(RE::PlayerCharacter * player, bool nearFire)
	{
		auto sky = RE::Sky::GetSingleton();
		auto cell = player->GetParentCell();
		if (!sky || !sky->currentWeather || !cell) {
			return false;
		}

		auto location = player->GetCurrentLocation();
		const bool outside = !cell->IsInteriorCell() || (location && keywordLocSetOutdoor && location->HasKeyword(keywordLocSetOutdoor));
		if (!outside) {
			return false;
		}

		const float hour = sky->currentGameHour;
		const float sunGlare = GetSkySunGlare(sky);
		float dayStart = 0.0f;
		float dayEnd = 0.0f;
		GetDayHours(sky, dayStart, dayEnd);
		auto high = player->GetHighProcess();
		const float lightLevel = high ? high->lightLevel : 0.0f;

		// once in the sun, it takes a slightly lower light level to count as shadow again
		const float minLightLevel = _settings.minLightLevel + (nearFire ? _settings.fireLightMargin : 0.0f) - (_inSunlight ? _settings.lightLevelHysteresis : 0.0f);
		_lightLevel = lightLevel;

		return hour > dayStart && hour < dayEnd
			&& sunGlare > _settings.minSunGlare
			&& (_settings.minLightLevel <= 0 || lightLevel > minLightLevel);
	}

	// the reason for fire heat, or "" for none
	const char * FireReason(RE::PlayerCharacter * player)
	{
		if (Clock::now() < FromTicks(_fireEventUntil.load())) {
			return "fire event"; // hit, fire effect or own fire spell, see the events below
		}
		if (_settings.fireHits && IsBurning(player)) {
			return "burning";
		}
		if (_settings.ownFireSpells && IsCastingFire(player)) {
			return "casting fire";
		}
		if (_settings.nearFire && IsNearFireCached(player)) {
			return "fire nearby";
		}
		return "";
	}

	// Tells the player once per game start if the switch works, so nobody has to read the log for that.
	void ShowSwitchResult()
	{
		static bool shown = false;
		const int result = _switchResult;
		if (shown || result == 0) {
			return;
		}
		shown = true;
		RE::SendHUDMessage::ShowHUDMessage(result > 0 ? "Immersive Heat: connected" : "Immersive Heat: no connection, see the log");
	}

	// Reads the heat sources and sets the switches. Calls into the engine, so it must run on the game's main thread.
	void UpdateFromGameState()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		const bool playing = player && player->Is3DLoaded()
			&& !(_settings.noHeatInMenu && RE::UI::GetSingleton() && RE::UI::GetSingleton()->GameIsPaused());

		// checked every time so the hysteresis always knows the last sunlight state
		if (player && player->Is3DLoaded()) {
			ShowSwitchResult();
		}
		const bool nearFire = playing && (_settings.nearFire || _settings.sunlight) && IsNearFireCached(player);
		_inSunlight = playing && _settings.sunlight && IsInSunlight(player, nearFire);
		const std::string fireReason = playing ? FireReason(player) : "";

		SetHeat(_inSunlight, !fireReason.empty());

		const std::string reason = std::string(_inSunlight ? "sunlight" : "") + (_inSunlight && !fireReason.empty() ? ", " : "") + fireReason;
		if (reason != _lastReason) {
			Log("Heat: %s (light level %.0f)", reason.empty() ? "none" : reason.c_str(), _lightLevel);
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
		SKSE::GetTaskInterface()->AddTask([]() {
			UpdateFromGameState();
			_checkPending = false;
		});
		return true;
	}

	bool IsGameInForeground()
	{
		// the VR game keeps running without the window focus
		if (REL::Module::IsVR())
			return true;

		DWORD processId = 0;
		GetWindowThreadProcessId(GetForegroundWindow(), &processId);
		return processId == GetCurrentProcessId();
	}

	// Background timer: queues the heat check on the main thread and switches off when nobody plays.
	void HeatCheckThread()
	{
		Clock::time_point lastCheck;
		while (_keepRunning) {
			if (!IsGameInForeground()) {
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
	// The heat then lasts at least fFireHitSeconds; the checks keep it on while the fire goes on.

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

	// hit by a fire spell, shout or fire enchanted weapon
	class HitEventHandler : public RE::BSTEventSink<RE::TESHitEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent * event, RE::BSTEventSource<RE::TESHitEvent> *) override
		{
			if (event && _settings.fireHits && IsPlayer(event->target) && IsFireSource(event->source)) {
				HeatFromFireEvent("hit by fire");
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	// any fire damage effect applied to the player, also without a hit: hazards, cloaks, traps, enchantments
	class MagicEffectApplyEventHandler : public RE::BSTEventSink<RE::TESMagicEffectApplyEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESMagicEffectApplyEvent * event, RE::BSTEventSource<RE::TESMagicEffectApplyEvent> *) override
		{
			if (event && _settings.fireHits && IsPlayer(event->target) && IsFireEffect(RE::TESForm::LookupByID<RE::EffectSetting>(event->magicEffect))) {
				HeatFromFireEvent("fire effect");
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	// the player casts a fire spell (charging is covered by the regular check)
	class SpellCastEventHandler : public RE::BSTEventSink<RE::TESSpellCastEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESSpellCastEvent * event, RE::BSTEventSource<RE::TESSpellCastEvent> *) override
		{
			if (event && _settings.ownFireSpells && IsPlayer(event->object) && IsFireMagic(RE::TESForm::LookupByID<RE::MagicItem>(event->spell))) {
				HeatFromFireEvent("casting fire");
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	HitEventHandler hitEventHandler;
	MagicEffectApplyEventHandler magicEffectApplyEventHandler;
	SpellCastEventHandler spellCastEventHandler;

	// ---- start / exit ----

	void StartMod()
	{
		WriteDefaultsToIniFile();
		LoadSettings();

		keywordLocSetOutdoor = RE::TESForm::LookupByID<RE::BGSKeyword>(KEYWORD_ID_LOC_SET_OUTDOOR);
		if (auto events = RE::ScriptEventSourceHolder::GetSingleton()) {
			events->AddEventSink<RE::TESHitEvent>(&hitEventHandler);
			events->AddEventSink<RE::TESMagicEffectApplyEvent>(&magicEffectApplyEventHandler);
			events->AddEventSink<RE::TESSpellCastEvent>(&spellCastEventHandler);
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
		} exitHandler;
	}
}
