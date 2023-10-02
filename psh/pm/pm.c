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
#include <sys/mman.h>

#include "../psh.h"


static void psh_pm_help(const char *progname)
{
	printf("usage: %s [options]\n", progname);
	printf("Options:\n");
	printf("  -p       Don't monitor parent process\n");
	printf("  -r       Reboot if any process running so far dies\n");
	printf("  -t secs  Set processes monitor interval (default: 300)\n");
	printf("  -m bytes Reboot if amount of taken total memory is larger than [bytes]\n");
	printf("  -k bytes Reboot if amount of taken kernel memory is larger than [bytes]\n");
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


static void psh_pm_reboot(const char *reason)
{
	printf("pm: rebooting! reason: %s\n", reason);

	int err = reboot(PHOENIX_REBOOT_MAGIC);
	if (err < 0) {
		printf("pm: failed to restart the machine\n");
	}
}


static unsigned int psh_pm_getTotal(void)
{
	meminfo_t info;

	memset(&info, 0, sizeof(info));

	info.page.mapsz = -1;
	info.entry.mapsz = -1;
	info.entry.kmapsz = -1;
	info.maps.mapsz = -1;

	meminfo(&info);
	return info.page.alloc;
}


int psh_pm(int argc, char *argv[])
{
	int restart = 0, ignore_ppid = 0, interval = 300;
	int c;
	pid_t ppid;
	int itsize, ctsize;
	int itcnt;
	threadinfo_t *itinfo = NULL;
	threadinfo_t *ctinfo = NULL;
	int rebootNoMem = 0;

	uint32_t maxTotal = UINT32_MAX;
	uint32_t maxKernel = UINT32_MAX;
	char *str;

	while ((c = getopt(argc, argv, "prt:m:k:h")) != -1) {
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

			case 'm':
				maxTotal = strtoul(optarg, &str, 10);
				if ((str == optarg) || (*str != '\0')) {
					fprintf(stderr, "pm: invalid -m value\n");
					return EXIT_FAILURE;
				}
				printf("pm: monitoring total mem usage - limit %u bytes\n", maxTotal);
				rebootNoMem = 1; /* monitoring memory - reboot also when we encounter ENOMEM error internally */
				break;

			case 'k':
				maxKernel = strtoul(optarg, &str, 10);
				if ((str == optarg) || (*str != '\0')) {
					fprintf(stderr, "pm: invalid -k value\n");
					return EXIT_FAILURE;
				}
				printf("pm: monitoring kernel mem usage - limit %u bytes\n", maxKernel);
				rebootNoMem = 1; /* monitoring memory - reboot also when we encounter ENOMEM error internally */
				break;

			case 'h':
			default:
				psh_pm_help(argv[0]);
				return (c == 'h') ? EXIT_SUCCESS : EXIT_FAILURE;
		}
	}

	ppid = getppid();

	itcnt = psh_pm_getThreads(&itinfo, &itsize);
	if (itcnt < 0)
		return EXIT_FAILURE;

	/* print warning if more than 90% of monitored threshold is reached */
	const uint32_t warnKernel = maxKernel - maxKernel / 10;
	const uint32_t warnTotal = maxTotal - maxTotal / 10;

	for (;;) {
		sleep(interval);

		int ctcnt = psh_pm_getThreads(&ctinfo, &ctsize);
		if (ctcnt < 0) {
			if ((rebootNoMem != 0) && (ctcnt == -ENOMEM)) {
				psh_pm_reboot("ENOMEM while getting thread info");
			}
			continue;
		}

		uint32_t currKernel = 0;

		int i = 0;
		int j = 0;

		while (i < itcnt) {
			pid_t ipid = itinfo[i].pid;
			pid_t cpid = ctinfo[j].pid;

			if (ctinfo[i].pid == 0) { /* kernel idle process - shows kernel RAM usage */
				currKernel = ctinfo[i].vmem;
			}

			if ((!ignore_ppid || ipid != ppid) && (ipid < cpid)) {
				fprintf(stderr, "pm: process %d died\n", ipid);
				if (restart) {
					psh_pm_reboot("monitored process died");
				}
			}

			if (ipid <= cpid) {
				int k;
				for (k = i + 1; k < itcnt && itinfo[k].pid == itinfo[i].pid; k++)
					;
				i = k;
			}

			if (ipid >= cpid) {
				int k;
				for (k = j + 1; k < ctcnt && ctinfo[k].pid == ctinfo[j].pid; k++)
					;
				j = k;
			}
		}

		if (rebootNoMem != 0) {
			uint32_t currTotal = psh_pm_getTotal();
			if ((currTotal > warnTotal) || (currKernel > warnKernel)) {
				printf("pm: mem: total: %u / %u   kernel: %u / %u\n", currTotal, maxTotal, currKernel, maxKernel);

				if (currTotal > maxTotal) {
					psh_pm_reboot("total mem exceeded limit");
				}

				if (currKernel > maxKernel) {
					psh_pm_reboot("kernel mem exceeded limit");
				}
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
