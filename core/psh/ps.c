/*
 * Phoenix-RTOS
 *
 * ps - prints threads and processes
 *
 * Copyright 2020 Phoenix Systems
 * Author: Pawel Pisarczyk, Jan Sikorski, Lukasz Kosinski, Maciej Purski
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

#include "psh.h"


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


int psh_ps(int argc, char **argv)
{
	int (*cmp)(const void *, const void *) = psh_ps_cmpcpu;
	int c, tcnt, i, j, n = 32, threads = 0;
	threadinfo_t *info, *rinfo;
	unsigned int h, m;
	char buff[8];

	while ((c = getopt(argc, argv, "cnpt")) != -1) {
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
		printf("%5s %5s %2s %5s %5s %5s %7s %6s %3s %-28s\n", "PID", "PPID", "PR", "STATE", "%CPU", "WAIT", "TIME", "VMEM", "THR", "CMD");
	}
	else {
		printf("%5s %5s %2s %5s %5s %5s %7s %6s %-32s\n", "PID", "PPID", "PR", "STATE", "%CPU", "WAIT", "TIME", "VMEM", "CMD");
	}

	qsort(info, tcnt, sizeof(threadinfo_t), cmp);

	for (i = 0; i < tcnt; i++) {
		info[i].cpuTime /= 10000;
		h = info[i].cpuTime / 3600;
		m = (info[i].cpuTime - h * 3600) / 60;
		psh_prefix(10, info[i].wait, -6, 1, buff);
		printf("%5u %5u %2d %5s %3u.%u %4ss %4u:%02u ", info[i].pid, info[i].ppid, info[i].priority, (info[i].state) ? "sleep" : "ready",
			info[i].load / 10, info[i].load % 10, buff, h, m);

		psh_prefix(2, info[i].vmem, 0, 1, buff);
		printf("%6s ", buff);

		if (!threads)
			printf("%3u %-28s\n", info[i].tid, info[i].name);
		else
			printf("%-32s\n", info[i].name);
	}

	free(info);
	return EOK;
}
