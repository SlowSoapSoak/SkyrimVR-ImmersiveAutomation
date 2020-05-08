#include "ImmersiveWinds.h"
#include <shlobj.h>
#include <intrin.h>
#include <vector>
#include <iostream>
#include <fstream> 
#include <stdio.h>
#include <shlobj.h>
#include <intrin.h>
#include <string>
#include "skse64/GameForms.h"

#include "SPScanner.h"

namespace ImmersiveWinds {


	std::string GetIniPath() {
		CHAR my_documents[MAX_PATH];
		HRESULT result = SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, my_documents);

		std::string sExecPath(my_documents);
		sExecPath.append("\\My Games\\Skyrim VR\\SKSE\\");
		sExecPath.append("immersiveWinds.ini");
		return sExecPath;
	}

	void ScanAndCheckConnection() {

		std::string plugName = "ImmersiveWindsPlug";
		TPLinkHelper tpLinkConn(_logsEnabled);

		std::string sExecPath = ImmersiveWinds::GetIniPath();

		char iniFileBuffer[100];
		GetPrivateProfileStringA("Internal", "sTpLinkPlugIp", "",
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
				WritePrivateProfileStringA("Internal", "sTpLinkPlugIp", defaultIp.c_str(), sExecPath.c_str());
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
			WritePrivateProfileStringA("Internal", "sTpLinkPlugIp", defaultIp.c_str(), sExecPath.c_str());
			//Console::ReadLine();
		}
		else {
			// check if correct device:
			tpLinkConn.LoadIPConfigFromFile();
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
				WritePrivateProfileStringA("Internal", "sTpLinkPlugIp", defaultIp.c_str(), sExecPath.c_str());
			}
		}
	}

	void SwitchStateControllThread() {
		// delay execution by 8 seconds to give time to scan
		std::this_thread::sleep_for(std::chrono::milliseconds(8000));

		while (_keepRunning) {
			if (_previousSwitchState != _currentSwitchState) {
				// state change detected!
				// reset all state variables:
				//std::cout << "var reset\n";
				if (_logsEnabled) {
					std::string logMsg("\nSwitching mode to ");
					logMsg.append(std::to_string(_currentSwitchState));
					_MESSAGE(logMsg.c_str());
				}
				_s1Swap = true;
				_levelValues[_currentSwitchState].counterLeaveCurrentState = 0;
				_previousSwitchState = _currentSwitchState;
				_beginTime = clock();
			}

			//if (_shoutLevel > 0) {
			//	if (float(clock() - _shoutBeginTime) > 30) {
			//		// after 30 seconds the wind gets back to normal.
			//		_shoutLevel = 0;
			//		UpdateNativeWindStateInternal();
			//	}
			//}

			// handle states:
			if (_s1Swap && _levelValues[_currentSwitchState].counterLeaveOnMax != 0) {
				if (_levelValues[_currentSwitchState].counterLeaveCurrentState == 0) {
					//std::cout << "Mode" << currentSwitchState << ": Swap on\n";
					if (_levelValues[_currentSwitchState].randomOnAdd > 0) {
						_levelValues[_currentSwitchState].currentRandomOn = _levelValues[_currentSwitchState].distributionOn(_generator);
					}
					if (_logsEnabled) {
						_MESSAGE("SwitchingOn");
					}
					TPLinkHelper tpLinkConn(_logsEnabled);
					if (tpLinkConn.LoadIPConfigFromFile()) {
						tpLinkConn.SwitchRelayState(1);
					}
				}
				_levelValues[_currentSwitchState].counterLeaveCurrentState++;
				if (float(clock() - _beginTime) >= (_levelValues[_currentSwitchState].counterLeaveOnMax + _levelValues[_currentSwitchState].currentRandomOn) * CLOCKS_PER_SEC) {
					_levelValues[_currentSwitchState].counterLeaveCurrentState = 0;
					_beginTime = clock();
					_s1Swap = false;
				}
			}
			else if (_levelValues[_currentSwitchState].counterLeaveOffMax != 0) {
				if (_levelValues[_currentSwitchState].counterLeaveCurrentState == 0) {
					if (_levelValues[_currentSwitchState].randomOffAdd > 0) {
						_levelValues[_currentSwitchState].currentRandomOff = _levelValues[_currentSwitchState].distributionOff(_generator);
					}
					if (_logsEnabled) {
						_MESSAGE("SwitchingOff");
					}
					//std::cout << "Mode" << currentSwitchState << ": Swap off\n";
					TPLinkHelper tpLinkConn(_logsEnabled);
					if (tpLinkConn.LoadIPConfigFromFile()) {
						tpLinkConn.SwitchRelayState(0);
					}
				}
				_levelValues[_currentSwitchState].counterLeaveCurrentState++;
				if (float(clock() - _beginTime) >= (_levelValues[_currentSwitchState].counterLeaveOffMax + _levelValues[_currentSwitchState].currentRandomOff) * CLOCKS_PER_SEC) {
					_levelValues[_currentSwitchState].counterLeaveCurrentState = 0;
					_beginTime = clock();
					_s1Swap = true;
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
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
		//	// older version 
		//	// read old data
		//	unsigned int useUDP = GetPrivateProfileIntA("ImmersiveWinds", "useUdp", 0, sExecPath.c_str());
		//	unsigned int bIntermittentModeEnabled = GetPrivateProfileIntA("ImmersiveWinds", "bIntermittentModeEnabled", 0, sExecPath.c_str());
		//	GetPrivateProfileStringA("ImmersiveWinds", "adapterIp", "",
		//		iniFileBuffer, 100, sExecPath.c_str());
		//	std::string adapterIp(iniFileBuffer);

		//	// delete old contents
		//	std::ofstream ofs;
		//	ofs.open(sExecPath, std::ofstream::out | std::ofstream::trunc);
		//	ofs.close();

		//	//_MESSAGE((std::string(" old useUdp :") + std::to_string(useUDP)).c_str());

		//	// write new content using previous settings
		//	WritePrivateProfileStringA("Internal", "sTpLinkPlugIp", adapterIp.c_str(), sExecPath.c_str());
			WritePrivateProfileStringA("GeneralModConfig", "bUseUdp", std::to_string(1).c_str(), sExecPath.c_str());
			//WritePrivateProfileStringA("GeneralModConfig", "bIntermittentModeEnabled", std::to_string(bIntermittentModeEnabled).c_str(), sExecPath.c_str());
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
		else if(sCurrentVersion != MOD_VERSION){
			WritePrivateProfileStringA("Internal", "sModVersion", MOD_VERSION.c_str(), sExecPath.c_str());
		}

		if (iniFileContents.find("sTpLinkPlugIp") == std::string::npos) {
			WritePrivateProfileStringA("Internal", "sTpLinkPlugIp", "", sExecPath.c_str());
		}
		if (iniFileContents.find("bUseUdp") == std::string::npos) {
			WritePrivateProfileStringA("GeneralModConfig", "bUseUdp", std::to_string(1).c_str(), sExecPath.c_str());
		}
		if (iniFileContents.find("bIntermittentModeEnabled") == std::string::npos) {
			WritePrivateProfileStringA("GeneralModConfig", "bIntermittentModeEnabled", std::to_string(1).c_str(),
				sExecPath.c_str());
		}
		if (iniFileContents.find("bLoggingEnabled") == std::string::npos) {
			WritePrivateProfileStringA("GeneralModConfig", "bLoggingEnabled", std::to_string(0).c_str(),
				sExecPath.c_str());
		}

		// write all IntermittendModeStrengthConfig vars
		for (int i = 1; i < 7; i++) {
			std::string intermittendLXOn = "fIntermittentL" + std::to_string(i) + "On";
			std::string intermittendLXOff = "fIntermittentL" + std::to_string(i) + "Off";
			std::string intermittendLXRandomAddOn = "fIntermittentL" + std::to_string(i) + "RandomAddOn";
			std::string intermittendLXRandomAddOff = "fIntermittentL" + std::to_string(i) + "RandomAddOff";
			
			if (iniFileContents.find(intermittendLXOn) == std::string::npos) {
				WritePrivateProfileStringA("IntermittendModeStrengthConfig", intermittendLXOn.c_str(), std::to_string(DEFAULTS_INTERMITTENT[i - 1][0]).c_str(),
					sExecPath.c_str());
			}
			if (iniFileContents.find(intermittendLXOff) == std::string::npos) {
				WritePrivateProfileStringA("IntermittendModeStrengthConfig", intermittendLXOff.c_str(), std::to_string(DEFAULTS_INTERMITTENT[i - 1][1]).c_str(),
					sExecPath.c_str());
			}
			if (iniFileContents.find(intermittendLXRandomAddOn) == std::string::npos) {
				WritePrivateProfileStringA("IntermittendModeStrengthConfig", intermittendLXRandomAddOn.c_str(), std::to_string(DEFAULTS_INTERMITTENT[i - 1][2]).c_str(),
					sExecPath.c_str());
			}
			if (iniFileContents.find(intermittendLXRandomAddOff) == std::string::npos) {
				WritePrivateProfileStringA("IntermittendModeStrengthConfig", intermittendLXRandomAddOff.c_str(), std::to_string(DEFAULTS_INTERMITTENT[i - 1][3]).c_str(),
					sExecPath.c_str());
			}
			//_MESSAGE("looping config write");
		}

		//_MESSAGE("looping config write done");
		
		if (iniFileContents.find("iInsideHouseBase") == std::string::npos) {
			WritePrivateProfileStringA("IntermittendModeLevelConfig", "iInsideHouseBase", std::to_string(DEFAULT_INTERMITTENT_iInsideHouseBase).c_str(),
				sExecPath.c_str());
		}
		if (iniFileContents.find("iInsideDungeonBase") == std::string::npos) {
			WritePrivateProfileStringA("IntermittendModeLevelConfig", "iInsideDungeonBase", std::to_string(DEFAULT_INTERMITTENT_iInsideDungeonBase).c_str(),
				sExecPath.c_str());
		}
		if (iniFileContents.find("iInsideCaveBase") == std::string::npos) {
			WritePrivateProfileStringA("IntermittendModeLevelConfig", "iInsideCaveBase", std::to_string(DEFAULT_INTERMITTENT_iInsideCaveBase).c_str(),
				sExecPath.c_str());
		}
		if (iniFileContents.find("iInsideIceCaveBase") == std::string::npos) {
			WritePrivateProfileStringA("IntermittendModeLevelConfig", "iInsideIceCaveBase", std::to_string(DEFAULT_INTERMITTENT_iInsideIceCaveBase).c_str(),
				sExecPath.c_str());
		}
		if (iniFileContents.find("iOutsideBase") == std::string::npos) {
			WritePrivateProfileStringA("IntermittendModeLevelConfig", "iOutsideBase", std::to_string(DEFAULT_INTERMITTENT_iOutsideBase).c_str(),
				sExecPath.c_str());
		}
		if (iniFileContents.find("iOutsideTimeDayMod") == std::string::npos) {
			WritePrivateProfileStringA("IntermittendModeLevelConfig", "iOutsideTimeDayMod", std::to_string(DEFAULT_INTERMITTENT_iOutsideTimeDayMod).c_str(),
				sExecPath.c_str());
		}
		if (iniFileContents.find("iOutsideTimeNightMod") == std::string::npos) {
			WritePrivateProfileStringA("IntermittendModeLevelConfig", "iOutsideTimeNightMod", std::to_string(DEFAULT_INTERMITTENT_iOutsideTimeNightMod).c_str(),
				sExecPath.c_str());
		}
		if (iniFileContents.find("iOutsideWeatherPleasantMod") == std::string::npos) {
			WritePrivateProfileStringA("IntermittendModeLevelConfig", "iOutsideWeatherPleasantMod", std::to_string(DEFAULT_INTERMITTENT_iOutsideWeatherPleasantMod).c_str(),
				sExecPath.c_str());
		}
		if (iniFileContents.find("iOutsideWeatherCloudyMod") == std::string::npos) {
			WritePrivateProfileStringA("IntermittendModeLevelConfig", "iOutsideWeatherCloudyMod", std::to_string(DEFAULT_INTERMITTENT_iOutsideWeatherCloudyMod).c_str(),
				sExecPath.c_str());
		}
		if (iniFileContents.find("iOutsideWeatherRainMod") == std::string::npos) {
			WritePrivateProfileStringA("IntermittendModeLevelConfig", "iOutsideWeatherRainMod", std::to_string(DEFAULT_INTERMITTENT_iOutsideWeatherRainMod).c_str(),
				sExecPath.c_str());
		}
		if (iniFileContents.find("iOutsideWeatherSnowMod") == std::string::npos) {
			WritePrivateProfileStringA("IntermittendModeLevelConfig", "iOutsideWeatherSnowMod", std::to_string(DEFAULT_INTERMITTENT_iOutsideWeatherSnowMod).c_str(),
				sExecPath.c_str());
		}
		if (iniFileContents.find("iOutsideWeatherStormMod") == std::string::npos) {
			WritePrivateProfileStringA("IntermittendModeLevelConfig", "iOutsideWeatherStormMod", std::to_string(DEFAULT_INTERMITTENT_iOutsideWeatherStormMod).c_str(),
				sExecPath.c_str());
		}
		if (iniFileContents.find("iOutsideAltitudeHighMod") == std::string::npos) {
			WritePrivateProfileStringA("IntermittendModeLevelConfig", "iOutsideAltitudeHighMod", std::to_string(DEFAULT_INTERMITTENT_iOutsideAltitudeHighMod).c_str(),
				sExecPath.c_str());
		}
		if (iniFileContents.find("iOutsideAltitudeVeryHighMod") == std::string::npos) {
			WritePrivateProfileStringA("IntermittendModeLevelConfig", "iOutsideAltitudeVeryHighMod", std::to_string(DEFAULT_INTERMITTENT_iOutsideAltitudeVeryHighMod).c_str(),
				sExecPath.c_str());
		}
		if (iniFileContents.find("iHighAltitudeBegin") == std::string::npos) {
			WritePrivateProfileStringA("IntermittendModeLevelConfig", "iHighAltitudeBegin", std::to_string(DEFAULT_INTERMITTENT_iHighAltitudeBegin).c_str(),
				sExecPath.c_str());
		}
		if (iniFileContents.find("iVeryHighAltitudeBegin") == std::string::npos) {
			WritePrivateProfileStringA("IntermittendModeLevelConfig", "iVeryHighAltitudeBegin", std::to_string(DEFAULT_INTERMITTENT_iVeryHighAltitudeBegin).c_str(),
				sExecPath.c_str());
		}
		
		//_MESSAGE("write settings done");
	}

	void LoadSettings() {
		std::string sExecPath = GetIniPath();
		unsigned int bIntermittentModeEnabled = GetPrivateProfileIntA("GeneralModConfig", "bIntermittentModeEnabled", 0, sExecPath.c_str());
		unsigned int bLoggingEnabled = GetPrivateProfileIntA("GeneralModConfig", "bLoggingEnabled", 0, sExecPath.c_str());
		_logsEnabled = bLoggingEnabled > 0;

		std::vector<std::vector<float>> intermittendWindStrengthConfig;

		
		if (bIntermittentModeEnabled > 0) {
			char iniFileBuffer[100];
			for (int i = 1; i < 7; i++) {
				intermittendWindStrengthConfig.push_back(std::vector<float>());

				std::string intermittendLXOn = "fIntermittentL" + std::to_string(i) + "On";
				std::string intermittendLXOff = "fIntermittentL" + std::to_string(i) + "Off";
				std::string intermittendLXRandomAddOn = "fIntermittentL" + std::to_string(i) + "RandomAddOn";
				std::string intermittendLXRandomAddOff = "fIntermittentL" + std::to_string(i) + "RandomAddOff";

				GetPrivateProfileStringA("IntermittendModeStrengthConfig", intermittendLXOn.c_str(), std::to_string(DEFAULTS_INTERMITTENT[i-1][0]).c_str(),
					iniFileBuffer, 100, sExecPath.c_str());
				std::string intermittendLXOnStr(iniFileBuffer);

				intermittendWindStrengthConfig[i-1].push_back(strtof(intermittendLXOnStr.c_str(), 0));

				GetPrivateProfileStringA("IntermittendModeStrengthConfig", intermittendLXOff.c_str(), std::to_string(DEFAULTS_INTERMITTENT[i - 1][1]).c_str(),
					iniFileBuffer, 100, sExecPath.c_str());
				std::string intermittendLXOffStr(iniFileBuffer);
				intermittendWindStrengthConfig[i-1].push_back(strtof(intermittendLXOffStr.c_str(), 0));

				GetPrivateProfileStringA("IntermittendModeStrengthConfig", intermittendLXRandomAddOn.c_str(), std::to_string(DEFAULTS_INTERMITTENT[i - 1][2]).c_str(),
					iniFileBuffer, 100, sExecPath.c_str());
				std::string intermittendLXRandomAddOnStr(iniFileBuffer);
				intermittendWindStrengthConfig[i-1].push_back(strtof(intermittendLXRandomAddOnStr.c_str(), 0));

				GetPrivateProfileStringA("IntermittendModeStrengthConfig", intermittendLXRandomAddOff.c_str(), std::to_string(DEFAULTS_INTERMITTENT[i - 1][3]).c_str(),
					iniFileBuffer, 100, sExecPath.c_str());
				std::string intermittendLXRandomAddOffStr(iniFileBuffer);
				intermittendWindStrengthConfig[i-1].push_back(strtof(intermittendLXRandomAddOffStr.c_str(), 0));

			}

			//_MESSAGE("loaded intermittent strengths");

			for (int i = 0; i < 6; i++) {
				if (intermittendWindStrengthConfig[i][0] <= 0 && intermittendWindStrengthConfig[i][1] <= 0) {
					intermittendWindStrengthConfig[i][0] = DEFAULTS_INTERMITTENT[i][0];
					intermittendWindStrengthConfig[i][1] = DEFAULTS_INTERMITTENT[i][1];
				}
				else if (intermittendWindStrengthConfig[i][0] <= 0) {
					intermittendWindStrengthConfig[i][1] = 999999;
				}
				else if (intermittendWindStrengthConfig[i][1] <= 0) {
					intermittendWindStrengthConfig[i][0] = 999999;
				}
				else {
					if (intermittendWindStrengthConfig[i][0] < 1) {
						intermittendWindStrengthConfig[i][0] = 1;
					}
					if (intermittendWindStrengthConfig[i][1] < 1) {
						intermittendWindStrengthConfig[i][1] = 1;
					}
				}
			}


			_MESSAGE("corrected intermittent strengths");

			iInsideHouseBase = GetPrivateProfileIntA("IntermittendModeLevelConfig", "iInsideHouseBase", DEFAULT_INTERMITTENT_iInsideHouseBase, sExecPath.c_str());
			iInsideDungeonBase = GetPrivateProfileIntA("IntermittendModeLevelConfig", "iInsideDungeonBase", DEFAULT_INTERMITTENT_iInsideDungeonBase, sExecPath.c_str());
			iInsideCaveBase = GetPrivateProfileIntA("IntermittendModeLevelConfig", "iInsideCaveBase", DEFAULT_INTERMITTENT_iInsideCaveBase, sExecPath.c_str());
			iInsideIceCaveBase = GetPrivateProfileIntA("IntermittendModeLevelConfig", "iInsideIceCaveBase", DEFAULT_INTERMITTENT_iInsideIceCaveBase, sExecPath.c_str());
			iOutsideBase = GetPrivateProfileIntA("IntermittendModeLevelConfig", "iOutsideBase", DEFAULT_INTERMITTENT_iOutsideBase, sExecPath.c_str());
			iOutsideTimeDayMod = GetPrivateProfileIntA("IntermittendModeLevelConfig", "iOutsideTimeDayMod", DEFAULT_INTERMITTENT_iOutsideTimeDayMod, sExecPath.c_str());
			iOutsideTimeNightMod = GetPrivateProfileIntA("IntermittendModeLevelConfig", "iOutsideTimeNightMod", DEFAULT_INTERMITTENT_iOutsideTimeNightMod, sExecPath.c_str());
			iOutsideWeatherPleasantMod = GetPrivateProfileIntA("IntermittendModeLevelConfig", "iOutsideWeatherPleasantMod", DEFAULT_INTERMITTENT_iOutsideWeatherPleasantMod, sExecPath.c_str());
			iOutsideWeatherCloudyMod = GetPrivateProfileIntA("IntermittendModeLevelConfig", "iOutsideWeatherCloudyMod", DEFAULT_INTERMITTENT_iOutsideWeatherCloudyMod, sExecPath.c_str());
			iOutsideWeatherRainMod = GetPrivateProfileIntA("IntermittendModeLevelConfig", "iOutsideWeatherRainMod", DEFAULT_INTERMITTENT_iOutsideWeatherRainMod, sExecPath.c_str());
			iOutsideWeatherSnowMod = GetPrivateProfileIntA("IntermittendModeLevelConfig", "iOutsideWeatherSnowMod", DEFAULT_INTERMITTENT_iOutsideWeatherSnowMod, sExecPath.c_str());
			iOutsideWeatherStormMod = GetPrivateProfileIntA("IntermittendModeLevelConfig", "iOutsideWeatherStormMod", DEFAULT_INTERMITTENT_iOutsideWeatherStormMod, sExecPath.c_str());
			iOutsideAltitudeHighMod = GetPrivateProfileIntA("IntermittendModeLevelConfig", "iOutsideAltitudeHighMod", DEFAULT_INTERMITTENT_iOutsideAltitudeHighMod, sExecPath.c_str());
			iOutsideAltitudeVeryHighMod = GetPrivateProfileIntA("IntermittendModeLevelConfig", "iOutsideAltitudeVeryHighMod", DEFAULT_INTERMITTENT_iOutsideAltitudeVeryHighMod, sExecPath.c_str());
			iHighAltitudeBegin = GetPrivateProfileIntA("IntermittendModeLevelConfig", "iHighAltitudeBegin", DEFAULT_INTERMITTENT_iHighAltitudeBegin, sExecPath.c_str());
			iVeryHighAltitudeBegin = GetPrivateProfileIntA("IntermittendModeLevelConfig", "iVeryHighAltitudeBegin", DEFAULT_INTERMITTENT_iVeryHighAltitudeBegin, sExecPath.c_str());

			//_MESSAGE("loaded intermittent mode");
			ImmersiveWinds::_levelValues[0] = ImmersiveWinds::LevelValues(0, 999999, 0, 0);
			for (int i = 0; i < 6; i++) {
				ImmersiveWinds::_levelValues[i+1] = ImmersiveWinds::LevelValues(intermittendWindStrengthConfig[i][0], intermittendWindStrengthConfig[i][1], intermittendWindStrengthConfig[i][2], intermittendWindStrengthConfig[i][3]);
			}
		}
		else {

			// set the values in a way that only 2 states will be necessary
			iHighAltitudeBegin = 4000;
			iVeryHighAltitudeBegin = 18000;
			iInsideHouseBase = 0;
			iInsideDungeonBase = 0;
			iInsideCaveBase = 0;
			iInsideIceCaveBase = 0;
			iOutsideBase = 1;
			iOutsideTimeDayMod = 0;
			iOutsideTimeNightMod = 0;
			iOutsideWeatherPleasantMod = 0;
			iOutsideWeatherCloudyMod = 0;
			iOutsideWeatherRainMod = 0;
			iOutsideWeatherSnowMod = 0;
			iOutsideWeatherStormMod = 0;
			iOutsideAltitudeHighMod = 0;
			iOutsideAltitudeVeryHighMod = 0;

			//_MESSAGE("loaded simple mode");
			ImmersiveWinds::_levelValues[0] = ImmersiveWinds::LevelValues(0, 999999, 0, 0);
			ImmersiveWinds::_levelValues[1] = ImmersiveWinds::LevelValues(999999, 0, 0, 0);

		}
		//_MESSAGE("load settings done");
	}

	void AsyncExcecSwitchState(long state)
	{
		if (_logsEnabled) {
			std::string logMsg("\nSetSwitchState called ");
			logMsg.append(std::to_string(state));
			_MESSAGE(logMsg.c_str());
		}
		TPLinkHelper tpLinkConn(_logsEnabled);

		// load the ip for the connection
		tpLinkConn.LoadIPConfigFromFile();
		if (state >= 1) {
			tpLinkConn.SwitchRelayState(1);
		}
		else {
			tpLinkConn.SwitchRelayState(0);
		}
	}

	float GetWindSpeed(TESWeather* thisWeather)
	{
		return (thisWeather) ? (thisWeather->general.windSpeed / 256.0) : 0.0;
	}

	float SetSwitchState(StaticFunctionTag *base, long bool_param) {
		return 0;
	}

	void UpdateNativeWindStateInternal() {
		


		int resultingLevel = 0;
		int mod = 0;
		if (_internalCell > 0) {
			resultingLevel = iInsideHouseBase;

			std::bitset<32> keywordBits(_keyWordMatches);
			if (keywordBits[13] == true // dwelling
				|| keywordBits[14] == true // farm
				|| keywordBits[15] == true // guild
				|| keywordBits[20] == true // inn (Gasthaus)
				|| keywordBits[21] == true // jail
				|| keywordBits[22] == true // lumber mill
				|| keywordBits[25] == true // steward dwelling
				|| keywordBits[26] == true // store
				|| keywordBits[27] == true // player house
				|| keywordBits[28] == true // house
				|| keywordBits[29] == true // castle
				) {
				resultingLevel = iInsideHouseBase;
			}
			else {
				// dungeon base is the default if house was not detected.
				resultingLevel = iInsideDungeonBase;

				//if (keywordBits[2] == true // nordic ruin
				//	|| keywordBits[3] == true // dwarwen ruin
				//	|| keywordBits[5] == true // dungeon
				//	|| keywordBits[7] == true // dragon priest lair
				//	|| keywordBits[8] == true // dragon lair
				//	|| keywordBits[9] == true // draugr crypt
				//	|| keywordBits[10] == true // falmer hive
				//	|| keywordBits[11] == true // mine
				//	|| keywordBits[12] == true // temple
				//	) {
				//	if (resultingLevel < iInsideDungeonBase) {
				//		resultingLevel = iInsideDungeonBase;
				//	}
				//}
				if (keywordBits[0] == true) { // cave
					if (resultingLevel < iInsideCaveBase) {
						resultingLevel = iInsideCaveBase;
					}
				}
				if (keywordBits[1] == true) { // ice cave
					if (resultingLevel < iInsideIceCaveBase) {
						resultingLevel = iInsideIceCaveBase;
					}
				}

				if (keywordBits[4] == true) { // outside actually; shall other mods be applied as well?
					if (resultingLevel < iOutsideBase) {
						resultingLevel = iOutsideBase;
					}
				}
				// ...
			}

			
		}
		else {
			resultingLevel = iOutsideBase;

			// altitude mode
			/*if (_altitude > iVeryHighAltitudeBegin) {
				mod += iOutsideAltitudeVeryHighMod;
				_MESSAGE("very high altitude!");
			}
			else if (_altitude > iHighAltitudeBegin) {
				mod += iOutsideAltitudeHighMod;
				_MESSAGE("high altitude!");
			}*/

			// daytime mod
			int timeCasted = (int)_inGameTime;
			float inGameDayTime = _inGameTime - timeCasted;
			if (inGameDayTime > 0.3 && inGameDayTime < 0.8) { // check if daytime
				mod += iOutsideTimeDayMod;
			}
			else { // nighttime
				mod += iOutsideTimeNightMod;
			}

			// weather mod

			long weatherIdMasked = 0x00FFFFFF & _weatherId;
			if (weatherIdMasked == 0x10C32F || weatherIdMasked == 0x10A241    // vanilla
				|| weatherIdMasked == 0x10A23C || weatherIdMasked == 0x0D4886
				|| weatherIdMasked == 0x0C8221 || weatherIdMasked == 0x0C8220
				|| weatherIdMasked == 0x001330 || weatherIdMasked == 0x001318 // true storms
				|| weatherIdMasked == 0x18BE6E || weatherIdMasked == 0x187DD3 // vivid weathers 
				|| weatherIdMasked == 0x12DDEE || weatherIdMasked == 0x12DDED
				|| weatherIdMasked == 0x12DDEC || weatherIdMasked == 0x16B287)
			{
				// TODO add more id's for support for other mods.
				//_MESSAGE("StormWeather detected!");

				mod += iOutsideWeatherStormMod;
			}
			else {
				if (_weatherClass == 0) { // pleasant
					mod += iOutsideWeatherPleasantMod;
				}
				else if (_weatherClass == 1) { // cloudy
					if (_sunglare > 0.5) {
						//_MESSAGE("cloudy but sunglare is over 0.5 -> good weather");
						// still good weather? 
						mod += iOutsideWeatherPleasantMod;
					}
					else {
						mod += iOutsideWeatherCloudyMod;
					}
				}
				else if (_weatherClass == 2) { // rain
					mod += iOutsideWeatherRainMod;
				}
				else if (_weatherClass == 3) { // snow
					mod += iOutsideWeatherSnowMod;
				}

			}
		}
		int result = resultingLevel + mod; // assign to the global strength variable

		if (_shoutLevel > 0) { // shout has the highest prio
			if (_shoutLevel == 1 && result < 5) {
				result = 5;
			}
			if (_shoutLevel == 2) {
				result = 6;
			}
		}
		if (result > 6) {
			result = 6;
		}
		_currentSwitchState = result;
		if (_logsEnabled) {
			std::string logMsgRes("Resulted in: ");
			logMsgRes.append(std::to_string(result));
			_MESSAGE(logMsgRes.c_str());
		}
	}

	void UpdateNativeWindState(StaticFunctionTag *base, long bool_internalCell, float altitude, long weatherClass, long keyWordMatches, float inGameTime,
		float sunglare, long locationId, long weatherId, float windSpeed) {
		if (_logsEnabled) {
			std::string logMsg("\nUpdateNativeWindState called ");
			logMsg.append(std::to_string(bool_internalCell));
			logMsg.append("; ");
			logMsg.append(std::to_string(altitude));
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
		}
		

		_internalCell = bool_internalCell;
		_altitude = altitude;
		_weatherClass = weatherClass;
		_keyWordMatches = keyWordMatches;
		_inGameTime = inGameTime;
		_sunglare = sunglare;
		_locationId = locationId;
		_weatherId = weatherId;
		_windSpeed = windSpeed;

		UpdateNativeWindStateInternal();
	}


	void TriggerShout(StaticFunctionTag *base, long level) {
		if (_logsEnabled) {
			std::string shoutLog = "Triggered shout with level: ";
			shoutLog.append(std::to_string(level));
			_MESSAGE(shoutLog.c_str());
		}
		_shoutLevel = level;
		_shoutBeginTime = clock();
		UpdateNativeWindStateInternal();
	}

	void EndShout(StaticFunctionTag *base) {
		if (_logsEnabled) {
			std::string shoutLog = "End shout";
			_MESSAGE(shoutLog.c_str());
		}
		_shoutLevel = 0;
		UpdateNativeWindStateInternal();
	}

	bool RegisterFuncs(VMClassRegistry* registry) {
		/*registry->RegisterFunction(
			new NativeFunction1<StaticFunctionTag, void, long>("TriggerShout", "ImmersiveWindsPluginScript", ImmersiveWinds::TriggerShout, registry));*/
		registry->RegisterFunction(
			new NativeFunction0 <TESWeather, float>("GetWindSpeed", "Weather", ImmersiveWinds::GetWindSpeed, registry));
		
		registry->RegisterFunction(
			new NativeFunction1<StaticFunctionTag, void, long>("TriggerShout", "ImmersiveWindsPluginScript", ImmersiveWinds::TriggerShout, registry));
		registry->RegisterFunction(
			new NativeFunction0<StaticFunctionTag, void>("EndShout", "ImmersiveWindsPluginScript", ImmersiveWinds::EndShout, registry));
		registry->RegisterFunction(
			new NativeFunction1<StaticFunctionTag, float, long>("SetSwitchState", "ImmersiveWindsPluginScript", ImmersiveWinds::SetSwitchState, registry));
		registry->RegisterFunction(
			new NativeFunction9<StaticFunctionTag, void, long, float, long, long, float, float, long, long, float>("UpdateNativeWindState", "ImmersiveWindsPluginScript", ImmersiveWinds::UpdateNativeWindState, registry));
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
		std::thread t(ScanAndCheckConnection);
		t.detach();

		//_MESSAGE("Load settings");
		// start thread for plug control (delayed)
		std::thread t2(SwitchStateControllThread);
		t2.detach();
	}

}
