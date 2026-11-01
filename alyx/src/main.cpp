// Immersive Alyx: switches the fan and the heat lamp for Half-Life: Alyx. The game has no plugin interface like
// SKSE, its Lua scripts can not reach the network. immersive_alyx.lua writes the state into the console, the game
// writes the console into console.log (launch option -condebug) and this program reads it and switches the plugs.

#include "HttpSwitch.h"
#include "PlugFinder.h"
#include "SteamGame.h"
#include "TPLinkSPHelper.h"
#include "WindSimulation.h"
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <unordered_map>

namespace
{
	using Clock = std::chrono::steady_clock;

	const char * VERSION = "1.0.0";
	const char * LINE_TAG = "[ImmersiveAlyx]";

	const int LOG_POLL_MS = 50;
	const int SIMULATION_STEP_MS = 50;
	// the script sends its state every 0.25 s; without it the game is paused, in a menu, loading or closed
	const float GAME_PAUSED_SECONDS = 2.0f;
	const float RELAY_RESEND_INTERVAL_SECONDS = 15.0f;

	const char * INI_GENERAL = "GeneralModConfig";
	const char * INI_INTERNAL = "Internal";
	const char * INI_SIM = "SimulatedWind";
	const char * INI_ALYX = "Alyx";

	std::atomic<bool> _keepRunning(true);
	bool _logging = false;
	std::mutex _logMutex;
	std::ofstream _logFile;

	// ---- helpers ----

	float SecondsSince(Clock::time_point start)
	{
		return std::chrono::duration<float>(Clock::now() - start).count();
	}

	std::string Trim(const std::string & text)
	{
		const size_t begin = text.find_first_not_of(" \t\r\n");
		if (begin == std::string::npos) {
			return "";
		}
		return text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
	}

	std::filesystem::path ProgramFolder()
	{
		wchar_t buffer[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, buffer, MAX_PATH);
		return std::filesystem::path(buffer).parent_path();
	}

	std::string IniPath()
	{
		return (ProgramFolder() / "immersiveAlyx.ini").string();
	}

	std::string ReadIniString(const char * section, const char * key, const char * defaultValue)
	{
		char buffer[4096];
		GetPrivateProfileStringA(section, key, defaultValue, buffer, sizeof(buffer), IniPath().c_str());
		return Trim(buffer);
	}

	void WriteIniIfMissing(const char * section, const char * key, const std::string & value)
	{
		const char * missing = "<missing>";
		if (ReadIniString(section, key, missing) == missing) {
			WritePrivateProfileStringA(section, key, value.c_str(), IniPath().c_str());
		}
	}

	bool ReadIniBool(const char * section, const char * key, bool defaultValue)
	{
		return GetPrivateProfileIntA(section, key, defaultValue ? 1 : 0, IniPath().c_str()) > 0;
	}

	float ReadIniFloat(const char * section, const char * key, float defaultValue)
	{
		return strtof(ReadIniString(section, key, std::to_string(defaultValue).c_str()).c_str(), nullptr);
	}

	void Log(const char * fmt, ...)
	{
		if (!_logging) {
			return;
		}
		char buffer[4096];
		va_list args;
		va_start(args, fmt);
		vsnprintf(buffer, sizeof(buffer), fmt, args);
		va_end(args);
		_MESSAGE("%s", buffer);
	}

	// ---- devices ----

	// a fan or a heat lamp: a TP-Link plug found by its name, or a http switch
	struct Device
	{
		const char * section;
		const char * ipKey;
		const char * defaultPlugName;
		const char * webhook; // Home Assistant, the same as in the game plugins

		bool enabled = true;
		bool http = false;
		std::string plugName;
		std::string httpMethod = "GET";
		std::string onUrl;
		std::string offUrl;

		std::mutex connectionMutex;
		std::string workingIp;
		int useUdp = -1;
		std::atomic<bool> scanFinished = false;
		bool sent = false;
		bool firstSend = true;
		Clock::time_point lastSend;
	};

