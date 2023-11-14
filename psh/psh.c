/*
 * Phoenix-RTOS
 *
 * Phoenix-RTOS SHell
 *
 * Copyright 2017, 2018, 2020-2023 Phoenix Systems
 * Author: Pawel Pisarczyk, Jan Sikorski, Lukasz Kosinski, Mateusz Niewiadomski, Gerard Swiderski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <sys/msg.h>
#include <sys/pwman.h>

#include <libgen.h>

#include "psh.h"


psh_common_t psh_common = { NULL };


const psh_appentry_t *psh_applist_first(void)
{
	return psh_common.pshapplist;
}


const psh_appentry_t *psh_applist_next(const psh_appentry_t *current)
{
	if (current == NULL) {
		return NULL;
	}
	return current->next;
}


void psh_registerapp(psh_appentry_t *newapp)
{
	psh_appentry_t *prevapp = NULL;

	/* find position */
	newapp->next = psh_common.pshapplist;
	while ((newapp->next != NULL) && (strcmp(newapp->next->name, newapp->name) < 0)) {
		prevapp = newapp->next;
		newapp->next = prevapp->next;
	}

	/* insert */
	if (prevapp == NULL) {
		psh_common.pshapplist = newapp;
	}
	else {
		prevapp->next = newapp;
	}

	return;
}


const psh_appentry_t *psh_findapp(char *appname)
{
	const psh_appentry_t *app;
	for (app = psh_common.pshapplist; app != NULL; app = app->next) {
		if (strcmp(appname, app->name) == 0) {
			break;
		}
	}
	return app;
}


static char *psh_stralloc(char *oldstr, const char *str)
{
	size_t len = strlen(str) + sizeof('\0');
	char *newstr = realloc(oldstr, len);
	if (newstr != NULL) {
		memcpy(newstr, str, len);
	}
	return newstr;
}


size_t psh_write(int fd, const void *buf, size_t count)
{
	ssize_t res;
	size_t len = 0;

	while (len != count) {
		res = write(fd, (const uint8_t *)buf + len, count - len);
		if (res <= 0) {
			if ((errno == EINTR) || (errno == EAGAIN)) {
				continue;
			}
			break;
		}
		else {
			len += (size_t)res;
		}
	}

	/* on error: (len != count) and errno is set */
	return len;
}


int psh_ttyopen(const char *ttydev)
{
	char *newPath;

	int fd = open(ttydev, O_RDWR);
	if (fd < 0) {
		return -errno;
	}

	if (isatty(fd) != 1) {
		close(fd);
		return -ENOTTY;
	}

	newPath = psh_stralloc(psh_common.ttydev, ttydev);
	if (newPath == NULL) {
		close(fd);
		return -ENOMEM;
	}

	psh_common.ttydev = newPath;

	dup2(fd, STDIN_FILENO);
	dup2(fd, STDOUT_FILENO);
	dup2(fd, STDERR_FILENO);

	close(fd);

	return EOK;
}


int main(int argc, char **argv)
{
	char *base;
	oid_t oid;
	const psh_appentry_t *app;
	int err = EOK;
	unsigned int ispshlogin;

	keepidle(1);

	/* Wait for root filesystem */
	while (lookup("/", NULL, &oid) < 0) {
		usleep(10000);
	}

	/* Check if its first shell */
	psh_common.tcpid = tcgetpgrp(STDIN_FILENO);
	base = basename(argv[0]);
	ispshlogin = (strcmp(base, "pshlogin") == 0);
	do {
		/* login prompt */
		if (ispshlogin != 0) {
			app = psh_findapp("auth");
			if (app != NULL) {
				while (app->run(0, NULL) != 0)
					;
			}
		}

		/* Run app */
		app = psh_findapp(base);
		if (app != NULL) {
			err = app->run(argc, argv);
			psh_common.exitStatus = err;
		}
		else {
			err = PSH_UNKNOWN_CMD;
			psh_common.exitStatus = err;
			fprintf(stderr, "psh: %s: unknown command\n", argv[0]);
			break;
		}

	} while ((psh_common.tcpid == -1) && (ispshlogin != 0));

	free(psh_common.ttydev);

	keepidle(0);

	return (err < 0) ? 1 : err;
}
