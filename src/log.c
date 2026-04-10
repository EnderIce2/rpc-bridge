#include <windows.h>
#include <stdio.h>
#include <time.h>

static FILE *g_logFile = NULL;
static HANDLE g_logMutex = NULL;

extern BOOL RunningAsService;

VOID ExitBridge(UINT uExitCode);

void print(char const *fmt, ...)
{
	SYSTEMTIME st;
	GetLocalTime(&st);

	if (g_logMutex)
	{
		/* 1s timeout to prevent deadlocks */
		DWORD result = WaitForSingleObject(g_logMutex, 1000);
		if (result == WAIT_TIMEOUT)
		{
			printf("Log mutex wait timed out\n");
			if (g_logFile)
			{
				fprintf(g_logFile, "Log mutex wait timed out\n");
				fflush(g_logFile);
			}
		}
	}

	char prefix[32];
	snprintf(prefix, sizeof(prefix), "[%02d:%02d:%02d-%d] ",
			 st.wHour, st.wMinute, st.wSecond, GetCurrentProcessId());

	va_list args;

	/* stdout */
	va_start(args, fmt);
	printf("%s", prefix);
	vprintf(fmt, args);
	va_end(args);

	if (g_logFile)
	{
		va_start(args, fmt);
		fprintf(g_logFile, "%s", prefix);
		vfprintf(g_logFile, fmt, args);
		va_end(args);
		fflush(g_logFile); /* flush immediately so log is readable while running */
	}

	if (g_logMutex)
		ReleaseMutex(g_logMutex);
}

void LogInit()
{
	static const LPCTSTR logPath = "C:\\windows\\logs\\bridge.log";
	static const LPCSTR rotatedLogPath = "C:\\windows\\logs\\bridge.log.1";
	static const LARGE_INTEGER maxLogSize = {.QuadPart = 1LL * 1024 * 1024};

	CreateDirectory("C:\\windows\\logs", NULL);

	WIN32_FILE_ATTRIBUTE_DATA logAttrs;
	if (GetFileAttributesEx(logPath, GetFileExInfoStandard, &logAttrs))
	{
		LARGE_INTEGER logSize;
		logSize.HighPart = (LONG)logAttrs.nFileSizeHigh;
		logSize.LowPart = logAttrs.nFileSizeLow;
		if (logSize.QuadPart >= maxLogSize.QuadPart)
			MoveFileEx(logPath, rotatedLogPath, MOVEFILE_REPLACE_EXISTING);
	}

	g_logFile = fopen(logPath, "a");
	if (g_logFile == NULL)
	{
		printf("Failed to open log file: %ld\n", GetLastError());
		if (!RunningAsService)
			MessageBox(NULL, "Failed to open log file", "Error", MB_OK | MB_ICONERROR);
		ExitBridge(1);
	}

	g_logMutex = CreateMutex(NULL, FALSE, "rpc-bridge-logger");
	if (g_logMutex == NULL)
	{
		printf("Failed to create log mutex: %ld\n", GetLastError());
		if (!RunningAsService)
			MessageBox(NULL, "Failed to create log mutex", "Error", MB_OK | MB_ICONERROR);
		ExitBridge(1);
	}

	print("------ rpc-bridge started [%d] ------\n", RunningAsService);
}

void LogClose()
{
	if (g_logFile)
	{
		print("------ rpc-bridge stopped [%d] ------\n", RunningAsService);
		fclose(g_logFile);
		g_logFile = NULL;
	}

	if (g_logMutex)
	{
		CloseHandle(g_logMutex);
		g_logMutex = NULL;
	}
}
