#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sysexits.h>
#include <unistd.h>
#include <errno.h>
#include <sys/msg.h>
#include <sys/debug.h>
#include <sys/stat.h>

#define PORT_DESCRIPTOR 3

#define LOG_ERROR(msg, ...) do { \
	char buf[128]; \
	sprintf(buf, __FILE__ ":%d - " msg "\n", __LINE__, ##__VA_ARGS__ ); \
	debug(buf); \
} while (0)


static int runServer(const char *name, unsigned port)
{
	pid_t pid;
	int status, pfd;
	const char *argv[2];

	argv[0] = name;
	argv[1] = NULL;

	if ((pfd = portCreate(port)) == -1)
		return -1;

	if (dup2(pfd, PORT_DESCRIPTOR) != PORT_DESCRIPTOR) {
		close(pfd);
		return -1;
	}
	close(pfd);
	pid = fork();

	if (pid == -1) {
		/* error */
		LOG_ERROR("Failed to fork %s", name);
		status = -1;
	}
	else if (pid == 0) {
		/* child */
		ProcExec(AT_FDSYSPAGE, name, argv, NULL);
		LOG_ERROR("Failed to exec %s", name);
		_exit(EX_OSERR);
	}
	else {
		/* parent */
		if (waitpid(pid, &status, 0) == -1) {
			LOG_ERROR("Failed to daemonize %s", name);
			status = -1;
		}
	}

	return status;
}


static int openStd(void)
{
	int i, fd;

	for (i = 0; i < 3; ++i) {
		fd = open("/dev/console", i ? O_WRONLY : O_RDONLY);

		if (fd < 0)
			return -1;

		if (dup2(fd, i) != i) {
			close(fd);
			return -1;
		}
		close(fd);
	}

	return 0;
}


static int runInit(const char *name)
{
	const char *argv[2];

	argv[0] = name;
	argv[1] = NULL;

	if (openStd())
		return -1;

	ProcExec(AT_FDSYSPAGE, name, argv, NULL);
	_exit(EX_OSERR);
}


extern int SetRoot(int port, id_t id, mode_t mode);

static int pinitSetRoot(int fd, int id, mode_t mode)
{
	LOG_ERROR("Seting root fd %d id %d mode 0x%x", fd, id, mode);
	if (SetRoot(fd, id, mode)) {
		LOG_ERROR("Failed to set root", fd, id, mode);
		return -1;
	}
	close(fd);
	return 0;
}


int main(int argc, char **argv)
{
	int id;
	debug("PINIT START\n");
	id = runServer("dummyfs", 1);
	if (id < 0)
		return -1;

	if (pinitSetRoot(PORT_DESCRIPTOR, id, S_IFDIR | 0755)) {
		LOG_ERROR("Failed to set root");
		exit(EX_OSERR);
	}

	if (mkdir("/dev", 0555)) {
		LOG_ERROR("Failed to create /dev - %d", errno);
		exit(EX_CANTCREAT);
	}

	if (runServer("pc-uart", 2) || runServer("pc-tty", 3))
		_exit(EX_OSERR);

	runInit("psh");
	return 0;
}