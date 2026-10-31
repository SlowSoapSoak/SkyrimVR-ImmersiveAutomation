#pragma once
#include <string>

// Switches a plug with a plain http(s) request, e.g. Shelly, Tasmota or a Home Assistant webhook.
namespace HttpSwitch
{
	// Sends method ("GET", "POST", ...) to url and returns true for a 2xx answer.
	bool Request(const std::string & method, const std::string & url, bool loggingEnabled);

	// Url of a webhook of the Home Assistant blueprint. address can be a plain ip, http:// and port 8123 are added if missing.
	std::string HomeAssistantUrl(std::string address, const std::string & webhookId, bool on);
}
