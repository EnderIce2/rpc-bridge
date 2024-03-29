#include <windows.h>
#include <assert.h>
#include <stdio.h>

SERVICE_STATUS g_ServiceStatus;
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;

void print(char const *fmt, ...);
void CreateBridge();
LPTSTR GetErrorMessage();

void WINAPI ServiceCtrlHandler(DWORD CtrlCode)
{
	switch (CtrlCode)
	{
	case SERVICE_CONTROL_STOP:
	case SERVICE_ACCEPT_SHUTDOWN:
	{
		g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
		SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

		print("Stopping service\n");

		/* ... */

		g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
		SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
		break;
	}
	default:
	{
		print("Unrecognized service control code %d\n", CtrlCode);
	}
	}
}

DWORD WINAPI ServiceWorkerThread(LPVOID lpParam)
{
	print("Service started\n");
	CreateBridge();
	return ERROR_SUCCESS;
}

void ServiceMain(DWORD argc, LPTSTR *argv)
{
	print("Starting service\n");
	g_StatusHandle = RegisterServiceCtrlHandler("rpc-bridge",
												ServiceCtrlHandler);
	if (g_StatusHandle == NULL)
	{
		print("Failed to register service control handler\n");
		return;
	}

	ZeroMemory(&g_ServiceStatus, sizeof(g_ServiceStatus));

	g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
	SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

	g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
	SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

	g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP |
										 SERVICE_ACCEPT_SHUTDOWN;
	g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 0;
	SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

	HANDLE hThread = CreateThread(NULL, 0, ServiceWorkerThread,
								  NULL, 0, NULL);
	WaitForSingleObject(hThread, INFINITE);

	g_ServiceStatus.dwControlsAccepted = 0;
	g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 3;
	SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

	print("Service stopped.\n");
	return;
}

void DetectDarwin()
{
	static void(CDECL * wine_get_host_version)(const char **sysname, const char **release);
	wine_get_host_version = (void *)GetProcAddress(GetModuleHandle("ntdll.dll"),
												   "wine_get_host_version");
	const char *__sysname;
	const char *__release;
	wine_get_host_version(&__sysname, &__release);
	if (strcmp(__sysname, "Darwin") == 0)
	{
		/* FIXME: I don't know how to get the TMPDIR without getenv */
		MessageBox(NULL, "Registering as a service is not supported on macOS at the moment.",
				   "Unsupported", MB_OK | MB_ICONINFORMATION);
		ExitProcess(1);
	}
}

void InstallService()
{
	print("Registering to run on startup\n");

	DetectDarwin();

	SC_HANDLE schSCManager, schService;
	DWORD dwTagId;

	schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
	if (schSCManager == NULL)
	{
		MessageBox(NULL, GetErrorMessage(),
				   NULL, MB_OK | MB_ICONSTOP);
		ExitProcess(1);
	}

	schService =
		CreateService(schSCManager,
					  "rpc-bridge", "Wine RPC Bridge",
					  SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
					  SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
					  "C:\\bridge.exe --service",
					  NULL, &dwTagId, NULL, NULL, NULL);

	if (schService == NULL)
	{
		MessageBox(NULL, GetErrorMessage(),
				   NULL, MB_OK | MB_ICONSTOP);
		ExitProcess(1);
	}
	else
	{
		char filename[MAX_PATH];
		GetModuleFileName(NULL, filename, MAX_PATH);
		CopyFile(filename, "C:\\bridge.exe", FALSE);

		print("Service installed successfully\n");
		CloseServiceHandle(schService);
	}

	CloseServiceHandle(schSCManager);
	ExitProcess(0);
}

void RemoveService()
{
	print("Unregistering from startup\n");

	SC_HANDLE schSCManager, schService;
	SERVICE_STATUS ssSvcStatus;

	schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
	if (schSCManager == NULL)
	{
		MessageBox(NULL, GetErrorMessage(),
				   NULL, MB_OK | MB_ICONSTOP);
		ExitProcess(1);
	}

	schService = OpenService(schSCManager, "rpc-bridge",
							 SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);

	if (schService == NULL)
	{
		MessageBox(NULL, GetErrorMessage(),
				   NULL, MB_OK | MB_ICONSTOP);
		ExitProcess(1);
	}

	if (ControlService(schService, SERVICE_CONTROL_STOP, &ssSvcStatus))
	{
		print("Stopping service\n");
		Sleep(1000);

		while (QueryServiceStatus(schService, &ssSvcStatus))
		{
			if (ssSvcStatus.dwCurrentState == SERVICE_STOP_PENDING)
			{
				print("Waiting for service to stop\n");
				Sleep(1000);
			}
			else
				break;
		}

		if (ssSvcStatus.dwCurrentState == SERVICE_STOPPED)
			print("Service stopped\n");
		else
			print("Service failed to stop\n");
	}

	if (!DeleteService(schService))
	{
		MessageBox(NULL, GetErrorMessage(),
				   NULL, MB_OK | MB_ICONSTOP);
		ExitProcess(1);
	}

	DeleteFile("C:\\bridge.exe");
	print("Service removed successfully\n");
	CloseServiceHandle(schService);
	CloseServiceHandle(schSCManager);
	ExitProcess(0);
}
