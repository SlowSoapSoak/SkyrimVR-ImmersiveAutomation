#pragma once
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include "DeviceInfo.h"


#define DEFAULT_BUFLEN 4096

class TPLinkHelper
{
private:
	SOCKET ConnectSocket = INVALID_SOCKET;

	std::string m_sIp;
	const char* m_ip;
	int m_port;
	bool m_udp;
	bool m_loggingEnabled;

public:

	TPLinkHelper(bool enableLogging);

	bool ConnectToHost(int PortNo, const char * IPAddress);

	bool OpenConnection();
	void CloseConnection();

	bool LoadIPConfigFromFile();


	void SwitchRelayState(long on);

	void SwitchLEDState(long on);

	DeviceInfo GetSystemInfo();


	static std::vector<char> EncryptMessage(std::vector<char> pMessage, int pProtocolType);

	static std::vector<char> DecryptMessage(std::vector<char> pMessage, int pProtocolType);

private:
	int SendAndReceiveEncoded(std::string messageSend, std::string &messageAnswer);

	int SendAndReceive(const char *sendbuf, int sendLen, char* recvbufR, int &recvbuflen);

	int ReceiveOnce(int s, char *buf);
	int ReceiveAll(int s, char *buf, int *len);

};