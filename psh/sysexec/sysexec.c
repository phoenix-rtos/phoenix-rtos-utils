/*
 * Phoenix-RTOS
 *
 * sysexec - launch program from syspage using given map
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <getopt.h>

#include <sys/threads.h>
#include <sys/types.h>

#include "../psh.h"


static void psh_sysexecinfo(void)
{
	printf("launch program from syspage using given map");
}


static void psh_sysexecUsage(void)
{
	fputs("Usage: sysexec [-m datamap] [-M codemap] progname [args]...\n", stderr);
}


static int psh_sysexec_argmatch(const char *cmd, size_t cmdlen, int argc, const char **argv)
{
	int ii;
	size_t len;
	const char *currc = cmd, *cmdend = cmd + cmdlen - 1, *nextc = NULL;

	for (ii = 0; ii < argc; ii++) {

		if ((nextc = strchr(currc, ' ')) == NULL || nextc > cmdend)
			nextc = cmdend;
		len = nextc - currc;
		if (*currc == '*') /* "accept all" wildcard */
			return 1;
		if (strlen(argv[ii]) != len || memcmp(argv[ii], currc, len) != 0)
			return -1;

		if (nextc == cmdend || ii == (argc - 1))
			break;

		currc = nextc + 1;
	}

	if (nextc == cmdend && ii == (argc - 1))
		return 1;
	else
		return -1;
}


static int psh_sysexec_checkcommand(int argc, const char **argv)
{
	int acc = -1, cmdlen;
	static const char whitelist[] = PSH_SYSEXECWL;
	const char *curr;
	char cmdbuff[80] = "";
	FILE *wlfile;

	/* check against /etc/whitelist file */
	if ((wlfile = fopen("/etc/whitelist", "r")) != NULL) {
		acc = 0;
		while (fgets(cmdbuff, sizeof(cmdbuff), wlfile) != NULL && acc != 1) {
			if (isprint(cmdbuff[sizeof(cmdbuff) - 2])) {
				while (isprint(fgetc(wlfile)))
					;
				memset(cmdbuff, 0, sizeof(cmdbuff));
				continue;
			}

			cmdlen = strcspn(cmdbuff, "\n")  + 1;
			if (psh_sysexec_argmatch(cmdbuff, cmdlen, argc, argv) > 0)
				acc = 1;

			memset(cmdbuff, 0, sizeof(cmdbuff));
		}
		fclose(wlfile);
	}

	/* check PSH_SYSEXECWL flag */
	curr = whitelist;
	while (*curr != '\0' && acc != 1) {
		acc = 0;
		cmdlen = strcspn(curr, ";") + 1;
		if (psh_sysexec_argmatch(curr, cmdlen, argc, argv) > 0)
			return 1;
		else
			curr = curr + cmdlen;
	}

	return (acc != 0);
}


static int psh_sysexec(int argc, char **argv)
{
	pid_t pid, err;
	int opt, status = 0;
	const char *progName;
	const char *codeMapName = NULL;
	const char *dataMapName = NULL;

	while ((opt = getopt(argc, argv, "hm:M:")) != -1) {
		switch (opt) {
			case 'm':
				dataMapName = optarg;
				break;

			case 'M':
				codeMapName = optarg;
				break;

			case 'h':
				psh_sysexecUsage();
				return EOK;

			default:
				psh_sysexecUsage();
				return -EINVAL;
		}
	}

	progName = argv[optind];
	if (progName == NULL) {
		fprintf(stderr, "psh: missing program name for sysexec\n");
		psh_sysexecUsage();
		return -EINVAL;
	}

	if (psh_sysexec_checkcommand(argc, (const char **)argv) != 1) {
		fprintf(stderr, "Unknown command!\n");
		return -EINVAL;
	}

	pid = spawnSyspage(codeMapName, dataMapName, progName, &argv[optind]);
	if (pid > 0) {
		do {
			err = waitpid(pid, &status, 0);
		} while (err < 0 && errno == EINTR);
		/* Take back terminal control */
		tcsetpgrp(STDIN_FILENO, getpgrp());
		return err >= 0 ? WEXITSTATUS(status) : errno;
	}

	switch (pid) {
		case -ENOMEM:
			fprintf(stderr, "psh: out of memory\n");
			break;

		case -ENOENT:
			fprintf(stderr, "psh: syspage program '%s' not found\n", progName);
			break;

		case -EINVAL:
			if (dataMapName != NULL || codeMapName != NULL) {
				fprintf(stderr, "psh: invalid map set:");
				if (dataMapName != NULL) {
					fprintf(stderr, " '%s'", dataMapName);
				}
				if (codeMapName != NULL) {
					if (dataMapName != NULL) {
						fputc(',', stderr);
					}
					fprintf(stderr, " '%s'", codeMapName);
				}
				fputc('\n', stderr);
			}
			else {
				fprintf(stderr, "psh: invalid program '%s'\n", progName);
			}
			break;

		default:
			fprintf(stderr, "psh: sysexec failed with code %d\n", pid);
	}

	return pid < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}


void __attribute__((constructor)) sysexec_registerapp(void)
{
	static psh_appentry_t app = { .name = "sysexec", .run = psh_sysexec, .info = psh_sysexecinfo };
	psh_registerapp(&app);
}
