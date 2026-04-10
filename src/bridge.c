#include <namedpipeapi.h>
#include <windows.h>
#include <winuser.h>
#include <assert.h>
#include <stdio.h>

#define O_RDONLY 00

#define __darwin_MAP_ANON 0x1000

#define SYS_SOCKET 1
#define SYS_CONNECT 3

#define likely(expr) (__builtin_expect(!!(expr), 1))
#define unlikely(expr) (__builtin_expect(!!(expr), 0))

#define force_inline \
	__inline__       \
		__attribute__((__always_inline__, __gnu_inline__))
#define naked __attribute__((naked))

#define BUFFER_LENGTH 2048

typedef unsigned short sa_family_t;
typedef char *caddr_t;
typedef unsigned socklen_t;
struct sockaddr_un
{
	sa_family_t sun_family; /* AF_UNIX */
	char sun_path[108];		/* Pathname */
};

typedef struct
{
	SOCKET hSocket;
	HANDLE hPipe;
} BRIDGE_THREAD;

VOID ExitBridge(UINT uExitCode);
void print(char const *fmt, ...);
LPTSTR GetErrorMessage();
extern BOOL RunningAsService;
BOOL RetryNewConnection;
BOOL IsLinux;
HANDLE hOut = NULL;
HANDLE hIn = NULL;

extern void SC_CreateBridge();

char *getenv_custom(const char *name)
{
	static char lpBuffer[512];
	/* if "BRIDGE_RPC_PATH" is set, return it instead */
	DWORD ret = GetEnvironmentVariable("BRIDGE_RPC_PATH", lpBuffer, sizeof(lpBuffer));
	if (ret != 0)
	{
		print("Custom path: %s\n", lpBuffer);
		return lpBuffer;
	}

	char *value = getenv(name);
	if (value != NULL)
	{
		print("getenv(\"%s\") -> \"%s\"\n", name, value);
		return value;
	}

	if (IsLinux)
	{
		FILE *f = fopen("/proc/self/environ", "rb");
		if (f != NULL)
		{
			static char envBuf[8192];
			size_t totalRead = 0;
			size_t n;
			while ((n = fread(envBuf + totalRead, 1, sizeof(envBuf) - totalRead - 1, f)) > 0)
				totalRead += n;

			fclose(f);
			envBuf[totalRead] = '\0';

			/* /proc/self/environ is null-delimited KEY=VALUE\0KEY=VALUE\0... */
			size_t nameLen = strlen(name);
			char *entry = envBuf;
			while (entry < envBuf + totalRead)
			{
				if (strncmp(entry, name, nameLen) == 0 && entry[nameLen] == '=')
				{
					print("/proc/self/environ \"%s\" -> \"%s\"\n",
						  name, entry + nameLen + 1);
					return entry + nameLen + 1;
				}
				entry += strlen(entry) + 1;
			}
		}
		else
		{
			print("Failed to open /proc/self/environ\n");
			return NULL;
		}

		print("getenv_custom(\"%s\") failed\n", name);
	}

	/* Use GetEnvironmentVariable as a last resort */
	ret = GetEnvironmentVariable(name, lpBuffer, sizeof(lpBuffer));
	if (ret != 0)
	{
		print("GetEnvironmentVariable(\"%s\", ...) -> \"%s\"\n", name, lpBuffer);
		return lpBuffer;
	}
	print("GetEnvironmentVariable(\"%s\", ...) failed: %d\n", name, GetLastError());

	/* if the name is "XDG_RUNTIME_DIR", return "/run/user/1000", not ideal but it should work */
	if (IsLinux)
	{
		if (strcmp(name, "XDG_RUNTIME_DIR") == 0)
		{
			print("warning: getenv(\"XDG_RUNTIME_DIR\") failed, falling back to hardcoded path\n");
			print("getenv_custom(\"%s\")! -> \"/run/user/1000\"\n", name);
			return "/run/user/1000";
		}
	}

	return NULL;
}

const char *FindIPC()
{
	if (IsLinux)
		return getenv_custom("XDG_RUNTIME_DIR");

	const char *runtime = getenv_custom("TMPDIR");
	if (runtime != NULL)
		return runtime;

	runtime = "/tmp/rpc-bridge/tmpdir";
	print("IPC directory not set, fallback to /tmp/rpc-bridge/tmpdir\n");

	DWORD dwAttrib = GetFileAttributes(runtime);
	if (dwAttrib == INVALID_FILE_ATTRIBUTES || !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY))
	{
		print("IPC directory does not exist: %s. Check the github guide on how to install the launchd service.\n", runtime);

		int result = MessageBox(NULL, "IPC directory does not exist\nDo you want to open the installation guide?",
								"Directory not found",
								MB_YESNO | MB_ICONSTOP);
		if (result == IDYES)
			ShellExecute(NULL, "open", "https://enderice2.github.io/rpc-bridge/macos.html", NULL, NULL, SW_SHOWNORMAL);
		ExitBridge(1);
	}

	return runtime;
}

