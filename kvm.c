#if defined(__linux__)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <signal.h>

void transferData(int fromSock, int toSock)
{
	char buffer[256];
	ssize_t bytesRead;
	while ((bytesRead = read(fromSock, buffer, sizeof(buffer))) > 0)
	{
		write(toSock, buffer, bytesRead);
	}
}

int connectToSocket(const char *socketPath)
{
	int sock = socket(AF_UNIX, SOCK_STREAM, 0);
	assert(sock >= 0);

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, socketPath, sizeof(addr.sun_path) - 1);

	int result = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
	assert(result == 0);

	return sock;
}

int kvmSock;
int discordSock;
void sigintHandler(int sig)
{
	close(kvmSock);
	close(discordSock);
	exit(0);
}

int main()
{
	signal(SIGINT, sigintHandler);

	const char *kvmSocketPath = "/tmp/kvm-rpc-bridge";
	const char *discordSocketPath = getenv("XDG_RUNTIME_DIR");
	if (discordSocketPath == NULL)
	{
		fprintf(stderr, "XDG_RUNTIME_DIR environment variable not set\n");
		return EXIT_FAILURE;
	}
	char discordSocketFullPath[256];
	snprintf(discordSocketFullPath, sizeof(discordSocketFullPath), "%s/discord-ipc-0", discordSocketPath);

	printf("Trying Discord socket at %s\n", discordSocketFullPath);
	discordSock = connectToSocket(discordSocketFullPath);
	printf("Connected to Discord socket successfully\n");

	// Connect to KVM socket
	printf("Trying KVM socket at %s\n", kvmSocketPath);
	kvmSock = connectToSocket(kvmSocketPath);

	// Read specific message from KVM and send confirmation
	const char kvmExpectedMsg[] = {0x69, 0x15, 0xBB};
	const char kvmResponseMsg[] = {0x6E, 0x1D, 0xB4};

	char buffer[3];
	ssize_t bytesRead = read(kvmSock, buffer, sizeof(buffer));
	printf("Read %ld bytes from KVM (%x %x %x)\n", bytesRead, buffer[0], buffer[1], buffer[2]);
	assert(bytesRead == sizeof(kvmExpectedMsg));
	assert(memcmp(buffer, kvmExpectedMsg, sizeof(kvmExpectedMsg)) == 0);

	ssize_t bytesSent = write(kvmSock, kvmResponseMsg, sizeof(kvmResponseMsg));
	assert(bytesSent == sizeof(kvmResponseMsg));
	printf("Connected to KVM socket successfully\n");

	// Transfer data between KVM and Discord sockets
	if (fork() == 0)
	{
		// Child process: transfer data from KVM to Discord
		transferData(kvmSock, discordSock);
	}
	else
	{
		// Parent process: transfer data from Discord to KVM
		transferData(discordSock, kvmSock);
	}

	close(kvmSock);
	close(discordSock);
	return 0;
}

#endif // __linux__
