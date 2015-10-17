#pragma once

#include <Windows.h>
#include <varargs.h>

extern HANDLE g_log;

namespace aslog
{

namespace level
{

enum level
{
	debug, info, warn, error
};

}

HRESULT openlog();
void closelog();

void setlevel(aslog::level::level l);

void debug(LPCWSTR format, ...);
void info(LPCWSTR format, ...);
void warn(LPCWSTR format, ...);
void error(LPCWSTR format, ...);

void log(aslog::level::level l, LPCWSTR format, va_list args);
}
