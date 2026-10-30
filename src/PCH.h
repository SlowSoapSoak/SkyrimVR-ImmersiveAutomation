#pragma once

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

// Windows API only after CommonLib (it refuses otherwise); WIN32_LEAN_AND_MEAN is set project-wide
// so no windows.h include drags in the old winsock.h, which clashes with winsock2.h
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <shlobj.h>

#include <cstdarg>
#include <cstdio>

using namespace std::literals;

// printf-style logging shim kept from the old SKSE code base, forwards to the CommonLib/spdlog logger
inline void _MESSAGE(const char* fmt, ...)
{
	char buffer[4096];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	SKSE::log::info("{}", buffer);
}
