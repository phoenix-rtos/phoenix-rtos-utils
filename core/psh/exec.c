/*
 * Phoenix-RTOS
 *
 * exec - replaces shell with the given command
 *
 * Copyright 2020, 2021 Phoenix Systems
 * Author: Maciej Purski, Lukasz Kosinski, Mateusz Niewiadomski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>


void psh_execinfo(void)
{
	printf("  exec    - replace shell with the given command\n");
}


int psh_exec(int argc, char **argv)
{
	int err;

	if (argc < 2) {
		fprintf(stderr, "usage: %s command [args]...\n", argv[0]);
		return -EINVAL;
	}

	switch (err = execve(argv[1], argv + 1, NULL)) {
	case EOK:
		break;

	case -ENOMEM:
		fprintf(stderr, "psh: out of memory\n");
		break;

	case -EINVAL:
		fprintf(stderr, "psh: invalid executable\n");
		break;

	default:
		fprintf(stderr, "psh: exec failed with code %d\n", err);
	}

	return err;
}
