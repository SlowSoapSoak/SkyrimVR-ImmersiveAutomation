#include "PlugFinder.h"
#include "SPScanner.h"
#include "TPLinkSPHelper.h"

namespace
{
	const int SCAN_MAX_ATTEMPTS = 5;
	const int SCAN_RETRY_DELAY_MS = 10000;
}

std::string FindTpLinkPlug(const std::string & iniPath, const char * ipKey, const std::string & plugName, bool loggingEnabled, const std::atomic<bool> & keepRunning)
{
	char buffer[100];
	GetPrivateProfileStringA("Internal", ipKey, "", buffer, sizeof(buffer), iniPath.c_str());
	const std::string savedIp(buffer);
	if (loggingEnabled) {
		_MESSAGE("%s: saved ip '%s'", plugName.c_str(), savedIp.c_str());
	}

	if (!savedIp.empty()) {
		TPLinkHelper tpLinkConn(loggingEnabled);
		if (!tpLinkConn.ConnectToHost(9999, savedIp.c_str())) {
			_MESSAGE("%s: no connection to %s", plugName.c_str(), savedIp.c_str());
		}
		else {
			// check that it is the right device
			int useUdp = -1;
			std::string ip;
			if (tpLinkConn.LoadIPConfigFromFile(iniPath, ipKey, useUdp, ip) && tpLinkConn.GetSystemInfo().alias() == plugName) {
				return savedIp;
			}
			_MESSAGE("%s: %s is a different device", plugName.c_str(), savedIp.c_str());
		}
	}

	for (int attempt = 1; attempt <= SCAN_MAX_ATTEMPTS && keepRunning; attempt++) {
		SPScanner scanner;
		const std::string ipOfDevice = scanner.ScanForTpPlug(plugName);
		_MESSAGE("%s: scan %d returned '%s'", plugName.c_str(), attempt, ipOfDevice.c_str());
		if (!ipOfDevice.empty()) {
			WritePrivateProfileStringA("Internal", ipKey, ipOfDevice.c_str(), iniPath.c_str());
			return ipOfDevice;
		}
		if (attempt < SCAN_MAX_ATTEMPTS) {
			Sleep(SCAN_RETRY_DELAY_MS);
		}
	}
	_MESSAGE("Could not find a plug named %s", plugName.c_str());
	return "";
}
