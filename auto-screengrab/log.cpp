#include "log.h"

#include <Windows.h>
#include <ShlObj.h>
#include <strsafe.h>

HANDLE g_log = INVALID_HANDLE_VALUE;
aslog::level::level g_level = aslog::level::debug;

HRESULT aslog::openlog()
{
	HRESULT hr;
	PWSTR ladPath = NULL;
	WCHAR buf[32767];

	if (g_log != INVALID_HANDLE_VALUE) {
		hr = S_OK;
		goto out;
	}

	hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, NULL, &ladPath);
	if (hr != S_OK) {
		goto out;
	}

	hr = StringCbPrintf(buf, sizeof(buf), L"\\\\?\\%s\\auto-screengrab", ladPath);
	if (hr != S_OK) {
		goto out;
	}

	if (!CreateDirectory(buf, NULL)) {
		DWORD err = GetLastError();
		if (err != ERROR_ALREADY_EXISTS) {
			hr = HRESULT_FROM_WIN32(err);
			goto out;
		}
	}

	hr = StringCbPrintf(buf, sizeof(buf), L"\\\\?\\%s\\auto-screengrab\\log.txt", ladPath);
	if (hr != S_OK) {
		goto out;
	}

	g_log = CreateFile(buf, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (g_log == INVALID_HANDLE_VALUE) {
		hr = HRESULT_FROM_WIN32(GetLastError());
		goto out;
	}

	if (SetFilePointer(g_log, 0, NULL, FILE_END) == INVALID_SET_FILE_POINTER) {
		hr = HRESULT_FROM_WIN32(GetLastError());
		goto out;
	}

	hr = S_OK;

out:
	if (g_log != INVALID_HANDLE_VALUE && hr != S_OK) {
		aslog::closelog();
	}

	if (ladPath != NULL) {
		CoTaskMemFree(ladPath);
	}
	return hr;
}

void aslog::setlevel(level::level l)
{
	g_level = l;
}

static WCHAR linesep[] = { L'\r', L'\n' };
static LPCWSTR leveltext[] = {
	L"debug",
	L"info",
	L"warn",
	L"error"
};

#define helperfunc(x) \
void aslog::##x(LPCWSTR format, ...) \
{ \
	if (level::##x < g_level) { \
        return; \
	} \
    va_list args; \
    va_start(args, format); \
    log(level::##x, format, args); \
}

helperfunc(debug)
helperfunc(info)
helperfunc(warn)
helperfunc(error)

#undef helperfunc

void aslog::log(level::level l, LPCWSTR format, va_list args)
{
	if (l < g_level) {
		return;
	}

	SYSTEMTIME time;
	GetSystemTime(&time);

	WCHAR buf1[32767], buf2[32767];
	StringCbPrintfW(buf1, sizeof(buf1), L"%d-%d-%d %d:%d:%d %s: %s\r\n", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, leveltext[l], format);

	StringCbVPrintfW(buf2, sizeof(buf2), buf1, args);
	size_t len;
	StringCbLength(buf2, sizeof(buf2), &len);
	DWORD dummy;
	WriteFile(g_log, buf2, len, &dummy, NULL);
}

void aslog::closelog()
{
	if (g_log != INVALID_HANDLE_VALUE) {
		CloseHandle(g_log);
		g_log = INVALID_HANDLE_VALUE;
	}
}
