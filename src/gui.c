#include <windowsx.h>
#include <windows.h>
#include <winuser.h>
#include <assert.h>
#include <stdio.h>

#include "resource.h"

/**
 * The entire code could be better written, but at least it works.
 *
 * This will make installation and removal of the bridge WAY easier.
 */

VOID ExitBridge(UINT uExitCode);
LPTSTR GetErrorMessage();
void print(char const *fmt, ...);
void InstallService(int ServiceStartType, LPCSTR Path);
void RemoveService();
void CreateBridge();
extern BOOL IsLinux;

HWND hwnd = NULL;
HANDLE hBridge = NULL;
BOOL gIsUpdate = FALSE;
extern HANDLE hOut;
extern HANDLE hIn;

typedef struct
{
	WORD major, minor, patch, build;
} Version;

typedef enum
{
	BRIDGE_NOT_INSTALLED = 0,
	BRIDGE_INSTALLED,
	BRIDGE_INSTALLED_NEWER,
	BRIDGE_INSTALLED_OLDER,
} BRIDGE_UPDATER;

Version GetFileVersion(const char *path)
{
	Version v = {0};
	DWORD dwHandle;
	DWORD dwSize = GetFileVersionInfoSize(path, &dwHandle);
	if (dwSize == 0)
		return v;

	void *pData = LocalAlloc(LPTR, dwSize);
	if (!pData)
		return v;

	if (GetFileVersionInfo(path, dwHandle, dwSize, pData))
	{
		VS_FIXEDFILEINFO *pInfo;
		UINT uLen;
		if (VerQueryValue(pData, "\\", (void **)&pInfo, &uLen))
		{
			v.major = HIWORD(pInfo->dwFileVersionMS);
			v.minor = LOWORD(pInfo->dwFileVersionMS);
			v.patch = HIWORD(pInfo->dwFileVersionLS);
			v.build = LOWORD(pInfo->dwFileVersionLS);
		}
	}
	LocalFree(pData);
	return v;
}

BRIDGE_UPDATER CheckInstalledVersion()
{
	if (GetFileAttributes("C:\\windows\\bridge.exe") == INVALID_FILE_ATTRIBUTES)
		return BRIDGE_NOT_INSTALLED;

	Version installed = GetFileVersion("C:\\windows\\bridge.exe");
	if (installed.major == 0 && installed.minor == 0)
		return BRIDGE_INSTALLED_OLDER;

	char currentPath[MAX_PATH];
	GetModuleFileName(NULL, currentPath, MAX_PATH);
	Version current = GetFileVersion(currentPath);

	print("Installed version: %d.%d.%d.%d\n", installed.major, installed.minor, installed.patch, installed.build);
	print("Current version:   %d.%d.%d.%d\n", current.major, current.minor, current.patch, current.build);

	ULONGLONG inst = ((ULONGLONG)installed.major << 48) | ((ULONGLONG)installed.minor << 32) | ((ULONGLONG)installed.patch << 16) | (ULONGLONG)installed.build;
	ULONGLONG curr = ((ULONGLONG)current.major << 48) | ((ULONGLONG)current.minor << 32) | ((ULONGLONG)current.patch << 16) | (ULONGLONG)current.build;

	if (inst < curr)
		return BRIDGE_INSTALLED_OLDER;
	else if (inst > curr)
		return BRIDGE_INSTALLED_NEWER;
	return BRIDGE_INSTALLED;
}

