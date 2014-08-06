#include "NetDefines.h"

#include <stdio.h>
#include <cstdarg>

#ifdef _WIN32
#include <Windows.h>

void netlog(char* o)
{
	OutputDebugString(o);
}

void netlogf(const char* fmt, ...)
{
	va_list		argptr;
	char		msg[500];

	va_start (argptr,fmt);
	vsnprintf(msg, sizeof(msg), fmt, argptr);
	va_end (argptr);

	OutputDebugString(msg);
}

void NetSleep(unsigned int ms)
{
	Sleep(ms);
}

unsigned int NetGetTime()
{
	return GetTickCount();
}
#else
#include <unistd.h>

void netlog(char* o)
{
	//OutputDebugString(o);
}

void netlogf(const char* fmt, ...)
{
	va_list		argptr;
	char		msg[500];

	va_start (argptr,fmt);
	vsnprintf(msg, sizeof(msg), fmt, argptr);
	va_end (argptr);

	//OutputDebugString(msg);
}

void NetSleep(unsigned int ms)
{
	usleep(ms*1000);
}

unsigned int NetGetTime()
{
	struct timeval tv;
	if(gettimeofday(&tv, NULL) != 0)
		return 0;

	return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}
#endif
