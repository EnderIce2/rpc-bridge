#include <namedpipeapi.h>
#include <windows.h>
#include <winuser.h>
#include <assert.h>
#include <stdio.h>

#define __NR_read 3
#define __NR_write 4
#define __NR_open 5
#define __NR_close 6
#define __NR_socketcall 102

#define SYS_SOCKET 1
#define SYS_CONNECT 3

#define O_RDONLY 00

#define likely(expr) (__builtin_expect(!!(expr), 1))
#define unlikely(expr) (__builtin_expect(!!(expr), 0))

#define force_inline \
	__inline__       \
		__attribute__((__always_inline__, __gnu_inline__))

typedef unsigned short sa_family_t;
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

static force_inline int syscall(int num,
								intptr_t arg1, intptr_t arg2, intptr_t arg3,
								intptr_t arg4, intptr_t arg5, intptr_t arg6)
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

static inline int sys_read(int fd, void *buf, size_t count)
{
	return syscall(__NR_read, fd, (intptr_t)buf, count, 0, 0, 0);
}

static inline int sys_write(int fd, const void *buf, size_t count)
{
	return syscall(__NR_write, fd, (intptr_t)buf, count, 0, 0, 0);
}

static inline int sys_open(const char *pathname, int flags, int mode)
{
	return syscall(__NR_open, (intptr_t)pathname, flags, mode, 0, 0, 0);
}

static inline int sys_close(int fd)
{
	return syscall(__NR_close, fd, 0, 0, 0, 0, 0);
}

static inline int sys_socketcall(int call, unsigned long *args)
{
	return syscall(__NR_socketcall, call, (intptr_t)args, 0, 0, 0, 0);
}

char *linux_getenv(const char *name)
{
	int fd = sys_open("/proc/self/environ", O_RDONLY, 0);
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
	const char *runtime = linux_getenv("XDG_RUNTIME_DIR");
	if (runtime == NULL)
	{
		print("XDG_RUNTIME_DIR not set\n");
		if (!RunningAsService)
		{
			MessageBox(NULL,
					   "XDG_RUNTIME_DIR not set",
					   "Environment variable not set",
					   MB_OK | MB_ICONSTOP);
		}
		ExitProcess(1);
	}

	print("XDG_RUNTIME_DIR: %s\n", runtime);

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

		unsigned long socketArgs[] = {
			(unsigned long)fd,
			(unsigned long)&socketAddr,
			sizeof(socketAddr)};

		sockRet = sys_socketcall(SYS_CONNECT, socketArgs);

		free(pipePath);
		if (sockRet >= 0)
			break;
	}

	if (sockRet < 0)
	{
		print("socketcall failed for: %d\n", sockRet);
		if (!RunningAsService)
		{
			MessageBox(NULL,
					   "Failed to connect to Discord",
					   "Socket Connection failed",
					   MB_OK | MB_ICONSTOP);
		}
		ExitProcess(1);
	}
}

void PipeBufferInThread(LPVOID lpParam)
{
	bridge_thread *bt = (bridge_thread *)lpParam;
	print("In thread started using fd %d and pipe %#x\n",
		  bt->fd, bt->hPipe);
	while (TRUE)
	{
		char buffer[1024];
		int read = sys_read(bt->fd, buffer, sizeof(buffer));

		if (unlikely(read < 0))
		{
			print("Failed to read from unix pipe: %d\n",
				  GetErrorMessage());
			Sleep(1000);
			continue;
		}

		if (unlikely(read == 0))
		{
			print("EOF\n");
			Sleep(1000);
			continue;
		}

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
		char buffer[1024];
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
		if (!RunningAsService)
			MessageBox(NULL, GetErrorMessage(),
					   NULL, MB_OK | MB_ICONSTOP);
		print("Pipe already exists: %s\n",
			  GetErrorMessage());
		ExitProcess(1);
	}

	HANDLE hPipe =
		CreateNamedPipe("\\\\.\\pipe\\discord-ipc-0",
						PIPE_ACCESS_DUPLEX,
						PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
						1, 1024, 1024, 0, NULL);

	if (hPipe == INVALID_HANDLE_VALUE)
	{
		if (!RunningAsService)
			MessageBox(NULL, GetErrorMessage(),
					   NULL, MB_OK | MB_ICONSTOP);
		print("Failed to create pipe: %s\n",
			  GetErrorMessage());
		ExitProcess(1);
	}

	print("Pipe %s(%#x) created\n", lpszPipename, hPipe);
	print("Waiting for pipe connection\n");
	if (!ConnectNamedPipe(hPipe, NULL))
	{
		if (!RunningAsService)
			MessageBox(NULL, GetErrorMessage(),
					   NULL, MB_OK | MB_ICONSTOP);
		print("Failed to connect to pipe: %s\n",
			  GetErrorMessage());
		ExitProcess(1);
	}
	print("Pipe connected\n");

	unsigned long socketArgs[] = {
		(unsigned long)AF_UNIX,
		(unsigned long)SOCK_STREAM,
		0};

	int fd = sys_socketcall(SYS_SOCKET, socketArgs);

	print("Socket %d created\n", fd);

	if (fd < 0)
	{
		if (!RunningAsService)
			MessageBox(NULL, GetErrorMessage(),
					   NULL, MB_OK | MB_ICONSTOP);
		print("Failed to create socket: %d\n", fd);
		ExitProcess(1);
	}

	ConnectToSocket(fd);
	print("Connected to Discord\n");

	bridge_thread bt = {fd, hPipe};

	HANDLE hIn = CreateThread(NULL, 0,
							  (LPTHREAD_START_ROUTINE)PipeBufferInThread,
							  (LPVOID)&bt,
							  0, NULL);

	HANDLE hOut = CreateThread(NULL, 0,
							   (LPTHREAD_START_ROUTINE)PipeBufferOutThread,
							   (LPVOID)&bt,
							   0, NULL);

	if (hIn == NULL || hOut == NULL)
	{
		if (!RunningAsService)
			MessageBox(NULL, GetErrorMessage(), NULL, MB_OK | MB_ICONSTOP);
		print("Failed to create threads: %s\n", GetErrorMessage());
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
