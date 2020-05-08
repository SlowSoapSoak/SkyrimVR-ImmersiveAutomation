#include "skse64_common/skse_version.h"
//#include "skse64_common/Utilities.h"
//#include "skse64_common/SafeWrite.h"
#include "skse64_loader_common/IdentifyEXE.h"
#include <shlobj.h>
#include <intrin.h>
#include <string>
#include "skse64/PluginAPI.h"		// super
//#include "skse64/PapyrusObjects.h"		

#include "Enumerations.h"
#include "EndGameDetect.h"

#include "SPScanner.h"

static PluginHandle					g_pluginHandle = kPluginHandle_Invalid;
static SKSEPapyrusInterface         * g_papyrus = NULL;
static SKSEObjectInterface         * g_object = NULL;
static EndGameDetect endGameDetect;


#pragma comment(lib, "Ws2_32.lib")


extern "C" {

	bool SKSEPlugin_Query(const SKSEInterface * skse, PluginInfo * info) {	// Called by SKSE to learn about this plugin and check that it's safe to load it
		gLog.OpenRelative(CSIDL_MYDOCUMENTS, "\\My Games\\Skyrim VR\\SKSE\\ImmersiveWindsPluginScript.log");
		gLog.SetPrintLevel(IDebugLog::kLevel_Error);
		gLog.SetLogLevel(IDebugLog::kLevel_DebugMessage);

		std::string logMsg("Immersive Winds V: ");
		logMsg.append(ImmersiveWinds::MOD_VERSION);
		_MESSAGE(logMsg.c_str());


		// populate info structure
		info->infoVersion = PluginInfo::kInfoVersion;
		info->name = "ImmersiveWindsPluginScript";
		info->version = 010101; // 1.1.1

		// store plugin handle so we can identify ourselves later
		g_pluginHandle = skse->GetPluginHandle();


		std::string skseVers = "SKSE Version: ";
		skseVers += std::to_string(skse->runtimeVersion);
		_MESSAGE(skseVers.c_str());

		if (skse->isEditor)
		{
			_MESSAGE("loaded in editor, marking as incompatible");
			return false;
		}
		else if (skse->runtimeVersion < RUNTIME_VR_VERSION_1_4_15)
		{
			_MESSAGE("unsupported runtime version %08X", skse->runtimeVersion);
			return false;
		}

		// ### do not do anything else in this callback
		// ### only fill out PluginInfo and return true/false

		// supported runtime version
		return true;
	}

	bool SKSEPlugin_Load(const SKSEInterface * skse) {	// Called by SKSE to load this plugin
		_MESSAGE("ImmersiveWindsPluginScript loaded");

		g_papyrus = (SKSEPapyrusInterface *)skse->QueryInterface(kInterface_Papyrus);
		//g_object = (SKSEObjectInterface *)skse->QueryInterface(kInterface_Object);
		//SKSEPersistentObjectStorage objects = g_object->GetPersistentObjectStorage();
		//objects.AccessObject

		//Check if the function registration was a success...
		bool btest = g_papyrus->Register(ImmersiveWinds::RegisterFuncs);

		if (btest) {
			_MESSAGE("Register Succeeded");
		}

		return true;
	}

};