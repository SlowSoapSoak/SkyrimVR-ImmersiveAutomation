#include "TPLinkSPHelper.h"
#include "Enumerations.h"
#include <string>
#include <fstream>

namespace ImmersiveWinds { std::string GetIniPath(); }

namespace
{
	const int CONNECT_TIMEOUT_MS = 2000;
	const int RECEIVE_TIMEOUT_MS = 2000;
	// tcp messages start with the length of the encrypted payload (4 bytes, big endian)
	const size_t TCP_HEADER_LENGTH = 4;
	// upper bound for a tcp answer, protects against a broken length header
	const size_t MAX_RESPONSE_LENGTH = 64 * 1024;

	// connect() with a timeout, a blocking connect to an unreachable ip takes about 20 seconds
	bool ConnectWithTimeout(SOCKET socket, const sockaddr_in& target, int timeoutMs)
	{
		u_long nonBlocking = 1;
		if (ioctlsocket(socket, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
			return false;
		}

		bool connected = connect(socket, (const sockaddr*)&target, sizeof(target)) == 0;
		if (!connected && WSAGetLastError() == WSAEWOULDBLOCK) {
			fd_set writeSet;
			FD_ZERO(&writeSet);
			FD_SET(socket, &writeSet);
			// a failed connect is reported in the error set, not the write set
			fd_set errorSet;
			FD_ZERO(&errorSet);
			FD_SET(socket, &errorSet);
			timeval timeout;
			timeout.tv_sec = timeoutMs / 1000;
			timeout.tv_usec = (timeoutMs % 1000) * 1000;
			connected = select(0, nullptr, &writeSet, &errorSet, &timeout) > 0 && FD_ISSET(socket, &writeSet);
		}

		u_long blocking = 0;
		ioctlsocket(socket, FIONBIO, &blocking);
		return connected;
	}

	size_t ReadTcpPayloadLength(const std::vector<char>& data)
	{
		return (size_t((unsigned char)data[0]) << 24) | (size_t((unsigned char)data[1]) << 16) |
		       (size_t((unsigned char)data[2]) << 8) | size_t((unsigned char)data[3]);
	}
}

bool EnsureWinsock()
{
	static const bool started = [] {
		WSADATA wsadata;
		const int error = WSAStartup(MAKEWORD(2, 2), &wsadata);
		if (error) {
			_MESSAGE("WSAStartup failed: %d", error);
		}
		return error == 0;
	}();
	return started;
}

TPLinkHelper::TPLinkHelper(bool enableLogging)
{
	m_loggingEnabled = enableLogging;
	m_port = 0;
	m_udp = false;
}

TPLinkHelper::~TPLinkHelper()
{
	CloseConnection();
}

bool TPLinkHelper::ConnectToHost(int PortNo, const char* IPAddress)
{
	m_port = PortNo;
	m_sIp = IPAddress ? IPAddress : "";
	bool canConnect = OpenConnection();
	CloseConnection();
	return canConnect;
}

bool TPLinkHelper::OpenConnection()
{
	CloseConnection();

	if (!EnsureWinsock()) {
		return false;
	}

	sockaddr_in target{};
	target.sin_family = AF_INET;
	target.sin_port = htons((u_short)m_port);
	if (inet_pton(AF_INET, m_sIp.c_str(), &target.sin_addr) != 1) {
		if (m_loggingEnabled) {
			_MESSAGE("invalid ip: %s", m_sIp.c_str());
		}
		return false;
	}

	ConnectSocket = m_udp ? socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP) : socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (ConnectSocket == INVALID_SOCKET) {
		_MESSAGE("socket creation failed: %d", WSAGetLastError());
		return false;
	}

