#ifndef _BRIDGE_H
#define _BRIDGE_H

#include <windows.h>

typedef struct _OS_INFO
{
	BOOL IsLinux;
	BOOL IsDarwin;
	BOOL IsWine;
} OS_INFO;

#endif // _BRIDGE_H
