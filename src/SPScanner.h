#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "NetworkInterfaceInfo.h"
class SPScanner
{
public:
	SPScanner();
	~SPScanner();
	std::string ScanForTpPlug(std::string plugName);
private:
	int GetNetworkInterfaceInfos(std::vector<NetworkInterfaceInfo> & results);
};
