#include <namedpipeapi.h>
#include <windows.h>
#include <winuser.h>
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
#define __darwin_fcntl 0x200005C
#define __darwin_sysctl 0x20000CA

#define O_RDONLY 00

#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANON 0x20
#define MAP_FAILED ((void *)-1)

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
} bridge_thread;

void print(char const *fmt, ...);
LPTSTR GetErrorMessage();
extern BOOL RunningAsService;
BOOL RetryNewConnection;
BOOL IsLinux;
HANDLE hOut = NULL;
HANDLE hIn = NULL;

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
	assert(IsLinux);
	return linux_syscall(__linux_mmap2, addr, length, prot, flags, fd, offset);
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

void *environStr = NULL;
char *native_getenv(const char *name)
{
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
	{
		environStr = sys_mmap(0x20000, 4096, PROT_READ | PROT_WRITE,
							  MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
		print("Allocated 4096 bytes at %#lx\n", environStr);
	}

	if ((uintptr_t)environStr > 0x7effffff)
		print("Warning: environStr %#lx is above 2GB\n", environStr);

	const char *linux_environ = "/proc/self/environ";
	memcpy(environStr, linux_environ, strlen(linux_environ) + 1);

	int fd = sys_open(environStr, O_RDONLY, 0);

	// sys_munmap(environStr, 4096);
	if (fd < 0)
	{
		print("Failed to open /proc/self/environ: %d\n", fd);
		return NULL;
	}

	char buffer[4096];
	char *result = NULL;
	int bytesRead;

	while ((bytesRead = sys_read(fd, buffer, sizeof(buffer) - 1)) > 0)
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

void ConnectToSocket(int fd)
{
	print("Connecting to socket\n");
	const char *runtime;
	if (IsLinux)
		runtime = native_getenv("XDG_RUNTIME_DIR");
	else
	{
		runtime = native_getenv("TMPDIR");
		if (runtime == NULL)
		{
			runtime = "/tmp/rpc-bridge/tmpdir";
			print("IPC directory not set, fallback to /tmp/rpc-bridge/tmpdir\n");

			// Check if the directory exists
			DWORD dwAttrib = GetFileAttributes(runtime);
			if (dwAttrib == INVALID_FILE_ATTRIBUTES || !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY))
			{
				print("IPC directory does not exist: %s. If you're on MacOS, see the github guide on how to install the launchd service.\n", runtime);
				// Handle the case where the directory doesn't exist
				// For example, create the directory

				int result = MessageBox(NULL, "IPC directory does not exist\nDo you want to open the installation guide?",
										"Directory not found",
										MB_YESNO | MB_ICONSTOP);
				if (result == IDYES)
					ShellExecute(NULL, "open", "https://enderice2.github.io/rpc-bridge/installation.html#macos", NULL, NULL, SW_SHOWNORMAL);
				ExitProcess(1);
			}
		}
	}

	print("IPC directory: %s\n", runtime);

	/* TODO: check for multiple discord instances and create a pipe for each */
	const char *discordUnixPipes[] = {
		"/discord-ipc-0",
		"/snap.discord/discord-ipc-0",
		"/app/com.discordapp.Discord/discord-ipc-0",
	};

	struct sockaddr_un socketAddr;
	socketAddr.sun_family = AF_UNIX;
	char *pipePath = NULL;
	int sockRet = -1;

	for (int i = 0; i < sizeof(discordUnixPipes) / sizeof(discordUnixPipes[0]); i++)
	{
		pipePath = malloc(strlen(runtime) + strlen(discordUnixPipes[i]) + 1);
		strcpy(pipePath, runtime);
		strcat(pipePath, discordUnixPipes[i]);
		strcpy_s(socketAddr.sun_path, sizeof(socketAddr.sun_path), pipePath);

		print("Connecting to %s\n", pipePath);

		if (IsLinux)
		{
			unsigned long socketArgs[] = {
				(unsigned long)fd,
				(unsigned long)(intptr_t)&socketAddr,
				sizeof(socketAddr)};

			sockRet = sys_socketcall(SYS_CONNECT, socketArgs);
		}
		else
			sockRet = sys_connect(fd, (caddr_t)&socketAddr, sizeof(socketAddr));

		free(pipePath);
		if (sockRet >= 0)
			break;
	}

	if (sockRet < 0)
	{
		print("socketcall failed for: %d\n", sockRet);
		if (!RunningAsService)
			MessageBox(NULL, "Failed to connect to Discord",
					   "Socket Connection failed",
					   MB_OK | MB_ICONSTOP);
		ExitProcess(1);
	}
}

void PipeBufferInThread(LPVOID lpParam)
{
	bridge_thread *bt = (bridge_thread *)lpParam;
	print("In thread started using fd %d and pipe %#x\n",
		  bt->fd, bt->hPipe);
	int EOFCount = 0;
	while (TRUE)
	{
		char buffer[BUFFER_LENGTH];
		int read = sys_read(bt->fd, buffer, sizeof(buffer));

		if (unlikely(read < 0))
		{
			print("Failed to read from unix pipe: %d\n",
				  GetErrorMessage());
			Sleep(1000);
			continue;
		}

		if (EOFCount > 4)
		{
			print("EOF count exceeded\n");
			RetryNewConnection = TRUE;
			TerminateThread(hOut, 0);
			break;
		}

		if (unlikely(read == 0))
		{
			print("EOF\n");
			Sleep(1000);
			EOFCount++;
			continue;
		}
		EOFCount = 0;

		print("Reading %d bytes from unix pipe: \"", read);
		for (int i = 0; i < read; i++)
			print("%c", buffer[i]);
		print("\"\n");

		DWORD dwWritten;
		if (unlikely(!WriteFile(bt->hPipe, buffer, read,
								&dwWritten, NULL)))
		{
			if (GetLastError() == ERROR_BROKEN_PIPE)
			{
				RetryNewConnection = TRUE;
				print("In Broken pipe\n");
				break;
			}

			print("Failed to read from pipe: %d\n",
				  GetErrorMessage());
			Sleep(1000);
			continue;
		}

		if (unlikely(dwWritten < 0))
		{
			print("Failed to write to pipe: %d\n",
				  GetErrorMessage());
			Sleep(1000);
			continue;
		}

		while (dwWritten < read)
		{
			int last_written = dwWritten;
			if (unlikely(!WriteFile(bt->hPipe, buffer + dwWritten,
									read - dwWritten, &dwWritten, NULL)))
			{
				if (GetLastError() == ERROR_BROKEN_PIPE)
				{
					RetryNewConnection = TRUE;
					print("In Broken pipe\n");
					break;
				}

				print("Failed to read from pipe: %d\n",
					  GetErrorMessage());
				Sleep(1000);
				continue;
			}

			if (unlikely(last_written == dwWritten))
			{
				print("Failed to write to pipe: %d\n",
					  GetErrorMessage());
				Sleep(1000);
				continue;
			}
		}
	}
}

void PipeBufferOutThread(LPVOID lpParam)
{
	bridge_thread *bt = (bridge_thread *)lpParam;
	print("Out thread started using fd %d and pipe %#x\n",
		  bt->fd, bt->hPipe);
	while (TRUE)
	{
		char buffer[BUFFER_LENGTH];
		DWORD dwRead;

		if (unlikely(!ReadFile(bt->hPipe, buffer, sizeof(buffer),
							   &dwRead, NULL)))
		{
			if (GetLastError() == ERROR_BROKEN_PIPE)
			{
				RetryNewConnection = TRUE;
				print("Out Broken pipe\n");
				break;
			}

			print("Failed to read from pipe: %d\n",
				  GetErrorMessage());
			Sleep(1000);
			continue;
		}

		print("Writing %d bytes to unix pipe: \"", dwRead);
		for (int i = 0; i < dwRead; i++)
			print("%c", buffer[i]);
		print("\"\n");

		int written = sys_write(bt->fd, buffer, dwRead);
		if (unlikely(written < 0))
		{
			print("Failed to write to socket: %d\n",
				  written);
			continue;
		}

		while (written < dwRead)
		{
			int last_written = written;
			written += sys_write(bt->fd, buffer + written, dwRead - written);
			if (unlikely(last_written == written))
			{
				print("Failed to write to socket: %d\n",
					  GetErrorMessage());
				Sleep(1000);
				continue;
			}
		}
	}
}

void CreateBridge()
{
	LPCTSTR lpszPipename = TEXT("\\\\.\\pipe\\discord-ipc-0");

NewConnection:
	if (GetNamedPipeInfo((HANDLE)lpszPipename,
						 NULL, NULL,
						 NULL, NULL))
	{
		print("Pipe already exists: %s\n",
			  GetErrorMessage());
		if (!RunningAsService)
		{
			MessageBox(NULL, GetErrorMessage(),
					   "Pipe already exists",
					   MB_OK | MB_ICONSTOP);
		}
		ExitProcess(1);
	}

	HANDLE hPipe =
		CreateNamedPipe("\\\\.\\pipe\\discord-ipc-0",
						PIPE_ACCESS_DUPLEX,
						PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
						1, 1024, 1024, 0, NULL);

	if (hPipe == INVALID_HANDLE_VALUE)
	{
		print("Failed to create pipe: %s\n",
			  GetErrorMessage());
		if (!RunningAsService)
		{
			MessageBox(NULL, GetErrorMessage(),
					   "Failed to create pipe",
					   MB_OK | MB_ICONSTOP);
		}
		ExitProcess(1);
	}

	print("Pipe %s(%#x) created\n", lpszPipename, hPipe);
	print("Waiting for pipe connection\n");
	if (!ConnectNamedPipe(hPipe, NULL))
	{
		print("Failed to connect to pipe: %s\n",
			  GetErrorMessage());
		if (!RunningAsService)
			MessageBox(NULL, GetErrorMessage(),
					   NULL, MB_OK | MB_ICONSTOP);
		ExitProcess(1);
	}
	print("Pipe connected\n");

	int fd;
	if (IsLinux)
	{
		unsigned long socketArgs[] = {
			(unsigned long)AF_UNIX,
			(unsigned long)SOCK_STREAM,
			0};
		fd = sys_socketcall(SYS_SOCKET, socketArgs);
	}
	else
		fd = sys_socket(AF_UNIX, SOCK_STREAM, 0);

	print("Socket %d created\n", fd);

	if (fd < 0)
	{
		print("Failed to create socket: %d\n", fd);
		if (!RunningAsService)
			MessageBox(NULL, "Failed to create socket",
					   NULL, MB_OK | MB_ICONSTOP);
		ExitProcess(1);
	}

	ConnectToSocket(fd);
	print("Connected to Discord\n");

	bridge_thread bt = {fd, hPipe};

	hIn = CreateThread(NULL, 0,
					   (LPTHREAD_START_ROUTINE)PipeBufferInThread,
					   (LPVOID)&bt,
					   0, NULL);
	print("Created in thread %#lx\n", hIn);

	hOut = CreateThread(NULL, 0,
						(LPTHREAD_START_ROUTINE)PipeBufferOutThread,
						(LPVOID)&bt,
						0, NULL);
	print("Created out thread %#lx\n", hOut);

	if (hIn == NULL || hOut == NULL)
	{
		print("Failed to create threads: %s\n", GetErrorMessage());
		if (!RunningAsService)
		{
			MessageBox(NULL, GetErrorMessage(),
					   "Failed to create threads",
					   MB_OK | MB_ICONSTOP);
		}
		ExitProcess(1);
	}

	print("Waiting for threads to exit\n");
	WaitForSingleObject(hOut, INFINITE);
	print("Buffer out thread exited\n");

	if (RetryNewConnection)
	{
		RetryNewConnection = FALSE;
		print("Retrying new connection\n");
		if (!TerminateThread(hIn, 0))
			print("Failed to terminate thread: %s\n",
				  GetErrorMessage());

		if (!TerminateThread(hOut, 0))
			print("Failed to terminate thread: %s\n",
				  GetErrorMessage());

		sys_close(fd);
		CloseHandle(hOut);
		CloseHandle(hIn);
		CloseHandle(hPipe);
		Sleep(1000);
		goto NewConnection;
	}

	WaitForSingleObject(hIn, INFINITE);
	print("Buffer in thread exited\n");
	CloseHandle(hPipe);
}
