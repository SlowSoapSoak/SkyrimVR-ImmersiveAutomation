#include "HeatF4.h"

#include <spdlog/sinks/basic_file_sink.h>

namespace
{
	constexpr const char * PLUGIN_NAME = "ImmersiveHeat";
	constexpr const char * PLUGIN_AUTHOR = "madrum";
	constexpr REL::Version PLUGIN_VERSION{ 1, 0, 0, 0 };

	void SetupLog()
	{
		auto path = F4SE::log::log_directory();
		if (!path) {
			return;
		}
		*path /= "ImmersiveHeat.log";
		auto log = std::make_shared<spdlog::logger>("global", std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true));
		log->set_level(spdlog::level::trace);
		log->flush_on(spdlog::level::trace);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] %v");
	}

	void OnF4SEMessage(F4SE::MessagingInterface::Message * msg)
	{
		if (msg && msg->type == F4SE::MessagingInterface::kGameDataReady) {
			ImmersiveHeatF4::StartMod();
		}
	}
}

// F4SE of the next gen update reads the plugin data from here
F4SE_EXPORT constinit auto F4SEPlugin_Version = []() noexcept {
	F4SE::PluginVersionData data{};
	data.PluginVersion(PLUGIN_VERSION);
	data.PluginName(PLUGIN_NAME);
	data.AuthorName(PLUGIN_AUTHOR);
	data.UsesAddressLibrary(true);
	data.UsesSigScanning(false);
	data.IsLayoutDependent(true);
	data.HasNoStructUse(false);
	data.CompatibleVersions({ F4SE::RUNTIME_LATEST });
	return data;
}();

// F4SE of the original game and F4SEVR ask here
F4SE_EXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface * a_f4se, F4SE::PluginInfo * a_info)
{
	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = PLUGIN_NAME;
	a_info->version = PLUGIN_VERSION.pack();
	return !a_f4se->IsEditor();
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface * a_f4se)
{
	F4SE::Init(a_f4se, false);
	SetupLog();
	spdlog::info("Immersive Heat {}, game version {}", ImmersiveHeatF4::MOD_VERSION, a_f4se->RuntimeVersion().string("."));

	F4SE::GetMessagingInterface()->RegisterListener(OnF4SEMessage);
	return true;
}
