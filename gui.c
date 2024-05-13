#include <windowsx.h>
#include <windows.h>
#include <winuser.h>
#include <assert.h>
#include <stdio.h>

/**
 * The entire code could be better written, but at least it works.
 *
 * This will make installation and removal of the bridge WAY easier.
 */

LPTSTR GetErrorMessage();
void print(char const *fmt, ...);
void InstallService(int ServiceStartType, LPCSTR Path);
void RemoveService();
void CreateBridge();
extern BOOL IsLinux;

HWND hwnd = NULL;
HANDLE hBridge = NULL;
extern HANDLE hOut;
extern HANDLE hIn;

BOOL IsAlreadyRunning = FALSE;
VOID HandleStartButton(BOOL Silent)
{
	if (IsAlreadyRunning)
	{
		HWND item = GetDlgItem(hwnd, 4);
		SetWindowText(item, "Do you want to start, install or remove the bridge?");
		RedrawWindow(item, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
		item = GetDlgItem(hwnd, /* Start Button */ 1);
		Button_SetText(item, "Start");
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

	if (!IsLinux)
	{
		hBridge = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)CreateBridge,
							   NULL, 0, NULL);

		HWND item = GetDlgItem(hwnd, /* Start Button */ 1);
		Button_SetText(item, "Stop");
		item = GetDlgItem(hwnd, 4);
		SetWindowText(item, "Bridge is running...");
		IsAlreadyRunning = TRUE;
		ShowWindow(hwnd, SW_MINIMIZE);
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
		// print("OpenService failed: %s\n", GetErrorMessage());

		/* Service doesn't exist; running without any service */

		hBridge = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)CreateBridge,
							   NULL, 0, NULL);

		HWND item = GetDlgItem(hwnd, /* Start Button */ 1);
		Button_SetText(item, "Stop");
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
		return;
	}

	if (!QueryServiceConfig(schService, lpqsc, dwBytesNeeded, &dwBytesNeeded))
	{
		print("QueryServiceConfig failed: %s\n", GetErrorMessage());
		return;
	}

	if (StartService(schService, 0, NULL) == FALSE)
	{
		if (GetLastError() == ERROR_SERVICE_ALREADY_RUNNING)
			return;
		print("StartService failed: %s\n", GetErrorMessage());
	}

	LocalFree(lpqsc);
	CloseServiceHandle(schService);
	CloseServiceHandle(hSCManager);
	if (Silent == FALSE)
		MessageBox(NULL, "Bridge service started successfully", "Info", MB_OK);
}

VOID HandleInstallButton()
{
	char filename[MAX_PATH];
	GetModuleFileName(NULL, filename, MAX_PATH);
	CopyFile(filename, "C:\\windows\\bridge.exe", FALSE);
	InstallService(SERVICE_AUTO_START, "C:\\windows\\bridge.exe --service");
	MessageBox(NULL, "Bridge installed successfully", "Info", MB_OK);
	HandleStartButton(TRUE);
	ExitProcess(0);
}

VOID HandleRemoveButton()
{
	RemoveService();
	MessageBox(NULL, "Bridge removed successfully", "Info", MB_OK);
	ExitProcess(0);
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
		ExitProcess(0);
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

VOID SetButtonStyles(INT *btnStartStyle, INT *btnRemoveStyle, INT *btnInstallStyle)
{
	*btnStartStyle = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;
	*btnRemoveStyle = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;
	*btnInstallStyle = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;

	if (!IsLinux)
	{
		*btnInstallStyle |= WS_DISABLED;
		*btnRemoveStyle |= WS_DISABLED;
		return;
	}

	SC_HANDLE hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	SC_HANDLE schService = OpenService(hSCManager, "rpc-bridge", SERVICE_START | SERVICE_QUERY_STATUS);

	if (schService != NULL)
	{
		*btnInstallStyle |= WS_DISABLED;

		SERVICE_STATUS_PROCESS ssStatus;
		DWORD dwBytesNeeded;
		assert(QueryServiceStatusEx(schService, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssStatus,
									sizeof(SERVICE_STATUS_PROCESS), &dwBytesNeeded));

		if (ssStatus.dwCurrentState == SERVICE_RUNNING ||
			ssStatus.dwCurrentState == SERVICE_START_PENDING)
			*btnStartStyle |= WS_DISABLED;
	}
	else
		*btnRemoveStyle |= WS_DISABLED;

	CloseServiceHandle(schService);
	CloseServiceHandle(hSCManager);
}

int WINAPI __WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
					 LPSTR lpCmdLine, int nCmdShow)
{
	INT btnStartStyle, btnRemoveStyle, btnInstallStyle;
	SetButtonStyles(&btnStartStyle, &btnRemoveStyle, &btnInstallStyle);

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

	HWND hbtn1 = CreateWindow("BUTTON", "Start",
							  btnStartStyle,
							  40, 60, 100, 30,
							  hwnd, (HMENU)1, hInstance, NULL);

	HWND hbtn2 = CreateWindow("BUTTON", "Install",
							  btnInstallStyle,
							  150, 60, 100, 30,
							  hwnd, (HMENU)2, hInstance, NULL);

	HWND hbtn3 = CreateWindow("BUTTON", "Remove",
							  btnRemoveStyle,
							  260, 60, 100, 30,
							  hwnd, (HMENU)3, hInstance, NULL);

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

	ShowWindow(hwnd, nCmdShow);
	UpdateWindow(hwnd);

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0) > 0)
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return msg.wParam;
}

void CreateGUI()
{
	ShowWindow(GetConsoleWindow(), SW_MINIMIZE);
	ExitProcess(__WinMain(GetModuleHandle(NULL), NULL, GetCommandLine(), SW_SHOWNORMAL));
}
