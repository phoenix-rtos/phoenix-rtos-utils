/*
 * Phoenix-RTOS
 *
 * perf - track kernel performance events
 *
 * Copyright 2017, 2018, 2020, 2021, 2025 Phoenix Systems
 * Author: Pawel Pisarczyk, Jan Sikorski, Maciej Purski, Mateusz Niewiadomski, Adam Greloch
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/threads.h>
#include <sys/perf.h>

#include "../psh.h"


#include <board_config.h>


#ifndef PSH_PERF_BUFSZ
#define PSH_PERF_BUFSZ (4 << 20)
#endif

#ifndef PSH_PERF_USE_RTT
#define PSH_PERF_USE_RTT 0
#endif


void psh_perfinfo(void)
{
	printf("track kernel performance events");
	return;
}


static void perfHelp(void)
{
	printf("Usage: perf [threads | trace] [timeout]\n");
}


int psh_perf(int argc, char **argv)
{
	time_t timeout = 0, elapsed = 0, sleeptime = 1000 * 1000;
	threadinfo_t *info, *rinfo;
	int bcount, tcnt, n = 32;
	char *buffer = NULL, *end, *mode_str;
	perf_mode_t mode;

	if (argc == 1) {
		perfHelp();
		return 0;
	}

	mode_str = argv[1];

	if (strcmp(mode_str, "threads") == 0) {
		mode = perf_mode_threads;
	}
	else if (strcmp(mode_str, "trace") == 0) {
		mode = perf_mode_trace;
	}
	else {
		fprintf(stderr, "perf: invalid mode: %s\n", mode_str);
		return -EINVAL;
	}

	if (argc > 2) {
		timeout = strtoul(argv[2], &end, 10);

		if (*end != '\0' || !timeout) {
			fprintf(stderr, "perf: timeout argument must be integer greater than 0\n");
			return -EINVAL;
		}
		timeout *= 1000 * 1000;
	}

	if (mode != perf_mode_trace) {
		/* in trace mode threads info is contained in the perf datastream already */
		info = malloc(n * sizeof(threadinfo_t));
		if (info == NULL) {
			fprintf(stderr, "perf: out of memory\n");
			return -ENOMEM;
		}

		tcnt = threadsinfo(n, info);
		while (tcnt >= n) {
			n *= 2;
			rinfo = realloc(info, n * sizeof(threadinfo_t));
			if (rinfo == NULL) {
				fprintf(stderr, "perf: out of memory\n");
				free(info);
				return -ENOMEM;
			}
			info = rinfo;
			tcnt = threadsinfo(n, info);
		}

		if (fwrite(&tcnt, sizeof(tcnt), 1, stdout) != 1) {
			fprintf(stderr, "perf: failed or partial write\n");
			free(info);
			return -EIO;
		}

		if (fwrite(info, sizeof(threadinfo_t), tcnt, stdout) != tcnt) {
			fprintf(stderr, "perf: failed or partial write\n");
			free(info);
			return -EIO;
		}

		free(info);
	}

	if (argc == 1)
		return EOK;

#if PSH_PERF_USE_RTT
	fprintf(stderr, "perf %s: using RTT buffer\n", mode_str);
#else
	fprintf(stderr, "perf %s: using memory buffer\n", mode_str);
	buffer = malloc(PSH_PERF_BUFSZ);
	if (buffer == NULL) {
		fprintf(stderr, "perf: out of memory\n");
		return -ENOMEM;
	}
#endif

	if (perf_start(mode, -1) < 0) {
		fprintf(stderr, "perf %s: could not start: %s\n", mode_str, strerror(errno));
		if (buffer != NULL) {
			free(buffer);
		}
		return -1;
	}

	while (elapsed < timeout) {
		if (buffer != NULL) {
			bcount = perf_read(mode, buffer, PSH_PERF_BUFSZ);

			if (bcount < 0) {
				fprintf(stderr, "perf %s: perf_read failed %d\n", mode_str, bcount);
			}

			if (fwrite(buffer, 1, bcount, stdout) < bcount) {
				fprintf(stderr, "perf %s: failed or partial write\n", mode_str);
				break;
			}

			fprintf(stderr, "perf %s: wrote %d/%d bytes\n", mode_str, bcount, PSH_PERF_BUFSZ);
		}
		else {
			fprintf(stderr, "perf %s: elapsed %lld/%lld s\n", mode_str, elapsed / (1000 * 1000), timeout / (1000 * 1000));
		}
		usleep(sleeptime);
		elapsed += sleeptime;
	}

	perf_finish(mode);

	if (buffer != NULL) {
		free(buffer);
	}

	fprintf(stderr, "perf %s: finished\n", mode_str);

	return EOK;
}


void __attribute__((constructor)) perf_registerapp(void)
{
	static psh_appentry_t app = { .name = "perf", .run = psh_perf, .info = psh_perfinfo };
	psh_registerapp(&app);
}
