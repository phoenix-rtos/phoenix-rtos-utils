/*
 * Phoenix-RTOS
 *
 * Phoenix-RTOS SHell
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

#include "../psh.h"


void psh_helpinfo(void)
{
	printf("prints this help message");
}


int psh_help(int argc, char **argv)
{
	const psh_appentry_t *app;
	int ii = 0;

	printf("Available commands:\n");
	for (app = psh_getapp(ii); app != NULL; app = psh_getapp(++ii) ) {
		if (app->info != NULL) {
			printf("%*s - ", 16, app->name);
			app->info();
			printf("\n");
		}
	}
	return EOK;
}


void __attribute__((constructor)) help_registerapp(void)
{
	static psh_appentry_t app = {.name = "help", .run = psh_help, .info = psh_helpinfo};
	psh_registerapp(&app);
}
