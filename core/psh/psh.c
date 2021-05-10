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

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/msg.h>
#include <sys/pwman.h>

#include <posix/utils.h>

#include "psh.h"


psh_common_t psh_common = {NULL};


void psh_registerapp(psh_appentry_t *newapp)
{	
	psh_appentry_t *appiterator;

	newapp->next = NULL;
	if (psh_common.pshapplist == NULL) {
		psh_common.pshapplist = newapp;
		return;
	}
	for (appiterator = psh_common.pshapplist; appiterator->next != NULL; appiterator = appiterator->next)
		;
	appiterator->next = newapp;
	return;
}


const psh_appentry_t *psh_findapp(char *appname) {
	const psh_appentry_t *app;
	for (app = psh_common.pshapplist; app != NULL; app = app->next) {
		if (strcmp(appname, app->name) == 0)
			break;
	}
	return app;
}


const psh_appentry_t *psh_getapp(int n) {
	const psh_appentry_t *entry;
	for (entry = psh_common.pshapplist; (entry != NULL) && (n > 0); entry = entry->next, n--)
	;
	return entry;
}


int main(int argc, char **argv)
{
	char *base, *dir;
	oid_t oid;
	const psh_appentry_t *app;

	keepidle(1);

	/* Wait for root filesystem */
	while (lookup("/", NULL, &oid) < 0)
		usleep(10000);

	/* Wait for console */
	while (write(1, "", 0) < 0)
		usleep(50000);

	splitname(argv[0], &base, &dir);

	/* Run app */
	if ((app = psh_findapp(base)) != NULL)
		app->run(argc, argv);
	else
		fprintf(stderr, "psh: %s: unknown command\n", argv[0]);

	keepidle(0);

	return EOK;
}
