/*
 * Phoenix-RTOS
 *
 * tty - print or replace interactive shell tty device
 *
 * Copyright 2023 Phoenix Systems
 * Author: Gerard Swiderski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include "../psh.h"


static void psh_ttyInfo(void)
{
	printf("print or replace interactive shell tty device");
}


static int psh_ttyMain(int argc, char **argv)
{
	int ret, newfd;
	char *newpath;
	pid_t pgrp;

	if (psh_common.ttydev == NULL) {
		fprintf(stderr, "psh: cannot run standalone\n");
		return -EINVAL;
	}

	if (argc < 2) {
		printf("%s\n", psh_common.ttydev);
		return EOK;
	}

	if (argv[1][0] == '-' && argv[1][1] == 'h') {
		printf("Usage: tty [/dev/console]\n");
		return EOK;
	}

	pgrp = getpgrp();
	if (pgrp < 0) {
		fprintf(stderr, "psh: tty invalid process group\n");
		return -EINVAL;
	}

	if (getsid(0) != getpid()) {
		fprintf(stderr, "psh: only a session leader can change its tty device\n");
		return -EPERM;
	}

	printf("Changing psh tty device '%s' to '%s'\n", psh_common.ttydev, argv[1]);

	/*
	 * Validate the new device before giving up the current one, so that a bad
	 * argument cannot leave the shell with no controlling terminal.
	 */
	newfd = open(argv[1], O_RDWR);
	if (newfd < 0) {
		fprintf(stderr, "psh: unable to change tty device to %s\n", argv[1]);
		return -errno;
	}

	if (isatty(newfd) != 1) {
		close(newfd);
		fprintf(stderr, "psh: %s is not a terminal\n", argv[1]);
		return -ENOTTY;
	}

	/* Allocate up front: nothing past the release below may fail */
	newpath = strdup(argv[1]);
	if (newpath == NULL) {
		close(newfd);
		fprintf(stderr, "psh: out of memory\n");
		return -ENOMEM;
	}

	/* A session may own only one controlling terminal, so release before acquiring */
	if (ioctl(STDIN_FILENO, TIOCNOTTY, 0) < 0 && errno != ENOTTY) {
		ret = -errno;
		free(newpath);
		close(newfd);
		fprintf(stderr, "psh: failed to release the current tty device\n");
		return ret;
	}

	if (ioctl(newfd, TIOCSCTTY, 0) < 0) {
		ret = -errno;
		free(newpath);
		close(newfd);
		fprintf(stderr, "psh: failed to acquire %s as the controlling terminal\n", argv[1]);
		/* stdio still refers to the old terminal - take it back */
		if (ioctl(STDIN_FILENO, TIOCSCTTY, 0) < 0) {
			fprintf(stderr, "psh: lost the controlling terminal, another session took '%s'\n",
					psh_common.ttydev);
		}
		return ret;
	}

	/* Point of no return - from here stdio belongs to the new device */
	psh_ttyinstall(newfd, newpath);

	if (tcsetpgrp(STDIN_FILENO, pgrp) < 0) {
		fprintf(stderr, "psh: failed to set terminal control\n");
		return -errno;
	}

	return EOK;
}


void __attribute__((constructor)) tty_registerapp(void)
{
	static psh_appentry_t app = { .name = "tty", .run = psh_ttyMain, .info = psh_ttyInfo };
	psh_registerapp(&app);
}
