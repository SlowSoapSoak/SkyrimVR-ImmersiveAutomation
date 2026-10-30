#pragma once
#include <string>

// Immersive Heat: switches a heat lamp (or anything else on a smart plug) while the player is near a heat source
// in the game. Sunlight and fire each go to switch 1 or 2, so one device can serve both or each gets its own.
namespace ImmersiveHeat
{
	const std::string MOD_VERSION = "1.1.0";

	// My Games\<Skyrim Special Edition|Skyrim VR|...>\SKSE\immersiveHeat.ini, matching the running game
	std::string GetIniPath();

	// called once the game data is loaded: reads the settings, registers the events and starts the threads
	void StartMod();

	// stops the plug control and switches the plugs off, when the game exits
	void SwitchOffOnExit();
}
