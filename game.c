#include <windows.h>
#include <tlhelp32.h>
#include <winbase.h>
#include <winuser.h>
#include <assert.h>
#include <stdio.h>

void print(char const *fmt, ...);
LPTSTR GetErrorMessage();

BOOL IsChildProcess(DWORD parentID, DWORD childID)
{
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32 pe32;

	if (hSnapshot == INVALID_HANDLE_VALUE)
		return FALSE;

	pe32.dwSize = sizeof(PROCESSENTRY32);

	if (Process32First(hSnapshot, &pe32))
	{
		do
		{
			if (pe32.th32ParentProcessID == parentID && pe32.th32ProcessID == childID)
			{
				CloseHandle(hSnapshot);
				return TRUE;
			}
		} while (Process32Next(hSnapshot, &pe32));
	}

	CloseHandle(hSnapshot);
	return FALSE;
}

BOOL FileExists(LPCTSTR szPath)
{
	DWORD dwAttrib = GetFileAttributes(szPath);
	return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
			!(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

void LaunchGame(int argc, char **argv)
{
	char *gamePath = "";
	int startArg = 1;
	if (argc > 1)
	{
		print("Checking if %s is a valid file\n", argv[1]);
		if (FileExists(argv[1]))
		{
			print("Checking if %s is a valid executable\n", argv[1]);
			DWORD dwBinaryType;
			if (!GetBinaryType(argv[1], &dwBinaryType))
			{
				MessageBox(NULL, GetErrorMessage(),
						   NULL, MB_OK | MB_ICONSTOP);
				ExitProcess(1);
			}
			print("Executable type: %d\n", dwBinaryType);

			gamePath = argv[1];
			startArg = 2;
		}
		else
		{
			MessageBox(NULL, "Invalid game path specified",
					   NULL, MB_OK | MB_ICONSTOP);
			print("%s is not a valid file\n", argv[1]);
			ExitProcess(1);
		}
	}
	else if (argc == 1)
	{
		print("No game path specified. Idling...\n");
		while (TRUE)
			Sleep(1000);
	}
	else
	{
		MessageBox(NULL, "No game path specified",
				   NULL, MB_OK | MB_ICONSTOP);
		ExitProcess(1);
	}

	STARTUPINFO game_si;
	PROCESS_INFORMATION game_pi;

	ZeroMemory(&game_si, sizeof(STARTUPINFO));
	game_si.cb = sizeof(STARTUPINFO);
	game_si.hStdOutput = INVALID_HANDLE_VALUE;
	game_si.hStdError = INVALID_HANDLE_VALUE;
	game_si.dwFlags |= STARTF_USESTDHANDLES;

	char *gameArgs = LocalAlloc(LPTR, 512);
	for (int i = startArg; i < argc; i++)
	{
		assert(strlen(gameArgs) + strlen(argv[i]) < 512);
		gameArgs = strcat(gameArgs, argv[i]);
		gameArgs = strcat(gameArgs, " ");
	}
	print("Launching \"%s\" with arguments \"%s\"\n", gamePath, gameArgs);
	if (!CreateProcess(gamePath, gameArgs, NULL, NULL, FALSE,
					   0, NULL, NULL, &game_si, &game_pi))
	{
		MessageBox(NULL, GetErrorMessage(), NULL, MB_OK | MB_ICONSTOP);
		ExitProcess(1);
	}
	LocalFree(gameArgs);
	DWORD parentID = game_pi.dwProcessId;
	print("Waiting for PID %d to exit...\n", game_pi.dwProcessId);
	WaitForSingleObject(game_pi.hProcess, INFINITE);
	print("PID %d exited\n", game_pi.dwProcessId);

	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32 pe32;

	if (hSnapshot != INVALID_HANDLE_VALUE)
	{
		pe32.dwSize = sizeof(PROCESSENTRY32);
		if (Process32First(hSnapshot, &pe32))
		{
			do
			{
				if (IsChildProcess(parentID, pe32.th32ProcessID))
				{
					WaitForSingleObject(OpenProcess(SYNCHRONIZE, FALSE,
													pe32.th32ProcessID),
										INFINITE);
					print("Waiting for PID %d\n", pe32.th32ProcessID);
				}
			} while (Process32Next(hSnapshot, &pe32));
		}
		CloseHandle(hSnapshot);
	}
	else
	{
		MessageBox(NULL, GetErrorMessage(), NULL, MB_OK | MB_ICONSTOP);
		ExitProcess(0);
	}

	CloseHandle(game_pi.hProcess);
	print("Game exited\n");
}
