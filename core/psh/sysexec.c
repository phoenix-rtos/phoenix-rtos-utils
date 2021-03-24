/*
 * Phoenix-RTOS
 *
 * sysexec - launch program from syspage using given map
 *
 * Copyright 2017, 2018, 2020, 2021 Phoenix Systems
 * Author: Pawel Pisarczyk, Jan Sikorski, Lukasz Kosinski, Mateusz Niewiadomski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/threads.h>
#include <sys/types.h>


void psh_sysexecinfo(void)
{
	printf("  sysexec - launch program from syspage using given map\n");
}


int psh_sysexec(int argc, char **argv)
{
	int pid;

	if (argc < 3) {
		fprintf(stderr, "usage: %s map progname [args]...\n", argv[0]);
		return -EINVAL;
	}

	pid = spawnSyspage(argv[1], argv[2], argv + 2);

	if (pid > 0) {
		waitpid(pid, NULL, 0);
		return EOK;
	}

	switch (pid) {
		case -ENOMEM:
			fprintf(stderr, "psh: out of memory\n");
			break;

		case -EINVAL:
			fprintf(stderr, "psh: no exec %s or no map %s defined\n",
				argv[2], argv[1]);
			break;

		default:
			fprintf(stderr, "psh: sysexec failed with code %d\n", pid);
	}

	return EOK;
}
