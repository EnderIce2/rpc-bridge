#include <windows.h>
#include <winuser.h>
#include <assert.h>
#include <stdio.h>

#include "resource.h"

BOOL RunningAsService = FALSE;

static const char *(CDECL *wine_get_version)(void);
static const char *(CDECL *wine_get_build_id)(void);
static void(CDECL *wine_get_host_version)(const char **sysname, const char **release);

void LogInit();
void LogClose();
void print(char const *fmt, ...);
void CreateGUI();
void CreateBridge();
void LaunchGame(int argc, char **argv);
void ServiceMain(int argc, char *argv[]);
void InstallService(int ServiceStartType, LPCSTR Path);
char *getenv_custom(const char *name);
void RemoveService();
extern BOOL IsLinux;

/* this because atexit() doesn't work with ExitProcess */
VOID ExitBridge(UINT uExitCode)
{
	LogClose();
	ExitProcess(uExitCode);
}

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
		print("Failed to load ntdll.dll: %s\n", GetErrorMessage());
		ExitBridge(1);
	}

	if (!GetProcAddress(hNTdll, "wine_get_version"))
	{
		print("This program is only intended to run under Wine.\n");
		MessageBox(NULL, "This program is only intended to run under Wine.",
				   "Error", MB_OK | MB_ICONINFORMATION);
		ExitBridge(1);
	}

	/* https://gitlab.winehq.org/wine/wine/-/blob/master/dlls/ntdll/version.c#L215-248 */
	wine_get_version = GetProcAddress(hNTdll, "wine_get_version");
	wine_get_build_id = GetProcAddress(hNTdll, "wine_get_build_id");
	wine_get_host_version = GetProcAddress(hNTdll, "wine_get_host_version");
	assert(wine_get_version && wine_get_build_id && wine_get_host_version);

	const char *__sysname;
	const char *__release;
	wine_get_host_version(&__sysname, &__release);
	if (strcmp(__sysname, "Linux") != 0 && strcmp(__sysname, "Darwin") != 0)
	{
		print("Running on unsupported platform \"%s\" \"%s\"\n", __sysname, __release);
		int result = MessageBox(NULL, "This program is designed for Linux and macOS only!\nDo you want to proceed?",
								NULL, MB_YESNO | MB_ICONQUESTION);
		if (result == IDNO)
			ExitBridge(1);
	}

	IsLinux = strcmp(__sysname, "Linux") == 0;
}

