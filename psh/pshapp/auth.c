/*
 * Phoenix-RTOS
 *
 * login - utility for user validation
 *
 * Copyright 2021 Phoenix Systems
 * Author: Mateusz Niewiadomski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <ctype.h>
#include <errno.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "../psh.h"


static int psh_authcredentget(char *buff, int ispasswd, int maxinlen)
{
	int inputlen = -1;
	char c;
	struct termios origattr, ttyattr;

	/* Setting terminal attributes */
	if (tcgetattr(STDIN_FILENO, &origattr) < 0) {
		fprintf(stderr, "psh: saving tty settings fail\n");
		return -ENOTTY;
	}
	ttyattr = origattr;
	if (ispasswd)
		ttyattr.c_lflag &= ~(ECHO);
	else
		ttyattr.c_lflag |= (ICANON | ECHO | ISIG);

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &ttyattr) < 0) {
		fprintf(stderr, "psh: setting tty attributes fail\n");
		return -ENOTTY;
	}
	/* user input */
	for (;;) {
		if (inputlen < 0) {
			if (ispasswd)
				fprintf(stdout, "Password: ");
			else
				fprintf(stdout, "Login: ");

			fflush(stdout);
			inputlen++;
		}

		if (read(STDIN_FILENO, &c, 1) != 1) {
			inputlen = -1;
			break;
		}
			
		if (c == '\n') {
			if (inputlen <= maxinlen && inputlen > 0) {
				buff[inputlen] = '\0';
				inputlen--;
				break;
			}
			else {
				inputlen = -1;
				if (ispasswd) {
					inputlen = 0;
					memset(buff, '\0', maxinlen);
					break;
				}
				else
					continue;
			}
		}
		else if (isprint(c)) {
			if (inputlen < maxinlen)
				buff[inputlen] = c;
			inputlen++;
			continue;
		}
	}
	
	if (ispasswd || inputlen < 0)
		fprintf(stdout, "\n");

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &origattr) < 0) {
		fprintf(stderr, "psh: setting tty attributes fail\n");
		return -ENOTTY;
	}
	
	if (inputlen >= 0)
		return inputlen;

	return -EINTR;
}


static int psh_auth(int argc, char **argv)
{
	const int maxlen = 32;
	const char *consolePath = _PATH_CONSOLE;
	char username[maxlen+1], passwd[maxlen+1], *shadow;
	struct passwd *userdata;
	int retries;
	int c;

	while ((c = getopt(argc, argv, "t:h")) != -1) {
		switch (c) {
			case 't':
				consolePath = optarg;
				break;
			case 'h':
			default:
				printf("usage: %s [options]\n", argv[0]);
				printf("  -t <terminal dev>:  path to terminal device, default %s\n", _PATH_CONSOLE);
				printf("  -h:                 shows this help message\n");
				return EOK;
		}
	}

	/* This is temporary, until all architectures support /dev/console */
	for (retries = 5; retries > 0; retries--) {
		if (psh_ttyopen(consolePath) == 0)
			break;
		else
			usleep(100000);
	}

	/* check tty */
	if (isatty(STDOUT_FILENO) == 0) {
		sleep(1);
		fprintf(stderr, "psh: unable to login, not a tty\n");
		return -ENOTTY;
	}

	/* Get login from user */
	if (psh_authcredentget(username, 0, maxlen) == -EINTR)
		return -EINTR;
	userdata = getpwnam(username);

	/* Get password from user */
	if (psh_authcredentget(passwd, 1, maxlen) == -EINTR)
		return -EINTR;

	/* validate against /etc/passwd */
	if (userdata != NULL) {
		shadow = crypt(passwd, userdata->pw_passwd);
		if (shadow != NULL && strcmp(userdata->pw_passwd, shadow) == 0) {
			memset(passwd, '\0', maxlen);
			return EOK;
		}
	}

	/* validate against defuser */
	shadow = crypt(passwd, PSH_DEFUSRPWDHASH);
	memset(passwd, '\0', maxlen);
	if (shadow != NULL && strcmp(username, "defuser") == 0 && strcmp(shadow, PSH_DEFUSRPWDHASH) == 0)
		return EOK;

	sleep(2);
	return -EACCES;
}


void __attribute__((constructor)) pshauth_registerapp(void)
{
	static psh_appentry_t app = { .name = "auth", .run = psh_auth, .info = NULL };
	psh_registerapp(&app);
}