	Device _fan{ "Fan", "sFanIp", "ImmersiveWindsPlug", "ImmersiveWinds" };
	Device _heat{ "Heat", "sHeatIp", "ImmersiveHeatPlug1", "ImmersiveHeat1" };

	void ScanForPlug(Device & device)
	{
		if (device.enabled && !device.http) {
			const std::string ip = FindTpLinkPlug(IniPath(), device.ipKey, device.plugName, _logging, _keepRunning);
			if (!ip.empty()) {
				std::lock_guard<std::mutex> lock(device.connectionMutex);
				device.workingIp = ip;
			}
		}
		device.scanFinished = true;
	}

	bool SwitchPlug(Device & device, bool on)
	{
		if (device.http) {
			return HttpSwitch::Request(device.httpMethod, on ? device.onUrl : device.offUrl, _logging);
		}
		TPLinkHelper tpLinkConn(_logging);
		std::lock_guard<std::mutex> lock(device.connectionMutex);
		return tpLinkConn.LoadIPConfigFromFile(IniPath(), device.ipKey, device.useUdp, device.workingIp) && tpLinkConn.SwitchRelayState(on ? 1 : 0);
	}

	// sends the state when it changes and again every RELAY_RESEND_INTERVAL_SECONDS
	void UpdateDevice(Device & device, bool on)
	{
		if (!device.enabled || !device.scanFinished) {
			return;
		}
		if (device.firstSend || on != device.sent || SecondsSince(device.lastSend) >= RELAY_RESEND_INTERVAL_SECONDS) {
			if (device.firstSend || on != device.sent) {
				Log("%s %s", device.section, on ? "on" : "off");
			}
			if (!SwitchPlug(device, on)) {
				_MESSAGE("%s: switching %s failed", device.section, on ? "on" : "off");
			}
			device.sent = on;
			device.firstSend = false;
			device.lastSend = Clock::now();
		}
	}

	// ---- settings ----

	ImmersiveWinds::WindSimSettings _simSettings;
	float _outsideWind = 0.25f;
	float _insideWind = 0.0f;
	float _motionStartSpeed = 100.0f;
	float _motionFullSpeed = 400.0f;
	float _motionWindMax = 0.6f;
	float _blastWind = 0.8f;
	float _blastSeconds = 2.5f;
	float _blastHeatSeconds = 2.0f;
	std::filesystem::path _consoleLog;

	struct FloatSetting
	{
		const char * section;
		const char * key;
		float defaultValue;
		float * value;
	};

	const FloatSetting FLOAT_SETTINGS[] = {
		{ INI_SIM, "fOutsideWind", 0.25f, &_outsideWind },
		{ INI_SIM, "fInsideWind", 0.0f, &_insideWind },
		{ INI_SIM, "fTurbulence", 0.25f, &_simSettings.turbulence },
		{ INI_SIM, "fTurbulenceSeconds", 4.0f, &_simSettings.turbulenceSeconds },
		{ INI_SIM, "fGustsPerMinute", 3.0f, &_simSettings.gustsPerMinute },
		{ INI_SIM, "fGustMinSeconds", 3.0f, &_simSettings.gustMinSeconds },
		{ INI_SIM, "fGustMaxSeconds", 8.0f, &_simSettings.gustMaxSeconds },
		{ INI_SIM, "fGustStrength", 0.6f, &_simSettings.gustStrength },
		{ INI_SIM, "fLullShare", 0.25f, &_simSettings.lullShare },
		{ INI_SIM, "fMotionStartSpeed", 100.0f, &_motionStartSpeed },
		{ INI_SIM, "fMotionFullSpeed", 400.0f, &_motionFullSpeed },
		{ INI_SIM, "fMotionWindMax", 0.6f, &_motionWindMax },
		{ INI_SIM, "fFanSpinUpSeconds", 1.5f, &_simSettings.fanSpinUpSeconds },
		{ INI_SIM, "fFanSpinDownSeconds", 4.0f, &_simSettings.fanSpinDownSeconds },
		{ INI_SIM, "fHysteresis", 0.05f, &_simSettings.hysteresis },
		{ INI_SIM, "fMinOnSeconds", 1.0f, &_simSettings.minOnSeconds },
		{ INI_SIM, "fMinOffSeconds", 1.0f, &_simSettings.minOffSeconds },
		{ INI_ALYX, "fBlastWind", 0.8f, &_blastWind },
		{ INI_ALYX, "fBlastSeconds", 2.5f, &_blastSeconds },
		{ INI_ALYX, "fBlastHeatSeconds", 2.0f, &_blastHeatSeconds },
	};

