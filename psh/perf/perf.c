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
	time_t timeout = 0, elapsed = 0, sleeptime = 200 * 1000;
	threadinfo_t *info, *rinfo;
	const size_t bufsz = 4 << 20;
	int bcount, tcnt, n = 32;
	char *buffer, *end, *mode_str;
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

	if ((info = malloc(n * sizeof(threadinfo_t))) == NULL) {
		fprintf(stderr, "perf: out of memory\n");
		return -ENOMEM;
	}

	while ((tcnt = threadsinfo(n, info)) >= n) {
		n *= 2;
		if ((rinfo = realloc(info, n * sizeof(threadinfo_t))) == NULL) {
			fprintf(stderr, "perf: out of memory\n");
			free(info);
			return -ENOMEM;
		}
		info = rinfo;
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

	if (argc == 1)
		return EOK;

	if ((buffer = malloc(bufsz)) == NULL) {
		fprintf(stderr, "perf: out of memory\n");
		return -ENOMEM;
	}

	if (perf_start(mode, -1) < 0) {
		fprintf(stderr, "perf %s: could not start\n", mode_str);
		free(buffer);
		return -1;
	}

	while (elapsed < timeout) {
		bcount = perf_read(mode, buffer, bufsz);

		if (fwrite(buffer, 1, bcount, stdout) < bcount) {
			fprintf(stderr, "perf %s: failed or partial write\n", mode_str);
			break;
		}

		fprintf(stderr, "perf %s: wrote %d/%zd bytes\n", mode_str, bcount, bufsz);

		usleep(sleeptime);
		elapsed += sleeptime;
	}

	perf_finish(mode);
	free(buffer);

	fprintf(stderr, "perf %s: finished\n", mode_str);

	return EOK;
}


void __attribute__((constructor)) perf_registerapp(void)
{
	static psh_appentry_t app = { .name = "perf", .run = psh_perf, .info = psh_perfinfo };
	psh_registerapp(&app);
}
