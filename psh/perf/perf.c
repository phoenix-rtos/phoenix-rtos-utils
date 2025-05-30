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

#include <trace.h>

#include "../psh.h"

#include <board_config.h>


#ifndef PSH_PERF_BUFSZ
#define PSH_PERF_BUFSZ (512 << 10)
#endif

#ifndef PERF_RTT_ENABLED
#define PERF_RTT_ENABLED 0
#endif

#define PSH_PERF_DEFAULT_TIMEOUT_MS   (3 * 1000)
#define PSH_PERF_DEFAULT_SLEEPTIME_MS (100)
#define PSH_PERF_BUFSZ_EXP            (18)

#define LOG_TAG "perf: "

/* clang-format off */
#define log_info(fmt, ...) do { fprintf(stderr, LOG_TAG fmt "\n", ##__VA_ARGS__); } while (0)
#define log_warning(fmt, ...) do { log_info("warning: " fmt, ##__VA_ARGS__); } while (0)
#define log_error(fmt, ...) do { log_info("error: " fmt, ##__VA_ARGS__); } while (0)
/* clang-format on */


static char *modeStrs[] = {
	[perf_mode_trace] = "trace",
};

_Static_assert(sizeof(modeStrs) / sizeof(modeStrs[0]) == perf_mode_count, "modeStrs must handle all perf modes");


static void psh_perfinfo(void)
{
	printf("track kernel performance events");
	return;
}


static void perfHelp(void)
{
	printf("Usage: perf -m MODE"
#if !PERF_RTT_ENABLED
		   " -o [stream output dir]"
#endif
		   " [options]\n"
		   "Modes:\n"
		   "  trace - kernel tracing\n"
		   "Options:\n"
		   "  -t [timeout] (default: %d ms)\n"
		   "  -b [bufsize exp] (default: %d -> (2 << 18) B)\n"
		   "  -s [sleeptime] (default: %d ms)]\n"
		   "  -j [start | stop] - just start/stop perf and exit\n"
		   "  -p [prio]\n",
			PSH_PERF_DEFAULT_TIMEOUT_MS,
			PSH_PERF_BUFSZ_EXP,
			PSH_PERF_DEFAULT_SLEEPTIME_MS);
}


static int psh_perf(int argc, char **argv)
{
	char *end, *modeStr = NULL, *destDir = NULL;

	time_t timeoutMs = PSH_PERF_DEFAULT_TIMEOUT_MS;
	time_t sleeptimeMs = PSH_PERF_DEFAULT_SLEEPTIME_MS;

	perf_mode_t mode = perf_mode_trace;

	size_t bufSize = 2 << PSH_PERF_BUFSZ_EXP;

	/* clang-format off */
	enum { perf_none, perf_just_start, perf_just_stop } restrictTo = perf_none;
	/* clang-format on */

	int opt;
	for (;;) {
		opt = getopt(argc, argv, "o:m:t:b:s:p:j:h");
		if (opt == -1) {
			break;
		}
		switch (opt) {
			case 'o':
				destDir = optarg;
				break;
			case 'm':
				modeStr = optarg;
				mode = -1;
				for (size_t i = 0; i < sizeof(modeStrs) / sizeof(modeStrs[0]); i++) {
					if (strcmp(modeStr, modeStrs[i]) == 0) {
						mode = i;
						break;
					}
				}
				if (mode == -1) {
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
			case 'b': {
				size_t exp = strtoul(optarg, &end, 10);
				if (*end != '\0' || exp == 0) {
					log_error("bufsize argument must be integer greater than 0");
					return -EINVAL;
				}
				bufSize = 2 << exp;
			} break;
			case 's':
				sleeptimeMs = strtoul(optarg, &end, 10);
				if (*end != '\0' || sleeptimeMs == 0) {
					log_error("sleeptime argument must be integer greater than 0");
					return -EINVAL;
				}
				break;
			case 'p': {
				int prio = strtoul(optarg, &end, 10);
				if (*end != '\0') {
					log_error("bad prio: %s", optarg);
					return -EINVAL;
				}
				priority(prio);
			} break;
			case 'j':
				if (strcmp(optarg, "start") == 0) {
					restrictTo = perf_just_start;
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

	if (modeStr == NULL || (PERF_RTT_ENABLED == 0 && destDir == NULL)) {
		perfHelp();
		return -EINVAL;
	}

	switch (mode) {
		case perf_mode_trace: {
			trace_ctx_t ctx;

			int res = trace_init(&ctx, false);
			if (res < 0) {
				log_error("trace_init failed: %d", res);
				return -EIO;
			}

			if (restrictTo == perf_just_start) {
				res = trace_start(&ctx);
				if (res < 0) {
					log_error("trace_start failed: %d", res);
					return -EIO;
				}
			}
			else if (restrictTo == perf_just_stop) {
				res = trace_stopAndGather(&ctx, bufSize, destDir);
				if (res < 0) {
					log_error("trace_stopAndGather failed: %d", res);
					return -EIO;
				}
			}
			else {
				res = trace_record(&ctx, sleeptimeMs, timeoutMs, bufSize, destDir);
				if (res < 0) {
					log_error("trace_record failed: %d", res);
					return -EIO;
				}
			}
			break;
		}
		default:
			log_error("unsupported mode");
			break;
	}

	return EOK;
}


void __attribute__((constructor)) perf_registerapp(void)
{
	static psh_appentry_t app = { .name = "perf", .run = psh_perf, .info = psh_perfinfo };
	psh_registerapp(&app);
}
