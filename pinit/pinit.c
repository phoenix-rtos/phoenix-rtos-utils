#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sysexits.h>
#include <unistd.h>
#include <errno.h>
#include <sys/msg.h>
#include <sys/debug.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <termios.h>

#define PORT_DESCRIPTOR 3

#define LOG_ERROR(msg, ...) do { \
	char buf[128]; \
	sprintf(buf, __FILE__ ":%d - " msg "\n", __LINE__, ##__VA_ARGS__ ); \
	debug(buf); \
} while (0)


static int runServer(const char *name, unsigned port, ...)
{
	pid_t pid;
	int status, pfd;
	const char *argv[32];
	va_list list;
	int i = 1;

	argv[0] = name;
	va_start(list, port);
	while (i < 32 && (argv[i] = va_arg(list, char *)) != NULL)
		i++;
	va_end(list);
	argv[31] = NULL;

	if ((pfd = portCreate(port)) == -1)
		return -1;

	if (dup2(pfd, PORT_DESCRIPTOR) != PORT_DESCRIPTOR) {
		close(pfd);
		return -1;
	}

	if (pfd != PORT_DESCRIPTOR) {
		close(pfd);
	}


	pid = fork();

	if (pid == -1) {
		/* error */
		LOG_ERROR("Failed to fork %s", name);
		status = -1;
	}
	else if (pid == 0) {
		/* child */
		if (name[0] != '/')
			ProcExec(AT_FDSYSPAGE, name, argv, NULL);
		else
			ProcExec(-1, name, argv, NULL);

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
		fd = open("/dev/ttyS0", i ? O_WRONLY : O_RDONLY);
		if (fd != i)
			debug("open std err 1\n");
		if (fd < 0)
			debug("open std err 2\n");

		if (fd == 0) {
			if (ioctl(0, TIOCSCTTY) < 0)
				debug("TIOCSCTTY error\n");
		}
	}
	return 0;
}


extern int SetRoot(int port, id_t id, mode_t mode);

static int pinitSetRoot(int fd, id_t id, mode_t mode)
{
	if (SetRoot(fd, id, mode)) {
		LOG_ERROR("Failed to set root", fd, id, mode);
		return -1;
	}
	return 0;
}


int main(int argc, char **argv)
{
	int id;

	id = runServer("dummyfs", 1, NULL);
	if (id < 0)
		return -1;

	if (pinitSetRoot(PORT_DESCRIPTOR, id, S_IFDIR | 0755)) {
		LOG_ERROR("Failed to set root");
		exit(EX_OSERR);
	}

	close(PORT_DESCRIPTOR);

	if (mkdir("/dev", 0555)) {
		LOG_ERROR("Failed to create /dev - %d", errno);
		exit(EX_CANTCREAT);
	}

	if (mknod("/dev/null", S_IFCHR, 0) == -1)
		LOG_ERROR("Failed to create null device node: %s", strerror(errno));

	if (mknod("/dev/zero", S_IFCHR, 1) == -1)
		LOG_ERROR("Failed to create zero device node: %s", strerror(errno));

	if (runServer("pc-ata", 3, NULL)) {
		LOG_ERROR("Failed to run pc-ata");
		_exit(EX_OSERR);
	}

	mkdir("/mnt", 0777);
	int dev = open("/dev", O_RDWR);

	if (dev < 0) {
		LOG_ERROR("open /dev");
	}

	int sp = open("/syspage", O_RDWR);

	if (sp < 0) {
		LOG_ERROR("open /syspage");
	}

	int mfd = fsMount("ext2", -1, "/dev/hda1", 33);

	if (mfd < 0) {
		LOG_ERROR("moutn ext");
	}

	fsBind(-1, "/", mfd, NULL);

	chdir("/");
	mkdir("/dev", 0777);
	mkdir("/syspage", 0777);
	if (fsBind(-1, "/dev", dev, NULL) < 0)
		LOG_ERROR("bind dev");
	if (fsBind(-1, "/syspage", sp, NULL) < 0)
		LOG_ERROR("bind syspage");
	close(dev);
	close(sp);
	close(mfd);



	if (runServer("/sbin/pc-tty", 2, NULL)) {
		LOG_ERROR("Failed to run pc-tty");
		_exit(EX_OSERR);
	}

	if (runServer("/sbin/ptysrv", 13, NULL)) {
		LOG_ERROR("Failed to run ptysrv");
		_exit(EX_OSERR);
	}

	if (openStd()) {
		LOG_ERROR("openStd");
		return -1;
	}

	if (runServer("/sbin/lwip", 12, "virtio", NULL))
		_exit(EX_OSERR);

	close(PORT_DESCRIPTOR);

	close(0);
	close(1);
	close(2);



	chdir("/");

	const char *init_args[2];
	init_args[0] = "linuxrc";
	init_args[1] = NULL;

	setsid();

	if (openStd()) {
		LOG_ERROR("openStd");
		return -1;
	}

	ProcExec(AT_FDCWD, "/linuxrc", init_args, NULL);
	_exit(EX_OSERR);

	return 0;
}
