#pragma once
#include <filesystem>
#include <optional>

namespace SteamGame
{
	// Folder game\hlvr of Half-Life: Alyx, searched in all Steam libraries (registry and libraryfolders.vdf).
	std::optional<std::filesystem::path> FindAlyxGameFolder();

	// Copies immersive_alyx.lua into scripts\vscripts and adds its load line to cfg\skill_manifest.cfg, if missing.
	// script is the lua file next to the program. Returns false if something could not be written.
	bool InstallScript(const std::filesystem::path & gameFolder, const std::filesystem::path & script);
}
