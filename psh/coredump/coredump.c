/*
 * Phoenix-RTOS
 *
 * coredump - coredump configuration applet
 *
 * Copyright 2025 Phoenix Systems
 * Author: Jakub Klimek
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <coredump.h>
#include <stdio.h>
#include <stdlib.h>
#include <phoenix/types.h>

#include "../psh.h"

void psh_coredumpinfo(void)
{
	printf("configure coredump server settings");
	return;
}


int psh_coredump(int argc, char **argv)
{
	return coredump_configure(argv[0], argc - 1, &argv[1]);
}


void __attribute__((constructor)) coredump_registerapp(void)
{
	static psh_appentry_t app = { .name = "coredump", .run = psh_coredump, .info = psh_coredumpinfo };
	psh_registerapp(&app);
}
