#pragma once
#include <atomic>
#include <string>

// Finds the TP-Link plug named plugName. The ip saved under [Internal] ipKey in iniPath is used if a plug with
// that name answers there, otherwise the local network is scanned (several attempts while keepRunning is set)
// and a found ip is written to the ini. A failed search keeps the saved ip, so a plug that is only offline for
// a while is not forgotten. Returns the ip of the plug, or "" if it was not found.
std::string FindTpLinkPlug(const std::string & iniPath, const char * ipKey, const std::string & plugName, bool loggingEnabled, const std::atomic<bool> & keepRunning);
