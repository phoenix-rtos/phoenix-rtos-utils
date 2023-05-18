/*
 * Phoenix-RTOS
 *
 * ps - prints threads and processes
 *
 * Copyright 2020, 2021 Phoenix Systems
 * Author: Pawel Pisarczyk, Jan Sikorski, Lukasz Kosinski, Maciej Purski, Mateusz Niewiadomski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/minmax.h>
#include <sys/threads.h>

#include "../psh.h"


static int psh_ps_cmpname(const void *t1, const void *t2)
{
	return strcmp(((threadinfo_t *)t1)->name, ((threadinfo_t *)t2)->name);
}


static int psh_ps_cmppid(const void *t1, const void *t2)
{
	return (int)((threadinfo_t *)t1)->pid - (int)((threadinfo_t *)t2)->pid;
}


static int psh_ps_cmpcpu(const void *t1, const void *t2)
{
	return ((threadinfo_t *)t2)->load - ((threadinfo_t *)t1)->load;
}


static void psh_psinfo(void)
{
	printf("prints processes and threads");
}


static void usage(const char *progname)
{
	printf("Usage: %s [options]\n", progname);
	printf("\nDisplaying:\n");
	printf("    -t    Show threads\n");
	printf("    -f    Show full commandline\n");
	printf("    -h    Show help instead\n");
	printf("\nSorting:\n");
	printf("    -c    Sort by current CPU usage\n");
	printf("    -n    Sort by name\n");
	printf("    -p    Sort by PID [default]\n");
}


static int psh_ps(int argc, char **argv)
{
	int (*cmp)(const void *, const void *) = psh_ps_cmppid;
	int c, tcnt, i, j, n = 32;
	unsigned int threads = 0, fullcmd = 0;
	threadinfo_t *info, *rinfo;
	unsigned int d, h, m, s;
	char buff[8];

	while ((c = getopt(argc, argv, "cfhnpt")) != -1) {
		switch (c) {
			case 'c':
				cmp = psh_ps_cmpcpu;
				break;

			case 'n':
				cmp = psh_ps_cmpname;
				break;

			case 'p':
				cmp = psh_ps_cmppid;
				break;

			case 't':
				threads = 1;
				break;

			case 'f':
				fullcmd = 1;
				break;

			case 'h':
				usage(argv[0]);
				return EOK;

			default:
				return EOK;
		}
	}

	if ((info = malloc(n * sizeof(threadinfo_t))) == NULL) {
		fprintf(stderr, "ps: out of memory\n");
		return -ENOMEM;
	}

	while ((tcnt = threadsinfo(n, info)) >= n) {
		n *= 2;
		if ((rinfo = realloc(info, n * sizeof(threadinfo_t))) == NULL) {
			fprintf(stderr, "ps: out of memory\n");
			free(info);
			return -ENOMEM;
		}
		info = rinfo;
	}

	if (!threads) {
		qsort(info, tcnt, sizeof(threadinfo_t), psh_ps_cmppid);
		for (i = 0; i < tcnt; i++) {
			info[i].tid = 1;

			for (j = i + 1; j < tcnt && info[j].pid == info[i].pid; j++) {
				info[i].tid++;
				info[i].load += info[j].load;
				info[i].cpuTime += info[j].cpuTime;
				info[i].priority = min(info[i].priority, info[j].priority);
				info[i].state = min(info[i].state, info[j].state);
				info[i].wait = max(info[i].wait, info[j].wait);
			}

			if (j > i + 1) {
				memcpy(info + i + 1, info + j, (tcnt - j) * sizeof(threadinfo_t));
				tcnt -= j - i - 1;
			}
		}
		printf("%8s %8s %2s %5s %5s %7s %11s %6s %3s %-16s\n", "PID", "PPID", "PR", "STATE", "%CPU", "WAIT", "TIME", "VMEM", "THR", "CMD");
	}
	else {
		printf("%8s %8s %2s %5s %5s %7s %11s %6s %-20s\n", "PID", "PPID", "PR", "STATE", "%CPU", "WAIT", "TIME", "VMEM", "CMD");
	}

	qsort(info, tcnt, sizeof(threadinfo_t), cmp);

	for (i = 0; i < tcnt; i++) {
		psh_prefix(10, info[i].wait, -6, 1, buff);
		printf("%8u %8u %2d %5s %3u.%u %6ss ", info[i].pid, info[i].ppid, info[i].priority, (info[i].state) ? "sleep" : "ready",
			info[i].load / 10, info[i].load % 10, buff);

		s = (info[i].cpuTime + 500000) / 1000000;
		d = s / 86400;
		s %= 86400;
		h = s / 3600;
		s %= 3600;
		m = s / 60;
		s %= 60;
		psh_prefix(2, info[i].vmem, 0, 1, buff);

		if (d > 0) {
			printf("%2u-", d);
		}
		else {
			printf("   ");
		}
		printf("%02u:%02u:%02u %6s ", h, m, s, buff);

		if (!threads) {
			printf("%3u %.*s\n", info[i].tid, (int)(fullcmd ? sizeof(info[i].name) : 16), info[i].name);
		}
		else {
			printf("%.*s\n", (int)(fullcmd ? sizeof(info[i].name) : 20), info[i].name);
		}
	}

	free(info);
	return EOK;
}


void __attribute__((constructor)) ps_registerapp(void)
{
	static psh_appentry_t app = { .name = "ps", .run = psh_ps, .info = psh_psinfo };
	psh_registerapp(&app);
}