VOID HandleStartButton(BOOL Silent)
{
	static BOOL IsAlreadyRunning = FALSE;
	if (IsAlreadyRunning)
	{
		HWND item = GetDlgItem(hwnd, 4);
		SetWindowText(item, "Do you want to start, install or remove the bridge?");
		RedrawWindow(item, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
		item = GetDlgItem(hwnd, /* Start Button */ 1);
		Button_SetText(item, "&Start");
		EnableWindow(item, FALSE);
		RedrawWindow(item, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);

		print("Killing %#x, %#lx and waiting for %#lx\n", hIn, hOut, hBridge);
		if (hIn != NULL)
			TerminateThread(hIn, 0);
		if (hOut != NULL)
			TerminateThread(hOut, 0);
		WaitForSingleObject(hBridge, INFINITE);

		EnableWindow(item, TRUE);
		IsAlreadyRunning = FALSE;
		return;
	}

	SC_HANDLE hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (hSCManager == NULL)
	{
		print("OpenSCManager failed: %s\n", GetErrorMessage());
		return;
	}

	SC_HANDLE schService = OpenService(hSCManager, "rpc-bridge",
									   SERVICE_QUERY_CONFIG | SERVICE_CHANGE_CONFIG | SERVICE_START);
	if (schService == NULL)
	{
		print("Service doesn't exist\n");
		CloseServiceHandle(hSCManager);

		/* Service doesn't exist; running without any service */

		hBridge = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)CreateBridge,
							   NULL, 0, NULL);

		HWND item = GetDlgItem(hwnd, /* Start Button */ 1);
		Button_SetText(item, "&Stop");

		/* FIXME: the Stop procedure is broken! There should be a stop procedure and not just killing threads. */
		EnableWindow(item, FALSE);
		item = GetDlgItem(hwnd, 4);
		SetWindowText(item, "Bridge is running...");
		IsAlreadyRunning = TRUE;
		ShowWindow(hwnd, SW_MINIMIZE);
		return;
	}

	DWORD dwBytesNeeded;
	QueryServiceConfig(schService, NULL, 0, &dwBytesNeeded);
	LPQUERY_SERVICE_CONFIG lpqsc = (LPQUERY_SERVICE_CONFIG)LocalAlloc(LPTR, dwBytesNeeded);
	if (lpqsc == NULL)
	{
		print("LocalAlloc failed: %s\n", GetErrorMessage());
		CloseServiceHandle(schService);
		CloseServiceHandle(hSCManager);
		return;
	}

	if (!QueryServiceConfig(schService, lpqsc, dwBytesNeeded, &dwBytesNeeded))
	{
		print("QueryServiceConfig failed: %s\n", GetErrorMessage());
		CloseServiceHandle(schService);
		CloseServiceHandle(hSCManager);
		return;
	}

	if (StartService(schService, 0, NULL) == FALSE)
	{
		if (GetLastError() == ERROR_SERVICE_ALREADY_RUNNING)
		{
			CloseServiceHandle(schService);
			CloseServiceHandle(hSCManager);
			return;
		}
		print("StartService failed: %s\n", GetErrorMessage());
	}

	LocalFree(lpqsc);
	CloseServiceHandle(schService);
	CloseServiceHandle(hSCManager);
	if (Silent == FALSE)
		MessageBox(NULL, "Bridge service started successfully", "Info", MB_OK);
	print("Bridge service started successfully\n");
}

VOID HandleInstallButton()
{
	char currentPath[MAX_PATH];
	GetModuleFileName(NULL, currentPath, MAX_PATH);

	if (gIsUpdate)
	{
		RemoveService();

		if (!CopyFile(currentPath, "C:\\windows\\bridge.exe", FALSE))
		{
			print("CopyFile failed: %s\n", GetErrorMessage());
			MessageBox(NULL, "Failed to copy bridge executable", "Error", MB_OK | MB_ICONSTOP);
			return;
		}

		InstallService(SERVICE_AUTO_START, "C:\\windows\\bridge.exe --service");
		MessageBox(NULL, "Bridge updated successfully", "Info", MB_OK);
	}
	else
	{
		if (!CopyFile(currentPath, "C:\\windows\\bridge.exe", FALSE))
		{
			print("CopyFile failed: %s\n", GetErrorMessage());
			MessageBox(NULL, "Failed to copy bridge executable", "Error", MB_OK | MB_ICONSTOP);
			return;
		}

		InstallService(SERVICE_AUTO_START, "C:\\windows\\bridge.exe --service");
		MessageBox(NULL, "Bridge installed successfully", "Info", MB_OK);
	}

	HandleStartButton(TRUE);
	ExitBridge(0);
}

VOID HandleRemoveButton()
{
	RemoveService();
	if (DeleteFile("C:\\windows\\bridge.exe"))
	{
		MessageBox(NULL, "Bridge removed successfully", "Info", MB_OK);
		ExitBridge(0);
	}

	DWORD err = GetLastError();
	print("DeleteFile failed (%d), trying delayed delete\n", err);

	if (MoveFileEx("C:\\windows\\bridge.exe", NULL, MOVEFILE_DELAY_UNTIL_REBOOT))
	{
		MessageBox(NULL,
				   "Bridge service removed successfully.\n"
				   "The bridge executable will be deleted on prefix restart.",
				   "Info", MB_OK);
		ExitBridge(0);
	}

	print("MoveFileEx failed: %s\n", GetErrorMessage());
	MessageBox(NULL,
			   "Bridge service was removed, but the executable could not be deleted.\n"
			   "You can delete C:\\windows\\bridge.exe manually.",
			   "Warning", MB_OK | MB_ICONWARNING);
	ExitBridge(0);

	ExitBridge(0);
}

