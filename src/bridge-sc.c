#include <windows.h>
#include <assert.h>
#include <stdio.h>

#define __linux_read 3
#define __linux_write 4
#define __linux_open 5
#define __linux_close 6
#define __linux_munmap 91
#define __linux_socketcall 102
#define __linux_mmap2 192
#define __linux_socket 359
#define __linux_connect 362

#define __darwin_read 0x2000003
#define __darwin_write 0x2000004
#define __darwin_open 0x2000005
#define __darwin_close 0x2000006
#define __darwin_socket 0x2000061
#define __darwin_connect 0x2000062
#define __darwin_mmap 0x20000C5
#define __darwin_fcntl 0x200005C
#define __darwin_sysctl 0x20000CA

#define O_RDONLY 00

/* macos & linux are the same for PROT_READ, PROT_WRITE, MAP_FIXED & MAP_PRIVATE */
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANON 0x20
#define MAP_FAILED ((void *)-1)

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
	int fd;
	HANDLE hPipe;
} SC_BRIDGE_THREAD;

/* Shared with main file */
VOID ExitBridge(UINT uExitCode);
void print(char const *fmt, ...);
LPTSTR GetErrorMessage();
extern BOOL RunningAsService;
extern BOOL RetryNewConnection;
extern BOOL IsLinux;
extern HANDLE hOut;
extern HANDLE hIn;

static force_inline int linux_syscall(int num,
									  int arg1, int arg2, int arg3,
									  int arg4, int arg5, int arg6)
{
	int ret;
	__asm__ __volatile__(
		"int $0x80\n"
		: "=a"(ret)
		: "0"(num), "b"(arg1), "c"(arg2),
		  "d"(arg3), "S"(arg4), "D"(arg5)
		: "memory");
	return ret;
}

static naked int darwin_syscall(int num,
								long arg1, long arg2, long arg3,
								long arg4, long arg5, long arg6)
{
	register long r10 __asm__("r10") = arg4;
	register long r8 __asm__("r8") = arg5;
	register long r9 __asm__("r9") = arg6;
	__asm__ __volatile__(
		"syscall\n"
		"jae noerror\n"
		"negq %%rax\n"
		"noerror:\n"
		"ret\n"
		: "=a"(num)
		: "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8), "r"(r9)
		: "memory");
}

static inline int sys_read(int fd, void *buf, size_t count)
{
	if (IsLinux)
		return linux_syscall(__linux_read, fd, buf, count, 0, 0, 0);
	else
		return darwin_syscall(__darwin_read, fd, buf, count, 0, 0, 0);
}

static inline int sys_write(int fd, const void *buf, size_t count)
{
	if (IsLinux)
		return linux_syscall(__linux_write, fd, buf, count, 0, 0, 0);
	else
		return darwin_syscall(__darwin_write, fd, buf, count, 0, 0, 0);
}

static inline int sys_open(const char *pathname, int flags, int mode)
{
	if (IsLinux)
		return linux_syscall(__linux_open, pathname, flags, mode, 0, 0, 0);
	else
		return darwin_syscall(__darwin_open, pathname, flags, mode, 0, 0, 0);
}

static inline int sys_close(int fd)
{
	if (IsLinux)
		return linux_syscall(__linux_close, fd, 0, 0, 0, 0, 0);
	else
		return darwin_syscall(__darwin_close, fd, 0, 0, 0, 0, 0);
}

static inline unsigned int *sys_mmap(unsigned int *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	if (IsLinux)
		return linux_syscall(__linux_mmap2, addr, length, prot, flags, fd, offset);
	else
	{
		if (flags & MAP_ANON)
		{
			flags &= ~MAP_ANON;
			flags |= __darwin_MAP_ANON;
		}
		return darwin_syscall(__darwin_mmap, addr, length, prot, flags, fd, offset);
	}
}

static inline int sys_munmap(unsigned int *addr, size_t length)
{
	assert(IsLinux);
	return linux_syscall(__linux_munmap, addr, length, 0, 0, 0, 0);
}

