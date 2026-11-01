#pragma once
#include <string>

// Immersive Heat for Fallout 4 / Fallout 4 VR: switches one or two heat lamps for sunlight and fire.
namespace ImmersiveHeatF4
{
	const std::string MOD_VERSION = "1.0.0";

	// My Games\<Fallout4|Fallout4VR>\F4SE\immersiveHeat.ini
	std::string GetIniPath();

	// called once the game data is loaded: reads the settings, registers the events and starts the threads
	void StartMod();

	// switches the used plugs off, when the game exits
	void SwitchOffOnExit();
}
