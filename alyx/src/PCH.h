#pragma once

// WIN32_LEAN_AND_MEAN is set project-wide so no windows.h include drags in the old winsock.h
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// printf-style logging used by the plug code shared with the game plugins (src/): console and ImmersiveAlyx.log
void _MESSAGE(const char * fmt, ...);
