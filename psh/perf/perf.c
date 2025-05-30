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
#include <limits.h>

#include <sys/threads.h>
#include <sys/perf.h>
#include <sys/stat.h>

#include "../psh.h"

#include <board_config.h>


#ifndef PSH_PERF_BUFSZ
#define PSH_PERF_BUFSZ (512 << 10)
#endif

#ifndef PERF_RTT_ENABLED
#define PERF_RTT_ENABLED 0
#endif

#define PSH_PERF_DEFAULT_TIMEOUT_MS    (3 * 1000)
#define PSH_PERF_DEFAULT_SLEEPTIME_MS  (100)
#define PSH_PERF_DEFAULT_STOP_DELAY_MS (0)
#define PSH_PERF_DEFAULT_NCPUS         (1)
#define PSH_PERF_BUFSZ_EXP             (18)

#define MS_BETWEEN(ts0, ts1) (((ts1).tv_sec - (ts0).tv_sec) * 1000 + ((ts1).tv_nsec - (ts0).tv_nsec) / (1000 * 1000))


typedef struct {
	char name[16];
	FILE *dest;
} chan_t;


static struct {
	size_t total;
	char *buffer;
	perf_mode_t mode;
	int nchans;
	size_t bufSize;
	chan_t *chans;
} perf_common = {
	.mode = perf_mode_trace,
	.bufSize = 2 << PSH_PERF_BUFSZ_EXP,
};


static char *modeStrs[] = {
	[perf_mode_trace] = "trace",
	[perf_mode_threads] = "threads",
};


static void psh_perfinfo(void)
{
	printf("track kernel performance events");
	return;
}


static void perfHelp(void)
{
	printf("Usage: perf -m [threads | trace]"
#if !PERF_RTT_ENABLED
		   " -o [stream output dir]"
#endif
		   " [options]\n"
		   "Options:\n"
		   "  -t [timeout] (default: %d ms)\n"
		   "  -b [bufsize exp (default: %d -> (2 << 18) B)]\n"
		   "  -s [sleeptime (default: %d ms)]\n"
		   "  -f [time to freeze after perf start (default: %d ms)]\n"
		   "  -c [ncpus (default: %d)]\n"
		   "  -p [prio]\n",
			PSH_PERF_DEFAULT_TIMEOUT_MS,
			PSH_PERF_BUFSZ_EXP,
			PSH_PERF_DEFAULT_SLEEPTIME_MS,
			PSH_PERF_DEFAULT_SLEEPTIME_MS,
			PSH_PERF_DEFAULT_NCPUS);
}


static size_t read_channels(void)
{
	char *modeStr = modeStrs[perf_common.mode];
	int bcount;
	size_t total = 0;

	for (size_t i = 0; i < perf_common.nchans; i++) {
		chan_t *chan = &perf_common.chans[i];
		bcount = perf_read(perf_common.mode, perf_common.buffer, perf_common.bufSize, i);
		total += bcount;

		if (bcount < 0) {
			fprintf(stderr, "perf %s: perf_read failed %d\n", modeStr, bcount);
			return -EIO;
		}

		int ret = fwrite(perf_common.buffer, 1, bcount, chan->dest);
		if (ret < bcount) {
			fprintf(stderr, "perf %s: failed or partial write: %d/%d\n", modeStr, ret, bcount);
			break;
		}

		fprintf(stderr, "perf %s: wrote %d/%zu bytes to %s\n", modeStr, bcount, perf_common.bufSize, chan->name);
	}

	return total;
}


static int emitThreadInfo(void)
{
	chan_t *chan = &perf_common.chans[0];
	threadinfo_t *info, *rinfo;
	int tcnt, n = 32;
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

	if (fwrite(&tcnt, sizeof(tcnt), 1, chan->dest) != 1) {
		fprintf(stderr, "perf: failed or partial write\n");
		free(info);
		return -EIO;
	}

	if (fwrite(info, sizeof(threadinfo_t), tcnt, chan->dest) != tcnt) {
		fprintf(stderr, "perf: failed or partial write\n");
		free(info);
		return -EIO;
	}

	free(info);

	return EOK;
}


