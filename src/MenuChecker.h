#pragma once
#include <unordered_map>
#include <atomic>


namespace ImmersiveWinds
{
	extern std::vector<std::string> gameStoppingMenus;

	extern std::vector<std::string> gameStoppingMenusNoDialogue;

	extern std::unordered_map<std::string, bool> menuTypes;

	bool isGameStopped();

	bool isGameStoppedNoDialogue();

	// result of isGameStoppedNoDialogue(), updated on every menu event so other threads can read it safely
	extern std::atomic<bool> menuStopsGameNoDialogue;

	class AllMenuEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent * evn, RE::BSTEventSource<RE::MenuOpenCloseEvent> * dispatcher) override;
	};

	extern AllMenuEventHandler menuEvent;
}