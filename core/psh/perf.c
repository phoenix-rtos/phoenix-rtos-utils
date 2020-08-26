/*
 * Phoenix-RTOS
 *
 * perf - track kernel performance events
 *
 * Copyright 2017, 2018, 2020 Phoenix Systems
 * Author: Pawel Pisarczyk, Jan Sikorski, Maciej Purski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/threads.h>


int psh_perf(int argc, char **argv)
{
	time_t timeout = 0, elapsed = 0, sleeptime = 200 * 1000;
	threadinfo_t *info, *rinfo;
	const size_t bufsz = 4 << 20;
	int bcount, tcnt, n = 32;
	perf_event_t *buffer;
	char *timeout_s;

	if (argc > 1) {
		timeout_s = argv[1];

		if (!(timeout = strtoull(timeout_s, NULL, 10))) {
			printf("perf: timeout argument must be integer greater than 0\n");
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
			free(info);
			fprintf(stderr, "perf: out of memory\n");
			return -ENOMEM;
		}
		info = rinfo;
	}

	if (fwrite(&tcnt, sizeof(tcnt), 1, stdout) != 1) {
		fprintf(stderr, "perf: failed or partial write\n");
		free(info);
		return -1;
	}

	if (fwrite(info, sizeof(threadinfo_t), tcnt, stdout) != tcnt) {
		fprintf(stderr, "perf: failed or partial write\n");
		free(info);
		return -1;
	}

	free(info);

	if (argc == 1)
		return EOK;

	if ((buffer = malloc(bufsz)) == NULL) {
		fprintf(stderr, "perf: out of memory\n");
		return -ENOMEM;
	}

	if (perf_start(-1) < 0) {
		fprintf(stderr, "perf: could not start\n");
		free(buffer);
		return -1;
	}

	while (elapsed < timeout) {
		bcount = perf_read(buffer, bufsz);

		if (fwrite(buffer, 1, bcount, stdout) < bcount) {
			fprintf(stderr, "perf: failed or partial write\n");
			break;
		}

		fprintf(stderr, "perf: wrote %d/%d bytes\n", bcount, bufsz);

		usleep(sleeptime);
		elapsed += sleeptime;
	}

	perf_finish();
	free(buffer);

	return EOK;
}
