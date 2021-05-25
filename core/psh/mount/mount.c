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

#include "../psh.h"


void psh_mountinfo(void)
{
	printf("mounts a filesystem");
}


int psh_mount(int argc, char **argv)
{
	int err;
	char *mount_data = NULL;

	if (argc < 5 || argc > 6) {
		fprintf(stderr, "usage: %s <source> <target> <fstype> <mode> [data]\n", argv[0]);
		return -EINVAL;
	}

	if (argc == 6)
		mount_data = argv[5];

	if ((err = mount(argv[1], argv[2], argv[3], atoi(argv[4]), mount_data)) < 0) {
		fprintf(stderr, "mount: %s\n", strerror(err));
		return 1;
	}

	return EOK;
}


void __attribute__((constructor)) mount_registerapp(void)
{
	static psh_appentry_t app = {.name = "mount", .run = psh_mount, .info = psh_mountinfo};
	psh_registerapp(&app);
}
