#include <windows.h>
#include <winuser.h>
#include <assert.h>
#include <stdio.h>

#include "bridge.h"
#include "resource.h"

FILE *g_logFile = NULL;
BOOL RunningAsService = FALSE;
char *logFilePath = NULL;

void CreateGUI();
void CreateBridge();
void LaunchGame(int argc, char **argv);
void ServiceMain(int argc, char *argv[]);
void InstallService(int ServiceStartType, LPCSTR Path);
char *native_getenv(const char *name);
void RemoveService();
extern OS_INFO OSInfo;

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
	{
		MessageBox(NULL, "Failed to load ntdll.dll",
				   GetErrorMessage(), MB_OK | MB_ICONERROR);
		ExitProcess(1);
	}

	if (!GetProcAddress(hNTdll, "wine_get_version"))
		return;
	OSInfo.IsWine = TRUE;

	static void(CDECL * wine_get_host_version)(const char **sysname, const char **release);
	wine_get_host_version = (void *)GetProcAddress(hNTdll, "wine_get_host_version");

	assert(wine_get_host_version);
	const char *__sysname;
	const char *__release;
	wine_get_host_version(&__sysname, &__release);
	if (strcmp(__sysname, "Linux") != 0 && strcmp(__sysname, "Darwin") != 0)
	{
		int result = MessageBox(NULL, "This program is designed for Linux and macOS only!\nDo you want to proceed?",
								NULL, MB_YESNO | MB_ICONQUESTION);
		if (result == IDNO)
			ExitProcess(1);
	}

	OSInfo.IsLinux = strcmp(__sysname, "Linux") == 0;
	OSInfo.IsDarwin = strcmp(__sysname, "Darwin") == 0;
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

