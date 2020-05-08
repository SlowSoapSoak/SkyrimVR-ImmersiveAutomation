#include "TPLinkSPHelper.h"
#include "Enumerations.h"
#include <iostream>
#include <string>
#include <fstream> 
#include <stdio.h>
#include <shlobj.h>
#include <intrin.h>

TPLinkHelper::TPLinkHelper()
{
	m_ip = NULL;
	m_port = 0;
	m_udp = false;
}

bool TPLinkHelper::ConnectToHost(int PortNo, const char* IPAddress)
{
	std::string logMsg("ConnectToHost called ");
	logMsg.append(std::to_string(PortNo));
	logMsg.append(IPAddress);
	//_MESSAGE(logMsg.c_str());
	m_port = PortNo;
	m_ip = IPAddress;
	bool canConnect = OpenConnection();
	if (canConnect) {
		CloseConnection();
	}
	return canConnect;
}

bool TPLinkHelper::OpenConnection()
{
	//_MESSAGE("OpenConnection called");
	//_MESSAGE(m_ip);

	//Start up Winsock…
	WSADATA wsadata;

	int error = WSAStartup(MAKEWORD(2, 2), &wsadata);

	//Did something happen? TODO: log
	if (error)
		return false;

	//Did we get the right Winsock version?
	if (false)//(wsadata.wVersion != 0x0202)
	{
		WSACleanup(); //Clean up Winsock
		return false;
	}

	//Fill out the information needed to initialize a socket…
	SOCKADDR_IN target; //Socket address information

	target.sin_family = AF_INET; // address family Internet
	target.sin_port = htons(m_port); //Port to connect on
	target.sin_addr.s_addr = inet_addr(m_ip); //Target IP

	if (m_udp) {
		ConnectSocket = socket(AF_INET, SOCK_DGRAM, 0);
		if (ConnectSocket == INVALID_SOCKET)
		{
			return ""; // error
		}

		struct sockaddr_in Recv_addr;
		Recv_addr.sin_family = AF_INET;
		Recv_addr.sin_port = htons(9999);
		Recv_addr.sin_addr.s_addr = inet_addr(m_ip);


		int timeout = 3000;
		setsockopt(ConnectSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(int));

		//if (bind(ConnectSocket, (sockaddr*)&Recv_addr, sizeof(Recv_addr)) < 0)
		//{
		//	closesocket(ConnectSocket);
		//	return false; // error
		//}
		return true;
	}
	else {
		ConnectSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); //Create socket
		if (ConnectSocket == INVALID_SOCKET)
		{
			_MESSAGE("socket creation failed");
			return false; //Couldn't create the socket
		}

		//Try connecting...

		if (connect(ConnectSocket, (SOCKADDR *)&target, sizeof(target)) == SOCKET_ERROR)
		{
			return false; //Couldn't connect
		}
		else
			return true; //Success
	}
}

//CLOSECONNECTION – shuts down the socket and closes any connection on it
void TPLinkHelper::CloseConnection()
{
	//_MESSAGE("CloseConnection called");
	//Close the socket if it exists
	if (ConnectSocket)
		closesocket(ConnectSocket);

	WSACleanup(); //Clean up Winsock
}

bool TPLinkHelper::LoadIPConfigFromFile(std::string configName)
{
	CHAR my_documents[MAX_PATH];
	HRESULT result = SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, my_documents);
	//_MESSAGE(my_documents);

	std::string sExecPath(my_documents);
	sExecPath.append("\\My Games\\Skyrim VR\\SKSE\\");
	//_MESSAGE(sExecPath.c_str());
	sExecPath.append("immersiveSun.ini");
	//std::cout << sExecPath << "\n";
	//_MESSAGE(sExecPath.c_str());

	if (INVALID_FILE_ATTRIBUTES == GetFileAttributesA(sExecPath.c_str()) && GetLastError() == ERROR_FILE_NOT_FOUND)
	{
		_MESSAGE(sExecPath.c_str());
		_MESSAGE("file not found");
		std::ofstream outfile(sExecPath);
		//outfile << std::endl;
		outfile.close();
	}

	char iniFileBuffer[100];
	GetPrivateProfileStringA("Internal", configName.c_str(), "",
		iniFileBuffer, 100, sExecPath.c_str());
	std::string defaultIp(iniFileBuffer);
	m_sIp = defaultIp;
	std::cout << m_sIp;
	//_MESSAGE(m_sIp.c_str());

	unsigned int useUDP = GetPrivateProfileIntA("GeneralModConfig", "bUseUdp", 0, sExecPath.c_str());
	if (useUDP > 0) {
		m_udp = true;
	}
	else {
		m_udp = false;
	}
	std::cout << m_sIp;
	//_MESSAGE(m_sIp.c_str());

	m_port = 9999;
	m_ip = (char*)m_sIp.c_str();
	//_MESSAGE(m_ip);

	return m_sIp != "";
}

