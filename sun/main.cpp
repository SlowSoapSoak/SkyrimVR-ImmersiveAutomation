#include "ImmersiveSun.h"

#include <spdlog/sinks/basic_file_sink.h>

namespace
{
	void SetupLog()
	{
		auto path = SKSE::log::log_directory();
		if (!path) {
			return;
		}
		*path /= "ImmersiveSunPluginScript.log";
		auto log = std::make_shared<spdlog::logger>("global", std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true));
		log->set_level(spdlog::level::trace);
		log->flush_on(spdlog::level::trace);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] %v");
	}

	void OnSKSEMessage(SKSE::MessagingInterface::Message* msg)
	{
		if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
			ImmersiveSun::StartMod();
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
	SKSE::Init(skse);
	SetupLog();

	_MESSAGE("Immersive Sun: %s, game version %s", ImmersiveSun::MOD_VERSION.c_str(), skse->RuntimeVersion().string(".").c_str());

	SKSE::GetMessagingInterface()->RegisterListener(OnSKSEMessage);
	return true;
}
