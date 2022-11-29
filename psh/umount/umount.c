/*
 * Phoenix-RTOS
 *
 * umount - unmount a filesystem
 *
 * Copyright 2022 Phoenix Systems
 * Author: Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>

#include <sys/mount.h>

#include "../psh.h"


static void psh_umount_info(void)
{
	printf("unmount a filesystem");
}


static int psh_umount(int argc, char **argv)
{
	if (argc != 2) {
		printf("Usage: %s <target>\n", argv[0]);
		return EXIT_FAILURE;
	}

	return (umount(argv[1]) < 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}


void __attribute__((constructor)) umount_registerapp(void)
{
	static psh_appentry_t app = { .name = "umount", .run = psh_umount, .info = psh_umount_info };
	psh_registerapp(&app);
}
