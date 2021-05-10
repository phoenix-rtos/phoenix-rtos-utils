/*
 * Phoenix-RTOS
 *
 * kill - send signal_kill to process
 *
 * Copyright 2017, 2018, 2020, 2021 Phoenix Systems
 * Author: Pawel Pisarczyk, Jan Sikorski, Maciej Purski, Lukasz Kosinski, Mateusz Niewiadomski
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

#include "../psh.h"


void psh_killinfo(void)
{
	printf("terminates process");
}


int psh_kill(int argc, char **argv)
{
	unsigned int pid;
	char *end;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <pid>\n", argv[0]);
		return -EINVAL;
	}

	pid = strtoul(argv[1], &end, 10);
	if (*end != '\0') {
		fprintf(stderr, "kill: could not parse process id: %s\n", argv[1]);
		return -EINVAL;
	}

	return signalPost(pid, -1, signal_kill);
}


void __attribute__((constructor)) kill_registerapp(void)
{
	static psh_appentry_t app = {.name = "kill", .run = psh_kill, .info = psh_killinfo};
	psh_registerapp(&app);
}
