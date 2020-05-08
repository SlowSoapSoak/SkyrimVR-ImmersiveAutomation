#include "ImmersiveSun.h"
#include <shlobj.h>
#include <intrin.h>
#include <vector>
#include <iostream>
#include <fstream> 
#include <stdio.h>
#include <shlobj.h>
#include <intrin.h>
#include <string>

#include "SPScanner.h"

namespace ImmersiveSun {


	std::string GetIniPath() {
		CHAR my_documents[MAX_PATH];
		HRESULT result = SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, my_documents);

		std::string sExecPath(my_documents);
		sExecPath.append("\\My Games\\Skyrim VR\\SKSE\\");
		sExecPath.append("immersiveSun.ini");
		return sExecPath;
	}

	void ScanAndCheckConnection(std::string plugName, std::string settingName) {
		/*std::string plugName1 = "ImmersiveSunPlug1";
		std::string plugName2 = "ImmersiveSunPlug2";*/
		TPLinkHelper tpLinkConn;

		std::string sExecPath = ImmersiveSun::GetIniPath();

		char iniFileBuffer[100];
		GetPrivateProfileStringA("Internal", settingName.c_str(), "",
			iniFileBuffer, 100, sExecPath.c_str());
		std::string defaultIp(iniFileBuffer);
		_MESSAGE("READ ini file:");
		_MESSAGE(defaultIp.c_str());
		if (defaultIp == "") {
			_MESSAGE("no ip specified");
			// search for device
			// until 1 is found
			// if one is found
			// save new ip to ini file.
			SPScanner scanner;
			std::string ipOfDevice = scanner.ScanForTpPlug(plugName);
			_MESSAGE("scan returned");
			defaultIp = ipOfDevice;
			if (ipOfDevice != "") {
				WritePrivateProfileStringA("Internal", settingName.c_str(), defaultIp.c_str(), sExecPath.c_str());
			}
			//Console::ReadLine();
		}

		bool successConnect = tpLinkConn.ConnectToHost(9999, (char*)defaultIp.c_str());
		if (successConnect) {
			_MESSAGE("Connect Succeeded");
		}
		if (!successConnect) {
			_MESSAGE("no connection");
			// search for device
			// until 1 is found
			// if one is found
			// save new ip to ini file.
			SPScanner scanner;
			std::string ipOfDevice = scanner.ScanForTpPlug(plugName);
			defaultIp = ipOfDevice;
			WritePrivateProfileStringA("Internal", settingName.c_str(), defaultIp.c_str(), sExecPath.c_str());
			//Console::ReadLine();
		}
		else {
			// check if correct device:
			DeviceInfo deviceInfo = tpLinkConn.GetSystemInfo();
			if (deviceInfo.alias() != plugName) {
				_MESSAGE("wrong device");
				// search for device
				// until 1 is found
				// if one is found
				// save new ip to ini file.
				SPScanner scanner;
				std::string ipOfDevice = scanner.ScanForTpPlug(plugName);
				defaultIp = ipOfDevice;
				WritePrivateProfileStringA("Internal", settingName.c_str(), defaultIp.c_str(), sExecPath.c_str());
			}
		}
	}

	void ScanAndCheckConnectionWrapper() {
		ScanAndCheckConnection("ImmersiveSunPlug1", "sTpLinkPlugIp1");
		ScanAndCheckConnection("ImmersiveSunPlug2", "sTpLinkPlugIp2");
	}

	void SwitchStateControllThread() {
		// delay execution by 8 seconds to give time to scan
		std::this_thread::sleep_for(std::chrono::milliseconds(8000));
		//int i = 0;
		while (keepRunning) {
			// log the light data, maybe can be sorted out, what is what:
			//if (_playerRef != NULL) {
			//	if (i == 20) {
			//		i = 0;
			//		ExtraSceneData* data = static_cast<RTTI_ExtraSceneData*>(_playerRef->extraData.GetByType(kExtraData_SceneData)); //ExtraContainerChanges
			//		if (data) {
			//			std::string logMsgExtraLightData("\nExtraLightData: ");
			//			float unknown1 = data->unk10;
			//			logMsgExtraLightData.append(std::to_string(unknown1));
			//			/*UInt32 unknown2 = data->unk14;
			//			UInt32 unknown3 = data->unk18;
			//			float unknown4 = data->unk1C;
			//			UInt8 unknown5 = data->unk20;

			//			logMsgExtraLightData.append("; ");
			//			logMsgExtraLightData.append(std::to_string(unknown2));
			//			logMsgExtraLightData.append("; ");
			//			logMsgExtraLightData.append(std::to_string(unknown3));
			//			logMsgExtraLightData.append("; ");
			//			logMsgExtraLightData.append(std::to_string(unknown4));
			//			logMsgExtraLightData.append("; ");
			//			logMsgExtraLightData.append(std::to_string(unknown5));*/
			//			_MESSAGE(logMsgExtraLightData.c_str());
			//		}
			//		
			//	}
			//	i++;
			//}
			int internalCurrentSwitchState1 = currentSwitchState1;
			int internalCurrentSwitchState2 = currentSwitchState2;
			if (previousSwitchState1 != internalCurrentSwitchState1) {
				// state change detected!
				// reset all state variables:
				//std::cout << "var reset\n";
				std::string logMsg("\nSwitching mode to ");
				logMsg.append(std::to_string(internalCurrentSwitchState1));
				_MESSAGE(logMsg.c_str());

				TPLinkHelper tpLinkConn;
				if (tpLinkConn.LoadIPConfigFromFile("sTpLinkPlugIp1")) {
					tpLinkConn.SwitchRelayState(internalCurrentSwitchState1);
				}

				previousSwitchState1 = internalCurrentSwitchState1;
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
			}
			if (previousSwitchState2 != internalCurrentSwitchState2) {
				// state change detected!
				// reset all state variables:
				//std::cout << "var reset\n";
				std::string logMsg("\nSwitching mode to ");
				logMsg.append(std::to_string(internalCurrentSwitchState2));
				_MESSAGE(logMsg.c_str());

				TPLinkHelper tpLinkConn;
				if (tpLinkConn.LoadIPConfigFromFile("sTpLinkPlugIp2")) {
					tpLinkConn.SwitchRelayState(internalCurrentSwitchState2);
				}

				previousSwitchState2 = internalCurrentSwitchState2;
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(30));
		}
	}

	void WriteDefaultsToIniFile() {
		std::string sExecPath = GetIniPath();
		if (INVALID_FILE_ATTRIBUTES == GetFileAttributesA(sExecPath.c_str()) && GetLastError() == ERROR_FILE_NOT_FOUND)
		{
			std::ofstream outfile(sExecPath);
			//outfile << std::endl;
			outfile.close();
		}

		// check the version:
		char iniFileBuffer[100];
		GetPrivateProfileStringA("Internal", "sModVersion", "",
			iniFileBuffer, 100, sExecPath.c_str());
		std::string sCurrentVersion(iniFileBuffer);
		if (sCurrentVersion != MOD_VERSION) { // update code
			
		}

		std::ifstream iniFileContentStream(sExecPath);
		std::string iniFileContents;
		if (iniFileContentStream.is_open())
		{
			std::string line;
			while (getline(iniFileContentStream, line))
			{
				iniFileContents.append(line);
			}
			iniFileContentStream.close();

		}
		else {
			//_MESSAGE("Error: could not create ini file.");
		}

		if (iniFileContents.find("sModVersion") == std::string::npos) {
			WritePrivateProfileStringA("Internal", "sModVersion", MOD_VERSION.c_str(), sExecPath.c_str());
		}
		if (iniFileContents.find("sTpLinkPlugIp1") == std::string::npos) {
			WritePrivateProfileStringA("Internal", "sTpLinkPlugIp1", "", sExecPath.c_str());
		}
		if (iniFileContents.find("sTpLinkPlugIp2") == std::string::npos) {
			WritePrivateProfileStringA("Internal", "sTpLinkPlugIp2", "", sExecPath.c_str());
		}
		if (iniFileContents.find("bUseUdp") == std::string::npos) {
			WritePrivateProfileStringA("GeneralModConfig", "bUseUdp", std::to_string(1).c_str(), sExecPath.c_str());
		}

	}

	void LoadSettings() {
		std::string sExecPath = GetIniPath();
		// TODO
	}

	void RegisterPlayerInstance(StaticFunctionTag *base, TESObjectREFR* playerRef) {
		//currentSwitchState = bool_param;
		_playerRef = playerRef;
		_MESSAGE("Setting player ref!");
	}

	void UpdateNativeSunState(StaticFunctionTag *base, long bool_internalCell, long weatherClass, long keyWordMatches, float inGameTime,
			float sunglare, long locationId, long weatherId, float playerLightLevel) {
		// TODO
		std::string logMsg("\nUpdateNativeSunState called ");
		logMsg.append(std::to_string(bool_internalCell));
		logMsg.append("; ");
		logMsg.append(std::to_string(playerLightLevel));
		logMsg.append("; ");
		logMsg.append(std::to_string(weatherClass));
		logMsg.append("; ");
		logMsg.append(std::to_string(keyWordMatches));
		logMsg.append("; ");
		logMsg.append(std::to_string(inGameTime));
		logMsg.append("; ");
		logMsg.append(std::to_string(sunglare));
		logMsg.append("; ");
		logMsg.append(std::to_string(locationId));
		logMsg.append("; ");
		logMsg.append(std::to_string(weatherId));

		_MESSAGE(logMsg.c_str());
		int resultingLevel = 0;
		int mod = 0;
		if (bool_internalCell > 0) {
			std::bitset<32> keywordBits(keyWordMatches);
			if (keywordBits[4] == false) { // NOT outside
				currentSwitchState1 = 0;
				return;
			}
		}
		else {
			// daytime mod
			int timeCasted = (int)inGameTime;
			float inGameDayTime = inGameTime - timeCasted;
			std::string logMsgTime("\nCalc Time ");
			logMsgTime.append(std::to_string(inGameDayTime));
			logMsgTime.append("; result: ");
			logMsgTime.append(std::to_string(sunglare > 0.4 && playerLightLevel > 138 && inGameDayTime > 0.3 && inGameDayTime < 0.74 ? 1 : 0));
			_MESSAGE(logMsgTime.c_str());
			if (sunglare > 0.4 && playerLightLevel > 138 && inGameDayTime > 0.3 && inGameDayTime < 0.74) {
				currentSwitchState1 = 1; // sun is shining on the player
			}
			else {
				// if it doesn't match set the state to off again
				currentSwitchState1 = 0;
			}
		}
		
		return;
	}

	bool RegisterFuncs(VMClassRegistry* registry) {

		registry->RegisterFunction(
			new NativeFunction1<StaticFunctionTag, void, TESObjectREFR*>("RegisterPlayerInstance", "ImmersiveSunPluginScript", ImmersiveSun::RegisterPlayerInstance, registry));
		registry->RegisterFunction(
			new NativeFunction8<StaticFunctionTag, void, long, long, long, float, float, long, long, float>("UpdateNativeSunState", "ImmersiveSunPluginScript", ImmersiveSun::UpdateNativeSunState, registry));
		InitBgThread();
		return true;
	}

	void InitBgThread() {
		_MESSAGE("InitBgThread");

		// int the settings first - very important
		WriteDefaultsToIniFile();
		LoadSettings();

		//_MESSAGE("Load settings");
		// start scan thread
		std::thread t(ScanAndCheckConnectionWrapper);
		t.detach();

		//_MESSAGE("Load settings");
		// start thread for plug control (delayed)
		std::thread t2(SwitchStateControllThread);
		t2.detach();
	}

}
