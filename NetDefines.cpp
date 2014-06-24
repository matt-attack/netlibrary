#include "NetDefines.h"

#include <Windows.h>
#include <stdio.h>

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