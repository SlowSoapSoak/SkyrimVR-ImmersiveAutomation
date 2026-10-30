#include "ImmersiveWinds.h"
#include <mutex>


namespace ImmersiveWinds {

	std::atomic<bool> _keepRunning(true);
	std::atomic<long> _currentSwitchState(0);

	// workingIp and useUDP are written by the scan thread and read by the switch threads
	static std::mutex _connectionConfigMutex;

	static const int SCAN_MAX_ATTEMPTS = 5;
	static const int SCAN_RETRY_DELAY_MS = 10000;
	static const int WEATHER_CHECK_INTERVAL_MS = 3000;
	// the relay state is sent again after this time, in case a (udp) message got lost or the plug restarted
	static const float RELAY_RESEND_INTERVAL_SECONDS = 15.0f;

	static std::atomic<bool> _weatherTaskPending(false);
	static std::atomic<bool> _scanFinished(false);
	static std::chrono::steady_clock::time_point _lastRelaySend;

	float SecondsSince(const std::chrono::steady_clock::time_point & start)
	{
		return std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
	}

	bool LoadConnectionConfig(TPLinkHelper & tpLinkConn)
	{
		std::lock_guard<std::mutex> lock(_connectionConfigMutex);
		return tpLinkConn.LoadIPConfigFromFile(useUDP, workingIp);
	}

	void SetWorkingIp(const std::string & ip)
	{
		std::lock_guard<std::mutex> lock(_connectionConfigMutex);
		workingIp = ip;
	}

	// My Games\<Skyrim Special Edition|Skyrim VR|...>\SKSE\immersiveWinds.ini, matching the running game
	std::string GetIniPath()
	{
		const auto dir = SKSE::log::log_directory();
		return dir ? (*dir / "immersiveWinds.ini").string() : "immersiveWinds.ini";
	}

	void ScanAndCheckConnection()
	{
		const std::string plugName = "ImmersiveWindsPlug";
		const std::string sExecPath = ImmersiveWinds::GetIniPath();

		char iniFileBuffer[100];
		GetPrivateProfileStringA("Internal", "sTpLinkPlugIp", "",
			iniFileBuffer, 100, sExecPath.c_str());
		const std::string savedIp(iniFileBuffer);
		LOG("READ ini file: %s", savedIp.c_str());

		if (savedIp.empty()) {
			LOG("no ip specified");
		}
		else {
			TPLinkHelper tpLinkConn(_logsEnabled);
			if (!tpLinkConn.ConnectToHost(9999, savedIp.c_str())) {
				LOG("no connection");
			}
			else {
				LOG("Connect Succeeded");
				// check if correct device:
				if (LoadConnectionConfig(tpLinkConn) && tpLinkConn.GetSystemInfo().alias() == plugName) {
					return;
				}
				LOG("wrong device");
			}
		}

		// search for the device until one is found and save its ip to the ini file.
		// A failed scan keeps the saved ip, so a plug that is only temporarily offline is not forgotten.
		for (int attempt = 1; attempt <= SCAN_MAX_ATTEMPTS && _keepRunning; attempt++) {
			SPScanner scanner;
			const std::string ipOfDevice = scanner.ScanForTpPlug(plugName);
			LOG("scan %d returned: %s", attempt, ipOfDevice.c_str());
			if (!ipOfDevice.empty()) {
				WritePrivateProfileStringA("Internal", "sTpLinkPlugIp", ipOfDevice.c_str(), sExecPath.c_str());
				SetWorkingIp(ipOfDevice);
				return;
			}
			if (attempt < SCAN_MAX_ATTEMPTS) {
				Sleep(SCAN_RETRY_DELAY_MS);
			}
		}
		LOG_ERR("Could not find a plug named %s", plugName.c_str());
	}

	void ScanThread()
	{
		ScanAndCheckConnection();
		_scanFinished = true;
	}

	void SendRelayState(long on)
	{
		TPLinkHelper tpLinkConn(_logsEnabled);
		if (LoadConnectionConfig(tpLinkConn)) {
			tpLinkConn.SwitchRelayState(on);
		}
		_lastRelaySend = std::chrono::steady_clock::now();
	}

