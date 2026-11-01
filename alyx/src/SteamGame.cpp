#include "SteamGame.h"
#include <fstream>
#include <regex>
#include <sstream>

namespace
{
	const char * SCRIPT_FILE = "immersive_alyx.lua";
	const char * LOAD_LINE = "script_reload_code immersive_alyx.lua";

	std::optional<std::filesystem::path> SteamFolder()
	{
		char buffer[MAX_PATH] = {};
		DWORD size = sizeof(buffer);
		if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath", RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) {
			return std::nullopt;
		}
		return std::filesystem::path(buffer);
	}

	// all library folders of libraryfolders.vdf, the Steam folder itself first
	std::vector<std::filesystem::path> SteamLibraries(const std::filesystem::path & steam)
	{
		std::vector<std::filesystem::path> libraries{ steam };
		std::ifstream file(steam / "steamapps" / "libraryfolders.vdf");
		std::stringstream content;
		content << file.rdbuf();
		const std::string text = content.str();
		const std::regex pathEntry("\"path\"\\s+\"([^\"]+)\"");
		for (auto it = std::sregex_iterator(text.begin(), text.end(), pathEntry); it != std::sregex_iterator(); ++it) {
			// the vdf escapes the backslashes
			std::string path = (*it)[1].str();
			for (size_t pos = path.find("\\\\"); pos != std::string::npos; pos = path.find("\\\\", pos + 1)) {
				path.erase(pos, 1);
			}
			libraries.emplace_back(path);
		}
		return libraries;
	}

	bool FileContainsLine(const std::filesystem::path & file, const std::string & line)
	{
		std::ifstream input(file);
		std::string current;
		while (std::getline(input, current)) {
			if (current.find(line) != std::string::npos) {
				return true;
			}
		}
		return false;
	}
}

namespace SteamGame
{
	std::optional<std::filesystem::path> FindAlyxGameFolder()
	{
		const auto steam = SteamFolder();
		if (!steam) {
			return std::nullopt;
		}
		for (const auto & library : SteamLibraries(*steam)) {
			const auto folder = library / "steamapps" / "common" / "Half-Life Alyx" / "game" / "hlvr";
			std::error_code error;
			if (std::filesystem::is_directory(folder, error)) {
				return folder;
			}
		}
		return std::nullopt;
	}

	bool InstallScript(const std::filesystem::path & gameFolder, const std::filesystem::path & script)
	{
		std::error_code error;
		const auto vscripts = gameFolder / "scripts" / "vscripts";
		std::filesystem::create_directories(vscripts, error);
		std::filesystem::copy_file(script, vscripts / SCRIPT_FILE, std::filesystem::copy_options::overwrite_existing, error);
		if (error) {
			_MESSAGE("Could not copy %s to %s: %s", script.string().c_str(), vscripts.string().c_str(), error.message().c_str());
			return false;
		}

		// skill_manifest.cfg runs on every map load; the line is added at the end, other mods' lines stay
		const auto manifest = gameFolder / "cfg" / "skill_manifest.cfg";
		if (FileContainsLine(manifest, LOAD_LINE)) {
			return true;
		}
		std::ofstream output(manifest, std::ios::app);
		output << "\n" << LOAD_LINE << "\n";
		if (!output) {
			_MESSAGE("Could not add the load line to %s", manifest.string().c_str());
			return false;
		}
		_MESSAGE("Added '%s' to %s", LOAD_LINE, manifest.string().c_str());
		return true;
	}
}
