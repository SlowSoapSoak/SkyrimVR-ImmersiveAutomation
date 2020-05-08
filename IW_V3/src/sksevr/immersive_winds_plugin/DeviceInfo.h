#pragma once
#include <string>
class DeviceInfo
{
private:
	int m_ErrorCode;
	std::string m_ErrorMessage;
	std::string m_SoftwareVersion;
	std::string m_HardwareVersion;
	std::string m_DeviceType;
	std::string m_Model;
	std::string m_MAC;
	std::string m_ID;
	std::string m_HardwareID;
	std::string m_FirmwareID;
	std::string m_OEMID;
	std::string m_Alias;
	std::string m_DeviceName;
	std::string m_IconHash;
	char m_RelayState;
	long m_OnTime;
	std::string m_ActiveMode;
	std::string m_Feature;
	long m_Updating;
	long m_RSSI;
	char m_LedOffState;
	long m_Latitude;
	long m_Longitude;


public:
	DeviceInfo();
	void LoadFromJson(std::string json);

	int errorCode() const;
	std::string errorMessage() const;
	std::string softwareVersion() const;
	std::string hardwareVersion() const;
	std::string deviceType() const;
	std::string model() const;
	std::string MAC() const;
	std::string ID() const;
	std::string hardwareID() const;
	std::string firmwareID() const;
	std::string OEMID() const;
	std::string alias() const;
	std::string deviceName() const;
	std::string iconHash() const;
	char relayState() const;
	long onTime() const;
	std::string activeMode() const;
	std::string feature() const;
	long updating() const;
	long RSSI() const;
	char ledOffState() const;
	long latitude() const;
	long longitude() const;
};