	void WriteDefaults(const Device & device)
	{
		WriteIniIfMissing(INI_INTERNAL, device.ipKey, "");
		WriteIniIfMissing(device.section, "bEnabled", "1");
		WriteIniIfMissing(device.section, "sSwitchType", "tplink");
		WriteIniIfMissing(device.section, "sPlugName", device.defaultPlugName);
		WriteIniIfMissing(device.section, "sHttpMethod", "GET");
		WriteIniIfMissing(device.section, "sOnUrl", "");
		WriteIniIfMissing(device.section, "sOffUrl", "");
	}

	void LoadDevice(Device & device, const std::string & homeAssistant)
	{
		device.enabled = ReadIniBool(device.section, "bEnabled", true);
		device.http = _stricmp(ReadIniString(device.section, "sSwitchType", "tplink").c_str(), "http") == 0;
		device.plugName = ReadIniString(device.section, "sPlugName", device.defaultPlugName);
		if (device.plugName.empty()) {
			device.plugName = device.defaultPlugName;
		}
		device.httpMethod = ReadIniString(device.section, "sHttpMethod", "GET");
		device.onUrl = ReadIniString(device.section, "sOnUrl", "");
		device.offUrl = ReadIniString(device.section, "sOffUrl", "");
		if (!homeAssistant.empty()) {
			device.http = true;
			device.httpMethod = "POST";
			device.onUrl = HttpSwitch::HomeAssistantUrl(homeAssistant, device.webhook, true);
			device.offUrl = HttpSwitch::HomeAssistantUrl(homeAssistant, device.webhook, false);
		}
		if (device.enabled) {
			_MESSAGE("%s: %s %s", device.section, device.http ? "http" : "tplink", device.http ? device.onUrl.c_str() : device.plugName.c_str());
		}
	}

	void LoadSettings()
	{
		WritePrivateProfileStringA(INI_INTERNAL, "sModVersion", VERSION, IniPath().c_str());
		WriteIniIfMissing(INI_GENERAL, "sConsoleLog", "");
		WriteIniIfMissing(INI_GENERAL, "bInstallScript", "1");
		WriteIniIfMissing(INI_GENERAL, "sHomeAssistantUrl", "");
		WriteIniIfMissing(INI_GENERAL, "bUseUdp", "1"); // read by TPLinkHelper
		WriteIniIfMissing(INI_GENERAL, "bLoggingEnabled", "0");
		WriteDefaults(_fan);
		WriteDefaults(_heat);
		for (const FloatSetting & setting : FLOAT_SETTINGS) {
			WriteIniIfMissing(setting.section, setting.key, std::to_string(setting.defaultValue));
		}

		_logging = ReadIniBool(INI_GENERAL, "bLoggingEnabled", false);
		const std::string homeAssistant = ReadIniString(INI_GENERAL, "sHomeAssistantUrl", "");
		LoadDevice(_fan, homeAssistant);
		LoadDevice(_heat, homeAssistant);
		for (const FloatSetting & setting : FLOAT_SETTINGS) {
			*setting.value = std::max(0.0f, ReadIniFloat(setting.section, setting.key, setting.defaultValue));
		}
		_motionFullSpeed = std::max(_motionFullSpeed, _motionStartSpeed + 1.0f);

		// console.log: from the ini, otherwise in the game folder found in the Steam libraries
		const std::string configured = ReadIniString(INI_GENERAL, "sConsoleLog", "");
		const auto gameFolder = SteamGame::FindAlyxGameFolder();
		if (!configured.empty()) {
			_consoleLog = configured;
		}
		else if (gameFolder) {
			_consoleLog = *gameFolder / "console.log";
		}

		if (gameFolder && ReadIniBool(INI_GENERAL, "bInstallScript", true)) {
			const auto script = ProgramFolder() / "immersive_alyx.lua";
			if (std::filesystem::exists(script)) {
				SteamGame::InstallScript(*gameFolder, script);
			}
			else {
				_MESSAGE("%s is missing, the game script is not installed", script.string().c_str());
			}
		}
	}

