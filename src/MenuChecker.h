#pragma once
#include <atomic>


namespace ImmersiveWinds
{
	// true while a menu is open that pauses the game (dialogue does not count), updated on every menu event
	// so other threads can read it safely
	extern std::atomic<bool> menuStopsGameNoDialogue;

	// Queues a check of the game state on the main thread (ImmersiveWinds.cpp), unless one is already queued.
	bool RequestGameStateCheck();

	class AllMenuEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent * evn, RE::BSTEventSource<RE::MenuOpenCloseEvent> * dispatcher) override;
	};

	extern AllMenuEventHandler menuEvent;
}
