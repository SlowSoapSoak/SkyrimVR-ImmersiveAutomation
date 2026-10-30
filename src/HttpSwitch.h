#pragma once
#include <string>

// Switches a plug with a plain http(s) request, e.g. Shelly, Tasmota or a Home Assistant webhook.
namespace HttpSwitch
{
	// Sends method ("GET", "POST", ...) to url and returns true for a 2xx answer.
	bool Request(const std::string & method, const std::string & url, bool loggingEnabled);
}
