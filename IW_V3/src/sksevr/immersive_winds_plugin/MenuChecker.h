#pragma once
#include "skse64/GameReferences.h"

#include "skse64/PapyrusVM.h"
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

	class AllMenuEventHandler : public BSTEventSink <MenuOpenCloseEvent> {
	public:
		virtual EventResult	ReceiveEvent(MenuOpenCloseEvent * evn, EventDispatcher<MenuOpenCloseEvent> * dispatcher);
	};

	extern AllMenuEventHandler menuEvent;
}