#include "MenuChecker.h"
#include <string>
#include <unordered_set>

namespace ImmersiveWinds
{
	namespace
	{
		// menus that pause the game, the dialogue menu is not included
		const std::unordered_set<std::string> gameStoppingMenusNoDialogue{
			"BarterMenu",
			"Book Menu",
			"Console",
			"Native UI Menu",
			"ContainerMenu",
			"Crafting Menu",
			"Credits Menu",
			"Cursor Menu",
			"Debug Text Menu",
			"FavoritesMenu",
			"GiftMenu",
			"InventoryMenu",
			"Journal Menu",
			"Kinect Menu",
			"Loading Menu",
			"Lockpicking Menu",
			"MagicMenu",
			"Main Menu",
			"MapMarkerText3D",
			"MapMenu",
			"MessageBoxMenu",
			"Mist Menu",
			"Quantity Menu",
			"RaceSex Menu",
			"Sleep/Wait Menu",
			"StatsMenuSkillRing",
			"StatsMenuPerks",
			"Training Menu",
			"Tutorial Menu",
			"TweenMenu"
		};

		// the game stopping menus that are open right now, only used by the event handler
		std::unordered_set<std::string> openStoppingMenus;
	}

	std::atomic<bool> menuStopsGameNoDialogue(false);

	AllMenuEventHandler menuEvent;

	RE::BSEventNotifyControl AllMenuEventHandler::ProcessEvent(const RE::MenuOpenCloseEvent * evn, RE::BSTEventSource<RE::MenuOpenCloseEvent> *)
	{
		if (!evn)
			return RE::BSEventNotifyControl::kContinue;

		const std::string menuName = evn->menuName.c_str();
		if (gameStoppingMenusNoDialogue.contains(menuName))
		{
			if (evn->opening)
				openStoppingMenus.insert(menuName);
			else
				openStoppingMenus.erase(menuName);
			menuStopsGameNoDialogue = !openStoppingMenus.empty();
		}

		return RE::BSEventNotifyControl::kContinue;
	}
}
