/*
 * Phoenix-RTOS
 *
 *  mkdir - creates directory
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
#include <string.h>

#include <sys/stat.h>

#include "../psh.h"


void psh_mkdirinfo(void)
{
	printf("creates directory");
}


int psh_mkdir(int argc, char **argv)
{
	int i;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <dir path>...\n", argv[0]);
		return -EINVAL;
	}

	for (i = 1; i < argc; i++) {
		if (mkdir(argv[i], 0) < 0) {
			fprintf(stderr, "mkdir: failed to create %s directory: %s\n", argv[i], strerror(errno));
		}
	}

	return EOK;
}


void __attribute__((constructor)) mkdir_registerapp(void)
{
	static psh_appentry_t app = {.name = "mkdir", .run = psh_mkdir, .info = psh_mkdirinfo};
	psh_registerapp(&app);
}
