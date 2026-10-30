#pragma once
#include <vector>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "DeviceInfo.h"


#define DEFAULT_BUFLEN 4096

// Starts Winsock once for the whole process. It stays loaded until the game exits,
// starting and stopping it for every message only added overhead.
bool EnsureWinsock();

class TPLinkHelper
{
private:
	SOCKET ConnectSocket = INVALID_SOCKET;

	std::string m_sIp;
	int m_port;
	bool m_udp;
	bool m_loggingEnabled;

public:

	TPLinkHelper(bool enableLogging);
	~TPLinkHelper();

	// owns a socket, copies would close it twice
	TPLinkHelper(const TPLinkHelper&) = delete;
	TPLinkHelper& operator=(const TPLinkHelper&) = delete;

	bool ConnectToHost(int PortNo, const char * IPAddress);

	bool OpenConnection();
	void CloseConnection();

	// Uses defaultIp and useUDP if both are set, otherwise reads them from iniPath ([Internal] ipKey and
	// [GeneralModConfig] bUseUdp) and returns them in defaultIp and useUDP. Returns false without an ip.
	bool LoadIPConfigFromFile(const std::string &iniPath, const char *ipKey, int &useUDP, std::string &defaultIp);


	bool SwitchRelayState(long on);

	void SwitchLEDState(long on);

	DeviceInfo GetSystemInfo();


	static std::vector<char> EncryptMessage(std::vector<char> pMessage, int pProtocolType);

	static std::vector<char> DecryptMessage(std::vector<char> pMessage, int pProtocolType);

private:
	int SendAndReceiveEncoded(const std::string &messageSend, std::string &messageAnswer);

	int SendAndReceive(const std::vector<char> &request, std::vector<char> &response);

	bool SendAll(const std::vector<char> &request);

};