static inline int sys_socketcall(int call, unsigned long *args)
{
	assert(IsLinux);
	return linux_syscall(__linux_socketcall, call, args, 0, 0, 0, 0);
}

static inline int sys_socket(int domain, int type, int protocol)
{
	if (IsLinux)
		return linux_syscall(__linux_socket, domain, type, protocol, 0, 0, 0);
	else
		return darwin_syscall(__darwin_socket, domain, type, protocol, 0, 0, 0);
}

static inline int sys_connect(int s, caddr_t name, socklen_t namelen)
{
	if (IsLinux)
		return linux_syscall(__linux_connect, s, name, namelen, 0, 0, 0);
	else
		return darwin_syscall(__darwin_connect, s, name, namelen, 0, 0, 0);
}

static char *sc_getenv(const char *name)
{
	static void *environStr = NULL;
	static char lpBuffer[512];
	DWORD ret = GetEnvironmentVariable("BRIDGE_RPC_PATH", lpBuffer, sizeof(lpBuffer));
	if (ret != 0)
		return lpBuffer;

	if (!IsLinux)
	{
		char *value = getenv(name);
		if (value == NULL)
		{
			print("Failed to get environment variable: %s\n", name);

			/* Use GetEnvironmentVariable as a last resort */
			DWORD ret = GetEnvironmentVariable(name, lpBuffer, sizeof(lpBuffer));
			if (ret == 0)
			{
				print("GetEnvironmentVariable(\"%s\", ...) failed: %d\n", name, ret);
				return NULL;
			}
			return lpBuffer;
		}
		return value;
	}

	/* I hope the 0x20000 is okay */
	if (environStr == NULL)
		environStr = sys_mmap(0x20000, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);

	if ((uintptr_t)environStr > 0x7effffff)
		print("Warning: environStr %#lx is above 2GB\n", environStr);

	const char *linux_environ = "/proc/self/environ";
	memcpy(environStr, linux_environ, strlen(linux_environ) + 1);

	int fd = sys_open(environStr, O_RDONLY, 0);

	if (fd < 0)
	{
		print("Failed to open /proc/self/environ: %d\n", fd);
		return NULL;
	}

	char *buffer = sys_mmap(0x22000, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
	char *result = NULL;
	int bytesRead;

	while ((bytesRead = sys_read(fd, buffer, 0x1000 - 1)) > 0)
	{
		buffer[bytesRead] = '\0';
		char *env = buffer;
		while (*env)
		{
			if (strstr(env, name) == env)
			{
				env += strlen(name);
				if (*env == '=')
				{
					env++;
					result = strdup(env);
					break;
				}
			}
			env += strlen(env) + 1;
		}

		if (result)
			break;
	}

	sys_close(fd);
	return result;
}

int SC_ConnectToSocket()
{
	const char *runtime;
	if (IsLinux)
		runtime = sc_getenv("XDG_RUNTIME_DIR");
	else
	{
		runtime = sc_getenv("TMPDIR");
		if (runtime == NULL)
		{
			runtime = "/tmp/rpc-bridge/tmpdir";
			print("IPC directory not set, fallback to /tmp/rpc-bridge/tmpdir\n");

			DWORD dwAttrib = GetFileAttributes(runtime);
			if (dwAttrib == INVALID_FILE_ATTRIBUTES || !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY))
			{
				print("IPC directory does not exist: %s. If you're on MacOS, see the github guide on how to install the launchd service.\n", runtime);

				int result = MessageBox(NULL, "IPC directory does not exist\nDo you want to open the installation guide?",
										"Directory not found",
										MB_YESNO | MB_ICONSTOP);
				if (result == IDYES)
					ShellExecute(NULL, "open", "https://enderice2.github.io/rpc-bridge/macos.html", NULL, NULL, SW_SHOWNORMAL);
				ExitBridge(1);
			}
		}
	}

	print("SC IPC directory: %s\n", runtime);

	const char *discordUnixSockets[] = {
		"%s/discord-ipc-%d",
		"%s/app/com.discordapp.Discord/discord-ipc-%d",
		"%s/.flatpak/dev.vencord.Vesktop/xdg-run/discord-ipc-%d",
		"%s/.flatpak/com.discordapp.Discord/xdg-run/discord-ipc-%d",
		"%s/snap.discord/discord-ipc-%d",
		"%s/snap.discord-canary/discord-ipc-%d",
	};

	static struct sockaddr_un *socketAddr = NULL;
	if (!socketAddr)
		socketAddr = sys_mmap((void *)0x24000, 0x1000,
							  PROT_READ | PROT_WRITE,
							  MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);

	char path[256];
	for (int i = 0; i < sizeof(discordUnixSockets) / sizeof(discordUnixSockets[0]); i++)
	{
		for (int j = 0; j < 10; j++)
		{
			snprintf(path, sizeof(path), discordUnixSockets[i], runtime, j);
			print("SC Probing %s\n", path);

			int fd = sys_socket(AF_UNIX, SOCK_STREAM, 0);
			if (fd < 0)
			{
				print("SC socket() failed: %d\n", fd);
				ExitBridge(1);
			}

			socketAddr->sun_family = AF_UNIX;
			strncpy(socketAddr->sun_path, path, sizeof(socketAddr->sun_path) - 1);
			socketAddr->sun_path[sizeof(socketAddr->sun_path) - 1] = '\0';

			if (sys_connect(fd, socketAddr, sizeof(*socketAddr)) == 0)
			{
				print("SC Connected to %s\n", path);
				return fd;
			}
			print("    error: %d\n", -1);
			sys_close(fd);
		}
	}

	if (!RunningAsService)
		MessageBox(NULL, "Failed to connect to Discord",
				   "Socket Connection failed", MB_OK | MB_ICONSTOP);
	ExitBridge(1);
}

