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

#include <sys/stat.h>


void psh_mkdirinfo(void){
	printf("  mkdir   - creates directory\n");
}


int psh_mkdir(int argc, char **argv)
{
	int i;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <dir path>...\n", argv[0]);
		return -EINVAL;
	}

	for (i = 1; i < argc; i++) {
		if (mkdir(argv[i], 0) < 0)
			fprintf(stderr, "mkdir: failed to create %s directory\n", argv[i]);
	}

	return EOK;
}
