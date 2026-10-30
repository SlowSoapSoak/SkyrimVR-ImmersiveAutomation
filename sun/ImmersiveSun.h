#pragma once
#include <string>

// Immersive Sun: switches a heat lamp (or anything else on a smart plug) while the player is near a heat
// source in the game: direct sunlight, fire nearby, being hit by fire, own fire spells.
namespace ImmersiveSun
{
	const std::string MOD_VERSION = "1.1.0";

	// My Games\<Skyrim Special Edition|Skyrim VR|...>\SKSE\immersiveSun.ini, matching the running game
	std::string GetIniPath();

	// called once the game data is loaded: reads the settings, registers the events and starts the threads
	void StartMod();

	// stops the plug control and switches the plug off, when the game exits
	void SwitchOffOnExit();
}