static void cleanup(void)
{
	if (perf_common.buffer != NULL) {
		free(perf_common.buffer);
	}
	if (perf_common.chans != NULL) {
		free(perf_common.chans);
	}
}


static int initChannels(const char *destDir, int ncpus)
{
	int res;
	char path[PATH_MAX];
	struct stat sb;

	res = stat(destDir, &sb);
	if (res == 0 && !S_ISDIR(sb.st_mode)) {
		fprintf(stderr, "perf: %s is not a directory\n", destDir);
		cleanup();
		return -ENOENT;
	}

	if (res < 0) {
		if (mkdir(destDir, 0777) < 0) {
			fprintf(stderr, "perf: mkdir failed: %s\n", strerror(errno));
			cleanup();
			return -EIO;
		}
	}

	switch (perf_common.mode) {
		case perf_mode_threads:
			chan_t *chan = &perf_common.chans[0];
			strncpy(chan->name, "channel_threads", sizeof(chan->name));
			break;
		case perf_mode_trace:
			for (size_t i = 0; i < ncpus; i++) {
				for (size_t j = 0; j < perf_trace_channel_count; j++) {
					chan_t *chan = &perf_common.chans[j + perf_trace_channel_count * i];
					res = snprintf(chan->name, sizeof(chan->name), "%s%zu",
							j == perf_trace_channel_meta ? "channel_meta" : "channel_event", i);
					if (res < 0 || res >= sizeof(chan->name)) {
						fprintf(stderr, "perf: snprintf failed\n");
						cleanup();
						return -ENOMEM;
					}
				}
			}
			break;
		default:
			fprintf(stderr, "perf: invalid mode: %d\n", perf_common.mode);
			cleanup();
			return -EINVAL;
	}

	for (size_t i = 0; i < perf_common.nchans; i++) {
		chan_t *chan = &perf_common.chans[i];

		res = snprintf(path, sizeof(path), "%s/%s", destDir, chan->name);
		if (res < 0 || res >= sizeof(path)) {
			fprintf(stderr, "perf: snprintf failed\n");
			cleanup();
			return -ENOMEM;
		}

		FILE *destFile = fopen(path, "wb");
		if (destFile == NULL) {
			fprintf(stderr, "perf: failed to open '%s': %s\n", path, strerror(errno));
			cleanup();
			return -EIO;
		}

		chan->dest = destFile;
	}

	return EOK;
}