	DWORD timeout = RECEIVE_TIMEOUT_MS;
	setsockopt(ConnectSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

	if (m_udp) {
		// for udp this only sets the default destination, so send/recv work the same for both protocols
		return connect(ConnectSocket, (const sockaddr*)&target, sizeof(target)) == 0;
	}
	return ConnectWithTimeout(ConnectSocket, target, CONNECT_TIMEOUT_MS);
}

void TPLinkHelper::CloseConnection()
{
	if (ConnectSocket != INVALID_SOCKET) {
		closesocket(ConnectSocket);
		ConnectSocket = INVALID_SOCKET;
	}
}

bool TPLinkHelper::LoadIPConfigFromFile(int &useUDP, std::string &defaultIp)
{
	if(useUDP != -1 && !defaultIp.empty())
	{
		m_udp = (useUDP > 0);
		m_port = 9999;
		m_sIp = defaultIp;
	}
	else
	{
		const std::string sExecPath = ImmersiveWinds::GetIniPath();

		if (INVALID_FILE_ATTRIBUTES == GetFileAttributesA(sExecPath.c_str()) && GetLastError() == ERROR_FILE_NOT_FOUND)
		{
			_MESSAGE("%s", sExecPath.c_str());
			_MESSAGE("file not found");
			std::ofstream outfile(sExecPath);
			outfile.close();
		}

		char iniFileBuffer[100];
		GetPrivateProfileStringA("Internal", "sTpLinkPlugIp", "",
			iniFileBuffer, 100, sExecPath.c_str());
		defaultIp = iniFileBuffer;
		m_sIp = defaultIp;

		useUDP = GetPrivateProfileIntA("GeneralModConfig", "bUseUdp", 0, sExecPath.c_str());
		m_udp = (useUDP > 0);

		m_port = 9999;
	}
	return m_sIp != "";
}


int TPLinkHelper::SendAndReceiveEncoded(const std::string &messageSend, std::string &messageAnswer)
{
	messageAnswer.clear();

	const int protocolType = m_udp ? Enumerations::ProtocolType_UDP : Enumerations::ProtocolType_TCP;
	if (m_loggingEnabled) {
		_MESSAGE(m_udp ? "using udp" : "using tcp");
	}

	std::vector<char> response;
	if (SendAndReceive(EncryptMessage(std::vector<char>(messageSend.begin(), messageSend.end()), protocolType), response)) {
		_MESSAGE("unsuccessful");
		return 1;
	}

	std::vector<char> decrypted = DecryptMessage(response, protocolType);
	messageAnswer.assign(decrypted.begin(), decrypted.end());
	return 0;
}

bool TPLinkHelper::SendAll(const std::vector<char> &request)
{
	size_t sent = 0;
	while (sent < request.size()) {
		const int result = send(ConnectSocket, request.data() + sent, (int)(request.size() - sent), 0);
		if (result == SOCKET_ERROR) {
			return false;
		}
		sent += result;
		if (m_udp) {
			break; // a datagram is sent completely or not at all
		}
	}
	return true;
}

int TPLinkHelper::SendAndReceive(const std::vector<char> &request, std::vector<char> &response)
{
	response.clear();

	if (!OpenConnection()) {
		if (m_loggingEnabled) {
			_MESSAGE("Could not connect to %s", m_sIp.c_str());
		}
		CloseConnection();
		return 1;
	}

	if (!SendAll(request)) {
		_MESSAGE("send failed: %d", WSAGetLastError());
		CloseConnection();
		return 1;
	}

	char buffer[DEFAULT_BUFLEN];
	bool complete = false;
	if (m_udp) {
		// one datagram is the whole answer, waiting for more only ran into the receive timeout
		const int received = recv(ConnectSocket, buffer, sizeof(buffer), 0);
		if (received > 0) {
			response.assign(buffer, buffer + received);
			complete = true;
		}
		else if (m_loggingEnabled) {
			_MESSAGE("recv wsa error result: %d", WSAGetLastError());
		}
	}
	else {
		// signal the end of the request, older firmware may wait for it
		shutdown(ConnectSocket, SD_SEND);

		// read until the length announced in the header has arrived
		size_t expectedLength = 0;
		while (!complete && response.size() < MAX_RESPONSE_LENGTH) {
			const int received = recv(ConnectSocket, buffer, sizeof(buffer), 0);
			if (received <= 0) {
				if (received < 0 && m_loggingEnabled) {
					_MESSAGE("recv wsa error result: %d", WSAGetLastError());
				}
				break;
			}
			response.insert(response.end(), buffer, buffer + received);
			if (expectedLength == 0 && response.size() >= TCP_HEADER_LENGTH) {
				expectedLength = TCP_HEADER_LENGTH + ReadTcpPayloadLength(response);
			}
			complete = expectedLength != 0 && response.size() >= expectedLength;
		}
		if (complete) {
			response.resize(expectedLength);
		}
	}
	CloseConnection();

	if (!complete) {
		response.clear();
		return 1;
	}
	if (m_loggingEnabled) {
		_MESSAGE("got an answer. ");
	}
	return 0;
}

/// <summary>
/// Encrypt the command message
/// </summary>
/// <param name="pMessage">Message</param>
/// <param name="pProtocolType">Protocol type</param>
/// <returns>Returns the encrypted bytes of the message</returns>
std::vector<char> TPLinkHelper::EncryptMessage(std::vector<char> pMessage, int pProtocolType)
{
	std::vector<char> mBuffer;
	int key = 0xAB;

	if ((pMessage.size() > 0))
	{
		// Add the length of the message as prefix (4 bytes, big endian)
		if (pProtocolType == Enumerations::ProtocolType_TCP)
		{
			const size_t length = pMessage.size();
			mBuffer.push_back((char)((length >> 24) & 0xFF));
			mBuffer.push_back((char)((length >> 16) & 0xFF));
			mBuffer.push_back((char)((length >> 8) & 0xFF));
			mBuffer.push_back((char)(length & 0xFF));
		}

		// Encode the message
		for (unsigned int i = 0; i < pMessage.size(); i++)
		{
			char b = (char)(key ^ pMessage.at(i));
			key = b;
			mBuffer.push_back(b);
		}
	}

	return mBuffer;
}

/// <summary>
/// Decrypt the message
/// </summary>
/// <param name="pMessage">Message</param>
/// <param name="pProtocolType">Protocol type</param>
/// <returns>Returns the decrypted message</returns>
std::vector<char> TPLinkHelper::DecryptMessage(std::vector<char> pMessage, int pProtocolType)
{
	std::vector<char> mBuffer;
	int key = 0xAB;

	//Skip the first 4 bytes in TCP communications (4 bytes header)
	char header = (pProtocolType == Enumerations::ProtocolType_UDP) ? (char)0x00 : (char)0x04;

	if (pMessage.size() > 0)
	{
		for (unsigned int i = header; i < pMessage.size(); i++)
		{
			char b = (char)(key ^ pMessage[i]);
			key = pMessage[i];
			mBuffer.push_back(b);
		}
	}

	return mBuffer;
}

bool TPLinkHelper::SwitchRelayState(long on)
{
	std::string result = "";
	const std::string msg = (on == 1) ? "{\"system\":{\"set_relay_state\":{\"state\":1}}}" : "{\"system\":{\"set_relay_state\":{\"state\":0}}}";
	return SendAndReceiveEncoded(msg, result) == 0;
}

void TPLinkHelper::SwitchLEDState(long on)
{
	std::string result = "";
	if (on == 1) {
		std::string msg = "{\"system\":{\"set_led_off\":{\"state\":1}}}";
		SendAndReceiveEncoded(msg, result);
		_MESSAGE("%s", result.c_str());
	}
	else {
		std::string msg = "{\"system\":{\"set_led_off\":{\"state\":0}}}";
		SendAndReceiveEncoded(msg, result);
		_MESSAGE("%s", result.c_str());
	}
}

DeviceInfo TPLinkHelper::GetSystemInfo()
{
	_MESSAGE("GetSystemInfo called");
	std::string result = "";

	std::string msg = "{\"system\":{\"get_sysinfo\":{}}}";
	SendAndReceiveEncoded(msg, result);
	_MESSAGE("%s", result.c_str());

	DeviceInfo info;
	info.LoadFromJson(result);
	_MESSAGE("created device info successfully");
	return info;
}