int TPLinkHelper::SendAndReceiveEncoded(std::string messageSend, std::string& messageAnswer)
{
	//_MESSAGE("SendAndReceiveEncoded called");
	//std::cout << "going to send\n";
	int answerLengthInternal;
	std::vector<char> vMsgSend(messageSend.begin(), messageSend.end());
	if (m_udp) {
		vMsgSend = EncryptMessage(vMsgSend, Enumerations::ProtocolType_UDP);
		_MESSAGE("using udp");
	}
	else {
		vMsgSend = EncryptMessage(vMsgSend, Enumerations::ProtocolType_TCP);
		_MESSAGE("using tcp");
	}

	std::string sMsgSend(vMsgSend.begin(), vMsgSend.end());
	//_MESSAGE(sMsgSend.c_str());
	answerLengthInternal = 1600;
	char buf[DEFAULT_BUFLEN];
	int code = SendAndReceive(sMsgSend.c_str(), vMsgSend.size(), buf, answerLengthInternal);
	if (code) {
		// return if unsuccessful
		_MESSAGE("unsuccessful");
	}
	std::string log = "answer length: ";
	log.append(std::to_string(answerLengthInternal));
	_MESSAGE(log.c_str());
	std::string sbuf(buf, answerLengthInternal);
	//std::cout << "rec sbuf " << sbuf << "\n";
	std::vector<char> vMsgReceive(sbuf.begin(), sbuf.end());
	if (m_udp) {
		vMsgReceive = DecryptMessage(vMsgReceive, Enumerations::ProtocolType_UDP);
	}
	else {
		vMsgReceive = DecryptMessage(vMsgReceive, Enumerations::ProtocolType_TCP);
	}
	messageAnswer = std::string(vMsgReceive.begin(), vMsgReceive.end());
	size_t endOfMsg = messageAnswer.rfind('\0');
	//printf("endOfMsg: %ld\n", endOfMsg);
	if (endOfMsg != -1) {
		messageAnswer.resize(endOfMsg);
	}
	//std::cout << "rec msg " << messageAnswer << "\n";
	return 0;
}