void HandleArguments(int argc, char *argv[])
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
			ExitProcess(1);
		}
	}
	else if (strcmp(argv[1], "--steam") == 0)
	{
		assert(OSInfo.IsWine == TRUE);

		/* All this mess just so when you close the game,
			it automatically closes the bridge and Steam
			will not say that the game is still running. */

		print("Running as Steam\n");
		if (argc > 2)
		{
			if (strcmp(argv[2], "--no-service") == 0)
				CreateBridge();
		}

		SC_HANDLE hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
		if (hSCManager == NULL)
		{
			print("(Steam) OpenSCManager: %s\n", GetErrorMessage());
			ExitProcess(1);
		}

		SC_HANDLE schService = OpenService(hSCManager, "rpc-bridge",
										   SERVICE_QUERY_CONFIG | SERVICE_CHANGE_CONFIG | SERVICE_START);
		if (schService == NULL)
		{
			if (GetLastError() != ERROR_SERVICE_DOES_NOT_EXIST)
			{
				print("(Steam) OpenService: %s\n", GetErrorMessage());
				ExitProcess(1);
			}

			print("(Steam) Service does not exist, registering...\n");

			WCHAR *(CDECL * wine_get_dos_file_name)(LPCSTR str) =
				(void *)GetProcAddress(GetModuleHandleA("KERNEL32"),
									   "wine_get_dos_file_name");

			char *unixPath = native_getenv("BRIDGE_PATH");
			if (unixPath == NULL)
			{
				print("(Steam) BRIDGE_PATH not set\n");
				ExitProcess(1);
			}
			WCHAR *dosPath = wine_get_dos_file_name(unixPath);
			LPSTR asciiPath = (LPSTR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, MAX_PATH);
			WideCharToMultiByte(CP_ACP, 0, dosPath, -1, asciiPath, MAX_PATH, NULL, NULL);

			strcat_s(asciiPath, MAX_PATH, " --service");
			print("(Steam) Binary path: %s\n", asciiPath);

			InstallService(SERVICE_DEMAND_START, asciiPath);
			HeapFree(GetProcessHeap(), 0, asciiPath);

			/* Create handle for StartService below */
			print("(Steam) Service registered, opening handle...\n");
			/* FIXME: For some reason here it freezes??? */
			schService = OpenService(hSCManager, "rpc-bridge", SERVICE_START);
			if (schService == NULL)
			{
				print("(Steam) Cannot open service after creation: %s\n", GetErrorMessage());
				ExitProcess(1);
			}
		}
		else
		{
			DWORD dwBytesNeeded;
			QueryServiceConfig(schService, NULL, 0, &dwBytesNeeded);
			LPQUERY_SERVICE_CONFIG lpqsc = (LPQUERY_SERVICE_CONFIG)LocalAlloc(LPTR, dwBytesNeeded);
			if (lpqsc == NULL)
			{
				print("(Steam) LocalAlloc: %s\n", GetErrorMessage());
				ExitProcess(1);
			}

			if (!QueryServiceConfig(schService, lpqsc, dwBytesNeeded, &dwBytesNeeded))
			{
				print("(Steam) QueryServiceConfig: %s\n", GetErrorMessage());
				ExitProcess(1);
			}

			WCHAR *(CDECL * wine_get_dos_file_name)(LPCSTR str) =
				(void *)GetProcAddress(GetModuleHandleA("KERNEL32"),
									   "wine_get_dos_file_name");

			char *unixPath = native_getenv("BRIDGE_PATH");
			if (unixPath == NULL)
			{
				print("(Steam) BRIDGE_PATH not set\n");
				ExitProcess(1);
			}
			WCHAR *dosPath = wine_get_dos_file_name(unixPath);
			LPSTR asciiPath = (LPSTR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, MAX_PATH);
			WideCharToMultiByte(CP_ACP, 0, dosPath, -1, asciiPath, MAX_PATH, NULL, NULL);

			strcat_s(asciiPath, MAX_PATH, " --service");
			print("(Steam) Binary path: %s\n", asciiPath);

			if (strcmp(lpqsc->lpBinaryPathName, asciiPath) != 0)
			{
				print("(Steam) Service binary path is not correct, updating...\n");
				ChangeServiceConfig(schService, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE,
									asciiPath, NULL, NULL, NULL, NULL, NULL, NULL);
			}
			else
				print("(Steam) Service binary path is correct\n");
			HeapFree(GetProcessHeap(), 0, asciiPath);
		}

		print("(Steam) Starting service...\n");
		if (StartService(schService, 0, NULL) == FALSE)
		{
			if (GetLastError() == ERROR_SERVICE_ALREADY_RUNNING)
			{
				print("(Steam) Service is already running\n");
				CloseServiceHandle(schService);
				CloseServiceHandle(hSCManager);
				ExitProcess(0);
			}

			print("StartService: %s\n", GetErrorMessage());
			MessageBox(NULL, GetErrorMessage(),
					   "StartService",
					   MB_OK | MB_ICONSTOP);
			ExitProcess(1);
		}

		print("(Steam) Service started successfully, exiting...\n");
		CloseServiceHandle(schService);
		CloseServiceHandle(hSCManager);
		ExitProcess(0);
	}
	else if (strcmp(argv[1], "--install") == 0)
	{
		char filename[MAX_PATH];
		GetModuleFileName(NULL, filename, MAX_PATH);
		CopyFile(filename, "C:\\windows\\bridge.exe", FALSE);

		InstallService(SERVICE_AUTO_START, "C:\\windows\\bridge.exe --service");
		ExitProcess(0);
	}
	else if (strcmp(argv[1], "--uninstall") == 0)
	{
		RemoveService();
		ExitProcess(0);
	}
	else if (strcmp(argv[1], "--rpc") == 0)
	{
		if (argc < 3)
		{
			print("No directory provided\n");
			ExitProcess(1);
		}

		SetEnvironmentVariable("BRIDGE_RPC_PATH", argv[2]);
		print("BRIDGE_RPC_PATH has been set to \"%s\"\n", argv[2]);
		CreateBridge();
		ExitProcess(0);
	}
	else if (strcmp(argv[1], "--version") == 0)
	{
		printf("%s\n", VER_VERSION_STR);
		ExitProcess(0);
	}
	else if (strcmp(argv[1], "--help") == 0)
	{
		printf("Usage:\n"
			   "  %s [args]\n"
			   "\n"
			   "Arguments:\n"
			   "  --help         Show this help\n"
			   "\n"
			   "  --version      Show version\n"
			   "\n"
			   "  --install      Install service\n"
			   "        This will copy the binary to C:\\windows\\bridge.exe and register it as a service\n"
			   "\n"
			   "  --uninstall    Uninstall service\n"
			   "        This will remove the service and delete C:\\windows\\bridge.exe\n"
			   "\n"
			   "  --steam        Reserved for Steam\n"
			   "        This will start the service and exit (used with bridge.sh)\n"
			   "\n"
			   "  --no-service   Do not run as service\n"
			   "        (only for --steam)\n"
			   "\n"
			   "  --service      Reserved for service\n"
			   "\n"
			   "  --rpc <dir>    Set RPC_PATH environment variable\n"
			   "        This is used to specify the directory where 'discord-ipc-0' is located\n"
			   "\n"
			   "Note: If no arguments are provided, the GUI will be shown instead\n",
			   argv[0]);
		ExitProcess(0);
	}
}

int main(int argc, char *argv[])
{
	DetectWine();
	if (OSInfo.IsWine)
		logFilePath = "C:\\windows\\logs\\bridge.log";
	else
	{
		logFilePath = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, MAX_PATH);
		GetTempPath(MAX_PATH, logFilePath);
		strcat_s(logFilePath, MAX_PATH, "bridge.log");
	}
	g_logFile = fopen(logFilePath, "w");
	if (g_logFile == NULL)
	{
		printf("Failed to open logs file: %ld\n",
			   GetLastError());
		ExitProcess(1);
	}

	if (argc > 1)
		HandleArguments(argc, argv);
	else
		CreateGUI();

	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)CreateBridge,
				 NULL, 0, NULL);
	Sleep(500);
	LaunchGame(argc, argv);

	fclose(g_logFile);
	ExitProcess(0);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	return main(__argc, __argv);
}