	// ---- state from the game ----

	struct GameState
	{
		Clock::time_point lastState;  // last state line, the heartbeat
		float speed = 0.0f;
		bool outside = false;
		bool fire = false;
		Clock::time_point lastBlast;
		bool anyBlast = false;
	};

	std::mutex _stateMutex;
	GameState _state;

	// key=value pairs of a line
	std::unordered_map<std::string, std::string> ParseValues(const std::string & text)
	{
		std::unordered_map<std::string, std::string> values;
		std::stringstream stream(text);
		std::string token;
		while (stream >> token) {
			const size_t equals = token.find('=');
			if (equals != std::string::npos) {
				values[token.substr(0, equals)] = token.substr(equals + 1);
			}
		}
		return values;
	}

	void HandleLine(const std::string & line)
	{
		const size_t tag = line.find(LINE_TAG);
		if (tag == std::string::npos) {
			return;
		}
		const std::string message = Trim(line.substr(tag + strlen(LINE_TAG)));
		const auto values = ParseValues(message);
		const auto value = [&values](const char * key) {
			const auto it = values.find(key);
			return it != values.end() ? strtof(it->second.c_str(), nullptr) : 0.0f;
		};

		std::lock_guard<std::mutex> lock(_stateMutex);
		if (message.rfind("state", 0) == 0) {
			const bool wasPaused = SecondsSince(_state.lastState) >= GAME_PAUSED_SECONDS;
			_state.lastState = Clock::now();
			_state.speed = value("speed");
			const bool outside = value("outside") > 0;
			const bool fire = value("fire") > 0;
			if (wasPaused || outside != _state.outside || fire != _state.fire) {
				Log("Game: %s, %s", outside ? "outside" : "inside", fire ? "fire nearby" : "no fire");
			}
			_state.outside = outside;
			_state.fire = fire;
		}
		else if (message.rfind("blast", 0) == 0) {
			_state.lastBlast = Clock::now();
			_state.anyBlast = true;
			Log("Explosion at %.0f units", value("dist"));
		}
		else {
			Log("Game: %s", message.c_str());
		}
	}

