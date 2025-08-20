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
#include <stdbool.h>
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
#define PSH_PERF_BUFSZ_EXP             (18)

#define MS_BETWEEN(ts0, ts1) (((ts1).tv_sec - (ts0).tv_sec) * 1000 + ((ts1).tv_nsec - (ts0).tv_nsec) / (1000 * 1000))

#define LOG_TAG "perf: "

/* clang-format off */
#define log_info(fmt, ...) do { fprintf(stderr, LOG_TAG fmt "\n", ##__VA_ARGS__); } while (0)
#define log_warning(fmt, ...) do { log_info("warning: " fmt, ##__VA_ARGS__); } while (0)
#define log_error(fmt, ...) do { log_info("error: " fmt, ##__VA_ARGS__); } while (0)
/* clang-format on */


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
	bool started;
	bool rolling;
	bool warnReadTooSlow;
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
		   "  -b [bufsize exp] (default: %d -> (2 << 18) B)\n"
		   "  -s [sleeptime] (default: %d ms)]\n"
		   "  -d [time to freeze after perf start] (default: %d ms)\n"
		   "  -j [start | stop] - just start/stop perf and exit\n"
		   "  -p [prio]\n",
			PSH_PERF_DEFAULT_TIMEOUT_MS,
			PSH_PERF_BUFSZ_EXP,
			PSH_PERF_DEFAULT_SLEEPTIME_MS,
			PSH_PERF_DEFAULT_SLEEPTIME_MS);
}


static size_t read_channels(void)
{
	int bcount;
	size_t total = 0;

	for (size_t i = 0; i < perf_common.nchans; i++) {
		chan_t *chan = &perf_common.chans[i];

		bcount = perf_read(perf_common.mode, perf_common.buffer, perf_common.bufSize, i);
		if (bcount < 0) {
			log_error("perf_read failed: %d", bcount);
			return -EIO;
		}

		total += bcount;

		if (perf_common.bufSize == bcount && !perf_common.rolling) {
			perf_common.warnReadTooSlow = true;
		}

		int ret = fwrite(perf_common.buffer, 1, bcount, chan->dest);
		if (ret < bcount) {
			log_error("failed or partial write: %d/%d", ret, bcount);
			return -EIO;
		}

		log_info("wrote %d/%zu bytes to %s", bcount, perf_common.bufSize, chan->name);
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
		log_error("out of memory");
		return -ENOMEM;
	}

	tcnt = threadsinfo(n, info);
	while (tcnt >= n) {
		n *= 2;
		rinfo = realloc(info, n * sizeof(threadinfo_t));
		if (rinfo == NULL) {
			log_error("out of memory");
			free(info);
			return -ENOMEM;
		}
		info = rinfo;
		tcnt = threadsinfo(n, info);
	}

	if (fwrite(&tcnt, sizeof(tcnt), 1, chan->dest) != 1) {
		log_error("failed or partial write");
		free(info);
		return -EIO;
	}

	if (fwrite(info, sizeof(threadinfo_t), tcnt, chan->dest) != tcnt) {
		log_error("failed or partial write");
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
		perf_common.buffer = NULL;
	}
	if (perf_common.chans != NULL) {
		free(perf_common.chans);
		perf_common.chans = NULL;
	}
	if (perf_common.started) {
		if (perf_finish(perf_common.mode) < 0) {
			log_error("could not finish");
		}
		else {
			log_info("finished");
		}
	}
}


