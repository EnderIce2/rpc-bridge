#include <windows.h>
#include <winuser.h>
#include <assert.h>
#include <stdio.h>

FILE *g_logFile = NULL;
BOOL RunningAsService = FALSE;

void CreateBridge();
void LaunchGame(int argc, char **argv);
void ServiceMain(int argc, char *argv[]);
void InstallService();
void RemoveService();

LPTSTR GetErrorMessage()
{
	DWORD err = GetLastError();
	if (err == 0)
		return "Error";

	WORD wLangID = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);

	LPSTR buffer = NULL;
	size_t size = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
									FORMAT_MESSAGE_FROM_SYSTEM |
									FORMAT_MESSAGE_IGNORE_INSERTS,
								NULL, err, wLangID, (LPSTR)&buffer, 0, NULL);

	LPTSTR message = NULL;
	if (size > 0)
	{
		message = (LPTSTR)LocalAlloc(LPTR, (size + 1) * sizeof(TCHAR));
		if (message != NULL)
		{
			memcpy(message, buffer, size * sizeof(TCHAR));
			message[size] = '\0';
		}
		LocalFree(buffer);
	}

	return message;
}

void DetectWine()
{
	HMODULE hNTdll = GetModuleHandle("ntdll.dll");
	if (!hNTdll)
		ExitProcess(1);
	if (!GetProcAddress(hNTdll, "wine_get_version"))
	{
		MessageBox(NULL, "This program is only intended to run under Wine.",
				   GetErrorMessage(), MB_OK | MB_ICONINFORMATION);
		ExitProcess(1);
	}
}

void print(char const *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
	va_start(args, fmt);
	vfprintf(g_logFile, fmt, args);
	va_end(args);
}

int main(int argc, char *argv[])
{
	DetectWine();
	char *logFilePath = "C:\\bridge.log";
	g_logFile = fopen(logFilePath, "w");
	if (g_logFile == NULL)
	{
		printf("Failed to open logs file: %ld\n",
			   GetLastError());
		ExitProcess(1);
	}

	if (argc > 1)
	{
		if (strcmp(argv[1], "--service") == 0)
		{
			RunningAsService = TRUE;
			print("Running as service\n");

			SERVICE_TABLE_ENTRY ServiceTable[] =
				{
					{"rpc-bridge", (LPSERVICE_MAIN_FUNCTION)ServiceMain},
					{NULL, NULL},
				};

			if (StartServiceCtrlDispatcher(ServiceTable) == FALSE)
			{
				print("Service failed to start\n");
				return GetLastError();
			}
			return 0;
		}

		if (strcmp(argv[1], "--install") == 0)
			InstallService();
		else if (strcmp(argv[1], "--uninstall") == 0)
			RemoveService();
	}

	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)CreateBridge,
				 NULL, 0, NULL);
	Sleep(500);
	LaunchGame(argc, argv);

	fclose(g_logFile);
	ExitProcess(0);
}
