/*
 * Phoenix-RTOS
 *
 * / - run file specified in command (empty implementation)
 *
 * Copyright 2017, 2018, 2020, 2021 Phoenix Systems
 * Author: Pawel Pisarczyk, Jan Sikorski, Lukasz Kosinski, Mateusz Niewiadomski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

#include <sys/pwman.h>

#include "../psh.h"

#define SIGNAL_SHIFT 128


int psh_runfile(int argc, char **argv)
{
	pid_t pid, ret;
	int status = 0, retval = 0;

	pid = vfork();
	if (pid > 0) {
		do {
			ret = waitpid(pid, &status, 0);
		} while (ret < 0 && errno == EINTR);
	}
	else if (!pid) {
		/* Put process in its own process group */
		pid = getpid();
		if (setpgid(pid, pid) < 0) {
			fprintf(stderr, "psh: failed to put %s process in its own process group\n", argv[0]);
			_psh_exit(EXIT_FAILURE);
		}

		/* Take terminal control */
		tcsetpgrp(STDIN_FILENO, pid);

		/* Execute the file */
		execv(argv[0], argv);

		switch (errno) {
		case EIO:
			fprintf(stderr, "psh: failed to load %s executable\n", argv[0]);
			break;

		case ENOMEM:
			fprintf(stderr, "psh: out of memory\n");
			break;

		case EACCES:
		case ENOEXEC:
			fprintf(stderr, "psh: %s is not an executable\n", argv[0]);
			break;

		case EINVAL:
		case ENOENT:
			fprintf(stderr, "psh: %s not found\n", argv[0]);
			break;

		default:
			fprintf(stderr, "psh: exec failed with code %d\n", -errno);
		}

		_psh_exit(EXIT_FAILURE);
	}
	else {
		fprintf(stderr, "psh: vfork failed with code %d\n", pid);
	}

	/* Take back terminal control */
	tcsetpgrp(STDIN_FILENO, getpgid(getpid()));

	if (!((pid > 0) && (ret >= 0))) {
		retval = EXIT_FAILURE;
	}
	else if (WIFSIGNALED(status) != 0) {
		/* Checking for the signal termination */
		retval = SIGNAL_SHIFT + WTERMSIG(status);
	}
	else {
		/* Checking for the normal termination */
		retval = WEXITSTATUS(status);
	}

	return retval;
}


void __attribute__((constructor)) runfile_registerapp(void)
{
	static psh_appentry_t app = {.name = "/", .run = psh_runfile, .info = NULL};
	psh_registerapp(&app);
}