static int initChannels(const char *destDir)
{
	int res;
	char path[PATH_MAX];
	struct stat sb;

	res = stat(destDir, &sb);
	if (res == 0 && !S_ISDIR(sb.st_mode)) {
		log_error("%s is not a directory", destDir);
		return -ENOENT;
	}

	if (res < 0) {
		if (mkdir(destDir, 0777) < 0) {
			log_error("mkdir failed: %s", strerror(errno));
			return -EIO;
		}
	}

	switch (perf_common.mode) {
		case perf_mode_threads:
			chan_t *chan = &perf_common.chans[0];
			strncpy(chan->name, "channel_threads", sizeof(chan->name));
			break;
		case perf_mode_trace:
			for (size_t i = 0; i < perf_common.nchans / perf_trace_channel_count; i++) {
				for (size_t j = 0; j < perf_trace_channel_count; j++) {
					chan_t *chan = &perf_common.chans[j + perf_trace_channel_count * i];
					res = snprintf(chan->name, sizeof(chan->name), "%s%zu",
							j == perf_trace_channel_meta ? "channel_meta" : "channel_event", i);
					if (res < 0 || res >= sizeof(chan->name)) {
						log_error("snprintf failed");
						return -ENOMEM;
					}
				}
			}
			break;
		default:
			log_error("invalid mode: %d", perf_common.mode);
			return -EINVAL;
	}

	for (size_t i = 0; i < perf_common.nchans; i++) {
		chan_t *chan = &perf_common.chans[i];

		res = snprintf(path, sizeof(path), "%s/%s", destDir, chan->name);
		if (res < 0 || res >= sizeof(path)) {
			log_error("snprintf failed");
			return -ENOMEM;
		}

		FILE *destFile = fopen(path, "wb");
		if (destFile == NULL) {
			log_error("failed to open '%s': %s", path, strerror(errno));
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
	int opt;

	perf_common.total = 0;
	perf_common.started = false;
	perf_common.warnReadTooSlow = false;

	/* clang-format off */
	enum { perf_none, perf_just_start, perf_just_stop } restrictTo = perf_none;
	/* clang-format on */

	for (;;) {
		opt = getopt(argc, argv, "o:m:t:b:s:d:p:j:h");
		if (opt == -1) {
			break;
		}
		switch (opt) {
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
					log_error("invalid mode: %s", modeStr);
					return -EINVAL;
				}
				break;
			case 't':
				timeoutMs = strtoul(optarg, &end, 10);
				if (*end != '\0' || timeoutMs == 0) {
					log_error("timeout argument must be integer greater than 0");
					return -EINVAL;
				}
				break;
			case 'b':
				size_t exp = strtoul(optarg, &end, 10);
				if (*end != '\0' || exp == 0) {
					log_error("bufsize argument must be integer greater than 0");
					return -EINVAL;
				}
				perf_common.bufSize = 2 << exp;
				break;
			case 's':
				sleeptimeMs = strtoul(optarg, &end, 10);
				if (*end != '\0' || sleeptimeMs == 0) {
					log_error("sleeptime argument must be integer greater than 0");
					return -EINVAL;
				}
				break;
			case 'd':
				stopDelayUs = strtoul(optarg, &end, 10);
				if (*end != '\0' || stopDelayUs == 0) {
					log_error("freeze time argument must be integer greater than 0");
					return -EINVAL;
				}
				stopDelayUs *= 1000;
				perf_common.rolling = true;
				break;
			case 'p':
				int prio = strtoul(optarg, &end, 10);
				if (*end != '\0') {
					log_error("bad prio: %s", optarg);
					return -EINVAL;
				}
				priority(prio);
				break;
			case 'j':
				if (strcmp(optarg, "start") == 0) {
					restrictTo = perf_just_start;
					perf_common.rolling = true;
				}
				else if (strcmp(optarg, "stop") == 0) {
					restrictTo = perf_just_stop;
				}
				else {
					log_error("bad arg: -n %s", optarg);
					return -EINVAL;
				}
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

	unsigned flags = 0;

	if (perf_common.rolling) {
		if (perf_common.mode == perf_mode_trace) {
			flags |= PERF_TRACE_FLAG_ROLLING;
			log_info("rolling mode");
		}
		else {
			log_error("rolling mode required but not supported in %s mode", modeStrs[perf_common.mode]);
			cleanup();
			return -ENOSYS;
		}
	}

	if (restrictTo == perf_just_stop) {
		res = perf_stop(perf_common.mode);
	}
	else {
		res = perf_start(perf_common.mode, flags, NULL, 0);
	}

	if (res <= 0) {
		log_error("perf syscall failed: %d", res);
		cleanup();
		return -EIO;
	}

	if (restrictTo == perf_just_start) {
		/*
		 * perf_common.started is kept as false intentionally so that
		 * perf_finish() is not called in cleanup()
		 */
		cleanup();
		log_info("perf started");
		return EOK;
	}

	perf_common.started = true;
	perf_common.nchans = res;
	log_info("%d channel(s) to read", perf_common.nchans);

	perf_common.chans = malloc(sizeof(chan_t) * perf_common.nchans);
	if (perf_common.chans == NULL) {
		log_error("malloc failed");
		cleanup();
		return -ENOMEM;
	}

#if PERF_RTT_ENABLED
	log_info("using RTT buffer", modeStr);
#else
	res = initChannels(destDir);
	if (res < 0) {
		cleanup();
		return res;
	}

	log_info("using memory buffer");
	perf_common.buffer = malloc(perf_common.bufSize);
	if (perf_common.buffer == NULL) {
		log_error("out of memory");
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

	if (stopDelayUs != 0 || restrictTo == perf_just_stop) {
		if (restrictTo != perf_just_stop) {
			usleep(stopDelayUs);

			if (perf_stop(perf_common.mode) < 0) {
				log_error("could not stop: %s", strerror(errno));
				cleanup();
				return -EIO;
			}
		}

		log_info("perf stopped");

		while (read_channels() > 0) { }

		log_info("nothing left to write, exiting");
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
				/* exit the trace */

				if (perf_stop(perf_common.mode) < 0) {
					log_error("could not stop: %s", strerror(errno));
					cleanup();
					return -EIO;
				}

				/*
				 * gather all that's left - otherwise there could be garbage at the end
				 * of trace in multicore scenario
				 */
				while (perf_common.buffer != NULL && read_channels() > 0) { }
				break;
			}

			if (perf_common.buffer == NULL) {
				log_info("elapsed %lld/%lld ms", ms, timeoutMs);
			}

			usleep(sleeptimeMs * 1000);
		}
	}

	if (perf_common.warnReadTooSlow) {
		log_warning("read buffer was fully utilized during perf_read - read rate may be too slow");
	}

	/* perf_finish done in cleanup() */
	cleanup();

	return EOK;
}


void __attribute__((constructor)) perf_registerapp(void)
{
	static psh_appentry_t app = { .name = "perf", .run = psh_perf, .info = psh_perfinfo };
	psh_registerapp(&app);
}