int TPLinkHelper::SendAndReceive(const char *sendbuf, int sendLen, char* recvbuf, int &recvbuflen)
{
	//_MESSAGE("SendAndReceive called");
	bool couldOpenConnection = OpenConnection();
	if (!couldOpenConnection) {
		std::string log = "Could not connect ";
		if (m_ip == NULL) {
			log.append("null");
		}
		else {
			log.append(m_ip);
		}
		//_MESSAGE(log.c_str());
	}
	else {
		std::string log = "Could connect ";
		if (m_ip == NULL) {
			log.append("null");
		}
		else {
			log.append(m_ip);
		}
		//_MESSAGE(log.c_str());
	}
	if (ConnectSocket != INVALID_SOCKET) {

		int iResult;
		if (m_udp) {
			struct sockaddr_in Sender_addr;
			Sender_addr.sin_family = AF_INET;
			Sender_addr.sin_port = htons(9999);
			Sender_addr.sin_addr.s_addr = inet_addr(m_ip);

			if (sendto(ConnectSocket, sendbuf, sendLen, 0, (sockaddr *)&Sender_addr, sizeof(Sender_addr)) < 0)
			{
				closesocket(ConnectSocket);
				WSACleanup();
				return 1;// error
			}
		}
		else {

			// Send an initial buffer
			iResult = send(ConnectSocket, sendbuf, sendLen, 0); //(int)strlen(sendbuf)
			if (iResult == SOCKET_ERROR) {
				printf("send failed: %d\n", WSAGetLastError());
				closesocket(ConnectSocket);
				WSACleanup();
				return 1;
			}
			printf("Bytes Sent: %ld\n", iResult);
		}
		//_MESSAGE("sent message successfully.");

		//ReceiveOnce(ConnectSocket, recvbuf);


		// shutdown the connection for sending since no more data will be sent
		// the client can still use the ConnectSocket for receiving data
		iResult = shutdown(ConnectSocket, SD_SEND);
		if (iResult == SOCKET_ERROR) {
			//_MESSAGE("shutdown failed");
			printf("shutdown failed: %d\n", WSAGetLastError());
			closesocket(ConnectSocket);
			WSACleanup();
			return 1;
		}

		// Receive data until the server closes the connection
		int total = 0;
		do {
			if (m_udp) {
				struct sockaddr_in Recv_addr;
				socklen_t addrlen = sizeof(struct sockaddr_in);
				iResult = recvfrom(ConnectSocket, recvbuf + total, recvbuflen, 0, (struct sockaddr *)&Recv_addr, &addrlen);
			}
			else {
				iResult = recv(ConnectSocket, recvbuf + total, recvbuflen, 0);
			}
			if (iResult > 0)
			{
				total += iResult;
				printf("Bytes received: %d\n", iResult);
			}
			else if (iResult == 0)
			{
				_MESSAGE("connection closed by server");
			}
			else
			{
				std::string error("recv wsa error result: ");
				error.append(std::to_string(WSAGetLastError()));
				_MESSAGE(error.c_str());
			}
		} while (iResult > 0);
		recvbuflen = total;
		if (total > 0) {
			_MESSAGE("got an answer. ");
		}
	}
	else {
		std::string log = "Socket is invalid! Couldn't send message.";
		//_MESSAGE(log.c_str());
	}
	CloseConnection();

	return 0;
}

int TPLinkHelper::ReceiveOnce(int s, char * buf)
{
	int n = recv(s, buf, DEFAULT_BUFLEN, 0);
	return n;
}

int TPLinkHelper::ReceiveAll(int s, char *buf, int *len)
{
	//_MESSAGE("ReceiveAll called");
	int total = 0;        // how many bytes we've received
	int bytesleft = *len; // how many we have left to receive
	int n = -1;
	int counter = 0;

	while (total < *len) {
		if (counter <= 100) {
			break; // break while loop
		}
		n = recv(s, buf + total, bytesleft, 0);
		if (n <= 0) { break; }
		total += n;
		bytesleft -= n;
		counter++;
	}

	*len = total; // return number actually received here

	return (n <= 0) ? -1 : 0; // return -1 on failure, 0 on success
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
		// Add prefix of the message
		if (pProtocolType == Enumerations::ProtocolType_TCP)
		{
			mBuffer.push_back(0x00);
			mBuffer.push_back(0x00);
			mBuffer.push_back(0x00);
			mBuffer.push_back(0x00);
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

void TPLinkHelper::SwitchRelayState(long on)
{
	//_MESSAGE("SwitchRelayState called");
	std::string result = "";
	if (on == 1) {
		std::string msg = "{\"system\":{\"set_relay_state\":{\"state\":1}}}";
		SendAndReceiveEncoded(msg, result);
		//_MESSAGE(result.c_str());
	}
	else {
		std::string msg = "{\"system\":{\"set_relay_state\":{\"state\":0}}}";
		SendAndReceiveEncoded(msg, result);
		//_MESSAGE(result.c_str());
	}
}

void TPLinkHelper::SwitchLEDState(long on)
{
	std::string result = "";
	if (on == 1) {
		std::string msg = "{\"system\":{\"set_led_off\":{\"state\":1}}}";
		SendAndReceiveEncoded(msg, result);
		_MESSAGE(result.c_str());
	}
	else {
		std::string msg = "{\"system\":{\"set_led_off\":{\"state\":0}}}";
		SendAndReceiveEncoded(msg, result);
		_MESSAGE(result.c_str());
	}
}

DeviceInfo TPLinkHelper::GetSystemInfo()
{
	_MESSAGE("GetSystemInfo called");
	std::string result = "";

	std::string msg = "{\"system\":{\"get_sysinfo\":{}}}";
	SendAndReceiveEncoded(msg, result);
	_MESSAGE(result.c_str());

	DeviceInfo info;
	info.LoadFromJson(result);
	_MESSAGE("created device info successfully");
	return info;
}