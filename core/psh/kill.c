/*
 * Phoenix-RTOS
 *
 * kill - send signal_kill to process
 *
 * Copyright 2017, 2018, 2020 Phoenix Systems
 * Author: Pawel Pisarczyk, Jan Sikorski, Maciej Purski, Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/threads.h>


int psh_kill(int argc, char **argv)
{
	unsigned int pid;
	char *end;

	if (argc != 2) {
		printf("usage: %s <pid>\n", argv[0]);
		return -EINVAL;
	}

	pid = strtoul(argv[1], &end, 10);
	if ((*end != '\0') || (pid == 0 && argv[1][0] != '0')) {
		printf("kill: could not parse process id: %s\n", argv[1]);
		return -EINVAL;
	}

	return signalPost(pid, -1, signal_kill);
}
