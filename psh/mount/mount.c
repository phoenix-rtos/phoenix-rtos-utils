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
	(void)printf("mounts a filesystem");
}


int psh_mount(int argc, char **argv)
{
	int err;
	char *mount_data = NULL;
	char *endptr;
	unsigned long mode;

	if ((argc < 5) || (argc > 6)) {
		(void)fprintf(stderr, "usage: %s <source> <target> <fstype> <mode> [data]\n", argv[0]);
		return EXIT_FAILURE;
	}

	if (argc == 6) {
		mount_data = argv[5];
	}

	mode = strtoul(argv[4], &endptr, 0);
	if (*endptr != '\0') {
		(void)fprintf(stderr, "mount: invalid mode\n");
		return EXIT_FAILURE;
	}
	err = mount(argv[1], argv[2], argv[3], mode, mount_data);
	if (err < 0) {
		(void)fprintf(stderr, "mount: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}


void __attribute__((constructor)) mount_registerapp(void)
{
	static psh_appentry_t app = { .name = "mount", .run = psh_mount, .info = psh_mountinfo };
	psh_registerapp(&app);
}