void SC_PipeBufferInThread(LPVOID lpParam)
{
	SC_BRIDGE_THREAD *bt = (SC_BRIDGE_THREAD *)lpParam;
	print("SC IN thread fd:%d pipe:%#x\n", bt->fd, bt->hPipe);
	char buffer[BUFFER_LENGTH];
	int EOFCount = 0;

	while (TRUE)
	{
		int r = sys_read(bt->fd, buffer, BUFFER_LENGTH);
		if (unlikely(r < 0))
		{
			print("SC IN read error: %d\n", r);
			Sleep(1000);
			continue;
		}

		if (unlikely(r == 0))
		{
			EOFCount++;
			if (EOFCount > 4)
			{
				print("SC IN EOF exceeded\n");
				RetryNewConnection = TRUE;
				TerminateThread(hOut, 0);
				break;
			}
			Sleep(1000);
			continue;
		}
		EOFCount = 0;

		DWORD dwWritten;
		WINBOOL bResult = WriteFile(bt->hPipe, buffer, r, &dwWritten, NULL);
		if (unlikely(bResult == FALSE))
		{
			if (GetLastError() == ERROR_BROKEN_PIPE)
			{
				RetryNewConnection = TRUE;
				print("SC IN broken pipe\n");
				break;
			}
			Sleep(1000);
			continue;
		}

		DWORD total = dwWritten;
		BOOL broken = FALSE;
		while (total < (DWORD)r)
		{
			bResult = WriteFile(bt->hPipe, buffer + total, r - total, &dwWritten, NULL);
			if (unlikely(bResult == FALSE))
			{
				if (GetLastError() == ERROR_BROKEN_PIPE)
				{
					RetryNewConnection = TRUE;
					broken = TRUE;
					break;
				}
				Sleep(1000);
				continue;
			}
			if (unlikely(dwWritten == 0))
			{
				Sleep(1000);
				continue;
			}
			total += dwWritten;
		}
		if (unlikely(broken))
			break;
	}
}

