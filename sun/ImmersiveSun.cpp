#include "ImmersiveSun.h"
#include "HttpSwitch.h"
#include "PlugFinder.h"
#include "TPLinkSPHelper.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>


namespace ImmersiveSun {

	namespace
	{
		using Clock = std::chrono::steady_clock;

		const char * PLUG_NAME = "ImmersiveSunPlug1";
		const char * IP_KEY = "sTpLinkPlugIp1";

		// ini sections
		const char * INI_INTERNAL = "Internal";
		const char * INI_GENERAL = "GeneralModConfig";
		const char * INI_HTTP = "HttpSwitch";
		const char * INI_HEAT = "HeatSources";

		const float HEAT_CHECK_INTERVAL_SECONDS = 1.0f;
		const int FOCUS_CHECK_INTERVAL_MS = 250;
		// the plug state is sent again after this time, in case a (udp) message got lost or the plug restarted
		const float RELAY_RESEND_INTERVAL_SECONDS = 15.0f;
		// a queued check that did not run after this time means the game stands still (frozen, long loading screen)
		const float GAME_STALLED_SECONDS = 10.0f;

		const RE::FormID KEYWORD_ID_LOC_SET_OUTDOOR = 0x0130F3;

		// ---- settings ----

		struct Settings
		{
			bool logging = false;
			bool noHeatInMenu = true;

			// sSwitchType: "tplink" (TP-Link Kasa plug, found by a network scan) or "http" (sOnUrl / sOffUrl)
			bool useHttpSwitch = false;
			std::string httpMethod = "GET";
			std::string httpOnUrl;
			std::string httpOffUrl;

			// direct sunlight: outside, at day, bright sun and the player standing in the light
			bool sunlight = true;
			float minSunGlare = 0.4f;     // 0-1
			float minLightLevel = 138.0f; // light level at the player (Actor.GetLightLevel), 0 disables the check
			float dayStartHour = 7.2f;
			float dayEndHour = 17.76f;

			// fire nearby: a reference whose model path contains one of fireModels (and none of fireModelsExclude)
			bool nearFire = true;
			float fireRadius = 350.0f;    // game units, about 5 m
			std::vector<std::string> fireModels;
			std::vector<std::string> fireModelsExclude;

			// fire attacks: hit by fire, burning (fire damage effects on the player), casting own fire spells
			bool fireHits = true;
			float fireHitSeconds = 3.0f;
			bool ownFireSpells = true;
		};

		const char * DEFAULT_FIRE_MODELS = "fire,flame,brazier,forge,smelter,lava,embers,hearth";
		const char * DEFAULT_FIRE_MODELS_EXCLUDE = "firewood,fireplace,nofire,unlit";

		Settings _settings;

		// ---- state ----

		std::atomic<bool> _keepRunning(true);
		// whether the plug should be on, written by the heat check and the focus handling, read by the switch thread
		std::atomic<bool> _heatOn(false);

		// workingIp and useUDP are written by the scan thread and read by the switch thread
		std::mutex _connectionConfigMutex;
		std::string workingIp;
		int useUDP = -1;

		std::atomic<bool> _scanFinished(false);
		std::atomic<bool> _checkPending(false);
		std::atomic<Clock::rep> _checkQueuedAt(0);
		// end of the heat after a fire hit (steady_clock ticks), written by the hit event
		std::atomic<Clock::rep> _fireHitUntil(0);

		RE::BGSKeyword * keywordLocSetOutdoor = nullptr;
		// model check results per base form, only used on the main thread
		std::unordered_map<RE::FormID, bool> _fireModelCache;
		// last reason that was logged, to log only changes
		std::string _lastReason;
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
		return dir ? (*dir / "immersiveSun.ini").string() : "immersiveSun.ini";
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

	// ---- settings ----

