#include "SPScanner.h"
#include <string>
#include "TPLinkSPHelper.h"
#include "Enumerations.h"
#include "DeviceInfo.h"

#include <winsock2.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace
{
	// how long to wait for further answers on one network interface
	const DWORD SCAN_RECEIVE_TIMEOUT_MS = 3000;

	std::string ToString(const in_addr& address)
	{
		char text[INET_ADDRSTRLEN] = {};
		inet_ntop(AF_INET, &address, text, sizeof(text));
		return text;
	}

	// Broadcasts get_sysinfo on one interface and returns the ip of the plug named plugName, or "".
	std::string ScanInterface(const NetworkInterfaceInfo& networkInterface, const std::vector<char>& request, const std::string& plugName)
	{
		SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (sock == INVALID_SOCKET)
		{
			_MESSAGE("socket creation failed: %d", WSAGetLastError());
			return "";
		}

		BOOL enabled = TRUE;
		setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char*)&enabled, sizeof(enabled));
		DWORD timeout = SCAN_RECEIVE_TIMEOUT_MS;
		setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

		// bind to the address of this interface (any free port), so the broadcast leaves through this adapter.
		// Port 9999 is not needed for the answers and may be taken by another program.
		sockaddr_in localAddress{};
		localAddress.sin_family = AF_INET;
		localAddress.sin_port = 0;
		sockaddr_in broadcastAddress{};
		broadcastAddress.sin_family = AF_INET;
		broadcastAddress.sin_port = htons(9999);
		if (inet_pton(AF_INET, networkInterface.Ip.c_str(), &localAddress.sin_addr) != 1
			|| inet_pton(AF_INET, networkInterface.Broadcast.c_str(), &broadcastAddress.sin_addr) != 1
			|| bind(sock, (const sockaddr*)&localAddress, sizeof(localAddress)) == SOCKET_ERROR
			|| sendto(sock, request.data(), (int)request.size(), 0, (const sockaddr*)&broadcastAddress, sizeof(broadcastAddress)) == SOCKET_ERROR)
		{
			_MESSAGE("broadcast on %s failed: %d", networkInterface.Ip.c_str(), WSAGetLastError());
			closesocket(sock);
			return "";
		}

		// receive answers until the timeout, other smart devices may answer as well
		char buf[DEFAULT_BUFLEN];
		while (true)
		{
			sockaddr_in sourceAddress{};
			int addressLength = sizeof(sourceAddress);
			const int received = recvfrom(sock, buf, sizeof(buf), 0, (sockaddr*)&sourceAddress, &addressLength);
			if (received <= 0)
			{
				break;
			}

			std::vector<char> answer = TPLinkHelper::DecryptMessage(std::vector<char>(buf, buf + received), Enumerations::ProtocolType_UDP);
			const std::string recvMsg(answer.begin(), answer.end());
			_MESSAGE("Received from %s: %s", ToString(sourceAddress.sin_addr).c_str(), recvMsg.c_str());

			DeviceInfo info;
			info.LoadFromJson(recvMsg);
			if (info.alias() == plugName)
			{
				closesocket(sock);
				return ToString(sourceAddress.sin_addr);
			}
		}

		closesocket(sock);
		return "";
	}
}


SPScanner::SPScanner()
{
}


SPScanner::~SPScanner()
{
}

std::string SPScanner::ScanForTpPlug(std::string plugName)
{
	if (!EnsureWinsock()) {
		return "";
	}

	const std::string broadCastMsg = "{\"system\":{\"get_sysinfo\":{}}}";
	const std::vector<char> request = TPLinkHelper::EncryptMessage(std::vector<char>(broadCastMsg.begin(), broadCastMsg.end()), Enumerations::ProtocolType_UDP);

	std::vector<NetworkInterfaceInfo> interfaces;
	GetNetworkInterfaceInfos(interfaces);

	// an interface that fails (e.g. a disconnected vpn) must not stop the scan of the others
	for (const NetworkInterfaceInfo& networkInterface : interfaces)
	{
		const std::string ip = ScanInterface(networkInterface, request, plugName);
		if (!ip.empty())
		{
			return ip;
		}
	}
	return "";
}

int SPScanner::GetNetworkInterfaceInfos(std::vector<NetworkInterfaceInfo> & results)
{
	const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
	ULONG bufSz = 16 * 1024;
	std::vector<BYTE> buf;
	ULONG result = ERROR_BUFFER_OVERFLOW;
	// the adapter list can grow between the calls, so retry a few times with the size windows asks for
	for (int attempt = 0; attempt < 3 && result == ERROR_BUFFER_OVERFLOW; attempt++)
	{
		buf.resize(bufSz);
		result = GetAdaptersAddresses(AF_INET, flags, nullptr, (IP_ADAPTER_ADDRESSES*)buf.data(), &bufSz);
	}
	if (result != NO_ERROR)
	{
		_MESSAGE("GetAdaptersAddresses failed: %lu", result);
		return 0;
	}

	for (IP_ADAPTER_ADDRESSES* adapter = (IP_ADAPTER_ADDRESSES*)buf.data(); adapter != nullptr; adapter = adapter->Next)
	{
		// adapters that are down or loopback can not reach the plug, each one would only add a receive timeout to the scan
		if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
		{
			continue;
		}

		for (IP_ADAPTER_UNICAST_ADDRESS* address = adapter->FirstUnicastAddress; address != nullptr; address = address->Next)
		{
			if (address->Address.lpSockaddr->sa_family != AF_INET)
			{
				continue;
			}
			// /31 and /32 (e.g. vpn tunnels) have no broadcast address
			const ULONG prefixLength = address->OnLinkPrefixLength;
			if (prefixLength == 0 || prefixLength > 30)
			{
				continue;
			}

			const in_addr ip = ((const sockaddr_in*)address->Address.lpSockaddr)->sin_addr;
			ULONG mask = 0;
			ConvertLengthToIpv4Mask(prefixLength, &mask);
			in_addr netMask{};
			netMask.s_addr = mask;
			in_addr broadcast{};
			broadcast.s_addr = ip.s_addr | ~mask;

			NetworkInterfaceInfo info;
			info.Ip = ToString(ip);
			info.NetMask = ToString(netMask);
			info.Broadcast = ToString(broadcast);
			_MESSAGE("Found adapter: %s, mask %s, broadcast %s", info.Ip.c_str(), info.NetMask.c_str(), info.Broadcast.c_str());
			results.push_back(info);
		}
	}
	return (int)results.size();
}