void HandleArguments(int argc, char *argv[])
{
	if (argc <= 1)
		return;

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
			ExitBridge(1);
		}
	}
	else if (strcmp(argv[1], "--steam") == 0)
	{
		/* All this mess just so when you close the game,
			it automatically closes the bridge and Steam
			will not say that the game is still running. */

		print("Running as Steam\n");
		if (IsLinux == FALSE)
			CreateBridge();

		if (argc > 2)
		{
			if (strcmp(argv[2], "--no-service") == 0)
				CreateBridge();
		}

		SC_HANDLE hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
		if (hSCManager == NULL)
		{
			print("(Steam) OpenSCManager: %s\n", GetErrorMessage());
			ExitBridge(1);
		}

		SC_HANDLE schService = OpenService(hSCManager, "rpc-bridge",
										   SERVICE_QUERY_CONFIG | SERVICE_CHANGE_CONFIG | SERVICE_START);
		if (schService == NULL)
		{
			if (GetLastError() != ERROR_SERVICE_DOES_NOT_EXIST)
			{
				print("(Steam) OpenService: %s\n", GetErrorMessage());
				ExitBridge(1);
			}

			print("(Steam) Service does not exist, registering...\n");

			WCHAR *(CDECL * wine_get_dos_file_name)(LPCSTR str) =
				(void *)GetProcAddress(GetModuleHandleA("KERNEL32"),
									   "wine_get_dos_file_name");

			char *unixPath = getenv_custom("BRIDGE_PATH");
			if (unixPath == NULL)
			{
				print("(Steam) BRIDGE_PATH not set\n");
				ExitBridge(1);
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
				ExitBridge(1);
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
				ExitBridge(1);
			}

			if (!QueryServiceConfig(schService, lpqsc, dwBytesNeeded, &dwBytesNeeded))
			{
				print("(Steam) QueryServiceConfig: %s\n", GetErrorMessage());
				ExitBridge(1);
			}

			WCHAR *(CDECL * wine_get_dos_file_name)(LPCSTR str) =
				(void *)GetProcAddress(GetModuleHandleA("KERNEL32"),
									   "wine_get_dos_file_name");

			char *unixPath = getenv_custom("BRIDGE_PATH");
			if (unixPath == NULL)
			{
				print("(Steam) BRIDGE_PATH not set\n");
				ExitBridge(1);
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

		print("(Steam) Starting service and then exiting...\n");

		if (StartService(schService, 0, NULL) != FALSE)
		{
			CloseServiceHandle(schService);
			CloseServiceHandle(hSCManager);
			ExitBridge(0);
		}
		else if (GetLastError() == ERROR_SERVICE_ALREADY_RUNNING)
		{
			CloseServiceHandle(schService);
			CloseServiceHandle(hSCManager);
			ExitBridge(0);
		}

		MessageBox(NULL, GetErrorMessage(),
				   "StartService",
				   MB_OK | MB_ICONSTOP);
		ExitBridge(1);
	}
	else if (strcmp(argv[1], "--install") == 0)
	{
		char filename[MAX_PATH];
		GetModuleFileName(NULL, filename, MAX_PATH);
		CopyFile(filename, "C:\\windows\\bridge.exe", FALSE);

		InstallService(SERVICE_AUTO_START, "C:\\windows\\bridge.exe --service");
		ExitBridge(0);
	}
	else if (strcmp(argv[1], "--uninstall") == 0)
	{
		RemoveService();
		ExitBridge(0);
	}
	else if (strcmp(argv[1], "--rpc") == 0)
	{
		if (argc < 3)
		{
			print("No directory provided\n");
			ExitBridge(1);
		}

		SetEnvironmentVariable("BRIDGE_RPC_PATH", argv[2]);
		print("BRIDGE_RPC_PATH has been set to \"%s\"\n", argv[2]);
		CreateBridge();
		ExitBridge(0);
	}
	else if (strcmp(argv[1], "--help") == 0)
	{
		print("Usage:\n"
			  "  %s [args]\n"
			  "\n"
			  "Arguments:\n"
			  "  --help         Show this help\n"
			  "\n"
			  "  --version      Show version\n"
			  "\n"
			  "  --install      Install service\n"
			  "        Copy the binary to C:\\windows\\bridge.exe and register it as a service\n"
			  "\n"
			  "  --uninstall    Uninstall service\n"
			  "        Remove the service and delete C:\\windows\\bridge.exe\n"
			  "\n"
			  "  --steam        Reserved for Steam\n"
			  "        Start the service and exit (used with bridge.sh)\n"
			  "\n"
			  "  --no-service   Do not run as service\n"
			  "        (only for --steam)\n"
			  "\n"
			  "  --service      Reserved for service\n"
			  "\n"
			  "  --rpc <dir>    Set RPC_PATH environment variable\n"
			  "        Used to specify the directory where 'discord-ipc-0' is located\n"
			  "\n"
			  "Note: If no arguments are provided, the GUI will be shown instead\n",
			  argv[0]);
		ExitBridge(0);
	}
}

int main(int argc, char *argv[])
{
	if (argc > 1 && strcmp(argv[1], "--version") == 0)
	{
		printf("%s-%s\n", VER_VERSION_STR, GIT_COMMIT);
		ExitBridge(0);
	}

	LogInit();
	DetectWine();

	print("rpc-bridge v%s %s-%s %s %s\n",
		  VER_VERSION_STR, GIT_BRANCH, GIT_COMMIT,
		  IsLinux ? "Linux" : "macOS", wine_get_build_id());

	HandleArguments(argc, argv);
	CreateGUI();

	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)CreateBridge,
				 NULL, 0, NULL);
	Sleep(500);
	LaunchGame(argc, argv);
	ExitBridge(0);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	return main(__argc, __argv);
}