	void WriteDefaultsToIniFile()
	{
		const std::string path = GetIniPath();
		const Settings defaults;

		if (ReadIniString(INI_INTERNAL, "sModVersion", "", path) != MOD_VERSION) {
			WritePrivateProfileStringA(INI_INTERNAL, "sModVersion", MOD_VERSION.c_str(), path.c_str());
		}
		WriteIniIfMissing(INI_INTERNAL, IP_KEY, "", path);

		WriteIniIfMissing(INI_GENERAL, "sSwitchType", "tplink", path);
		WriteIniIfMissing(INI_GENERAL, "bUseUdp", "1", path);
		WriteIniIfMissing(INI_GENERAL, "bLoggingEnabled", "0", path);
		WriteIniIfMissing(INI_GENERAL, "bNoHeatInMenu", "1", path);

		WriteIniIfMissing(INI_HTTP, "sHttpMethod", "GET", path);
		WriteIniIfMissing(INI_HTTP, "sOnUrl", "", path);
		WriteIniIfMissing(INI_HTTP, "sOffUrl", "", path);

		WriteIniIfMissing(INI_HEAT, "bSunlight", "1", path);
		WriteIniIfMissing(INI_HEAT, "fMinSunGlare", std::to_string(defaults.minSunGlare), path);
		WriteIniIfMissing(INI_HEAT, "fMinLightLevel", std::to_string(defaults.minLightLevel), path);
		WriteIniIfMissing(INI_HEAT, "fDayStartHour", std::to_string(defaults.dayStartHour), path);
		WriteIniIfMissing(INI_HEAT, "fDayEndHour", std::to_string(defaults.dayEndHour), path);
		WriteIniIfMissing(INI_HEAT, "bNearFire", "1", path);
		WriteIniIfMissing(INI_HEAT, "fFireRadius", std::to_string(defaults.fireRadius), path);
		WriteIniIfMissing(INI_HEAT, "sFireModels", DEFAULT_FIRE_MODELS, path);
		WriteIniIfMissing(INI_HEAT, "sFireModelsExclude", DEFAULT_FIRE_MODELS_EXCLUDE, path);
		WriteIniIfMissing(INI_HEAT, "bFireHits", "1", path);
		WriteIniIfMissing(INI_HEAT, "fFireHitSeconds", std::to_string(defaults.fireHitSeconds), path);
		WriteIniIfMissing(INI_HEAT, "bOwnFireSpells", "1", path);
	}

	void LoadSettings()
	{
		const std::string path = GetIniPath();
		const Settings defaults;
		Settings & s = _settings;

		s.logging = ReadIniBool(INI_GENERAL, "bLoggingEnabled", defaults.logging, path);
		s.noHeatInMenu = ReadIniBool(INI_GENERAL, "bNoHeatInMenu", defaults.noHeatInMenu, path);
		s.useHttpSwitch = _stricmp(Trim(ReadIniString(INI_GENERAL, "sSwitchType", "tplink", path)).c_str(), "http") == 0;
		s.httpMethod = Trim(ReadIniString(INI_HTTP, "sHttpMethod", "GET", path));
		s.httpOnUrl = Trim(ReadIniString(INI_HTTP, "sOnUrl", "", path));
		s.httpOffUrl = Trim(ReadIniString(INI_HTTP, "sOffUrl", "", path));

		s.sunlight = ReadIniBool(INI_HEAT, "bSunlight", defaults.sunlight, path);
		s.minSunGlare = ReadIniFloat(INI_HEAT, "fMinSunGlare", defaults.minSunGlare, path);
		s.minLightLevel = ReadIniFloat(INI_HEAT, "fMinLightLevel", defaults.minLightLevel, path);
		s.dayStartHour = ReadIniFloat(INI_HEAT, "fDayStartHour", defaults.dayStartHour, path);
		s.dayEndHour = ReadIniFloat(INI_HEAT, "fDayEndHour", defaults.dayEndHour, path);
		s.nearFire = ReadIniBool(INI_HEAT, "bNearFire", defaults.nearFire, path);
		s.fireRadius = ReadIniFloat(INI_HEAT, "fFireRadius", defaults.fireRadius, path);
		s.fireModels = SplitLowerList(ReadIniString(INI_HEAT, "sFireModels", DEFAULT_FIRE_MODELS, path));
		s.fireModelsExclude = SplitLowerList(ReadIniString(INI_HEAT, "sFireModelsExclude", DEFAULT_FIRE_MODELS_EXCLUDE, path));
		s.fireHits = ReadIniBool(INI_HEAT, "bFireHits", defaults.fireHits, path);
		s.fireHitSeconds = ReadIniFloat(INI_HEAT, "fFireHitSeconds", defaults.fireHitSeconds, path);
		s.ownFireSpells = ReadIniBool(INI_HEAT, "bOwnFireSpells", defaults.ownFireSpells, path);

		_MESSAGE("Heat sources: sunlight %d, fire nearby %d (radius %g), fire hits %d, own fire spells %d, switch %s",
			s.sunlight, s.nearFire, s.fireRadius, s.fireHits, s.ownFireSpells, s.useHttpSwitch ? "http" : "tplink");
	}

