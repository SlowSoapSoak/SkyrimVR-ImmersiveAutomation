#include "DeviceInfo.h"
#include <vector>
#include <iostream>
#include <stdio.h>

DeviceInfo::DeviceInfo()
{
}

void DeviceInfo::LoadFromJson(std::string json)
{
	std::string msg = "LoadFromJson with: ";
	msg.append(json);
	_MESSAGE(msg.c_str());

	std::vector<std::string> valueKeys;
	valueKeys.push_back("\"sw_ver\":");
	valueKeys.push_back("\"hw_ver\":");
	valueKeys.push_back("\"type\":");
	valueKeys.push_back("\"model\":");
	valueKeys.push_back("\"alias\":");
	valueKeys.push_back("\"mac\":");
	valueKeys.push_back("\"deviceId\":");
	valueKeys.push_back("\"hwId\":");
	valueKeys.push_back("\"fwId\":");
	valueKeys.push_back("\"oemId\":");
	valueKeys.push_back("\"dev_name\":");
	valueKeys.push_back("\"icon_hash\":");
	valueKeys.push_back("\"relay_state\":");
	valueKeys.push_back("\"on_time\":");
	valueKeys.push_back("\"active_mode\":");
	valueKeys.push_back("\"feature\":");
	valueKeys.push_back("\"updating\":");
	valueKeys.push_back("\"rssi\":");
	valueKeys.push_back("\"led_off\":");
	/*valueKeys.push_back("\"latitude\":");
	valueKeys.push_back("\"longitude\":");*/

	for (int i = 0; i < valueKeys.size(); i++) {
		size_t value_sep = json.find(valueKeys.at(i));
		size_t line_sep = json.find(',', value_sep);
		if (value_sep == std::string::npos || line_sep == std::string::npos) {
			continue;
		}
		std::string jsonvalue = json.substr(value_sep + valueKeys.at(i).length(), line_sep - (value_sep + valueKeys.at(i).length()));
		_MESSAGE(jsonvalue.c_str());
		
		if (valueKeys.at(i) == "\"sw_ver\":") {
			m_SoftwareVersion = jsonvalue.substr(1, jsonvalue.length() - 2); 
		}
		else if (valueKeys.at(i) == "\"hw_ver\":") {
			m_HardwareVersion = jsonvalue.substr(1, jsonvalue.length() - 2);
			//std::cout << m_HardwareVersion << "\n";
		}
		else if (valueKeys.at(i) == "\"type\":") {
			m_HardwareVersion = jsonvalue.substr(1, jsonvalue.length() - 2);
			//std::cout << m_HardwareVersion << "\n";
		}
		else if (valueKeys.at(i) == "\"model\":") {
			m_Model = jsonvalue.substr(1, jsonvalue.length() - 2);
			//std::cout << m_Model << "\n";
		}
		else if (valueKeys.at(i) == "\"alias\":") {
			m_Alias = jsonvalue.substr(1, jsonvalue.length() - 2);
			_MESSAGE("found alias:");
			_MESSAGE(m_Alias.c_str());
		}
	}


	/*internal void LoadFromJson(JToken pJson)
	{
	this.ErrorCode = pJson["err_code"].Value<int>();
	this.ErrorMessage = (this.ErrorCode != 0) ? pJson["err_msg"].Value<string>() : "";
	this.SoftwareVersion = (this.ErrorCode == 0) ? pJson["sw_ver"].Value<string>() : "";
	this.HardwareVersion = (this.ErrorCode == 0) ? pJson["hw_ver"].Value<string>() : "";
	this.DeviceType = (this.ErrorCode == 0) ? pJson["type"].Value<string>() : "";
	this.Model = (this.ErrorCode == 0) ? pJson["model"].Value<string>() : "";
	this.MAC = (this.ErrorCode == 0) ? pJson["mac"].Value<string>() : "";
	this.ID = (this.ErrorCode == 0) ? pJson["deviceId"].Value<string>() : "";
	this.HardwareID = (this.ErrorCode == 0) ? pJson["hwId"].Value<string>() : "";
	this.FirmwareID = (this.ErrorCode == 0) ? pJson["fwId"].Value<string>() : "";
	this.OEMID = (this.ErrorCode == 0) ? pJson["oemId"].Value<string>() : "";
	this.Alias = (this.ErrorCode == 0) ? pJson["alias"].Value<string>() : "";
	this.DeviceName = (this.ErrorCode == 0) ? pJson["dev_name"].Value<string>() : "";
	this.IconHash = (this.ErrorCode == 0) ? pJson["icon_hash"].Value<string>() : "";
	this.RelayState = (this.ErrorCode == 0) ? pJson["relay_state"].Value<byte>() : (byte)0;
	this.OnTime = (this.ErrorCode == 0) ? pJson["on_time"].Value<long>() : 0;
	this.ActiveMode = (this.ErrorCode == 0) ? pJson["active_mode"].Value<string>() : "";
	this.Feature = (this.ErrorCode == 0) ? pJson["feature"].Value<string>() : "";
	this.Updating = (this.ErrorCode == 0) ? pJson["updating"].Value<long>() : 0;
	this.RSSI = (this.ErrorCode == 0) ? pJson["rssi"].Value<long>() : 0;
	this.LedOffState = (this.ErrorCode == 0) ? pJson["led_off"].Value<byte>() : (byte)0;
	this.Latitude = (this.ErrorCode == 0) ? pJson["latitude"].Value<long>() : 0;
	this.Longitude = (this.ErrorCode == 0) ? pJson["longitude"].Value<long>() : 0;
	}*/
}

int DeviceInfo::errorCode() const
{
	return m_ErrorCode;
}

std::string DeviceInfo::errorMessage() const
{
	return m_ErrorMessage;
}

std::string DeviceInfo::softwareVersion() const
{
	return m_SoftwareVersion;
}

std::string DeviceInfo::hardwareVersion() const
{
	return m_HardwareVersion;
}

std::string DeviceInfo::deviceType() const
{
	return m_DeviceType;
}

std::string DeviceInfo::model() const
{
	return m_Model;
}

std::string DeviceInfo::MAC() const
{
	return m_MAC;
}

std::string DeviceInfo::ID() const
{
	return m_ID;
}

std::string DeviceInfo::hardwareID() const
{
	return m_HardwareID;
}

std::string DeviceInfo::firmwareID() const
{
	return m_FirmwareID;
}

std::string DeviceInfo::OEMID() const
{
	return m_OEMID;
}

std::string DeviceInfo::alias() const
{
	return m_Alias;
}

std::string DeviceInfo::deviceName() const
{
	return m_DeviceName;
}

std::string DeviceInfo::iconHash() const
{
	return m_IconHash;
}

char DeviceInfo::relayState() const
{
	return m_RelayState;
}

long DeviceInfo::onTime() const
{
	return m_OnTime;
}

std::string DeviceInfo::activeMode() const
{
	return m_ActiveMode;
}

std::string DeviceInfo::feature() const
{
	return m_Feature;
}

long DeviceInfo::updating() const
{
	return m_Updating;
}

long DeviceInfo::RSSI() const
{
	return m_RSSI;
}

char DeviceInfo::ledOffState() const
{
	return m_LedOffState;
}

long DeviceInfo::latitude() const
{
	return m_Latitude;
}

long DeviceInfo::longitude() const
{
	return m_Longitude;
}