	void SwitchStateControllThread() {
		// wait until the plug was found, otherwise the first switch commands would get lost
		while (!_scanFinished && _keepRunning) {
			Sleep(100);
		}

		while (_keepRunning) 
		{
			// read the state once, it is changed by the weather thread
			const long currentState = _currentSwitchState;
			LevelValues & level = _levelValues[currentState];

			if (_previousSwitchState != currentState) {
				// state change detected!
				// reset all state variables:
				//std::cout << "var reset\n";
				LOG("\nSwitching mode to %ld", currentState);
				
				_s1Swap = true;
				level.counterLeaveCurrentState = 0;
				_previousSwitchState = currentState;
				_beginTime = std::chrono::steady_clock::now();
			}

			//if (_shoutLevel > 0) {
			//	if (float(clock() - _shoutBeginTime) > 30) {
			//		// after 30 seconds the wind gets back to normal.
			//		_shoutLevel = 0;
			//		UpdateNativeWindStateInternal();
			//	}
			//}

			// handle states:
			if (_s1Swap && level.counterLeaveOnMax != 0) {
				if (level.counterLeaveCurrentState == 0) {
					//std::cout << "Mode" << currentSwitchState << ": Swap on\n";
					if (level.randomOnAdd > 0) {
						level.currentRandomOn = level.distributionOn(_generator);
					}
					
					LOG("SwitchingOn");
					
					SendRelayState(1);
				}
				else if (SecondsSince(_lastRelaySend) >= RELAY_RESEND_INTERVAL_SECONDS) {
					SendRelayState(1);
				}
				level.counterLeaveCurrentState++;
				if (SecondsSince(_beginTime) >= level.counterLeaveOnMax + level.currentRandomOn) {
					level.counterLeaveCurrentState = 0;
					_beginTime = std::chrono::steady_clock::now();
					_s1Swap = false;
				}
			}
			else if (level.counterLeaveOffMax != 0) {
				if (level.counterLeaveCurrentState == 0) {
					if (level.randomOffAdd > 0) {
						level.currentRandomOff = level.distributionOff(_generator);
					}
					
					LOG("SwitchingOff");
					
					//std::cout << "Mode" << currentSwitchState << ": Swap off\n";
					SendRelayState(0);
				}
				else if (SecondsSince(_lastRelaySend) >= RELAY_RESEND_INTERVAL_SECONDS) {
					SendRelayState(0);
				}
				level.counterLeaveCurrentState++;
				if (SecondsSince(_beginTime) >= level.counterLeaveOffMax + level.currentRandomOff) {
					level.counterLeaveCurrentState = 0;
					_beginTime = std::chrono::steady_clock::now();
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
		if (iniFileContents.find("bNoWindInMenu") == std::string::npos) {
			WritePrivateProfileStringA("GeneralModConfig", "bNoWindInMenu", std::to_string(0).c_str(),
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
		unsigned int bNoWindInMenu = GetPrivateProfileIntA("GeneralModConfig", "bNoWindInMenu", 0, sExecPath.c_str());
		_noWindInMenu = bNoWindInMenu > 0;

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
		LoadConnectionConfig(tpLinkConn);
		if (state >= 1) {
			tpLinkConn.SwitchRelayState(1);
		}
		else {
			tpLinkConn.SwitchRelayState(0);
		}
	}

	float GetWindSpeed(TESWeather* thisWeather)
	{
		return (thisWeather) ? (thisWeather->data.windSpeed / 256.0f) : 0.0f;
	}

	float GetSunGlare(TESWeather* thisWeather)
	{
		return (thisWeather) ? static_cast<float>(thisWeather->data.sunGlare) : 0.0f;
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
				if (_currentLocation->HasKeyword(keywordLocTypeDwelling) // dwelling
					|| _currentLocation->HasKeyword(keywordLocTypeFarm) // farm
					|| _currentLocation->HasKeyword(keywordLocTypeGuild) // guild
					|| _currentLocation->HasKeyword(keywordLocTypeInn) // inn (Gasthaus)
					|| _currentLocation->HasKeyword(keywordLocTypeJail) // jail
					|| _currentLocation->HasKeyword(keywordLocTypeLumberMill) // lumber mill
					|| _currentLocation->HasKeyword(keywordLocTypeStewardsDwelling) // steward dwelling
					|| _currentLocation->HasKeyword(keywordLocTypeStore) // store
					|| _currentLocation->HasKeyword(keywordLocTypePlayerHouse) // player house
					|| _currentLocation->HasKeyword(keywordLocTypeHouse) // house
					|| _currentLocation->HasKeyword(keywordLocTypeCastle) // castle
					) {
					resultingLevel = iInsideHouseBase;
				}
				else {
					// dungeon base is the default if house was not detected.
					resultingLevel = iInsideDungeonBase;

					//if (_currentLocation->HasKeyword(keywordLocSetNordicRuin) // nordic ruin
					//	|| _currentLocation->HasKeyword(keywordLocSetDwarvenRuin) // dwarwen ruin
					//	|| _currentLocation->HasKeyword(keywordLocTypeDungeon) // dungeon
					//	|| _currentLocation->HasKeyword(keywordLocTypeDragonPriestLair) // dragon priest lair
					//	|| _currentLocation->HasKeyword(keywordLocTypeDragonLair) // dragon lair
					//	|| _currentLocation->HasKeyword(keywordLocTypeDraugrCrypt) // draugr crypt
					//	|| _currentLocation->HasKeyword(keywordLocTypeFalmerHive) // falmer hive
					//	|| _currentLocation->HasKeyword(keywordLocTypeMine) // mine
					//	|| _currentLocation->HasKeyword(keywordLocTypeTemple) // temple
					//	) {
					//	if (resultingLevel < iInsideDungeonBase) {
					//		resultingLevel = iInsideDungeonBase;
					//	}
					//}
					if (_currentLocation->HasKeyword(keywordLocSetCave)) { // cave
						if (resultingLevel < iInsideCaveBase) {
							resultingLevel = iInsideCaveBase;
						}
					}
					if (_currentLocation->HasKeyword(keywordLocSetCaveIce)) { // ice cave
						if (resultingLevel < iInsideIceCaveBase) {
							resultingLevel = iInsideIceCaveBase;
						}
					}

					if (_currentLocation->HasKeyword(keywordLocSetOutdoor)) { // outside actually; shall other mods be applied as well?
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
			if (_altitude > iVeryHighAltitudeBegin) {
				mod += iOutsideAltitudeVeryHighMod;
				LOG("very high altitude!");
			}
			else if (_altitude > iHighAltitudeBegin) {
				mod += iOutsideAltitudeHighMod;
				LOG("high altitude!");
			}

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
		auto player = RE::PlayerCharacter::GetSingleton();
		auto effects = player ? player->AsMagicTarget()->GetActiveEffectList() : nullptr;
		if (!effects)
			return 0;

		for (const auto ae : *effects)
		{
			const auto mgef = ae ? ae->GetBaseObject() : nullptr;
			if (!mgef)
				continue;
			if (mgef->GetFormID() == ImmersiveWindsCallEffect1FormId)
				return 1;
			if (mgef->GetFormID() == ImmersiveWindsCallEffect2FormId)
				return 2;
		}
		return 0;
	}


	void InitBgThread() {
		LOG("InitBgThread");

		// int the settings first - very important
		WriteDefaultsToIniFile();
		LoadSettings();
		_generator.seed(std::random_device()());

		//LOG("Load settings");
		// start scan thread
		std::thread t(ScanThread);
		t.detach();

		/*std::thread t3(StartConnection);
		t3.detach();*/
		

		//LOG("Load settings");
		// start thread for plug control (delayed)
		std::thread t2(SwitchStateControllThread);
		t2.detach();

		// start thread that checks the game state
		std::thread t3(WeatherCheck);
		t3.detach();
	}

	bool FillFormIds()
	{
		keywordLocSetCave = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocSetCave);
		keywordLocSetCaveIce = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocSetCaveIce);
		keywordLocSetDwarvenRuin = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocSetDwarvenRuin);
		keywordLocSetNordicRuin = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocSetNordicRuin);
		keywordLocSetOutdoor = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocSetOutdoor);
		keywordLocTypeDungeon = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocTypeDungeon);
		keywordLocTypeCity = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocTypeCity);
		keywordLocTypeDragonPriestLair = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocTypeDragonPriestLair);
		keywordLocTypeDragonLair = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocTypeDragonLair);
		keywordLocTypeDraugrCrypt = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocTypeDraugrCrypt);
		keywordLocTypeFalmerHive = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocTypeFalmerHive);
		keywordLocTypeMine = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocTypeMine);
		keywordLocTypeTemple = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocTypeTemple);
		keywordLocTypeDwelling = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocTypeDwelling);
		keywordLocTypeFarm = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocTypeFarm);
		keywordLocTypeGuild = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocTypeGuild);
		keywordLocTypeHold = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocTypeHold);
		keywordLocTypeHoldCapital = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocTypeHoldCapital);
		keywordLocTypeHoldMajor = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocTypeHoldMajor);
		keywordLocTypeHoldMinor = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocTypeHoldMinor);
		keywordLocTypeInn = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocTypeInn);
		keywordLocTypeJail = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocTypeJail);
		keywordLocTypeLumberMill = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocTypeLumberMill);
		keywordLocTypeShip = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocTypeShip);
		keywordLocTypeShipwreck = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocTypeShipwreck);
		keywordLocTypeStewardsDwelling = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocTypeStewardsDwelling);
		keywordLocTypeStore = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocTypeStore);
		keywordLocTypePlayerHouse = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocTypePlayerHouse);
		keywordLocTypeHouse = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocTypeHouse);
		keywordLocTypeCastle = RE::TESForm::LookupByID<BGSKeyword>(keywordIdLocTypeCastle);

		auto dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler || !dataHandler->LookupModByName(pluginName))
			return false;

		LOG("Plugin is loaded. Trying to find forms.");

		// LookupFormID resolves the load order index (regular and light plugins)
		ImmersiveWindsCall1FormId = dataHandler->LookupFormID(ImmersiveWindsCall1FormId, pluginName);
		ImmersiveWindsCall2FormId = dataHandler->LookupFormID(ImmersiveWindsCall2FormId, pluginName);
		ImmersiveWindsCallEffect1FormId = dataHandler->LookupFormID(ImmersiveWindsCallEffect1FormId, pluginName);
		ImmersiveWindsCallEffect2FormId = dataHandler->LookupFormID(ImmersiveWindsCallEffect2FormId, pluginName);
		ImmersiveWindsCallShout = dataHandler->LookupForm<TESShout>(ImmersiveWindsCallShoutFormId, pluginName);
		WordRazen = dataHandler->LookupForm<TESWordOfPower>(WordRazenFormId, pluginName);
		WordGan = dataHandler->LookupForm<TESWordOfPower>(WordGanFormId, pluginName);
		return true;
	}

	int GetClassification(TESWeather * weather)
	{
		using Flag = TESWeather::WeatherDataFlag;
		const auto& flags = weather->data.flags;

		if (flags.any(Flag::kPleasant))
			return 0;
		if (flags.any(Flag::kCloudy))
			return 1; //Cloudy
		if (flags.any(Flag::kRainy))
			return 2; //Rainy
		if (flags.any(Flag::kSnow))
			return 3; //Snowy

		return -1;
	}

	// Calls a vanilla Papyrus global (Game.TeachWord / Game.UnlockWord), same behaviour on SE, AE and VR
	void CallGameFunction(const char * function, TESWordOfPower * wop)
	{
		auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!vm)
			return;
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
		vm->DispatchStaticCall("Game", function, RE::MakeFunctionArguments(std::move(wop)), callback);
	}

	// Reads the game state (player, cell, weather, shout effects) and updates the wind level.
	// Calls into the engine, so it must run on the game's main thread (see WeatherCheck).
	void UpdateFromGameState()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->Is3DLoaded())
		{
			return;
		}

		if (menuStopsGameNoDialogue && _noWindInMenu)
		{
			_currentSwitchState = 0; //No fan in menu.
			return;
		}

		const auto cell = player->GetParentCell();

		if (!cell)
		{
			return;
		}

		if (WordRazen && WordGan)
		{
			if (!teach.load())
			{
				if (!WordRazen->GetKnown())
				{
					LOG("Player doesn't know the word.");
					if (ImmersiveWindsCallShout)
					{
						player->AddShout(ImmersiveWindsCallShout);
						CallGameFunction("TeachWord", WordRazen);
						CallGameFunction("TeachWord", WordGan);
						CallGameFunction("UnlockWord", WordRazen);
						CallGameFunction("UnlockWord", WordGan);
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

		const auto worldSpace = cell->GetRuntimeData().worldSpace;
		const bool isInterior = !worldSpace || (std::find(notExteriorWorlds.begin(), notExteriorWorlds.end(), worldSpace->GetFormID()) != notExteriorWorlds.end()); //Interior Cell
		interiorCell.store(isInterior);

		const auto skyPtr = RE::Sky::GetSingleton();
		if (skyPtr != nullptr && skyPtr->currentWeather != nullptr)
		{
			UpdateNativeWindState(isInterior, player->GetPosition().z, GetClassification(skyPtr->currentWeather), player->GetCurrentLocation(), skyPtr->currentGameHour, GetSunGlare(skyPtr->currentWeather), cell->GetFormID(), skyPtr->currentWeather->GetFormID(), GetWindSpeed(skyPtr->currentWeather));
		}
	}

	// Background timer: queues UpdateFromGameState on the main thread every few seconds.
	void WeatherCheck()
	{
		while (_keepRunning)
		{
			if (menuStopsGameNoDialogue && _noWindInMenu)
			{
				// also handled here, so the fan stops even if the game does not process tasks while the menu is open
				_currentSwitchState = 0; //No fan in menu.
			}
			// only one check at a time, so tasks do not pile up while the game does not process them (e.g. loading screens)
			else if (!_weatherTaskPending.exchange(true))
			{
				SKSE::GetTaskInterface()->AddTask([]() {
					UpdateFromGameState();
					_weatherTaskPending = false;
				});
			}
			Sleep(WEATHER_CHECK_INTERVAL_MS);
		}
	}

	void ResetTeach()
	{
		teach.store(false);
	}

	//This function is used to initialize the mod.
	void StartMod()
	{
		// ponytail: the VR build patched an unknown game function here (SafeWriteJump at VR 0x127B460+0x38D);
		// dropped in the port because its purpose and SE/AE address are unknown. Re-add via Address Library ID if needed.
		if (auto ui = RE::UI::GetSingleton())
			ui->AddEventSink<RE::MenuOpenCloseEvent>(&ImmersiveWinds::menuEvent);

		teach.store(false);
		FillFormIds();
		// starts all threads, after the settings are loaded
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

		_MESSAGE("%s", logBuffer);
	}	
}
