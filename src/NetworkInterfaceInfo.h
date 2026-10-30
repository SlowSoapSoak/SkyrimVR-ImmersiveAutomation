#pragma once
#include <string>

class NetworkInterfaceInfo
{
public:
	NetworkInterfaceInfo();
	std::string Ip = "";
	std::string NetMask = "";
	std::string Broadcast = "";
};
