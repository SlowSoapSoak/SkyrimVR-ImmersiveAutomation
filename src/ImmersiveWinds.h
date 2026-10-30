#pragma once
#include <atomic>
#include <string>
#include "SPScanner.h"
#include "TPLinkSPHelper.h"
#include "MenuChecker.h"

namespace ImmersiveWinds
{
	const std::string MOD_VERSION = "1.3.0";

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

	// shared with EndGameDetect, all other state lives in ImmersiveWinds.cpp
	extern std::atomic<bool> _keepRunning;
	extern std::atomic<long> _currentSwitchState;

	// My Games\<Skyrim Special Edition|Skyrim VR|...>\SKSE\immersiveWinds.ini, matching the running game
	std::string GetIniPath();

	// called once the game data is loaded: reads the settings and starts the threads
	void StartMod();

	// the shout words are taught again on the next check (new game / loaded game)
	void ResetTeach();
}
