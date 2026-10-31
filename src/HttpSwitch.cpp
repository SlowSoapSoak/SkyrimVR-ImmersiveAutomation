#include "HttpSwitch.h"
#include <winhttp.h>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace
{
	const int TIMEOUT_MS = 2000;

	std::wstring ToWide(const std::string & text)
	{
		if (text.empty()) {
			return std::wstring();
		}
		const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
		std::wstring result(length, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), result.data(), length);
		return result;
	}

	// closes a WinHTTP handle when it goes out of scope
	struct Handle
	{
		HINTERNET value = nullptr;
		~Handle()
		{
			if (value) {
				WinHttpCloseHandle(value);
			}
		}
	};
}

namespace HttpSwitch
{
	std::string HomeAssistantUrl(std::string address, const std::string & webhookId, bool on)
	{
		while (!address.empty() && address.back() == '/') {
			address.pop_back();
		}
		if (address.find("://") == std::string::npos) {
			address = "http://" + address;
		}
		if (address.find(':', address.find("://") + 3) == std::string::npos) {
			address += ":8123";
		}
		return address + "/api/webhook/" + webhookId + (on ? "?state=on" : "?state=off");
	}

	bool Request(const std::string & method, const std::string & url, bool loggingEnabled)
	{
		if (url.empty()) {
			return false;
		}

		const std::wstring wideUrl = ToWide(url);
		URL_COMPONENTS parts{};
		parts.dwStructSize = sizeof(parts);
		parts.dwHostNameLength = (DWORD)-1;
		parts.dwUrlPathLength = (DWORD)-1;
		parts.dwExtraInfoLength = (DWORD)-1;
		if (!WinHttpCrackUrl(wideUrl.c_str(), (DWORD)wideUrl.size(), 0, &parts)) {
			_MESSAGE("invalid url: %s", url.c_str());
			return false;
		}
		if (!parts.lpszHostName || parts.dwHostNameLength == 0) {
			_MESSAGE("invalid url: %s", url.c_str());
			return false;
		}
		const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
		std::wstring path;
		if (parts.lpszUrlPath) {
			path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
		}
		if (parts.lpszExtraInfo) {
			path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength); // query string
		}
		if (path.empty()) {
			path = L"/";
		}

		Handle session{ WinHttpOpen(L"ImmersiveWinds", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
		if (!session.value) {
			_MESSAGE("WinHttpOpen failed: %lu", GetLastError());
			return false;
		}
		WinHttpSetTimeouts(session.value, TIMEOUT_MS, TIMEOUT_MS, TIMEOUT_MS, TIMEOUT_MS);

		Handle connection{ WinHttpConnect(session.value, host.c_str(), parts.nPort, 0) };
		const DWORD flags = (parts.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
		Handle request{ connection.value ? WinHttpOpenRequest(connection.value, ToWide(method).c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags) : nullptr };
		if (!request.value
			|| !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
			|| !WinHttpReceiveResponse(request.value, nullptr)) {
			if (loggingEnabled) {
				_MESSAGE("http request to %s failed: %lu", url.c_str(), GetLastError());
			}
			return false;
		}

		DWORD statusCode = 0;
		DWORD size = sizeof(statusCode);
		WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
		if (loggingEnabled) {
			_MESSAGE("%s %s: %lu", method.c_str(), url.c_str(), statusCode);
		}
		return statusCode >= 200 && statusCode < 300;
	}
}
