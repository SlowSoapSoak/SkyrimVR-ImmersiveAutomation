#include "SPScanner.h"
#include <string>
#include <stdio.h>
#include <iostream>
#include "TPLinkSPHelper.h"
#include "Enumerations.h"
#include "DeviceInfo.h"

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <stdio.h>
#include <winsock2.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>

#include <thread>
#include <future>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")


SPScanner::SPScanner()
{
}


SPScanner::~SPScanner()
{
}

//std::string listenForUDPAnswers(std::string plugName) {
//	
//	return "";
//}

std::string SPScanner::ScanForTpPlug(std::string plugName)
{
	std::string returnIP;

	WSAData d;
	if (WSAStartup(MAKEWORD(2, 2), &d) != 0) {
		return "";
	}

	std::string broadCastMsg = "{\"system\":{\"get_sysinfo\":{}}}";

	std::vector<char> vBroadCastMsg(broadCastMsg.begin(), broadCastMsg.end());
	vBroadCastMsg = TPLinkHelper::EncryptMessage(vBroadCastMsg, Enumerations::ProtocolType_UDP);
	std::string broadCastMsgEnc = std::string(vBroadCastMsg.begin(), vBroadCastMsg.end());

	std::vector<NetworkInterfaceInfo> interfaces;
	GetNetworkInterfaceInfos(interfaces);

	//std::future<std::string> ret = std::async(&listenForUDPAnswers, plugName);
	

	for (int k = 0; k < interfaces.size(); k++) {
		SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
		if (sock == INVALID_SOCKET)
		{
			return ""; // error
		}

		BOOL enabled = TRUE;
		if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char*)&enabled, sizeof(BOOL)) < 0)
		{
			closesocket(sock);
			return ""; // error
		}

		struct sockaddr_in Sender_addr;
		Sender_addr.sin_family = AF_INET;
		Sender_addr.sin_port = htons(9999);
		Sender_addr.sin_addr.s_addr = inet_addr(interfaces.at(k).Broadcast.c_str());

		struct sockaddr_in Recv_addr;
		Recv_addr.sin_family = AF_INET;
		Recv_addr.sin_port = htons(9999);
		Recv_addr.sin_addr.s_addr = INADDR_ANY;

		if (bind(sock, (sockaddr*)&Recv_addr, sizeof(Recv_addr)) < 0)
		{
			closesocket(sock);
			return ""; // error
		}

		int timeout = 3000;
		setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(int));
		if (sendto(sock, broadCastMsgEnc.c_str(), broadCastMsgEnc.length(), 0, (sockaddr *)&Sender_addr, sizeof(Sender_addr)) < 0)
		{
			closesocket(sock);
			return "";// error
		}

		// Receive data until timeout
		int iResult = 0;
		int total = 0;
		struct sockaddr_in srcaddr;
		do {
			char buf[4096];
			//recv(sock, buf, 4096, 0);
			socklen_t addrlen = sizeof(struct sockaddr_in);

			iResult = recvfrom(sock, buf, 4096, 0, (struct sockaddr *)&srcaddr, &addrlen);
			_MESSAGE("start receive");
			if (iResult > 0)
			{
				sockaddr addr;
				int len2 = sizeof(addr);
				getpeername(sock, &addr, &len2);

				total += iResult;
				printf("Bytes received: %d\n", iResult);
				_MESSAGE("Bytes received");
				std::string recvMsg = buf;
				std::vector<char> vBroadCastAnswer(recvMsg.begin(), recvMsg.end());
				vBroadCastAnswer = TPLinkHelper::DecryptMessage(vBroadCastAnswer, Enumerations::ProtocolType_UDP);
				recvMsg = std::string(vBroadCastAnswer.begin(), vBroadCastAnswer.end());
				recvMsg.resize(recvMsg.length());
				size_t endOfMsg = recvMsg.rfind('\0');
				//printf("endOfMsg: %ld\n", endOfMsg);
				if (endOfMsg != -1) {
					recvMsg.resize(endOfMsg);
				}
				std::cout << "rebound: " << recvMsg << "\n";
				if (recvMsg.find("\"get_sysinfo\":{}") != std::string::npos) {
					// ignore rebound
					//_MESSAGE("ignore rebound");
				}
				else if (recvMsg.find("{") != std::string::npos) {
					_MESSAGE("json message?");
					recvMsg.resize(550);
					DeviceInfo info;
					info.LoadFromJson(recvMsg);
					if (info.alias() == plugName)
					{
						returnIP = inet_ntoa(srcaddr.sin_addr);
						closesocket(sock);
						WSACleanup();
						return returnIP;
					}
				}
			}
			else if (iResult == 0)
				printf("Connection closed\n");
			else
				printf("recv failed: %d\n", WSAGetLastError());
		} while (iResult > 0);

		closesocket(sock);
	}
	//returnIP = ret.get();

	WSACleanup();
	return "";
}

int SPScanner::GetNetworkInterfaceInfos(std::vector<NetworkInterfaceInfo> & results)
{
	ULONG bufSz = 0;
	if (GetAdaptersInfo(NULL, &bufSz) == ERROR_BUFFER_OVERFLOW)
	{
		std::vector<BYTE> buf;
		buf.resize(bufSz, 0);
		if (GetAdaptersInfo((IP_ADAPTER_INFO*)&buf[0], &bufSz) == ERROR_SUCCESS)
		{
			IP_ADAPTER_INFO* pAdapterInfo = (IP_ADAPTER_INFO*)&buf[0];
			for (; pAdapterInfo != NULL; pAdapterInfo = pAdapterInfo->Next)
			{
				NetworkInterfaceInfo info;
				unsigned long ip = inet_addr(pAdapterInfo->IpAddressList.IpAddress.String);
				unsigned long mask = inet_addr(pAdapterInfo->IpAddressList.IpMask.String);
				unsigned long bcip = ip | ~mask;
				struct in_addr ia;
				ia.S_un.S_addr = bcip;
				info.Ip = pAdapterInfo->IpAddressList.IpAddress.String;
				info.NetMask = pAdapterInfo->IpAddressList.IpMask.String;
				info.Broadcast = inet_ntoa(ia);
				_MESSAGE("\nFound adapter:");
				_MESSAGE(info.Ip.c_str());
				_MESSAGE(info.NetMask.c_str());
				_MESSAGE(info.Broadcast.c_str());
				results.push_back(info);
			}
		}
	}
	return 0;
}
