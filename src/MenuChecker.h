#pragma once
#include <unordered_map>


namespace ImmersiveWinds
{
	extern std::vector<std::string> gameStoppingMenus;

	extern std::vector<std::string> gameStoppingMenusNoDialogue;

	extern std::unordered_map<std::string, bool> menuTypes;

	bool isGameStopped();

	bool isGameStoppedNoDialogue();

	class AllMenuEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent * evn, RE::BSTEventSource<RE::MenuOpenCloseEvent> * dispatcher) override;
	};

	extern AllMenuEventHandler menuEvent;
}