SOCKET ConnectToSocket()
{
	print("Connecting to socket\n");
	const char *runtime = FindIPC();
	print("IPC directory: %s\n", runtime);

	if (runtime == NULL)
	{
		print("Failed to find IPC directory\n");
		if (!RunningAsService)
		{
			MessageBox(NULL, "Failed to find IPC directory.",
					   "Directory not found",
					   MB_OK | MB_ICONSTOP);
		}
		ExitBridge(1);
	}

	/* FIXME: WSAEAFNOSUPPORT: https://gitlab.winehq.org/wine/wine/-/merge_requests/2786 */
	WSADATA wsaData;
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0)
	{
		print("WSAStartup failed: %d\n", iResult);
		ExitBridge(1);
	}

	/* TODO: check for multiple discord instances and create a pipe for each */
	const char *discordUnixSockets[] = {
		"%s/discord-ipc-%d",
		"%s/app/com.discordapp.Discord/discord-ipc-%d",
		"%s/.flatpak/dev.vencord.Vesktop/xdg-run/discord-ipc-%d",
		"%s/.flatpak/com.discordapp.Discord/xdg-run/discord-ipc-%d",
		"%s/snap.discord/discord-ipc-%d",
		"%s/snap.discord-canary/discord-ipc-%d",
	};

	char unixPath[256];
	for (int i = 0; i < sizeof(discordUnixSockets) / sizeof(discordUnixSockets[0]); i++)
	{
		for (int j = 0; j < 10; j++)
		{
			snprintf(unixPath, sizeof(unixPath), discordUnixSockets[i], runtime, j);
			print("Probing %s\n", unixPath);

			SOCKET s = socket(AF_UNIX, SOCK_STREAM, 0);
			if (s == INVALID_SOCKET)
			{
				if (WSAGetLastError() == WSAEAFNOSUPPORT)
				{
					print("AF_UNIX sockets not supported on this version of Wine/Proton!\nv1.4.0.1 is the version that works for you: https://github.com/enderice2/rpc-bridge/releases/tag/v1.4.0.1\n");
					int result = MessageBox(NULL, "AF_UNIX sockets not supported on this version of Wine/Proton!\nUse an older version of bridge (v1.4.0.1) or install a newer version of Wine/Proton.\n\nDo you want to open the download page for v1.4.0.1?",
											NULL, MB_YESNO | MB_ICONQUESTION);
					if (result == IDYES)
						ShellExecute(NULL, "open", "https://github.com/enderice2/rpc-bridge/releases/tag/v1.4.0.1", NULL, NULL, SW_SHOWNORMAL);
					WSACleanup();
					return INVALID_SOCKET;
				}

				print("socket() failed: %d\n", WSAGetLastError());
				WSACleanup();
				ExitBridge(1);
			}

			struct sockaddr_un addr;
			addr.sun_family = AF_UNIX;
			strncpy(addr.sun_path, unixPath, sizeof(addr.sun_path) - 1);
			addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

			if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == 0)
			{
				print("Connected to %s\n", unixPath);
				return s;
			}

			print("    error: %d\n", WSAGetLastError());
			closesocket(s);
		}
	}

	if (!RunningAsService)
		MessageBox(NULL, "Failed to connect to Discord",
				   "Socket Connection failed", MB_OK | MB_ICONSTOP);
	ExitBridge(1);
}

