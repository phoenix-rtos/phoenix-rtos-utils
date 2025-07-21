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
#define PSH_PERF_BUFSZ (512 << 10)
#endif

#ifndef PSH_PERF_USE_RTT
#define PSH_PERF_USE_RTT 0
#endif


void psh_perfinfo(void)
{
	printf("track kernel performance events");
	return;
}


#define PSH_PERF_DEFAULT_TIMEOUT_S 3


static void perfHelp(void)
{
	printf("Usage: perf -m [threads | trace] -e [event stream output path] -o [meta stream output path] [options]\n"
		   "Options:\n"
		   "  -t [timeout] (default: %d s)\n"
		   "  -b [bufsize exp]\n"
		   "  -s [sleeptime [ms]]\n"
		   "  -d [time to freeze buf after perf start [ms]]\n"
		   "  -p [prio]\n",
			PSH_PERF_DEFAULT_TIMEOUT_S);
}


int psh_perf(int argc, char **argv)
{
	time_t timeout = PSH_PERF_DEFAULT_TIMEOUT_S, elapsed = 0, sleeptime = 100 * 1000, stopDelay = 0;
	threadinfo_t *info, *rinfo;
	int bcount, tcnt, n = 32;
	char *buffer = NULL, *end, *mode_str = NULL;
	perf_mode_t mode = perf_mode_trace;
	int opt;

	size_t bufsize = PSH_PERF_BUFSZ;

	if (argc == 1) {
		perfHelp();
		return 0;
	}

	int prio = 4;

	char *metaDestPath = NULL;
	char *eventDestPath = NULL;

	for (;;) {
		opt = getopt(argc, argv, "o:e:m:t:b:s:d:");
		if (opt == -1) {
			break;
		}
		switch (opt) {
			case 'e':
				eventDestPath = optarg;
				break;
			case 'o':
				metaDestPath = optarg;
				break;
			case 'm':
				mode_str = optarg;

				if (strcmp(mode_str, "threads") == 0) {
					mode = perf_mode_threads;
				}
				else if (strcmp(mode_str, "trace") == 0) {
					mode = perf_mode_trace;
				}
				else {
					fprintf(stderr, "perf: invalid mode: %s\n", mode_str);
					return EXIT_FAILURE;
				}
				break;
			case 't':
				timeout = strtoul(optarg, &end, 10);
				if (*end != '\0' || !timeout) {
					fprintf(stderr, "perf: timeout argument must be integer greater than 0\n");
					return -EINVAL;
				}
				timeout *= 1000 * 1000;
				break;
			case 'b':
				bufsize = strtoul(optarg, &end, 10);
				if (*end != '\0' || !bufsize) {
					fprintf(stderr, "perf: bufsize argument must be integer greater than 0\n");
					return -EINVAL;
				}
				bufsize = 2 << bufsize;
				break;
			case 's':
				sleeptime = strtoul(optarg, &end, 10);
				if (*end != '\0' || !sleeptime) {
					fprintf(stderr, "perf: sleeptime argument must be integer greater than 0\n");
					return -EINVAL;
				}
				sleeptime *= 1000;
				break;
			case 'd':
				stopDelay = strtoul(optarg, &end, 10);
				if (*end != '\0' || !stopDelay) {
					fprintf(stderr, "perf: stopDelay argument must be integer greater than 0\n");
					return -EINVAL;
				}
				stopDelay *= 1000;
				break;
			case 'p':
				prio = strtoul(optarg, &end, 10);
				/* TODO: error check */
				break;
			case 'h':
				perfHelp();
				return EXIT_FAILURE;
		}
	}

	if (mode_str == NULL) {
		perfHelp();
		return EXIT_FAILURE;
	}

	if (metaDestPath == NULL || eventDestPath == NULL) {
		perfHelp();
		return EXIT_FAILURE;
	}

	FILE *metaDestFile = fopen(metaDestPath, "wb");
	if (metaDestFile == NULL) {
		fprintf(stderr, "perf: failed to open '%s': %s\n", metaDestPath, strerror(errno));
		return EXIT_FAILURE;
	}

	FILE *eventDestFile = fopen(eventDestPath, "wb");
	if (eventDestFile == NULL) {
		fprintf(stderr, "perf: failed to open '%s': %s\n", eventDestPath, strerror(errno));
		return EXIT_FAILURE;
	}

	priority(prio);

	if (mode != perf_mode_trace) {
		/* in trace mode threads info is contained in the perf datastream already */
		info = malloc(n * sizeof(threadinfo_t));
		if (info == NULL) {
			fprintf(stderr, "perf: out of memory\n");
			return EXIT_FAILURE;
		}

		tcnt = threadsinfo(n, info);
		while (tcnt >= n) {
			n *= 2;
			rinfo = realloc(info, n * sizeof(threadinfo_t));
			if (rinfo == NULL) {
				fprintf(stderr, "perf: out of memory\n");
				free(info);
				return EXIT_FAILURE;
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
	buffer = malloc(bufsize);
	if (buffer == NULL) {
		fprintf(stderr, "perf: out of memory\n");
		return EXIT_FAILURE;
	}
#endif

	unsigned flags = 0;

	if (stopDelay != 0) {
		flags |= PERF_TRACE_FLAG_ROLLING;
	}

	if (perf_start(mode, flags, (void *)-1) < 0) {
		fprintf(stderr, "perf %s: could not start: %s\n", mode_str, strerror(errno));
		if (buffer != NULL) {
			free(buffer);
		}
		return EXIT_FAILURE;
	}

	if (stopDelay != 0) {
		usleep(stopDelay);

		if (perf_stop(mode) < 0) {
			fprintf(stderr, "perf %s: could not stop: %s\n", mode_str, strerror(errno));
			if (buffer != NULL) {
				free(buffer);
			}
			return EXIT_FAILURE;
		}

		fprintf(stderr, "perf %s: stopped trace\n", mode_str);
	}

#define CHAN_COUNT 2
	struct {
		const char name[8];
		FILE *dest;
		perf_trace_channel_t chan;
	} chans[CHAN_COUNT] = {
		{
			.name = "meta",
			.dest = metaDestFile,
			.chan = perf_trace_channel_meta,
		},
		{
			.name = "event",
			.dest = eventDestFile,
			.chan = perf_trace_channel_event,
		},
	};

	size_t total = 0;

	while (elapsed < timeout || stopDelay != 0) {
		if (buffer != NULL) {
			total = 0;
			for (size_t i = 0; i < CHAN_COUNT; i++) {
				bcount = perf_read(mode, buffer, bufsize, chans[i].chan);
				total += bcount;

				if (bcount < 0) {
					fprintf(stderr, "perf %s: perf_read failed %d\n", mode_str, bcount);
					return EXIT_FAILURE;
				}

				size_t ret = fwrite(buffer, 1, bcount, chans[i].dest);
				if (ret < bcount) {
					fprintf(stderr, "perf %s: failed or partial write: %ld/%d\n", mode_str, ret, bcount);
					break;
				}

				fprintf(stderr, "perf %s: wrote %d/%zu bytes to %s\n", mode_str, bcount, bufsize, chans[i].name);
			}

			if (total == 0 && stopDelay != 0) {
				fprintf(stderr, "perf %s: nothing left to write, exiting\n", mode_str);
				break;
			}
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