	// Reads the lines the game appends to console.log. The file is opened again for every read: the game creates a
	// new one on each start.
	void LogReaderThread()
	{
		std::uintmax_t offset = 0;
		bool started = false;
		bool waitingReported = false;
		std::string partial;

		while (_keepRunning) {
			std::error_code error;
			const std::uintmax_t size = std::filesystem::file_size(_consoleLog, error);
			if (error) {
				if (!waitingReported) {
					_MESSAGE("Waiting for %s (start the game with the launch option -condebug)", _consoleLog.string().c_str());
					waitingReported = true;
				}
				offset = 0;
				started = true; // a file that appears later is read from its beginning
				Sleep(1000);
				continue;
			}
			waitingReported = false;
			if (!started) {
				offset = size; // old lines from before the program started do not count
				started = true;
			}
			if (size < offset) {
				offset = 0; // a new file
				partial.clear();
			}
			if (size > offset) {
				HANDLE file = CreateFileW(_consoleLog.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (file != INVALID_HANDLE_VALUE) {
					LARGE_INTEGER position;
					position.QuadPart = static_cast<LONGLONG>(offset);
					SetFilePointerEx(file, position, nullptr, FILE_BEGIN);
					std::vector<char> buffer(static_cast<size_t>(std::min<std::uintmax_t>(size - offset, 1 << 20)));
					DWORD read = 0;
					if (ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
						offset += read;
						partial.append(buffer.data(), read);
						size_t newline;
						while ((newline = partial.find('\n')) != std::string::npos) {
							HandleLine(partial.substr(0, newline));
							partial.erase(0, newline + 1);
						}
					}
					CloseHandle(file);
				}
			}
			Sleep(LOG_POLL_MS);
		}
	}

	// ---- plug control ----

	// The fan follows the simulated wind, the heat lamp fire and explosions. Both are off while the game is paused.
	void ControlThread()
	{
		ImmersiveWinds::WindSimulation wind(_simSettings, std::random_device()());
		auto lastStep = Clock::now();
		while (_keepRunning) {
			GameState state;
			{
				std::lock_guard<std::mutex> lock(_stateMutex);
				state = _state;
			}
			const bool playing = SecondsSince(state.lastState) < GAME_PAUSED_SECONDS;
			const float sinceBlast = state.anyBlast ? SecondsSince(state.lastBlast) : 1e9f;

			float base = 0.0f;
			float motion = 0.0f;
			if (playing) {
				base = state.outside ? _outsideWind : _insideWind;
				if (sinceBlast < _blastSeconds) {
					// the gust of the explosion fades out
					base = std::max(base, _blastWind * (1.0f - sinceBlast / _blastSeconds));
				}
				motion = std::clamp((state.speed - _motionStartSpeed) / (_motionFullSpeed - _motionStartSpeed), 0.0f, 1.0f) * _motionWindMax;
			}

			const auto now = Clock::now();
			const float dt = std::chrono::duration<float>(now - lastStep).count();
			lastStep = now;
			UpdateDevice(_fan, wind.Update(dt, base, motion));
			UpdateDevice(_heat, playing && (state.fire || sinceBlast < _blastHeatSeconds));

			Sleep(SIMULATION_STEP_MS);
		}
	}

	void SwitchAllOff()
	{
		for (Device * device : { &_fan, &_heat }) {
			if (device->enabled && device->scanFinished) {
				SwitchPlug(*device, false);
			}
		}
	}

	// closing the window, Ctrl+C, logoff and shutdown: plugs off first
	BOOL WINAPI OnConsoleClose(DWORD)
	{
		_keepRunning = false;
		SwitchAllOff();
		return FALSE;
	}
}

void _MESSAGE(const char * fmt, ...)
{
	char buffer[4096];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	std::lock_guard<std::mutex> lock(_logMutex);
	SYSTEMTIME time;
	GetLocalTime(&time);
	char line[4200];
	snprintf(line, sizeof(line), "[%02d:%02d:%02d] %s", time.wHour, time.wMinute, time.wSecond, buffer);
	printf("%s\n", line);
	if (_logFile.is_open()) {
		_logFile << line << std::endl;
	}
}

int main()
{
	_logFile.open(ProgramFolder() / "ImmersiveAlyx.log", std::ios::trunc);
	_MESSAGE("Immersive Alyx %s - close this window to stop, the plugs are switched off then", VERSION);
	SetConsoleCtrlHandler(OnConsoleClose, TRUE);

	LoadSettings();
	if (_consoleLog.empty()) {
		_MESSAGE("Half-Life: Alyx not found in the Steam libraries, set sConsoleLog in %s", IniPath().c_str());
		_MESSAGE("Press Enter to close.");
		getchar();
		return 1;
	}
	_MESSAGE("Reading %s", _consoleLog.string().c_str());

	std::thread(ScanForPlug, std::ref(_fan)).detach();
	std::thread(ScanForPlug, std::ref(_heat)).detach();
	std::thread(LogReaderThread).detach();
	std::thread control(ControlThread);
	control.join();
	return 0;
}
