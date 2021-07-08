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
#include <termios.h>
#include <unistd.h>

#include <sys/msg.h>
#include <sys/pwman.h>

#include <libgen.h>

#include "psh.h"


psh_common_t psh_common = {NULL};


const psh_appentry_t *psh_applist_first(void)
{
	return psh_common.pshapplist;
}


const psh_appentry_t *psh_applist_next(const psh_appentry_t *current)
{
	if (current == NULL)
		return NULL;
	return current->next;
}


void psh_registerapp(psh_appentry_t *newapp)
{	
	psh_appentry_t *prevapp = NULL;

	/* find position */
	newapp->next = psh_common.pshapplist;
	while (newapp->next != NULL && strcmp(newapp->next->name, newapp->name) < 0) {
		prevapp = newapp->next;
		newapp->next = prevapp->next;
	}

	/* insert */
	if (prevapp == NULL)
		psh_common.pshapplist = newapp;
	else
		prevapp->next = newapp;

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


int main(int argc, char **argv)
{
	char *base;
	oid_t oid;
	const psh_appentry_t *app;
	pid_t tcpid;

	keepidle(1);

	/* Wait for root filesystem */
	while (lookup("/", NULL, &oid) < 0)
		usleep(10000);

	/* Wait for console */
	while (write(1, "", 0) < 0)
		usleep(50000);

	/* Check if its first shell */
	tcpid = tcgetpgrp(STDIN_FILENO);
	base = basename(argv[0]);
	do {
		/* login prompt */
		if (strcmp(base, "pshlogin") == 0 && (app = psh_findapp("auth")) != NULL)
			while (app->run(0, NULL) != 0)
				;

		/* Run app */
		if ((app = psh_findapp(base)) != NULL) {
			app->run(argc, argv);
		}
		else {
			fprintf(stderr, "psh: %s: unknown command\n", argv[0]);
			break;
		}

	} while (tcpid == -1 && strcmp(base, "pshlogin") == 0);

	keepidle(0);

	return EOK;
}