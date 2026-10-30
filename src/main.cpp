#include "ImmersiveWinds.h"
#include "EndGameDetect.h"

#include <spdlog/sinks/basic_file_sink.h>

#pragma comment(lib, "Ws2_32.lib")

static EndGameDetect endGameDetect;

namespace
{
	void SetupLog()
	{
		auto path = SKSE::log::log_directory();
		if (!path) {
			return;
		}
		*path /= "ImmersiveWindsPluginScript.log";
		auto log = std::make_shared<spdlog::logger>("global", std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true));
		log->set_level(spdlog::level::trace);
		log->flush_on(spdlog::level::trace);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] %v");
	}

	void OnSKSEMessage(SKSE::MessagingInterface::Message* msg)
	{
		switch (msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			ImmersiveWinds::StartMod();
			break;
		case SKSE::MessagingInterface::kNewGame:
		case SKSE::MessagingInterface::kPreLoadGame:
		case SKSE::MessagingInterface::kPostLoadGame:
			ImmersiveWinds::ResetTeach();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
	SKSE::Init(skse);
	SetupLog();

	_MESSAGE("Immersive Winds: %s, game version %s", ImmersiveWinds::MOD_VERSION.c_str(), skse->RuntimeVersion().string(".").c_str());

	SKSE::GetMessagingInterface()->RegisterListener(OnSKSEMessage);
	return true;
}