void SC_PipeBufferOutThread(LPVOID lpParam)
{
	SC_BRIDGE_THREAD *bt = (SC_BRIDGE_THREAD *)lpParam;
	print("SC OUT thread fd:%d pipe:%#x\n", bt->fd, bt->hPipe);
	char buffer[BUFFER_LENGTH];

	while (TRUE)
	{
		DWORD dwRead;
		WINBOOL bResult = ReadFile(bt->hPipe, buffer, BUFFER_LENGTH, &dwRead, NULL);
		if (unlikely(bResult == FALSE))
		{
			if (GetLastError() == ERROR_BROKEN_PIPE)
			{
				RetryNewConnection = TRUE;
				print("SC OUT broken pipe\n");
				break;
			}
			Sleep(1000);
			continue;
		}

		int written = sys_write(bt->fd, buffer, dwRead);
		if (unlikely(written < 0))
		{
			Sleep(1000);
			continue;
		}

		while (written < (int)dwRead)
		{
			int w = sys_write(bt->fd, buffer + written, dwRead - written);
			if (w < 0)
			{
				Sleep(1000);
				continue;
			}
			if (w == 0)
			{
				Sleep(1000);
				continue;
			}
			written += w;
		}
	}
}

void SC_CreateBridge()
{
	LPCTSTR lpszPipename = TEXT("\\\\.\\pipe\\discord-ipc-0");

NewConnection:;
	HANDLE hTest = CreateFile(lpszPipename, GENERIC_READ, 0,
							  NULL, OPEN_EXISTING, 0, NULL);
	if (hTest != INVALID_HANDLE_VALUE)
	{
		CloseHandle(hTest);
		print("SC hPipe already exists\n");
		if (!RunningAsService)
			MessageBox(NULL, "Pipe already exists", "Error", MB_OK | MB_ICONSTOP);
		ExitBridge(1);
	}

	HANDLE hPipe = CreateNamedPipe("\\\\.\\pipe\\discord-ipc-0",
								   PIPE_ACCESS_DUPLEX,
								   PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
								   PIPE_UNLIMITED_INSTANCES,
								   BUFFER_LENGTH, BUFFER_LENGTH, 0, NULL);
	if (hPipe == INVALID_HANDLE_VALUE)
	{
		print("SC Failed to create hPipe: %d\n", GetLastError());
		if (!RunningAsService)
			MessageBox(NULL, GetErrorMessage(), "Failed to create pipe", MB_OK | MB_ICONSTOP);
		ExitBridge(1);
	}

	print("SC hPipe %s(%#x) created\n", lpszPipename, hPipe);
	print("SC Waiting for hPipe connection\n");
	if (!ConnectNamedPipe(hPipe, NULL))
	{
		print("SC Failed to connect hPipe: %d\n", GetLastError());
		if (!RunningAsService)
			MessageBox(NULL, GetErrorMessage(), NULL, MB_OK | MB_ICONSTOP);
		ExitBridge(1);
	}
	print("SC hPipe connected\n");

	int fd = SC_ConnectToSocket();
	print("SC Connected to Discord\n");

	SC_BRIDGE_THREAD bt = {fd, hPipe};

	hIn = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)SC_PipeBufferInThread, &bt, 0, NULL);
	print("SC Created IN thread %#lx\n", hIn);
	hOut = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)SC_PipeBufferOutThread, &bt, 0, NULL);
	print("SC Created OUT thread %#lx\n", hOut);

	if (hIn == NULL || hOut == NULL)
	{
		print("SC Failed to create threads: %s\n", GetErrorMessage());
		if (!RunningAsService)
			MessageBox(NULL, GetErrorMessage(), "Failed to create threads", MB_OK | MB_ICONSTOP);
		ExitBridge(1);
	}

	print("SC Waiting for threads to exit\n");
	WaitForSingleObject(hOut, INFINITE);
	print("SC OUT thread exited\n");

	if (RetryNewConnection)
	{
		RetryNewConnection = FALSE;
		print("SC Retrying connection\n");

		TerminateThread(hIn, 0);
		TerminateThread(hOut, 0);

		sys_close(fd);
		CloseHandle(hOut);
		CloseHandle(hIn);
		CloseHandle(hPipe);
		hIn = NULL;
		hOut = NULL;
		Sleep(1000);
		goto NewConnection;
	}

	WaitForSingleObject(hIn, INFINITE);
	print("SC IN thread exited\n");
	sys_close(fd);
	CloseHandle(hPipe);
}
