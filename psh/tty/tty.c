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
#include <signal.h>
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
	int ret;
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

	if (ioctl(STDIN_FILENO, TIOCNOTTY, 0) < 0) {
		fprintf(stderr, "psh: failed to release the current tty device\n");
		return -errno;
	}

	ret = psh_ttyopen(argv[1]);
	if (ret < 0) {
		fprintf(stderr, "psh: unable to change tty device to %s\n", argv[1]);
		return ret;
	}

	if (ioctl(STDIN_FILENO, TIOCSCTTY, 0) < 0) {
		/* TODO: handle this as fatal error due to dup2() in psh_ttyopen() */
		fprintf(stderr, "psh: failed to acquire %s as the controlling terminal\n", argv[1]);
		return -errno;
	}

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