	// ---- plug ----

	bool LoadConnectionConfig(TPLinkHelper & tpLinkConn)
	{
		std::lock_guard<std::mutex> lock(_connectionConfigMutex);
		return tpLinkConn.LoadIPConfigFromFile(GetIniPath(), IP_KEY, useUDP, workingIp);
	}

	void ScanThread()
	{
		// a http switch is addressed by its urls, there is nothing to search
		if (!_settings.useHttpSwitch) {
			const std::string ip = FindTpLinkPlug(GetIniPath(), IP_KEY, PLUG_NAME, _settings.logging, _keepRunning);
			if (!ip.empty()) {
				std::lock_guard<std::mutex> lock(_connectionConfigMutex);
				workingIp = ip;
			}
		}
		_scanFinished = true;
	}

	bool SwitchPlug(bool on)
	{
		if (_settings.useHttpSwitch) {
			return HttpSwitch::Request(_settings.httpMethod, on ? _settings.httpOnUrl : _settings.httpOffUrl, _settings.logging);
		}
		TPLinkHelper tpLinkConn(_settings.logging);
		return LoadConnectionConfig(tpLinkConn) && tpLinkConn.SwitchRelayState(on ? 1 : 0);
	}

	// Sends the wanted state to the plug when it changes, and again every RELAY_RESEND_INTERVAL_SECONDS.
	void SwitchThread()
	{
		// wait until the plug was found, otherwise the first switch commands would get lost
		while (!_scanFinished && _keepRunning) {
			Sleep(100);
		}

		bool sentState = false;
		Clock::time_point lastSend;
		bool firstSend = true;
		while (_keepRunning) {
			const bool wanted = _heatOn;
			if (firstSend || wanted != sentState || SecondsSince(lastSend) >= RELAY_RESEND_INTERVAL_SECONDS) {
				if (firstSend || wanted != sentState) {
					Log("Switching the plug %s", wanted ? "on" : "off");
				}
				if (!SwitchPlug(wanted)) {
					Log("Switching the plug %s failed", wanted ? "on" : "off");
				}
				sentState = wanted;
				lastSend = Clock::now();
				firstSend = false;
			}
			Sleep(100);
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

	bool IsFireModel(RE::TESBoundObject * base)
	{
		if (!base) {
			return false;
		}
		const auto cached = _fireModelCache.find(base->GetFormID());
		if (cached != _fireModelCache.end()) {
			return cached->second;
		}

		bool fire = false;
		if (auto model = base->As<RE::TESModel>()) {
			const char * path = model->GetModel();
			const std::string lower = ToLower(path ? path : "");
			const auto contains = [&lower](const std::string & word) { return lower.find(word) != std::string::npos; };
			fire = !lower.empty()
				&& std::any_of(_settings.fireModels.begin(), _settings.fireModels.end(), contains)
				&& std::none_of(_settings.fireModelsExclude.begin(), _settings.fireModelsExclude.end(), contains);
			if (fire) {
				Log("Fire model: %s (%08X)", path, base->GetFormID());
			}
		}
		_fireModelCache[base->GetFormID()] = fire;
		return fire;
	}

	bool IsNearFire(RE::PlayerCharacter * player)
	{
		auto tes = RE::TES::GetSingleton();
		if (!tes) {
			return false;
		}
		bool found = false;
		tes->ForEachReferenceInRange(player, _settings.fireRadius, [&](RE::TESObjectREFR * ref) {
			if (ref && ref != player && !ref->IsDisabled() && ref->Is3DLoaded() && IsFireModel(ref->GetBaseObject())) {
				found = true;
				return RE::BSContainer::ForEachResult::kStop;
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		return found;
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

	bool IsInSunlight(RE::PlayerCharacter * player)
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
		// CommonLib types sunGlare signed, the value is 0-255
		const float sunGlare = static_cast<std::uint8_t>(sky->currentWeather->data.sunGlare) / 256.0f;
		auto high = player->GetHighProcess();
		const float lightLevel = high ? high->lightLevel : 0.0f;

		return hour > _settings.dayStartHour && hour < _settings.dayEndHour
			&& sunGlare > _settings.minSunGlare
			&& (_settings.minLightLevel <= 0 || lightLevel > _settings.minLightLevel);
	}

	// Reads the heat sources and sets _heatOn. Calls into the engine, so it must run on the game's main thread.
	void UpdateFromGameState()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		std::string reason;
		if (!player || !player->Is3DLoaded()) {
			reason = "";
		}
		else if (_settings.noHeatInMenu && RE::UI::GetSingleton() && RE::UI::GetSingleton()->GameIsPaused()) {
			reason = "";
		}
		else if (_settings.fireHits && Clock::now() < FromTicks(_fireHitUntil.load())) {
			reason = "hit by fire";
		}
		else if (_settings.fireHits && IsBurning(player)) {
			reason = "burning";
		}
		else if (_settings.ownFireSpells && IsCastingFire(player)) {
			reason = "casting fire";
		}
		else if (_settings.nearFire && IsNearFire(player)) {
			reason = "fire nearby";
		}
		else if (_settings.sunlight && IsInSunlight(player)) {
			reason = "sunlight";
		}

		_heatOn = !reason.empty();
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
				_heatOn = false;
				lastCheck = {}; // check right when the game is back
			}
			else if (SecondsSince(lastCheck) >= HEAT_CHECK_INTERVAL_SECONDS) {
				lastCheck = Clock::now();
				if (!RequestGameStateCheck() && SecondsSince(FromTicks(_checkQueuedAt.load())) >= GAME_STALLED_SECONDS) {
					_heatOn = false;
				}
			}
			Sleep(FOCUS_CHECK_INTERVAL_MS);
		}
	}

	// ---- events ----

	class HitEventHandler : public RE::BSTEventSink<RE::TESHitEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent * event, RE::BSTEventSource<RE::TESHitEvent> *) override
		{
			if (event && event->target && event->target.get() == RE::PlayerCharacter::GetSingleton() && IsFireSource(event->source)) {
				const auto until = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(_settings.fireHitSeconds));
				_fireHitUntil = until.time_since_epoch().count();
				// react right away instead of with the next timer check
				RequestGameStateCheck();
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	HitEventHandler hitEventHandler;

	// ---- start / exit ----

	void StartMod()
	{
		WriteDefaultsToIniFile();
		LoadSettings();

		keywordLocSetOutdoor = RE::TESForm::LookupByID<RE::BGSKeyword>(KEYWORD_ID_LOC_SET_OUTDOOR);
		if (auto events = RE::ScriptEventSourceHolder::GetSingleton()) {
			events->AddEventSink<RE::TESHitEvent>(&hitEventHandler);
		}

		std::thread(ScanThread).detach();
		std::thread(SwitchThread).detach();
		std::thread(HeatCheckThread).detach();
	}

	void SwitchOffOnExit()
	{
		_heatOn = false;
		_keepRunning = false;
		// before the settings are loaded the plug ip comes from the ini file
		SwitchPlug(false);
	}

	namespace
	{
		// Switches the plug off when the game exits. Defined last in this file on purpose: objects of one
		// file are destroyed in reverse order, so the settings and connection data above still exist then.
		struct ExitHandler
		{
			~ExitHandler() { SwitchOffOnExit(); }
		} exitHandler;
	}
}