void ShowLicenseDialog()
{
	HMODULE hModule = GetModuleHandle(NULL);
	HRSRC hRes = FindResource(hModule, MAKEINTRESOURCE(IDR_LICENSE_TXT), RT_RCDATA);
	if (!hRes)
	{
		MessageBox(NULL, "Resource not found", "Error", MB_OK | MB_ICONERROR);
		return;
	}

	HGLOBAL hResData = LoadResource(NULL, hRes);
	if (!hResData)
	{
		MessageBox(NULL, "Resource failed to load", "Error", MB_OK | MB_ICONERROR);
		return;
	}

	DWORD resSize = SizeofResource(NULL, hRes);
	void *pRes = LockResource(hResData);
	if (!pRes)
	{
		MessageBox(NULL, "Resource failed to lock", "Error", MB_OK | MB_ICONERROR);
		return;
	}

	char *licenseText = (char *)malloc(resSize + 1);
	if (!licenseText)
	{
		MessageBox(NULL, "Memory allocation failed", "Error", MB_OK | MB_ICONERROR);
		return;
	}

	memcpy(licenseText, pRes, resSize);
	licenseText[resSize] = '\0';
	MessageBoxA(hwnd, licenseText, "About", MB_OK);
	free(licenseText);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_COMMAND:
	{
		switch (LOWORD(wParam))
		{
		case 1:
			HandleStartButton(FALSE);
			break;
		case 2:
			HandleInstallButton();
			break;
		case 3:
			HandleRemoveButton();
			break;
		case IDM_VIEW_LOG:
			ShellExecute(NULL, "open", "C:\\windows\\notepad.exe", "C:\\windows\\logs\\bridge.log", NULL, SW_SHOW);
			break;
		case IDM_HELP_DOCUMENTATION:
			ShellExecute(NULL, "open", "https://enderice2.github.io/rpc-bridge/index.html", NULL, NULL, SW_SHOWNORMAL);
			break;
		case IDM_HELP_LICENSE:
			ShowLicenseDialog();
			break;
		case IDM_HELP_ABOUT:
		{
			char msg[256];
			snprintf(msg, sizeof(msg),
					 "rpc-bridge v%s\n"
					 "  branch: %s\n"
					 "  commit: %s\n\n"
					 "Simple bridge that allows you to use Discord Rich Presence with Wine games/software.\n\n"
					 "Created by EnderIce2\n\n"
					 "Licensed under the MIT License",
					 VER_VERSION_STR, GIT_BRANCH, GIT_COMMIT);
			MessageBox(NULL, msg, "About", MB_OK);
			break;
		}
		default:
			break;
		}
		break;
	}
	case WM_CLOSE:
		DestroyWindow(hwnd);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		ExitBridge(0);
		break;
	case WM_CTLCOLORSTATIC:
	{
		HDC hdcStatic = (HDC)wParam;
		SetBkMode(hdcStatic, TRANSPARENT);
		return (INT_PTR)(HBRUSH)GetStockObject(NULL_BRUSH);
	}
	default:
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	return 0;
}

