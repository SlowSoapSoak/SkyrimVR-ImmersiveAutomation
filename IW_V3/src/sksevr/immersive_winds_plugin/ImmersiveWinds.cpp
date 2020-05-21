#include "ImmersiveWinds.h"


namespace ImmersiveWinds {


	std::string GetIniPath()
	{
		CHAR my_documents[MAX_PATH];
		HRESULT result = SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, my_documents);

		std::string sExecPath(my_documents);
		sExecPath.append("\\My Games\\Skyrim VR\\SKSE\\");
		sExecPath.append("immersiveWinds.ini");
		return sExecPath;
	}

	void ScanAndCheckConnection()
	{

		std::string plugName = "ImmersiveWindsPlug";
		TPLinkHelper tpLinkConn(_logsEnabled);

		std::string sExecPath = ImmersiveWinds::GetIniPath();

		char iniFileBuffer[100];
		GetPrivateProfileStringA("Internal", "sTpLinkPlugIp", "",
			iniFileBuffer, 100, sExecPath.c_str());
		std::string defaultIp(iniFileBuffer);
		LOG("READ ini file:");
		LOG(defaultIp.c_str());
		if (defaultIp.empty()) {
			LOG("no ip specified");
			// search for device
			// until 1 is found
			// if one is found
			// save new ip to ini file.
			SPScanner scanner;
			std::string ipOfDevice = scanner.ScanForTpPlug(plugName);
			LOG("scan returned: %s", ipOfDevice.c_str());
			defaultIp = ipOfDevice;
			if (!ipOfDevice.empty()) {
				WritePrivateProfileStringA("Internal", "sTpLinkPlugIp", defaultIp.c_str(), sExecPath.c_str());
				workingIp = defaultIp;
			}

			//Console::ReadLine();
		}		

		bool successConnect = tpLinkConn.ConnectToHost(9999, (char*)defaultIp.c_str());
		if (successConnect) {
			LOG("Connect Succeeded");
		}
		if (!successConnect) {
			LOG("no connection");
			// search for device
			// until 1 is found
			// if one is found
			// save new ip to ini file.
			SPScanner scanner;
			std::string ipOfDevice = scanner.ScanForTpPlug(plugName);
			defaultIp = ipOfDevice;
			WritePrivateProfileStringA("Internal", "sTpLinkPlugIp", defaultIp.c_str(), sExecPath.c_str());
			workingIp = defaultIp;
			//Console::ReadLine();
		}
		else {
			// check if correct device:
			tpLinkConn.LoadIPConfigFromFile(useUDP,workingIp);
			DeviceInfo deviceInfo = tpLinkConn.GetSystemInfo();
			if (deviceInfo.alias() != plugName) {
				LOG("wrong device");
				// search for device
				// until 1 is found
				// if one is found
				// save new ip to ini file.
				SPScanner scanner;
				std::string ipOfDevice = scanner.ScanForTpPlug(plugName);
				defaultIp = ipOfDevice;
				WritePrivateProfileStringA("Internal", "sTpLinkPlugIp", defaultIp.c_str(), sExecPath.c_str());
				workingIp = defaultIp;
			}
		}
	}

	void SwitchStateControllThread() {
		// delay execution by 8 seconds to give time to scan
		Sleep(8000);

		while (_keepRunning) 
		{
			if (_previousSwitchState != _currentSwitchState) {
				// state change detected!
				// reset all state variables:
				//std::cout << "var reset\n";
				LOG("\nSwitching mode to %ld", _currentSwitchState);
				
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
					
					LOG("SwitchingOn");
					
					TPLinkHelper tpLinkConn(_logsEnabled);
					if (tpLinkConn.LoadIPConfigFromFile(useUDP, workingIp)) {
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
					
					LOG("SwitchingOff");
					
					//std::cout << "Mode" << currentSwitchState << ": Swap off\n";
					TPLinkHelper tpLinkConn(_logsEnabled);
					if (tpLinkConn.LoadIPConfigFromFile(useUDP, workingIp)) {
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
			Sleep(100);
		}
		LOG_ERR("Exiting");
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

		//	//LOG((std::string(" old useUdp :") + std::to_string(useUDP)).c_str());

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
			//LOG("Error: could not create ini file.");
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
			//LOG("looping config write");
		}

		//LOG("looping config write done");
		
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
		
		//LOG("write settings done");
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

			//LOG("loaded intermittent strengths");

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


			LOG("corrected intermittent strengths");

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

			//LOG("loaded intermittent mode");
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

			//LOG("loaded simple mode");
			ImmersiveWinds::_levelValues[0] = ImmersiveWinds::LevelValues(0, 999999, 0, 0);
			ImmersiveWinds::_levelValues[1] = ImmersiveWinds::LevelValues(999999, 0, 0, 0);

		}
		//LOG("load settings done");
	}

	void AsyncExcecSwitchState(long state)
	{
		LOG("\nSetSwitchState called %ld", state);
			
		TPLinkHelper tpLinkConn(_logsEnabled);

		// load the ip for the connection
		tpLinkConn.LoadIPConfigFromFile(useUDP,workingIp);
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

	float GetSunGlare(TESWeather* thisWeather)
	{
		return (thisWeather) ? (thisWeather->general.sunGlare) : 0.0;
	}

	void UpdateNativeWindStateInternal()
	{
		int resultingLevel = 0;
		int mod = 0;
		if (_internalCell) 
		{
			resultingLevel = iInsideHouseBase;

			if (_currentLocation)
			{
				if (_currentLocation->keyword.HasKeyword(keywordLocTypeDwelling) // dwelling
					|| _currentLocation->keyword.HasKeyword(keywordLocTypeFarm) // farm
					|| _currentLocation->keyword.HasKeyword(keywordLocTypeGuild) // guild
					|| _currentLocation->keyword.HasKeyword(keywordLocTypeInn) // inn (Gasthaus)
					|| _currentLocation->keyword.HasKeyword(keywordLocTypeJail) // jail
					|| _currentLocation->keyword.HasKeyword(keywordLocTypeLumberMill) // lumber mill
					|| _currentLocation->keyword.HasKeyword(keywordLocTypeStewardsDwelling) // steward dwelling
					|| _currentLocation->keyword.HasKeyword(keywordLocTypeStore) // store
					|| _currentLocation->keyword.HasKeyword(keywordLocTypePlayerHouse) // player house
					|| _currentLocation->keyword.HasKeyword(keywordLocTypeHouse) // house
					|| _currentLocation->keyword.HasKeyword(keywordLocTypeCastle) // castle
					) {
					resultingLevel = iInsideHouseBase;
				}
				else {
					// dungeon base is the default if house was not detected.
					resultingLevel = iInsideDungeonBase;

					//if (_currentLocation->keyword.HasKeyword(keywordLocSetNordicRuin) // nordic ruin
					//	|| _currentLocation->keyword.HasKeyword(keywordLocSetDwarvenRuin) // dwarwen ruin
					//	|| _currentLocation->keyword.HasKeyword(keywordLocTypeDungeon) // dungeon
					//	|| _currentLocation->keyword.HasKeyword(keywordLocTypeDragonPriestLair) // dragon priest lair
					//	|| _currentLocation->keyword.HasKeyword(keywordLocTypeDragonLair) // dragon lair
					//	|| _currentLocation->keyword.HasKeyword(keywordLocTypeDraugrCrypt) // draugr crypt
					//	|| _currentLocation->keyword.HasKeyword(keywordLocTypeFalmerHive) // falmer hive
					//	|| _currentLocation->keyword.HasKeyword(keywordLocTypeMine) // mine
					//	|| _currentLocation->keyword.HasKeyword(keywordLocTypeTemple) // temple
					//	) {
					//	if (resultingLevel < iInsideDungeonBase) {
					//		resultingLevel = iInsideDungeonBase;
					//	}
					//}
					if (_currentLocation->keyword.HasKeyword(keywordLocSetCave)) { // cave
						if (resultingLevel < iInsideCaveBase) {
							resultingLevel = iInsideCaveBase;
						}
					}
					if (_currentLocation->keyword.HasKeyword(keywordLocSetCaveIce)) { // ice cave
						if (resultingLevel < iInsideIceCaveBase) {
							resultingLevel = iInsideIceCaveBase;
						}
					}

					if (_currentLocation->keyword.HasKeyword(keywordLocSetOutdoor)) { // outside actually; shall other mods be applied as well?
						if (resultingLevel < iOutsideBase) {
							resultingLevel = iOutsideBase;
						}
					}
					// ...
				}
			}			
		}
		else {
			resultingLevel = iOutsideBase;

			// altitude mode
			/*if (_altitude > iVeryHighAltitudeBegin) {
				mod += iOutsideAltitudeVeryHighMod;
				LOG("very high altitude!");
			}
			else if (_altitude > iHighAltitudeBegin) {
				mod += iOutsideAltitudeHighMod;
				LOG("high altitude!");
			}*/

			// daytime mod
			
			if (_inGameTime > 7 && _inGameTime < 19) { // check if daytime
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
				|| weatherIdMasked == 0x00E001 || weatherIdMasked == 0x00E002 || weatherIdMasked == 0x00E003 // Mythical Ages
				|| weatherIdMasked == 0x12DDEE || weatherIdMasked == 0x12DDED
				|| weatherIdMasked == 0x12DDEC || weatherIdMasked == 0x16B287)
			{
				// TODO add more id's for support for other mods.
				//LOG("StormWeather detected!");

				mod += iOutsideWeatherStormMod;
			}
			else {
				if (_weatherClass == 0) { // pleasant
					mod += iOutsideWeatherPleasantMod;
				}
				else if (_weatherClass == 1) { // cloudy
					if (_sunglare > 0.5) {
						//LOG("cloudy but sunglare is over 0.5 -> good weather");
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

		LOG("Resulted in: %d", result);		
	}

	void UpdateNativeWindState(bool bool_internalCell, float altitude, long weatherClass, BGSLocation* curLocation, float inGameTime, float sunglare, long locationId, long weatherId, float windSpeed)
	{
		LOG("\nUpdateNativeWindState called %d; %g; %ld; %g; %g; %ld; %ld", (bool_internalCell ? 1 : 0), altitude, weatherClass, inGameTime, sunglare, locationId, weatherId);		

		_internalCell = bool_internalCell;
		_altitude = altitude;
		_weatherClass = weatherClass;
		_currentLocation = curLocation;
		_inGameTime = inGameTime;
		_sunglare = sunglare;
		_locationId = locationId;
		_weatherId = weatherId;
		_windSpeed = windSpeed;

		UpdateNativeWindStateInternal();
	}


	void TriggerShout(long level)
	{
		LOG("Triggered shout with level: %ld", level);
		
		_shoutLevel = level;
		UpdateNativeWindStateInternal();
	}

	void EndShout()
	{		
		LOG("End shout");
		
		_shoutLevel = 0;
		UpdateNativeWindStateInternal();
	}

	long CheckShoutEffects()
	{
		if (*g_thePlayer)
		{
			MagicTarget::GetEffectCount counter;
			(*g_thePlayer)->magicTarget.ForEachActiveEffect(counter);
			for (UInt32 i = 0; i < counter.GetCount(); i++)
			{
				MagicTarget::GetNthEffect nthEffect(i);
				(*g_thePlayer)->magicTarget.ForEachActiveEffect(nthEffect);
				ActiveEffect* ae = nthEffect.GetResult();
				if (ae)
				{
					if (ae->effect)
					{
						if (ae->effect->mgef)
						{
							if (ae->effect->mgef->formID == ImmersiveWindsCallEffect1FormId)
							{								
								return 1;
							}
							else if(ae->effect->mgef->formID == ImmersiveWindsCallEffect2FormId)
							{
								return 2;
							}
						}
					}
				}

			}
		}
		return 0;
	}


	void InitBgThread() {
		LOG("InitBgThread");

		// int the settings first - very important
		WriteDefaultsToIniFile();
		LoadSettings();

		//LOG("Load settings");
		// start scan thread
		std::thread t(ScanAndCheckConnection);
		t.detach();

		/*std::thread t3(StartConnection);
		t3.detach();*/
		

		//LOG("Load settings");
		// start thread for plug control (delayed)
		std::thread t2(SwitchStateControllThread);
		t2.detach();
	}

	bool PlayerKnows(TESForm* form)
	{
		return PlayerKnowsNative((*g_skyrimVM)->GetClassRegistry(), 0, form);
	}

	UInt32 GetFullFormID(const ModInfo * modInfo, UInt32 formLower)
	{
		return (modInfo->modIndex << 24) | formLower;
	}

	// get mod index from a normal form ID 32 bit unsigned
	static inline UInt32 GetModIndex(UInt32 formId)
	{
		return formId >> 24;
	}

	// get base formID (without mod index)
	static inline UInt32 GetBaseFormID(UInt32 formId)
	{
		return formId & 0x00FFFFFF;
	}

	// check if mod index is valid (mod index is the upper 8 bits of form ID)
	static inline bool IsValidModIndex(UInt32 modIndex)
	{
		return modIndex > 0 && modIndex != 0xFF;
	}

	bool FillFormIds()
	{
		DataHandler * dataHandler = DataHandler::GetSingleton();

		TESForm* keywordForm = LookupFormByID(keywordIdLocSetCave); if (keywordForm) keywordLocSetCave = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocSetCaveIce); if (keywordForm) keywordLocSetCaveIce = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocSetDwarvenRuin); if (keywordForm) keywordLocSetDwarvenRuin = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocSetNordicRuin); if (keywordForm) keywordLocSetNordicRuin = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocSetOutdoor); if (keywordForm) keywordLocSetOutdoor = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocTypeDungeon); if (keywordForm) keywordLocTypeDungeon = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocTypeCity); if (keywordForm) keywordLocTypeCity = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocTypeDragonPriestLair); if (keywordForm) keywordLocTypeDragonPriestLair = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocTypeDragonLair); if (keywordForm) keywordLocTypeDragonLair = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocTypeDraugrCrypt); if (keywordForm) keywordLocTypeDraugrCrypt = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocTypeFalmerHive); if (keywordForm) keywordLocTypeFalmerHive = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocTypeMine); if (keywordForm) keywordLocTypeMine = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocTypeTemple); if (keywordForm) keywordLocTypeTemple = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocTypeDwelling); if (keywordForm) keywordLocTypeDwelling = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocTypeFarm); if (keywordForm) keywordLocTypeFarm = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocTypeGuild); if (keywordForm) keywordLocTypeGuild = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocTypeHold); if (keywordForm) keywordLocTypeHold = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocTypeHoldCapital); if (keywordForm) keywordLocTypeHoldCapital = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocTypeHoldMajor); if (keywordForm) keywordLocTypeHoldMajor = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocTypeHoldMinor); if (keywordForm) keywordLocTypeHoldMinor = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocTypeInn); if (keywordForm) keywordLocTypeInn = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocTypeJail); if (keywordForm) keywordLocTypeJail = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocTypeLumberMill); if (keywordForm) keywordLocTypeLumberMill = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocTypeShip); if (keywordForm) keywordLocTypeShip = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocTypeShipwreck); if (keywordForm) keywordLocTypeShipwreck = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocTypeStewardsDwelling); if (keywordForm) keywordLocTypeStewardsDwelling = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocTypeStore); if (keywordForm) keywordLocTypeStore = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocTypePlayerHouse); if (keywordForm) keywordLocTypePlayerHouse = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocTypeHouse); if (keywordForm) keywordLocTypeHouse = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);
		keywordForm = LookupFormByID(keywordIdLocTypeCastle); if (keywordForm) keywordLocTypeCastle = DYNAMIC_CAST(keywordForm, TESForm, BGSKeyword);

		const ModInfo * modInfo = dataHandler->LookupModByName(pluginName.c_str());

		if (modInfo)
		{
			LOG("Plugin is loaded. Trying to find forms.");

			if (IsValidModIndex(modInfo->modIndex)) //If plugin is in the load order.
			{
				ImmersiveWindsCall1FormId = GetFullFormID(modInfo, GetBaseFormID(ImmersiveWindsCall1FormId));
				ImmersiveWindsCall2FormId = GetFullFormID(modInfo, GetBaseFormID(ImmersiveWindsCall2FormId));
				ImmersiveWindsCallEffect1FormId = GetFullFormID(modInfo, GetBaseFormID(ImmersiveWindsCallEffect1FormId));
				ImmersiveWindsCallEffect2FormId = GetFullFormID(modInfo, GetBaseFormID(ImmersiveWindsCallEffect2FormId));
				ImmersiveWindsCallShoutFormId = GetFullFormID(modInfo, GetBaseFormID(ImmersiveWindsCallShoutFormId));

				if(ImmersiveWindsCallShoutFormId > 0)
				{
					TESForm* form = LookupFormByID(ImmersiveWindsCallShoutFormId);
					if(form)
					{
						ImmersiveWindsCallShout = DYNAMIC_CAST(form, TESForm, TESShout);
					}
				}
				
				WordRazenFormId = GetFullFormID(modInfo, GetBaseFormID(WordRazenFormId));
				if (WordRazenFormId > 0)
				{
					TESForm* form = LookupFormByID(WordRazenFormId);
					if (form)
					{
						WordRazen = DYNAMIC_CAST(form, TESForm, TESWordOfPower);
					}
				}
				
				WordGanFormId = GetFullFormID(modInfo, GetBaseFormID(WordGanFormId));
				if (WordGanFormId > 0)
				{
					TESForm* form = LookupFormByID(WordGanFormId);
					if (form)
					{
						WordGan = DYNAMIC_CAST(form, TESForm, TESWordOfPower);
					}
				}			

				return true;
			}
		}
		return false;
	}

	int GetClassification(TESWeather * weather)
	{
		const auto flags = *((byte *)weather + 0x66F);

		if (flags & 1)
			return 0;
		if (flags & 2)
			return 1; //Cloudy
		if (flags & 4)
			return 2; //Rainy
		if (flags & 8)
			return 3; //Snowy

		return -1;
	}

	void UnlockWordOfPower(TESWordOfPower * wop)
	{
		UnlockWordNative((*g_skyrimVM)->GetClassRegistry(), 0, NULL, wop);
	}

	void TeachWordOfPower(TESWordOfPower * wop)
	{
		TeachWordNative((*g_skyrimVM)->GetClassRegistry(), 0, NULL, wop);
	}

	void WeatherCheck()
	{
		TESObjectCELL* cell = nullptr;
				
		int classification = 0;

		bool isInterior = false;

		while (true)
		{
			if (!(*g_thePlayer) || !(*g_thePlayer)->loadedState)
			{
				//LOG_INFO("player null. Waiting for 5seconds");
				Sleep(5000);
				continue;
			}

			if (isGameStoppedNoDialogue())
			{
				_currentSwitchState = 0; //No fan in menu.
				Sleep(3000);
				continue;
			}

			cell = (*g_thePlayer)->parentCell;

			if (!cell)
			{
				continue;
			}

			if (WordRazen && WordGan)
			{
				if (!teach.load())
				{
					if (!PlayerKnows(LookupFormByID(WordRazen->formID)))
					{
						LOG("Player doesn't know the word.");
						if (ImmersiveWindsCallShout)
						{
							ActorAddShoutNative((*g_thePlayer), ImmersiveWindsCallShout);
							TeachWordOfPower(WordRazen);
							TeachWordOfPower(WordGan);
							UnlockWordOfPower(WordRazen);
							UnlockWordOfPower(WordGan);
							LOG("Words learned and unlocked.");
						}
					}
					else
					{
						LOG("Player knows the word.");
					}
					teach.store(true);
				}
				const long shoutLevel = CheckShoutEffects();
				if(_shoutLevel != shoutLevel)
				{
					if(shoutLevel > 0)
					{
						TriggerShout(shoutLevel);
					}
					else
					{
						EndShout();
					}
				}
			}

			if (!(cell->unk120) || (std::find(notExteriorWorlds.begin(), notExteriorWorlds.end(), cell->unk120->formID) != notExteriorWorlds.end())) //Interior Cell
			{
				isInterior = true;
				interiorCell.store(true);
			}
			else
			{
				isInterior = false;
				interiorCell.store(false);
			}
			
			const auto skyPtr = *g_SkyPtr;
			if (skyPtr != nullptr && skyPtr->currentWeather != nullptr)
			{				
				UpdateNativeWindState(isInterior, (*g_thePlayer)->pos.z, GetClassification(skyPtr->currentWeather), (*g_thePlayer)->currentLocation, skyPtr->timeOfDay, GetSunGlare(skyPtr->currentWeather), cell->formID, skyPtr->currentWeather->formID, GetWindSpeed(skyPtr->currentWeather));

				Sleep(3000);
			}
			else
			{
				//LOG_INFO("Sky is null. waiting for 5 seconds.");
				Sleep(5000);
			}
		}
	}

	void ResetTeach()
	{
		teach.store(false);
	}

	//This function is used to initialize the mod.
	void StartMod()
	{
		SafeWriteJump(addressStart.GetUIntPtr(), addressEnd.GetUIntPtr());

		MenuManager * menuManager = MenuManager::GetSingleton();
		if (menuManager)
			menuManager->MenuOpenCloseEventDispatcher()->AddEventSink(&ImmersiveWinds::menuEvent);

		teach.store(false);
		FillFormIds();
		std::thread t7(WeatherCheck);
		t7.detach();
		InitBgThread();
	}

	void Log(const int msgLogLevel, const char * fmt, ...)
	{
		if (msgLogLevel > 0 && !_logsEnabled)
		{
			return;
		}

		va_list args;
		char logBuffer[4096];

		va_start(args, fmt);
		vsprintf_s(logBuffer, sizeof(logBuffer), fmt, args);
		va_end(args);

		_MESSAGE(logBuffer);
	}	
}