void PipeBufferInThread(LPVOID lpParam)
{
	BRIDGE_THREAD *bt = (BRIDGE_THREAD *)lpParam;
	print("IN thread started hSocket:%#x hPipe:%#x\n", bt->hSocket, bt->hPipe);
	int EOFCount = 0;

	while (TRUE)
	{
		char buffer[BUFFER_LENGTH];
		DWORD dwRead;
		int recvRet = recv((SOCKET)bt->hSocket, buffer, BUFFER_LENGTH, 0);
		if (unlikely(recvRet == SOCKET_ERROR))
		{
			int err = WSAGetLastError();
			if (err == WSAECONNRESET || err == WSAECONNABORTED || err == WSAESHUTDOWN)
			{
				RetryNewConnection = TRUE;
				print("IN socket disconnected: %d\n", err);
				break;
			}
			print("Failed to read from hSocket: %d\n", err);
			Sleep(1000);
			continue;
		}
		dwRead = (DWORD)recvRet;

		if (unlikely(dwRead == 0))
		{
			print("EOF\n");
			Sleep(1000);

			if (EOFCount > 4)
			{
				print("EOF count exceeded\n");
				RetryNewConnection = TRUE;
				TerminateThread(hOut, 0);
				break;
			}
			EOFCount++;
			continue;
		}
		EOFCount = 0;

		print("Reading %d bytes from hSocket: \"", dwRead);
		for (int i = 0; i < dwRead; i++)
			print("%c", buffer[i]);
		print("\"\n");

		DWORD dwWritten;
		WINBOOL bResult = WriteFile(bt->hPipe, buffer, dwRead, &dwWritten, NULL);
		if (unlikely(bResult == FALSE))
		{
			if (GetLastError() == ERROR_BROKEN_PIPE)
			{
				RetryNewConnection = TRUE;
				print("IN Broken pipe\n");
				break;
			}

			print("Failed to write to hPipe: %d\n", GetLastError());
			Sleep(1000);
			continue;
		}

		if (unlikely(dwWritten == 0))
		{
			print("Failed to write to hPipe: %d\n", GetLastError());
			Sleep(1000);
			continue;
		}

		DWORD dwTotalWritten = dwWritten;
		BOOL broken = FALSE;
		while (dwTotalWritten < dwRead)
		{
			bResult = WriteFile(bt->hPipe, buffer + dwTotalWritten, dwRead - dwTotalWritten, &dwWritten, NULL);
			if (unlikely(bResult == FALSE))
			{
				if (GetLastError() == ERROR_BROKEN_PIPE)
				{
					RetryNewConnection = TRUE;
					print("IN Broken pipe\n");
					broken = TRUE;
					break;
				}

				print("Failed to write to hPipe: %d\n", GetLastError());
				Sleep(1000);
				continue;
			}

			if (unlikely(dwWritten == 0))
			{
				print("Failed to write to hPipe: %d\n", GetLastError());
				Sleep(1000);
				continue;
			}
			dwTotalWritten += dwWritten;
		}
		if (unlikely(broken))
			break;
	}
}

void PipeBufferOutThread(LPVOID lpParam)
{
	BRIDGE_THREAD *bt = (BRIDGE_THREAD *)lpParam;
	print("OUT thread started using hSocket:%#x hPipe:%#x\n", bt->hSocket, bt->hPipe);

	while (TRUE)
	{
		char buffer[BUFFER_LENGTH];
		DWORD dwRead;
		WINBOOL bResult = ReadFile(bt->hPipe, buffer, BUFFER_LENGTH, &dwRead, NULL);
		if (unlikely(bResult == FALSE))
		{
			if (GetLastError() == ERROR_BROKEN_PIPE)
			{
				RetryNewConnection = TRUE;
				print("OUT Broken pipe\n");
				break;
			}

			print("Failed to read from hPipe: %d\n", GetLastError());
			Sleep(1000);
			continue;
		}

		print("Writing %d bytes to hSocket: \"", dwRead);
		for (int i = 0; i < dwRead; i++)
			print("%c", buffer[i]);
		print("\"\n");

		int sendRet = send((SOCKET)bt->hSocket, buffer, dwRead, 0);
		if (unlikely(sendRet == SOCKET_ERROR))
		{
			int err = WSAGetLastError();
			if (err == WSAECONNRESET || err == WSAECONNABORTED || err == WSAESHUTDOWN)
			{
				RetryNewConnection = TRUE;
				print("OUT socket disconnected: %d\n", err);
				break;
			}
			print("Failed to write to hSocket: %d\n", err);
			Sleep(1000);
			continue;
		}

		DWORD totalWritten = (DWORD)sendRet;
		BOOL broken = FALSE;
		while (totalWritten < dwRead)
		{
			int w = send((SOCKET)bt->hSocket, buffer + totalWritten, dwRead - totalWritten, 0);
			if (unlikely(w == SOCKET_ERROR))
			{
				int err = WSAGetLastError();
				if (err == WSAECONNRESET || err == WSAECONNABORTED || err == WSAESHUTDOWN)
				{
					RetryNewConnection = TRUE;
					broken = TRUE;
					break;
				}
				print("Failed to write to hSocket: %d\n", err);
				Sleep(1000);
				continue;
			}

			if (unlikely(w == 0))
			{
				print("Failed to write to hSocket: %d\n", GetLastError());
				Sleep(1000);
				continue;
			}
			totalWritten += (DWORD)w;
		}
		if (unlikely(broken))
			break;
	}
}