VOID SetButtonStyles(INT *btnStartStyle, INT *btnRemoveStyle, INT *btnInstallStyle, BOOL *bIsUpdate)
{
	*btnStartStyle = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP;
	*btnRemoveStyle = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP;
	*btnInstallStyle = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP;
	*bIsUpdate = FALSE;

	int installed = CheckInstalledVersion();
	SC_HANDLE hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	SC_HANDLE schService = OpenService(hSCManager, "rpc-bridge",
									   SERVICE_START | SERVICE_QUERY_STATUS);
	BOOL serviceExists = (schService != NULL);

	if (installed == BRIDGE_INSTALLED_OLDER)
	{
		/* Update available — always allow */
		*bIsUpdate = TRUE;
	}
	else if (installed == BRIDGE_INSTALLED || installed == BRIDGE_INSTALLED_NEWER)
	{
		if (serviceExists)
			*btnInstallStyle |= WS_DISABLED;
	}

	if (serviceExists)
	{
		SERVICE_STATUS_PROCESS ssStatus;
		DWORD dwBytesNeeded;
		assert(QueryServiceStatusEx(schService, SC_STATUS_PROCESS_INFO,
									(LPBYTE)&ssStatus,
									sizeof(SERVICE_STATUS_PROCESS), &dwBytesNeeded));

		if (ssStatus.dwCurrentState == SERVICE_RUNNING ||
			ssStatus.dwCurrentState == SERVICE_START_PENDING)
			*btnStartStyle |= WS_DISABLED;
	}
	else
	{
		*btnRemoveStyle |= WS_DISABLED; /* no service = nothing to remove */
	}

	if (schService != NULL)
		CloseServiceHandle(schService);
	CloseServiceHandle(hSCManager);
}

int WINAPI __WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
					 LPSTR lpCmdLine, int nCmdShow)
{
	INT btnStartStyle, btnRemoveStyle, btnInstallStyle;
	SetButtonStyles(&btnStartStyle, &btnRemoveStyle, &btnInstallStyle, &gIsUpdate);

	const char szClassName[] = "BridgeWindowClass";

	WNDCLASSEX wc;
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = 0;
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(NULL, IDI_WINLOGO);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = szClassName;
	wc.hIconSm = LoadIcon(NULL, IDI_WINLOGO);

	assert(RegisterClassEx(&wc));

	hwnd = CreateWindowEx(WS_EX_WINDOWEDGE,
						  szClassName,
						  "Discord RPC Bridge",
						  WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME,
						  (GetSystemMetrics(SM_CXSCREEN) - 400) / 2,
						  (GetSystemMetrics(SM_CYSCREEN) - 150) / 2,
						  400, 150,
						  NULL, NULL, hInstance, NULL);

	HICON hIcon = LoadIcon(hInstance, "IDI_ICON_128");
	SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);

	HWND hLbl4 = CreateWindowEx(WS_EX_TRANSPARENT,
								"STATIC", "Do you want to start, install or remove the bridge?",
								WS_CHILD | WS_VISIBLE | SS_CENTER,
								0, 15, 400, 25,
								hwnd, (HMENU)4, hInstance, NULL);

	HWND hbtn1 = CreateWindow("BUTTON", "&Start",
							  btnStartStyle,
							  40, 60, 100, 30,
							  hwnd, (HMENU)1, hInstance, NULL);

	HWND hbtn2 = CreateWindow("BUTTON", "&Install",
							  btnInstallStyle,
							  150, 60, 100, 30,
							  hwnd, (HMENU)2, hInstance, NULL);

	HWND hbtn3 = CreateWindow("BUTTON", "&Remove",
							  btnRemoveStyle,
							  260, 60, 100, 30,
							  hwnd, (HMENU)3, hInstance, NULL);

	HMENU hMenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDR_MAINMENU));
	SetMenu(hwnd, hMenu);

	HDC hDC = GetDC(hwnd);
	int nHeight = -MulDiv(11, GetDeviceCaps(hDC, LOGPIXELSY), 72);

	HFONT hFont = CreateFont(nHeight, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, ANSI_CHARSET,
							 OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
							 DEFAULT_PITCH | FF_DONTCARE, TEXT("Segoe UI"));
	ReleaseDC(hwnd, hDC);

	SendMessage(hwnd, WM_SETFONT, hFont, TRUE);
	SendMessage(hLbl4, WM_SETFONT, hFont, TRUE);
	SendMessage(hbtn1, WM_SETFONT, hFont, TRUE);
	SendMessage(hbtn2, WM_SETFONT, hFont, TRUE);
	SendMessage(hbtn3, WM_SETFONT, hFont, TRUE);

	if (gIsUpdate)
		SetWindowText(hbtn2, "&Update");

	ShowWindow(hwnd, nCmdShow);
	UpdateWindow(hwnd);

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0) > 0)
	{
		if (!IsDialogMessage(hwnd, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return msg.wParam;
}

void CreateGUI()
{
	ShowWindow(GetConsoleWindow(), SW_MINIMIZE);
	ExitBridge(__WinMain(GetModuleHandle(NULL), NULL, GetCommandLine(), SW_SHOWNORMAL));
}
