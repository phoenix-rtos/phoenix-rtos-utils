/*
 * Phoenix-RTOS
 *
 * reboot - restart the machine
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
#include <unistd.h>

#include <sys/reboot.h>

#include "../psh.h"


void psh_rebootinfo(void)
{
	printf("restarts the machine");
}


int psh_reboot(int argc, char **argv)
{
	int c, magic = PHOENIX_REBOOT_MAGIC;

#ifdef TARGET_IMX6ULL
	while ((c = getopt(argc, argv, "s")) != -1) {
		switch (c) {
		case 's':
			magic = ~magic;
			break;

		default:
			return EOK;
		}
	}
#else
	(void)c;
#endif

	if (reboot(magic) < 0) {
		fprintf(stderr, "reboot: failed to restart the machine\n");
		return -EPERM;
	}

	return EOK;
}


void __attribute__((constructor)) reboot_registerapp(void)
{
	static psh_appentry_t app = {.name = "reboot", .run = psh_reboot, .info = psh_rebootinfo};
	psh_registerapp(&app);
}