void CreateBridge()
{
	{
		/* should i check if the Wine version is 11.5 or newer or still do this? */
		print("Checking if AF_UNIX sockets are supported\n");
		WSADATA wsa;
		WSAStartup(MAKEWORD(2, 2), &wsa);
		SOCKET probe = socket(AF_UNIX, SOCK_STREAM, 0);
		if (probe == INVALID_SOCKET && WSAGetLastError() == WSAEAFNOSUPPORT)
		{
			print("AF_UNIX not supported, using syscall fallback\n");
			WSACleanup();

			print("Using syscall fallback\n");
			SC_CreateBridge();
			return;
		}
		else if (probe != INVALID_SOCKET)
		{
			print("AF_UNIX sockets supported, using normal socket code\n");
			closesocket(probe);
		}
		else
		{
			print("socket() failed: %d\n", WSAGetLastError());
			WSACleanup();
			if (!RunningAsService)
			{
				MessageBox(NULL, WSAGetLastError(),
						   "Failed to create socket",
						   MB_OK | MB_ICONSTOP);
			}
			ExitBridge(1);
		}
	}

	LPCTSTR lpszPipename = TEXT("\\\\.\\pipe\\discord-ipc-0");

NewConnection:
	HANDLE hTest = CreateFile(lpszPipename, GENERIC_READ, 0,
							  NULL, OPEN_EXISTING, 0, NULL);
	if (hTest != INVALID_HANDLE_VALUE)
	{
		CloseHandle(hTest);
		print("hPipe already exists: %s\n", GetErrorMessage());
		if (!RunningAsService)
		{
			MessageBox(NULL, GetErrorMessage(),
					   "Pipe already exists",
					   MB_OK | MB_ICONSTOP);
		}
		ExitBridge(1);
	}

	HANDLE hPipe =
		CreateNamedPipe("\\\\.\\pipe\\discord-ipc-0",
						PIPE_ACCESS_DUPLEX,
						PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
						PIPE_UNLIMITED_INSTANCES, BUFFER_LENGTH, BUFFER_LENGTH, 0, NULL);

	if (hPipe == INVALID_HANDLE_VALUE)
	{
		print("Failed to create hPipe: %d\n", GetLastError());
		if (!RunningAsService)
		{
			MessageBox(NULL, GetErrorMessage(),
					   "Failed to create pipe",
					   MB_OK | MB_ICONSTOP);
		}
		ExitBridge(1);
	}

	print("hPipe %s(%#x) created\n", lpszPipename, hPipe);
	print("Waiting for hPipe connection\n");
	if (!ConnectNamedPipe(hPipe, NULL))
	{
		print("Failed to connect to hPipe: %d\n", GetLastError());
		if (!RunningAsService)
			MessageBox(NULL, GetErrorMessage(),
					   NULL, MB_OK | MB_ICONSTOP);
		ExitBridge(1);
	}
	print("hPipe connected\n");

	SOCKET hSocket = ConnectToSocket();
	if (hSocket == INVALID_SOCKET)
	{
		print("Failed to connect to socket\n");
		if (!RunningAsService)
		{
			MessageBox(NULL, "Failed to connect to socket",
					   "Connection failed", MB_OK | MB_ICONSTOP);
		}
		ExitBridge(1);
	}

	print("Connected to Discord\n");

	BRIDGE_THREAD bt = {hSocket, hPipe};

	hIn = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)PipeBufferInThread, &bt, 0, NULL);
	print("Created IN thread %#lx\n", hIn);

	hOut = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)PipeBufferOutThread, &bt, 0, NULL);
	print("Created OUT thread %#lx\n", hOut);

	if (hIn == NULL || hOut == NULL)
	{
		print("Failed to create threads: %s\n", GetErrorMessage());
		if (!RunningAsService)
			MessageBox(NULL, GetErrorMessage(),
					   "Failed to create threads", MB_OK | MB_ICONSTOP);
		ExitBridge(1);
	}

	print("Waiting for threads to exit\n");
	WaitForSingleObject(hOut, INFINITE);
	print("OUT thread exited\n");

	if (RetryNewConnection)
	{
		RetryNewConnection = FALSE;
		print("Retrying connection\n");

		TerminateThread(hIn, 0);
		TerminateThread(hOut, 0);

		closesocket((SOCKET)hSocket);
		CloseHandle(hOut);
		CloseHandle(hIn);
		CloseHandle(hPipe);
		WSACleanup();
		Sleep(1000);
		goto NewConnection;
	}

	WaitForSingleObject(hIn, INFINITE);
	print("IN thread exited\n");
	closesocket((SOCKET)hSocket);
	CloseHandle(hPipe);
	WSACleanup();
}
