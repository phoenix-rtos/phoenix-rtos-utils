/*
 * Phoenix-RTOS
 *
 * mount - mounts a filesystem
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

#include <sys/mount.h>


void psh_mountinfo(void)
{
	printf("  mount   - mounts a filesystem\n");
}


int psh_mount(int argc, char **argv)
{
	int err;

	if (argc != 6) {
		fprintf(stderr, "usage: %s <source> <target> <fstype> <mode> <data>\n", argv[0]);
		return -EINVAL;
	}

	if ((err = mount(argv[1], argv[2], argv[3], atoi(argv[4]), argv[5])) < 0)
		fprintf(stderr, "mount: %s\n", strerror(err));

	return EOK;
}