static int psh_perf(int argc, char **argv)
{
	char *end, *modeStr = NULL, *destDir = NULL;
	int res;

	time_t timeoutMs = PSH_PERF_DEFAULT_TIMEOUT_MS;
	time_t sleeptimeMs = PSH_PERF_DEFAULT_SLEEPTIME_MS;
	time_t stopDelayUs = PSH_PERF_DEFAULT_STOP_DELAY_MS;
	int ncpus = PSH_PERF_DEFAULT_NCPUS;
	int opt;

	for (;;) {
		opt = getopt(argc, argv, "o:m:t:b:s:d:c:h");
		if (opt == -1) {
			break;
		}
		switch (opt) {
			case 'c':
				ncpus = strtoul(optarg, &end, 10);
				if (*end != '\0' || ncpus == 0) {
					fprintf(stderr, "perf: ncpus argument must be integer greater than 0\n");
					return -EINVAL;
				}
				break;

			case 'o':
				destDir = optarg;
				break;
			case 'm':
				modeStr = optarg;
				perf_common.mode = -1;
				for (size_t i = 0; i < sizeof(modeStrs) / sizeof(modeStrs[0]); i++) {
					if (strcmp(modeStr, modeStrs[i]) == 0) {
						perf_common.mode = i;
						break;
					}
				}
				if (perf_common.mode == -1) {
					fprintf(stderr, "perf: invalid mode: %s\n", modeStr);
					return -EINVAL;
				}
				break;
			case 't':
				timeoutMs = strtoul(optarg, &end, 10);
				if (*end != '\0' || timeoutMs == 0) {
					fprintf(stderr, "perf: timeout argument must be integer greater than 0\n");
					return -EINVAL;
				}
				break;
			case 'b':
				size_t exp = strtoul(optarg, &end, 10);
				if (*end != '\0' || exp == 0) {
					fprintf(stderr, "perf: bufsize argument must be integer greater than 0\n");
					return -EINVAL;
				}
				perf_common.bufSize = 2 << exp;
				break;
			case 's':
				sleeptimeMs = strtoul(optarg, &end, 10);
				if (*end != '\0' || sleeptimeMs == 0) {
					fprintf(stderr, "perf: sleeptime argument must be integer greater than 0\n");
					return -EINVAL;
				}
				break;
			case 'd':
				stopDelayUs = strtoul(optarg, &end, 10);
				if (*end != '\0' || stopDelayUs == 0) {
					fprintf(stderr, "perf: freeze time argument must be integer greater than 0\n");
					return -EINVAL;
				}
				stopDelayUs *= 1000;
				break;
			case 'p':
				int prio = strtoul(optarg, &end, 10);
				if (*end != '\0') {
					fprintf(stderr, "perf: bad prio: %s\n", optarg);
					return -EINVAL;
				}
				priority(prio);
				break;
			default:
			case 'h':
				perfHelp();
				return EOK;
		}
	}

	if (modeStr == NULL || destDir == NULL) {
		perfHelp();
		return -EINVAL;
	}

	perf_common.nchans = perf_common.mode == perf_mode_trace ? 2 * ncpus : 1;
	perf_common.chans = malloc(sizeof(chan_t) * perf_common.nchans);
	if (perf_common.chans == NULL) {
		fprintf(stderr, "perf: malloc failed\n");
		return -ENOMEM;
	}

#if PERF_RTT_ENABLED
	fprintf(stderr, "perf %s: using RTT buffer\n", modeStr);
#else
	res = initChannels(destDir, ncpus);
	if (res < 0) {
		return res;
	}

	fprintf(stderr, "perf %s: using memory buffer\n", modeStr);
	perf_common.buffer = malloc(perf_common.bufSize);
	if (perf_common.buffer == NULL) {
		fprintf(stderr, "perf: out of memory\n");
		cleanup();
		return -ENOMEM;
	}
#endif

	if (perf_common.mode == perf_mode_threads) {
		/* in trace mode threads info is contained in the perf datastream already */
		res = emitThreadInfo();
		if (res < 0) {
			cleanup();
			return res;
		}
	}

	unsigned flags = 0;

	if (stopDelayUs != 0) {
		flags |= PERF_TRACE_FLAG_ROLLING;
	}

	if (perf_start(perf_common.mode, flags, NULL, 0) < 0) {
		fprintf(stderr, "perf %s: could not start: %s\n", modeStr, strerror(errno));
		cleanup();
		return -EIO;
	}

	if (stopDelayUs != 0) {
		usleep(stopDelayUs);

		if (perf_stop(perf_common.mode) < 0) {
			fprintf(stderr, "perf %s: could not stop: %s\n", modeStr, strerror(errno));
			cleanup();
			return -EIO;
		}

		fprintf(stderr, "perf %s: stopped trace\n", modeStr);

		while (read_channels() != 0) { }

		fprintf(stderr, "perf %s: nothing left to write, exiting\n", modeStr);
	}
	else {
		struct timespec ts[2];
		clock_gettime(CLOCK_MONOTONIC_RAW, &ts[0]);

		for (;;) {
			if (perf_common.buffer != NULL) {
				read_channels();
			}

			clock_gettime(CLOCK_MONOTONIC_RAW, &ts[1]);
			time_t ms = MS_BETWEEN(ts[0], ts[1]);
			if (ms >= timeoutMs) {
				break;
			}

			if (perf_common.buffer == NULL) {
				fprintf(stderr, "perf %s: elapsed %lld/%lld ms\n", modeStr, ms, timeoutMs);
			}

			usleep(sleeptimeMs * 1000);
		}
	}

	perf_finish(perf_common.mode);
	cleanup();
	fprintf(stderr, "perf %s: finished\n", modeStr);

	return EOK;
}


void __attribute__((constructor)) perf_registerapp(void)
{
	static psh_appentry_t app = { .name = "perf", .run = psh_perf, .info = psh_perfinfo };
	psh_registerapp(&app);
}
