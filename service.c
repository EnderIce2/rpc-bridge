#include <windows.h>
#include <assert.h>
#include <stdio.h>

SERVICE_STATUS g_ServiceStatus;
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;

void print(char const *fmt, ...);
void CreateBridge();
LPTSTR GetErrorMessage();
extern BOOL IsLinux;

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

void InstallService(int ServiceStartType, LPCSTR Path)
{
	print("Registering service\n");

	if (IsLinux == FALSE)
	{
		/* FIXME: I don't know how to get the TMPDIR without getenv */
		MessageBox(NULL, "Registering as a service is not supported on macOS at the moment.",
				   "Unsupported", MB_OK | MB_ICONINFORMATION);
		ExitProcess(1);
	}

	SC_HANDLE schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
	if (schSCManager == NULL)
	{
		print("Failed to open service manager\n");
		MessageBox(NULL, GetErrorMessage(),
				   "OpenSCManager",
				   MB_OK | MB_ICONSTOP);
		ExitProcess(1);
	}

	DWORD dwTagId;
	SC_HANDLE schService = CreateService(schSCManager,
										 "rpc-bridge", "Wine RPC Bridge",
										 SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
										 ServiceStartType, SERVICE_ERROR_NORMAL,
										 Path, NULL, &dwTagId, NULL, NULL, NULL);

	if (schService == NULL)
	{
		print("Failed to create service\n");
		MessageBox(NULL, GetErrorMessage(),
				   "CreateService",
				   MB_OK | MB_ICONSTOP);
		ExitProcess(1);
	}

	print("Service installed successfully\n");
	CloseServiceHandle(schService);
	CloseServiceHandle(schSCManager);
}

void RemoveService()
{
	print("Unregistering from startup\n");

	SC_HANDLE schSCManager, schService;
	SERVICE_STATUS ssSvcStatus;

	schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (schSCManager == NULL)
	{
		MessageBox(NULL, GetErrorMessage(),
				   "OpenSCManager",
				   MB_OK | MB_ICONSTOP);
		ExitProcess(1);
	}

	schService = OpenService(schSCManager, "rpc-bridge",
							 SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);

	if (schService == NULL)
	{
		MessageBox(NULL, GetErrorMessage(),
				   "OpenService",
				   MB_OK | MB_ICONSTOP);
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
				   "DeleteService",
				   MB_OK | MB_ICONSTOP);
		ExitProcess(1);
	}

	DeleteFile("C:\\windows\\bridge.exe");
	print("Service removed successfully\n");
	CloseServiceHandle(schService);
	CloseServiceHandle(schSCManager);
}
