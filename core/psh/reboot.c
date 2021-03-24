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


void psh_rebootinfo(void)
{
	printf("  reboot  - restarts the machine\n");
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
		return -1;
	}

	return EOK;
}
