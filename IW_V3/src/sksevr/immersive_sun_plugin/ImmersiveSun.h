#pragma once
#include "skse64/PapyrusNativeFunctions.h"
#include "skse64/PapyrusObjects.h"
#include "TPLinkSPHelper.h"
#include <thread>
#include <map>
#include <ctime>
#include <chrono>

#include "skse64/GameRTTI.h"
#include "skse64/GameAPI.h"
#include "skse64/GameReferences.h"
#include "skse64/GameData.h"
#include "skse64/GameSettings.h"
#include "skse64/GameForms.h"
#include "skse64/GameCamera.h"
#include "skse64/GameMenus.h"
#include "skse64/GameThreads.h"
#include "skse64/GameExtraData.h"

#include <bitset>
#include <random>

namespace ImmersiveSun
{
	const std::string MOD_VERSION = "1.0.0";

	static TESObjectREFR* _playerRef = NULL;

	static bool keepRunning = true;
	static long previousSwitchState1 = 0;
	static long currentSwitchState1 = 0;

	static long previousSwitchState2 = 0;
	static long currentSwitchState2 = 0;

	std::string GetIniPath();

	void WriteDefaultsToIniFile();

	void RegisterPlayerInstance(StaticFunctionTag *base, TESObjectREFR *playerRef);

	void UpdateNativeSunState(StaticFunctionTag *base, long bool_internalCell, long weatherClass, long keyWordMatches, float inGameTime, float sunglare, long locationId, long weatherId, float playerLightLevel);

	bool RegisterFuncs(VMClassRegistry* registry);

	void InitBgThread();
}
