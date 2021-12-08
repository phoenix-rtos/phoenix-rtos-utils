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

#include <stdint.h>
#include <sys/reboot.h>

#include "../psh.h"


static void psh_rebootinfo(void)
{
	printf("restarts the machine");
}


static void print_help(const char *progname)
{
	printf("Usage: %s [options] address\n", progname);
	printf("Options\n");
	printf("  -s:  reboot to secondary boot option\n");
	printf("  -g:  get bootreason (platform-specific)\n");
	printf("  -h:  show help\n");
}


static int psh_reboot(int argc, char **argv)
{
	int c, magic = PHOENIX_REBOOT_MAGIC;
	int op_get = 0;
	int op_secondary = 0;

	while ((c = getopt(argc, argv, "ghs")) != -1) {
		switch (c) {
			case 's':
				op_secondary = 1;
				break;

			case 'h':
				print_help(argv[0]);
				return 0;

			case 'g':
				op_get = 1;
				break;

			default:
				print_help(argv[0]);
				return 1;
		}
	}

	if (op_secondary)
		magic = ~magic;

	if (op_get) {
		uint32_t reason;
		if (reboot_reason(&reason) < 0)
			return 1;

		printf("0x%08x\n", reason);
	}
	else {
		if (reboot(magic) < 0) {
			fprintf(stderr, "reboot: failed to restart the machine\n");
			return 1;
		}
	}

	return 0;
}


void __attribute__((constructor)) reboot_registerapp(void)
{
	static psh_appentry_t app = {.name = "reboot", .run = psh_reboot, .info = psh_rebootinfo};
	psh_registerapp(&app);
}
