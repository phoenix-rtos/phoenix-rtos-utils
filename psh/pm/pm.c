/*
 * Phoenix-RTOS
 *
 * pm - Process Monitor
 *
 * Copyright 2022 Phoenix Systems
 * Author: Ziemowit Leszczynski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/threads.h>
#include <sys/reboot.h>
#include <sys/minmax.h>

#include "../psh.h"


static void psh_pm_help(const char *progname)
{
	printf("usage: %s [options]\n", progname);
	printf("Options:\n");
	printf("  -p       Don't monitor parent process\n");
	printf("  -r       Reboot if any process running so far dies\n");
	printf("  -t secs  Set processes monitor interval\n");
	printf("  -h       Show help instead\n");
}


static void psh_pm_info(void)
{
	printf("process monitor");
}


static int psh_pm_cmppid(const void *t1, const void *t2)
{
	return (int)((threadinfo_t *)t1)->pid - (int)((threadinfo_t *)t2)->pid;
}


static int psh_pm_getThreads(threadinfo_t **pinfo, int *n)
{
	int tcnt, m = *n;
	threadinfo_t *info = *pinfo, *rinfo;

	if (info == NULL) {
		m = 32;
		info = malloc(m * sizeof(threadinfo_t));
		if (info == NULL) {
			fprintf(stderr, "pm: out of memory\n");
			return -ENOMEM;
		}
	}

	while ((tcnt = threadsinfo(m, info)) >= m) {
		m *= 2;
		if ((rinfo = realloc(info, m * sizeof(threadinfo_t))) == NULL) {
			fprintf(stderr, "pm: out of memory\n");
			*pinfo = info;
			*n = m / 2;
			return -ENOMEM;
		}
		info = rinfo;
	}

	qsort(info, tcnt, sizeof(threadinfo_t), psh_pm_cmppid);

	*pinfo = info;
	*n = m;

	return tcnt;
}


int psh_pm(int argc, char *argv[])
{
	int restart = 0, ignore_ppid = 0, interval = 300;
	int c, i, j, k, err;
	pid_t ipid, cpid, ppid;
	int itsize, ctsize;
	int itcnt, ctcnt;
	threadinfo_t *itinfo = NULL;
	threadinfo_t *ctinfo = NULL;

	while ((c = getopt(argc, argv, "prt:h")) != -1) {
		switch (c) {
			case 'p':
				ignore_ppid = 1;
				break;

			case 'r':
				restart = 1;
				break;

			case 't':
				interval = min(1, atoi(optarg));
				break;

			default:
				psh_pm_help(argv[0]);
				return EXIT_FAILURE;
		}
	}

	ppid = getppid();

	itcnt = psh_pm_getThreads(&itinfo, &itsize);
	if (itcnt < 0)
		return EXIT_FAILURE;

	for (;;) {
		sleep(interval);

		ctcnt = psh_pm_getThreads(&ctinfo, &ctsize);
		if (ctcnt < 0)
			continue;

		i = 0;
		j = 0;

		while (i < itcnt) {
			ipid = itinfo[i].pid;
			cpid = ctinfo[j].pid;

			if ((!ignore_ppid || ipid != ppid) && (ipid < cpid)) {
				fprintf(stderr, "pm: process %d died\n", ipid);
				if (restart) {
					err = reboot(PHOENIX_REBOOT_MAGIC);
					if (err < 0)
						fprintf(stderr, "pm: failed to restart the machine\n");
				}
			}

			if (ipid <= cpid) {
				for (k = i + 1; k < itcnt && itinfo[k].pid == itinfo[i].pid; k++)
					;
				i = k;
			}

			if (ipid >= cpid) {
				for (k = j + 1; k < ctcnt && ctinfo[k].pid == ctinfo[j].pid; k++)
					;
				j = k;
			}
		}
	}

	free(itinfo);
	free(ctinfo);

	return EXIT_SUCCESS;
}


void __attribute__((constructor)) pm_registerapp(void)
{
	static psh_appentry_t app = { .name = "pm", .run = psh_pm, .info = psh_pm_info };
	psh_registerapp(&app);
}
