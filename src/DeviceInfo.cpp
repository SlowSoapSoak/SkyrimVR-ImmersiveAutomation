#include "DeviceInfo.h"
#include <nlohmann/json.hpp>

namespace
{
	std::string GetString(const nlohmann::json& object, const char* key)
	{
		const auto it = object.find(key);
		return (it != object.end() && it->is_string()) ? it->get<std::string>() : std::string();
	}

	long GetNumber(const nlohmann::json& object, const char* key)
	{
		const auto it = object.find(key);
		return (it != object.end() && it->is_number()) ? it->get<long>() : 0;
	}
}

DeviceInfo::DeviceInfo() :
	m_ErrorCode(0), m_RelayState(0), m_OnTime(0), m_Updating(0), m_RSSI(0), m_LedOffState(0), m_Latitude(0), m_Longitude(0)
{
}

// Reads the answer to {"system":{"get_sysinfo":{}}}. Invalid json leaves all values empty.
void DeviceInfo::LoadFromJson(std::string json)
{
	_MESSAGE("LoadFromJson with: %s", json.c_str());

	const nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
	if (root.is_discarded() || !root.is_object()) {
		_MESSAGE("no valid json");
		return;
	}
	const auto system = root.find("system");
	if (system == root.end() || !system->is_object()) {
		return;
	}
	const auto sysinfoIt = system->find("get_sysinfo");
	if (sysinfoIt == system->end() || !sysinfoIt->is_object()) {
		return;
	}
	const nlohmann::json& sysinfo = *sysinfoIt;

	m_ErrorCode = GetNumber(sysinfo, "err_code");
	m_ErrorMessage = GetString(sysinfo, "err_msg");
	m_SoftwareVersion = GetString(sysinfo, "sw_ver");
	m_HardwareVersion = GetString(sysinfo, "hw_ver");
	m_DeviceType = GetString(sysinfo, "type");
	if (m_DeviceType.empty()) {
		m_DeviceType = GetString(sysinfo, "mic_type");
	}
	m_Model = GetString(sysinfo, "model");
	m_MAC = GetString(sysinfo, "mac");
	m_ID = GetString(sysinfo, "deviceId");
	m_HardwareID = GetString(sysinfo, "hwId");
	m_FirmwareID = GetString(sysinfo, "fwId");
	m_OEMID = GetString(sysinfo, "oemId");
	m_Alias = GetString(sysinfo, "alias");
	m_DeviceName = GetString(sysinfo, "dev_name");
	m_IconHash = GetString(sysinfo, "icon_hash");
	m_RelayState = (char)GetNumber(sysinfo, "relay_state");
	m_OnTime = GetNumber(sysinfo, "on_time");
	m_ActiveMode = GetString(sysinfo, "active_mode");
	m_Feature = GetString(sysinfo, "feature");
	m_Updating = GetNumber(sysinfo, "updating");
	m_RSSI = GetNumber(sysinfo, "rssi");
	m_LedOffState = (char)GetNumber(sysinfo, "led_off");
	m_Latitude = GetNumber(sysinfo, "latitude");
	m_Longitude = GetNumber(sysinfo, "longitude");

	_MESSAGE("found alias: %s", m_Alias.c_str());
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
